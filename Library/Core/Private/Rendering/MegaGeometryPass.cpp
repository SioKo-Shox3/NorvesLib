#include "Rendering/MegaGeometryPass.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/CameraViewConstants.h"
#include "Debug/DebugConfig.h"
#include "Math/MatrixUtils.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IBuffer.h"
#include "RHI/IFramebuffer.h"
#include "RHI/ITexture.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/ISampler.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/DeviceCapabilities.h"
#include "Text/IdentityPool.h"
#include "Logging/LogMacros.h"
#include <cstring>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    using namespace Container;

    // ========================================
    // コンストラクタ / デストラクタ
    // ========================================

    MegaGeometryPass::MegaGeometryPass(const MegaGeometryPassSettings &settings)
        : m_Settings(settings)
    {
    }

    MegaGeometryPass::~MegaGeometryPass()
    {
        if (IsInitialized())
        {
            Shutdown();
        }
    }

    // ========================================
    // Initialize
    // ========================================

    bool MegaGeometryPass::Initialize(ViewRenderContext &context)
    {
        m_Device = context.Device;
        if (!m_Device)
        {
            return false;
        }

        // カリングコンピュートシェーダーの読み込み
        if (context.ShaderMgr)
        {
            m_CullShader = context.ShaderMgr->LoadShader(
                "cluster_cull.comp", RHI::ShaderStage::Compute);
        }
        if (!m_CullShader)
        {
            NORVES_LOG_WARNING("MegaGeometryPass", "カリングシェーダーの読み込みに失敗。パスは無効化されます");
            m_bInitialized = true;
            return true;
        }

        // カリング用GPUリソース作成
        if (!CreateCullResources(m_Device))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリング用GPUリソースの作成に失敗");
            return false;
        }

        // カリングコンピュートパイプライン作成
        RHI::ComputePipelineDesc cullPipelineDesc;
        cullPipelineDesc.computeShader = m_CullShader;
        {
            RHI::DescriptorSetDesc cullDsDesc;

            RHI::DescriptorBinding uniformBinding;
            uniformBinding.binding = 0;
            uniformBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uniformBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(uniformBinding);

            RHI::DescriptorBinding clusterBinding;
            clusterBinding.binding = 1;
            clusterBinding.type = RHI::ResourceBindType::RWBuffer;
            clusterBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(clusterBinding);

            RHI::DescriptorBinding indirectBinding;
            indirectBinding.binding = 2;
            indirectBinding.type = RHI::ResourceBindType::RWBuffer;
            indirectBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(indirectBinding);

            RHI::DescriptorBinding drawCountBinding;
            drawCountBinding.binding = 3;
            drawCountBinding.type = RHI::ResourceBindType::RWBuffer;
            drawCountBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(drawCountBinding);

            RHI::DescriptorBinding hiZBinding;
            hiZBinding.binding = 4;
            hiZBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            hiZBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(hiZBinding);

            cullPipelineDesc.descriptorSetLayouts.push_back(cullDsDesc);
        }
        m_CullPipeline = m_Device->CreateComputePipeline(cullPipelineDesc);
        if (!m_CullPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリングパイプラインの作成に失敗");
            return false;
        }

        // GBuffer描画用シェーダーの読み込み（MegaGeometry専用GBuffer互換シェーダー）
        if (context.ShaderMgr)
        {
            m_DrawVertexShader = context.ShaderMgr->LoadShader(
                "megageometry.vert", RHI::ShaderStage::Vertex);
            m_DrawFragmentShader = context.ShaderMgr->LoadShader(
                "megageometry.frag", RHI::ShaderStage::Pixel);
        }
        if (!m_DrawVertexShader || !m_DrawFragmentShader)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "GBuffer描画シェーダーの読み込みに失敗");
            return false;
        }

        // デフォルトPBRテクスチャの作成
        auto createDefault1x1 = [this](const char *debugName, uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> RHI::TexturePtr
        {
            RHI::TextureDesc texDesc;
            texDesc.Width = 1;
            texDesc.Height = 1;
            texDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            texDesc.Usage = RHI::ResourceUsage::ShaderRead;
            texDesc.DebugName = debugName;

            auto tex = m_Device->CreateTexture(texDesc);
            if (tex)
            {
                uint8_t pixel[4] = {r, g, b, a};
                tex->Update(pixel, 4, 4);
            }
            return tex;
        };

        m_DefaultWhiteTexture = createDefault1x1("MegaGeometry_DefaultWhite", 255, 255, 255, 255);
        m_DefaultFlatNormalTexture = createDefault1x1("MegaGeometry_DefaultFlatNormal", 128, 128, 255, 255);
        m_DefaultBlackTexture = createDefault1x1("MegaGeometry_DefaultBlack", 0, 0, 0, 255);

        if (!m_DefaultWhiteTexture || !m_DefaultFlatNormalTexture || !m_DefaultBlackTexture)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "デフォルトPBRテクスチャの作成に失敗");
            return false;
        }

        // デフォルトLinearサンプラーの作成
        RHI::SamplerDesc sampDesc;
        sampDesc.filterMin = RHI::FilterMode::Linear;
        sampDesc.filterMag = RHI::FilterMode::Linear;
        sampDesc.filterMip = RHI::FilterMode::Linear;
        sampDesc.addressU = RHI::TextureAddressMode::Wrap;
        sampDesc.addressV = RHI::TextureAddressMode::Wrap;
        sampDesc.addressW = RHI::TextureAddressMode::Wrap;
        m_DefaultLinearSampler = m_Device->CreateSampler(sampDesc);
        if (!m_DefaultLinearSampler)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "デフォルトサンプラーの作成に失敗");
            return false;
        }

        // Hi-Z 深度ピラミッド用リソース作成
        if (context.ShaderMgr)
        {
            m_HiZShader = context.ShaderMgr->LoadShader(
                "hiz_generate.comp", RHI::ShaderStage::Compute);
        }
        if (m_HiZShader)
        {
            RHI::ComputePipelineDesc hiZPipelineDesc;
            hiZPipelineDesc.computeShader = m_HiZShader;
            {
                RHI::DescriptorSetDesc hiZDsDesc;

                RHI::DescriptorBinding srcBinding;
                srcBinding.binding = 0;
                srcBinding.type = RHI::ResourceBindType::CombinedImageSampler;
                srcBinding.stages = RHI::ShaderStage::Compute;
                hiZDsDesc.bindings.push_back(srcBinding);

                RHI::DescriptorBinding dstBinding;
                dstBinding.binding = 1;
                dstBinding.type = RHI::ResourceBindType::RWTexture;
                dstBinding.stages = RHI::ShaderStage::Compute;
                hiZDsDesc.bindings.push_back(dstBinding);

                RHI::DescriptorBinding paramBinding;
                paramBinding.binding = 2;
                paramBinding.type = RHI::ResourceBindType::ConstantBuffer;
                paramBinding.stages = RHI::ShaderStage::Compute;
                hiZDsDesc.bindings.push_back(paramBinding);

                hiZPipelineDesc.descriptorSetLayouts.push_back(hiZDsDesc);
            }
            m_HiZPipeline = m_Device->CreateComputePipeline(hiZPipelineDesc);

            // Hi-Z用Nearestサンプラー（Clamp）
            RHI::SamplerDesc hiZSampDesc;
            hiZSampDesc.filterMin = RHI::FilterMode::Point;
            hiZSampDesc.filterMag = RHI::FilterMode::Point;
            hiZSampDesc.filterMip = RHI::FilterMode::Point;
            hiZSampDesc.addressU = RHI::TextureAddressMode::Clamp;
            hiZSampDesc.addressV = RHI::TextureAddressMode::Clamp;
            hiZSampDesc.addressW = RHI::TextureAddressMode::Clamp;
            m_HiZNearestSampler = m_Device->CreateSampler(hiZSampDesc);

            // Hi-ZパラメータUBO (ivec2 destSize = 8 bytes, pad to 16)
            RHI::BufferDesc hiZUboDesc(
                16,
                RHI::ResourceUsage::ConstantBuffer,
                true,
                "MegaGeometry_HiZParamsUBO");
            m_HiZParamsBuffer = m_Device->CreateBuffer(hiZUboDesc);

            if (!m_HiZPipeline || !m_HiZNearestSampler || !m_HiZParamsBuffer)
            {
                NORVES_LOG_WARNING("MegaGeometryPass", "Hi-Zリソースの作成に失敗。オクルージョンカリング無効");
                m_HiZPipeline.reset();
                m_HiZShader.reset();
                m_HiZNearestSampler.reset();
                m_HiZParamsBuffer.reset();
            }
            else
            {
                NORVES_LOG_INFO("MegaGeometryPass", "Hi-Zオクルージョンカリング有効");
            }
        }
        else
        {
            NORVES_LOG_WARNING("MegaGeometryPass", "Hi-Zシェーダーの読み込みに失敗。オクルージョンカリング無効");
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("MegaGeometryPass", "初期化完了 (MaxDrawCount: %u)", m_Settings.MaxDrawCount);
        return true;
    }

    // ========================================
    // Shutdown
    // ========================================

    void MegaGeometryPass::Shutdown()
    {
        m_CullPipeline.reset();
        m_CullShader.reset();
        m_IndirectDrawBuffer.reset();
        m_DrawCountBuffer.reset();
        m_IndirectDrawBufferHandle = {};
        m_DrawCountBufferHandle = {};
        m_MegaGeometryCompleteHandle = {};
        m_InstanceIndirectDrawBuffers.clear();
        m_InstanceDrawCountBuffers.clear();
        m_CullUniformBuffers.clear();
        m_CullDescriptorSets.clear();

        m_DrawPipeline.reset();
        m_DrawWireframePipeline.reset();
        m_DrawVertexShader.reset();
        m_DrawFragmentShader.reset();
        m_DrawUniformBuffers.clear();
        m_DrawDescriptorSets.clear();

        m_GBufferRenderPass.reset();
        m_GBufferFramebuffer.reset();
        m_AlbedoTexture.reset();
        m_NormalTexture.reset();
        m_MaterialTexture.reset();
        m_EmissiveTexture.reset();
        m_DepthTexture.reset();
        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferEmissiveHandle = {};
        m_GBufferDepthHandle = {};

        m_DefaultWhiteTexture.reset();
        m_DefaultFlatNormalTexture.reset();
        m_DefaultBlackTexture.reset();
        m_DefaultLinearSampler.reset();

        m_HiZTexture.reset();
        m_HiZShader.reset();
        m_HiZPipeline.reset();
        m_HiZDescriptorSet.reset();
        m_HiZParamsBuffer.reset();
        m_HiZNearestSampler.reset();
        m_HiZMipCount = 0;

        m_Instances.clear();
        m_bPreferRenderGraphGBufferResources = false;
        m_bGBufferRenderPassUsesRenderGraphAttachmentStates = false;

        m_bInitialized = false;
    }

    // ========================================
    // Setup
    // ========================================

    void MegaGeometryPass::Setup(ViewRenderContext &context)
    {
        m_Instances.clear();

        if (context.SnapshotMegaGeometryProxies)
        {
            const auto &megaGeometryProxies = *context.SnapshotMegaGeometryProxies;
            m_Instances.reserve(megaGeometryProxies.size());

            for (const auto &proxy : megaGeometryProxies)
            {
                if (!proxy.IsValid())
                {
                    continue;
                }

                MegaMeshInstance instance;
                instance.Handle = proxy.MegaMeshHandle;
                std::memcpy(instance.WorldMatrix, &proxy.WorldTransform, sizeof(float) * 16);
                m_Instances.push_back(instance);
            }
        }

        const bool bNeedsDrawPipeline = !m_Instances.empty() && m_CullPipeline;
        const bool bNeedsAttachmentTransitionPass = m_bPreferRenderGraphGBufferResources;
        if (!bNeedsDrawPipeline && !bNeedsAttachmentTransitionPass)
        {
            return;
        }

        if (!m_bPreferRenderGraphGBufferResources)
        {
            m_AlbedoTexture.reset();
            m_NormalTexture.reset();
            m_MaterialTexture.reset();
            m_EmissiveTexture.reset();
            m_DepthTexture.reset();
        }

        // Legacy bridge fallback: named resource が不足している場合だけSharedResourcesから補完する。
        if (context.SharedResources)
        {
            if (!m_AlbedoTexture)
            {
                m_AlbedoTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Albedo"));
            }
            if (!m_NormalTexture)
            {
                m_NormalTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Normal"));
            }
            if (!m_MaterialTexture)
            {
                m_MaterialTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Material"));
            }
            if (!m_EmissiveTexture)
            {
                m_EmissiveTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Emissive"));
            }
            if (!m_DepthTexture)
            {
                m_DepthTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Depth"));
            }
        }

        // GBufferテクスチャが利用可能か確認
        if (!m_AlbedoTexture ||
            !m_NormalTexture ||
            !m_MaterialTexture ||
            !m_EmissiveTexture ||
            !m_DepthTexture)
        {
            return;
        }

        // 画面サイズ変更に対応
        uint32_t width = context.GetActiveRenderWidth();
        uint32_t height = context.GetActiveRenderHeight();
        const bool bUseRenderGraphAttachmentStates = m_bPreferRenderGraphGBufferResources;

        bool bFramebufferTargetsChanged = true;
        if (m_GBufferFramebuffer)
        {
            bFramebufferTargetsChanged =
                m_GBufferFramebuffer->GetColorAttachment(0).get() != m_AlbedoTexture.get() ||
                m_GBufferFramebuffer->GetColorAttachment(1).get() != m_NormalTexture.get() ||
                m_GBufferFramebuffer->GetColorAttachment(2).get() != m_MaterialTexture.get() ||
                m_GBufferFramebuffer->GetColorAttachment(3).get() != m_EmissiveTexture.get() ||
                m_GBufferFramebuffer->GetDepthStencilAttachment().get() != m_DepthTexture.get();
        }

        bool bDrawPipelinesReady = m_DrawPipeline != nullptr;
#if NORVES_BUILD_DEVELOPMENT
        bDrawPipelinesReady = bDrawPipelinesReady && m_DrawWireframePipeline != nullptr;
#endif

        if (width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            bFramebufferTargetsChanged ||
            m_bGBufferRenderPassUsesRenderGraphAttachmentStates != bUseRenderGraphAttachmentStates ||
            !m_GBufferRenderPass ||
            !m_GBufferFramebuffer ||
            (bNeedsDrawPipeline && !bDrawPipelinesReady))
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // GBuffer互換レンダーパス作成（Load既存内容）
            if (!CreateDrawPipeline(context,
                                    bNeedsDrawPipeline,
                                    bUseRenderGraphAttachmentStates))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "描画パイプラインの作成に失敗");
                return;
            }

            // Hi-Z深度ピラミッドの作成・再作成
            if (m_HiZPipeline)
            {
                if (!CreateHiZResources(context))
                {
                    NORVES_LOG_WARNING("MegaGeometryPass", "Hi-Zテクスチャの作成に失敗。オクルージョンカリング無効");
                }
            }
        }
    }

    void MegaGeometryPass::Declare(RenderGraphBuilder &builder)
    {
        m_IndirectDrawBufferHandle = {};
        m_DrawCountBufferHandle = {};
        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferEmissiveHandle = {};
        m_GBufferDepthHandle = {};

        if (m_IndirectDrawBuffer)
        {
            m_IndirectDrawBufferHandle = builder.ImportBuffer(m_IndirectDrawBuffer,
                                                              RHI::ResourceState::Common,
                                                              "MegaGeometry_IndirectDraw");
            if (m_IndirectDrawBufferHandle.IsValid())
            {
                builder.Write(m_IndirectDrawBufferHandle,
                              RHI::ResourceState::Common,
                              RHI::ResourceState::Common);
            }
        }

        if (m_DrawCountBuffer)
        {
            m_DrawCountBufferHandle = builder.ImportBuffer(m_DrawCountBuffer,
                                                           RHI::ResourceState::Common,
                                                           "MegaGeometry_DrawCount");
            if (m_DrawCountBufferHandle.IsValid())
            {
                builder.Write(m_DrawCountBufferHandle,
                              RHI::ResourceState::Common,
                              RHI::ResourceState::Common);
            }
        }

        RGTextureHandle albedoProbe;
        RGTextureHandle normalProbe;
        RGTextureHandle materialProbe;
        RGTextureHandle emissiveProbe;
        RGTextureHandle depthProbe;
        const bool bHasAllNamedGBufferAttachments =
            builder.TryGetTexture(RenderGraphResourceNames::GBufferAlbedo, albedoProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normalProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferMaterial, materialProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferEmissive, emissiveProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, depthProbe);

        if (bHasAllNamedGBufferAttachments)
        {
            RGTextureHandle albedoHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferAlbedo,
                                                    albedoHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferAlbedoHandle = albedoHandle.ToResourceHandle();
            }

            RGTextureHandle normalHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferNormal,
                                                    normalHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferNormalHandle = normalHandle.ToResourceHandle();
            }

            RGTextureHandle materialHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferMaterial,
                                                    materialHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferMaterialHandle = materialHandle.ToResourceHandle();
            }

            RGTextureHandle emissiveHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferEmissive,
                                                    emissiveHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferEmissiveHandle = emissiveHandle.ToResourceHandle();
            }

            RGTextureHandle depthHandle;
            if (builder.TryUseAttachment(RenderGraphResourceNames::GBufferDepth,
                                         depthHandle,
                                         RGAttachmentKind::DepthStencil,
                                         RGAttachmentMutability::Write,
                                         RHI::AttachmentLoadOp::Load,
                                         RHI::AttachmentStoreOp::Store,
                                         RHI::ResourceState::DepthWrite,
                                         RHI::ResourceState::ShaderResource))
            {
                m_GBufferDepthHandle = depthHandle.ToResourceHandle();
            }
        }

        m_MegaGeometryCompleteHandle = builder.CreateLogical("MegaGeometryComplete");
        builder.Write(m_MegaGeometryCompleteHandle,
                      RHI::ResourceState::Common,
                      RHI::ResourceState::Common);
        builder.PreserveInsertionOrder();
    }

    void MegaGeometryPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        RHI::TexturePtr graphAlbedoTexture = m_GBufferAlbedoHandle.IsValid()
                                                 ? resources.GetTexture(m_GBufferAlbedoHandle)
                                                 : nullptr;
        RHI::TexturePtr graphNormalTexture = m_GBufferNormalHandle.IsValid()
                                                 ? resources.GetTexture(m_GBufferNormalHandle)
                                                 : nullptr;
        RHI::TexturePtr graphMaterialTexture = m_GBufferMaterialHandle.IsValid()
                                                   ? resources.GetTexture(m_GBufferMaterialHandle)
                                                   : nullptr;
        RHI::TexturePtr graphEmissiveTexture = m_GBufferEmissiveHandle.IsValid()
                                                   ? resources.GetTexture(m_GBufferEmissiveHandle)
                                                   : nullptr;
        RHI::TexturePtr graphDepthTexture = m_GBufferDepthHandle.IsValid()
                                                ? resources.GetTexture(m_GBufferDepthHandle)
                                                : nullptr;

        const bool bHasAllRenderGraphGBufferResources =
            graphAlbedoTexture &&
            graphNormalTexture &&
            graphMaterialTexture &&
            graphEmissiveTexture &&
            graphDepthTexture;

        m_bPreferRenderGraphGBufferResources = bHasAllRenderGraphGBufferResources;
        if (m_bPreferRenderGraphGBufferResources)
        {
            m_AlbedoTexture = graphAlbedoTexture;
            m_NormalTexture = graphNormalTexture;
            m_MaterialTexture = graphMaterialTexture;
            m_EmissiveTexture = graphEmissiveTexture;
            m_DepthTexture = graphDepthTexture;
        }
        else
        {
            m_AlbedoTexture.reset();
            m_NormalTexture.reset();
            m_MaterialTexture.reset();
            m_EmissiveTexture.reset();
            m_DepthTexture.reset();
        }

        Setup(context);
        Execute(context);
        m_bPreferRenderGraphGBufferResources = false;
    }

    // ========================================
    // Execute
    // ========================================

    void MegaGeometryPass::Execute(ViewRenderContext &context)
    {
        const bool bCanEnqueueEmptyTransitionPass =
            m_bPreferRenderGraphGBufferResources &&
            m_GBufferRenderPass &&
            m_GBufferFramebuffer;
        auto enqueueEmptyTransitionPass = [this, &context, bCanEnqueueEmptyTransitionPass]() -> void
        {
            if (!bCanEnqueueEmptyTransitionPass)
            {
                return;
            }

            context.EnqueueFullscreenPass(m_GBufferRenderPass,
                                          m_GBufferFramebuffer,
                                          context.GetActiveLocalViewport(),
                                          context.GetActiveLocalScissor(),
                                          RHI::PipelinePtr{},
                                          RHI::DescriptorSetPtr{},
                                          0,
                                          0);
        };

        if (m_Instances.empty() || !m_CullPipeline || !context.Resources.MegaGeometry)
        {
            enqueueEmptyTransitionPass();
            return;
        }

        bool bDrawPipelinesReady = m_DrawPipeline != nullptr;
#if NORVES_BUILD_DEVELOPMENT
        bDrawPipelinesReady = bDrawPipelinesReady && m_DrawWireframePipeline != nullptr;
#endif

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !bDrawPipelinesReady)
        {
            enqueueEmptyTransitionPass();
            return;
        }

        if (!context.GetActiveCamera())
        {
            enqueueEmptyTransitionPass();
            return;
        }

        context.EnqueueMegaGeometryPass(this);
    }

    void MegaGeometryPass::RecordFrameCommand(const MegaGeometryPassCommand &command, RHI::ICommandList *commandList)
    {
        if (m_Instances.empty() || !m_CullPipeline || !commandList || !command.MegaGeometry || !command.bHasMainCamera)
        {
            return;
        }

        bool bDrawPipelinesReady = m_DrawPipeline != nullptr;
#if NORVES_BUILD_DEVELOPMENT
        bDrawPipelinesReady = bDrawPipelinesReady && m_DrawWireframePipeline != nullptr;
#endif

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !bDrawPipelinesReady)
        {
            return;
        }

        if (!EnsurePerInstanceBindings(static_cast<uint32_t>(m_Instances.size())))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "Failed to prepare per-instance bindings");
            return;
        }

        auto *cmdList = commandList;

        auto recordEmptyRenderPass = [this, &command, cmdList]() -> void
        {
            cmdList->BeginRenderPass(m_GBufferRenderPass, m_GBufferFramebuffer);
            cmdList->SetViewport(command.Viewport);
            cmdList->SetScissor(command.Scissor);
            cmdList->EndRenderPass();
        };

        using namespace NorvesLib::Math;

        const auto &cam = command.MainCamera;
        const float aspectRatio = m_CurrentHeight > 0
                                      ? static_cast<float>(m_CurrentWidth) / static_cast<float>(m_CurrentHeight)
                                      : 1.0f;
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(cam, aspectRatio, m_Device);

        const ClipSpaceFrustumPlanes frustumPlanes =
            MatrixUtils::ExtractClipSpaceFrustumPlanes(cameraConstants.ViewProjectionMatrix,
                                                       ClipSpaceDepthRange::ZeroToOne);

        struct DrawableInstance
        {
            size_t InstanceIndex = 0;
            const MegaGeometry::MegaMeshGPUData *GpuData = nullptr;
            RHI::BufferPtr IndirectDrawBuffer;
            RHI::BufferPtr DrawCountBuffer;
            RHI::DescriptorSetPtr DrawDescriptorSet;
        };

        VariableArray<DrawableInstance> drawableInstances;
        drawableInstances.reserve(m_Instances.size());

        // ========================================
        // Hi-Z 深度ピラミッド生成（オクルージョンカリング用）
        // ========================================
        // 現状のMegaGeometryは、クラスタ球ベースのHi-Z判定が角度・距離依存で過剰カリングを起こし、
        // モデル全体の消失や穴あきに繋がるため一旦無効化する。
        // 専用の深度プレパスや、より保守的な遮蔽判定が整うまではFrustum/Backfaceのみで描画安定性を優先する。

        // ========================================
        // 各MegaMeshインスタンスに対してカリングを実行し、描画入力を準備
        // ========================================
        for (size_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
        {
            const auto &instance = m_Instances[instanceIndex];
            const auto *gpuData = command.MegaGeometry->GetMegaMeshGPUData(instance.Handle);
            if (!gpuData || gpuData->ClusterCount == 0)
            {
                continue;
            }

            auto cullUniformBuffer = m_CullUniformBuffers[instanceIndex];
            auto cullDescriptorSet = m_CullDescriptorSets[instanceIndex];
            auto drawUniformBuffer = m_DrawUniformBuffers[instanceIndex];
            auto drawDescriptorSet = m_DrawDescriptorSets[instanceIndex];
            auto indirectDrawBuffer = m_InstanceIndirectDrawBuffers[instanceIndex];
            auto drawCountBuffer = m_InstanceDrawCountBuffers[instanceIndex];

            if (!cullUniformBuffer ||
                !cullDescriptorSet ||
                !drawUniformBuffer ||
                !drawDescriptorSet ||
                !indirectDrawBuffer ||
                !drawCountBuffer)
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "Invalid per-instance MegaGeometry binding at slot %zu", instanceIndex);
                return;
            }

            // ----------------------------------------
            // 1. DrawCountバッファをゼロクリア
            // ----------------------------------------
            cmdList->BufferBarrier(drawCountBuffer,
                                   RHI::ResourceState::Common,
                                   RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(drawCountBuffer, 0, sizeof(uint32_t), 0);
            cmdList->BufferBarrier(drawCountBuffer,
                                   RHI::ResourceState::CopyDest,
                                   RHI::ResourceState::UnorderedAccess);

            // IndirectDrawバッファもゼロクリアしてからUAV状態にする
            cmdList->BufferBarrier(indirectDrawBuffer,
                                   RHI::ResourceState::Common,
                                   RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(indirectDrawBuffer,
                                0,
                                static_cast<uint64_t>(m_Settings.MaxDrawCount) *
                                    sizeof(MegaGeometry::DrawIndexedIndirectCommand),
                                0);
            cmdList->BufferBarrier(indirectDrawBuffer,
                                   RHI::ResourceState::CopyDest,
                                   RHI::ResourceState::UnorderedAccess);

            // ----------------------------------------
            // 2. カリングユニフォーム更新
            // ----------------------------------------
            CullUniformData uniformData{};

            cameraConstants.CopyShaderView(uniformData.ViewMatrix);
            cameraConstants.CopyShaderProjection(uniformData.ProjectionMatrix);
            cameraConstants.CopyCameraPosition(uniformData.CameraPosition);
            uniformData.CameraPosition[3] = 0.0f;

            frustumPlanes.CopyToShaderData(uniformData.FrustumPlanes);

            uniformData.TotalClusterCount = gpuData->ClusterCount;
            uniformData.MaxDrawCount = m_Settings.MaxDrawCount;
            uniformData.LODBias = m_Settings.LODBias;
            uniformData.ScreenHeight = static_cast<float>(m_CurrentHeight);

            // projectionFactor = screenHeight / (2 * tan(fov/2))
            float halfFovTan = std::tan(cameraConstants.FieldOfViewRadians * 0.5f);
            uniformData.ProjectionFactor = (halfFovTan > 1e-6f)
                                               ? static_cast<float>(m_CurrentHeight) / (2.0f * halfFovTan)
                                               : 1.0f;

            // Hi-Zオクルージョンカリングは現状のクラスタ球近似が攻めすぎているため無効化
            uniformData.HiZWidth = 0;
            uniformData.HiZHeight = 0;
            uniformData.HiZMipCount = 0;
            uniformData.bHiZEnabled = 0;

            std::memcpy(uniformData.WorldMatrix, instance.WorldMatrix, sizeof(float) * 16);
            cullUniformBuffer->Update(&uniformData, sizeof(CullUniformData));

            // ----------------------------------------
            // 3. カリングディスクリプタセット更新
            // ----------------------------------------
            cullDescriptorSet->BindConstantBuffer(0, cullUniformBuffer, 0,
                                                  static_cast<uint32_t>(sizeof(CullUniformData)));
            cullDescriptorSet->BindStorageBuffer(1, gpuData->ClusterBuffer, 0,
                                                 static_cast<uint32_t>(gpuData->ClusterCount * sizeof(MegaGeometry::GPUClusterData)));
            cullDescriptorSet->BindStorageBuffer(2, indirectDrawBuffer, 0,
                                                 static_cast<uint32_t>(m_Settings.MaxDrawCount * sizeof(MegaGeometry::DrawIndexedIndirectCommand)));
            cullDescriptorSet->BindStorageBuffer(3, drawCountBuffer, 0,
                                                 sizeof(uint32_t));

            // Hi-Zテクスチャバインド（無い場合はデフォルトテクスチャでフォールバック）
            if (m_HiZTexture)
            {
                cullDescriptorSet->BindTexture(4, m_HiZTexture);
                cullDescriptorSet->BindSampler(4, m_HiZNearestSampler);
            }
            else
            {
                cullDescriptorSet->BindTexture(4, m_DefaultBlackTexture);
                cullDescriptorSet->BindSampler(4, m_DefaultLinearSampler);
            }

            cullDescriptorSet->Update();

            // ----------------------------------------
            // 4. カリングコンピュートディスパッチ
            // ----------------------------------------
            cmdList->SetPipeline(m_CullPipeline);
            cmdList->SetDescriptorSet(cullDescriptorSet, 0);

            uint32_t groupCount = (gpuData->ClusterCount + 63) / 64;
            cmdList->Dispatch(groupCount, 1, 1);

            // ----------------------------------------
            // 5. バリア: Compute UAV → IndirectArgument
            // ----------------------------------------
            cmdList->BufferBarrier(indirectDrawBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::IndirectArgument);
            cmdList->BufferBarrier(drawCountBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::IndirectArgument);

            // ----------------------------------------
            // 6. GBuffer描画用ディスクリプタ更新
            // ----------------------------------------

            // PerObject UBO更新（ワールド変換行列）
            struct PerObjectUBO
            {
                float World[16];
                float View[16];
                float Projection[16];
                float CameraPosition[4];
                float ObjectColor[4];
                float EmissiveColor[4];
                float PomParams[4];
            };

            PerObjectUBO perObject{};
            std::memcpy(perObject.World, instance.WorldMatrix, sizeof(float) * 16);
            cameraConstants.CopyShaderView(perObject.View);
            cameraConstants.CopyShaderProjection(perObject.Projection);
            cameraConstants.CopyCameraPosition(perObject.CameraPosition);

            // マテリアル値を設定
            const auto &mat = gpuData->Material;
            perObject.ObjectColor[0] = mat.BaseColor[0];
            perObject.ObjectColor[1] = mat.BaseColor[1];
            perObject.ObjectColor[2] = mat.BaseColor[2];
            perObject.ObjectColor[3] = mat.BaseColor[3];
            perObject.EmissiveColor[0] = mat.EmissiveColor[0];
            perObject.EmissiveColor[1] = mat.EmissiveColor[1];
            perObject.EmissiveColor[2] = mat.EmissiveColor[2];
            perObject.EmissiveColor[3] = mat.EmissiveColor[3];
            perObject.PomParams[0] = mat.HeightScale;
            perObject.PomParams[1] = mat.bHasHeightMap ? 1.0f : 0.0f;
            perObject.PomParams[2] = 0.0f;
            perObject.PomParams[3] = 0.0f;

            drawUniformBuffer->Update(&perObject, sizeof(PerObjectUBO));

            drawDescriptorSet->BindConstantBuffer(0, drawUniformBuffer, 0,
                                                  static_cast<uint32_t>(sizeof(PerObjectUBO)));

            // PBRテクスチャバインド（マテリアルテクスチャまたはデフォルトにフォールバック）
            auto albedo = mat.AlbedoTexture ? mat.AlbedoTexture : m_DefaultWhiteTexture;
            auto normal = mat.NormalTexture ? mat.NormalTexture : m_DefaultFlatNormalTexture;
            auto metallic = mat.MetallicTexture ? mat.MetallicTexture : m_DefaultBlackTexture;
            auto roughness = mat.RoughnessTexture ? mat.RoughnessTexture : m_DefaultWhiteTexture;
            auto ao = mat.AOTexture ? mat.AOTexture : m_DefaultWhiteTexture;
            auto height = mat.HeightTexture ? mat.HeightTexture : m_DefaultBlackTexture;

            drawDescriptorSet->BindTexture(1, albedo);
            drawDescriptorSet->BindSampler(1, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(2, normal);
            drawDescriptorSet->BindSampler(2, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(3, metallic);
            drawDescriptorSet->BindSampler(3, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(4, roughness);
            drawDescriptorSet->BindSampler(4, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(5, ao);
            drawDescriptorSet->BindSampler(5, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(6, height);
            drawDescriptorSet->BindSampler(6, m_DefaultLinearSampler);

            drawDescriptorSet->Update();
            DrawableInstance drawableInstance;
            drawableInstance.InstanceIndex = instanceIndex;
            drawableInstance.GpuData = gpuData;
            drawableInstance.IndirectDrawBuffer = indirectDrawBuffer;
            drawableInstance.DrawCountBuffer = drawCountBuffer;
            drawableInstance.DrawDescriptorSet = drawDescriptorSet;
            drawableInstances.push_back(drawableInstance);
        }

        if (drawableInstances.empty())
        {
            recordEmptyRenderPass();
            return;
        }

        // ========================================
        // GBuffer render pass は frame command ごとに1回だけ開く
        // ========================================
        cmdList->BeginRenderPass(m_GBufferRenderPass, m_GBufferFramebuffer);
        cmdList->SetViewport(command.Viewport);
        cmdList->SetScissor(command.Scissor);
        cmdList->SetPipeline(SelectDrawPipeline(command.DebugMode));

        const auto &caps = m_Device->GetCapabilities();
        for (const DrawableInstance &drawableInstance : drawableInstances)
        {
            const auto *gpuData = drawableInstance.GpuData;
            if (!gpuData)
            {
                continue;
            }

            cmdList->SetDescriptorSet(drawableInstance.DrawDescriptorSet, 0);

            // 頂点/インデックスバッファ設定
            cmdList->SetVertexBuffer(gpuData->VertexBuffer, 0, 0);
            cmdList->SetIndexBuffer(gpuData->IndexBuffer, 0);

            // IndirectDraw発行
            // DrawIndirectCount対応の場合はGPU側カウントを参照し、
            // 実際に可視なクラスタ数だけドローコールを発行する。
            // 非対応の場合はMaxDrawCountをそのまま使用（instanceCount=0で空振り）。
            if (caps.bDrawIndirectCount)
            {
                cmdList->DrawIndexedIndirectCount(
                    drawableInstance.IndirectDrawBuffer, 0,
                    drawableInstance.DrawCountBuffer, 0,
                    m_Settings.MaxDrawCount,
                    sizeof(MegaGeometry::DrawIndexedIndirectCommand));
            }
            else
            {
                cmdList->DrawIndexedIndirect(
                    drawableInstance.IndirectDrawBuffer, 0,
                    m_Settings.MaxDrawCount,
                    sizeof(MegaGeometry::DrawIndexedIndirectCommand));
            }
        }

        cmdList->EndRenderPass();

        // IndirectDrawバッファを次のフレーム用に戻す
        for (const DrawableInstance &drawableInstance : drawableInstances)
        {
            cmdList->BufferBarrier(drawableInstance.IndirectDrawBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::Common);
            cmdList->BufferBarrier(drawableInstance.DrawCountBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::Common);
        }
    }

    // ========================================
    // MegaMeshインスタンス管理
    // ========================================

    void MegaGeometryPass::AddMegaMeshInstance(MegaGeometry::MegaMeshHandle handle, const float *worldMatrix)
    {
        MegaMeshInstance instance;
        instance.Handle = handle;
        if (worldMatrix)
        {
            std::memcpy(instance.WorldMatrix, worldMatrix, sizeof(float) * 16);
        }
        else
        {
            // 単位行列
            std::memset(instance.WorldMatrix, 0, sizeof(float) * 16);
            instance.WorldMatrix[0] = 1.0f;
            instance.WorldMatrix[5] = 1.0f;
            instance.WorldMatrix[10] = 1.0f;
            instance.WorldMatrix[15] = 1.0f;
        }
        m_Instances.push_back(instance);
    }

    void MegaGeometryPass::ClearMegaMeshInstances()
    {
        m_Instances.clear();
    }

    // ========================================
    // カリング用GPUリソース作成
    // ========================================

    bool MegaGeometryPass::CreateCullResources(RHI::IDevice *device)
    {
        // IndirectDrawコマンドバッファ (SSBO + IndirectBuffer)
        uint64_t indirectSize = static_cast<uint64_t>(m_Settings.MaxDrawCount) * sizeof(MegaGeometry::DrawIndexedIndirectCommand);
        RHI::BufferDesc indirectDesc(
            indirectSize,
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
            false,
            "MegaGeometry_IndirectDraw");
        m_IndirectDrawBuffer = device->CreateBuffer(indirectDesc);
        if (!m_IndirectDrawBuffer)
        {
            return false;
        }

        // DrawCountバッファ（atomic counter用 SSBO）
        RHI::BufferDesc countDesc(
            sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
            false,
            "MegaGeometry_DrawCount");
        m_DrawCountBuffer = device->CreateBuffer(countDesc);
        if (!m_DrawCountBuffer)
        {
            return false;
        }

        m_InstanceIndirectDrawBuffers.clear();
        m_InstanceDrawCountBuffers.clear();
        return true;
    }

    // ========================================
    // GBuffer互換グラフィックスパイプライン作成
    // ========================================

    bool MegaGeometryPass::CreateDrawPipeline(ViewRenderContext &context,
                                              bool bRequireDrawPipeline,
                                              bool bUseRenderGraphAttachmentStates)
    {
        if (!m_Device)
        {
            return false;
        }

        // GBuffer互換レンダーパス作成（Load既存内容）
        RHI::RenderPassDesc rpDesc;
        const RHI::ResourceState colorInitialState = bUseRenderGraphAttachmentStates
                                                         ? RHI::ResourceState::RenderTarget
                                                         : RHI::ResourceState::ShaderResource;
        const RHI::ResourceState depthInitialState = bUseRenderGraphAttachmentStates
                                                         ? RHI::ResourceState::DepthWrite
                                                         : RHI::ResourceState::ShaderResource;

        // Albedo: Load（GBufferPassで書いた内容を保持）
        RHI::AttachmentDesc albedoAttach;
        albedoAttach.format = RHI::Format::R8G8B8A8_UNORM;
        albedoAttach.isDepthStencil = false;
        albedoAttach.clear = false;
        albedoAttach.loadOp = RHI::AttachmentLoadOp::Load;
        albedoAttach.storeOp = RHI::AttachmentStoreOp::Store;
        albedoAttach.initialState = colorInitialState;
        albedoAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(albedoAttach);

        // Normal: Load
        RHI::AttachmentDesc normalAttach;
        normalAttach.format = RHI::Format::R16G16B16A16_FLOAT;
        normalAttach.isDepthStencil = false;
        normalAttach.clear = false;
        normalAttach.loadOp = RHI::AttachmentLoadOp::Load;
        normalAttach.storeOp = RHI::AttachmentStoreOp::Store;
        normalAttach.initialState = colorInitialState;
        normalAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(normalAttach);

        // Material: Load
        RHI::AttachmentDesc materialAttach;
        materialAttach.format = RHI::Format::R8G8B8A8_UNORM;
        materialAttach.isDepthStencil = false;
        materialAttach.clear = false;
        materialAttach.loadOp = RHI::AttachmentLoadOp::Load;
        materialAttach.storeOp = RHI::AttachmentStoreOp::Store;
        materialAttach.initialState = colorInitialState;
        materialAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(materialAttach);

        // Emissive: Load
        RHI::AttachmentDesc emissiveAttach;
        emissiveAttach.format = RHI::Format::R16G16B16A16_FLOAT;
        emissiveAttach.isDepthStencil = false;
        emissiveAttach.clear = false;
        emissiveAttach.loadOp = RHI::AttachmentLoadOp::Load;
        emissiveAttach.storeOp = RHI::AttachmentStoreOp::Store;
        emissiveAttach.initialState = colorInitialState;
        emissiveAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(emissiveAttach);

        // Depth: Load + DepthTest
        rpDesc.hasDepthStencil = true;
        rpDesc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
        rpDesc.depthStencilAttachment.isDepthStencil = true;
        rpDesc.depthStencilAttachment.clear = false;
        rpDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        rpDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        rpDesc.depthStencilAttachment.initialState = depthInitialState;
        rpDesc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;

        m_GBufferRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_GBufferRenderPass)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "GBuffer互換レンダーパスの作成に失敗");
            return false;
        }

        // フレームバッファ作成（GBufferPassと同じテクスチャを参照）
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_GBufferRenderPass;
        fbDesc.colorTargets.push_back(m_AlbedoTexture);
        fbDesc.colorTargets.push_back(m_NormalTexture);
        fbDesc.colorTargets.push_back(m_MaterialTexture);
        fbDesc.colorTargets.push_back(m_EmissiveTexture);
        fbDesc.depthStencilTarget = m_DepthTexture;
        fbDesc.width = m_CurrentWidth;
        fbDesc.height = m_CurrentHeight;

        m_GBufferFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_GBufferFramebuffer)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "フレームバッファの作成に失敗");
            return false;
        }

        m_bGBufferRenderPassUsesRenderGraphAttachmentStates = bUseRenderGraphAttachmentStates;

        if (!bRequireDrawPipeline)
        {
            m_DrawPipeline.reset();
            m_DrawWireframePipeline.reset();
            return true;
        }

        if (!m_DrawVertexShader || !m_DrawFragmentShader)
        {
            m_DrawPipeline.reset();
            m_DrawWireframePipeline.reset();
            return false;
        }

        m_DrawPipeline.reset();
        m_DrawWireframePipeline.reset();

        if (!CreateDrawPipelineVariant(RHI::PolygonMode::Fill, m_DrawPipeline))
        {
            return false;
        }

