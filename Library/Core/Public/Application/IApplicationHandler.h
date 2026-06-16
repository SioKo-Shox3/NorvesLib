#pragma once

#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "GameMode/IStateMachine.h"

namespace NorvesLib::Core::Application
{

    /**
     * @brief アプリケーションライフサイクルイベントハンドラ インターフェース
     *
     * ゲーム側でこのインターフェースを実装し、アプリケーションの
     * 各種イベント（起動、更新、終了など）を受け取ります。
     */
    class IApplicationHandler
    {
    public:
        virtual ~IApplicationHandler() = default;

        // ========== ライフサイクルイベント ==========

        /**
         * @brief アプリケーション起動前に呼ばれる（最初期設定）
         * @param args コマンドライン引数
         * @return 初期化を継続する場合true
         */
        virtual bool OnPreInitialize(const Container::VariableArray<Container::String> &args) = 0;

        /**
         * @brief アプリケーション起動時に呼ばれる
         * @return 初期化成功時true
         */
        virtual bool OnInitialize() = 0;

        /**
         * @brief アプリケーション起動完了後に呼ばれる
         */
        virtual void OnPostInitialize() = 0;

        /**
         * @brief 毎フレーム呼ばれる更新処理
         * @param deltaTime 前フレームからの経過時間（秒）
         */
        virtual void OnUpdate(float deltaTime) = 0;

        /**
         * @brief シミュレーション更新を進めてよいか
         *
         * false の間は当該フレームでシミュレーション更新（GameMode 更新・World Tick）を
         * 進めない（OnUpdate・描画は継続）。既定実装は ApplicationHandlerBase が true を
         * 返す。
         * @return シミュレーションを進める場合 true
         */
        virtual bool ShouldAdvanceSimulation() const = 0;

        /**
         * @brief 描画前に呼ばれる
         */
        virtual void OnPreRender() = 0;

        /**
         * @brief 描画後に呼ばれる
         */
        virtual void OnPostRender() = 0;

        /**
         * @brief アプリケーション終了前に呼ばれる
         */
        virtual void OnPreShutdown() = 0;

        /**
         * @brief アプリケーション終了時に呼ばれる
         */
        virtual void OnShutdown() = 0;

        // ========== フォーカス・サスペンドイベント ==========

        /**
         * @brief アプリケーションがフォーカスを得たとき
         */
        virtual void OnFocusGained() = 0;

        /**
         * @brief アプリケーションがフォーカスを失ったとき
         */
        virtual void OnFocusLost() = 0;

        /**
         * @brief アプリケーションがサスペンドされるとき（モバイル等）
         */
        virtual void OnSuspend() = 0;

        /**
         * @brief アプリケーションがレジュームされるとき
         */
        virtual void OnResume() = 0;

        // ========== GameModeへのアクセス ==========

        /**
         * @brief 初期GameModeステートマシンを作成して返す
         * @return GameModeステートマシンへのユニークポインタ
         */
        virtual Container::TUniquePtr<GameMode::IStateMachine> CreateGameModeStateMachine() = 0;
    };

} // namespace NorvesLib::Core::Application
