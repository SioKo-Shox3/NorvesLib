#include "Rendering/VolumetricsPass.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct alignas(16) GPUVolumetricsParams
        {
            float InverseViewProjection[16];
            float CameraPositionAndPreExposure[4];
            float FogParameters[4];
            float FallbackFogColor[4];
        };

        static_assert(sizeof(GPUVolumetricsParams) == 112u);

        RHI::DescriptorSetDesc CreateVolumetricsDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;

            RHI::DescriptorBinding sceneDepthBinding;
            sceneDepthBinding.binding = 0u;
            sceneDepthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            sceneDepthBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(sceneDepthBinding);

            RHI::DescriptorBinding skyRadianceBinding;
            skyRadianceBinding.binding = 1u;
            skyRadianceBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            skyRadianceBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(skyRadianceBinding);

            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = 2u;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(paramsBinding);
            return desc;
        }

        float SafePreExposure(const CameraProxy* camera)
        {
            if (!camera || !std::isfinite(camera->PreExposure) || camera->PreExposure <= 0.0f)
            {
                return 1.0f;
            }
            return std::clamp(camera->PreExposure, 1.0e-6f, 1.0e6f);
        }

        float SafeFogColorChannel(float value)
        {
            return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
        }
    } // namespace

    VolumetricsPass::~VolumetricsPass()
    {
        Shutdown();
    }

    bool VolumetricsPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Device or ShaderManager is null");
            return false;
        }

        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("volumetrics.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to load fullscreen volumetrics shaders");
            Shutdown();
            return false;
        }

        RHI::BufferDesc paramsBufferDesc(sizeof(GPUVolumetricsParams),
                                         RHI::ResourceUsage::ConstantBuffer,
                                         true,
                                         "VolumetricsParams");
        m_ParamsBuffer = m_Device->CreateBuffer(paramsBufferDesc);
        if (!m_ParamsBuffer)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to create parameter buffer");
            Shutdown();
            return false;
        }

        RHI::SamplerDesc depthSamplerDesc;
        depthSamplerDesc.filterMin = RHI::FilterMode::Point;
        depthSamplerDesc.filterMag = RHI::FilterMode::Point;
        depthSamplerDesc.filterMip = RHI::FilterMode::Point;
        depthSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        depthSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        depthSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_SceneDepthSampler = m_Device->CreateSampler(depthSamplerDesc);

        RHI::SamplerDesc skyRadianceSamplerDesc;
        skyRadianceSamplerDesc.filterMin = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.filterMag = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.filterMip = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.addressU = RHI::TextureAddressMode::Wrap;
        skyRadianceSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        skyRadianceSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_SkyRadianceSampler = m_Device->CreateSampler(skyRadianceSamplerDesc);

        m_DescriptorSet = m_Device->CreateDescriptorSet(CreateVolumetricsDescriptorSetDesc());
        if (!m_SceneDepthSampler || !m_SkyRadianceSampler || !m_DescriptorSet)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to create sampler or descriptor set");
            Shutdown();
            return false;
        }

        m_DescriptorSet->BindConstantBuffer(2u,
                                            m_ParamsBuffer,
                                            0u,
                                            sizeof(GPUVolumetricsParams));
        m_bInitialized = true;
        return true;
    }

    void VolumetricsPass::Shutdown()
    {
        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();
        m_DescriptorSet.reset();
        m_SkyRadianceSampler.reset();
        m_SceneDepthSampler.reset();
        m_ParamsBuffer.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_Device = nullptr;
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_CurrentWidth = 0u;
        m_CurrentHeight = 0u;
        m_CurrentFormat = RHI::Format::UNKNOWN;
        m_FramebufferSceneColorTexture = nullptr;
        m_bInitialized = false;
    }

    void VolumetricsPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void VolumetricsPass::Execute(ViewRenderContext& /*context*/)
    {
    }

    void VolumetricsPass::Declare(RenderGraphBuilder& builder)
    {
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        m_FogParameters = context && context->SnapshotScene
                              ? SanitizeVolumetricFogParameters(
                                    context->SnapshotScene->VolumetricFog)
                              : MakeDefaultVolumetricFogParameters();
        if (!m_FogParameters.bEnabled)
        {
            return;
        }

        RGTextureHandle sceneDepthHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth,
                                    sceneDepthHandle,
                                    RHI::ResourceState::ShaderResource))
        {
            return;
        }
        m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();

        RGTextureHandle skyRadianceHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::SkyAtmosphereRadiance,
                                   skyRadianceHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_SkyAtmosphereRadianceHandle = skyRadianceHandle.ToResourceHandle();
        }

        RGTextureHandle sceneColorHandle;
        if (!builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                 sceneColorHandle,
                                                 RHI::AttachmentLoadOp::Load,
                                                 RHI::AttachmentStoreOp::Store,
                                                 RHI::ResourceState::RenderTarget,
                                                 RHI::ResourceState::ShaderResource))
        {
            m_SceneDepthHandle = {};
            m_SkyAtmosphereRadianceHandle = {};
            return;
        }
        m_SceneColorHandle = sceneColorHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void VolumetricsPass::Execute(RenderGraphResources& resources,
                                  ViewRenderContext& context)
    {
        if (!m_FogParameters.bEnabled ||
            !m_SceneColorHandle.IsValid() ||
            !m_SceneDepthHandle.IsValid())
        {
            return;
        }

        RHI::TexturePtr sceneColorTexture = resources.GetTexture(m_SceneColorHandle);
        RHI::TexturePtr sceneDepthTexture = resources.GetTexture(m_SceneDepthHandle);
        RHI::TexturePtr skyRadianceTexture = m_SkyAtmosphereRadianceHandle.IsValid()
                                                 ? resources.GetTexture(m_SkyAtmosphereRadianceHandle)
                                                 : RHI::TexturePtr{};
        if (!sceneColorTexture)
        {
            return;
        }
        if (!PrepareResources(sceneColorTexture))
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }
        if (!sceneDepthTexture || !m_ParamsBuffer || !m_DescriptorSet)
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        const CameraProxy* activeCamera = context.GetActiveCamera();
        if (!activeCamera)
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        GPUVolumetricsParams params{};
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*activeCamera,
                                                context.GetActiveAspectRatio(),
                                                context.Device);
        cameraConstants.CopyShaderInverseViewProjection(params.InverseViewProjection);
        params.CameraPositionAndPreExposure[0] = activeCamera->PositionX;
        params.CameraPositionAndPreExposure[1] = activeCamera->PositionY;
        params.CameraPositionAndPreExposure[2] = activeCamera->PositionZ;
        params.CameraPositionAndPreExposure[3] = SafePreExposure(activeCamera);
        params.FogParameters[0] = m_FogParameters.DensityAtBaseHeight;
        params.FogParameters[1] = m_FogParameters.BaseHeight;
        params.FogParameters[2] = m_FogParameters.HeightFalloffPerUnit;
        params.FogParameters[3] = skyRadianceTexture ? 1.0f : 0.0f;

        const SceneProxy* scene = context.SnapshotScene;
        params.FallbackFogColor[0] = scene ? SafeFogColorChannel(scene->FogColorR) : 0.0f;
        params.FallbackFogColor[1] = scene ? SafeFogColorChannel(scene->FogColorG) : 0.0f;
        params.FallbackFogColor[2] = scene ? SafeFogColorChannel(scene->FogColorB) : 0.0f;
        params.FallbackFogColor[3] = 1.0f;
        m_ParamsBuffer->Update(&params, sizeof(params));

        m_DescriptorSet->BindTexture(0u, sceneDepthTexture);
        m_DescriptorSet->BindSampler(0u, m_SceneDepthSampler);
        m_DescriptorSet->BindTexture(1u,
                                     skyRadianceTexture ? skyRadianceTexture : sceneDepthTexture);
        m_DescriptorSet->BindSampler(1u,
                                     skyRadianceTexture ? m_SkyRadianceSampler : m_SceneDepthSampler);
        m_DescriptorSet->Update();

        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      m_Pipeline,
                                      m_DescriptorSet);
    }

    void VolumetricsPass::EnqueueEmptyNativePass(
        ViewRenderContext& context,
        const RHI::TexturePtr& sceneColorTexture) const
    {
        if (m_RenderPass && m_Framebuffer &&
            m_FramebufferSceneColorTexture == sceneColorTexture.get())
        {
            context.EnqueueFullscreenPass(m_RenderPass,
                                          m_Framebuffer,
                                          context.GetActiveLocalViewport(),
                                          context.GetActiveLocalScissor(),
                                          nullptr,
                                          nullptr);
            return;
        }

        if (sceneColorTexture)
        {
            context.EnqueueTextureBarrier(sceneColorTexture,
                                          RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
        }
    }

    bool VolumetricsPass::PrepareResources(const RHI::TexturePtr& sceneColorTexture)
    {
        if (!m_Device || !sceneColorTexture)
        {
            return false;
        }

        const uint32_t width = sceneColorTexture->GetWidth();
        const uint32_t height = sceneColorTexture->GetHeight();
        const RHI::Format format = sceneColorTexture->GetFormat();
        if (width == 0u || height == 0u)
        {
            return false;
        }

        const bool bResourcesChanged =
            !m_RenderPass ||
            !m_Framebuffer ||
            !m_Pipeline ||
            m_CurrentWidth != width ||
            m_CurrentHeight != height ||
            m_CurrentFormat != format ||
            m_FramebufferSceneColorTexture != sceneColorTexture.get();
        if (!bResourcesChanged)
        {
            return true;
        }

        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();

        RHI::RenderPassDesc renderPassDesc;
        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = format;
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = false;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::RenderTarget;
        colorAttachment.finalState = RHI::ResourceState::ShaderResource;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;
        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to create render pass");
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        framebufferDesc.colorTargets.push_back(sceneColorTexture);
        framebufferDesc.width = width;
        framebufferDesc.height = height;
        m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to create framebuffer");
            m_RenderPass.reset();
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_VertexShader;
        pipelineDesc.pixelShader = m_FragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = RHI::BlendFactor::One;
        blendAttachment.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blendAttachment.colorBlendOp = RHI::BlendOp::Add;
        blendAttachment.srcAlphaBlendFactor = RHI::BlendFactor::Zero;
        blendAttachment.dstAlphaBlendFactor = RHI::BlendFactor::One;
        blendAttachment.alphaBlendOp = RHI::BlendOp::Add;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::R |
                                         RHI::ColorWriteMask::G |
                                         RHI::ColorWriteMask::B;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);
        pipelineDesc.renderPass = m_RenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(CreateVolumetricsDescriptorSetDesc());
        m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to create graphics pipeline");
            m_Framebuffer.reset();
            m_RenderPass.reset();
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_CurrentFormat = format;
        m_FramebufferSceneColorTexture = sceneColorTexture.get();
        return true;
    }
} // namespace NorvesLib::Core::Rendering
