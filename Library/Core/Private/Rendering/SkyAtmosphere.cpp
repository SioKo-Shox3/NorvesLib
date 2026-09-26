#include "Rendering/SkyAtmosphere.h"

#include "Math/MathTypes.h"
#include "Math/VectorUtils.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDegreesToRadians = kPi / 180.0f;
        constexpr float kFp16Max = 65504.0f;

        constexpr float kRayleighScatteringR = 5.802e-6f;
        constexpr float kRayleighScatteringG = 13.558e-6f;
        constexpr float kRayleighScatteringB = 33.100e-6f;
        constexpr float kMieScattering = 3.996e-6f;

        float ClampFinite(float value, float minimum, float maximum, float fallback)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        float ClampPositiveFinite(float value, float minimum, float maximum, float fallback)
        {
            if (!std::isfinite(value) || value <= 0.0f)
            {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        bool IsFiniteVector(const Math::Vector3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        Math::Vector3 NormalizeDirection(const Math::Vector3& value)
        {
            if (!IsFiniteVector(value) ||
                Math::VectorUtils::Length(value) <= Math::Constants::EPSILON)
            {
                return Math::Vector3::Zero;
            }
            return Math::VectorUtils::Normalize(value);
        }

        Math::Vector3 MakeSunDirection(const SkyAtmosphereParameters& parameters)
        {
            const float altitude = parameters.SunAltitudeDegrees * kDegreesToRadians;
            const float azimuth = parameters.SunAzimuthDegrees * kDegreesToRadians;
            const float horizontal = std::cos(altitude);
            return NormalizeDirection(Math::Vector3(horizontal * std::cos(azimuth),
                                                    std::sin(altitude),
                                                    horizontal * std::sin(azimuth)));
        }

        float ComputeHenyeyGreensteinPhase(float cosine, float anisotropy)
        {
            const float g2 = anisotropy * anisotropy;
            const float denominator = std::max(1.0f + g2 - 2.0f * anisotropy * cosine, 0.01f);
            return (1.0f - g2) /
                (4.0f * kPi * denominator * std::sqrt(denominator));
        }

        float ComputeRayleighPhase(float cosine)
        {
            return 3.0f * (1.0f + cosine * cosine) / (16.0f * kPi);
        }

        float ComputeTransmittance(float scatteringCoefficient,
                                   float scaleHeight,
                                   float cosine)
        {
            const float opticalDepth = scatteringCoefficient * scaleHeight /
                std::max(cosine, 0.05f);
            return std::exp(-opticalDepth);
        }

        // 透過率LUTの1層分の減衰。LUTの値を変えないよう、演算の順序をLUTの生成と揃える。
        float ComputeLayerTransmittance(float scattering,
                                        float scaleHeight,
                                        float density,
                                        float cosine)
        {
            const float opticalDepth = scattering * scaleHeight * density /
                                       std::max(cosine, 0.05f);
            return std::clamp(std::exp(-std::max(opticalDepth, 0.0f)), 0.0f, 1.0f);
        }

        Math::Vector3 ComputeAtmosphereTransmittanceFromSanitized(
            const SkyAtmosphereParameters& sanitized,
            float altitudeFraction,
            float cosine)
        {
            const float altitude = std::isfinite(altitudeFraction)
                ? std::clamp(altitudeFraction, 0.0f, 1.0f)
                : 0.0f;
            const float safeCosine = std::isfinite(cosine) ? cosine : 0.0f;
            const float rayleighDensity =
                std::exp(-altitude * sanitized.AtmosphereHeightMeters /
                         sanitized.RayleighScaleHeightMeters);
            const float mieDensity =
                std::exp(-altitude * sanitized.AtmosphereHeightMeters /
                         sanitized.MieScaleHeightMeters);
            const float mie = ComputeLayerTransmittance(kMieScattering,
                                                        sanitized.MieScaleHeightMeters,
                                                        mieDensity,
                                                        safeCosine);
            return Math::Vector3(
                ComputeLayerTransmittance(kRayleighScatteringR,
                                          sanitized.RayleighScaleHeightMeters,
                                          rayleighDensity,
                                          safeCosine) * mie,
                ComputeLayerTransmittance(kRayleighScatteringG,
                                          sanitized.RayleighScaleHeightMeters,
                                          rayleighDensity,
                                          safeCosine) * mie,
                ComputeLayerTransmittance(kRayleighScatteringB,
                                          sanitized.RayleighScaleHeightMeters,
                                          rayleighDensity,
                                          safeCosine) * mie);
        }

        float ComputeSunDiskIrradianceFromSanitized(
            const SkyAtmosphereParameters& sanitized)
        {
            if (!sanitized.bEnabled)
            {
                return 0.0f;
            }
            return sanitized.SunLuminanceNits * SolarDiskSolidAngleSteradians;
        }
    } // namespace

    SkyAtmosphereParameters MakeDefaultSkyAtmosphereParameters()
    {
        return SkyAtmosphereParameters{};
    }

    SkyAtmosphereParameters SanitizeSkyAtmosphereParameters(
        const SkyAtmosphereParameters& parameters)
    {
        SkyAtmosphereParameters result = MakeDefaultSkyAtmosphereParameters();
        result.bEnabled = parameters.bEnabled;
        result.SunAltitudeDegrees = ClampFinite(parameters.SunAltitudeDegrees,
                                                0.0f,
                                                90.0f,
                                                result.SunAltitudeDegrees);
        result.SunAzimuthDegrees = ClampFinite(parameters.SunAzimuthDegrees,
                                               -360.0f,
                                               360.0f,
                                               result.SunAzimuthDegrees);
        result.SunLuminanceNits = ClampPositiveFinite(parameters.SunLuminanceNits,
                                                      1.0e3f,
                                                      2.0e9f,
                                                      result.SunLuminanceNits);
        result.PlanetRadiusMeters = ClampPositiveFinite(parameters.PlanetRadiusMeters,
                                                        1.0e6f,
                                                        1.0e8f,
                                                        result.PlanetRadiusMeters);
        result.AtmosphereHeightMeters = ClampPositiveFinite(parameters.AtmosphereHeightMeters,
                                                            1.0e3f,
                                                            2.0e5f,
                                                            result.AtmosphereHeightMeters);
        result.RayleighScaleHeightMeters = ClampPositiveFinite(parameters.RayleighScaleHeightMeters,
                                                               1.0e2f,
                                                               5.0e4f,
                                                               result.RayleighScaleHeightMeters);
        result.MieScaleHeightMeters = ClampPositiveFinite(parameters.MieScaleHeightMeters,
                                                          1.0e2f,
                                                          5.0e4f,
                                                          result.MieScaleHeightMeters);
        result.MieAnisotropy = ClampFinite(parameters.MieAnisotropy,
                                           -0.9f,
                                           0.9f,
                                           result.MieAnisotropy);
        result.GroundAlbedo.x = ClampFinite(parameters.GroundAlbedo.x,
                                             0.0f,
                                             1.0f,
                                             result.GroundAlbedo.x);
        result.GroundAlbedo.y = ClampFinite(parameters.GroundAlbedo.y,
                                             0.0f,
                                             1.0f,
                                             result.GroundAlbedo.y);
        result.GroundAlbedo.z = ClampFinite(parameters.GroundAlbedo.z,
                                             0.0f,
                                             1.0f,
                                             result.GroundAlbedo.z);
        return result;
    }

    Math::Vector3 MakeSunDirectionFromAltitudeAzimuth(float altitudeDegrees,
                                                       float azimuthDegrees)
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.SunAltitudeDegrees = altitudeDegrees;
        parameters.SunAzimuthDegrees = azimuthDegrees;
        return MakeSunDirection(SanitizeSkyAtmosphereParameters(parameters));
    }

    SkyRadianceSample EvaluateHillaireSkyReference(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection)
    {
        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        SkyRadianceSample result;
        if (!sanitized.bEnabled)
        {
            return result;
        }

        const Math::Vector3 view = NormalizeDirection(viewDirection);
        if (view == Math::Vector3::Zero)
        {
            return result;
        }

        result.bValid = true;
        if (view.y <= 0.0f)
        {
            result.Radiance = Math::Vector3::Zero;
            result.MeanSunTransmittance = 0.0f;
            return result;
        }

        const Math::Vector3 sunDirection = MakeSunDirection(sanitized);
        const float cosineToSun = std::clamp(
            Math::VectorUtils::Dot(view, sunDirection), -1.0f, 1.0f);
        const float rayleighPhase = ComputeRayleighPhase(cosineToSun);
        const float miePhase = ComputeHenyeyGreensteinPhase(
            cosineToSun, sanitized.MieAnisotropy);
        const float viewCosine = std::max(view.y, 0.05f);
        const float sunCosine = std::max(sunDirection.y, 0.05f);

        const Math::Vector3 sunTransmittance(
            ComputeTransmittance(kRayleighScatteringR,
                                 sanitized.RayleighScaleHeightMeters,
                                 sunCosine) *
                ComputeTransmittance(kMieScattering,
                                     sanitized.MieScaleHeightMeters,
                                     sunCosine),
            ComputeTransmittance(kRayleighScatteringG,
                                 sanitized.RayleighScaleHeightMeters,
                                 sunCosine) *
                ComputeTransmittance(kMieScattering,
                                     sanitized.MieScaleHeightMeters,
                                     sunCosine),
            ComputeTransmittance(kRayleighScatteringB,
                                 sanitized.RayleighScaleHeightMeters,
                                 sunCosine) *
                ComputeTransmittance(kMieScattering,
                                     sanitized.MieScaleHeightMeters,
                                     sunCosine));

        const Math::Vector3 rayleighScatter(
            kRayleighScatteringR * sanitized.RayleighScaleHeightMeters / viewCosine *
                rayleighPhase * sunTransmittance.x,
            kRayleighScatteringG * sanitized.RayleighScaleHeightMeters / viewCosine *
                rayleighPhase * sunTransmittance.y,
            kRayleighScatteringB * sanitized.RayleighScaleHeightMeters / viewCosine *
                rayleighPhase * sunTransmittance.z);
        const float mieScatter = kMieScattering * sanitized.MieScaleHeightMeters /
            viewCosine * miePhase;
        const Math::Vector3 scatteredRadiance(
            rayleighScatter.x + mieScatter * sunTransmittance.x,
            rayleighScatter.y + mieScatter * sunTransmittance.y,
            rayleighScatter.z + mieScatter * sunTransmittance.z);
        const float solarDiskIrradianceScale =
            ComputeSunDiskIrradianceFromSanitized(sanitized);
        result.Radiance = scatteredRadiance * solarDiskIrradianceScale;
        result.MeanSunTransmittance =
            (sunTransmittance.x + sunTransmittance.y + sunTransmittance.z) / 3.0f;

        if (!IsFiniteVector(result.Radiance) || !std::isfinite(result.MeanSunTransmittance))
        {
            return SkyRadianceSample{};
        }
        return result;
    }

    SkyRadianceSample EvaluateSkyViewRadiance(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection)
    {
        SkyRadianceSample sample = EvaluateHillaireSkyReference(parameters, viewDirection);
        if (!sample.bValid)
        {
            return sample;
        }
        const Math::Vector3 view = NormalizeDirection(viewDirection);
        const Math::Vector3 transmittance = ComputeAtmosphereTransmittanceFromSanitized(
            SanitizeSkyAtmosphereParameters(parameters), 0.0f, std::max(view.y, 0.0f));
        sample.Radiance = Math::Vector3(sample.Radiance.x * transmittance.x,
                                        sample.Radiance.y * transmittance.y,
                                        sample.Radiance.z * transmittance.z);
        return sample;
    }

    float ComputeSunDiskIrradiance(
        const SkyAtmosphereParameters& parameters)
    {
        return ComputeSunDiskIrradianceFromSanitized(
            SanitizeSkyAtmosphereParameters(parameters));
    }

    Math::Vector3 ComputeAtmosphereTransmittance(
        const SkyAtmosphereParameters& parameters,
        float altitudeFraction,
        float cosine)
    {
        return ComputeAtmosphereTransmittanceFromSanitized(
            SanitizeSkyAtmosphereParameters(parameters), altitudeFraction, cosine);
    }

    Math::Vector3 ComputeSunGroundTransmittance(
        const SkyAtmosphereParameters& parameters)
    {
        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled)
        {
            return Math::Vector3::Zero;
        }
        return ComputeAtmosphereTransmittanceFromSanitized(
            sanitized, 0.0f, MakeSunDirection(sanitized).y);
    }

    Math::Vector3 ComputeSunGroundIlluminance(
        const SkyAtmosphereParameters& parameters)
    {
        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled)
        {
            return Math::Vector3::Zero;
        }
        const Math::Vector3 transmittance = ComputeAtmosphereTransmittanceFromSanitized(
            sanitized, 0.0f, MakeSunDirection(sanitized).y);
        const float irradiance = ComputeSunDiskIrradianceFromSanitized(sanitized);
        return Math::Vector3(transmittance.x * irradiance,
                             transmittance.y * irradiance,
                             transmittance.z * irradiance);
    }

    float ComputeSunDiskPreExposedLuminance(
        const SkyAtmosphereParameters& parameters,
        float preExposure)
    {
        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled || !std::isfinite(preExposure) || preExposure <= 0.0f)
        {
            return 0.0f;
        }
        return sanitized.SunLuminanceNits * preExposure;
    }

    bool IsSunDiskWithinFp16SafetyRange(
        const SkyAtmosphereParameters& parameters,
        float preExposure,
        float safetyFraction)
    {
        if (!std::isfinite(safetyFraction) || safetyFraction <= 0.0f || safetyFraction > 1.0f)
        {
            return false;
        }
        const float preExposedLuminance =
            ComputeSunDiskPreExposedLuminance(parameters, preExposure);
        return std::isfinite(preExposedLuminance) &&
            preExposedLuminance <= kFp16Max * safetyFraction;
    }
} // namespace NorvesLib::Core::Rendering
