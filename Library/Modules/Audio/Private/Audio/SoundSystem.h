#pragma once

#include "Audio/ISoundSystem.h"
#include "Audio/SoundBank.h"
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
        AudioResult LoadBank(const Core::Asset::AssetSystem& assets,
                             Core::Container::AnsiStringView bankLogicalPath, SoundBankLoadReport& outReport) override;
        AudioResult UnloadBank(Core::Container::AnsiStringView bankLogicalPath) override;
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
            uint8_t Priority = 128;
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

        struct RecentPlay
        {
            double TimeSeconds = 0.0;
            uint64_t Frame = 0;
            Math::Vector3 Position;
            bool bGlobal = true;
        };

        struct DefinitionState
        {
            static constexpr uint32_t RecentPlayCapacity = 8;
            RecentPlay RecentPlays[RecentPlayCapacity];
            uint32_t RecentPlayCount = 0;
            uint32_t NextRecentPlay = 0;
            uint32_t LastClipIndex = UINT32_MAX;
            double LastPlayedSeconds = 0.0;
            bool bHasPlayed = false;
        };

        [[nodiscard]] bool IsOwnerThread() const;
        AudioResult LoadBankFromText(Core::Container::AnsiStringView bankKey,
                                     Core::Container::Span<const uint8_t> jsonBytes,
                                     const SoundBankClipResolver& resolveClip, SoundBankLoadReport& outReport);
        AudioResult Play(SoundId id, const Math::Vector3& position, bool bGlobal,
                         const SoundPlayParams& params, SoundInstanceHandle& outHandle);
        // 上限と抑制の方針は、この入口に集約する。
        AudioResult CheckPlayAdmission(const SoundDefinition& definition, Instance& candidate);
        [[nodiscard]] static bool IsLowerVoiceRank(const Instance& left, const Instance& right);
        AudioResult StealInstance(uint32_t index);
        void RecordPlay(SoundId id, const Math::Vector3& position, bool bGlobal);
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
        Core::Container::UnorderedMap<SoundId, DefinitionState, SoundId::Hasher> m_DefinitionStates;
        Core::Container::VariableArray<Instance> m_Instances;
        Core::Container::UnorderedMap<Core::Identity, Core::Container::VariableArray<SoundId>, Core::Identity::Hasher> m_Banks;
        Core::Container::VariableArray<uint32_t> m_FreeIndices;
        SoundSystemDiagnostics m_Diagnostics;
        float m_MasterVolume = 1.0f;
        float m_CategoryVolumes[kSoundCategoryCount] = {1.0f, 1.0f};
        double m_TimeSeconds = 0.0;
        uint64_t m_Frame = 0;
        uint64_t m_PlaySequence = 0;
        bool m_bShutdown = false;
#if NORVES_BUILD_DEBUG
        Thread::Thread::ThreadId m_OwnerThreadId;
#endif
    };

    class SoundSystemTestAccess
    {
    public:
        // UTF-8 の JSON バイト列から、実際の読み込みと同じ登録経路を通す。
        static AudioResult LoadBankFromText(SoundSystem& system, Core::Container::AnsiStringView bankKey,
                                            Core::Container::Span<const uint8_t> jsonBytes,
                                            const SoundBankClipResolver& resolveClip, SoundBankLoadReport& outReport);
        static bool PrepareInstanceGenerationWrap(SoundSystem& system, SoundInstanceHandle& handle);
        [[nodiscard]] static VoiceHandle GetVoiceHandle(const SoundSystem& system, SoundInstanceHandle handle);
    };
} // namespace NorvesLib::Modules::Audio::Private
