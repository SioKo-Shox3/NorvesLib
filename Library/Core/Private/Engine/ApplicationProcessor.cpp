#include "Engine/ApplicationProcessor.h"
#include "Engine/Engine.h"
#include "Boot/BootConfig.h"
#include "Application/IApplicationHandler.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Application/ApplicationFactory.h"
#include "Platform/PlatformApplicationFactory.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/SceneView.h"
#include "RHI/Vulkan/VulkanRHI.h"
#include "RHI/RHIConfig.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include "Thread/JobSystem.h"
#include <chrono>
#include <limits>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace NorvesLib::Core::Container;

namespace
{
    constexpr TCHAR kExitAfterFramesOption[] = TEXT("--exit-after-frames=");
    constexpr size_t kExitAfterFramesOptionLength = (sizeof(kExitAfterFramesOption) / sizeof(TCHAR)) - 1;

    uint64_t ParsePositiveFrameCount(const TCHAR *pValueText, bool &bValid)
    {
        bValid = false;

        if (!pValueText || pValueText[0] == TEXT('\0'))
        {
            return 0;
        }

        uint64_t value = 0;
        for (const TCHAR *pChar = pValueText; *pChar != TEXT('\0'); ++pChar)
        {
            if (*pChar < TEXT('0') || *pChar > TEXT('9'))
            {
                return 0;
            }

            const uint64_t digit = static_cast<uint64_t>(*pChar - TEXT('0'));
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            {
                return 0;
            }

            value = value * 10 + digit;
        }

        if (value == 0)
        {
            return 0;
        }

        bValid = true;
        return value;
    }

    bool TryParseExitAfterFramesOption(const TCHAR *pText, uint64_t &outFrameCount, bool &bMatched)
    {
        outFrameCount = 0;
        bMatched = false;

        if (!pText)
        {
            return false;
        }

        // プレフィックスが一致するか確認
        for (size_t i = 0; i < kExitAfterFramesOptionLength; ++i)
        {
            if (pText[i] == TEXT('\0') || pText[i] != kExitAfterFramesOption[i])
            {
                return false;
            }
        }

        bMatched = true;
        bool bValid = false;
        const uint64_t parsedFrameCount = ParsePositiveFrameCount(
            pText + kExitAfterFramesOptionLength,
            bValid);
        if (!bValid)
        {
            return false;
        }

        outFrameCount = parsedFrameCount;
        return true;
    }
} // namespace

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

        // JobSystemを初期化（ワーカースレッドの起動）
        Thread::JobSystem::Get().Initialize();
        LOG_INFO("JobSystem initialized");

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

        // コマンドライン引数を config.Arguments から取得してパース
        m_ExitAfterFrames = 0;
        const VariableArray<String> &args = config.Arguments;
        for (size_t i = 0; i < args.size(); ++i)
        {
            uint64_t parsedFrameCount = 0;
            bool bMatchedExitAfterFrames = false;
            if (TryParseExitAfterFramesOption(args[i].c_str(), parsedFrameCount, bMatchedExitAfterFrames))
            {
                m_ExitAfterFrames = parsedFrameCount;
                LOG_INFO_F("ApplicationProcessor runtime option exit_after_frames=%llu",
                           static_cast<unsigned long long>(m_ExitAfterFrames));
            }
            else if (bMatchedExitAfterFrames)
            {
                LOG_WARNING("ApplicationProcessor runtime option --exit-after-frames ignored: value must be a positive integer");
            }
        }

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

        // ゲームワールドを初期化
        {
            GEngine->GetWorld().Initialize();

            // WorldにメインSceneViewを設定
            auto &coordinator = GEngine->GetRenderWorld().GetRenderingCoordinator();
            auto mainSceneView = coordinator.GetMainSceneView();
            if (mainSceneView)
            {
                GEngine->GetWorld().SetSceneView(mainSceneView.get());
                LOG_INFO("World connected to MainSceneView");
            }
            else
            {
                LOG_WARNING("MainSceneView not available for World");
            }

            LOG_INFO("World initialized successfully");
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
            // 入力システムのフレーム開始（前フレーム状態保存、累積値リセット）
            // ※ProcessPlatformMessagesの前に呼ぶこと。
            //   メッセージ処理中にInjectされた入力をOnUpdateで参照するため。
            GEngine->GetInputSystem().BeginFrame();

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

            // ゲームワールドをFinalize
            GEngine->GetWorld().Finalize();

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

        // JobSystemをシャットダウン
        Thread::JobSystem::Get().Shutdown();

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
#if NORVES_ENABLE_STATS
        auto &statsManager = Debug::StatsManager::Get();
        const bool bTraceActive = statsManager.IsTraceActive();
        std::chrono::high_resolution_clock::time_point gameThreadStartTime;
        if (bTraceActive)
        {
            gameThreadStartTime = std::chrono::high_resolution_clock::now();
        }
#endif

        // デルタタイムを計算
        float deltaTime = CalculateDeltaTime();
        GEngine->SetDeltaTime(deltaTime);

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            statsManager.BeginFrame(GEngine->GetFrameCount(), deltaTime);
        }
