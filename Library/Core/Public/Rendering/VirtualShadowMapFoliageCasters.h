#pragma once

// 草・低木の株（FoliageTypes.h の束）を、仮想シャドウマップ（VSM）の影の塊（VsmShadowChunk）の記録にする CPU の計算。
// 株は FoliagePass が頂点シェーダーで置いて風に揺らす（頂点はワールドに無い）ので、塊の記録は株のメッシュの頂点（FoliageVertex）と
// インデックスのアドレスに、株の置き方（根元・向き・倍率・位相・距離で縮める倍率）と束の風の値を World の 12 個の float に持たせ、
// VSM の描画（vsm_draw.vert）が foliage.vert と同じ式（Common/FoliagePlacement.glsl）で置く。葉の帯は両面で描く（VSM の描画は背面を省かない）。
//
// 選び方: カメラから根元までの距離が束の ShadowDistance 以内の株を、束の ShadowPriority の小さい順、同じ段の中は近い順に、
// 塊の数の予算（FOLIAGE_SHADOW_CHUNK_BUDGET と記録の上限 MAX_CASTER_CHUNKS の残りの小さい方）に収まるだけ選ぶ。
// 距離の外・予算の外の株は太陽の影を落とさない（照明の接触影だけが残る）。遠い株は束の ShadowLods の簡略のメッシュで描き、
// 1 株の塊の数を減らす（SelectFoliageShadowLod）。
// 株のメッシュは 128 三角形のブロック（MESH_BLOCK_INDICES。メッシュの先頭から整列）ごとに 1 つの塊にし、塊の境界はブロックの
// ローカルの境界を株の置き方で写して、風で動く量の上限（FoliageWindMargin）だけ広げる。
//
// World の並び（vsm_draw.vert の VsmPlaceFoliageVertex と同じ）:
//   World[0..3]  = 根元の位置 xyz、Y 軸まわりの向き[rad]
//   World[4..7]  = 横の倍率、高さの倍率、揺れの位相、距離で縮める倍率（foliage.vert の FoliageFadeScale を CPU で求めた値）
//   World[8..11] = 風の強さ、揺れの周波数[Hz]、メッシュの高さ[m]（倍率 1 のとき）、0
// 全部の束に共通の風（向き・時計・突風の速さと大きさ）は描画の定数（VirtualShadowMapRasterDispatch::FoliageWind）で渡す。
// この計算は描画の装置にも ViewRenderContext にも依らない（GPU の無いテストが直接呼べる）。

