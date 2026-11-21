#include "Audio/AudioListenerComponent.h"

#include "Audio/AudioSpatialHost.h"

namespace NorvesLib::Modules::Audio
{
    IMPLEMENT_CLASS(AudioListenerComponent, Core::Component::Component)

    AudioListenerComponent::AudioListenerComponent()
    {
        Mode = AudioListenerMode::MainCamera;
        Priority = 0;
    }

    void AudioListenerComponent::Initialize()
    {
        if (HasFlag(Core::OF_Initialized))
        {
            return;
        }
        Super::Initialize();
        m_AudioModule = FindAudioModule(Core::Module::GetModuleRegistry());
        if (auto* host = Private::FindAudioSpatialHost(m_AudioModule))
        {
            host->RegisterListener(*this);
        }
    }

    IAudioModule* AudioListenerComponent::FindRegisteredModule() const
    {
        IAudioModule* module = FindAudioModule(Core::Module::GetModuleRegistry());
        return module == m_AudioModule ? module : nullptr;
    }

    void AudioListenerComponent::Finalize()
    {
        if (auto* host = Private::FindAudioSpatialHost(FindRegisteredModule()))
        {
            host->UnregisterListener(*this);
        }
        m_AudioModule = nullptr;
        Super::Finalize();
    }
}
