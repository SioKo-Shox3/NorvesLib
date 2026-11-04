// 複数フレームの再ヒット抑止、攻撃窓の更新、時間間隔と部位選択を検証する。
#include "CombatTestSupport.h"

namespace
{
    using namespace NorvesLib::Test::CombatSupport;

    Combat::CombatShape Blade()
    {
        return Capsule({0, 0, 0}, {0, 1, 0}, 0.02f);
    }

    Combat::CombatShape Target(float x = 0.0f)
    {
        return Sphere(0.05f, {x, 0.5f, 0});
    }

    void CheckDelivery(const EventRecorder& hit, size_t hitIndex,
                       const EventRecorder& hurt, size_t hurtIndex,
                       const EventRecorder& any, size_t anyIndex,
                       uint64_t instanceId, uint64_t victimId, uint32_t partId)
    {
        COMBAT_CHECK(hitIndex < hit.Count() && hurtIndex < hurt.Count() && anyIndex < any.Count());
        COMBAT_CHECK(instanceId != 0 && victimId != 0);
        const auto& hitEvent = hit.Events[hitIndex];
        const auto& hurtEvent = hurt.Events[hurtIndex];
        const auto& anyEvent = any.Events[anyIndex];
        for (const auto* event : {&hitEvent, &hurtEvent, &anyEvent})
        {
            COMBAT_CHECK(event->AttackInstanceId == instanceId);
            COMBAT_CHECK(event->VictimActorId == victimId && event->PartId == partId);
            COMBAT_CHECK(event->FrameNumber != 0 && event->FrameNumber == hitEvent.FrameNumber);
        }
    }

    void OncePerVictimAndReopen()
    {
        constexpr float dt = 0.05f;
        auto& sword = SpawnEntity({-0.25f, 0, 0});
        Combat::AttackProfile settings;
        settings.RehitPolicy = Combat::ERehitPolicy::OncePerVictim;
        auto& hitbox = AddHitbox(sword, Blade(), MakeProfile(settings));
        auto& victim = SpawnEntity();
        auto& hurtbox = AddHurtbox(victim, 1, Target());
        EventRecorder hit, hurt, any;
        hit.Subscribe(hitbox.OnHit);
        hurt.Subscribe(hurtbox.OnHurt);
        any.Subscribe(GetGameplay().GetOnAnyHit());

        COMBAT_CHECK(hitbox.BeginAttack());
        const uint64_t firstInstance = hitbox.GetAttackInstanceId();
        StepFrame(dt);
        COMBAT_CHECK(hit.Count() == 0 && hurt.Count() == 0 && any.Count() == 0);

        // 刀身と球が重なったまま 3 フレームかけて少しずつ移動する。
        for (const float x : {-0.03f, 0.0f, 0.03f})
        {
            SetPosition(sword, {x, 0, 0});
            StepFrame(dt);
            COMBAT_CHECK(hit.Count() == 1 && hurt.Count() == 1 && any.Count() == 1);
            CheckDelivery(hit, 0, hurt, 0, any, 0, firstInstance, hurtbox.GetActorId(), 1);
        }

        // 同じ配置で開き直し、初回フレームで刀身を外へ戻してから再び重ねる。
        hitbox.EndAttack();
        COMBAT_CHECK(hitbox.BeginAttack());
        const uint64_t secondInstance = hitbox.GetAttackInstanceId();
        COMBAT_CHECK(secondInstance > firstInstance);
        SetPosition(sword, {-0.25f, 0, 0});
        StepFrame(dt);
        COMBAT_CHECK(hit.Count() == 1 && hurt.Count() == 1 && any.Count() == 1);
        SetPosition(sword, {-0.03f, 0, 0});
        StepFrame(dt);
        COMBAT_CHECK(hit.Count() == 2 && hurt.Count() == 2 && any.Count() == 2);
        CheckDelivery(hit, 1, hurt, 1, any, 1, secondInstance, hurtbox.GetActorId(), 1);

        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        hit.Unsubscribe(hitbox.OnHit);
        hurt.Unsubscribe(hurtbox.OnHurt);
        GetWorld().RemoveEntity(&sword);
        GetWorld().RemoveEntity(&victim);
        std::puts("CombatMultiHitTest: 重複抑止と窓の更新を検証済み");
    }

