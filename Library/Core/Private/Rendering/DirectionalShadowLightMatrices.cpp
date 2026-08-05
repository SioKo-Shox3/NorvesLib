#include "Rendering/DirectionalShadowLightMatrices.h"

#include "Rendering/ShadowMapPass.h"
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

        bool IsFiniteBounds(const BoundingSphere& bounds)
        {
            return std::isfinite(bounds.CenterX) &&
                std::isfinite(bounds.CenterY) &&
                std::isfinite(bounds.CenterZ) &&
                std::isfinite(bounds.Radius);
        }

        bool BuildSkinnedCasterWorldBounds(const SkinnedMeshProxy& proxy, BoundingSphere& outBounds)
        {
            if (!proxy.MeshHandle.IsValid() || proxy.ComponentId == 0 || !proxy.bVisible ||
                !proxy.bCastShadow || !proxy.bHasAnimatedBounds)
            {
                return false;
            }
            const Math::Vector3 localMin = proxy.AnimatedBounds.Min;
            const Math::Vector3 localMax = proxy.AnimatedBounds.Max;
            if (!std::isfinite(localMin.x) || !std::isfinite(localMin.y) || !std::isfinite(localMin.z) ||
                !std::isfinite(localMax.x) || !std::isfinite(localMax.y) || !std::isfinite(localMax.z) ||
                localMin.x > localMax.x || localMin.y > localMax.y || localMin.z > localMax.z)
            {
                return false;
            }
            for (float value : proxy.WorldTransform.values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }

            const Math::Vector3 localCenter = proxy.AnimatedBounds.Center();
            const Math::Vector3 localHalfExtents = proxy.AnimatedBounds.HalfExtents();
            const Math::Matrix4x4& world = proxy.WorldTransform;
            const Math::Vector3 worldCenter(
                localCenter.x * world.m00 + localCenter.y * world.m10 + localCenter.z * world.m20 + world.m30,
                localCenter.x * world.m01 + localCenter.y * world.m11 + localCenter.z * world.m21 + world.m31,
                localCenter.x * world.m02 + localCenter.y * world.m12 + localCenter.z * world.m22 + world.m32);
            const Math::Vector3 worldHalfExtents(
                std::abs(world.m00) * localHalfExtents.x +
                    std::abs(world.m10) * localHalfExtents.y +
                    std::abs(world.m20) * localHalfExtents.z,
                std::abs(world.m01) * localHalfExtents.x +
                    std::abs(world.m11) * localHalfExtents.y +
                    std::abs(world.m21) * localHalfExtents.z,
                std::abs(world.m02) * localHalfExtents.x +
                    std::abs(world.m12) * localHalfExtents.y +
                    std::abs(world.m22) * localHalfExtents.z);
            outBounds.CenterX = worldCenter.x;
            outBounds.CenterY = worldCenter.y;
            outBounds.CenterZ = worldCenter.z;
            outBounds.Radius = Math::VectorUtils::Length(worldHalfExtents);
            return outBounds.IsValid() && IsFiniteBounds(outBounds);
        }

        Math::Vector3 MakeBoundsCenter(const BoundingSphere& bounds)
        {
            return Math::Vector3(bounds.CenterX, bounds.CenterY, bounds.CenterZ);
        }

        float MinFloat(float lhs, float rhs)
        {
            return lhs <= rhs ? lhs : rhs;
        }

        float MaxFloat(float lhs, float rhs)
        {
            return lhs >= rhs ? lhs : rhs;
        }

        void IncludeCasterBounds(const BoundingSphere& bounds,
                                 Math::Vector3& minBounds,
                                 Math::Vector3& maxBounds,
                                 bool& bHasCaster)
        {
            const Math::Vector3 center = MakeBoundsCenter(bounds);
            const Math::Vector3 radius(bounds.Radius, bounds.Radius, bounds.Radius);
            const Math::Vector3 casterMin = center - radius;
            const Math::Vector3 casterMax = center + radius;

            if (!bHasCaster)
            {
                minBounds = casterMin;
                maxBounds = casterMax;
                bHasCaster = true;
                return;
            }

            minBounds.x = MinFloat(minBounds.x, casterMin.x);
            minBounds.y = MinFloat(minBounds.y, casterMin.y);
            minBounds.z = MinFloat(minBounds.z, casterMin.z);
            maxBounds.x = MaxFloat(maxBounds.x, casterMax.x);
            maxBounds.y = MaxFloat(maxBounds.y, casterMax.y);
            maxBounds.z = MaxFloat(maxBounds.z, casterMax.z);
        }

        float CalculateCasterFitRadius(const BoundingSphere& bounds, const Math::Vector3& target)
        {
            return Math::VectorUtils::Length(MakeBoundsCenter(bounds) - target) + bounds.Radius;
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

    DirectionalShadowMatrixSettings MakeDirectionalShadowMatrixSettings(
        const ShadowMapPassSettings& settings)
    {
        DirectionalShadowMatrixSettings result = MakeDefaultDirectionalShadowMatrixSettings();
        result.OrthoSize = settings.OrthoSize;
        result.NearPlane = settings.NearPlane;
        result.FarPlane = settings.FarPlane;
        return result;
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

    bool IsEligibleDirectionalShadowMeshCaster(const MeshProxy& proxy)
    {
        return proxy.IsValid() &&
            proxy.bCastShadow &&
            proxy.WorldBounds.IsValid() &&
            IsFiniteBounds(proxy.WorldBounds);
    }

    bool IsEligibleDirectionalShadowMegaGeometryCaster(const MegaGeometryProxy& proxy)
    {
        return proxy.IsValid() &&
            proxy.bCastShadow &&
            proxy.WorldBounds.IsValid() &&
            IsFiniteBounds(proxy.WorldBounds);
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

    DirectionalShadowMatrixSettings FitDirectionalShadowMatrixSettingsToCasters(
        const DirectionalShadowMatrixSettings& baseSettings,
        const Container::VariableArray<MeshProxy>* meshProxies,
        const Container::VariableArray<SkinnedMeshProxy>* skinnedMeshProxies,
        const Container::VariableArray<MegaGeometryProxy>* megaGeometryProxies)
    {
        DirectionalShadowMatrixSettings result = baseSettings;
        Math::Vector3 minBounds;
        Math::Vector3 maxBounds;
        bool bHasCaster = false;

        if (meshProxies != nullptr)
        {
            for (const MeshProxy& proxy : *meshProxies)
            {
                if (IsEligibleDirectionalShadowMeshCaster(proxy))
                {
                    IncludeCasterBounds(proxy.WorldBounds, minBounds, maxBounds, bHasCaster);
                }
            }
        }

        if (megaGeometryProxies != nullptr)
        {
            for (const MegaGeometryProxy& proxy : *megaGeometryProxies)
            {
                if (IsEligibleDirectionalShadowMegaGeometryCaster(proxy))
                {
                    IncludeCasterBounds(proxy.WorldBounds, minBounds, maxBounds, bHasCaster);
                }
            }
        }

        if (skinnedMeshProxies != nullptr)
        {
            for (const SkinnedMeshProxy& proxy : *skinnedMeshProxies)
            {
                BoundingSphere worldBounds;
                if (BuildSkinnedCasterWorldBounds(proxy, worldBounds))
                {
                    IncludeCasterBounds(worldBounds, minBounds, maxBounds, bHasCaster);
                }
            }
        }

        if (!bHasCaster)
        {
            return result;
        }

        const Math::Vector3 target = (minBounds + maxBounds) * 0.5f;
        float fitRadius = 0.0f;

        if (meshProxies != nullptr)
        {
            for (const MeshProxy& proxy : *meshProxies)
            {
                if (IsEligibleDirectionalShadowMeshCaster(proxy))
                {
                    fitRadius = MaxFloat(fitRadius, CalculateCasterFitRadius(proxy.WorldBounds, target));
                }
            }
        }

        if (megaGeometryProxies != nullptr)
        {
            for (const MegaGeometryProxy& proxy : *megaGeometryProxies)
            {
                if (IsEligibleDirectionalShadowMegaGeometryCaster(proxy))
                {
                    fitRadius = MaxFloat(fitRadius, CalculateCasterFitRadius(proxy.WorldBounds, target));
                }
            }
        }

        if (skinnedMeshProxies != nullptr)
        {
            for (const SkinnedMeshProxy& proxy : *skinnedMeshProxies)
            {
                BoundingSphere worldBounds;
                if (BuildSkinnedCasterWorldBounds(proxy, worldBounds))
                {
                    fitRadius = MaxFloat(fitRadius, CalculateCasterFitRadius(worldBounds, target));
                }
            }
        }

        result.Target = target;
        result.OrthoSize = MaxFloat(baseSettings.OrthoSize, fitRadius);
        result.LightDistance = MaxFloat(baseSettings.LightDistance, fitRadius + baseSettings.NearPlane);
        result.FarPlane = MaxFloat(baseSettings.FarPlane, result.LightDistance + fitRadius);
        return result;
    }

    DirectionalShadowMatrixSettings FitDirectionalShadowMatrixSettingsToCasters(
        const DirectionalShadowMatrixSettings& baseSettings,
        const Container::VariableArray<MeshProxy>* meshProxies,
        const Container::VariableArray<MegaGeometryProxy>* megaGeometryProxies)
    {
        return FitDirectionalShadowMatrixSettingsToCasters(
            baseSettings, meshProxies, nullptr, megaGeometryProxies);
    }

    DirectionalShadowMatrixResult BuildFittedDirectionalShadowLightMatrices(
        const Container::VariableArray<LightProxy>* lightProxies,
        const Container::VariableArray<MeshProxy>* meshProxies,
        const Container::VariableArray<SkinnedMeshProxy>* skinnedMeshProxies,
        const Container::VariableArray<MegaGeometryProxy>* megaGeometryProxies,
        const DirectionalShadowMatrixSettings& baseSettings)
    {
        const DirectionalShadowMatrixSettings fittedSettings =
            FitDirectionalShadowMatrixSettingsToCasters(
                baseSettings, meshProxies, skinnedMeshProxies, megaGeometryProxies);
        return BuildDirectionalShadowLightMatrices(lightProxies, fittedSettings);
    }

    DirectionalShadowMatrixResult BuildFittedDirectionalShadowLightMatrices(
        const Container::VariableArray<LightProxy>* lightProxies,
        const Container::VariableArray<MeshProxy>* meshProxies,
        const Container::VariableArray<MegaGeometryProxy>* megaGeometryProxies,
        const DirectionalShadowMatrixSettings& baseSettings)
    {
        return BuildFittedDirectionalShadowLightMatrices(
            lightProxies, meshProxies, nullptr, megaGeometryProxies, baseSettings);
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
