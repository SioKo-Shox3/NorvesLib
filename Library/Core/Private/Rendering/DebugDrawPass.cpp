#include "Rendering/DebugDrawPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/DebugDrawQueue.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/FramePacket.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"
#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct DebugLineCameraUBO
        {
            float view[16];
            float projection[16];
        };

        constexpr uint32_t DEBUG_LINE_UNIFORM_SLOTS = 32;
        constexpr uint32_t DEBUG_LINE_VERTEX_RING_SLOT_COUNT = FRAME_PACKET_BUFFER_COUNT;
        constexpr uint32_t DEBUG_LINE_INITIAL_VERTEX_CAPACITY = 4096;
        constexpr uint64_t DEBUG_LINE_INITIAL_VERTEX_BYTES =
            static_cast<uint64_t>(DEBUG_LINE_INITIAL_VERTEX_CAPACITY) * sizeof(DebugLineVertex);

        static_assert(sizeof(DebugLineVertex) == sizeof(float) * 7);
        static_assert(offsetof(DebugLineVertex, Position) == 0);
        static_assert(offsetof(DebugLineVertex, Color) == sizeof(float) * 3);

        RHI::DescriptorSetDesc CreateDebugLineDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc descriptorSetDesc;

            RHI::DescriptorBinding uboBinding;
            uboBinding.binding = 0;
            uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uboBinding.stages = RHI::ShaderStage::Vertex;
            descriptorSetDesc.bindings.push_back(uboBinding);

            return descriptorSetDesc;
        }
    } // namespace

    DebugDrawPass::~DebugDrawPass()
    {
        Shutdown();
    }

    bool DebugDrawPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Device is null");
            return false;
        }

        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "ShaderManager is null");
            return false;
        }

        m_Device = context.Device;

        m_LineVertexShader = context.ShaderMgr->LoadShader("line.vert", RHI::ShaderStage::Vertex);
        if (!m_LineVertexShader)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to load line vertex shader");
            m_Device = nullptr;
            return false;
        }

        m_LineFragmentShader = context.ShaderMgr->LoadShader("line.frag", RHI::ShaderStage::Pixel);
        if (!m_LineFragmentShader)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to load line fragment shader");
            m_LineVertexShader.reset();
            m_Device = nullptr;
            return false;
        }

        const RHI::DescriptorSetDesc descriptorSetDesc = CreateDebugLineDescriptorSetDesc();
        if (!m_UniformAllocator.Initialize(m_Device,
                                           sizeof(DebugLineCameraUBO),
                                           DEBUG_LINE_UNIFORM_SLOTS,
                                           descriptorSetDesc))
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to initialize line uniform allocator");
            m_LineVertexShader.reset();
            m_LineFragmentShader.reset();
            m_Device = nullptr;
            return false;
        }

        if (!m_VertexRing.Initialize(m_Device,
                                     DEBUG_LINE_VERTEX_RING_SLOT_COUNT,
                                     RHI::ResourceUsage::VertexBuffer,
                                     DEBUG_LINE_INITIAL_VERTEX_BYTES))
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to initialize line vertex ring");
            m_UniformAllocator.Shutdown();
            m_LineVertexShader.reset();
            m_LineFragmentShader.reset();
            m_Device = nullptr;
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("DebugDrawPass", "DebugDrawPass initialized");
        return true;
    }

    void DebugDrawPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_ToneMappedColorTexture.reset();
        m_SceneDepthTexture.reset();
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_LineVertexShader.reset();
        m_LineFragmentShader.reset();
        m_UniformAllocator.Shutdown();
        m_VertexRing.Shutdown();
        m_ToneMappedColorHandle = {};
        m_SceneDepthHandle = {};
        m_RenderPassSignature = {};
        m_CurrentWidth = 0;
        m_CurrentHeight = 0;
        m_Device = nullptr;
        m_bInitialized = false;

        NORVES_LOG_INFO("DebugDrawPass", "DebugDrawPass shutdown");
    }

    void DebugDrawPass::Setup(ViewRenderContext& context)
    {
        (void)context;
    }

    void DebugDrawPass::Execute(ViewRenderContext& context)
    {
        (void)context;
    }

    void DebugDrawPass::Declare(RenderGraphBuilder& builder)
    {
        m_ToneMappedColorHandle = {};
        m_SceneDepthHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        if (!context ||
            !context->SnapshotDebugLineVertices ||
            context->SnapshotDebugLineVertices->empty() ||
            !context->GetActiveCamera())
        {
            builder.PreserveInsertionOrder();
            return;
        }

        RGTextureHandle availableToneMappedColor;
        RGTextureHandle availableSceneDepth;
        if (!builder.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, availableToneMappedColor) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SceneDepth, availableSceneDepth))
        {
            builder.PreserveInsertionOrder();
            return;
        }

        RGTextureHandle toneMappedColorHandle;
        if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::ToneMappedColor,
                                                toneMappedColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource))
        {
            m_ToneMappedColorHandle = toneMappedColorHandle.ToResourceHandle();
            builder.ExportTexture(RenderGraphResourceNames::ToneMappedColor, toneMappedColorHandle);
        }

        RGTextureHandle sceneDepthHandle;
        if (builder.TryUseAttachment(RenderGraphResourceNames::SceneDepth,
                                     sceneDepthHandle,
                                     RGAttachmentKind::DepthStencil,
                                     RGAttachmentMutability::ReadOnly,
                                     RHI::AttachmentLoadOp::Load,
                                     RHI::AttachmentStoreOp::DontCare,
                                     RHI::ResourceState::DepthRead,
                                     RHI::ResourceState::DepthRead))
        {
            m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();
        }

        builder.PreserveInsertionOrder();
    }

    void DebugDrawPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        const Container::VariableArray<DebugLineVertex>* vertices = context.SnapshotDebugLineVertices;
        if (!vertices || vertices->empty())
        {
            return;
        }

        const CameraProxy* activeCamera = context.GetActiveCamera();
        if (!activeCamera)
        {
            return;
        }

        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("DebugDrawPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        if (!m_ToneMappedColorHandle.IsValid() || !m_SceneDepthHandle.IsValid())
        {
            return;
        }

        RHI::TexturePtr toneMappedColorTexture = resources.GetTexture(m_ToneMappedColorHandle);
        RHI::TexturePtr sceneDepthTexture = resources.GetTexture(m_SceneDepthHandle);
        if (!toneMappedColorTexture || !sceneDepthTexture)
        {
            return;
        }

        if (!PrepareResources(toneMappedColorTexture->GetWidth(),
                              toneMappedColorTexture->GetHeight(),
                              toneMappedColorTexture,
                              sceneDepthTexture))
        {
            return;
        }

        const uint64_t uploadBytes = static_cast<uint64_t>(vertices->size()) * sizeof(DebugLineVertex);
        const uint32_t vertexSlot = context.FrameIndex % DEBUG_LINE_VERTEX_RING_SLOT_COUNT;
        RHI::BufferPtr vertexBuffer = m_VertexRing.Upload(vertexSlot, vertices->data(), uploadBytes);
        if (!vertexBuffer)
        {
            return;
        }

        CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);

        DebugLineCameraUBO uboData{};
        cameraConstants.CopyShaderView(uboData.view);
        cameraConstants.CopyShaderProjection(uboData.projection);

        m_UniformAllocator.Reset();
        DynamicUniformAllocator::Allocation allocation = m_UniformAllocator.Allocate();
        if (!allocation.UniformBuffer || !allocation.DescriptorSet)
        {
            return;
        }

        allocation.UniformBuffer->Update(&uboData, sizeof(DebugLineCameraUBO));

        context.EnqueueFrameCommand(FrameCommand::CreateDebugDrawLineList(
            m_RenderPass,
            m_Framebuffer,
            context.GetActiveLocalViewport(),
            context.GetActiveLocalScissor(),
            m_Pipeline,
            allocation.DescriptorSet,
            vertexBuffer,
            static_cast<uint32_t>(vertices->size())));
    }

    bool DebugDrawPass::PrepareResources(uint32_t width,
                                         uint32_t height,
                                         const RHI::TexturePtr& toneMappedColorTexture,
                                         const RHI::TexturePtr& sceneDepthTexture)
    {
        if (!m_Device ||
            !toneMappedColorTexture ||
            !sceneDepthTexture ||
            !m_LineVertexShader ||
            !m_LineFragmentShader)
        {
            return false;
        }

        const RenderPassSignature signature =
            CreateRenderPassSignature(width, height, toneMappedColorTexture, sceneDepthTexture);
        const bool bResourcesChanged =
            !RenderPassSignatureEquals(m_RenderPassSignature, signature) ||
            !m_RenderPass ||
            !m_Framebuffer ||
            !m_Pipeline;

        m_ToneMappedColorTexture = toneMappedColorTexture;
        m_SceneDepthTexture = sceneDepthTexture;
        if (!bResourcesChanged)
        {
            return true;
        }

        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_RenderPassSignature = {};

        RHI::RenderPassDesc renderPassDesc;

        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = signature.ToneMappedColor.Format;
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = false;
        colorAttachment.loadOp = signature.ToneMappedColor.LoadOp;
        colorAttachment.storeOp = signature.ToneMappedColor.StoreOp;
        colorAttachment.initialState = signature.ToneMappedColor.InitialState;
        colorAttachment.finalState = signature.ToneMappedColor.FinalState;
        renderPassDesc.colorAttachments.push_back(colorAttachment);

        renderPassDesc.hasDepthStencil = true;
        renderPassDesc.depthStencilAttachment.format = signature.SceneDepth.Format;
        renderPassDesc.depthStencilAttachment.isDepthStencil = true;
        renderPassDesc.depthStencilAttachment.clear = false;
        renderPassDesc.depthStencilAttachment.loadOp = signature.SceneDepth.LoadOp;
        renderPassDesc.depthStencilAttachment.storeOp = signature.SceneDepth.StoreOp;
        renderPassDesc.depthStencilAttachment.initialState = signature.SceneDepth.InitialState;
        renderPassDesc.depthStencilAttachment.finalState = signature.SceneDepth.FinalState;

        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to create debug draw render pass");
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        framebufferDesc.colorTargets.push_back(toneMappedColorTexture);
        framebufferDesc.depthStencilTarget = sceneDepthTexture;
        framebufferDesc.width = width;
        framebufferDesc.height = height;

        m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to create debug draw framebuffer");
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_LineVertexShader;
        pipelineDesc.pixelShader = m_LineFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::LineList;

        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(DebugLineVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);

        RHI::VertexAttributeDesc positionAttribute;
        positionAttribute.location = 0;
        positionAttribute.binding = 0;
        positionAttribute.format = RHI::Format::R32G32B32_FLOAT;
        positionAttribute.offset = offsetof(DebugLineVertex, Position);
        pipelineDesc.vertexAttributes.push_back(positionAttribute);

        RHI::VertexAttributeDesc colorAttribute;
        colorAttribute.location = 1;
        colorAttribute.binding = 0;
        colorAttribute.format = RHI::Format::R32G32B32A32_FLOAT;
        colorAttribute.offset = offsetof(DebugLineVertex, Color);
        pipelineDesc.vertexAttributes.push_back(colorAttribute);

        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = false;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;

        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        RHI::DescriptorSetDesc descriptorSetDesc = CreateDebugLineDescriptorSetDesc();
        pipelineDesc.renderPass = m_RenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(descriptorSetDesc);

        m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to create debug draw pipeline");
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_RenderPassSignature = signature;
        return true;
    }

    bool DebugDrawPass::AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                                  const AttachmentSignature& rhs) const
    {
        return lhs.Kind == rhs.Kind &&
               lhs.Format == rhs.Format &&
               lhs.LoadOp == rhs.LoadOp &&
               lhs.StoreOp == rhs.StoreOp &&
               lhs.InitialState == rhs.InitialState &&
               lhs.FinalState == rhs.FinalState &&
               lhs.Target == rhs.Target &&
               lhs.Width == rhs.Width &&
               lhs.Height == rhs.Height &&
               lhs.bDepthReadOnly == rhs.bDepthReadOnly;
    }

    bool DebugDrawPass::RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                                  const RenderPassSignature& rhs) const
    {
        return lhs.bValid == rhs.bValid &&
               AttachmentSignatureEquals(lhs.ToneMappedColor, rhs.ToneMappedColor) &&
               AttachmentSignatureEquals(lhs.SceneDepth, rhs.SceneDepth);
    }

    DebugDrawPass::RenderPassSignature DebugDrawPass::CreateRenderPassSignature(
        uint32_t width,
        uint32_t height,
        const RHI::TexturePtr& toneMappedColorTexture,
        const RHI::TexturePtr& sceneDepthTexture) const
    {
        RenderPassSignature signature;
        signature.bValid = true;
        signature.ToneMappedColor = {RGAttachmentKind::Color,
                                     toneMappedColorTexture ? toneMappedColorTexture->GetFormat() : RHI::Format::UNKNOWN,
                                     RHI::AttachmentLoadOp::Load,
                                     RHI::AttachmentStoreOp::Store,
                                     RHI::ResourceState::RenderTarget,
                                     RHI::ResourceState::ShaderResource,
                                     toneMappedColorTexture.get(),
                                     width,
                                     height,
                                     false};
        signature.SceneDepth = {RGAttachmentKind::DepthStencil,
                                sceneDepthTexture ? sceneDepthTexture->GetFormat() : RHI::Format::UNKNOWN,
                                RHI::AttachmentLoadOp::Load,
                                RHI::AttachmentStoreOp::DontCare,
                                RHI::ResourceState::DepthRead,
                                RHI::ResourceState::DepthRead,
                                sceneDepthTexture.get(),
                                width,
                                height,
                                true};
        return signature;
    }

} // namespace NorvesLib::Core::Rendering