    void IntervalRehit()
    {
        constexpr float dt = 0.05f;
        auto& sword = SpawnEntity({-0.25f, 0, 0});
        Combat::AttackProfile settings;
        settings.MaxDurationSeconds = 2.0f;
        settings.RehitPolicy = Combat::ERehitPolicy::Interval;
        settings.RehitIntervalSeconds = 0.3f;
        auto& hitbox = AddHitbox(sword, Blade(), MakeProfile(settings));
        auto& victim = SpawnEntity();
        auto& hurtbox = AddHurtbox(victim, 1, Target());
        EventRecorder hit, hurt, any;
        hit.Subscribe(hitbox.OnHit);
        hurt.Subscribe(hurtbox.OnHurt);
        any.Subscribe(GetGameplay().GetOnAnyHit());

        COMBAT_CHECK(hitbox.BeginAttack());
        const uint64_t instanceId = hitbox.GetAttackInstanceId();
        StepFrame(dt);
        COMBAT_CHECK(hit.Count() == 0 && hurt.Count() == 0 && any.Count() == 0);
        SetPosition(sword, {0, 0, 0});

        // 最初の命中を時刻 0 とし、以後は試験側で dt を積み上げる。
        double elapsed = 0.0;
        double previousHitTime = 0.0;
        uint64_t firstHitFrame = 0;
        for (size_t frame = 0; frame <= 24; ++frame)
        {
            StepFrame(dt);
            if (frame != 0)
            {
                elapsed += static_cast<double>(dt);
            }
            const size_t expectedCount = 1 + frame / 6;
            COMBAT_CHECK(hit.Count() == expectedCount && hurt.Count() == expectedCount &&
                         any.Count() == expectedCount);
            if (frame % 6 == 0)
            {
                const size_t index = expectedCount - 1;
                CheckDelivery(hit, index, hurt, index, any, index, instanceId, hurtbox.GetActorId(), 1);
                if (frame == 0)
                {
                    firstHitFrame = hit.Events[index].FrameNumber;
                }
                else
                {
                    COMBAT_CHECK(std::fabs(elapsed - previousHitTime - 0.3) < 1.0e-4);
                }
                COMBAT_CHECK(hit.Events[index].FrameNumber == firstHitFrame + frame);
                COMBAT_CHECK(std::fabs(elapsed - 0.3 * static_cast<double>(index)) < 1.0e-4);
                previousHitTime = elapsed;
            }
        }
        COMBAT_CHECK(hit.Count() == 5 && hurt.Count() == 5 && any.Count() == 5);

        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        hit.Unsubscribe(hitbox.OnHit);
        hurt.Unsubscribe(hurtbox.OnHurt);
        GetWorld().RemoveEntity(&sword);
        GetWorld().RemoveEntity(&victim);
        std::puts("CombatMultiHitTest: 0.3 秒間隔を検証済み");
    }

