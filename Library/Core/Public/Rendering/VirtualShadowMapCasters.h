#pragma once

// 太陽の仮想シャドウマップ（VSM。--shadow-method=vsm）の投影物を、影の塊（VsmShadowChunk）の記録にする CPU の計算。
// VirtualShadowMapPass が、集めたメッシュのプロキシ・計算スキニングの結果からここで記録を作り、VirtualShadowMapRaster の展開・描画へ渡す。
//
// 集め方は影を落とす物すべて（影を落とすメッシュのプロキシと、スキニングの変形した頂点。主カメラの錐台で省かれた物も含める）で、
// メッシュを 128 三角形以下の塊（BuildMeshIndexChunks）に分け、塊ごとに
//   - ワールドの境界（AABB）: 手続きメッシュは登録時に求めた塊の境界（無ければメッシュ全体）にインスタンスの変換をかけた値、
//     スキニングは描画の境界（アニメーション後の境界にワールド行列をかけた値）
//   - 頂点・インデックスの読み方: 手続きメッシュはメッシュのバッファのアドレス（BDA）とインスタンスの変換、
//     スキニングは SkinningComputePass が変形した頂点（ワールド空間なので変換は単位行列）
// を持つ記録にする。ビジビリティバッファの塊の記録（VisibilityRasterPass）と同じ形の描画の記録を使う。
//
// 段ごとの絞り込み: 塊の境界がどの段の範囲にも入らないときは、展開が 1 つもインスタンスを作らないので、CPU で省く。
// 入る段があるときは、その段の集合を記録（VsmShadowChunk::LevelMask）に持ち、展開は集合の外の段を処理しない。
// この計算は描画の装置にも ViewRenderContext にも依らない（GPU の無いテストが直接呼べる）。

