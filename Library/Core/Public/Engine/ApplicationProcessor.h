#pragma once

#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "RHI/RHITypes.h"
#include "Delegate/Delegate.h"

#include <cstdint>

namespace NorvesLib { class IWindow; }

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
    class FixedStepScheduler;
    struct FixedStepAdvanceResult;
    struct FixedStepFrameTiming;
    struct ApplicationFixedStepTestAccess;
    struct ApplicationInputFrameTestAccess;
    struct ApplicationInputFocusTestAccess;
    class Engine;

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
        ApplicationProcessor();
        ~ApplicationProcessor();

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

        // 現在の固定更新設定（60または120Hz）。
        uint32_t GetFixedUpdateRateHz() const;

        /**
         * @brief アプリケーションを終了
         */
        void Shutdown();

        /**
         * @brief シングルトンインスタンスを取得
         */
        static ApplicationProcessor &GetInstance();

        /**
         * @brief シングルトンインスタンスを破棄
         */
        static void DestroyInstance();

    private:
        friend struct ApplicationFixedStepTestAccess;
        friend struct ApplicationInputFrameTestAccess;
        friend struct ApplicationInputFocusTestAccess;
        friend struct ApplicationHapticsTestAccess;
        void ConnectInputWindow(Container::TSharedPtr<NorvesLib::IWindow> window);
        void DisconnectInputWindow();
        void OnWindowInputFocusChanged(bool focused);
        void QueueInputDeviceFocus(bool focused, bool resetOperations, bool notifyRouter = false);
        bool ApplyPendingInputDeviceFocus();
        void DispatchInputFocusEvents();
        bool SynchronizeInputCursorMode();

        /**
         * @brief 1フレームの処理を実行
         */
        void Tick();

        /**
         * @brief プラットフォームメッセージを処理
         * @return 続行する場合true、終了要求がある場合false
         */
        bool ProcessPlatformMessages();

        // handlerは呼出元が全フレーム中保持する借用参照。描画/OS処理を含まない。
        FixedStepAdvanceResult TickSimulation(int64_t rawDeltaNanoseconds, float deltaTime,
            bool bAdvanceSimulation, Application::IApplicationHandler* handler);
        bool BeginInputFrame(int64_t timeNanoseconds);
        bool UpdateInputFrame(int64_t timeNanoseconds, int64_t rawDeltaNanoseconds);
        bool UpdateHapticsFrame(int64_t rawDeltaNanoseconds);
        void TickSimulationAndHaptics(int64_t rawDeltaNanoseconds, float deltaTime,
            Application::IApplicationHandler* handler);
        int64_t CalculateRawDeltaTimeNanoseconds();
        float ClampVariableDeltaTime(int64_t rawDeltaNanoseconds) const;
        FixedStepAdvanceResult AdvanceFixedSimulation(
            int64_t rawDeltaNanoseconds,
            bool bAdvanceSimulation, FixedStepFrameTiming* timing = nullptr);

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
        // ApplicationProcessor がデバイス寿命を管理。RenderWorld 終了後に解放。
        RHI::DevicePtr m_Device;
        Container::TSharedPtr<NorvesLib::IWindow> m_InputWindow;
        Delegate<void,bool> m_InputFocusSubscription;
        Engine* m_InputEngine = nullptr; // GEngineとの一致確認用。非所有。
        Container::VariableArray<bool> m_PendingInputFocus;
        bool m_HasInputFocus = false;
        bool m_InputFocused = false;
        bool m_DispatchingInputFocus = false;
        Engine* m_PendingInputDeviceFocusEngine = nullptr;
        Container::VariableArray<bool> m_PendingRouterInputFocus;
        bool m_HasPendingInputDeviceFocus = false;
        bool m_PendingInputDeviceFocus = false;
        bool m_PendingInputDeviceFocusLoss = false;
        bool m_PendingInputDeviceFocusReset = false;
        bool m_ApplyingInputDeviceFocus = false;
        uint64_t m_PendingInputDeviceFocusSerial = 0;
        bool m_CursorFailureWarned = false;
        bool m_HapticsFailureWarned = false;
        uint64_t m_InputFocusConnectionSerial = 0;
        Container::TUniquePtr<FixedStepScheduler> m_FixedStepScheduler;
        int64_t m_LastFrameTimeNanoseconds = 0;
        float m_TargetFrameTime = 1.0f / 60.0f; // デフォルト60FPS
        uint64_t m_ExitAfterFrames = 0;         // 0は無効
        uint64_t m_ExitAfterRenderedFrames = 0;
        uint64_t m_AssetSettleRenderedBaseline = 0;
        bool m_bWaitForAssetSettle = false;
        bool m_bObservedPendingAssets = false;
        bool m_bAssetSettleBaselineLatched = false;
        // --capture-png: 描画フレーム数の終了条件に達したら最終出力を取得してPNGに保存し、保存後に終了する。
        Container::String m_CapturePngPath;
        uint64_t m_CaptureRequestedRenderedFrame = 0;
        bool m_bCaptureRequested = false;
    };

} // namespace NorvesLib::Core::Engine
