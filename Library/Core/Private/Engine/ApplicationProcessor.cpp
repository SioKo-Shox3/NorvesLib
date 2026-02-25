#include "Engine/ApplicationProcessor.h"
#include "Engine/Engine.h"
#include "Boot/BootConfig.h"
#include "Application/IApplicationHandler.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Application/ApplicationFactory.h"
#include "Platform/Windows/WindowsApplicationFactory.h"
#include "Rendering/RenderWorld.h"
#include "RHI/Vulkan/VulkanRHI.h"
#include "RHI/RHIConfig.h"
#include "Logging/LogMacros.h"
#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Engine
{

    // シングルトンインスタンス
    static ApplicationProcessor *s_Instance = nullptr;

    ApplicationProcessor &ApplicationProcessor::GetInstance()
    {
        if (!s_Instance)
        {
            s_Instance = new ApplicationProcessor();
        }
        return *s_Instance;
    }

    bool ApplicationProcessor::Initialize(const Boot::BootConfig &config)
    {
        LOG_INFO("ApplicationProcessor::Initialize() - Starting initialization");

        // GEngineを作成
        if (!CreateEngine())
        {
            LOG_ERROR("Failed to create engine");
            return false;
        }

        // ターゲットフレームレートを設定
        if (config.TargetFrameRate > 0.0f)
        {
            m_TargetFrameTime = 1.0f / config.TargetFrameRate;
        }

        // ApplicationHandlerを作成
        if (config.CreateHandler)
        {
            auto handler = config.CreateHandler();
            if (!handler)
            {
                LOG_ERROR("Failed to create application handler");
                return false;
            }
            GEngine->SetApplicationHandler(handler);
            LOG_INFO("Application handler created successfully");
        }
        else
        {
            LOG_ERROR("No handler creator specified in BootConfig");
            return false;
        }

        // コマンドライン引数を取得（現時点では空）
        VariableArray<String> args;
#ifdef _WIN32
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv)
        {
            for (int i = 0; i < argc; ++i)
            {
                args.emplace_back(reinterpret_cast<const TCHAR *>(argv[i]));
            }
            LocalFree(argv);
        }
#endif

        // OnPreInitialize呼び出し
        auto *handler = GEngine->GetApplicationHandler();
        if (handler && !handler->OnPreInitialize(args))
        {
            LOG_ERROR("OnPreInitialize failed");
            return false;
        }

        // プラットフォームアプリケーションを作成
        if (!CreatePlatformApplication(config))
        {
            LOG_ERROR("Failed to create platform application");
            return false;
        }

        // メインウィンドウを作成
        if (!CreateMainWindow(config))
        {
            LOG_ERROR("Failed to create main window");
            return false;
        }

        // RHI初期化（エンジン層の責務）
        if (!RHI_INITIALIZE())
        {
            LOG_ERROR("Failed to initialize RHI");
            return false;
        }
        LOG_INFO("RHI initialized successfully");

        // レンダリングシステムを初期化
        {
            Rendering::RenderWorldSettings renderSettings;
            renderSettings.Device = RHI_GET_DEVICE();
            renderSettings.WindowHandle = GEngine->GetMainWindow()->GetNativeHandle();
            renderSettings.Width = config.WindowWidth;
            renderSettings.Height = config.WindowHeight;
            renderSettings.BackBufferCount = 2;
            renderSettings.bVSync = true;
            renderSettings.bEnableValidation = true;

            if (!GEngine->GetRenderWorld().Initialize(renderSettings))
            {
                LOG_ERROR("Failed to initialize RenderWorld");
                return false;
            }
            LOG_INFO("RenderWorld initialized successfully");
        }

        // OnInitialize呼び出し
        if (handler && !handler->OnInitialize())
        {
            LOG_ERROR("OnInitialize failed");
            return false;
        }

        // GameModeステートマシンを作成
        if (handler)
        {
            auto stateMachine = handler->CreateGameModeStateMachine();
            if (stateMachine)
            {
                GEngine->SetGameModeStateMachine(std::move(stateMachine));
                LOG_INFO("GameMode state machine created successfully");
            }
        }

        // OnPostInitialize呼び出し
        if (handler)
        {
            handler->OnPostInitialize();
        }

        GEngine->SetRunning(true);
        LOG_INFO("ApplicationProcessor::Initialize() - Initialization completed");

        return true;
    }

    int ApplicationProcessor::Run()
    {
        LOG_INFO("ApplicationProcessor::Run() - Starting main loop");

        // 初期時間を設定
        auto now = std::chrono::high_resolution_clock::now();
        m_LastFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(
                              now.time_since_epoch())
                              .count();

        while (GEngine && GEngine->IsRunning() && !GEngine->IsExitRequested())
        {
            // プラットフォームメッセージ処理
            if (!ProcessPlatformMessages())
            {
                break;
            }

            // 1フレームの処理
            Tick();
        }

        LOG_INFO("ApplicationProcessor::Run() - Main loop ended");

        return GEngine ? GEngine->GetExitCode() : 0;
    }

    void ApplicationProcessor::Shutdown()
    {
        LOG_INFO("ApplicationProcessor::Shutdown() - Starting shutdown");

        if (GEngine)
        {
            auto *handler = GEngine->GetApplicationHandler();

            // OnPreShutdown呼び出し
            if (handler)
            {
                handler->OnPreShutdown();
            }

            // レンダリングシステムをシャットダウン
            GEngine->GetRenderWorld().Shutdown();

            // RHI終了（エンジン層の責務）
            RHI_SHUTDOWN();
            LOG_INFO("RHI shutdown completed");

            // GameModeステートマシンを解放
            GEngine->SetGameModeStateMachine(nullptr);

            // OnShutdown呼び出し
            if (handler)
            {
                handler->OnShutdown();
            }

            // メインウィンドウを解放
            GEngine->SetMainWindow(nullptr);

            // プラットフォームアプリケーションを終了
            auto *platformApp = GEngine->GetPlatformApp();
            if (platformApp)
            {
                platformApp->Shutdown();
            }
            GEngine->SetPlatformApp(nullptr);

            // ハンドラを解放
            GEngine->SetApplicationHandler(nullptr);
        }

        // GEngineを破棄
        DestroyEngine();

        // シングルトンを解放
        if (s_Instance)
        {
            delete s_Instance;
            s_Instance = nullptr;
        }

        LOG_INFO("ApplicationProcessor::Shutdown() - Shutdown completed");
    }

    void ApplicationProcessor::Tick()
    {
        // デルタタイムを計算
        float deltaTime = CalculateDeltaTime();
        GEngine->SetDeltaTime(deltaTime);

        auto *handler = GEngine->GetApplicationHandler();

        // OnUpdate呼び出し
        if (handler)
        {
            handler->OnUpdate(deltaTime);
        }

        // GameModeの更新
        GEngine->UpdateGameModeStateMachine(deltaTime);

        // OnPreRender呼び出し
        if (handler)
        {
            handler->OnPreRender();
        }

        // TODO: 描画処理
        {
            auto &renderWorld = GEngine->GetRenderWorld();
            if (renderWorld.IsInitialized())
            {
                renderWorld.BeginFrame();
                renderWorld.RenderTriangle();
                renderWorld.EndFrame();
            }
        }

        // OnPostRender呼び出し
        if (handler)
        {
            handler->OnPostRender();
        }

        // フレームカウントをインクリメント
        GEngine->IncrementFrameCount();
    }

    bool ApplicationProcessor::ProcessPlatformMessages()
    {
#ifdef _WIN32
        MSG msg = {};
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                GEngine->RequestExit(static_cast<int>(msg.wParam));
                return false;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
#endif
        return true;
    }

    float ApplicationProcessor::CalculateDeltaTime()
    {
        auto now = std::chrono::high_resolution_clock::now();
        uint64_t currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
                                   now.time_since_epoch())
                                   .count();

        float deltaTime = static_cast<float>(currentTime - m_LastFrameTime) / 1000000.0f;
        m_LastFrameTime = currentTime;

        // 異常値の制限（最大0.1秒）
        if (deltaTime > 0.1f)
        {
            deltaTime = 0.1f;
        }

        return deltaTime;
    }

    bool ApplicationProcessor::CreateEngine()
    {
        if (GEngine)
        {
            LOG_WARNING("Engine already exists");
            return true;
        }

        GEngine = new Engine();
        LOG_INFO("Engine created successfully");
        return true;
    }

    void ApplicationProcessor::DestroyEngine()
    {
        if (GEngine)
        {
            delete GEngine;
            GEngine = nullptr;
            LOG_INFO("Engine destroyed");
        }
    }

    bool ApplicationProcessor::CreatePlatformApplication(const Boot::BootConfig &config)
    {
        (void)config; // 現時点では未使用

#ifdef _WIN32
        auto platformApp = Platform::WindowsApplicationFactory::CreateWindowsApplication();
        if (!platformApp)
        {
            LOG_ERROR("Failed to create Windows application");
            return false;
        }

        // 引数を取得
        VariableArray<String> args;
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv)
        {
            for (int i = 0; i < argc; ++i)
            {
                args.emplace_back(reinterpret_cast<const TCHAR *>(argv[i]));
            }
            LocalFree(argv);
        }

        if (!platformApp->Initialize(args))
        {
            LOG_ERROR("Failed to initialize platform application");
            return false;
        }

        GEngine->SetPlatformApp(std::move(platformApp));
        LOG_INFO("Platform application created and initialized");
        return true;
#else
        LOG_ERROR("Unsupported platform");
        return false;
#endif
    }

    bool ApplicationProcessor::CreateMainWindow(const Boot::BootConfig &config)
    {
#ifdef _WIN32
        auto window = Platform::WindowsApplicationFactory::CreateWindowsWindow();
        if (!window)
        {
            LOG_ERROR("Failed to create window");
            return false;
        }

        // ウィンドウタイトルをStringからstd::stringに変換（必要に応じて）
        if (!window->Create(config.WindowTitle, config.WindowWidth, config.WindowHeight))
        {
            LOG_ERROR("Failed to create window with specified parameters");
            return false;
        }

        window->Show();

        // プラットフォームアプリケーションにウィンドウを登録
        auto *platformApp = GEngine->GetPlatformApp();
        if (platformApp)
        {
            platformApp->RegisterWindow(window);
        }

        GEngine->SetMainWindow(window);
        LOG_INFO("Main window created successfully");
        return true;
#else
        (void)config;
        LOG_ERROR("Unsupported platform");
        return false;
#endif
    }

} // namespace NorvesLib::Core::Engine
