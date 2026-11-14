#include "Rendering/EnvironmentParameters.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float ClampUnitOrDefault(float value, float fallback)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        }
    } // namespace

    EnvironmentParameters MakeDefaultEnvironmentParameters()
    {
        return EnvironmentParameters{};
    }

    EnvironmentParameters SanitizeEnvironmentParameters(const EnvironmentParameters& in)
    {
        EnvironmentParameters result = MakeDefaultEnvironmentParameters();
        result.bEnabled = in.bEnabled;
        result.Seed = in.Seed;
        result.DiscontinuityId = in.DiscontinuityId;
        result.RainIntensity = ClampUnitOrDefault(in.RainIntensity, result.RainIntensity);
        result.Wetness = ClampUnitOrDefault(in.Wetness, result.Wetness);
        result.CloudCover = ClampUnitOrDefault(in.CloudCover, result.CloudCover);
        if (std::isfinite(in.GameTimeSeconds))
        {
            result.GameTimeSeconds = in.GameTimeSeconds < 0.0 ? 0.0 : in.GameTimeSeconds;
        }
        return result;
    }
} // namespace NorvesLib::Core::Rendering
