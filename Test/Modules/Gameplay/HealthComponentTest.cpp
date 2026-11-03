// 過剰ダメージ・撃破後・不正入力・無敵時の状態と通知、連続ダメージ、スタッガの通知順を反証する。
#include "Gameplay/Combat/HealthComponent.h"
#include "Object/World.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Combat;
    namespace C = NorvesLib::Core;

#define HEALTH_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "HealthComponent %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    struct Fixture
    {
        C::World World;
        HealthComponent* Health = nullptr;

        Fixture()
        {
            World.Initialize();
            auto* entity = World.SpawnEntity<C::Entity>();
            HEALTH_CHECK(entity != nullptr);
            Health = World.CreateComponent<HealthComponent>(entity);
            HEALTH_CHECK(Health != nullptr);
        }

        ~Fixture()
        {
            World.Finalize();
        }
    };

    void ClampAndLatch()
    {
        Fixture fixture;
        auto& health = *fixture.Health;
        health.getHealth() = 10.0f;
        unsigned damaged = 0;
        unsigned defeated = 0;
        health.OnDamaged.Add([&](const HealthComponent& source, const DamageResult& result)
        {
            HEALTH_CHECK(&source == &health);
            HEALTH_CHECK(result.HealthBefore == 10.0f);
            ++damaged;
        });
        health.OnDefeated.Add([&](const HealthComponent& source)
        {
            HEALTH_CHECK(&source == &health && source.IsDefeated());
            ++defeated;
        });

        const auto zero = health.ApplyDamage({0.0f});
        HEALTH_CHECK(zero.Outcome == EDamageOutcome::Applied && zero.AppliedAmount == 0.0f);
        HEALTH_CHECK(zero.HealthAfter == 10.0f && !health.IsDefeated());
        const auto lethal = health.ApplyDamage({25.0f});
        HEALTH_CHECK(lethal.Outcome == EDamageOutcome::Defeated && lethal.AppliedAmount == 10.0f);
        HEALTH_CHECK(lethal.HealthBefore == 10.0f && lethal.HealthAfter == 0.0f);
        HEALTH_CHECK(static_cast<float>(health.getHealth()) == 0.0f && health.IsDefeated());
        HEALTH_CHECK(damaged == 2 && defeated == 1);
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto ignored = health.ApplyDamage({5.0f, 10.0f});
            HEALTH_CHECK(ignored.Outcome == EDamageOutcome::IgnoredAlreadyDefeated);
            HEALTH_CHECK(ignored.AppliedAmount == 0.0f && ignored.HealthBefore == 0.0f &&
                         ignored.HealthAfter == 0.0f && !ignored.bStaggerBroken);
            HEALTH_CHECK(static_cast<float>(health.getHealth()) == 0.0f && health.GetStaggerMeter() == 0.0f);
        }
        HEALTH_CHECK(damaged == 2 && defeated == 1);
    }

    void InvalidDamagePreservesState()
    {
        Fixture fixture;
        auto& health = *fixture.Health;
        health.getStaggerThreshold() = 10.0f;
        health.ApplyDamage({5.0f, 3.0f});
        unsigned notifications = 0;
        health.OnDamaged.Add([&](const HealthComponent&, const DamageResult&) { ++notifications; });
        health.OnDefeated.Add([&](const HealthComponent&) { ++notifications; });
        health.OnStaggerBroken.Add([&](const HealthComponent&) { ++notifications; });
        const float invalid[] = {-1.0f, std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity()};
        for (float value : invalid)
        {
            const DamageInfo inputs[] = {{value, 9.0f}, {5.0f, value}};
            for (const auto& input : inputs)
            {
                const auto result = health.ApplyDamage(input);
                HEALTH_CHECK(result.Outcome == EDamageOutcome::RejectedInvalid);
                HEALTH_CHECK(result.AppliedAmount == 0.0f && result.HealthBefore == 95.0f &&
                             result.HealthAfter == 95.0f && !result.bStaggerBroken);
                HEALTH_CHECK(static_cast<float>(health.getHealth()) == 95.0f && health.GetStaggerMeter() == 3.0f);
                HEALTH_CHECK(!health.IsDefeated() && notifications == 0);
            }
        }
    }

    void Invulnerability()
    {
        Fixture fixture;
        auto& health = *fixture.Health;
        health.getStaggerThreshold() = 10.0f;
        unsigned damaged = 0;
        unsigned otherNotifications = 0;
        health.OnDamaged.Add([&](const HealthComponent&, const DamageResult&) { ++damaged; });
        health.OnDefeated.Add([&](const HealthComponent&) { ++otherNotifications; });
        health.OnStaggerBroken.Add([&](const HealthComponent&) { ++otherNotifications; });
        health.SetInvulnerable(true);
        HEALTH_CHECK(health.IsInvulnerable());
        const auto ignored = health.ApplyDamage({200.0f, 20.0f});
        HEALTH_CHECK(ignored.Outcome == EDamageOutcome::IgnoredInvulnerable);
        HEALTH_CHECK(ignored.AppliedAmount == 0.0f && ignored.HealthBefore == 100.0f &&
                     ignored.HealthAfter == 100.0f && !ignored.bStaggerBroken);
        HEALTH_CHECK(static_cast<float>(health.getHealth()) == 100.0f && health.GetStaggerMeter() == 0.0f);
        HEALTH_CHECK(!health.IsDefeated() && damaged == 0 && otherNotifications == 0);
        health.SetInvulnerable(false);
        HEALTH_CHECK(!health.IsInvulnerable());
        const auto applied = health.ApplyDamage({25.0f, 3.0f});
        HEALTH_CHECK(applied.Outcome == EDamageOutcome::Applied && applied.AppliedAmount == 25.0f);
        HEALTH_CHECK(static_cast<float>(health.getHealth()) == 75.0f && health.GetStaggerMeter() == 3.0f);
        HEALTH_CHECK(damaged == 1 && otherNotifications == 0);
    }

    void SequentialDamage()
    {
        Fixture fixture;
        auto& health = *fixture.Health;
        unsigned damaged = 0;
        unsigned defeated = 0;
        const float before[] = {100.0f, 70.0f, 40.0f};
        const float after[] = {70.0f, 40.0f, 0.0f};
        health.OnDamaged.Add([&](const HealthComponent&, const DamageResult& result)
        {
            HEALTH_CHECK(damaged < 3);
            HEALTH_CHECK(result.HealthBefore == before[damaged] && result.HealthAfter == after[damaged]);
            ++damaged;
        });
        health.OnDefeated.Add([&](const HealthComponent&) { ++defeated; });
        // フレームを進めずに連続で呼び、呼び出しごとの減算と通知を確かめる。
        HEALTH_CHECK(health.ApplyDamage({30.0f}).Outcome == EDamageOutcome::Applied);
        HEALTH_CHECK(health.ApplyDamage({30.0f}).Outcome == EDamageOutcome::Applied);
        HEALTH_CHECK(health.ApplyDamage({50.0f}).Outcome == EDamageOutcome::Defeated);
        HEALTH_CHECK(damaged == 3 && defeated == 1 && static_cast<float>(health.getHealth()) == 0.0f);
    }

    void StaggerAndNotificationOrder()
    {
        Fixture fixture;
        auto& health = *fixture.Health;
        health.getStaggerThreshold() = 10.0f;
        unsigned broken = 0;
        health.OnStaggerBroken.Add([&](const HealthComponent& source)
        {
            HEALTH_CHECK(source.GetStaggerMeter() == 0.0f);
            ++broken;
        });
        HEALTH_CHECK(!health.ApplyDamage({0.0f, 4.0f}).bStaggerBroken);
        HEALTH_CHECK(health.GetStaggerMeter() == 4.0f && broken == 0);
        HEALTH_CHECK(health.ApplyDamage({0.0f, 7.0f}).bStaggerBroken);
        HEALTH_CHECK(health.GetStaggerMeter() == 0.0f && broken == 1);
        HEALTH_CHECK(!health.ApplyDamage({0.0f, 3.0f}).bStaggerBroken);
        HEALTH_CHECK(health.GetStaggerMeter() == 3.0f && broken == 1);
        health.ResetStagger();
        HEALTH_CHECK(health.GetStaggerMeter() == 0.0f);
        HEALTH_CHECK(health.ApplyDamage({0.0f, 10.0f}).bStaggerBroken && broken == 2);

        health.getStaggerThreshold() = 0.0f;
        HEALTH_CHECK(!health.ApplyDamage({0.0f, 100.0f}).bStaggerBroken);
        HEALTH_CHECK(health.GetStaggerMeter() == 0.0f && broken == 2);

        health.getStaggerThreshold() = 10.0f;
        health.getHealth() = 10.0f;
        unsigned order = 0;
        health.OnDamaged.Add([&](const HealthComponent&, const DamageResult& result)
        {
            HEALTH_CHECK(order++ == 0 && result.Outcome == EDamageOutcome::Defeated && result.bStaggerBroken);
        });
        health.OnStaggerBroken.Add([&](const HealthComponent&) { HEALTH_CHECK(order++ == 1); });
        health.OnDefeated.Add([&](const HealthComponent&) { HEALTH_CHECK(order++ == 2); });
        const auto result = health.ApplyDamage({25.0f, 12.0f});
        HEALTH_CHECK(result.Outcome == EDamageOutcome::Defeated && result.bStaggerBroken);
        HEALTH_CHECK(order == 3 && broken == 3 && health.GetStaggerMeter() == 0.0f);
    }

    void StaggerRounding()
    {
        Fixture fixture;
        auto& health = *fixture.Health;
        health.getStaggerThreshold() = 1.0f;
        unsigned broken = 0;
        health.OnStaggerBroken.Add([&](const HealthComponent&) { ++broken; });
        const float belowThreshold = std::nextafter(1.0f, 0.0f);
        HEALTH_CHECK(!health.ApplyDamage({0.0f, belowThreshold}).bStaggerBroken);
        HEALTH_CHECK(health.GetStaggerMeter() == belowThreshold && broken == 0);
        // float に丸めると閾値に達する加算でも、通知して計を戻す。
        const auto result = health.ApplyDamage({0.0f, std::numeric_limits<float>::epsilon() * 0.375f});
        HEALTH_CHECK(result.bStaggerBroken && health.GetStaggerMeter() == 0.0f && broken == 1);
    }
} // namespace

int main()
{
    ClampAndLatch();
    InvalidDamagePreservesState();
    Invulnerability();
    SequentialDamage();
    StaggerAndNotificationOrder();
    StaggerRounding();
    std::puts("HealthComponentTest: PASS");
    return 0;
}
