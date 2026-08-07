#include "Audio/AudioDomain.h"

#include "Audio/AudioClipResource.h"
#include "Container/VariableArray.h"
#include "Thread/Atomic.h"

#include <utility>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        struct BackendEvent
        {
            VoiceHandle Handle;
            uint64_t ShutdownEpoch = 0;
            AudioBackendEventKind Kind = AudioBackendEventKind::Drained;
        };

        struct ActiveVoiceRecord
        {
            uint32_t Generation = 1;
            AudioVoiceState State = AudioVoiceState::Destroyed;
            Core::Container::TSharedPtr<AudioClipResource> Clip;
            Core::Asset::AssetBlob PcmPayload;
            Core::Container::TUniquePtr<AudioBackendEventMailbox> Mailbox;
            bool bBackendVoiceCreated = false;
            bool bCountedActive = false;
            bool bRetired = false;
        };

        class AudioService final : public IAudioService, private IAudioBackendEventSink
        {
            friend class AudioServiceTestAccess;

        public:
            explicit AudioService(Core::Container::TUniquePtr<IAudioBackend> backend)
                : m_Backend(std::move(backend))
            {
            }

            ~AudioService() override
            {
                Shutdown();
            }

            AudioResult Initialize() override
            {
                if (m_bShutdown || m_bInitialized)
                {
                    return AudioResult::AlreadyInState;
                }
                if (!m_Backend)
                {
                    return AudioResult::BackendFailure;
                }

                ++m_ShutdownEpoch;
                if (m_ShutdownEpoch == 0)
                {
                    ++m_ShutdownEpoch;
                }
                const AudioResult result = m_Backend->Initialize(*this, m_ShutdownEpoch);
                if (result != AudioResult::Success)
                {
                    m_bAcceptingEvents.Store(false, std::memory_order_release);
                    m_Backend->Shutdown();
                    return AudioResult::BackendFailure;
                }
                m_bAcceptingEvents.Store(true, std::memory_order_release);
                m_bInitialized = true;
                return AudioResult::Success;
            }

            AudioResult CreateVoice(const Core::Container::TSharedPtr<AudioClipResource>& clip,
                                    VoiceHandle& outHandle) override
            {
                outHandle = {};
                if (!m_bInitialized || m_bShutdown || !clip || !clip->IsValid())
                {
                    return AudioResult::InvalidState;
                }

                const uint32_t index = AcquireSlot();
                ActiveVoiceRecord& record = m_Voices[index];
                record.Clip = clip;
                record.PcmPayload = clip->GetPcmBlob();
                record.State = AudioVoiceState::Created;
                const VoiceHandle handle{index, record.Generation};

                if (m_Backend->CreateVoice(handle, clip->GetFormat(), *record.Mailbox) != AudioResult::Success)
                {
                    ReleaseSlot(index);
                    return AudioResult::BackendFailure;
                }
                record.bBackendVoiceCreated = true;

                if (m_Backend->SubmitVoice(handle, record.PcmPayload.GetSpan()) != AudioResult::Success)
                {
                    if (m_Backend->DestroyVoice(handle) == AudioResult::Success)
                    {
                        ReleaseSlot(index);
                    }
                    else
                    {
                        QuarantineSlot(index);
                    }
                    return AudioResult::BackendFailure;
                }

                record.State = AudioVoiceState::Submitted;
                record.bCountedActive = true;
                ++m_ActiveVoiceCount;
                outHandle = handle;
                return AudioResult::Success;
            }

            AudioResult StartVoice(VoiceHandle handle) override
            {
                ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (record->State == AudioVoiceState::Playing)
                {
                    return AudioResult::AlreadyInState;
                }
                if (record->State != AudioVoiceState::Submitted)
                {
                    return AudioResult::InvalidState;
                }
                if (m_Backend->StartVoice(handle) != AudioResult::Success)
                {
                    if (m_Backend->DestroyVoice(handle) == AudioResult::Success)
                    {
                        ReleaseSlot(handle.Index);
                    }
                    else
                    {
                        QuarantineSlot(handle.Index);
                    }
                    return AudioResult::BackendFailure;
                }
                record->State = AudioVoiceState::Playing;
                return AudioResult::Success;
            }

            AudioResult StopVoice(VoiceHandle handle) override
            {
                ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (record->State == AudioVoiceState::Stopping || record->State == AudioVoiceState::Drained)
                {
                    return AudioResult::AlreadyInState;
                }
                if (record->State != AudioVoiceState::Playing)
                {
                    return AudioResult::InvalidState;
                }
                const AudioBackendStopResult stopResult = m_Backend->StopVoice(handle);
                if (stopResult.Result != AudioResult::Success)
                {
                    if (stopResult.bVoiceStopped)
                    {
                        QuarantineSlot(handle.Index);
                    }
                    return AudioResult::BackendFailure;
                }
                if (!stopResult.bVoiceStopped || !stopResult.bBuffersFlushed)
                {
                    QuarantineSlot(handle.Index);
                    return AudioResult::BackendFailure;
                }
                record->State = AudioVoiceState::Stopping;
                return AudioResult::Success;
            }

            AudioResult DestroyVoice(VoiceHandle handle) override
            {
                ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (record->State != AudioVoiceState::Drained)
                {
                    return AudioResult::InvalidState;
                }
                if (m_Backend->DestroyVoice(handle) != AudioResult::Success)
                {
                    return AudioResult::BackendFailure;
                }
                ReleaseSlot(handle.Index);
                return AudioResult::Success;
            }

            AudioResult GetVoiceState(VoiceHandle handle, AudioVoiceState& outState) const override
            {
                const ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                outState = record->State;
                return AudioResult::Success;
            }

            void Tick() override
            {
                for (ActiveVoiceRecord& voice : m_Voices)
                {
                    if (!voice.Mailbox)
                    {
                        continue;
                    }
                    BackendEvent event;
                    if (!voice.Mailbox->TryConsume(event.Handle, event.ShutdownEpoch, event.Kind))
                    {
                        continue;
                    }
                    if (event.ShutdownEpoch != m_ShutdownEpoch || m_bShutdown)
                    {
                        ++m_LateEventCount;
                        continue;
                    }
                    ActiveVoiceRecord* record = Resolve(event.Handle);
                    if (record == nullptr)
                    {
                        ++m_StaleEventCount;
                        continue;
                    }
                    if (record->State == AudioVoiceState::Quarantined)
                    {
                        ++m_StaleEventCount;
                        continue;
                    }
                    if (event.Kind == AudioBackendEventKind::VoiceError)
                    {
                        record->State = AudioVoiceState::Stopping;
                    }
                    if (event.Kind == AudioBackendEventKind::Drained &&
                        record->State == AudioVoiceState::Drained)
                    {
                        continue;
                    }
                    if (record->State == AudioVoiceState::Playing)
                    {
                        record->State = AudioVoiceState::Stopping;
                    }
                    if (record->State == AudioVoiceState::Stopping)
                    {
                        record->State = AudioVoiceState::Drained;
                    }
                    else
                    {
                        ++m_StaleEventCount;
                    }
                }
            }

            AudioResult Shutdown() override
            {
                if (m_bShutdown)
                {
                    return AudioResult::AlreadyInState;
                }
                m_bShutdown = true;
                if (!m_Backend)
                {
                    return AudioResult::BackendFailure;
                }

                m_bAcceptingEvents.Store(false, std::memory_order_release);

                bool bBackendFailure = false;
                if (m_bInitialized)
                {
                    for (uint32_t index = 0; index < m_Voices.size(); ++index)
                    {
                        ActiveVoiceRecord& record = m_Voices[index];
                        if (record.State == AudioVoiceState::Playing)
                        {
                            const VoiceHandle handle{index, record.Generation};
                            const AudioBackendStopResult stopResult = m_Backend->StopVoice(handle);
                            if (stopResult.Result != AudioResult::Success)
                            {
                                bBackendFailure = true;
                            }
                            record.State = AudioVoiceState::Stopping;
                        }
                        else if (record.State == AudioVoiceState::Submitted || record.State == AudioVoiceState::Created)
                        {
                            record.State = AudioVoiceState::Stopping;
                        }
                    }

                    m_Backend->QuiesceCallbacks();
                    DrainEventsForShutdown();

                    for (uint32_t index = 0; index < m_Voices.size(); ++index)
                    {
                        ActiveVoiceRecord& record = m_Voices[index];
                        if (record.State == AudioVoiceState::Destroyed)
                        {
                            continue;
                        }
                        const VoiceHandle handle{index, record.Generation};
                        if (record.bBackendVoiceCreated &&
                            m_Backend->DestroyVoice(handle) != AudioResult::Success)
                        {
                            bBackendFailure = true;
                            QuarantineSlot(index);
                            continue;
                        }
                        ReleaseSlot(index);
                    }
                }
                m_Backend->Shutdown();
                m_bInitialized = false;
                return bBackendFailure ? AudioResult::BackendFailure : AudioResult::Success;
            }

            AudioDiagnostics GetDiagnostics() const override
            {
                AudioDiagnostics diagnostics;
                diagnostics.ActiveVoiceCount = m_ActiveVoiceCount;
                diagnostics.InFlightCallbackCount = m_InFlightCallbacks.Load();
                diagnostics.StaleEventCount = m_StaleEventCount.Load();
                diagnostics.LateEventCount = m_LateEventCount.Load();
                diagnostics.DroppedEventCount = m_DroppedEventCount.Load();
                diagnostics.QuarantinedVoiceCount = m_QuarantinedVoiceCount;
                diagnostics.PendingEventCount = GetPendingEventCount();
                return diagnostics;
            }

        private:
            void EnqueueBackendEvent(AudioBackendEventMailbox& mailbox,
                                     VoiceHandle handle,
                                     uint64_t shutdownEpoch,
                                     AudioBackendEventKind kind) noexcept override
            {
                ++m_InFlightCallbacks;
                if (!m_bAcceptingEvents.Load(std::memory_order_acquire))
                {
                    ++m_LateEventCount;
                }
                else
                {
                    switch (mailbox.TryPublish(handle, shutdownEpoch, kind))
                    {
                    case AudioBackendEventPublishResult::Published:
                        break;
                    case AudioBackendEventPublishResult::Coalesced:
                        ++m_DroppedEventCount;
                        break;
                    case AudioBackendEventPublishResult::StaleHandle:
                        ++m_StaleEventCount;
                        break;
                    case AudioBackendEventPublishResult::StaleEpoch:
                        ++m_LateEventCount;
                        break;
                    case AudioBackendEventPublishResult::Closed:
                        ++m_StaleEventCount;
                        break;
                    }
                }
                --m_InFlightCallbacks;
            }

            uint32_t AcquireSlot()
            {
                uint32_t index = 0;
                if (!m_FreeIndices.empty())
                {
                    index = m_FreeIndices.back();
                    m_FreeIndices.pop_back();
                }
                else
                {
                    index = static_cast<uint32_t>(m_Voices.size());
                    m_Voices.emplace_back();
                }

                ActiveVoiceRecord& record = m_Voices[index];
                if (!record.Mailbox)
                {
                    record.Mailbox = Core::Container::MakeUnique<AudioBackendEventMailbox>();
                    ++m_MailboxAllocationCount;
                }
                record.Mailbox->Configure(VoiceHandle{index, record.Generation}, m_ShutdownEpoch);
                return index;
            }

            void ReleaseSlot(uint32_t index)
            {
                ActiveVoiceRecord& record = m_Voices[index];
                const bool bWasQuarantined = record.State == AudioVoiceState::Quarantined;
                record.Mailbox->Close();
                record.State = AudioVoiceState::Destroyed;
                record.Clip.reset();
                record.PcmPayload = Core::Asset::AssetBlob::Invalid();
                record.bBackendVoiceCreated = false;
                if (record.bCountedActive && m_ActiveVoiceCount > 0)
                {
                    --m_ActiveVoiceCount;
                }
                record.bCountedActive = false;
                if (m_QuarantinedVoiceCount > 0 && bWasQuarantined)
                {
                    --m_QuarantinedVoiceCount;
                }
                if (record.Generation == UINT32_MAX)
                {
                    record.bRetired = true;
                    return;
                }
                ++record.Generation;
                m_FreeIndices.push_back(index);
            }

            void QuarantineSlot(uint32_t index)
            {
                ActiveVoiceRecord& record = m_Voices[index];
                if (record.State != AudioVoiceState::Quarantined)
                {
                    record.State = AudioVoiceState::Quarantined;
                    ++m_QuarantinedVoiceCount;
                }
                if (record.bCountedActive && m_ActiveVoiceCount > 0)
                {
                    --m_ActiveVoiceCount;
                }
                record.bCountedActive = false;
            }

            ActiveVoiceRecord* Resolve(VoiceHandle handle)
            {
                if (!handle.IsValid() || handle.Index >= m_Voices.size())
                {
                    return nullptr;
                }
                ActiveVoiceRecord& record = m_Voices[handle.Index];
                if (record.Generation != handle.Generation || record.State == AudioVoiceState::Destroyed)
                {
                    return nullptr;
                }
                return &record;
            }

            const ActiveVoiceRecord* Resolve(VoiceHandle handle) const
            {
                if (!handle.IsValid() || handle.Index >= m_Voices.size())
                {
                    return nullptr;
                }
                const ActiveVoiceRecord& record = m_Voices[handle.Index];
                if (record.Generation != handle.Generation || record.State == AudioVoiceState::Destroyed)
                {
                    return nullptr;
                }
                return &record;
            }

            void DrainEventsForShutdown()
            {
                for (ActiveVoiceRecord& record : m_Voices)
                {
                    if (record.Mailbox)
                    {
                        record.Mailbox->Close();
                    }
                }
            }

            [[nodiscard]] size_t GetPendingEventCount() const noexcept
            {
                size_t pendingEventCount = 0;
                for (const ActiveVoiceRecord& record : m_Voices)
                {
                    if (record.Mailbox && record.Mailbox->HasPendingEvent())
                    {
                        ++pendingEventCount;
                    }
                }
                return pendingEventCount;
            }

            Core::Container::TUniquePtr<IAudioBackend> m_Backend;
            Core::Container::VariableArray<ActiveVoiceRecord> m_Voices;
            Core::Container::VariableArray<uint32_t> m_FreeIndices;
            Thread::Atomic<uint32_t> m_InFlightCallbacks{0};
            Thread::Atomic<uint64_t> m_LateEventCount{0};
            Thread::Atomic<uint64_t> m_DroppedEventCount{0};
            Thread::Atomic<bool> m_bAcceptingEvents{false};
            uint64_t m_ShutdownEpoch = 0;
            Thread::Atomic<uint64_t> m_StaleEventCount{0};
            uint32_t m_ActiveVoiceCount = 0;
            uint32_t m_QuarantinedVoiceCount = 0;
            size_t m_MailboxAllocationCount = 0;
            bool m_bInitialized = false;
            bool m_bShutdown = false;
        };
    } // namespace

    Core::Container::TUniquePtr<IAudioService> CreateAudioServiceForBackend(
        Core::Container::TUniquePtr<IAudioBackend> backend)
    {
        if (!backend)
        {
            return {};
        }
        return Core::Container::MakeUnique<AudioService>(std::move(backend));
    }

    bool AudioServiceTestAccess::PrepareVoiceGenerationWrap(IAudioService& service, VoiceHandle& handle)
    {
        auto* concrete = dynamic_cast<AudioService*>(&service);
        if (concrete == nullptr)
        {
            return false;
        }
        ActiveVoiceRecord* record = concrete->Resolve(handle);
        if (record == nullptr || !record->Mailbox)
        {
            return false;
        }
        record->Generation = UINT32_MAX;
        handle.Generation = UINT32_MAX;
        record->Mailbox->Configure(handle, concrete->m_ShutdownEpoch);
        return true;
    }

    size_t AudioServiceTestAccess::GetVoiceSlotCount(const IAudioService& service)
    {
        const auto* concrete = dynamic_cast<const AudioService*>(&service);
        return concrete != nullptr ? concrete->m_Voices.size() : 0;
    }

    size_t AudioServiceTestAccess::GetMailboxAllocationCount(const IAudioService& service)
    {
        const auto* concrete = dynamic_cast<const AudioService*>(&service);
        return concrete != nullptr ? concrete->m_MailboxAllocationCount : 0;
    }
} // namespace NorvesLib::Modules::Audio::Private
