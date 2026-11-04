// faction と ActorId の除外を、同じ配置で命中する対照とともに検証する。
#include "CombatTestSupport.h"

namespace
{
    using namespace NorvesLib::Test::CombatSupport;

    Combat::CombatShape Blade()
    {
        return Capsule({0, 0, 0}, {0, 1, 0}, 0.02f);
    }

    Combat::CombatShape Target()
    {
        return Sphere(0.05f, {0, 0.5f, 0});
    }

    // 窓の前にも逆向きに横切らせ、初回が閉じた窓の履歴を使わないことを確かめる。
    void OpenAndSweep(Combat::HitboxComponent& hitbox, C::Entity& sword,
                      EventRecorder& onHit, EventRecorder& onAnyHit)
    {
        hitbox.EndAttack();
        SetPosition(sword, {0.25f, 0, 0});
        StepFrame(1.0f / 30.0f);
        SetPosition(sword, {-0.25f, 0, 0});
        COMBAT_CHECK(hitbox.BeginAttack());
        StepFrame(1.0f / 30.0f);
        COMBAT_CHECK(onHit.Count() == 0 && onAnyHit.Count() == 0);
        SetPosition(sword, {0.25f, 0, 0});
        StepFrame(1.0f / 30.0f);
    }

    void CheckDelivery(const EventRecorder& hits, const EventRecorder& hurts, const EventRecorder& any,
                       const Combat::HitboxComponent& hitbox, const Combat::HurtboxSetComponent& victim,
                       uint32_t partId)
    {
        COMBAT_CHECK(hits.Count() == 1 && hurts.Count() == 1 && any.Count() == 1);
        const auto& event = hits.Events[0];
        COMBAT_CHECK(event.AttackInstanceId != 0 && event.AttackInstanceId == hitbox.GetAttackInstanceId());
        COMBAT_CHECK(event.AttackerActorId == hitbox.GetActorId());
        COMBAT_CHECK(event.VictimActorId == victim.GetActorId() && event.PartId == partId);
        COMBAT_CHECK(event.HitboxIndex == hitbox.GetRegistrationIndex());
        COMBAT_CHECK(event.HurtboxIndex == victim.GetRegistrationIndex());
        COMBAT_CHECK(event.S > 0.0f && event.S < 1.0f);
        for (const auto* recorder : {&hurts, &any})
        {
            const auto& delivered = recorder->Events[0];
            COMBAT_CHECK(delivered.AttackInstanceId == event.AttackInstanceId &&
                         delivered.VictimActorId == event.VictimActorId && delivered.PartId == event.PartId &&
                         delivered.HitboxIndex == event.HitboxIndex && delivered.HurtboxIndex == event.HurtboxIndex &&
                         delivered.FrameNumber == event.FrameNumber);
        }
    }

    void FactionMask()
    {
        auto& sword = SpawnEntity();
        Combat::AttackProfile settings;
        settings.HitMask = 0b01;
        auto& hitbox = AddHitbox(sword, Blade(), MakeProfile(settings));
        auto& a = SpawnEntity();
        auto& excluded = AddHurtbox(a, 1, Target(), 0b10);
        EventRecorder hit, hurtA, hurtB, any;
        hit.Subscribe(hitbox.OnHit);
        hurtA.Subscribe(excluded.OnHurt);
        any.Subscribe(GetGameplay().GetOnAnyHit());
        OpenAndSweep(hitbox, sword, hit, any);
        COMBAT_CHECK(hit.Count() == 0 && hurtA.Count() == 0 && any.Count() == 0);

        // A と同じ位置・形の B だけをマスク内にし、3 系統すべての通知を確認する。
        auto& b = SpawnEntity();
        auto& included = AddHurtbox(b, 2, Target(), 0b01);
        hurtB.Subscribe(included.OnHurt);
        OpenAndSweep(hitbox, sword, hit, any);
        COMBAT_CHECK(hurtA.Count() == 0);
        CheckDelivery(hit, hurtB, any, hitbox, included, 2);
        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        GetWorld().RemoveEntity(&sword);
        GetWorld().RemoveEntity(&a);
        GetWorld().RemoveEntity(&b);
        std::puts("CombatLayerTest: faction 検証済み");
    }

