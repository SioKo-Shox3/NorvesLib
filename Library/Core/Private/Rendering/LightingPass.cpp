#include "Rendering/LightingPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/SSAOPass.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SceneView.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/TransientResourcePool.h"
#include "Math/MatrixUtils.h"
#include "Logging/LogMacros.h"

// HDR環境マップロード用
#include "stb_image.h"
#include <cmath>
#include <algorithm>

namespace NorvesLib::Core::Rendering
{
    // ========================================
    // float32 → float16 変換ヘルパー
    // ========================================
    static uint16_t FloatToHalf(float value)
    {
        union
        {
            float f;
            uint32_t u;
        } conv;
        conv.f = value;
        uint32_t f32 = conv.u;

        uint16_t sign = static_cast<uint16_t>((f32 >> 16) & 0x8000);
        int32_t exponent = static_cast<int32_t>((f32 >> 23) & 0xFF) - 127;
        uint32_t mantissa = f32 & 0x007FFFFF;

        if (exponent > 15)
        {
            return sign | 0x7C00; // Overflow → Infinity
        }
        else if (exponent > -15)
        {
            return sign | static_cast<uint16_t>(((exponent + 15) << 10) | (mantissa >> 13));
        }
        else if (exponent > -25)
        {
            mantissa |= 0x800000;
            uint32_t shift = static_cast<uint32_t>(-14 - exponent);
            return sign | static_cast<uint16_t>(mantissa >> (shift + 13));
        }
        else
        {
            return sign; // Too small → zero
        }
    }

    // ========================================
    // GPU側ライトデータ構造（シェーダーのUBOレイアウトに対応）
    // ========================================

    /** @brief GPUライトデータ（std140アライメント） */
    struct GPULightData
    {
        float position[4];    // xyz=position, w=type (0:Dir, 1:Point, 2:Spot)
        float direction[4];   // xyz=direction, w=innerAngle
        float color[4];       // xyz=color, w=intensity
        float attenuation[4]; // x=range, y=outerAngle, z=unused, w=unused
    };

    /** @brief ライティングパラメータUBO */
    struct GPULightingParams
    {
        float invViewProjection[16]; // mat4
        float cameraPosition[4];     // vec4
        float ambientColor[4];       // vec4 (xyz=color, w=intensity)
        float lightView[16];         // mat4 （シャドウマップ用ライトビュー行列）
        float lightProjection[16];   // mat4 （シャドウマップ用ライトプロジェクション行列）
        uint32_t lightCount;         // uint
        uint32_t bShadowEnabled;     // uint （シャドウマップ有効フラグ）
        uint32_t envMapMipLevels;    // uint （環境マップミップレベル数）
        uint32_t bIBLEnabled;        // uint （IBL有効フラグ）
        uint32_t bSSAOEnabled;       // uint （SSAO有効フラグ）
        uint32_t bNeuralBRDFEnabled; // uint （Neural BRDF有効フラグ）
        uint32_t _pad1;              // padding
        uint32_t _pad2;              // padding
    };

    static constexpr uint32_t MAX_LIGHTS = 16;
    static constexpr uint32_t LIGHTING_PARAMS_SIZE = sizeof(GPULightingParams);
    static constexpr uint32_t LIGHT_BUFFER_SIZE = sizeof(GPULightData) * MAX_LIGHTS;

    static RHI::DescriptorSetDesc CreateLightingDescriptorSetDesc(bool bNeuralBRDFAvailable)
    {
        RHI::DescriptorSetDesc dsDesc;

        RHI::DescriptorBinding albedoBinding;
        albedoBinding.binding = 0;
        albedoBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        albedoBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(albedoBinding);

        RHI::DescriptorBinding normalBinding;
        normalBinding.binding = 1;
        normalBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        normalBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(normalBinding);

        RHI::DescriptorBinding materialBinding;
        materialBinding.binding = 2;
        materialBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        materialBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(materialBinding);

        RHI::DescriptorBinding depthBinding;
        depthBinding.binding = 3;
        depthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        depthBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(depthBinding);

        RHI::DescriptorBinding paramsBinding;
        paramsBinding.binding = 4;
        paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramsBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(paramsBinding);

        RHI::DescriptorBinding lightBinding;
        lightBinding.binding = 5;
        lightBinding.type = RHI::ResourceBindType::ConstantBuffer;
        lightBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(lightBinding);

        RHI::DescriptorBinding shadowBinding;
        shadowBinding.binding = 6;
        shadowBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        shadowBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(shadowBinding);

        RHI::DescriptorBinding emissiveBinding;
        emissiveBinding.binding = 7;
        emissiveBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        emissiveBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(emissiveBinding);

        RHI::DescriptorBinding envMapBinding;
        envMapBinding.binding = 8;
        envMapBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        envMapBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(envMapBinding);

        RHI::DescriptorBinding brdfLutBinding;
        brdfLutBinding.binding = 9;
        brdfLutBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        brdfLutBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(brdfLutBinding);

        RHI::DescriptorBinding ssaoBinding;
        ssaoBinding.binding = 10;
        ssaoBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        ssaoBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(ssaoBinding);

        if (bNeuralBRDFAvailable)
        {
            RHI::DescriptorBinding neuralWeightBinding;
            neuralWeightBinding.binding = 11;
            neuralWeightBinding.type = RHI::ResourceBindType::StructuredBuffer;
            neuralWeightBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(neuralWeightBinding);
        }

        return dsDesc;
    }

    LightingPass::LightingPass(const LightingPassSettings& settings)
        : m_Settings(settings)
    {
    }

    LightingPass::~LightingPass()
    {
        Shutdown();
    }

    bool LightingPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("LightingPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // フルスクリーン頂点シェーダー作成
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("LightingPass", "ShaderManager is null");
            return false;
        }

