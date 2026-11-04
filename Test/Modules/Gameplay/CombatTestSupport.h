// 戦闘試験で Engine の World、論理時計、形状と通知の記録を共有する。
#pragma once

#include "Component/AnimatorComponent.h"
#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "Engine/Engine.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Gameplay/IGameplayModule.h"
#include "Object/World.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#define COMBAT_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "戦闘試験の条件違反 %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

namespace NorvesLib::Test::CombatSupport
{
    namespace C = NorvesLib::Core;
    namespace M = NorvesLib::Math;
    namespace G = NorvesLib::Modules::Gameplay;
    namespace Combat = NorvesLib::Modules::Gameplay::Combat;
    using ProfilePtr = C::Container::TSharedPtr<const Combat::AttackProfile>;
    using HitSignal = C::MulticastDelegate<const Combat::CombatHitEvent&>;

    inline void ConfigureFailureReporting()
    {
#ifdef _MSC_VER
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    }

    inline C::Engine::Engine& GetEngine()
    {
        // Engine は大きいためヒープに置き、プロセスの終了まで保持する。
        static C::Engine::Engine* engine = new C::Engine::Engine();
        return *engine;
    }

    inline G::IGameplayModule& GetGameplay()
    {
        static G::IGameplayModule* gameplay = []()
        {
            ConfigureFailureReporting();
            auto& engine = GetEngine();
            engine.GetWorld().Initialize();
            auto& registry = C::Module::GetModuleRegistry();
            auto* module = G::RegisterGameplayModule(registry);
            COMBAT_CHECK(module != nullptr);
            COMBAT_CHECK(registry.InstallAll(engine));
            COMBAT_CHECK(G::FindGameplayModule(registry) == module);
            return module;
        }();
        return *gameplay;
    }

    inline C::World& GetWorld()
    {
        (void)GetGameplay();
        return GetEngine().GetWorld();
    }

    // dt は実時間の秒。BeginFrame の rawNanoseconds は絶対時刻ではなく経過時間。
    inline void StepFrame(float dt)
    {
        auto& gameplay = GetGameplay();
        COMBAT_CHECK(std::isfinite(dt) && dt >= 0.0f && dt < 9.0e9f);
        const auto nanoseconds = static_cast<int64_t>(std::llround(static_cast<double>(dt) * 1.0e9));
        COMBAT_CHECK(GetEngine().GetTimeSystem().BeginFrame(nanoseconds, dt) ==
                     C::Engine::TimeSystemResult::Success);
        gameplay.LateTick(dt);
    }

    inline void SetPosition(C::Entity& entity, const M::Vector3& position)
    {
        entity.SetLocalPosition(position.x, position.y, position.z);
    }

    inline void SetRotation(C::Entity& entity, const M::Quaternion& rotation)
    {
        entity.SetLocalRotation(rotation);
    }

    inline C::Entity& SpawnEntity(const M::Vector3& position = {}, C::Entity* parent = nullptr)
    {
        auto* entity = GetWorld().SpawnEntity<C::Entity>(parent);
        COMBAT_CHECK(entity != nullptr);
        SetPosition(*entity, position);
        return *entity;
    }

    inline Combat::CombatShape Sphere(float radius, const M::Vector3& center = {})
    {
        Combat::CombatShape shape;
        shape.Type = Combat::ECombatShapeType::Sphere;
        shape.Center = center;
        shape.Radius = radius;
        return shape;
    }

    inline Combat::CombatShape Capsule(const M::Vector3& a, const M::Vector3& b, float radius)
    {
        Combat::CombatShape shape;
        shape.Type = Combat::ECombatShapeType::Capsule;
        shape.PointA = a;
        shape.PointB = b;
        shape.Radius = radius;
        return shape;
    }

    inline ProfilePtr MakeProfile(const Combat::AttackProfile& settings = {})
    {
        // const の実体を作り、呼び出し元の設定の変更と切り離す。
        return C::Container::MakeShared<const Combat::AttackProfile>(settings);
    }

    inline Combat::HitboxComponent& AddHitbox(C::Entity& owner, const Combat::CombatShape& shape,
                                             const ProfilePtr& profile)
    {
        auto* hitbox = GetWorld().CreateComponent<Combat::HitboxComponent>(&owner);
        COMBAT_CHECK(hitbox != nullptr);
        COMBAT_CHECK(hitbox->SetShape(shape));
        hitbox->SetProfile(profile);
        return *hitbox;
    }

    inline Combat::HurtboxSetComponent& AddHurtboxSet(C::Entity& owner)
    {
        auto* hurtbox = GetWorld().CreateComponent<Combat::HurtboxSetComponent>(&owner);
        COMBAT_CHECK(hurtbox != nullptr);
        return *hurtbox;
    }

    inline uint32_t AddPart(Combat::HurtboxSetComponent& hurtbox, uint32_t partId,
                           const Combat::CombatShape& shape, uint32_t faction = ~uint32_t{0},
                           int32_t priority = 0, C::EntityHandle binding = {})
    {
        Combat::HurtboxPart part;
        part.PartId = partId;
        part.Shape = shape;
        part.Faction = faction;
        part.Priority = priority;
        part.Binding = binding;
        const auto index = hurtbox.AddPart(part);
        COMBAT_CHECK(index != Combat::HurtboxSetComponent::InvalidPartIndex);
        return index;
    }

    inline Combat::HurtboxSetComponent& AddHurtbox(C::Entity& owner, uint32_t partId,
                                                 const Combat::CombatShape& shape,
                                                 uint32_t faction = ~uint32_t{0}, int32_t priority = 0)
    {
        auto& hurtbox = AddHurtboxSet(owner);
        AddPart(hurtbox, partId, shape, faction, priority);
        return hurtbox;
    }

    inline C::Component::AnimatorComponent& AddAnimator(C::Entity& owner)
    {
        auto* animator = GetWorld().CreateComponent<C::Component::AnimatorComponent>(&owner);
        COMBAT_CHECK(animator != nullptr);
        // Hitbox が先にある場合も、同じ Entity のイベントを受けられるようにする。
        for (auto* component : owner.GetComponents())
        {
            if (auto* hitbox = C::CastTo<Combat::HitboxComponent>(component))
            {
                COMBAT_CHECK(hitbox->BindAnimator(animator));
            }
        }
        return *animator;
    }

    struct EventRecorder
    {
        C::Container::VariableArray<Combat::CombatHitEvent> Events;

        void Record(const Combat::CombatHitEvent& event)
        {
            Events.push_back(event);
        }

        // 受け手を破棄する前に Unsubscribe する。配送中の購読変更も試験できる。
        void Subscribe(HitSignal& signal)
        {
            signal.Add(this, &EventRecorder::Record);
        }

        void Unsubscribe(HitSignal& signal)
        {
            signal.Remove(this, &EventRecorder::Record);
        }

        void Clear()
        {
            Events.clear();
        }

        size_t Count() const
        {
            return Events.size();
        }
    };
} // namespace NorvesLib::Test::CombatSupport
