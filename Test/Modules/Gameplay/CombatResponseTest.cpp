// 当たりによるエフェクトとヒットストップの要求を検証する。
#include "CombatTestSupport.h"

namespace
{
    using namespace NorvesLib::Test::CombatSupport;

    constexpr float kFrameSeconds = 1.0f / 60.0f;

    void ClearPreviousRequests()
    {
        (void)GetGameplay();
        // 前の検査の攻撃を片付けたうえで、実時間を進めて倍率の要求を切らす。
        StepFrame(0.5f);
        COMBAT_CHECK(GetEngine().GetTimeSystem().GetActiveRequestCount() == 0);
        GetEngine().GetParticleSystem().GetHitEffects().ClearRequests();
    }

    void RemovePair(C::Entity& attacker, C::Entity& victim)
    {
        COMBAT_CHECK(GetWorld().RemoveEntity(&attacker));
        COMBAT_CHECK(GetWorld().RemoveEntity(&victim));
    }

    void HitEffectUsesContactAndSurface()
    {
        ClearPreviousRequests();
        GetGameplay().SetCombatResponseSettings({});
        auto& attacker = SpawnEntity({-0.1f, 0.0f, 0.0f});
        auto& hitbox = AddHitbox(attacker, Sphere(0.3f), MakeProfile());
        auto& victim = SpawnEntity();
        auto& hurtbox = AddHurtboxSet(victim);
        Combat::HurtboxPart part;
        part.PartId = 7;
        part.Shape = Sphere(0.3f);
        part.SurfaceName = C::Identity("Metal");
        COMBAT_CHECK(hurtbox.AddPart(part) != Combat::HurtboxSetComponent::InvalidPartIndex);

        EventRecorder events;
        events.Subscribe(GetGameplay().GetOnAnyHit());
        COMBAT_CHECK(hitbox.BeginAttack());
        StepFrame(kFrameSeconds);

        auto& emitter = GetEngine().GetParticleSystem().GetHitEffects();
        const auto& requests = emitter.GetPendingRequests();
        COMBAT_CHECK(events.Count() == 1 && requests.size() == 1);
        COMBAT_CHECK((requests[0].Position - events.Events[0].Point).Length() <= 1.0e-5f);
        COMBAT_CHECK((requests[0].Normal - events.Events[0].Normal.Normalized()).Length() <= 1.0e-5f);
        COMBAT_CHECK(emitter.SelectEffect("Metal") != emitter.SelectEffect(""));
        COMBAT_CHECK(requests[0].EffectIndex == emitter.SelectEffect("Metal"));
        events.Unsubscribe(GetGameplay().GetOnAnyHit());
        RemovePair(attacker, victim);
    }

    struct BeforeFlushRecorder
    {
        size_t Expected = 0;
        size_t Count = 0;

        void Record(const Combat::CombatHitEvent&)
        {
            COMBAT_CHECK(GetEngine().GetTimeSystem().GetActiveRequestCount() == Expected);
            ++Count;
        }
    };

    void HitStopCombinesHits()
    {
        ClearPreviousRequests();
        GetGameplay().SetCombatResponseSettings({});
        auto& first = SpawnEntity({-0.1f, 0.0f, 0.0f});
        auto& second = SpawnEntity({0.0f, -0.1f, 0.0f});
        auto& third = SpawnEntity({0.0f, 0.0f, -0.1f});
        Combat::AttackProfile profile;
        profile.HitStopSecondsHint = 0.03f;
        auto& firstHitbox = AddHitbox(first, Sphere(0.3f), MakeProfile(profile));
        profile.HitStopSecondsHint = 0.08f;
        auto& secondHitbox = AddHitbox(second, Sphere(0.3f), MakeProfile(profile));
        profile.HitStopSecondsHint = 0.16f;
        auto& thirdHitbox = AddHitbox(third, Sphere(0.3f), MakeProfile(profile));
        auto& victim = SpawnEntity();
        AddHurtbox(victim, 1, Sphere(0.3f));

        auto& signal = GetGameplay().GetOnAnyHit();
        EventRecorder events;
        events.Subscribe(signal);
        const size_t before = GetEngine().GetTimeSystem().GetActiveRequestCount();
        BeforeFlushRecorder beforeFlush{before};
        signal.Add(&beforeFlush, &BeforeFlushRecorder::Record);
        COMBAT_CHECK(firstHitbox.BeginAttack());
        COMBAT_CHECK(secondHitbox.BeginAttack());
        COMBAT_CHECK(thirdHitbox.BeginAttack());
        StepFrame(kFrameSeconds);

        COMBAT_CHECK(events.Count() == 3 && beforeFlush.Count == 3);
        COMBAT_CHECK(events.Events[0].FrameNumber == events.Events[1].FrameNumber &&
                     events.Events[1].FrameNumber == events.Events[2].FrameNumber);
        auto& time = GetEngine().GetTimeSystem();
        COMBAT_CHECK(time.GetActiveRequestCount() == before + 1);
        signal.Remove(&beforeFlush, &BeforeFlushRecorder::Record);
        events.Unsubscribe(signal);
        COMBAT_CHECK(GetWorld().RemoveEntity(&first));
        COMBAT_CHECK(GetWorld().RemoveEntity(&second));
        COMBAT_CHECK(GetWorld().RemoveEntity(&third));
        COMBAT_CHECK(GetWorld().RemoveEntity(&victim));

        // 最長の 0.16 秒より前には残り、経過後に消えることを確かめる。
        StepFrame(0.10f);
        COMBAT_CHECK(time.GetActiveRequestCount() == before + 1);
        StepFrame(0.07f);
        COMBAT_CHECK(time.GetActiveRequestCount() == before);
    }

    void SettingsDisableBothResponses()
    {
        ClearPreviousRequests();
        Combat::CombatResponseSettings settings;
        settings.bHitEffects = false;
        settings.bHitStop = false;
        GetGameplay().SetCombatResponseSettings(settings);

        auto& attacker = SpawnEntity({-0.1f, 0.0f, 0.0f});
        Combat::AttackProfile profile;
        profile.HitStopSecondsHint = 0.1f;
        auto& hitbox = AddHitbox(attacker, Sphere(0.3f), MakeProfile(profile));
        auto& victim = SpawnEntity();
        AddHurtbox(victim, 1, Sphere(0.3f));
        EventRecorder events;
        events.Subscribe(GetGameplay().GetOnAnyHit());
        COMBAT_CHECK(hitbox.BeginAttack());
        StepFrame(kFrameSeconds);

        COMBAT_CHECK(events.Count() == 1);
        COMBAT_CHECK(GetEngine().GetParticleSystem().GetHitEffects().GetPendingRequests().empty());
        COMBAT_CHECK(GetEngine().GetTimeSystem().GetActiveRequestCount() == 0);
        events.Unsubscribe(GetGameplay().GetOnAnyHit());
        RemovePair(attacker, victim);
        GetGameplay().SetCombatResponseSettings({});
    }
} // namespace

int main()
{
    ConfigureFailureReporting();
    HitEffectUsesContactAndSurface();
    HitStopCombinesHits();
    SettingsDisableBothResponses();
    std::puts("CombatResponseTest PASS");
    return 0;
}
