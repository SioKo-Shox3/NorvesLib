#pragma once

// ページに分けたメッシュの配置の計算（GPU を持たない純粋な計算。CPU の試験が確かめられる）。
//   - ページの範囲の検査（根のページの連続・頂点とインデックスの範囲・クラスタがページの範囲に収まること）
//   - 根のページだけを区画へ書くときの大きさ
//   - ページの親子の関係（ページは、親のページが全て常駐していないと描けない）
//   - ページを区画へ置いたときの、クラスタの記録の頂点・インデックスの位置の書き換え
//
// 描画は「インスタンスの基点 + クラスタの位置」で頂点・インデックスを引く（Common/MegaGeometryCull.glsl の
// drawCommands）。ページの区画はメッシュの区画と同じプールの塊の中にあるので、クラスタの位置を
// 「ページの区画の基点 − メッシュの基点 + ページの中の位置」へ書き換えれば、シェーダーを変えずに引ける。
// インデックスの足し算は uint32 の剰余で、頂点の足し算は int の 2 の補数なので、基点の差が負でも結果は合う。

#include "Container/Containers.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <algorithm>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    namespace GeometryPageLayout
    {
        /** @brief 頂点 1 つの大きさ（Mesh3DVertex と同じ。位置・法線・UV の float 8 個） */
        constexpr uint64_t VertexStrideBytes = 32;
        /** @brief ページの区画の中の頂点・インデックスの整列（プールの区画の整列と同じ） */
        constexpr uint64_t RegionAlignmentBytes = 256;

        constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        /** @brief 根のページ（常駐）が占める範囲 */
        struct RootExtent
        {
            uint32_t PageCount = 0;
            uint32_t ClusterCount = 0;
            uint32_t VertexCount = 0;
            uint32_t IndexCount = 0;
        };

        /**
         * @brief ページの範囲が整っているかを検査し、根のページが占める範囲を返す
         *
         * 整っている条件: ページは 2 つ以上・先頭のページが根・根のページは先頭から連続・ページはクラスタ・頂点・
         * インデックスの全体の配列に隙間なく並ぶ・全ページの合計が配列の大きさと一致する（インデックスは
         * フォールバックの手前まで）・クラスタは自分のページの頂点・インデックスの範囲に収まる・
         * フォールバックのインデックスは根のページの頂点だけを指す。
         *
         * @param indexData フォールバックの範囲を読むための全体のインデックス
         */
        inline bool ValidatePages(const VariableArray<MeshPageInfo> &pages,
                                  const VariableArray<MeshCluster> &clusters,
                                  uint32_t vertexCount,
                                  uint32_t indexCount,
                                  const uint32_t *indexData,
                                  uint32_t fallbackIndexOffset,
                                  uint32_t fallbackIndexCount,
                                  RootExtent &outRoot)
        {
            outRoot = RootExtent();
            if (pages.size() < 2 || !pages[0].bRoot || indexData == nullptr)
            {
                return false;
            }

            uint64_t clusterBase = 0;
            uint64_t vertexBase = 0;
            uint64_t indexBase = 0;
            bool bInRootRun = true;
            for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
            {
                const MeshPageInfo &page = pages[pageIndex];
                if (page.bRoot && !bInRootRun)
                {
                    return false; // 根のページが先頭から連続していない
                }
                if (!page.bRoot)
                {
                    bInRootRun = false;
                }
                if (page.FirstCluster != clusterBase || page.FirstVertex != vertexBase || page.FirstIndex != indexBase)
                {
                    return false;
                }
                if (!page.bRoot && (page.ClusterCount == 0 || page.VertexCount == 0 || page.IndexCount == 0))
                {
                    return false; // ストリーミングするページは、描けるクラスタを持つ
                }
                if (clusterBase + page.ClusterCount > clusters.size())
                {
                    return false;
                }
                for (uint32_t offset = 0; offset < page.ClusterCount; ++offset)
                {
                    const MeshCluster &cluster = clusters[page.FirstCluster + offset];
                    const uint64_t vertexEnd = static_cast<uint64_t>(cluster.VertexOffset) + cluster.VertexCount;
                    const uint64_t indexEnd = static_cast<uint64_t>(cluster.IndexOffset) + cluster.IndexCount;
                    if (cluster.PageId != pageIndex || cluster.VertexOffset < 0 ||
                        static_cast<uint64_t>(cluster.VertexOffset) < page.FirstVertex ||
                        vertexEnd > static_cast<uint64_t>(page.FirstVertex) + page.VertexCount ||
                        cluster.IndexOffset < page.FirstIndex ||
                        indexEnd > static_cast<uint64_t>(page.FirstIndex) + page.IndexCount)
                    {
                        return false;
                    }
                }
                clusterBase += page.ClusterCount;
                vertexBase += page.VertexCount;
                indexBase += page.IndexCount;
                if (page.bRoot)
                {
                    outRoot.PageCount = static_cast<uint32_t>(pageIndex) + 1u;
                    outRoot.ClusterCount = static_cast<uint32_t>(clusterBase);
                    outRoot.VertexCount = static_cast<uint32_t>(vertexBase);
                    outRoot.IndexCount = static_cast<uint32_t>(indexBase);
                }
            }

            const uint64_t clusterIndexEnd = fallbackIndexCount > 0u ? fallbackIndexOffset : indexCount;
            if (clusterBase != clusters.size() || vertexBase != vertexCount || indexBase != clusterIndexEnd ||
                (fallbackIndexCount > 0u &&
                 static_cast<uint64_t>(fallbackIndexOffset) + fallbackIndexCount != indexCount))
            {
                return false;
            }
            for (uint32_t i = 0; i < fallbackIndexCount; ++i)
            {
                if (indexData[fallbackIndexOffset + i] >= outRoot.VertexCount)
                {
                    return false; // フォールバックの三角形が、常駐しない頂点を指している
                }
            }
            return true;
        }

        /** @brief 根のページだけを区画へ書くときの、インデックスの数（根のクラスタのインデックスに、フォールバックを続ける） */
        inline uint32_t ComputeResidentIndexCount(const RootExtent &root, uint32_t fallbackIndexCount)
        {
            return root.IndexCount + fallbackIndexCount;
        }

        /** @brief 根のページのインデックスの後ろへ移したフォールバックの、インデックスの中の開始位置 */
        inline uint32_t ComputeResidentFallbackOffset(const RootExtent &root)
        {
            return root.IndexCount;
        }

        /**
         * @brief ページの親子の関係
         *
         * ページ P のクラスタ p は、自分を作ったグループ（もっと細かい子）のページ ChildPageId[p] を持つ。
         * このとき ChildPageId[p] のページ（細かい側）は、P（粗い側）を親に持つ。
         * - 細かい側のページを描くには、親のページが全て常駐している必要がある（親が無いと、細かい側だけが描かれて
         *   親の段の穴になる）。
         * - 親のページを外すには、それを親に持つページが全て常駐していない必要がある（子から外す）。
         */
        struct PageRelations
        {
            /** @brief ページごとの親のページ（粗い側。重複なし・昇順） */
            VariableArray<VariableArray<uint32_t>> Parents;
            /** @brief ページごとの子のページ（細かい側。重複なし・昇順） */
            VariableArray<VariableArray<uint32_t>> Children;
        };

        inline void ComputePageRelations(const VariableArray<MeshCluster> &clusters,
                                         const VariableArray<uint32_t> &childPageIds,
                                         uint32_t pageCount,
                                         PageRelations &out)
        {
            out.Parents.assign(pageCount, VariableArray<uint32_t>());
            out.Children.assign(pageCount, VariableArray<uint32_t>());
            const size_t count = std::min(clusters.size(), childPageIds.size());
            auto insertSorted = [](VariableArray<uint32_t> &list, uint32_t value)
            {
                auto position = std::lower_bound(list.begin(), list.end(), value);
                if (position == list.end() || *position != value)
                {
                    list.insert(position, value);
                }
            };
            for (size_t index = 0; index < count; ++index)
            {
                const uint32_t parentPage = clusters[index].PageId;
                const uint32_t childPage = childPageIds[index];
                if (childPage == INVALID_PAGE_ID || parentPage >= pageCount || childPage >= pageCount ||
                    parentPage == childPage)
                {
                    continue;
                }
                insertSorted(out.Children[parentPage], childPage);
                insertSorted(out.Parents[childPage], parentPage);
            }
        }

        /** @brief ストリーミングするページ 1 つの区画の配置（頂点の後ろに、整列してインデックスを置く） */
        struct PageRegionLayout
        {
            uint64_t VertexBytes = 0;
            uint64_t IndexBytes = 0;
            uint64_t IndexOffsetBytes = 0;
            uint64_t RegionBytes = 0;
        };

        inline PageRegionLayout ComputePageRegionLayout(uint32_t vertexCount, uint32_t indexCount)
        {
            PageRegionLayout layout;
            layout.VertexBytes = static_cast<uint64_t>(vertexCount) * VertexStrideBytes;
            layout.IndexBytes = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);
            layout.IndexOffsetBytes = AlignUp(layout.VertexBytes, RegionAlignmentBytes);
            layout.RegionBytes = layout.IndexOffsetBytes + layout.IndexBytes;
            return layout;
        }

        /** @brief 区画の基点（プールの塊の先頭からのバイト）から、描画が引く頂点・インデックスの基点（要素の単位）を求める */
        struct RegionBases
        {
            int64_t VertexBase = 0;
            int64_t IndexBase = 0;
        };

        inline RegionBases ComputePageRegionBases(uint64_t regionOffsetBytes, const PageRegionLayout &layout)
        {
            RegionBases bases;
            bases.VertexBase = static_cast<int64_t>(regionOffsetBytes / VertexStrideBytes);
            bases.IndexBase = static_cast<int64_t>((regionOffsetBytes + layout.IndexOffsetBytes) / sizeof(uint32_t));
            return bases;
        }

        /**
         * @brief ページのクラスタの記録の頂点・インデックスの位置を、ページの区画を指すように書き換える
         *
         * @param records ページのクラスタの記録（全体の配列の位置のまま持っているもの。FirstCluster から ClusterCount 個）
         * @param page ページの範囲
         * @param pageBases ページの区画の基点
         * @param meshBases メッシュの区画の基点（インスタンスの基点として GPU が足す値）
         * @param out 書き換えた記録の置き場（ClusterCount 個）
         */
        inline void PatchClusterRecords(const GPUClusterData *records,
                                        const MeshPageInfo &page,
                                        const RegionBases &pageBases,
                                        const RegionBases &meshBases,
                                        GPUClusterData *out)
        {
            for (uint32_t offset = 0; offset < page.ClusterCount; ++offset)
            {
                GPUClusterData record = records[offset];
                const int64_t localIndex = static_cast<int64_t>(record.IndexOffset) - static_cast<int64_t>(page.FirstIndex);
                const int64_t localVertex =
                    static_cast<int64_t>(record.VertexOffset) - static_cast<int64_t>(page.FirstVertex);
                record.IndexOffset = static_cast<uint32_t>(static_cast<uint64_t>(pageBases.IndexBase - meshBases.IndexBase +
                                                                                 localIndex));
                record.VertexOffset = static_cast<int32_t>(pageBases.VertexBase - meshBases.VertexBase + localVertex);
                out[offset] = record;
            }
        }

        /** @brief 描画が引く位置（シェーダーの drawCommands と同じ式）。インデックスは uint32 の剰余、頂点は int */
        inline uint32_t ResolveFirstIndex(int64_t meshIndexBase, uint32_t clusterIndexOffset)
        {
            return static_cast<uint32_t>(static_cast<uint64_t>(meshIndexBase)) + clusterIndexOffset;
        }

        inline int32_t ResolveVertexOffset(int64_t meshVertexBase, int32_t clusterVertexOffset)
        {
            return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(meshVertexBase)) +
                                        static_cast<uint32_t>(clusterVertexOffset));
        }
    } // namespace GeometryPageLayout
} // namespace NorvesLib::Core::Rendering::MegaGeometry
