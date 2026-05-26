#include "Rendering/ForwardPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/TransientResourcePool.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    ForwardPass::ForwardPass(SceneView *sceneView, SceneRenderer *sceneRenderer)
        : m_SceneView(sceneView), m_SceneRenderer(sceneRenderer)
    {
    }

    ForwardPass::~ForwardPass()
    {
        Shutdown();
    }

    bool ForwardPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("ForwardPass", "Device is null");
            return false;
        }

        if (!m_SceneView || !m_SceneRenderer)
        {
            NORVES_LOG_ERROR("ForwardPass", "SceneView or SceneRenderer is null");
            return false;
        }

        // フォワードパスではSceneRendererが保持するDefaultPipelineを使用するため、
        // ここでの追加パイプライン作成は不要
        // 将来的にマテリアルシステム完成後、マテリアル固有パイプラインを解決する

        m_bInitialized = true;
        NORVES_LOG_INFO("ForwardPass", "ForwardPass initialized");
        return true;
    }

    void ForwardPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_ColorTexture = nullptr;
        m_DepthTexture = nullptr;
        m_ForwardRenderPass.reset();
        m_ForwardFramebuffer.reset();

        m_bInitialized = false;
        NORVES_LOG_INFO("ForwardPass", "ForwardPass shutdown");
    }

    void ForwardPass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.RenderWidth;
        uint32_t height = context.RenderHeight;

        if (width == 0 || height == 0)
        {
            return;
        }

        // レンダーパスが外部管理の場合（SwapChainに直接描画）、
        // TransientPoolからの独自レンダーターゲット取得は不要
        if (context.bRenderPassActive)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;
            return;
        }

        // 独自レンダーパスモード: TransientPoolが必要
        if (!context.TransientPool)
        {
            return;
        }

        // フォワード描画のカラー・深度バッファをTransientResourcePoolから取得
        m_ColorTexture = context.TransientPool->AcquireRenderTarget(
            width, height, RHI::Format::R16G16B16A16_FLOAT, "ForwardColor");

        m_DepthTexture = context.TransientPool->AcquireRenderTarget(
            width, height, RHI::Format::D32_FLOAT, "ForwardDepth");

        // サイズ変更があればレンダーパス・フレームバッファを再作成
        if (width != m_CurrentWidth || height != m_CurrentHeight)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // TODO: 独自レンダーパスモード用のリソース作成（TransientPool使用時）
            //
            // RenderPassDesc desc;
            // desc.colorAttachments = { Format::R16G16B16A16_FLOAT };
            // desc.depthAttachment = Format::D32_FLOAT;
            // desc.colorLoadOp = LoadOp::Clear;
            // desc.depthLoadOp = LoadOp::Clear;
            // m_ForwardRenderPass = context.Device->CreateRenderPass(desc);
            //
            // FramebufferDesc fbDesc;
            // fbDesc.renderPass = m_ForwardRenderPass;
            // fbDesc.colorAttachments = { m_ColorTexture };
            // fbDesc.depthAttachment = m_DepthTexture;
            // fbDesc.width = width;
            // fbDesc.height = height;
            // m_ForwardFramebuffer = context.Device->CreateFramebuffer(fbDesc);

            NORVES_LOG_INFO("ForwardPass", "Forward resources resized (%ux%u)", width, height);
        }
    }

    void ForwardPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList || !m_SceneView || !m_SceneRenderer)
        {
            return;
        }

        // DrawCommandの準備（Cull → Batch → GenerateCommands）
        m_SceneView->PrepareDrawCommands();
        const auto &drawCommands = m_SceneView->GetDrawCommands();

        // SharedResourceRegistryに出力を登録（非nullテクスチャのみ）
        if (m_bRegisterOutputs && context.SharedResources)
        {
            if (m_ColorTexture)
            {
                context.SharedResources->RegisterTexture("SceneColor", m_ColorTexture);
            }
            if (m_DepthTexture)
            {
                context.SharedResources->RegisterTexture("SceneDepth", m_DepthTexture);
            }
        }

        if (drawCommands.empty())
        {
            return;
        }

        // フォワード描画の実行
        if (context.bRenderPassActive)
        {
            // レンダーパスが外部管理の場合（SwapChainに直接描画）
            // BeginRenderPass/EndRenderPassは呼ばない
            // ビューポート・シザーも外部で設定済み
            if (m_bTransparentOnly)
            {
                // 半透明のみ（ディファードレンダリングと併用時）
                const auto &transparentCommands = m_SceneView->GetTransparentCommands();
                if (!transparentCommands.empty())
                {
                    m_SceneRenderer->ExecuteDrawCommands(transparentCommands, context.CommandList);
                }
            }
            else
            {
                // 全DrawCommandを実行
                m_SceneRenderer->ExecuteDrawCommands(drawCommands, context.CommandList);
            }
        }
        else
        {
            // 独自レンダーパスモード: 独自レンダーターゲットへの描画
            // TransientPool経由で取得したカラー/深度バッファに描画
            //
            // TODO: TransientResourcePool統合後に実装
            // context.CommandList->BeginRenderPass(m_ForwardRenderPass, m_ForwardFramebuffer);
            //
            // RHI::Viewport viewport;
            // viewport.x = 0.0f;
            // viewport.y = 0.0f;
            // viewport.width = static_cast<float>(m_CurrentWidth);
            // viewport.height = static_cast<float>(m_CurrentHeight);
            // viewport.minDepth = 0.0f;
            // viewport.maxDepth = 1.0f;
            // context.CommandList->SetViewport(viewport);
            //
            // RHI::ScissorRect scissor;
            // scissor.left = 0;
            // scissor.top = 0;
            // scissor.right = static_cast<int32_t>(m_CurrentWidth);
            // scissor.bottom = static_cast<int32_t>(m_CurrentHeight);
            // context.CommandList->SetScissor(scissor);
            //
            // m_SceneRenderer->ExecuteDrawCommands(drawCommands, context.CommandList);
            //
            // context.CommandList->EndRenderPass();
        }
    }

} // namespace NorvesLib::Core::Rendering
