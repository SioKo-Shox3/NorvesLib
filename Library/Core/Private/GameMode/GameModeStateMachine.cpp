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

    void GameModeStateMachine::Start(GameModeId initialMode, GameModeParams params)
    {
        m_Pending.Type = GameModeTransitionType::Change;
        m_Pending.Target = initialMode;
        m_Pending.Params = std::move(params);
        m_Pending.ExitCode = 0;
        m_bHasPending = true;
    }

    void GameModeStateMachine::Update(float deltaTime)
    {
        if (m_bShutdown)
        {
            return;
        }

        m_DeltaTime = deltaTime;

        if (m_bHasPending)
        {
            ApplyPendingTransition();
        }

        if (m_CurrentMode)
        {
            GameModeContext ctx{
                *GEngine,
                GEngine->GetWorld(),
                GEngine->GetRenderResources(),
                GEngine->GetInputSystem(),
                *this,
                *m_Scope,
                deltaTime};
            m_CurrentMode->Tick(ctx, deltaTime);
        }
    }

    void GameModeStateMachine::ApplyPendingTransition()
    {
        // 1) 保留フラグをクリアし、要求をコピーアウトしてからリセット。
        m_bHasPending = false;
        GameModeTransitionRequest req = m_Pending;
        m_Pending = GameModeTransitionRequest{};

        // 2) Phase 3/4 では Change のみサポート。
        if (req.Type != GameModeTransitionType::Change)
        {
            NORVES_LOG_WARNING("GameMode", "Unsupported pending transition type ignored");
            return;
        }

        // 3) 既存モードがあれば Leave → Scope クリーンアップ → 解放。
        if (m_CurrentMode)
        {
            {
                GameModeContext ctx{
                    *GEngine,
                    GEngine->GetWorld(),
                    GEngine->GetRenderResources(),
                    GEngine->GetInputSystem(),
                    *this,
                    *m_Scope,
                    m_DeltaTime};
                m_CurrentMode->Leave(ctx, GameModeExitReason::Change);
            }
            if (m_Scope)
            {
                m_Scope->Cleanup();
            }
            m_CurrentMode.reset();
            m_Scope.reset();
        }

        // 4) 新しいモードを生成。未登録なら何もしない。
        Container::TUniquePtr<IGameMode> next = m_Registry.Create(req.Target, req.Params);
        if (!next)
        {
            NORVES_LOG_WARNING("GameMode", "GameModeRegistry has no creator for requested id");
            return;
        }

        // 5) 新しいモード用の Scope を確立する。
        m_Scope = Container::MakeUnique<GameModeScope>(
            &GEngine->GetWorld(),
            &GEngine->GetRenderResources());

        // 6) モードを設定し Enter を呼ぶ。Failed なら Scope を巻き戻す。
        m_CurrentMode = std::move(next);
        {
            GameModeContext ctx{
                *GEngine,
                GEngine->GetWorld(),
                GEngine->GetRenderResources(),
                GEngine->GetInputSystem(),
                *this,
                *m_Scope,
                m_DeltaTime};
            GameModeEnterResult r = m_CurrentMode->Enter(ctx);
            if (r == GameModeEnterResult::Failed)
            {
                if (m_Scope)
                {
                    m_Scope->Cleanup();
                }
                m_CurrentMode.reset();
                m_Scope.reset();
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

        if (m_CurrentMode)
        {
            {
                GameModeContext ctx{
                    *GEngine,
                    GEngine->GetWorld(),
                    GEngine->GetRenderResources(),
                    GEngine->GetInputSystem(),
                    *this,
                    *m_Scope,
                    m_DeltaTime};
                m_CurrentMode->Leave(ctx, GameModeExitReason::Shutdown);
            }
            if (m_Scope)
            {
                m_Scope->Cleanup();
            }
            m_CurrentMode.reset();
            m_Scope.reset();
        }

        m_bHasPending = false;
        m_Pending = GameModeTransitionRequest{};
    }

    void GameModeStateMachine::RequestChange(GameModeId id, GameModeParams params)
    {
        m_Pending.Type = GameModeTransitionType::Change;
        m_Pending.Target = id;
        m_Pending.Params = std::move(params);
        m_Pending.ExitCode = 0;
        m_bHasPending = true;
    }

    void GameModeStateMachine::RequestPush(GameModeId id, GameModeParams params)
    {
        (void)id;
        (void)params;
        NORVES_LOG_WARNING("GameMode", "RequestPush is not implemented yet (Phase 5); request ignored");
    }

    void GameModeStateMachine::RequestPop()
    {
        NORVES_LOG_WARNING("GameMode", "RequestPop is not implemented yet (Phase 5); request ignored");
    }

    void GameModeStateMachine::RequestReset(GameModeId id, GameModeParams params)
    {
        (void)id;
        (void)params;
        NORVES_LOG_WARNING("GameMode", "RequestReset is not implemented yet (Phase 5); request ignored");
    }

    void GameModeStateMachine::RequestExitApplication(int exitCode)
    {
        if (GEngine)
        {
            GEngine->RequestExit(exitCode);
        }
    }

} // namespace NorvesLib::Core::GameMode
