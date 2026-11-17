// 遠景の雨の層の定数を作り、シーンの色へアルファ合成する描画を記録する。
#include "Rendering/RainCurtainPass.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/EnvironmentGpuParams.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/HeightFogGpuParams.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // rain_curtain.frag と共有する std140 の並び。
        struct alignas(16) GPURainCurtainParams
        {
            float CameraRelativeInverseViewProjection[16];
            float DepthLinearization[4];
            GPUHeightFogParams Fog;
            float LayerDistance[4];
            float LayerOpacity[4];
            float LayerScroll[4];
            float WindShear[4];
        };
        static_assert(sizeof(GPURainCurtainParams) == 240u);
        static_assert(offsetof(GPURainCurtainParams, CameraRelativeInverseViewProjection) == 0u);
        static_assert(offsetof(GPURainCurtainParams, DepthLinearization) == 64u);
        static_assert(offsetof(GPURainCurtainParams, Fog) == 80u);
        static_assert(offsetof(GPURainCurtainParams, LayerDistance) == 176u);
        static_assert(offsetof(GPURainCurtainParams, LayerOpacity) == 192u);
        static_assert(offsetof(GPURainCurtainParams, LayerScroll) == 208u);
        static_assert(offsetof(GPURainCurtainParams, WindShear) == 224u);

        RHI::DescriptorSetDesc CreateRainCurtainDescriptorSetDesc()
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
    }

    RainCurtainPass::~RainCurtainPass()
    {
        Shutdown();
    }

    bool RainCurtainPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("RainCurtainPass", "デバイスかシェーダーマネージャーがありません");
            Shutdown();
            return false;
        }

        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("rain_curtain.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_ERROR("RainCurtainPass", "雨のカーテンのシェーダーを読み込めませんでした");
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
        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateRainCurtainDescriptorSetDesc());
        if (!m_SceneDepthSampler || !m_SkyRadianceSampler || !m_DescriptorSetLayout)
        {
            NORVES_LOG_ERROR("RainCurtainPass", "サンプラーか記述子セットのレイアウトを作れませんでした");
            Shutdown();
            return false;
        }

        m_bInitialized = true;
        return true;
    }

    void RainCurtainPass::Shutdown()
    {
        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();
        m_UnownedDescriptorSets.clear();
        m_UnownedBuffers.clear();
        m_DescriptorSetLayout.reset();
        m_SkyRadianceSampler.reset();
        m_SceneDepthSampler.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_Device = nullptr;
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_Environment = {};
        m_RainTuning = {};
        m_Curtain = {};
        m_Wind = {};
        m_CurrentWidth = 0u;
        m_CurrentHeight = 0u;
        m_CurrentFormat = RHI::Format::UNKNOWN;
        m_FramebufferSceneColorTexture = nullptr;
        m_bInitialized = false;
    }

    void RainCurtainPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void RainCurtainPass::Execute(ViewRenderContext& /*context*/)
    {
    }

    void RainCurtainPass::Declare(RenderGraphBuilder& builder)
    {
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        const SceneProxy* snapshot = context ? context->SnapshotScene : nullptr;
        if (!snapshot)
        {
            return;
        }
        const EnvironmentParameters environment = SanitizeEnvironmentParameters(snapshot->Environment);
        if (!environment.bEnabled || !(environment.RainIntensity > 0.0f))
        {
            return;
        }
        const RainTuning tuning = SanitizeRainTuning(snapshot->RainTuningSnapshot
                                                        ? *snapshot->RainTuningSnapshot
                                                        : MakeDefaultRainTuning());
        const RainCurtainEvaluation curtain = EvaluateRainCurtain(environment.RainIntensity, tuning);
        bool bHasActiveLayer = false;
        for (uint32_t layer = 0u; layer < RainCurtainLayerCount; ++layer)
        {
            if (curtain.LayerOpacity[layer] > 0.0f && tuning.CurtainLayerDistanceMeters[layer] > 0.0f)
            {
                bHasActiveLayer = true;
                break;
            }
        }
        if (!bHasActiveLayer)
        {
            return;
        }
        m_Environment = environment;
        m_RainTuning = tuning;
        m_Curtain = curtain;
        m_Wind = SanitizeWindParameters(snapshot->Wind);

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

        // SSR の出力があれば優先し、無ければ照明のシーンの色へ重ねる。
        RGTextureHandle sceneColorHandle;
        const bool bHasSceneColor =
            builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SSRSceneColor,
                                                sceneColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource) ||
            builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                sceneColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource);
        if (!bHasSceneColor)
        {
            m_SceneColorHandle = {};
            m_SceneDepthHandle = {};
            m_SkyAtmosphereRadianceHandle = {};
            return;
        }
        m_SceneColorHandle = sceneColorHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void RainCurtainPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_SceneColorHandle.IsValid() || !m_SceneDepthHandle.IsValid())
        {
            return;
        }
        const RHI::TexturePtr sceneColorTexture = resources.GetTexture(m_SceneColorHandle);
        const RHI::TexturePtr sceneDepthTexture = resources.GetTexture(m_SceneDepthHandle);
        const RHI::TexturePtr skyRadianceTexture = m_SkyAtmosphereRadianceHandle.IsValid()
                                                     ? resources.GetTexture(m_SkyAtmosphereRadianceHandle)
                                                     : RHI::TexturePtr{};
        if (!PrepareResources(sceneColorTexture) || !sceneDepthTexture || !m_DescriptorSetLayout)
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        const CameraProxy* camera = context.GetActiveCamera();
        if (!camera || camera->Projection != ProjectionType::Perspective)
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }
        const CameraViewConstants cameraConstants = CameraViewConstants::BuildForDevice(
            *camera, context.GetActiveAspectRatio(), context.Device);
        GPURainCurtainParams params{};
        cameraConstants.CopyShaderCameraRelativeInverseViewProjection(params.CameraRelativeInverseViewProjection);
        cameraConstants.CopyDepthLinearization(params.DepthLinearization);
        if (!(params.DepthLinearization[0] > 0.0f))
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }
        params.Fog = BuildHeightFogGpuParams(context, *camera, static_cast<bool>(skyRadianceTexture));

        float fall = EvaluateRainNear(m_Environment.RainIntensity, m_Wind, m_RainTuning).FallSpeedMetersPerSecond;
        if (!(fall > 0.0f))
        {
            fall = 1.0f;
        }
        // ゲームの時刻を使うのでポーズで止まる。double のまま折り返してから float へ移す。
        const float wrapped = static_cast<float>(std::fmod(m_Environment.GameTimeSeconds,
                                                          EnvironmentGameTimeWrapSeconds));
        for (uint32_t layer = 0u; layer < RainCurtainLayerCount; ++layer)
        {
            params.LayerDistance[layer] = m_RainTuning.CurtainLayerDistanceMeters[layer];
            params.LayerOpacity[layer] = m_Curtain.LayerOpacity[layer];
            params.LayerScroll[layer] = wrapped * fall * m_RainTuning.CurtainLayerScrollSpeed[layer];
        }
        // 遠景の層には突風を入れず、平均の水平風速だけで傾きを決める。
        if (m_Wind.bEnabled)
        {
            float dx = 0.0f;
            float dz = 0.0f;
            WindDirectionXZ(m_Wind, dx, dz);
            params.WindShear[0] = dx * m_Wind.BaseSpeed / fall * m_RainTuning.CurtainShearFactor;
            params.WindShear[1] = dz * m_Wind.BaseSpeed / fall * m_RainTuning.CurtainShearFactor;
        }

        RHI::DescriptorSetPtr descriptorSet;
        RHI::BufferPtr paramsBuffer;
        uint32_t paramsOffset = 0u;
        if (FrameResourceLane* frameResources = context.FrameResources)
        {
            const FrameUploadAllocation paramsAllocation = frameResources->AllocateConstants(params);
            if (paramsAllocation.IsValid())
            {
                paramsBuffer = paramsAllocation.Buffer;
                paramsOffset = paramsAllocation.GetOffset32();
            }
            descriptorSet = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout).Set;
        }
        else if (m_Device)
        {
            // 持ち主が無いときも毎回作り、提出済みの定数と記述子セットを上書きしない。
            paramsBuffer = m_Device->CreateBuffer(RHI::BufferDesc(sizeof(GPURainCurtainParams),
                                                                  RHI::ResourceUsage::ConstantBuffer,
                                                                  true,
                                                                  "RainCurtainParams.Unowned"));
            if (paramsBuffer)
            {
                paramsBuffer->Update(&params, sizeof(params));
                m_UnownedBuffers.push_back(paramsBuffer);
            }
            descriptorSet = m_Device->CreateDescriptorSet(CreateRainCurtainDescriptorSetDesc());
            if (descriptorSet)
            {
                m_UnownedDescriptorSets.push_back(descriptorSet);
            }
        }
        if (!paramsBuffer || !descriptorSet)
        {
            NORVES_LOG_ERROR("RainCurtainPass", "定数か記述子セットを借りられないため、このフレームは雨のカーテンを描きません");
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        descriptorSet->BindTexture(0u, sceneDepthTexture);
        descriptorSet->BindSampler(0u, m_SceneDepthSampler);
        descriptorSet->BindTexture(1u, skyRadianceTexture ? skyRadianceTexture : sceneDepthTexture);
        descriptorSet->BindSampler(1u, skyRadianceTexture ? m_SkyRadianceSampler : m_SceneDepthSampler);
        descriptorSet->BindConstantBuffer(2u, paramsBuffer, paramsOffset, sizeof(GPURainCurtainParams));
        descriptorSet->Update();
        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      m_Pipeline,
                                      descriptorSet);
    }

    void RainCurtainPass::EnqueueEmptyNativePass(
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

    bool RainCurtainPass::PrepareResources(const RHI::TexturePtr& sceneColorTexture)
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
            NORVES_LOG_ERROR("RainCurtainPass", "雨のカーテンのレンダーパスを作れませんでした");
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
            NORVES_LOG_ERROR("RainCurtainPass", "雨のカーテンのフレームバッファを作れませんでした");
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
        blendAttachment.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
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
        pipelineDesc.descriptorSetLayouts.push_back(CreateRainCurtainDescriptorSetDesc());
        m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("RainCurtainPass", "雨のカーテンの描画パイプラインを作れませんでした");
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

} // 名前空間 NorvesLib::Core::Rendering
