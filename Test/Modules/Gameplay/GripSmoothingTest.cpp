// 動画由来の疎な位置キーで生じる荷重の跳ねと指数平滑を検証する。
#include "Gameplay/Grip/GripSolver.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Grip;
    namespace M = NorvesLib::Math;

#define GRIP_SMOOTHING_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "GripSmoothingTest %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kKeySeconds = 0.0625;
    constexpr double kDt = 1.0 / 60.0;

    M::Vector3 GripAt(double time)
    {
        const int key = static_cast<int>(std::floor(time / kKeySeconds));
        const double fraction = (time - key * kKeySeconds) / kKeySeconds;
        const double previous = 0.4 * std::sin(2.0 * kPi * key * kKeySeconds);
        const double current = 0.4 * std::sin(2.0 * kPi * (key + 1) * kKeySeconds);
        return M::Vector3(static_cast<float>(previous + (current - previous) * fraction), 0.0f, 0.0f);
    }

    void MeasureSmoothing()
    {
        const GripTuning tuning;
        GripSolverInput input;
        input.Length = 1.0f;
        input.DeltaSeconds = static_cast<float>(kDt);
        input.SwingDampingPerSecond = tuning.SwingDampingPerSecond;
        input.MaxSubsteps = tuning.MaxSubsteps;
        GripBodyState body{M::Vector3(0.0f, -1.0f, 0.0f), M::Vector3(0.0f, 0.0f, 0.0f)};
        float smoothed = 1.0f;
        double rawSum = 0.0;
        double smoothSum = 0.0;
        float rawPeak = 0.0f;
        float smoothPeak = 0.0f;
        int samples = 0;
        for (int frame = 0; frame < 6 * 60; ++frame)
        {
            input.GripPrevious = GripAt(frame * kDt);
            input.GripCurrent = GripAt((frame + 1) * kDt);
            GripSolverStep step;
            const EGripSolverResult result = StepGripSolver(input, body, step);
            if (result != EGripSolverResult::Ok)
            {
                std::printf("smoothing frame=%d solver_result=%d\n", frame, static_cast<int>(result));
            }
            GRIP_SMOOTHING_CHECK(result == EGripSolverResult::Ok);
            smoothed = SmoothGripLoadG(smoothed, step.MeanLoadG, input.DeltaSeconds, tuning.SmoothingSeconds);
            GRIP_SMOOTHING_CHECK(std::isfinite(step.MeanLoadG) && std::isfinite(smoothed));
            if (frame >= 60)
            {
                rawSum += step.MeanLoadG;
                smoothSum += smoothed;
                if (step.MeanLoadG > rawPeak)
                {
                    rawPeak = step.MeanLoadG;
                }
                if (smoothed > smoothPeak)
                {
                    smoothPeak = smoothed;
                }
                ++samples;
            }
        }
        const double rawMean = rawSum / samples;
        const double smoothMean = smoothSum / samples;
        const double rawRatio = rawPeak / rawMean;
        const double smoothRatio = smoothPeak / smoothMean;
        std::printf("smoothing amplitude=0.4 samples=%d raw_peak=%.9g raw_mean=%.9f raw_peak_mean=%.9f\n",
                    samples, rawPeak, rawMean, rawRatio);
        std::printf("smoothing tau=%.9g smooth_peak=%.9g smooth_mean=%.9f smooth_peak_mean=%.9f\n",
                    tuning.SmoothingSeconds, smoothPeak, smoothMean, smoothRatio);
        // 実測のピーク / 平均は生 2.198、平滑後 1.335。閾値 1.65 の両側に 10% 以上の余裕がある。
        constexpr double threshold = 1.65;
        std::printf("smoothing threshold=%.9f smooth_limit_10pct=%.9f raw_limit_10pct=%.9f\n",
                    threshold, threshold * 0.9, threshold * 1.1);
        GRIP_SMOOTHING_CHECK(samples == 5 * 60 && rawMean > 0.0 && smoothMean > 0.0);
        GRIP_SMOOTHING_CHECK(smoothRatio <= threshold);
        GRIP_SMOOTHING_CHECK(rawRatio > threshold);
        GRIP_SMOOTHING_CHECK(smoothRatio <= threshold * 0.9);
        GRIP_SMOOTHING_CHECK(rawRatio > threshold * 1.1);
    }
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    MeasureSmoothing();
    const float raw = 2.5f;
    const float bypass = SmoothGripLoadG(100.0f, raw, static_cast<float>(kDt), 0.0f);
    std::printf("smoothing zero_tau raw=%.9g output=%.9g\n", raw, bypass);
    GRIP_SMOOTHING_CHECK(bypass == raw);
    std::printf("GripSmoothingTest PASS\n");
    return 0;
}
