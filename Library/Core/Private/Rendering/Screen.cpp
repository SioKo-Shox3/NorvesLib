#include "Rendering/Screen.h"
#include "Rendering/View.h"

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

        // TODO: RHI SwapChain作成
        // m_SwapChain = device->CreateSwapChain(settings);

        m_bInitialized = true;
        return true;
    }

    void Screen::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // Viewをクリア
        m_Views.clear();
        m_ViewPriorities.clear();

        // RHIリソース解放
        m_CurrentBackBuffer = nullptr;
        m_SwapChain = nullptr;
        m_Device = nullptr;

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

        // TODO: SwapChainのリサイズ
        // if (m_SwapChain)
        // {
        //     m_SwapChain->Resize(width, height);
        // }

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

    void Screen::BeginFrame()
    {
        // TODO: 次のバックバッファを取得
        // m_CurrentBackBuffer = m_SwapChain->AcquireNextImage();
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

    void Screen::EndFrame()
    {
        // TODO: Present
        // if (m_SwapChain)
        // {
        //     m_SwapChain->Present(m_bVSyncEnabled);
        // }
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
