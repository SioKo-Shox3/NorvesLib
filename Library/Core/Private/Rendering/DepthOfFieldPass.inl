// ラスタの被写界深度（DepthOfFieldPass）の実装。Coreの登録済みのSceneView.cppから取り込む。
#include "Rendering/DepthOfFieldPass.h"
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
    namespace DepthOfFieldPassDetail
    {
        struct alignas(16) GPUDepthOfFieldParams
        {
            float InverseViewProjection[16];
            // xyz: カメラ位置、w: ピント距離（m）
            float CameraPositionAndFocusDistance[4];
            // xyz: 視線方向、w: 入力画像の画素でのCoCの直径の係数（CocScale × FilmScale）
            float CameraForwardAndCocScale[4];
            // xy: 画像の寸法、z: CoCの半径の上限（入力画像の画素）、w: 最終画像の拡大の倍率（FilmScale、gatherが使う）
            float ImageSizeAndLimits[4];
        };
        static_assert(sizeof(GPUDepthOfFieldParams) == 112u);

        constexpr uint32_t ParamsBinding = 2u;

        RHI::DescriptorSetDesc CreateGatherDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            RHI::DescriptorBinding sceneColorBinding;
            sceneColorBinding.binding = 0u;
            sceneColorBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            sceneColorBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(sceneColorBinding);

            RHI::DescriptorBinding sceneDepthBinding;
            sceneDepthBinding.binding = 1u;
            sceneDepthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            sceneDepthBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(sceneDepthBinding);

            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = ParamsBinding;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(paramsBinding);
            return desc;
        }

        RHI::DescriptorSetDesc CreateResampleDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            RHI::DescriptorBinding gatherBinding;
            gatherBinding.binding = 0u;
            gatherBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            gatherBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(gatherBinding);

            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = ParamsBinding;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(paramsBinding);
            return desc;
        }

        RHI::SamplerPtr CreateClampSampler(RHI::IDevice& device, RHI::FilterMode filter)
        {
            RHI::SamplerDesc desc;
            desc.filterMin = filter;
            desc.filterMag = filter;
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

        // 被写界深度を掛けられるカメラと表示か（ピント距離0のピンホールと検証表示では働かない）。
        const CameraProxy* FindDepthOfFieldCamera(const ViewRenderContext& context)
        {
            const CameraProxy* camera = context.GetActiveCamera();
            if (!camera || context.GetActiveDebugMode() != DebugViewMode::Normal ||
                !BuildDepthOfFieldLens(*camera, 1u).bEnabled)
            {
                return nullptr;
            }
            return camera;
        }
    } // namespace DepthOfFieldPassDetail

    DepthOfFieldPass::~DepthOfFieldPass()
    {
        Shutdown();
    }

    bool DepthOfFieldPass::Initialize(ViewRenderContext& context)
    {
        using namespace DepthOfFieldPassDetail;
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("DepthOfFieldPass", "Device or ShaderManager is null");
            return false;
        }
        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_GatherShader = context.ShaderMgr->LoadShader("DepthOfField.frag", RHI::ShaderStage::Pixel);
        m_ResampleShader =
            context.ShaderMgr->LoadShader("DepthOfFieldResample.frag", RHI::ShaderStage::Pixel);
        RHI::BufferDesc paramsDesc(sizeof(GPUDepthOfFieldParams), RHI::ResourceUsage::ConstantBuffer,
                                   true, "DepthOfFieldParams");
        m_ParamsBuffer = m_VertexShader && m_GatherShader && m_ResampleShader
                             ? m_Device->CreateBuffer(paramsDesc)
                             : RHI::BufferPtr{};
        m_PointSampler = CreateClampSampler(*m_Device, RHI::FilterMode::Point);
        m_LinearSampler = CreateClampSampler(*m_Device, RHI::FilterMode::Linear);
        m_GatherDescriptorSet = m_Device->CreateDescriptorSet(CreateGatherDescriptorSetDesc());
        m_ResampleDescriptorSet = m_Device->CreateDescriptorSet(CreateResampleDescriptorSetDesc());
        if (!m_ParamsBuffer || !m_PointSampler || !m_LinearSampler || !m_GatherDescriptorSet ||
            !m_ResampleDescriptorSet)
        {
            NORVES_LOG_ERROR("DepthOfFieldPass", "Failed to create shaders, samplers or descriptor sets");
            Shutdown();
            return false;
        }
        m_GatherDescriptorSet->BindConstantBuffer(ParamsBinding, m_ParamsBuffer, 0u,
                                                  sizeof(GPUDepthOfFieldParams));
        m_ResampleDescriptorSet->BindConstantBuffer(ParamsBinding, m_ParamsBuffer, 0u,
                                                    sizeof(GPUDepthOfFieldParams));
        m_bInitialized = true;
        return true;
    }

    void DepthOfFieldPass::Shutdown()
    {
        m_ResamplePipeline.reset();
        m_ResampleFramebuffer.reset();
        m_ResampleRenderPass.reset();
        m_GatherPipeline.reset();
        m_GatherFramebuffer.reset();
        m_GatherRenderPass.reset();
        m_GatherTexture.reset();
        m_ResampleDescriptorSet.reset();
        m_GatherDescriptorSet.reset();
        m_LinearSampler.reset();
        m_PointSampler.reset();
        m_ParamsBuffer.reset();
        m_ResampleShader.reset();
        m_GatherShader.reset();
        m_VertexShader.reset();
        m_Device = nullptr;
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_CurrentWidth = 0u;
        m_CurrentHeight = 0u;
        m_CurrentFormat = RHI::Format::UNKNOWN;
        m_FramebufferSceneColorTexture = nullptr;
        m_bInitialized = false;
    }

    void DepthOfFieldPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void DepthOfFieldPass::Execute(ViewRenderContext& /*context*/)
    {
    }

    void DepthOfFieldPass::Declare(RenderGraphBuilder& builder)
    {
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        const ViewRenderContext* context = builder.GetContext();
        if (!m_bEnabled || !context || !DepthOfFieldPassDetail::FindDepthOfFieldCamera(*context))
        {
            return;
        }
        RGTextureHandle sceneDepthHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth, sceneDepthHandle,
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
        m_SceneColorHandle = sceneColorHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void DepthOfFieldPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_SceneColorHandle.IsValid() || !m_SceneDepthHandle.IsValid())
        {
            return;
        }
        const RHI::TexturePtr sceneColor = resources.GetTexture(m_SceneColorHandle);
        const RHI::TexturePtr sceneDepth = resources.GetTexture(m_SceneDepthHandle);
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
        Apply(context, sceneColor, sceneDepth);
    }

    bool DepthOfFieldPass::Apply(ViewRenderContext& context,
                                 const RHI::TexturePtr& sceneColor,
                                 const RHI::TexturePtr& sceneDepth)
    {
        using namespace DepthOfFieldPassDetail;
        if (!sceneColor)
        {
            return false;
        }
        const CameraProxy* camera = FindDepthOfFieldCamera(context);
        const DepthOfFieldLens lens =
            camera ? BuildDepthOfFieldLens(*camera, sceneColor->GetHeight()) : DepthOfFieldLens{};
        if (!m_bInitialized || !lens.bEnabled || !sceneDepth ||
            sceneDepth->GetWidth() != sceneColor->GetWidth() ||
            sceneDepth->GetHeight() != sceneColor->GetHeight() || !PrepareResources(sceneColor))
        {
            context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
            return false;
        }

        GPUDepthOfFieldParams params{};
        const CameraViewConstants cameraConstants = CameraViewConstants::BuildForDevice(
            *camera, context.GetActiveAspectRatio(), context.Device);
        cameraConstants.CopyShaderInverseViewProjection(params.InverseViewProjection);
        cameraConstants.CopyCameraPosition(params.CameraPositionAndFocusDistance, 3u);
        params.CameraPositionAndFocusDistance[3] = lens.FocusDistance;
        float forward[3] = {camera->ForwardX, camera->ForwardY, camera->ForwardZ};
        if (!PathTracingCameraDetail::Normalize(forward))
        {
            context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
            return false;
        }
        params.CameraForwardAndCocScale[0] = forward[0];
        params.CameraForwardAndCocScale[1] = forward[1];
        params.CameraForwardAndCocScale[2] = forward[2];
        // 入力画像はピントの倍率で拡大する前の画角なので、出力画素のCoCをFilmScale倍して使う。
        params.CameraForwardAndCocScale[3] = lens.CocScale * lens.FilmScale;
        params.ImageSizeAndLimits[0] = static_cast<float>(sceneColor->GetWidth());
        params.ImageSizeAndLimits[1] = static_cast<float>(sceneColor->GetHeight());
        params.ImageSizeAndLimits[2] = DepthOfFieldMaxCocRadiusPixels;
        params.ImageSizeAndLimits[3] = lens.FilmScale;
        m_ParamsBuffer->Update(&params, sizeof(params));

        m_GatherDescriptorSet->BindTexture(0u, sceneColor);
        m_GatherDescriptorSet->BindSampler(0u, m_PointSampler);
        m_GatherDescriptorSet->BindTexture(1u, sceneDepth);
        m_GatherDescriptorSet->BindSampler(1u, m_PointSampler);
        m_GatherDescriptorSet->Update();
        m_ResampleDescriptorSet->BindTexture(0u, m_GatherTexture);
        m_ResampleDescriptorSet->BindSampler(0u, m_LinearSampler);
        m_ResampleDescriptorSet->Update();

        const RHI::Viewport viewport = context.GetActiveLocalViewport();
        const RHI::ScissorRect scissor = context.GetActiveLocalScissor();
        // 1. SceneColorと深度から、PTと同じ倍率で拡大しながらぼかした色を中間textureへ書く
        //    （中間の前の内容は捨てる）。
        context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                      RHI::ResourceState::ShaderResource);
        context.EnqueueTextureBarrier(m_GatherTexture, RHI::ResourceState::Undefined,
                                      RHI::ResourceState::RenderTarget);
        context.EnqueueFullscreenPass(m_GatherRenderPass, m_GatherFramebuffer, viewport, scissor,
                                      m_GatherPipeline, m_GatherDescriptorSet);
        // 2. SceneColorへそのまま書き戻す。
        context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::ShaderResource,
                                      RHI::ResourceState::RenderTarget);
        context.EnqueueFullscreenPass(m_ResampleRenderPass, m_ResampleFramebuffer, viewport,
                                      scissor, m_ResamplePipeline, m_ResampleDescriptorSet);
        return true;
    }

    bool DepthOfFieldPass::PrepareResources(const RHI::TexturePtr& sceneColor)
    {
        using namespace DepthOfFieldPassDetail;
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
            m_GatherPipeline.reset();
            m_GatherFramebuffer.reset();
            m_GatherRenderPass.reset();
            m_GatherTexture.reset();
            m_ResamplePipeline.reset();
            m_ResampleFramebuffer.reset();
            m_ResampleRenderPass.reset();
            m_FramebufferSceneColorTexture = nullptr;
            m_CurrentWidth = 0u;
            m_CurrentHeight = 0u;
            m_CurrentFormat = RHI::Format::UNKNOWN;

            m_GatherTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, format, "DepthOfField.Gather"));
            m_GatherRenderPass = m_GatherTexture ? CreateOverwriteRenderPass(*m_Device, format)
                                                 : RHI::RenderPassPtr{};
            if (!m_GatherRenderPass)
            {
                NORVES_LOG_ERROR("DepthOfFieldPass", "Failed to create gather texture or render pass");
                m_GatherTexture.reset();
                return false;
            }
            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = m_GatherRenderPass;
            framebufferDesc.colorTargets.push_back(m_GatherTexture);
            framebufferDesc.width = width;
            framebufferDesc.height = height;
            m_GatherFramebuffer = m_Device->CreateFramebuffer(framebufferDesc);
            m_GatherPipeline = m_GatherFramebuffer
                                   ? CreateFullscreenPipeline(*m_Device, m_VertexShader,
                                                              m_GatherShader, m_GatherRenderPass,
                                                              CreateGatherDescriptorSetDesc())
                                   : RHI::PipelinePtr{};
            m_ResampleRenderPass = CreateOverwriteRenderPass(*m_Device, format);
            m_ResamplePipeline = m_ResampleRenderPass
                                     ? CreateFullscreenPipeline(*m_Device, m_VertexShader,
                                                                m_ResampleShader,
                                                                m_ResampleRenderPass,
                                                                CreateResampleDescriptorSetDesc())
                                     : RHI::PipelinePtr{};
            if (!m_GatherPipeline || !m_ResamplePipeline)
            {
                NORVES_LOG_ERROR("DepthOfFieldPass", "Failed to create framebuffer or pipelines");
                m_GatherPipeline.reset();
                m_GatherFramebuffer.reset();
                m_GatherRenderPass.reset();
                m_GatherTexture.reset();
                m_ResamplePipeline.reset();
                m_ResampleRenderPass.reset();
                return false;
            }
            m_CurrentWidth = width;
            m_CurrentHeight = height;
            m_CurrentFormat = format;
        }
        if (m_FramebufferSceneColorTexture != sceneColor.get() || !m_ResampleFramebuffer)
        {
            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = m_ResampleRenderPass;
            framebufferDesc.colorTargets.push_back(sceneColor);
            framebufferDesc.width = width;
            framebufferDesc.height = height;
            m_ResampleFramebuffer = m_Device->CreateFramebuffer(framebufferDesc);
            m_FramebufferSceneColorTexture = m_ResampleFramebuffer ? sceneColor.get() : nullptr;
            if (!m_ResampleFramebuffer)
            {
                NORVES_LOG_ERROR("DepthOfFieldPass", "Failed to create scene color framebuffer");
                return false;
            }
        }
        return true;
    }
} // namespace NorvesLib::Core::Rendering