        m_LightingVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_LightingVertexShader)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create fullscreen vertex shader");
            return false;
        }

        // ========================================
        // ライティングフラグメントシェーダー作成
        // ========================================
        m_LightingFragmentShader = context.ShaderMgr->LoadShader("lighting.frag", RHI::ShaderStage::Pixel);
        if (!m_LightingFragmentShader)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting fragment shader");
            return false;
        }

        // ========================================
        // GBufferサンプラー作成
        // ========================================
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;

        m_GBufferSampler = m_Device->CreateSampler(samplerDesc);
        if (!m_GBufferSampler)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create GBuffer sampler");
            return false;
        }

        // ========================================
        // ライティングパラメータUBOバッファ作成
        // ========================================
        RHI::BufferDesc paramsUboDesc(
            LIGHTING_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "LightingParamsUBO");
        m_LightDataBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_LightDataBuffer)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting params buffer");
            return false;
        }

        // ========================================
        // ライト配列UBOバッファ作成（binding=5用）
        // ========================================
        RHI::BufferDesc lightArrayUboDesc(
            LIGHT_BUFFER_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "LightArrayUBO");
        m_LightArrayBuffer = m_Device->CreateBuffer(lightArrayUboDesc);
        if (!m_LightArrayBuffer)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create light array buffer");
            return false;
        }

        // ========================================
        // ライトバッファ作成（ライト配列用別UBO）
        // ========================================
        // Note: ライトバッファはm_LightDataBufferの後ろに配置するのではなく、
        //       別のバインディングポイント（binding=5）として作成

        m_bInitialized = true;

        // ========================================
        // Neural BRDF ウェイトデータ読み込み
        // ========================================
        if (!m_Settings.NeuralBRDFWeightPath.empty())
        {
            Container::String resolvedPath = m_Settings.NeuralBRDFWeightPath;
#ifdef NORVES_ASSET_DIR
            if (resolvedPath.size() > 0 && resolvedPath[0] != '/' && resolvedPath[0] != '\\' &&
                (resolvedPath.size() < 2 || resolvedPath[1] != ':'))
            {
                resolvedPath = Container::String(NORVES_ASSET_DIR) + "/" + resolvedPath;
            }
#endif

            if (m_NeuralBRDFData.LoadFromFile(resolvedPath))
            {
                const auto& weightData = m_NeuralBRDFData.GetWeightDataFP32();
                size_t bufferSize = m_NeuralBRDFData.GetWeightDataSizeFP32();

                RHI::BufferDesc weightBufDesc(
                    static_cast<uint64_t>(bufferSize),
                    RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst,
                    true, "NeuralBRDF_Weights");

                m_NeuralBRDFWeightBuffer = m_Device->CreateBuffer(weightBufDesc);
                if (m_NeuralBRDFWeightBuffer)
                {
                    m_NeuralBRDFWeightBuffer->Update(weightData.data(), bufferSize);
                    m_bNeuralBRDFAvailable = true;
                    NORVES_LOG_INFO("LightingPass", "Neural BRDF loaded: %zu parameters, %zu bytes",
                                    weightData.size(), bufferSize);
                }
                else
                {
                    NORVES_LOG_WARNING("LightingPass", "Failed to create Neural BRDF weight buffer");
                }
            }
            else
            {
                NORVES_LOG_WARNING("LightingPass", "Failed to load Neural BRDF weights, using analytical BRDF");
            }
        }

        // ========================================
        // IBL (Image-Based Lighting) リソース初期化
        // ========================================
        if (!m_Settings.EnvironmentMapPath.empty())
        {
            bool bEnvLoaded = LoadEnvironmentMap(m_Settings.EnvironmentMapPath);
            bool bBrdfGenerated = GenerateBRDFLut();

            if (bEnvLoaded && bBrdfGenerated)
            {
                // IBL用サンプラー作成（Linear + ミップマップ）
                RHI::SamplerDesc iblSamplerDesc;
                iblSamplerDesc.filterMin = RHI::FilterMode::Linear;
                iblSamplerDesc.filterMag = RHI::FilterMode::Linear;
                iblSamplerDesc.filterMip = RHI::FilterMode::Linear;
                iblSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
                iblSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
                iblSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;

                m_IBLSampler = m_Device->CreateSampler(iblSamplerDesc);
                if (m_IBLSampler)
                {
                    m_bIBLAvailable = true;
                    NORVES_LOG_INFO("LightingPass", "IBL initialized successfully");
                }
                else
                {
                    NORVES_LOG_WARNING("LightingPass", "Failed to create IBL sampler, falling back to flat ambient");
                }
            }
            else
            {
                NORVES_LOG_WARNING("LightingPass", "IBL resource loading failed, falling back to flat ambient");
            }
        }

        NORVES_LOG_INFO("LightingPass", "LightingPass initialized");
        return true;
    }

    void LightingPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_SceneColorTexture.reset();
        m_LightingRenderPass.reset();
        m_LightingFramebuffer.reset();
        m_LightingPipeline.reset();
        m_LightingVertexShader.reset();
        m_LightingFragmentShader.reset();
        m_LightDataBuffer.reset();
        m_LightArrayBuffer.reset();
        m_LightingDescriptorSet.reset();
        m_GBufferSampler.reset();

        // IBLリソース解放
        m_EnvironmentTexture.reset();
        m_BrdfLutTexture.reset();
        m_IBLSampler.reset();
        m_bIBLAvailable = false;

        // Neural BRDFリソース解放
        m_NeuralBRDFWeightBuffer.reset();
        m_bNeuralBRDFAvailable = false;

        m_Device = nullptr;
        m_SceneView = nullptr;
        m_GBufferPass = nullptr;
        m_SSAOPass = nullptr;
        m_SceneColorHandle = {};
        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferDepthHandle = {};
        m_GBufferEmissiveHandle = {};
        m_SSAOBlurredHandle = {};
        m_bUsingRenderGraphResources = false;
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferSceneColorTexture = nullptr;
        m_FramebufferWidth = 0;
        m_FramebufferHeight = 0;

        m_bInitialized = false;
        NORVES_LOG_INFO("LightingPass", "LightingPass shutdown");
    }

    void LightingPass::Setup(ViewRenderContext& context)
    {
        uint32_t width = context.GetActiveRenderWidth();
        uint32_t height = context.GetActiveRenderHeight();

        if (width == 0 || height == 0)
        {
            return;
        }

        m_bUsingRenderGraphResources = false;

        const bool bUseRenderGraphInitialState = m_bUsingRenderGraphResources;
        const bool bResourcesChanged =
            width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            !m_SceneColorTexture ||
            m_FramebufferSceneColorTexture != m_SceneColorTexture.get() ||
            m_bRenderPassUsesRenderGraphInitialState != bUseRenderGraphInitialState ||
            !m_LightingRenderPass ||
            !m_LightingFramebuffer ||
            !m_LightingPipeline ||
            !m_LightingDescriptorSet;

        if (bResourcesChanged)
        {
            RHI::TexturePtr sceneColorTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SceneColor"));
            if (!sceneColorTexture)
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to create SceneColor texture");
                return;
            }

            if (!PrepareLightingOutput(width,
                                       height,
                                       sceneColorTexture,
                                       bUseRenderGraphInitialState,
                                       context))
            {
                return;
            }

            NORVES_LOG_INFO("LightingPass", "Lighting resources resized (%ux%u)", width, height);
        }
    }

    void LightingPass::Declare(RenderGraphBuilder &builder)
    {
        const ViewRenderContext *context = builder.GetContext();

        uint32_t width = 0;
        uint32_t height = 0;
        if (context)
        {
            width = ResolveLightingWidth(*context);
            height = ResolveLightingHeight(*context);
        }

        if (width == 0)
        {
            width = m_CurrentWidth > 0 ? m_CurrentWidth : 1;
        }

        if (height == 0)
        {
            height = m_CurrentHeight > 0 ? m_CurrentHeight : 1;
        }

        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferDepthHandle = {};
        m_GBufferEmissiveHandle = {};
        m_SSAOBlurredHandle = {};

        RGTextureHandle albedoHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferAlbedo,
                                   albedoHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferAlbedoHandle = albedoHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackAlbedoHandle = m_GBufferPass->GetAlbedoHandle();
            if (fallbackAlbedoHandle.IsValid())
            {
                builder.Read(fallbackAlbedoHandle, RHI::ResourceState::ShaderResource);
                m_GBufferAlbedoHandle = fallbackAlbedoHandle;
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
            }
        }

        RGTextureHandle materialHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferMaterial,
                                   materialHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferMaterialHandle = materialHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackMaterialHandle = m_GBufferPass->GetMaterialHandle();
            if (fallbackMaterialHandle.IsValid())
            {
                builder.Read(fallbackMaterialHandle, RHI::ResourceState::ShaderResource);
                m_GBufferMaterialHandle = fallbackMaterialHandle;
            }
        }

        RGTextureHandle depthHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth,
                                   depthHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferDepthHandle = depthHandle.ToResourceHandle();
            builder.PublishTexture(RenderGraphResourceNames::SceneDepth, depthHandle);
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackDepthHandle = m_GBufferPass->GetDepthHandle();
            if (fallbackDepthHandle.IsValid())
            {
                builder.Read(fallbackDepthHandle, RHI::ResourceState::ShaderResource);
                m_GBufferDepthHandle = fallbackDepthHandle;
            }
        }

        RGTextureHandle emissiveHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferEmissive,
                                   emissiveHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferEmissiveHandle = emissiveHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackEmissiveHandle = m_GBufferPass->GetEmissiveHandle();
            if (fallbackEmissiveHandle.IsValid())
            {
                builder.Read(fallbackEmissiveHandle, RHI::ResourceState::ShaderResource);
                m_GBufferEmissiveHandle = fallbackEmissiveHandle;
            }
        }

        RGTextureHandle ssaoHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::SSAOBlurred,
                                   ssaoHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_SSAOBlurredHandle = ssaoHandle.ToResourceHandle();
        }
        else if (m_SSAOPass)
        {
            const RGResourceHandle fallbackSSAOHandle = m_SSAOPass->GetSSAOBlurredHandle();
            if (fallbackSSAOHandle.IsValid())
            {
                builder.Read(fallbackSSAOHandle, RHI::ResourceState::ShaderResource);
                m_SSAOBlurredHandle = fallbackSSAOHandle;
            }
        }

        m_SceneColorHandle = builder.WriteTextureAttachment(
            RenderGraphResourceNames::SceneColor,
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SceneColor"),
            RGAttachmentKind::Color,
            RHI::AttachmentLoadOp::Clear,
            RHI::AttachmentStoreOp::Store,
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);

        builder.PreserveInsertionOrder();
    }

    void LightingPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr sceneColorTexture = resources.GetTexture(m_SceneColorHandle);
        if (!sceneColorTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to resolve native SceneColor texture");
            return;
        }

        RHI::TexturePtr albedoTexture;
        RHI::TexturePtr normalTexture;
        RHI::TexturePtr materialTexture;
        RHI::TexturePtr depthTexture;
        RHI::TexturePtr emissiveTexture;
        if (m_GBufferAlbedoHandle.IsValid())
        {
            albedoTexture = resources.GetTexture(m_GBufferAlbedoHandle);
        }
        if (m_GBufferNormalHandle.IsValid())
        {
            normalTexture = resources.GetTexture(m_GBufferNormalHandle);
        }
        if (m_GBufferMaterialHandle.IsValid())
        {
            materialTexture = resources.GetTexture(m_GBufferMaterialHandle);
        }
        if (m_GBufferDepthHandle.IsValid())
        {
            depthTexture = resources.GetTexture(m_GBufferDepthHandle);
        }
        if (m_GBufferEmissiveHandle.IsValid())
        {
            emissiveTexture = resources.GetTexture(m_GBufferEmissiveHandle);
        }

        RHI::TexturePtr ssaoTexture;
        if (m_SSAOBlurredHandle.IsValid())
        {
            ssaoTexture = resources.GetTexture(m_SSAOBlurredHandle);
        }

        if ((!albedoTexture || !normalTexture || !materialTexture || !depthTexture || !emissiveTexture) &&
            m_GBufferPass)
        {
            albedoTexture = albedoTexture ? albedoTexture : resources.GetTexture(m_GBufferPass->GetAlbedoHandle());
            normalTexture = normalTexture ? normalTexture : resources.GetTexture(m_GBufferPass->GetNormalHandle());
            materialTexture = materialTexture ? materialTexture : resources.GetTexture(m_GBufferPass->GetMaterialHandle());
            depthTexture = depthTexture ? depthTexture : resources.GetTexture(m_GBufferPass->GetDepthHandle());
            emissiveTexture = emissiveTexture ? emissiveTexture : resources.GetTexture(m_GBufferPass->GetEmissiveHandle());
        }

        if (!ssaoTexture && m_SSAOPass)
        {
            ssaoTexture = resources.GetTexture(m_SSAOPass->GetSSAOBlurredHandle());
        }

        if (context.SharedResources)
        {
            if (!albedoTexture)
            {
                albedoTexture = context.SharedResources->GetTexturePtr("GBuffer_Albedo");
            }
            if (!normalTexture)
            {
                normalTexture = context.SharedResources->GetTexturePtr("GBuffer_Normal");
            }
            if (!materialTexture)
            {
                materialTexture = context.SharedResources->GetTexturePtr("GBuffer_Material");
            }
            if (!depthTexture)
            {
                depthTexture = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            }
            if (!emissiveTexture)
            {
                emissiveTexture = context.SharedResources->GetTexturePtr("GBuffer_Emissive");
            }
            if (!ssaoTexture)
            {
                ssaoTexture = context.SharedResources->GetTexturePtr("SSAO");
            }
        }

        if (!PrepareLightingOutput(sceneColorTexture->GetWidth(),
                                   sceneColorTexture->GetHeight(),
                                   sceneColorTexture,
                                   true,
                                   context))
        {
            TryEnqueueNativeTransitionPass(context);
            return;
        }

        ExecuteWithInputs(context,
                          albedoTexture,
                          normalTexture,
                          materialTexture,
                          depthTexture,
                          emissiveTexture,
                          ssaoTexture);
    }

    void LightingPass::Execute(ViewRenderContext& context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_LightingRenderPass || !m_LightingFramebuffer || !m_LightingPipeline)
        {
            NORVES_LOG_WARNING("LightingPass", "Lighting resources not ready, skipping");
            return;
        }

        // GBufferテクスチャをSharedResourceRegistryから取得
        RHI::ITexture* gbufferAlbedo = nullptr;
        RHI::ITexture* gbufferNormal = nullptr;
        RHI::ITexture* gbufferMaterial = nullptr;
        RHI::ITexture* gbufferDepth = nullptr;

        if (context.SharedResources)
        {
            gbufferAlbedo = context.SharedResources->GetTexture("GBuffer_Albedo");
            gbufferNormal = context.SharedResources->GetTexture("GBuffer_Normal");
            gbufferMaterial = context.SharedResources->GetTexture("GBuffer_Material");
            gbufferDepth = context.SharedResources->GetTexture("GBuffer_Depth");
        }

        // GBufferが無い場合はスキップ
        if (!gbufferAlbedo || !gbufferNormal || !gbufferMaterial || !gbufferDepth)
        {
            NORVES_LOG_WARNING("LightingPass", "GBuffer textures not available, skipping lighting");
            return;
        }

        // GBufferエミッシブ取得
        RHI::ITexture* gbufferEmissive = nullptr;
        if (context.SharedResources)
        {
            gbufferEmissive = context.SharedResources->GetTexture("GBuffer_Emissive");
        }

        // シャドウマップをSharedResourceRegistryから取得
        RHI::TexturePtr shadowMapPtr;
        bool bShadowAvailable = false;
        if (context.SharedResources)
        {
            shadowMapPtr = context.SharedResources->GetTexturePtr("ShadowMap");
            bShadowAvailable = (shadowMapPtr != nullptr);
        }

        bool bSSAOAvailable = false;
        if (context.SharedResources)
        {
            bSSAOAvailable = (context.SharedResources->GetTexturePtr("SSAO") != nullptr);
        }

        // ライト情報をGPUバッファに転送
        UpdateLightBuffer(context, bShadowAvailable, bSSAOAvailable);

        // HDRシーンカラーをSharedResourceRegistryに登録
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", m_SceneColorTexture);
            context.SharedResources->RegisterTexture("SceneDepth", gbufferDepth);
        }

        // GBufferテクスチャ（TexturePtr版）をディスクリプタセットにバインド
        RHI::TexturePtr albedoPtr;
        RHI::TexturePtr normalPtr;
        RHI::TexturePtr materialPtr;
        RHI::TexturePtr depthPtr;
        RHI::TexturePtr emissivePtr;

        if (context.SharedResources)
        {
            albedoPtr = context.SharedResources->GetTexturePtr("GBuffer_Albedo");
            normalPtr = context.SharedResources->GetTexturePtr("GBuffer_Normal");
            materialPtr = context.SharedResources->GetTexturePtr("GBuffer_Material");
            depthPtr = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            emissivePtr = context.SharedResources->GetTexturePtr("GBuffer_Emissive");
        }

        if (!albedoPtr || !normalPtr || !materialPtr || !depthPtr)
        {
            NORVES_LOG_WARNING("LightingPass", "GBuffer TexturePtr not available, skipping");
            TryEnqueueNativeTransitionPass(context);
            return;
        }

        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SceneDepth", depthPtr);
        }

        m_LightingDescriptorSet->BindTexture(0, albedoPtr);
        m_LightingDescriptorSet->BindTexture(1, normalPtr);
        m_LightingDescriptorSet->BindTexture(2, materialPtr);
        m_LightingDescriptorSet->BindTexture(3, depthPtr);
        m_LightingDescriptorSet->BindSampler(0, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(1, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(2, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(3, m_GBufferSampler);

        // シャドウマップバインド（利用不可の場合はGBuffer深度をフォールバック）
        if (bShadowAvailable)
        {
            m_LightingDescriptorSet->BindTexture(6, shadowMapPtr);
        }
        else
        {
            m_LightingDescriptorSet->BindTexture(6, depthPtr);
        }
        m_LightingDescriptorSet->BindSampler(6, m_GBufferSampler);

        // GBufferエミッシブバインド
        if (emissivePtr)
        {
            m_LightingDescriptorSet->BindTexture(7, emissivePtr);
        }
        else
        {
            // フォールバック（エミッシブ無し）: アルベドをダミーとしてバインド（値は無視される）
            m_LightingDescriptorSet->BindTexture(7, albedoPtr);
        }
        m_LightingDescriptorSet->BindSampler(7, m_GBufferSampler);

        // IBL環境マップ・BRDF LUTバインド
        if (m_bIBLAvailable)
        {
            m_LightingDescriptorSet->BindTexture(8, m_EnvironmentTexture);
            m_LightingDescriptorSet->BindSampler(8, m_IBLSampler);
            m_LightingDescriptorSet->BindTexture(9, m_BrdfLutTexture);
            m_LightingDescriptorSet->BindSampler(9, m_IBLSampler);
        }
        else
        {
            // IBL無効時: フォールバック（ダミーテクスチャバインド）
            m_LightingDescriptorSet->BindTexture(8, albedoPtr);
            m_LightingDescriptorSet->BindSampler(8, m_GBufferSampler);
            m_LightingDescriptorSet->BindTexture(9, albedoPtr);
            m_LightingDescriptorSet->BindSampler(9, m_GBufferSampler);
        }

        // SSAOテクスチャバインド（binding=10）
        RHI::TexturePtr ssaoPtr;
        if (context.SharedResources)
        {
            ssaoPtr = context.SharedResources->GetTexturePtr("SSAO");
        }
        if (ssaoPtr)
        {
            m_LightingDescriptorSet->BindTexture(10, ssaoPtr);
        }
        else
        {
            // SSAO利用不可時: ダミーバインド（albedoを流用、シェーダー側でfallback）
            m_LightingDescriptorSet->BindTexture(10, albedoPtr);
        }
        m_LightingDescriptorSet->BindSampler(10, m_GBufferSampler);

        // Neural BRDFウェイトバッファバインド（binding=11）
        if (m_bNeuralBRDFAvailable && m_NeuralBRDFWeightBuffer)
        {
            m_LightingDescriptorSet->BindStorageBuffer(
                11, m_NeuralBRDFWeightBuffer, 0,
                static_cast<uint32_t>(m_NeuralBRDFData.GetWeightDataSizeFP32()));
        }

        m_LightingDescriptorSet->Update();

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        context.EnqueueFullscreenPass(m_LightingRenderPass,
                                      m_LightingFramebuffer,
                                      viewport,
                                      scissor,
                                      m_LightingPipeline,
                                      m_LightingDescriptorSet);
    }

    uint32_t LightingPass::ResolveLightingWidth(const ViewRenderContext& context) const
    {
        return context.GetActiveRenderWidth();
    }

    uint32_t LightingPass::ResolveLightingHeight(const ViewRenderContext& context) const
    {
        return context.GetActiveRenderHeight();
    }

    bool LightingPass::CreateLightingResources(uint32_t width, uint32_t height, ViewRenderContext& context)
    {
        if (!m_Device)
        {
            return false;
        }

        RHI::TexturePtr sceneColorTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SceneColor"));
        if (!sceneColorTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create SceneColor texture");
            return false;
        }

        return PrepareLightingOutput(width, height, sceneColorTexture, false, context);
    }

    bool LightingPass::PrepareLightingOutput(uint32_t width,
                                             uint32_t height,
                                             const RHI::TexturePtr& sceneColorTexture,
                                             bool bUseRenderGraphInitialState,
                                             ViewRenderContext& context)
    {
        (void)context;

        if (!m_Device || !sceneColorTexture || width == 0 || height == 0)
        {
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_SceneColorTexture = sceneColorTexture;
        m_bUsingRenderGraphResources = bUseRenderGraphInitialState;

        const RenderPassSignature signature = CreateLightingRenderPassSignature(width,
                                                                                height,
                                                                                sceneColorTexture,
                                                                                bUseRenderGraphInitialState);

        if (!EnsureLightingRenderPass(signature))
        {
            return false;
        }

        if (!EnsureLightingFramebuffer(width, height, sceneColorTexture))
        {
            return false;
        }

        if (!EnsureLightingDescriptorSet())
        {
            return false;
        }

        if (!EnsureLightingPipeline())
        {
            return false;
        }

        return true;
    }

    bool LightingPass::AttachmentSignatureEquals(const AttachmentSignature& lhs,
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

    bool LightingPass::RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                                 const RenderPassSignature& rhs) const
    {
        return lhs.bValid == rhs.bValid &&
               AttachmentSignatureEquals(lhs.SceneColor, rhs.SceneColor);
    }

    LightingPass::RenderPassSignature LightingPass::CreateLightingRenderPassSignature(
        uint32_t width,
        uint32_t height,
        const RHI::TexturePtr& sceneColorTexture,
        bool bUseRenderGraphInitialState) const
    {
        RenderPassSignature signature;
        signature.bValid = true;
        signature.SceneColor = {RGAttachmentKind::Color,
                                sceneColorTexture ? sceneColorTexture->GetFormat() : m_Settings.OutputFormat,
                                RHI::AttachmentLoadOp::Clear,
                                RHI::AttachmentStoreOp::Store,
                                bUseRenderGraphInitialState ? RHI::ResourceState::RenderTarget : RHI::ResourceState::Undefined,
                                RHI::ResourceState::ShaderResource,
                                sceneColorTexture.get(),
                                width,
                                height,
                                false};
        return signature;
    }

    bool LightingPass::EnsureLightingRenderPass(const RenderPassSignature& signature)
    {
        if (m_LightingRenderPass &&
            RenderPassSignatureEquals(m_RenderPassSignature, signature))
        {
            return true;
        }

        m_LightingRenderPass.reset();
        m_LightingFramebuffer.reset();
        m_LightingPipeline.reset();
        m_LightingDescriptorSet.reset();
        m_FramebufferSceneColorTexture = nullptr;
        m_FramebufferWidth = 0;
        m_FramebufferHeight = 0;
        m_RenderPassSignature = {};

        RHI::RenderPassDesc rpDesc;

        RHI::AttachmentDesc colorAttach;
        colorAttach.format = signature.SceneColor.Format;
        colorAttach.isDepthStencil = false;
        colorAttach.clear = true;
        colorAttach.clearColor[0] = 0.0f;
        colorAttach.clearColor[1] = 0.0f;
        colorAttach.clearColor[2] = 0.0f;
        colorAttach.clearColor[3] = 1.0f;
        colorAttach.loadOp = signature.SceneColor.LoadOp;
        colorAttach.storeOp = signature.SceneColor.StoreOp;
        colorAttach.initialState = signature.SceneColor.InitialState;
        colorAttach.finalState = signature.SceneColor.FinalState;
        rpDesc.colorAttachments.push_back(colorAttach);
        rpDesc.hasDepthStencil = false;

        m_LightingRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_LightingRenderPass)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting render pass");
            return false;
        }

        m_bRenderPassUsesRenderGraphInitialState =
            signature.SceneColor.InitialState == RHI::ResourceState::RenderTarget;
        m_RenderPassSignature = signature;
        return true;
    }

    bool LightingPass::EnsureLightingFramebuffer(uint32_t width,
                                                 uint32_t height,
                                                 const RHI::TexturePtr& sceneColorTexture)
    {
        if (m_LightingFramebuffer &&
            m_FramebufferSceneColorTexture == sceneColorTexture.get() &&
            m_FramebufferWidth == width &&
            m_FramebufferHeight == height)
        {
            return true;
        }

        if (!m_LightingRenderPass || !sceneColorTexture)
        {
            return false;
        }

        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_LightingRenderPass;
        fbDesc.colorTargets.push_back(sceneColorTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_LightingFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_LightingFramebuffer)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting framebuffer");
            return false;
        }

        m_FramebufferSceneColorTexture = sceneColorTexture.get();
        m_FramebufferWidth = width;
        m_FramebufferHeight = height;
        return true;
    }

    bool LightingPass::EnsureLightingDescriptorSet()
    {
        if (m_LightingDescriptorSet)
        {
            return true;
        }

        RHI::DescriptorSetDesc dsDesc = CreateLightingDescriptorSetDesc(m_bNeuralBRDFAvailable);
        m_LightingDescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!m_LightingDescriptorSet)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting descriptor set");
            return false;
        }

        m_LightingDescriptorSet->BindConstantBuffer(4, m_LightDataBuffer, 0, LIGHTING_PARAMS_SIZE);
        m_LightingDescriptorSet->BindConstantBuffer(5, m_LightArrayBuffer, 0, LIGHT_BUFFER_SIZE);
        return true;
    }

    bool LightingPass::EnsureLightingPipeline()
    {
        if (m_LightingPipeline)
        {
            return true;
        }

        if (!m_LightingRenderPass || !m_LightingVertexShader || !m_LightingFragmentShader)
        {
            return false;
        }

        RHI::DescriptorSetDesc dsDesc = CreateLightingDescriptorSetDesc(m_bNeuralBRDFAvailable);

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_LightingVertexShader;
        pipelineDesc.pixelShader = m_LightingFragmentShader;
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

        pipelineDesc.renderPass = m_LightingRenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_LightingPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_LightingPipeline)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting pipeline");
            return false;
        }

        return true;
    }

    void LightingPass::ExecuteWithInputs(ViewRenderContext& context,
                                         const RHI::TexturePtr& albedoTexture,
                                         const RHI::TexturePtr& normalTexture,
                                         const RHI::TexturePtr& materialTexture,
                                         const RHI::TexturePtr& depthTexture,
                                         const RHI::TexturePtr& emissiveTexture,
                                         const RHI::TexturePtr& ssaoTexture)
    {
        if (!context.CommandList)
        {
            return;
        }

        RegisterOutputs(context, m_SceneColorTexture, depthTexture);

        if (!m_LightingRenderPass || !m_LightingFramebuffer || !m_LightingPipeline || !m_LightingDescriptorSet)
        {
            NORVES_LOG_WARNING("LightingPass", "Lighting resources not ready, skipping");
            TryEnqueueNativeTransitionPass(context);
            return;
        }

        if (!albedoTexture || !normalTexture || !materialTexture || !depthTexture)
        {
            NORVES_LOG_WARNING("LightingPass", "GBuffer textures not available, skipping lighting");
            TryEnqueueNativeTransitionPass(context);
            return;
        }

        if (!context.SharedResources)
        {
            TryEnqueueNativeTransitionPass(context);
            return;
        }

        context.SharedResources->RegisterTexturePtr("GBuffer_Albedo", albedoTexture);
        context.SharedResources->RegisterTexturePtr("GBuffer_Normal", normalTexture);
        context.SharedResources->RegisterTexturePtr("GBuffer_Material", materialTexture);
        context.SharedResources->RegisterTexturePtr("GBuffer_Depth", depthTexture);
        if (emissiveTexture)
        {
            context.SharedResources->RegisterTexturePtr("GBuffer_Emissive", emissiveTexture);
        }
        if (ssaoTexture)
        {
            context.SharedResources->RegisterTexturePtr("SSAO", ssaoTexture);
        }

        Execute(context);
        RegisterOutputs(context, m_SceneColorTexture, depthTexture);
    }

    void LightingPass::RegisterOutputs(ViewRenderContext& context,
                                       const RHI::TexturePtr& sceneColorTexture,
                                       const RHI::TexturePtr& depthTexture) const
    {
        if (!context.SharedResources)
        {
            return;
        }

        if (sceneColorTexture)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", sceneColorTexture);
        }

        if (depthTexture)
        {
            context.SharedResources->RegisterTexturePtr("SceneDepth", depthTexture);
        }
    }

    bool LightingPass::TryEnqueueNativeTransitionPass(ViewRenderContext& context) const
    {
        if (!m_bUsingRenderGraphResources || !m_LightingRenderPass || !m_LightingFramebuffer)
        {
            return false;
        }

        context.EnqueueFullscreenPass(m_LightingRenderPass,
                                      m_LightingFramebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      RHI::PipelinePtr{},
                                      RHI::DescriptorSetPtr{},
                                      0,
                                      0);
        return true;
    }

    void LightingPass::UpdateLightBuffer(ViewRenderContext& context,
                                         bool bShadowAvailable,
                                         bool bSSAOAvailable)
    {
        // ライティングパラメータを構築
        GPULightingParams params = {};

        using namespace NorvesLib::Math;

        const CameraProxy *activeCamera = context.GetActiveCamera();
        if (activeCamera)
        {
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyCameraPosition(params.cameraPosition);
            cameraConstants.CopyShaderInverseViewProjection(params.invViewProjection);
        }
        else
        {
            params.cameraPosition[0] = 0.0f;
            params.cameraPosition[1] = 2.0f;
            params.cameraPosition[2] = 5.0f;
            params.cameraPosition[3] = 1.0f;
            MatrixUtils::TransposeToShaderData(Matrix4x4::Identity, params.invViewProjection);
        }

        // アンビエントカラー
        params.ambientColor[0] = m_Settings.AmbientColor[0];
        params.ambientColor[1] = m_Settings.AmbientColor[1];
        params.ambientColor[2] = m_Settings.AmbientColor[2];
        params.ambientColor[3] = m_Settings.AmbientIntensity;

        // ========================================
        // シャドウマップ用ライトビュー・プロジェクション行列
        // ========================================
        if (bShadowAvailable)
        {
            // ShadowMapPassと同じライト方向・パラメータ
            float lightDirX = -0.577f;
            float lightDirY = -0.577f;
            float lightDirZ = -0.577f;

            float lightDistance = 20.0f;
            Vector3 lightPos(-lightDirX * lightDistance, -lightDirY * lightDistance, -lightDirZ * lightDistance);
            Vector3 lightTarget(0.0f, 0.0f, 0.0f);
            Vector3 upDir(0.0f, 1.0f, 0.0f);

            Matrix4x4 lightViewMat = MatrixUtils::CreateLookAt(lightPos, lightTarget, upDir);

            float orthoSize = 20.0f;
            Matrix4x4 lightProjMat = MatrixUtils::CreateOrthographic(
                orthoSize * 2.0f, orthoSize * 2.0f, 0.1f, 50.0f);

            // RHI側でAPI固有のクリップ空間補正を適用（シャドウマップではY反転なし）
            lightProjMat = context.Device->AdjustProjectionForClipSpace(lightProjMat, false);

            MatrixUtils::TransposeToShaderData(lightViewMat, params.lightView);
            MatrixUtils::TransposeToShaderData(lightProjMat, params.lightProjection);
            params.bShadowEnabled = 1;
        }
        else
        {
            // シャドウ無効時は単位行列を設定
            for (int i = 0; i < 16; ++i)
            {
                params.lightView[i] = (i % 5 == 0) ? 1.0f : 0.0f;
                params.lightProjection[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            }
            params.bShadowEnabled = 0;
        }

        // ========================================
        // SceneViewのLightProxyからライト配列を構築
        // ========================================
        uint32_t lightCount = 0;
        GPULightData lightArray[MAX_LIGHTS] = {};

        if (context.SnapshotLightProxies)
        {
            const auto &lightProxies = *context.SnapshotLightProxies;
            for (const auto &proxy : lightProxies)
            {
                if (lightCount >= MAX_LIGHTS)
                {
                    break;
                }
                if (!proxy.IsValid())
                {
                    continue;
                }

                GPULightData &gpu = lightArray[lightCount];

                // position.w = type (0:Dir, 1:Point, 2:Spot)
                gpu.position[0] = proxy.PositionX;
                gpu.position[1] = proxy.PositionY;
                gpu.position[2] = proxy.PositionZ;
                gpu.position[3] = static_cast<float>(static_cast<int>(proxy.Type));

                // direction
                gpu.direction[0] = proxy.DirectionX;
                gpu.direction[1] = proxy.DirectionY;
                gpu.direction[2] = proxy.DirectionZ;
                gpu.direction[3] = proxy.InnerConeAngle;

                // color * intensity
                gpu.color[0] = proxy.ColorR;
                gpu.color[1] = proxy.ColorG;
                gpu.color[2] = proxy.ColorB;
                gpu.color[3] = proxy.Intensity;

                // attenuation
                gpu.attenuation[0] = proxy.Range;
                gpu.attenuation[1] = proxy.OuterConeAngle;
                gpu.attenuation[2] = 0.0f;
                gpu.attenuation[3] = 0.0f;

                ++lightCount;
            }
        }

        // ライトが登録されていない場合はデフォルトのディレクショナルライトを追加
        if (lightCount == 0)
        {
            GPULightData &gpu = lightArray[0];
            gpu.position[3] = 0.0f; // Directional
            gpu.direction[0] = -0.577f;
            gpu.direction[1] = -0.577f;
            gpu.direction[2] = -0.577f;
            gpu.color[0] = 1.0f;
            gpu.color[1] = 1.0f;
            gpu.color[2] = 1.0f;
            gpu.color[3] = 1.0f;
            gpu.attenuation[0] = 100.0f;
            lightCount = 1;
        }

        params.lightCount = lightCount;

        // IBLパラメータ設定
        params.envMapMipLevels = m_bIBLAvailable ? m_EnvironmentMipLevels : 1;
        params.bIBLEnabled = m_bIBLAvailable ? 1 : 0;

        // SSAOパラメータ設定
        params.bSSAOEnabled = bSSAOAvailable ? 1 : 0;
        params.bNeuralBRDFEnabled = m_bNeuralBRDFAvailable ? 1 : 0;

        // IBL有効時はambientColor.wにIBL強度を設定
        if (m_bIBLAvailable)
        {
            params.ambientColor[3] = m_Settings.IBLIntensity;
        }

        m_LightDataBuffer->Update(&params, sizeof(GPULightingParams));

        // ライト配列バッファ更新
        m_LightArrayBuffer->Update(lightArray, sizeof(GPULightData) * lightCount);
    }

    // ========================================
    // HDR環境マップロード（ミップマップ付きRGBA16_FLOAT）
    // ========================================
    bool LightingPass::LoadEnvironmentMap(const Container::String &path)
    {
        if (path.empty())
        {
            NORVES_LOG_WARNING("LightingPass", "No environment map path specified");
            return false;
        }

        // パス解決
        Container::String resolvedPath = path;
#ifdef NORVES_ASSET_DIR
        if (path.size() > 0 && path[0] != '/' && path[0] != '\\' &&
            (path.size() < 2 || path[1] != ':'))
        {
            Container::String relativePath = path;
            if (relativePath.size() > 7)
            {
                Container::String prefix = relativePath.substr(0, 7);
                if (prefix == "Assets/" || prefix == "Assets\\")
                {
                    relativePath = relativePath.substr(7);
                }
            }
            resolvedPath = Container::String(NORVES_ASSET_DIR) + "/" + relativePath;
        }
#endif

        NORVES_LOG_INFO("LightingPass", "Loading HDR environment map...");
        NORVES_LOG_INFO("LightingPass", resolvedPath.c_str());

        // HDRファイル読み込み（float32 RGBA）
        int width = 0, height = 0, channels = 0;
        float *hdrData = stbi_loadf(resolvedPath.c_str(), &width, &height, &channels, 4);
        if (!hdrData)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to load HDR environment map");
            return false;
        }

        NORVES_LOG_INFO("LightingPass", "HDR loaded successfully");

        // ミップレベル数の計算
        uint32_t mipLevels = 1;
        {
            uint32_t maxDim = (std::max)(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            while (maxDim > 1)
            {
                maxDim >>= 1;
                ++mipLevels;
            }
        }
        m_EnvironmentMipLevels = mipLevels;

        // テクスチャ作成
        RHI::TextureDesc envDesc;
        envDesc.Width = static_cast<uint32_t>(width);
        envDesc.Height = static_cast<uint32_t>(height);
        envDesc.MipLevels = mipLevels;
        envDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        envDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        envDesc.DebugName = "EnvironmentMap";

        m_EnvironmentTexture = m_Device->CreateTexture(envDesc);
        if (!m_EnvironmentTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create environment map texture");
            stbi_image_free(hdrData);
            return false;
        }

        // ========================================
        // ミップチェーン生成とアップロード
        // ========================================
        uint32_t mipWidth = static_cast<uint32_t>(width);
        uint32_t mipHeight = static_cast<uint32_t>(height);
        size_t basePixelCount = static_cast<size_t>(width) * height;

        // float32データの作業バッファ
        Container::VariableArray<float> currentMipData(basePixelCount * 4);
        std::memcpy(currentMipData.data(), hdrData, basePixelCount * 4 * sizeof(float));
        stbi_image_free(hdrData);

        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            size_t pixelCount = static_cast<size_t>(mipWidth) * mipHeight;

            // float32 → half16 変換
            Container::VariableArray<uint16_t> halfData(pixelCount * 4);
            for (size_t i = 0; i < pixelCount * 4; ++i)
            {
                halfData[i] = FloatToHalf(currentMipData[i]);
            }

            uint32_t rowPitch = mipWidth * 4 * static_cast<uint32_t>(sizeof(uint16_t));
            uint32_t slicePitch = rowPitch * mipHeight;
            m_EnvironmentTexture->Update(halfData.data(), rowPitch, slicePitch, mip);

            // 次のミップレベル生成（box filter）
            if (mip + 1 < mipLevels)
            {
                uint32_t nextWidth = (std::max)(mipWidth / 2, 1u);
                uint32_t nextHeight = (std::max)(mipHeight / 2, 1u);
                Container::VariableArray<float> nextMipData(static_cast<size_t>(nextWidth) * nextHeight * 4);

                for (uint32_t y = 0; y < nextHeight; ++y)
                {
                    for (uint32_t x = 0; x < nextWidth; ++x)
                    {
                        uint32_t sx = x * 2;
                        uint32_t sy = y * 2;
                        uint32_t sx1 = (std::min)(sx + 1, mipWidth - 1);
                        uint32_t sy1 = (std::min)(sy + 1, mipHeight - 1);

                        for (uint32_t c = 0; c < 4; ++c)
                        {
                            float sum = 0.0f;
                            sum += currentMipData[(sy * mipWidth + sx) * 4 + c];
                            sum += currentMipData[(sy * mipWidth + sx1) * 4 + c];
                            sum += currentMipData[(sy1 * mipWidth + sx) * 4 + c];
                            sum += currentMipData[(sy1 * mipWidth + sx1) * 4 + c];
                            nextMipData[(y * nextWidth + x) * 4 + c] = sum * 0.25f;
                        }
                    }
                }

                currentMipData = std::move(nextMipData);
                mipWidth = nextWidth;
                mipHeight = nextHeight;
            }
        }

        NORVES_LOG_INFO("LightingPass", "Environment map created with mipmaps");
        return true;
    }

    // ========================================
    // BRDF LUT CPU生成（split-sum近似）
    // ========================================
    bool LightingPass::GenerateBRDFLut()
    {
        constexpr uint32_t LUT_SIZE = 256;
        constexpr uint32_t SAMPLE_COUNT = 1024;
        constexpr float PI = 3.14159265359f;

        NORVES_LOG_INFO("LightingPass", "Generating BRDF LUT...");

        // RG16_FLOAT LUT
        Container::VariableArray<uint16_t> lutData(static_cast<size_t>(LUT_SIZE) * LUT_SIZE * 2, 0);

        for (uint32_t y = 0; y < LUT_SIZE; ++y)
        {
            float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(LUT_SIZE);
            roughness = (std::max)(roughness, 0.01f); // 0に近いとGGXが不安定になる

            for (uint32_t x = 0; x < LUT_SIZE; ++x)
            {
                float NdotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(LUT_SIZE);
                NdotV = (std::max)(NdotV, 0.001f);

                // V vector in tangent space (N = (0,0,1))
                float Vx = std::sqrt(1.0f - NdotV * NdotV);
                float Vy = 0.0f;
                float Vz = NdotV;

                float A = 0.0f;
                float B = 0.0f;

                for (uint32_t i = 0; i < SAMPLE_COUNT; ++i)
                {
                    // Hammersley sequence
                    float u = static_cast<float>(i) / static_cast<float>(SAMPLE_COUNT);
                    uint32_t bits = i;
                    bits = (bits << 16u) | (bits >> 16u);
                    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                    float v = static_cast<float>(bits) * 2.3283064365386963e-10f;

                    // ImportanceSample GGX
                    float a = roughness * roughness;
                    float phi = 2.0f * PI * u;
                    float cosTheta = std::sqrt((1.0f - v) / (1.0f + (a * a - 1.0f) * v));
                    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

                    // Half vector in tangent space
                    float Hx = sinTheta * std::cos(phi);
                    float Hy = sinTheta * std::sin(phi);
                    float Hz = cosTheta;

                    // Reflect V around H to get L
                    float VdotH = Vx * Hx + Vy * Hy + Vz * Hz;
                    float Lx = 2.0f * VdotH * Hx - Vx;
                    float Ly = 2.0f * VdotH * Hy - Vy;
                    float Lz = 2.0f * VdotH * Hz - Vz;

                    float NdotL = (std::max)(Lz, 0.0f);
                    float NdotH = (std::max)(Hz, 0.0f);
                    VdotH = (std::max)(VdotH, 0.0f);

                    if (NdotL > 0.0f)
                    {
                        // Smith GGX for IBL: k = roughness^2 / 2
                        float k = (roughness * roughness) / 2.0f;
                        float G_V = NdotV / (NdotV * (1.0f - k) + k);
                        float G_L = NdotL / (NdotL * (1.0f - k) + k);
                        float G = G_V * G_L;

                        float G_Vis = (G * VdotH) / (NdotH * NdotV + 0.0001f);
                        float Fc = std::pow(1.0f - VdotH, 5.0f);

                        A += (1.0f - Fc) * G_Vis;
                        B += Fc * G_Vis;
                    }
                }

                A /= static_cast<float>(SAMPLE_COUNT);
                B /= static_cast<float>(SAMPLE_COUNT);

                // Clamp to valid range
                A = (std::max)(0.0f, (std::min)(1.0f, A));
                B = (std::max)(0.0f, (std::min)(1.0f, B));

                size_t idx = (static_cast<size_t>(y) * LUT_SIZE + x) * 2;
                lutData[idx + 0] = FloatToHalf(A);
                lutData[idx + 1] = FloatToHalf(B);
            }
        }

        // テクスチャ作成（R16G16_FLOAT）
        RHI::TextureDesc lutDesc;
        lutDesc.Width = LUT_SIZE;
        lutDesc.Height = LUT_SIZE;
        lutDesc.MipLevels = 1;
        lutDesc.TextureFormat = RHI::Format::R16G16_FLOAT;
        lutDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        lutDesc.DebugName = "BRDF_LUT";

        m_BrdfLutTexture = m_Device->CreateTexture(lutDesc);
        if (!m_BrdfLutTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create BRDF LUT texture");
            return false;
        }

        uint32_t rowPitch = LUT_SIZE * 2 * static_cast<uint32_t>(sizeof(uint16_t));
        uint32_t slicePitch = rowPitch * LUT_SIZE;
        m_BrdfLutTexture->Update(lutData.data(), rowPitch, slicePitch);

        NORVES_LOG_INFO("LightingPass", "BRDF LUT generated");
        return true;
    }

} // namespace NorvesLib::Core::Rendering
