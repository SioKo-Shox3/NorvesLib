#pragma once

#include "Gameplay/Enemy/EnemyComponent.h"

namespace Game::Gameplay
{
    /// シ者の共通の敵挙動。シ者固有の状態の中身と応答はこの派生型に追加する。
    class ShishaComponent : public NorvesLib::Modules::Gameplay::Enemy::EnemyComponent
    {
        REFLECTION_CLASS(ShishaComponent, NorvesLib::Modules::Gameplay::Enemy::EnemyComponent)

    public:
        ShishaComponent();

        /// 所属 World にこの型の Tick 本体を登録する。
        /// 初回更新は次の Tick の収集から。収集中に初期化した場合、そのフレームは更新せず次フレームから動く。
        void Initialize() override;
    };
} // namespace Game::Gameplay
