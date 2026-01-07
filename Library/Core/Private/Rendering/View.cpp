#include "Rendering/View.h"
#include "Rendering/Viewport.h"

namespace NorvesLib::Core::Rendering
{

    bool View::Initialize(const ViewSettings &settings)
    {
        if (m_bInitialized)
        {
            return false;
        }

        m_ViewType = settings.Type;
        m_Width = settings.Width;
        m_Height = settings.Height;
        m_bClearColor = settings.bClearColor;
        m_ClearColor[0] = settings.ClearColor[0];
        m_ClearColor[1] = settings.ClearColor[1];
        m_ClearColor[2] = settings.ClearColor[2];
        m_ClearColor[3] = settings.ClearColor[3];
        m_bClearDepth = settings.bClearDepth;
        m_ClearDepth = settings.ClearDepth;

        // TODO: 出力レンダーターゲット作成

        m_bInitialized = true;
        return true;
    }

    void View::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // Viewportをクリア
        for (auto &viewport : m_Viewports)
        {
            if (viewport)
            {
                viewport->Shutdown();
            }
        }
        m_Viewports.clear();

        // 出力リソース解放
        m_OutputRenderTarget = nullptr;
        m_OutputTexture = nullptr;

        m_bInitialized = false;
    }

    uint32_t View::AddViewport(Container::TSharedPtr<Viewport> viewport)
    {
        if (!viewport)
        {
            return UINT32_MAX;
        }

        uint32_t index = static_cast<uint32_t>(m_Viewports.size());
        m_Viewports.push_back(viewport);
        return index;
    }

    void View::RemoveViewport(uint32_t index)
    {
        if (index < m_Viewports.size())
        {
            m_Viewports.erase(m_Viewports.begin() + index);
        }
    }

    Container::TSharedPtr<Viewport> View::GetViewport(uint32_t index) const
    {
        if (index < m_Viewports.size())
        {
            return m_Viewports[index];
        }
        return nullptr;
    }

    Container::TSharedPtr<Viewport> View::GetMainViewport() const
    {
        if (!m_Viewports.empty())
        {
            return m_Viewports[0];
        }
        return nullptr;
    }

    void View::Render()
    {
        if (!m_bEnabled || !m_bInitialized)
        {
            return;
        }

        // 各Viewportを描画
        for (auto &viewport : m_Viewports)
        {
            if (viewport && viewport->IsEnabled())
            {
                // TODO: Viewportごとの描画
            }
        }

        // Viewportの結果を合成
        CompositeViewports();
    }

    void View::CompositeViewports()
    {
        // 全Viewportの出力を合成して、View出力に書き込み
        for (auto &viewport : m_Viewports)
        {
            if (viewport && viewport->IsEnabled())
            {
                // TODO: Viewportの出力をView出力にブレンド
            }
        }
    }

    void View::Resize(uint32_t width, uint32_t height)
    {
        if (width == m_Width && height == m_Height)
        {
            return;
        }

        m_Width = width;
        m_Height = height;

        // TODO: 出力レンダーターゲットのリサイズ
    }

} // namespace NorvesLib::Core::Rendering
