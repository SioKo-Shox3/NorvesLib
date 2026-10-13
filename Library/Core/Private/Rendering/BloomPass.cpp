#include "Rendering/BloomPass.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SSRPass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/TransientResourcePool.h"
#include "Logging/LogMacros.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    // ========================================
    // GPU側パラメータ構造体（シェーダーのUBOレイアウトに対応）
    // ========================================

    /** @brief 合成パラメータUBO（std140アライメント、bloom.frag の BloomCompositeParams） */
    struct GPUBloomParams
    {
        float intensity;
        float radius;
        float thresholdMode;
        float inverseMipCount;
        float lensDirtIntensity;
        float lensDirtThreshold;
        float lensDirtSceneRatio;
        float _pad0;
    };

    /** @brief 縮小パラメータUBO（bloom_downsample.frag の BloomDownsampleParams） */
    struct GPUBloomDownsampleParams
    {
        float firstLevel;
        float threshold;
        float softKnee;
        float _pad0;
    };

    /** @brief 拡大パラメータUBO（bloom_upsample.frag の BloomUpsampleParams） */
    struct GPUBloomUpsampleParams
    {
        float radius;
        float _pad0;
        float _pad1;
        float _pad2;
    };

    static constexpr uint32_t BLOOM_PARAMS_SIZE = sizeof(GPUBloomParams);
    static constexpr uint32_t BLOOM_DOWNSAMPLE_PARAMS_SIZE = sizeof(GPUBloomDownsampleParams);
    static constexpr uint32_t BLOOM_UPSAMPLE_PARAMS_SIZE = sizeof(GPUBloomUpsampleParams);

    namespace
    {
        constexpr uint32_t kLensDirtTextureSize = 512;

        // 決まった種から同じ列を返す乱数（xorshift32）。ダートの模様を起動ごとに変えない。
        class LensDirtRandom
        {
        public:
            explicit LensDirtRandom(uint32_t seed) : m_State(seed != 0u ? seed : 1u) {}

            float Next01()
            {
                m_State ^= m_State << 13;
                m_State ^= m_State >> 17;
                m_State ^= m_State << 5;
                return static_cast<float>(m_State >> 8) * (1.0f / 16777216.0f);
            }

            float Range(float minValue, float maxValue) { return minValue + (maxValue - minValue) * Next01(); }

        private:
            uint32_t m_State;
        };

        // レンズの前玉に付いた汚れの模様を作る（RGBA8、リニアな値）。
        // 大小の丸いしみ（縁がやわらかく、一部は縁がわずかに明るい）と、薄く広いくもりを重ねる。
        // 値は加算して1で頭打ちにし、色はしみごとに暖色・寒色へわずかに寄せる。
        void GenerateLensDirtPixels(uint32_t size, VariableArray<uint8_t> &outPixels)
        {
            VariableArray<float> accum(static_cast<size_t>(size) * size * 3u, 0.0f);
            LensDirtRandom random(0x9E3779B9u);

            auto addSpot = [&](float centerX, float centerY, float radius, float edgeWidth, float brightness, float rim,
                               float tintR, float tintB) {
                const int minX = std::max(0, static_cast<int>(std::floor(centerX - radius)));
                const int maxX = std::min(static_cast<int>(size) - 1, static_cast<int>(std::ceil(centerX + radius)));
                const int minY = std::max(0, static_cast<int>(std::floor(centerY - radius)));
                const int maxY = std::min(static_cast<int>(size) - 1, static_cast<int>(std::ceil(centerY + radius)));
                for (int y = minY; y <= maxY; ++y)
                {
                    for (int x = minX; x <= maxX; ++x)
                    {
                        const float dx = (static_cast<float>(x) + 0.5f - centerX) / radius;
                        const float dy = (static_cast<float>(y) + 0.5f - centerY) / radius;
                        const float distance = std::sqrt(dx * dx + dy * dy);
                        if (distance >= 1.0f)
                        {
                            continue;
                        }
                        // 外側の edgeWidth の割合でなめらかに0へ落とし、縁の近くだけ rim の割合で明るくする。
                        const float edge = std::clamp((1.0f - distance) / edgeWidth, 0.0f, 1.0f);
                        const float falloff = edge * edge * (3.0f - 2.0f * edge);
                        const float rimWeight = std::exp(-std::pow((distance - 0.85f) / 0.08f, 2.0f));
                        const float value = brightness * falloff * (1.0f + rim * rimWeight);
                        float *pixel = &accum[(static_cast<size_t>(y) * size + static_cast<size_t>(x)) * 3u];
                        pixel[0] += value * tintR;
                        pixel[1] += value;
                        pixel[2] += value * tintB;
                    }
                }
            };

            const float extent = static_cast<float>(size);
            // 薄く広いくもり
            for (int i = 0; i < 14; ++i)
            {
                const float tint = random.Range(-0.08f, 0.08f);
                addSpot(random.Range(0.0f, extent), random.Range(0.0f, extent), extent * random.Range(0.12f, 0.28f), 1.0f,
                        random.Range(0.02f, 0.05f), 0.0f, 1.0f + tint, 1.0f - tint);
            }
            // 中くらいのしみ
            for (int i = 0; i < 70; ++i)
            {
                const float tint = random.Range(-0.12f, 0.12f);
                addSpot(random.Range(0.0f, extent), random.Range(0.0f, extent), extent * random.Range(0.02f, 0.07f), 0.3f,
                        random.Range(0.25f, 0.6f), random.Range(0.2f, 0.8f), 1.0f + tint, 1.0f - tint);
            }
            // 小さな粒
            for (int i = 0; i < 260; ++i)
            {
                const float tint = random.Range(-0.1f, 0.1f);
                addSpot(random.Range(0.0f, extent), random.Range(0.0f, extent), extent * random.Range(0.004f, 0.012f), 0.5f,
                        random.Range(0.3f, 0.8f), 0.0f, 1.0f + tint, 1.0f - tint);
            }

            outPixels.resize(static_cast<size_t>(size) * size * 4u);
            for (size_t i = 0; i < static_cast<size_t>(size) * size; ++i)
            {
                for (size_t channel = 0; channel < 3u; ++channel)
                {
                    const float value = std::clamp(accum[i * 3u + channel], 0.0f, 1.0f);
                    outPixels[i * 4u + channel] = static_cast<uint8_t>(std::lround(value * 255.0f));
                }
                outPixels[i * 4u + 3u] = 255u;
            }
        }

        RHI::DescriptorSetDesc MakeSamplerParamsLayout(uint32_t samplerCount)
        {
            RHI::DescriptorSetDesc dsDesc;
            for (uint32_t binding = 0; binding < samplerCount; ++binding)
            {
                RHI::DescriptorBinding samplerBinding;
                samplerBinding.binding = binding;
                samplerBinding.type = RHI::ResourceBindType::CombinedImageSampler;
                samplerBinding.stages = RHI::ShaderStage::Pixel;
                dsDesc.bindings.push_back(samplerBinding);
            }

            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = samplerCount;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(paramsBinding);
            return dsDesc;
        }

        RHI::PipelinePtr CreateFullscreenPipeline(RHI::IDevice *device,
                                                  const RHI::ShaderPtr &vertexShader,
                                                  const RHI::ShaderPtr &pixelShader,
                                                  const RHI::RenderPassPtr &renderPass,
                                                  const RHI::DescriptorSetDesc &layout)
        {
            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = vertexShader;
            pipelineDesc.pixelShader = pixelShader;
            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            pipelineDesc.rasterState.lineWidth = 1.0f;
            pipelineDesc.depthStencilState.depthTestEnable = false;
            pipelineDesc.depthStencilState.depthWriteEnable = false;

            // ブレンド無効（加算はシェーダー内で行う）
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);

            pipelineDesc.renderPass = renderPass;
            pipelineDesc.descriptorSetLayouts.push_back(layout);
            return device->CreateGraphicsPipeline(pipelineDesc);
        }

        RHI::Viewport MakeMipViewport(uint32_t width, uint32_t height)
        {
            RHI::Viewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(width);
            viewport.height = static_cast<float>(height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            return viewport;
        }

        RHI::ScissorRect MakeMipScissor(uint32_t width, uint32_t height)
        {
            RHI::ScissorRect scissor;
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = static_cast<int32_t>(width);
            scissor.bottom = static_cast<int32_t>(height);
            return scissor;
        }
    }

    BloomPass::BloomPass(const BloomSettings &settings)
        : m_Settings(settings)
    {
    }

    BloomPass::~BloomPass()
    {
        Shutdown();
    }

    bool BloomPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("BloomPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // フルスクリーン頂点シェーダー作成（LightingPassとキャッシュ共有）
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("BloomPass", "ShaderManager is null");
            return false;
        }

        m_BloomVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_BloomVertexShader)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create fullscreen vertex shader");
            return false;
        }

        // ========================================
        // ブルームフラグメントシェーダー作成
        // ========================================
        m_BloomFragmentShader = context.ShaderMgr->LoadShader("bloom.frag", RHI::ShaderStage::Pixel);
        if (!m_BloomFragmentShader)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom fragment shader");
            return false;
        }

        m_DownsampleFragmentShader = context.ShaderMgr->LoadShader("bloom_downsample.frag", RHI::ShaderStage::Pixel);
        m_UpsampleFragmentShader = context.ShaderMgr->LoadShader("bloom_upsample.frag", RHI::ShaderStage::Pixel);
        if (!m_DownsampleFragmentShader || !m_UpsampleFragmentShader)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom downsample/upsample fragment shaders");
            return false;
        }

        // ========================================
        // SceneColorサンプラー作成（リニアフィルタ）
        // ========================================
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Linear;
        samplerDesc.filterMag = RHI::FilterMode::Linear;
        samplerDesc.filterMip = RHI::FilterMode::Linear;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;

        m_SceneColorSampler = m_Device->CreateSampler(samplerDesc);
        if (!m_SceneColorSampler)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create scene color sampler");
            return false;
        }

        // ========================================
        // 記述子セットのレイアウト（同じ形のパスと共有する 1 つをデバイスから引く。セットと定数は毎フレーム借りる）
        // ========================================
        m_CompositeDescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(MakeSamplerParamsLayout(3));
        m_DownsampleDescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(MakeSamplerParamsLayout(1));
        m_UpsampleDescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(MakeSamplerParamsLayout(2));
        if (!m_CompositeDescriptorSetLayout || !m_DownsampleDescriptorSetLayout || !m_UpsampleDescriptorSetLayout)
        {
            NORVES_LOG_ERROR("BloomPass", "記述子セットのレイアウトを作れませんでした");
            return false;
        }

        // ========================================
        // レンズダートの模様（手続きで作り、合成で常にバインドする。強さ0なら読まれても寄与しない）
        // ========================================
        RHI::TextureDesc lensDirtDesc;
        lensDirtDesc.Width = kLensDirtTextureSize;
        lensDirtDesc.Height = kLensDirtTextureSize;
        lensDirtDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
        lensDirtDesc.Usage = RHI::ResourceUsage::ShaderRead;
        lensDirtDesc.DebugName = "BloomLensDirt";
        m_LensDirtTexture = m_Device->CreateTexture(lensDirtDesc);
        if (!m_LensDirtTexture)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create lens dirt texture");
            return false;
        }
        VariableArray<uint8_t> lensDirtPixels;
        GenerateLensDirtPixels(kLensDirtTextureSize, lensDirtPixels);
        m_LensDirtTexture->Update(lensDirtPixels.data(),
                                  kLensDirtTextureSize * 4u,
                                  kLensDirtTextureSize * kLensDirtTextureSize * 4u);

        m_bInitialized = true;
        NORVES_LOG_INFO("BloomPass", "BloomPass initialized");
        return true;
    }

    void BloomPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        ReleaseMipChain();
        m_DownsampleFragmentShader.reset();
        m_UpsampleFragmentShader.reset();
        m_OutputTexture.reset();
        m_BloomRenderPass.reset();
        m_BloomFramebuffer.reset();
        m_BloomPipeline.reset();
        m_BloomVertexShader.reset();
        m_BloomFragmentShader.reset();
        m_UnownedDescriptorSets.clear();
        m_UnownedBuffers.clear();
        m_CompositeDescriptorSetLayout.reset();
        m_DownsampleDescriptorSetLayout.reset();
        m_UpsampleDescriptorSetLayout.reset();
        m_SceneColorSampler.reset();
        m_LensDirtTexture.reset();
        m_Device = nullptr;
        m_OutputHandle = {};
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferOutputTexture = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("BloomPass", "BloomPass shutdown");
    }

    void BloomPass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.GetActiveRenderWidth();
        uint32_t height = context.GetActiveRenderHeight();

        if (width == 0 || height == 0 || !m_Device)
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
            // HDR出力テクスチャ作成（ToneMappingの前なのでHDR維持）
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "BloomOutput"));

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create output texture");
                return;
            }
        }

        const bool bNeedsPrepare =
            bNeedsOutputTexture ||
            !m_BloomRenderPass ||
            !m_BloomFramebuffer ||
            !m_BloomPipeline ||
            m_FramebufferOutputTexture != m_OutputTexture.get() ||
            m_bRenderPassUsesRenderGraphInitialState;

        if (bNeedsPrepare)
        {
            if (PrepareResources(width, height, m_OutputTexture, false))
            {
                NORVES_LOG_INFO("BloomPass", "Bloom resources resized (%ux%u)", width, height);
            }
        }
    }

    bool BloomPass::PrepareResources(uint32_t width,
                                     uint32_t height,
                                     const RHI::TexturePtr& outputTexture,
                                     bool bUseRenderGraphInitialState)
    {
        if (!m_Device ||
            !outputTexture ||
            !m_BloomVertexShader ||
            !m_BloomFragmentShader)
        {
            return false;
        }

        if (!EnsureMipChain(width, height))
        {
            return false;
        }

        const bool bResourcesChanged =
            width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            outputTexture.get() != m_FramebufferOutputTexture ||
            bUseRenderGraphInitialState != m_bRenderPassUsesRenderGraphInitialState ||
            !m_BloomRenderPass ||
            !m_BloomFramebuffer ||
            !m_BloomPipeline;

        m_OutputTexture = outputTexture;
        if (!bResourcesChanged)
        {
            return true;
        }

        m_BloomRenderPass.reset();
        m_BloomFramebuffer.reset();
        m_BloomPipeline.reset();

        // ========================================
        // レンダーパス作成（1カラー、デプスなし）
        // ========================================
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

        m_BloomRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_BloomRenderPass)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom render pass");
            return false;
        }

        // ========================================
        // フレームバッファ作成
        // ========================================
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_BloomRenderPass;
        fbDesc.colorTargets.push_back(outputTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_BloomFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_BloomFramebuffer)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom framebuffer");
            return false;
        }

        // ========================================
        // ディスクリプタセットの形（セットは Execute のたびに借りる）
        // ========================================
        // binding 0: SceneColor（combined image sampler）
        // binding 1: 積み上げたブルーム（combined image sampler）
        // binding 2: レンズダートの模様（combined image sampler）
        // binding 3: BloomCompositeParams UBO
        const RHI::DescriptorSetDesc dsDesc = MakeSamplerParamsLayout(3);

        // ========================================
        // パイプライン作成（フルスクリーン描画）
        // ========================================
        m_BloomPipeline = CreateFullscreenPipeline(
            m_Device, m_BloomVertexShader, m_BloomFragmentShader, m_BloomRenderPass, dsDesc);
        if (!m_BloomPipeline)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom pipeline");
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_FramebufferOutputTexture = outputTexture.get();
        m_bRenderPassUsesRenderGraphInitialState = bUseRenderGraphInitialState;
        return true;
    }

    void BloomPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_BloomRenderPass || !m_BloomFramebuffer || !m_BloomPipeline)
        {
            NORVES_LOG_WARNING("BloomPass", "Bloom resources not ready, skipping");
            return;
        }

        // HDRシーンカラーをSharedResourceRegistryから取得（TexturePtr版）
        RHI::TexturePtr sceneColorPtr;
        if (context.SharedResources)
        {
            sceneColorPtr = context.SharedResources->GetTexturePtr("SceneColor");
        }

        if (!sceneColorPtr)
        {
            NORVES_LOG_WARNING("BloomPass", "SceneColor not available, skipping bloom");
            return;
        }

        ExecuteWithInput(context, sceneColorPtr, true);
    }

    void BloomPass::Declare(RenderGraphBuilder &builder)
    {
        m_bLegacyInputFallbackActive = false;
        const ViewRenderContext *context = builder.GetContext();
        const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
        const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

        m_InputSceneColorHandle = {};

        RGTextureHandle sceneColorHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::SSRSceneColor,
                                   sceneColorHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputSceneColorHandle = sceneColorHandle.ToResourceHandle();
        }
        else if (builder.TryReadTexture(RenderGraphResourceNames::SceneColor,
                                        sceneColorHandle,
                                        RHI::ResourceState::ShaderResource))
        {
            m_InputSceneColorHandle = sceneColorHandle.ToResourceHandle();
        }
        else if (m_InputPass)
        {
            const RGResourceHandle fallbackSceneColorHandle = m_InputPass->GetSceneColorHandle();
            if (fallbackSceneColorHandle.IsValid())
            {
                builder.Read(fallbackSceneColorHandle, RHI::ResourceState::ShaderResource);
                m_InputSceneColorHandle = fallbackSceneColorHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle outputHandle = builder.WriteTexture(
            RenderGraphResourceNames::BloomSceneColor,
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "BloomOutput"),
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);
        m_OutputHandle = outputHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void BloomPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
        if (!outputTexture)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to resolve native bloom output texture");
            return;
        }

        if (!PrepareResources(outputTexture->GetWidth(), outputTexture->GetHeight(), outputTexture, true))
        {
            return;
        }

        RHI::TexturePtr sceneColorPtr;
        if (m_InputSceneColorHandle.IsValid())
        {
            sceneColorPtr = resources.GetTexture(m_InputSceneColorHandle);
        }
        if (m_InputPass)
        {
            const RGResourceHandle sceneColorHandle = m_InputPass->GetSceneColorHandle();
            if (!sceneColorPtr && sceneColorHandle.IsValid())
            {
                sceneColorPtr = resources.GetTexture(sceneColorHandle);
            }
        }

        bool bUsedSharedResourceFallback = false;
        if (!sceneColorPtr && context.SharedResources)
        {
            sceneColorPtr = context.SharedResources->GetTexturePtr("SceneColor");
            bUsedSharedResourceFallback = sceneColorPtr != nullptr;
        }

        if (!sceneColorPtr)
        {
            EnqueueEmptyNativePass(context);
            return;
        }

        ExecuteWithInput(context,
                         sceneColorPtr,
                         m_bLegacyInputFallbackActive || bUsedSharedResourceFallback);
    }

    void BloomPass::ExecuteWithInput(ViewRenderContext &context,
                                     const RHI::TexturePtr& sceneColorPtr,
                                     bool bRegisterLegacyBridge)
    {
        if (!m_BloomRenderPass || !m_BloomFramebuffer || !m_BloomPipeline || m_MipLevels.empty())
        {
            NORVES_LOG_WARNING("BloomPass", "Bloom resources not ready, skipping");
            return;
        }

        // 縮小・拡大の段を積む（合成が読む最上段のテクスチャを毎フレーム書き直す）
        if (!EnqueueMipChain(context, sceneColorPtr))
        {
            NORVES_LOG_ERROR("BloomPass", "縮小・拡大の段の定数か記述子セットを借りられないため、このフレームはブルームを描きません");
            EnqueueEmptyNativePass(context);
            return;
        }

        // 合成パラメータ更新
        GPUBloomParams params = {};
        const bool bDebugPostProcessBypass =
            IsDebugPostProcessBypassMode(context.GetActiveDebugMode());
        params.intensity = bDebugPostProcessBypass ? 0.0f : m_Settings.Intensity;
        params.radius = m_Settings.Radius;
        params.thresholdMode = m_Settings.Threshold > 0.0f ? 1.0f : 0.0f;
        params.inverseMipCount = 1.0f / static_cast<float>(m_MipLevels.size());
        // レンズダートはカメラのレンズ効果の値が正ならそれを、無ければ View の設定を使う（有限でない値・負は0）。
        float lensDirtIntensity = m_Settings.LensDirtIntensity;
        const CameraProxy *activeCamera = context.GetActiveCamera();
        if (activeCamera != nullptr && activeCamera->LensEffects.LensDirtIntensity > 0.0f)
        {
            lensDirtIntensity = activeCamera->LensEffects.LensDirtIntensity;
        }
        params.lensDirtIntensity =
            (bDebugPostProcessBypass || !std::isfinite(lensDirtIntensity)) ? 0.0f : std::max(lensDirtIntensity, 0.0f);
        params.lensDirtThreshold =
            std::isfinite(m_Settings.LensDirtThreshold) ? std::max(m_Settings.LensDirtThreshold, 0.0f) : 0.0f;
        params.lensDirtSceneRatio =
            std::isfinite(m_Settings.LensDirtSceneRatio) ? std::max(m_Settings.LensDirtSceneRatio, 0.0f) : 0.0f;

        // 合成の定数と記述子セットはこの Execute の分をフレームの持ち主から借りる
        const RHI::DescriptorSetPtr compositeDescriptorSet =
            BorrowDescriptorSet(context, m_CompositeDescriptorSetLayout, MakeSamplerParamsLayout(3));
        if (!compositeDescriptorSet ||
            !BindFrameConstants(context, *compositeDescriptorSet, 3, &params, BLOOM_PARAMS_SIZE,
                                "BloomParamsUBO.Unowned"))
        {
            NORVES_LOG_ERROR("BloomPass", "合成の定数か記述子セットを借りられないため、このフレームはブルームを描きません");
            EnqueueEmptyNativePass(context);
            return;
        }

        // 最上段の拡大結果（1段だけのときは縮小結果）を合成で読む
        const BloomMipLevel &topLevel = m_MipLevels[0];
        const RHI::TexturePtr &bloomTexture =
            topLevel.UpTexture ? topLevel.UpTexture : topLevel.DownTexture;

        compositeDescriptorSet->BindTexture(0, sceneColorPtr);
        compositeDescriptorSet->BindSampler(0, m_SceneColorSampler);
        compositeDescriptorSet->BindTexture(1, bloomTexture);
        compositeDescriptorSet->BindSampler(1, m_SceneColorSampler);
        compositeDescriptorSet->BindTexture(2, m_LensDirtTexture);
        compositeDescriptorSet->BindSampler(2, m_SceneColorSampler);
        compositeDescriptorSet->Update();

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        context.EnqueueFullscreenPass(m_BloomRenderPass,
                                      m_BloomFramebuffer,
                                      viewport,
                                      scissor,
                                      m_BloomPipeline,
                                      compositeDescriptorSet);

        // ブルーム適用済みSceneColorとしてSharedResourceRegistryに上書き登録
        // → 後段のToneMappingPassが "SceneColor" として読み取る
        if (bRegisterLegacyBridge && context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", m_OutputTexture);
        }
    }

    bool BloomPass::EnsureMipChain(uint32_t width, uint32_t height)
    {
        const uint32_t requestedCount =
            m_Settings.MipCount < 1u ? 1u
                                     : (m_Settings.MipCount > MaxBloomMipCount ? MaxBloomMipCount
                                                                               : m_Settings.MipCount);

        if (!m_MipLevels.empty() &&
            m_MipChainWidth == width &&
            m_MipChainHeight == height &&
            m_MipChainRequestedCount == requestedCount)
        {
            return true;
        }

        ReleaseMipChain();

        if (!m_Device || !m_BloomVertexShader || !m_DownsampleFragmentShader || !m_UpsampleFragmentShader ||
            width == 0 || height == 0)
        {
            return false;
        }

        // 段の寸法を決める（縦横とも1画素まで。1×1に達したらそれ以上は縮小しない）
        VariableArray<BloomMipLevel> levels;
        uint32_t levelWidth = width;
        uint32_t levelHeight = height;
        for (uint32_t level = 0; level < requestedCount; ++level)
        {
            if (levelWidth <= 1 && levelHeight <= 1)
            {
                break;
            }
            levelWidth = levelWidth > 1 ? levelWidth / 2 : 1;
            levelHeight = levelHeight > 1 ? levelHeight / 2 : 1;

            BloomMipLevel mip;
            mip.Width = levelWidth;
            mip.Height = levelHeight;
            levels.push_back(mip);
        }

        if (levels.empty())
        {
            return false;
        }

        // 段の書き込み用レンダーパス（前の内容は捨て、書いた後はシェーダーから読む）
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

        m_MipRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_MipRenderPass)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom mip render pass");
            return false;
        }

        const RHI::DescriptorSetDesc downLayout = MakeSamplerParamsLayout(1);
        const RHI::DescriptorSetDesc upLayout = MakeSamplerParamsLayout(2);

        m_DownsamplePipeline = CreateFullscreenPipeline(
            m_Device, m_BloomVertexShader, m_DownsampleFragmentShader, m_MipRenderPass, downLayout);
        m_UpsamplePipeline = CreateFullscreenPipeline(
            m_Device, m_BloomVertexShader, m_UpsampleFragmentShader, m_MipRenderPass, upLayout);
        if (!m_DownsamplePipeline || !m_UpsamplePipeline)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom mip pipelines");
            ReleaseMipChain();
            return false;
        }

        const size_t levelCount = levels.size();
        for (size_t level = 0; level < levelCount; ++level)
        {
            BloomMipLevel &mip = levels[level];

            mip.DownTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(mip.Width, mip.Height, m_Settings.OutputFormat, "BloomDownsample"));
            if (!mip.DownTexture)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create bloom downsample level %zu", level);
                ReleaseMipChain();
                return false;
            }

            RHI::FramebufferDesc downFbDesc;
            downFbDesc.renderPass = m_MipRenderPass;
            downFbDesc.colorTargets.push_back(mip.DownTexture);
            downFbDesc.width = mip.Width;
            downFbDesc.height = mip.Height;
            mip.DownFramebuffer = m_Device->CreateFramebuffer(downFbDesc);
            if (!mip.DownFramebuffer)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create bloom downsample framebuffer %zu", level);
                ReleaseMipChain();
                return false;
            }

            // 最下段は拡大の元になるだけなので拡大結果を持たない
            if (level + 1 >= levelCount)
            {
                continue;
            }

            mip.UpTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(mip.Width, mip.Height, m_Settings.OutputFormat, "BloomUpsample"));
            if (!mip.UpTexture)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create bloom upsample level %zu", level);
                ReleaseMipChain();
                return false;
            }

            RHI::FramebufferDesc upFbDesc;
            upFbDesc.renderPass = m_MipRenderPass;
            upFbDesc.colorTargets.push_back(mip.UpTexture);
            upFbDesc.width = mip.Width;
            upFbDesc.height = mip.Height;
            mip.UpFramebuffer = m_Device->CreateFramebuffer(upFbDesc);
            if (!mip.UpFramebuffer)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create bloom upsample framebuffer %zu", level);
                ReleaseMipChain();
                return false;
            }
        }

        m_MipLevels = std::move(levels);
        m_MipChainWidth = width;
        m_MipChainHeight = height;
        m_MipChainRequestedCount = requestedCount;
        return true;
    }

    void BloomPass::ReleaseMipChain()
    {
        m_MipLevels.clear();
        m_DownsamplePipeline.reset();
        m_UpsamplePipeline.reset();
        m_MipRenderPass.reset();
        m_MipChainWidth = 0;
        m_MipChainHeight = 0;
        m_MipChainRequestedCount = 0;
    }

    bool BloomPass::EnqueueMipChain(ViewRenderContext &context, const RHI::TexturePtr& sceneColorPtr)
    {
        const size_t levelCount = m_MipLevels.size();
        const RHI::DescriptorSetDesc downLayout = MakeSamplerParamsLayout(1);
        const RHI::DescriptorSetDesc upLayout = MakeSamplerParamsLayout(2);

        // 段ごとの定数と記述子セットは、この Execute の分をフレームの持ち主から借りる
        // 縮小: SceneColor → 段0 → 段1 → …
        for (size_t level = 0; level < levelCount; ++level)
        {
            const BloomMipLevel &mip = m_MipLevels[level];

            GPUBloomDownsampleParams downParams = {};
            downParams.firstLevel = level == 0 ? 1.0f : 0.0f;
            downParams.threshold = m_Settings.Threshold;
            downParams.softKnee = m_Settings.SoftKnee;

            const RHI::DescriptorSetPtr downDescriptorSet =
                BorrowDescriptorSet(context, m_DownsampleDescriptorSetLayout, downLayout);
            if (!downDescriptorSet ||
                !BindFrameConstants(context, *downDescriptorSet, 1, &downParams, BLOOM_DOWNSAMPLE_PARAMS_SIZE,
                                    "BloomDownsampleParamsUBO.Unowned"))
            {
                return false;
            }

            const RHI::TexturePtr &source = level == 0 ? sceneColorPtr : m_MipLevels[level - 1].DownTexture;
            downDescriptorSet->BindTexture(0, source);
            downDescriptorSet->BindSampler(0, m_SceneColorSampler);
            downDescriptorSet->Update();

            context.EnqueueFullscreenPass(m_MipRenderPass,
                                          mip.DownFramebuffer,
                                          MakeMipViewport(mip.Width, mip.Height),
                                          MakeMipScissor(mip.Width, mip.Height),
                                          m_DownsamplePipeline,
                                          downDescriptorSet);
        }

        // 拡大: 最下段から上へ、下の段をテントで広げてこの段の縮小結果へ加える
        for (size_t level = levelCount - 1; level-- > 0;)
        {
            const BloomMipLevel &mip = m_MipLevels[level];
            const BloomMipLevel &lower = m_MipLevels[level + 1];

            GPUBloomUpsampleParams upParams = {};
            upParams.radius = m_Settings.Radius;

            const RHI::DescriptorSetPtr upDescriptorSet =
                BorrowDescriptorSet(context, m_UpsampleDescriptorSetLayout, upLayout);
            if (!upDescriptorSet ||
                !BindFrameConstants(context, *upDescriptorSet, 2, &upParams, BLOOM_UPSAMPLE_PARAMS_SIZE,
                                    "BloomUpsampleParamsUBO.Unowned"))
            {
                return false;
            }

            const RHI::TexturePtr &lowerTexture = lower.UpTexture ? lower.UpTexture : lower.DownTexture;
            upDescriptorSet->BindTexture(0, lowerTexture);
            upDescriptorSet->BindSampler(0, m_SceneColorSampler);
            upDescriptorSet->BindTexture(1, mip.DownTexture);
            upDescriptorSet->BindSampler(1, m_SceneColorSampler);
            upDescriptorSet->Update();

            context.EnqueueFullscreenPass(m_MipRenderPass,
                                          mip.UpFramebuffer,
                                          MakeMipViewport(mip.Width, mip.Height),
                                          MakeMipScissor(mip.Width, mip.Height),
                                          m_UpsamplePipeline,
                                          upDescriptorSet);
        }
        return true;
    }

    RHI::DescriptorSetPtr BloomPass::BorrowDescriptorSet(ViewRenderContext &context,
                                                         const RHI::DescriptorSetLayoutPtr &layout,
                                                         const RHI::DescriptorSetDesc &desc)
    {
        if (FrameResourceLane *frameResources = context.FrameResources)
        {
            return frameResources->AllocateDescriptorSet(layout).Set;
        }
        // 持ち主の無い文脈（RenderingCoordinator を通さない検査）: 使い回さないセットを作る。提出の完了を知る手段が
        // 無いので、記録したコマンドが読み終える前に破棄されないようパスを手放すまで持つ
        RHI::DescriptorSetPtr descriptorSet = m_Device ? m_Device->CreateDescriptorSet(desc) : RHI::DescriptorSetPtr{};
        if (descriptorSet)
        {
            m_UnownedDescriptorSets.push_back(descriptorSet);
        }
        return descriptorSet;
    }

    bool BloomPass::BindFrameConstants(ViewRenderContext &context,
                                       RHI::IDescriptorSet &set,
                                       uint32_t binding,
                                       const void *data,
                                       uint32_t size,
                                       const char *unownedDebugName)
    {
        if (FrameResourceLane *frameResources = context.FrameResources)
        {
            const FrameUploadAllocation allocation =
                frameResources->AllocateUploadData(data, size, FrameUploadUsage::Constants);
            if (!allocation.IsValid())
            {
                return false;
            }
            set.BindConstantBuffer(binding, allocation.Buffer, allocation.GetOffset32(), allocation.GetSize32());
            return true;
        }
        // 持ち主の無い文脈: 使い回さない専用のバッファを作り、パスを手放すまで持つ（BorrowDescriptorSet と同じ理由）
        RHI::BufferPtr buffer = m_Device ? m_Device->CreateBuffer(RHI::BufferDesc(
                                               size, RHI::ResourceUsage::ConstantBuffer, true, unownedDebugName))
                                         : RHI::BufferPtr{};
        if (!buffer)
        {
            return false;
        }
        buffer->Update(data, size);
        set.BindConstantBuffer(binding, buffer, 0, size);
        m_UnownedBuffers.push_back(std::move(buffer));
        return true;
    }

    bool BloomPass::EnqueueEmptyNativePass(ViewRenderContext &context) const
    {
        if (!m_BloomRenderPass || !m_BloomFramebuffer)
        {
            return false;
        }

        context.EnqueueFullscreenPass(m_BloomRenderPass,
                                      m_BloomFramebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      nullptr,
                                      nullptr);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
