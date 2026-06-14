#pragma once

#include "IStateMachine.h"
#include "IGameModeController.h"
#include "GameModeRegistry.h"
#include "GameModeTransition.h"
#include "GameModeScope.h"
#include "IGameMode.h"
#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモードステートマシン
     *
     * IStateMachine（更新・シャットダウン）と IGameModeController（遷移要求）を
     * 1 つに実装するモードドライバ。ゲームモードを「スタック」で保持し、
     * 遷移要求を FIFO の「キュー」に積んで次の Update 先頭でまとめて適用する。
     *
     * スタックモデル:
     * - スタック最上段（back()）がアクティブモードで、毎フレーム Tick される。
     * - Change はトップを Leave→pop して新モードを push する（深さ不変）。
     * - Push は現トップを Suspend してスタックへ残し、新モードを積む（深さ+1）。
     * - Pop はトップを Leave→pop し、下のモードがあれば Resume する（深さ-1）。
     * - ResetStack は全モードを上から Leave→pop し、新モードを 1 段積む。
     * - Quit はスタックに触れずアプリ終了を要求する。
     *
     * 所有権モデル:
     * - 各 StackEntry が IGameMode と GameModeScope を TUniquePtr で所有する。
     *   StackEntry 内では Mode を Scope より先に宣言し、破棄時にモードが先に
     *   壊れる（モードがスコープ資源を参照していても安全）。
     * - World / RenderResources 等のサブシステムは GEngine が所有（非所有参照のみ）。
     *
     * 再入不変条件: 各 Request* は m_PendingQueue へ push_back するのみで、
     * m_Stack を同期的に変更しない。よってコールバック（Enter/Leave/Suspend/
     * Resume）内で m_Stack がリサイズされることはなく、キューに積まれた要求は
     * 同一フレーム内のドレインで連鎖的に解決される。
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
        /// スタック 1 段分のエントリ（モード本体と、そのモードのスコープ）。
        struct StackEntry
        {
            Container::TUniquePtr<IGameMode>     Mode;   // Scope より先に宣言: 破棄時にモードが先に壊れる
            Container::TUniquePtr<GameModeScope> Scope;
        };

        /// 保留キューを FIFO で空になるまで適用する（Update 先頭から呼ばれる）。
        void DrainPendingQueue();
        /// 1 件の遷移要求をスタックへ適用する（種別ごとに分岐）。
        void ApplyTransition(const GameModeTransitionRequest& req);
        /// 呼び出しサイト用の GameModeContext を構築する（値返し・コピー省略）。
        GameModeContext MakeContext(GameModeScope& scope, float dt);

        GameModeRegistry                                    m_Registry;
        Container::VariableArray<StackEntry>                m_Stack;        // top = back()
        Container::VariableArray<GameModeTransitionRequest> m_PendingQueue; // FIFO; front = [0]
        float                                               m_DeltaTime = 0.0f;
        bool                                                m_bShutdown = false;
    };

} // namespace NorvesLib::Core::GameMode
