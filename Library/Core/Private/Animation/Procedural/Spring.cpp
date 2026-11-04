#include "Animation/Procedural/Spring.h"

#include "Math/CriticalDamping.h"
#include <cfloat>
#include <cmath>

namespace NorvesLib::Core::Animation::Procedural
{
    namespace
    {
        constexpr double TwoPi = 6.283185307179586476925286766559;
        constexpr double HalfErrorRoot = 1.6783469900166605;
        constexpr double StepTolerance = 1e-6;

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsValid(const SpringSettings& settings)
        {
            return std::isfinite(settings.FrequencyHz) && settings.FrequencyHz > 0.0f &&
                   std::isfinite(settings.DampingRatio) && settings.DampingRatio > 0.0f &&
                   settings.DampingRatio <= 1.0f;
        }

        bool TryFloat(double value, float& result)
        {
            if (!std::isfinite(value) || std::fabs(value) > FLT_MAX)
            {
                return false;
            }
            result = static_cast<float>(value);
            return true;
        }

        bool TryCriticalStep(float& position, float& velocity, float target, double omega)
        {
            const double halfLife = HalfErrorRoot / omega;
            if (halfLife <= FLT_MAX)
            {
                return Math::TryCriticalDamp(position, velocity, target, static_cast<float>(halfLife),
                                             static_cast<float>(SpringSubstepSeconds));
            }

            // 半減期が float に収まらない場合、時間と値を 2 の累乗で縮める。
            // 速度も時間・値の尺度に合わせ、同じ運動を表すよう変換する。
            double timeScale = 1.0;
            while (halfLife * timeScale > FLT_MAX)
            {
                timeScale *= 0.5;
            }
            double valueScale = 1.0;
            while (std::fabs(static_cast<double>(velocity)) * valueScale / timeScale > FLT_MAX * 0.5)
            {
                valueScale *= 0.5;
            }
            float scaledPosition = static_cast<float>(position * valueScale);
            float scaledVelocity = static_cast<float>(velocity * valueScale / timeScale);
            const float scaledTarget = static_cast<float>(target * valueScale);
            if (!Math::TryCriticalDamp(scaledPosition, scaledVelocity, scaledTarget,
                                       static_cast<float>(halfLife * timeScale),
                                       static_cast<float>(SpringSubstepSeconds * timeScale)))
            {
                return false;
            }
            float nextPosition = 0.0f;
            float nextVelocity = 0.0f;
            if (!TryFloat(static_cast<double>(scaledPosition) / valueScale, nextPosition) ||
                !TryFloat(static_cast<double>(scaledVelocity) * timeScale / valueScale, nextVelocity))
            {
                return false;
            }
            position = nextPosition;
            velocity = nextVelocity;
            return true;
        }

        bool TryOscillatingStep(float& position, float& velocity, float target, double omega, double dampingRatio)
        {
            const double time = SpringSubstepSeconds;
            const double alpha = dampingRatio * omega;
            const double phase = omega * std::sqrt(1.0 - dampingRatio * dampingRatio) * time;
            const double decay = std::exp(-alpha * time);
            if (decay == 0.0)
            {
                position = target;
                velocity = 0.0f;
                return true;
            }

            const double phaseSquared = phase * phase;
            const double sincMinusOne = phase < 0.01
                                            ? phaseSquared * (-1.0 / 6.0 + phaseSquared * (1.0 / 120.0 - phaseSquared / 5040.0))
                                            : std::sin(phase) / phase - 1.0;
            const double sinc = phase < 0.01 ? 1.0 + sincMinusOne : std::sin(phase) / phase;
            const double cosine = std::cos(phase);
            const double error = static_cast<double>(position) - target;
            const double a = alpha * time;
            double nextPosition = 0.0;
            if (omega * time < 0.5)
            {
                // 小さい時間では一次項の相殺を避けて位置変化だけを求める。
                const double criticalResponse =
                    a < 0.01 ? -a * a * (0.5 - a / 3.0 + a * a / 8.0 - a * a * a / 30.0 + a * a * a * a / 144.0)
                             : std::expm1(-a) + a * decay;
                const double halfSine = std::sin(phase * 0.5);
                const double response = criticalResponse + decay * (-2.0 * halfSine * halfSine + a * sincMinusOne);
                nextPosition = position + error * response + static_cast<double>(velocity) * time * decay * sinc;
            }
            else
            {
                nextPosition = target + decay * (error * (cosine + a * sinc) + static_cast<double>(velocity) * time * sinc);
            }
            const double nextVelocity = decay * (velocity * (cosine - a * sinc) - omega * omega * error * time * sinc);
            float resultPosition = 0.0f;
            float resultVelocity = 0.0f;
            if (!TryFloat(nextPosition, resultPosition) || !TryFloat(nextVelocity, resultVelocity))
            {
                return false;
            }
            position = resultPosition;
            velocity = resultVelocity;
            return true;
        }

