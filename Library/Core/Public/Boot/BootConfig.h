#pragma once

#include "Container/String.h"
#include "Container/PointerTypes.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace NorvesLib::Core::Application
{
    class IApplicationHandler;
}

namespace NorvesLib::Core::Boot
{

    /**
     * @brief アプリケーション起動設定構造体
     *
     * ゲーム側でこの構造体を設定し、ライブラリに渡すことで
     * アプリケーションの起動動作をカスタマイズします。
     */
    struct BootConfig
    {
        // ========== ウィンドウ設定 ==========

        /**
         * @brief ウィンドウタイトル
         */
        Container::String WindowTitle = TEXT("NorvesLib Application");

        /**
         * @brief ウィンドウ幅（ピクセル）
         */
        int WindowWidth = 1280;

        /**
         * @brief ウィンドウ高さ（ピクセル）
         */
        int WindowHeight = 720;

        /**
         * @brief フルスクリーンモード
         */
        bool bFullscreen = false;

        /**
         * @brief ウィンドウリサイズ可能
         */
        bool bResizable = true;

        // ========== エンジン設定 ==========

        /**
         * @brief 垂直同期（VSync）有効
         */
        bool bVSync = true;

        /**
         * @brief ターゲットフレームレート（FPS）
         * 0以下の場合は無制限
         */
        float TargetFrameRate = 60.0f;

        /**
         * @brief デバッグコンソール有効（Windowsのみ）
         */
        bool bEnableDebugConsole = true;

        // ========== ロギング設定 ==========

        /**
         * @brief ログファイル名
         */
        Container::String LogFileName = TEXT("Game.log");

        // ========== プラットフォーム固有パラメータ ==========

#ifdef _WIN32
        /**
         * @brief アプリケーションインスタンスハンドル（Windows用）
         */
        HINSTANCE hInstance = nullptr;

        /**
         * @brief 前のインスタンスハンドル（Windows用、常にnullptr）
         */
        HINSTANCE hPrevInstance = nullptr;

        /**
         * @brief コマンドライン引数（Windows用）
         */
        LPSTR lpCmdLine = nullptr;

        /**
         * @brief ウィンドウ表示状態（Windows用）
         */
        int nCmdShow = SW_SHOW;
#endif

        // ========== ハンドラ作成 ==========

        /**
         * @brief ApplicationHandlerを作成する関数ポインタ型
         */
        using HandlerCreator = Container::TSharedPtr<Application::IApplicationHandler> (*)();

        /**
         * @brief ApplicationHandler作成関数
         * ゲーム側で必ず設定すること
         */
        HandlerCreator CreateHandler = nullptr;
    };

    /**
     * @brief デフォルトのBootConfigを作成
     * @return デフォルト設定のBootConfig
     */
    inline BootConfig CreateDefaultBootConfig()
    {
        return BootConfig{};
    }

} // namespace NorvesLib::Core::Boot
