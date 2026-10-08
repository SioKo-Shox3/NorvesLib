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
// スライス（太陽では段）ごとの絞り込み: 塊の境界がどのスライスの範囲にも入らないときは、展開が 1 つもインスタンスを作らないので、CPU で省く。
// 入るスライスがあるときは、その集合を記録（VsmShadowChunk::LevelMask）に持ち、展開は集合の外のスライスを処理しない。
// 集合は 32 スライスずつの組の印で、組の番号を記録の Reserved に書く（ビット b が スライス Reserved × 32 + b）。
// 1 つの投影物が複数の組にまたがるときは、同じ描画の記録を持つ塊を組ごとに出す。
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
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace VirtualShadowMap
    {
        /** @brief 1 フレームの投影物の塊の記録の上限。超えた塊は書かずに数える */
        constexpr uint32_t MAX_CASTER_CHUNKS = 32768;
        /**
         * @brief 展開のインスタンスの容量（バッファの大きさ = この数 × 16 バイト = 16 MiB）。超えた塊は展開が描かずに溢れとして数え、
         *        その範囲の dirty のページには再描画の印（PAGE_ENTRY_RETRY）を付ける
         *
         * 負荷モード 300 個の最初の約 40 フレーム（全ページが新しく割り当てられる）の要求は最大 885457 インスタンス、
         * --vsm-cache=off の負荷モード 300 個は最大 885316 で、2^19（524288）を超えていた。
         */
        constexpr uint32_t RASTER_INSTANCE_CAPACITY = 1u << 20;
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

        /** @brief ワールドの境界が、ライト空間の基底（右・上）で覆う矩形（XY の最小・最大）。境界が有限でなければ false */
        inline bool LightSpaceRect(const double (&right)[3], const double (&up)[3], const CasterBounds& bounds, double (&outMin)[2], double (&outMax)[2])
        {
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

        /** @brief ワールドの境界がライト空間で覆う矩形（XY の最小・最大）。境界が有限でなければ false */
        inline bool LightSpaceRect(const VirtualShadowMapClipmap& clipmap, const CasterBounds& bounds, double (&outMin)[2], double (&outMax)[2])
        {
            const double right[3] = {clipmap.LightRight.x, clipmap.LightRight.y, clipmap.LightRight.z};
            const double up[3] = {clipmap.LightUp.x, clipmap.LightUp.y, clipmap.LightUp.z};
            return LightSpaceRect(right, up, bounds, outMin, outMax);
        }

        /**
         * @brief 境界が範囲に入る段の集合（組 group の印。ビット b が 段 group × 32 + b）。展開が塊の覆うページを数える範囲
         *        （段ごとの絶対のページの範囲）と同じ判定
         *
         * 範囲は [OriginPage, OriginPage + PagesPerAxis) で、ライト空間の矩形の floor(位置 / ページの幅) がそれに触れる段を返す。
         * クリップマップの段は最大 VirtualShadowMapMaxLevels（16）なので、組 0 より後ろは 0。
         * クリップマップが無効・境界が有限でないときは 0。
         */
        inline uint32_t LevelMaskForBounds(const VirtualShadowMapClipmap& clipmap, const CasterBounds& bounds, uint32_t group = 0u)
        {
            if (!clipmap.bEnabled || clipmap.PagesPerAxis == 0u || group != 0u)
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

        /** @brief 塊の段の印の組の数の上限（スライスの上限 ÷ 32） */
        constexpr uint32_t MAX_SLICE_GROUPS = SliceGroupCount(MAX_SLICES);

        /** @brief スライスの組ごとの印（Masks[g] が組 g の LevelMask）。GroupCount は印を持つ組の数の上限（それより後ろの組の印は 0） */
        struct SliceMasks
        {
            uint32_t Masks[MAX_SLICE_GROUPS] = {};
            uint32_t GroupCount = 0;

            /** @brief どの組にも印が無ければ false（どのスライスの範囲にも入らない） */
            bool IsAny() const
            {
                for (uint32_t group = 0; group < GroupCount; ++group)
                {
                    if (Masks[group] != 0u)
                    {
                        return true;
                    }
                }
                return false;
            }
        };

        /** @brief 外から渡すスライスの表（GPUVsmSlice の配列）。null・0 件ならクリップマップの段を使う */
        struct CasterSliceTable
        {
            const GPUVsmSlice* Slices = nullptr;
            uint32_t Count = 0;
        };

        /**
         * @brief 境界の球（AABB の外接球）が、透視のスライス（点光源の面）に写るか。展開（vsm_expand.comp の PerspectivePageRange）が
         *        ページを数える範囲と同じ手順を倍精度で行い、浮動小数の丸めで展開より狭くならないよう球をわずかに広げる
         *
         * 球の中心を面の座標（x, y = 面の接線方向、z = 面の軸の向きの距離）へ写し、光源の Range の内側・近い平面と遠い平面の間・
         * 面の錐台の 4 つの側面の内側で交わるときだけ true。近い平面（z ≤ near）をまたぐ球は面全体に写るので true。
         * 面の NDC の矩形が [-1, 1]² と重ならないときは false。Range・近い平面が使えない値（info[2] が info[3] 以下）のスライスは false。
         */
        inline bool PerspectiveSliceTouchesBounds(const GPUVsmSlice& slice, const CasterBounds& bounds)
        {
            const double range = static_cast<double>(slice.info[2]);
            const double nearPlane = static_cast<double>(slice.info[3]);
            if (!(range > nearPlane) || !(nearPlane >= 0.0) || slice.origin[3] <= 0)
            {
                return false;
            }
            double center[3] = {};
            double extentSquared = 0.0;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                if (!std::isfinite(bounds.Min[axis]) || !std::isfinite(bounds.Max[axis]) || bounds.Min[axis] > bounds.Max[axis])
                {
                    return false;
                }
                center[axis] = 0.5 * (static_cast<double>(bounds.Min[axis]) + static_cast<double>(bounds.Max[axis]));
                const double extent = 0.5 * (static_cast<double>(bounds.Max[axis]) - static_cast<double>(bounds.Min[axis]));
                extentSquared += extent * extent;
            }
            const double radius = std::sqrt(extentSquared) * 1.0001 + 1.0e-3;
            const float* const rows[3] = {slice.axisX, slice.axisY, slice.axisZ};
            double c[3] = {};
            for (uint32_t row = 0; row < 3u; ++row)
            {
                c[row] = static_cast<double>(rows[row][0]) * center[0] + static_cast<double>(rows[row][1]) * center[1] +
                         static_cast<double>(rows[row][2]) * center[2] + static_cast<double>(rows[row][3]);
            }
            const double reach = range + radius;
            if (!(c[0] * c[0] + c[1] * c[1] + c[2] * c[2] <= reach * reach))
            {
                return false;
            }
            if (c[2] + radius < nearPlane || c[2] - radius > range)
            {
                return false;
            }
            const double side = radius * 1.4142136;
            if (c[0] - c[2] > side || -c[0] - c[2] > side || c[1] - c[2] > side || -c[1] - c[2] > side)
            {
                return false;
            }
            if (c[2] - radius <= nearPlane)
            {
                return true;
            }
            // 球を面の NDC へ写した矩形が、面（[-1, 1]²）と重なるか
            const double r2 = radius * radius;
            const double denominator = c[2] * c[2] - r2;
            for (uint32_t axis = 0; axis < 2u; ++axis)
            {
                const double spread = radius * std::sqrt(std::max(c[axis] * c[axis] + denominator, 0.0));
                const double low = (c[axis] * c[2] - spread) / denominator;
                const double high = (c[axis] * c[2] + spread) / denominator;
                if (high < -1.0 || low > 1.0)
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief 境界が範囲に入るスライスの、組ごとの印。LevelMaskForBounds と同じ判定を、スライスの表の正射影のスライス（投影の種類が正射影で
         *        ページの一辺が正のもの）について行う。ライト空間の基底はスライスごとの axisX・axisY、範囲は [原点, 原点 + 一辺)
         *
         * 透視のスライス（点光源の面）は、境界の外接球が光源の Range の内側で面の錐台と交わるとき（PerspectiveSliceTouchesBounds）に印を付ける。
         * 境界が有限でなければ、正射影のスライスの印は空（透視のスライスは境界を見て同じく空）。
         */
        inline SliceMasks SliceMasksForBounds(const GPUVsmSlice* slices, uint32_t sliceCount, const CasterBounds& bounds)
        {
            SliceMasks result;
            sliceCount = std::min(sliceCount, MAX_SLICES);
            result.GroupCount = SliceGroupCount(sliceCount);
            double right[3] = {};
            double up[3] = {};
            double lightMin[2] = {};
            double lightMax[2] = {};
            bool bRectValid = false;
            bool bRectKnown = false;
            for (uint32_t index = 0; slices != nullptr && index < sliceCount; ++index)
            {
                const GPUVsmSlice& slice = slices[index];
                if (slice.extra[2] == VirtualShadowMapSliceProjectionPerspective)
                {
                    if (slice.info[0] > 0.0f && PerspectiveSliceTouchesBounds(slice, bounds))
                    {
                        result.Masks[index / SLICES_PER_GROUP] |= 1u << (index % SLICES_PER_GROUP);
                    }
                    continue;
                }
                if (slice.extra[2] != VirtualShadowMapSliceProjectionOrtho || !(slice.info[0] > 0.0f) || slice.origin[3] <= 0)
                {
                    continue;
                }
                const double sliceRight[3] = {slice.axisX[0], slice.axisX[1], slice.axisX[2]};
                const double sliceUp[3] = {slice.axisY[0], slice.axisY[1], slice.axisY[2]};
                // 基底が前のスライスと同じなら矩形を使い回す（太陽の段はどれも同じ基底）
                if (!bRectKnown || std::memcmp(right, sliceRight, sizeof(right)) != 0 || std::memcmp(up, sliceUp, sizeof(up)) != 0)
                {
                    std::memcpy(right, sliceRight, sizeof(right));
                    std::memcpy(up, sliceUp, sizeof(up));
                    bRectValid = LightSpaceRect(right, up, bounds, lightMin, lightMax);
                    bRectKnown = true;
                }
                if (!bRectValid)
                {
                    return result;
                }
                const double pageMeters = static_cast<double>(slice.info[0]);
                const double lastX = static_cast<double>(slice.origin[0]) + static_cast<double>(slice.origin[3]) - 1.0;
                const double lastY = static_cast<double>(slice.origin[1]) + static_cast<double>(slice.origin[3]) - 1.0;
                const bool bTouchesX = std::floor(lightMax[0] / pageMeters) >= static_cast<double>(slice.origin[0]) &&
                                       std::floor(lightMin[0] / pageMeters) <= lastX;
                const bool bTouchesY = std::floor(lightMax[1] / pageMeters) >= static_cast<double>(slice.origin[1]) &&
                                       std::floor(lightMin[1] / pageMeters) <= lastY;
                if (bTouchesX && bTouchesY)
                {
                    result.Masks[index / SLICES_PER_GROUP] |= 1u << (index % SLICES_PER_GROUP);
                }
            }
            return result;
        }

        /** @brief 境界のスライスの組ごとの印。スライスの表があればその表から、なければクリップマップの段から求める（組は 1 つ） */
        inline SliceMasks MasksForBounds(const VirtualShadowMapClipmap& clipmap, const CasterSliceTable* table, const CasterBounds& bounds)
        {
            if (table != nullptr && table->Slices != nullptr && table->Count != 0u)
            {
                return SliceMasksForBounds(table->Slices, table->Count, bounds);
            }
            SliceMasks result;
            result.GroupCount = 1u;
            result.Masks[0] = LevelMaskForBounds(clipmap, bounds);
            return result;
        }

        /**
         * @brief 描画の記録を持つ塊を、印のある組ごとに 1 つ出す（LevelMask = その組の印、Reserved = 組の番号）
         *
         * 上限（MAX_CASTER_CHUNKS）に収まらない塊は書かずに DroppedChunks で数える。出した塊の数を返す。
         */
        inline uint32_t PushChunkPerGroup(const VsmShadowChunk& chunk,
                                          const SliceMasks& masks,
                                          Container::VariableArray<VsmShadowChunk>& inOutChunks,
                                          CasterStats& inOutStats)
        {
            uint32_t pushed = 0;
            for (uint32_t group = 0; group < masks.GroupCount; ++group)
            {
                if (masks.Masks[group] == 0u)
                {
                    continue;
                }
                if (inOutChunks.size() >= MAX_CASTER_CHUNKS)
                {
                    ++inOutStats.DroppedChunks;
                    continue;
                }
                VsmShadowChunk grouped = chunk;
                grouped.LevelMask = masks.Masks[group];
                grouped.Reserved = group;
                inOutChunks.push_back(grouped);
                ++pushed;
            }
            return pushed;
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

        /**
         * @brief 無効にするワールドの境界を、境界の箱を覆う球（x, y, z = 中心、w = 半径の 4 つの float）の並びにする
         *
         * 点光源の面のページの無効化に使う。球は箱の中心を中心にした外接球（半径 = 箱の対角線の半分）なので、箱の内側のどの点も、
         * どの面の NDC でも球の矩形の内側に写る。境界が有限でない・最小が最大を超えるときは false（全ページを無効にする）。
         * 球の数が maxSpheres を超えるときも false。
         * @return false なら、球では足りないので全ページを無効にすること
         */
        inline bool BuildInvalidationSpheres(const Container::VariableArray<CasterBounds>& changedBounds,
                                             uint32_t maxSpheres,
                                             Container::VariableArray<float>& outSpheres)
        {
            outSpheres.clear();
            if (changedBounds.size() > maxSpheres)
            {
                return false;
            }
            for (const CasterBounds& bounds : changedBounds)
            {
                double center[3] = {};
                double halfDiagonalSquared = 0.0;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    const double low = static_cast<double>(bounds.Min[axis]);
                    const double high = static_cast<double>(bounds.Max[axis]);
                    if (!std::isfinite(low) || !std::isfinite(high) || low > high)
                    {
                        return false;
                    }
                    center[axis] = 0.5 * (low + high);
                    const double half = 0.5 * (high - low);
                    halfDiagonalSquared += half * half;
                }
                // 単精度へ丸めても球が箱を覆うように、半径をわずかに広げる
                const double radius = std::sqrt(halfDiagonalSquared) * (1.0 + 1.0e-6) + 1.0e-6;
                if (!std::isfinite(radius) || radius > static_cast<double>(std::numeric_limits<float>::max()))
                {
                    return false;
                }
                outSpheres.push_back(static_cast<float>(center[0]));
                outSpheres.push_back(static_cast<float>(center[1]));
                outSpheres.push_back(static_cast<float>(center[2]));
                outSpheres.push_back(static_cast<float>(radius));
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
         * @param slices スライスの表（null ならクリップマップの段）。複数の組にまたがる塊は、組ごとに 1 つずつ出す
         */
        inline void AppendProceduralInstance(const ProceduralDrawInput& draw,
                                             const Container::VariableArray<ProceduralChunkPlan>& plan,
                                             const float world[16],
                                             const VirtualShadowMapClipmap& clipmap,
                                             Container::VariableArray<VsmShadowChunk>& inOutChunks,
                                             CasterStats& inOutStats,
                                             const CasterSliceTable* slices = nullptr)
        {
            if (plan.empty() || draw.MeshBounds == nullptr)
            {
                return;
            }
            if (!MasksForBounds(clipmap, slices, TransformBoundsByWorld(world, *draw.MeshBounds)).IsAny())
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
                const SliceMasks masks = MasksForBounds(clipmap, slices, bounds);
                if (!masks.IsAny())
                {
                    ++inOutStats.CulledChunks;
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
                std::copy(rows, rows + 12, chunk.World);
                const uint32_t pushed = PushChunkPerGroup(chunk, masks, inOutChunks, inOutStats);
                inOutStats.ProceduralChunks += pushed;
                bAdded = bAdded || pushed != 0u;
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
                                          CasterStats& inOutStats,
                                          const CasterSliceTable* slices = nullptr)
        {
            if (chunks.empty())
            {
                return;
            }
            const SliceMasks masks = MasksForBounds(clipmap, slices, worldBounds);
            if (!masks.IsAny())
            {
                inOutStats.CulledChunks += static_cast<uint32_t>(chunks.size());
                return;
            }
            bool bAdded = false;
            for (const MeshIndexChunk& entry : chunks)
            {
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
                const uint32_t pushed = PushChunkPerGroup(chunk, masks, inOutChunks, inOutStats);
                inOutStats.SkinnedChunks += pushed;
                bAdded = bAdded || pushed != 0u;
            }
            inOutStats.SkinnedDraws += bAdded ? 1u : 0u;
        }
    } // namespace VirtualShadowMap
} // namespace NorvesLib::Core::Rendering
