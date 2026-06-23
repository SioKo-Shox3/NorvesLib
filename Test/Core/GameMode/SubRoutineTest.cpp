#include "Library/Core/Public/GameMode/GameModeStateMachine.h"
#include "Library/Core/Public/GameMode/GameModeId.h"
#include "Library/Core/Public/GameMode/GameModeParams.h"
#include "Library/Core/Public/GameMode/IGameMode.h"
#include "Library/Core/Public/GameMode/ISubRoutine.h"
#include "Library/Core/Public/GameMode/GameModeContext.h"
#include "Library/Core/Public/GameMode/GameModeTransition.h"
#include "Library/Core/Public/Container/Containers.h"
#include "Library/Core/Public/Engine/Engine.h"

#include <cassert>
#include <iostream>

// ---------------------------------------------------------------------------
// SubRoutine（GameMode 併走サブユニット）のライフサイクルテスト
//
// 検証項目:
//  - PushSubRoutine -> Enter（GameMode は Suspend されない＝並走）
//  - アクティブ段では Mode の Tick 後にサブルーチンが登録順で Tick される
//  - PopSubRoutine -> 末尾サブルーチンの Leave
//  - 複数サブルーチンの Tick 順（登録順）と Pop 順（末尾=LIFO）
//  - Mode 退場（Pop/Shutdown）時、サブルーチンが Mode の Leave より
//    前に逆順で Leave される
//  - Suspend 中の段（下位段）のサブルーチンは Tick されない
//
// 本番の深さ/中身アクセサは存在しないため、すべてイベントログで検証する。
// ---------------------------------------------------------------------------
namespace
{
    using namespace NorvesLib::Core::GameMode;
    namespace Container = NorvesLib::Core::Container;

    const GameModeId kModeA = NorvesLib::Core::Identity("SubRoutineTestModeA");
    const GameModeId kModeB = NorvesLib::Core::Identity("SubRoutineTestModeB");

    const char* ReasonName(GameModeExitReason reason)
    {
        switch (reason)
        {
        case GameModeExitReason::Change:   return "Change";
        case GameModeExitReason::Push:     return "Push";
        case GameModeExitReason::Pop:      return "Pop";
        case GameModeExitReason::Reset:    return "Reset";
        case GameModeExitReason::Shutdown: return "Shutdown";
        default:                           return "Unknown";
        }
    }

    struct CallLog
    {
        Container::VariableArray<Container::AnsiString> Events;

        void Append(const Container::AnsiString& event)
        {
            Events.push_back(event);
        }

        std::size_t Mark() const
        {
            return Events.size();
        }
    };

    // 記録用ダミー GameMode。
    struct DummyMode : public IGameMode
    {
        CallLog*    Log = nullptr;
        const char* Name = "?";

        DummyMode(CallLog* log, const char* name) : Log(log), Name(name) {}

        GameModeEnterResult Enter(GameModeContext& ctx) override
        {
            (void)ctx;
            Log->Append(Container::AnsiString("Enter ") + Container::AnsiString(Name));
            return GameModeEnterResult::Succeeded;
        }

        void Tick(GameModeContext& ctx, float deltaTime) override
        {
            (void)ctx;
            (void)deltaTime;
            Log->Append(Container::AnsiString("Tick ") + Container::AnsiString(Name));
        }

        void Leave(GameModeContext& ctx, GameModeExitReason reason) override
        {
            (void)ctx;
            Log->Append(Container::AnsiString("Leave ") + Container::AnsiString(Name)
                        + Container::AnsiString(":") + Container::AnsiString(ReasonName(reason)));
        }

        void Suspend(GameModeContext& ctx) override
        {
            (void)ctx;
            Log->Append(Container::AnsiString("Suspend ") + Container::AnsiString(Name));
        }

        void Resume(GameModeContext& ctx) override
        {
            (void)ctx;
            Log->Append(Container::AnsiString("Resume ") + Container::AnsiString(Name));
        }

        const char* GetDebugName() const override { return Name; }
    };

    // 記録用ダミー SubRoutine。Enter/Tick/Leave をログへ記録する。
    struct DummySub : public ISubRoutine
    {
        CallLog*    Log = nullptr;
        const char* Name = "?";

