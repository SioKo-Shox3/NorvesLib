#include "Rendering/SkySunLight.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{

    bool MakeSkySunLightProxy(const SkyAtmosphereParameters& parameters, LightProxy& outLight)
    {
        const SkyAtmosphereParameters sanitized = SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled)
        {
            return false;
        }
        const Math::Vector3 illuminance = ComputeSunGroundIlluminance(sanitized);
        const float luminance = 0.2126f * illuminance.x + 0.7152f * illuminance.y +
                                0.0722f * illuminance.z;
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

    void ReplaceSkySunLight(const SkyAtmosphereParameters& parameters,
                            Container::VariableArray<LightProxy>& lights)
    {
        lights.erase(std::remove_if(lights.begin(), lights.end(),
                                    [](const LightProxy& light) { return IsSkySunLight(light); }),
                     lights.end());
        LightProxy sun;
        if (MakeSkySunLightProxy(parameters, sun))
        {
            lights.push_back(sun);
        }
    }

} // namespace NorvesLib::Core::Rendering
