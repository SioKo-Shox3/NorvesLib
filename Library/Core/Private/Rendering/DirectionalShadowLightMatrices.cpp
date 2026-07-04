#include "Rendering/DirectionalShadowLightMatrices.h"

#include "Math/MatrixUtils.h"
#include "Math/MathTypes.h"
#include "Math/VectorUtils.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        bool IsFiniteDirection(const Math::Vector3& direction)
        {
            return std::isfinite(direction.x) &&
                std::isfinite(direction.y) &&
                std::isfinite(direction.z);
        }

        bool IsNonZeroDirection(const Math::Vector3& direction)
        {
            return Math::VectorUtils::Length(direction) > Math::Constants::EPSILON;
        }

        Math::Vector3 MakeDirection(const LightProxy& proxy)
        {
            return Math::Vector3(proxy.DirectionX, proxy.DirectionY, proxy.DirectionZ);
        }

        Math::Vector3 SelectStableUpVector(const Math::Vector3& normalizedDirection)
        {
            const Math::Vector3 defaultUp(0.0f, 1.0f, 0.0f);
            const float defaultUpAlignment =
                std::abs(Math::VectorUtils::Dot(normalizedDirection, defaultUp));
            if (defaultUpAlignment <= 0.98f)
            {
                return defaultUp;
            }

            return Math::Vector3(1.0f, 0.0f, 0.0f);
        }
    } // namespace

    DirectionalShadowMatrixSettings MakeDefaultDirectionalShadowMatrixSettings()
    {
        return DirectionalShadowMatrixSettings{};
    }

    bool IsEligibleDirectionalShadowLight(const LightProxy& proxy)
    {
        const Math::Vector3 direction = MakeDirection(proxy);
        return proxy.IsValid() &&
            proxy.Type == LightType::Directional &&
            proxy.bCastShadows &&
            IsFiniteDirection(direction) &&
            IsNonZeroDirection(direction);
    }

    const LightProxy* SelectDirectionalShadowLight(
        const Container::VariableArray<LightProxy>* lightProxies)
    {
        if (lightProxies == nullptr)
        {
            return nullptr;
        }

        for (const LightProxy& proxy : *lightProxies)
        {
            if (IsEligibleDirectionalShadowLight(proxy))
            {
                return &proxy;
            }
        }

        return nullptr;
    }

    uint32_t CountShaderVisibleDirectionalLights(
        const Container::VariableArray<LightProxy>* lightProxies)
    {
        if (lightProxies == nullptr)
        {
            return 0;
        }

        uint32_t count = 0;
        for (const LightProxy& proxy : *lightProxies)
        {
            if (proxy.IsValid() && proxy.Type == LightType::Directional)
            {
                ++count;
            }
        }

        return count;
    }

    DirectionalShadowMatrixResult BuildDirectionalShadowLightMatrices(
        const Container::VariableArray<LightProxy>* lightProxies,
        const DirectionalShadowMatrixSettings& settings)
    {
        DirectionalShadowMatrixResult result;
        const uint32_t shaderVisibleDirectionalCount =
            CountShaderVisibleDirectionalLights(lightProxies);
        result.bHasMultipleDirectionalLights = shaderVisibleDirectionalCount > 1;

        if (shaderVisibleDirectionalCount != 1)
        {
            return result;
        }

        const LightProxy* selectedLight = SelectDirectionalShadowLight(lightProxies);
        if (selectedLight == nullptr)
        {
            return result;
        }

        const Math::Vector3 normalizedDirection =
            Math::VectorUtils::Normalize(MakeDirection(*selectedLight));
        const Math::Vector3 lightPosition =
            settings.Target - normalizedDirection * settings.LightDistance;
        const Math::Vector3 up = SelectStableUpVector(normalizedDirection);

        result.bEnabled = true;
        result.LightId = selectedLight->LightId;
        result.Direction = normalizedDirection;
        result.LightPosition = lightPosition;
        result.View = Math::MatrixUtils::CreateLookAt(lightPosition, settings.Target, up);
        result.Projection = Math::MatrixUtils::CreateOrthographic(
            settings.OrthoSize * 2.0f,
            settings.OrthoSize * 2.0f,
            settings.NearPlane,
            settings.FarPlane);

        return result;
    }

    void CopyShadowMatrixToShaderData(const Math::Matrix4x4& matrix, float* outData)
    {
        Math::MatrixUtils::TransposeToShaderData(matrix, outData);
    }

    void CopyIdentityShadowMatricesToShaderData(float* outView, float* outProjection)
    {
        CopyShadowMatrixToShaderData(Math::Matrix4x4::Identity, outView);
        CopyShadowMatrixToShaderData(Math::Matrix4x4::Identity, outProjection);
    }
} // namespace NorvesLib::Core::Rendering
