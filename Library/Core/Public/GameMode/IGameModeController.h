#pragma once

#include "GameModeId.h"
#include "GameModeParams.h"

namespace NorvesLib::Core::GameMode
{

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
    };

} // namespace NorvesLib::Core::GameMode
