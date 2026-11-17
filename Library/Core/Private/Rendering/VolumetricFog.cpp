#include "Rendering/VolumetricFog.h"

#include "Rendering/Wind.h"

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

        // 安全化済みの四元数でローカルの各軸を回し、標準偏差の逆数を掛ける。
        void BuildLocalFogInverseAxes(const LocalFogVolumeProxy& proxy, float axes[3][3])
        {
            const float x = proxy.Rotation[0];
            const float y = proxy.Rotation[1];
            const float z = proxy.Rotation[2];
            const float w = proxy.Rotation[3];
            const float xx = x * x;
            const float yy = y * y;
            const float zz = z * z;
            const float xy = x * y;
            const float xz = x * z;
            const float yz = y * z;
            const float wx = w * x;
            const float wy = w * y;
            const float wz = w * z;
            const float inverseSigmaX = proxy.Falloff / proxy.Radius[0];
            const float inverseSigmaY = proxy.Falloff / proxy.Radius[1];
            const float inverseSigmaZ = proxy.Falloff / proxy.Radius[2];

            axes[0][0] = (1.0f - 2.0f * (yy + zz)) * inverseSigmaX;
            axes[0][1] = 2.0f * (xy + wz) * inverseSigmaX;
            axes[0][2] = 2.0f * (xz - wy) * inverseSigmaX;
            axes[1][0] = 2.0f * (xy - wz) * inverseSigmaY;
            axes[1][1] = (1.0f - 2.0f * (xx + zz)) * inverseSigmaY;
            axes[1][2] = 2.0f * (yz + wx) * inverseSigmaY;
            axes[2][0] = 2.0f * (xz + wy) * inverseSigmaZ;
            axes[2][1] = 2.0f * (yz - wx) * inverseSigmaZ;
            axes[2][2] = (1.0f - 2.0f * (xx + yy)) * inverseSigmaZ;
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

    LocalFogVolumeProxy MakeDefaultLocalFogVolumeProxy()
    {
        return LocalFogVolumeProxy{};
    }

    LocalFogVolumeProxy SanitizeLocalFogVolumeProxy(const LocalFogVolumeProxy& proxy)
    {
        LocalFogVolumeProxy result = MakeDefaultLocalFogVolumeProxy();
        for (uint32_t i = 0; i < 3u; ++i)
        {
            result.Center[i] = std::isfinite(proxy.Center[i]) ? proxy.Center[i] : result.Center[i];
            result.Radius[i] = ClampFinite(proxy.Radius[i], 0.01f, 100000.0f, result.Radius[i]);
            result.Color[i] = ClampFinite(proxy.Color[i], 0.0f, 1.0f, result.Color[i]);
        }
        result.Density = ClampFinite(proxy.Density, 0.0f, 10.0f, result.Density);
        result.Falloff = ClampFinite(proxy.Falloff, 0.5f, 8.0f, result.Falloff);
        result.NoiseAmount = ClampFinite(proxy.NoiseAmount, 0.0f, 1.0f, result.NoiseAmount);
        result.NoiseScaleMeters = ClampFinite(proxy.NoiseScaleMeters, 0.1f, 1000.0f, result.NoiseScaleMeters);

        double lengthSquared = 0.0;
        bool bFiniteRotation = true;
        for (uint32_t i = 0; i < 4u; ++i)
        {
            bFiniteRotation = bFiniteRotation && std::isfinite(proxy.Rotation[i]);
            const double component = static_cast<double>(proxy.Rotation[i]);
            lengthSquared += component * component;
        }
        if (bFiniteRotation && lengthSquared > 0.0)
        {
            const double inverseLength = 1.0 / std::sqrt(lengthSquared);
            for (uint32_t i = 0; i < 4u; ++i)
            {
                result.Rotation[i] = static_cast<float>(static_cast<double>(proxy.Rotation[i]) * inverseLength);
            }
        }
        return result;
    }

    bool IsValidLocalFogVolumeProxy(const LocalFogVolumeProxy& proxy)
    {
        if (!std::isfinite(proxy.Density) || proxy.Density <= 0.0f ||
            !std::isfinite(proxy.Falloff) || !std::isfinite(proxy.NoiseAmount) ||
            !std::isfinite(proxy.NoiseScaleMeters))
        {
            return false;
        }
        for (uint32_t i = 0; i < 3u; ++i)
        {
            if (!std::isfinite(proxy.Center[i]) || !std::isfinite(proxy.Radius[i]) ||
                proxy.Radius[i] <= 0.0f || !std::isfinite(proxy.Color[i]))
            {
                return false;
            }
        }
        for (uint32_t i = 0; i < 4u; ++i)
        {
            if (!std::isfinite(proxy.Rotation[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool AreEqualLocalFogVolumeProxy(const LocalFogVolumeProxy& a, const LocalFogVolumeProxy& b)
    {
        if (a.Density != b.Density || a.Falloff != b.Falloff || a.NoiseAmount != b.NoiseAmount ||
            a.NoiseScaleMeters != b.NoiseScaleMeters)
        {
            return false;
        }
        for (uint32_t i = 0; i < 3u; ++i)
        {
            if (a.Center[i] != b.Center[i] || a.Radius[i] != b.Radius[i] || a.Color[i] != b.Color[i])
            {
                return false;
            }
        }
        for (uint32_t i = 0; i < 4u; ++i)
        {
            if (a.Rotation[i] != b.Rotation[i])
            {
                return false;
            }
        }
        return true;
    }

    float ComputeLocalFogOpticalDepth(const LocalFogVolumeProxy& proxy, const float origin[3], const float direction[3],
                                      float t0, float t1)
    {
        if (!IsValidLocalFogVolumeProxy(proxy) || origin == nullptr || direction == nullptr ||
            !std::isfinite(t0) || (!std::isfinite(t1) && !(std::isinf(t1) && t1 > 0.0f)) || t1 <= t0)
        {
            return 0.0f;
        }
        for (uint32_t i = 0; i < 3u; ++i)
        {
            if (!std::isfinite(origin[i]) || !std::isfinite(direction[i]))
            {
                return 0.0f;
            }
        }

        const LocalFogVolumeProxy sanitized = SanitizeLocalFogVolumeProxy(proxy);
        float axes[3][3];
        BuildLocalFogInverseAxes(sanitized, axes);
        double aSquared = 0.0;
        double aDotB = 0.0;
        double bSquared = 0.0;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            double a = 0.0;
            double b = 0.0;
            for (uint32_t component = 0; component < 3u; ++component)
            {
                const double inverseAxis = static_cast<double>(axes[axis][component]);
                a += inverseAxis * (static_cast<double>(origin[component]) - sanitized.Center[component]);
                b += inverseAxis * static_cast<double>(direction[component]);
            }
            aSquared += a * a;
            aDotB += a * b;
            bSquared += b * b;
        }

        const double start = static_cast<double>(t0);
        const bool bInfiniteEnd = std::isinf(t1);
        if (bSquared < 1.0e-12)
        {
            const double end = bInfiniteEnd ? 1.0e7 : static_cast<double>(t1);
            if (end <= start)
            {
                return 0.0f;
            }
            return static_cast<float>(static_cast<double>(sanitized.Density) * std::exp(-0.5 * aSquared) * (end - start));
        }

        constexpr double pi = 3.14159265358979323846;
        const double tau0 = aDotB / bSquared;
        const double hSquared = std::max(aSquared - aDotB * aDotB / bSquared, 0.0);
        const double k = std::sqrt(bSquared / 2.0);
        const double endErf = bInfiniteEnd ? 1.0 : std::erf(k * (static_cast<double>(t1) + tau0));
        const double startErf = std::erf(k * (start + tau0));
        const double opticalDepth = static_cast<double>(sanitized.Density) * std::exp(-0.5 * hSquared) *
                                    std::sqrt(pi / (2.0 * bSquared)) * (endErf - startErf);
        return static_cast<float>(std::max(opticalDepth, 0.0));
    }

    uint32_t SelectNearestLocalFogVolumes(const LocalFogVolumeProxy* volumes, uint32_t volumeCount,
                                          const float cameraPosition[3],
                                          LocalFogVolumeProxy* outVolumes, uint32_t& outDroppedCount)
    {
        outDroppedCount = 0u;
        if (volumes == nullptr || cameraPosition == nullptr || outVolumes == nullptr)
        {
            return 0u;
        }
        for (uint32_t i = 0; i < 3u; ++i)
        {
            if (!std::isfinite(cameraPosition[i]))
            {
                return 0u;
            }
        }

        LocalFogVolumeProxy selected[MaxLocalFogVolumes];
        double distancesSquared[MaxLocalFogVolumes] = {};
        uint32_t selectedCount = 0u;
        for (uint32_t i = 0; i < volumeCount; ++i)
        {
            if (!IsValidLocalFogVolumeProxy(volumes[i]))
            {
                continue;
            }
            double distanceSquared = 0.0;
            for (uint32_t component = 0; component < 3u; ++component)
            {
                const double difference = static_cast<double>(volumes[i].Center[component]) - cameraPosition[component];
                distanceSquared += difference * difference;
            }
            uint32_t position = 0u;
            while (position < selectedCount && distancesSquared[position] <= distanceSquared)
            {
                ++position;
            }
            if (selectedCount < MaxLocalFogVolumes)
            {
                ++selectedCount;
            }
            else
            {
                ++outDroppedCount;
            }
            if (position < MaxLocalFogVolumes)
            {
                for (uint32_t j = selectedCount - 1u; j > position; --j)
                {
                    selected[j] = selected[j - 1u];
                    distancesSquared[j] = distancesSquared[j - 1u];
                }
                selected[position] = volumes[i];
                distancesSquared[position] = distanceSquared;
            }
        }
        for (uint32_t i = 0; i < selectedCount; ++i)
        {
            outVolumes[i] = selected[i];
        }
        return selectedCount;
    }

    GPULocalFogParams BuildLocalFogGpuParams(const LocalFogVolumeProxy* volumes, uint32_t volumeCount,
                                             const WindParameters& wind, float windClockSeconds)
    {
        GPULocalFogParams result{};
        const WindParameters sanitizedWind = SanitizeWindParameters(wind);
        if (sanitizedWind.bEnabled)
        {
            result.CountAndWindClock[1] = std::isfinite(windClockSeconds) ? windClockSeconds : 0.0f;
            result.CountAndWindClock[2] = LocalFogGustDisplacementSeconds;
            result.CountAndWindClock[3] = 1.0f;
            WindDirectionXZ(sanitizedWind, result.WindDirectionSpeedGust[0], result.WindDirectionSpeedGust[1]);
            result.WindDirectionSpeedGust[2] = sanitizedWind.BaseSpeed;
            result.WindDirectionSpeedGust[3] = sanitizedWind.GustStrength;
            result.WindGustShape[0] = sanitizedWind.GustFrequencyHz;
            result.WindGustShape[1] = sanitizedWind.GustScaleMeters;
            result.WindGustShape[2] = sanitizedWind.Turbulence;
        }

        uint32_t packedCount = 0u;
        const uint32_t inputCount = volumes != nullptr ? std::min(volumeCount, MaxLocalFogVolumes) : 0u;
        for (uint32_t i = 0; i < inputCount; ++i)
        {
            if (!IsValidLocalFogVolumeProxy(volumes[i]))
            {
                continue;
            }
            const LocalFogVolumeProxy sanitized = SanitizeLocalFogVolumeProxy(volumes[i]);
            float axes[3][3];
            BuildLocalFogInverseAxes(sanitized, axes);
            GPULocalFogVolume& destination = result.Volumes[packedCount];
            for (uint32_t component = 0; component < 3u; ++component)
            {
                destination.CenterAndDensity[component] = sanitized.Center[component];
                destination.InverseAxisX[component] = axes[0][component];
                destination.InverseAxisY[component] = axes[1][component];
                destination.InverseAxisZ[component] = axes[2][component];
                destination.ColorAndReserved[component] = sanitized.Color[component];
            }
            destination.CenterAndDensity[3] = sanitized.Density;
            destination.InverseAxisX[3] = sanitized.NoiseAmount;
            destination.InverseAxisY[3] = 1.0f / sanitized.NoiseScaleMeters;
            ++packedCount;
        }
        result.CountAndWindClock[0] = static_cast<float>(packedCount);
        return result;
    }
} // namespace NorvesLib::Core::Rendering
