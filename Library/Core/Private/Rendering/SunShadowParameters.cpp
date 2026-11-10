#include "Rendering/SunShadowParameters.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    SunShadowParameters SanitizeSunShadowParameters(const SunShadowParameters& parameters)
    {
        SunShadowParameters result;
        if (std::isfinite(parameters.MaxDistance) && parameters.MaxDistance > 0.0f)
        {
            result.MaxDistance = parameters.MaxDistance;
        }
        if (std::isfinite(parameters.SplitLambda) && parameters.SplitLambda > 0.0f)
        {
            result.SplitLambda = parameters.SplitLambda > 1.0f ? 1.0f : parameters.SplitLambda;
        }
        return result;
    }
} // namespace NorvesLib::Core::Rendering