#endif

        // 注: BeginFrame()はRun()ループ内でProcessPlatformMessagesの前に呼ばれている

        auto *handler = GEngine->GetApplicationHandler();

        // OnUpdate呼び出し
        if (handler)
        {
            handler->OnUpdate(deltaTime);
        }

        // GameModeの更新
        GEngine->UpdateGameModeStateMachine(deltaTime);

        // ゲームワールドのTick更新
        GEngine->GetWorld().Tick(deltaTime);

        // ワールドからSceneViewへProxy同期
        GEngine->GetWorld().SyncToSceneView();

        // OnPreRender呼び出し
        if (handler)
        {
            handler->OnPreRender();
        }

        // 描画処理
        {
            auto &renderWorld = GEngine->GetRenderWorld();
            if (renderWorld.IsInitialized())
            {
                renderWorld.BeginFrame();
                renderWorld.Render();
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
        if (m_ExitAfterFrames > 0 && GEngine->GetFrameCount() >= m_ExitAfterFrames)
        {
            LOG_INFO_F("ApplicationProcessor::Tick() - exit-after-frames reached frame=%llu target=%llu",
                       static_cast<unsigned long long>(GEngine->GetFrameCount()),
                       static_cast<unsigned long long>(m_ExitAfterFrames));
            GEngine->RequestExit(0);
        }

        // 入力システムのフレーム終了
        GEngine->GetInputSystem().EndFrame();

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            auto gameThreadEndTime = std::chrono::high_resolution_clock::now();
            const float gameThreadTimeMs =
                std::chrono::duration<float, std::milli>(gameThreadEndTime - gameThreadStartTime).count();
            statsManager.SetGameThreadTimeMs(gameThreadTimeMs);
            statsManager.EndFrame();
        }
#endif
    }

    bool ApplicationProcessor::ProcessPlatformMessages()
    {
        auto* platformApp = GEngine ? GEngine->GetPlatformApp() : nullptr;
        if (!platformApp)
        {
            return true;
        }

        platformApp->PumpMessages();

        if (platformApp->IsExitRequested())
        {
            GEngine->RequestExit(platformApp->GetExitCode());
            return false;
        }

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
        auto platformApp = Platform::CreatePlatformApplication();
        if (!platformApp)
        {
            LOG_ERROR("Failed to create platform application");
            return false;
        }

        if (!platformApp->Initialize(config.Arguments))
        {
            LOG_ERROR("Failed to initialize platform application");
            return false;
        }

        GEngine->SetPlatformApp(std::move(platformApp));
        LOG_INFO("Platform application created and initialized");
        return true;
    }

    bool ApplicationProcessor::CreateMainWindow(const Boot::BootConfig &config)
    {
        auto window = Platform::CreatePlatformWindow();
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
    }

} // namespace NorvesLib::Core::Engine
