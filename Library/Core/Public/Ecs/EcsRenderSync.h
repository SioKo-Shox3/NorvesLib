#pragma once

/**
 * @file EcsRenderSync.h
 * @brief 段 RenderSync の「変わった行」と Remove の一覧（設計 1.10）と、全走査との照合
 *
 * 一覧は変更ログ（描画の値の変更・変換の版の変化）・通知・公開の状態（EcsPublishState）から作る。
 * 同期はまだ全走査のままなので、一覧と全走査で見つけた変化が一致することを検査 ECS_RENDER_SYNC で確かめる。
 *
 * - 変わった行: 次の同期で proxy を作り直す行。鍵は proxy の鍵（Mega は Entity の ObjectId、他は ComponentId）。
 * - Remove: 構造の変化で proxy を外す行（行の削除・部分木の非アクティブ化・破棄待ち）。
 *   可視の変更や proxy を作れなかったことによる取り下げは、変わった行を作り直した結果として起きるので Remove には載せない。
 */

#include "Container/Containers.h"

#include <cstdint>
#include <cstdio>

namespace NorvesLib::Core::Ecs
{
    /// 一覧の項目の種類。Board は変わった行（出す先が決まる前）、WorldBoard・ScreenBoard は出している先
    enum class EcsRenderSyncKind : uint8_t
    {
        None = 0,
        Mesh,
        SkinnedMesh,
        MegaGeometry,
        Light,
        Board,
        WorldBoard,
        ScreenBoard,
    };

    /// 種類と proxy の鍵を 1 つの値に詰める（鍵は上位 8 bit を使わない大きさ）
    inline uint64_t EcsRenderSyncKey(EcsRenderSyncKind kind, uint64_t key)
    {
        return (static_cast<uint64_t>(kind) << 56) | (key & 0x00FFFFFFFFFFFFFFull);
    }

    inline EcsRenderSyncKind EcsRenderSyncKindOf(uint64_t packed)
    {
        return static_cast<EcsRenderSyncKind>(packed >> 56);
    }

    inline uint64_t EcsRenderSyncKeyOf(uint64_t packed)
    {
        return packed & 0x00FFFFFFFFFFFFFFull;
    }

    /// 1 回の同期の一覧
    struct EcsRenderSyncList
    {
        /// 変わった行（Mesh・SkinnedMesh・MegaGeometry・Light・Board）
        Container::UnorderedSet<uint64_t> Changed;
        /// Remove（Mesh・SkinnedMesh・MegaGeometry・Light・WorldBoard・ScreenBoard）
        Container::UnorderedSet<uint64_t> Removed;

        void Clear()
        {
            Changed.clear();
            Removed.clear();
        }
    };

    /// 全走査との照合の結果
    struct EcsRenderSyncReport
    {
        /// 照合した項目の数（走査で変わった行・外された proxy・Remove の合計）
        uint32_t Examined = 0;
        /// 走査では変わった行なのに、一覧に無い
        uint32_t ChangedMissing = 0;
        /// 一覧にあるのに、走査では変わった行でない
        uint32_t ChangedExtra = 0;
        /// 走査で外された proxy が、一覧の Remove にも変わった行にも無い
        uint32_t RemovedUnexplained = 0;
        /// 一覧の Remove にあるのに、走査後も生きている
        uint32_t RemovedStillLive = 0;
        /// 最初の食い違いの説明
        char FirstDetail[160] = {};

        [[nodiscard]] uint32_t Violations() const
        {
            return ChangedMissing + ChangedExtra + RemovedUnexplained + RemovedStillLive;
        }

        void NoteDetail(const char *what, uint64_t packed)
        {
            if (FirstDetail[0] == '\0')
            {
                std::snprintf(FirstDetail, sizeof(FirstDetail), "%s (kind=%u, key=%llu)", what,
                              static_cast<unsigned>(EcsRenderSyncKindOf(packed)),
                              static_cast<unsigned long long>(EcsRenderSyncKeyOf(packed)));
            }
        }
    };

    /**
     * @brief 一覧を、全走査の結果と照合する
     * @param list 同期の前に作った一覧
     * @param scanChanged 走査で proxy を作り直した行（Board は EcsRenderSyncKind::Board）
     * @param scanStale 走査の終わりに外された proxy（Board は出していた先の種類）
     * @param liveAfter 走査後に生きている proxy（Board は出している先の種類）
     */
    inline EcsRenderSyncReport EcsRenderSyncCompare(const EcsRenderSyncList &list,
                                                    const Container::UnorderedSet<uint64_t> &scanChanged,
                                                    const Container::UnorderedSet<uint64_t> &scanStale,
                                                    const Container::UnorderedSet<uint64_t> &liveAfter)
    {
        EcsRenderSyncReport report;
        report.Examined = static_cast<uint32_t>(scanChanged.size() + scanStale.size() + list.Removed.size());

        for (const uint64_t packed : scanChanged)
        {
            if (list.Changed.find(packed) == list.Changed.end())
            {
                ++report.ChangedMissing;
                report.NoteDetail("走査で変わった行が一覧に無い", packed);
            }
        }
        for (const uint64_t packed : list.Changed)
        {
            if (scanChanged.find(packed) == scanChanged.end())
            {
                ++report.ChangedExtra;
                report.NoteDetail("一覧の変わった行が走査では変わっていない", packed);
            }
        }
        for (const uint64_t packed : scanStale)
        {
            if (list.Removed.find(packed) != list.Removed.end())
            {
                continue;
            }
            const EcsRenderSyncKind kind = EcsRenderSyncKindOf(packed);
            const bool bBoard = kind == EcsRenderSyncKind::WorldBoard || kind == EcsRenderSyncKind::ScreenBoard;
            const uint64_t changedKey =
                EcsRenderSyncKey(bBoard ? EcsRenderSyncKind::Board : kind, EcsRenderSyncKeyOf(packed));
            if (list.Changed.find(changedKey) == list.Changed.end())
            {
                ++report.RemovedUnexplained;
                report.NoteDetail("走査で外された proxy が一覧に無い", packed);
            }
        }
        for (const uint64_t packed : list.Removed)
        {
            if (liveAfter.find(packed) != liveAfter.end())
            {
                ++report.RemovedStillLive;
                report.NoteDetail("一覧の Remove なのに走査後も生きている", packed);
            }
        }
        return report;
    }
}
