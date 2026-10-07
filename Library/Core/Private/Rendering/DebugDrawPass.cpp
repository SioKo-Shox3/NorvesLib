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
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct DebugLineCameraUBO
        {
            float view[16];
            float projection[16];
            // xy: 描画先（PresentationColor）の画素座標から SceneDepth の画素座標への倍率
            float depthCoordScale[4];
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
            uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(uboBinding);

            RHI::DescriptorBinding sceneDepthBinding;
            sceneDepthBinding.binding = 1;
            sceneDepthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            sceneDepthBinding.stages = RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(sceneDepthBinding);

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

        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_PointSampler = m_Device->CreateSampler(samplerDesc);
        if (!m_PointSampler)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to create scene depth sampler");
            m_LineVertexShader.reset();
            m_LineFragmentShader.reset();
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

        m_PresentationColorTexture.reset();
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_PointSampler.reset();
        m_LineVertexShader.reset();
        m_LineFragmentShader.reset();
        m_UniformAllocator.Shutdown();
        m_VertexRing.Shutdown();
        m_PresentationColorHandle = {};
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
        m_PresentationColorHandle = {};
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

        // Upscale の後の最終解像度の画像へ描く（Upscale が要らないときは ToneMappedColor と同じテクスチャ）。
        RGTextureHandle availablePresentationColor;
        RGTextureHandle availableSceneDepth;
        if (!builder.TryGetTexture(RenderGraphResourceNames::PresentationColor, availablePresentationColor) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SceneDepth, availableSceneDepth))
        {
            builder.PreserveInsertionOrder();
            return;
        }

        RGTextureHandle sceneDepthHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth,
                                    sceneDepthHandle,
                                    RHI::ResourceState::ShaderResource))
        {
            builder.PreserveInsertionOrder();
            return;
        }
        m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();

        RGTextureHandle presentationColorHandle;
        if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::PresentationColor,
                                                presentationColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource))
        {
            m_PresentationColorHandle = presentationColorHandle.ToResourceHandle();
            builder.ExportTexture(RenderGraphResourceNames::PresentationColor, presentationColorHandle);
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

        if (!m_PresentationColorHandle.IsValid() || !m_SceneDepthHandle.IsValid())
        {
            return;
        }

        RHI::TexturePtr presentationColorTexture = resources.GetTexture(m_PresentationColorHandle);
        RHI::TexturePtr sceneDepthTexture = resources.GetTexture(m_SceneDepthHandle);
        if (!presentationColorTexture || !sceneDepthTexture || sceneDepthTexture->GetWidth() == 0 ||
            sceneDepthTexture->GetHeight() == 0)
        {
            return;
        }

        if (!PrepareResources(presentationColorTexture->GetWidth(),
                              presentationColorTexture->GetHeight(),
                              presentationColorTexture))
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

        // デバッグの線は TAA と Upscale の後に描くので、投影のサブピクセルのジッタを外したカメラで描く（線が揺れない）。
        CameraProxy lineCamera = *activeCamera;
        lineCamera.ProjectionJitterNdcX = 0.0f;
        lineCamera.ProjectionJitterNdcY = 0.0f;
        CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(lineCamera, context.GetActiveAspectRatio(), context.Device);

        DebugLineCameraUBO uboData{};
        cameraConstants.CopyShaderView(uboData.view);
        cameraConstants.CopyShaderProjection(uboData.projection);
        // Upscale は内部解像度の画像全体を画面全体へ広げるので、描画先と SceneDepth の画素は大きさの比で対応する。
        const float colorToDepthX = static_cast<float>(sceneDepthTexture->GetWidth()) /
                                    static_cast<float>(presentationColorTexture->GetWidth());
        const float colorToDepthY = static_cast<float>(sceneDepthTexture->GetHeight()) /
                                    static_cast<float>(presentationColorTexture->GetHeight());
        uboData.depthCoordScale[0] = colorToDepthX;
        uboData.depthCoordScale[1] = colorToDepthY;

        m_UniformAllocator.Reset();
        DynamicUniformAllocator::Allocation allocation = m_UniformAllocator.Allocate();
        if (!allocation.UniformBuffer || !allocation.DescriptorSet)
        {
            return;
        }

        allocation.UniformBuffer->Update(&uboData, sizeof(DebugLineCameraUBO));
        allocation.DescriptorSet->BindTexture(1, sceneDepthTexture);
        allocation.DescriptorSet->BindSampler(1, m_PointSampler);
        allocation.DescriptorSet->Update();

        // 内部解像度の Viewport を描画先の解像度へ広げる（同じ解像度なら従来の Viewport のまま）。
        RHI::Viewport viewport = context.GetActiveLocalViewport();
        viewport.x /= colorToDepthX;
        viewport.y /= colorToDepthY;
        viewport.width /= colorToDepthX;
        viewport.height /= colorToDepthY;
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();
        scissor.left = static_cast<int32_t>(std::lround(static_cast<float>(scissor.left) / colorToDepthX));
        scissor.top = static_cast<int32_t>(std::lround(static_cast<float>(scissor.top) / colorToDepthY));
        scissor.right = std::min(static_cast<int32_t>(presentationColorTexture->GetWidth()),
                                 static_cast<int32_t>(std::lround(static_cast<float>(scissor.right) / colorToDepthX)));
        scissor.bottom = std::min(static_cast<int32_t>(presentationColorTexture->GetHeight()),
                                  static_cast<int32_t>(std::lround(static_cast<float>(scissor.bottom) / colorToDepthY)));

        context.EnqueueFrameCommand(FrameCommand::CreateDebugDrawLineList(
            m_RenderPass,
            m_Framebuffer,
            viewport,
            scissor,
            m_Pipeline,
            allocation.DescriptorSet,
            vertexBuffer,
            static_cast<uint32_t>(vertices->size())));
    }

    bool DebugDrawPass::PrepareResources(uint32_t width,
                                         uint32_t height,
                                         const RHI::TexturePtr& presentationColorTexture)
    {
        if (!m_Device ||
            !presentationColorTexture ||
            !m_LineVertexShader ||
            !m_LineFragmentShader)
        {
            return false;
        }

        const RenderPassSignature signature =
            CreateRenderPassSignature(width, height, presentationColorTexture);
        const bool bResourcesChanged =
            !RenderPassSignatureEquals(m_RenderPassSignature, signature) ||
            !m_RenderPass ||
            !m_Framebuffer ||
            !m_Pipeline;

        m_PresentationColorTexture = presentationColorTexture;
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
        colorAttachment.format = signature.PresentationColor.Format;
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = false;
        colorAttachment.loadOp = signature.PresentationColor.LoadOp;
        colorAttachment.storeOp = signature.PresentationColor.StoreOp;
        colorAttachment.initialState = signature.PresentationColor.InitialState;
        colorAttachment.finalState = signature.PresentationColor.FinalState;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;

        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("DebugDrawPass", "Failed to create debug draw render pass");
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        framebufferDesc.colorTargets.push_back(presentationColorTexture);
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

        // 遮蔽は line.frag が SceneDepth を読んで行う（描画先と SceneDepth の解像度が違ってもよい）。
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

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
               AttachmentSignatureEquals(lhs.PresentationColor, rhs.PresentationColor);
    }

    DebugDrawPass::RenderPassSignature DebugDrawPass::CreateRenderPassSignature(
        uint32_t width,
        uint32_t height,
        const RHI::TexturePtr& presentationColorTexture) const
    {
        RenderPassSignature signature;
        signature.bValid = true;
        signature.PresentationColor = {RGAttachmentKind::Color,
                                       presentationColorTexture ? presentationColorTexture->GetFormat()
                                                                : RHI::Format::UNKNOWN,
                                       RHI::AttachmentLoadOp::Load,
                                       RHI::AttachmentStoreOp::Store,
                                       RHI::ResourceState::RenderTarget,
                                       RHI::ResourceState::ShaderResource,
                                       presentationColorTexture.get(),
                                       width,
                                       height,
                                       false};
        return signature;
    }

} // namespace NorvesLib::Core::Rendering
