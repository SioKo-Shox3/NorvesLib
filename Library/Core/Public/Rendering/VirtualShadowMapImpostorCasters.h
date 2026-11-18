// インポスターの板を、太陽の仮想シャドウマップ（VSM）の影の塊にする装置非依存の計算。
// 選び方: 有効な配置・アトラスのうち、カメラから中心までが影の距離 + 半径以内の個体を近い順に並べる。
// 呼び出し側は塊の予算と記録の残りに収まる数を選び、アトラス順に積んで被覆による切り抜きの描画をまとめる。
// 境界は中心 ± HalfSize の箱。太陽のクリップマップに触れない箱は省き、点光源のスライスは対象にしない。
//
// World[0..3] = 中心 xyz、HalfSize、World[4..7] = AxisX xyz、AxisY x、
// World[8..11] = AxisY yz、セルの数 X・Y（float）。板の頂点はシェーダーが 2 三角形として作る。
// 記録の VertexBase・Flags・PreviousVertexAddress の下位 32 bit に AxisZ xyz の float のビットを入れる。
// PreviousVertexAddress の上位 32 bit の下位 31 bit はアトラス幅、最上位 bit は配置（0 = 半球、1 = 全球）、
// LodPayload はアトラスの高さ。PreviousTransformIndex は 0（ディゾルブなし）、頂点・インデックスのアドレスは 0。
// InstanceIndex・MaterialIndex・Reserved は切り抜き描画が閾値・描画番号・被覆の乗数に使う。
#pragma once

