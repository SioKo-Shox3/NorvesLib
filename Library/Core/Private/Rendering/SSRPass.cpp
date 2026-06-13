#include "Rendering/SSRPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/CameraViewConstants.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    // SSRパラメータUBO = mat4 x4 + vec4 + 7floats + 1uint + pad = 304 bytes
    // projection(64) + invProjection(64) + view(64) + invView(64) + screenSize(16) + 7*4 + 4 + 4pad = 320
    static constexpr uint32_t SSR_PARAMS_SIZE = 320;

    struct GPUSSRParams
    {
        float projection[16];
        float invProjection[16];
        float view[16];
        float invView[16];
        float screenSize[4];
        float maxDistance;
        float thickness;
        float maxSteps;
        float fadeStart;
        float fadeEnd;
        float roughnessCutoff;
        float intensity;
        uint32_t bEnabled;
    };

    SSRPass::SSRPass(const SSRSettings &settings)
        : m_Settings(settings)
    {
    }

    SSRPass::~SSRPass()
    {
        if (m_bInitialized)
        {
            Shutdown();
        }
    }

    bool SSRPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("SSRPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("SSRPass", "ShaderManager is null");
            return false;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_VertexShader)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to load fullscreen vertex shader");
            return false;
        }

        m_FragmentShader = context.ShaderMgr->LoadShader("ssr.frag", RHI::ShaderStage::Pixel);
        if (!m_FragmentShader)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to load SSR fragment shader");
            return false;
        }

        // リニアサンプラー
        {
            RHI::SamplerDesc desc;
            desc.filterMin = RHI::FilterMode::Linear;
            desc.filterMag = RHI::FilterMode::Linear;
            desc.filterMip = RHI::FilterMode::Linear;
            desc.addressU = RHI::TextureAddressMode::Clamp;
            desc.addressV = RHI::TextureAddressMode::Clamp;
            desc.addressW = RHI::TextureAddressMode::Clamp;
            m_LinearSampler = m_Device->CreateSampler(desc);
        }

        // ポイントサンプラー（深度用）
        {
            RHI::SamplerDesc desc;
            desc.filterMin = RHI::FilterMode::Point;
            desc.filterMag = RHI::FilterMode::Point;
            desc.filterMip = RHI::FilterMode::Point;
            desc.addressU = RHI::TextureAddressMode::Clamp;
            desc.addressV = RHI::TextureAddressMode::Clamp;
            desc.addressW = RHI::TextureAddressMode::Clamp;
            m_PointSampler = m_Device->CreateSampler(desc);
        }

        if (!m_LinearSampler || !m_PointSampler)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to create samplers");
            return false;
        }

        // パラメータUBO
        RHI::BufferDesc paramsUboDesc(
            SSR_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "SSRParamsUBO");
        m_ParamsBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_ParamsBuffer)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to create params buffer");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("SSRPass", "SSRPass initialized");
        return true;
    }

    void SSRPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_OutputTexture.reset();
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_ParamsBuffer.reset();
        m_DescriptorSet.reset();
        m_LinearSampler.reset();
        m_PointSampler.reset();
        m_Device = nullptr;
        m_OutputHandle = {};
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferOutputTexture = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("SSRPass", "SSRPass shutdown");
    }

    void SSRPass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.GetActiveRenderWidth();
        uint32_t height = context.GetActiveRenderHeight();

        if (width == 0 || height == 0)
        {
            return;
        }

        const bool bNeedsOutputTexture =
            !m_OutputTexture ||
            width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            m_bRenderPassUsesRenderGraphInitialState;

        if (bNeedsOutputTexture)
        {
            // 出力テクスチャ（HDRフォーマット）
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSROutput"));

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("SSRPass", "Failed to create output texture");
                return;
            }
        }

        const bool bNeedsPrepare =
            bNeedsOutputTexture ||
            !m_RenderPass ||
            !m_Framebuffer ||
            !m_Pipeline ||
            !m_DescriptorSet ||
            m_FramebufferOutputTexture != m_OutputTexture.get() ||
            m_bRenderPassUsesRenderGraphInitialState;

        if (bNeedsPrepare)
        {
            if (PrepareResources(width, height, m_OutputTexture, false))
            {
                NORVES_LOG_INFO("SSRPass", "Resources created");
            }
        }
    }

    bool SSRPass::PrepareResources(uint32_t width,
                                   uint32_t height,
                                   const RHI::TexturePtr &outputTexture,
                                   bool bUseRenderGraphInitialState)
    {
        if (!m_Device ||
            !outputTexture ||
            !m_VertexShader ||
            !m_FragmentShader ||
            !m_ParamsBuffer)
        {
            return false;
        }

        const bool bResourcesChanged =
            width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            outputTexture.get() != m_FramebufferOutputTexture ||
            bUseRenderGraphInitialState != m_bRenderPassUsesRenderGraphInitialState ||
            !m_RenderPass ||
            !m_Framebuffer ||
            !m_Pipeline ||
            !m_DescriptorSet;

        m_OutputTexture = outputTexture;
        if (!bResourcesChanged)
        {
            return true;
        }

        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_DescriptorSet.reset();

        RHI::RenderPassDesc rpDesc;
        RHI::AttachmentDesc colorAttach;
        colorAttach.format = outputTexture->GetFormat();
        colorAttach.isDepthStencil = false;
        colorAttach.clear = false;
        colorAttach.loadOp = RHI::AttachmentLoadOp::DontCare;
        colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttach.initialState = bUseRenderGraphInitialState
                                       ? RHI::ResourceState::RenderTarget
                                       : RHI::ResourceState::Undefined;
        colorAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(colorAttach);
        rpDesc.hasDepthStencil = false;

        m_RenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to create render pass");
            return false;
        }

        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_RenderPass;
        fbDesc.colorTargets.push_back(outputTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_Framebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to create framebuffer");
            return false;
        }

        RHI::DescriptorSetDesc dsDesc;

        for (uint32_t i = 0; i < 4; ++i)
        {
            RHI::DescriptorBinding binding;
            binding.binding = i;
            binding.type = RHI::ResourceBindType::CombinedImageSampler;
            binding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(binding);
        }

        {
            RHI::DescriptorBinding binding;
            binding.binding = 4;
            binding.type = RHI::ResourceBindType::ConstantBuffer;
            binding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(binding);
        }

        {
            RHI::DescriptorBinding binding;
            binding.binding = 5;
            binding.type = RHI::ResourceBindType::CombinedImageSampler;
            binding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(binding);
        }

        m_DescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!m_DescriptorSet)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to create descriptor set");
            return false;
        }

        m_DescriptorSet->BindConstantBuffer(4, m_ParamsBuffer, 0, SSR_PARAMS_SIZE);

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
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        pipelineDesc.renderPass = m_RenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to create pipeline");
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_FramebufferOutputTexture = outputTexture.get();
        m_bRenderPassUsesRenderGraphInitialState = bUseRenderGraphInitialState;
        return true;
    }

    void SSRPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline)
        {
            NORVES_LOG_WARNING("SSRPass", "Resources not ready, skipping");
            return;
        }

        // 入力テクスチャ取得
        RHI::TexturePtr normalTex, materialTex, depthTex, sceneColorTex;
        if (context.SharedResources)
        {
            normalTex = context.SharedResources->GetTexturePtr("GBuffer_Normal");
            materialTex = context.SharedResources->GetTexturePtr("GBuffer_Material");
            depthTex = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            sceneColorTex = context.SharedResources->GetTexturePtr("SceneColor");
        }

        if (!normalTex || !materialTex || !depthTex || !sceneColorTex)
        {
            NORVES_LOG_WARNING("SSRPass", "Required textures not available, skipping");
            return;
        }

        ExecuteWithInputs(context, normalTex, materialTex, depthTex, sceneColorTex);
    }

    void SSRPass::Declare(RenderGraphBuilder &builder)
    {
        const ViewRenderContext *context = builder.GetContext();
        const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
        const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

        if (m_GBufferPass)
        {
            const RGResourceHandle normalHandle = m_GBufferPass->GetNormalHandle();
            if (normalHandle.IsValid())
            {
                builder.Read(normalHandle, RHI::ResourceState::ShaderResource);
            }

            const RGResourceHandle materialHandle = m_GBufferPass->GetMaterialHandle();
            if (materialHandle.IsValid())
            {
                builder.Read(materialHandle, RHI::ResourceState::ShaderResource);
            }

            const RGResourceHandle depthHandle = m_GBufferPass->GetDepthHandle();
            if (depthHandle.IsValid())
            {
                builder.Read(depthHandle, RHI::ResourceState::ShaderResource);
            }
        }

        if (m_LightingPass)
        {
            const RGResourceHandle sceneColorHandle = m_LightingPass->GetSceneColorHandle();
            if (sceneColorHandle.IsValid())
            {
                builder.Read(sceneColorHandle, RHI::ResourceState::ShaderResource);
            }
        }

        m_OutputHandle = builder.CreateTexture(
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSROutput"));
        builder.Write(m_OutputHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
        builder.PreserveInsertionOrder();
    }

    void SSRPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("SSRPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
        if (!outputTexture)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to resolve native SSR output texture");
            return;
        }

        if (!PrepareResources(outputTexture->GetWidth(), outputTexture->GetHeight(), outputTexture, true))
        {
            return;
        }

        RHI::TexturePtr normalTex;
        RHI::TexturePtr materialTex;
        RHI::TexturePtr depthTex;
        if (m_GBufferPass)
        {
            const RGResourceHandle normalHandle = m_GBufferPass->GetNormalHandle();
            if (normalHandle.IsValid())
            {
                normalTex = resources.GetTexture(normalHandle);
            }

            const RGResourceHandle materialHandle = m_GBufferPass->GetMaterialHandle();
            if (materialHandle.IsValid())
            {
                materialTex = resources.GetTexture(materialHandle);
            }

            const RGResourceHandle depthHandle = m_GBufferPass->GetDepthHandle();
            if (depthHandle.IsValid())
            {
                depthTex = resources.GetTexture(depthHandle);
            }
        }

        RHI::TexturePtr sceneColorTex;
        if (m_LightingPass)
        {
            const RGResourceHandle sceneColorHandle = m_LightingPass->GetSceneColorHandle();
            if (sceneColorHandle.IsValid())
            {
                sceneColorTex = resources.GetTexture(sceneColorHandle);
            }
        }

        if ((!normalTex || !materialTex || !depthTex || !sceneColorTex) && context.SharedResources)
        {
            if (!normalTex)
            {
                normalTex = context.SharedResources->GetTexturePtr("GBuffer_Normal");
            }
            if (!materialTex)
            {
                materialTex = context.SharedResources->GetTexturePtr("GBuffer_Material");
            }
            if (!depthTex)
            {
                depthTex = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            }
            if (!sceneColorTex)
            {
                sceneColorTex = context.SharedResources->GetTexturePtr("SceneColor");
            }
        }

        if (!normalTex || !materialTex || !depthTex || !sceneColorTex)
        {
            EnqueueEmptyNativePass(context);
            return;
        }

        ExecuteWithInputs(context, normalTex, materialTex, depthTex, sceneColorTex);
    }

    void SSRPass::ExecuteWithInputs(ViewRenderContext &context,
                                    const RHI::TexturePtr &normalTex,
                                    const RHI::TexturePtr &materialTex,
                                    const RHI::TexturePtr &depthTex,
                                    const RHI::TexturePtr &sceneColorTex)
    {
        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline || !m_DescriptorSet)
        {
            NORVES_LOG_WARNING("SSRPass", "Resources not ready, skipping");
            return;
        }

        // プロジェクション行列をカメラから計算
        using namespace NorvesLib::Math;

        GPUSSRParams params = {};
        params.screenSize[0] = static_cast<float>(m_CurrentWidth);
        params.screenSize[1] = static_cast<float>(m_CurrentHeight);
        params.screenSize[2] = 1.0f / static_cast<float>(m_CurrentWidth);
        params.screenSize[3] = 1.0f / static_cast<float>(m_CurrentHeight);
        params.maxDistance = m_Settings.MaxDistance;
        params.thickness = m_Settings.Thickness;
        params.maxSteps = m_Settings.MaxSteps;
        params.fadeStart = m_Settings.FadeStart;
        params.fadeEnd = m_Settings.FadeEnd;
        params.roughnessCutoff = m_Settings.RoughnessCutoff;
        params.intensity = m_Settings.Intensity;
        params.bEnabled = m_Settings.bEnabled ? 1u : 0u;

        // ビューとプロジェクション行列
        const CameraProxy *activeCamera = context.GetActiveCamera();
        if (activeCamera)
        {
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyShaderProjection(params.projection);
            cameraConstants.CopyShaderView(params.view);
            cameraConstants.CopyShaderInverseProjection(params.invProjection);
            cameraConstants.CopyShaderInverseView(params.invView);
        }

        m_ParamsBuffer->Update(&params, sizeof(GPUSSRParams));

        // 出力テクスチャ登録（SceneColorを上書き）
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", m_OutputTexture);
        }

        // テクスチャバインド
        m_DescriptorSet->BindTexture(0, normalTex);
        m_DescriptorSet->BindSampler(0, m_LinearSampler);

        m_DescriptorSet->BindTexture(1, materialTex);
        m_DescriptorSet->BindSampler(1, m_LinearSampler);

        m_DescriptorSet->BindTexture(2, depthTex);
        m_DescriptorSet->BindSampler(2, m_PointSampler);

        m_DescriptorSet->BindTexture(3, sceneColorTex);
        m_DescriptorSet->BindSampler(3, m_LinearSampler);

        // binding 5 (noise) - sceneColorをダミーとしてバインド
        m_DescriptorSet->BindTexture(5, sceneColorTex);
        m_DescriptorSet->BindSampler(5, m_LinearSampler);

        m_DescriptorSet->Update();

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      viewport,
                                      scissor,
                                      m_Pipeline,
                                      m_DescriptorSet);
    }

    bool SSRPass::EnqueueEmptyNativePass(ViewRenderContext &context) const
    {
        if (!m_RenderPass || !m_Framebuffer)
        {
            return false;
        }

        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      nullptr,
                                      nullptr);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
