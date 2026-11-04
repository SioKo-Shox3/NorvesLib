#pragma once

#include "Audio/ISoundSystem.h"
#include "Container/UnorderedMap.h"
#include "Debug/DebugConfig.h"
#include "Random/Random.h"

#if NORVES_BUILD_DEBUG
#include "Thread/Thread.h"
#endif

namespace NorvesLib::Modules::Audio::Private
{
    struct SoundSystemConfig
    {
        uint32_t MaxConcurrentVoices = 32;
    };

    class SoundSystem final : public ISoundSystem
    {
        friend class SoundSystemTestAccess;

    public:
        SoundSystem(IAudioService& service, Random::Generator generator, const SoundSystemConfig& config = {});
        SoundSystem(const SoundSystem&) = delete;
        SoundSystem& operator=(const SoundSystem&) = delete;

        void Update(float deltaSeconds);
        AudioResult Shutdown();
        AudioResult RegisterDefinition(SoundDefinition definition);
        AudioResult UnregisterDefinition(SoundId id);
        AudioResult PlayGlobal(SoundId id, const SoundPlayParams& params, SoundInstanceHandle& outHandle) override;
        AudioResult PlayAt(SoundId id, const Math::Vector3& position, const SoundPlayParams& params, SoundInstanceHandle& outHandle) override;
        AudioResult Stop(SoundInstanceHandle handle, const SoundFade& fade) override;
        AudioResult StopOwner(uint64_t ownerKey, const SoundFade& fade) override;
        AudioResult SetVolume(SoundInstanceHandle handle, float volume) override;
        AudioResult SetPitch(SoundInstanceHandle handle, float semitones) override;
        [[nodiscard]] bool IsPlaying(SoundInstanceHandle handle) const override;
        void SetMasterVolume(float volume) override;
        [[nodiscard]] float GetMasterVolume() const override;
        void SetCategoryVolume(SoundCategory category, float volume) override;
        [[nodiscard]] float GetCategoryVolume(SoundCategory category) const override;
        [[nodiscard]] SoundSystemDiagnostics GetDiagnostics() const override;

    private:
        struct Instance
        {
            uint32_t Generation = 1;
            bool bActive = false;
            bool bStopRequested = false;
            bool bStopVoiceIssued = false;
            bool bGlobal = true;
            VoiceHandle Voice;
            SoundId DefinitionId;
            SoundPlayParams Params;
            uint64_t OwnerKey = 0;
            double AgeSeconds = 0.0;
            uint64_t PlaySequence = 0;
            Math::Vector3 Position;
            SoundCategory Category = SoundCategory::Effect;
            uint32_t FadeOutMs = 0;
            float BaseGain = 1.0f;
            float Gain = 1.0f;
            float FadeStartGain = 1.0f;
            float FadeSeconds = 0.0f;
            double FadeElapsedSeconds = 0.0;
            SoundFadeCurve FadeCurve = SoundFadeCurve::Linear;
            float LastVoiceVolume = 0.0f;
        };

        [[nodiscard]] bool IsOwnerThread() const;
        AudioResult Play(SoundId id, const Math::Vector3& position, bool bGlobal,
                         const SoundPlayParams& params, SoundInstanceHandle& outHandle);
        // 上限と抑制の方針は、この入口に集約する。
        AudioResult CheckPlayAdmission(const SoundDefinition& definition, const Math::Vector3& position, bool bGlobal);
        Core::Container::TSharedPtr<AudioClipResource> SelectClip(const SoundDefinition& definition);
        uint32_t AcquireSlot();
        void ReleaseSlot(uint32_t index);
        Instance* Resolve(SoundInstanceHandle handle);
        const Instance* Resolve(SoundInstanceHandle handle) const;
        float ResolveFadeSeconds(const SoundFade& fade, uint32_t defaultMs, bool bStopping);
        void BeginStop(Instance& instance, const SoundFade& fade);
        [[nodiscard]] float GetVoiceVolume(const Instance& instance) const;
        AudioResult ApplyVolume(Instance& instance);

        IAudioService& m_Service;
        Random::Generator m_Generator;
        SoundSystemConfig m_Config;
        Core::Container::UnorderedMap<SoundId, SoundDefinition, SoundId::Hasher> m_Definitions;
        Core::Container::VariableArray<Instance> m_Instances;
        Core::Container::VariableArray<uint32_t> m_FreeIndices;
        SoundSystemDiagnostics m_Diagnostics;
        float m_MasterVolume = 1.0f;
        float m_CategoryVolumes[kSoundCategoryCount] = {1.0f, 1.0f};
        uint64_t m_PlaySequence = 0;
        bool m_bShutdown = false;
#if NORVES_BUILD_DEBUG
        Thread::Thread::ThreadId m_OwnerThreadId;
#endif
    };

    class SoundSystemTestAccess
    {
    public:
        static bool PrepareInstanceGenerationWrap(SoundSystem& system, SoundInstanceHandle& handle);
        [[nodiscard]] static VoiceHandle GetVoiceHandle(const SoundSystem& system, SoundInstanceHandle handle);
    };
} // namespace NorvesLib::Modules::Audio::Private
