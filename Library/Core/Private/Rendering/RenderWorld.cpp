#include "Rendering/RenderWorld.h"
#include "Rendering/RenderResourceContexts.h"
#include "Rendering/SceneView.h"
#include "Rendering/Viewport.h"
#include "Resource/GLTFAnalyzer.h"
#include "RHI/IDevice.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <chrono>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        using LoadProfileClock = std::chrono::steady_clock;

        LoadProfileClock::time_point LoadProfileNow()
        {
            return LoadProfileClock::now();
        }

        double LoadProfileElapsedMs(LoadProfileClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(LoadProfileClock::now() - startTime).count();
        }
    }

    bool RenderWorld::Initialize(const RenderWorldSettings &settings)
    {
        if (m_bInitialized)
        {
            LOG_WARNING("RenderWorld is already initialized");
            return true;
        }

        LOG_INFO("RenderWorld::Initialize() - Starting RHI initialization");

        m_Width = settings.Width;
        m_Height = settings.Height;
        m_RenderScale = settings.RenderScale;
        m_bVSyncEnabled = settings.bVSync;
        m_bFullscreen = settings.bFullscreen;
        m_bMultiThreadedRendering = settings.bEnableMultiThreadedRendering;
        m_bResizePending.Store(false, std::memory_order_release);
        m_PendingWidth.Store(settings.Width, std::memory_order_release);
        m_PendingHeight.Store(settings.Height, std::memory_order_release);

        // ========================================
        // 1. RHI Device (passed from Engine layer)
        // ========================================
        m_Device = settings.Device;
        if (!m_Device)
        {
            NORVES_LOG_ERROR("Rendering", "RHI Device is null - Engine must initialize RHI before RenderWorld");
            return false;
        }

        LOG_INFO("RHI Device received (API: %d)", static_cast<int>(m_Device->GetAPI()));

        // ========================================
        // 2. RenderingCoordinator初期化
        // ========================================
        RenderingCoordinatorSettings coordSettings;
        coordSettings.Device = m_Device;
        coordSettings.WindowHandle = settings.WindowHandle;
        coordSettings.Width = settings.Width;
        coordSettings.Height = settings.Height;
        coordSettings.RenderScale = settings.RenderScale;
        coordSettings.BackBufferCount = settings.BackBufferCount;
        coordSettings.bVSync = settings.bVSync;
        coordSettings.bEnableMultiThreadedRendering = settings.bEnableMultiThreadedRendering;
        coordSettings.MaxDrawCallsPerFrame = settings.MaxDrawCallsPerFrame;
        coordSettings.bEnableValidation = settings.bEnableValidation;
        coordSettings.RenderGraphDumpOptions = settings.RenderGraphDumpOptions;

        if (!m_RenderingCoordinator.Initialize(coordSettings))
        {
            NORVES_LOG_ERROR("Rendering", "Failed to initialize RenderingCoordinator");
            return false;
        }

        // ========================================
        // 3. RenderResources初期化
        // ========================================
        if (!m_RenderResources.Initialize(m_Device))
        {
            NORVES_LOG_ERROR("Rendering", "Failed to initialize RenderResources");
            return false;
        }

        // RenderingCoordinatorにRenderResourcesを設定
        m_RenderingCoordinator.SetRenderResources(&m_RenderResources);

        // ========================================
        // 4. RenderThread初期化・起動（マルチスレッドレンダリング有効時のみ）
        // ========================================
        if (m_bMultiThreadedRendering)
        {
            if (!m_RenderThread.Initialize(&m_RenderingCoordinator))
            {
                NORVES_LOG_ERROR("Rendering", "Failed to initialize RenderThread");
                return false;
            }
            m_RenderThread.Start();
            LOG_INFO("RenderThread started (multi-threaded rendering enabled)");
        }

        m_bInitialized = true;
        LOG_INFO("RenderWorld::Initialize() - Initialization completed successfully");
        return true;
    }

    void RenderWorld::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        LOG_INFO("RenderWorld::Shutdown() - Starting shutdown");

        // RenderThreadを先に停止してからリソース破棄へ進む
        // （Coordinatorのリソースへのアクセスが残らないようにする）
        m_RenderThread.Shutdown();
        m_bResizePending.Store(false, std::memory_order_release);

        // pre-device-teardown フック: RenderThread 停止済み(in-flight 参照なし)かつ
        // device 生存中(下記 m_Device.reset() より前)の地点でモジュールの RHI リソースを
        // 安全に解放させる。登録が無ければ no-op。
        if (m_PreDeviceTeardownHook)
        {
            m_PreDeviceTeardownHook(m_PreDeviceTeardownContext);
        }

        Resource::GLTFAnalyzer::CancelPendingModelLoadsAndWait();

        // RenderResourcesの終了（メッシュGPUリソース等の解放）
        m_RenderResources.Shutdown();

        // RenderingCoordinatorの終了（RHIリソース解放を含む）
        m_RenderingCoordinator.Shutdown();

        // デバイス参照の解放（Engine層がRHI終了を管理）
        m_Device.reset();

        m_bInitialized = false;
        LOG_INFO("RenderWorld::Shutdown() - Shutdown completed");
    }

    void RenderWorld::BeginFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // 保留中のリサイズをフレーム開始時に安全に適用する
        bool bExpectedResizePending = true;
        if (m_bResizePending.CompareExchangeStrong(bExpectedResizePending,
                                                   false,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire))
        {
            uint32_t w = m_PendingWidth.Load(std::memory_order_acquire);
            uint32_t h = m_PendingHeight.Load(std::memory_order_acquire);

            WaitForRender();
            m_Width = w;
            m_Height = h;
            m_RenderingCoordinator.Resize(w, h);
        }

        auto textureFlushStartTime = LoadProfileNow();
        uint32_t textureFlushProcessed = m_RenderResources.Textures().FlushCompletedTextureLoads();
        double textureFlushMs = LoadProfileElapsedMs(textureFlushStartTime);
        if (textureFlushProcessed > 0)
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=renderworld_texture_flush role=main_render processed=%u ms=%.3f success=1",
                            static_cast<unsigned int>(textureFlushProcessed),
                            textureFlushMs);
        }

        auto modelFlushStartTime = LoadProfileNow();
        ModelLoadResourceContext modelLoadContext{
            m_RenderResources.Textures(),
            m_RenderResources.MegaGeometry()};
        uint32_t modelFlushProcessed = Resource::GLTFAnalyzer::FlushCompletedModelLoads(modelLoadContext);
        double modelFlushMs = LoadProfileElapsedMs(modelFlushStartTime);
        if (modelFlushProcessed > 0)
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=renderworld_model_flush role=main_render processed=%u ms=%.3f success=1",
                            static_cast<unsigned int>(modelFlushProcessed),
                            modelFlushMs);
        }
        m_RenderingCoordinator.BeginFrame();
    }

    void RenderWorld::Render()
    {
        if (!m_bInitialized)
        {
            return;
        }

#if NORVES_ENABLE_STATS
        auto &statsManager = NorvesLib::Debug::StatsManager::Get();
        const bool bTraceActive = statsManager.IsTraceActive();
        std::chrono::high_resolution_clock::time_point renderPrepareStartTime;
        if (bTraceActive)
        {
            renderPrepareStartTime = std::chrono::high_resolution_clock::now();
        }
#endif

        // GT側の作業: シーン収集 → DrawCommandスナップショット生成
        // 実際の描画（swapchain acquire/submit/present）はEndFrame経由でRTまたはSTが担当。
        m_RenderingCoordinator.CollectScene();
        m_RenderingCoordinator.GenerateDrawCommands();

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            auto renderPrepareEndTime = std::chrono::high_resolution_clock::now();
            const float renderPrepareTimeMs =
                std::chrono::duration<float, std::milli>(renderPrepareEndTime - renderPrepareStartTime).count();
            statsManager.SetRenderPrepareTimeMs(renderPrepareTimeMs);
        }
