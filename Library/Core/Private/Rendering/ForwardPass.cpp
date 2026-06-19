#include "Rendering/ForwardPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include "Logging/LogMacros.h"
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct TransparentForwardUBO
        {
            float view[16];
            float projection[16];
            float cameraPosition[4];
            float emissiveColor[4];
            float pomParams[4];
        };
    } // namespace

    ForwardPass::ForwardPass(SceneView* sceneView, SceneRenderer* sceneRenderer)
        : m_SceneView(sceneView), m_SceneRenderer(sceneRenderer)
    {
    }

    ForwardPass::~ForwardPass()
    {
        Shutdown();
    }

    bool ForwardPass::Initialize(ViewRenderContext& context)
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

        m_Device = context.Device;

        if (m_bTransparentOnly)
        {
            if (!context.ShaderMgr)
            {
                NORVES_LOG_ERROR("ForwardPass", "ShaderManager is null");
                return false;
            }

            m_TransparentVertexShader = context.ShaderMgr->LoadShader("forward_transparent.vert",
                                                                      RHI::ShaderStage::Vertex);
            if (!m_TransparentVertexShader)
            {
                NORVES_LOG_ERROR("ForwardPass", "Failed to load transparent forward vertex shader");
                return false;
            }

            m_TransparentFragmentShader = context.ShaderMgr->LoadShader("forward_transparent.frag",
                                                                        RHI::ShaderStage::Pixel);
            if (!m_TransparentFragmentShader)
            {
                NORVES_LOG_ERROR("ForwardPass", "Failed to load transparent forward fragment shader");
                return false;
            }

            RHI::DescriptorSetDesc descriptorSetDesc;

            RHI::DescriptorBinding uboBinding;
            uboBinding.binding = 0;
            uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(uboBinding);

            for (uint32_t binding = 1; binding <= 6; ++binding)
            {
                RHI::DescriptorBinding textureBinding;
                textureBinding.binding = binding;
                textureBinding.type = RHI::ResourceBindType::CombinedImageSampler;
                textureBinding.stages = RHI::ShaderStage::Pixel;
                descriptorSetDesc.bindings.push_back(textureBinding);
            }

            RHI::DescriptorBinding instanceBinding;
            instanceBinding.binding = 7;
            instanceBinding.type = RHI::ResourceBindType::StructuredBuffer;
            instanceBinding.stages = RHI::ShaderStage::Vertex;
            descriptorSetDesc.bindings.push_back(instanceBinding);

            constexpr uint32_t UBO_SIZE = sizeof(TransparentForwardUBO);
            constexpr uint32_t MAX_OBJECTS = 256;
            if (!m_UniformAllocator.Initialize(m_Device, UBO_SIZE, MAX_OBJECTS, descriptorSetDesc))
            {
                NORVES_LOG_ERROR("ForwardPass", "Failed to initialize transparent uniform allocator");
                return false;
            }

            auto createDefault1x1Texture = [this](const char* debugName,
                                                  uint8_t r,
                                                  uint8_t g,
                                                  uint8_t b,
                                                  uint8_t a) -> RHI::TexturePtr
            {
                RHI::TextureDesc textureDesc;
                textureDesc.Width = 1;
                textureDesc.Height = 1;
                textureDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
                textureDesc.Usage = RHI::ResourceUsage::ShaderRead;
                textureDesc.DebugName = debugName;

                RHI::TexturePtr texture = m_Device->CreateTexture(textureDesc);
                if (texture)
                {
                    uint8_t pixel[4] = {r, g, b, a};
                    texture->Update(pixel, 4, 4);
                }

                return texture;
            };

            m_DefaultWhiteTexture = createDefault1x1Texture("ForwardDefaultWhite1x1", 255, 255, 255, 255);
            m_DefaultFlatNormalTexture = createDefault1x1Texture("ForwardDefaultFlatNormal1x1", 128, 128, 255, 255);
            m_DefaultBlackTexture = createDefault1x1Texture("ForwardDefaultBlack1x1", 0, 0, 0, 255);
            m_DefaultMidGrayTexture = createDefault1x1Texture("ForwardDefaultMidGray1x1", 128, 128, 128, 255);

            if (!m_DefaultWhiteTexture ||
                !m_DefaultFlatNormalTexture ||
                !m_DefaultBlackTexture ||
                !m_DefaultMidGrayTexture)
            {
                NORVES_LOG_ERROR("ForwardPass", "Failed to create transparent forward fallback textures");
                return false;
            }

            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Anisotropic;
            samplerDesc.filterMag = RHI::FilterMode::Anisotropic;
            samplerDesc.filterMip = RHI::FilterMode::Anisotropic;
            samplerDesc.addressU = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressV = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressW = RHI::TextureAddressMode::Wrap;
            samplerDesc.maxAnisotropy = 4;

            m_DefaultLinearSampler = m_Device->CreateSampler(samplerDesc);
            if (!m_DefaultLinearSampler)
            {
                NORVES_LOG_ERROR("ForwardPass", "Failed to create transparent forward sampler");
                return false;
            }
        }

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
        m_SceneColorTexture.reset();
        m_GBufferDepthTexture.reset();
        m_ForwardRenderPass.reset();
        m_ForwardFramebuffer.reset();
        m_TransparentPipeline.reset();
        m_TransparentVertexShader.reset();
        m_TransparentFragmentShader.reset();
        m_DefaultWhiteTexture.reset();
        m_DefaultFlatNormalTexture.reset();
        m_DefaultBlackTexture.reset();
        m_DefaultMidGrayTexture.reset();
        m_DefaultLinearSampler.reset();
        m_UniformAllocator.Shutdown();
        m_Device = nullptr;
        m_bTransparentRenderPassUsesRenderGraphInitialStates = false;
        m_FramebufferSceneColorTexture = nullptr;
        m_FramebufferGBufferDepthTexture = nullptr;
        m_TransparentRenderPassSignature = {};

        m_bInitialized = false;
        NORVES_LOG_INFO("ForwardPass", "ForwardPass shutdown");
    }

    void ForwardPass::Setup(ViewRenderContext& context)
    {
        const uint32_t width = context.GetActiveRenderWidth();
        const uint32_t height = context.GetActiveRenderHeight();

        if (width == 0 || height == 0)
        {
            return;
        }

        if (m_bTransparentOnly)
        {
            if (!context.SharedResources)
            {
                return;
            }

            RHI::TexturePtr sceneColorTexture = context.SharedResources->GetTexturePtr("SceneColor");
            RHI::TexturePtr gbufferDepthTexture = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            if (!sceneColorTexture || !gbufferDepthTexture)
            {
                return;
            }

            if (PrepareTransparentResources(width, height, sceneColorTexture, gbufferDepthTexture, false))
            {
                NORVES_LOG_INFO("ForwardPass",
                                "Transparent forward resources updated (%ux%u)",
                                width,
                                height);
            }

            return;
        }

        if (context.bRenderPassActive)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;
            return;
        }

        if (!context.TransientPool)
        {
            return;
        }

        m_ColorTexture = context.TransientPool->AcquireRenderTarget(
            width, height, RHI::Format::R16G16B16A16_FLOAT, "ForwardColor");

        m_DepthTexture = context.TransientPool->AcquireDepthStencil(
            width, height, RHI::Format::D32_FLOAT, "ForwardDepth");

        if (width != m_CurrentWidth || height != m_CurrentHeight)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;
            NORVES_LOG_INFO("ForwardPass", "Forward resources resized (%ux%u)", width, height);
        }
    }

    void ForwardPass::Declare(RenderGraphBuilder &builder)
    {
        m_RenderGraphSceneColorHandle = {};
        m_RenderGraphDepthHandle = {};

        if (m_bTransparentOnly)
        {
            RGTextureHandle sceneColorHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                    sceneColorHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_RenderGraphSceneColorHandle = sceneColorHandle.ToResourceHandle();
            }

            RGTextureHandle depthHandle;
            if (builder.TryUseAttachment(RenderGraphResourceNames::SceneDepth,
                                         depthHandle,
                                         RGAttachmentKind::DepthStencil,
                                         RGAttachmentMutability::ReadOnly,
                                         RHI::AttachmentLoadOp::Load,
                                         RHI::AttachmentStoreOp::Store,
                                         RHI::ResourceState::DepthRead,
                                         RHI::ResourceState::DepthRead))
            {
                m_RenderGraphDepthHandle = depthHandle.ToResourceHandle();
            }
            else if (builder.TryUseAttachment(RenderGraphResourceNames::GBufferDepth,
                                              depthHandle,
                                              RGAttachmentKind::DepthStencil,
                                              RGAttachmentMutability::ReadOnly,
                                              RHI::AttachmentLoadOp::Load,
                                              RHI::AttachmentStoreOp::Store,
                                              RHI::ResourceState::DepthRead,
                                              RHI::ResourceState::DepthRead))
            {
                m_RenderGraphDepthHandle = depthHandle.ToResourceHandle();
            }

            builder.PreserveInsertionOrder();
            return;
        }

        builder.PreserveInsertionOrder();
    }

    void ForwardPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bTransparentOnly)
        {
            Setup(context);
            Execute(context);
            return;
        }

        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("ForwardPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr sceneColorTexture;
        RHI::TexturePtr gbufferDepthTexture;

        if (m_RenderGraphSceneColorHandle.IsValid())
        {
            sceneColorTexture = resources.GetTexture(m_RenderGraphSceneColorHandle);
        }

        if (m_RenderGraphDepthHandle.IsValid())
        {
            gbufferDepthTexture = resources.GetTexture(m_RenderGraphDepthHandle);
        }

        if (m_LightingPass)
        {
            const RGResourceHandle sceneColorHandle = m_LightingPass->GetSceneColorHandle();
            if (!sceneColorTexture && sceneColorHandle.IsValid())
            {
                sceneColorTexture = resources.GetTexture(sceneColorHandle);
            }
        }

        if (m_GBufferPass)
        {
            const RGResourceHandle depthHandle = m_GBufferPass->GetDepthHandle();
            if (!gbufferDepthTexture && depthHandle.IsValid())
            {
                gbufferDepthTexture = resources.GetTexture(depthHandle);
            }
        }

        if ((!sceneColorTexture || !gbufferDepthTexture) && context.SharedResources)
        {
            if (!sceneColorTexture)
            {
                sceneColorTexture = context.SharedResources->GetTexturePtr("SceneColor");
            }

            if (!gbufferDepthTexture)
            {
                gbufferDepthTexture = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            }
        }

        if (!sceneColorTexture || !gbufferDepthTexture)
        {
            return;
        }

        if (!PrepareTransparentResources(sceneColorTexture->GetWidth(),
                                         sceneColorTexture->GetHeight(),
                                         sceneColorTexture,
                                         gbufferDepthTexture,
                                         true))
        {
            return;
        }

        RegisterTransparentBridge(context, sceneColorTexture, gbufferDepthTexture);
        ExecuteTransparentCommands(context, true);
    }

    void ForwardPass::Execute(ViewRenderContext& context)
    {
        if (!context.CommandList || !m_SceneRenderer)
        {
            return;
        }

        if (m_bTransparentOnly)
        {
            RegisterTransparentBridge(context, m_SceneColorTexture, m_GBufferDepthTexture);
            ExecuteTransparentCommands(context, false);
            return;
        }

        const DrawCommandView drawCommands = context.GetActiveDrawCommands();

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

        if (context.bRenderPassActive)
        {
            m_SceneRenderer->ExecuteDrawCommands(drawCommands, context.CommandList);
            return;
        }

        // 単独フォワードパイプラインのFrameCommand化は未移行。
    }

    bool ForwardPass::CreateTransparentResources(uint32_t width,
                                                 uint32_t height,
                                                 bool bUseRenderGraphInitialStates)
    {
        if (!m_Device ||
            !m_SceneColorTexture ||
            !m_GBufferDepthTexture ||
            !m_TransparentVertexShader ||
            !m_TransparentFragmentShader)
        {
            return false;
        }

        const RenderPassSignature signature = CreateTransparentRenderPassSignature(width,
                                                                                   height,
                                                                                   bUseRenderGraphInitialStates);

        m_ForwardFramebuffer.reset();
        m_TransparentPipeline.reset();
        m_ForwardRenderPass.reset();
        m_TransparentRenderPassSignature = {};

        RHI::RenderPassDesc renderPassDesc;

        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = signature.SceneColor.Format;
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = false;
        colorAttachment.loadOp = signature.SceneColor.LoadOp;
        colorAttachment.storeOp = signature.SceneColor.StoreOp;
        colorAttachment.initialState = signature.SceneColor.InitialState;
        colorAttachment.finalState = signature.SceneColor.FinalState;
        renderPassDesc.colorAttachments.push_back(colorAttachment);

        renderPassDesc.hasDepthStencil = true;
        renderPassDesc.depthStencilAttachment.format = signature.Depth.Format;
        renderPassDesc.depthStencilAttachment.isDepthStencil = true;
        renderPassDesc.depthStencilAttachment.clear = false;
        renderPassDesc.depthStencilAttachment.loadOp = signature.Depth.LoadOp;
        renderPassDesc.depthStencilAttachment.storeOp = signature.Depth.StoreOp;
        renderPassDesc.depthStencilAttachment.initialState = signature.Depth.InitialState;
        renderPassDesc.depthStencilAttachment.finalState = signature.Depth.FinalState;

        m_ForwardRenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_ForwardRenderPass)
        {
            NORVES_LOG_ERROR("ForwardPass", "Failed to create transparent forward render pass");
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_ForwardRenderPass;
        framebufferDesc.colorTargets.push_back(m_SceneColorTexture);
        framebufferDesc.depthStencilTarget = m_GBufferDepthTexture;
        framebufferDesc.width = width;
        framebufferDesc.height = height;

        m_ForwardFramebuffer = m_Device->CreateFramebuffer(framebufferDesc);
        if (!m_ForwardFramebuffer)
        {
            NORVES_LOG_ERROR("ForwardPass", "Failed to create transparent forward framebuffer");
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_TransparentVertexShader;
        pipelineDesc.pixelShader = m_TransparentFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(Mesh3DVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);

        RHI::VertexAttributeDesc positionAttribute;
        positionAttribute.location = 0;
        positionAttribute.binding = 0;
        positionAttribute.format = RHI::Format::R32G32B32_FLOAT;
        positionAttribute.offset = 0;
        pipelineDesc.vertexAttributes.push_back(positionAttribute);

        RHI::VertexAttributeDesc normalAttribute;
        normalAttribute.location = 1;
        normalAttribute.binding = 0;
        normalAttribute.format = RHI::Format::R32G32B32_FLOAT;
        normalAttribute.offset = sizeof(float) * 3;
        pipelineDesc.vertexAttributes.push_back(normalAttribute);

        RHI::VertexAttributeDesc texCoordAttribute;
        texCoordAttribute.location = 2;
        texCoordAttribute.binding = 0;
        texCoordAttribute.format = RHI::Format::R32G32_FLOAT;
        texCoordAttribute.offset = sizeof(float) * 6;
        pipelineDesc.vertexAttributes.push_back(texCoordAttribute);

        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = false;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;

        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
        blendAttachment.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blendAttachment.colorBlendOp = RHI::BlendOp::Add;
        blendAttachment.srcAlphaBlendFactor = RHI::BlendFactor::One;
        blendAttachment.dstAlphaBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blendAttachment.alphaBlendOp = RHI::BlendOp::Add;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        pipelineDesc.renderPass = m_ForwardRenderPass;

        RHI::DescriptorSetDesc descriptorSetDesc;

        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
        descriptorSetDesc.bindings.push_back(uboBinding);

        for (uint32_t binding = 1; binding <= 6; ++binding)
        {
            RHI::DescriptorBinding textureBinding;
            textureBinding.binding = binding;
            textureBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            textureBinding.stages = RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(textureBinding);
        }

        RHI::DescriptorBinding instanceBinding;
        instanceBinding.binding = 7;
        instanceBinding.type = RHI::ResourceBindType::StructuredBuffer;
        instanceBinding.stages = RHI::ShaderStage::Vertex;
        descriptorSetDesc.bindings.push_back(instanceBinding);

        pipelineDesc.descriptorSetLayouts.push_back(descriptorSetDesc);

        m_TransparentPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_TransparentPipeline)
        {
            NORVES_LOG_ERROR("ForwardPass", "Failed to create transparent forward pipeline");
            return false;
        }

        m_TransparentRenderPassSignature = signature;
        return true;
    }

    bool ForwardPass::AttachmentSignatureEquals(const AttachmentSignature& lhs,
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

    bool ForwardPass::RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                                const RenderPassSignature& rhs) const
    {
        return lhs.bValid == rhs.bValid &&
               AttachmentSignatureEquals(lhs.SceneColor, rhs.SceneColor) &&
               AttachmentSignatureEquals(lhs.Depth, rhs.Depth);
    }

    ForwardPass::RenderPassSignature ForwardPass::CreateTransparentRenderPassSignature(
        uint32_t width,
        uint32_t height,
        bool bUseRenderGraphInitialStates) const
    {
        RenderPassSignature signature;
        signature.bValid = true;
        signature.SceneColor = {RGAttachmentKind::Color,
                                m_SceneColorTexture ? m_SceneColorTexture->GetFormat() : RHI::Format::UNKNOWN,
                                RHI::AttachmentLoadOp::Load,
                                RHI::AttachmentStoreOp::Store,
                                bUseRenderGraphInitialStates ? RHI::ResourceState::RenderTarget : RHI::ResourceState::ShaderResource,
                                RHI::ResourceState::ShaderResource,
                                m_SceneColorTexture.get(),
                                width,
                                height,
                                false};
        signature.Depth = {RGAttachmentKind::DepthStencil,
                           m_GBufferDepthTexture ? m_GBufferDepthTexture->GetFormat() : RHI::Format::UNKNOWN,
                           RHI::AttachmentLoadOp::Load,
                           RHI::AttachmentStoreOp::Store,
                           RHI::ResourceState::DepthRead,
                           RHI::ResourceState::DepthRead,
                           m_GBufferDepthTexture.get(),
                           width,
                           height,
                           true};
        return signature;
    }

    bool ForwardPass::PrepareTransparentResources(uint32_t width,
                                                  uint32_t height,
                                                  const RHI::TexturePtr& sceneColorTexture,
                                                  const RHI::TexturePtr& gbufferDepthTexture,
                                                  bool bUseRenderGraphInitialStates)
    {
        if (!sceneColorTexture || !gbufferDepthTexture)
        {
            return false;
        }

        m_SceneColorTexture = sceneColorTexture;
        m_GBufferDepthTexture = gbufferDepthTexture;

        const RenderPassSignature signature = CreateTransparentRenderPassSignature(width,
                                                                                   height,
                                                                                   bUseRenderGraphInitialStates);
        const bool bResourcesChanged =
            !RenderPassSignatureEquals(m_TransparentRenderPassSignature, signature) ||
            !m_ForwardRenderPass ||
            !m_ForwardFramebuffer ||
            !m_TransparentPipeline;

        if (!bResourcesChanged)
        {
            return true;
        }

        if (!CreateTransparentResources(width, height, bUseRenderGraphInitialStates))
        {
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_bTransparentRenderPassUsesRenderGraphInitialStates = bUseRenderGraphInitialStates;
        m_FramebufferSceneColorTexture = sceneColorTexture.get();
        m_FramebufferGBufferDepthTexture = gbufferDepthTexture.get();
        return true;
    }

    void ForwardPass::ExecuteTransparentCommands(ViewRenderContext& context,
                                                 bool bUseRenderGraphManagedStates)
    {
        const DrawCommandView activeTransparentCommands = context.GetActiveTransparentCommands();
        if (activeTransparentCommands.empty())
        {
            if (bUseRenderGraphManagedStates)
            {
                EnqueueEmptyTransparentPass(context);
            }
            return;
        }

        if (!m_ForwardRenderPass ||
            !m_ForwardFramebuffer ||
            !m_TransparentPipeline ||
            !context.InstanceDataBuffer)
        {
            if (bUseRenderGraphManagedStates)
            {
                EnqueueEmptyTransparentPass(context);
            }
            return;
        }

        const uint64_t instanceDataSize64 = context.InstanceDataBuffer->GetSize();
        if (instanceDataSize64 == 0)
        {
            if (bUseRenderGraphManagedStates)
            {
                EnqueueEmptyTransparentPass(context);
            }
            return;
        }

        auto* materials = context.Resources.Materials;
        auto* textures = context.Resources.Textures;
        auto* meshes = context.Resources.Meshes;
        if (!materials || !textures || !meshes)
        {
            if (bUseRenderGraphManagedStates)
            {
                EnqueueEmptyTransparentPass(context);
            }
            return;
        }

        CameraViewConstants cameraConstants;
        float cameraPosition[4] = {0.0f, 1.5f, 4.0f, 1.0f};

        const CameraProxy *activeCamera = context.GetActiveCamera();
        if (activeCamera)
        {
            cameraConstants =
                CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyCameraPosition(cameraPosition);
        }

        float viewData[16];
        float projectionData[16];
        cameraConstants.CopyShaderView(viewData);
        cameraConstants.CopyShaderProjection(projectionData);

        const uint32_t instanceDataSize = instanceDataSize64 > 0xFFFFFFFFull
                                              ? 0xFFFFFFFFu
                                              : static_cast<uint32_t>(instanceDataSize64);

        m_UniformAllocator.Reset();

        auto transparentCommands = MakeShared<Container::VariableArray<DrawCommand>>();
        transparentCommands->reserve(activeTransparentCommands.size());

        for (const DrawCommand& cmd : activeTransparentCommands)
        {
            auto allocation = m_UniformAllocator.Allocate();
            if (!allocation.UniformBuffer)
            {
                NORVES_LOG_WARNING("ForwardPass",
                                   "Transparent UBO allocation failed, skipping remaining objects");
                break;
            }

            TransparentForwardUBO uboData{};
            std::memcpy(uboData.view, viewData, sizeof(viewData));
            std::memcpy(uboData.projection, projectionData, sizeof(projectionData));
            std::memcpy(uboData.cameraPosition, cameraPosition, sizeof(cameraPosition));

            TextureHandle matAlbedo;
            TextureHandle matNormal;
            TextureHandle matMetallic;
            TextureHandle matRoughness;
            TextureHandle matAO;
            TextureHandle matHeight;

            if (cmd.Draw.MaterialHandle.IsValid())
            {
                const auto* materialData = materials->GetData(cmd.Draw.MaterialHandle);
                if (materialData)
                {
                    matAlbedo = materialData->AlbedoTexture;
                    matNormal = materialData->NormalTexture;
                    matMetallic = materialData->MetallicTexture;
                    matRoughness = materialData->RoughnessTexture;
                    matAO = materialData->AOTexture;
                    matHeight = materialData->HeightTexture;
                }
            }

            allocation.UniformBuffer->Update(&uboData, sizeof(TransparentForwardUBO));

            auto resolveTexture = [&](TextureHandle handle, const RHI::TexturePtr& defaultTexture) -> RHI::TexturePtr
            {
                if (handle.IsValid())
                {
                    RHI::TexturePtr texture = textures->GetRHITexturePtr(handle);
                    if (texture)
                    {
                        return texture;
                    }
                }

                return defaultTexture;
            };

            allocation.DescriptorSet->BindTexture(1, resolveTexture(matAlbedo, m_DefaultWhiteTexture));
            allocation.DescriptorSet->BindSampler(1, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(2, resolveTexture(matNormal, m_DefaultFlatNormalTexture));
            allocation.DescriptorSet->BindSampler(2, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(3, resolveTexture(matMetallic, m_DefaultBlackTexture));
            allocation.DescriptorSet->BindSampler(3, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(4, resolveTexture(matRoughness, m_DefaultMidGrayTexture));
            allocation.DescriptorSet->BindSampler(4, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(5, resolveTexture(matAO, m_DefaultWhiteTexture));
            allocation.DescriptorSet->BindSampler(5, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(6, resolveTexture(matHeight, m_DefaultBlackTexture));
            allocation.DescriptorSet->BindSampler(6, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindStorageBuffer(7,
                                                        context.InstanceDataBuffer,
                                                        0,
                                                        instanceDataSize);
            allocation.DescriptorSet->Update();

            DrawCommand drawCommand = cmd;
            drawCommand.Pipeline = m_TransparentPipeline;
            drawCommand.DescriptorSet = allocation.DescriptorSet;
            drawCommand.DescriptorSetSlot = 0;
            transparentCommands->push_back(drawCommand);
        }

        if (transparentCommands->empty())
        {
            if (bUseRenderGraphManagedStates)
            {
                EnqueueEmptyTransparentPass(context);
            }
            return;
        }

        if (!bUseRenderGraphManagedStates)
        {
            context.EnqueueTextureBarrier(m_GBufferDepthTexture,
                                          RHI::ResourceState::ShaderResource,
                                          RHI::ResourceState::DepthRead);
        }

        context.EnqueueFrameCommand(FrameCommand::CreateGeometryPass(m_ForwardRenderPass,
                                                                     m_ForwardFramebuffer,
                                                                     transparentCommands,
                                                                     context.GetActiveLocalViewport(),
                                                                     context.GetActiveLocalScissor(),
                                                                     meshes));

        if (!bUseRenderGraphManagedStates)
        {
            context.EnqueueTextureBarrier(m_GBufferDepthTexture,
                                          RHI::ResourceState::DepthRead,
                                          RHI::ResourceState::ShaderResource);
        }
    }

    void ForwardPass::EnqueueEmptyTransparentPass(ViewRenderContext& context) const
    {
        if (!m_ForwardRenderPass || !m_ForwardFramebuffer)
        {
            return;
        }

        context.EnqueueFrameCommand(FrameCommand::CreateGeometryPass(
            m_ForwardRenderPass,
            m_ForwardFramebuffer,
            MakeShared<Container::VariableArray<DrawCommand>>(),
            context.GetActiveLocalViewport(),
            context.GetActiveLocalScissor(),
            context.Resources.Meshes));
    }

    void ForwardPass::RegisterTransparentBridge(ViewRenderContext& context,
                                                const RHI::TexturePtr& sceneColorTexture,
                                                const RHI::TexturePtr& gbufferDepthTexture) const
    {
        if (!m_bRegisterOutputs || !context.SharedResources)
        {
            return;
        }

        if (sceneColorTexture)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", sceneColorTexture);
        }

        if (gbufferDepthTexture)
        {
            context.SharedResources->RegisterTexturePtr("GBuffer_Depth", gbufferDepthTexture);
            context.SharedResources->RegisterTexturePtr("SceneDepth", gbufferDepthTexture);
        }
    }

} // namespace NorvesLib::Core::Rendering
