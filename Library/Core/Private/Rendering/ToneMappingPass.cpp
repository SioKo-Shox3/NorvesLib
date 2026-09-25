#include "Rendering/ToneMappingPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/ToneMappingPassGpuTypes.h"
#include "Rendering/BloomPass.h"
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

#include <cstring>
#include <exception>
#include <fstream>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // tonemapping.frag の operatorType と一致させる
        constexpr uint32_t Aces20LutOperatorType = 4u;
        constexpr uint32_t AcesFilmicOperatorType = 1u;

        // Scripts/BakeAcesOutputLut.py が書く見出し（32 byte、little-endian）
        constexpr char Aces20LutMagic[8] = {'N', 'L', 'U', 'T', '3', 'D', '0', '1'};
        constexpr uint32_t Aces20LutHeaderSize = 32u;
        constexpr uint32_t Aces20LutFormatRgba16F = 1u;
        constexpr uint32_t Aces20LutBytesPerTexel = 8u;
        // shaper はシェーダに固定してあるので、見出しの値が一致するLUTだけを受け入れる
        constexpr float Aces20LutShaperOffset = 0.00390625f; // 2^-8
        constexpr float Aces20LutShaperMax = 256.0f;

        struct Aces20LutHeader
        {
            char Magic[8];
            uint32_t Size;
            uint32_t Channels;
            uint32_t Format;
            uint32_t Reserved;
            float ShaperOffset;
            float ShaperMax;
        };
        static_assert(sizeof(Aces20LutHeader) == Aces20LutHeaderSize);

        Container::String ResolveAssetPath(const char* relativePath)
        {
            Container::String resolvedPath = relativePath;
#ifdef NORVES_ASSET_DIR
            resolvedPath = Container::String(NORVES_ASSET_DIR) + "/" + resolvedPath;
#endif
            return resolvedPath;
        }

        RHI::TexturePtr ResolveSharedTexturePtr(ViewRenderContext& context, const char* name)
        {
            if (!context.SharedResources)
            {
                return nullptr;
            }

            RHI::TexturePtr texturePtr = context.SharedResources->GetTexturePtr(name);
            if (texturePtr)
            {
                return texturePtr;
            }

            RHI::ITexture* rawTexture = context.SharedResources->GetTexture(name);
            if (!rawTexture)
            {
                return nullptr;
            }

            return RHI::TexturePtr(rawTexture, [](RHI::ITexture*) {});
        }
    } // namespace

    static constexpr uint32_t TONEMAPPING_PARAMS_SIZE = sizeof(GPUToneMappingParams);

    ToneMappingPass::ToneMappingPass(const ToneMappingSettings& settings)
        : m_Settings(settings)
    {
    }

    ToneMappingPass::~ToneMappingPass()
    {
        Shutdown();
    }

    bool ToneMappingPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // フルスクリーン頂点シェーダー作成（LightingPassとキャッシュ共有）
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "ShaderManager is null");
            return false;
        }

        m_ToneMappingVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_ToneMappingVertexShader)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create fullscreen vertex shader");
            return false;
        }

        // ========================================
        // トーンマッピングフラグメントシェーダー作成
        // ========================================
        m_ToneMappingFragmentShader = context.ShaderMgr->LoadShader("tonemapping.frag", RHI::ShaderStage::Pixel);
        if (!m_ToneMappingFragmentShader)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create tone mapping fragment shader");
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
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create scene color sampler");
            return false;
        }

        // 3D LUT は格子点の中心を引くので、三線形補間・端で飽和させる
        RHI::SamplerDesc colorLutSamplerDesc = samplerDesc;
        colorLutSamplerDesc.filterMip = RHI::FilterMode::Point;
        m_ColorLutSampler = m_Device->CreateSampler(colorLutSamplerDesc);
        if (!m_ColorLutSampler)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create color LUT sampler");
            return false;
        }

        // ========================================
        // パラメータUBOバッファ作成
        // ========================================
        RHI::BufferDesc paramsUboDesc(
            TONEMAPPING_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "ToneMappingParamsUBO");
        m_ParamsBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_ParamsBuffer)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create params buffer");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("ToneMappingPass", "ToneMappingPass initialized (Operator=%d)",
                        static_cast<int>(m_Settings.Operator));
        return true;
    }

    void ToneMappingPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_OutputTexture.reset();
        m_ToneMappingRenderPass.reset();
        m_ToneMappingFramebuffer.reset();
        m_ToneMappingPipeline.reset();
        m_ToneMappingVertexShader.reset();
        m_ToneMappingFragmentShader.reset();
        m_ParamsBuffer.reset();
        m_ToneMappingDescriptorSet.reset();
        m_SceneColorSampler.reset();
        m_ColorLutTexture.reset();
        m_ColorLutFallbackTexture.reset();
        m_ColorLutSampler.reset();
        m_bColorLutLoadFailed = false;
        m_Device = nullptr;
        m_OutputHandle = {};
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferOutputTexture = nullptr;
        m_RenderPassSignature = {};

        m_bInitialized = false;
        NORVES_LOG_INFO("ToneMappingPass", "ToneMappingPass shutdown");
    }

    void ToneMappingPass::Setup(ViewRenderContext &context)
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
            // display-linear Rec.709出力テクスチャ作成
            RHI::TextureDesc outputDesc =
                RHI::TextureDesc::RenderTarget(width,
                                               height,
                                               RHI::Format::R16G16B16A16_FLOAT,
                                               "ToneMappedColor");
            outputDesc.Usage = outputDesc.Usage | RHI::ResourceUsage::TransferSrc;
            m_OutputTexture = m_Device->CreateTexture(outputDesc);

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("ToneMappingPass", "Failed to create output texture");
                return;
            }
        }

        const bool bNeedsPrepare =
            bNeedsOutputTexture ||
            !m_ToneMappingRenderPass ||
            !m_ToneMappingFramebuffer ||
            !m_ToneMappingPipeline ||
            !m_ToneMappingDescriptorSet ||
            m_FramebufferOutputTexture != m_OutputTexture.get() ||
            m_bRenderPassUsesRenderGraphInitialState;

        if (bNeedsPrepare)
        {
            if (PrepareResources(width, height, m_OutputTexture, false))
            {
                NORVES_LOG_INFO("ToneMappingPass", "ToneMapping resources resized (%ux%u)", width, height);
            }
        }
    }

    bool ToneMappingPass::PrepareResources(uint32_t width,
                                           uint32_t height,
                                           const RHI::TexturePtr& outputTexture,
                                           bool bUseRenderGraphInitialState)
    {
        if (!m_Device ||
            !outputTexture ||
            !m_ToneMappingVertexShader ||
            !m_ToneMappingFragmentShader ||
            !m_ParamsBuffer)
        {
            return false;
        }

        const RenderPassSignature signature = CreateToneMappingRenderPassSignature(width,
                                                                                   height,
                                                                                   outputTexture,
                                                                                   bUseRenderGraphInitialState);
        const bool bResourcesChanged =
            !RenderPassSignatureEquals(m_RenderPassSignature, signature) ||
            !m_ToneMappingRenderPass ||
            !m_ToneMappingFramebuffer ||
            !m_ToneMappingPipeline ||
            !m_ToneMappingDescriptorSet;

        m_OutputTexture = outputTexture;
        if (!bResourcesChanged)
        {
            return true;
        }

        m_ToneMappingRenderPass.reset();
        m_ToneMappingFramebuffer.reset();
        m_ToneMappingPipeline.reset();
        m_ToneMappingDescriptorSet.reset();
        m_RenderPassSignature = {};

        // ========================================
        // レンダーパス作成（1カラー、デプスなし）
        // ========================================
        RHI::RenderPassDesc rpDesc;

        RHI::AttachmentDesc colorAttach;
        colorAttach.format = signature.Output.Format;
        colorAttach.isDepthStencil = false;
        colorAttach.clear = false;
        colorAttach.loadOp = signature.Output.LoadOp;
        colorAttach.storeOp = signature.Output.StoreOp;
        colorAttach.initialState = signature.Output.InitialState;
        colorAttach.finalState = signature.Output.FinalState;
        rpDesc.colorAttachments.push_back(colorAttach);

        rpDesc.hasDepthStencil = false;

        m_ToneMappingRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_ToneMappingRenderPass)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create tone mapping render pass");
            return false;
        }

        // ========================================
        // フレームバッファ作成
        // ========================================
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_ToneMappingRenderPass;
        fbDesc.colorTargets.push_back(outputTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_ToneMappingFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_ToneMappingFramebuffer)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create tone mapping framebuffer");
            return false;
        }

        // ========================================
        // ディスクリプタセット作成
        // ========================================
        // binding 0: SceneColor（combined image sampler）
        // binding 1: ToneMappingParams UBO
        // binding 2: 表示変換の3D LUT（combined image sampler）
        RHI::DescriptorSetDesc dsDesc;

        RHI::DescriptorBinding sceneColorBinding;
        sceneColorBinding.binding = 0;
        sceneColorBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        sceneColorBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(sceneColorBinding);

        RHI::DescriptorBinding paramsBinding;
        paramsBinding.binding = 1;
        paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramsBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(paramsBinding);

        RHI::DescriptorBinding colorLutBinding;
        colorLutBinding.binding = 2;
        colorLutBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        colorLutBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(colorLutBinding);

        m_ToneMappingDescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!m_ToneMappingDescriptorSet)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create descriptor set");
            return false;
        }

        // UBOバインド（テクスチャはExecute時にバインド）
        m_ToneMappingDescriptorSet->BindConstantBuffer(1, m_ParamsBuffer, 0, TONEMAPPING_PARAMS_SIZE);

        // ========================================
        // パイプライン作成（フルスクリーン描画）
        // ========================================
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_ToneMappingVertexShader;
        pipelineDesc.pixelShader = m_ToneMappingFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力なし

        // ラスタライザ
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        // デプステスト無効
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        // ブレンド無効
        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        pipelineDesc.renderPass = m_ToneMappingRenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_ToneMappingPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_ToneMappingPipeline)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create tone mapping pipeline");
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_FramebufferOutputTexture = outputTexture.get();
        m_bRenderPassUsesRenderGraphInitialState =
            signature.Output.InitialState == RHI::ResourceState::RenderTarget;
        m_RenderPassSignature = signature;
        return true;
    }

    bool ToneMappingPass::AttachmentSignatureEquals(const AttachmentSignature& lhs,
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

    bool ToneMappingPass::RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                                    const RenderPassSignature& rhs) const
    {
        return lhs.bValid == rhs.bValid &&
               AttachmentSignatureEquals(lhs.Output, rhs.Output);
    }

    ToneMappingPass::RenderPassSignature ToneMappingPass::CreateToneMappingRenderPassSignature(
        uint32_t width,
        uint32_t height,
        const RHI::TexturePtr& outputTexture,
        bool bUseRenderGraphInitialState) const
    {
        RenderPassSignature signature;
        signature.bValid = true;
        signature.Output = {RGAttachmentKind::Color,
                            outputTexture ? outputTexture->GetFormat() : RHI::Format::R16G16B16A16_FLOAT,
                            RHI::AttachmentLoadOp::DontCare,
                            RHI::AttachmentStoreOp::Store,
                            bUseRenderGraphInitialState ? RHI::ResourceState::RenderTarget : RHI::ResourceState::Undefined,
                            RHI::ResourceState::ShaderResource,
                            outputTexture.get(),
                            width,
                            height,
                            false};
        return signature;
    }

    void ToneMappingPass::Execute(ViewRenderContext& context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_ToneMappingRenderPass || !m_ToneMappingFramebuffer || !m_ToneMappingPipeline)
        {
            NORVES_LOG_WARNING("ToneMappingPass", "ToneMapping resources not ready, skipping");
            return;
        }

        // HDRシーンカラーをSharedResourceRegistryから取得（TexturePtr版）
        RHI::TexturePtr sceneColorPtr;
        sceneColorPtr = ResolveSharedTexturePtr(context, "SceneColor");

        if (!sceneColorPtr)
        {
            NORVES_LOG_WARNING("ToneMappingPass", "SceneColor not available, skipping tone mapping");
            return;
        }

        ExecuteWithInput(context, sceneColorPtr, true);
    }

    void ToneMappingPass::Declare(RenderGraphBuilder &builder)
    {
        m_bLegacyInputFallbackActive = false;
        const ViewRenderContext *context = builder.GetContext();
        const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
        const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

        m_InputSceneColorHandle = {};

        RGTextureHandle sceneColorHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::BloomSceneColor,
                                   sceneColorHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputSceneColorHandle = sceneColorHandle.ToResourceHandle();
        }
        else if (builder.TryReadTexture(RenderGraphResourceNames::SSRSceneColor,
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

        RGTextureDesc outputDesc = RGTextureDesc::RenderTarget(width,
                                                               height,
                                                               RHI::Format::R16G16B16A16_FLOAT,
                                                               "ToneMappedColor");
        outputDesc.Usage = outputDesc.Usage | RHI::ResourceUsage::TransferSrc;

        RGTextureHandle outputHandle = builder.WriteTextureAttachment(
            RenderGraphResourceNames::ToneMappedColor,
            outputDesc,
            RGAttachmentKind::Color,
            RHI::AttachmentLoadOp::DontCare,
            RHI::AttachmentStoreOp::Store,
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);
        m_OutputHandle = outputHandle.ToResourceHandle();
        builder.ExportTexture(RenderGraphResourceNames::ToneMappedColor, outputHandle);
        builder.PreserveInsertionOrder();
    }

    void ToneMappingPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("ToneMappingPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
        if (!outputTexture)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to resolve native tone mapping output texture");
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
        if (!sceneColorPtr)
        {
            sceneColorPtr = ResolveSharedTexturePtr(context, "SceneColor");
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

    void ToneMappingPass::ExecuteWithInput(ViewRenderContext& context,
                                           const RHI::TexturePtr& sceneColorPtr,
                                           bool bRegisterLegacyBridge)
    {
        if (!m_ToneMappingRenderPass ||
            !m_ToneMappingFramebuffer ||
            !m_ToneMappingPipeline ||
            !m_ToneMappingDescriptorSet)
        {
            NORVES_LOG_WARNING("ToneMappingPass", "ToneMapping resources not ready, skipping");
            return;
        }

        const uint32_t operatorType = PrepareColorLut();
        const RHI::TexturePtr& colorLut =
            operatorType == Aces20LutOperatorType ? m_ColorLutTexture : m_ColorLutFallbackTexture;
        if (!colorLut)
        {
            NORVES_LOG_WARNING("ToneMappingPass", "Color LUT texture not ready, skipping");
            return;
        }

        // パラメータバッファ更新
        GPUToneMappingParams params = {};
        params.operatorType = operatorType;
        const bool bDebugPostProcessBypass =
            IsDebugPostProcessBypassMode(context.GetActiveDebugMode());
        params.bBypass = bDebugPostProcessBypass ? 1u : 0u;
        params._pad0 = 0u;
        // Vignette
        params.vignetteIntensity = m_Settings.VignetteIntensity;
        params.vignetteRadius = m_Settings.VignetteRadius;
        params.vignetteSoftness = m_Settings.VignetteSoftness;
        params._pad1 = 0.0f;
        params._pad2 = 0.0f;
        // Color Grading
        params.colorFilter[0] = m_Settings.ColorFilter[0];
        params.colorFilter[1] = m_Settings.ColorFilter[1];
        params.colorFilter[2] = m_Settings.ColorFilter[2];
        params.colorFilter[3] = m_Settings.ColorFilterIntensity;
        params.contrast = m_Settings.Contrast;
        params.saturation = m_Settings.Saturation;
        params.brightness = m_Settings.Brightness;
        params.temperature = m_Settings.Temperature;
        m_ParamsBuffer->Update(&params, sizeof(GPUToneMappingParams));

        // トーンマッピング結果をSharedResourceRegistryに登録
        if (bRegisterLegacyBridge && context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("ToneMappedColor", m_OutputTexture);
        }

        // SceneColorテクスチャをディスクリプタセットにバインド
        m_ToneMappingDescriptorSet->BindTexture(0, sceneColorPtr);
        m_ToneMappingDescriptorSet->BindSampler(0, m_SceneColorSampler);
        m_ToneMappingDescriptorSet->BindTexture(2, colorLut);
        m_ToneMappingDescriptorSet->BindSampler(2, m_ColorLutSampler);
        m_ToneMappingDescriptorSet->Update();

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        context.EnqueueFullscreenPass(m_ToneMappingRenderPass,
                                      m_ToneMappingFramebuffer,
                                      viewport,
                                      scissor,
                                      m_ToneMappingPipeline,
                                      m_ToneMappingDescriptorSet);
    }

    uint32_t ToneMappingPass::PrepareColorLut()
    {
        if (!m_Device)
        {
            return static_cast<uint32_t>(m_Settings.Operator);
        }

        if (!m_ColorLutFallbackTexture)
        {
            // LUT演算子以外でも binding 2 を有効な3D textureで満たす
            RHI::TextureDesc fallbackDesc;
            fallbackDesc.Width = 1u;
            fallbackDesc.Height = 1u;
            fallbackDesc.Depth = 1u;
            fallbackDesc.Dimension = RHI::TextureDimension::Texture3D;
            fallbackDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
            fallbackDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
            fallbackDesc.DebugName = "ToneMappingColorLutFallback";
            // 作成・転送の例外はここで受け止め、RenderThreadへ伝えない
            try
            {
                m_ColorLutFallbackTexture = m_Device->CreateTexture(fallbackDesc);
                if (m_ColorLutFallbackTexture)
                {
                    const uint16_t zeroTexel[4] = {0u, 0u, 0u, 0u};
                    m_ColorLutFallbackTexture->Update(zeroTexel,
                                                      Aces20LutBytesPerTexel,
                                                      Aces20LutBytesPerTexel);
                }
            }
            catch (const std::exception& exception)
            {
                NORVES_LOG_ERROR("ToneMappingPass",
                                 "Failed to initialize color LUT fallback: %s",
                                 exception.what());
                m_ColorLutFallbackTexture.reset();
            }
        }

        if (m_Settings.Operator != ToneMappingOperator::Aces20Lut)
        {
            return static_cast<uint32_t>(m_Settings.Operator);
        }

        if (!m_ColorLutTexture && !m_bColorLutLoadFailed)
        {
            m_ColorLutTexture = LoadAces20SdrLut();
            m_bColorLutLoadFailed = !m_ColorLutTexture;
            if (m_bColorLutLoadFailed)
            {
                NORVES_LOG_ERROR("ToneMappingPass",
                                 "ACES 2.0 SDR LUT is unavailable; falling back to ACES Filmic");
            }
        }

        return m_ColorLutTexture ? Aces20LutOperatorType : AcesFilmicOperatorType;
    }

    RHI::TexturePtr ToneMappingPass::LoadAces20SdrLut() const
    {
        const Container::String path = ResolveAssetPath(Aces20SdrLutAssetPath);
        std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to open color LUT: %s", path.c_str());
            return nullptr;
        }

        const std::streamoff fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        Aces20LutHeader header = {};
        if (fileSize < static_cast<std::streamoff>(Aces20LutHeaderSize) ||
            !file.read(reinterpret_cast<char*>(&header), sizeof(header)))
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Color LUT header is truncated: %s", path.c_str());
            return nullptr;
        }

        const uint64_t lutSize = header.Size;
        const uint64_t payloadSize = lutSize * lutSize * lutSize * Aces20LutBytesPerTexel;
        if (std::memcmp(header.Magic, Aces20LutMagic, sizeof(Aces20LutMagic)) != 0 ||
            header.Size < 2u || header.Size > 256u ||
            header.Channels != 4u ||
            header.Format != Aces20LutFormatRgba16F ||
            header.ShaperOffset != Aces20LutShaperOffset ||
            header.ShaperMax != Aces20LutShaperMax ||
            static_cast<uint64_t>(fileSize) != Aces20LutHeaderSize + payloadSize)
        {
            NORVES_LOG_ERROR("ToneMappingPass",
                             "Color LUT header does not match the expected shaper/layout: %s",
                             path.c_str());
            return nullptr;
        }

        Container::VariableArray<uint8_t> texels;
        texels.resize(static_cast<size_t>(payloadSize));
        if (!file.read(reinterpret_cast<char*>(texels.data()), static_cast<std::streamsize>(payloadSize)))
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to read color LUT texels: %s", path.c_str());
            return nullptr;
        }

        // x=R, y=G, z=B の順（R が最も速く変わる）はそのまま3D textureの行・スライスの並びになる
        RHI::TextureDesc lutDesc;
        lutDesc.Width = header.Size;
        lutDesc.Height = header.Size;
        lutDesc.Depth = header.Size;
        lutDesc.Dimension = RHI::TextureDimension::Texture3D;
        lutDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        lutDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        lutDesc.DebugName = "Aces20SdrRec709Lut";
        const uint32_t rowPitch = header.Size * Aces20LutBytesPerTexel;
        const uint32_t slicePitch = rowPitch * header.Size;
        RHI::TexturePtr lutTexture;
        // 作成・転送の例外はここで受け止め、呼び出し側で ACES Filmic へ退避させる
        try
        {
            lutTexture = m_Device->CreateTexture(lutDesc);
            if (!lutTexture)
            {
                NORVES_LOG_ERROR("ToneMappingPass", "Failed to create color LUT texture");
                return nullptr;
            }
            lutTexture->Update(texels.data(), rowPitch, slicePitch);
        }
        catch (const std::exception& exception)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create or upload color LUT: %s", exception.what());
            return nullptr;
        }
        NORVES_LOG_INFO("ToneMappingPass", "ACES 2.0 SDR LUT loaded (%u^3)", header.Size);
        return lutTexture;
    }

    bool ToneMappingPass::EnqueueEmptyNativePass(ViewRenderContext &context) const
    {
        if (!m_ToneMappingRenderPass || !m_ToneMappingFramebuffer)
        {
            return false;
        }

        context.EnqueueFullscreenPass(m_ToneMappingRenderPass,
                                      m_ToneMappingFramebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      nullptr,
                                      nullptr);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
