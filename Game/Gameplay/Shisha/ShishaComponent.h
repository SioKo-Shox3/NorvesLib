#pragma once

#include "Gameplay/Enemy/EnemyComponent.h"

namespace Game::Gameplay
{
    /// シ者の共通の敵挙動。撃破時は定義の応答 ID を表で引いて実行する。
    /// 枯れ度の源の鍵・回復秒数・目標値は PROPERTY で指定する。
    class ShishaComponent : public NorvesLib::Modules::Gameplay::Enemy::EnemyComponent
    {
        REFLECTION_CLASS(ShishaComponent, NorvesLib::Modules::Gameplay::Enemy::EnemyComponent)

    public:
        ShishaComponent();

        /// 所属 World にこの型の Tick 本体を登録する。
        /// 初回更新は次の Tick の収集から。収集中に初期化した場合、そのフレームは更新せず次フレームから動く。
        void Initialize() override;
        void BeginPlay() override;
        void EndPlay() override;

        PROPERTY(NorvesLib::Core::Container::String, WitherSourceKey)
        PROPERTY(float, WitherRecoverSeconds)
        PROPERTY(float, WitherRecoverTarget)

    private:
        void OnShishaDefeatCompleted(const NorvesLib::Core::Container::String& enemyId);

        bool m_bDefeatResponseSubscribed = false;
    };
} // namespace Game::Gameplay
