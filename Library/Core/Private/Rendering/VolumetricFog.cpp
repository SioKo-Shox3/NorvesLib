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
        constexpr float kMaximumFogDistanceMeters = 1.0e6f;
        constexpr float kMaximumFogAmbientScale = 16.0f;
        constexpr float kMaximumFogTint = 16.0f;
        constexpr float kDefaultFogReferenceElevationDegrees = 10.0f;
        constexpr float kMaximumFogReferenceElevationDegrees = 90.0f;
        constexpr float kDefaultExponentialStepRatio = 1.15f;
        constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
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
        result.MaxDistanceMeters = ClampNonNegativeFinite(
            parameters.MaxDistanceMeters,
            kMaximumFogDistanceMeters,
            0.0f);
        result.AmbientScale = ClampNonNegativeFinite(
            parameters.AmbientScale,
            kMaximumFogAmbientScale,
            1.0f);
        for (int channel = 0; channel < 3; ++channel)
        {
            result.Tint[channel] = ClampNonNegativeFinite(
                parameters.Tint[channel],
                kMaximumFogTint,
                1.0f);
        }
        result.SkyBlend = ClampFinite(parameters.SkyBlend, 0.0f, 1.0f, 0.0f);
        result.ReferenceElevationDegrees = ClampFinite(
            parameters.ReferenceElevationDegrees,
            0.0f,
            kMaximumFogReferenceElevationDegrees,
            kDefaultFogReferenceElevationDegrees);
        switch (parameters.StepDistribution)
        {
        case VolumetricFogStepDistribution::Uniform:
        case VolumetricFogStepDistribution::Exponential:
            result.StepDistribution = parameters.StepDistribution;
            break;
        default:
            result.StepDistribution = VolumetricFogStepDistribution::Uniform;
            break;
        }
        result.ExponentialStepRatio = ClampFinite(
            parameters.ExponentialStepRatio, 1.0f, 2.0f, kDefaultExponentialStepRatio);
        result.ScatteringCutoffMeters = ClampNonNegativeFinite(
            parameters.ScatteringCutoffMeters, kMaximumFogDistanceMeters, 0.0f);
        result.bPixelJitter = parameters.bPixelJitter;
        result.bTemporalJitter = parameters.bTemporalJitter;
        return result;
    }

    VolumetricFogStep ComputeVolumetricFogStep(VolumetricFogStepDistribution distribution, float ratio,
                                             uint32_t stepIndex, uint32_t stepCount, float spanMeters)
    {
        if (stepCount == 0u || stepIndex >= stepCount || !std::isfinite(spanMeters) || spanMeters <= 0.0f)
        {
            return {};
        }

        if (distribution == VolumetricFogStepDistribution::Uniform ||
            !std::isfinite(ratio) || ratio < VolumetricMinimumExponentialStepRatio || stepCount == 1u)
        {
            const float length = spanMeters / static_cast<float>(stepCount);
            const float start = static_cast<float>(stepIndex) * length;
            return {start, length};
        }

        const float denominator = std::pow(ratio, static_cast<float>(stepCount)) - 1.0f;
        const float growth = std::pow(ratio, static_cast<float>(stepIndex));
        const float start = spanMeters * (growth - 1.0f) / denominator;
        const float length = spanMeters * growth * (ratio - 1.0f) / denominator;
        return {start, length};
    }

    float ComputeVolumetricFogScatteringSpan(const VolumetricFogParameters& parameters, float fogDistanceMeters)
    {
        const VolumetricFogParameters p = SanitizeVolumetricFogParameters(parameters);
        if (!std::isfinite(fogDistanceMeters) || fogDistanceMeters <= 0.0f)
        {
            return 0.0f;
        }

        return p.ScatteringCutoffMeters > 0.0f
                   ? std::min(fogDistanceMeters, p.ScatteringCutoffMeters)
                   : fogDistanceMeters;
    }

    VolumetricFogStep ComputeVolumetricAtmosphereMarchStep(const VolumetricFogParameters& parameters,
                                                        uint32_t stepIndex, uint32_t stepCount,
                                                        float rayDistanceMeters)
    {
        const VolumetricFogParameters p = SanitizeVolumetricFogParameters(parameters);
        const float cutoff = p.ScatteringCutoffMeters;
        if (cutoff > 0.0f && rayDistanceMeters > cutoff && stepCount > VolumetricAtmosphereFarStepCount)
        {
            const uint32_t nearCount = stepCount - VolumetricAtmosphereFarStepCount;
            if (stepIndex < nearCount)
            {
                return ComputeVolumetricFogStep(
                    p.StepDistribution, p.ExponentialStepRatio, stepIndex, nearCount, cutoff);
            }

            VolumetricFogStep far = ComputeVolumetricFogStep(
                VolumetricFogStepDistribution::Uniform, 1.0f, stepIndex - nearCount,
                VolumetricAtmosphereFarStepCount, rayDistanceMeters - cutoff);
            far.StartMeters += cutoff;
            return far;
        }

        return ComputeVolumetricFogStep(
            p.StepDistribution, p.ExponentialStepRatio, stepIndex, stepCount, rayDistanceMeters);
    }

    FogReferenceElevationSinCos ComputeFogReferenceElevationSinCos(const VolumetricFogParameters& parameters)
    {
        const double radians = static_cast<double>(
            SanitizeVolumetricFogParameters(parameters).ReferenceElevationDegrees) * kDegreesToRadians;
        return {static_cast<float>(std::sin(radians)), static_cast<float>(std::cos(radians))};
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
        const float fogDistance = sanitized.MaxDistanceMeters > 0.0f
                                      ? std::min(rayDistance, sanitized.MaxDistanceMeters)
                                      : rayDistance;
        const double distance = static_cast<double>(fogDistance);
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
