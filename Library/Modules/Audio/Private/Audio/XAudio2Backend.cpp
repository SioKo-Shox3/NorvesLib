#include "Audio/XAudio2BackendFactory.h"

#include "Container/VariableArray.h"
#include "Thread/Atomic.h"

#include <cmath>

#include <Windows.h>
#include <xaudio2.h>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        class XAudio2Backend;

        class XAudio2VoiceCallback final : public IXAudio2VoiceCallback
        {
        public:
            XAudio2VoiceCallback(XAudio2Backend& owner,
                                 VoiceHandle handle,
                                 AudioBackendEventMailbox& mailbox)
                : m_Owner(owner), m_Handle(handle), m_Mailbox(mailbox)
            {
            }

            void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
            void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
            void STDMETHODCALLTYPE OnStreamEnd() override;
            void STDMETHODCALLTYPE OnBufferStart(void*) override {}
            void STDMETHODCALLTYPE OnBufferEnd(void*) override;
            void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
            void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override;

        private:
            XAudio2Backend& m_Owner;
            VoiceHandle m_Handle;
            AudioBackendEventMailbox& m_Mailbox;
        };

        struct XAudio2VoiceRecord
        {
            uint32_t Generation = 0;
            uint32_t InputChannelCount = 0;
            IXAudio2SourceVoice* Voice = nullptr;
            Core::Container::TUniquePtr<XAudio2VoiceCallback> Callback;
        };

        class XAudio2Backend final : public IAudioBackend
        {
        public:
            ~XAudio2Backend() override
            {
                Shutdown();
            }

            AudioResult Initialize(IAudioBackendEventSink& eventSink, uint64_t shutdownEpoch) override
            {
                if (m_Engine != nullptr)
                {
                    return AudioResult::AlreadyInState;
                }
                IXAudio2* engine = nullptr;
                if (FAILED(XAudio2Create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR)))
                {
                    return AudioResult::BackendFailure;
                }
                IXAudio2MasteringVoice* masteringVoice = nullptr;
                if (FAILED(engine->CreateMasteringVoice(&masteringVoice)))
                {
                    engine->Release();
                    return AudioResult::BackendFailure;
                }
                XAUDIO2_VOICE_DETAILS voiceDetails{};
                masteringVoice->GetVoiceDetails(&voiceDetails);
                m_OutputChannelCount = voiceDetails.InputChannels;
                m_EventSink.Store(&eventSink);
                m_ShutdownEpoch.Store(shutdownEpoch);
                m_bAcceptingCallbacks.Store(true);
                m_Engine = engine;
                m_MasteringVoice = masteringVoice;
                return AudioResult::Success;
            }

            AudioResult CreateVoice(VoiceHandle handle,
                                    const AudioPcmFormat& format,
                                    float maxFrequencyRatio,
                                    AudioBackendEventMailbox& mailbox) override
            {
                if (m_Engine == nullptr || !format.IsSupported() || !handle.IsValid() ||
                    !std::isfinite(maxFrequencyRatio) || maxFrequencyRatio < XAUDIO2_MIN_FREQ_RATIO ||
                    maxFrequencyRatio > XAUDIO2_MAX_FREQ_RATIO)
                {
                    return AudioResult::BackendFailure;
                }
                if (handle.Index >= m_Voices.size())
                {
                    m_Voices.resize(static_cast<size_t>(handle.Index) + 1);
                }
                XAudio2VoiceRecord& record = m_Voices[handle.Index];
                if (record.Voice != nullptr)
                {
                    return AudioResult::BackendFailure;
                }

                auto callback = Core::Container::MakeUnique<XAudio2VoiceCallback>(*this, handle, mailbox);
                WAVEFORMATEX waveFormat{};
                waveFormat.wFormatTag = WAVE_FORMAT_PCM;
                waveFormat.nChannels = format.ChannelCount;
                waveFormat.nSamplesPerSec = format.SampleRate;
                waveFormat.wBitsPerSample = format.BitsPerSample;
                waveFormat.nBlockAlign = format.BlockAlignment;
                waveFormat.nAvgBytesPerSec = format.SampleRate * format.BlockAlignment;
                IXAudio2SourceVoice* voice = nullptr;
                if (FAILED(m_Engine->CreateSourceVoice(&voice, &waveFormat, 0, maxFrequencyRatio, callback.get())))
                {
                    return AudioResult::BackendFailure;
                }
                record.Generation = handle.Generation;
                record.InputChannelCount = format.ChannelCount;
                record.Voice = voice;
                record.Callback = std::move(callback);
                return AudioResult::Success;
            }

            AudioResult SubmitVoice(VoiceHandle handle,
                                    Core::Container::Span<const uint8_t> pcmBytes,
                                    const AudioBufferDesc& bufferDesc) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                if (record == nullptr || pcmBytes.empty() || pcmBytes.size() > XAUDIO2_MAX_BUFFER_BYTES)
                {
                    return AudioResult::BackendFailure;
                }
                const uint32_t blockAlignment = record->InputChannelCount * sizeof(int16_t);
                if (pcmBytes.size() % blockAlignment != 0)
                {
                    return AudioResult::BackendFailure;
                }
                const uint32_t totalFrames = static_cast<uint32_t>(pcmBytes.size() / blockAlignment);
                if (bufferDesc.StartFrame >= totalFrames)
                {
                    return AudioResult::BackendFailure;
                }
                uint32_t loopLength = 0;
                if (bufferDesc.bLoop)
                {
                    if (bufferDesc.LoopBeginFrame >= totalFrames)
                    {
                        return AudioResult::BackendFailure;
                    }
                    const uint32_t remainingFrames = totalFrames - bufferDesc.LoopBeginFrame;
                    loopLength = bufferDesc.LoopLengthFrames == 0 ? remainingFrames : bufferDesc.LoopLengthFrames;
                    // 減算後の範囲で調べ、加算のオーバーフローを防ぐ。ループ開始は再生開始より前でもよい。
                    if (loopLength > remainingFrames ||
                        bufferDesc.LoopBeginFrame + loopLength <= bufferDesc.StartFrame)
                    {
                        return AudioResult::BackendFailure;
                    }
                }
                XAUDIO2_BUFFER buffer{};
                buffer.Flags = XAUDIO2_END_OF_STREAM;
                buffer.AudioBytes = static_cast<UINT32>(pcmBytes.size());
                buffer.pAudioData = pcmBytes.data();
                buffer.PlayBegin = bufferDesc.StartFrame;
                // 長さ0は開始位置も0の場合だけ有効なので、途中開始時は末尾までの長さを明示する。
                buffer.PlayLength = bufferDesc.StartFrame == 0 ? 0 : totalFrames - bufferDesc.StartFrame;
                if (bufferDesc.bLoop)
                {
                    buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
                    buffer.LoopBegin = bufferDesc.LoopBeginFrame;
                    // クリップ全体のループは、末尾ちょうどの明示長を避けて文書化された長さ0で指定する。
                    buffer.LoopLength = bufferDesc.LoopBeginFrame == 0 && loopLength == totalFrames ? 0 : loopLength;
                }
                return SUCCEEDED(record->Voice->SubmitSourceBuffer(&buffer))
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult SetVoiceVolume(VoiceHandle handle, float volume) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                return record != nullptr && SUCCEEDED(record->Voice->SetVolume(volume))
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult SetVoiceFrequencyRatio(VoiceHandle handle, float ratio) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                return record != nullptr && SUCCEEDED(record->Voice->SetFrequencyRatio(ratio))
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult SetVoiceOutputGains(VoiceHandle handle, float leftGain, float rightGain) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                if (record == nullptr || record->InputChannelCount < 1 || record->InputChannelCount > 2 ||
                    m_OutputChannelCount < 1 || m_OutputChannelCount > XAUDIO2_MAX_AUDIO_CHANNELS)
                {
                    return AudioResult::BackendFailure;
                }
                const uint32_t sourceChannels = record->InputChannelCount;
                float levels[2 * XAUDIO2_MAX_AUDIO_CHANNELS]{};
                // 行列は入力チャンネル数 × 出力番号 + 入力番号の順に並べる。
                if (m_OutputChannelCount == 1)
                {
                    const float monoGain = (leftGain + rightGain) * 0.5f;
                    for (uint32_t source = 0; source < sourceChannels; ++source)
                    {
                        levels[source] = monoGain;
                    }
                }
                else
                {
                    levels[0] = leftGain;
                    levels[sourceChannels + (sourceChannels == 1 ? 0 : 1)] = rightGain;
                }
                return SUCCEEDED(record->Voice->SetOutputMatrix(nullptr, sourceChannels, m_OutputChannelCount, levels))
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult PauseVoice(VoiceHandle handle) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                return record != nullptr && SUCCEEDED(record->Voice->Stop(XAUDIO2_PLAY_TAILS))
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult ResumeVoice(VoiceHandle handle) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                return record != nullptr && SUCCEEDED(record->Voice->Start(0))
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioResult StartVoice(VoiceHandle handle) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                return record != nullptr && SUCCEEDED(record->Voice->Start(0))
                    ? AudioResult::Success
                    : AudioResult::BackendFailure;
            }

            AudioBackendStopResult StopVoice(VoiceHandle handle) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return {AudioResult::BackendFailure, false, false};
                }
                if (FAILED(record->Voice->Stop(0)))
                {
                    return {AudioResult::BackendFailure, false, false};
                }
                if (FAILED(record->Voice->FlushSourceBuffers()))
                {
                    return {AudioResult::BackendFailure, true, false};
                }
                return {AudioResult::Success, true, true};
            }

            void QuiesceCallbacks() override
            {
                m_bAcceptingCallbacks.Store(false);
                m_EventSink.Exchange(nullptr);
                if (m_Engine != nullptr)
                {
                    m_Engine->StopEngine();
                }
                for (XAudio2VoiceRecord& record : m_Voices)
                {
                    if (record.Voice != nullptr)
                    {
                        record.Voice->Stop(0);
                        record.Voice->FlushSourceBuffers();
                    }
                }
                while (m_InFlightCallbacks.Load() != 0)
                {
                    ::Sleep(0);
                }
            }

            AudioResult DestroyVoice(VoiceHandle handle) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                if (record == nullptr)
                {
                    return AudioResult::BackendFailure;
                }
                record->Voice->DestroyVoice();
                record->Voice = nullptr;
                record->Callback.reset();
                return AudioResult::Success;
            }

            void Shutdown() override
            {
                m_bAcceptingCallbacks.Store(false);
                m_EventSink.Exchange(nullptr);
                for (XAudio2VoiceRecord& record : m_Voices)
                {
                    if (record.Voice != nullptr)
                    {
                        record.Voice->DestroyVoice();
                        record.Voice = nullptr;
                        record.Callback.reset();
                    }
                }
                if (m_MasteringVoice != nullptr)
                {
                    m_MasteringVoice->DestroyVoice();
                    m_MasteringVoice = nullptr;
                }
                if (m_Engine != nullptr)
                {
                    m_Engine->Release();
                    m_Engine = nullptr;
                }
                m_OutputChannelCount = 0;
                m_ShutdownEpoch.Store(0);
            }

            void Emit(AudioBackendEventMailbox& mailbox,
                      VoiceHandle handle,
                      AudioBackendEventKind kind) noexcept
            {
                ++m_InFlightCallbacks;
                if (!m_bAcceptingCallbacks.Load())
                {
                    --m_InFlightCallbacks;
                    return;
                }
                IAudioBackendEventSink* sink = m_EventSink.Load();
                if (sink != nullptr)
                {
                    sink->EnqueueBackendEvent(mailbox, handle, m_ShutdownEpoch.Load(), kind);
                }
                --m_InFlightCallbacks;
            }

        private:
            XAudio2VoiceRecord* Resolve(VoiceHandle handle)
            {
                if (!handle.IsValid() || handle.Index >= m_Voices.size())
                {
                    return nullptr;
                }
                XAudio2VoiceRecord& record = m_Voices[handle.Index];
                return record.Generation == handle.Generation && record.Voice != nullptr ? &record : nullptr;
            }

            IXAudio2* m_Engine = nullptr;
            IXAudio2MasteringVoice* m_MasteringVoice = nullptr;
            uint32_t m_OutputChannelCount = 0;
            Thread::Atomic<IAudioBackendEventSink*> m_EventSink{nullptr};
            Thread::Atomic<uint64_t> m_ShutdownEpoch{0};
            Thread::Atomic<bool> m_bAcceptingCallbacks{false};
            Thread::Atomic<uint32_t> m_InFlightCallbacks{0};
            Core::Container::VariableArray<XAudio2VoiceRecord> m_Voices;
        };

        void XAudio2VoiceCallback::OnStreamEnd()
        {
            m_Owner.Emit(m_Mailbox, m_Handle, AudioBackendEventKind::Drained);
        }

        void XAudio2VoiceCallback::OnBufferEnd(void*)
        {
            m_Owner.Emit(m_Mailbox, m_Handle, AudioBackendEventKind::Drained);
        }

        void XAudio2VoiceCallback::OnVoiceError(void*, HRESULT)
        {
            m_Owner.Emit(m_Mailbox, m_Handle, AudioBackendEventKind::VoiceError);
        }
    } // namespace

    Core::Container::TUniquePtr<IAudioBackend> CreatePlatformAudioBackend()
    {
        return Core::Container::MakeUnique<XAudio2Backend>();
    }
} // namespace NorvesLib::Modules::Audio::Private
