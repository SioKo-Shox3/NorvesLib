#pragma once

#include "Container/String.h"
#include "Container/PointerTypes.h"

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