#endif
    }

    void RenderWorld::CollectScene()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_RenderingCoordinator.CollectScene();
    }

    void RenderWorld::SetMainCamera(const CameraProxy &camera)
    {
        m_RenderingCoordinator.SetMainCamera(camera);
    }

    void RenderWorld::EndFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // パケットを確定（Writing→Ready）して返す
        FramePacket *packet = m_RenderingCoordinator.EndFrame();

        if (m_bMultiThreadedRendering && m_RenderThread.IsRunning())
        {
            // ========================================
            // MT経路: RenderThreadにパケットを渡して非同期描画
            // ========================================
            // NotifyNewFrame内でpacketはQueued状態に遷移されるため
            // 以後はRenderThreadの所有として扱う。
            if (packet)
            {
                m_RenderThread.NotifyNewFrame(packet);
            }
        }
        else
        {
            // ========================================
            // ST経路: GameThreadでインライン描画
            // ========================================
            m_RenderingCoordinator.RenderFrame(packet);
            m_RenderingCoordinator.ReleasePacket(packet);
        }

        // 統計更新
        m_Stats.FrameNumber++;
#if NORVES_ENABLE_STATS
        if (NorvesLib::Debug::StatsManager::Get().IsTraceActive())
        {
            const auto &coordStats = m_RenderingCoordinator.GetStats();
            m_Stats.DeltaTime = coordStats.DeltaTime;
            m_Stats.FPS = coordStats.FPS;
            m_Stats.DrawCalls = coordStats.DrawCalls;
            m_Stats.TrianglesRendered = coordStats.TrianglesRendered;
            m_Stats.VisibleObjects = coordStats.VisibleObjects;
            m_Stats.GameThreadTimeMs = coordStats.GameThreadTimeMs;
            m_Stats.RenderThreadTimeMs = m_RenderThread.GetStats().FrameTimeMs;
            m_Stats.GPUTimeMs = coordStats.GPUTimeMs;
        }
