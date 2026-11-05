#pragma once

#include "CombatSliceTypes.h"
#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "Delegate/Delegate.h"
#include "Gameplay/Combat/CombatHitEvent.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class World;

    namespace GameMode { struct GameModeContext; }
    namespace Particle { class DecalPool; class DefeatBloodPresenter; }
    namespace Rendering { class RenderWorld; }
}

namespace NorvesLib::Modules::Gameplay
{
    class IGameplayModule;
    namespace Enemy { class EnemyComponent; }
    namespace World { struct InteractionPrompt; }
}

namespace Game::GameModes
{
    // 戦闘通知からゲーム固有の反応と描画用デカールをつなぐ。
    class CombatSliceReactions
    {
    public:
        CombatSliceReactions();
        ~CombatSliceReactions();
        CombatSliceReactions(const CombatSliceReactions&) = delete;
        CombatSliceReactions& operator=(const CombatSliceReactions&) = delete;
        CombatSliceReactions(CombatSliceReactions&&) = delete;
        CombatSliceReactions& operator=(CombatSliceReactions&&) = delete;

        bool Bind(NorvesLib::Core::GameMode::GameModeContext& ctx, const CombatSliceSceneIds& ids);
        void Unbind(NorvesLib::Core::GameMode::GameModeContext& ctx);
        void ApplyPendingDamage(NorvesLib::Core::GameMode::GameModeContext& ctx);
        void TickPresenter(float deltaSeconds);
        void TickDecalsAndSubmit(float deltaSeconds, NorvesLib::Core::Rendering::RenderWorld& render);
        uint32_t GetDamageAppliedCount() const;
        bool HasDefeatTriggered() const;
        uint32_t GetPresenterActiveCount() const;

    private:
        struct Subscription;

        void HandleHit(const NorvesLib::Modules::Gameplay::Combat::CombatHitEvent& event);
        void HandleDefeat(const NorvesLib::Modules::Gameplay::Enemy::EnemyComponent& enemy);
        void UpdatePrompt(const NorvesLib::Modules::Gameplay::World::InteractionPrompt& prompt);

        CombatSliceSceneIds m_Ids;
        NorvesLib::Core::World* m_World = nullptr;
        NorvesLib::Modules::Gameplay::IGameplayModule* m_BoundModule = nullptr;
        uint64_t m_ShishaComponentId = 0;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Modules::Gameplay::Combat::CombatHitEvent> m_PendingDamage;
        uint32_t m_DamageAppliedCount = 0;
        bool m_bDefeatTriggered = false;

        // 参照先の置き場より先に presenter を破棄する。
        NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Particle::DecalPool> m_Decals;
        NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Particle::DefeatBloodPresenter> m_Presenter;
        NorvesLib::Core::Container::TSharedPtr<Subscription> m_Subscription;
        NorvesLib::Core::Delegate<void, const NorvesLib::Modules::Gameplay::Combat::CombatHitEvent&> m_HitCallback;
        NorvesLib::Core::Delegate<void, const NorvesLib::Modules::Gameplay::Enemy::EnemyComponent&> m_DefeatCallback;
        NorvesLib::Core::Delegate<void, const NorvesLib::Modules::Gameplay::World::InteractionPrompt&,
                                  const NorvesLib::Modules::Gameplay::World::InteractionPrompt&> m_PromptCallback;
    };
} // namespace Game::GameModes
