// しがみつきの位置拘束と荷重から顎のゲージの変化を求める。
#include "Gameplay/Grip/GripSolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Modules::Gameplay::Grip
{
    namespace
    {
        using Math::Vector3;

        bool IsFinite(const Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsValidTuning(const GripTuning& tuning)
        {
            return std::isfinite(tuning.FreeLoadG) && tuning.FreeLoadG > 0.0f &&
                   std::isfinite(tuning.PanicLoadG) && tuning.PanicLoadG > tuning.FreeLoadG &&
                   std::isfinite(tuning.BaseDrainPerSecond) && tuning.BaseDrainPerSecond >= 0.0f &&
                   std::isfinite(tuning.LoadDrainPerSecond) && tuning.LoadDrainPerSecond >= 0.0f &&
                   std::isfinite(tuning.RecoveryPerSecond) && tuning.RecoveryPerSecond >= 0.0f &&
                   std::isfinite(tuning.SmoothingSeconds) && tuning.SmoothingSeconds >= 0.0f &&
                   std::isfinite(tuning.TensionWeight) && tuning.TensionWeight >= 0.0f &&
                   std::isfinite(tuning.AccelerationWeight) && tuning.AccelerationWeight >= 0.0f &&
                   (tuning.TensionWeight > 0.0f || tuning.AccelerationWeight > 0.0f) &&
                   tuning.MaxSubsteps >= 1 && tuning.MaxSubsteps <= kGripHardMaxSubsteps &&
                   std::isfinite(tuning.MaxLoadG) && tuning.MaxLoadG >= tuning.PanicLoadG &&
                   std::isfinite(tuning.SwingDampingPerSecond) && tuning.SwingDampingPerSecond >= 0.0f;
        }

        // 小さいベクトルも既存の正規化関数で単位長にできるよう尺度を整える。
        float VectorScale(const Vector3& value)
        {
            const float largest = std::max({std::fabs(value.x), std::fabs(value.y), std::fabs(value.z)});
            if (largest == 0.0f)
            {
                return 0.0f;
            }
            const float maxFloat = std::numeric_limits<float>::max();
            return largest < 1.0f / maxFloat ? maxFloat : 1.0f / largest;
        }

        double VectorLength(const Vector3& value)
        {
            const float scale = VectorScale(value);
            return scale == 0.0f ? 0.0 : double((value * scale).Length()) / double(scale);
        }

        Vector3 NormalizeDirection(const Vector3& value)
        {
            return (value * VectorScale(value)).Normalized();
        }

        bool IsRepresentableFloat(double value)
        {
            return std::isfinite(value) && std::fabs(value) <= double(std::numeric_limits<float>::max());
        }

        bool TryScaleVector(const Vector3& value, double scale, Vector3& out)
        {
            const double x = double(value.x) * scale;
            const double y = double(value.y) * scale;
            const double z = double(value.z) * scale;
            if (!IsRepresentableFloat(x) || !IsRepresentableFloat(y) || !IsRepresentableFloat(z))
            {
                return false;
            }
            out = Vector3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            return true;
        }
    } // namespace

    EGripSolverResult StepGripSolver(const GripSolverInput& input, GripBodyState& inOutBody, GripSolverStep& outStep)
    {
        if (!IsFinite(input.GripPrevious) || !IsFinite(input.GripCurrent) ||
            !IsFinite(inOutBody.Position) || !IsFinite(inOutBody.Velocity) || !IsFinite(input.Gravity) ||
            !std::isfinite(input.Length) || input.Length <= 0.0f ||
            !std::isfinite(input.DeltaSeconds) || input.DeltaSeconds <= 0.0f ||
            !std::isfinite(input.SwingDampingPerSecond) || input.SwingDampingPerSecond < 0.0f ||
            input.MaxSubsteps < 1 || input.MaxSubsteps > kGripHardMaxSubsteps)
        {
            return EGripSolverResult::InvalidInput;
        }

        const float delta = std::min(input.DeltaSeconds, kGripMaxDeltaSeconds);
        const float required = std::ceil(delta / kGripTargetSubstepSeconds - 1.0e-4f);
        const uint32_t substeps = std::min(std::max(static_cast<uint32_t>(required), uint32_t(1)), input.MaxSubsteps);
        const float h = delta / static_cast<float>(substeps);
        const double inverseH = 1.0 / double(h);
        const double inverseH2 = inverseH * inverseH;
        const float h2 = static_cast<float>(double(h) * double(h));
        const float damping = std::exp(-input.SwingDampingPerSecond * h);

        GripBodyState body = inOutBody;
        GripSolverStep step;
        step.Substeps = substeps;
        step.SubstepSeconds = h;
        double sumLoadG = 0.0;
        for (uint32_t i = 0; i < substeps; ++i)
        {
            const float previousFraction = static_cast<float>(i) / static_cast<float>(substeps);
            const float currentFraction = static_cast<float>(i + 1) / static_cast<float>(substeps);
            const Vector3 gripPrevious = i == 0 ? input.GripPrevious :
                Vector3::Lerp(input.GripPrevious, input.GripCurrent, previousFraction);
            const Vector3 gripCurrent = i + 1 == substeps ? input.GripCurrent :
                Vector3::Lerp(input.GripPrevious, input.GripCurrent, currentFraction);
            if (!IsFinite(gripPrevious) || !IsFinite(gripCurrent))
            {
                return EGripSolverResult::InvalidInput;
            }

            const Vector3 gripDelta = gripCurrent - gripPrevious;
            Vector3 gripVelocity;
            const Vector3 predicted = body.Position + body.Velocity * h + input.Gravity * h2;
            if (!IsFinite(gripDelta) || !TryScaleVector(gripDelta, inverseH, gripVelocity) || !IsFinite(predicted))
            {
                return EGripSolverResult::InvalidInput;
            }

            Vector3 direction = predicted - gripCurrent;
            if (!IsFinite(direction))
            {
                return EGripSolverResult::InvalidInput;
            }
            if (VectorLength(direction) < 1.0e-6 * double(input.Length))
            {
                direction = body.Position - gripCurrent;
                if (!IsFinite(direction))
                {
                    return EGripSolverResult::InvalidInput;
                }
                if (direction == Vector3::Zero)
                {
                    direction = input.Gravity * -1.0f;
                    if (direction == Vector3::Zero)
                    {
                        direction = Vector3::UnitY;
                    }
                }
            }

            const Vector3 newPosition = gripCurrent + NormalizeDirection(direction) * input.Length;
            const Vector3 correction = newPosition - predicted;
            if (!IsFinite(newPosition) || !IsFinite(correction))
            {
                return EGripSolverResult::InvalidInput;
            }
            const double loadG = VectorLength(correction) * inverseH2 / double(kGripStandardGravity);
            Vector3 tension;
            Vector3 velocity;
            Vector3 normal;
            const Vector3 displacement = newPosition - body.Position;
            const Vector3 offset = newPosition - gripCurrent;
            if (!IsRepresentableFloat(loadG) || !IsFinite(displacement) || !IsFinite(offset) ||
                !TryScaleVector(correction, inverseH2, tension) ||
                !TryScaleVector(displacement, inverseH, velocity) ||
                !TryScaleVector(offset, 1.0 / double(input.Length), normal))
            {
                return EGripSolverResult::InvalidInput;
            }

            const Vector3 relativeVelocity = velocity - gripVelocity;
            const Vector3 radialVelocity = normal * Vector3::Dot(normal, relativeVelocity);
            body.Velocity = gripVelocity + radialVelocity + (relativeVelocity - radialVelocity) * damping;
            body.Position = newPosition;
            step.MeanTensionPerMass += tension * (1.0f / static_cast<float>(substeps));
            sumLoadG += loadG;
            step.PeakLoadG = std::max(step.PeakLoadG, static_cast<float>(loadG));
            if (!IsFinite(body.Velocity) || !IsFinite(step.MeanTensionPerMass))
            {
                return EGripSolverResult::InvalidInput;
            }
        }

        const Vector3 finalOffset = body.Position - input.GripCurrent;
        if (!IsFinite(finalOffset))
        {
            return EGripSolverResult::InvalidInput;
        }
        const double residual = std::fabs(VectorLength(finalOffset) - double(input.Length));
        if (!IsRepresentableFloat(residual))
        {
            return EGripSolverResult::InvalidInput;
        }
        step.MeanLoadG = static_cast<float>(sumLoadG / double(substeps));
        step.ResidualMeters = static_cast<float>(residual);
        inOutBody = body;
        outStep = step;
        return EGripSolverResult::Ok;
    }

    float SmoothGripLoadG(float previousSmoothedLoadG, float rawLoadG, float deltaSeconds, float smoothingSeconds)
    {
        if (!std::isfinite(previousSmoothedLoadG) || !std::isfinite(rawLoadG) ||
            !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f ||
            !std::isfinite(smoothingSeconds) || smoothingSeconds < 0.0f)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        if (smoothingSeconds == 0.0f)
        {
            return rawLoadG;
        }
        const double alpha = -std::expm1(-double(deltaSeconds) / double(smoothingSeconds));
        return static_cast<float>(double(previousSmoothedLoadG) +
                                  alpha * (double(rawLoadG) - double(previousSmoothedLoadG)));
    }

    float MixGripShakeLoadG(float tensionLoadG, const Vector3& anchorAcceleration, const GripTuning& tuning)
    {
        if (!std::isfinite(tensionLoadG) || !IsFinite(anchorAcceleration) || !IsValidTuning(tuning))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        const double mixed = double(tuning.TensionWeight) * double(tensionLoadG) +
            double(tuning.AccelerationWeight) * VectorLength(anchorAcceleration) / double(kGripStandardGravity);
        return static_cast<float>(std::clamp(mixed, 0.0, double(tuning.MaxLoadG)));
    }

    bool StepGripGauge(GripGaugeState& state, float shakeLoadG, bool bFeetOnSurface, float deltaSeconds,
                       const GripTuning& tuning, GripGaugeStep& outStep)
    {
        if (!std::isfinite(state.Value) || state.Value < 0.0f || state.Value > 1.0f ||
            !std::isfinite(shakeLoadG) || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f ||
            !IsValidTuning(tuning))
        {
            return false;
        }

        GripGaugeState next = state;
        GripGaugeStep step;
        if (!state.bDepleted)
        {
            if (bFeetOnSurface && shakeLoadG <= tuning.FreeLoadG)
            {
                next.Value = static_cast<float>(std::min(1.0, double(state.Value) +
                                                               double(tuning.RecoveryPerSecond) * deltaSeconds));
                step.Recovered = next.Value - state.Value;
            }
            else
            {
                const double t = std::clamp((double(shakeLoadG) - double(tuning.FreeLoadG)) /
                                            (double(tuning.PanicLoadG) - double(tuning.FreeLoadG)), 0.0, 1.0);
                const double smooth = t * t * (3.0 - 2.0 * t);
                const double drain = (double(tuning.BaseDrainPerSecond) +
                                      double(tuning.LoadDrainPerSecond) * smooth) * deltaSeconds;
                next.Value = static_cast<float>(std::max(0.0, double(state.Value) - drain));
                step.Drained = state.Value - next.Value;
                if (state.Value > 0.0f && next.Value == 0.0f)
                {
                    next.bDepleted = true;
                    step.bDepletedNow = true;
                }
            }
        }
        state = next;
        outStep = step;
        return true;
    }

    void ResetGripGauge(GripGaugeState& state)
    {
        state = GripGaugeState{};
    }
} // namespace NorvesLib::Modules::Gameplay::Grip
