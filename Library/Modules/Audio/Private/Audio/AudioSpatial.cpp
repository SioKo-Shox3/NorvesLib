#include "Audio/AudioSpatial.h"
#include "Math/MathTypes.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::Audio
{
    namespace
    {
        constexpr float kMinDirectionalDistance = 1.0e-4f;

        bool IsFinite(const Math::Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        struct DirectionAndDistance
        {
            Math::Vector3 Direction;
            float Distance;
        };

        DirectionAndDistance GetDirectionAndDistance(const Math::Vector3& from, const Math::Vector3& to) noexcept
        {
            const Math::Vector3 delta = to - from;
            const float distance = delta.Length();
            if (std::isfinite(distance))
            {
                return {distance < kMinDirectionalDistance ? Math::Vector3::Zero : delta / distance, distance};
            }

            // 有限な座標でも差や長さの二乗が float の上限を超える場合は、倍精度で方向を求める。
            const double x = static_cast<double>(to.x) - from.x;
            const double y = static_cast<double>(to.y) - from.y;
            const double z = static_cast<double>(to.z) - from.z;
            const double length = std::hypot(x, y, z);
            return {{static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length)},
                    static_cast<float>(length)};
        }
    }

    bool IsValidEmitterSpatialSettings(const AudioEmitterSpatialSettings& settings) noexcept
    {
        return std::isfinite(settings.MinDistance) && settings.MinDistance >= 0.0f &&
               std::isfinite(settings.MaxDistance) && settings.MaxDistance > settings.MinDistance &&
               std::isfinite(settings.DopplerScale) && settings.DopplerScale >= 0.0f;
    }

    AudioStereoGains ComputeEqualPowerPan(float pan) noexcept
    {
        const float p = std::isnan(pan) ? 0.0f : std::clamp(pan, -1.0f, 1.0f);
        const float angle = (p + 1.0f) * Math::Constants::PI / 4.0f;
        return {std::cos(angle), std::sin(angle)};
    }

    float ComputeDopplerRatio(const Math::Vector3& listenerPosition, const Math::Vector3& listenerVelocity,
                              const Math::Vector3& emitterPosition, const Math::Vector3& emitterVelocity,
                              float dopplerScale, float speedOfSound) noexcept
    {
        if (!std::isfinite(dopplerScale) || dopplerScale <= 0.0f || !std::isfinite(speedOfSound) ||
            !IsFinite(listenerPosition) || !IsFinite(listenerVelocity) || !IsFinite(emitterPosition) || !IsFinite(emitterVelocity))
        {
            return 1.0f;
        }
        const auto [direction, distance] = GetDirectionAndDistance(emitterPosition, listenerPosition);
        if (distance < kMinDirectionalDistance)
        {
            return 1.0f;
        }
        const double scale = dopplerScale;
        const double sourceSpeed = scale * Math::Vector3::Dot(emitterVelocity, direction);
        const double listenerSpeed = scale * Math::Vector3::Dot(listenerVelocity, direction);
        const double denominator = speedOfSound - sourceSpeed;
        if (denominator <= 1.0e-3)
        {
            return kAudioMaxDopplerRatio;
        }
        const double ratio = (speedOfSound - listenerSpeed) / denominator;
        return std::isfinite(ratio) ? std::clamp(static_cast<float>(ratio), kAudioMinDopplerRatio, kAudioMaxDopplerRatio) : 1.0f;
    }

    AudioSpatialResult ComputeAudioSpatial(const AudioListenerState& listener,
                                           const Math::Vector3& emitterPosition,
                                           const Math::Vector3& emitterVelocity,
                                           const AudioEmitterSpatialSettings& settings) noexcept
    {
        AudioSpatialResult result;
        if (!IsValidEmitterSpatialSettings(settings) || !IsFinite(listener.Position) || !IsFinite(listener.Right) ||
            !IsFinite(listener.Up) || !IsFinite(listener.Forward) || !IsFinite(listener.Velocity) ||
            !IsFinite(emitterPosition) || !IsFinite(emitterVelocity))
        {
            result.DistanceGain = 0.0f;
            result.LeftGain = 0.0f;
            result.RightGain = 0.0f;
            return result;
        }

        const auto [direction, distance] = GetDirectionAndDistance(listener.Position, emitterPosition);
        result.Distance = distance;
        result.DistanceGain = Core::Scene::EvaluateAcousticDistanceGain(
            {settings.MinDistance, settings.MaxDistance, settings.Rolloff}, distance);

        const float spread = settings.MinDistance > 0.0f ? std::clamp(distance / settings.MinDistance, 0.0f, 1.0f) : 1.0f;
        result.Pan = std::clamp(Math::Vector3::Dot(direction, listener.Right), -1.0f, 1.0f) * spread;
        const AudioStereoGains stereo = ComputeEqualPowerPan(result.Pan);
        result.LeftGain = result.DistanceGain * stereo.Left;
        result.RightGain = result.DistanceGain * stereo.Right;

        const float rear = std::max(0.0f, -Math::Vector3::Dot(direction, listener.Forward)) * spread;
        result.RearVolumeScale = std::pow(10.0f, -kAudioRearAttenuationDb * rear / 20.0f);
        result.RearHighFrequencyFactor = 1.0f - (1.0f - kAudioRearHighFrequencyFactor) * rear;
        result.DopplerRatio = ComputeDopplerRatio(listener.Position, listener.Velocity, emitterPosition, emitterVelocity,
                                                  settings.DopplerScale);
        return result;
    }
}
