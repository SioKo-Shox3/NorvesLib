// フレーム間隔が異なるときのゲージの減少量と長い dt での安定性を検証する。
#include "Gameplay/Grip/GripSolver.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Grip;
    namespace M = NorvesLib::Math;

#define GRIP_FRAME_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "GripFrameRateInvarianceTest %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    constexpr double kPi = 3.14159265358979323846;

    M::Vector3 GripAt(double time)
    {
        return M::Vector3(static_cast<float>(2.0 * std::sin(2.0 * kPi * time / 0.5)), 0.0f, 0.0f);
    }

    struct Measurement
    {
        double Drained = 0.0;
        double MeanLoad = 0.0;
        float MinGauge = 1.0f;
        float MinLoad = 1.0e30f;
        float MaxLoad = 0.0f;
        int BelowFree = 0;
        int Between = 0;
        int AbovePanic = 0;
    };

    Measurement Measure(double dt, int frames, const GripTuning& tuning)
    {
        GripSolverInput input;
        input.Length = 1.0f;
        input.DeltaSeconds = static_cast<float>(dt);
        input.SwingDampingPerSecond = tuning.SwingDampingPerSecond;
        input.MaxSubsteps = tuning.MaxSubsteps;
        GripBodyState body{M::Vector3(0.0f, -1.0f, 0.0f), M::Vector3(0.0f, 0.0f, 0.0f)};
        GripGaugeState gauge;
        float smoothed = 1.0f;
        Measurement result;
        for (int frame = 0; frame < frames; ++frame)
        {
            input.GripPrevious = GripAt(frame * dt);
            input.GripCurrent = GripAt((frame + 1) * dt);
            GripSolverStep solverStep;
            const EGripSolverResult solverResult = StepGripSolver(input, body, solverStep);
            if (solverResult != EGripSolverResult::Ok)
            {
                std::printf("frame_rate dt=%.9g frame=%d solver_result=%d\n", dt, frame, static_cast<int>(solverResult));
            }
            GRIP_FRAME_CHECK(solverResult == EGripSolverResult::Ok);
            smoothed = SmoothGripLoadG(smoothed, solverStep.MeanLoadG, input.DeltaSeconds, tuning.SmoothingSeconds);
            const float shake = MixGripShakeLoadG(smoothed, M::Vector3(0.0f, 0.0f, 0.0f), tuning);
            GripGaugeStep gaugeStep;
            const bool bOk = StepGripGauge(gauge, shake, false, input.DeltaSeconds, tuning, gaugeStep);
            if (!bOk || !std::isfinite(smoothed) || !std::isfinite(shake))
            {
                std::printf("frame_rate dt=%.9g frame=%d raw=%.9g smoothed=%.9g shake=%.9g ok=%d\n",
                            dt, frame, solverStep.MeanLoadG, smoothed, shake, bOk);
            }
            GRIP_FRAME_CHECK(bOk && std::isfinite(smoothed) && std::isfinite(shake));
            result.Drained += gaugeStep.Drained;
            result.MeanLoad += smoothed;
            if (gauge.Value < result.MinGauge)
            {
                result.MinGauge = gauge.Value;
            }
            if (smoothed < result.MinLoad)
            {
                result.MinLoad = smoothed;
            }
            if (smoothed > result.MaxLoad)
            {
                result.MaxLoad = smoothed;
            }
            if (smoothed <= tuning.FreeLoadG)
            {
                ++result.BelowFree;
            }
            else if (smoothed < tuning.PanicLoadG)
            {
                ++result.Between;
            }
            else
            {
                ++result.AbovePanic;
            }
        }
        result.MeanLoad /= frames;
        std::printf("frame_rate dt=%.9g panic=%.9g drained=%.9f mean_load_g=%.9f min_load=%.9g max_load=%.9g "
                    "below=%d between=%d above=%d min_gauge=%.9g\n",
                    dt, tuning.PanicLoadG, result.Drained, result.MeanLoad, result.MinLoad, result.MaxLoad,
                    result.BelowFree, result.Between, result.AbovePanic, result.MinGauge);
        GRIP_FRAME_CHECK(result.MinGauge > 0.0f);
        return result;
    }

    void LongDelta(double phase)
    {
        const GripTuning tuning;
        constexpr double dt = 0.25;
        GripSolverInput input;
        input.Length = 1.0f;
        input.DeltaSeconds = static_cast<float>(dt);
        input.SwingDampingPerSecond = tuning.SwingDampingPerSecond;
        input.MaxSubsteps = tuning.MaxSubsteps;
        GripBodyState body{GripAt(phase) + M::Vector3(0.0f, -1.0f, 0.0f), M::Vector3(0.0f, 0.0f, 0.0f)};
        float maxResidual = 0.0f;
        float maxSpeed = 0.0f;
        for (int frame = 0; frame < 20; ++frame)
        {
            input.GripPrevious = GripAt(phase + frame * dt);
            input.GripCurrent = GripAt(phase + (frame + 1) * dt);
            GripSolverStep step;
            const EGripSolverResult result = StepGripSolver(input, body, step);
            const float speed = body.Velocity.Length();
            if (result != EGripSolverResult::Ok || !std::isfinite(body.Position.x) || !std::isfinite(body.Position.y) ||
                !std::isfinite(body.Position.z) || !std::isfinite(body.Velocity.x) || !std::isfinite(body.Velocity.y) ||
                !std::isfinite(body.Velocity.z) || !(step.ResidualMeters < 1.0e-3f) || !(speed < 200.0f))
            {
                std::printf("long_dt phase=%.6f frame=%d result=%d pos=(%.9g,%.9g,%.9g) vel=(%.9g,%.9g,%.9g) residual=%.9g speed=%.9g\n",
                            phase, frame, static_cast<int>(result), body.Position.x, body.Position.y, body.Position.z,
                            body.Velocity.x, body.Velocity.y, body.Velocity.z, step.ResidualMeters, speed);
            }
            GRIP_FRAME_CHECK(result == EGripSolverResult::Ok);
            GRIP_FRAME_CHECK(std::isfinite(body.Position.x) && std::isfinite(body.Position.y) && std::isfinite(body.Position.z));
            GRIP_FRAME_CHECK(std::isfinite(body.Velocity.x) && std::isfinite(body.Velocity.y) && std::isfinite(body.Velocity.z));
            GRIP_FRAME_CHECK(step.ResidualMeters < 1.0e-3f * input.Length);
            GRIP_FRAME_CHECK(speed < 200.0f);
            if (step.ResidualMeters > maxResidual)
            {
                maxResidual = step.ResidualMeters;
            }
            if (speed > maxSpeed)
            {
                maxSpeed = speed;
            }
        }
        std::printf("long_dt phase=%.6f dt=0.25 frames=20 max_residual_m=%.9g max_speed_mps=%.9g\n",
                    phase, maxResidual, maxSpeed);
    }
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    GripTuning tuning;
    // 実測の平滑荷重は約 3.14〜64.79 g。60 g なら各刻みの 97% 以上が free と panic の間に入り、
    // 上限 80 g は実測の最大値を超えるため、荷重の切り詰めで差が消えない。
    tuning.PanicLoadG = 60.0f;
    tuning.MaxLoadG = 80.0f;
    const Measurement a = Measure(1.0 / 30.0, 90, tuning);
    const Measurement b = Measure(1.0 / 60.0, 180, tuning);
    const Measurement c = Measure(1.0 / 144.0, 432, tuning);
    const double minimum = std::fmin(a.Drained, std::fmin(b.Drained, c.Drained));
    const double maximum = std::fmax(a.Drained, std::fmax(b.Drained, c.Drained));
    const double mean = (a.Drained + b.Drained + c.Drained) / 3.0;
    const double relativeDifference = (maximum - minimum) / mean;
    std::printf("frame_rate panic=%.9g relative_difference=%.9f limit=0.05\n", tuning.PanicLoadG, relativeDifference);
    GRIP_FRAME_CHECK(a.Between > 90 / 2 && b.Between > 180 / 2 && c.Between > 432 / 2);
    GRIP_FRAME_CHECK(relativeDifference < 0.05);
    LongDelta(0.0);
    // 半周期ごとの標本が常に原点になるため、位相をずらした同じ軌道でも確かめる。
    LongDelta(0.0625);
    std::printf("GripFrameRateInvarianceTest PASS\n");
    return 0;
}
