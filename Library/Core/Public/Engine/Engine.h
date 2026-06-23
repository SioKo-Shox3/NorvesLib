#pragma once

#include "Container/PointerTypes.h"
#include "GameMode/IStateMachine.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Rendering/RenderWorld.h"
#include "Object/World.h"
#include "Input/InputSystem.h"
#include "Input/InputRouter.h"

namespace NorvesLib::Core::Application
{
    class IApplicationHandler;
}

namespace NorvesLib::Core::Engine
{

    /**
     * @brief エンジンクラス
     *
     * アプリケーション動作において必ず確保される機能の実体をまとめたクラス。
     * ロジックは持たず、各種サブシステムへの参照を保持するデータコンテナとして機能します。
     *
     * グローバルインスタンス GEngine を通じてアクセスします。
     */
    class Engine
    {
    public:
        // コンストラクタで InputSystem へ Router を配線する（Engine.cpp）。
        Engine();
        ~Engine();

        // コピー・ムーブ禁止
        Engine(const Engine &) = delete;
        Engine &operator=(const Engine &) = delete;
        Engine(Engine &&) = delete;
        Engine &operator=(Engine &&) = delete;

        // ========== プラットフォームアプリケーション ==========

        /**
         * @brief プラットフォーム固有のアプリケーションを設定
         */
        void SetPlatformApp(Container::TUniquePtr<NorvesLib::IApplication> app)
        {
            m_PlatformApp = std::move(app);
        }

        /**
         * @brief プラットフォーム固有のアプリケーションを取得
         */
        NorvesLib::IApplication *GetPlatformApp() const
        {
            return m_PlatformApp.get();
        }

        // ========== アプリケーションハンドラ ==========

        /**
         * @brief アプリケーションハンドラを設定
         */
        void SetApplicationHandler(Container::TSharedPtr<Application::IApplicationHandler> handler)
        {
            m_Handler = handler;
        }

        /**
         * @brief アプリケーションハンドラを取得
         */
        Application::IApplicationHandler *GetApplicationHandler() const
        {
            return m_Handler.get();
        }

        /**
         * @brief アプリケーションハンドラを共有ポインタとして取得
         */
        Container::TSharedPtr<Application::IApplicationHandler> GetApplicationHandlerShared() const
        {
            return m_Handler;
        }

        // ========== GameModeステートマシン ==========

        /**
         * @brief GameModeステートマシンを設定
         * @param stateMachine ステートマシンへのユニークポインタ
         */
        void SetGameModeStateMachine(Container::TUniquePtr<GameMode::IStateMachine> stateMachine)
        {
            m_GameModeStateMachine = std::move(stateMachine);
        }

        /**
         * @brief GameModeステートマシンを更新
         * @param deltaTime フレーム間隔（秒）
         */
        void UpdateGameModeStateMachine(float deltaTime)
        {
            if (m_GameModeStateMachine)
            {
                m_GameModeStateMachine->Update(deltaTime);
            }
        }

        /**
         * @brief GameModeステートマシンを取得
         * @return ステートマシンへのポインタ
         */
        GameMode::IStateMachine *GetGameModeStateMachine() const
        {
            return m_GameModeStateMachine.get();
        }

        /**
         * @brief GameModeステートマシンを型安全に取得
         * @tparam T ステートマシンの具象型
         * @return ステートマシンへのポインタ
         */
        template <typename T>
        T *GetGameModeStateMachineAs() const
        {
            return static_cast<T *>(m_GameModeStateMachine.get());
        }

        // ========== メインウィンドウ ==========

        /**
         * @brief メインウィンドウを設定
         */
        void SetMainWindow(Container::TSharedPtr<NorvesLib::IWindow> window)
        {
            m_MainWindow = window;
        }

        /**
         * @brief メインウィンドウを取得
         */
        NorvesLib::IWindow *GetMainWindow() const
        {
            return m_MainWindow.get();
        }

        /**
         * @brief メインウィンドウを共有ポインタとして取得
         */
        Container::TSharedPtr<NorvesLib::IWindow> GetMainWindowShared() const
        {
            return m_MainWindow;
        }

        // ========== 実行状態 ==========

        /**
         * @brief 実行中フラグを設定
         */
        void SetRunning(bool bRunning)
        {
            m_bIsRunning = bRunning;
        }

