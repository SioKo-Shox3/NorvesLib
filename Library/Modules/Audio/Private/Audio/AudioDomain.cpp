#include "Audio/AudioDomain.h"

#include "Audio/AudioClipResource.h"
#include "Container/VariableArray.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

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
            uint32_t Generation = 0;
            AudioVoiceState State = AudioVoiceState::Destroyed;
            Core::Container::TSharedPtr<AudioClipResource> Clip;
            bool bBackendVoiceCreated = false;
            bool bCountedActive = false;
        };

        class AudioService final : public IAudioService, private IAudioBackendEventSink
        {
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
                    return AudioResult::BackendFailure;
                }
                {
                    Thread::ScopedLock lock(m_EventMutex);
                    m_bAcceptingEvents = true;
                }
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
                record.State = AudioVoiceState::Created;
                const VoiceHandle handle{index, record.Generation};

                if (m_Backend->CreateVoice(handle, clip->GetFormat()) != AudioResult::Success)
                {
                    ReleaseSlot(index);
                    return AudioResult::BackendFailure;
                }
                record.bBackendVoiceCreated = true;

                if (m_Backend->SubmitVoice(handle, clip->GetPcmBytes()) != AudioResult::Success)
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
                Core::Container::VariableArray<BackendEvent> events;
                {
                    Thread::ScopedLock lock(m_EventMutex);
                    events.swap(m_PendingEvents);
                }

                for (const BackendEvent& event : events)
                {
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

                {
                    Thread::ScopedLock lock(m_EventMutex);
                    m_bAcceptingEvents = false;
                }

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
                for (uint32_t index = 0; index < m_Voices.size(); ++index)
                {
                    if (m_Voices[index].State != AudioVoiceState::Destroyed)
                    {
                        ReleaseSlot(index);
                    }
                }
                m_bInitialized = false;
                return bBackendFailure ? AudioResult::BackendFailure : AudioResult::Success;
            }

            AudioDiagnostics GetDiagnostics() const override
            {
                AudioDiagnostics diagnostics;
                diagnostics.ActiveVoiceCount = m_ActiveVoiceCount;
                diagnostics.InFlightCallbackCount = m_InFlightCallbacks.Load();
                diagnostics.StaleEventCount = m_StaleEventCount;
                diagnostics.LateEventCount = m_LateEventCount.Load();
                diagnostics.QuarantinedVoiceCount = m_QuarantinedVoiceCount;
                {
                    Thread::ScopedLock lock(m_EventMutex);
                    diagnostics.PendingEventCount = m_PendingEvents.size();
                }
                return diagnostics;
            }

        private:
            void EnqueueBackendEvent(VoiceHandle handle,
                                     uint64_t shutdownEpoch,
                                     AudioBackendEventKind kind) noexcept override
            {
                ++m_InFlightCallbacks;
                {
                    Thread::ScopedLock lock(m_EventMutex);
                    if (!m_bAcceptingEvents)
                    {
                        ++m_LateEventCount;
                    }
                    else
                    {
                        m_PendingEvents.push_back(BackendEvent{handle, shutdownEpoch, kind});
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
                ++record.Generation;
                if (record.Generation == 0)
                {
                    ++record.Generation;
                }
                return index;
            }

            void ReleaseSlot(uint32_t index)
            {
                ActiveVoiceRecord& record = m_Voices[index];
                const bool bWasQuarantined = record.State == AudioVoiceState::Quarantined;
                record.State = AudioVoiceState::Destroyed;
                record.Clip.reset();
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
                Thread::ScopedLock lock(m_EventMutex);
                m_PendingEvents.clear();
            }

            Core::Container::TUniquePtr<IAudioBackend> m_Backend;
            Core::Container::VariableArray<ActiveVoiceRecord> m_Voices;
            Core::Container::VariableArray<uint32_t> m_FreeIndices;
            mutable Thread::Mutex m_EventMutex;
            Core::Container::VariableArray<BackendEvent> m_PendingEvents;
            Thread::Atomic<uint32_t> m_InFlightCallbacks{0};
            Thread::Atomic<uint64_t> m_LateEventCount{0};
            bool m_bAcceptingEvents = false;
            uint64_t m_ShutdownEpoch = 0;
            uint64_t m_StaleEventCount = 0;
            uint32_t m_ActiveVoiceCount = 0;
            uint32_t m_QuarantinedVoiceCount = 0;
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
} // namespace NorvesLib::Modules::Audio::Private
