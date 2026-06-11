#pragma once

#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "Container/String.h"
#include "EngineGlobals/MemoryOverrides.h"

namespace NorvesLib
{

    class IWindow;

    /**
     * @brief アプリケーション抽象インターフェース
     *
     * OS毎のエントリーポイントを抽象化し、起動引数のサニタイズや
     * アプリケーションのライフサイクル管理を提供する
     */
    class IApplication
    {
    public:
        /**
         * @brief 仮想デストラクタ
         */
        virtual ~IApplication() = default;

        /**
         * @brief アプリケーションの初期化
         * @param args コマンドライン引数
         * @return 初期化の成否
         */
        virtual bool Initialize(const Core::Container::VariableArray<Core::Container::String> &args) = 0;

        /**
         * @brief アプリケーションの終了
         */
        virtual void Shutdown() = 0;

        /**
         * @brief メインウィンドウの取得
         * @return メインウィンドウへの参照
         */
        virtual IWindow *GetMainWindow() = 0;

        /**
         * @brief アプリケーションにウィンドウを登録
         * @param window 登録するウィンドウ
         */
        virtual void RegisterWindow(Core::Container::TSharedPtr<IWindow> window) = 0;

        /**
         * @brief ウィンドウの登録解除
         * @param window 解除するウィンドウ
         */
        virtual void UnregisterWindow(Core::Container::TSharedPtr<IWindow> window) = 0;

        /**
         * @brief 溜まったOSメッセージを全て処理する
         */
        virtual void PumpMessages() = 0;

        /**
         * @brief 終了要求（WM_QUITなど）が発生しているかを返す
         * @return 終了要求がある場合 true
         */
        virtual bool IsExitRequested() const = 0;

        /**
         * @brief 終了要求の終了コードを返す（IsExitRequested() が true のとき有効）
         * @return WM_QUIT の wParam 等で得られた終了コード
         */
        virtual int GetExitCode() const = 0;
    };

} // namespace NorvesLib
