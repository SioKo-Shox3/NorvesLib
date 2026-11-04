// 遷移の安全点、連鎖の上限、無効な要求、停止時の寿命の契約を反証する。
#include "Library/Core/Public/GameMode/TStateMachine.h"

#include <cassert>
#include <cstddef>
#include <iostream>

namespace
{
    enum class TestState
    {
        A,
        B,
        Unregistered,
        OutOfRange = 4,
        Negative = -1
    };

    enum class Event
    {
        EnterA,
        UpdateAStart,
        UpdateAEnd,
        ExitA,
        EnterB,
        UpdateB,
        ExitB
    };

    struct TestContext;
    using Machine = NorvesLib::Core::GameMode::TStateMachine<TestContext, TestState, 2, 4>;

    struct TestContext
    {
        Event Events[128]{};
        std::size_t EventCount = 0;
        std::size_t EnterCount = 0;
        bool bRequestFromUpdate = false;
        bool bChainFromA = false;
        bool bChainFromB = false;
        bool bShutdownFromExitA = false;

        void Add(Event event)
        {
            assert(EventCount < 128);
            Events[EventCount++] = event;
        }

        void ClearEvents()
        {
            EventCount = 0;
        }
    };

    template <std::size_t Count>
    void ExpectEvents(const TestContext& context, const Event (&expected)[Count])
    {
        assert(context.EventCount == Count);
        for (std::size_t i = 0; i < Count; ++i)
        {
            assert(context.Events[i] == expected[i]);
        }
    }

    void RegisterStates(Machine& machine)
    {
        const bool bRegisteredA = machine.RegisterState(TestState::A, {
            [](Machine& stateMachine, TestContext& context)
            {
                context.Add(Event::EnterA);
                ++context.EnterCount;
                if (context.bChainFromA)
                {
                    stateMachine.RequestChange(TestState::B);
                }
            },
            [](Machine& stateMachine, TestContext& context, float)
            {
                context.Add(Event::UpdateAStart);
                if (context.bRequestFromUpdate)
                {
                    context.bRequestFromUpdate = false;
                    stateMachine.RequestChange(TestState::B);
                    assert(stateMachine.GetCurrentState() == TestState::A);
                }
                context.Add(Event::UpdateAEnd);
            },
            [](Machine& stateMachine, TestContext& context)
            {
                context.Add(Event::ExitA);
                if (context.bShutdownFromExitA)
                {
                    stateMachine.Shutdown(context);
                }
            }});
        const bool bRegisteredB = machine.RegisterState(TestState::B, {
            [](Machine& stateMachine, TestContext& context)
            {
                context.Add(Event::EnterB);
                ++context.EnterCount;
                if (context.bChainFromB)
                {
                    stateMachine.RequestChange(TestState::A);
                }
            },
            [](Machine&, TestContext& context, float)
            {
                context.Add(Event::UpdateB);
            },
            [](Machine&, TestContext& context)
            {
                context.Add(Event::ExitB);
            }});
        assert(bRegisteredA && bRegisteredB);
    }
}

