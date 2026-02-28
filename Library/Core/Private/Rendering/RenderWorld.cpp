#include "Rendering/RenderWorld.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{

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
        m_bVSyncEnabled = settings.bVSync;
        m_bFullscreen = settings.bFullscreen;
        m_bMultiThreadedRendering = settings.bEnableMultiThreadedRendering;

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
        coordSettings.BackBufferCount = settings.BackBufferCount;
        coordSettings.bVSync = settings.bVSync;
        coordSettings.bEnableMultiThreadedRendering = settings.bEnableMultiThreadedRendering;
        coordSettings.MaxDrawCallsPerFrame = settings.MaxDrawCallsPerFrame;
        coordSettings.bEnableValidation = settings.bEnableValidation;

        if (!m_RenderingCoordinator.Initialize(coordSettings))
        {
            NORVES_LOG_ERROR("Rendering", "Failed to initialize RenderingCoordinator");
            return false;
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

        m_RenderingCoordinator.BeginFrame();
    }

    void RenderWorld::Render()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_RenderingCoordinator.RenderFrame(nullptr);
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
        // TODO: カメラ情報をRenderingCoordinator/SceneViewに反映
        (void)camera;
    }

    void RenderWorld::EndFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_RenderingCoordinator.EndFrame();

        // 統計更新
        m_Stats.FrameNumber++;
    }

    void RenderWorld::WaitForRender()
    {
        if (m_Device)
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

        LOG_INFO("RenderWorld::Resize(%u, %u)", width, height);

        m_Width = width;
        m_Height = height;

        // RenderingCoordinatorにリサイズを委譲
        m_RenderingCoordinator.Resize(width, height);
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

} // namespace NorvesLib::Core::Rendering