        /**
         * @brief 実行中かどうかを取得
         */
        bool IsRunning() const
        {
            return m_bIsRunning;
        }

        /**
         * @brief 終了コードを設定
         */
        void SetExitCode(int exitCode)
        {
            m_ExitCode = exitCode;
        }

        /**
         * @brief 終了コードを取得
         */
        int GetExitCode() const
        {
            return m_ExitCode;
        }

        // ========== フレーム情報 ==========

        /**
         * @brief デルタタイムを設定
         */
        void SetDeltaTime(float deltaTime)
        {
            m_DeltaTime = deltaTime;
        }

        /**
         * @brief デルタタイムを取得
         */
        float GetDeltaTime() const
        {
            return m_DeltaTime;
        }

        /**
         * @brief フレームカウントをインクリメント
         */
        void IncrementFrameCount()
        {
            ++m_FrameCount;
        }

        /**
         * @brief フレームカウントを取得
         */
        uint64_t GetFrameCount() const
        {
            return m_FrameCount;
        }

        // ========== 終了要求 ==========

        /**
         * @brief 終了要求を発行
         * @param exitCode 終了コード
         */
        void RequestExit(int exitCode = 0)
        {
            m_bExitRequested = true;
            m_ExitCode = exitCode;
        }

        /**
         * @brief 終了要求があるかどうか
         */
        bool IsExitRequested() const
        {
            return m_bExitRequested;
        }

        // ========== レンダリングシステム ==========

        /**
         * @brief レンダリングワールドを取得
         * @return RenderWorldへの参照
         */
        Rendering::RenderWorld &GetRenderWorld()
        {
            return m_RenderWorld;
        }

        /**
         * @brief レンダリングリソースを取得
         */
        Rendering::RenderResources &GetRenderResources()
        {
            return m_RenderWorld.GetRenderResources();
        }

        // ========== ゲームワールド ==========

        /**
         * @brief ゲームワールドを取得
         * @return Worldへの参照
         */
        World &GetWorld()
        {
            return m_World;
        }

        /**
         * @brief ゲームワールドを取得（const版）
         */
        const World &GetWorld() const
        {
            return m_World;
        }

        // ========== 入力システム ==========

        /**
         * @brief 入力システムを取得
         * @return InputSystemへの参照
         */
        Input::InputSystem &GetInputSystem()
        {
            return m_InputSystem;
        }

        /**
         * @brief 入力システムを取得（const版）
         */
        const Input::InputSystem &GetInputSystem() const
        {
            return m_InputSystem;
        }

        // ========== 入力ルーター ==========

        /**
         * @brief 入力ルーターを取得
         * @return InputRouterへの参照
         */
        Input::InputRouter &GetInputRouter()
        {
            return m_InputRouter;
        }

        /**
         * @brief 入力ルーターを取得（const版）
         */
        const Input::InputRouter &GetInputRouter() const
        {
            return m_InputRouter;
        }

    private:
        // サブシステムへの参照
        Container::TUniquePtr<NorvesLib::IApplication> m_PlatformApp;
        Container::TSharedPtr<Application::IApplicationHandler> m_Handler;
        Container::TUniquePtr<GameMode::IStateMachine> m_GameModeStateMachine;
        Container::TSharedPtr<NorvesLib::IWindow> m_MainWindow;

        // レンダリングシステム（GEngine配下で実体保持）
        Rendering::RenderWorld m_RenderWorld;

        // ゲームワールド（GEngine配下で実体保持）
        World m_World;

        // 入力システム（GEngine配下で実体保持）
        Input::InputSystem m_InputSystem;

        // 入力ルーター（GEngine配下で実体保持・InputSystem からの配送先）
        Input::InputRouter m_InputRouter;

        // 実行状態
        bool m_bIsRunning = false;
        bool m_bExitRequested = false;
        int m_ExitCode = 0;

        // フレーム情報
        float m_DeltaTime = 0.0f;
        uint64_t m_FrameCount = 0;
    };

    /**
     * @brief エンジンのグローバルインスタンス
     *
     * アプリケーション全体で唯一のエンジンインスタンスへのアクセスを提供します。
     * ApplicationProcessorによって初期化・終了されます。
     */
    extern Engine *GEngine;

} // namespace NorvesLib::Core::Engine
