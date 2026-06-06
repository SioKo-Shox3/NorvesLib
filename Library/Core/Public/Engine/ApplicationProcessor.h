#pragma once

#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Core::Application
{
    class IApplicationHandler;
}

namespace NorvesLib::Core::Boot
{
    struct BootConfig;
}

namespace NorvesLib::Core::Engine
{

    /**
     * @brief アプリケーション処理クラス
     *
     * アプリケーションの初期化、メインループ、終了処理など
     * 実際の動作ロジックを担当します。
     *
     * Engineクラス（GEngine）はデータコンテナとして機能し、
     * このクラスがロジックを担当します。
     */
    class ApplicationProcessor
    {
    public:
        ApplicationProcessor() = default;
        ~ApplicationProcessor() = default;

        // コピー・ムーブ禁止
        ApplicationProcessor(const ApplicationProcessor &) = delete;
        ApplicationProcessor &operator=(const ApplicationProcessor &) = delete;
        ApplicationProcessor(ApplicationProcessor &&) = delete;
        ApplicationProcessor &operator=(ApplicationProcessor &&) = delete;

        /**
         * @brief アプリケーションを初期化
         * @param config 起動設定
         * @return 成功時true
         */
        bool Initialize(const Boot::BootConfig &config);

        /**
         * @brief メインループを実行
         * @return 終了コード
         */
        int Run();

        /**
         * @brief アプリケーションを終了
         */
        void Shutdown();

        /**
         * @brief シングルトンインスタンスを取得
         */
        static ApplicationProcessor &GetInstance();

    private:
        /**
         * @brief 1フレームの処理を実行
         */
        void Tick();

        /**
         * @brief プラットフォームメッセージを処理
         * @return 続行する場合true、終了要求がある場合false
         */
        bool ProcessPlatformMessages();

        /**
         * @brief デルタタイムを計算
         * @return フレーム間隔（秒）
         */
        float CalculateDeltaTime();

        /**
         * @brief GEngineを作成・初期化
         * @return 成功時true
         */
        bool CreateEngine();

        /**
         * @brief GEngineを破棄
         */
        void DestroyEngine();

        /**
         * @brief プラットフォームアプリケーションを作成
         * @param config 起動設定
         * @return 成功時true
         */
        bool CreatePlatformApplication(const Boot::BootConfig &config);

        /**
         * @brief メインウィンドウを作成
         * @param config 起動設定
         * @return 成功時true
         */
        bool CreateMainWindow(const Boot::BootConfig &config);

    private:
        // 時間計測用
        uint64_t m_LastFrameTime = 0;
        float m_TargetFrameTime = 1.0f / 60.0f; // デフォルト60FPS
        uint64_t m_ExitAfterFrames = 0;         // 0は無効
    };

} // namespace NorvesLib::Core::Engine
