// CSM と点光源のキューブで共有する MegaGeometry の投影物の選別と警告の記録。
#include "Rendering/ShadowCasterCollection.h"
#include "Logging/LogMacros.h"
#include "Rendering/MegaGeometry/MegaGeometryLODSelection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    // 影の地図の1テクセルのワールドでの大きさに見合う段を選ぶ（影に指定された段より細かくはしない）。
    MegaShadowRange ResolveMegaShadowRange(const MegaShadowCaster& caster, float texelSize)
    {
        MegaShadowRange range;
        range.FirstIndex = caster.FirstIndex;
        range.IndexCount = caster.IndexCount;
        if (!caster.GpuData)
        {
            return range;
        }
        range.Level = caster.GpuData->ShadowLODLevel;
        const uint32_t level = MegaGeometry::SelectShadowLODLevel(*caster.GpuData,
                                                                  caster.WorldScale,
                                                                  texelSize,
                                                                  MegaShadowLODErrorTexels);
        if (level != range.Level && level < caster.GpuData->LevelRanges.size() &&
            caster.GpuData->LevelRanges[level].IndexCount > 0u)
        {
            range.Level = level;
            range.FirstIndex = caster.GpuData->LevelRanges[level].FirstIndex;
            range.IndexCount = caster.GpuData->LevelRanges[level].IndexCount;
        }
        return range;
    }

    bool ExceedsShadowCasterTriangleBudget(uint32_t triangleCount, uint32_t budget)
    {
        return budget != 0u && triangleCount > budget;
    }

    bool IsMegaShadowMeshOverTriangleBudget(const MegaGeometry::MegaMeshGPUData* gpuData, uint32_t budget)
    {
        return gpuData && ExceedsShadowCasterTriangleBudget(gpuData->ShadowIndexCount / 3u, budget);
    }

    bool ShadowCasterBudgetWarnings::MarkFirstWarning(uint64_t meshId)
    {
#if NORVES_ENABLE_LOGGING
        return m_WarnedMeshIds.insert(meshId).second;
#else
        (void)meshId;
        return false;
#endif
    }

    bool AdmitMegaShadowCasterDraw(const MegaShadowCaster& caster, const MegaShadowRange& range, uint32_t budget,
                                  ShadowCasterBudgetWarnings& warnings)
    {
        const uint32_t triangleCount = range.IndexCount / 3u;
        if (!ExceedsShadowCasterTriangleBudget(triangleCount, budget))
        {
            return true;
        }
#if NORVES_ENABLE_LOGGING
        if (warnings.MarkFirstWarning(caster.MeshId))
        {
            NORVES_LOG_WARNING("ShadowMapPass", "SHADOW_CASTER_BUDGET_EXCEEDED mesh=%s triangles=%u budget=%u",
                               caster.GpuData && !caster.GpuData->DebugName.empty() ? caster.GpuData->DebugName.c_str() : "?",
                               triangleCount, budget);
        }
#else
        (void)caster;
        (void)warnings;
#endif
        return false;
    }

    // 記録用に「メッシュ名:段/三角形数」を足す
    void AppendMegaShadowLevel(Container::String& out, const MegaShadowCaster& caster, const MegaShadowRange& range)
    {
        char text[160] = {};
        std::snprintf(text,
                      sizeof(text),
                      "%s%s:%u/%u",
                      out.empty() ? "" : ",",
                      caster.GpuData && !caster.GpuData->DebugName.empty() ? caster.GpuData->DebugName.c_str() : "?",
                      range.Level,
                      range.IndexCount / 3u);
        out += text;
    }

    // ローカルの境界球をワールド行列（行ベクトル規約、並進は行3）で写す。半径は最大の軸の伸びで広げる。
    BoundingSphere TransformMegaBounds(const BoundingSphere& local, const float* world)
    {
        BoundingSphere result;
        result.CenterX = local.CenterX * world[0] + local.CenterY * world[4] +
                         local.CenterZ * world[8] + world[12];
        result.CenterY = local.CenterX * world[1] + local.CenterY * world[5] +
                         local.CenterZ * world[9] + world[13];
        result.CenterZ = local.CenterX * world[2] + local.CenterY * world[6] +
                         local.CenterZ * world[10] + world[14];
        float maxScaleSquared = 0.0f;
        for (uint32_t row = 0; row < 3; ++row)
        {
            const float lengthSquared = world[row * 4 + 0] * world[row * 4 + 0] +
                                        world[row * 4 + 1] * world[row * 4 + 1] +
                                        world[row * 4 + 2] * world[row * 4 + 2];
            maxScaleSquared = std::max(maxScaleSquared, lengthSquared);
        }
        result.Radius = local.Radius * std::sqrt(maxScaleSquared);
        return result;
    }

    // ワールドの境界球が描ける大きさか（有限で半径が正）
    bool IsDrawableMegaBounds(const BoundingSphere& bounds)
    {
        return std::isfinite(bounds.CenterX) && std::isfinite(bounds.CenterY) && std::isfinite(bounds.CenterZ) &&
               std::isfinite(bounds.Radius) && bounds.Radius > 0.0f;
    }

    // 影に使う頂点・インデックスのバッファと段を持つか
    bool IsMegaShadowMeshUsable(const MegaGeometry::MegaMeshGPUData* gpuData)
    {
        return gpuData && gpuData->VertexBuffer && gpuData->IndexBuffer && gpuData->ShadowIndexCount != 0u;
    }

    // 投影物 1 つを作る。影を落とさない・影に使う段が無い・境界が描けない物は false を返して外す
    bool TryMakeMegaShadowCaster(const float* world, bool bCastShadow,
                                const MegaGeometry::MegaMeshGPUData* gpuData, uint64_t meshId,
                                MegaShadowCaster& outCaster)
    {
        if (!bCastShadow || !IsMegaShadowMeshUsable(gpuData))
        {
            return false;
        }
        MegaShadowCaster caster;
        caster.VertexBuffer = gpuData->VertexBuffer;
        caster.IndexBuffer = gpuData->IndexBuffer;
        caster.VertexBufferOffsetBytes = gpuData->VertexBufferOffsetBytes;
        caster.IndexBufferOffsetBytes = gpuData->IndexBufferOffsetBytes;
        caster.FirstIndex = gpuData->ShadowFirstIndex;
        caster.IndexCount = gpuData->ShadowIndexCount;
        caster.GpuData = gpuData;
        caster.MeshId = meshId;
        std::memcpy(caster.World, world, sizeof(caster.World));
        caster.Bounds = TransformMegaBounds(gpuData->TotalBounds, caster.World);
        caster.WorldScale = gpuData->TotalBounds.Radius > 0.0f
                                ? caster.Bounds.Radius / gpuData->TotalBounds.Radius
                                : 1.0f;
        if (!IsDrawableMegaBounds(caster.Bounds))
        {
            return false;
        }
        outCaster = caster;
        return true;
    }
} // namespace NorvesLib::Core::Rendering
