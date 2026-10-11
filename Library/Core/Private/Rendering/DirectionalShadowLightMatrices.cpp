#include "Rendering/DirectionalShadowLightMatrices.h"

#include "Rendering/ShadowCasterRanges.h"
#include "Rendering/ShadowMapPass.h"
#include "Rendering/SkySunLight.h"
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
            const Math::Vector3 worldCenter =
                Math::MatrixUtils::TransformPointRowVector(world, localCenter);
            const Math::Vector3 worldHalfExtents =
                Math::MatrixUtils::AbsUpper3x3TransformExtentsRowVector(world, localHalfExtents);
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

    const LightProxy* SelectShadowedDirectionalLight(
        const Container::VariableArray<LightProxy>* lightProxies)
    {
        if (lightProxies == nullptr)
        {
            return nullptr;
        }

        for (const LightProxy& proxy : *lightProxies)
        {
            if (IsSkySunLight(proxy) && IsEligibleDirectionalShadowLight(proxy))
            {
                return &proxy;
            }
        }

        if (CountShaderVisibleDirectionalLights(lightProxies) != 1u)
        {
            return nullptr;
        }
        return SelectDirectionalShadowLight(lightProxies);
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

    void CollectDirectionalShadowCasterBounds(
        const Container::VariableArray<MeshProxy>* meshProxies,
        const Container::VariableArray<SkinnedMeshProxy>* skinnedMeshProxies,
        const Container::VariableArray<MegaGeometryProxy>* megaGeometryProxies,
        Container::VariableArray<BoundingSphere>& outBounds)
    {
        outBounds.clear();
        if (meshProxies != nullptr)
        {
            for (const MeshProxy& proxy : *meshProxies)
            {
                if (IsEligibleDirectionalShadowMeshCaster(proxy))
                {
                    outBounds.push_back(proxy.WorldBounds);
                }
            }
        }
        if (megaGeometryProxies != nullptr)
        {
            for (const MegaGeometryProxy& proxy : *megaGeometryProxies)
            {
                if (IsEligibleDirectionalShadowMegaGeometryCaster(proxy))
                {
                    outBounds.push_back(proxy.WorldBounds);
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
                    outBounds.push_back(worldBounds);
                }
            }
        }
    }

    bool BuildSkinnedShadowCasterWorldBounds(const SkinnedMeshProxy& proxy,
                                             BoundingSphere& outBounds)
    {
        return BuildSkinnedCasterWorldBounds(proxy, outBounds);
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

    // ========================================
    // 影を落とす物の範囲の AABB（ShadowCasterRangeSet）
    // ========================================

    void ShadowCasterRangeSet::Clear()
    {
        m_Items.clear();
        m_Ranges.clear();
        m_Count = 0;
    }

    uint64_t ShadowCasterRangeSet::MakeRangeKey(const BoundingSphere& bounds)
    {
        // 球の中心が入る格子の番号（各軸 21 ビットへ丸めて詰める。遠すぎる位置は端の格子へ寄せる）
        const float centers[3] = {bounds.CenterX, bounds.CenterY, bounds.CenterZ};
        uint64_t key = 0;
        for (const float center : centers)
        {
            const float cell = std::floor(center / RangeSizeMeters);
            const float clamped = cell < -1048576.0f ? -1048576.0f : (cell > 1048575.0f ? 1048575.0f : cell);
            key = (key << 21) | (static_cast<uint64_t>(static_cast<int64_t>(clamped) + 1048576) & 0x1FFFFFull);
        }
        return key;
    }

    bool ShadowCasterRangeSet::Set(uint32_t id, const BoundingSphere& bounds)
    {
        if (!bounds.IsValid() || !IsFiniteBounds(bounds))
        {
            Remove(id);
            return false;
        }
        if (id >= m_Items.size())
        {
            m_Items.resize(static_cast<size_t>(id) + 1u);
        }
        Item& item = m_Items[id];
        const uint64_t key = MakeRangeKey(bounds);
        if (item.bPlaced && item.RangeKey == key)
        {
            // 同じ範囲の中で動いた: 範囲を作り直すだけ（AABB が縮むかもしれない）
            item.Bounds = bounds;
            m_Ranges[key].bDirty = true;
            return true;
        }
        Remove(id);
        Range& range = m_Ranges[key];
        item.Bounds = bounds;
        item.RangeKey = key;
        item.IndexInRange = static_cast<uint32_t>(range.Members.size());
        item.bPlaced = true;
        range.Members.push_back(id);
        range.bDirty = true;
        ++m_Count;
        return true;
    }

    void ShadowCasterRangeSet::Remove(uint32_t id)
    {
        if (id >= m_Items.size() || !m_Items[id].bPlaced)
        {
            return;
        }
        Item& item = m_Items[id];
        const auto found = m_Ranges.find(item.RangeKey);
        if (found != m_Ranges.end())
        {
            // 範囲の物の一覧から外す（末尾の物を空いた位置へ移す）
            Range& range = found->second;
            const uint32_t position = item.IndexInRange;
            if (position < range.Members.size())
            {
                const uint32_t last = range.Members.back();
                range.Members[position] = last;
                m_Items[last].IndexInRange = position;
                range.Members.pop_back();
            }
            if (range.Members.empty())
            {
                m_Ranges.erase(found);
            }
            else
            {
                range.bDirty = true;
            }
        }
        item = Item{};
        --m_Count;
    }

    void ShadowCasterRangeSet::Rebuild(Range& range) const
    {
        bool bFirst = true;
        for (const uint32_t member : range.Members)
        {
            const BoundingSphere& bounds = m_Items[member].Bounds;
            const float center[3] = {bounds.CenterX, bounds.CenterY, bounds.CenterZ};
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                const float low = center[axis] - bounds.Radius;
                const float high = center[axis] + bounds.Radius;
                range.Min[axis] = bFirst ? low : MinFloat(range.Min[axis], low);
                range.Max[axis] = bFirst ? high : MaxFloat(range.Max[axis], high);
            }
            bFirst = false;
        }
        // AABB の中心から、物の球をすべて含む半径（物が 1 つなら、その物の半径。中心も一致する）
        const Math::Vector3 rangeCenter((range.Min[0] + range.Max[0]) * 0.5f,
                                        (range.Min[1] + range.Max[1]) * 0.5f,
                                        (range.Min[2] + range.Max[2]) * 0.5f);
        range.Radius = 0.0f;
        for (const uint32_t member : range.Members)
        {
            range.Radius = MaxFloat(range.Radius, CalculateCasterFitRadius(m_Items[member].Bounds, rangeCenter));
        }
        if (range.Members.size() == 1u)
        {
            range.Radius = m_Items[range.Members[0]].Bounds.Radius;
        }
        range.bDirty = false;
    }

    void ShadowCasterRangeSet::AppendBounds(Container::VariableArray<BoundingSphere>& out)
    {
        out.reserve(out.size() + m_Ranges.size());
        for (auto& [key, range] : m_Ranges)
        {
            if (range.bDirty)
            {
                Rebuild(range);
            }
            // 物が 1 つの範囲は、その物の球をそのまま返す（AABB の中心は球の中心と同じ）
            BoundingSphere bounds;
            if (range.Members.size() == 1u)
            {
                bounds = m_Items[range.Members[0]].Bounds;
            }
            else
            {
                bounds.CenterX = (range.Min[0] + range.Max[0]) * 0.5f;
                bounds.CenterY = (range.Min[1] + range.Max[1]) * 0.5f;
                bounds.CenterZ = (range.Min[2] + range.Max[2]) * 0.5f;
                bounds.Radius = range.Radius;
            }
            out.push_back(bounds);
        }
    }
} // namespace NorvesLib::Core::Rendering
