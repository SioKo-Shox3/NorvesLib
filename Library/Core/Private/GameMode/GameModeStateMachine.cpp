#include "GameMode/GameModeStateMachine.h"
#include "Engine/Engine.h"
#include "Object/World.h"
#include "Rendering/RenderResources.h"
#include "Input/InputSystem.h"
#include "Logging/LogMacros.h"

#include <utility>

namespace NorvesLib::Core::GameMode
{
    using NorvesLib::Core::Engine::GEngine;

    GameModeContext GameModeStateMachine::MakeContext(GameModeScope& scope, float dt)
    {
        // メンバ順は GameModeContext.h と厳密に一致:
        // EngineRef, WorldRef, RenderResourcesRef, InputRef, ControllerRef, ScopeRef, DeltaTime。
        // 参照メンバを持つため値返しだが、C++17 の保証されたコピー省略で安全。
        return GameModeContext{
            .EngineRef          = *GEngine,
            .WorldRef           = GEngine->GetWorld(),
            .RenderResourcesRef = GEngine->GetRenderResources(),
            .InputRef           = GEngine->GetInputSystem(),
            .ControllerRef      = *this,
            .ScopeRef           = scope,
            .DeltaTime          = dt};
    }

    void GameModeStateMachine::Start(GameModeId initialMode, GameModeParams params)
    {
        GameModeTransitionRequest req;
        req.Type = GameModeTransitionType::Change;
        req.Target = initialMode;
        req.Params = std::move(params);
        req.ExitCode = 0;
        m_PendingQueue.push_back(std::move(req));
    }

    void GameModeStateMachine::Update(float deltaTime)
    {
        if (m_bShutdown)
        {
            return;
        }

        m_DeltaTime = deltaTime; // ドレイン前に設定し、Enter/Leave が現在の delta を見る（従来挙動）

        DrainPendingQueue();

        if (!m_Stack.empty())
        {
            StackEntry& top = m_Stack.back();
            GameModeContext ctx = MakeContext(*top.Scope, deltaTime);
            top.Mode->Tick(ctx, deltaTime);

            // トップ段（=アクティブ段）のサブルーチンを登録順で Tick する。
            // Suspend 中（トップでない）段のサブルーチンは Tick しない（並走は
            // アクティブ段のみ）。サブルーチンの Tick 中に push/pop が要求されても、
            // それらは遅延キューへ積まれ次フレームのドレインで適用されるため、
            // この走査中に SubRoutines 配列がリサイズされることはない（再入安全）。
            for (Container::TUniquePtr<ISubRoutine>& sub : top.SubRoutines)
            {
                if (sub)
                {
                    sub->Tick(ctx, deltaTime);
                }
            }
        }
    }

    void GameModeStateMachine::LeaveEntrySubRoutines(StackEntry& entry)
    {
        // 段が破棄される直前に、サブルーチンを積んだ逆順で Leave する。
        // Mode はまだ生存しているため、サブルーチンは Mode のリソースを参照した
        // まま安全に後始末できる。Leave 後に配列を空にして破棄を確定する。
        if (entry.SubRoutines.empty())
        {
            return;
        }
        GameModeContext ctx = MakeContext(*entry.Scope, m_DeltaTime);
        for (std::size_t i = entry.SubRoutines.size(); i > 0; --i)
        {
            Container::TUniquePtr<ISubRoutine>& sub = entry.SubRoutines[i - 1];
            if (sub)
            {
                sub->Leave(ctx);
            }
        }
        entry.SubRoutines.clear();
    }

    void GameModeStateMachine::DrainPendingQueue()
    {
        // FIFO ドレイン。コールバック（Enter/Leave/Suspend/Resume）は
        // ctx.ControllerRef.Request* 経由で更なる要求を積めるが、それらは
        // m_PendingQueue へ push_back するだけで m_Stack を同期的に変更しない。
        // よってこのループは連鎖要求を同一フレームで解決し、コールバック内で
        // StackEntry 参照が無効化されることはない。
        while (!m_PendingQueue.empty())
        {
            // pop_front の前に move アウトする（リクエストは TUniquePtr<ISubRoutine>
            // を含みムーブ専用のため、コピーではなく move で取り出す）。
            GameModeTransitionRequest req = std::move(m_PendingQueue.front());
            m_PendingQueue.pop_front();
            ApplyTransition(req);
        }
    }

