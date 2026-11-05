// しがみつきの拘束、荷重、入力拒否、サブステップを検証する。
#include "Gameplay/Grip/GripSolver.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Grip;
    namespace M = NorvesLib::Math;

#define GRIP_SOLVER_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "GripSolverTest %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kDt = 1.0 / 60.0;

    GripSolverInput MakeInput()
    {
        GripSolverInput input;
        input.GripPrevious = M::Vector3(0.0f, 0.0f, 0.0f);
        input.GripCurrent = input.GripPrevious;
        input.Length = 1.0f;
        input.DeltaSeconds = static_cast<float>(kDt);
        return input;
    }

    void StepChecked(const GripSolverInput& input, GripBodyState& body, GripSolverStep& step, int frame)
    {
        const EGripSolverResult result = StepGripSolver(input, body, step);
        if (result != EGripSolverResult::Ok)
        {
            std::printf("solver frame=%d dt=%.9g result=%d\n", frame, input.DeltaSeconds, static_cast<int>(result));
        }
        GRIP_SOLVER_CHECK(result == EGripSolverResult::Ok);
    }

    void Pendulum()
    {
        GripSolverInput input = MakeInput();
        const double angle = 5.0 * kPi / 180.0;
        GripBodyState body{M::Vector3(static_cast<float>(std::sin(angle)), static_cast<float>(-std::cos(angle)), 0.0f),
                           M::Vector3(0.0f, 0.0f, 0.0f)};
        double previousCrossing = -1.0;
        double sumPeriods = 0.0;
        int periodCount = 0;
        float maxResidual = 0.0f;
        int maxResidualFrame = 0;
        for (int frame = 0; frame < 20 * 60; ++frame)
        {
            const double t0 = frame * kDt;
            const float oldX = body.Position.x;
            GripSolverStep step;
            StepChecked(input, body, step, frame);
            if (step.ResidualMeters > maxResidual)
            {
                maxResidual = step.ResidualMeters;
                maxResidualFrame = frame;
            }
            if (oldX < 0.0f && body.Position.x >= 0.0f)
            {
                const double crossing = t0 + kDt * (-double(oldX)) / (double(body.Position.x) - double(oldX));
                if (previousCrossing >= 0.0)
                {
                    sumPeriods += crossing - previousCrossing;
                    ++periodCount;
                }
                previousCrossing = crossing;
            }
        }
        const double expectedPeriod = 2.0 * kPi * std::sqrt(1.0 / 9.81);
        const double meanPeriod = periodCount > 0 ? sumPeriods / periodCount : 0.0;
        std::printf("pendulum period=%.9f expected=%.9f periods=%d dt=%.9f angle=5deg\n",
                    meanPeriod, expectedPeriod, periodCount, kDt);
        std::printf("pendulum max_residual_m=%.9g limit=0.001 frame=%d L=1\n", maxResidual, maxResidualFrame);
        GRIP_SOLVER_CHECK(periodCount > 0);
        GRIP_SOLVER_CHECK(std::fabs(meanPeriod - expectedPeriod) <= 0.02 * expectedPeriod);
        GRIP_SOLVER_CHECK(maxResidual < 1.0e-3f * input.Length);
    }

    void ConstantVelocity()
    {
        GripSolverInput input = MakeInput();
        input.SwingDampingPerSecond = 4.0f;
        GripBodyState body{M::Vector3(0.0f, -1.0f, 0.0f), M::Vector3(0.0f, 0.0f, 0.0f)};
        double sum = 0.0;
        for (int frame = 0; frame < 6 * 60; ++frame)
        {
            const double t0 = frame * kDt;
            const double t1 = (frame + 1) * kDt;
            input.GripPrevious = M::Vector3(static_cast<float>(3.0 * t0), 0.0f, 0.0f);
            input.GripCurrent = M::Vector3(static_cast<float>(3.0 * t1), 0.0f, 0.0f);
            GripSolverStep step;
            StepChecked(input, body, step, frame);
            if (frame >= 5 * 60)
            {
                sum += step.MeanLoadG;
            }
        }
        const double mean = sum / 60.0;
        std::printf("constant_velocity mean_load_g=%.9f expected=1 tolerance=0.02 speed=3 dt=%.9f\n", mean, kDt);
        GRIP_SOLVER_CHECK(std::fabs(mean - 1.0) <= 0.02);
    }

    void ConstantAcceleration()
    {
        GripSolverInput input = MakeInput();
        input.SwingDampingPerSecond = 4.0f;
        GripBodyState body{M::Vector3(0.0f, -1.0f, 0.0f), M::Vector3(0.0f, 0.0f, 0.0f)};
        double sum = 0.0;
        for (int frame = 0; frame < 210; ++frame)
        {
            const double t0 = frame * kDt;
            const double t1 = (frame + 1) * kDt;
            input.GripPrevious = M::Vector3(static_cast<float>(2.0 * t0 * t0), 0.0f, 0.0f);
            input.GripCurrent = M::Vector3(static_cast<float>(2.0 * t1 * t1), 0.0f, 0.0f);
            GripSolverStep step;
            StepChecked(input, body, step, frame);
            if (frame >= 180)
            {
                sum += step.MeanLoadG;
            }
        }
        const double mean = sum / 30.0;
        const double expected = std::sqrt(9.81 * 9.81 + 4.0 * 4.0) / 9.81;
        std::printf("constant_acceleration mean_load_g=%.9f expected=%.9f tolerance=5%% acceleration=4 dt=%.9f\n",
                    mean, expected, kDt);
        GRIP_SOLVER_CHECK(std::fabs(mean - expected) <= 0.05 * expected);
    }

    void Reject(const char* name, const GripSolverInput& input, GripBodyState body)
    {
        GripSolverStep step;
        step.Substeps = 37;
        step.SubstepSeconds = 0.25f;
        step.MeanLoadG = 2.5f;
        step.PeakLoadG = 3.5f;
        step.MeanTensionPerMass = M::Vector3(4.0f, 5.0f, 6.0f);
        step.ResidualMeters = 7.0f;
        unsigned char beforeBody[sizeof(body)];
        unsigned char beforeStep[sizeof(step)];
        std::memcpy(beforeBody, &body, sizeof(body));
        std::memcpy(beforeStep, &step, sizeof(step));
        const EGripSolverResult result = StepGripSolver(input, body, step);
        const bool bBodyUnchanged = std::memcmp(beforeBody, &body, sizeof(body)) == 0;
        const bool bStepUnchanged = std::memcmp(beforeStep, &step, sizeof(step)) == 0;
        std::printf("reject %s result=%d body_unchanged=%d step_unchanged=%d\n", name,
                    static_cast<int>(result), bBodyUnchanged, bStepUnchanged);
        GRIP_SOLVER_CHECK(result == EGripSolverResult::InvalidInput);
        GRIP_SOLVER_CHECK(bBodyUnchanged);
        GRIP_SOLVER_CHECK(bStepUnchanged);
    }

    void InvalidInputs()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const GripBodyState validBody{M::Vector3(0.0f, -1.0f, 0.0f), M::Vector3(0.0f, 0.0f, 0.0f)};
        GripBodyState body = validBody;
        body.Position.x = nan;
        Reject("body_position_nan", MakeInput(), body);
        body = validBody;
        body.Velocity.x = infinity;
        Reject("body_velocity_inf", MakeInput(), body);
        GripSolverInput input = MakeInput();
        input.GripPrevious.x = nan;
        Reject("grip_previous_nan", input, validBody);
        input = MakeInput();
        input.GripCurrent.x = infinity;
        Reject("grip_current_inf", input, validBody);
        input = MakeInput();
        input.Gravity.y = nan;
        Reject("gravity_nan", input, validBody);
        input = MakeInput();
        input.SwingDampingPerSecond = infinity;
        Reject("damping_inf", input, validBody);
        input = MakeInput();
        input.Length = nan;
        Reject("length_nan", input, validBody);
        input = MakeInput();
        input.DeltaSeconds = infinity;
        Reject("dt_inf", input, validBody);
        input = MakeInput();
        input.Length = 0.0f;
        Reject("length_zero", input, validBody);
        input = MakeInput();
        input.DeltaSeconds = 0.0f;
        Reject("dt_zero", input, validBody);
        input = MakeInput();
        input.MaxSubsteps = 0;
        Reject("max_substeps_zero", input, validBody);
        input.MaxSubsteps = 9;
        Reject("max_substeps_nine", input, validBody);
    }

    void Substeps(float dt, uint32_t maximum, uint32_t expected)
    {
        GripSolverInput input = MakeInput();
        input.DeltaSeconds = dt;
        input.MaxSubsteps = maximum;
        GripBodyState body{M::Vector3(0.0f, -1.0f, 0.0f), M::Vector3(0.0f, 0.0f, 0.0f)};
        GripSolverStep step;
        StepChecked(input, body, step, 0);
        const double covered = double(step.Substeps) * double(step.SubstepSeconds);
        const double expectedCovered = dt < kGripMaxDeltaSeconds ? dt : kGripMaxDeltaSeconds;
        std::printf("substeps dt=%.9g max=%u count=%u expected=%u covered=%.9g expected_covered=%.9g\n",
                    dt, maximum, step.Substeps, expected, covered, expectedCovered);
        GRIP_SOLVER_CHECK(step.Substeps == expected);
        GRIP_SOLVER_CHECK(std::fabs(covered - expectedCovered) < 1.0e-7);
    }
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Pendulum();
    ConstantVelocity();
    ConstantAcceleration();
    InvalidInputs();
    Substeps(1.0f / 30.0f, 8, 4);
    Substeps(1.0f / 60.0f, 8, 2);
    Substeps(1.0f / 144.0f, 8, 1);
    Substeps(0.5f, 8, 4);
    Substeps(1.0f / 30.0f, 2, 2);
    std::printf("GripSolverTest PASS\n");
    return 0;
}
