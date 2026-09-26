// ラスタの動きぼけ（MotionBlurPass）の実装。Coreの登録済みのSceneView.cppから取り込む。
#include "Rendering/MotionBlurPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace MotionBlurPassDetail
    {
        struct alignas(16) GPUMotionBlurParams
        {
            float InverseViewProjection[16];
            float PreviousView[16];
            float PreviousProjection[16];
            // xyz: カメラ位置、w: 前のカメラがあるか（1/0）
            float CameraPositionAndHistory[4];
            // xy: 画像の寸法、z: シャッター時間/フレーム長、w: 動きの長さの上限（画素）
            float ImageSizeAndShutter[4];
            // x: tileの一辺（画素）、yz: tileの数
            float TileInfo[4];
        };
        static_assert(sizeof(GPUMotionBlurParams) == 240u);

        constexpr uint32_t ParamsBinding = 4u;
        constexpr RHI::Format TileMaxFormat = RHI::Format::R16G16B16A16_FLOAT;

        RHI::DescriptorSetDesc CreateDescriptorSetDesc(uint32_t textureCount)
        {
            RHI::DescriptorSetDesc desc;
            for (uint32_t binding = 0u; binding < textureCount; ++binding)
            {
                RHI::DescriptorBinding textureBinding;
                textureBinding.binding = binding;
                textureBinding.type = RHI::ResourceBindType::CombinedImageSampler;
                textureBinding.stages = RHI::ShaderStage::Pixel;
                desc.bindings.push_back(textureBinding);
            }
            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = ParamsBinding;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(paramsBinding);
            return desc;
        }

        // tile: velocityと深度。gather: SceneColor・深度・velocity・tile。書き戻し: gatherの結果。
        RHI::DescriptorSetDesc CreateTileMaxDescriptorSetDesc() { return CreateDescriptorSetDesc(2u); }
        RHI::DescriptorSetDesc CreateGatherDescriptorSetDesc() { return CreateDescriptorSetDesc(4u); }
        RHI::DescriptorSetDesc CreateResolveDescriptorSetDesc() { return CreateDescriptorSetDesc(1u); }

        RHI::SamplerPtr CreatePointSampler(RHI::IDevice& device)
        {
            RHI::SamplerDesc desc;
            desc.filterMin = RHI::FilterMode::Point;
            desc.filterMag = RHI::FilterMode::Point;
            desc.filterMip = RHI::FilterMode::Point;
            desc.addressU = RHI::TextureAddressMode::Clamp;
            desc.addressV = RHI::TextureAddressMode::Clamp;
            desc.addressW = RHI::TextureAddressMode::Clamp;
            return device.CreateSampler(desc);
        }

        // 全画素を上書きするfullscreen passのrender pass（読み込まず、ShaderResourceで終える）。
        RHI::RenderPassPtr CreateOverwriteRenderPass(RHI::IDevice& device, RHI::Format format)
        {
            RHI::RenderPassDesc desc;
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = format;
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = false;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::DontCare;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            desc.colorAttachments.push_back(colorAttachment);
            desc.hasDepthStencil = false;
            return device.CreateRenderPass(desc);
        }

        RHI::PipelinePtr CreateFullscreenPipeline(RHI::IDevice& device,
                                                  const RHI::ShaderPtr& vertexShader,
                                                  const RHI::ShaderPtr& pixelShader,
                                                  const RHI::RenderPassPtr& renderPass,
                                                  const RHI::DescriptorSetDesc& layout)
        {
            RHI::GraphicsPipelineDesc desc;
            desc.vertexShader = vertexShader;
            desc.pixelShader = pixelShader;
            desc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            desc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            desc.rasterState.cullMode = RHI::CullMode::None;
            desc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            desc.rasterState.lineWidth = 1.0f;
            desc.depthStencilState.depthTestEnable = false;
            desc.depthStencilState.depthWriteEnable = false;
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::R | RHI::ColorWriteMask::G |
                                             RHI::ColorWriteMask::B | RHI::ColorWriteMask::A;
            desc.blendState.attachments.push_back(blendAttachment);
            desc.renderPass = renderPass;
            desc.descriptorSetLayouts.push_back(layout);
            return device.CreateGraphicsPipeline(desc);
        }

        RHI::FramebufferPtr CreateFramebuffer(RHI::IDevice& device, const RHI::RenderPassPtr& renderPass,
                                              const RHI::TexturePtr& target)
        {
            RHI::FramebufferDesc desc;
            desc.renderPass = renderPass;
            desc.colorTargets.push_back(target);
            desc.width = target->GetWidth();
            desc.height = target->GetHeight();
            return device.CreateFramebuffer(desc);
        }

        uint32_t TileCount(uint32_t pixels)
        {
            return (pixels + MotionBlurTileSize - 1u) / MotionBlurTileSize;
        }

        // 動きぼけを掛けられるカメラと表示か（前のカメラが無い、検証表示中、シャッター0では働かない）。
        bool IsMotionBlurActive(const ViewRenderContext& context, const MotionBlurSettings& settings)
        {
            return context.GetActiveCamera() != nullptr && context.GetPreviousCamera() != nullptr &&
                   context.GetActiveDebugMode() == DebugViewMode::Normal &&
                   ComputeMotionBlurShutterFraction(settings) > 0.0f;
        }
    } // namespace MotionBlurPassDetail

    MotionBlurPass::~MotionBlurPass()
    {
        Shutdown();
    }

    bool MotionBlurPass::Initialize(ViewRenderContext& context)
    {
        using namespace MotionBlurPassDetail;
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("MotionBlurPass", "Device or ShaderManager is null");
            return false;
        }
        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_TileMaxShader = context.ShaderMgr->LoadShader("MotionBlurTileMax.frag", RHI::ShaderStage::Pixel);
        m_GatherShader = context.ShaderMgr->LoadShader("MotionBlur.frag", RHI::ShaderStage::Pixel);
        m_ResolveShader = context.ShaderMgr->LoadShader("MotionBlurResolve.frag", RHI::ShaderStage::Pixel);
        RHI::BufferDesc paramsDesc(sizeof(GPUMotionBlurParams), RHI::ResourceUsage::ConstantBuffer,
                                   true, "MotionBlurParams");
        m_ParamsBuffer = m_VertexShader && m_TileMaxShader && m_GatherShader && m_ResolveShader
                             ? m_Device->CreateBuffer(paramsDesc)
                             : RHI::BufferPtr{};
        m_PointSampler = CreatePointSampler(*m_Device);
        m_TileMaxDescriptorSet = m_Device->CreateDescriptorSet(CreateTileMaxDescriptorSetDesc());
        m_GatherDescriptorSet = m_Device->CreateDescriptorSet(CreateGatherDescriptorSetDesc());
        m_ResolveDescriptorSet = m_Device->CreateDescriptorSet(CreateResolveDescriptorSetDesc());
        if (!m_ParamsBuffer || !m_PointSampler || !m_TileMaxDescriptorSet || !m_GatherDescriptorSet ||
            !m_ResolveDescriptorSet)
        {
            NORVES_LOG_ERROR("MotionBlurPass", "Failed to create shaders, samplers or descriptor sets");
            Shutdown();
            return false;
        }
        for (const RHI::DescriptorSetPtr& descriptorSet :
             {m_TileMaxDescriptorSet, m_GatherDescriptorSet, m_ResolveDescriptorSet})
        {
            descriptorSet->BindConstantBuffer(ParamsBinding, m_ParamsBuffer, 0u,
                                              sizeof(GPUMotionBlurParams));
        }
        m_bInitialized = true;
        return true;
    }

    void MotionBlurPass::Shutdown()
    {
        m_ResolvePipeline.reset();
        m_ResolveFramebuffer.reset();
        m_ResolveRenderPass.reset();
        m_GatherPipeline.reset();
        m_GatherFramebuffer.reset();
        m_GatherRenderPass.reset();
        m_GatherTexture.reset();
        m_TileMaxPipeline.reset();
        m_TileMaxFramebuffer.reset();
        m_TileMaxRenderPass.reset();
        m_TileMaxTexture.reset();
        m_ResolveDescriptorSet.reset();
        m_GatherDescriptorSet.reset();
        m_TileMaxDescriptorSet.reset();
        m_PointSampler.reset();
        m_ParamsBuffer.reset();
        m_ResolveShader.reset();
        m_GatherShader.reset();
        m_TileMaxShader.reset();
        m_VertexShader.reset();
        m_Device = nullptr;
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_VelocityHandle = {};
        m_CurrentWidth = 0u;
        m_CurrentHeight = 0u;
        m_CurrentFormat = RHI::Format::UNKNOWN;
        m_FramebufferSceneColorTexture = nullptr;
        m_bInitialized = false;
    }

    void MotionBlurPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void MotionBlurPass::Execute(ViewRenderContext& /*context*/)
    {
    }

    void MotionBlurPass::Declare(RenderGraphBuilder& builder)
    {
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_VelocityHandle = {};
        const ViewRenderContext* context = builder.GetContext();
        if (!m_bEnabled || !context || !MotionBlurPassDetail::IsMotionBlurActive(*context, m_Settings))
        {
            return;
        }
        RGTextureHandle sceneDepthHandle;
        RGTextureHandle velocityHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth, sceneDepthHandle,
                                    RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::GBufferVelocity, velocityHandle,
                                    RHI::ResourceState::ShaderResource))
        {
            return;
        }
        RGTextureHandle sceneColorHandle;
        if (!builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                 sceneColorHandle,
                                                 RHI::AttachmentLoadOp::Load,
                                                 RHI::AttachmentStoreOp::Store,
                                                 RHI::ResourceState::RenderTarget,
                                                 RHI::ResourceState::ShaderResource))
        {
            return;
        }
        m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();
        m_VelocityHandle = velocityHandle.ToResourceHandle();
        m_SceneColorHandle = sceneColorHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void MotionBlurPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_SceneColorHandle.IsValid() || !m_SceneDepthHandle.IsValid() || !m_VelocityHandle.IsValid())
        {
            return;
        }
        const RHI::TexturePtr sceneColor = resources.GetTexture(m_SceneColorHandle);
        const RHI::TexturePtr sceneDepth = resources.GetTexture(m_SceneDepthHandle);
        const RHI::TexturePtr velocity = resources.GetTexture(m_VelocityHandle);
        if (!sceneColor)
        {
            return;
        }
        if (!m_bInitialized && !Initialize(context))
        {
            context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
            return;
        }
        Apply(context, sceneColor, sceneDepth, velocity);
    }

    bool MotionBlurPass::Apply(ViewRenderContext& context,
                               const RHI::TexturePtr& sceneColor,
                               const RHI::TexturePtr& sceneDepth,
                               const RHI::TexturePtr& velocity)
    {
        using namespace MotionBlurPassDetail;
        if (!sceneColor)
        {
            return false;
        }
        const float shutterFraction = ComputeMotionBlurShutterFraction(m_Settings);
        const CameraProxy* camera = context.GetActiveCamera();
        const CameraProxy* previousCamera = context.GetPreviousCamera();
        const auto sameSize = [&](const RHI::TexturePtr& texture)
        {
            return texture && texture->GetWidth() == sceneColor->GetWidth() &&
                   texture->GetHeight() == sceneColor->GetHeight();
        };
        if (!m_bInitialized || !IsMotionBlurActive(context, m_Settings) || !camera || !previousCamera ||
            !sameSize(sceneDepth) || !sameSize(velocity) || !PrepareResources(sceneColor))
        {
            context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
            return false;
        }

        GPUMotionBlurParams params{};
        const float aspect = context.GetActiveAspectRatio();
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*camera, aspect, context.Device);
        const CameraViewConstants previousConstants =
            CameraViewConstants::BuildForDevice(*previousCamera, aspect, context.Device);
        cameraConstants.CopyShaderInverseViewProjection(params.InverseViewProjection);
        previousConstants.CopyShaderView(params.PreviousView);
        previousConstants.CopyShaderProjection(params.PreviousProjection);
        cameraConstants.CopyCameraPosition(params.CameraPositionAndHistory, 3u);
        params.CameraPositionAndHistory[3] = 1.0f;
        params.ImageSizeAndShutter[0] = static_cast<float>(sceneColor->GetWidth());
        params.ImageSizeAndShutter[1] = static_cast<float>(sceneColor->GetHeight());
        params.ImageSizeAndShutter[2] = shutterFraction;
        params.ImageSizeAndShutter[3] = MotionBlurMaxLengthPixels;
        params.TileInfo[0] = static_cast<float>(MotionBlurTileSize);
        params.TileInfo[1] = static_cast<float>(m_TileMaxTexture->GetWidth());
        params.TileInfo[2] = static_cast<float>(m_TileMaxTexture->GetHeight());
        m_ParamsBuffer->Update(&params, sizeof(params));

        m_TileMaxDescriptorSet->BindTexture(0u, velocity);
        m_TileMaxDescriptorSet->BindSampler(0u, m_PointSampler);
        m_TileMaxDescriptorSet->BindTexture(1u, sceneDepth);
        m_TileMaxDescriptorSet->BindSampler(1u, m_PointSampler);
        m_TileMaxDescriptorSet->Update();
        m_GatherDescriptorSet->BindTexture(0u, velocity);
        m_GatherDescriptorSet->BindSampler(0u, m_PointSampler);
        m_GatherDescriptorSet->BindTexture(1u, sceneDepth);
        m_GatherDescriptorSet->BindSampler(1u, m_PointSampler);
        m_GatherDescriptorSet->BindTexture(2u, sceneColor);
        m_GatherDescriptorSet->BindSampler(2u, m_PointSampler);
        m_GatherDescriptorSet->BindTexture(3u, m_TileMaxTexture);
        m_GatherDescriptorSet->BindSampler(3u, m_PointSampler);
        m_GatherDescriptorSet->Update();
        m_ResolveDescriptorSet->BindTexture(0u, m_GatherTexture);
        m_ResolveDescriptorSet->BindSampler(0u, m_PointSampler);
        m_ResolveDescriptorSet->Update();

        const RHI::Viewport viewport = context.GetActiveLocalViewport();
        const RHI::ScissorRect scissor = context.GetActiveLocalScissor();
        RHI::Viewport tileViewport;
        tileViewport.width = static_cast<float>(m_TileMaxTexture->GetWidth());
        tileViewport.height = static_cast<float>(m_TileMaxTexture->GetHeight());
        RHI::ScissorRect tileScissor;
        tileScissor.right = static_cast<int32_t>(m_TileMaxTexture->GetWidth());
        tileScissor.bottom = static_cast<int32_t>(m_TileMaxTexture->GetHeight());
        // 1. tileごとに最も長い動きを求める（前の内容は捨てる）。
        context.EnqueueTextureBarrier(m_TileMaxTexture, RHI::ResourceState::Undefined,
                                      RHI::ResourceState::RenderTarget);
        context.EnqueueFullscreenPass(m_TileMaxRenderPass, m_TileMaxFramebuffer, tileViewport, tileScissor,
                                      m_TileMaxPipeline, m_TileMaxDescriptorSet);
        // 2. SceneColor・深度・velocity・tileから、ぼかした色を中間textureへ書く。
        context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                      RHI::ResourceState::ShaderResource);
        context.EnqueueTextureBarrier(m_GatherTexture, RHI::ResourceState::Undefined,
                                      RHI::ResourceState::RenderTarget);
        context.EnqueueFullscreenPass(m_GatherRenderPass, m_GatherFramebuffer, viewport, scissor,
                                      m_GatherPipeline, m_GatherDescriptorSet);
        // 3. SceneColorへそのまま書き戻す。
        context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::ShaderResource,
                                      RHI::ResourceState::RenderTarget);
        context.EnqueueFullscreenPass(m_ResolveRenderPass, m_ResolveFramebuffer, viewport, scissor,
                                      m_ResolvePipeline, m_ResolveDescriptorSet);
        return true;
    }

    bool MotionBlurPass::PrepareResources(const RHI::TexturePtr& sceneColor)
    {
        using namespace MotionBlurPassDetail;
        const uint32_t width = sceneColor->GetWidth();
        const uint32_t height = sceneColor->GetHeight();
        const RHI::Format format = sceneColor->GetFormat();
        if (!m_Device || width == 0u || height == 0u)
        {
            return false;
        }
        const bool bSizeChanged = !m_GatherTexture || m_CurrentWidth != width ||
                                  m_CurrentHeight != height || m_CurrentFormat != format;
        if (bSizeChanged)
        {
            m_TileMaxPipeline.reset();
            m_TileMaxFramebuffer.reset();
            m_TileMaxRenderPass.reset();
            m_TileMaxTexture.reset();
            m_GatherPipeline.reset();
            m_GatherFramebuffer.reset();
            m_GatherRenderPass.reset();
            m_GatherTexture.reset();
            m_ResolvePipeline.reset();
            m_ResolveFramebuffer.reset();
            m_ResolveRenderPass.reset();
            m_FramebufferSceneColorTexture = nullptr;
            m_CurrentWidth = 0u;
            m_CurrentHeight = 0u;
            m_CurrentFormat = RHI::Format::UNKNOWN;

            m_TileMaxTexture = m_Device->CreateTexture(RHI::TextureDesc::RenderTarget(
                TileCount(width), TileCount(height), TileMaxFormat, "MotionBlur.TileMax"));
            m_GatherTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, format, "MotionBlur.Gather"));
            m_TileMaxRenderPass = m_TileMaxTexture ? CreateOverwriteRenderPass(*m_Device, TileMaxFormat)
                                                   : RHI::RenderPassPtr{};
            m_GatherRenderPass = m_GatherTexture ? CreateOverwriteRenderPass(*m_Device, format)
                                                 : RHI::RenderPassPtr{};
            m_ResolveRenderPass = CreateOverwriteRenderPass(*m_Device, format);
            m_TileMaxFramebuffer = m_TileMaxRenderPass
                                       ? CreateFramebuffer(*m_Device, m_TileMaxRenderPass, m_TileMaxTexture)
                                       : RHI::FramebufferPtr{};
            m_GatherFramebuffer = m_GatherRenderPass
                                      ? CreateFramebuffer(*m_Device, m_GatherRenderPass, m_GatherTexture)
                                      : RHI::FramebufferPtr{};
            m_TileMaxPipeline = m_TileMaxFramebuffer
                                    ? CreateFullscreenPipeline(*m_Device, m_VertexShader, m_TileMaxShader,
                                                               m_TileMaxRenderPass,
                                                               CreateTileMaxDescriptorSetDesc())
                                    : RHI::PipelinePtr{};
            m_GatherPipeline = m_GatherFramebuffer
                                   ? CreateFullscreenPipeline(*m_Device, m_VertexShader, m_GatherShader,
                                                              m_GatherRenderPass,
                                                              CreateGatherDescriptorSetDesc())
                                   : RHI::PipelinePtr{};
            m_ResolvePipeline = m_ResolveRenderPass
                                    ? CreateFullscreenPipeline(*m_Device, m_VertexShader, m_ResolveShader,
                                                               m_ResolveRenderPass,
                                                               CreateResolveDescriptorSetDesc())
                                    : RHI::PipelinePtr{};
            if (!m_TileMaxPipeline || !m_GatherPipeline || !m_ResolvePipeline)
            {
                NORVES_LOG_ERROR("MotionBlurPass", "Failed to create textures, framebuffers or pipelines");
                m_TileMaxPipeline.reset();
                m_TileMaxFramebuffer.reset();
                m_TileMaxRenderPass.reset();
                m_TileMaxTexture.reset();
                m_GatherPipeline.reset();
                m_GatherFramebuffer.reset();
                m_GatherRenderPass.reset();
                m_GatherTexture.reset();
                m_ResolvePipeline.reset();
                m_ResolveRenderPass.reset();
                return false;
            }
            m_CurrentWidth = width;
            m_CurrentHeight = height;
            m_CurrentFormat = format;
        }
        if (m_FramebufferSceneColorTexture != sceneColor.get() || !m_ResolveFramebuffer)
        {
            m_ResolveFramebuffer = CreateFramebuffer(*m_Device, m_ResolveRenderPass, sceneColor);
            m_FramebufferSceneColorTexture = m_ResolveFramebuffer ? sceneColor.get() : nullptr;
            if (!m_ResolveFramebuffer)
            {
                NORVES_LOG_ERROR("MotionBlurPass", "Failed to create scene color framebuffer");
                return false;
            }
        }
        return true;
    }
} // namespace NorvesLib::Core::Rendering
