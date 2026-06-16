#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Container/PointerTypes.h"
#include "Debug/DebugConfig.h"
#include "RHI/RHIDeviceDesc.h"

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

        // ========== RHI設定 ==========

        /**
         * @brief 使用するグラフィックスAPI
         * Default の場合はプラットフォームの既定APIが選択されます（現在 Windows では Vulkan）。
         */
        RHI::GraphicsAPI Api = RHI::GraphicsAPI::Default;

        /**
         * @brief RHI バリデーションレイヤーを有効にするか
         * デフォルトは Debug ビルドで true、Release ビルドで false（NORVES_BUILD_DEBUG 連動）。
         */
#if NORVES_BUILD_DEBUG
        bool bEnableRHIValidation = true;
#else
        bool bEnableRHIValidation = false;
#endif

        // ========== ロギング設定 ==========

        /**
         * @brief ログファイル名
         */
        Container::String LogFileName = TEXT("Game.log");

        /**
         * @brief ログを標準出力にも出すか（Windowsのみ意味を持つ）
         *
         * NorvesEditor 連携（--bridge-port）では Game の stdout は READY ハンドシェイク
         * 専用の継承パイプであり、ログが混じるとエディタの READY 検出を壊す。エディタ
         * モードではこれを false にして Logger を file 出力のみにする（WindowsEntryPoint
         * が設定）。通常起動では true。
         */
        bool bLogToStdout = true;

        // ========== コマンドライン引数 ==========

        /**
         * @brief コマンドライン引数リスト
         *
         * エンジン側エントリポイント（WindowsEntryPoint 等）が設定します。
         * ゲーム側の CreateApplicationBootConfig では設定不要です。
         */
        Container::VariableArray<Container::String> Arguments;

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
