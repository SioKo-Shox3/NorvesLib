#include "Audio/AudioComponent.h"

#include "Audio/IAudioModule.h"
#include "Thread/Atomic.h"

#include <algorithm>

namespace NorvesLib::Modules::Audio
{
    namespace
    {
        constexpr uint64_t kLastComponentOwnerKey = UINT64_MAX >> 1;
        Thread::Atomic<uint64_t> g_NextComponentOwnerKey{1};

        uint64_t AllocateComponentOwnerKey()
        {
            uint64_t next = g_NextComponentOwnerKey.Load();
            while (next <= kLastComponentOwnerKey)
            {
                if (g_NextComponentOwnerKey.CompareExchangeWeak(next, next + 1))
                {
                    return next;
                }
            }
            // キーを使い切ったら折り返さず、以後の再生を拒否する。
            return 0;
        }
    } // namespace

    IMPLEMENT_CLASS(AudioComponent, Core::Component::Component)

    AudioComponent::AudioComponent()
    {
        bAutoPlay = false;
        Volume = 1.0f;
        PitchSemitones = 0.0f;
    }

    IAudioModule* AudioComponent::FindRegisteredModule() const
    {
        IAudioModule* module = FindAudioModule(Core::Module::GetModuleRegistry());
        return module == m_AudioModule ? module : nullptr;
    }

    void AudioComponent::PruneHandles(ISoundSystem& soundSystem)
    {
        m_Handles.erase(std::remove_if(m_Handles.begin(), m_Handles.end(),
            [&soundSystem](SoundInstanceHandle handle) { return !soundSystem.IsPlaying(handle); }),
            m_Handles.end());
    }

    void AudioComponent::Initialize()
    {
        if (HasFlag(Core::OF_Initialized))
        {
            return;
        }
        Super::Initialize();
        m_OwnerKey = AllocateComponentOwnerKey();
        m_AudioModule = FindAudioModule(Core::Module::GetModuleRegistry());
        if (m_AudioModule && m_OwnerKey != 0 && bAutoPlay.Get() && !SoundName.Get().empty())
        {
            SoundPlayParams params;
            params.Volume = Volume.Get();
            params.PitchSemitones = PitchSemitones.Get();
            SoundInstanceHandle handle;
            (void)Play(SoundId(SoundName.Get()), params, handle);
        }
    }

    void AudioComponent::Finalize()
    {
        if (IAudioModule* module = FindRegisteredModule(); module && m_OwnerKey != 0)
        {
            (void)module->GetSoundSystem().ReleaseOwner(m_OwnerKey, SoundFade{});
        }
        m_Handles.clear();
        m_AudioModule = nullptr;
        m_OwnerKey = 0;
        Super::Finalize();
    }

    AudioResult AudioComponent::Play(SoundId id, const SoundPlayParams& params, SoundInstanceHandle& outHandle)
    {
        outHandle = {};
        IAudioModule* module = FindRegisteredModule();
        if (!module || m_OwnerKey == 0)
        {
            return AudioResult::InvalidState;
        }
        ISoundSystem& soundSystem = module->GetSoundSystem();
        PruneHandles(soundSystem);
        SoundPlayParams ownedParams = params;
        ownedParams.OwnerKey = m_OwnerKey;
        const AudioResult result = soundSystem.PlayGlobal(id, ownedParams, outHandle);
        if (result == AudioResult::Success)
        {
            m_Handles.push_back(outHandle);
        }
        return result;
    }

    AudioResult AudioComponent::Stop(SoundInstanceHandle handle, const SoundFade& fade)
    {
        IAudioModule* module = FindRegisteredModule();
        if (!module || m_OwnerKey == 0)
        {
            return AudioResult::InvalidState;
        }
        if (std::find(m_Handles.begin(), m_Handles.end(), handle) == m_Handles.end())
        {
            return AudioResult::StaleHandle;
        }
        return module->GetSoundSystem().Stop(handle, fade);
    }

    AudioResult AudioComponent::StopAll(const SoundFade& fade)
    {
        IAudioModule* module = FindRegisteredModule();
        if (!module || m_OwnerKey == 0)
        {
            return AudioResult::InvalidState;
        }
        ISoundSystem& soundSystem = module->GetSoundSystem();
        PruneHandles(soundSystem);
        return soundSystem.StopOwner(m_OwnerKey, fade);
    }
} // namespace NorvesLib::Modules::Audio
