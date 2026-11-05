#include "CombatSliceReactions.h"

#include "Engine/Engine.h"
#include "GameMode/GameModeContext.h"
#include "Gameplay/Combat/HealthComponent.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Gameplay/IGameplayModule.h"
#include "Gameplay/Shisha/ShishaComponent.h"
#include "Gameplay/World/InteractionService.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Particle/DecalPool.h"
#include "Particle/DefeatBloodPresenter.h"
#include "Rendering/RenderWorld.h"

#if NORVES_ENABLE_CORE_TEXT
#include "Component/TextComponent.h"
#endif

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    namespace Combat = NorvesLib::Modules::Gameplay::Combat;
    namespace Enemy = NorvesLib::Modules::Gameplay::Enemy;
    namespace Gameplay = NorvesLib::Modules::Gameplay;
    namespace Interaction = NorvesLib::Modules::Gameplay::World;

    struct CombatSliceReactions::Subscription
    {
        CombatSliceReactions* Target = nullptr;
    };

    CombatSliceReactions::CombatSliceReactions() = default;

    CombatSliceReactions::~CombatSliceReactions()
    {
        if (m_Subscription)
        {
            m_Subscription->Target = nullptr;
        }
    }

    bool CombatSliceReactions::Bind(GameModeContext& ctx, const CombatSliceSceneIds& ids)
    {
        if (m_Subscription)
        {
            NORVES_LOG_ERROR("CombatSlice", "戦闘の反応は既に結線されています。");
            return false;
        }

        auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        auto* owner = ctx.WorldRef.FindEntityByObjectId(ids.Shisha);
        auto* shisha = owner ? owner->GetComponent<Game::Gameplay::ShishaComponent>() : nullptr;
        if (!module || !shisha || shisha->IsPendingDestroy())
        {
            NORVES_LOG_ERROR("CombatSlice", "Gameplay モジュールまたはシ者を見つけられません。");
            return false;
        }

        m_Ids = ids;
        m_World = &ctx.WorldRef;
        m_BoundModule = module;
        m_ShishaComponentId = shisha->GetComponentId();
        m_PendingDamage.clear();
        m_DamageAppliedCount = 0;
        m_bDefeatTriggered = false;
        m_Decals = Container::MakeUnique<Particle::DecalPool>();
        m_Presenter = Container::MakeUnique<Particle::DefeatBloodPresenter>(
            ctx.EngineRef.GetParticleSystem().GetVfxEffects(), *m_Decals);

        m_Subscription = Container::MakeShared<Subscription>();
        m_Subscription->Target = this;
        const Container::TWeakPtr<Subscription> weak = m_Subscription;
        m_HitCallback = [weak](const Combat::CombatHitEvent& event)
        {
            if (const auto state = weak.lock(); state && state->Target)
            {
                state->Target->HandleHit(event);
            }
        };
        m_DefeatCallback = [weak](const Enemy::EnemyComponent& enemy)
        {
            if (const auto state = weak.lock(); state && state->Target)
            {
                state->Target->HandleDefeat(enemy);
            }
        };
        m_PromptCallback = [weak](const Interaction::InteractionPrompt&, const Interaction::InteractionPrompt& prompt)
        {
            if (const auto state = weak.lock(); state && state->Target)
            {
                state->Target->UpdatePrompt(prompt);
            }
        };

        module->GetOnAnyHit().Add(m_HitCallback);
        shisha->OnDefeatStarted.Add(m_DefeatCallback);
        auto& interaction = module->GetInteractionService();
        interaction.OnPromptChanged.Add(m_PromptCallback);
        UpdatePrompt(interaction.GetCurrentPrompt());
        return true;
    }

    void CombatSliceReactions::Unbind(GameModeContext& ctx)
    {
        if (!m_Subscription)
        {
            return;
        }

        // 配送用の写しが残っていても、以降はこの型へ入らない。
        m_Subscription->Target = nullptr;
        auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        if (module && module == m_BoundModule)
        {
            module->GetOnAnyHit().Remove(m_HitCallback);
            module->GetInteractionService().OnPromptChanged.Remove(m_PromptCallback);
        }
        auto* owner = ctx.WorldRef.FindEntityByObjectId(m_Ids.Shisha);
        auto* shisha = owner ? owner->GetComponent<Game::Gameplay::ShishaComponent>() : nullptr;
        if (shisha && shisha->GetComponentId() == m_ShishaComponentId)
        {
            shisha->OnDefeatStarted.Remove(m_DefeatCallback);
        }

        UpdatePrompt({});
        ctx.EngineRef.GetRenderWorld().SetDecals({});
        m_Presenter.reset();
        m_Decals.reset();
        m_PendingDamage.clear();
        m_Subscription.reset();
        m_HitCallback.Clear();
        m_DefeatCallback.Clear();
        m_PromptCallback.Clear();
        m_Ids = {};
        m_World = nullptr;
        m_BoundModule = nullptr;
        m_ShishaComponentId = 0;
    }

    void CombatSliceReactions::HandleHit(const Combat::CombatHitEvent& event)
    {
        // 当たりの配送中は判定を無効化せず、次の Tick で処理する。
        m_PendingDamage.push_back(event);
    }

    void CombatSliceReactions::ApplyPendingDamage(GameModeContext& ctx)
    {
        Container::VariableArray<Combat::CombatHitEvent> pending;
        pending.swap(m_PendingDamage);
        for (const auto& event : pending)
        {
            if (!m_Subscription)
            {
                break;
            }
            auto* shisha = ctx.WorldRef.FindEntityByObjectId(m_Ids.Shisha);
            auto* sword = ctx.WorldRef.FindEntityByObjectId(m_Ids.Sword);
            auto* hurtbox = shisha ? shisha->GetComponent<Combat::HurtboxSetComponent>() : nullptr;
            auto* health = shisha ? shisha->GetComponent<Combat::HealthComponent>() : nullptr;
            auto* hitbox = sword ? sword->GetComponent<Combat::HitboxComponent>() : nullptr;
            if (!hurtbox || !health || !hitbox || health->IsDefeated() ||
                event.VictimActorId != hurtbox->GetActorId())
            {
                continue;
            }
            const auto profile = hitbox->GetProfile();
            if (!profile || profile->ProfileId.GetHash() != event.ProfileId)
            {
                continue;
            }

            // HurtboxIndex は登録番号であり、部位は PartId の値で探す。
            for (const auto& part : hurtbox->GetParts())
            {
                if (part.PartId != event.PartId)
                {
                    continue;
                }
                const Combat::DamageInfo damage{profile->Damage * part.DamageScale, profile->Stagger};
                const auto result = health->ApplyDamage(damage);
                if (result.Outcome == Combat::EDamageOutcome::Applied ||
                    result.Outcome == Combat::EDamageOutcome::Defeated)
                {
                    ++m_DamageAppliedCount;
                }
                break;
            }
        }
    }

    void CombatSliceReactions::HandleDefeat(const Enemy::EnemyComponent& enemy)
    {
        auto* owner = enemy.GetOwner();
        if (!owner || !m_Presenter)
        {
            return;
        }
        auto position = owner->GetWorldTransform().position;
        position.y = 0.01f;
        const bool triggered = m_Presenter->Trigger(position, NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));
        m_bDefeatTriggered = m_bDefeatTriggered || triggered;
        NORVES_LOG_INFO("CombatSlice", "撃破の血を始めます: %s", triggered ? "成功" : "失敗");
    }

    void CombatSliceReactions::UpdatePrompt(const Interaction::InteractionPrompt& prompt)
    {
#if NORVES_ENABLE_CORE_TEXT
        auto* owner = m_World && m_Ids.Prompt ? m_World->FindEntityByObjectId(m_Ids.Prompt) : nullptr;
        auto* text = owner ? owner->GetComponent<Component::TextComponent>() : nullptr;
        if (text)
        {
            text->SetVisible(prompt.IsValid());
            if (prompt.IsValid())
            {
                text->SetText("[E] " + prompt.PromptTextKey);
            }
        }
#else
        (void)prompt;
#endif
    }

    void CombatSliceReactions::TickPresenter(float deltaSeconds)
    {
        if (m_Presenter)
        {
            m_Presenter->Tick(deltaSeconds);
        }
    }

    void CombatSliceReactions::TickDecalsAndSubmit(float deltaSeconds, Rendering::RenderWorld& render)
    {
        if (m_Decals)
        {
            m_Decals->Tick(deltaSeconds);
            render.SetDecals(m_Decals->BuildScene());
        }
    }

    uint32_t CombatSliceReactions::GetDamageAppliedCount() const
    {
        return m_DamageAppliedCount;
    }

    bool CombatSliceReactions::HasDefeatTriggered() const
    {
        return m_bDefeatTriggered;
    }

    uint32_t CombatSliceReactions::GetPresenterActiveCount() const
    {
        return m_Presenter ? m_Presenter->GetActiveCount() : 0;
    }
} // namespace Game::GameModes
