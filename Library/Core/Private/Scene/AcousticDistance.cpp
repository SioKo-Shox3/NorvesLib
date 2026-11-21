#include "Scene/AcousticDistance.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        double EvaluateTableGain(Container::Span<const AcousticDistanceTablePoint> table, double t) noexcept
        {
            AcousticDistanceTablePoint previous{0.0f, 1.0f};
            for (const auto& point : table)
            {
                const float x = point.NormalizedDistance;
                if (!(previous.NormalizedDistance < x && x < 1.0f))
                {
                    continue;
                }

                const float gain = std::isnan(point.Gain) ? previous.Gain : std::clamp(point.Gain, 0.0f, previous.Gain);
                if (t <= x)
                {
                    const double fraction = (t - previous.NormalizedDistance) / (static_cast<double>(x) - previous.NormalizedDistance);
                    return std::lerp(static_cast<double>(previous.Gain), static_cast<double>(gain), fraction);
                }
                previous = {x, gain};
            }
            return previous.Gain * ((1.0 - t) / (1.0 - previous.NormalizedDistance));
        }
    }

    float EvaluateAcousticDistanceGain(const AcousticDistanceParams& params, float distance) noexcept
    {
        if (std::isnan(distance))
        {
            return 0.0f;
        }
        const double min = std::isfinite(params.MinDistance) ? std::max(params.MinDistance, kAcousticMinDistanceFloor) : 1.0f;
        const double max = params.MaxDistance;
        const double d = std::max(distance, 0.0f);
        if (std::isfinite(max) && d >= max)
        {
            return 0.0f;
        }
        if (d <= min)
        {
            return 1.0f;
        }
        if (!std::isfinite(max))
        {
            return 0.0f;
        }

        const double t = (d - min) / (max - min);
        double gain = 1.0 - t;
        switch (params.Rolloff)
        {
        case AcousticRolloff::InverseClamped:
        {
            const double floor = min / max;
            gain = (min / d - floor) / (1.0 - floor);
            break;
        }
        case AcousticRolloff::Linear:
            break;
        case AcousticRolloff::LogDb:
        {
            const double range = std::isnan(params.LogDbRange) ? 60.0f : std::clamp(params.LogDbRange, 1.0f, 120.0f);
            const double floor = std::pow(10.0, -range / 20.0);
            gain = (std::pow(10.0, -range * t / 20.0) - floor) / (1.0 - floor);
            break;
        }
        case AcousticRolloff::Table:
            gain = EvaluateTableGain(params.Table, t);
            break;
        }
        return static_cast<float>(std::clamp(gain, 0.0, 1.0));
    }
}
