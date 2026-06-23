#pragma once

#include "GameModeId.h"
#include "GameModeParams.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::GameMode
{

    class ISubRoutine;

    /**
     * @brief ゲームモードコントローラーインターフェース
     *
     * ゲームモード内のロジックコードがステート遷移を要求するための純粋仮想インターフェース。
     * GameModeContext::ControllerRef を通じてゲームモード実装に渡される。
     *
     * すべてのRequest*メソッドはリクエストを積むだけで即時遷移しない。
     * 実際の遷移は ApplicationProcessor が次フレーム先頭で処理する。
     */
    class IGameModeController
    {
    public:
        // 仮想デストラクタ
        virtual ~IGameModeController() = default;

        /**
         * @brief 指定GameModeへ切り替え（スタックを置換）
         * @param id     遷移先のGameModeId
         * @param params 遷移先に渡すパラメータ（デフォルト空）
         */
        virtual void RequestChange(GameModeId id, GameModeParams params = {}) = 0;

        /**
         * @brief 指定GameModeをスタックに積む（現在のGameModeはサスペンド）
         * @param id     遷移先のGameModeId
         * @param params 遷移先に渡すパラメータ（デフォルト空）
         */
        virtual void RequestPush(GameModeId id, GameModeParams params = {}) = 0;

        /**
         * @brief スタックの先頭GameModeを取り除き前のGameModeへ戻る
         */
        virtual void RequestPop() = 0;

        /**
         * @brief スタックをリセットして指定GameModeで再スタート
         * @param id     再スタート先のGameModeId
         * @param params 遷移先に渡すパラメータ（デフォルト空）
         */
        virtual void RequestReset(GameModeId id, GameModeParams params = {}) = 0;

        /**
         * @brief アプリケーション終了を要求する
         * @param exitCode プロセス終了コード（デフォルト 0）
         */
        virtual void RequestExitApplication(int exitCode = 0) = 0;

        /**
         * @brief 現在のトップ段へサブルーチンを積む（GameMode は Suspend しない）
         *
         * 他の Request* と同じく遅延適用。次の Update 先頭のドレインで、現在の
         * トップ段の sub-stack 末尾へ追加され Enter が呼ばれる。アクティブ段で
         * ある限り Mode の Tick 後に毎フレーム Tick される。
         * @param sub 積むサブルーチン（所有権を移譲）
         */
        virtual void RequestPushSubRoutine(Container::TUniquePtr<ISubRoutine> sub) = 0;

        /**
         * @brief 現在のトップ段の最後のサブルーチンを取り除く
         *
         * 他の Request* と同じく遅延適用。次の Update 先頭のドレインで、現在の
         * トップ段の sub-stack 末尾のサブルーチンに Leave を呼び破棄する。
         * 空のときは無視する。
         */
        virtual void RequestPopSubRoutine() = 0;
    };

} // namespace NorvesLib::Core::GameMode