        DummySub(CallLog* log, const char* name) : Log(log), Name(name) {}

        void Enter(GameModeContext& ctx) override
        {
            (void)ctx;
            Log->Append(Container::AnsiString("SubEnter ") + Container::AnsiString(Name));
        }

        void Tick(GameModeContext& ctx, float deltaTime) override
        {
            (void)ctx;
            (void)deltaTime;
            Log->Append(Container::AnsiString("SubTick ") + Container::AnsiString(Name));
        }

        void Leave(GameModeContext& ctx) override
        {
            (void)ctx;
            Log->Append(Container::AnsiString("SubLeave ") + Container::AnsiString(Name));
        }

        const char* DebugName() const override { return Name; }
    };

    void RegisterAB(GameModeStateMachine& sm, CallLog* log)
    {
        sm.Registry().Register(kModeA, [log](const GameModeParams&)
        {
            return Container::MakeUnique<DummyMode>(log, "A");
        });
        sm.Registry().Register(kModeB, [log](const GameModeParams&)
        {
            return Container::MakeUnique<DummyMode>(log, "B");
        });
    }

    void AssertAppended(const CallLog& log, std::size_t mark,
                        const char* const* expected, std::size_t expectedCount,
                        const char* phase)
    {
        const std::size_t appended = log.Events.size() - mark;
        if (appended != expectedCount)
        {
            std::cout << "[FAIL] " << phase << ": appended count "
                      << appended << " != expected " << expectedCount << std::endl;
            for (std::size_t i = mark; i < log.Events.size(); ++i)
            {
                std::cout << "    got[" << (i - mark) << "] = "
                          << log.Events[i].c_str() << std::endl;
            }
        }
        assert(appended == expectedCount);
        for (std::size_t i = 0; i < expectedCount; ++i)
        {
            const Container::AnsiString& got = log.Events[mark + i];
            if (!(got == Container::AnsiString(expected[i])))
            {
                std::cout << "[FAIL] " << phase << ": appended[" << i << "] = '"
                          << got.c_str() << "' != expected '" << expected[i] << "'" << std::endl;
            }
            assert(got == Container::AnsiString(expected[i]));
        }
        std::cout << "[PASS] " << phase << std::endl;
    }

    bool AppendedContains(const CallLog& log, std::size_t mark, const char* needle)
    {
        for (std::size_t i = mark; i < log.Events.size(); ++i)
        {
            if (log.Events[i] == Container::AnsiString(needle))
            {
                return true;
            }
        }
        return false;
    }

    using NorvesLib::Core::Engine::Engine;
    using NorvesLib::Core::Engine::GEngine;

} // anonymous namespace

