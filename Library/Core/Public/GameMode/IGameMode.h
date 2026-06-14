#pragma once

#include <cstdint>

#include "GameModeContext.h"
#include "GameModeTransition.h"   // GameModeExitReason

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモード Enter の結果
     *
     * IGameMode::Enter が遷移の成否を呼び出し側（ステートマシン）へ返すための
     * 結果コード。Failed の場合、ステートマシンは確立した Scope を巻き戻す。
     */
    enum class GameModeEnterResult : uint32_t
    {
        Succeeded, ///< 入場成功。以後 Tick が回る。
        Failed,    ///< 入場失敗。ステートマシンが Scope を片付ける。
    };

    /**
     * @brief ゲームモードインターフェース
     *
     * ステートマシンで管理される各ゲームモード（タイトル、ゲームプレイ等）の
     * 基底インターフェース。各メソッドには呼び出しサイトで構築された
     * GameModeContext が渡され、ゲームモードはそれ経由で World / RenderResources /
     * Input / コントローラ / スコープへアクセスする。
     */
    class IGameMode
    {
    public:
        // 仮想デストラクタ
        virtual ~IGameMode() = default;

        /**
         * @brief ステートに入ったときに呼ばれる
         * @param ctx 実行コンテキスト
         * @return 入場結果（Succeeded / Failed）
         */
        virtual GameModeEnterResult Enter(GameModeContext& ctx) = 0;

        /**
         * @brief ステート実行中に毎フレーム呼ばれる
         * @param ctx       実行コンテキスト
         * @param deltaTime フレーム間隔（秒）
         */
        virtual void Tick(GameModeContext& ctx, float deltaTime) = 0;

        /**
         * @brief ステートから出るときに呼ばれる
         * @param ctx    実行コンテキスト
         * @param reason 退場理由
         */
        virtual void Leave(GameModeContext& ctx, GameModeExitReason reason) = 0;

        /**
         * @brief スタックに積まれて一時停止するときに呼ばれる（任意実装）
         * @param ctx 実行コンテキスト
         */
        virtual void Suspend(GameModeContext& ctx) { (void)ctx; }

        /**
         * @brief スタックから復帰するときに呼ばれる（任意実装）
         * @param ctx 実行コンテキスト
         */
        virtual void Resume(GameModeContext& ctx) { (void)ctx; }

        /**
         * @brief デバッグ用の名前を返す
         * @return ゲームモードを識別する文字列リテラル
         */
        virtual const char* GetDebugName() const = 0;
    };

} // namespace NorvesLib::Core::GameMode
