#include "Rendering/SSAOPass.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // GPU構造体（std140レイアウト一致）
    // ========================================

    struct GPUSSAOParams
    {
        float projection[16];
        float invProjection[16];
        float view[16];
        float screenSize[4];   // xy=寸法, zw=1/寸法
        float radiusParams[4]; // x=半径(m), y=減衰を始める距離(m), z=減衰の幅(m), w=画面上の半径の上限(画素)
        float noiseParams[4];  // x=スライスの向きの時間のずらし, y=段の位置の時間のずらし, z=可視率の指数
    };

    struct GPUBlurParams
    {
        float invProjection[16];
        float texelSize[4]; // xy=1/width, 1/height
    };

    static_assert(sizeof(GPUSSAOParams) == 240, "gtao.frag の GTAOParams と std140 の大きさが一致すること");
    static_assert(sizeof(GPUBlurParams) == 80, "gtao_denoise.frag の DenoiseParams と std140 の大きさが一致すること");

    static constexpr uint32_t SSAO_PARAMS_SIZE = sizeof(GPUSSAOParams);
    static constexpr uint32_t BLUR_PARAMS_SIZE = sizeof(GPUBlurParams);

    // TAAで時間方向に蓄積するときの、フレームごとの雑音のずらし（黄金比系の低食い違い列）
    static constexpr float TemporalSliceNoiseStep = 0.6180339887f;
    static constexpr float TemporalStepNoiseStep = 0.7548776662f;
    // 時間のずらしを繰り返す長さ（浮動小数の精度を保つため、フレーム番号をこの周期で丸める）
    static constexpr uint64_t TemporalNoisePeriod = 64u;

    static RHI::DescriptorSetDesc CreateSSAODescriptorSetDesc()
    {
        RHI::DescriptorSetDesc desc;

        RHI::DescriptorBinding depthBinding;
        depthBinding.binding = 0;
        depthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        depthBinding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(depthBinding);

        RHI::DescriptorBinding normalBinding;
        normalBinding.binding = 1;
        normalBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        normalBinding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(normalBinding);

        RHI::DescriptorBinding paramsBinding;
        paramsBinding.binding = 2;
        paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramsBinding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(paramsBinding);

        return desc;
    }

    static RHI::DescriptorSetDesc CreateSSAOBlurDescriptorSetDesc()
    {
        RHI::DescriptorSetDesc desc;

        RHI::DescriptorBinding ssaoBinding;
        ssaoBinding.binding = 0;
        ssaoBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        ssaoBinding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(ssaoBinding);

        RHI::DescriptorBinding depthBinding;
        depthBinding.binding = 1;
        depthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        depthBinding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(depthBinding);

        RHI::DescriptorBinding paramsBinding;
        paramsBinding.binding = 2;
        paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramsBinding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(paramsBinding);

        return desc;
    }

    // ========================================
    // コンストラクタ・デストラクタ
    // ========================================

    SSAOPass::SSAOPass(const SSAOSettings &settings)
        : m_Settings(settings)
    {
    }

    SSAOPass::~SSAOPass()
    {
        Shutdown();
    }

    // ========================================
    // Initialize
    // ========================================

    bool SSAOPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("SSAOPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("SSAOPass", "ShaderManager is null");
            return false;
        }

        // シェーダー読み込み
        m_SSAOVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_SSAOVertexShader)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to load fullscreen vertex shader");
            return false;
        }

        m_SSAOFragmentShader = context.ShaderMgr->LoadShader("gtao.frag", RHI::ShaderStage::Pixel);
        if (!m_SSAOFragmentShader)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to load GTAO fragment shader");
            return false;
        }

        m_BlurFragmentShader = context.ShaderMgr->LoadShader("gtao_denoise.frag", RHI::ShaderStage::Pixel);
        if (!m_BlurFragmentShader)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to load GTAO denoise fragment shader");
            return false;
        }

        // サンプラー作成
        {
            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Linear;
            samplerDesc.filterMag = RHI::FilterMode::Linear;
            samplerDesc.filterMip = RHI::FilterMode::Linear;
            samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
            samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
            samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
            m_LinearClampSampler = m_Device->CreateSampler(samplerDesc);
        }

        if (!m_LinearClampSampler)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create samplers");
            return false;
        }

        // パラメータUBO
        {
            RHI::BufferDesc desc(SSAO_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "SSAOParamsUBO");
            m_SSAOParamsBuffer = m_Device->CreateBuffer(desc);
        }
        // 雑音除去パラメータUBO
        {
            RHI::BufferDesc desc(BLUR_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "SSAOBlurParamsUBO");
            m_BlurParamsBuffer = m_Device->CreateBuffer(desc);
        }

        if (!m_SSAOParamsBuffer || !m_BlurParamsBuffer)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create UBO buffers");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("SSAOPass", "SSAOPass initialized");
        return true;
    }

    // ========================================
    // Shutdown
    // ========================================

    void SSAOPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_GBufferPass = nullptr;
        m_SSAORawTexture.reset();
        m_SSAOBlurredTexture.reset();
        m_SSAORawHandle = {};
        m_SSAOBlurredHandle = {};
        m_GBufferDepthHandle = {};
        m_GBufferNormalHandle = {};
        m_SSAORenderPass.reset();
        m_SSAOFramebuffer.reset();
        m_SSAOPipeline.reset();
        m_SSAOVertexShader.reset();
        m_SSAOFragmentShader.reset();
        m_SSAOParamsBuffer.reset();
        m_SSAODescriptorSet.reset();
        m_BlurRenderPass.reset();
        m_BlurFramebuffer.reset();
        m_BlurPipeline.reset();
        m_BlurFragmentShader.reset();
        m_BlurParamsBuffer.reset();
        m_BlurDescriptorSet.reset();
        m_LinearClampSampler.reset();
        m_Device = nullptr;
        m_CurrentWidth = 0;
        m_CurrentHeight = 0;
        m_bUsingRenderGraphResources = false;
        m_bSSAOInitialStateFromRenderGraph = false;
        m_bBlurInitialStateFromRenderGraph = false;
        m_SSAOFramebufferTexture = nullptr;
        m_BlurFramebufferTexture = nullptr;
        m_SSAOFramebufferWidth = 0;
        m_SSAOFramebufferHeight = 0;
        m_BlurFramebufferWidth = 0;
        m_BlurFramebufferHeight = 0;

        m_bInitialized = false;
        NORVES_LOG_INFO("SSAOPass", "SSAOPass shutdown");
    }

    // ========================================
    // Setup（リサイズ時にリソース再作成）
    // ========================================

    void SSAOPass::Setup(ViewRenderContext &context)
    {
        uint32_t width = ResolveSSAOWidth(context);
        uint32_t height = ResolveSSAOHeight(context);

        if (width == 0 || height == 0)
        {
            return;
        }

        if (width == m_CurrentWidth &&
            height == m_CurrentHeight &&
            !m_bUsingRenderGraphResources &&
            m_SSAORawTexture &&
            m_SSAOBlurredTexture &&
            m_SSAORenderPass &&
            m_SSAOFramebuffer &&
            m_SSAOPipeline &&
            m_BlurRenderPass &&
            m_BlurFramebuffer &&
            m_BlurPipeline)
        {
            return;
        }

        CreateSSAOResources(width, height, context);
    }

    void SSAOPass::Declare(RenderGraphBuilder &builder)
    {
        m_bLegacyInputFallbackActive = false;
        const ViewRenderContext *context = builder.GetContext();

        uint32_t width = 0;
        uint32_t height = 0;
        if (context)
        {
            width = ResolveSSAOWidth(*context);
            height = ResolveSSAOHeight(*context);
        }

        if (width == 0)
        {
            width = m_CurrentWidth > 0 ? m_CurrentWidth : 1;
        }

        if (height == 0)
        {
            height = m_CurrentHeight > 0 ? m_CurrentHeight : 1;
        }

        m_GBufferDepthHandle = {};
        m_GBufferNormalHandle = {};

        RGTextureHandle depthHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth,
                                   depthHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferDepthHandle = depthHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackDepthHandle = m_GBufferPass->GetDepthHandle();
            if (fallbackDepthHandle.IsValid())
            {
                builder.Read(fallbackDepthHandle, RHI::ResourceState::ShaderResource);
                m_GBufferDepthHandle = fallbackDepthHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle normalHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferNormal,
                                   normalHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferNormalHandle = normalHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackNormalHandle = m_GBufferPass->GetNormalHandle();
            if (fallbackNormalHandle.IsValid())
            {
                builder.Read(fallbackNormalHandle, RHI::ResourceState::ShaderResource);
                m_GBufferNormalHandle = fallbackNormalHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        m_SSAORawHandle = builder.WriteTexture(
            RenderGraphResourceNames::SSAORaw,
            RGTextureDesc::RenderTarget(width, height, RawFormat, "SSAORaw"),
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);

        m_SSAOBlurredHandle = builder.WriteTexture(
            RenderGraphResourceNames::SSAOBlurred,
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSAOBlurred"),
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);

        builder.PreserveInsertionOrder();
    }

    // ========================================
    // Execute
    // ========================================

    void SSAOPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("SSAOPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr rawTexture = resources.GetTexture(m_SSAORawHandle);
        RHI::TexturePtr blurredTexture = resources.GetTexture(m_SSAOBlurredHandle);
        if (!rawTexture || !blurredTexture)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to resolve native SSAO textures");
            return;
        }

        RHI::TexturePtr depthTexture;
        RHI::TexturePtr normalTexture;
        bool bUsedSharedResourceFallback = false;
        if (m_GBufferDepthHandle.IsValid())
        {
            depthTexture = resources.GetTexture(m_GBufferDepthHandle);
        }
        if (m_GBufferNormalHandle.IsValid())
        {
            normalTexture = resources.GetTexture(m_GBufferNormalHandle);
        }

        if ((!depthTexture || !normalTexture) && m_GBufferPass)
        {
            const RGResourceHandle fallbackDepthHandle = m_GBufferPass->GetDepthHandle();
            const RGResourceHandle fallbackNormalHandle = m_GBufferPass->GetNormalHandle();
            depthTexture = depthTexture ? depthTexture : resources.GetTexture(fallbackDepthHandle);
            normalTexture = normalTexture ? normalTexture : resources.GetTexture(fallbackNormalHandle);
        }

        if ((!depthTexture || !normalTexture) && context.SharedResources)
        {
            if (!depthTexture)
            {
                depthTexture = context.SharedResources->GetTexturePtr("GBuffer_Depth");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || depthTexture != nullptr;
            }

            if (!normalTexture)
            {
                normalTexture = context.SharedResources->GetTexturePtr("GBuffer_Normal");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || normalTexture != nullptr;
            }
        }

        if (!PrepareSSAOAttachments(rawTexture->GetWidth(),
                                    rawTexture->GetHeight(),
                                    rawTexture,
                                    blurredTexture,
                                    true))
        {
            return;
        }

        ExecuteWithGBufferTextures(context,
                                   depthTexture,
                                   normalTexture,
                                   m_bLegacyInputFallbackActive || bUsedSharedResourceFallback);
    }

    void SSAOPass::Execute(ViewRenderContext &context)
    {
        RHI::TexturePtr depthTexture;
        RHI::TexturePtr normalTexture;
        if (context.SharedResources)
        {
            depthTexture = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            normalTexture = context.SharedResources->GetTexturePtr("GBuffer_Normal");
        }

        ExecuteWithGBufferTextures(context, depthTexture, normalTexture, true);
    }

    bool SSAOPass::CreateSSAOResources(uint32_t width, uint32_t height, ViewRenderContext &context)
    {
        (void)context;

        if (!m_Device)
        {
            return false;
        }

        RHI::TexturePtr rawTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, RawFormat, "SSAORaw"));
        RHI::TexturePtr blurredTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSAOBlurred"));

        if (!rawTexture || !blurredTexture)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO textures");
            return false;
        }

        return PrepareSSAOAttachments(width, height, rawTexture, blurredTexture, false);
    }

    uint32_t SSAOPass::ResolveSSAOWidth(const ViewRenderContext &context) const
    {
        return context.GetActiveRenderWidth();
    }

    uint32_t SSAOPass::ResolveSSAOHeight(const ViewRenderContext &context) const
    {
        return context.GetActiveRenderHeight();
    }

    bool SSAOPass::PrepareSSAOAttachments(uint32_t width,
                                          uint32_t height,
                                          const RHI::TexturePtr &rawTexture,
                                          const RHI::TexturePtr &blurredTexture,
                                          bool bUseRenderGraphInitialStates)
    {
        if (!m_Device)
        {
            return false;
        }

        if (!rawTexture || !blurredTexture)
        {
            NORVES_LOG_ERROR("SSAOPass", "SSAO attachment textures are incomplete");
            return false;
        }

        m_SSAORawTexture = rawTexture;
        m_SSAOBlurredTexture = blurredTexture;
        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_bUsingRenderGraphResources = bUseRenderGraphInitialStates;

        if (!EnsureSSAORenderPass(bUseRenderGraphInitialStates))
        {
            return false;
        }

        if (!EnsureSSAOFramebuffer(width, height, rawTexture))
        {
            return false;
        }

        if (!EnsureSSAOPipeline())
        {
            return false;
        }

        if (!EnsureBlurRenderPass(bUseRenderGraphInitialStates))
        {
            return false;
        }

        if (!EnsureBlurFramebuffer(width, height, blurredTexture))
        {
            return false;
        }

        return EnsureBlurPipeline();
    }

    bool SSAOPass::EnsureSSAORenderPass(bool bUseRenderGraphInitialStates)
    {
        if (!m_Device)
        {
            return false;
        }

        if (m_SSAORenderPass &&
            m_bSSAOInitialStateFromRenderGraph == bUseRenderGraphInitialStates)
        {
            return true;
        }

        m_SSAORenderPass.reset();
        m_SSAOFramebuffer.reset();
        m_SSAOPipeline.reset();
        m_SSAOFramebufferTexture = nullptr;
        m_SSAOFramebufferWidth = 0;
        m_SSAOFramebufferHeight = 0;

        RHI::RenderPassDesc rpDesc;
        RHI::AttachmentDesc colorAttach;
        colorAttach.format = RawFormat;
        colorAttach.isDepthStencil = false;
        colorAttach.clear = false;
        colorAttach.loadOp = RHI::AttachmentLoadOp::DontCare;
        colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttach.initialState = bUseRenderGraphInitialStates
                                       ? RHI::ResourceState::RenderTarget
                                       : RHI::ResourceState::Undefined;
        colorAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(colorAttach);
        rpDesc.hasDepthStencil = false;

        m_SSAORenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_SSAORenderPass)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO render pass");
            return false;
        }

        m_bSSAOInitialStateFromRenderGraph = bUseRenderGraphInitialStates;
        return true;
    }

    bool SSAOPass::EnsureSSAOFramebuffer(uint32_t width,
                                         uint32_t height,
                                         const RHI::TexturePtr &rawTexture)
    {
        if (m_SSAOFramebuffer &&
            m_SSAOFramebufferTexture == rawTexture.get() &&
            m_SSAOFramebufferWidth == width &&
            m_SSAOFramebufferHeight == height)
        {
            return true;
        }

        if (!m_Device || !m_SSAORenderPass || !rawTexture)
        {
            return false;
        }

        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_SSAORenderPass;
        fbDesc.colorTargets.push_back(rawTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_SSAOFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_SSAOFramebuffer)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO framebuffer");
            return false;
        }

        m_SSAOFramebufferTexture = rawTexture.get();
        m_SSAOFramebufferWidth = width;
        m_SSAOFramebufferHeight = height;
        return true;
    }

    bool SSAOPass::EnsureSSAOPipeline()
    {
        if (!m_Device || !m_SSAORenderPass || !m_SSAOVertexShader || !m_SSAOFragmentShader)
        {
            return false;
        }

        RHI::DescriptorSetDesc ssaoDsDesc = CreateSSAODescriptorSetDesc();
        if (!m_SSAODescriptorSet)
        {
            m_SSAODescriptorSet = m_Device->CreateDescriptorSet(ssaoDsDesc);
            if (!m_SSAODescriptorSet)
            {
                NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO descriptor set");
                return false;
            }

            m_SSAODescriptorSet->BindConstantBuffer(2, m_SSAOParamsBuffer, 0, SSAO_PARAMS_SIZE);
        }

        if (m_SSAOPipeline)
        {
            return true;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_SSAOVertexShader;
        pipelineDesc.pixelShader = m_SSAOFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::BlendAttachmentDesc blendState;
        blendState.blendEnable = false;
        blendState.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendState);

        pipelineDesc.renderPass = m_SSAORenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(ssaoDsDesc);

        m_SSAOPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_SSAOPipeline)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO pipeline");
            return false;
        }

        return true;
    }

    bool SSAOPass::EnsureBlurRenderPass(bool bUseRenderGraphInitialStates)
    {
        if (!m_Device)
        {
            return false;
        }

        if (m_BlurRenderPass &&
            m_bBlurInitialStateFromRenderGraph == bUseRenderGraphInitialStates)
        {
            return true;
        }

        m_BlurRenderPass.reset();
        m_BlurFramebuffer.reset();
        m_BlurPipeline.reset();
        m_BlurFramebufferTexture = nullptr;
        m_BlurFramebufferWidth = 0;
        m_BlurFramebufferHeight = 0;

        RHI::RenderPassDesc rpDesc;
        RHI::AttachmentDesc colorAttach;
        colorAttach.format = m_Settings.OutputFormat;
        colorAttach.isDepthStencil = false;
        colorAttach.clear = false;
        colorAttach.loadOp = RHI::AttachmentLoadOp::DontCare;
        colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttach.initialState = bUseRenderGraphInitialStates
                                       ? RHI::ResourceState::RenderTarget
                                       : RHI::ResourceState::Undefined;
        colorAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(colorAttach);
        rpDesc.hasDepthStencil = false;

        m_BlurRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_BlurRenderPass)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO blur render pass");
            return false;
        }

        m_bBlurInitialStateFromRenderGraph = bUseRenderGraphInitialStates;
        return true;
    }

    bool SSAOPass::EnsureBlurFramebuffer(uint32_t width,
                                         uint32_t height,
                                         const RHI::TexturePtr &blurredTexture)
    {
        if (m_BlurFramebuffer &&
            m_BlurFramebufferTexture == blurredTexture.get() &&
            m_BlurFramebufferWidth == width &&
            m_BlurFramebufferHeight == height)
        {
            return true;
        }

        if (!m_Device || !m_BlurRenderPass || !blurredTexture)
        {
            return false;
        }

        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_BlurRenderPass;
        fbDesc.colorTargets.push_back(blurredTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_BlurFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_BlurFramebuffer)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO blur framebuffer");
            return false;
        }

        m_BlurFramebufferTexture = blurredTexture.get();
        m_BlurFramebufferWidth = width;
        m_BlurFramebufferHeight = height;
        return true;
    }

    bool SSAOPass::EnsureBlurPipeline()
    {
        if (!m_Device || !m_BlurRenderPass || !m_SSAOVertexShader || !m_BlurFragmentShader)
        {
            return false;
        }

        RHI::DescriptorSetDesc blurDsDesc = CreateSSAOBlurDescriptorSetDesc();
        if (!m_BlurDescriptorSet)
        {
            m_BlurDescriptorSet = m_Device->CreateDescriptorSet(blurDsDesc);
            if (!m_BlurDescriptorSet)
            {
                NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO blur descriptor set");
                return false;
            }

            m_BlurDescriptorSet->BindConstantBuffer(2, m_BlurParamsBuffer, 0, BLUR_PARAMS_SIZE);
        }

        if (m_BlurPipeline)
        {
            return true;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_SSAOVertexShader;
        pipelineDesc.pixelShader = m_BlurFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::BlendAttachmentDesc blendState;
        blendState.blendEnable = false;
        blendState.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendState);

        pipelineDesc.renderPass = m_BlurRenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(blurDsDesc);

        m_BlurPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_BlurPipeline)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO blur pipeline");
            return false;
        }

        NORVES_LOG_INFO("SSAOPass", "Resources created");
        return true;
    }

    void SSAOPass::ExecuteWithGBufferTextures(ViewRenderContext &context,
                                              const RHI::TexturePtr &depthTexture,
                                              const RHI::TexturePtr &normalTexture,
                                              bool bRegisterLegacyBridge)
    {
        if (!context.CommandList ||
            !m_SSAOPipeline ||
            !m_BlurPipeline ||
            !m_SSAODescriptorSet ||
            !m_BlurDescriptorSet ||
            m_CurrentWidth == 0 ||
            m_CurrentHeight == 0)
        {
            return;
        }

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        if (!depthTexture || !normalTexture)
        {
            NORVES_LOG_WARNING("SSAOPass", "GBuffer textures not available, skipping SSAO");
            TryEnqueueNativeTransitionPasses(context);
            return;
        }

        GPUSSAOParams ssaoParams = {};
        GPUBlurParams blurParams = {};

        const CameraProxy *activeCamera = context.GetActiveCamera();
        bool bTemporalAccumulation = false;
        if (activeCamera)
        {
            // GBufferを描いたのと同じ（TAAのジッタ込みの）投影で深度を復元する
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyShaderProjection(ssaoParams.projection);
            cameraConstants.CopyShaderInverseProjection(ssaoParams.invProjection);
            cameraConstants.CopyShaderView(ssaoParams.view);
            cameraConstants.CopyShaderInverseProjection(blurParams.invProjection);
            // ジッタが掛かっていれば、このViewportはTAAの履歴で時間方向に蓄積される
            bTemporalAccumulation = activeCamera->ProjectionJitterNdcX != 0.0f ||
                                    activeCamera->ProjectionJitterNdcY != 0.0f;
        }

        const float radius = std::isfinite(m_Settings.Radius) && m_Settings.Radius > 0.0f
                                 ? m_Settings.Radius
                                 : 0.0f;
        const float falloffFraction = std::isfinite(m_Settings.FalloffFraction)
                                          ? std::clamp(m_Settings.FalloffFraction, 0.0f, 1.0f)
                                          : 0.0f;
        const float falloffRange = radius * falloffFraction;

        ssaoParams.screenSize[0] = static_cast<float>(m_CurrentWidth);
        ssaoParams.screenSize[1] = static_cast<float>(m_CurrentHeight);
        ssaoParams.screenSize[2] = 1.0f / static_cast<float>(m_CurrentWidth);
        ssaoParams.screenSize[3] = 1.0f / static_cast<float>(m_CurrentHeight);
        ssaoParams.radiusParams[0] = radius;
        ssaoParams.radiusParams[1] = radius - falloffRange;
        ssaoParams.radiusParams[2] = falloffRange;
        ssaoParams.radiusParams[3] = std::isfinite(m_Settings.MaxRadiusPixels)
                                         ? std::max(m_Settings.MaxRadiusPixels, 0.0f)
                                         : 0.0f;
        if (bTemporalAccumulation)
        {
            // 雑音の4×4の並びをフレームごとにずらし、TAAが別の向き・段の結果を混ぜるようにする
            const float frame = static_cast<float>(context.FrameNumber % TemporalNoisePeriod);
            ssaoParams.noiseParams[0] = frame * TemporalSliceNoiseStep - std::floor(frame * TemporalSliceNoiseStep);
            ssaoParams.noiseParams[1] = frame * TemporalStepNoiseStep - std::floor(frame * TemporalStepNoiseStep);
        }
        ssaoParams.noiseParams[2] = std::isfinite(m_Settings.Intensity) && m_Settings.Intensity > 0.0f
                                        ? m_Settings.Intensity
                                        : 1.0f;

        m_SSAOParamsBuffer->Update(&ssaoParams, SSAO_PARAMS_SIZE);

        m_SSAODescriptorSet->BindTexture(0, depthTexture);
        m_SSAODescriptorSet->BindSampler(0, m_LinearClampSampler);
        m_SSAODescriptorSet->BindTexture(1, normalTexture);
        m_SSAODescriptorSet->BindSampler(1, m_LinearClampSampler);
        m_SSAODescriptorSet->Update();

        context.EnqueueFullscreenPass(m_SSAORenderPass,
                                      m_SSAOFramebuffer,
                                      viewport,
                                      scissor,
                                      m_SSAOPipeline,
                                      m_SSAODescriptorSet);

        blurParams.texelSize[0] = 1.0f / static_cast<float>(m_CurrentWidth);
        blurParams.texelSize[1] = 1.0f / static_cast<float>(m_CurrentHeight);
        blurParams.texelSize[2] = 0.0f;
        blurParams.texelSize[3] = 0.0f;
        m_BlurParamsBuffer->Update(&blurParams, BLUR_PARAMS_SIZE);

        m_BlurDescriptorSet->BindTexture(0, m_SSAORawTexture);
        m_BlurDescriptorSet->BindSampler(0, m_LinearClampSampler);
        m_BlurDescriptorSet->BindTexture(1, depthTexture);
        m_BlurDescriptorSet->BindSampler(1, m_LinearClampSampler);
        m_BlurDescriptorSet->Update();

        context.EnqueueFullscreenPass(m_BlurRenderPass,
                                      m_BlurFramebuffer,
                                      viewport,
                                      scissor,
                                      m_BlurPipeline,
                                      m_BlurDescriptorSet);

        if (bRegisterLegacyBridge && context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SSAO", m_SSAOBlurredTexture);
        }
    }

    bool SSAOPass::TryEnqueueNativeTransitionPasses(ViewRenderContext &context) const
    {
        if (!m_bUsingRenderGraphResources)
        {
            return false;
        }

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        bool bEnqueued = false;
        if (m_SSAORenderPass && m_SSAOFramebuffer)
        {
            context.EnqueueFullscreenPass(m_SSAORenderPass,
                                          m_SSAOFramebuffer,
                                          viewport,
                                          scissor,
                                          RHI::PipelinePtr{},
                                          RHI::DescriptorSetPtr{},
                                          0,
                                          0);
            bEnqueued = true;
        }

        if (m_BlurRenderPass && m_BlurFramebuffer)
        {
            context.EnqueueFullscreenPass(m_BlurRenderPass,
                                          m_BlurFramebuffer,
                                          viewport,
                                          scissor,
                                          RHI::PipelinePtr{},
                                          RHI::DescriptorSetPtr{},
                                          0,
                                          0);
            bEnqueued = true;
        }

        return bEnqueued;
    }

} // namespace NorvesLib::Core::Rendering
