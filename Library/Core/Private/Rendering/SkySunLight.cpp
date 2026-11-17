#include "Rendering/SkySunLight.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{

    bool MakeSkySunLightProxy(const SkyAtmosphereParameters& parameters, LightProxy& outLight)
    {
        const SkyAtmosphereParameters sanitized = SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled || sanitized.SunAltitudeDegrees < 0.0f)
        {
            return false;
        }
        const Math::Vector3 illuminance = ComputeSunGroundIlluminance(sanitized);
        const float luminance = 0.2126f * illuminance.x + 0.7152f * illuminance.y +
                                0.0722f * illuminance.z;
        // 雲の直達の係数には下限があるので、曇り度では 0 にならない（0 になるのは透過率が 0 のときだけ）。
        if (!std::isfinite(luminance) || luminance <= 0.0f)
        {
            return false;
        }
        const Math::Vector3 sunDirection = MakeSunDirectionFromAltitudeAzimuth(
            sanitized.SunAltitudeDegrees, sanitized.SunAzimuthDegrees);

        LightProxy light;
        light.LightId = SkySunLightId;
        light.Type = LightType::Directional;
        light.DirectionX = -sunDirection.x;
        light.DirectionY = -sunDirection.y;
        light.DirectionZ = -sunDirection.z;
        // 色は輝度1へ正規化して詰められるので、比の情報（透過率の色）だけを持たせる。
        light.ColorR = illuminance.x / luminance;
        light.ColorG = illuminance.y / luminance;
        light.ColorB = illuminance.z / luminance;
        light.CanonicalIntensity = luminance;
        light.bCastShadows = true;
        light.bVisible = true;
        outLight = light;
        return true;
    }

    bool MakeSkyMoonLightProxy(const SkyAtmosphereParameters& parameters, LightProxy& outLight)
    {
        const SkyAtmosphereParameters sanitized = SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled || !std::isfinite(sanitized.MoonIlluminanceLux) ||
            sanitized.MoonIlluminanceLux <= 0.0f)
        {
            return false;
        }
        const float luminance = 0.2126f * sanitized.MoonColor.x + 0.7152f * sanitized.MoonColor.y +
                                0.0722f * sanitized.MoonColor.z;
        if (!std::isfinite(luminance) || luminance <= 0.0f)
        {
            return false;
        }
        const Math::Vector3 moonDirection = MakeSunDirectionFromAltitudeAzimuth(
            sanitized.MoonAltitudeDegrees, sanitized.MoonAzimuthDegrees);

        LightProxy light;
        light.LightId = SkyMoonLightId;
        light.Type = LightType::Directional;
        light.DirectionX = -moonDirection.x;
        light.DirectionY = -moonDirection.y;
        light.DirectionZ = -moonDirection.z;
        light.ColorR = sanitized.MoonColor.x / luminance;
        light.ColorG = sanitized.MoonColor.y / luminance;
        light.ColorB = sanitized.MoonColor.z / luminance;
        light.CanonicalIntensity = sanitized.MoonIlluminanceLux;
        light.bCastShadows = sanitized.bMoonCastShadows;
        light.bVisible = true;
        outLight = light;
        return true;
    }

    bool MakeSkyDirectionalLightProxy(const SkyAtmosphereParameters& parameters, LightProxy& outLight)
    {
        return MakeSkySunLightProxy(parameters, outLight) || MakeSkyMoonLightProxy(parameters, outLight);
    }

    void ReplaceSkySunLight(const SkyAtmosphereParameters& parameters,
                            Container::VariableArray<LightProxy>& lights)
    {
        lights.erase(std::remove_if(lights.begin(), lights.end(),
                                    [](const LightProxy& light)
                                    {
                                        return IsSkySunLight(light) || IsSkyMoonLight(light);
                                    }),
                     lights.end());
        LightProxy sun;
        if (MakeSkyDirectionalLightProxy(parameters, sun))
        {
            lights.push_back(sun);
        }
    }

} // namespace NorvesLib::Core::Rendering
