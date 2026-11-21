#include "Audio/NullAudioBackend.h"

#include <cmath>

namespace NorvesLib::Modules::Audio::Private
{
    AudioResult NullAudioBackend::Initialize(IAudioBackendEventSink& eventSink, uint64_t shutdownEpoch)
    {
        m_EventSink = &eventSink;
        m_ShutdownEpoch = shutdownEpoch;
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::CreateBus(AudioBusId bus, AudioBusId parent)
    {
        const uint32_t index = static_cast<uint32_t>(bus);
        const uint32_t parentIndex = static_cast<uint32_t>(parent);
        if (m_EventSink == nullptr || index >= kAudioBusCount ||
            (bus != AudioBusId::Master && parentIndex >= kAudioBusCount))
        {
            return AudioResult::BackendFailure;
        }
        if (m_Buses[index].bCreated)
        {
            return AudioResult::AlreadyInState;
        }
        if (bus != AudioBusId::Master && !m_Buses[parentIndex].bCreated)
        {
            return AudioResult::BackendFailure;
        }
        m_Buses[index].bCreated = true;
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::SetBusVolume(AudioBusId bus, float gain)
    {
        const uint32_t index = static_cast<uint32_t>(bus);
        if (index >= kAudioBusCount || !m_Buses[index].bCreated || !std::isfinite(gain) || gain < 0.0f)
        {
            return AudioResult::BackendFailure;
        }
        m_Buses[index].Volume = gain;
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::CreateVoice(VoiceHandle handle, const AudioBackendVoiceDesc& desc,
                                             AudioBackendEventMailbox& mailbox)
    {
        if (static_cast<uint32_t>(desc.OutputBus) >= kAudioBusCount)
        {
            return AudioResult::BackendFailure;
        }
        if (handle.IsValid())
        {
            if (handle.Index >= m_Voices.size())
            {
                m_Voices.resize(static_cast<size_t>(handle.Index) + 1);
            }
            m_Voices[handle.Index] = Voice{handle, &mailbox};
        }
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::SubmitVoice(VoiceHandle handle, Core::Container::Span<const uint8_t>,
                                              const AudioBufferDesc& bufferDesc)
    {
        if (Voice* voice = Resolve(handle))
        {
            voice->bSubmitted = true;
            voice->bLoop = bufferDesc.bLoop;
        }
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::SetVoiceVolume(VoiceHandle, float)
    {
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::SetVoiceFrequencyRatio(VoiceHandle, float)
    {
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::SetVoiceOutputGains(VoiceHandle, float, float)
    {
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::PauseVoice(VoiceHandle)
    {
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::ResumeVoice(VoiceHandle)
    {
        return AudioResult::Success;
    }

    AudioResult NullAudioBackend::StartVoice(VoiceHandle handle)
    {
        if (Voice* voice = Resolve(handle); voice != nullptr && !voice->bLoop)
        {
            PublishDrained(*voice);
        }
        return AudioResult::Success;
    }

    AudioBackendStopResult NullAudioBackend::StopVoice(VoiceHandle handle)
    {
        if (Voice* voice = Resolve(handle))
        {
            // ループもここで終端にし、ドメインの Stopping 状態を回収可能にする。
            PublishDrained(*voice);
        }
        return {AudioResult::Success, true, true};
    }

    void NullAudioBackend::QuiesceCallbacks()
    {
        // 通知は呼び出し元のスレッドだけで発行する。
    }

    AudioResult NullAudioBackend::DestroyVoice(VoiceHandle handle)
    {
        if (Voice* voice = Resolve(handle))
        {
            // 復帰後にメールボックスを参照しない。
            *voice = {};
        }
        return AudioResult::Success;
    }

    void NullAudioBackend::Shutdown()
    {
        m_Voices.clear();
        for (Bus& bus : m_Buses)
        {
            bus = {};
        }
        m_EventSink = nullptr;
        m_ShutdownEpoch = 0;
    }

    NullAudioBackend::Voice* NullAudioBackend::Resolve(VoiceHandle handle)
    {
        if (handle.Index < m_Voices.size() && m_Voices[handle.Index].Handle == handle &&
            m_Voices[handle.Index].Mailbox != nullptr)
        {
            return &m_Voices[handle.Index];
        }
        return nullptr;
    }

    void NullAudioBackend::PublishDrained(Voice& voice)
    {
        if (voice.bSubmitted && m_EventSink != nullptr)
        {
            // PCM は保持せず、既存のメールボックスへ同期的に終端通知だけを渡す。
            voice.bSubmitted = false;
            m_EventSink->EnqueueBackendEvent(*voice.Mailbox, voice.Handle, m_ShutdownEpoch,
                                            AudioBackendEventKind::Drained);
        }
    }
} // namespace NorvesLib::Modules::Audio::Private
