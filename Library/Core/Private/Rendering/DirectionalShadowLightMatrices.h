#pragma once

#include "Rendering/SceneProxy.h"
#include "Container/Containers.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct DirectionalShadowMatrixSettings
    {
        float OrthoSize = 20.0f;
        float NearPlane = 0.1f;
        float FarPlane = 50.0f;
        float LightDistance = 20.0f;
        Math::Vector3 Target = Math::Vector3(0.0f, 0.0f, 0.0f);
    };

    struct DirectionalShadowMatrixResult
    {
        bool bEnabled = false;
        bool bHasMultipleDirectionalLights = false;
        uint64_t LightId = 0;
        Math::Vector3 Direction = Math::Vector3(0.0f, -1.0f, 0.0f);
        Math::Vector3 LightPosition = Math::Vector3(0.0f, 0.0f, 0.0f);
        Math::Matrix4x4 View = Math::Matrix4x4::Identity;
        Math::Matrix4x4 Projection = Math::Matrix4x4::Identity;
    };

    DirectionalShadowMatrixSettings MakeDefaultDirectionalShadowMatrixSettings();

    bool IsEligibleDirectionalShadowLight(const LightProxy& proxy);

    const LightProxy* SelectDirectionalShadowLight(
        const Container::VariableArray<LightProxy>* lightProxies);

    uint32_t CountShaderVisibleDirectionalLights(
        const Container::VariableArray<LightProxy>* lightProxies);

    DirectionalShadowMatrixResult BuildDirectionalShadowLightMatrices(
        const Container::VariableArray<LightProxy>* lightProxies,
        const DirectionalShadowMatrixSettings& settings);

    void CopyShadowMatrixToShaderData(const Math::Matrix4x4& matrix, float* outData);

    void CopyIdentityShadowMatricesToShaderData(float* outView, float* outProjection);
} // namespace NorvesLib::Core::Rendering