    void SameActor()
    {
        auto& attacker = SpawnEntity();
        auto& sword = SpawnEntity({}, &attacker);
        // Component の持ち主は同じ Entity。姿勢だけを動く剣へ束縛する。
        auto& hitbox = AddHitbox(attacker, Blade(), MakeProfile());
        COMBAT_CHECK(hitbox.SetBinding(&sword));
        auto& self = AddHurtbox(attacker, 1, Target());
        auto& other = SpawnEntity();
        auto& control = AddHurtbox(other, 2, Target());
        COMBAT_CHECK(hitbox.GetActorId() != 0 && hitbox.GetActorId() == self.GetActorId());
        COMBAT_CHECK(control.GetActorId() != hitbox.GetActorId());
        EventRecorder hit, selfHurt, otherHurt, any;
        hit.Subscribe(hitbox.OnHit);
        selfHurt.Subscribe(self.OnHurt);
        otherHurt.Subscribe(control.OnHurt);
        any.Subscribe(GetGameplay().GetOnAnyHit());
        OpenAndSweep(hitbox, sword, hit, any);
        COMBAT_CHECK(selfHurt.Count() == 0);
        CheckDelivery(hit, otherHurt, any, hitbox, control, 2);
        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        GetWorld().RemoveEntity(&attacker);
        GetWorld().RemoveEntity(&other);
        std::puts("CombatLayerTest: 同一 Actor 検証済み");
    }

    void ChildWeaponActor()
    {
        auto& owner = SpawnEntity();
        auto& ownerHurtbox = AddHurtbox(owner, 1, Target());
        auto& other = SpawnEntity();
        auto& control = AddHurtbox(other, 2, Target());
        auto& sword = SpawnEntity({}, &owner);
        auto& explicitHitbox = AddHitbox(sword, Blade(), MakeProfile());
        explicitHitbox.SetActorId(ownerHurtbox.GetActorId());
        COMBAT_CHECK(explicitHitbox.GetActorId() == ownerHurtbox.GetActorId());
        EventRecorder hit, ownerHurt, otherHurt, any;
        hit.Subscribe(explicitHitbox.OnHit);
        ownerHurt.Subscribe(ownerHurtbox.OnHurt);
        otherHurt.Subscribe(control.OnHurt);
        any.Subscribe(GetGameplay().GetOnAnyHit());
        OpenAndSweep(explicitHitbox, sword, hit, any);
        COMBAT_CHECK(ownerHurt.Count() == 0);
        CheckDelivery(hit, otherHurt, any, explicitHitbox, control, 2);
        explicitHitbox.EndAttack();
        hit.Clear();
        otherHurt.Clear();
        any.Clear();

        // 既定 ActorId の子でも同じ軌道を通すと、持ち主まで被弾する。
        auto& defaultSword = SpawnEntity({}, &owner);
        auto& defaultHitbox = AddHitbox(defaultSword, Blade(), MakeProfile());
        COMBAT_CHECK(defaultHitbox.GetActorId() != 0 &&
                     defaultHitbox.GetActorId() != ownerHurtbox.GetActorId());
        hit.Subscribe(defaultHitbox.OnHit);
        OpenAndSweep(defaultHitbox, defaultSword, hit, any);
        COMBAT_CHECK(hit.Count() == 2 && ownerHurt.Count() == 1 && otherHurt.Count() == 1 && any.Count() == 2);
        COMBAT_CHECK(ownerHurt.Events[0].VictimActorId == ownerHurtbox.GetActorId());
        COMBAT_CHECK(otherHurt.Events[0].VictimActorId == control.GetActorId());
        for (size_t i = 0; i < hit.Count(); ++i)
        {
            COMBAT_CHECK(hit.Events[i].AttackInstanceId == defaultHitbox.GetAttackInstanceId());
            COMBAT_CHECK(hit.Events[i].AttackerActorId == defaultHitbox.GetActorId());
            COMBAT_CHECK(hit.Events[i].VictimActorId == any.Events[i].VictimActorId);
            COMBAT_CHECK(hit.Events[i].S > 0.0f && hit.Events[i].S < 1.0f);
        }
        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        GetWorld().RemoveEntity(&owner);
        GetWorld().RemoveEntity(&other);
        std::puts("CombatLayerTest: 子の Actor 明示指定 検証済み");
    }
} // namespace

int main()
{
    ConfigureFailureReporting();
    FactionMask();
    SameActor();
    ChildWeaponActor();
    std::puts("CombatLayerTest PASS");
    return 0;
}
