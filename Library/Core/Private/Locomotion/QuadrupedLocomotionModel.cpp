#include "Locomotion/QuadrupedLocomotionModel.h"
#include "Math/CriticalDamping.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Locomotion
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        bool Finite(const Math::Vector3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        bool Nonnegative(float v)
        {
            return std::isfinite(v) && v >= 0;
        }
        bool Positive(float v)
        {
            return std::isfinite(v) && v > 0;
        }
        bool UnitRange(float v)
        {
            return Nonnegative(v) && v <= 1;
        }
        double PlanarLength(const Math::Vector3& v)
        {
            return std::hypot(static_cast<double>(v.x), static_cast<double>(v.z));
        }
        Math::Vector3 Forward(float yaw)
        {
            return {std::sin(yaw), 0, std::cos(yaw)};
        }
        bool ValidState(const LocomotionState& s)
        {
            return Finite(s.PlanarVelocity) && Nonnegative(s.Speed) && std::isfinite(s.Yaw) &&
                   std::isfinite(s.SmoothedTarget) && std::isfinite(s.TargetDerivative) && Nonnegative(s.GaitElapsed) &&
                   Nonnegative(s.Phase) && s.Phase < 1 && std::isfinite(s.Pitch) && std::isfinite(s.Bank) &&
                   Nonnegative(s.JumpBufferRemaining) && Nonnegative(s.CoyoteRemaining) && Nonnegative(s.AirTime) &&
                   Nonnegative(s.LandingRemaining) && std::isfinite(s.PreviousVerticalVelocity) && s.Gait < 4;
        }
        float DecayTo(float value, float target, float halfLife, float dt)
        {
            if (halfLife == 0)
                return target;
            return static_cast<float>(target + (static_cast<double>(value) - target) *
                                                   std::exp2(-static_cast<double>(dt) / halfLife));
        }
        double IntegrateSpeed(float& speed, double target, double rate, double dt)
        {
            const double start = speed;
            const double difference = target - start;
            if (rate <= 0 || difference == 0)
                return start * dt;
            const double reachTime = std::fabs(difference) / rate;
            const double reach = std::min(dt, reachTime);
            const double acceleration = std::copysign(rate, difference);
            const double finish = reachTime <= dt ? target
                                                  : std::clamp(start + acceleration * reach, std::min(start, target),
                                                               std::max(start, target));
            speed = static_cast<float>(finish);
            return .5 * (start + finish) * reach + finish * (dt - reach);
        }
    } // namespace
    bool QuadrupedLocomotionModel::IsValidParams(const QuadrupedLocomotionParams& p)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto& g = p.Gaits[i];
            if (!Nonnegative(g.SpeedMin) || !Positive(g.SpeedMax) || g.SpeedMin >= g.SpeedMax ||
                !Nonnegative(g.UpThreshold) || !Nonnegative(g.DownThreshold) || !Nonnegative(g.MinDwell) ||
                !Positive(g.Acceleration) || !Positive(g.Deceleration) || !Positive(g.Brake) ||
                !Positive(g.TurnRateCap) || !Positive(g.LateralAcceleration) || !Positive(g.StrideLength))
                return false;
            if (i > 0 && (p.Gaits[i - 1].SpeedMax >= g.SpeedMax || p.Gaits[i - 1].UpThreshold <= g.DownThreshold ||
                          g.SpeedMin >= p.Gaits[i - 1].SpeedMax))
                return false;
        }
        return Nonnegative(p.TargetHalfLife) && Nonnegative(p.PivotSpeed) && Positive(p.PivotEnterAngle) &&
               Nonnegative(p.PivotExitAngle) && p.PivotExitAngle < p.PivotEnterAngle && p.PivotEnterAngle <= Pi &&
               Positive(p.PivotRate) && UnitRange(p.TurnSlowdown) && Positive(p.SlopeLimit) && p.SlopeLimit < Pi * .5 &&
               Nonnegative(p.UphillCoefficient) && Nonnegative(p.DownhillBoost) && UnitRange(p.MinimumSlopeScale) &&
               Nonnegative(p.LeanGain) && Nonnegative(p.MaximumBank) && p.MaximumBank < Pi * .5 &&
               Nonnegative(p.LeanHalfLife) && Positive(p.JumpHeightMin) && Positive(p.JumpHeightMax) &&
               p.JumpHeightMax >= p.JumpHeightMin && Nonnegative(p.CoyoteTime) && Nonnegative(p.JumpBuffer) &&
               UnitRange(p.AirControl) && Positive(p.AirTurnRate) && UnitRange(p.LandingSpeedLoss) &&
               Nonnegative(p.LandingRecovery);
    }
    float QuadrupedLocomotionModel::SlopeSpeedScale(const QuadrupedLocomotionParams& p, const Math::Vector3& normal,
                                                    const Math::Vector3& forward)
    {
        if (!Finite(normal) || !Finite(forward) || normal.y <= 0 || !IsValidParams(p))
            return 0;
        const double tilt =
            std::atan2(std::hypot(static_cast<double>(normal.x), static_cast<double>(normal.z)), normal.y);
        const double grade =
            -(static_cast<double>(normal.x) * forward.x + static_cast<double>(normal.z) * forward.z) / normal.y;
        if (tilt > p.SlopeLimit && grade > 0)
            return 0;
        const double scale = grade >= 0 ? 1 - p.UphillCoefficient * grade : 1 - p.DownhillBoost * grade;
        return static_cast<float>(std::clamp(scale, static_cast<double>(p.MinimumSlopeScale), 1.0 + p.DownhillBoost));
    }
    void QuadrupedLocomotionModel::AcknowledgeJump(LocomotionState& s)
    {
        s.JumpBufferRemaining = 0;
        s.CoyoteRemaining = 0;
        s.bJumpConsumed = true;
    }
    LocomotionStepStatus QuadrupedLocomotionModel::Step(LocomotionState& state, const QuadrupedLocomotionParams& p,
                                                        const LocomotionIntent& intent,
                                                        const LocomotionGroundInfo& ground, float dt,
                                                        LocomotionOutput& out)
    {
        if (!IsValidParams(p) || !ValidState(state) || !Finite(intent.Move) || !Finite(ground.Normal) ||
            !Finite(ground.Velocity) || !Finite(ground.PlatformVelocity) || !Nonnegative(ground.Gravity) ||
            !Nonnegative(dt) || dt > 1 || (ground.bGrounded && ground.Normal.y <= 0))
            return LocomotionStepStatus::InvalidArgument;
        if (!ground.bReady)
            return LocomotionStepStatus::GroundNotReady;
        LocomotionState s = state;
        LocomotionOutput o;
        const double inputLength = PlanarLength(intent.Move);
        const double magnitude = std::min(inputLength, 1.0);
        const bool moving = inputLength > 1e-6;
        const float targetYaw = moving ? static_cast<float>(std::atan2(intent.Move.x, intent.Move.z)) : s.Yaw;
        const double observedSpeed = std::hypot(static_cast<double>(ground.Velocity.x) - ground.PlatformVelocity.x,
                                                static_cast<double>(ground.Velocity.z) - ground.PlatformVelocity.z);
        if (dt > 0)
        {
            o.bLanded = s.bInitialized && !s.bWasGrounded && ground.bGrounded;
            if (o.bLanded)
            {
                s.bJumpConsumed = false;
                const double impact = std::clamp(
                    (static_cast<double>(ground.PlatformVelocity.y) - s.PreviousVerticalVelocity) / 10.0, 0.0, 1.0);
                s.LandingRemaining = static_cast<float>(p.LandingRecovery * impact);
                s.Speed *= static_cast<float>(1 - p.LandingSpeedLoss * impact);
            }
            if (ground.bGrounded && !s.bJumpConsumed)
                s.CoyoteRemaining = p.CoyoteTime;
            if (intent.bJumpPressed)
                s.JumpBufferRemaining = p.JumpBuffer;
            // バッファ0は「今回の押下だけ有効」。コヨーテ0は空中発射を許さない。
            if ((intent.bJumpPressed || s.JumpBufferRemaining > 0) && !s.bJumpConsumed &&
                (ground.bGrounded || s.CoyoteRemaining > 0) && ground.Gravity > 0)
            {
                const double fraction = std::clamp(observedSpeed / p.Gaits[3].SpeedMax, 0.0, 1.0);
                const double height = p.JumpHeightMin + (p.JumpHeightMax - p.JumpHeightMin) * fraction;
                o.JumpSpeed = static_cast<float>(std::sqrt(2 * ground.Gravity * height));
                if (!std::isfinite(o.JumpSpeed))
                    return LocomotionStepStatus::InvalidArgument;
                o.bJumpRequested = true;
            }
            const int count = std::max(1, static_cast<int>(std::ceil(static_cast<double>(dt) * 240)));
            const float h = dt / count;
            double totalYaw = 0, phase = 0;
            for (int step = 0; step < count; ++step)
            {
                const auto& oldGait = p.Gaits[s.Gait];
                if (s.GaitElapsed >= oldGait.MinDwell)
                {
                    uint8_t next = s.Gait;
                    if (s.Gait < 3 && observedSpeed >= oldGait.UpThreshold)
                        ++next;
                    else if (s.Gait > 0 && observedSpeed <= oldGait.DownThreshold)
                        --next;
                    if (next != s.Gait)
                    {
                        s.Gait = next;
                        s.GaitElapsed = 0;
                    }
                }
                s.GaitElapsed = static_cast<float>(std::min(static_cast<double>(s.GaitElapsed) + h, 1e6));
                const auto& gait = p.Gaits[s.Gait];
                const double error = std::remainder(static_cast<double>(targetYaw) - s.Yaw, 2 * Pi);
                if (!moving)
                    s.bPivot = false;
                else if (s.bPivot && std::fabs(error) <= p.PivotExitAngle)
                    s.bPivot = false;
                else if (!s.bPivot && s.Speed < p.PivotSpeed && std::fabs(error) >= p.PivotEnterAngle)
                    s.bPivot = true;
                const float slopeScale = ground.bGrounded ? SlopeSpeedScale(p, ground.Normal, Forward(s.Yaw)) : 1;
                double target = magnitude * (intent.bSprint ? p.Gaits[3].SpeedMax : p.Gaits[1].SpeedMax) * slopeScale;
                if (s.bPivot || s.LandingRemaining > 0)
                    target = 0;
                if (!std::isfinite(target) || target > std::numeric_limits<float>::max() ||
                    !Math::TryCriticalDamp(s.SmoothedTarget, s.TargetDerivative, static_cast<float>(target),
                                           p.TargetHalfLife, h))
                    return LocomotionStepStatus::InvalidArgument;
                const double oldSpeed = s.Speed;
                double cap = s.bPivot ? p.PivotRate
                                      : std::min(static_cast<double>(gait.TurnRateCap),
                                                 gait.LateralAcceleration / std::max(oldSpeed, 1e-6));
                if (!ground.bGrounded)
                    cap = std::min(cap, static_cast<double>(p.AirTurnRate));
                const double turn = moving ? std::clamp(error, -cap * h, cap * h) : 0;
                const double turnFraction = cap > 0 ? std::fabs(turn) / (cap * h) : 0;
                const bool braking = moving && std::cos(error) < -1e-6;
                const double adjustedTarget = braking ? 0
                                                      : std::max(0.0, static_cast<double>(s.SmoothedTarget)) *
                                                            (1 - p.TurnSlowdown * turnFraction * turnFraction);
                const double rate = (braking                    ? gait.Brake
                                     : adjustedTarget > s.Speed ? gait.Acceleration
                                                                : gait.Deceleration) *
                                    (ground.bGrounded ? (adjustedTarget > s.Speed ? slopeScale : 1) : p.AirControl);
                const double distance = IntegrateSpeed(s.Speed, adjustedTarget, rate, h);
                // 区間の平均速度と厳密な円弧方向で変位を積分する。
                const double middle = s.Yaw + turn * .5;
                const double sinc = std::fabs(turn) < 1e-8 ? 1 : std::sin(turn * .5) / (turn * .5);
                o.PlanarDisplacement.x += static_cast<float>(distance * sinc * std::sin(middle));
                o.PlanarDisplacement.z += static_cast<float>(distance * sinc * std::cos(middle));
                s.Yaw = static_cast<float>(std::remainder(static_cast<double>(s.Yaw) + turn, 2 * Pi));
                totalYaw += turn;
                phase += observedSpeed * h / gait.StrideLength;
                const auto forward = Forward(s.Yaw);
                const double pitch = ground.bGrounded ? std::atan2(-(static_cast<double>(ground.Normal.x) * forward.x +
                                                                     static_cast<double>(ground.Normal.z) * forward.z),
                                                                   ground.Normal.y)
                                                      : 0;
                const double bank =
                    std::clamp(std::atan2(s.Speed * turn / h, std::max(ground.Gravity, .001f)) * p.LeanGain,
                               -static_cast<double>(p.MaximumBank), static_cast<double>(p.MaximumBank));
                s.Pitch = DecayTo(s.Pitch, static_cast<float>(pitch), p.LeanHalfLife, h);
                s.Bank = DecayTo(s.Bank, static_cast<float>(bank), p.LeanHalfLife, h);
                s.LandingRemaining = std::max(0.f, s.LandingRemaining - h);
            }
            o.DeltaYaw = static_cast<float>(totalYaw);
            o.TurnRate = o.DeltaYaw / dt;
            o.PhaseDelta = static_cast<float>(phase);
            s.Phase = static_cast<float>(std::fmod(s.Phase + phase, 1.0));
            if (s.Phase >= 1)
                s.Phase = 0;
            s.PlanarVelocity = Forward(s.Yaw) * s.Speed;
            s.JumpBufferRemaining = std::max(0.f, s.JumpBufferRemaining - dt);
            if (!ground.bGrounded)
                s.CoyoteRemaining = std::max(0.f, s.CoyoteRemaining - dt);
            s.AirTime = ground.bGrounded ? 0 : static_cast<float>(std::min(static_cast<double>(s.AirTime) + dt, 1e6));
            s.bWasGrounded = ground.bGrounded;
            s.bInitialized = true;
            s.PreviousVerticalVelocity = ground.Velocity.y;
        }
        o.PlanarVelocity = s.PlanarVelocity;
        o.Yaw = s.Yaw;
        o.Speed = static_cast<float>(observedSpeed);
        o.Gait = s.Gait;
        o.Pitch = s.Pitch;
        o.Bank = s.Bank;
        o.AirTime = s.AirTime;
        o.VerticalVelocity = ground.Velocity.y;
        o.bGrounded = ground.bGrounded;
        for (auto& weight : o.GaitWeights)
            weight = 0;
        unsigned low = 0;
        while (low < 3 && observedSpeed > p.Gaits[low].SpeedMax)
            ++low;
        if (low == 3 || observedSpeed <= p.Gaits[low + 1].SpeedMin)
            o.GaitWeights[low] = 1;
        else
        {
            const double t = std::clamp((observedSpeed - p.Gaits[low + 1].SpeedMin) /
                                            (p.Gaits[low].SpeedMax - p.Gaits[low + 1].SpeedMin),
                                        0.0, 1.0);
            o.GaitWeights[low] = static_cast<float>(1 - t);
            o.GaitWeights[low + 1] = static_cast<float>(t);
        }
        if (!ValidState(s) || !Finite(o.PlanarDisplacement) || !std::isfinite(o.PhaseDelta) || !std::isfinite(o.Speed) ||
            !std::isfinite(o.TurnRate))
            return LocomotionStepStatus::InvalidArgument;
        state = s;
        out = o;
        return LocomotionStepStatus::Success;
    }
} // namespace NorvesLib::Core::Locomotion
