#pragma once

// GBuffer の書き手と照明が束ねる水たまりの UBO。GLSL 側の写しは Assets/Shaders/Common/Puddle.glsl の PuddleSurfaceGpuParams

#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ViewRenderContext.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    struct alignas(16) PuddleSurfaceGpuParams
    {
        float OverheadWindow[4] = {}; // xy = 雨の遮蔽マップの窓の原点 XZ（m）、z = 範囲（m）、w = テクセル幅（m）
        float OverheadDepth[4] = {};  // x = 上端 Y、y = 範囲（m）、z = 解像度、w = 有効 1/0
        float PuddleField[4] = {};    // 水たまりの層: xy = 原点 XZ（m）、z = 1/範囲（1/m）、w = 有効 1/0
    };
    static_assert(sizeof(PuddleSurfaceGpuParams) == 48);
    static_assert(alignof(PuddleSurfaceGpuParams) == 16);
    static_assert(offsetof(PuddleSurfaceGpuParams, OverheadWindow) == 0);
    static_assert(offsetof(PuddleSurfaceGpuParams, OverheadDepth) == 16);
    static_assert(offsetof(PuddleSurfaceGpuParams, PuddleField) == 32);
    static_assert(std::is_trivially_copyable_v<PuddleSurfaceGpuParams>);
    static_assert(std::is_standard_layout_v<PuddleSurfaceGpuParams>);

    /** @brief 雨の遮蔽マップがこのフレーム・このビューの物で、有効な窓と深度を持つか。
     * 束縛する側が代わりのテクスチャへ切り替える判定にも使う。
     */
    inline bool IsOverheadHeightMapUsable(const ViewRenderContext& context)
    {
        const uint32_t viewId = context.CurrentViewport ? context.CurrentViewport->ViewId : UINT32_MAX;
        const uint32_t viewportId = context.CurrentViewport ? context.CurrentViewport->ViewportId : UINT32_MAX;
        const OverheadHeightMapResources& overhead = context.OverheadHeightMap;
        return overhead.Matches(context.FrameNumber, viewId, viewportId) &&
            std::isfinite(overhead.OriginX) && std::isfinite(overhead.OriginZ) &&
            std::isfinite(overhead.TexelMeters) && overhead.TexelMeters > 0.0f &&
            std::isfinite(overhead.CoverageMeters) && overhead.CoverageMeters > 0.0f &&
            std::isfinite(overhead.TopY) && std::isfinite(overhead.RangeMeters) &&
            overhead.RangeMeters > 0.0f && overhead.Resolution > 0u;
    }

    /** @brief 文脈から窓と深度を詰める。無効・非有限の窓は 0 にし、雨の遮蔽は「開けている」と扱う。
     * 水たまりの層は有効な座標と正の範囲を持つとき、原点・範囲の逆数・有効 1 を詰める。
     */
    inline PuddleSurfaceGpuParams MakePuddleSurfaceGpuParams(const ViewRenderContext& context)
    {
        PuddleSurfaceGpuParams value;
        if (IsOverheadHeightMapUsable(context))
        {
            const OverheadHeightMapResources& overhead = context.OverheadHeightMap;
            value.OverheadWindow[0] = overhead.OriginX;
            value.OverheadWindow[1] = overhead.OriginZ;
            value.OverheadWindow[2] = overhead.CoverageMeters;
            value.OverheadWindow[3] = overhead.TexelMeters;
            value.OverheadDepth[0] = overhead.TopY;
            value.OverheadDepth[1] = overhead.RangeMeters;
            value.OverheadDepth[2] = static_cast<float>(overhead.Resolution);
            value.OverheadDepth[3] = 1.0f;
        }
        const WorldFieldState& puddle = context.PhysicalLighting.WorldField.PuddleNear;
        if (puddle.bValid && std::isfinite(puddle.OriginXZ[0]) && std::isfinite(puddle.OriginXZ[1]) &&
            std::isfinite(puddle.ExtentMeters) && puddle.ExtentMeters > 0.0f)
        {
            constexpr double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
            const double invExtent = 1.0 / static_cast<double>(puddle.ExtentMeters);
            if (std::abs(puddle.OriginXZ[0]) <= maxFloat && std::abs(puddle.OriginXZ[1]) <= maxFloat &&
                std::isfinite(invExtent) && invExtent <= maxFloat)
            {
                value.PuddleField[0] = static_cast<float>(puddle.OriginXZ[0]);
                value.PuddleField[1] = static_cast<float>(puddle.OriginXZ[1]);
                value.PuddleField[2] = static_cast<float>(invExtent);
                value.PuddleField[3] = 1.0f;
            }
        }
        return value;
    }

    /** @brief このフレームの水たまりの UBO をフレームの持ち主から借りる。持ち主が無ければ空を返す。 */
    inline FrameUploadAllocation AcquirePuddleSurfaceConstants(const ViewRenderContext& context)
    {
        if (context.FrameResources)
        {
            return context.FrameResources->AllocateConstants(MakePuddleSurfaceGpuParams(context));
        }
        return {};
    }
} // 名前空間 NorvesLib::Core::Rendering
