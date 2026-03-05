#include "Rendering/SSAOPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SceneProxy.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "Math/Matrix4x4.h"
#include "Math/MatrixUtils.h"
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
        float screenSize[4];   // xy=size, zw=1/size
        float radius;
        float bias;
        float intensity;
        float _pad0;
    };

    struct GPUBlurParams
    {
        float texelSize[4];   // xy=1/width, 1/height
    };

    static constexpr uint32_t SSAO_PARAMS_SIZE = sizeof(GPUSSAOParams);
    static constexpr uint32_t KERNEL_BUFFER_SIZE = 64 * 4 * sizeof(float); // vec4 * 64
    static constexpr uint32_t BLUR_PARAMS_SIZE = sizeof(GPUBlurParams);

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

    SSAOPass::SSAOPass(const SSAOSettings& settings)
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

    bool SSAOPass::Initialize(ViewRenderContext& context)
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

        m_SSAORawTexture.reset();
        m_SSAOBlurredTexture.reset();
        m_NoiseTexture.reset();
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

        m_bInitialized = false;
        NORVES_LOG_INFO("SSAOPass", "SSAOPass shutdown");
    }

    // ========================================
    // Setup（リサイズ時にリソース再作成）
    // ========================================

    void SSAOPass::Setup(ViewRenderContext& context)
    {
        uint32_t width = context.ScreenWidth;
        uint32_t height = context.ScreenHeight;

        if (width == 0 || height == 0)
        {
            return;
        }

        if (width == m_CurrentWidth && height == m_CurrentHeight)
        {
            return;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;

        // ========================================
        // SSAOパス用リソース
        // ========================================

        // 生AOテクスチャ
        m_SSAORawTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSAORaw"));

        // ブラー済みAOテクスチャ
        m_SSAOBlurredTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSAOBlurred"));

        if (!m_SSAORawTexture || !m_SSAOBlurredTexture)
        {
            NORVES_LOG_ERROR("SSAOPass", "Failed to create SSAO textures");
            return;
        }

        // ---- SSAOレンダーパス ----
        {
            RHI::RenderPassDesc rpDesc;
            RHI::AttachmentDesc colorAttach;
            colorAttach.format = m_Settings.OutputFormat;
            colorAttach.isDepthStencil = false;
            colorAttach.clear = false;
            colorAttach.loadOp = RHI::AttachmentLoadOp::DontCare;
            colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttach.initialState = RHI::ResourceState::Undefined;
            colorAttach.finalState = RHI::ResourceState::ShaderResource;
            rpDesc.colorAttachments.push_back(colorAttach);
            rpDesc.hasDepthStencil = false;

            m_SSAORenderPass = m_Device->CreateRenderPass(rpDesc);

            RHI::FramebufferDesc fbDesc;
            fbDesc.renderPass = m_SSAORenderPass;
            fbDesc.colorTargets.push_back(m_SSAORawTexture);
            fbDesc.width = width;
            fbDesc.height = height;
            m_SSAOFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        }

        // ---- SSAOディスクリプタセット ----
        RHI::DescriptorSetDesc ssaoDsDesc;
        {
            // binding 0: gbufferDepth (combined image sampler)
            RHI::DescriptorBinding depthBinding;
            depthBinding.binding = 0;
            depthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            depthBinding.stages = RHI::ShaderStage::Pixel;
            ssaoDsDesc.bindings.push_back(depthBinding);

            // binding 1: gbufferNormal (combined image sampler)
            RHI::DescriptorBinding normalBinding;
            normalBinding.binding = 1;
            normalBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            normalBinding.stages = RHI::ShaderStage::Pixel;
            ssaoDsDesc.bindings.push_back(normalBinding);

            // binding 2: SSAOParams UBO
            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = 2;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            ssaoDsDesc.bindings.push_back(paramsBinding);

            // binding 3: SampleKernel UBO
            RHI::DescriptorBinding kernelBinding;
            kernelBinding.binding = 3;
            kernelBinding.type = RHI::ResourceBindType::ConstantBuffer;
            kernelBinding.stages = RHI::ShaderStage::Pixel;
            ssaoDsDesc.bindings.push_back(kernelBinding);

            // binding 4: noiseTexture (combined image sampler)
            RHI::DescriptorBinding noiseBinding;
            noiseBinding.binding = 4;
            noiseBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            noiseBinding.stages = RHI::ShaderStage::Pixel;
            ssaoDsDesc.bindings.push_back(noiseBinding);

            m_SSAODescriptorSet = m_Device->CreateDescriptorSet(ssaoDsDesc);

            if (m_SSAODescriptorSet)
            {
                m_SSAODescriptorSet->BindConstantBuffer(2, m_SSAOParamsBuffer, 0, SSAO_PARAMS_SIZE);
                m_SSAODescriptorSet->BindConstantBuffer(3, m_KernelBuffer, 0, KERNEL_BUFFER_SIZE);
                // ノイズテクスチャはここでバインド（変わらないため）
                if (m_NoiseTexture)
                {
                    m_SSAODescriptorSet->BindTexture(4, m_NoiseTexture);
                    m_SSAODescriptorSet->BindSampler(4, m_NearestRepeatSampler);
                }
            }
        }

        // ---- SSAOパイプライン ----
        {
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
        }

        // ========================================
        // ブラーパス用リソース
        // ========================================
        {
            RHI::RenderPassDesc rpDesc;
            RHI::AttachmentDesc colorAttach;
            colorAttach.format = m_Settings.OutputFormat;
            colorAttach.isDepthStencil = false;
            colorAttach.clear = false;
            colorAttach.loadOp = RHI::AttachmentLoadOp::DontCare;
            colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttach.initialState = RHI::ResourceState::Undefined;
            colorAttach.finalState = RHI::ResourceState::ShaderResource;
            rpDesc.colorAttachments.push_back(colorAttach);
            rpDesc.hasDepthStencil = false;

            m_BlurRenderPass = m_Device->CreateRenderPass(rpDesc);

            RHI::FramebufferDesc fbDesc;
            fbDesc.renderPass = m_BlurRenderPass;
            fbDesc.colorTargets.push_back(m_SSAOBlurredTexture);
            fbDesc.width = width;
            fbDesc.height = height;
            m_BlurFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        }

        // ---- ブラーディスクリプタセット ----
        // ---- ブラーディスクリプタセット ----
        RHI::DescriptorSetDesc blurDsDesc;
        {
            // binding 0: ssaoInput (combined image sampler)
            RHI::DescriptorBinding ssaoBinding;
            ssaoBinding.binding = 0;
            ssaoBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            ssaoBinding.stages = RHI::ShaderStage::Pixel;
            blurDsDesc.bindings.push_back(ssaoBinding);

            // binding 1: gbufferDepth (combined image sampler)
            RHI::DescriptorBinding depthBinding;
            depthBinding.binding = 1;
            depthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            depthBinding.stages = RHI::ShaderStage::Pixel;
            blurDsDesc.bindings.push_back(depthBinding);

            // binding 2: BlurParams UBO
            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = 2;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            blurDsDesc.bindings.push_back(paramsBinding);

            m_BlurDescriptorSet = m_Device->CreateDescriptorSet(blurDsDesc);

            if (m_BlurDescriptorSet)
            {
                m_BlurDescriptorSet->BindConstantBuffer(2, m_BlurParamsBuffer, 0, BLUR_PARAMS_SIZE);
            }
        }

        // ---- ブラーパイプライン ----
        {
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
        }

        NORVES_LOG_INFO("SSAOPass", "Resources created");
    }

    // ========================================
    // Execute
    // ========================================

    void SSAOPass::Execute(ViewRenderContext& context)
    {
        if (!context.CommandList || !m_SSAOPipeline || !m_BlurPipeline)
        {
            return;
        }

        // GBufferテクスチャ取得
        RHI::TexturePtr depthPtr;
        RHI::TexturePtr normalPtr;
        if (context.SharedResources)
        {
            depthPtr = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            normalPtr = context.SharedResources->GetTexturePtr("GBuffer_Normal");
        }

        if (!depthPtr || !normalPtr)
        {
            NORVES_LOG_WARNING("SSAOPass", "GBuffer textures not available, skipping SSAO");
            return;
        }

        // ========================================
        // パス1: SSAOサンプリング
        // ========================================

        // パラメータ更新
        GPUSSAOParams ssaoParams = {};

        // RowMajor → ColumnMajor転置ヘルパー
        auto TransposeToFloat = [](const NorvesLib::Math::Matrix4x4& mat, float* out)
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    out[col * 4 + row] = mat.m[row][col];
                }
            }
        };

        // プロジェクション行列を計算（CameraProxyのパラメータから）
        if (context.MainCamera)
        {
            using namespace NorvesLib::Math;

            const auto& cam = *context.MainCamera;
            float aspectRatio = (m_CurrentHeight > 0)
                                    ? static_cast<float>(m_CurrentWidth) / static_cast<float>(m_CurrentHeight)
                                    : 16.0f / 9.0f;
            float fovRadians = cam.FieldOfView * (3.14159265f / 180.0f);
            Matrix4x4 projMat = MatrixUtils::CreatePerspectiveFieldOfView(
                fovRadians, aspectRatio, cam.NearPlane, cam.FarPlane);

            // Vulkan Projection修正（Y反転、Z範囲）
            projMat.m22 *= -1.0f;
            projMat.m32 *= -1.0f;
            projMat.m11 *= -1.0f;

            float projData[16];
            TransposeToFloat(projMat, projData);
            std::memcpy(ssaoParams.projection, projData, sizeof(projData));

            // 逆プロジェクション行列
            Matrix4x4 invProjMat = MatrixUtils::Inverse(projMat);
            float invProjData[16];
            TransposeToFloat(invProjMat, invProjData);
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

        // GBufferテクスチャをバインド
        m_SSAODescriptorSet->BindTexture(0, depthPtr);
        m_SSAODescriptorSet->BindSampler(0, m_LinearClampSampler);
        m_SSAODescriptorSet->BindTexture(1, normalPtr);
        m_SSAODescriptorSet->BindSampler(1, m_LinearClampSampler);
        m_SSAODescriptorSet->Update();

        // SSAOレンダーパス実行
        context.CommandList->BeginRenderPass(m_SSAORenderPass, m_SSAOFramebuffer);

        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_CurrentWidth);
        viewport.height = static_cast<float>(m_CurrentHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        context.CommandList->SetViewport(viewport);

        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_CurrentWidth);
        scissor.bottom = static_cast<int32_t>(m_CurrentHeight);
        context.CommandList->SetScissor(scissor);

        context.CommandList->SetPipeline(m_SSAOPipeline);
        context.CommandList->SetDescriptorSet(m_SSAODescriptorSet, 0);
        context.CommandList->Draw(3, 0);
        context.CommandList->EndRenderPass();

        // ========================================
        // パス2: ブラー
        // ========================================

        GPUBlurParams blurParams = {};
        blurParams.texelSize[0] = 1.0f / static_cast<float>(m_CurrentWidth);
        blurParams.texelSize[1] = 1.0f / static_cast<float>(m_CurrentHeight);
        blurParams.texelSize[2] = 0.0f;
        blurParams.texelSize[3] = 0.0f;
        m_BlurParamsBuffer->Update(&blurParams, BLUR_PARAMS_SIZE);

        // 生AOをバインド
        m_BlurDescriptorSet->BindTexture(0, m_SSAORawTexture);
        m_BlurDescriptorSet->BindSampler(0, m_LinearClampSampler);
        m_BlurDescriptorSet->BindTexture(1, depthPtr);
        m_BlurDescriptorSet->BindSampler(1, m_LinearClampSampler);
        m_BlurDescriptorSet->Update();

        context.CommandList->BeginRenderPass(m_BlurRenderPass, m_BlurFramebuffer);
        context.CommandList->SetViewport(viewport);
        context.CommandList->SetScissor(scissor);
        context.CommandList->SetPipeline(m_BlurPipeline);
        context.CommandList->SetDescriptorSet(m_BlurDescriptorSet, 0);
        context.CommandList->Draw(3, 0);
        context.CommandList->EndRenderPass();

        // 結果をSharedResourceRegistryに登録
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SSAO", m_SSAOBlurredTexture);
        }
    }

} // namespace NorvesLib::Core::Rendering
