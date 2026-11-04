// 戦闘の配送中の変更、再初期化、スレッド境界と Entity の破棄を検証する。
#include "CombatTestSupport.h"

namespace
{
    using namespace NorvesLib::Test::CombatSupport;
    constexpr float kDt = 1.0f / 60.0f;

    ProfilePtr RepeatingProfile(bool bAllParts = false)
    {
        Combat::AttackProfile settings;
        settings.MaxDurationSeconds = 10.0f;
        settings.RehitPolicy = Combat::ERehitPolicy::Interval;
        settings.RehitIntervalSeconds = 0.0f;
        settings.PerVictimPolicy = bAllParts ? Combat::EPerVictimPolicy::AllParts
                                            : Combat::EPerVictimPolicy::FirstPart;
        return MakeProfile(settings);
    }

    void CheckHeap()
    {
#if defined(_MSC_VER) && defined(_DEBUG)
        COMBAT_CHECK(_CrtCheckMemory());
#endif
    }

    void AdvanceAfterDestruction(C::EntityHandle handle)
    {
        GetWorld().EndFixedStep();
        COMBAT_CHECK(GetWorld().ResolveEntityHandle(handle) == nullptr);
        CheckHeap();
        for (int frame = 0; frame < 3; ++frame)
        {
            StepFrame(kDt);
            CheckHeap();
        }
    }

    struct DestroyOnHit
    {
        C::Entity* Victim = nullptr;
        uint64_t VictimId = 0;
        size_t Count = 0;

        void Receive(const Combat::CombatHitEvent& event)
        {
            if (event.VictimActorId == VictimId)
            {
                ++Count;
                Victim->MarkForDestroy();
            }
        }
    };

    void MarkVictimDuringDelivery()
    {
        auto& attacker = SpawnEntity();
        auto& hitbox = AddHitbox(attacker, Sphere(0.1f), RepeatingProfile(true));
        auto& victim = SpawnEntity();
        const auto victimHandle = GetWorld().GetEntityHandle(victim);
        auto& hurtbox = AddHurtbox(victim, 1, Sphere(0.1f));
        AddPart(hurtbox, 2, Sphere(0.1f));
        auto& control = SpawnEntity();
        auto& controlHurtbox = AddHurtbox(control, 3, Sphere(0.1f));
        const auto controlId = controlHurtbox.GetActorId();
        EventRecorder hit, hurt, controlHurt, any;
        hit.Subscribe(hitbox.OnHit);
        hurt.Subscribe(hurtbox.OnHurt);
        controlHurt.Subscribe(controlHurtbox.OnHurt);
        any.Subscribe(GetGameplay().GetOnAnyHit());
        COMBAT_CHECK(hitbox.BeginAttack());
        StepFrame(kDt);
        // 同じ配置で victim の両部位と対照へ、合計 3 候補が配送される。
        COMBAT_CHECK(hit.Count() == 3 && hurt.Count() == 2 && controlHurt.Count() == 1 && any.Count() == 3);
        hit.Clear();
        hurt.Clear();
        controlHurt.Clear();
        any.Clear();
        DestroyOnHit destroy{&victim, hurtbox.GetActorId()};
        hitbox.OnHit.Add(&destroy, &DestroyOnHit::Receive);
        StepFrame(kDt);
        // 当該候補の複製済み通知は完了し、次の部位だけが再検証で除かれる。
        COMBAT_CHECK(destroy.Count == 1 && victim.IsPendingDestroy());
        COMBAT_CHECK(hit.Count() == 2 && hurt.Count() == 1 && controlHurt.Count() == 1 && any.Count() == 2);
        COMBAT_CHECK(hurt.Events[0].PartId == 1 && any.Events[0].VictimActorId == destroy.VictimId);
        COMBAT_CHECK(any.Events[1].VictimActorId == controlId);
        std::puts("CombatLifecycleTest: 破棄予約した当該候補の OnHurt=1、OnAnyHit=1、後続部位=0、別 victim=1");
        hitbox.OnHit.Remove(&destroy, &DestroyOnHit::Receive);
        hit.Clear();
        any.Clear();
        AdvanceAfterDestruction(victimHandle);
        COMBAT_CHECK(hurt.Count() == 1 && hit.Count() == 3 && any.Count() == 3 && controlHurt.Count() == 4);
        for (const auto& event : any.Events)
        {
            COMBAT_CHECK(event.VictimActorId == controlId);
        }
        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        COMBAT_CHECK(GetWorld().RemoveEntity(&attacker));
        COMBAT_CHECK(GetWorld().RemoveEntity(&control));
        std::puts("CombatLifecycleTest: 配送中の victim 破棄 検証済み");
    }

