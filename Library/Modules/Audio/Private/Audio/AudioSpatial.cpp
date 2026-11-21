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

        double DotDouble(const Math::Vector3& a, const Math::Vector3& b) noexcept
        {
            return static_cast<double>(a.x) * static_cast<double>(b.x) +
                   static_cast<double>(a.y) * static_cast<double>(b.y) +
                   static_cast<double>(a.z) * static_cast<double>(b.z);
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
               std::isfinite(settings.MaxDistance) &&
               settings.MaxDistance > std::max(settings.MinDistance, Core::Scene::kAcousticMinDistanceFloor) &&
               std::isfinite(settings.DopplerScale) && settings.DopplerScale >= 0.0f;
    }

    AudioStereoGains ComputeEqualPowerPan(float pan) noexcept
    {
        const float p = std::isnan(pan) ? 0.0f : std::clamp(pan, -1.0f, 1.0f);
        constexpr float quarterPi = Math::Constants::PI / 4.0f;
        return {std::clamp(std::cos((1.0f + p) * quarterPi), 0.0f, 1.0f),
                std::clamp(std::cos((1.0f - p) * quarterPi), 0.0f, 1.0f)};
    }

    float ComputeDopplerRatio(const Math::Vector3& listenerPosition, const Math::Vector3& listenerVelocity,
                              const Math::Vector3& emitterPosition, const Math::Vector3& emitterVelocity,
                              float dopplerScale, float speedOfSound) noexcept
    {
        if (!std::isfinite(dopplerScale) || dopplerScale <= 0.0f || !std::isfinite(speedOfSound) || speedOfSound <= 0.0f ||
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
        const double sourceSpeed = scale * DotDouble(emitterVelocity, direction);
        const double listenerSpeed = scale * DotDouble(listenerVelocity, direction);
        const double numerator = speedOfSound - listenerSpeed;
        const double denominator = speedOfSound - sourceSpeed;
        if (denominator != 0.0)
        {
            const double ratio = numerator / denominator;
            if (std::isfinite(ratio) && ratio > 0.0)
            {
                return static_cast<float>(std::clamp(ratio, static_cast<double>(kAudioMinDopplerRatio),
                                                     static_cast<double>(kAudioMaxDopplerRatio)));
            }
        }
        else if (numerator == 0.0)
        {
            return 1.0f;
        }
        return numerator > 0.0 ? kAudioMaxDopplerRatio : kAudioMinDopplerRatio;
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
        const double right = DotDouble(direction, listener.Right);
        const double forward = DotDouble(direction, listener.Forward);
        const double horizontal = std::hypot(right, forward);
        result.Pan = horizontal < 1.0e-6 ? 0.0f : static_cast<float>(std::clamp(right / horizontal, -1.0, 1.0) * spread);
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
