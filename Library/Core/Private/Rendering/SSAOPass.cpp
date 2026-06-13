#include "Rendering/SSAOPass.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/CameraViewConstants.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "Logging/LogMacros.h"

#include <cmath>
#include <cstdlib>
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
        float screenSize[4]; // xy=size, zw=1/size
        float radius;
        float bias;
        float intensity;
        float _pad0;
    };

    struct GPUBlurParams
    {
        float texelSize[4]; // xy=1/width, 1/height
    };

    static constexpr uint32_t SSAO_PARAMS_SIZE = sizeof(GPUSSAOParams);
    static constexpr uint32_t KERNEL_BUFFER_SIZE = 64 * 4 * sizeof(float); // vec4 * 64
    static constexpr uint32_t BLUR_PARAMS_SIZE = sizeof(GPUBlurParams);

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

        RHI::DescriptorBinding kernelBinding;
        kernelBinding.binding = 3;
        kernelBinding.type = RHI::ResourceBindType::ConstantBuffer;
        kernelBinding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(kernelBinding);

        RHI::DescriptorBinding noiseBinding;
        noiseBinding.binding = 4;
        noiseBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        noiseBinding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(noiseBinding);

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
    // 簡易乱数生成（std::randベース）
    // ========================================
    static float RandomFloat()
    {
        return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    }

    static float Lerp(float a, float b, float t)
    {
        return a + t * (b - a);
    }

    // ========================================
    // コンストラクタ・デストラクタ
    // ========================================

    SSAOPass::SSAOPass(const SSAOSettings &settings)
        : m_Settings(settings)
    {
        std::memset(m_KernelData, 0, sizeof(m_KernelData));
        GenerateKernel();
    }

    SSAOPass::~SSAOPass()
    {
        Shutdown();
    }

    // ========================================
    // カーネル生成
    // ========================================

    void SSAOPass::GenerateKernel()
    {
        std::srand(42); // 固定シード（再現性のため）

        for (int i = 0; i < 64; ++i)
        {
            // 半球内のランダム方向
            float x = RandomFloat() * 2.0f - 1.0f;
            float y = RandomFloat() * 2.0f - 1.0f;
            float z = RandomFloat(); // 法線方向（半球）

            // 正規化
            float length = std::sqrt(x * x + y * y + z * z);
            if (length > 0.0001f)
            {
                x /= length;
                y /= length;
                z /= length;
            }

            // ランダムスケール（中心に近いサンプルを多く）
            float scale = static_cast<float>(i) / 64.0f;
            scale = Lerp(0.1f, 1.0f, scale * scale);
            x *= scale;
            y *= scale;
            z *= scale;

            m_KernelData[i * 4 + 0] = x;
            m_KernelData[i * 4 + 1] = y;
            m_KernelData[i * 4 + 2] = z;
            m_KernelData[i * 4 + 3] = 0.0f;
        }
    }

    // ========================================
    // ノイズテクスチャ生成
    // ========================================

    void SSAOPass::GenerateNoiseTexture()
    {
        if (!m_Device)
        {
            return;
        }

        // 4x4 ランダム回転ベクトルテクスチャ (RGB8)
        constexpr uint32_t NOISE_SIZE = 4;
        uint8_t noiseData[NOISE_SIZE * NOISE_SIZE * 4];

        std::srand(12345); // 固定シード
        for (uint32_t i = 0; i < NOISE_SIZE * NOISE_SIZE; ++i)
        {
            float x = RandomFloat() * 2.0f - 1.0f;
            float y = RandomFloat() * 2.0f - 1.0f;
            float z = 0.0f; // Z方向は使わない（接線空間内の回転のみ）

            // 正規化
            float len = std::sqrt(x * x + y * y);
            if (len > 0.0001f)
            {
                x /= len;
                y /= len;
            }

            // [0,1]にマッピングしてRGBA8に格納
            noiseData[i * 4 + 0] = static_cast<uint8_t>((x * 0.5f + 0.5f) * 255.0f);
            noiseData[i * 4 + 1] = static_cast<uint8_t>((y * 0.5f + 0.5f) * 255.0f);
            noiseData[i * 4 + 2] = static_cast<uint8_t>((z * 0.5f + 0.5f) * 255.0f);
            noiseData[i * 4 + 3] = 255;
        }

        RHI::TextureDesc noiseDesc;
        noiseDesc.Width = NOISE_SIZE;
        noiseDesc.Height = NOISE_SIZE;
        noiseDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
        noiseDesc.Usage = RHI::ResourceUsage::ShaderRead;
        noiseDesc.DebugName = "SSAONoise4x4";

        m_NoiseTexture = m_Device->CreateTexture(noiseDesc);
        if (m_NoiseTexture)
        {
            // 4x4 RGBA8 = row pitch 16 bytes
            m_NoiseTexture->Update(noiseData, NOISE_SIZE * 4, sizeof(noiseData));
        }
        else
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create noise texture");
        }
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

        m_SSAOFragmentShader = context.ShaderMgr->LoadShader("ssao.frag", RHI::ShaderStage::Pixel);
        if (!m_SSAOFragmentShader)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to load SSAO fragment shader");
            return false;
        }

        m_BlurFragmentShader = context.ShaderMgr->LoadShader("ssao_blur.frag", RHI::ShaderStage::Pixel);
        if (!m_BlurFragmentShader)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to load SSAO blur fragment shader");
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
        {
            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Point;
            samplerDesc.filterMag = RHI::FilterMode::Point;
            samplerDesc.filterMip = RHI::FilterMode::Point;
            samplerDesc.addressU = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressV = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressW = RHI::TextureAddressMode::Wrap;
            m_NearestRepeatSampler = m_Device->CreateSampler(samplerDesc);
        }

        if (!m_LinearClampSampler || !m_NearestRepeatSampler)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create samplers");
            return false;
        }

        // パラメータUBO
        {
            RHI::BufferDesc desc(SSAO_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "SSAOParamsUBO");
            m_SSAOParamsBuffer = m_Device->CreateBuffer(desc);
        }
        // カーネルUBO
        {
            RHI::BufferDesc desc(KERNEL_BUFFER_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "SSAOKernelUBO");
            m_KernelBuffer = m_Device->CreateBuffer(desc);
            if (m_KernelBuffer)
            {
                m_KernelBuffer->Update(m_KernelData, KERNEL_BUFFER_SIZE);
            }
        }
        // ブラーパラメータUBO
        {
            RHI::BufferDesc desc(BLUR_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "SSAOBlurParamsUBO");
            m_BlurParamsBuffer = m_Device->CreateBuffer(desc);
        }

        if (!m_SSAOParamsBuffer || !m_KernelBuffer || !m_BlurParamsBuffer)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create UBO buffers");
            return false;
        }

        // ノイズテクスチャ生成
        GenerateNoiseTexture();

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
        m_NoiseTexture.reset();
        m_SSAORawHandle = {};
        m_SSAOBlurredHandle = {};
        m_SSAORenderPass.reset();
        m_SSAOFramebuffer.reset();
        m_SSAOPipeline.reset();
        m_SSAOVertexShader.reset();
        m_SSAOFragmentShader.reset();
        m_SSAOParamsBuffer.reset();
        m_KernelBuffer.reset();
        m_SSAODescriptorSet.reset();
        m_BlurRenderPass.reset();
        m_BlurFramebuffer.reset();
        m_BlurPipeline.reset();
        m_BlurFragmentShader.reset();
        m_BlurParamsBuffer.reset();
        m_BlurDescriptorSet.reset();
        m_LinearClampSampler.reset();
        m_NearestRepeatSampler.reset();
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

        if (m_GBufferPass)
        {
            const RGResourceHandle depthHandle = m_GBufferPass->GetDepthHandle();
            if (depthHandle.IsValid())
            {
                builder.Read(depthHandle, RHI::ResourceState::ShaderResource);
            }

            const RGResourceHandle normalHandle = m_GBufferPass->GetNormalHandle();
            if (normalHandle.IsValid())
            {
                builder.Read(normalHandle, RHI::ResourceState::ShaderResource);
            }
        }

        m_SSAORawHandle = builder.CreateTexture(
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSAORaw"));
        builder.Write(m_SSAORawHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);

        m_SSAOBlurredHandle = builder.CreateTexture(
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSAOBlurred"));
        builder.Write(m_SSAOBlurredHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);

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
        if (m_GBufferPass)
        {
            const RGResourceHandle depthHandle = m_GBufferPass->GetDepthHandle();
            const RGResourceHandle normalHandle = m_GBufferPass->GetNormalHandle();
            if (depthHandle.IsValid())
            {
                depthTexture = resources.GetTexture(depthHandle);
            }
            if (normalHandle.IsValid())
            {
                normalTexture = resources.GetTexture(normalHandle);
            }
        }

        if ((!depthTexture || !normalTexture) && context.SharedResources)
        {
            depthTexture = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            normalTexture = context.SharedResources->GetTexturePtr("GBuffer_Normal");
        }

        if (!PrepareSSAOAttachments(rawTexture->GetWidth(),
                                    rawTexture->GetHeight(),
                                    rawTexture,
                                    blurredTexture,
                                    true))
        {
            return;
        }

        ExecuteWithGBufferTextures(context, depthTexture, normalTexture);
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

        ExecuteWithGBufferTextures(context, depthTexture, normalTexture);
    }

    bool SSAOPass::CreateSSAOResources(uint32_t width, uint32_t height, ViewRenderContext &context)
    {
        (void)context;

        if (!m_Device)
        {
            return false;
        }

        RHI::TexturePtr rawTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSAORaw"));
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
            m_SSAODescriptorSet->BindConstantBuffer(3, m_KernelBuffer, 0, KERNEL_BUFFER_SIZE);
            if (m_NoiseTexture)
            {
                m_SSAODescriptorSet->BindTexture(4, m_NoiseTexture);
                m_SSAODescriptorSet->BindSampler(4, m_NearestRepeatSampler);
            }
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
                                              const RHI::TexturePtr &normalTexture)
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

        const CameraProxy *activeCamera = context.GetActiveCamera();
        if (activeCamera)
        {
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
            float projData[16];
            cameraConstants.CopyShaderProjection(projData);
            std::memcpy(ssaoParams.projection, projData, sizeof(projData));

            float invProjData[16];
            cameraConstants.CopyShaderInverseProjection(invProjData);
            std::memcpy(ssaoParams.invProjection, invProjData, sizeof(invProjData));
        }

        ssaoParams.screenSize[0] = static_cast<float>(m_CurrentWidth);
        ssaoParams.screenSize[1] = static_cast<float>(m_CurrentHeight);
        ssaoParams.screenSize[2] = 1.0f / static_cast<float>(m_CurrentWidth);
        ssaoParams.screenSize[3] = 1.0f / static_cast<float>(m_CurrentHeight);
        ssaoParams.radius = m_Settings.Radius;
        ssaoParams.bias = m_Settings.Bias;
        ssaoParams.intensity = m_Settings.Intensity;
        ssaoParams._pad0 = 0.0f;

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

        GPUBlurParams blurParams = {};
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

        if (context.SharedResources)
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
