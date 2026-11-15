#include "Environment/EnvironmentState.h"

#include <cmath>

namespace NorvesLib::Core::Environment
{
    namespace
    {
        float ClampFinite(float value, float fallback, float minimum, float maximum)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            return value < minimum ? minimum : (value > maximum ? maximum : value);
        }

        float ClampMinimumFinite(float value, float fallback, float minimum)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            return value < minimum ? minimum : value;
        }

        float WrapFinite(float value, float fallback, float period)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            if (value >= 0.0f && value < period)
            {
                return value;
            }

            float wrapped = std::fmod(value, period);
            if (wrapped < 0.0f)
            {
                wrapped += period;
            }
            return wrapped >= period ? 0.0f : wrapped;
        }
    } // namespace

    void SetEnvironmentTargetDuration(EnvironmentTarget& target, float seconds, ETransitionCurve curve)
    {
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            target.DurationSeconds[index] = seconds;
            target.Curves[index] = curve;
        }
    }

    EnvironmentState MakeEnvironmentStateFromScene(const Rendering::SkyAtmosphereParameters& sky,
                                                   const Rendering::VolumetricFogParameters& fog,
                                                   const Rendering::CloudLayerParameters& cloudLayer,
                                                   const Rendering::WindParameters& wind)
    {
        EnvironmentState result;
        result.SunAltitudeDegrees = sky.SunAltitudeDegrees;
        result.SunAzimuthDegrees = sky.SunAzimuthDegrees;
        result.FogDensityAtBaseHeight = fog.DensityAtBaseHeight;
        result.FogBaseHeight = fog.BaseHeight;
        result.FogHeightFalloffPerUnit = fog.HeightFalloffPerUnit;
        result.FogMaxDistanceMeters = fog.MaxDistanceMeters;
        result.FogAmbientScale = fog.AmbientScale;
        result.bCloudLayerEnabled = cloudLayer.bEnabled;
        result.CloudLayerCoverage = cloudLayer.Coverage;
        result.WindDirectionYawDegrees = wind.DirectionYawDegrees;
        result.WindBaseSpeed = wind.BaseSpeed;
        result.WindGustStrength = wind.GustStrength;
        return result;
    }

    EnvironmentSceneBase MakeEnvironmentSceneBase(const Rendering::SkyAtmosphereParameters& sky,
                                                   const Rendering::VolumetricFogParameters& fog,
                                                   const Rendering::CloudLayerParameters& cloudLayer,
                                                   const Rendering::WindParameters& wind)
    {
        return EnvironmentSceneBase{sky, fog, cloudLayer, wind};
    }

    EnvironmentState SanitizeEnvironmentState(const EnvironmentState& in)
    {
        const EnvironmentState defaults;
        EnvironmentState result = in;
        result.RainIntensity = ClampFinite(in.RainIntensity, defaults.RainIntensity, 0.0f, 1.0f);
        result.Wetness = ClampFinite(in.Wetness, defaults.Wetness, 0.0f, 1.0f);
        result.CloudCover = ClampFinite(in.CloudCover, defaults.CloudCover, 0.0f, 1.0f);
        result.WindDirectionYawDegrees = WrapFinite(in.WindDirectionYawDegrees, defaults.WindDirectionYawDegrees, 360.0f);
        result.WindBaseSpeed = ClampFinite(in.WindBaseSpeed, defaults.WindBaseSpeed, 0.0f, 100.0f);
        result.WindGustStrength = ClampFinite(in.WindGustStrength, defaults.WindGustStrength, 0.0f, 1.0f);
        result.FogDensityAtBaseHeight = ClampMinimumFinite(in.FogDensityAtBaseHeight, defaults.FogDensityAtBaseHeight, 0.0f);
        result.FogBaseHeight = std::isfinite(in.FogBaseHeight) ? in.FogBaseHeight : defaults.FogBaseHeight;
        result.FogHeightFalloffPerUnit = ClampMinimumFinite(in.FogHeightFalloffPerUnit, defaults.FogHeightFalloffPerUnit, 0.0f);
        result.FogMaxDistanceMeters = ClampMinimumFinite(in.FogMaxDistanceMeters, defaults.FogMaxDistanceMeters, 0.0f);
        result.FogAmbientScale = ClampMinimumFinite(in.FogAmbientScale, defaults.FogAmbientScale, 0.0f);
        result.CloudLayerCoverage = ClampFinite(in.CloudLayerCoverage, defaults.CloudLayerCoverage, 0.0f, 1.0f);
        result.SunAltitudeDegrees = ClampFinite(in.SunAltitudeDegrees, defaults.SunAltitudeDegrees, -90.0f, 90.0f);
        result.SunAzimuthDegrees = ClampFinite(in.SunAzimuthDegrees, defaults.SunAzimuthDegrees, -360.0f, 360.0f);
        result.TimeOfDayHours = WrapFinite(in.TimeOfDayHours, defaults.TimeOfDayHours, 24.0f);
        result.WetTimeConstantSeconds = ClampMinimumFinite(in.WetTimeConstantSeconds, defaults.WetTimeConstantSeconds, 0.001f);
        result.DryTimeConstantSeconds = ClampMinimumFinite(in.DryTimeConstantSeconds, defaults.DryTimeConstantSeconds, 0.001f);
        if (!std::isfinite(in.GameTimeSeconds))
        {
            result.GameTimeSeconds = defaults.GameTimeSeconds;
        }
        else if (in.GameTimeSeconds < 0.0)
        {
            result.GameTimeSeconds = 0.0;
        }
        return result;
    }

    EnvironmentResolved ResolveEnvironment(const EnvironmentState& state, const EnvironmentSceneBase& base)
    {
        EnvironmentResolved result{base.Sky, base.Fog, base.CloudLayer, base.Wind, {}};
        result.Sky.SunAltitudeDegrees = state.SunAltitudeDegrees;
        result.Sky.SunAzimuthDegrees = state.SunAzimuthDegrees;
        result.Fog.DensityAtBaseHeight = state.FogDensityAtBaseHeight;
        result.Fog.BaseHeight = state.FogBaseHeight;
        result.Fog.HeightFalloffPerUnit = state.FogHeightFalloffPerUnit;
        result.Fog.MaxDistanceMeters = state.FogMaxDistanceMeters;
        result.Fog.AmbientScale = state.FogAmbientScale;
        result.CloudLayer.bEnabled = state.bCloudLayerEnabled;
        result.CloudLayer.Coverage = state.CloudLayerCoverage;
        result.Wind.DirectionYawDegrees = state.WindDirectionYawDegrees;
        result.Wind.BaseSpeed = state.WindBaseSpeed;
        result.Wind.GustStrength = state.WindGustStrength;
        result.Environment.bEnabled = true;
        result.Environment.RainIntensity = state.RainIntensity;
        result.Environment.Wetness = state.Wetness;
        result.Environment.CloudCover = state.CloudCover;
        result.Environment.GameTimeSeconds = state.GameTimeSeconds;
        result.Environment.Seed = state.Seed;
        result.Environment.DiscontinuityId = state.DiscontinuityId;
        return result;
    }

    float AdvanceEnvironmentWetness(float wetness, float rainIntensity, double dtSeconds,
                                    float wetTimeConstantSeconds, float dryTimeConstantSeconds)
    {
        if (dtSeconds <= 0.0)
        {
            return wetness;
        }

        const double rain = rainIntensity < 0.0f ? 0.0 : (rainIntensity > 1.0f ? 1.0 : static_cast<double>(rainIntensity));
        const double smoothProgress = (rain - 0.02) / (0.2 - 0.02);
        const double s = smoothProgress < 0.0 ? 0.0 : (smoothProgress > 1.0 ? 1.0 : smoothProgress);
        const double effectiveRain = s * s * (3.0 - 2.0 * s);
        const double wetRate = 3.0 / static_cast<double>(wetTimeConstantSeconds);
        const double dryRate = 3.0 / static_cast<double>(dryTimeConstantSeconds);
        const double k = wetRate * effectiveRain + dryRate * (1.0 - effectiveRain);
        if (k <= 0.0)
        {
            return wetness;
        }

        const double equilibrium = wetRate * effectiveRain / k;
        const double result = equilibrium + (static_cast<double>(wetness) - equilibrium) * std::exp(-dtSeconds * k);
        return static_cast<float>(result < 0.0 ? 0.0 : (result > 1.0 ? 1.0 : result));
    }

    float EvaluateTransitionCurve(ETransitionCurve curve, float progress)
    {
        const float s = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
        if (curve == ETransitionCurve::Linear)
        {
            return s;
        }
        return s * s * (3.0f - 2.0f * s);
    }

    float GetEnvironmentField(const EnvironmentState& state, EEnvironmentField field)
    {
        switch (field)
        {
        case EEnvironmentField::RainIntensity: return state.RainIntensity;
        case EEnvironmentField::CloudCover: return state.CloudCover;
        case EEnvironmentField::WindDirectionYaw: return state.WindDirectionYawDegrees;
        case EEnvironmentField::WindBaseSpeed: return state.WindBaseSpeed;
        case EEnvironmentField::WindGustStrength: return state.WindGustStrength;
        case EEnvironmentField::FogDensityAtBaseHeight: return state.FogDensityAtBaseHeight;
        case EEnvironmentField::FogBaseHeight: return state.FogBaseHeight;
        case EEnvironmentField::FogHeightFalloffPerUnit: return state.FogHeightFalloffPerUnit;
        case EEnvironmentField::FogMaxDistanceMeters: return state.FogMaxDistanceMeters;
        case EEnvironmentField::FogAmbientScale: return state.FogAmbientScale;
        case EEnvironmentField::CloudLayerEnabled: return state.bCloudLayerEnabled ? 1.0f : 0.0f;
        case EEnvironmentField::CloudLayerCoverage: return state.CloudLayerCoverage;
        case EEnvironmentField::SunAltitude: return state.SunAltitudeDegrees;
        case EEnvironmentField::SunAzimuth: return state.SunAzimuthDegrees;
        case EEnvironmentField::TimeOfDayHours: return state.TimeOfDayHours;
        case EEnvironmentField::Wetness: return state.Wetness;
        default: return 0.0f;
        }
    }

    void SetEnvironmentField(EnvironmentState& state, EEnvironmentField field, float value)
    {
        switch (field)
        {
        case EEnvironmentField::RainIntensity: state.RainIntensity = value; break;
        case EEnvironmentField::CloudCover: state.CloudCover = value; break;
        case EEnvironmentField::WindDirectionYaw: state.WindDirectionYawDegrees = value; break;
        case EEnvironmentField::WindBaseSpeed: state.WindBaseSpeed = value; break;
        case EEnvironmentField::WindGustStrength: state.WindGustStrength = value; break;
        case EEnvironmentField::FogDensityAtBaseHeight: state.FogDensityAtBaseHeight = value; break;
        case EEnvironmentField::FogBaseHeight: state.FogBaseHeight = value; break;
        case EEnvironmentField::FogHeightFalloffPerUnit: state.FogHeightFalloffPerUnit = value; break;
        case EEnvironmentField::FogMaxDistanceMeters: state.FogMaxDistanceMeters = value; break;
        case EEnvironmentField::FogAmbientScale: state.FogAmbientScale = value; break;
        case EEnvironmentField::CloudLayerEnabled: state.bCloudLayerEnabled = value >= 0.5f; break;
        case EEnvironmentField::CloudLayerCoverage: state.CloudLayerCoverage = value; break;
        case EEnvironmentField::SunAltitude: state.SunAltitudeDegrees = value; break;
        case EEnvironmentField::SunAzimuth: state.SunAzimuthDegrees = value; break;
        case EEnvironmentField::TimeOfDayHours: state.TimeOfDayHours = value; break;
        case EEnvironmentField::Wetness: state.Wetness = value; break;
        default: break;
        }
    }
} // namespace NorvesLib::Core::Environment
