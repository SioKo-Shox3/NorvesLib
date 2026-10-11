#pragma once

/**
 * @file EcsRenderSync.h
 * @brief 段 RenderSync の「変わった行」と Remove の一覧（設計 1.10）
 *
 * 一覧は変更ログ（描画の値の変更・変換の版の変化）・通知・公開の状態（EcsPublishState）から作る。
 * 同期は一覧の行だけを公開し、Remove だけを外す。結果は検査 ECS_RENDER_SYNC が全行から作り直した proxy と照合する。
 *
 * - 変わった行: 次の同期で proxy を作り直す行。鍵は proxy の鍵（Mega は Entity の ObjectId、他は ComponentId）。
 * - Remove: 構造の変化で proxy を外す行（行の削除・部分木の非アクティブ化・破棄待ち）。
 *   可視の変更や proxy を作れなかったことによる取り下げは、変わった行を作り直した結果として起きるので Remove には載せない。
 */

#include "Container/Containers.h"

#include <cstdint>

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
}
