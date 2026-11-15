#include "Rendering/SunShadowParameters.h"

#include "Rendering/SkyAtmosphere.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    SunShadowParameters SanitizeSunShadowParameters(const SunShadowParameters& parameters)
    {
        SunShadowParameters result;
        result.bEnabled = parameters.bEnabled;
        result.MaxShadowCasterTriangles = parameters.MaxShadowCasterTriangles;
        if (std::isfinite(parameters.AutoDisableBelowSunIlluminanceLux) &&
            parameters.AutoDisableBelowSunIlluminanceLux >= 0.0f)
        {
            result.AutoDisableBelowSunIlluminanceLux = parameters.AutoDisableBelowSunIlluminanceLux;
        }
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

    SunShadowFrameState EvaluateSunShadowFrameState(const SunShadowParameters& shadow,
                                                   const SkyAtmosphereParameters& sky)
    {
        const SunShadowParameters sanitized = SanitizeSunShadowParameters(shadow);
        SunShadowFrameState result;
        if (!sanitized.bEnabled)
        {
            result.bActive = false;
            return result;
        }
        if (sanitized.AutoDisableBelowSunIlluminanceLux <= 0.0f ||
            !SanitizeSkyAtmosphereParameters(sky).bEnabled)
        {
            return result;
        }

        const Math::Vector3 illuminance = ComputeSunGroundIlluminance(sky);
        result.SunIlluminanceLux = 0.2126f * illuminance.x + 0.7152f * illuminance.y +
                                   0.0722f * illuminance.z;
        if (!std::isfinite(result.SunIlluminanceLux) ||
            result.SunIlluminanceLux < sanitized.AutoDisableBelowSunIlluminanceLux)
        {
            result.bActive = false;
            result.bAutoDisabled = true;
        }
        return result;
    }
} // namespace NorvesLib::Core::Rendering
