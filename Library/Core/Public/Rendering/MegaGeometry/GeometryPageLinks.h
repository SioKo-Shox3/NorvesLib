#pragma once

// 焼き込み済みの階層（NVMESH v1）の、ページどうしの親子の関係を求める。
//
// クラスタ P は、グループ G（1つ細かい段のクラスタの集まり）を簡略化して作られる。このとき P は G の境界球と誤差を
// そのまま自分の値として持つ（クッカーの規則。グループのメンバの ParentBounds・ParentError も同じ値）。
// 1つのグループは1つのページに収まるので、G のメンバのページが P の「子のページ」になる。
// カリングは、もっと細かい子が欲しいのに子のページが常駐していないとき、穴を作らずに P を代わりに描き、子のページを要求する。
// クラスタの配列は GPU に常駐したまま（ページごとに常駐するのは頂点とインデックスの中身）なので、
// P からその子のページを引けるよう、ここで ChildPageId を埋める。
//
// 焼き込みが P の生成元のグループの番号（MeshCluster::SourceGroupId）を持つときは、その番号のグループのページが
// 子のページになる。値が同じ別のグループがあっても取り違えない。
// 番号を持たない旧い資産（NVMESH の番号の項目の導入前）では、境界球・誤差の値の一致で照合する。
// 別のグループが同じ値を持つと（同じ形の部品の複製など）、P がどちらから作られたかを決められないので、
// そのグループのページを「固定」して常駐のままにし（PinnedPages。ストアが非常駐への変更を拒否する）、
// 穴と要求の漏れを作らない。番号を持つクラスタだけの資産は固定するページを作らない。

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
        /** @brief LinkedClusters のうち、作ったグループの番号で決めたクラスタの数（残りは値の照合） */
        uint32_t LinkedByIdClusters = 0;
        /** @brief グループのメンバが複数のページにまたがって、子のページを決められなかったグループの数 */
        uint32_t SplitGroups = 0;
        /** @brief 同じ段・同じ球・同じ誤差のグループが複数あり、そのページが食い違うために、生成元を決められなかったクラスタの数 */
        uint32_t AmbiguousClusters = 0;
        /**
         * @brief 常駐のまま固定するページ（昇順）。生成元を決められない子のページと、複数のページにまたがるグループのページ
         *
         * 作ったグループの番号を持つクラスタだけで、グループがページをまたがない資産では空。
         * 固定したページは非常駐にしない。子が常駐しているなら親は描かれないので、取り違えても穴にならない。
         */
        VariableArray<uint32_t> PinnedPages;
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
     * クラスタが作ったグループの番号（SourceGroupId）を持ち、その番号が1つ細かい段のグループを指すときは、
     * そのグループのメンバのページが子のページ。番号を持たない（または指す先が合わない）クラスタは、値の照合で求める。
     * 1つのグループのメンバが複数のページにまたがるときは、そのグループから作られたクラスタの子のページを決められないので
     * INVALID_PAGE_ID のままにし、メンバのページを固定する。
     * 値の照合で、同じ値のグループが複数あってページが食い違うときも、候補のページを全て固定する（ChildPageId は先頭の候補のページ）。
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

        // 固定するページ（ページの番号で引く）
        VariableArray<uint8_t> pinned(outResult.PageCount, 0);
        // グループごとの子のページ（メンバが全て同じページのとき）。またがるグループは INVALID_PAGE_ID
        VariableArray<uint32_t> groupPage(groups.size(), INVALID_PAGE_ID);
        // 子のページを決められるグループ（メンバの範囲が正しく、空でない）と、メンバがページをまたぐグループ
        VariableArray<uint8_t> groupUsable(groups.size(), 0);
        VariableArray<uint8_t> groupSplit(groups.size(), 0);
        const auto pinGroupMembers = [&](uint32_t groupIndex)
        {
            const MeshClusterGroup &group = groups[groupIndex];
            for (uint32_t member = 0; member < group.ClusterCount; ++member)
            {
                pinned[clusters[group.ClusterOffset + member].PageId] = 1;
            }
        };
        for (uint32_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
        {
            const MeshClusterGroup &group = groups[groupIndex];
            const uint64_t end = static_cast<uint64_t>(group.ClusterOffset) + group.ClusterCount;
            if (group.ClusterCount == 0 || end > clusters.size())
            {
                continue;
            }
            groupUsable[groupIndex] = 1;
            const uint32_t page = clusters[group.ClusterOffset].PageId;
            bool bSamePage = true;
            for (uint32_t member = 1; member < group.ClusterCount; ++member)
            {
                bSamePage = bSamePage && clusters[group.ClusterOffset + member].PageId == page;
            }
            if (!bSamePage)
            {
                ++outResult.SplitGroups;
                groupSplit[groupIndex] = 1;
            }
            else
            {
                groupPage[groupIndex] = page;
            }
        }

        // 作ったグループの番号が、1つ細かい段のグループを指しているクラスタ。指す先が合わなければ値の照合へ戻る
        const auto hasSourceGroup = [&](const MeshCluster &cluster)
        {
            return cluster.LODLevel != 0 && cluster.SourceGroupId < groups.size() &&
                   groupUsable[cluster.SourceGroupId] != 0 &&
                   groups[cluster.SourceGroupId].LODLevel + 1u == cluster.LODLevel;
        };

        bool bNeedsValueMatch = false;
        for (const MeshCluster &cluster : clusters)
        {
            bNeedsValueMatch = bNeedsValueMatch || (cluster.LODLevel != 0 && !hasSourceGroup(cluster));
        }

        VariableArray<GroupKey> keys;
        if (bNeedsValueMatch)
        {
            keys.reserve(groups.size());
            for (uint32_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
            {
                if (groupUsable[groupIndex] == 0)
                {
                    continue;
                }
                // またがるグループも照合の候補に入れる（入れないと、同じ値の別のグループへ誤って結び付く）。
                // このグループから作られたクラスタは1つ粗い段（LODLevel + 1）にある
                const MeshClusterGroup &group = groups[groupIndex];
                keys.push_back(MakeKey(group.LODLevel + 1u, group.Bounds, group.Error, groupIndex));
                // 値の照合では、またがるグループの子のページは決められないので、メンバのページを固定する
                if (groupSplit[groupIndex] != 0)
                {
                    pinGroupMembers(groupIndex);
                }
            }
            std::sort(keys.begin(), keys.end(), KeyLess);
        }

        for (size_t clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex)
        {
            const MeshCluster &cluster = clusters[clusterIndex];
            if (cluster.LODLevel == 0)
            {
                continue;
            }
            if (hasSourceGroup(cluster))
            {
                if (groupSplit[cluster.SourceGroupId] != 0)
                {
                    pinGroupMembers(cluster.SourceGroupId);
                }
                outChildPages[clusterIndex] = groupPage[cluster.SourceGroupId];
                ++outResult.LinkedClusters;
                ++outResult.LinkedByIdClusters;
                continue;
            }
            const GroupKey probe = MakeKey(cluster.LODLevel, cluster.Bounds, cluster.LODError, 0);
            auto first = std::lower_bound(keys.begin(), keys.end(), probe, KeyLess);
            if (first == keys.end() || !KeySameValue(*first, probe))
            {
                continue;
            }
            // 同じ値の候補が全て同じページなら、生成元が違っても子のページは決まる
            auto last = first + 1;
            bool bSameChildPage = true;
            while (last != keys.end() && KeySameValue(*last, probe))
            {
                bSameChildPage = bSameChildPage && groupPage[last->GroupIndex] == groupPage[first->GroupIndex];
                ++last;
            }
            if (!bSameChildPage)
            {
                ++outResult.AmbiguousClusters;
                for (auto candidate = first; candidate != last; ++candidate)
                {
                    const uint32_t page = groupPage[candidate->GroupIndex];
                    if (page != INVALID_PAGE_ID)
                    {
                        pinned[page] = 1;
                    }
                }
            }
            outChildPages[clusterIndex] = groupPage[first->GroupIndex];
            ++outResult.LinkedClusters;
        }

        for (uint32_t page = 0; page < outResult.PageCount; ++page)
        {
            if (pinned[page] != 0)
            {
                outResult.PinnedPages.push_back(page);
            }
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
