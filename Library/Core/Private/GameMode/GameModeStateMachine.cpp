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
            *GEngine,
            GEngine->GetWorld(),
            GEngine->GetRenderResources(),
            GEngine->GetInputSystem(),
            *this,
            scope,
            dt};
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
        }
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
            GameModeTransitionRequest req = m_PendingQueue.front(); // erase の前にコピーアウト
            m_PendingQueue.erase(m_PendingQueue.begin());
            ApplyTransition(req);
        }
    }

    void GameModeStateMachine::ApplyTransition(const GameModeTransitionRequest& req)
    {
        switch (req.Type)
        {
        case GameModeTransitionType::Change:
        {
            if (!m_Stack.empty())
            {
                StackEntry& top = m_Stack.back();
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
        if (GEngine)
        {
            GEngine->RequestExit(exitCode);
        }
    }

} // namespace NorvesLib::Core::GameMode