#if NORVES_BUILD_DEVELOPMENT
        if (!CreateDrawPipelineVariant(RHI::PolygonMode::Line, m_DrawWireframePipeline))
        {
            return false;
        }
#endif

        return true;
    }

    bool MegaGeometryPass::CreateDrawPipelineVariant(RHI::PolygonMode polygonMode, RHI::PipelinePtr &outPipeline)
    {
        if (!m_Device || !m_GBufferRenderPass || !m_DrawVertexShader || !m_DrawFragmentShader)
        {
            return false;
        }

        // グラフィックスパイプライン作成
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_DrawVertexShader;
        pipelineDesc.pixelShader = m_DrawFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力レイアウト（Mesh3DVertex互換）
        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(Mesh3DVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);

        // Position: location=0, vec3
        RHI::VertexAttributeDesc posAttr;
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = RHI::Format::R32G32B32_FLOAT;
        posAttr.offset = 0;
        pipelineDesc.vertexAttributes.push_back(posAttr);

        // Normal: location=1, vec3
        RHI::VertexAttributeDesc normalAttr;
        normalAttr.location = 1;
        normalAttr.binding = 0;
        normalAttr.format = RHI::Format::R32G32B32_FLOAT;
        normalAttr.offset = sizeof(float) * 3;
        pipelineDesc.vertexAttributes.push_back(normalAttr);

        // TexCoord: location=2, vec2
        RHI::VertexAttributeDesc texCoordAttr;
        texCoordAttr.location = 2;
        texCoordAttr.binding = 0;
        texCoordAttr.format = RHI::Format::R32G32_FLOAT;
        texCoordAttr.offset = sizeof(float) * 6;
        pipelineDesc.vertexAttributes.push_back(texCoordAttr);

        // ラスタライザ
        pipelineDesc.rasterState.polygonMode = polygonMode;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        // デプステスト有効
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;

        // MRT用ブレンドステート（4カラーアタッチメント分）
        for (int i = 0; i < 4; ++i)
        {
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
        }

        pipelineDesc.renderPass = m_GBufferRenderPass;

        // ディスクリプタセットレイアウト（GBufferPassと同一: set=0, binding 0=UBO, 1-6=textures）
        RHI::DescriptorSetDesc dsDesc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex;
        dsDesc.bindings.push_back(uboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(texBinding);
        }

        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        outPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!outPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "グラフィックスパイプラインの作成に失敗");
            return false;
        }

        return true;
    }

    RHI::PipelinePtr MegaGeometryPass::SelectDrawPipeline(DebugViewMode mode) const
    {
#if NORVES_BUILD_DEVELOPMENT
        if (mode == DebugViewMode::Wireframe && m_DrawWireframePipeline)
        {
            return m_DrawWireframePipeline;
        }
#endif

        return m_DrawPipeline;
    }

    bool MegaGeometryPass::EnsurePerInstanceBindings(uint32_t requiredCount)
    {
        if (!m_Device)
        {
            return false;
        }

        if (!m_IndirectDrawBuffer || !m_DrawCountBuffer)
        {
            return false;
        }

        if (m_InstanceIndirectDrawBuffers.empty())
        {
            m_InstanceIndirectDrawBuffers.push_back(m_IndirectDrawBuffer);
            m_InstanceDrawCountBuffers.push_back(m_DrawCountBuffer);
        }

        while (m_InstanceIndirectDrawBuffers.size() < requiredCount)
        {
            const uint64_t indirectSize =
                static_cast<uint64_t>(m_Settings.MaxDrawCount) *
                sizeof(MegaGeometry::DrawIndexedIndirectCommand);
            RHI::BufferDesc indirectDesc(
                indirectSize,
                RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
                false,
                "MegaGeometry_IndirectDraw_Instance");
            auto indirectDrawBuffer = m_Device->CreateBuffer(indirectDesc);

            RHI::BufferDesc countDesc(
                sizeof(uint32_t),
                RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
                false,
                "MegaGeometry_DrawCount_Instance");
            auto drawCountBuffer = m_Device->CreateBuffer(countDesc);

            if (!indirectDrawBuffer || !drawCountBuffer)
            {
                return false;
            }

            m_InstanceIndirectDrawBuffers.push_back(indirectDrawBuffer);
            m_InstanceDrawCountBuffers.push_back(drawCountBuffer);
        }

        RHI::DescriptorSetDesc cullDsDesc;
        RHI::DescriptorBinding cullUboBinding;
        cullUboBinding.binding = 0;
        cullUboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        cullUboBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(cullUboBinding);

        RHI::DescriptorBinding clusterBinding;
        clusterBinding.binding = 1;
        clusterBinding.type = RHI::ResourceBindType::StructuredBuffer;
        clusterBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(clusterBinding);

        RHI::DescriptorBinding indirectBinding;
        indirectBinding.binding = 2;
        indirectBinding.type = RHI::ResourceBindType::RWBuffer;
        indirectBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(indirectBinding);

        RHI::DescriptorBinding countBinding;
        countBinding.binding = 3;
        countBinding.type = RHI::ResourceBindType::RWBuffer;
        countBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(countBinding);

        RHI::DescriptorBinding hiZBinding;
        hiZBinding.binding = 4;
        hiZBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        hiZBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(hiZBinding);

        RHI::DescriptorSetDesc drawDsDesc;
        RHI::DescriptorBinding drawUboBinding;
        drawUboBinding.binding = 0;
        drawUboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        drawUboBinding.stages = RHI::ShaderStage::Vertex;
        drawDsDesc.bindings.push_back(drawUboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            drawDsDesc.bindings.push_back(texBinding);
        }

        while (m_CullUniformBuffers.size() < requiredCount)
        {
            RHI::BufferDesc cullUboDesc(
                sizeof(CullUniformData),
                RHI::ResourceUsage::ConstantBuffer,
                true,
                "MegaGeometry_CullUBO");
            auto cullUniformBuffer = m_Device->CreateBuffer(cullUboDesc);
            auto cullDescriptorSet = m_Device->CreateDescriptorSet(cullDsDesc);
            if (!cullUniformBuffer || !cullDescriptorSet)
            {
                return false;
            }

            constexpr uint32_t PER_OBJECT_UBO_SIZE = 256;
            RHI::BufferDesc drawUboDesc(
                PER_OBJECT_UBO_SIZE,
                RHI::ResourceUsage::ConstantBuffer,
                true,
                "MegaGeometry_DrawUBO");
            auto drawUniformBuffer = m_Device->CreateBuffer(drawUboDesc);
            auto drawDescriptorSet = m_Device->CreateDescriptorSet(drawDsDesc);
            if (!drawUniformBuffer || !drawDescriptorSet)
            {
                return false;
            }

            m_CullUniformBuffers.push_back(cullUniformBuffer);
            m_CullDescriptorSets.push_back(cullDescriptorSet);
            m_DrawUniformBuffers.push_back(drawUniformBuffer);
            m_DrawDescriptorSets.push_back(drawDescriptorSet);
        }

        return true;
    }

    // ========================================
    // Hi-Z 深度ピラミッドリソース作成
    // ========================================

    bool MegaGeometryPass::CreateHiZResources(ViewRenderContext &context)
    {
        if (!m_Device || !m_HiZPipeline || m_CurrentWidth == 0 || m_CurrentHeight == 0)
        {
            return false;
        }

        // 既存リソースをリセット
        m_HiZTexture.reset();
        m_HiZDescriptorSet.reset();

        // Hi-Zテクスチャ: 半解像度ベース、ミップチェーン付き
        uint32_t hiZWidth = (m_CurrentWidth + 1) / 2;
        uint32_t hiZHeight = (m_CurrentHeight + 1) / 2;
        uint32_t maxDim = (hiZWidth > hiZHeight) ? hiZWidth : hiZHeight;
        m_HiZMipCount = static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDim)))) + 1;

        RHI::TextureDesc hiZTexDesc;
        hiZTexDesc.Width = hiZWidth;
        hiZTexDesc.Height = hiZHeight;
        hiZTexDesc.MipLevels = m_HiZMipCount;
        hiZTexDesc.TextureFormat = RHI::Format::R32_FLOAT;
        hiZTexDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
        hiZTexDesc.DebugName = "MegaGeometry_HiZPyramid";

        m_HiZTexture = m_Device->CreateTexture(hiZTexDesc);
        if (!m_HiZTexture)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "Hi-Zテクスチャの作成に失敗 (%ux%u, %u mips)", hiZWidth, hiZHeight, m_HiZMipCount);
            m_HiZMipCount = 0;
            return false;
        }

        // Hi-Z生成用ディスクリプタセット
        // binding 0: ソースミップ (CombinedImageSampler)
        // binding 1: デストミップ (RWTexture / StorageImage)
        // binding 2: HiZParams UBO (ConstantBuffer)
        RHI::DescriptorSetDesc hiZDsDesc;

        RHI::DescriptorBinding srcBinding;
        srcBinding.binding = 0;
        srcBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        srcBinding.stages = RHI::ShaderStage::Compute;
        hiZDsDesc.bindings.push_back(srcBinding);

        RHI::DescriptorBinding dstBinding;
        dstBinding.binding = 1;
        dstBinding.type = RHI::ResourceBindType::RWTexture;
        dstBinding.stages = RHI::ShaderStage::Compute;
        hiZDsDesc.bindings.push_back(dstBinding);

        RHI::DescriptorBinding paramBinding;
        paramBinding.binding = 2;
        paramBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramBinding.stages = RHI::ShaderStage::Compute;
        hiZDsDesc.bindings.push_back(paramBinding);

        m_HiZDescriptorSet = m_Device->CreateDescriptorSet(hiZDsDesc);
        if (!m_HiZDescriptorSet)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "Hi-Zディスクリプタセットの作成に失敗");
            m_HiZTexture.reset();
            m_HiZMipCount = 0;
            return false;
        }

        NORVES_LOG_INFO("MegaGeometryPass", "Hi-Z深度ピラミッド作成 (%ux%u, %u mips)", hiZWidth, hiZHeight, m_HiZMipCount);
        return true;
    }

    // ========================================
    // Hi-Z 深度ピラミッド生成
    // ========================================

    void MegaGeometryPass::GenerateHiZPyramid(RHI::ICommandList *cmdList)
    {
        if (!m_HiZTexture || !m_HiZPipeline || !m_HiZDescriptorSet || m_HiZMipCount == 0)
        {
            return;
        }

        // GBuffer深度テクスチャをシェーダーリソースとして使用（既にShaderResource状態のはず）
        // Hi-Zテクスチャ全ミップをUAV状態に遷移
        cmdList->TextureBarrier(m_HiZTexture,
                                RHI::ResourceState::Undefined,
                                RHI::ResourceState::UnorderedAccess,
                                0, 0, m_HiZMipCount, 0);

        cmdList->SetPipeline(m_HiZPipeline);

        uint32_t srcWidth = m_CurrentWidth;
        uint32_t srcHeight = m_CurrentHeight;

        for (uint32_t mip = 0; mip < m_HiZMipCount; ++mip)
        {
            uint32_t destWidth = (m_HiZTexture->GetWidth() >> mip);
            uint32_t destHeight = (m_HiZTexture->GetHeight() >> mip);
            if (destWidth < 1) destWidth = 1;
            if (destHeight < 1) destHeight = 1;

            // HiZParams UBO更新
            int32_t params[4] = {
                static_cast<int32_t>(destWidth),
                static_cast<int32_t>(destHeight),
                0, 0
            };
            m_HiZParamsBuffer->Update(params, 16);

            // ソースバインド
            if (mip == 0)
            {
                // 初回: GBuffer深度テクスチャを読み取り
                m_HiZDescriptorSet->BindTexture(0, m_DepthTexture);
                m_HiZDescriptorSet->BindSampler(0, m_HiZNearestSampler);
            }
            else
            {
                // 前のミップをシェーダーリソースに遷移
                cmdList->TextureBarrier(m_HiZTexture,
                                        RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::ShaderResource,
                                        mip - 1, 0, 1, 0);

                // 前のミップをソースとしてバインド
                m_HiZDescriptorSet->BindTexture(0, m_HiZTexture);
                m_HiZDescriptorSet->BindSampler(0, m_HiZNearestSampler);
            }

            // デストミップをストレージテクスチャとしてバインド
            m_HiZDescriptorSet->BindStorageTexture(1, m_HiZTexture, mip);

            // パラメータUBO
            m_HiZDescriptorSet->BindConstantBuffer(2, m_HiZParamsBuffer, 0, 16);
            m_HiZDescriptorSet->Update();
            cmdList->SetDescriptorSet(m_HiZDescriptorSet, 0);

            // ディスパッチ（8x8ワークグループ）
            uint32_t groupX = (destWidth + 7) / 8;
            uint32_t groupY = (destHeight + 7) / 8;
            cmdList->Dispatch(groupX, groupY, 1);

            srcWidth = destWidth;
            srcHeight = destHeight;
        }

        // 最後のミップをシェーダーリソースに遷移
        cmdList->TextureBarrier(m_HiZTexture,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource,
                                m_HiZMipCount - 1, 0, 1, 0);
    }

} // namespace NorvesLib::Core::Rendering
