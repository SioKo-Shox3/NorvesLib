#pragma once

// 焼き込み済みの階層（NVMESH v1）の、ページどうしの親子の関係を求める。
//
// クラスタ P は、グループ G（1つ細かい段のクラスタの集まり）を簡略化して作られる。このとき P は G の境界球と誤差を
// そのまま自分の値として持つ（クッカーの規則。グループのメンバの ParentBounds・ParentError も同じ値）。
// 1つのグループは1つのページに収まるので、G のメンバのページが P の「子のページ」になる。
// カリングは、もっと細かい子が欲しいのに子のページが常駐していないとき、穴を作らずに P を代わりに描き、子のページを要求する。
// クラスタの配列は GPU に常駐したまま（ページごとに常駐するのは頂点とインデックスの中身）なので、
// P からその子のページを引けるよう、ここで ChildPageId を埋める。

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /** @brief ComputeGeometryPageLinks の結果 */
    struct GeometryPageLinkResult
    {
        /** @brief メッシュのページの数（クラスタの PageId の最大 + 1。最小 1） */
        uint32_t PageCount = 1;
        /** @brief ChildPageId を埋めたクラスタの数 */
        uint32_t LinkedClusters = 0;
        /** @brief グループのメンバが複数のページにまたがって、子のページを決められなかったグループの数 */
        uint32_t SplitGroups = 0;
        /** @brief 同じ段・同じ球・同じ誤差のグループが複数あって、先頭を採ったクラスタの数 */
        uint32_t AmbiguousClusters = 0;
    };

    namespace GeometryPageLinksDetail
    {
        // クッカーがグループの値をそのままクラスタへ書くので、浮動小数のビット列の一致で照合する
        struct GroupKey
        {
            uint32_t Level = 0;
            uint32_t Bits[5] = {};
            uint32_t GroupIndex = 0;
        };

        inline uint32_t FloatBits(float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        inline GroupKey MakeKey(uint32_t level, const BoundingSphere &bounds, float error, uint32_t groupIndex)
        {
            GroupKey key;
            key.Level = level;
            key.Bits[0] = FloatBits(bounds.CenterX);
            key.Bits[1] = FloatBits(bounds.CenterY);
            key.Bits[2] = FloatBits(bounds.CenterZ);
            key.Bits[3] = FloatBits(bounds.Radius);
            key.Bits[4] = FloatBits(error);
            key.GroupIndex = groupIndex;
            return key;
        }

        inline bool KeyLess(const GroupKey &a, const GroupKey &b)
        {
            if (a.Level != b.Level)
            {
                return a.Level < b.Level;
            }
            for (uint32_t i = 0; i < 5; ++i)
            {
                if (a.Bits[i] != b.Bits[i])
                {
                    return a.Bits[i] < b.Bits[i];
                }
            }
            return a.GroupIndex < b.GroupIndex;
        }

        inline bool KeySameValue(const GroupKey &a, const GroupKey &b)
        {
            if (a.Level != b.Level)
            {
                return false;
            }
            for (uint32_t i = 0; i < 5; ++i)
            {
                if (a.Bits[i] != b.Bits[i])
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace GeometryPageLinksDetail

    /**
     * @brief クラスタごとの子のページの番号を、グループの表から求める
     *
     * @param clusters 全段のクラスタ（PageId・LODLevel・Bounds・LODError を読む）
     * @param groups グループの表（メンバは clusters の連続した範囲）
     * @param outChildPages クラスタごとの子のページの番号（clusters と同じ数。無ければ INVALID_PAGE_ID）
     * @param outResult ページの数と、求められなかった件数
     * @return クラスタの PageId が上限を超えるなど、ページの数を決められないとき false（outChildPages は全て INVALID_PAGE_ID）
     *
     * グループを持たないメッシュ（v1.0 以前・実行時に構築した階層）は子のページが無い（INVALID_PAGE_ID）。
     * 1つのグループのメンバが複数のページにまたがるときは、そのグループから作られたクラスタの子のページを決められないので
     * INVALID_PAGE_ID のままにする（常に自分の誤差で決まる、穴を作らない側）。
     */
    inline bool ComputeGeometryPageLinks(const VariableArray<MeshCluster> &clusters,
                                         const VariableArray<MeshClusterGroup> &groups,
                                         VariableArray<uint32_t> &outChildPages,
                                         GeometryPageLinkResult &outResult)
    {
        using namespace GeometryPageLinksDetail;

        outResult = GeometryPageLinkResult{};
        outChildPages.assign(clusters.size(), INVALID_PAGE_ID);
        // 1つの表に置くページの数の上限（GeometryPageTable の上限より十分小さい）
        constexpr uint32_t MaxPagesPerMesh = 1u << 24;

        uint32_t maxPageId = 0;
        for (const MeshCluster &cluster : clusters)
        {
            maxPageId = std::max(maxPageId, cluster.PageId);
        }
        if (maxPageId >= MaxPagesPerMesh)
        {
            return false;
        }
        outResult.PageCount = maxPageId + 1u;

        if (groups.empty())
        {
            return true;
        }

        // グループごとの子のページ（メンバが全て同じページのとき）。またがるグループは INVALID_PAGE_ID
        VariableArray<uint32_t> groupPage(groups.size(), INVALID_PAGE_ID);
        VariableArray<GroupKey> keys;
        keys.reserve(groups.size());
        for (uint32_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
        {
            const MeshClusterGroup &group = groups[groupIndex];
            const uint64_t end = static_cast<uint64_t>(group.ClusterOffset) + group.ClusterCount;
            if (group.ClusterCount == 0 || end > clusters.size())
            {
                continue;
            }
            const uint32_t page = clusters[group.ClusterOffset].PageId;
            bool bSamePage = true;
            for (uint32_t member = 1; member < group.ClusterCount; ++member)
            {
                bSamePage = bSamePage && clusters[group.ClusterOffset + member].PageId == page;
            }
            if (!bSamePage)
            {
                ++outResult.SplitGroups;
                continue;
            }
            groupPage[groupIndex] = page;
            // このグループから作られたクラスタは1つ粗い段（LODLevel + 1）にある
            keys.push_back(MakeKey(group.LODLevel + 1u, group.Bounds, group.Error, groupIndex));
        }
        std::sort(keys.begin(), keys.end(), KeyLess);

        for (size_t clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex)
        {
            const MeshCluster &cluster = clusters[clusterIndex];
            if (cluster.LODLevel == 0)
            {
                continue;
            }
            const GroupKey probe = MakeKey(cluster.LODLevel, cluster.Bounds, cluster.LODError, 0);
            auto first = std::lower_bound(keys.begin(), keys.end(), probe, KeyLess);
            if (first == keys.end() || !KeySameValue(*first, probe))
            {
                continue;
            }
            auto next = first + 1;
            if (next != keys.end() && KeySameValue(*next, probe))
            {
                ++outResult.AmbiguousClusters;
            }
            outChildPages[clusterIndex] = groupPage[first->GroupIndex];
            ++outResult.LinkedClusters;
        }
        return true;
    }

    /** @brief ComputeGeometryPageLinks の結果を、クラスタの ChildPageId へ書く（CPU の試験用の写しが読む） */
    inline bool ApplyGeometryPageLinks(VariableArray<MeshCluster> &clusters,
                                       const VariableArray<MeshClusterGroup> &groups,
                                       GeometryPageLinkResult &outResult)
    {
        VariableArray<uint32_t> childPages;
        const bool bOk = ComputeGeometryPageLinks(clusters, groups, childPages, outResult);
        for (size_t index = 0; index < clusters.size(); ++index)
        {
            clusters[index].ChildPageId = childPages[index];
        }
        return bOk;
    }
} // namespace NorvesLib::Core::Rendering::MegaGeometry
