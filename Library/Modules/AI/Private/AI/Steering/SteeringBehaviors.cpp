#include "AI/Steering/SteeringBehaviors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace NorvesLib::Modules::AI::Steering
{
    namespace
    {
        constexpr uint32_t kDefaultWanderSeed = 0x9e3779b9u;

        float FiniteOr(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }

        float ValidLimit(float value, float fallback)
        {
            return std::isfinite(value) && value >= 0.0f ? value : fallback;
        }

        float FiniteFloat(double value)
        {
            constexpr double bound = std::numeric_limits<float>::max();
            return FiniteOr(static_cast<float>(std::clamp(value, -bound, bound)), 0.0f);
        }

        // 両端が有限でも差は float の範囲を超え得るため、引く前に拡張する。
        double Difference(float to, float from)
        {
            return static_cast<double>(to) - from;
        }

        double Length(double x, double y, double z)
        {
            return std::sqrt(x * x + y * y + z * z);
        }

        double HorizontalAngle(const Math::Vector3& from, double toX, double toZ)
        {
            const double cross = from.z * toX - from.x * toZ;
            const double dot = from.x * toX + from.z * toZ;
            // 正反対で外積が符号付き零でも、必ず正の回転を選ぶ。
            return std::atan2(cross == 0.0 ? 0.0 : cross, dot);
        }

        Math::Vector3 RoundVelocity(double x, double y, double z, double speedLimit)
        {
            Math::Vector3 result(FiniteFloat(x), FiniteFloat(y), FiniteFloat(z));
            if (Length(result.x, result.y, result.z) > speedLimit)
            {
                // 成分を float に丸めたことで上限を超えた場合だけ、零側へ 1 段戻す。
                result.x = std::nextafter(result.x, 0.0f);
                result.y = std::nextafter(result.y, 0.0f);
                result.z = std::nextafter(result.z, 0.0f);
            }
            return result;
        }

        Math::Vector3 WithSpeed(double x, double y, double z, double length, double speed)
        {
            if (length == 0.0 || speed == 0.0)
            {
                return {};
            }
            const double scale = speed / length;
            return RoundVelocity(x * scale, y * scale, z * scale, speed);
        }

        Math::Vector3 ArriveOffset(double x, double y, double z, const SteeringLimits& limits,
                                   const ArriveParams& params)
        {
            const ArriveParams defaults;
            const double radius = std::max(0.0f, FiniteOr(params.StopRadius, defaults.StopRadius));
            const double distance = Length(x, y, z);
            if (distance <= radius || limits.MaxAcceleration == 0.0f)
            {
                return {};
            }
            const double braking = std::max(0.0f, FiniteOr(params.BrakingFactor, defaults.BrakingFactor));
            const double speed = std::sqrt(2.0 * limits.MaxAcceleration * braking * (distance - radius));
            return WithSpeed(x, y, z, distance, std::min<double>(limits.MaxSpeed, speed));
        }
    }

    bool IsFiniteVector(const Math::Vector3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    Math::Vector3 SanitizeVector(const Math::Vector3& v)
    {
        return IsFiniteVector(v) ? v : Math::Vector3{};
    }

    Math::Vector3 ToPlane(const Math::Vector3& v, SteeringPlane plane)
    {
        Math::Vector3 result = SanitizeVector(v);
        if (plane == SteeringPlane::Ground)
        {
            result.y = 0.0f;
        }
        return result;
    }

    float PlanarLength(const Math::Vector3& v)
    {
        const Math::Vector3 clean = SanitizeVector(v);
        return FiniteFloat(Length(clean.x, 0.0, clean.z));
    }

    Math::Vector3 SafeNormalize(const Math::Vector3& v, const Math::Vector3& fallback)
    {
        const Math::Vector3 clean = SanitizeVector(v);
        const double length = Length(clean.x, clean.y, clean.z);
        if (length < 1e-6f)
        {
            return SanitizeVector(fallback);
        }
        return WithSpeed(clean.x, clean.y, clean.z, length, 1.0);
    }

    Math::Vector3 ClampLength(const Math::Vector3& v, float maxLength)
    {
        const Math::Vector3 clean = SanitizeVector(v);
        const double limit = ValidLimit(maxLength, 0.0f);
        const double length = Length(clean.x, clean.y, clean.z);
        return length > limit ? WithSpeed(clean.x, clean.y, clean.z, length, limit) : clean;
    }

    SteeringLimits SanitizeLimits(const SteeringLimits& limits)
    {
        SteeringLimits result;
        result.MaxSpeed = ValidLimit(limits.MaxSpeed, result.MaxSpeed);
        result.MaxAcceleration = ValidLimit(limits.MaxAcceleration, result.MaxAcceleration);
        for (int i = 0; i < 4; ++i)
        {
            result.TurnRateBySpeed[i] = ValidLimit(limits.TurnRateBySpeed[i], result.TurnRateBySpeed[i]);
        }
        return result;
    }

    float EvaluateTurnRate(const SteeringLimits& limits, float speed)
    {
        const SteeringLimits clean = SanitizeLimits(limits);
        if (clean.MaxSpeed == 0.0f)
        {
            return clean.TurnRateBySpeed[0];
        }
        const double ratio = std::clamp(static_cast<double>(FiniteOr(speed, 0.0f)) / clean.MaxSpeed, 0.0, 1.0);
        const double key = ratio * 3.0;
        const int left = std::min(static_cast<int>(key), 2);
        return FiniteFloat(std::lerp<double>(clean.TurnRateBySpeed[left], clean.TurnRateBySpeed[left + 1], key - left));
    }

    Math::Vector3 Seek(const SteeringKinematics& self, const Math::Vector3& target,
                       const SteeringLimits& limits, SteeringPlane plane)
    {
        const Math::Vector3 from = ToPlane(self.Position, plane);
        const Math::Vector3 to = ToPlane(target, plane);
        const double x = Difference(to.x, from.x);
        const double y = Difference(to.y, from.y);
        const double z = Difference(to.z, from.z);
        const double distance = Length(x, y, z);
        return distance < 1e-4f ? Math::Vector3{} : WithSpeed(x, y, z, distance, SanitizeLimits(limits).MaxSpeed);
    }

    Math::Vector3 Flee(const SteeringKinematics& self, const Math::Vector3& threat,
                       const SteeringLimits& limits, SteeringPlane plane)
    {
        const Math::Vector3 from = ToPlane(threat, plane);
        const Math::Vector3 to = ToPlane(self.Position, plane);
        const double x = Difference(to.x, from.x);
        const double y = Difference(to.y, from.y);
        const double z = Difference(to.z, from.z);
        const double distance = Length(x, y, z);
        const float speed = SanitizeLimits(limits).MaxSpeed;
        if (distance > 0.0)
        {
            return WithSpeed(x, y, z, distance, speed);
        }
        const Math::Vector3 velocity = ToPlane(self.Velocity, plane);
        const double length = Length(velocity.x, velocity.y, velocity.z);
        return length > 0.0 ? WithSpeed(velocity.x, velocity.y, velocity.z, length, speed)
                            : Math::Vector3(0.0f, 0.0f, speed);
    }

    Math::Vector3 Arrive(const SteeringKinematics& self, const Math::Vector3& target,
                         const SteeringLimits& limits, const ArriveParams& params, SteeringPlane plane)
    {
        const Math::Vector3 from = ToPlane(self.Position, plane);
        const Math::Vector3 to = ToPlane(target, plane);
        return ArriveOffset(Difference(to.x, from.x), Difference(to.y, from.y), Difference(to.z, from.z),
                            SanitizeLimits(limits), params);
    }

    Math::Vector3 Pursue(const SteeringKinematics& self, const SteeringKinematics& target,
                         const SteeringLimits& limits, const PursueParams& pursueParams,
                         const ArriveParams& arriveParams, SteeringPlane plane)
    {
        const SteeringLimits clean = SanitizeLimits(limits);
        const Math::Vector3 from = ToPlane(self.Position, plane);
        const Math::Vector3 to = ToPlane(target.Position, plane);
        const Math::Vector3 velocity = ToPlane(target.Velocity, plane);
        const double x = Difference(to.x, from.x);
        const double y = Difference(to.y, from.y);
        const double z = Difference(to.z, from.z);
        const double horizon = std::max(0.0f, FiniteOr(pursueParams.MaxPredictionTime, PursueParams{}.MaxPredictionTime));
        const double t = std::min(horizon, Length(x, y, z) / std::max<double>(clean.MaxSpeed, 0.1));
        return ArriveOffset(x + velocity.x * t, y + velocity.y * t, z + velocity.z * t, clean, arriveParams);
    }

    Math::Vector3 KeepDistance(const SteeringKinematics& self, const SteeringKinematics& target,
                               const SteeringLimits& limits, const KeepDistanceParams& params,
                               KeepDistanceState& state, SteeringPlane plane)
    {
        const SteeringLimits clean = SanitizeLimits(limits);
        const KeepDistanceParams defaults;
        const double minDistance = std::max(0.0f, FiniteOr(params.MinDistance, defaults.MinDistance));
        const double maxDistance = std::max<double>(minDistance + 0.1, FiniteOr(params.MaxDistance, defaults.MaxDistance));
        const double fraction = std::clamp(FiniteOr(params.HysteresisFraction, defaults.HysteresisFraction), 0.0f, 0.5f);
        const double margin = fraction * (maxDistance - minDistance);
        const Math::Vector3 position = ToPlane(self.Position, plane);
        const Math::Vector3 center = ToPlane(target.Position, plane);
        const double x = Difference(position.x, center.x);
        const double z = Difference(position.z, center.z);
        const double distance = Length(x, 0.0, z);

        const KeepDistanceMode previous = state.bInitialized ? state.Mode : KeepDistanceMode::Hold;
        if (distance > maxDistance ||
            (previous == KeepDistanceMode::Approach && distance >= minDistance && distance > maxDistance - margin))
        {
            state.Mode = KeepDistanceMode::Approach;
        }
        else if (distance < minDistance ||
                 (previous == KeepDistanceMode::Retreat && distance < minDistance + margin))
        {
            state.Mode = KeepDistanceMode::Retreat;
        }
        else
        {
            state.Mode = KeepDistanceMode::Hold;
        }
        state.bInitialized = true;
        if (state.Mode == KeepDistanceMode::Hold)
        {
            return ClampLength(ToPlane(target.Velocity, plane), clean.MaxSpeed);
        }

        // 帯は水平距離で定義する。飛行中は水平半径を保つ点の高さを目標に合わせる。
        const double offset = (minDistance + maxDistance) * 0.5 - distance;
        double dirX = 0.0;
        double dirZ = 1.0;
        if (distance > 0.0)
        {
            dirX = x / distance;
            dirZ = z / distance;
        }
        else
        {
            const Math::Vector3 velocity = ToPlane(self.Velocity, SteeringPlane::Ground);
            const double speed = Length(velocity.x, 0.0, velocity.z);
            if (speed > 0.0)
            {
                dirX = -velocity.x / speed;
                dirZ = -velocity.z / speed;
            }
        }
        return ArriveOffset(dirX * offset, Difference(center.y, position.y), dirZ * offset, clean, {});
    }

    void InitializeWander(WanderState& state, uint32_t seed)
    {
        state.Rng = seed == 0 ? kDefaultWanderSeed : seed;
        state.Angle = 0.0f;
        state.bInitialized = true;
    }

    Math::Vector3 Wander(const SteeringKinematics& self, const SteeringLimits& limits, const WanderParams& params,
                         WanderState& state, float dt, SteeringPlane plane)
    {
        if (!state.bInitialized)
        {
            InitializeWander(state, 0);
        }
        const WanderParams defaults;
        state.Angle = FiniteOr(state.Angle, 0.0f);
        if (std::isfinite(dt) && dt > 0.0f)
        {
            uint32_t x = state.Rng == 0 ? kDefaultWanderSeed : state.Rng;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            state.Rng = x;
            const float u = static_cast<float>(x >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f;
            const double jitter = std::max(0.0f, FiniteOr(params.JitterRate, defaults.JitterRate));
            state.Angle = FiniteFloat(std::remainder(state.Angle + u * jitter * dt, 2.0 * std::numbers::pi));
        }

        const Math::Vector3 heading = SafeNormalize(ToPlane(self.Velocity, SteeringPlane::Ground), {0.0f, 0.0f, 1.0f});
        const double cosine = std::cos(static_cast<double>(state.Angle));
        const double sine = std::sin(static_cast<double>(state.Angle));
        const double distance = std::max(0.0f, FiniteOr(params.CircleDistance, defaults.CircleDistance));
        const double radius = std::max(0.0f, FiniteOr(params.CircleRadius, defaults.CircleRadius));
        // 自分の位置を原点として計算し、大きなワールド座標でも円上の相対位置を保つ。
        const double x = heading.x * distance + (heading.x * cosine + heading.z * sine) * radius;
        const double z = heading.z * distance + (heading.z * cosine - heading.x * sine) * radius;
        const double fraction = std::clamp(FiniteOr(params.SpeedFraction, defaults.SpeedFraction), 0.0f, 1.0f);
        const double speed = SanitizeLimits(limits).MaxSpeed * fraction;
        return ToPlane(WithSpeed(x, 0.0, z, Length(x, 0.0, z), speed), plane);
    }

    Math::Vector3 ApplySteeringLimits(const Math::Vector3& currentVelocity, const Math::Vector3& desiredVelocity,
                                      const SteeringLimits& limits, float dt, SteeringPlane plane)
    {
        const SteeringLimits clean = SanitizeLimits(limits);
        const Math::Vector3 current = ClampLength(ToPlane(currentVelocity, plane), clean.MaxSpeed);
        if (!std::isfinite(dt) || dt <= 0.0f || clean.MaxAcceleration == 0.0f)
        {
            return current;
        }
        const Math::Vector3 desired = ClampLength(ToPlane(desiredVelocity, plane), clean.MaxSpeed);
        const double currentXZ = Length(current.x, 0.0, current.z);
        const double desiredXZ = Length(desired.x, 0.0, desired.z);
        double targetX = desired.x;
        double targetZ = desired.z;
        double maxTurn = std::numbers::pi;
        if (currentXZ > 1e-3f && desiredXZ > 1e-3f)
        {
            const double angle = HorizontalAngle(current, desired.x, desired.z);
            const float speed = FiniteFloat(Length(current.x, current.y, current.z));
            maxTurn = static_cast<double>(EvaluateTurnRate(clean, speed)) * dt;
            if (std::abs(angle) > maxTurn)
            {
                const double turn = std::copysign(maxTurn, angle);
                const double cosine = std::cos(turn);
                const double sine = std::sin(turn);
                const double ratio = desiredXZ / currentXZ;
                targetX = (current.x * cosine + current.z * sine) * ratio;
                targetZ = (current.z * cosine - current.x * sine) * ratio;
            }
        }

        const double dx = targetX - current.x;
        const double dy = Difference(desired.y, current.y);
        const double dz = targetZ - current.z;
        const double deltaLength = Length(dx, dy, dz);
        const double maxDelta = static_cast<double>(clean.MaxAcceleration) * dt;
        const double step = deltaLength <= maxDelta ? 1.0 : maxDelta / deltaLength;
        double fraction = step;
        double retreat = std::numeric_limits<float>::epsilon();
        while (fraction > 0.0)
        {
            const Math::Vector3 result = RoundVelocity(std::lerp<double>(current.x, targetX, fraction),
                                                       std::lerp<double>(current.y, desired.y, fraction),
                                                       std::lerp<double>(current.z, targetZ, fraction), clean.MaxSpeed);
            const double change = Length(Difference(result.x, current.x), Difference(result.y, current.y),
                                         Difference(result.z, current.z));
            if (change <= maxDelta &&
                (maxTurn >= std::numbers::pi || std::abs(HorizontalAngle(current, result.x, result.z)) <= maxTurn))
            {
                return result;
            }
            // 上限ぎりぎりの速度が float への丸めで上限を越えたら、同じ線分上で歩幅を戻す。
            // 有効な変化が表現できない刻みでは、現在速度をそのまま保つ。
            fraction = step * (1.0 - retreat);
            retreat *= 4.0;
        }
        return current;
    }
}