#include "Container/Containers.h"
#include "Rendering/FoliageTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/VirtualShadowMapCasters.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    namespace VirtualShadowMap
    {
        /**
         * @brief 1 フレームに株へ使う塊の数の上限（記録の上限 MAX_CASTER_CHUNKS の半分）
         *
         * 荒野のブラックブラッシュ 1 株は 3.8 万三角形 = 約 300 塊なので、近い順に約 50 株まで。
         */
        constexpr uint32_t FOLIAGE_SHADOW_CHUNK_BUDGET = MAX_CASTER_CHUNKS / 2u;
        /** @brief FoliageVertex の float の数（vsm_draw.vert が頂点を読む刻み） */
        constexpr uint32_t FOLIAGE_VERTEX_FLOATS = 13u;
        static_assert(sizeof(FoliageVertex) == FOLIAGE_VERTEX_FLOATS * sizeof(float), "vsm_draw.vert の株の頂点の読み方と合わせる");

        /**
         * @brief 株のメッシュのインデックスを MESH_BLOCK_INDICES ごとに区切ったブロックの、ローカルの境界（根元が原点）
         * @return ブロックを作れたら true（インデックスが 3 の倍数でない・範囲外の頂点を指すときは false で outBlocks は空）
         */
        inline bool BuildFoliageBlockBounds(const FoliageMeshData& mesh, Container::VariableArray<BoundingBox>& outBlocks)
        {
            outBlocks.clear();
            const uint32_t indexCount = static_cast<uint32_t>(mesh.Indices.size());
            if (indexCount < 3u || indexCount % 3u != 0u || mesh.Vertices.empty())
            {
                return false;
            }
            const uint32_t blockCount = (indexCount + MESH_BLOCK_INDICES - 1u) / MESH_BLOCK_INDICES;
            outBlocks.reserve(blockCount);
            for (uint32_t block = 0; block < blockCount; ++block)
            {
                const uint32_t first = block * MESH_BLOCK_INDICES;
                const uint32_t end = std::min(first + MESH_BLOCK_INDICES, indexCount);
                BoundingBox bounds;
                bool bFirst = true;
                for (uint32_t position = first; position < end; ++position)
                {
                    const uint32_t vertexIndex = mesh.Indices[position];
                    if (vertexIndex >= mesh.Vertices.size())
                    {
                        outBlocks.clear();
                        return false;
                    }
                    const float* p = mesh.Vertices[vertexIndex].Position;
                    if (bFirst)
                    {
                        bounds = BoundingBox{p[0], p[1], p[2], p[0], p[1], p[2]};
                        bFirst = false;
                    }
                    else
                    {
                        bounds.Expand(p[0], p[1], p[2]);
                    }
                }
                outBlocks.push_back(bounds);
            }
            return true;
        }

        /**
         * @brief 風で頂点が動く量の上限[m]
         *
         * foliage.vert の FoliageWindOffset の項の振幅は、傾き 1.0・揺れ 0.5・震え 0.25（どれも風の強さ倍）で、
         * しなりの重み（0〜1）の 2 乗と株の高さを掛ける。下げる量（ApplyFoliageWind）はずれの大きさを超えない。
         */
        inline float FoliageWindMargin(float windStrength, float plantHeight)
        {
            return 1.75f * std::abs(windStrength) * std::max(plantHeight, 0.0f);
        }

        /** @brief カメラから distance[m] の株を縮める倍率（foliage.vert の FoliageFadeScale と同じ規則。1 = そのまま、0 = 消える） */
        inline float FoliageFadeScale(const FoliageBatch& batch, float distance, float threshold)
        {
            if (batch.FadeEndDistance <= batch.FadeStartDistance)
            {
                return 1.0f;
            }
            // foliage.vert の kFadeShrinkBand と同じ幅
            constexpr float kFadeShrinkBand = 0.08f;
            const float keep =
                1.0f - std::clamp((distance - batch.FadeStartDistance) / (batch.FadeEndDistance - batch.FadeStartDistance), 0.0f, 1.0f);
            return std::clamp((keep - threshold) / kFadeShrinkBand, 0.0f, 1.0f);
        }

        /**
         * @brief ブロックのローカルの境界を、株の置き方（Y 軸まわりの向き・横と高さの倍率・根元の位置）でワールドの境界にし、
         *        風で動く量だけ横と下へ広げる
         */
        inline CasterBounds FoliageInstanceBounds(const BoundingBox& local, const FoliageInstance& instance, float fadeScale, float windMargin)
        {
            const float widthScale = instance.WidthScale * fadeScale;
            const float heightScale = instance.HeightScale * fadeScale;
            const float center[3] = {0.5f * (local.MinX + local.MaxX) * widthScale, 0.5f * (local.MinY + local.MaxY) * heightScale,
                                     0.5f * (local.MinZ + local.MaxZ) * widthScale};
            const float extent[3] = {0.5f * (local.MaxX - local.MinX) * std::abs(widthScale), 0.5f * (local.MaxY - local.MinY) * std::abs(heightScale),
                                     0.5f * (local.MaxZ - local.MinZ) * std::abs(widthScale)};
            // foliage.vert と同じ回転: (x, z) → (c·x + s·z, −s·x + c·z)
            const float c = std::cos(instance.Yaw);
            const float s = std::sin(instance.Yaw);
            const float worldCenter[3] = {instance.Position[0] + c * center[0] + s * center[2], instance.Position[1] + center[1],
                                          instance.Position[2] - s * center[0] + c * center[2]};
            const float worldExtent[3] = {std::abs(c) * extent[0] + std::abs(s) * extent[2], extent[1],
                                          std::abs(s) * extent[0] + std::abs(c) * extent[2]};
            CasterBounds bounds;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                bounds.Min[axis] = worldCenter[axis] - worldExtent[axis];
                bounds.Max[axis] = worldCenter[axis] + worldExtent[axis];
            }
            bounds.Min[0] -= windMargin;
            bounds.Max[0] += windMargin;
            bounds.Min[2] -= windMargin;
            bounds.Max[2] += windMargin;
            bounds.Min[1] -= windMargin;
            return bounds;
        }

        /** @brief 1 株の影に使うメッシュの段の数の上限（束の Mesh を含む。ShadowLods の 3 つ目より後ろは使わない） */
        constexpr uint32_t MAX_FOLIAGE_SHADOW_LODS = 4u;

        /**
         * @brief 根元までの距離 distance[m] の株の影に使うメッシュの段（0 = 束の Mesh、k = ShadowLods[k − 1]）
         *
         * StartDistance を超えた最後の段を選ぶ。メッシュの無い段は飛ばす。
         */
        inline uint32_t SelectFoliageShadowLod(const FoliageBatch& batch, float distance)
        {
            uint32_t lod = 0;
            const uint32_t count = std::min(static_cast<uint32_t>(batch.ShadowLods.size()), MAX_FOLIAGE_SHADOW_LODS - 1u);
            for (uint32_t index = 0; index < count; ++index)
            {
                const FoliageShadowLod& level = batch.ShadowLods[index];
                if (level.Mesh && !level.Mesh->Indices.empty() && distance >= level.StartDistance)
                {
                    lod = index + 1u;
                }
            }
            return lod;
        }

        /** @brief 段 lod（SelectFoliageShadowLod）のメッシュ */
        inline const Container::TSharedPtr<const FoliageMeshData>& FoliageShadowLodMesh(const FoliageBatch& batch, uint32_t lod)
        {
            return lod == 0u ? batch.Mesh : batch.ShadowLods[lod - 1u].Mesh;
        }

        /** @brief 影に描く株の候補（束の番号・インスタンスの番号・束の ShadowPriority・根元までの距離の 2 乗） */
        struct FoliageShadowCandidate
        {
            uint32_t Batch = 0;
            uint32_t Instance = 0;
            uint32_t Priority = 0;
            float DistanceSquared = 0.0f;
        };

        /**
         * @brief 根元がカメラから束の ShadowDistance 以内の株を集めて、束の ShadowPriority の小さい順、同じ段の中は近い順に並べる
         *
         * 区画（FoliageCell）があれば、カメラから区画の箱までの最短の距離が ShadowDistance を超える区画を見ない。
         * 距離で縮めて消える株（FoliageFadeScale が 0）は除く。
         */
        inline void CollectFoliageShadowCandidates(const FoliageScene& scene,
                                                   const float (&cameraPosition)[3],
                                                   Container::VariableArray<FoliageShadowCandidate>& outCandidates)
        {
            outCandidates.clear();
            for (uint32_t batchIndex = 0; batchIndex < static_cast<uint32_t>(scene.Batches.size()); ++batchIndex)
            {
                const FoliageBatch& batch = scene.Batches[batchIndex];
                if (!batch.Mesh || !batch.Instances || batch.Mesh->Indices.empty() || batch.Instances->empty() || !(batch.ShadowDistance > 0.0f))
                {
                    continue;
                }
                const auto& instances = *batch.Instances;
                const float limitSquared = batch.ShadowDistance * batch.ShadowDistance;
                const auto consider = [&](uint32_t first, uint32_t count) {
                    for (uint32_t index = first; index < first + count; ++index)
                    {
                        const FoliageInstance& instance = instances[index];
                        const float dx = instance.Position[0] - cameraPosition[0];
                        const float dy = instance.Position[1] - cameraPosition[1];
                        const float dz = instance.Position[2] - cameraPosition[2];
                        const float distanceSquared = dx * dx + dy * dy + dz * dz;
                        if (!(distanceSquared <= limitSquared) ||
                            FoliageFadeScale(batch, std::sqrt(distanceSquared), instance.FadeThreshold) <= 0.0f)
                        {
                            continue;
                        }
                        outCandidates.push_back(FoliageShadowCandidate{batchIndex, index, batch.ShadowPriority, distanceSquared});
                    }
                };
                if (!batch.Cells || batch.Cells->empty())
                {
                    consider(0u, static_cast<uint32_t>(instances.size()));
                    continue;
                }
                for (const FoliageCell& cell : *batch.Cells)
                {
                    if (cell.InstanceCount == 0u || static_cast<uint64_t>(cell.FirstInstance) + cell.InstanceCount > instances.size())
                    {
                        continue;
                    }
                    float squared = 0.0f;
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        const float outside = std::max({cell.BoundsMin[axis] - cameraPosition[axis], 0.0f, cameraPosition[axis] - cell.BoundsMax[axis]});
                        squared += outside * outside;
                    }
                    if (squared > limitSquared)
                    {
                        continue;
                    }
                    consider(cell.FirstInstance, cell.InstanceCount);
                }
            }
            std::sort(outCandidates.begin(), outCandidates.end(), [](const FoliageShadowCandidate& a, const FoliageShadowCandidate& b) {
                if (a.Priority != b.Priority)
                {
                    return a.Priority < b.Priority;
                }
                if (a.DistanceSquared != b.DistanceSquared)
                {
                    return a.DistanceSquared < b.DistanceSquared;
                }
                return a.Batch != b.Batch ? a.Batch < b.Batch : a.Instance < b.Instance;
            });
        }

        /** @brief 株のメッシュのバッファのアドレスとブロックの境界（VirtualShadowMapPass が束のメッシュごとに持つ） */
        struct FoliageShadowMesh
        {
            uint64_t VertexAddress = 0;
            uint64_t IndexAddress = 0;
            const BoundingBox* BlockBounds = nullptr;
            uint32_t BlockCount = 0;
            /** @brief メッシュのインデックスの数（最後のブロックの三角形の数を決める） */
            uint32_t IndexCount = 0;
        };

        /**
         * @brief 1 株の塊を記録に足す。どのスライスの範囲にも入らない塊は省き、記録の上限を超えたら書かずに数える
         * @param mesh 株の影に使う段のメッシュ（束の Mesh か ShadowLods の 1 つ）。風の置き方の株の高さは束の Mesh の Height を使う
         * @param bStill true なら風で揺らさない（風の強さを 0 にして置き、境界も広げない）
         * @param inOutWorldBounds 出力: 足した塊の境界を合わせた箱（足さなければ変えない）
         * @return 足した塊の数
         */
        inline uint32_t AppendFoliageInstance(const FoliageBatch& batch,
                                              const FoliageShadowMesh& mesh,
                                              const FoliageInstance& instance,
                                              float fadeScale,
                                              const VirtualShadowMapClipmap& clipmap,
                                              Container::VariableArray<VsmShadowChunk>& inOutChunks,
                                              CasterStats& inOutStats,
                                              CasterBounds& inOutWorldBounds,
                                              bool& inOutHasWorldBounds,
                                              const CasterSliceTable* slices = nullptr,
                                              bool bStill = false)
        {
            if (mesh.VertexAddress == 0u || mesh.IndexAddress == 0u || mesh.BlockBounds == nullptr || mesh.BlockCount == 0u || !batch.Mesh)
            {
                return 0u;
            }
            const float windStrength = bStill ? 0.0f : batch.WindStrength;
            const float plantHeight = batch.Mesh->Height * instance.HeightScale * fadeScale;
            const float windMargin = FoliageWindMargin(windStrength, plantHeight);
            VsmShadowChunk chunk;
            chunk.Record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::FoliageChunk);
            chunk.Record.VertexBase = 0u;
            chunk.Record.VertexAddress = mesh.VertexAddress;
            chunk.Record.IndexAddress = mesh.IndexAddress;
            const float placement[12] = {instance.Position[0], instance.Position[1], instance.Position[2], instance.Yaw,
                                         instance.WidthScale,  instance.HeightScale, instance.WindPhase,   fadeScale,
                                         windStrength,         batch.WindSwayFrequency, batch.Mesh->Height, 0.0f};
            std::copy(placement, placement + 12, chunk.World);
            uint32_t added = 0;
            for (uint32_t block = 0; block < mesh.BlockCount; ++block)
            {
                const uint32_t first = block * MESH_BLOCK_INDICES;
                if (first >= mesh.IndexCount)
                {
                    break;
                }
                const CasterBounds bounds = FoliageInstanceBounds(mesh.BlockBounds[block], instance, fadeScale, windMargin);
                const SliceMasks masks = MasksForBounds(clipmap, slices, bounds);
                if (!masks.IsAny())
                {
                    ++inOutStats.CulledChunks;
                    continue;
                }
                chunk.Record.FirstIndex = first;
                chunk.Record.TriangleCount = std::min(MESH_BLOCK_INDICES, mesh.IndexCount - first) / 3u;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    chunk.BoundsMin[axis] = bounds.Min[axis];
                    chunk.BoundsMax[axis] = bounds.Max[axis];
                }
                const uint32_t pushed = PushChunkPerGroup(chunk, masks, inOutChunks, inOutStats);
                if (pushed != 0u)
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
                added += pushed;
            }
            inOutStats.FoliageChunks += added;
            inOutStats.FoliageDraws += added != 0u ? 1u : 0u;
            return added;
        }
    } // namespace VirtualShadowMap
} // namespace NorvesLib::Core::Rendering