    void ShutdownAndInitialize()
    {
        auto& gameplay = GetGameplay();
        auto& attacker = SpawnEntity();
        auto& hitbox = AddHitbox(attacker, Sphere(0.1f), MakeProfile());
        auto& victim = SpawnEntity();
        auto& hurtbox = AddHurtbox(victim, 1, Sphere(0.1f));
        EventRecorder hit, hurt, any;
        hit.Subscribe(hitbox.OnHit);
        hurt.Subscribe(hurtbox.OnHurt);
        any.Subscribe(gameplay.GetOnAnyHit());
        for (int frame = 0; frame < 8; ++frame)
        {
            StepFrame(kDt);
        }
        COMBAT_CHECK(hitbox.BeginAttack());
        const auto oldId = hitbox.GetAttackInstanceId();
        const double oldOpenedAt = hitbox.GetWindowOpenedAt();
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 1 && hurt.Count() == 1 && any.Count() == 1);
        const auto oldFrame = hit.Events[0].FrameNumber;
        StepFrame(kDt);
        COMBAT_CHECK(hitbox.IsWindowOpen() && hit.Count() == 1 && hurt.Count() == 1 && any.Count() == 1);
        gameplay.Shutdown();
        COMBAT_CHECK(!hitbox.IsWindowOpen());
        COMBAT_CHECK(gameplay.Initialize());
        COMBAT_CHECK(!hitbox.IsWindowOpen());
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 1 && hurt.Count() == 1 && any.Count() == 1);
        COMBAT_CHECK(hitbox.BeginAttack());
        const auto newId = hitbox.GetAttackInstanceId();
        COMBAT_CHECK(newId > oldId);
        COMBAT_CHECK(oldOpenedAt > hitbox.GetWindowOpenedAt());
        COMBAT_CHECK(std::abs(hitbox.GetWindowOpenedAt() - static_cast<double>(kDt)) < 1.0e-6);
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 2 && hurt.Count() == 2 && any.Count() == 2);
        const auto& event = hit.Events[1];
        COMBAT_CHECK(event.AttackInstanceId == newId && event.VictimActorId == hurtbox.GetActorId());
        COMBAT_CHECK(event.FrameNumber == 2 && event.FrameNumber < oldFrame);
        COMBAT_CHECK(hurt.Events[1].FrameNumber == 2 && any.Events[1].FrameNumber == 2);
        any.Unsubscribe(gameplay.GetOnAnyHit());
        COMBAT_CHECK(GetWorld().RemoveEntity(&attacker));
        COMBAT_CHECK(GetWorld().RemoveEntity(&victim));
        std::puts("CombatLifecycleTest: 再初期化で窓・当たり済み・時計・フレームを初期化 検証済み");
    }

    void WrongThreadDoesNotMutate()
    {
        auto& gameplay = GetGameplay();
        auto& attacker = SpawnEntity({-1, 0, 0});
        auto& hitbox = AddHitbox(attacker, Sphere(0.1f), RepeatingProfile());
        auto& victim = SpawnEntity();
        auto& hurtbox = AddHurtbox(victim, 1, Sphere(0.1f));
        EventRecorder hit, hurt, any;
        hit.Subscribe(hitbox.OnHit);
        hurt.Subscribe(hurtbox.OnHurt);
        any.Subscribe(gameplay.GetOnAnyHit());
        COMBAT_CHECK(hitbox.BeginAttack());
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 0 && hurt.Count() == 0 && any.Count() == 0);
        SetPosition(attacker, {1, 0, 0});
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 1 && hurt.Count() == 1 && any.Count() == 1);
        const auto baseline = hit.Events[0];
        const auto instanceId = hitbox.GetAttackInstanceId();
        const auto openedAt = hitbox.GetWindowOpenedAt();
        SetPosition(attacker, {-1, 0, 0});
        bool bWorkerBegin = true;
        NorvesLib::Thread::Thread worker([&]()
        {
            gameplay.LateTick(100.0f);
            bWorkerBegin = hitbox.BeginAttack();
            hitbox.EndAttack();
        });
        worker.Join();
        COMBAT_CHECK(!bWorkerBegin && hitbox.IsWindowOpen());
        COMBAT_CHECK(hitbox.GetAttackInstanceId() == instanceId && hitbox.GetWindowOpenedAt() == openedAt);
        COMBAT_CHECK(hit.Count() == 1 && hurt.Count() == 1 && any.Count() == 1);
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 2 && hurt.Count() == 2 && any.Count() == 2);
        for (const auto* recorder : {&hit, &hurt, &any})
        {
            const auto& event = recorder->Events[1];
            COMBAT_CHECK(event.AttackInstanceId == instanceId && event.FrameNumber == baseline.FrameNumber + 1);
            COMBAT_CHECK(event.VictimActorId == baseline.VictimActorId && event.PartId == baseline.PartId);
            COMBAT_CHECK(event.S > 0.0f && event.S < 1.0f && std::abs(event.S - baseline.S) < 1.0e-4f);
        }
        COMBAT_CHECK(hitbox.IsWindowOpen());
        any.Unsubscribe(gameplay.GetOnAnyHit());
        COMBAT_CHECK(GetWorld().RemoveEntity(&attacker));
        COMBAT_CHECK(GetWorld().RemoveEntity(&victim));
        std::puts("CombatLifecycleTest: 別スレッドの操作は無変更、次の GameThread 配送は 1 フレーム後 検証済み");
    }

    void DestroyAttackerWithOpenWindow(bool bDestroyBinding)
    {
        auto& attacker = SpawnEntity();
        auto& sword = SpawnEntity({-1, 0, 0}, &attacker);
        auto& hitbox = AddHitbox(attacker, Sphere(0.1f), RepeatingProfile());
        COMBAT_CHECK(hitbox.SetBinding(&sword));
        auto& victim = SpawnEntity();
        auto& hurtbox = AddHurtbox(victim, 1, Sphere(0.1f));
        EventRecorder hit, hurt, any;
        hit.Subscribe(hitbox.OnHit);
        hurt.Subscribe(hurtbox.OnHurt);
        any.Subscribe(GetGameplay().GetOnAnyHit());
        COMBAT_CHECK(hitbox.BeginAttack());
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 0 && hurt.Count() == 0 && any.Count() == 0);
        SetPosition(sword, {1, 0, 0});
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 1 && hurt.Count() == 1 && any.Count() == 1);
        COMBAT_CHECK(hit.Events[0].VictimActorId == hurtbox.GetActorId());
        hitbox.EndAttack();
        hit.Clear();
        hurt.Clear();
        any.Clear();
        SetPosition(sword, {-1, 0, 0});
        COMBAT_CHECK(hitbox.BeginAttack());
        StepFrame(kDt);
        COMBAT_CHECK(hitbox.IsWindowOpen() && hit.Count() == 0 && hurt.Count() == 0 && any.Count() == 0);
        SetPosition(sword, {1, 0, 0});
        auto& destroyed = bDestroyBinding ? sword : attacker;
        const auto handle = GetWorld().GetEntityHandle(destroyed);
        destroyed.MarkForDestroy();
        StepFrame(kDt);
        // StepFrame は Gameplay だけを進めるため、この時点では Component の実体が残る。
        COMBAT_CHECK(!hitbox.IsWindowOpen());
        COMBAT_CHECK(hit.Count() == 0 && hurt.Count() == 0 && any.Count() == 0);
        AdvanceAfterDestruction(handle);
        COMBAT_CHECK(hit.Count() == 0 && hurt.Count() == 0 && any.Count() == 0);
        if (bDestroyBinding)
        {
            COMBAT_CHECK(!hitbox.IsWindowOpen());
            COMBAT_CHECK(GetWorld().RemoveEntity(&attacker));
        }
        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        COMBAT_CHECK(GetWorld().RemoveEntity(&victim));
        std::puts(bDestroyBinding ? "CombatLifecycleTest: 束縛先の子 Entity 破棄で窓を閉じる 検証済み"
                                 : "CombatLifecycleTest: 攻撃者 Entity 破棄で窓を閉じる 検証済み");
    }

    struct SubscribeDuringDelivery
    {
        HitSignal* Any = nullptr;
        HitSignal* Hit = nullptr;
        EventRecorder AddedAny;
        EventRecorder AddedHit;
        bool bSubscribed = false;

        void Receive(const Combat::CombatHitEvent&)
        {
            if (!bSubscribed)
            {
                bSubscribed = true;
                AddedAny.Subscribe(*Any);
                AddedHit.Subscribe(*Hit);
            }
        }
    };

    void AddedSubscribersWaitForNextNotification()
    {
        auto& attacker = SpawnEntity();
        auto& hitbox = AddHitbox(attacker, Sphere(0.1f), RepeatingProfile());
        auto& first = SpawnEntity();
        auto& firstHurtbox = AddHurtbox(first, 1, Sphere(0.1f));
        auto& second = SpawnEntity();
        auto& secondHurtbox = AddHurtbox(second, 2, Sphere(0.1f));
        auto& signal = GetGameplay().GetOnAnyHit();
        SubscribeDuringDelivery added{&signal, &hitbox.OnHit};
        // 最初の受け手が購読を追加し、その後に既存の受け手が配送を記録する。
        signal.Add(&added, &SubscribeDuringDelivery::Receive);
        EventRecorder hit, firstHurt, secondHurt, any;
        hit.Subscribe(hitbox.OnHit);
        firstHurt.Subscribe(firstHurtbox.OnHurt);
        secondHurt.Subscribe(secondHurtbox.OnHurt);
        any.Subscribe(signal);
        COMBAT_CHECK(hitbox.BeginAttack());
        const auto instanceId = hitbox.GetAttackInstanceId();
        StepFrame(kDt);
        COMBAT_CHECK(added.bSubscribed && hit.Count() == 2 && any.Count() == 2);
        COMBAT_CHECK(firstHurt.Count() == 1 && secondHurt.Count() == 1);
        const auto firstFrame = any.Events[0].FrameNumber;
        COMBAT_CHECK(any.Events[1].FrameNumber == firstFrame);
        COMBAT_CHECK(any.Events[0].VictimActorId != any.Events[1].VictimActorId);
        for (const auto* recorder : {&added.AddedAny, &added.AddedHit})
        {
            COMBAT_CHECK(recorder->Count() == 1);
            const auto& event = recorder->Events[0];
            const auto& nextNotification = any.Events[1];
            COMBAT_CHECK(event.VictimActorId == nextNotification.VictimActorId &&
                         event.PartId == nextNotification.PartId &&
                         event.AttackInstanceId == nextNotification.AttackInstanceId &&
                         event.FrameNumber == nextNotification.FrameNumber);
            COMBAT_CHECK(event.VictimActorId != any.Events[0].VictimActorId);
        }
        added.AddedAny.Clear();
        added.AddedHit.Clear();
        StepFrame(kDt);
        COMBAT_CHECK(hit.Count() == 4 && any.Count() == 4 && firstHurt.Count() == 2 && secondHurt.Count() == 2);
        COMBAT_CHECK(added.AddedAny.Count() == 2 && added.AddedHit.Count() == 2);
        for (const auto* recorder : {&added.AddedAny, &added.AddedHit})
        {
            for (const auto& event : recorder->Events)
            {
                COMBAT_CHECK(event.FrameNumber == firstFrame + 1 && event.AttackInstanceId == instanceId);
            }
        }
        signal.Remove(&added, &SubscribeDuringDelivery::Receive);
        added.AddedAny.Unsubscribe(signal);
        added.AddedHit.Unsubscribe(hitbox.OnHit);
        any.Unsubscribe(signal);
        COMBAT_CHECK(GetWorld().RemoveEntity(&attacker));
        COMBAT_CHECK(GetWorld().RemoveEntity(&first));
        COMBAT_CHECK(GetWorld().RemoveEntity(&second));
        std::puts("CombatLifecycleTest: 配送中の追加購読はその通知では呼ばれず次の通知から 検証済み");
    }
} // namespace

int main()
{
    ConfigureFailureReporting();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_ALLOC_MEM_DF |
                   _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_DELAY_FREE_MEM_DF);
    std::puts("CombatLifecycleTest: Debug CRT の常時ヒープ検査と解放済みブロックの保持を有効化");
#endif
    MarkVictimDuringDelivery();
    ShutdownAndInitialize();
    WrongThreadDoesNotMutate();
    DestroyAttackerWithOpenWindow(true);
    DestroyAttackerWithOpenWindow(false);
    AddedSubscribersWaitForNextNotification();
    CheckHeap();
    std::puts("CombatLifecycleTest PASS");
    return 0;
}
