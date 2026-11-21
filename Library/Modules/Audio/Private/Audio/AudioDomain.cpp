#include "Audio/AudioDomain.h"

#include "Audio/AudioClipResource.h"
#include "Audio/AudioOcclusionUpdater.h"
#include "Container/VariableArray.h"
#include "Math/MathTypes.h"
#include "Thread/Atomic.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace NorvesLib::Modules::Audio
{
    bool IsPlayableVoiceDesc(uint64_t frameCount, uint64_t pcmBytes, const AudioVoiceDesc& desc) noexcept
    {
        if (pcmBytes == 0 || pcmBytes > kAudioMaxVoicePcmBytes)
        {
            return false;
        }
        if (!std::isfinite(desc.MaxPitchSemitones) || desc.MaxPitchSemitones < 0.0f ||
            desc.MaxPitchSemitones > kAudioMaxVoicePitchSemitones || desc.StartFrame >= frameCount)
        {
            return false;
        }
        if (!desc.bLoop)
        {
            return desc.LoopBeginFrame == 0 && desc.LoopLengthFrames == 0;
        }
        const uint64_t loopEndFrame = desc.LoopLengthFrames == 0
            ? frameCount
            : static_cast<uint64_t>(desc.LoopBeginFrame) + desc.LoopLengthFrames;
        return desc.LoopBeginFrame < frameCount && loopEndFrame <= frameCount && loopEndFrame > desc.StartFrame;
    }
}

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        constexpr float kDefaultMaxFrequencyRatio = 2.0f;
        constexpr float kMinFrequencyRatio = kAudioMinVoiceFrequencyRatio;

        [[nodiscard]] constexpr bool CanSetVoiceParameters(AudioVoiceState state) noexcept
        {
            return state == AudioVoiceState::Created || state == AudioVoiceState::Submitted ||
                   state == AudioVoiceState::Playing;
        }

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
            float MaxFrequencyRatio = 2.0f;
            float UserVolume = 1.0f;   ///< 呼び手が指定した線形音量。
            float AcousticGain = 1.0f; ///< 遮蔽による線形利得。
            bool bPaused = false;
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
                return CreateVoice(clip, AudioVoiceDesc{}, outHandle);
            }

            AudioResult CreateVoice(const Core::Container::TSharedPtr<AudioClipResource>& clip,
                                    const AudioVoiceDesc& desc,
                                    VoiceHandle& outHandle) override
            {
                outHandle = {};
                if (!m_bInitialized || m_bShutdown || !clip || !clip->IsValid())
                {
                    return AudioResult::InvalidState;
                }

                if (!IsPlayableVoiceDesc(clip->GetFrameCount(), clip->GetPcmBlob().GetSize(), desc))
                {
                    return AudioResult::InvalidState;
                }

                const uint32_t index = AcquireSlot();
                ActiveVoiceRecord& record = m_Voices[index];
                record.Clip = clip;
                record.PcmPayload = clip->GetPcmBlob();
                record.State = AudioVoiceState::Created;
                record.UserVolume = 1.0f;
                record.AcousticGain = 1.0f;
                record.MaxFrequencyRatio = std::exp2(desc.MaxPitchSemitones / 12.0f);
                const VoiceHandle handle{index, record.Generation};

                if (m_Backend->CreateVoice(handle, clip->GetFormat(), record.MaxFrequencyRatio, *record.Mailbox) != AudioResult::Success)
                {
                    ReleaseSlot(index);
                    return AudioResult::BackendFailure;
                }
                record.bBackendVoiceCreated = true;

                const AudioBufferDesc bufferDesc{desc.bLoop, desc.LoopBeginFrame, desc.LoopLengthFrames, desc.StartFrame};
                if (m_Backend->SubmitVoice(handle, record.PcmPayload.GetSpan(), bufferDesc) != AudioResult::Success)
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

            AudioResult SetVoiceVolume(VoiceHandle handle, float volume) override
            {
                ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (!std::isfinite(volume) || volume < 0.0f || !CanSetVoiceParameters(record->State))
                {
                    return AudioResult::InvalidState;
                }
                if (m_Backend->SetVoiceVolume(handle, volume * record->AcousticGain) != AudioResult::Success)
                {
                    return AudioResult::BackendFailure;
                }
                record->UserVolume = volume;
                return AudioResult::Success;
            }

            AudioResult SetVoiceAcoustics(VoiceHandle handle, const VoiceAcousticParams& params) override
            {
                ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (!std::isfinite(params.TargetGainDb) || !std::isfinite(params.LowpassCutoffHz) ||
                    !CanSetVoiceParameters(record->State))
                {
                    return AudioResult::InvalidState;
                }

                const float gainDb = std::clamp(params.TargetGainDb, kVoiceAcousticMinGainDb, 0.0f);
                const float gain = std::pow(10.0f, gainDb / 20.0f);
                const float cutoff = std::clamp(params.LowpassCutoffHz, kVoiceAcousticMinCutoffHz, kVoiceAcousticMaxCutoffHz);
                const AudioResult volumeResult = m_Backend->SetVoiceVolume(handle, record->UserVolume * gain);
                const AudioResult filterResult = m_Backend->SetVoiceFilter(handle, cutoff, kVoiceLowpassOneOverQ);
                if (volumeResult != AudioResult::Success || filterResult != AudioResult::Success)
                {
                    return AudioResult::BackendFailure;
                }
                record->AcousticGain = gain;
                return AudioResult::Success;
            }

            AudioResult SetVoicePitch(VoiceHandle handle, float semitones) override
            {
                const ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (!std::isfinite(semitones) || !CanSetVoiceParameters(record->State))
                {
                    return AudioResult::InvalidState;
                }
                const float boundedSemitones = std::clamp(semitones, -120.0f, 120.0f);
                const float ratio = std::clamp(std::exp2(boundedSemitones / 12.0f),
                                               kMinFrequencyRatio,
                                               record->MaxFrequencyRatio);
                return m_Backend->SetVoiceFrequencyRatio(handle, ratio) == AudioResult::Success
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult SetVoicePan(VoiceHandle handle, float pan) override
            {
                const ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (!std::isfinite(pan) || !CanSetVoiceParameters(record->State))
                {
                    return AudioResult::InvalidState;
                }
                const float boundedPan = std::clamp(pan, -1.0f, 1.0f);
                const float angle = (boundedPan + 1.0f) * Math::Constants::PI / 4.0f;
                return m_Backend->SetVoiceOutputGains(handle, std::cos(angle), std::sin(angle)) == AudioResult::Success
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult SetVoiceOutputGains(VoiceHandle handle, float leftGain, float rightGain) override
            {
                const ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (!std::isfinite(leftGain) || leftGain < 0.0f || !std::isfinite(rightGain) || rightGain < 0.0f ||
                    !CanSetVoiceParameters(record->State))
                {
                    return AudioResult::InvalidState;
                }
                return m_Backend->SetVoiceOutputGains(handle, leftGain, rightGain) == AudioResult::Success
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult SetVoiceFrequencyRatio(VoiceHandle handle, float ratio) override
            {
                const ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (!std::isfinite(ratio) || ratio <= 0.0f || !CanSetVoiceParameters(record->State))
                {
                    return AudioResult::InvalidState;
                }
                const float boundedRatio = std::clamp(ratio, kMinFrequencyRatio, record->MaxFrequencyRatio);
                return m_Backend->SetVoiceFrequencyRatio(handle, boundedRatio) == AudioResult::Success
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult PauseVoice(VoiceHandle handle) override
            {
                ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (record->State != AudioVoiceState::Playing)
                {
                    return AudioResult::InvalidState;
                }
                if (record->bPaused)
                {
                    return AudioResult::AlreadyInState;
                }
                if (m_Backend->PauseVoice(handle) != AudioResult::Success)
                {
                    return AudioResult::BackendFailure;
                }
                record->bPaused = true;
                return AudioResult::Success;
            }

            AudioResult ResumeVoice(VoiceHandle handle) override
            {
                ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (record->State != AudioVoiceState::Playing)
                {
                    return AudioResult::InvalidState;
                }
                if (!record->bPaused)
                {
                    return AudioResult::AlreadyInState;
                }
                if (m_Backend->ResumeVoice(handle) != AudioResult::Success)
                {
                    return AudioResult::BackendFailure;
                }
                record->bPaused = false;
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
                record->bPaused = false;
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

            AudioResult AbortVoice(VoiceHandle handle) override
            {
                ActiveVoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::StaleHandle;
                }
                if (record->State == AudioVoiceState::Quarantined)
                {
                    return AudioResult::InvalidState;
                }
                if (record->State == AudioVoiceState::Playing)
                {
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
                }
                if (record->bBackendVoiceCreated && m_Backend->DestroyVoice(handle) != AudioResult::Success)
                {
                    QuarantineSlot(handle.Index);
                    return AudioResult::BackendFailure;
                }

                // XAudio2 の DestroyVoice は音声処理スレッドが空くまで同期的に待つため、
                // 復帰後にそのボイスのコールバックは来ない。復帰前に届いた通知も、
                // メールボックスの Close と世代の更新により新しいボイスには混ざらない。
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

            AudioOcclusionUpdater& GetOcclusionUpdater()
            {
                return m_Occlusion;
            }

            void SetAcousticsEvaluator(const Core::Scene::AcousticTransmissionEvaluator* evaluator)
            {
                m_AcousticsEvaluator = evaluator;
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
                        record->bPaused = false;
                    }
                    if (event.Kind == AudioBackendEventKind::Drained &&
                        record->State == AudioVoiceState::Drained)
                    {
                        continue;
                    }
                    if (record->State == AudioVoiceState::Playing)
                    {
                        record->State = AudioVoiceState::Stopping;
                        record->bPaused = false;
                    }
                    if (record->State == AudioVoiceState::Stopping)
                    {
                        record->State = AudioVoiceState::Drained;
                        record->bPaused = false;
                    }
                    else
                    {
                        ++m_StaleEventCount;
                    }
                }

                if (m_bShutdown)
                {
                    return;
                }

                const auto now = std::chrono::steady_clock::now();
                float dt = 0.0f;
                if (m_bOcclusionClockStarted)
                {
                    // 長い停止の後も遮蔽の利得と遮断周波数が目標へ跳ばないよう、経過時間に上限を設ける。
                    dt = std::min(std::chrono::duration<float>(now - m_LastOcclusionTick).count(), 0.1f);
                }
                m_LastOcclusionTick = now;
                m_bOcclusionClockStarted = true;
                m_Occlusion.Tick(*this, m_AcousticsEvaluator, dt);
            }

            AudioResult Shutdown() override
            {
                if (m_bShutdown)
                {
                    return AudioResult::AlreadyInState;
                }
                m_bShutdown = true;
                m_Occlusion.Reset();
                m_bOcclusionClockStarted = false;
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
                record.MaxFrequencyRatio = kDefaultMaxFrequencyRatio;
                record.bPaused = false;
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
            AudioOcclusionUpdater m_Occlusion;
            const Core::Scene::AcousticTransmissionEvaluator* m_AcousticsEvaluator = nullptr;
            std::chrono::steady_clock::time_point m_LastOcclusionTick{};
            bool m_bOcclusionClockStarted = false;
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

    AudioOcclusionUpdater* GetAudioServiceOcclusion(IAudioService& service)
    {
        auto* concrete = dynamic_cast<AudioService*>(&service);
        return concrete ? &concrete->GetOcclusionUpdater() : nullptr;
    }

    void SetAudioServiceAcousticsEvaluator(IAudioService& service, const Core::Scene::AcousticTransmissionEvaluator* evaluator)
    {
        if (auto* concrete = dynamic_cast<AudioService*>(&service))
        {
            concrete->SetAcousticsEvaluator(evaluator);
        }
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
