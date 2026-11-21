#pragma once

#include "Audio/AudioBackend.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Modules::Audio::Private
{
    // 出力装置を使わず、終端通知だけでボイスの寿命を進める。
    class NullAudioBackend final : public IAudioBackend
    {
    public:
        AudioResult Initialize(IAudioBackendEventSink& eventSink, uint64_t shutdownEpoch) override;
        using IAudioBackend::CreateVoice;
        AudioResult CreateVoice(VoiceHandle handle, const AudioBackendVoiceDesc& desc,
                                AudioBackendEventMailbox& mailbox) override;
        AudioResult CreateBus(AudioBusId bus, AudioBusId parent) override;
        AudioResult SetBusVolume(AudioBusId bus, float gain) override;
        AudioResult SubmitVoice(VoiceHandle handle, Core::Container::Span<const uint8_t> pcmBytes,
                                const AudioBufferDesc& bufferDesc) override;
        AudioResult SetVoiceVolume(VoiceHandle handle, float volume) override;
        AudioResult SetVoiceFilter(VoiceHandle handle, float lowpassCutoffHz, float oneOverQ) override;
        AudioResult SetVoiceFrequencyRatio(VoiceHandle handle, float ratio) override;
        AudioResult SetVoiceOutputGains(VoiceHandle handle, float leftGain, float rightGain) override;
        AudioResult PauseVoice(VoiceHandle handle) override;
        AudioResult ResumeVoice(VoiceHandle handle) override;
        AudioResult StartVoice(VoiceHandle handle) override;
        AudioBackendStopResult StopVoice(VoiceHandle handle) override;
        void QuiesceCallbacks() override;
        AudioResult DestroyVoice(VoiceHandle handle) override;
        void Shutdown() override;

    private:
        struct Bus
        {
            bool bCreated = false;
            float Volume = 1.0f;
        };

        struct Voice
        {
            VoiceHandle Handle;
            AudioBackendEventMailbox* Mailbox = nullptr;
            bool bSubmitted = false;
            bool bLoop = false;
        };

        Voice* Resolve(VoiceHandle handle);
        void PublishDrained(Voice& voice);

        Bus m_Buses[kAudioBusCount]{};
        Core::Container::VariableArray<Voice> m_Voices;
        IAudioBackendEventSink* m_EventSink = nullptr;
        uint64_t m_ShutdownEpoch = 0;
    };
} // namespace NorvesLib::Modules::Audio::Private
