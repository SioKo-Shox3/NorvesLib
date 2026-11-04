#pragma once

#include "Audio/IAudioService.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Math/Vector3.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Modules::Audio
{
    using SoundId = Core::Identity;

    enum class SoundSpatialMode : uint8_t { TwoD, ThreeD };
    enum class SoundInstanceLimitPolicy : uint8_t { StopOldest, RejectNew };
    enum class SoundFadeCurve : uint8_t { Linear, EqualPower };
    enum class SoundCategory : uint8_t { Effect, Ambience };
    inline constexpr uint32_t kSoundCategoryCount = 2;

    struct SoundDefinition
    {
        SoundId Id;
        Core::Container::VariableArray<Core::Container::TSharedPtr<AudioClipResource>> Clips;
        float VolumeDb = 0.0f;
        float VolumeJitterDb = 0.0f;
        float PitchJitterSemitones = 0.0f;
        bool bLoop = false;
        uint32_t LoopBeginFrame = 0;
        uint32_t LoopLengthFrames = 0;
        uint8_t Priority = 128; // 大きいほど残す。
        uint32_t MaxInstances = 0; // 0 は無制限。
        SoundInstanceLimitPolicy LimitPolicy = SoundInstanceLimitPolicy::StopOldest;
        uint32_t CooldownMs = 0;
        uint32_t DedupeWindowMs = 0;
        float DedupeRadius = 0.0f; // 0 は重複抑制なし。
        uint32_t FadeInMs = 0;
        uint32_t FadeOutMs = 0;
        SoundSpatialMode Spatial = SoundSpatialMode::TwoD;
        SoundCategory Category = SoundCategory::Effect;
        float NoiseRadius = 0.0f; // GR75 用に値だけを保持する。
        Core::Container::String SourceRef; // 出所の記録。
    };

    struct SoundInstanceHandle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;
        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Index != InvalidIndex && Generation != 0;
        }

        [[nodiscard]] constexpr bool operator==(const SoundInstanceHandle& other) const noexcept
        {
            return Index == other.Index && Generation == other.Generation;
        }
    };

    struct SoundFade
    {
        float Seconds = -1.0f; // 負は定義の既定値。
        SoundFadeCurve Curve = SoundFadeCurve::Linear;
    };

    struct SoundPlayParams
    {
        float Volume = 1.0f;
        float PitchSemitones = 0.0f;
        SoundFade FadeIn;
        uint64_t OwnerKey = 0; // 0 は持ち主なし。
    };

    struct SoundSystemDiagnostics
    {
        AudioDiagnostics Voice;
        uint32_t ActiveInstanceCount = 0;
        uint64_t SuppressedByDedupeCount = 0;
        uint64_t ClampedFadeCount = 0;
        float LastClampedFadeRequestedSeconds = 0.0f;
        float LastClampedFadeAppliedSeconds = 0.0f;
    };

    /** @brief GameThread 専用の、バックエンドに依存しない論理サウンド層。 */
    class ISoundSystem
    {
    public:
        virtual ~ISoundSystem() = default;
        virtual AudioResult PlayGlobal(SoundId id, const SoundPlayParams& params, SoundInstanceHandle& outHandle) = 0;
        virtual AudioResult PlayAt(SoundId id, const Math::Vector3& position, const SoundPlayParams& params, SoundInstanceHandle& outHandle) = 0;
        virtual AudioResult Stop(SoundInstanceHandle handle, const SoundFade& fade) = 0;
        virtual AudioResult StopOwner(uint64_t ownerKey, const SoundFade& fade) = 0;
        virtual AudioResult SetVolume(SoundInstanceHandle handle, float volume) = 0;
        virtual AudioResult SetPitch(SoundInstanceHandle handle, float semitones) = 0;
        [[nodiscard]] virtual bool IsPlaying(SoundInstanceHandle handle) const = 0;
        virtual void SetMasterVolume(float volume) = 0;
        [[nodiscard]] virtual float GetMasterVolume() const = 0;
        virtual void SetCategoryVolume(SoundCategory category, float volume) = 0;
        [[nodiscard]] virtual float GetCategoryVolume(SoundCategory category) const = 0;
        [[nodiscard]] virtual SoundSystemDiagnostics GetDiagnostics() const = 0;
    };
} // namespace NorvesLib::Modules::Audio
