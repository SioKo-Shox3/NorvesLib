#include "Rendering/CompositePass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ITexture.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t MaxRetainedCompositeFrameResources = 8;

        RHI::DescriptorSetDesc CreateCompositeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc descriptorSetDesc;

            RHI::DescriptorBinding sceneBinding;
            sceneBinding.binding = 0;
            sceneBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            sceneBinding.stages = RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(sceneBinding);

            RHI::DescriptorBinding canvasBinding;
            canvasBinding.binding = 1;
            canvasBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            canvasBinding.stages = RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(canvasBinding);

            return descriptorSetDesc;
        }

        RHI::RenderPassPtr CreateCompositeRenderPass(RHI::IDevice* device, RHI::TexturePtr outputTexture)
        {
            if (!device || !outputTexture)
            {
                return nullptr;
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
            return device->CreateRenderPass(renderPassDesc);
        }

        RHI::FramebufferPtr CreateCompositeFramebuffer(RHI::IDevice* device,
                                                       RHI::RenderPassPtr renderPass,
                                                       RHI::TexturePtr outputTexture)
        {
            if (!device || !renderPass || !outputTexture)
            {
                return nullptr;
            }

            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = renderPass;
            framebufferDesc.colorTargets.push_back(outputTexture);
            framebufferDesc.width = outputTexture->GetWidth();
            framebufferDesc.height = outputTexture->GetHeight();
            return device->CreateFramebuffer(framebufferDesc);
        }

        RHI::PipelinePtr CreateCompositePipeline(RHI::IDevice* device,
                                                 RHI::RenderPassPtr renderPass,
                                                 const CompositePassRequest& request)
        {
            if (!device || !renderPass || !request.VertexShader || !request.PixelShader)
            {
                return nullptr;
            }

            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = request.VertexShader;
            pipelineDesc.pixelShader = request.PixelShader;
            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            pipelineDesc.rasterState.lineWidth = 1.0f;
            pipelineDesc.depthStencilState.depthTestEnable = false;
            pipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc blend;
            blend.blendEnable = false;
            blend.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blend);
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.descriptorSetLayouts.push_back(CreateCompositeDescriptorSetDesc());
            return device->CreateGraphicsPipeline(pipelineDesc);
        }
    } // namespace

    void CompositePass::SetRequest(const CompositePassRequest& request)
    {
        m_Request = request;
        ResetResult();
    }

    void CompositePass::ResetResult()
    {
        RetainResultResources();
        m_Result = CompositePassResult{};
        m_SceneHandle = RGResourceHandle{};
        m_CanvasHandle = RGResourceHandle{};
        m_OutputHandle = RGResourceHandle{};
    }

    void CompositePass::ReleaseRetainedResources()
    {
        m_Result = CompositePassResult{};
        m_RetainedFrameResources.clear();
        m_SceneHandle = RGResourceHandle{};
        m_CanvasHandle = RGResourceHandle{};
        m_OutputHandle = RGResourceHandle{};
    }

    void CompositePass::RetainResultResources()
    {
        if (!m_Result.OutputTexture || !m_Result.RenderPass || !m_Result.Framebuffer || !m_Result.Pipeline)
        {
            return;
        }

        RetainedFrameResources resources;
        resources.OutputTexture = m_Result.OutputTexture;
        resources.RenderPass = m_Result.RenderPass;
        resources.Framebuffer = m_Result.Framebuffer;
        resources.Pipeline = m_Result.Pipeline;
        m_RetainedFrameResources.push_back(resources);

        while (m_RetainedFrameResources.size() > MaxRetainedCompositeFrameResources)
        {
            m_RetainedFrameResources.erase(m_RetainedFrameResources.begin());
        }
    }

    void CompositePass::Declare(RenderGraphBuilder& builder)
    {
        ResetResult();

        if (!m_Request.SceneTexture)
        {
            return;
        }

        m_SceneHandle = builder.ImportTexture(m_Request.SceneTexture,
                                              RHI::ResourceState::ShaderResource,
                                              "Composite.SceneInput");
        if (!m_SceneHandle.IsValid())
        {
            return;
        }
        builder.Read(m_SceneHandle, RHI::ResourceState::ShaderResource);

        if (m_Request.CanvasTexture)
        {
            m_CanvasHandle = builder.ImportTexture(m_Request.CanvasTexture,
                                                   RHI::ResourceState::ShaderResource,
                                                   "Composite.CanvasInput");
            if (m_CanvasHandle.IsValid())
            {
                builder.Read(m_CanvasHandle, RHI::ResourceState::ShaderResource);
                builder.PublishTexture(RenderGraphResourceNames::CanvasColor, m_CanvasHandle);
            }
        }

        const bool bCanAlphaOver = m_CanvasHandle.IsValid() &&
                                   m_Request.VertexShader &&
                                   m_Request.PixelShader &&
                                   m_Request.DescriptorSet &&
                                   m_Request.Sampler;
        if (bCanAlphaOver)
        {
            RGTextureHandle outputHandle = builder.WriteTextureAttachment(
                RenderGraphResourceNames::CompositeColor,
                RGTextureDesc::RenderTarget(m_Request.SceneTexture->GetWidth(),
                                            m_Request.SceneTexture->GetHeight(),
                                            m_Request.SceneTexture->GetFormat(),
                                            "Composite.Color"),
                RGAttachmentKind::Color,
                RHI::AttachmentLoadOp::Clear,
                RHI::AttachmentStoreOp::Store,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
            m_OutputHandle = outputHandle.ToResourceHandle();
            builder.ExportTexture(RenderGraphResourceNames::CompositeColor, outputHandle);
            builder.PreserveInsertionOrder();
            return;
        }

        m_OutputHandle = m_SceneHandle;
        builder.PublishTexture(RenderGraphResourceNames::CompositeColor, m_OutputHandle);
        builder.ExportTexture(RenderGraphResourceNames::CompositeColor, m_OutputHandle);
        builder.PreserveInsertionOrder();
    }

    void CompositePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_Result = CompositePassResult{};
        if (!m_OutputHandle.IsValid())
        {
            return;
        }

        m_Result.SceneTexture = resources.GetTexture(m_SceneHandle);
        if (m_CanvasHandle.IsValid())
        {
            m_Result.CanvasTexture = resources.GetTexture(m_CanvasHandle);
        }
        m_Result.OutputTexture = resources.GetTexture(m_OutputHandle);
        m_Result.bImportedCanvas = m_Result.CanvasTexture != nullptr;
        m_Result.bPublishedComposite = m_Result.OutputTexture != nullptr;
        m_Result.bScenePassthrough = m_OutputHandle == m_SceneHandle;

        if (m_Result.bScenePassthrough)
        {
            return;
        }

        if (!m_Result.SceneTexture ||
            !m_Result.CanvasTexture ||
            !m_Result.OutputTexture ||
            !context.Device ||
            !m_Request.DescriptorSet ||
            !m_Request.Sampler)
        {
            return;
        }

        RHI::RenderPassPtr renderPass = CreateCompositeRenderPass(context.Device, m_Result.OutputTexture);
        RHI::FramebufferPtr framebuffer =
            CreateCompositeFramebuffer(context.Device, renderPass, m_Result.OutputTexture);
        RHI::PipelinePtr pipeline = CreateCompositePipeline(context.Device, renderPass, m_Request);
        if (!renderPass || !framebuffer || !pipeline)
        {
            return;
        }

        m_Request.DescriptorSet->BindTexture(0, m_Result.SceneTexture);
        m_Request.DescriptorSet->BindSampler(0, m_Request.Sampler);
        m_Request.DescriptorSet->BindTexture(1, m_Result.CanvasTexture);
        m_Request.DescriptorSet->BindSampler(1, m_Request.Sampler);
        m_Request.DescriptorSet->Update();

        context.EnqueueFullscreenPass(renderPass,
                                      framebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      pipeline,
                                      m_Request.DescriptorSet);

        m_Result.RenderPass = renderPass;
        m_Result.Framebuffer = framebuffer;
        m_Result.Pipeline = pipeline;
    }

} // namespace NorvesLib::Core::Rendering
