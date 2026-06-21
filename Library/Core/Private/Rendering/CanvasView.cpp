#include "Rendering/CanvasView.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IRenderPass.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"
#include <algorithm>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        class CanvasClearPass final : public IRenderGraphPass
        {
        public:
            const char* GetName() const override
            {
                return "CanvasClearPass";
            }

            bool WasCleared() const
            {
                return m_bCleared;
            }

            RHI::TexturePtr GetOutputTexture() const
            {
                return m_OutputTexture;
            }

            void Declare(RenderGraphBuilder& builder) override
            {
                const ViewRenderContext* context = builder.GetContext();
                const uint32_t width = std::max(1u, context ? context->GetActiveRenderWidth() : 1u);
                const uint32_t height = std::max(1u, context ? context->GetActiveRenderHeight() : 1u);

                m_OutputHandle = builder.WriteTextureAttachment(
                    RenderGraphResourceNames::CanvasColor,
                    RGTextureDesc::RenderTarget(width,
                                                height,
                                                RHI::Format::R8G8B8A8_UNORM,
                                                "Canvas.Color"),
                    RGAttachmentKind::Color,
                    RHI::AttachmentLoadOp::Clear,
                    RHI::AttachmentStoreOp::Store,
                    RHI::ResourceState::RenderTarget,
                    RHI::ResourceState::ShaderResource);
                builder.ExportTexture(RenderGraphResourceNames::CanvasColor, m_OutputHandle);
                builder.PreserveInsertionOrder();
            }

            void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
            {
                m_bCleared = false;
                m_OutputTexture.reset();

                RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
                if (!outputTexture || !context.Device || !context.CommandList)
                {
                    return;
                }

                RHI::RenderPassDesc renderPassDesc;
                RHI::AttachmentDesc colorAttachment;
                colorAttachment.format = outputTexture->GetFormat();
                colorAttachment.isDepthStencil = false;
                colorAttachment.clear = true;
                colorAttachment.clearColor[0] = 0.0f;
                colorAttachment.clearColor[1] = 0.0f;
                colorAttachment.clearColor[2] = 0.0f;
                colorAttachment.clearColor[3] = 0.0f;
                colorAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
                colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
                colorAttachment.initialState = RHI::ResourceState::RenderTarget;
                colorAttachment.finalState = RHI::ResourceState::ShaderResource;
                renderPassDesc.colorAttachments.push_back(colorAttachment);
                renderPassDesc.hasDepthStencil = false;

                RHI::RenderPassPtr renderPass = context.Device->CreateRenderPass(renderPassDesc);
                if (!renderPass)
                {
                    NORVES_LOG_ERROR("CanvasView", "Failed to create canvas clear render pass");
                    return;
                }

                RHI::FramebufferDesc framebufferDesc;
                framebufferDesc.renderPass = renderPass;
                framebufferDesc.colorTargets.push_back(outputTexture);
                framebufferDesc.width = outputTexture->GetWidth();
                framebufferDesc.height = outputTexture->GetHeight();

                RHI::FramebufferPtr framebuffer = context.Device->CreateFramebuffer(framebufferDesc);
                if (!framebuffer)
                {
                    NORVES_LOG_ERROR("CanvasView", "Failed to create canvas clear framebuffer");
                    return;
                }

                context.CommandList->BeginRenderPass(renderPass, framebuffer);
                context.CommandList->SetViewport(context.GetActiveLocalViewport());
                context.CommandList->SetScissor(context.GetActiveLocalScissor());
                context.CommandList->EndRenderPass();

                m_OutputTexture = outputTexture;
                m_bCleared = true;
            }

        private:
            RGTextureHandle m_OutputHandle;
            RHI::TexturePtr m_OutputTexture;
            bool m_bCleared = false;
        };
    } // namespace

    CanvasView::CanvasView() = default;

    CanvasView::~CanvasView() = default;

    bool CanvasView::Initialize(const ViewSettings& settings)
    {
        ViewSettings canvasSettings = settings;
        canvasSettings.Type = ViewType::UI;
        canvasSettings.bClearColor = true;
        canvasSettings.ClearColor[0] = 0.0f;
        canvasSettings.ClearColor[1] = 0.0f;
        canvasSettings.ClearColor[2] = 0.0f;
        canvasSettings.ClearColor[3] = 0.0f;
        canvasSettings.bClearDepth = false;
        return View::Initialize(canvasSettings);
    }

    void CanvasView::Render(ViewRenderContext& context)
    {
        ResetFrameOutput();
        context.CurrentGraphExecutionResult = nullptr;
        context.bPresentationGraphPassHandled = false;

        if (!m_bEnabled || !m_bInitialized)
        {
            return;
        }

        if (!context.Graph)
        {
            NORVES_LOG_ERROR("CanvasView", "RenderGraph context is required");
            return;
        }

        context.Graph->Reset();

        CanvasClearPass clearPass;
        context.Graph->AddPass(&clearPass);

        if (!context.Graph->Compile(context))
        {
            NORVES_LOG_ERROR("CanvasView", "RenderGraph compile failed");
            ResetFrameOutput();
            return;
        }

        RenderGraphExecutionResult executionResult = context.Graph->ExecuteWithResult(context);
        if (!executionResult.bSuccess)
        {
            NORVES_LOG_ERROR("CanvasView",
                             "RenderGraph execution failed after %u pass(es)",
                             context.Graph->GetLastExecutedPassCount());
            ResetFrameOutput();
            return;
        }

        const RenderGraphExecutionResult& lastResult = context.Graph->GetLastExecutionResult();
        if (!clearPass.WasCleared())
        {
            ResetFrameOutput();
            return;
        }

        SetFrameOutputTexture(clearPass.GetOutputTexture());
        context.CurrentGraphExecutionResult = &lastResult;
    }

} // namespace NorvesLib::Core::Rendering