    void GameModeStateMachine::ApplyTransition(GameModeTransitionRequest& req)
    {
        switch (req.Type)
        {
        case GameModeTransitionType::Change:
        {
            if (!m_Stack.empty())
            {
                StackEntry& top = m_Stack.back();
                LeaveEntrySubRoutines(top); // Mode の Leave より前にサブルーチンを後始末
                GameModeContext ctx = MakeContext(*top.Scope, m_DeltaTime);
                top.Mode->Leave(ctx, GameModeExitReason::Change);
                top.Scope->Cleanup();
                m_Stack.pop_back();
            }

            Container::TUniquePtr<IGameMode> next = m_Registry.Create(req.Target, req.Params);
            if (!next)
            {
                NORVES_LOG_WARNING("GameMode", "GameModeRegistry has no creator for requested id (Change)");
                return;
            }

            auto scope = Container::MakeUnique<GameModeScope>(
                &GEngine->GetWorld(),
                &GEngine->GetRenderResources());
            m_Stack.push_back(StackEntry{ std::move(next), std::move(scope) });

            StackEntry& entry = m_Stack.back();
            GameModeContext enterCtx = MakeContext(*entry.Scope, m_DeltaTime);
            if (entry.Mode->Enter(enterCtx) == GameModeEnterResult::Failed)
            {
                entry.Scope->Cleanup();
                m_Stack.pop_back();
            }
            return;
        }

        case GameModeTransitionType::Push:
        {
            const bool bSuspended = !m_Stack.empty();
            if (bSuspended)
            {
                StackEntry& top = m_Stack.back();
                GameModeContext ctx = MakeContext(*top.Scope, m_DeltaTime);
                top.Mode->Suspend(ctx); // Leave ではない・Cleanup なし・スタックに残る
            }

            Container::TUniquePtr<IGameMode> next = m_Registry.Create(req.Target, req.Params);
            if (!next)
            {
                NORVES_LOG_WARNING("GameMode", "GameModeRegistry has no creator for requested id (Push)");
                if (bSuspended)
                {
                    StackEntry& prev = m_Stack.back();
                    GameModeContext ctx = MakeContext(*prev.Scope, m_DeltaTime);
                    prev.Mode->Resume(ctx);
                }
                return;
            }

            auto scope = Container::MakeUnique<GameModeScope>(
                &GEngine->GetWorld(),
                &GEngine->GetRenderResources());
            m_Stack.push_back(StackEntry{ std::move(next), std::move(scope) });

            StackEntry& entry = m_Stack.back();
            GameModeContext enterCtx = MakeContext(*entry.Scope, m_DeltaTime);
            if (entry.Mode->Enter(enterCtx) == GameModeEnterResult::Failed)
            {
                // Enter 失敗の新モードには Leave を呼ばない（Cleanup + pop のみ。既存挙動と一致）。
                entry.Scope->Cleanup();
                m_Stack.pop_back();
                if (bSuspended)
                {
                    StackEntry& prev = m_Stack.back();
                    GameModeContext resumeCtx = MakeContext(*prev.Scope, m_DeltaTime);
                    prev.Mode->Resume(resumeCtx);
                }
            }
            return;
        }

        case GameModeTransitionType::Pop:
        {
            if (m_Stack.empty())
            {
                NORVES_LOG_WARNING("GameMode", "RequestPop on empty stack; ignored");
                return;
            }

            StackEntry& top = m_Stack.back();
            LeaveEntrySubRoutines(top); // Mode の Leave より前にサブルーチンを後始末
            GameModeContext leaveCtx = MakeContext(*top.Scope, m_DeltaTime);
            top.Mode->Leave(leaveCtx, GameModeExitReason::Pop);
            top.Scope->Cleanup();
            m_Stack.pop_back();

            if (!m_Stack.empty())
            {
                StackEntry& nt = m_Stack.back();
                GameModeContext resumeCtx = MakeContext(*nt.Scope, m_DeltaTime);
                nt.Mode->Resume(resumeCtx);
            }
            // 空スタックは許容する。アプリは自動終了しない。
            return;
        }

        case GameModeTransitionType::ResetStack:
        {
            while (!m_Stack.empty())
            {
                StackEntry& e = m_Stack.back();
                LeaveEntrySubRoutines(e); // Mode の Leave より前にサブルーチンを後始末
                GameModeContext ctx = MakeContext(*e.Scope, m_DeltaTime);
                e.Mode->Leave(ctx, GameModeExitReason::Reset);
                e.Scope->Cleanup();
                m_Stack.pop_back();
            }

            Container::TUniquePtr<IGameMode> next = m_Registry.Create(req.Target, req.Params);
            if (!next)
            {
                NORVES_LOG_WARNING("GameMode", "GameModeRegistry has no creator for requested id (Reset)");
                return;
            }

            auto scope = Container::MakeUnique<GameModeScope>(
                &GEngine->GetWorld(),
                &GEngine->GetRenderResources());
            m_Stack.push_back(StackEntry{ std::move(next), std::move(scope) });

            StackEntry& entry = m_Stack.back();
            GameModeContext enterCtx = MakeContext(*entry.Scope, m_DeltaTime);
            if (entry.Mode->Enter(enterCtx) == GameModeEnterResult::Failed)
            {
                entry.Scope->Cleanup();
                m_Stack.pop_back();
            }
            return;
        }

        case GameModeTransitionType::Quit:
        {
            if (GEngine)
            {
                GEngine->RequestExit(req.ExitCode); // スタックには触れない
            }
            return;
        }

        case GameModeTransitionType::PushSubRoutine:
        {
            // GameMode は Suspend しない・並走させる。現在のトップ段の sub-stack
            // 末尾へ追加し Enter を呼ぶ。空スタックには積めない（積み先がない）。
            if (m_Stack.empty())
            {
                NORVES_LOG_WARNING("GameMode", "RequestPushSubRoutine on empty stack; ignored");
                return;
            }
            if (!req.SubRoutine)
            {
                NORVES_LOG_WARNING("GameMode", "RequestPushSubRoutine with null sub; ignored");
                return;
            }

            StackEntry& top = m_Stack.back();
            top.SubRoutines.push_back(std::move(req.SubRoutine));

            // push_back 後に末尾参照を取得して Enter する。Enter 内で push/pop が
            // 要求されても遅延キューへ積まれるだけで SubRoutines は同期変更されない。
            Container::TUniquePtr<ISubRoutine>& added = top.SubRoutines.back();
            GameModeContext ctx = MakeContext(*top.Scope, m_DeltaTime);
            added->Enter(ctx);
            return;
        }

        case GameModeTransitionType::PopSubRoutine:
        {
            if (m_Stack.empty())
            {
                NORVES_LOG_WARNING("GameMode", "RequestPopSubRoutine on empty stack; ignored");
                return;
            }

            StackEntry& top = m_Stack.back();
            if (top.SubRoutines.empty())
            {
                NORVES_LOG_WARNING("GameMode", "RequestPopSubRoutine with no sub on top entry; ignored");
                return;
            }

            // 末尾（最後に積んだもの）を Leave してから配列から除去する。
            // Leave 用に末尾を move アウトし、配列から先に外してから Leave する
            // ことで、Leave 中の再入要求が解決される次ドレインまで配列形状が安定。
            Container::TUniquePtr<ISubRoutine> sub = std::move(top.SubRoutines.back());
            top.SubRoutines.pop_back();
            if (sub)
            {
                GameModeContext ctx = MakeContext(*top.Scope, m_DeltaTime);
                sub->Leave(ctx);
            }
            // sub はここで破棄される（Leave 済み）。
            return;
        }

        case GameModeTransitionType::None:
        default:
        {
            NORVES_LOG_WARNING("GameMode", "Unknown/None transition type ignored");
            return;
        }
        }
    }