#include "Container/Containers.h"
#include "Rendering/LayerImpostor.h"
#include "Rendering/VirtualShadowMapCasters.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Rendering::VirtualShadowMap
{
    /** @brief 1 フレームにインポスターへ使う塊の上限 */
    constexpr uint32_t IMPOSTOR_SHADOW_CHUNK_BUDGET = MAX_CASTER_CHUNKS / 8u;

    /** @brief 影の候補の投影物とアトラスの番号、カメラからの距離の二乗 */
    struct ImpostorShadowCandidate
    {
        uint32_t Caster = 0;
        uint32_t Atlas = 0;
        float DistanceSquared = 0.0f;
    };

    /** @brief 配置とアトラスを検査し、距離の内側の候補を近い順に集める */
    inline void CollectImpostorShadowCandidates(const Container::VariableArray<ImpostorShadowCaster>& casters,
                                                const Container::VariableArray<ImpostorShadowAtlas>& atlases,
                                                const float (&cameraPosition)[3], float maxDistance,
                                                Container::VariableArray<ImpostorShadowCandidate>& outCandidates,
                                                uint32_t* outOfRange = nullptr)
    {
        outCandidates.clear();
        if (outOfRange)
        {
            *outOfRange = 0u;
        }
        if (!std::isfinite(maxDistance) || maxDistance < 0.0f ||
            !std::isfinite(cameraPosition[0]) || !std::isfinite(cameraPosition[1]) || !std::isfinite(cameraPosition[2]))
        {
            return;
        }
        for (uint32_t index = 0; index < static_cast<uint32_t>(casters.size()); ++index)
        {
            const ImpostorShadowCaster& caster = casters[index];
            if (caster.AtlasIndex >= atlases.size() || !std::isfinite(caster.HalfSize) || caster.HalfSize <= 0.0f)
            {
                continue;
            }
            const ImpostorShadowAtlas& atlas = atlases[caster.AtlasIndex];
            if (!atlas.AlbedoAtlas.IsValid() || atlas.CellResolution == 0u ||
                atlas.AxisCellCountX == 0u || atlas.AxisCellCountY == 0u ||
                atlas.AtlasWidth < atlas.AxisCellCountX || atlas.AtlasHeight < atlas.AxisCellCountY ||
                atlas.AtlasWidth > 0x7FFFFFFFu ||
                (atlas.Layout != ImpostorOctahedralLayout::HemiOctahedral && atlas.Layout != ImpostorOctahedralLayout::FullOctahedral))
            {
                continue;
            }
            bool bFinite = true;
            double distanceSquared = 0.0;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                bFinite = bFinite && std::isfinite(caster.Center[axis]) && std::isfinite(caster.AxisX[axis]) &&
                          std::isfinite(caster.AxisY[axis]) && std::isfinite(caster.AxisZ[axis]);
                const double delta = static_cast<double>(caster.Center[axis]) - cameraPosition[axis];
                distanceSquared += delta * delta;
            }
            if (!bFinite)
            {
                continue;
            }
            const double limit = static_cast<double>(maxDistance) + caster.HalfSize;
            if (distanceSquared > limit * limit)
            {
                if (outOfRange)
                {
                    ++*outOfRange;
                }
                continue;
            }
            const float distance = static_cast<float>(std::min(distanceSquared, static_cast<double>(std::numeric_limits<float>::max())));
            outCandidates.push_back({index, caster.AtlasIndex, distance});
        }
        // float の距離の二乗が丸めや飽和で同じになったときも、元の座標から近い順を保つ。
        const auto preciseDistanceSquared = [&](uint32_t index)
        {
            const ImpostorShadowCaster& caster = casters[index];
            const double dx = static_cast<double>(caster.Center[0]) - cameraPosition[0];
            const double dy = static_cast<double>(caster.Center[1]) - cameraPosition[1];
            const double dz = static_cast<double>(caster.Center[2]) - cameraPosition[2];
            return dx * dx + dy * dy + dz * dz;
        };
        std::sort(outCandidates.begin(), outCandidates.end(), [&](const ImpostorShadowCandidate& a, const ImpostorShadowCandidate& b)
        {
            if (a.DistanceSquared != b.DistanceSquared)
            {
                return a.DistanceSquared < b.DistanceSquared;
            }
            const double distanceA = preciseDistanceSquared(a.Caster);
            const double distanceB = preciseDistanceSquared(b.Caster);
            return distanceA != distanceB ? distanceA < distanceB : a.Caster < b.Caster;
        });
    }

    /** @brief 1 個体を太陽の段に触れる塊として積み、積んだ数を返す */
    inline uint32_t AppendImpostorShadowInstance(const ImpostorShadowCaster& caster,
                                                 const ImpostorShadowAtlas& atlas,
                                                 const VirtualShadowMapClipmap& clipmap,
                                                 Container::VariableArray<VsmShadowChunk>& inOutChunks,
                                                 CasterStats& inOutStats,
                                                 CasterBounds& inOutWorldBounds,
                                                 bool& inOutHasWorldBounds)
    {
        CasterBounds bounds;
        VsmShadowChunk chunk;
        chunk.Record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::ImpostorCard);
        chunk.Record.TriangleCount = 2u;
        chunk.Record.FirstIndex = 0u;
        chunk.Record.PreviousTransformIndex = 0u;
        chunk.Record.VertexBase = std::bit_cast<uint32_t>(caster.AxisZ[0]);
        chunk.Record.Flags = std::bit_cast<uint32_t>(caster.AxisZ[1]);
        const uint32_t widthAndLayout = atlas.AtlasWidth | (static_cast<uint32_t>(atlas.Layout) << 31u);
        chunk.Record.PreviousVertexAddress = static_cast<uint64_t>(std::bit_cast<uint32_t>(caster.AxisZ[2])) |
                                             (static_cast<uint64_t>(widthAndLayout) << 32u);
        chunk.Record.LodPayload = atlas.AtlasHeight;
        const float placement[12] = {caster.Center[0], caster.Center[1], caster.Center[2], caster.HalfSize,
                                     caster.AxisX[0], caster.AxisX[1], caster.AxisX[2], caster.AxisY[0],
                                     caster.AxisY[1], caster.AxisY[2], static_cast<float>(atlas.AxisCellCountX),
                                     static_cast<float>(atlas.AxisCellCountY)};
        std::copy(placement, placement + 12, chunk.World);
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            bounds.Min[axis] = caster.Center[axis] - caster.HalfSize;
            bounds.Max[axis] = caster.Center[axis] + caster.HalfSize;
            chunk.BoundsMin[axis] = bounds.Min[axis];
            chunk.BoundsMax[axis] = bounds.Max[axis];
        }
        const SliceMasks masks = MasksForBounds(clipmap, nullptr, bounds);
        if (!masks.IsAny())
        {
            ++inOutStats.CulledChunks;
            return 0u;
        }
        const uint32_t added = PushChunkPerGroup(chunk, masks, inOutChunks, inOutStats);
        if (added != 0u)
        {
            if (!inOutHasWorldBounds)
            {
                inOutWorldBounds = bounds;
                inOutHasWorldBounds = true;
            }
            else
            {
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    inOutWorldBounds.Min[axis] = std::min(inOutWorldBounds.Min[axis], bounds.Min[axis]);
                    inOutWorldBounds.Max[axis] = std::max(inOutWorldBounds.Max[axis], bounds.Max[axis]);
                }
            }
        }
        return added;
    }
} // namespace NorvesLib::Core::Rendering::VirtualShadowMap
