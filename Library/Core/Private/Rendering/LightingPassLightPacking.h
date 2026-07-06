#pragma once

#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/SceneProxy.h"
#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    inline GPULightData MakeDefaultLightingPassLight()
    {
        GPULightData light = {};
        light.position[3] = static_cast<float>(static_cast<int>(LightType::Directional));
        light.direction[0] = -0.577f;
        light.direction[1] = -0.577f;
        light.direction[2] = -0.577f;
        light.color[0] = 1.0f;
        light.color[1] = 1.0f;
        light.color[2] = 1.0f;
        light.color[3] = 1.0f;
        light.attenuation[0] = 100.0f;
        return light;
    }

    inline bool PackLightingPassLight(const LightProxy& proxy, GPULightData& outLight)
    {
        if (!proxy.IsValid())
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

        outLight.color[0] = proxy.ColorR;
        outLight.color[1] = proxy.ColorG;
        outLight.color[2] = proxy.ColorB;
        outLight.color[3] = proxy.Intensity;

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

        if (outLights.empty())
        {
            outLights.push_back(MakeDefaultLightingPassLight());
        }

        return static_cast<uint32_t>(outLights.size());
    }

} // namespace NorvesLib::Core::Rendering