    void GameModeStateMachine::Shutdown()
    {
        if (m_bShutdown)
        {
            return;
        }
        m_bShutdown = true;

        while (!m_Stack.empty())
        {
            StackEntry& e = m_Stack.back();
            LeaveEntrySubRoutines(e); // Mode の Leave より前にサブルーチンを後始末
            GameModeContext ctx = MakeContext(*e.Scope, m_DeltaTime);
            e.Mode->Leave(ctx, GameModeExitReason::Shutdown);
            e.Scope->Cleanup();
            m_Stack.pop_back();
        }

        m_PendingQueue.clear();
    }

    // 再入不変条件: 各 Request* は m_PendingQueue へ push_back するのみで、m_Stack を
    // 変更しない。よってコールバック内で m_Stack がリサイズされることはなく、push_back
    // 後に取得した StackEntry 参照は Enter/Suspend/Resume をまたいで有効なまま保たれる。
    void GameModeStateMachine::RequestChange(GameModeId id, GameModeParams params)
    {
        GameModeTransitionRequest r;
        r.Type = GameModeTransitionType::Change;
        r.Target = id;
        r.Params = std::move(params);
        r.ExitCode = 0;
        m_PendingQueue.push_back(std::move(r));
    }

    void GameModeStateMachine::RequestPush(GameModeId id, GameModeParams params)
    {
        GameModeTransitionRequest r;
        r.Type = GameModeTransitionType::Push;
        r.Target = id;
        r.Params = std::move(params);
        r.ExitCode = 0;
        m_PendingQueue.push_back(std::move(r));
    }

    void GameModeStateMachine::RequestPop()
    {
        GameModeTransitionRequest r;
        r.Type = GameModeTransitionType::Pop;
        m_PendingQueue.push_back(std::move(r));
    }

    void GameModeStateMachine::RequestReset(GameModeId id, GameModeParams params)
    {
        GameModeTransitionRequest r;
        r.Type = GameModeTransitionType::ResetStack;
        r.Target = id;
        r.Params = std::move(params);
        r.ExitCode = 0;
        m_PendingQueue.push_back(std::move(r));
    }

    void GameModeStateMachine::RequestExitApplication(int exitCode)
    {
        GameModeTransitionRequest r;
        r.Type     = GameModeTransitionType::Quit;
        r.ExitCode = exitCode;
        m_PendingQueue.push_back(std::move(r));
    }

    void GameModeStateMachine::RequestPushSubRoutine(Container::TUniquePtr<ISubRoutine> sub)
    {
        // 他の Request* と同じく遅延適用。サブルーチンの所有権をリクエストへ
        // 移し、次の Update 先頭のドレインで現在のトップ段へ積む。
        GameModeTransitionRequest r;
        r.Type       = GameModeTransitionType::PushSubRoutine;
        r.SubRoutine = std::move(sub);
        m_PendingQueue.push_back(std::move(r));
    }

    void GameModeStateMachine::RequestPopSubRoutine()
    {
        GameModeTransitionRequest r;
        r.Type = GameModeTransitionType::PopSubRoutine;
        m_PendingQueue.push_back(std::move(r));
    }

} // namespace NorvesLib::Core::GameMode