int main()
{
    // 初期要求と通常の要求はいずれも次の安全点まで適用されない。
    {
        TestContext context;
        Machine machine;
        RegisterStates(machine);
        machine.Start(TestState::A);
        assert(!machine.HasCurrentState());
        assert(context.EventCount == 0);
        machine.Update(context, 0.016f);
        const Event initial[] = {Event::EnterA, Event::UpdateAStart, Event::UpdateAEnd};
        ExpectEvents(context, initial);

        context.ClearEvents();
        machine.RequestChange(TestState::B);
        assert(machine.GetCurrentState() == TestState::A);
        assert(context.EventCount == 0);
        machine.Update(context, 0.016f);
        const Event changed[] = {Event::ExitA, Event::EnterB, Event::UpdateB};
        ExpectEvents(context, changed);

        // 同じ状態を要求した場合も Exit と Enter をやり直す。
        context.ClearEvents();
        machine.RequestChange(TestState::B);
        machine.Update(context, 0.016f);
        const Event restarted[] = {Event::ExitB, Event::EnterB, Event::UpdateB};
        ExpectEvents(context, restarted);
        machine.Shutdown(context);
    }

    // 状態の Update 中に積んだ要求は、その Update を抜けてから次回適用する。
    {
        TestContext context;
        Machine machine;
        RegisterStates(machine);
        machine.Start(TestState::A);
        machine.Update(context, 0.016f);
        context.ClearEvents();
        context.bRequestFromUpdate = true;
        machine.Update(context, 0.016f);
        const Event requested[] = {Event::UpdateAStart, Event::UpdateAEnd};
        ExpectEvents(context, requested);
        assert(machine.GetCurrentState() == TestState::A);

        context.ClearEvents();
        machine.Update(context, 0.016f);
        const Event applied[] = {Event::ExitA, Event::EnterB, Event::UpdateB};
        ExpectEvents(context, applied);
        assert(machine.GetCurrentState() == TestState::B);
        machine.Shutdown(context);
    }

    // Enter 内の要求は同じ安全点で解決される。
    {
        TestContext context;
        Machine machine;
        RegisterStates(machine);
        context.bChainFromA = true;
        machine.Start(TestState::A);
        machine.Update(context, 0.016f);
        const Event chained[] = {Event::EnterA, Event::ExitA, Event::EnterB, Event::UpdateB};
        ExpectEvents(context, chained);
        assert(machine.GetCurrentState() == TestState::B);
        machine.Shutdown(context);
    }

    // 循環する要求は上限で中断し、保留した要求を次回へ持ち越す。
    {
        TestContext context;
        Machine machine;
        RegisterStates(machine);
        context.bChainFromA = true;
        context.bChainFromB = true;
        machine.Start(TestState::A);
        machine.Update(context, 0.016f);
        assert(context.EnterCount > 1);
        assert(context.EnterCount <= 8);
        assert(machine.GetDrainLimitReachedCount() == 1);
        assert(machine.GetRejectedRequestCount() == 0);

        const std::size_t before = context.EnterCount;
        context.bChainFromA = false;
        context.bChainFromB = false;
        context.ClearEvents();
        machine.Update(context, 0.016f);
        assert(context.EnterCount == before + 1);
        assert(machine.GetDrainLimitReachedCount() == 1);
        assert(context.EventCount >= 3);
        machine.Shutdown(context);
    }

    // 未登録・範囲外・満杯の要求は拒否し、受理した要求は FIFO で適用する。
    {
        TestContext context;
        Machine machine;
        RegisterStates(machine);
        machine.Start(TestState::A);
        machine.Update(context, 0.016f);
        context.ClearEvents();
        machine.RequestChange(TestState::Unregistered);
        machine.RequestChange(TestState::OutOfRange);
        machine.RequestChange(TestState::Negative);
        assert(machine.GetRejectedRequestCount() == 3);
        assert(machine.GetCurrentState() == TestState::A);
        assert(context.EventCount == 0);

        machine.RequestChange(TestState::B);
        machine.RequestChange(TestState::A);
        machine.RequestChange(TestState::B);
        assert(machine.GetRejectedRequestCount() == 4);
        assert(machine.GetCurrentState() == TestState::A);
        machine.Update(context, 0.016f);
        const Event queued[] = {Event::ExitA, Event::EnterB, Event::ExitB,
                                Event::EnterA, Event::UpdateAStart, Event::UpdateAEnd};
        ExpectEvents(context, queued);
        assert(machine.GetCurrentState() == TestState::A);
        machine.Shutdown(context);
    }

    // Shutdown は Exit を一度だけ呼び、保留中と停止後の要求を実行しない。
    {
        TestContext context;
        Machine machine;
        RegisterStates(machine);
        machine.Start(TestState::A);
        machine.Update(context, 0.016f);
        machine.RequestChange(TestState::B);
        context.ClearEvents();
        machine.Shutdown(context);
        machine.Shutdown(context);
        const Event shutdown[] = {Event::ExitA};
        ExpectEvents(context, shutdown);
        assert(!machine.HasCurrentState());
        machine.RequestChange(TestState::B);
        machine.Start(TestState::A);
        assert(machine.GetRejectedRequestCount() == 2);
        machine.Update(context, 0.016f);
        ExpectEvents(context, shutdown);
    }

    // Exit 内から停止した場合も同じ Exit を再入させず、次の状態へ移らない。
    {
        TestContext context;
        Machine machine;
        RegisterStates(machine);
        machine.Start(TestState::A);
        machine.Update(context, 0.016f);
        context.ClearEvents();
        context.bShutdownFromExitA = true;
        machine.RequestChange(TestState::B);
        machine.Update(context, 0.016f);
        const Event shutdown[] = {Event::ExitA};
        ExpectEvents(context, shutdown);
        assert(!machine.HasCurrentState());
        machine.Shutdown(context);
        ExpectEvents(context, shutdown);
    }

    std::cout << "[TStateMachineTest] 全項目が成功しました" << std::endl;
    return 0;
}
