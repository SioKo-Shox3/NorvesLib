#pragma once

#include "IStateMachine.h"
#include "IGameModeController.h"
#include "GameModeRegistry.h"
#include "GameModeTransition.h"
#include "GameModeScope.h"
#include "IGameMode.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモードステートマシン
     *
     * IStateMachine（更新・シャットダウン）と IGameModeController（遷移要求）を
     * 1 つに実装する Phase 3/4 のモードドライバ。現在のゲームモードを 1 つだけ
     * 保持し、保留中の遷移要求を次の Update 先頭で適用する。
     *
     * 所有権モデル:
     * - 現在のゲームモードは m_CurrentMode（TUniquePtr）が所有する。
     * - GameModeScope は m_Scope が所有し、Cleanup でモード由来リソースを解放する。
     * - World / RenderResources 等のサブシステムは GEngine が所有（非所有参照のみ）。
     *
     * 寿命: Shutdown は冪等であり、デストラクタが安全網として再度呼ぶ。エンジンは
     * World/RHI を破棄する前に Shutdown を明示的に呼び、Leave がそれより先に走る
     * ことを保証する。
     */
    class GameModeStateMachine final : public IStateMachine, public IGameModeController
    {
    public:
        GameModeStateMachine() = default;
        ~GameModeStateMachine() override { Shutdown(); } // 冪等な安全網

        GameModeStateMachine(const GameModeStateMachine&) = delete;
        GameModeStateMachine& operator=(const GameModeStateMachine&) = delete;

        /**
         * @brief レジストリへの参照を返す（Creator 登録用）
         */
        GameModeRegistry& Registry() { return m_Registry; }

        /**
         * @brief 初期モードの遷移要求を積む
         *
         * 実際の遷移は次の Update 先頭で適用される。
         * @param initialMode 最初に入るゲームモード
         * @param params      生成パラメータ
         */
        void Start(GameModeId initialMode, GameModeParams params = {});

        // ========== IStateMachine ==========
        void Update(float deltaTime) override;
        void Shutdown() override;
        void* GetFactoryImpl() const override { return nullptr; }
        float GetDeltaTime() const override { return m_DeltaTime; }

        // ========== IGameModeController ==========
        void RequestChange(GameModeId id, GameModeParams params = {}) override;
        void RequestPush(GameModeId id, GameModeParams params = {}) override;
        void RequestPop() override;
        void RequestReset(GameModeId id, GameModeParams params = {}) override;
        void RequestExitApplication(int exitCode = 0) override;

    private:
        /// 保留中の遷移要求を適用する（Update 先頭から呼ばれる）。
        void ApplyPendingTransition();

        GameModeRegistry                     m_Registry;
        Container::TUniquePtr<IGameMode>     m_CurrentMode;
        Container::TUniquePtr<GameModeScope> m_Scope;
        GameModeTransitionRequest            m_Pending;
        bool                                 m_bHasPending = false;
        float                                m_DeltaTime = 0.0f;
        bool                                 m_bShutdown = false;
    };

} // namespace NorvesLib::Core::GameMode