        bool TryStep(float& position, float& velocity, float target, const SpringSettings& settings)
        {
            const double omega = TwoPi * settings.FrequencyHz;
            if (1.0 - static_cast<double>(settings.DampingRatio) <= 1e-4)
            {
                return TryCriticalStep(position, velocity, target, omega);
            }
            return TryOscillatingStep(position, velocity, target, omega, settings.DampingRatio);
        }

        bool TryStep(Math::Vector3& position, Math::Vector3& velocity, const Math::Vector3& target,
                     const SpringSettings& settings)
        {
            return TryStep(position.x, velocity.x, target.x, settings) &&
                   TryStep(position.y, velocity.y, target.y, settings) &&
                   TryStep(position.z, velocity.z, target.z, settings);
        }

        bool TryNormalize(const Math::Quaternion& value, Math::Quaternion& result)
        {
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) || !std::isfinite(value.w))
            {
                return false;
            }
            const double x = value.x;
            const double y = value.y;
            const double z = value.z;
            const double w = value.w;
            const double lengthSquared = x * x + y * y + z * z + w * w;
            if (lengthSquared == 0.0)
            {
                return false;
            }
            const double inverseLength = 1.0 / std::sqrt(lengthSquared);
            result = Math::Quaternion(static_cast<float>(x * inverseLength), static_cast<float>(y * inverseLength),
                                      static_cast<float>(z * inverseLength), static_cast<float>(w * inverseLength));
            return true;
        }

        bool TryRotationVector(const Math::Quaternion& rotation, const Math::Quaternion& target, Math::Vector3& result)
        {
            const Math::Quaternion inverseTarget(-target.x, -target.y, -target.z, target.w);
            Math::Quaternion difference;
            if (!TryNormalize(rotation * inverseTarget, difference))
            {
                return false;
            }
            // 180 度では符号が同じ回転を表すため、軸の符号も一意に決める。
            if (difference.w < 0.0f ||
                (difference.w == 0.0f && (difference.x < 0.0f ||
                 (difference.x == 0.0f && (difference.y < 0.0f ||
                  (difference.y == 0.0f && difference.z < 0.0f))))))
            {
                difference = Math::Quaternion(-difference.x, -difference.y, -difference.z, -difference.w);
            }
            const double x = difference.x;
            const double y = difference.y;
            const double z = difference.z;
            const double sine = std::sqrt(x * x + y * y + z * z);
            if (sine == 0.0)
            {
                result = Math::Vector3();
                return true;
            }
            const double angle = 2.0 * std::atan2(sine, static_cast<double>(difference.w));
            const double scale = angle / sine;
            result = Math::Vector3(static_cast<float>(x * scale), static_cast<float>(y * scale),
                                   static_cast<float>(z * scale));
            return IsFinite(result);
        }

        bool TryRotationFromVector(const Math::Vector3& vector, const Math::Quaternion& target, Math::Quaternion& result)
        {
            const double x = vector.x;
            const double y = vector.y;
            const double z = vector.z;
            const double angle = std::sqrt(x * x + y * y + z * z);
            const double scale = angle == 0.0 ? 0.5 : std::sin(angle * 0.5) / angle;
            const Math::Quaternion difference(static_cast<float>(x * scale), static_cast<float>(y * scale),
                                              static_cast<float>(z * scale), static_cast<float>(std::cos(angle * 0.5)));
            return TryNormalize(difference * target, result);
        }
    } // namespace

    void SpringStepAccumulator::Reset()
    {
        m_remainderSeconds = 0.0;
    }

    bool SpringStepAccumulator::Advance(double dt, int32_t& outSubsteps, double& outDroppedSeconds)
    {
        if (!std::isfinite(dt) || dt < 0.0)
        {
            return false;
        }
        if (dt == 0.0)
        {
            outSubsteps = 0;
            outDroppedSeconds = 0.0;
            return true;
        }
        const double available = m_remainderSeconds + dt;
        const int32_t steps = available + StepTolerance >= SpringMaxSubsteps * SpringSubstepSeconds
                                  ? SpringMaxSubsteps
                                  : static_cast<int32_t>(std::floor((available + StepTolerance) / SpringSubstepSeconds));
        double remainder = available - steps * SpringSubstepSeconds;
        if (remainder < 0.0)
        {
            remainder = 0.0;
        }
        double dropped = 0.0;
        if (steps == SpringMaxSubsteps && remainder >= SpringSubstepSeconds - StepTolerance)
        {
            dropped = remainder;
            remainder = 0.0;
        }
        m_remainderSeconds = remainder;
        outSubsteps = steps;
        outDroppedSeconds = dropped;
        return true;
    }

    bool ScalarSpring::Reset(float position, float velocity)
    {
        if (!std::isfinite(position) || !std::isfinite(velocity))
        {
            return false;
        }
        m_position = position;
        m_velocity = velocity;
        m_accumulator.Reset();
        return true;
    }

    bool ScalarSpring::Advance(float target, float dt, const SpringSettings& settings, float& outDroppedSeconds)
    {
        if (!std::isfinite(target) || !IsValid(settings))
        {
            return false;
        }
        SpringStepAccumulator accumulator = m_accumulator;
        int32_t steps = 0;
        double dropped = 0.0;
        if (!accumulator.Advance(dt, steps, dropped))
        {
            return false;
        }
        float position = m_position;
        float velocity = m_velocity;
        for (int32_t i = 0; i < steps; ++i)
        {
            if (!TryStep(position, velocity, target, settings))
            {
                return false;
            }
        }
        float droppedSeconds = 0.0f;
        if (!TryFloat(dropped, droppedSeconds))
        {
            return false;
        }
        m_position = position;
        m_velocity = velocity;
        m_accumulator = accumulator;
        outDroppedSeconds = droppedSeconds;
        return true;
    }

    bool VectorSpring::Reset(const Math::Vector3& position, const Math::Vector3& velocity)
    {
        if (!IsFinite(position) || !IsFinite(velocity))
        {
            return false;
        }
        m_position = position;
        m_velocity = velocity;
        m_accumulator.Reset();
        return true;
    }

    bool VectorSpring::Advance(const Math::Vector3& target, float dt, const SpringSettings& settings, float& outDroppedSeconds)
    {
        if (!IsFinite(target) || !IsValid(settings))
        {
            return false;
        }
        SpringStepAccumulator accumulator = m_accumulator;
        int32_t steps = 0;
        double dropped = 0.0;
        if (!accumulator.Advance(dt, steps, dropped))
        {
            return false;
        }
        Math::Vector3 position = m_position;
        Math::Vector3 velocity = m_velocity;
        for (int32_t i = 0; i < steps; ++i)
        {
            if (!TryStep(position, velocity, target, settings))
            {
                return false;
            }
        }
        float droppedSeconds = 0.0f;
        if (!TryFloat(dropped, droppedSeconds))
        {
            return false;
        }
        m_position = position;
        m_velocity = velocity;
        m_accumulator = accumulator;
        outDroppedSeconds = droppedSeconds;
        return true;
    }

    bool AngularSpring::Reset(const Math::Quaternion& rotation, const Math::Vector3& angularVelocity)
    {
        Math::Quaternion normalized;
        if (!IsFinite(angularVelocity) || !TryNormalize(rotation, normalized))
        {
            return false;
        }
        m_rotation = normalized;
        m_angularVelocity = angularVelocity;
        m_accumulator.Reset();
        return true;
    }

    bool AngularSpring::Advance(const Math::Quaternion& target, float dt, const SpringSettings& settings,
                                float& outDroppedSeconds)
    {
        Math::Quaternion normalizedTarget;
        if (!IsValid(settings) || !TryNormalize(target, normalizedTarget))
        {
            return false;
        }
        SpringStepAccumulator accumulator = m_accumulator;
        int32_t steps = 0;
        double dropped = 0.0;
        if (!accumulator.Advance(dt, steps, dropped))
        {
            return false;
        }
        Math::Quaternion rotation = m_rotation;
        Math::Vector3 velocity = m_angularVelocity;
        for (int32_t i = 0; i < steps; ++i)
        {
            Math::Vector3 error;
            if (!TryRotationVector(rotation, normalizedTarget, error) ||
                !TryStep(error, velocity, Math::Vector3(), settings) ||
                !TryRotationFromVector(error, normalizedTarget, rotation))
            {
                return false;
            }
        }
        float droppedSeconds = 0.0f;
        if (!TryFloat(dropped, droppedSeconds))
        {
            return false;
        }
        m_rotation = rotation;
        m_angularVelocity = velocity;
        m_accumulator = accumulator;
        outDroppedSeconds = droppedSeconds;
        return true;
    }
} // namespace NorvesLib::Core::Animation::Procedural
