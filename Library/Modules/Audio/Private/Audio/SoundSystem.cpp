#include "Audio/SoundSystem.h"

#include "Audio/AudioClipResource.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        constexpr float kMinimumFadeSeconds = 2.0f / 60.0f;

        bool IsValidFade(const SoundFade& fade)
        {
            return std::isfinite(fade.Seconds) &&
                   (fade.Curve == SoundFadeCurve::Linear || fade.Curve == SoundFadeCurve::EqualPower);
        }

        bool IsValidVolume(float volume)
        {
            return std::isfinite(volume) && volume >= 0.0f;
        }
    }

    SoundSystem::SoundSystem(IAudioService& service, Random::Generator generator, const SoundSystemConfig& config)
        : m_Service(service), m_Generator(std::move(generator)), m_Config(config)
#if NORVES_BUILD_DEBUG
        , m_OwnerThreadId(Thread::Thread::GetCurrentThreadId())
#endif
    {
    }

    bool SoundSystem::IsOwnerThread() const
    {
#if NORVES_BUILD_DEBUG
        if (m_OwnerThreadId != Thread::Thread::GetCurrentThreadId())
        {
            LOG_ERROR("SoundSystem は構築したスレッドからのみ呼び出せます。");
            return false;
        }
#endif
        return true;
    }

    AudioResult SoundSystem::RegisterDefinition(SoundDefinition definition)
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            return AudioResult::InvalidState;
        }
        if (!definition.Id.IsValid() || definition.Clips.empty() ||
            m_Definitions.find(definition.Id) != m_Definitions.end() ||
            static_cast<uint32_t>(definition.Category) >= kSoundCategoryCount ||
            !std::isfinite(definition.VolumeDb) || !std::isfinite(definition.PitchJitterSemitones))
        {
            return AudioResult::Rejected;
        }
        const SoundId id = definition.Id;
        m_Definitions.emplace(id, std::move(definition));
        return AudioResult::Success;
    }

    AudioResult SoundSystem::UnregisterDefinition(SoundId id)
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            return AudioResult::InvalidState;
        }
        const auto definition = m_Definitions.find(id);
        if (definition == m_Definitions.end())
        {
            return AudioResult::InvalidState;
        }
        for (Instance& instance : m_Instances)
        {
            if (instance.bActive && instance.DefinitionId == id && !instance.bStopVoiceIssued)
            {
                // 登録解除は、進行中の長いフェードも最短の包絡へ置き換える。
                BeginStop(instance, SoundFade{kMinimumFadeSeconds, SoundFadeCurve::Linear});
            }
        }
        m_Definitions.erase(definition);
        return AudioResult::Success;
    }

    AudioResult SoundSystem::PlayGlobal(SoundId id, const SoundPlayParams& params, SoundInstanceHandle& outHandle)
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            outHandle = {};
            return AudioResult::InvalidState;
        }
        return Play(id, Math::Vector3{}, true, params, outHandle);
    }

    AudioResult SoundSystem::PlayAt(SoundId id, const Math::Vector3& position,
                                    const SoundPlayParams& params, SoundInstanceHandle& outHandle)
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            outHandle = {};
            return AudioResult::InvalidState;
        }
        return Play(id, position, false, params, outHandle);
    }

    AudioResult SoundSystem::CheckPlayAdmission(const SoundDefinition&, const Math::Vector3&, bool)
    {
        return AudioResult::Success;
    }

    Core::Container::TSharedPtr<AudioClipResource> SoundSystem::SelectClip(const SoundDefinition& definition)
    {
        return definition.Clips.front();
    }

    AudioResult SoundSystem::Play(SoundId id, const Math::Vector3& position, bool bGlobal,
                                  const SoundPlayParams& params, SoundInstanceHandle& outHandle)
    {
        outHandle = {};
        const auto found = m_Definitions.find(id);
        if (found == m_Definitions.end() || !IsValidVolume(params.Volume) ||
            !std::isfinite(params.PitchSemitones) || !IsValidFade(params.FadeIn) ||
            !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            return AudioResult::InvalidState;
        }
        const SoundDefinition& definition = found->second;
        const AudioResult admission = CheckPlayAdmission(definition, position, bGlobal);
        if (admission != AudioResult::Success)
        {
            return admission;
        }

        Instance instance;
        instance.DefinitionId = id;
        instance.Params = params;
        instance.OwnerKey = params.OwnerKey;
        instance.Position = position;
        instance.bGlobal = bGlobal;
        instance.Category = definition.Category;
        instance.FadeOutMs = definition.FadeOutMs;
        instance.BaseGain = std::pow(10.0f, definition.VolumeDb / 20.0f);
        instance.FadeSeconds = ResolveFadeSeconds(params.FadeIn, definition.FadeInMs, false);
        instance.Gain = instance.FadeSeconds > 0.0f ? 0.0f : 1.0f;
        instance.FadeStartGain = instance.Gain;
        instance.FadeCurve = params.FadeIn.Curve;
        const float volume = GetVoiceVolume(instance);
        if (!IsValidVolume(volume))
        {
            return AudioResult::InvalidState;
        }

        const AudioVoiceDesc desc{definition.bLoop, definition.LoopBeginFrame, definition.LoopLengthFrames, 0,
            std::max(12.0f, params.PitchSemitones + std::abs(definition.PitchJitterSemitones))};
        AudioResult result = m_Service.CreateVoice(SelectClip(definition), desc, instance.Voice);
        if (result == AudioResult::Success)
        {
            result = m_Service.SetVoiceVolume(instance.Voice, volume);
        }
        if (result == AudioResult::Success)
        {
            result = m_Service.SetVoicePitch(instance.Voice, params.PitchSemitones);
        }
        if (result == AudioResult::Success)
        {
            result = m_Service.StartVoice(instance.Voice);
        }
        if (result != AudioResult::Success)
        {
            if (instance.Voice.IsValid())
            {
                m_Service.AbortVoice(instance.Voice);
            }
            return result;
        }

        const uint32_t index = AcquireSlot();
        instance.Generation = m_Instances[index].Generation;
        instance.bActive = true;
        instance.PlaySequence = ++m_PlaySequence;
        instance.LastVoiceVolume = volume;
        m_Instances[index] = std::move(instance);
        ++m_Diagnostics.ActiveInstanceCount;
        outHandle = SoundInstanceHandle{index, m_Instances[index].Generation};
        return AudioResult::Success;
    }

    float SoundSystem::ResolveFadeSeconds(const SoundFade& fade, uint32_t defaultMs, bool bStopping)
    {
        const float requested = fade.Seconds < 0.0f ? static_cast<float>(defaultMs) / 1000.0f : fade.Seconds;
        if ((bStopping || requested > 0.0f) && requested < kMinimumFadeSeconds)
        {
            ++m_Diagnostics.ClampedFadeCount;
            m_Diagnostics.LastClampedFadeRequestedSeconds = requested;
            m_Diagnostics.LastClampedFadeAppliedSeconds = kMinimumFadeSeconds;
            return kMinimumFadeSeconds;
        }
        return requested;
    }

    void SoundSystem::BeginStop(Instance& instance, const SoundFade& fade)
    {
        instance.bStopRequested = true;
        instance.FadeStartGain = instance.Gain;
        instance.FadeSeconds = ResolveFadeSeconds(fade, instance.FadeOutMs, true);
        instance.FadeElapsedSeconds = 0.0;
        instance.FadeCurve = fade.Curve;
    }

    AudioResult SoundSystem::Stop(SoundInstanceHandle handle, const SoundFade& fade)
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            return AudioResult::InvalidState;
        }
        Instance* instance = Resolve(handle);
        if (instance == nullptr)
        {
            return AudioResult::StaleHandle;
        }
        if (instance->bStopRequested)
        {
            return AudioResult::AlreadyInState;
        }
        if (!IsValidFade(fade))
        {
            return AudioResult::InvalidState;
        }
        BeginStop(*instance, fade);
        return AudioResult::Success;
    }

    AudioResult SoundSystem::StopOwner(uint64_t ownerKey, const SoundFade& fade)
    {
        if (!IsOwnerThread() || m_bShutdown || ownerKey == 0 || !IsValidFade(fade))
        {
            return AudioResult::InvalidState;
        }
        for (Instance& instance : m_Instances)
        {
            if (instance.bActive && instance.OwnerKey == ownerKey && !instance.bStopRequested)
            {
                BeginStop(instance, fade);
            }
        }
        return AudioResult::Success;
    }

    float SoundSystem::GetVoiceVolume(const Instance& instance) const
    {
        return instance.BaseGain * instance.Params.Volume * instance.Gain *
               m_CategoryVolumes[static_cast<uint32_t>(instance.Category)] * m_MasterVolume;
    }

    AudioResult SoundSystem::ApplyVolume(Instance& instance)
    {
        const float volume = GetVoiceVolume(instance);
        if (!IsValidVolume(volume))
        {
            return AudioResult::InvalidState;
        }
        if (volume == instance.LastVoiceVolume)
        {
            return AudioResult::Success;
        }
        const AudioResult result = m_Service.SetVoiceVolume(instance.Voice, volume);
        if (result == AudioResult::Success)
        {
            instance.LastVoiceVolume = volume;
        }
        return result;
    }

    void SoundSystem::Update(float deltaSeconds)
    {
        if (!IsOwnerThread() || m_bShutdown || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
        {
            return;
        }
        // 終端通知を先に回収し、破棄済みの物理ボイスにはパラメータを送らない。
        for (uint32_t index = 0; index < m_Instances.size(); ++index)
        {
            Instance& instance = m_Instances[index];
            if (!instance.bActive)
            {
                continue;
            }
            AudioVoiceState state = AudioVoiceState::Destroyed;
            const AudioResult result = m_Service.GetVoiceState(instance.Voice, state);
            if (result == AudioResult::StaleHandle)
            {
                ReleaseSlot(index);
            }
            else if (result == AudioResult::Success && state == AudioVoiceState::Drained)
            {
                const AudioResult destroyed = m_Service.DestroyVoice(instance.Voice);
                if (destroyed == AudioResult::Success || destroyed == AudioResult::StaleHandle)
                {
                    ReleaseSlot(index);
                }
            }
        }
        for (Instance& instance : m_Instances)
        {
            if (!instance.bActive)
            {
                continue;
            }
            instance.AgeSeconds += deltaSeconds;
            if (instance.bStopVoiceIssued)
            {
                continue;
            }
            if (instance.FadeSeconds > 0.0f)
            {
                instance.FadeElapsedSeconds += deltaSeconds;
                const float t = static_cast<float>(std::min(1.0,
                    instance.FadeElapsedSeconds / static_cast<double>(instance.FadeSeconds)));
                if (instance.bStopRequested)
                {
                    instance.Gain = instance.FadeStartGain *
                        (instance.FadeCurve == SoundFadeCurve::EqualPower ? std::sqrt(1.0f - t) : 1.0f - t);
                }
                else if (instance.FadeCurve == SoundFadeCurve::EqualPower)
                {
                    const float startSquared = instance.FadeStartGain * instance.FadeStartGain;
                    instance.Gain = std::sqrt(startSquared + (1.0f - startSquared) * t);
                }
                else
                {
                    instance.Gain = instance.FadeStartGain + (1.0f - instance.FadeStartGain) * t;
                }
            }
            ApplyVolume(instance);
            if (instance.bStopRequested && instance.FadeElapsedSeconds >= instance.FadeSeconds)
            {
                // 失敗時も再送しない。以後の物理寿命はサービスの終端通知と終了処理が持つ。
                instance.bStopVoiceIssued = true;
                m_Service.StopVoice(instance.Voice);
            }
        }
    }

    AudioResult SoundSystem::SetVolume(SoundInstanceHandle handle, float volume)
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            return AudioResult::InvalidState;
        }
        Instance* instance = Resolve(handle);
        if (instance == nullptr)
        {
            return AudioResult::StaleHandle;
        }
        if (!IsValidVolume(volume) || instance->bStopVoiceIssued)
        {
            return AudioResult::InvalidState;
        }
        const float previous = instance->Params.Volume;
        instance->Params.Volume = volume;
        const AudioResult result = ApplyVolume(*instance);
        if (result != AudioResult::Success)
        {
            instance->Params.Volume = previous;
        }
        return result;
    }

    AudioResult SoundSystem::SetPitch(SoundInstanceHandle handle, float semitones)
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            return AudioResult::InvalidState;
        }
        Instance* instance = Resolve(handle);
        if (instance == nullptr)
        {
            return AudioResult::StaleHandle;
        }
        if (!std::isfinite(semitones))
        {
            return AudioResult::InvalidState;
        }
        const AudioResult result = m_Service.SetVoicePitch(instance->Voice, semitones);
        if (result == AudioResult::Success)
        {
            instance->Params.PitchSemitones = semitones;
        }
        return result;
    }

    bool SoundSystem::IsPlaying(SoundInstanceHandle handle) const
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            return false;
        }
        const Instance* instance = Resolve(handle);
        return instance != nullptr && !instance->bStopRequested;
    }

    void SoundSystem::SetMasterVolume(float volume)
    {
        if (!IsOwnerThread() || m_bShutdown || !IsValidVolume(volume))
        {
            return;
        }
        m_MasterVolume = volume;
    }

    float SoundSystem::GetMasterVolume() const
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            return 0.0f;
        }
        return m_MasterVolume;
    }

    void SoundSystem::SetCategoryVolume(SoundCategory category, float volume)
    {
        if (!IsOwnerThread() || m_bShutdown || static_cast<uint32_t>(category) >= kSoundCategoryCount ||
            !IsValidVolume(volume))
        {
            return;
        }
        m_CategoryVolumes[static_cast<uint32_t>(category)] = volume;
    }

    float SoundSystem::GetCategoryVolume(SoundCategory category) const
    {
        if (!IsOwnerThread() || m_bShutdown || static_cast<uint32_t>(category) >= kSoundCategoryCount)
        {
            return 0.0f;
        }
        return m_CategoryVolumes[static_cast<uint32_t>(category)];
    }

    SoundSystemDiagnostics SoundSystem::GetDiagnostics() const
    {
        if (!IsOwnerThread() || m_bShutdown)
        {
            return {};
        }
        SoundSystemDiagnostics diagnostics = m_Diagnostics;
        diagnostics.Voice = m_Service.GetDiagnostics();
        diagnostics.Voice.StolenVoiceCount = m_Diagnostics.Voice.StolenVoiceCount;
        diagnostics.Voice.RejectedPlayCount = m_Diagnostics.Voice.RejectedPlayCount;
        diagnostics.Voice.SuppressedByCooldownCount = m_Diagnostics.Voice.SuppressedByCooldownCount;
        return diagnostics;
    }

    AudioResult SoundSystem::Shutdown()
    {
        if (!IsOwnerThread())
        {
            return AudioResult::InvalidState;
        }
        if (m_bShutdown)
        {
            return AudioResult::AlreadyInState;
        }
        m_bShutdown = true;
        for (uint32_t index = 0; index < m_Instances.size(); ++index)
        {
            if (m_Instances[index].bActive)
            {
                ReleaseSlot(index);
            }
        }
        m_Definitions.clear();
        return AudioResult::Success;
    }

    uint32_t SoundSystem::AcquireSlot()
    {
        if (!m_FreeIndices.empty())
        {
            const uint32_t index = m_FreeIndices.back();
            m_FreeIndices.pop_back();
            return index;
        }
        const uint32_t index = static_cast<uint32_t>(m_Instances.size());
        m_Instances.emplace_back();
        return index;
    }

    void SoundSystem::ReleaseSlot(uint32_t index)
    {
        const uint32_t generation = m_Instances[index].Generation;
        m_Instances[index] = Instance{};
        m_Instances[index].Generation = generation == UINT32_MAX ? 1 : generation + 1;
        m_FreeIndices.push_back(index);
        --m_Diagnostics.ActiveInstanceCount;
    }

    SoundSystem::Instance* SoundSystem::Resolve(SoundInstanceHandle handle)
    {
        return const_cast<Instance*>(std::as_const(*this).Resolve(handle));
    }

    const SoundSystem::Instance* SoundSystem::Resolve(SoundInstanceHandle handle) const
    {
        if (!handle.IsValid() || handle.Index >= m_Instances.size())
        {
            return nullptr;
        }
        const Instance& instance = m_Instances[handle.Index];
        return instance.bActive && instance.Generation == handle.Generation ? &instance : nullptr;
    }

    bool SoundSystemTestAccess::PrepareInstanceGenerationWrap(SoundSystem& system, SoundInstanceHandle& handle)
    {
        if (!system.IsOwnerThread() || system.m_bShutdown)
        {
            return false;
        }
        SoundSystem::Instance* instance = system.Resolve(handle);
        if (instance == nullptr)
        {
            return false;
        }
        instance->Generation = UINT32_MAX;
        handle.Generation = UINT32_MAX;
        return true;
    }

    VoiceHandle SoundSystemTestAccess::GetVoiceHandle(const SoundSystem& system, SoundInstanceHandle handle)
    {
        if (!system.IsOwnerThread() || system.m_bShutdown)
        {
            return {};
        }
        const SoundSystem::Instance* instance = system.Resolve(handle);
        return instance != nullptr ? instance->Voice : VoiceHandle{};
    }
} // namespace NorvesLib::Modules::Audio::Private