    void PerVictimSelection(Combat::EPerVictimPolicy policy, bool includeOtherVictim)
    {
        constexpr float dt = 0.05f;
        auto& sword = SpawnEntity({-0.5f, 0, 0});
        Combat::AttackProfile settings;
        settings.RehitPolicy = Combat::ERehitPolicy::OncePerPart;
        settings.PerVictimPolicy = policy;
        auto& hitbox = AddHitbox(sword, Blade(), MakeProfile(settings));
        auto& victim = SpawnEntity();
        auto& hurtbox = AddHurtboxSet(victim);
        if (policy == Combat::EPerVictimPolicy::FirstPart)
        {
            // 登録順に依存せず接触時刻が早い部位を選ぶことを確かめる。
            AddPart(hurtbox, 2, Target(0.12f), ~uint32_t{0}, 5);
            AddPart(hurtbox, 1, Target(-0.12f), ~uint32_t{0}, 0);
        }
        else
        {
            AddPart(hurtbox, 1, Target(-0.12f), ~uint32_t{0}, 0);
            AddPart(hurtbox, 2, Target(0.12f), ~uint32_t{0}, 5);
        }
        C::Entity* otherVictim = nullptr;
        Combat::HurtboxSetComponent* otherHurtbox = nullptr;
        if (includeOtherVictim)
        {
            otherVictim = &SpawnEntity({0.32f, 0, 0});
            otherHurtbox = &AddHurtbox(*otherVictim, 3, Target());
            COMBAT_CHECK(otherHurtbox->GetActorId() != 0 &&
                         otherHurtbox->GetActorId() != hurtbox.GetActorId());
        }
        EventRecorder hit, hurt, otherHurt, any;
        hit.Subscribe(hitbox.OnHit);
        hurt.Subscribe(hurtbox.OnHurt);
        if (otherHurtbox != nullptr)
        {
            otherHurt.Subscribe(otherHurtbox->OnHurt);
        }
        any.Subscribe(GetGameplay().GetOnAnyHit());

        COMBAT_CHECK(hitbox.BeginAttack());
        const uint64_t instanceId = hitbox.GetAttackInstanceId();
        StepFrame(dt);
        COMBAT_CHECK(hit.Count() == 0 && hurt.Count() == 0 && otherHurt.Count() == 0 && any.Count() == 0);
        SetPosition(sword, {0.5f, 0, 0});
        StepFrame(dt);

        if (policy == Combat::EPerVictimPolicy::FirstPart)
        {
            COMBAT_CHECK(includeOtherVictim && otherHurtbox != nullptr);
            COMBAT_CHECK(hit.Count() == 2 && hurt.Count() == 1 && otherHurt.Count() == 1 && any.Count() == 2);
            CheckDelivery(hit, 0, hurt, 0, any, 0, instanceId, hurtbox.GetActorId(), 1);
            CheckDelivery(hit, 1, otherHurt, 0, any, 1, instanceId, otherHurtbox->GetActorId(), 3);
            COMBAT_CHECK(hit.Events[0].FrameNumber == hit.Events[1].FrameNumber);
        }
        else if (policy == Combat::EPerVictimPolicy::HighestPriority)
        {
            COMBAT_CHECK(!includeOtherVictim);
            COMBAT_CHECK(hit.Count() == 1 && hurt.Count() == 1 && otherHurt.Count() == 0 && any.Count() == 1);
            CheckDelivery(hit, 0, hurt, 0, any, 0, instanceId, hurtbox.GetActorId(), 2);
        }
        else
        {
            COMBAT_CHECK(policy == Combat::EPerVictimPolicy::AllParts && !includeOtherVictim);
            COMBAT_CHECK(hit.Count() == 2 && hurt.Count() == 2 && otherHurt.Count() == 0 && any.Count() == 2);
            CheckDelivery(hit, 0, hurt, 0, any, 0, instanceId, hurtbox.GetActorId(), 1);
            CheckDelivery(hit, 1, hurt, 1, any, 1, instanceId, hurtbox.GetActorId(), 2);
            COMBAT_CHECK(hit.Events[0].FrameNumber == hit.Events[1].FrameNumber);
            COMBAT_CHECK(hit.Events[0].S > 0.0f && hit.Events[0].S < hit.Events[1].S && hit.Events[1].S < 1.0f);
        }

        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        hit.Unsubscribe(hitbox.OnHit);
        hurt.Unsubscribe(hurtbox.OnHurt);
        if (otherHurtbox != nullptr)
        {
            otherHurt.Unsubscribe(otherHurtbox->OnHurt);
        }
        GetWorld().RemoveEntity(&sword);
        GetWorld().RemoveEntity(&victim);
        if (otherVictim != nullptr)
        {
            GetWorld().RemoveEntity(otherVictim);
        }
    }
} // namespace

int main()
{
    ConfigureFailureReporting();
    OncePerVictimAndReopen();
    IntervalRehit();
    PerVictimSelection(Combat::EPerVictimPolicy::FirstPart, true);
    PerVictimSelection(Combat::EPerVictimPolicy::HighestPriority, false);
    PerVictimSelection(Combat::EPerVictimPolicy::AllParts, false);
    std::puts("CombatMultiHitTest: 部位選択を検証済み");
    std::puts("CombatMultiHitTest PASS");
    return 0;
}
