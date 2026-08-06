#include "Audio/XAudio2BackendFactory.h"

#include "Container/VariableArray.h"
#include "Thread/Atomic.h"

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
            XAudio2VoiceCallback(XAudio2Backend& owner, VoiceHandle handle)
                : m_Owner(owner), m_Handle(handle)
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
        };

        struct XAudio2VoiceRecord
        {
            uint32_t Generation = 0;
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
                m_EventSink.Store(&eventSink);
                m_ShutdownEpoch.Store(shutdownEpoch);
                m_bAcceptingCallbacks.Store(true);
                m_Engine = engine;
                m_MasteringVoice = masteringVoice;
                return AudioResult::Success;
            }

            AudioResult CreateVoice(VoiceHandle handle, const AudioPcmFormat& format) override
            {
                if (m_Engine == nullptr || !format.IsSupported() || !handle.IsValid())
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

                auto callback = Core::Container::MakeUnique<XAudio2VoiceCallback>(*this, handle);
                WAVEFORMATEX waveFormat{};
                waveFormat.wFormatTag = WAVE_FORMAT_PCM;
                waveFormat.nChannels = format.ChannelCount;
                waveFormat.nSamplesPerSec = format.SampleRate;
                waveFormat.wBitsPerSample = format.BitsPerSample;
                waveFormat.nBlockAlign = format.BlockAlignment;
                waveFormat.nAvgBytesPerSec = format.SampleRate * format.BlockAlignment;
                IXAudio2SourceVoice* voice = nullptr;
                if (FAILED(m_Engine->CreateSourceVoice(&voice, &waveFormat, 0, XAUDIO2_DEFAULT_FREQ_RATIO, callback.get())))
                {
                    return AudioResult::BackendFailure;
                }
                record.Generation = handle.Generation;
                record.Voice = voice;
                record.Callback = std::move(callback);
                return AudioResult::Success;
            }

            AudioResult SubmitVoice(VoiceHandle handle, Core::Container::Span<const uint8_t> pcmBytes) override
            {
                XAudio2VoiceRecord* record = Resolve(handle);
                if (record == nullptr || pcmBytes.empty() || pcmBytes.size() > UINT32_MAX)
                {
                    return AudioResult::BackendFailure;
                }
                XAUDIO2_BUFFER buffer{};
                buffer.Flags = XAUDIO2_END_OF_STREAM;
                buffer.AudioBytes = static_cast<UINT32>(pcmBytes.size());
                buffer.pAudioData = pcmBytes.data();
                return SUCCEEDED(record->Voice->SubmitSourceBuffer(&buffer))
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
                m_ShutdownEpoch.Store(0);
            }

            void Emit(VoiceHandle handle, AudioBackendEventKind kind) noexcept
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
                    sink->EnqueueBackendEvent(handle, m_ShutdownEpoch.Load(), kind);
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
            Thread::Atomic<IAudioBackendEventSink*> m_EventSink{nullptr};
            Thread::Atomic<uint64_t> m_ShutdownEpoch{0};
            Thread::Atomic<bool> m_bAcceptingCallbacks{false};
            Thread::Atomic<uint32_t> m_InFlightCallbacks{0};
            Core::Container::VariableArray<XAudio2VoiceRecord> m_Voices;
        };

        void XAudio2VoiceCallback::OnStreamEnd()
        {
            m_Owner.Emit(m_Handle, AudioBackendEventKind::Drained);
        }

        void XAudio2VoiceCallback::OnBufferEnd(void*)
        {
            m_Owner.Emit(m_Handle, AudioBackendEventKind::Drained);
        }

        void XAudio2VoiceCallback::OnVoiceError(void*, HRESULT)
        {
            m_Owner.Emit(m_Handle, AudioBackendEventKind::VoiceError);
        }
    } // namespace

    Core::Container::TUniquePtr<IAudioBackend> CreatePlatformAudioBackend()
    {
        return Core::Container::MakeUnique<XAudio2Backend>();
    }
} // namespace NorvesLib::Modules::Audio::Private
