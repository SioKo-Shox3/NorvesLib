#include "Rendering/VolumetricFog.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float kDefaultDensityAtBaseHeight = 0.01f;
        constexpr float kDefaultBaseHeight = 0.0f;
        constexpr float kDefaultHeightFalloffPerUnit = 0.01f;
        constexpr float kMaximumDensityAtBaseHeight = 10.0f;
        constexpr float kMaximumAbsoluteBaseHeight = 1.0e6f;
        constexpr float kMaximumHeightFalloffPerUnit = 1.0f;
        constexpr double kMaximumOpticalDepth = 80.0;

        float ClampFinite(float value, float minimum, float maximum, float fallback)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        float ClampNonNegativeFinite(float value, float maximum, float fallback)
        {
            if (!std::isfinite(value) || value < 0.0f)
            {
                return fallback;
            }
            return std::min(value, maximum);
        }
    } // namespace

    VolumetricFogParameters MakeDefaultVolumetricFogParameters()
    {
        return VolumetricFogParameters{};
    }

    VolumetricFogParameters SanitizeVolumetricFogParameters(
        const VolumetricFogParameters& parameters)
    {
        VolumetricFogParameters result = MakeDefaultVolumetricFogParameters();
        result.bEnabled = parameters.bEnabled;
        result.DensityAtBaseHeight = ClampNonNegativeFinite(
            parameters.DensityAtBaseHeight,
            kMaximumDensityAtBaseHeight,
            kDefaultDensityAtBaseHeight);
        result.BaseHeight = ClampFinite(parameters.BaseHeight,
                                        -kMaximumAbsoluteBaseHeight,
                                        kMaximumAbsoluteBaseHeight,
                                        kDefaultBaseHeight);
        result.HeightFalloffPerUnit = ClampNonNegativeFinite(
            parameters.HeightFalloffPerUnit,
            kMaximumHeightFalloffPerUnit,
            kDefaultHeightFalloffPerUnit);
        return result;
    }

    float ComputeHeightFogTransmittance(
        const VolumetricFogParameters& parameters,
        float rayOriginHeight,
        float rayDirectionY,
        float rayDistance)
    {
        const VolumetricFogParameters sanitized =
            SanitizeVolumetricFogParameters(parameters);
        if (!sanitized.bEnabled ||
            sanitized.DensityAtBaseHeight <= 0.0f ||
            !std::isfinite(rayOriginHeight) ||
            !std::isfinite(rayDirectionY) ||
            !std::isfinite(rayDistance) ||
            rayDistance <= 0.0f)
        {
            return 1.0f;
        }

        const double density = static_cast<double>(sanitized.DensityAtBaseHeight);
        const double falloff = static_cast<double>(sanitized.HeightFalloffPerUnit);
        const double originExponent = -falloff *
            (static_cast<double>(rayOriginHeight) - static_cast<double>(sanitized.BaseHeight));
        const double directionY = std::clamp(static_cast<double>(rayDirectionY), -1.0, 1.0);
        const double distance = static_cast<double>(rayDistance);
        const double verticalRate = falloff * directionY;

        double logIntegral = 0.0;
        if (verticalRate == 0.0)
        {
            logIntegral = std::log(distance);
        }
        else
        {
            const double exponentChange = verticalRate * distance;
            double logNumerator = 0.0;
            if (exponentChange > 80.0)
            {
                // 1 - exp(-x) is indistinguishable from 1 at this scale.
                logNumerator = 0.0;
            }
            else if (exponentChange > 0.0)
            {
                logNumerator = std::log(-std::expm1(-exponentChange));
            }
            else if (exponentChange < -80.0)
            {
                const double growth = -exponentChange;
                logNumerator = growth + std::log1p(-std::exp(-growth));
            }
            else
            {
                logNumerator = std::log(std::expm1(-exponentChange));
            }
            logIntegral = logNumerator - std::log(std::abs(verticalRate));
        }

        const double logOpticalDepth = std::log(density) + originExponent + logIntegral;
        if (logOpticalDepth >= std::log(kMaximumOpticalDepth))
        {
            return static_cast<float>(std::exp(-kMaximumOpticalDepth));
        }

        const double opticalDepth = std::exp(logOpticalDepth);
        return static_cast<float>(std::exp(-opticalDepth));
    }
} // namespace NorvesLib::Core::Rendering
