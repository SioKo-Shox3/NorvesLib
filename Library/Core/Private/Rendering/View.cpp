#include "Rendering/View.h"
#include "Rendering/Viewport.h"
#include "Rendering/IViewPass.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ViewRenderContext.h"
#include "Logging/LogMacros.h"
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        void FlushPendingFrameCommands(ViewRenderContext &context)
        {
            if (!context.Renderer || !context.PendingFrameCommands || !context.CommandList)
            {
                return;
            }

            if (context.PendingFrameCommands->empty())
            {
                return;
            }

            context.Renderer->ExecuteFrameCommands(*context.PendingFrameCommands, context.CommandList);
            context.PendingFrameCommands->clear();
        }
    } // namespace


    View::View() = default;

    View::~View() = default;

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

        // ポストプロセススタックの終了処理
        if (m_PostProcessStack)
        {
            m_PostProcessStack->Shutdown();
            m_PostProcessStack.reset();
        }

        // パスチェーンの終了処理
        for (auto &pass : m_Passes)
        {
            if (pass && pass->IsInitialized())
            {
                pass->Shutdown();
            }
        }
        m_Passes.clear();

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

    // ========================================
    // パスチェーン対応Render
    // ========================================

    void View::Render(ViewRenderContext &context)
    {
        if (!m_bEnabled || !m_bInitialized)
        {
            return;
        }

        // パスチェーンが登録されていればパスベースの描画
        if (!m_Passes.empty())
        {
            ExecutePassChain(context);

            // ポストプロセス実行
            if (m_PostProcessStack && m_PostProcessStack->GetPassCount() > 0)
            {
                // 未初期化のPostProcessStackを初期化
                if (!m_PostProcessStack->IsInitialized())
                {
                    m_PostProcessStack->Initialize(context);
                }

                m_PostProcessStack->Setup(context);
                m_PostProcessStack->Execute(context);
            }
        }
        else
        {
            // パス未登録の場合はレガシーRender()にフォールバック
            Render();
        }
    }

    void View::ExecutePassChain(ViewRenderContext &context)
    {
        for (auto &pass : m_Passes)
        {
            if (!pass || !pass->IsEnabled())
            {
                continue;
            }

            // 未初期化のパスを初期化
            if (!pass->IsInitialized())
            {
                if (!pass->Initialize(context))
                {
                    NORVES_LOG_ERROR("View", "Failed to initialize pass: %s", pass->GetName());
                    continue;
                }
            }

            FlushPendingFrameCommands(context);
            // Setup → Execute
            pass->Setup(context);
            pass->Execute(context);
            FlushPendingFrameCommands(context);
        }
    }

    // ========================================
    // パスチェーン管理
    // ========================================

    void View::AddPass(Container::TUniquePtr<IViewPass> pass)
    {
        if (pass)
        {
            NORVES_LOG_INFO("View", "Pass added: %s", pass->GetName());
            m_Passes.push_back(std::move(pass));
        }
    }

    void View::InsertPass(uint32_t index, Container::TUniquePtr<IViewPass> pass)
    {
        if (!pass)
        {
            return;
        }

        if (index >= m_Passes.size())
        {
            m_Passes.push_back(std::move(pass));
        }
        else
        {
            m_Passes.insert(m_Passes.begin() + index, std::move(pass));
        }
    }

    bool View::RemovePass(const char *name)
    {
        for (auto it = m_Passes.begin(); it != m_Passes.end(); ++it)
        {
            if (*it && std::strcmp((*it)->GetName(), name) == 0)
            {
                if ((*it)->IsInitialized())
                {
                    (*it)->Shutdown();
                }
                m_Passes.erase(it);
                NORVES_LOG_INFO("View", "Pass removed: %s", name);
                return true;
            }
        }
        return false;
    }

    IViewPass *View::FindPass(const char *name) const
    {
        for (const auto &pass : m_Passes)
        {
            if (pass && std::strcmp(pass->GetName(), name) == 0)
            {
                return pass.get();
            }
        }
        return nullptr;
    }

    void View::SetPassEnabled(const char *name, bool bEnabled)
    {
        IViewPass *pass = FindPass(name);
        if (pass)
        {
            pass->SetEnabled(bEnabled);
        }
    }

    void View::SetPostProcessStack(Container::TUniquePtr<PostProcessStack> stack)
    {
        m_PostProcessStack = std::move(stack);
    }

} // namespace NorvesLib::Core::Rendering
