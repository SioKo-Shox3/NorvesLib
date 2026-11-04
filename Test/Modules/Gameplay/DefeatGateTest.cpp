// 複数保留・所有者の消滅・保留なしの撃破で、安全点の完了通知が一回だけになることを反証する。
#include "Gameplay/Progression/DefeatGate.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Modules::Gameplay;
    namespace C = NorvesLib::Core;
    using String = C::Container::String;

#define DEFEAT_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "DefeatGate %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    void HoldsAndCompletion()
    {
        DefeatGate gate;
        const C::EntityHandle firstOwner{0, 1, 1};
        const C::EntityHandle secondOwner{1, 1, 1};
        bool bSecondAlive = true;
        const C::Delegate<bool, C::EntityHandle> isAlive([&](C::EntityHandle owner)
        {
            return owner == firstOwner || (owner == secondOwner && bSecondAlive);
        });
        unsigned completed = 0;
        gate.OnDefeatCompleted.Add([&](const String& id)
        {
            DEFEAT_CHECK(id == "shisha.forest");
            ++completed;
            gate.Update(isAlive);
        });

        DEFEAT_CHECK(!gate.AddHold("shisha.forest", firstOwner).IsValid());
        gate.BeginDefeat("shisha.forest");
        gate.BeginDefeat("shisha.forest");
        DEFEAT_CHECK(gate.GetIgnoredBeginCount() == 1);
        const auto first = gate.AddHold("shisha.forest", firstOwner);
        const auto second = gate.AddHold("shisha.forest", secondOwner);
        DEFEAT_CHECK(first.IsValid() && second.IsValid());
        gate.Update(isAlive);
        DEFEAT_CHECK(completed == 0);
        gate.Release(first);
        gate.Release(first);
        gate.Update(isAlive);
        DEFEAT_CHECK(completed == 0);
        gate.Release(second);
        DEFEAT_CHECK(completed == 0);
        gate.Update(isAlive);
        DEFEAT_CHECK(completed == 1);
        gate.Release(second);
        gate.Update(isAlive);
        gate.BeginDefeat("shisha.forest");
        DEFEAT_CHECK(gate.GetIgnoredBeginCount() == 2);
        DEFEAT_CHECK(!gate.AddHold("shisha.forest", firstOwner).IsValid());
        gate.Update(isAlive);
        DEFEAT_CHECK(completed == 1);
    }

    void EmptyAndDestroyedOwner()
    {
        DefeatGate gate;
        const C::EntityHandle owner{2, 1, 1};
        bool bAlive = true;
        const C::Delegate<bool, C::EntityHandle> isAlive([&](C::EntityHandle handle)
        {
            return handle == owner && bAlive;
        });
        unsigned emptyCompleted = 0;
        unsigned expiredCompleted = 0;
        gate.OnDefeatCompleted.Add([&](const String& id)
        {
            if (id == "shisha.empty")
            {
                ++emptyCompleted;
            }
            if (id == "shisha.expired")
            {
                ++expiredCompleted;
            }
        });
        gate.BeginDefeat("shisha.empty");
        gate.BeginDefeat("shisha.expired");
        const auto hold = gate.AddHold("shisha.expired", owner);
        DEFEAT_CHECK(hold.IsValid() && emptyCompleted == 0 && expiredCompleted == 0);
        gate.Update(isAlive);
        DEFEAT_CHECK(emptyCompleted == 1 && expiredCompleted == 0);
        bAlive = false;
        gate.Update(isAlive);
        DEFEAT_CHECK(emptyCompleted == 1 && expiredCompleted == 1);
        gate.Release(hold);
        gate.Update(isAlive);
        DEFEAT_CHECK(emptyCompleted == 1 && expiredCompleted == 1);
    }
} // namespace

int main()
{
    HoldsAndCompletion();
    EmptyAndDestroyedOwner();
    std::puts("DefeatGateTest: PASS");
    return 0;
}