int main()
{
    std::cout << "[SubRoutineTest] START" << std::endl;

    GEngine = new Engine();

    // -----------------------------------------------------------------------
    // 1. Push sub S1; Mode は Suspend されず並走する。
    //    Update -> ["SubEnter S1","Tick A","SubTick S1"]（Mode Tick の後に Sub Tick）
    //    Mode の Suspend は呼ばれない。
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log);

        sm.Start(kModeA);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "S1"));
        sm.Update(0.016f);

        const char* expected[] = { "SubEnter S1", "Tick A", "SubTick S1" };
        AssertAppended(log, mark, expected, 3, "1. PushSubRoutine + parallel Tick (mode not suspended)");
        assert(!AppendedContains(log, mark, "Suspend A")); // 並走: Mode は Suspend しない
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 2. 2 本の sub の登録順 Tick と Pop（末尾=LIFO）
    //    Push S1, Push S2 (同一フレーム) -> SubEnter S1, SubEnter S2, Tick A, SubTick S1, SubTick S2
    //    Pop -> SubLeave S2（末尾）; 次 Update は S1 のみ Tick。
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log);

        sm.Start(kModeA);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "S1"));
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "S2"));
        sm.Update(0.016f);
        const char* expected[] = { "SubEnter S1", "SubEnter S2", "Tick A", "SubTick S1", "SubTick S2" };
        AssertAppended(log, mark, expected, 5, "2a. Two subs: registration-order Tick after mode");

        // Pop は末尾(S2)を Leave する。
        mark = log.Mark();
        sm.RequestPopSubRoutine();
        sm.Update(0.016f);
        const char* expected2[] = { "SubLeave S2", "Tick A", "SubTick S1" };
        AssertAppended(log, mark, expected2, 3, "2b. PopSubRoutine leaves last (S2); S1 keeps ticking");
        assert(!AppendedContains(log, mark, "SubTick S2"));
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 3. Mode Pop 時、サブルーチンが Mode Leave より前に逆順で Leave される。
    //    Stack: A. Push S1, S2. Pop mode A.
    //    -> ["SubLeave S2","SubLeave S1","Leave A:Pop"]（逆順 -> その後 Mode）
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log);

        sm.Start(kModeA);
        sm.Update(0.016f);
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "S1"));
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "S2"));
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestPop(); // 唯一のモードを Pop -> スタック空
        sm.Update(0.016f);
        const char* expected[] = { "SubLeave S2", "SubLeave S1", "Leave A:Pop" };
        AssertAppended(log, mark, expected, 3, "3. Mode Pop: subs leave reverse-order before mode Leave");
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 4. Suspend 中（下位段）のサブルーチンは Tick されない。
    //    Stack: A(+S1). Push mode B. Update -> B が Tick されるが S1 は Tick されない。
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log);

        sm.Start(kModeA);
        sm.Update(0.016f);
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "S1"));
        sm.Update(0.016f);

        // B を push: A は Suspend され、A の S1 はもうアクティブ段ではない。
        std::size_t mark = log.Mark();
        sm.RequestPush(kModeB);
        sm.Update(0.016f);
        const char* expected[] = { "Suspend A", "Enter B", "Tick B" };
        AssertAppended(log, mark, expected, 3, "4a. Push mode B suspends A; B's entry has no subs");
        assert(!AppendedContains(log, mark, "SubTick S1")); // Suspend 中の段の sub は Tick されない

        // B を Pop して A へ復帰すると、A の S1 が再び Tick される（寿命は A の段に紐づく）。
        mark = log.Mark();
        sm.RequestPop();
        sm.Update(0.016f);
        assert(AppendedContains(log, mark, "Resume A"));
        assert(AppendedContains(log, mark, "Tick A"));
        assert(AppendedContains(log, mark, "SubTick S1")); // 復帰後は再び Tick される
        std::cout << "[PASS] 4b. After popping B, A's subroutine resumes ticking" << std::endl;
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 5. Shutdown 時、生存中のサブルーチンが Mode Leave より前に逆順で Leave。
    //    Stack: A(+S1,+S2). Shutdown -> ["SubLeave S2","SubLeave S1","Leave A:Shutdown"]
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log);

        sm.Start(kModeA);
        sm.Update(0.016f);
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "S1"));
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "S2"));
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.Shutdown();
        const char* expected[] = { "SubLeave S2", "SubLeave S1", "Leave A:Shutdown" };
        AssertAppended(log, mark, expected, 3, "5. Shutdown: subs leave reverse-order before mode Leave");
    }

    // -----------------------------------------------------------------------
    // 6. 空スタックへの PushSubRoutine / PopSubRoutine は無害（何も起きない）。
    //    また sub の無い段への PopSubRoutine も無害。
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log);

        // 空スタックで Push/Pop sub。
        std::size_t mark = log.Mark();
        sm.RequestPushSubRoutine(Container::MakeUnique<DummySub>(&log, "X"));
        sm.RequestPopSubRoutine();
        sm.Update(0.016f);
        assert(log.Events.size() == mark); // 何も Enter/Leave/Tick されない
        std::cout << "[PASS] 6a. Sub push/pop on empty stack is a no-op" << std::endl;

        // モードはあるが sub の無い段への PopSubRoutine。
        sm.RequestChange(kModeA);
        sm.Update(0.016f);
        mark = log.Mark();
        sm.RequestPopSubRoutine();
        sm.Update(0.016f);
        const char* expected[] = { "Tick A" }; // sub は無いので Mode Tick のみ
        AssertAppended(log, mark, expected, 1, "6b. PopSubRoutine with no sub is a no-op");
        sm.Shutdown();
    }

    delete GEngine;
    GEngine = nullptr;

    std::cout << "[SubRoutineTest] ALL PASSED" << std::endl;
    return 0;
}
