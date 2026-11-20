#pragma once

#include "Math/Vector3.h"
#include "Scene/AcousticDistance.h"

namespace NorvesLib::Modules::Audio
{
    inline constexpr float kSpeedOfSoundMetersPerSecond = 343.0f;
    inline constexpr float kAudioMinDopplerRatio = 0.5f;
    inline constexpr float kAudioMaxDopplerRatio = 2.0f;
    inline constexpr float kAudioRearAttenuationDb = 6.0f; // 真後ろでの音量減衰 (dB)。
    inline constexpr float kAudioRearHighFrequencyFactor = 0.5f; // 真後ろでの高域の係数。
    inline constexpr float kAudioSpatialParameterEpsilon = 1.0e-4f; // 音声パラメータの変更を通知する差の下限。

    enum class AudioSpatialization : uint8_t
    {
        None,
        World3D
    };

    enum class AudioOcclusionMode : uint8_t
    {
        None,
        Evaluator
    };

    /** リスナーの位置・基底・速度。既定の基底は CameraProxy と同じ右 +X、上 +Y、前 −Z。 */
    struct AudioListenerState
    {
        Math::Vector3 Position{0.0f, 0.0f, 0.0f};
        Math::Vector3 Right{1.0f, 0.0f, 0.0f};
        Math::Vector3 Up{0.0f, 1.0f, 0.0f};
        Math::Vector3 Forward{0.0f, 0.0f, -1.0f};
        Math::Vector3 Velocity{0.0f, 0.0f, 0.0f};
    };

    struct AudioEmitterSpatialSettings
    {
        float MinDistance = 1.0f;
        float MaxDistance = 50.0f;
        Core::Scene::AcousticRolloff Rolloff = Core::Scene::AcousticRolloff::InverseClamped;
        float DopplerScale = 1.0f;
        AudioOcclusionMode OcclusionMode = AudioOcclusionMode::None;
    };

    struct AudioStereoGains
    {
        float Left = 0.70710678f;
        float Right = 0.70710678f;
    };

    struct AudioSpatialResult
    {
        float Distance = 0.0f;
        float DistanceGain = 1.0f;
        float Pan = 0.0f; // 近距離の広がりを反映した −1 (左) から 1 (右) の値。
        float LeftGain = 0.70710678f; // 距離利得を掛けた等パワーの左右利得。
        float RightGain = 0.70710678f;
        float RearVolumeScale = 1.0f; // 音量に掛ける背後減衰の倍率。
        float RearHighFrequencyFactor = 1.0f; // 1 で高域の減衰なし。
        float DopplerRatio = 1.0f;
    };

    /** Min、Max、DopplerScale が有限で、0 ≤ Min < Max かつ DopplerScale ≥ 0 なら有効。 */
    [[nodiscard]] bool IsValidEmitterSpatialSettings(const AudioEmitterSpatialSettings& settings) noexcept;

    /** p = clamp(pan, −1, 1)（NaN は 0）、θ = (p + 1)π/4 として左右に (cos θ, sin θ) を返す。 */
    [[nodiscard]] AudioStereoGains ComputeEqualPowerPan(float pan) noexcept;

    /**
     * 音源からリスナーへの単位方向に投影した速度に dopplerScale を掛け、音源を vs、リスナーを vl とする。
     * (c − vl)/(c − vs) を [0.5, 2] に収める。c − vs ≤ 0.001 なら 2、距離が 0.0001 m 未満なら 1。
     */
    [[nodiscard]] float ComputeDopplerRatio(const Math::Vector3& listenerPosition, const Math::Vector3& listenerVelocity,
                                            const Math::Vector3& emitterPosition, const Math::Vector3& emitterVelocity,
                                            float dopplerScale, float speedOfSound = kSpeedOfSoundMetersPerSecond) noexcept;

    /**
     * リスナーから音源への単位方向 n と s = clamp(d/Min, 0, 1)（Min が 0 なら 1）から定位を求める。
     * パンは s·clamp(n·Right, −1, 1)、左右利得は距離利得と等パワーパンの積。距離が 0.0001 m 未満なら無指向とする。
     * 背後の度合い r = s·max(0, −n·Forward) に対し音量は 10^(−6r/20)、高域の係数は 1 − 0.5r。
     * 周波数比は ComputeDopplerRatio で求める。無効な入力では左右を無音、背後の係数と周波数比を 1 とする。
     */
    [[nodiscard]] AudioSpatialResult ComputeAudioSpatial(const AudioListenerState& listener,
                                                        const Math::Vector3& emitterPosition,
                                                        const Math::Vector3& emitterVelocity,
                                                        const AudioEmitterSpatialSettings& settings) noexcept;
}
