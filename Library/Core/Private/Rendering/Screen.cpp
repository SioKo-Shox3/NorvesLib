#include "Rendering/Screen.h"
#include "Rendering/View.h"
#include "RHI/IDevice.h"
#include "RHI/ISwapChain.h"
#include "RHI/ICommandList.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    bool Screen::Initialize(Container::TSharedPtr<RHI::IDevice> device, const ScreenSettings &settings)
    {
        if (m_bInitialized)
        {
            return false;
        }

        m_Device = device;
        m_Width = settings.Width;
        m_Height = settings.Height;
        m_bVSyncEnabled = settings.bVSync;
        m_bFullscreen = settings.bFullscreen;

        // SwapChain作成
        if (m_Device && settings.WindowHandle)
        {
            RHI::SwapChainDesc swapChainDesc;
            swapChainDesc.windowHandle = settings.WindowHandle;
            swapChainDesc.width = m_Width;
            swapChainDesc.height = m_Height;
            swapChainDesc.format = RHI::Format::B8G8R8A8_UNORM;
            swapChainDesc.bufferCount = settings.BackBufferCount;
            swapChainDesc.vsync = m_bVSyncEnabled;

            m_SwapChain = m_Device->CreateSwapChain(swapChainDesc);
            if (!m_SwapChain)
            {
                NORVES_LOG_ERROR("Screen", "Failed to create SwapChain");
                return false;
            }

            LOG_INFO("Screen: SwapChain created (%ux%u, %u buffers)",
                     m_SwapChain->GetWidth(), m_SwapChain->GetHeight(), m_SwapChain->GetBufferCount());
        }

        m_bInitialized = true;
        return true;
    }

    void Screen::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // 共有リソースレジストリをクリア（テクスチャ参照をデバイス破棄前に解放）
        m_SharedResourceRegistry.Clear();

        // Viewをクリア
        m_Views.clear();
        m_ViewPriorities.clear();

        // RHIリソース解放
        m_CurrentBackBuffer = nullptr;
        m_SwapChain.reset();
        m_Device.reset();

        m_bInitialized = false;
    }

    void Screen::Resize(uint32_t width, uint32_t height)
    {
        if (width == m_Width && height == m_Height)
        {
            return;
        }

        m_Width = width;
        m_Height = height;

        // SwapChainのリサイズ
        if (m_SwapChain)
        {
            m_SwapChain->Resize(width, height);
        }

        // 登録されているViewにもリサイズを通知
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Resize(width, height);
            }
        }
    }

    void Screen::AddView(Container::TSharedPtr<View> view, int32_t priority)
    {
        if (!view)
        {
            return;
        }

        // 優先度順に挿入位置を検索
        size_t insertIndex = 0;
        for (size_t i = 0; i < m_ViewPriorities.size(); ++i)
        {
            if (priority < m_ViewPriorities[i])
            {
                break;
            }
            insertIndex = i + 1;
        }

        // 挿入
        m_Views.insert(m_Views.begin() + insertIndex, view);
        m_ViewPriorities.insert(m_ViewPriorities.begin() + insertIndex, priority);
    }

    void Screen::RemoveView(Container::TSharedPtr<View> view)
    {
        for (size_t i = 0; i < m_Views.size(); ++i)
        {
            if (m_Views[i] == view)
            {
                m_Views.erase(m_Views.begin() + i);
                m_ViewPriorities.erase(m_ViewPriorities.begin() + i);
                return;
            }
        }
    }

    bool Screen::BeginFrame()
    {
        // フレーム開始時にView間共有リソースをクリア
        m_SharedResourceRegistry.BeginFrame();

        // SwapChainから次のバックバッファを取得
        if (m_SwapChain)
        {
            if (!m_SwapChain->BeginFrame())
            {
                // SwapChainのリクリエーションが必要な場合がある（ウィンドウリサイズ等）
                NORVES_LOG_WARNING("Screen", "SwapChain::BeginFrame() failed - may need recreation");
                return false;
            }
        }

        return true;
    }

    void Screen::CompositeViews()
    {
        // 各Viewの出力をScreenサーフェスに合成
        for (auto &view : m_Views)
        {
            if (view && view->IsEnabled())
            {
                // TODO: Viewの出力テクスチャをScreenに描画
            }
        }
    }

    void Screen::EndFrame(Container::TSharedPtr<RHI::ICommandList> commandList)
    {
        // コマンドリストをサブミットしてPresent
        if (m_SwapChain && commandList)
        {
            m_SwapChain->EndFrame(commandList);
        }
    }

    void Screen::SetVSync(bool bEnabled)
    {
        m_bVSyncEnabled = bEnabled;
        // TODO: SwapChainに反映
    }

    void Screen::SetFullscreen(bool bEnabled)
    {
        m_bFullscreen = bEnabled;
        // TODO: SwapChainに反映
    }

} // namespace NorvesLib::Core::Rendering
