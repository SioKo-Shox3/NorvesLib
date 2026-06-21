#include "DummyModule/DummyOverlayPass.h"

#include "Rendering/ViewRenderContext.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IFramebuffer.h"
#include "Logging/LogMacros.h"
#include "CoreTypes.h"

namespace NorvesLib::Modules::Dummy
{
    namespace
    {
        constexpr const char *kLogCategory = "Module";
        constexpr const char *kPassName = "DummyOverlayPass";

        // seam が渡す生ポインタ(IRenderPass*/IFramebuffer*)を、所有を持たない
        // TSharedPtr(=std::shared_ptr の aliasing 構築)へ橋渡しするヘルパ。空の
        // 制御ブロックと別れて参照カウントを持たないため、破棄時に delete されない。
        // RHI の Create*/BeginRenderPass は TSharedPtr を要求するが、seam の生存は
        // RenderingCoordinator が保証するので非所有参照で安全。
        template <typename T>
        Core::Container::TSharedPtr<T> NonOwning(T *raw)
        {
            return Core::Container::TSharedPtr<T>(Core::Container::TSharedPtr<T>{}, raw);
        }
    } // namespace

    const char *DummyOverlayPass::GetName() const
    {
        return kPassName;
    }

    bool DummyOverlayPass::Initialize(Core::Rendering::ViewRenderContext &context)
    {
        // 録画窓内の遅延初期化。seam の OverlayLoadRenderPass に対して単色三角形
        // パイプラインを生成する。失敗時は false を返し seam が当該 overlay を
        // 恒久無効化する(描画素通り)。
        if (!context.Device || !context.ShaderMgr || !context.OverlayLoadRenderPass)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "DummyOverlayPass Initialize skipped: Device/ShaderMgr/OverlayLoadRenderPass null");
            return false;
        }

        // 自己完結シェーダー(頂点バッファ・ディスクリプタ・push constant 不要)。
        RHI::ShaderPtr vertexShader = context.ShaderMgr->LoadShader("triangle.vert", RHI::ShaderStage::Vertex);
        RHI::ShaderPtr pixelShader = context.ShaderMgr->LoadShader("triangle.frag", RHI::ShaderStage::Pixel);
        if (!vertexShader || !pixelShader)
        {
            NORVES_LOG_WARNING(kLogCategory, "DummyOverlayPass Initialize failed: shader load");
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.pixelShader = pixelShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        // load 経路は depth load の render pass。深度テスト/書き込みは無効。
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::BlendAttachmentDesc blend;
        blend.blendEnable = false;
        blend.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blend);

        // seam の load render pass に対してパイプラインを生成(render pass 互換性のため)。
        // 生成は render pass を読むだけで所有しないので非所有参照で足りる。
        pipelineDesc.renderPass = NonOwning(context.OverlayLoadRenderPass);

        m_Pipeline = context.Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING(kLogCategory, "DummyOverlayPass Initialize failed: CreateGraphicsPipeline");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO(kLogCategory, "DummyOverlayPass Initialize: pipeline created");
        return true;
    }

    void DummyOverlayPass::Shutdown()
    {
        m_Pipeline.reset();
        m_bInitialized = false;
        NORVES_LOG_INFO(kLogCategory, "DummyOverlayPass Shutdown");
    }

    void DummyOverlayPass::Setup(Core::Rendering::ViewRenderContext & /*context*/)
    {
        // 一時リソースは使わない。準備フェーズは no-op。
    }

    void DummyOverlayPass::Execute(Core::Rendering::ViewRenderContext &context)
    {
        // seam 外(録画窓内)。CommandList と load render pass/framebuffer が揃わない
        // 場合は何もしない(構造的に no-op)。
        if (!context.CommandList || !context.OverlayLoadRenderPass || !context.OverlayLoadFramebuffer)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "DummyOverlayPass Execute skipped: CommandList/OverlayLoadRenderPass/Framebuffer null");
            return;
        }
        if (!m_Pipeline)
        {
            return;
        }

        RHI::ICommandList *commandList = context.CommandList;

        // 経路依存の load render pass を Begin(Begin/EndRecording は呼ばない)。
        commandList->BeginRenderPass(NonOwning(context.OverlayLoadRenderPass),
                                     NonOwning(context.OverlayLoadFramebuffer));

        // back buffer 全体を覆う viewport。
        const float fbWidth = static_cast<float>(context.OverlayLoadFramebuffer->GetWidth());
        const float fbHeight = static_cast<float>(context.OverlayLoadFramebuffer->GetHeight());

        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = fbWidth;
        viewport.height = fbHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        commandList->SetViewport(viewport);

        // scissor は全面。三角形は NDC 中央付近(triangle.vert: y=-0.5..0.5)に出るので
        // 画面中央に虹色三角形が 1 つ載る。これが overlay 実描画の可視証拠。
        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(fbWidth);
        scissor.bottom = static_cast<int32_t>(fbHeight);
        commandList->SetScissor(scissor);

        commandList->SetPipeline(m_Pipeline);

        // 頂点バッファ不要。triangle.vert が gl_VertexIndex から座標/色を生成する。
        commandList->Draw(3, 0);

        commandList->EndRenderPass();

        NORVES_LOG_INFO(kLogCategory,
                        "DummyOverlayPass Execute: RP=%s, bOverlayComposite=%d, draw issued (3 verts)",
                        context.OverlayLoadRenderPass ? "non-null" : "null",
                        context.bOverlayComposite ? 1 : 0);
    }
} // namespace NorvesLib::Modules::Dummy