#include "Container/Containers.h"
#include "Rendering/MeshIndexChunks.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapRaster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace VirtualShadowMap
    {
        /** @brief 1 フレームの投影物の塊の記録の上限。超えた塊は書かずに数える */
        constexpr uint32_t MAX_CASTER_CHUNKS = 32768;
        /** @brief 展開のインスタンスの容量（バッファの大きさ = この数 × 16 バイト）。超えた塊は展開が描かずに溢れとして数える */
        constexpr uint32_t RASTER_INSTANCE_CAPACITY = 1u << 19;
        /** @brief 手続きメッシュの塊の境界を持つ、インデックスのブロックの大きさ（128 三角形 × 3）。メッシュの先頭から整列する */
        constexpr uint32_t MESH_BLOCK_INDICES = MESH_CHUNK_MAX_TRIANGLES * 3u;

        /** @brief ワールドの AABB（最小・最大の順に x, y, z） */
        struct CasterBounds
        {
            float Min[3] = {};
            float Max[3] = {};
        };

        /** @brief 投影物を集めた結果の内訳（記録を作った数・省いた数） */
        struct CasterStats
        {
            /** @brief 記録にした手続きメッシュの描画（インスタンスごと）と塊の数 */
            uint32_t ProceduralDraws = 0;
            uint32_t ProceduralChunks = 0;
            /** @brief 記録にしたスキニングの描画と塊の数 */
            uint32_t SkinnedDraws = 0;
            uint32_t SkinnedChunks = 0;
            /** @brief どの段の範囲にも入らず、CPU で省いた塊の数 */
            uint32_t CulledChunks = 0;
            /** @brief 記録の上限（MAX_CASTER_CHUNKS）に収まらず、書かなかった塊の数 */
            uint32_t DroppedChunks = 0;
            /** @brief 境界・アドレス・塊を作れず、集められなかった描画の数 */
            uint32_t SkippedDraws = 0;

            bool operator==(const CasterStats& other) const
            {
                return ProceduralDraws == other.ProceduralDraws && ProceduralChunks == other.ProceduralChunks &&
                       SkinnedDraws == other.SkinnedDraws && SkinnedChunks == other.SkinnedChunks &&
                       CulledChunks == other.CulledChunks && DroppedChunks == other.DroppedChunks &&
                       SkippedDraws == other.SkippedDraws;
            }
        };

        /** @brief ローカルの AABB を、ワールド行列（列優先 16 個の float。GPUSceneInstanceData::World と同じ並び）でワールドの AABB にする */
        inline CasterBounds TransformBoundsByWorld(const float world[16], const BoundingBox& local)
        {
            const float center[3] = {0.5f * (local.MinX + local.MaxX), 0.5f * (local.MinY + local.MaxY), 0.5f * (local.MinZ + local.MaxZ)};
            const float extent[3] = {0.5f * (local.MaxX - local.MinX), 0.5f * (local.MaxY - local.MinY), 0.5f * (local.MaxZ - local.MinZ)};
            CasterBounds bounds;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                const float worldCenter = world[axis] * center[0] + world[4 + axis] * center[1] + world[8 + axis] * center[2] + world[12 + axis];
                const float worldExtent =
                    std::abs(world[axis]) * extent[0] + std::abs(world[4 + axis]) * extent[1] + std::abs(world[8 + axis]) * extent[2];
                bounds.Min[axis] = worldCenter - worldExtent;
                bounds.Max[axis] = worldCenter + worldExtent;
            }
            return bounds;
        }

        /** @brief ワールドの境界がライト空間で覆う矩形（XY の最小・最大）。境界が有限でなければ false */
        inline bool LightSpaceRect(const VirtualShadowMapClipmap& clipmap, const CasterBounds& bounds, double (&outMin)[2], double (&outMax)[2])
        {
            const double right[3] = {clipmap.LightRight.x, clipmap.LightRight.y, clipmap.LightRight.z};
            const double up[3] = {clipmap.LightUp.x, clipmap.LightUp.y, clipmap.LightUp.z};
            double centerRight = 0.0;
            double centerUp = 0.0;
            double extentRight = 0.0;
            double extentUp = 0.0;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                if (!std::isfinite(bounds.Min[axis]) || !std::isfinite(bounds.Max[axis]) || bounds.Min[axis] > bounds.Max[axis])
                {
                    return false;
                }
                const double center = 0.5 * (static_cast<double>(bounds.Min[axis]) + static_cast<double>(bounds.Max[axis]));
                const double extent = 0.5 * (static_cast<double>(bounds.Max[axis]) - static_cast<double>(bounds.Min[axis]));
                centerRight += center * right[axis];
                centerUp += center * up[axis];
                extentRight += extent * std::abs(right[axis]);
                extentUp += extent * std::abs(up[axis]);
            }
            outMin[0] = centerRight - extentRight;
            outMax[0] = centerRight + extentRight;
            outMin[1] = centerUp - extentUp;
            outMax[1] = centerUp + extentUp;
            return true;
        }

        /**
         * @brief 境界が範囲に入る段の集合（ビット L が段 L）。展開が塊の覆うページを数える範囲（段ごとの絶対のページの範囲）と同じ判定
         *
         * 範囲は [OriginPage, OriginPage + PagesPerAxis) で、ライト空間の矩形の floor(位置 / ページの幅) がそれに触れる段を返す。
         * クリップマップが無効・境界が有限でないときは 0。
         */
        inline uint32_t LevelMaskForBounds(const VirtualShadowMapClipmap& clipmap, const CasterBounds& bounds)
        {
            if (!clipmap.bEnabled || clipmap.PagesPerAxis == 0u)
            {
                return 0u;
            }
            double lightMin[2] = {};
            double lightMax[2] = {};
            if (!LightSpaceRect(clipmap, bounds, lightMin, lightMax))
            {
                return 0u;
            }
            const uint32_t levelCount = std::min(clipmap.LevelCount, std::min(LEVEL_COUNT, VirtualShadowMapMaxLevels));
            uint32_t mask = 0u;
            for (uint32_t level = 0; level < levelCount; ++level)
            {
                const VirtualShadowMapClipmapLevel& data = clipmap.Levels[level];
                const double pageMeters = static_cast<double>(data.PageMeters);
                if (!(pageMeters > 0.0))
                {
                    continue;
                }
                const double lastX = static_cast<double>(data.OriginPageX) + static_cast<double>(clipmap.PagesPerAxis) - 1.0;
                const double lastY = static_cast<double>(data.OriginPageY) + static_cast<double>(clipmap.PagesPerAxis) - 1.0;
                const bool bTouchesX = std::floor(lightMax[0] / pageMeters) >= static_cast<double>(data.OriginPageX) &&
                                       std::floor(lightMin[0] / pageMeters) <= lastX;
                const bool bTouchesY = std::floor(lightMax[1] / pageMeters) >= static_cast<double>(data.OriginPageY) &&
                                       std::floor(lightMin[1] / pageMeters) <= lastY;
                if (bTouchesX && bTouchesY)
                {
                    mask |= 1u << level;
                }
            }
            return mask;
        }

        /** @brief 64 ビットの値を混ぜる（キャッシュの無効化のために、投影物の変化を見分ける署名・鍵を作る） */
        inline uint64_t CasterHashCombine(uint64_t seed, uint64_t value)
        {
            // FNV-1a のように 1 バイトずつ混ぜる（値の並びが違えば署名が違うことだけを期待する）
            constexpr uint64_t Prime = 1099511628211ull;
            for (uint32_t shift = 0; shift < 64u; shift += 8u)
            {
                seed = (seed ^ ((value >> shift) & 0xFFull)) * Prime;
            }
            return seed;
        }

        /** @brief float の並びのビットを混ぜる（行列などの署名） */
        inline uint64_t CasterHashFloats(uint64_t seed, const float* values, uint32_t count)
        {
            for (uint32_t index = 0; index < count; ++index)
            {
                uint32_t bits = 0;
                std::memcpy(&bits, &values[index], sizeof(bits));
                seed = CasterHashCombine(seed, bits);
            }
            return seed;
        }

        /** @brief 投影物 1 つ（描画のインスタンス）の、フレームをまたいで見る動きの入力 */
        struct CasterMotionEntry
        {
            /** @brief フレームをまたいで同じ投影物を指す鍵 */
            uint64_t Key = 0;
            /** @brief 位置・形が同じかを見分ける署名（変換・メッシュ・描く範囲から作る）。違えば動いた */
            uint64_t Signature = 0;
            /** @brief 毎フレーム動いた物として扱う（スキニング。MegaGeometry の world ≠ previousWorld） */
            bool bAlwaysChanged = false;
            /** @brief ワールドの境界（AABB）が分かるか。分からない物が変わったときは、全ページを無効にする */
            bool bHasBounds = false;
            CasterBounds Bounds;
        };

        /**
         * @brief 投影物の動きを、前フレームの記録と比べて見つける（VSM のキャッシュの無効化の入力を作る）
         *
         * 毎フレーム Update に、そのフレームの影を落とす投影物をすべて渡す。鍵が前フレームに無い（現れた）・署名が違う・
         * 毎フレーム動く指定の投影物は「変わった」とし、今フレームの境界と前フレームの境界の両方を無効にする範囲として返す
         * （前フレームの境界を含めないと、動いた物が元あった場所の影のページが古いまま残る）。前フレームにあって今フレームに無い
         * （消えた）物も、前フレームの境界を返す。最初の Update（記録が空）は何も返さない（ページの表も空なので全ページが新規）。
         */
        class CasterMotionTracker
        {
        public:
            /** @brief 前フレームの記録を捨てる（次の Update は最初の呼び出しとして扱う） */
            void Reset()
            {
                m_Records.clear();
                m_bPrimed = false;
            }

            /**
             * @param entries このフレームの投影物
             * @param outChangedBounds 無効にするワールドの境界（呼ぶ前に空にしなくてよい。足す）
             * @param outInvalidateAll 境界の分からない物が変わった（または消えた）ときに true にする（false へは戻さない）
             */
            void Update(const Container::VariableArray<CasterMotionEntry>& entries,
                        Container::VariableArray<CasterBounds>& outChangedBounds,
                        bool& outInvalidateAll)
            {
                ++m_Stamp;
                for (const CasterMotionEntry& entry : entries)
                {
                    auto found = m_Records.find(entry.Key);
                    const bool bExisted = found != m_Records.end();
                    const bool bChanged = m_bPrimed && (!bExisted || entry.bAlwaysChanged || found->second.Signature != entry.Signature);
                    if (bChanged)
                    {
                        AddBounds(entry.bHasBounds, entry.Bounds, outChangedBounds, outInvalidateAll);
                        if (bExisted)
                        {
                            AddBounds(found->second.bHasBounds, found->second.Bounds, outChangedBounds, outInvalidateAll);
                        }
                    }
                    Record& record = bExisted ? found->second : m_Records[entry.Key];
                    record.Signature = entry.Signature;
                    record.bHasBounds = entry.bHasBounds;
                    record.Bounds = entry.Bounds;
                    record.Stamp = m_Stamp;
                }
                // 今フレームに無かった（消えた）投影物。前フレームの境界を無効にして、記録から外す
                for (auto iterator = m_Records.begin(); iterator != m_Records.end();)
                {
                    if (iterator->second.Stamp != m_Stamp)
                    {
                        if (m_bPrimed)
                        {
                            AddBounds(iterator->second.bHasBounds, iterator->second.Bounds, outChangedBounds, outInvalidateAll);
                        }
                        iterator = m_Records.erase(iterator);
                    }
                    else
                    {
                        ++iterator;
                    }
                }
                m_bPrimed = true;
            }

            /** @brief 記録している投影物の数（観測用） */
            size_t GetRecordCount() const { return m_Records.size(); }

        private:
            struct Record
            {
                uint64_t Signature = 0;
                bool bHasBounds = false;
                CasterBounds Bounds;
                uint64_t Stamp = 0;
            };

            static void AddBounds(bool bHasBounds,
                                  const CasterBounds& bounds,
                                  Container::VariableArray<CasterBounds>& outChangedBounds,
                                  bool& outInvalidateAll)
            {
                if (bHasBounds)
                {
                    outChangedBounds.push_back(bounds);
                }
                else
                {
                    outInvalidateAll = true;
                }
            }

            Container::UnorderedMap<uint64_t, Record> m_Records;
            uint64_t m_Stamp = 0;
            bool m_bPrimed = false;
        };

        /**
         * @brief 無効にするワールドの境界を、ライト空間の矩形（x, y = 最小、z, w = 最大の 4 つの float）の並びにする
         *
         * 境界が有限でないものは全ページの無効化にする（false を返す）。矩形が maxRects を超えるときも false（全ページを無効にする）。
         * @return false なら、矩形では足りないので全ページを無効にすること
         */
        inline bool BuildInvalidationRects(const VirtualShadowMapClipmap& clipmap,
                                           const Container::VariableArray<CasterBounds>& changedBounds,
                                           uint32_t maxRects,
                                           Container::VariableArray<float>& outRects)
        {
            outRects.clear();
            if (changedBounds.size() > maxRects)
            {
                return false;
            }
            for (const CasterBounds& bounds : changedBounds)
            {
                double lightMin[2] = {};
                double lightMax[2] = {};
                if (!LightSpaceRect(clipmap, bounds, lightMin, lightMax))
                {
                    return false;
                }
                outRects.push_back(static_cast<float>(lightMin[0]));
                outRects.push_back(static_cast<float>(lightMin[1]));
                outRects.push_back(static_cast<float>(lightMax[0]));
                outRects.push_back(static_cast<float>(lightMax[1]));
            }
            return true;
        }

        /** @brief 手続きメッシュの 1 描画（インデックスの範囲）の入力。インスタンスの変換は含まない */
        struct ProceduralDrawInput
        {
            /** @brief 頂点・インデックスのバッファの先頭のデバイスアドレス（どちらかが 0 なら塊を作れない） */
            uint64_t VertexAddress = 0;
            uint64_t IndexAddress = 0;
            /** @brief 描くインデックスの範囲（メッシュのインデックスバッファの中の位置と数）と、インデックスに足す頂点の基点 */
            uint32_t FirstIndex = 0;
            uint32_t IndexCount = 0;
            uint32_t VertexOffset = 0;
            /** @brief メッシュ全体のローカル空間の AABB（無ければ境界を作れず、塊を作らない） */
            const BoundingBox* MeshBounds = nullptr;
            /** @brief 登録時に求めた、インデックスを MESH_BLOCK_INDICES ごとに（メッシュの先頭から）区切ったブロックのローカルの AABB。無くてもよい */
            const BoundingBox* BlockBounds = nullptr;
            uint32_t BlockBoundsCount = 0;
        };

        /** @brief 手続きメッシュの 1 描画を分けた塊 1 つ（インスタンスの変換をかける前の、ローカル空間の境界） */
        struct ProceduralChunkPlan
        {
            /** @brief メッシュのインデックスバッファの中の、塊の最初のインデックスの位置 */
            uint32_t FirstIndex = 0;
            uint32_t TriangleCount = 0;
            BoundingBox LocalBounds;
        };

        /** @brief PlanProceduralChunks が使う作業配列（毎フレーム持ち回して、確保を繰り返さない） */
        struct ProceduralPlanScratch
        {
            Container::VariableArray<uint32_t> Cuts;
            Container::VariableArray<uint32_t> CutScratch;
            Container::VariableArray<MeshIndexChunk> Chunks;
        };

        /**
         * @brief 手続きメッシュの 1 描画を、128 三角形以下の塊（BuildMeshIndexChunks）に分け、塊ごとのローカルの境界を決める
         *
         * 登録時のブロックの境界は、頂点の基点が 0 のときだけ使える（基点が違うと、インデックスの値からブロックの頂点を引けない）。
         * 使えるときは、メッシュの先頭から整列した MESH_BLOCK_INDICES の境目で塊を区切り、塊が 1 つのブロックの中に収まるようにして、
         * そのブロックの境界を塊の境界にする。使えないとき（ブロックの境界が無い・基点が 0 でない・先頭が 3 の倍数でない）は
         * メッシュ全体の境界を塊の境界にする。
         * @return 塊を作れたら true。アドレスが無い・メッシュの境界が無い・塊に分けられないときは false（outPlan は空）
         */
        inline bool PlanProceduralChunks(const ProceduralDrawInput& draw,
                                         ProceduralPlanScratch& scratch,
                                         Container::VariableArray<ProceduralChunkPlan>& outPlan)
        {
            outPlan.clear();
            if (draw.VertexAddress == 0u || draw.IndexAddress == 0u || draw.IndexCount < 3u || draw.MeshBounds == nullptr ||
                !draw.MeshBounds->IsValid())
            {
                return false;
            }
            const bool bUseBlocks = draw.BlockBounds != nullptr && draw.BlockBoundsCount != 0u && draw.VertexOffset == 0u &&
                                    draw.FirstIndex % 3u == 0u;
            scratch.Cuts.clear();
            if (bUseBlocks)
            {
                // 範囲の内側にあるブロックの境目（メッシュの先頭からの絶対の位置）で区切る。位置は範囲の先頭からの相対で渡す
                const uint64_t end = static_cast<uint64_t>(draw.FirstIndex) + draw.IndexCount;
                for (uint64_t cut = (static_cast<uint64_t>(draw.FirstIndex) / MESH_BLOCK_INDICES + 1u) * MESH_BLOCK_INDICES; cut < end;
                     cut += MESH_BLOCK_INDICES)
                {
                    scratch.Cuts.push_back(static_cast<uint32_t>(cut - draw.FirstIndex));
                }
            }
            if (!BuildMeshIndexChunks(draw.IndexCount, scratch.Cuts.data(), static_cast<uint32_t>(scratch.Cuts.size()), scratch.Chunks, scratch.CutScratch))
            {
                return false;
            }
            outPlan.reserve(scratch.Chunks.size());
            for (const MeshIndexChunk& chunk : scratch.Chunks)
            {
                ProceduralChunkPlan plan;
                plan.FirstIndex = draw.FirstIndex + chunk.FirstIndex;
                plan.TriangleCount = chunk.IndexCount / 3u;
                plan.LocalBounds = *draw.MeshBounds;
                if (bUseBlocks)
                {
                    const uint32_t block = plan.FirstIndex / MESH_BLOCK_INDICES;
                    if (block < draw.BlockBoundsCount && draw.BlockBounds[block].IsValid())
                    {
                        plan.LocalBounds = draw.BlockBounds[block];
                    }
                }
                outPlan.push_back(plan);
            }
            return !outPlan.empty();
        }

        /** @brief ワールド行列（列優先 16 個の float）を、記録の 3×4（行ごとに 4 要素）の並びへ写す */
        inline void WorldRowsFromShaderMatrix(const float world[16], float (&outRows)[12])
        {
            for (uint32_t row = 0; row < 3u; ++row)
            {
                for (uint32_t column = 0; column < 4u; ++column)
                {
                    outRows[row * 4u + column] = world[column * 4u + row];
                }
            }
        }

        /**
         * @brief 手続きメッシュの 1 インスタンスぶんの塊を記録に足す。どの段の範囲にも入らない塊は省き、上限を超えたら書かずに数える
         *
         * 先にメッシュ全体の境界で判定し、どの段にも入らなければ塊の境界を 1 つずつ見ずに全部を省く。
         * @param world インスタンスの変換（列優先 16 個の float。GPUSceneInstanceData::World と同じ並び）
         */
        inline void AppendProceduralInstance(const ProceduralDrawInput& draw,
                                             const Container::VariableArray<ProceduralChunkPlan>& plan,
                                             const float world[16],
                                             const VirtualShadowMapClipmap& clipmap,
                                             Container::VariableArray<VsmShadowChunk>& inOutChunks,
                                             CasterStats& inOutStats)
        {
            if (plan.empty() || draw.MeshBounds == nullptr)
            {
                return;
            }
            if (LevelMaskForBounds(clipmap, TransformBoundsByWorld(world, *draw.MeshBounds)) == 0u)
            {
                inOutStats.CulledChunks += static_cast<uint32_t>(plan.size());
                return;
            }
            float rows[12] = {};
            WorldRowsFromShaderMatrix(world, rows);
            bool bAdded = false;
            for (const ProceduralChunkPlan& entry : plan)
            {
                const CasterBounds bounds = TransformBoundsByWorld(world, entry.LocalBounds);
                const uint32_t levelMask = LevelMaskForBounds(clipmap, bounds);
                if (levelMask == 0u)
                {
                    ++inOutStats.CulledChunks;
                    continue;
                }
                if (inOutChunks.size() >= MAX_CASTER_CHUNKS)
                {
                    ++inOutStats.DroppedChunks;
                    continue;
                }
                VsmShadowChunk chunk;
                chunk.Record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk);
                chunk.Record.TriangleCount = entry.TriangleCount;
                chunk.Record.FirstIndex = entry.FirstIndex;
                chunk.Record.VertexBase = draw.VertexOffset;
                chunk.Record.VertexAddress = draw.VertexAddress;
                chunk.Record.IndexAddress = draw.IndexAddress;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    chunk.BoundsMin[axis] = bounds.Min[axis];
                    chunk.BoundsMax[axis] = bounds.Max[axis];
                }
                chunk.LevelMask = levelMask;
                std::copy(rows, rows + 12, chunk.World);
                inOutChunks.push_back(chunk);
                ++inOutStats.ProceduralChunks;
                bAdded = true;
            }
            inOutStats.ProceduralDraws += bAdded ? 1u : 0u;
        }

        /**
         * @brief スキニングの 1 インスタンス（SkinningComputePass が変形したワールド空間の頂点）の塊を記録に足す
         *
         * 塊はどれも同じ描画の境界を持つ。頂点のアドレスはインスタンスの先頭まで加算済みで、頂点の基点は 0、変換は単位行列。
         * @param chunks インデックスの範囲の塊（登録時に分けたもの。インデックスバッファの中の位置）
         */
        inline void AppendSkinnedInstance(uint64_t vertexAddress,
                                          uint64_t indexAddress,
                                          const CasterBounds& worldBounds,
                                          const Container::VariableArray<MeshIndexChunk>& chunks,
                                          const VirtualShadowMapClipmap& clipmap,
                                          Container::VariableArray<VsmShadowChunk>& inOutChunks,
                                          CasterStats& inOutStats)
        {
            if (chunks.empty())
            {
                return;
            }
            const uint32_t levelMask = LevelMaskForBounds(clipmap, worldBounds);
            if (levelMask == 0u)
            {
                inOutStats.CulledChunks += static_cast<uint32_t>(chunks.size());
                return;
            }
            bool bAdded = false;
            for (const MeshIndexChunk& entry : chunks)
            {
                if (inOutChunks.size() >= MAX_CASTER_CHUNKS)
                {
                    ++inOutStats.DroppedChunks;
                    continue;
                }
                VsmShadowChunk chunk;
                chunk.Record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::SkinnedChunk);
                chunk.Record.TriangleCount = entry.IndexCount / 3u;
                chunk.Record.FirstIndex = entry.FirstIndex;
                chunk.Record.VertexBase = 0u;
                chunk.Record.VertexAddress = vertexAddress;
                chunk.Record.IndexAddress = indexAddress;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    chunk.BoundsMin[axis] = worldBounds.Min[axis];
                    chunk.BoundsMax[axis] = worldBounds.Max[axis];
                }
                chunk.LevelMask = levelMask;
                inOutChunks.push_back(chunk);
                ++inOutStats.SkinnedChunks;
                bAdded = true;
            }
            inOutStats.SkinnedDraws += bAdded ? 1u : 0u;
        }
    } // namespace VirtualShadowMap
} // namespace NorvesLib::Core::Rendering