#endif
    }

    void RenderWorld::WaitForRender()
    {
        if (m_RenderThread.IsRunning())
        {
            m_RenderThread.WaitForIdle();
        }
        else if (m_Device)
        {
            m_Device->WaitIdle();
        }
    }

    void RenderWorld::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return;
        }

        if (!m_bInitialized)
        {
            return;
        }

        const uint32_t currentWidth = m_PendingWidth.Load(std::memory_order_acquire);
        const uint32_t currentHeight = m_PendingHeight.Load(std::memory_order_acquire);
        const bool bResizePending = m_bResizePending.Load(std::memory_order_acquire);

        // 同サイズなら何もしない
        if (width == currentWidth && height == currentHeight && !bResizePending)
        {
            return;
        }

        LOG_INFO("RenderWorld::Resize(%u, %u) - queued for next BeginFrame", width, height);

        // BeginFrame冒頭で安全に適用できるよう保留にする
        m_PendingWidth.Store(width, std::memory_order_release);
        m_PendingHeight.Store(height, std::memory_order_release);
        m_bResizePending.Store(true, std::memory_order_release);
    }

    void RenderWorld::SetRenderScale(float renderScale)
    {
        m_RenderScale = renderScale;
        m_RenderingCoordinator.SetRenderScale(renderScale);
    }

    void RenderWorld::SetVSync(bool bEnabled)
    {
        m_bVSyncEnabled = bEnabled;
        // TODO: RenderingCoordinator経由でSwapChainに反映
    }

    void RenderWorld::SetFullscreen(bool bEnabled)
    {
        m_bFullscreen = bEnabled;
        // TODO: フルスクリーンモードの切り替え
    }

    void RenderWorld::SetDebugViewModeAll(DebugViewMode mode)
    {
        auto sceneView = m_RenderingCoordinator.GetMainSceneView();
        if (!sceneView)
        {
            return;
        }

        auto viewport = sceneView->GetMainViewport();
        if (!viewport)
        {
            return;
        }

        viewport->SetDebugViewMode(mode);
    }

    DebugViewMode RenderWorld::GetMainViewportDebugViewMode() const
    {
        auto sceneView = m_RenderingCoordinator.GetMainSceneView();
        if (!sceneView)
        {
            return DebugViewMode::Normal;
        }

        auto viewport = sceneView->GetMainViewport();
        if (!viewport)
        {
            return DebugViewMode::Normal;
        }

        return viewport->GetDebugViewMode();
    }

    DebugViewMode RenderWorld::CycleDebugViewMode()
    {
        const uint8_t count = static_cast<uint8_t>(DebugViewMode::Count);
        uint8_t next = static_cast<uint8_t>(GetMainViewportDebugViewMode());
        if (next >= count)
        {
            next = 0;
        }
        else
        {
            ++next;
            if (next >= count)
            {
                next = 0;
            }
        }

        const DebugViewMode mode = static_cast<DebugViewMode>(next);
        SetDebugViewModeAll(mode);
        return mode;
    }

} // namespace NorvesLib::Core::Rendering
