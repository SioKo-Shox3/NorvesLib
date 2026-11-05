// 全場面で再利用する地面・空・代役と、土台だけを見せる base の入口。
#pragma once

#include "GameModes/ShishaShowcase/ShishaShowcaseScenes.h"

namespace NorvesLib::Core::Component
{
    class SkinnedMeshComponent;
}

namespace Game::GameModes
{
    /// 地面・空・共通カメラを組み、代役の glTF を同期で読み込む。
    bool BuildShishaShowcaseStage(ShishaShowcaseSceneContext& ctx);
    /// 代役を取り付け、経過時間で最初のクリップを再生する。準備完了後も毎 Tick 呼ぶ。
    bool UpdateShishaShowcaseStage(ShishaShowcaseSceneContext& ctx);
    /// 土台の所有状態を返す。Entity 等は Scope が返す。
    void LeaveShishaShowcaseStage(ShishaShowcaseSceneContext& ctx);
    /// 取り付け済みの代役を借りる。準備前または退場後は nullptr を返す。
    NorvesLib::Core::Component::SkinnedMeshComponent* GetShishaShowcaseStandin(
        const ShishaShowcaseSceneContext& ctx);

    bool BuildShishaShowcaseBaseScene(ShishaShowcaseSceneContext& ctx);
    bool UpdateShishaShowcaseBaseScene(ShishaShowcaseSceneContext& ctx);
    void LeaveShishaShowcaseBaseScene(ShishaShowcaseSceneContext& ctx);
} // namespace Game::GameModes
