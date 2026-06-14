#include "Library/Core/Public/GameMode/GameModeStateMachine.h"
#include "Library/Core/Public/GameMode/GameModeId.h"
#include "Library/Core/Public/GameMode/GameModeParams.h"
#include "Library/Core/Public/GameMode/IGameMode.h"
#include "Library/Core/Public/GameMode/GameModeContext.h"
#include "Library/Core/Public/GameMode/GameModeTransition.h"
#include "Library/Core/Public/Container/Containers.h"
#include "Library/Core/Public/Engine/Engine.h"

#include <cassert>
#include <iostream>

// ---------------------------------------------------------------------------
// GameModeStateMachine のスタック/キュー挙動テスト
//
// 本番の深さアクセサは存在しないため、すべてイベントログ（CallLog）で検証する。
// テストシーム: 生の Engine は Initialize なしでも構築可能（検証済み）。Start/
// Update/Shutdown は GEngine->GetWorld()/GetRenderResources()/GetInputSystem()
// を参照するため、GEngine が指す実体が必須。各テストの前に new し、後に delete する。
// ---------------------------------------------------------------------------
namespace
{
    using namespace NorvesLib::Core::GameMode;
    namespace Container = NorvesLib::Core::Container;

    // 退場理由を文字列へ変換する。
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

    // 各モードのコールバック発火を時系列で記録する共有シンク。
    // ログ要素は AnsiString（= TString<char>）。Container::String は UNICODE 構成で
    // TCHAR=wchar_t になり得てナロー文字列リテラルや std::cout と相性が悪いため、
    // 構成非依存で安全な AnsiString を使う。
    struct CallLog
    {
        Container::VariableArray<Container::AnsiString> Events;

        void Append(const Container::AnsiString& event)
        {
            Events.push_back(event);
        }

        // 現在のサイズ（フェーズ境界のマーカとして使う）。
        std::size_t Mark() const
        {
            return Events.size();
        }
    };

    // 記録用のダミーゲームモード。
    struct DummyMode : public IGameMode
    {
        CallLog*            Log = nullptr;
        const char*         Name = "?";
        GameModeEnterResult EnterResult = GameModeEnterResult::Succeeded;

        DummyMode(CallLog* log, const char* name)
            : Log(log), Name(name)
        {
        }

        GameModeEnterResult Enter(GameModeContext& ctx) override
        {
            (void)ctx;
            Log->Append(Container::AnsiString("Enter ") + Container::AnsiString(Name));
            return EnterResult;
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

    // sm のレジストリに A(Rendering3DTest)/B(MemoryAgingTest) を登録する。
    // bFailB=true のとき B の Enter は Failed を返す。
    void RegisterAB(GameModeStateMachine& sm, CallLog* log, bool bFailB)
    {
        sm.Registry().Register(GameModeId::Rendering3DTest, [log](const GameModeParams&)
        {
            return Container::MakeUnique<DummyMode>(log, "A");
        });
        sm.Registry().Register(GameModeId::MemoryAgingTest, [log, bFailB](const GameModeParams&)
        {
            auto mode = Container::MakeUnique<DummyMode>(log, "B");
            if (bFailB)
            {
                mode->EnterResult = GameModeEnterResult::Failed;
            }
            return mode;
        });
    }

    // newly-appended なイベント（mark 以降）が expected と一致することを検証する。
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

    // appended なイベント群に needle が含まれないことを検証する。
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

// ---------------------------------------------------------------------------
// テスト本体
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "[GameModeStateMachineTest] START" << std::endl;

    // PRECONDITION: Start/Update/Shutdown の前に GEngine を実体へ向ける。
    GEngine = new Engine();

    // -----------------------------------------------------------------------
    // 1. Start(A)+Update -> ["Enter A","Tick A"]
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, false);

        std::size_t mark = log.Mark();
        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);

