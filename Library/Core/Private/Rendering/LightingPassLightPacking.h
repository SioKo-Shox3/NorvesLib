#pragma once

#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/SceneProxy.h"
#include "Container/Containers.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    inline double ComputeLocalInverseSquare(double distance)
    {
        const double distanceSquared = distance * distance;
        const double minimumDistanceSquared = 0.01 * 0.01;
        const double clampedDistanceSquared = distanceSquared > minimumDistanceSquared
                                                   ? distanceSquared
                                                   : minimumDistanceSquared;
        return 1.0 / clampedDistanceSquared;
    }

    inline double ComputeRangeWindow(double distance, double range)
    {
        const double safeRange = range > 0.0001 ? range : 0.0001;
        const double ratio = distance / safeRange;
        const double unclampedWindow = 1.0 - ratio * ratio * ratio * ratio;
        const double window = unclampedWindow > 0.0 ? unclampedWindow : 0.0;
        return window * window;
    }

    inline bool PackLightingPassLight(const LightProxy& proxy, GPULightData& outLight)
    {
        if (!proxy.IsValid())
        {
            return false;
        }

        if (!std::isfinite(proxy.ColorR) || !std::isfinite(proxy.ColorG) ||
            !std::isfinite(proxy.ColorB) || proxy.ColorR < 0.0f ||
            proxy.ColorG < 0.0f || proxy.ColorB < 0.0f)
        {
            return false;
        }

        outLight = {};
        outLight.position[0] = proxy.PositionX;
        outLight.position[1] = proxy.PositionY;
        outLight.position[2] = proxy.PositionZ;
        outLight.position[3] = static_cast<float>(static_cast<int>(proxy.Type));

        outLight.direction[0] = proxy.DirectionX;
        outLight.direction[1] = proxy.DirectionY;
        outLight.direction[2] = proxy.DirectionZ;
        outLight.direction[3] = proxy.InnerConeAngle;

        const double luminance = 0.2126 * static_cast<double>(proxy.ColorR) +
                                 0.7152 * static_cast<double>(proxy.ColorG) +
                                 0.0722 * static_cast<double>(proxy.ColorB);
        if (luminance <= 1.0e-6)
        {
            return false;
        }

        outLight.chromaticityAndIntensity[0] =
            static_cast<float>(static_cast<double>(proxy.ColorR) / luminance);
        outLight.chromaticityAndIntensity[1] =
            static_cast<float>(static_cast<double>(proxy.ColorG) / luminance);
        outLight.chromaticityAndIntensity[2] =
            static_cast<float>(static_cast<double>(proxy.ColorB) / luminance);
        outLight.chromaticityAndIntensity[3] = proxy.CanonicalIntensity;

        outLight.attenuation[0] = proxy.Range;
        outLight.attenuation[1] = proxy.OuterConeAngle;
        outLight.attenuation[2] = 0.0f;
        outLight.attenuation[3] = 0.0f;
        return true;
    }

    inline uint32_t PackLightingPassLights(Container::Span<const LightProxy> lightProxies,
                                           Container::VariableArray<GPULightData>& outLights)
    {
        outLights.clear();

        for (const LightProxy& proxy : lightProxies)
        {
            GPULightData light = {};
            if (PackLightingPassLight(proxy, light))
            {
                outLights.push_back(light);
            }
        }

        return static_cast<uint32_t>(outLights.size());
    }

} // namespace NorvesLib::Core::Rendering