        const char* expected[] = { "Enter A", "Tick A" };
        AssertAppended(log, mark, expected, 2, "1. Start(A)+Update");
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 2. Push(B)+Update -> ["Suspend A","Enter B","Tick B"], no "Tick A"
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, false);

        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestPush(GameModeId::MemoryAgingTest);
        sm.Update(0.016f);

        const char* expected[] = { "Suspend A", "Enter B", "Tick B" };
        AssertAppended(log, mark, expected, 3, "2. Push(B)+Update");
        assert(!AppendedContains(log, mark, "Tick A")); // 一時停止中の A は Tick されない
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 3. Pop()+Update -> ["Leave B:Pop","Resume A","Tick A"]
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, false);

        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);
        sm.RequestPush(GameModeId::MemoryAgingTest);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestPop();
        sm.Update(0.016f);

        const char* expected[] = { "Leave B:Pop", "Resume A", "Tick A" };
        AssertAppended(log, mark, expected, 3, "3. Pop()+Update");
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 4. Change(B)+Update (from depth-1 A) -> ["Leave A:Change","Enter B","Tick B"]
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, false);

        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestChange(GameModeId::MemoryAgingTest);
        sm.Update(0.016f);

        const char* expected[] = { "Leave A:Change", "Enter B", "Tick B" };
        AssertAppended(log, mark, expected, 3, "4. Change(B)+Update");
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 5. depth-2 (A, Push B) then Reset(A) -> top-to-bottom Leaves then re-enter A
    //    ["Leave B:Reset","Leave A:Reset","Enter A","Tick A"]
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, false);

        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);
        sm.RequestPush(GameModeId::MemoryAgingTest);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestReset(GameModeId::Rendering3DTest);
        sm.Update(0.016f);

        const char* expected[] = { "Leave B:Reset", "Leave A:Reset", "Enter A", "Tick A" };
        AssertAppended(log, mark, expected, 4, "5. Reset(A) from depth-2");
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 6. Pop until empty, then Pop()+Update -> nothing; then Change(A) re-enters A
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, false);

        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);

        // 唯一のモードを Pop して空にする。
        sm.RequestPop();
        sm.Update(0.016f);

        // 空スタックでの Pop はクラッシュせず、Enter/Leave/Tick を一切積まない。
        std::size_t mark = log.Mark();
        sm.RequestPop();
        sm.Update(0.016f);
        assert(log.Events.size() == mark); // 空スタックは何も Tick しない
        std::cout << "[PASS] 6a. Pop on empty stack appends nothing" << std::endl;

        // マシンはまだ使える: Change(A) で再び A に入る。
        mark = log.Mark();
        sm.RequestChange(GameModeId::Rendering3DTest);
        sm.Update(0.016f);
        const char* expected[] = { "Enter A", "Tick A" };
        AssertAppended(log, mark, expected, 2, "6b. Change(A) after empty (machine reusable)");
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 7. Shutdown depth 2 -> ["Leave B:Shutdown","Leave A:Shutdown"]; 2nd Shutdown no-op
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, false);

        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);
        sm.RequestPush(GameModeId::MemoryAgingTest);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.Shutdown();
        const char* expected[] = { "Leave B:Shutdown", "Leave A:Shutdown" };
        AssertAppended(log, mark, expected, 2, "7. Shutdown depth 2 top-to-bottom");

        // 2 回目の Shutdown は冪等で何も積まない。
        std::size_t mark2 = log.Mark();
        sm.Shutdown();
        assert(log.Events.size() == mark2);
        std::cout << "[PASS] 7b. 2nd Shutdown is idempotent (appends nothing)" << std::endl;
    }

    // -----------------------------------------------------------------------
    // 8. Multi-enqueue same frame: Start(A)+Update; Push(B); Pop(); single Update
    //    -> ["Suspend A","Enter B","Leave B:Pop","Resume A","Tick A"]
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, false);

        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestPush(GameModeId::MemoryAgingTest);
        sm.RequestPop();
        sm.Update(0.016f); // 1 回の Update で両要求を FIFO ドレイン

        const char* expected[] = { "Suspend A", "Enter B", "Leave B:Pop", "Resume A", "Tick A" };
        AssertAppended(log, mark, expected, 5, "8. Multi-enqueue FIFO same-frame drain");
        sm.Shutdown();
    }

    // -----------------------------------------------------------------------
    // 9. Enter-Failed rollback: B Enter -> Failed; Push(B) rolls back to A
    //    appended == ["Suspend A","Enter B","Resume A"], no "Leave B", no "Tick B"
    // -----------------------------------------------------------------------
    {
        CallLog log;
        GameModeStateMachine sm;
        RegisterAB(sm, &log, true); // B の Enter は Failed

        sm.Start(GameModeId::Rendering3DTest);
        sm.Update(0.016f);

        std::size_t mark = log.Mark();
        sm.RequestPush(GameModeId::MemoryAgingTest);
        sm.Update(0.016f);

        // ロールバックは Suspend A -> Enter B(Failed) -> Resume A。Update は同一フレームで
        // ドレイン後にトップ（A に復帰）を Tick するため、末尾に "Tick A" が続く。
        // Leave B は呼ばれず（Enter 失敗の新モードに Leave なし）、B は Tick されない。
        const char* expected[] = { "Suspend A", "Enter B", "Resume A", "Tick A" };
        AssertAppended(log, mark, expected, 4, "9a. Enter-Failed rollback (A restored, B never ticked)");
        assert(!AppendedContains(log, mark, "Leave B:Push"));
        assert(!AppendedContains(log, mark, "Leave B:Pop"));
        assert(!AppendedContains(log, mark, "Tick B"));

        // 次の Update も B ではなく A を Tick する（A がトップに戻っている）。
        std::size_t mark2 = log.Mark();
        sm.Update(0.016f);
        const char* expected2[] = { "Tick A" };
        AssertAppended(log, mark2, expected2, 1, "9b. After failed Push, A keeps ticking");
        sm.Shutdown();
    }

    delete GEngine;
    GEngine = nullptr;

    std::cout << "[GameModeStateMachineTest] ALL PASSED" << std::endl;
    return 0;
}
