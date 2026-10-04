#include "Rendering/MegaGeometryPass.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/SparseResidencyShading.h"
#include "Rendering/VirtualTextureFeedbackMaterial.h"
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
#include "Rendering/MegaGeometry/MegaGeometryLODSelection.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // VT の要求のバッファの binding（megageometry.frag の VT_FEEDBACK_BINDING と同じ。材質のテクスチャ 1-6 の次）
        constexpr uint32_t VirtualTextureFeedbackBindingIndex = 7;
    } // namespace

    using namespace Container;

    namespace
    {
        constexpr uint32_t DEBUG_PAYLOAD_MODE_NONE = 0u;
        constexpr uint32_t DEBUG_PAYLOAD_MODE_CLUSTER_INDEX = 1u;
        constexpr uint32_t DEBUG_PAYLOAD_MODE_LOD_LEVEL = 2u;

        // cluster_cull.comp の cullPass（CULL_PASS_* と同じ値）
        constexpr uint32_t CULL_PASS_SINGLE = 0u; // 従来の1回の判定（遮蔽の判定なし）
        constexpr uint32_t CULL_PASS_FIRST = 1u;  // 1パス目: 前のフレームで見えたクラスタだけ
        constexpr uint32_t CULL_PASS_SECOND = 2u; // 2パス目: HZB で判定して残りを描き、見えたビットを更新

        // 統計（cluster_cull.comp の stats[4]: pass1・pass2_tested・pass2_drawn・occluded）のバイト数
        constexpr uint32_t StatsBufferBytes = 4u * sizeof(uint32_t);

        // 使われなくなった「見えた」ビットのバッファを手放すまでのフレーム数（GPUはこれより前に使い終わっている）
        constexpr uint64_t VisibilityStaleFrames = 64;

        bool IsMegaGeometryDebugPayloadMode(DebugViewMode mode)
        {
            return mode == DebugViewMode::MegaGeometryClusters ||
                   mode == DebugViewMode::LODLevel;
        }

        uint32_t GetMegaGeometryDebugPayloadMode(DebugViewMode mode, bool bSupported)
        {
            if (!bSupported)
            {
                return DEBUG_PAYLOAD_MODE_NONE;
            }

            if (mode == DebugViewMode::MegaGeometryClusters)
            {
                return DEBUG_PAYLOAD_MODE_CLUSTER_INDEX;
            }

            if (mode == DebugViewMode::LODLevel)
            {
                return DEBUG_PAYLOAD_MODE_LOD_LEVEL;
            }

            return DEBUG_PAYLOAD_MODE_NONE;
        }
    } // namespace

    // ========================================
    // コンストラクタ / デストラクタ
    // ========================================

    MegaGeometryPass::MegaGeometryPass(const MegaGeometryPassSettings &settings)
        : m_Settings(settings)
    {
        // 撮り比べ用に、LODの段を選ぶ誤差の閾値（画素）を環境変数で替えられるようにする。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *value = std::getenv("NORVES_MEGA_LOD_ERROR_PX");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (value != nullptr && value[0] != '\0')
        {
            char *end = nullptr;
            const float threshold = std::strtof(value, &end);
            if (end != value && *end == '\0' && std::isfinite(threshold) && threshold > 0.0f)
            {
                m_Settings.LODBias = threshold;
            }
        }
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

            RHI::DescriptorBinding visibilityBinding;
            visibilityBinding.binding = 5;
            visibilityBinding.type = RHI::ResourceBindType::RWBuffer;
            visibilityBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(visibilityBinding);

            RHI::DescriptorBinding statsBinding;
            statsBinding.binding = 6;
            statsBinding.type = RHI::ResourceBindType::RWBuffer;
            statsBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(statsBinding);

            cullPipelineDesc.descriptorSetLayouts.push_back(cullDsDesc);
        }
        m_CullPipeline = m_Device->CreateComputePipeline(cullPipelineDesc);
        if (!m_CullPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリングパイプラインの作成に失敗");
            return false;
        }

        // 遮蔽カリング（2パス）の HZB。作れなければ従来の1回の判定で描く
        m_bHiZReady = m_HiZ.Initialize(m_Device, context.ShaderMgr);

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
        m_SecondInstanceIndirectDrawBuffers.clear();
        m_SecondInstanceDrawCountBuffers.clear();
        m_SecondCullUniformBuffers.clear();
        m_SecondCullDescriptorSets.clear();
        m_DummyVisibilityBuffer.reset();
        m_DummyStatsBuffer.reset();
        m_InstanceVisibilities.clear();
        m_RetiredBuffers.clear();
        m_OcclusionFrameCount = 0;
        for (StatsSlot &slot : m_StatsSlots)
        {
            if (slot.Buffer && slot.Mapped)
            {
                slot.Buffer->Unmap();
            }
            slot = StatsSlot{};
        }
        m_bStatsSlotsTried = false;
        m_bStatsLoggedOnce = false;
        m_bOcclusionFallbackLogged = false;
        m_HiZ.Shutdown();
        m_bHiZReady = false;

        m_DrawPipeline.reset();
        m_DrawWireframePipeline.reset();
        m_DrawVertexShader.reset();
        m_DrawFragmentShader.reset();
        m_DrawUniformBuffers.clear();
        m_DrawDescriptorSets.clear();

        m_GBufferRenderPass.reset();
        m_GBufferFramebuffer.reset();
        m_SecondGBufferRenderPass.reset();
        m_SecondGBufferFramebuffer.reset();
        m_AlbedoTexture.reset();
        m_NormalTexture.reset();
        m_MaterialTexture.reset();
        m_EmissiveTexture.reset();
        m_DepthTexture.reset();
        m_VelocityTexture.reset();
        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferEmissiveHandle = {};
        m_GBufferDepthHandle = {};
        m_GBufferVelocityHandle = {};

        m_DefaultWhiteTexture.reset();
        m_DefaultFlatNormalTexture.reset();
        m_DefaultBlackTexture.reset();
        m_DefaultLinearSampler.reset();

        m_Instances.clear();
        m_bPreferRenderGraphGBufferResources = false;
        m_bGBufferRenderPassUsesRenderGraphAttachmentStates = false;
        m_bMegaGeometryDebugPayloadUnsupportedWarned = false;

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
                instance.ObjectId = proxy.ObjectId;
                instance.Handle = proxy.MegaMeshHandle;
                std::memcpy(instance.WorldMatrix, &proxy.WorldTransform, sizeof(float) * 16);
                std::memcpy(instance.PreviousWorldMatrix, &proxy.PreviousWorldTransform, sizeof(float) * 16);
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
            m_VelocityTexture.reset();
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
            if (!m_VelocityTexture)
            {
                m_VelocityTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Velocity"));
            }
        }

        // GBufferテクスチャが利用可能か確認
        if (!m_AlbedoTexture ||
            !m_NormalTexture ||
            !m_MaterialTexture ||
            !m_EmissiveTexture ||
            !m_DepthTexture ||
            !m_VelocityTexture)
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
                m_GBufferFramebuffer->GetColorAttachment(4).get() != m_VelocityTexture.get() ||
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
        m_GBufferVelocityHandle = {};

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
        RGTextureHandle velocityProbe;
        const bool bHasAllNamedGBufferAttachments =
            builder.TryGetTexture(RenderGraphResourceNames::GBufferAlbedo, albedoProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normalProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferMaterial, materialProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferEmissive, emissiveProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, depthProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferVelocity, velocityProbe);

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

            // velocity: MegaGeometry の画素の動き（GBufferPass と同じ currentUV - previousUV）を書く。
            RGTextureHandle velocityHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferVelocity,
                                                    velocityHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferVelocityHandle = velocityHandle.ToResourceHandle();
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
        RHI::TexturePtr graphVelocityTexture = m_GBufferVelocityHandle.IsValid()
                                                   ? resources.GetTexture(m_GBufferVelocityHandle)
                                                   : nullptr;

        const bool bHasAllRenderGraphGBufferResources =
            graphAlbedoTexture &&
            graphNormalTexture &&
            graphMaterialTexture &&
            graphEmissiveTexture &&
            graphDepthTexture &&
            graphVelocityTexture;

        m_bPreferRenderGraphGBufferResources = bHasAllRenderGraphGBufferResources;
        if (m_bPreferRenderGraphGBufferResources)
        {
            m_AlbedoTexture = graphAlbedoTexture;
            m_NormalTexture = graphNormalTexture;
            m_MaterialTexture = graphMaterialTexture;
            m_EmissiveTexture = graphEmissiveTexture;
            m_DepthTexture = graphDepthTexture;
            m_VelocityTexture = graphVelocityTexture;
        }
        else
        {
            m_AlbedoTexture.reset();
            m_NormalTexture.reset();
            m_MaterialTexture.reset();
            m_EmissiveTexture.reset();
            m_DepthTexture.reset();
            m_VelocityTexture.reset();
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
        // velocity は前のカメラが無ければ0（GBufferPass と同じ）。
        const CameraViewConstants previousCameraConstants = CameraViewConstants::BuildForDevice(
            command.bHasPreviousCamera ? command.PreviousCamera : cam, aspectRatio, m_Device);
        const auto &caps = m_Device->GetCapabilities();
        const bool bMegaGeometryDebugPayloadRequested =
            IsMegaGeometryDebugPayloadMode(command.DebugMode);
        const bool bMegaGeometryDebugPayloadSupported =
            bMegaGeometryDebugPayloadRequested &&
            caps.bDrawIndirectFirstInstance;
        // デバッグ表示でなければ、描いているクラスタのLODの段を渡す（変位した材質が段の頂点の間隔を知るため）。
        const bool bLODLevelPayload = !bMegaGeometryDebugPayloadRequested && caps.bDrawIndirectFirstInstance;
        const uint32_t debugPayloadMode =
            bLODLevelPayload ? DEBUG_PAYLOAD_MODE_LOD_LEVEL
                             : GetMegaGeometryDebugPayloadMode(command.DebugMode, bMegaGeometryDebugPayloadSupported);

        if (bMegaGeometryDebugPayloadRequested &&
            !caps.bDrawIndirectFirstInstance &&
            !m_bMegaGeometryDebugPayloadUnsupportedWarned)
        {
            NORVES_LOG_WARNING("MegaGeometryPass",
                               "%s debug view requires DrawIndirectFirstInstance; using normal MegaGeometry shading",
                               DebugViewModeToString(command.DebugMode));
            m_bMegaGeometryDebugPayloadUnsupportedWarned = true;
        }

        const ClipSpaceFrustumPlanes frustumPlanes =
            MatrixUtils::ExtractClipSpaceFrustumPlanes(cameraConstants.ViewProjectionMatrix,
                                                       ClipSpaceDepthRange::ZeroToOne);

        // ========================================
        // 遮蔽カリングの経路を決める
        // ========================================
        // 2パス: 1パス目（前のフレームで見えたクラスタ）→ HZB（その時点の深度から）→ 2パス目（残りを遮蔽の判定で選ぶ）。
        // 使えないとき（--mega-occlusion=off・HZBが作れない・描く範囲が深度と一致しない）は、
        // 従来の1回の判定（遮蔽の判定なし）で描く。
        const bool bTwoPass = CanUseTwoPassOcclusion(command);
        if (bTwoPass)
        {
            ++m_OcclusionFrameCount;
        }

        // 統計（MEGA_OCCLUSION）の書き込み先。読み戻しのあるビルド（開発）だけ取る
        StatsSlot *statsSlot = nullptr;
#if NORVES_ENABLE_STATS
        if (bTwoPass)
        {
            EnsureStatsSlots();
            StatsSlot &slot = m_StatsSlots[m_OcclusionFrameCount % StatsSlotCount];
            if (slot.Buffer && slot.Mapped)
            {
                if (slot.bPending)
                {
                    // StatsSlotCount フレーム前の統計。そのフレームの提出は完了している（フレームの飛行数は2以下）
                    if (!m_bStatsLoggedOnce || slot.Frame % 30 == 0)
                    {
                        NORVES_LOG_INFO("MegaGeometryPass",
                                        "MEGA_OCCLUSION pass1=%u pass2_tested=%u pass2_drawn=%u occluded=%u",
                                        slot.Mapped[0],
                                        slot.Mapped[1],
                                        slot.Mapped[2],
                                        slot.Mapped[3]);
                        m_bStatsLoggedOnce = true;
                    }
                    slot.bPending = false;
                }
                statsSlot = &slot;
            }
        }
#endif

        struct DrawableInstance
        {
            size_t InstanceIndex = 0;
            const MegaGeometry::MegaMeshGPUData *GpuData = nullptr;
            // 1パス目（遮蔽の判定なしの従来の経路ではその1回）のIndirectDrawバッファ
            RHI::BufferPtr IndirectDrawBuffer;
            RHI::BufferPtr DrawCountBuffer;
            // 2パス目のIndirectDrawバッファ
            RHI::BufferPtr SecondIndirectDrawBuffer;
            RHI::BufferPtr SecondDrawCountBuffer;
            RHI::DescriptorSetPtr DrawDescriptorSet;
            // 「前のフレームで見えた」ビット（2パスのときだけ）
            RHI::BufferPtr VisibilityBuffer;
            bool bClearVisibility = false;
        };

        VariableArray<DrawableInstance> drawableInstances;
        drawableInstances.reserve(m_Instances.size());

        // ========================================
        // 各MegaMeshインスタンスの描画入力（PerObject UBO・テクスチャ）を準備
        // ========================================
        for (size_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
        {
            const auto &instance = m_Instances[instanceIndex];
            const auto *gpuData = command.MegaGeometry->GetMegaMeshGPUData(instance.Handle);
            if (!gpuData || gpuData->ClusterCount == 0)
            {
                continue;
            }

            auto drawUniformBuffer = m_DrawUniformBuffers[instanceIndex];
            auto drawDescriptorSet = m_DrawDescriptorSets[instanceIndex];
            auto indirectDrawBuffer = m_InstanceIndirectDrawBuffers[instanceIndex];
            auto drawCountBuffer = m_InstanceDrawCountBuffers[instanceIndex];

            if (!m_CullUniformBuffers[instanceIndex] ||
                !m_CullDescriptorSets[instanceIndex] ||
                !drawUniformBuffer ||
                !drawDescriptorSet ||
                !indirectDrawBuffer ||
                !drawCountBuffer ||
                (bTwoPass &&
                 (!m_SecondCullUniformBuffers[instanceIndex] ||
                  !m_SecondCullDescriptorSets[instanceIndex] ||
                  !m_SecondInstanceIndirectDrawBuffers[instanceIndex] ||
                  !m_SecondInstanceDrawCountBuffers[instanceIndex])))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "Invalid per-instance MegaGeometry binding at slot %zu", instanceIndex);
                return;
            }

            DrawableInstance drawableInstance;
            drawableInstance.InstanceIndex = instanceIndex;
            drawableInstance.GpuData = gpuData;
            drawableInstance.IndirectDrawBuffer = indirectDrawBuffer;
            drawableInstance.DrawCountBuffer = drawCountBuffer;
            if (bTwoPass)
            {
                const uint64_t visibilityKey =
                    instance.ObjectId != 0 ? instance.ObjectId : (0x8000000000000000ull | instanceIndex);
                drawableInstance.VisibilityBuffer = AcquireVisibilityBuffer(visibilityKey,
                                                                            instance,
                                                                            *gpuData,
                                                                            drawableInstance.bClearVisibility);
                if (!drawableInstance.VisibilityBuffer)
                {
                    NORVES_LOG_ERROR("MegaGeometryPass", "Failed to create the visibility buffer at slot %zu", instanceIndex);
                    continue;
                }
                drawableInstance.SecondIndirectDrawBuffer = m_SecondInstanceIndirectDrawBuffers[instanceIndex];
                drawableInstance.SecondDrawCountBuffer = m_SecondInstanceDrawCountBuffers[instanceIndex];
            }

            // PerObject UBO更新（ワールド変換行列）
            struct PerObjectUBO
            {
                float World[16];
                float View[16];
                float Projection[16];
                float CameraPosition[4];
                float ObjectColor[4];
                float EmissiveChromaticityAndLuminanceNits[4];
                float PomParams[4];
                float PreviousWorld[16];
                float PreviousView[16];
                float PreviousProjection[16];
                float FrameParams[4]; // x=前のカメラがあるか（1/0）, y=発光に掛けるプリエクスポージャ, z=変位の頂点の間隔（UV）, w=描画の番号がLODの段か（1/0）
                float MaterialParams[4]; // x=ORMの1枚を metallic の枠に張ったか（1/0）, y=法線が2チャンネル（BC5）か（1/0）, z=材質のテクスチャが sparse（VT）か（1/0）, w=VT のフィードバックのパラメータ（アルベド。0 は書かない）
                float VtFeedbackParams[4]; // VT のフィードバックのパラメータ: x=法線, y=ORM（metallic の枠）, z=高さ（0 は書かない）, w=未使用
            };
            static_assert(sizeof(PerObjectUBO) <= 512u);

            PerObjectUBO perObject{};
            std::memcpy(perObject.World, instance.WorldMatrix, sizeof(float) * 16);
            cameraConstants.CopyShaderView(perObject.View);
            cameraConstants.CopyShaderProjection(perObject.Projection);
            cameraConstants.CopyCameraPosition(perObject.CameraPosition);
            std::memcpy(perObject.PreviousWorld, instance.PreviousWorldMatrix, sizeof(float) * 16);
            previousCameraConstants.CopyShaderView(perObject.PreviousView);
            previousCameraConstants.CopyShaderProjection(perObject.PreviousProjection);
            perObject.FrameParams[0] = command.bHasPreviousCamera ? 1.0f : 0.0f;
            // 発光はプリエクスポージャ後の値で GBuffer_Emissive へ書く（GBufferPass・LightingPass と同じ値）。
            perObject.FrameParams[1] = ResolveSceneColorPreExposure(&cam);

            // マテリアル値を設定
            const auto &mat = gpuData->Material;
            perObject.ObjectColor[0] = mat.BaseColor[0];
            perObject.ObjectColor[1] = mat.BaseColor[1];
            perObject.ObjectColor[2] = mat.BaseColor[2];
            perObject.ObjectColor[3] = mat.BaseColor[3];
            perObject.EmissiveChromaticityAndLuminanceNits[0] = mat.EmissiveColor[0];
            perObject.EmissiveChromaticityAndLuminanceNits[1] = mat.EmissiveColor[1];
            perObject.EmissiveChromaticityAndLuminanceNits[2] = mat.EmissiveColor[2];
            perObject.EmissiveChromaticityAndLuminanceNits[3] = mat.EmissiveLuminanceNits;
            perObject.PomParams[0] = mat.HeightScale;
            perObject.PomParams[1] = mat.bHasHeightMap ? 1.0f : 0.0f;
            perObject.PomParams[2] = static_cast<float>(static_cast<uint8_t>(command.DebugMode));
            perObject.PomParams[3] = bMegaGeometryDebugPayloadSupported ? 1.0f : 0.0f;
            perObject.FrameParams[2] = mat.DisplacementUVSpacing > 0.0f ? mat.DisplacementUVSpacing : 0.0f;
            perObject.FrameParams[3] = bLODLevelPayload ? 1.0f : 0.0f;

            // PBRテクスチャ（マテリアルテクスチャまたはデフォルトにフォールバック）
            // 材質はハンドルしか持たないので、描画のたびに引き直す（解放済みならデフォルトに落ちる）。
            auto resolveTexture = [&command](TextureHandle handle, const RHI::TexturePtr &fallback) -> RHI::TexturePtr
            {
                if (command.Textures && handle.IsValid())
                {
                    if (RHI::TexturePtr texture = command.Textures->GetRHITexturePtr(handle))
                    {
                        return texture;
                    }
                }
                return fallback;
            };
            // ORM は metallic の枠に張り、シェーダーへフラグで伝える（descriptor の binding は増やさない）。
            // texture が解決できないときは別々の枠（既定値）の経路へ落とす。
            const RHI::TexturePtr orm = resolveTexture(mat.ORMTexture, nullptr);
            perObject.MaterialParams[0] = orm ? 1.0f : 0.0f;
            perObject.MaterialParams[1] = mat.bNormalTwoChannel ? 1.0f : 0.0f;

            // PBRテクスチャ
            auto albedo = resolveTexture(mat.AlbedoTexture, m_DefaultWhiteTexture);
            auto normal = resolveTexture(mat.NormalTexture, m_DefaultFlatNormalTexture);
            auto metallic = orm ? orm : resolveTexture(mat.MetallicTexture, m_DefaultBlackTexture);
            auto roughness = orm ? orm : resolveTexture(mat.RoughnessTexture, m_DefaultWhiteTexture);
            auto ao = orm ? orm : resolveTexture(mat.AOTexture, m_DefaultWhiteTexture);
            auto height = resolveTexture(mat.HeightTexture, m_DefaultBlackTexture);

            // 張るテクスチャに sparse（VT）が1枚でもあれば、シェーダーは常駐しないタイルを読まず粗いミップへ逃げる。
            perObject.MaterialParams[2] = AnySparseTexture(albedo, normal, metallic, roughness, ao, height) ? 1.0f : 0.0f;
            // VT の要求を書く先（このフレームのバッファ。対応しないデバイスでは null で、シェーダーに binding は入らない）。
            // アルベドが VT のとき、シェーダーがこのフレームの要求を書く（24bit 以下の整数は float に正確に載る）
            const TextureResources::VirtualTextureFeedbackTarget feedbackTarget =
                command.Textures ? command.Textures->GetVirtualTextureFeedbackTarget()
                                 : TextureResources::VirtualTextureFeedbackTarget{};
            perObject.MaterialParams[3] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.AlbedoTexture, albedo.get(), feedbackTarget));
            // 法線・ORM・高さも VT のとき、それぞれの表の番号で要求を書く（ORM の枠は metallic に張ったテクスチャ）
            perObject.VtFeedbackParams[0] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.NormalTexture, normal.get(), feedbackTarget));
            perObject.VtFeedbackParams[1] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.ORMTexture, orm.get(), feedbackTarget));
            perObject.VtFeedbackParams[2] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.HeightTexture, height.get(), feedbackTarget));

            drawUniformBuffer->Update(&perObject, sizeof(PerObjectUBO));

            drawDescriptorSet->BindConstantBuffer(0, drawUniformBuffer, 0,
                                                  static_cast<uint32_t>(sizeof(PerObjectUBO)));

            // PBRテクスチャバインド

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
            BindVirtualTextureFeedback(*drawDescriptorSet, VirtualTextureFeedbackBindingIndex, feedbackTarget);

            drawDescriptorSet->Update();
            drawableInstance.DrawDescriptorSet = drawDescriptorSet;
            drawableInstances.push_back(drawableInstance);
        }

        if (drawableInstances.empty())
        {
            recordEmptyRenderPass();
            return;
        }

        // ========================================
        // クラスタカリングの記録（パスごと）
        // ========================================
        // カリング用ユニフォームの、インスタンスによらない部分
        CullUniformData baseUniform{};
        cameraConstants.CopyShaderView(baseUniform.ViewMatrix);
        cameraConstants.CopyShaderProjection(baseUniform.ProjectionMatrix);
        cameraConstants.CopyCameraPosition(baseUniform.CameraPosition);
        baseUniform.CameraPosition[3] = 0.0f;
        frustumPlanes.CopyToShaderData(baseUniform.FrustumPlanes);
        baseUniform.MaxDrawCount = m_Settings.MaxDrawCount;
        baseUniform.LODBias = m_Settings.LODBias;
        baseUniform.ScreenHeight = static_cast<float>(m_CurrentHeight);
        // projectionFactor = screenHeight / (2 * tan(fov/2))
        const float halfFovTan = std::tan(cameraConstants.FieldOfViewRadians * 0.5f);
        baseUniform.ProjectionFactor = (halfFovTan > 1e-6f)
                                           ? static_cast<float>(m_CurrentHeight) / (2.0f * halfFovTan)
                                           : 1.0f;
        baseUniform.DebugPayloadMode = debugPayloadMode;

        RHI::BufferPtr statsBuffer = m_DummyStatsBuffer;
        if (statsSlot)
        {
            statsBuffer = statsSlot->Buffer;
            // 1フレームぶんの統計を0から数える（前に使ったのはホストが読んだ後）
            cmdList->BufferBarrier(statsBuffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(statsBuffer, 0, StatsBufferBytes, 0);
            cmdList->BufferBarrier(statsBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
            statsSlot->Frame = m_OcclusionFrameCount;
            statsSlot->bPending = true;
        }

        // cullPass: 0=従来の1回の判定、1=1パス目、2=2パス目。hiZTexture は2パス目の遮蔽の判定が読む HZB（null なら判定しない）
        auto recordCull = [&](const DrawableInstance &drawable, uint32_t cullPass, const RHI::TexturePtr &hiZTexture) -> void
        {
            const auto &instance = m_Instances[drawable.InstanceIndex];
            const auto *gpuData = drawable.GpuData;
            const bool bSecond = cullPass == CULL_PASS_SECOND;

            const RHI::BufferPtr &indirectDrawBuffer = bSecond ? drawable.SecondIndirectDrawBuffer : drawable.IndirectDrawBuffer;
            const RHI::BufferPtr &drawCountBuffer = bSecond ? drawable.SecondDrawCountBuffer : drawable.DrawCountBuffer;
            const RHI::BufferPtr &cullUniformBuffer =
                bSecond ? m_SecondCullUniformBuffers[drawable.InstanceIndex] : m_CullUniformBuffers[drawable.InstanceIndex];
            const RHI::DescriptorSetPtr &cullDescriptorSet =
                bSecond ? m_SecondCullDescriptorSets[drawable.InstanceIndex] : m_CullDescriptorSets[drawable.InstanceIndex];

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

            // 「前のフレームで見えた」ビット。1パス目が読む前に、前のフレームの2パス目の書き込みを見せる
            // （作り直した直後は0で埋める）
            if (cullPass == CULL_PASS_FIRST)
            {
                if (drawable.bClearVisibility)
                {
                    cmdList->BufferBarrier(drawable.VisibilityBuffer,
                                           RHI::ResourceState::Common,
                                           RHI::ResourceState::CopyDest);
                    cmdList->FillBuffer(drawable.VisibilityBuffer, 0, drawable.VisibilityBuffer->GetSize(), 0);
                    cmdList->BufferBarrier(drawable.VisibilityBuffer,
                                           RHI::ResourceState::CopyDest,
                                           RHI::ResourceState::UnorderedAccess);
                }
                else
                {
                    cmdList->BufferBarrier(drawable.VisibilityBuffer,
                                           RHI::ResourceState::UnorderedAccess,
                                           RHI::ResourceState::UnorderedAccess);
                }
            }

            // ----------------------------------------
            // 2. カリングユニフォーム更新
            // ----------------------------------------
            CullUniformData uniformData = baseUniform;
            uniformData.TotalClusterCount = gpuData->ClusterCount;
            uniformData.CullPass = cullPass;
            uniformData.bStatsEnabled = (statsSlot && cullPass != CULL_PASS_SINGLE) ? 1u : 0u;

            // 遮蔽の判定（2パス目だけ）。HZB の元の深度の解像度を渡す（HZB のミップ0 はその半分）
            if (bSecond && hiZTexture && m_DepthTexture)
            {
                uniformData.HiZWidth = m_DepthTexture->GetWidth();
                uniformData.HiZHeight = m_DepthTexture->GetHeight();
                uniformData.HiZMipCount = m_HiZ.GetMipCount();
                uniformData.bHiZEnabled = 1;
            }

            std::memcpy(uniformData.WorldMatrix, instance.WorldMatrix, sizeof(float) * 16);
            if (gpuData->LODBounds.IsValid())
            {
                uniformData.LODSphere[0] = gpuData->LODBounds.CenterX;
                uniformData.LODSphere[1] = gpuData->LODBounds.CenterY;
                uniformData.LODSphere[2] = gpuData->LODBounds.CenterZ;
                uniformData.LODSphere[3] = gpuData->LODBounds.Radius;
                if (!bSecond)
                {
                    LogUniformLODSelection(instance, *gpuData, uniformData);
                }
            }
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

            // binding 4 の HZB は2パス目の遮蔽の判定（bHiZEnabled=1）だけが読む
            cullDescriptorSet->BindTexture(4, (bSecond && hiZTexture) ? hiZTexture : m_DefaultBlackTexture);
            cullDescriptorSet->BindSampler(4, m_DefaultLinearSampler);

            // binding 5・6: 「前のフレームで見えた」ビットと統計。従来の経路は触らないので代わりのバッファを結ぶ
            if (drawable.VisibilityBuffer && cullPass != CULL_PASS_SINGLE)
            {
                cullDescriptorSet->BindStorageBuffer(5, drawable.VisibilityBuffer, 0,
                                                     static_cast<uint32_t>(drawable.VisibilityBuffer->GetSize()));
            }
            else
            {
                cullDescriptorSet->BindStorageBuffer(5, m_DummyVisibilityBuffer, 0,
                                                     static_cast<uint32_t>(m_DummyVisibilityBuffer->GetSize()));
            }
            cullDescriptorSet->BindStorageBuffer(6, statsBuffer, 0, StatsBufferBytes);

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
            if (cullPass == CULL_PASS_FIRST)
            {
                // 1パス目の読み取りを、2パス目の書き込みより前に済ませる
                cmdList->BufferBarrier(drawable.VisibilityBuffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::UnorderedAccess);
            }
            if (uniformData.bStatsEnabled != 0)
            {
                // 統計のカウンタへ、次のディスパッチが続けて足す
                cmdList->BufferBarrier(statsBuffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::UnorderedAccess);
            }
        };

        // 描画（GBuffer render pass をパスごとに1回だけ開く）
        auto recordDraws = [&](const RHI::RenderPassPtr &renderPass,
                               const RHI::FramebufferPtr &framebuffer,
                               bool bSecond) -> void
        {
            cmdList->BeginRenderPass(renderPass, framebuffer);
            cmdList->SetViewport(command.Viewport);
            cmdList->SetScissor(command.Scissor);
            cmdList->SetPipeline(SelectDrawPipeline(command.DebugMode));

            for (const DrawableInstance &drawableInstance : drawableInstances)
            {
                const auto *gpuData = drawableInstance.GpuData;
                if (!gpuData)
                {
                    continue;
                }

                const RHI::BufferPtr &indirectDrawBuffer =
                    bSecond ? drawableInstance.SecondIndirectDrawBuffer : drawableInstance.IndirectDrawBuffer;
                const RHI::BufferPtr &drawCountBuffer =
                    bSecond ? drawableInstance.SecondDrawCountBuffer : drawableInstance.DrawCountBuffer;

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
                        indirectDrawBuffer, 0,
                        drawCountBuffer, 0,
                        m_Settings.MaxDrawCount,
                        sizeof(MegaGeometry::DrawIndexedIndirectCommand));
                }
                else
                {
                    cmdList->DrawIndexedIndirect(
                        indirectDrawBuffer, 0,
                        m_Settings.MaxDrawCount,
                        sizeof(MegaGeometry::DrawIndexedIndirectCommand));
                }
            }

            cmdList->EndRenderPass();
        };

        if (!bTwoPass)
        {
            // 従来の経路: 全インスタンスを1回の判定（遮蔽の判定なし）で選び、1回の render pass で描く
            for (const DrawableInstance &drawableInstance : drawableInstances)
            {
                recordCull(drawableInstance, CULL_PASS_SINGLE, nullptr);
            }
            recordDraws(m_GBufferRenderPass, m_GBufferFramebuffer, false);
        }
        else
        {
            // 1パス目: 前のフレームで見えたクラスタだけを描く
            for (const DrawableInstance &drawableInstance : drawableInstances)
            {
                recordCull(drawableInstance, CULL_PASS_FIRST, nullptr);
            }
            recordDraws(m_GBufferRenderPass, m_GBufferFramebuffer, false);

            // GBufferPass の不透明＋1パス目の深度から HZB を作る（深度は1パス目の終わりで ShaderResource）。
            // 作れなかったときは遮蔽の判定をしない（1パス目で描かなかったクラスタを全て描く）
            const bool bHiZBuilt = m_HiZ.Build(cmdList, m_DepthTexture);

            // 2パス目: 判定を通った全クラスタを HZB で判定し、1パス目で描かなかった見えるものを描く
            for (const DrawableInstance &drawableInstance : drawableInstances)
            {
                recordCull(drawableInstance, CULL_PASS_SECOND, bHiZBuilt ? m_HiZ.GetTexture() : RHI::TexturePtr{});
            }
            recordDraws(m_SecondGBufferRenderPass, m_SecondGBufferFramebuffer, true);

            if (statsSlot)
            {
                // 統計のシェーダーの書き込みを、ホストの読み取りへ見せる
                cmdList->BufferBarrier(statsSlot->Buffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::HostRead);
            }
        }

        // IndirectDrawバッファを次のフレーム用に戻す
        for (const DrawableInstance &drawableInstance : drawableInstances)
        {
            cmdList->BufferBarrier(drawableInstance.IndirectDrawBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::Common);
            cmdList->BufferBarrier(drawableInstance.DrawCountBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::Common);
            if (bTwoPass)
            {
                cmdList->BufferBarrier(drawableInstance.SecondIndirectDrawBuffer,
                                       RHI::ResourceState::IndirectArgument,
                                       RHI::ResourceState::Common);
                cmdList->BufferBarrier(drawableInstance.SecondDrawCountBuffer,
                                       RHI::ResourceState::IndirectArgument,
                                       RHI::ResourceState::Common);
            }
        }

        if (bTwoPass)
        {
            ReleaseStaleVisibilityBuffers();
        }
    }

    void MegaGeometryPass::LogUniformLODSelection(const MegaMeshInstance &instance,
                                                  const MegaGeometry::MegaMeshGPUData &gpuData,
                                                  const CullUniformData &uniformData)
    {
        if (gpuData.LevelRanges.empty())
        {
            return;
        }

        // ワールド行列は行ベクトル規約（並進は行3）。半径は最大の軸の伸びで広げる（cluster_cull.comp と同じ）。
        const float *world = instance.WorldMatrix;
        const BoundingSphere &local = gpuData.LODBounds;
        float center[3] = {};
        float maxScaleSquared = 0.0f;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            center[axis] = local.CenterX * world[0 + axis] + local.CenterY * world[4 + axis] +
                           local.CenterZ * world[8 + axis] + world[12 + axis];
            const float lengthSquared = world[axis * 4 + 0] * world[axis * 4 + 0] +
                                        world[axis * 4 + 1] * world[axis * 4 + 1] +
                                        world[axis * 4 + 2] * world[axis * 4 + 2];
            maxScaleSquared = std::max(maxScaleSquared, lengthSquared);
        }
        const float scale = std::sqrt(maxScaleSquared);
        const float dx = center[0] - uniformData.CameraPosition[0];
        const float dy = center[1] - uniformData.CameraPosition[1];
        const float dz = center[2] - uniformData.CameraPosition[2];
        const float centerDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float radius = local.Radius * scale;

        // 中心の深さと画角は、シェーダーへ渡す列優先の行列から cluster_cull.comp と同じく求める
        // （配列の [列 * 4 + 行]。深さはクリップ座標の w を projection[2][3] で割った値）。
        const float *view = uniformData.ViewMatrix;
        const float *projection = uniformData.ProjectionMatrix;
        float viewPosition[4] = {};
        for (uint32_t row = 0; row < 4; ++row)
        {
            viewPosition[row] =
                view[0 * 4 + row] * center[0] + view[1 * 4 + row] * center[1] + view[2 * 4 + row] * center[2] +
                view[3 * 4 + row];
        }
        float clipW = 0.0f;
        for (uint32_t column = 0; column < 4; ++column)
        {
            clipW += projection[column * 4 + 3] * viewPosition[column];
        }
        const float depthScale = std::abs(projection[2 * 4 + 3]);
        const float centerDepth = depthScale > 1.0e-6f ? clipW / depthScale : 0.0f;
        const float tanHalfFovX = 1.0f / std::max(std::abs(projection[0 * 4 + 0]), 1.0e-6f);
        const float tanHalfFovY = 1.0f / std::max(std::abs(projection[1 * 4 + 1]), 1.0e-6f);
        const float errorScale = MegaGeometry::ComputeLODSphereErrorPixelsPerMeter(centerDistance,
                                                                                    centerDepth,
                                                                                    radius,
                                                                                    uniformData.ProjectionFactor,
                                                                                    tanHalfFovX,
                                                                                    tanHalfFovY) *
                                 scale;
        const uint32_t level = MegaGeometry::SelectCoarsestLODWithinError(gpuData.LevelRanges,
                                                                           errorScale,
                                                                           uniformData.LODBias);

        LoggedUniformLOD *logged = nullptr;
        for (LoggedUniformLOD &entry : m_LoggedUniformLODs)
        {
            if (entry.MegaMeshId == instance.Handle.Id)
            {
                logged = &entry;
                break;
            }
        }
        if (logged && logged->Level == level)
        {
            return;
        }
        if (!logged)
        {
            m_LoggedUniformLODs.push_back(LoggedUniformLOD{instance.Handle.Id, level});
        }
        else
        {
            logged->Level = level;
        }

        const MegaGeometry::MegaMeshLevelRange &range = gpuData.LevelRanges[level];
        const bool bHasCoarser = level + 1u < gpuData.LevelRanges.size();
        NORVES_LOG_INFO("MegaGeometryPass",
                        "mega_lod_select mesh=\"%s\" level=%u level_triangles=%u camera_to_lod_center_m=%.3f "
                        "lod_radius_m=%.3f center_depth_m=%.3f perspective_stretch=%.3f threshold_px=%.2f "
                        "level_error_px=%.3f coarser_level_error_px=%.3f",
                        gpuData.DebugName.empty() ? "" : gpuData.DebugName.c_str(),
                        level,
                        range.IndexCount / 3u,
                        static_cast<double>(centerDistance),
                        static_cast<double>(radius),
                        static_cast<double>(centerDepth),
                        static_cast<double>(MegaGeometry::ComputeLODSpherePerspectiveStretch(
                            centerDistance, centerDepth, radius, tanHalfFovX, tanHalfFovY)),
                        static_cast<double>(uniformData.LODBias),
                        static_cast<double>(range.Error * errorScale),
                        bHasCoarser ? static_cast<double>(gpuData.LevelRanges[level + 1u].Error * errorScale) : -1.0);
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

        // 従来の経路が binding 5・6 に結ぶ代わりのバッファ（シェーダーは触らない）
        RHI::BufferDesc dummyVisibilityDesc(
            4u * sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer,
            false,
            "MegaGeometry_DummyVisibility");
        m_DummyVisibilityBuffer = device->CreateBuffer(dummyVisibilityDesc);
        RHI::BufferDesc dummyStatsDesc(
            StatsBufferBytes,
            RHI::ResourceUsage::StorageBuffer,
            false,
            "MegaGeometry_DummyStats");
        m_DummyStatsBuffer = device->CreateBuffer(dummyStatsDesc);
        if (!m_DummyVisibilityBuffer || !m_DummyStatsBuffer)
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

        // Velocity: Load
        RHI::AttachmentDesc velocityAttach;
        velocityAttach.format = m_VelocityTexture ? m_VelocityTexture->GetFormat() : RHI::Format::R16G16_FLOAT;
        velocityAttach.isDepthStencil = false;
        velocityAttach.clear = false;
        velocityAttach.loadOp = RHI::AttachmentLoadOp::Load;
        velocityAttach.storeOp = RHI::AttachmentStoreOp::Store;
        velocityAttach.initialState = colorInitialState;
        velocityAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(velocityAttach);

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
        fbDesc.colorTargets.push_back(m_VelocityTexture);
        fbDesc.depthStencilTarget = m_DepthTexture;
        fbDesc.width = m_CurrentWidth;
        fbDesc.height = m_CurrentHeight;

        m_GBufferFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_GBufferFramebuffer)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "フレームバッファの作成に失敗");
            return false;
        }

        // 2パス目のレンダーパス・フレームバッファ。1パス目の描画の後に続けて開くので、全てのアタッチメントが
        // 1パス目の終わりの状態（ShaderResource）から始まる。作れなければ2パスの遮蔽カリングは使わない。
        m_SecondGBufferRenderPass.reset();
        m_SecondGBufferFramebuffer.reset();
        {
            RHI::RenderPassDesc secondRenderPassDesc = rpDesc;
            for (RHI::AttachmentDesc &attachment : secondRenderPassDesc.colorAttachments)
            {
                attachment.initialState = RHI::ResourceState::ShaderResource;
            }
            secondRenderPassDesc.depthStencilAttachment.initialState = RHI::ResourceState::ShaderResource;

            RHI::RenderPassPtr secondRenderPass = m_Device->CreateRenderPass(secondRenderPassDesc);
            if (secondRenderPass)
            {
                RHI::FramebufferDesc secondFbDesc = fbDesc;
                secondFbDesc.renderPass = secondRenderPass;
                RHI::FramebufferPtr secondFramebuffer = m_Device->CreateFramebuffer(secondFbDesc);
                if (secondFramebuffer)
                {
                    m_SecondGBufferRenderPass = secondRenderPass;
                    m_SecondGBufferFramebuffer = secondFramebuffer;
                }
            }
            if (!m_SecondGBufferRenderPass || !m_SecondGBufferFramebuffer)
            {
                NORVES_LOG_WARNING("MegaGeometryPass", "2パス目のGBuffer互換レンダーパスの作成に失敗。遮蔽カリングは使いません");
            }
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

        // MRT用ブレンドステート（Albedo・Normal・Material・Emissive・Velocity の5カラーアタッチメント分）
        for (int i = 0; i < 5; ++i)
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
        uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(uboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(texBinding);
        }
        if (UsesVirtualTextureFeedbackBinding(m_Device))
        {
            AddVirtualTextureFeedbackBinding(dsDesc, VirtualTextureFeedbackBindingIndex);
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

        // 2パス目のIndirectDrawバッファ（1パス目の描画が読み終わる前に書き換えないよう、別に持つ）
        while (m_SecondInstanceIndirectDrawBuffers.size() < requiredCount)
        {
            RHI::BufferDesc indirectDesc(
                static_cast<uint64_t>(m_Settings.MaxDrawCount) * sizeof(MegaGeometry::DrawIndexedIndirectCommand),
                RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
                false,
                "MegaGeometry_IndirectDraw_SecondPass");
            RHI::BufferDesc countDesc(
                sizeof(uint32_t),
                RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
                false,
                "MegaGeometry_DrawCount_SecondPass");
            auto indirectDrawBuffer = m_Device->CreateBuffer(indirectDesc);
            auto drawCountBuffer = m_Device->CreateBuffer(countDesc);
            if (!indirectDrawBuffer || !drawCountBuffer)
            {
                return false;
            }
            m_SecondInstanceIndirectDrawBuffers.push_back(indirectDrawBuffer);
            m_SecondInstanceDrawCountBuffers.push_back(drawCountBuffer);
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

        RHI::DescriptorBinding visibilityBinding;
        visibilityBinding.binding = 5;
        visibilityBinding.type = RHI::ResourceBindType::RWBuffer;
        visibilityBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(visibilityBinding);

        RHI::DescriptorBinding statsBinding;
        statsBinding.binding = 6;
        statsBinding.type = RHI::ResourceBindType::RWBuffer;
        statsBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(statsBinding);

        RHI::DescriptorSetDesc drawDsDesc;
        RHI::DescriptorBinding drawUboBinding;
        drawUboBinding.binding = 0;
        drawUboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        drawUboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
        drawDsDesc.bindings.push_back(drawUboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            drawDsDesc.bindings.push_back(texBinding);
        }
        if (UsesVirtualTextureFeedbackBinding(m_Device))
        {
            AddVirtualTextureFeedbackBinding(drawDsDesc, VirtualTextureFeedbackBindingIndex);
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

            constexpr uint32_t PER_OBJECT_UBO_SIZE = 512;
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

            // 2パス目のカリング用UBO・DescriptorSet（1パス目と同じ並びで、パスごとに別の値を持つ）
            RHI::BufferDesc secondCullUboDesc(
                sizeof(CullUniformData),
                RHI::ResourceUsage::ConstantBuffer,
                true,
                "MegaGeometry_CullUBO_SecondPass");
            auto secondCullUniformBuffer = m_Device->CreateBuffer(secondCullUboDesc);
            auto secondCullDescriptorSet = m_Device->CreateDescriptorSet(cullDsDesc);
            if (!secondCullUniformBuffer || !secondCullDescriptorSet)
            {
                return false;
            }

            m_CullUniformBuffers.push_back(cullUniformBuffer);
            m_CullDescriptorSets.push_back(cullDescriptorSet);
            m_SecondCullUniformBuffers.push_back(secondCullUniformBuffer);
            m_SecondCullDescriptorSets.push_back(secondCullDescriptorSet);
            m_DrawUniformBuffers.push_back(drawUniformBuffer);
            m_DrawDescriptorSets.push_back(drawDescriptorSet);
        }

        return true;
    }

    bool MegaGeometryPass::CanUseTwoPassOcclusion(const MegaGeometryPassCommand &command)
    {
        auto logFallbackOnce = [this](const char *reason) -> void
        {
            if (!m_bOcclusionFallbackLogged)
            {
                NORVES_LOG_INFO("MegaGeometryPass", "MEGA_OCCLUSION_OFF reason=%s", reason);
                m_bOcclusionFallbackLogged = true;
            }
        };

        if (!command.MegaGeometry || !command.MegaGeometry->IsOcclusionCullingEnabled())
        {
            logFallbackOnce("disabled");
            return false;
        }
        if (!m_bHiZReady || !m_DepthTexture || !m_SecondGBufferRenderPass || !m_SecondGBufferFramebuffer ||
            !m_DummyVisibilityBuffer || !m_DummyStatsBuffer)
        {
            logFallbackOnce("unavailable");
            return false;
        }

        // 遮蔽の判定は深度の全体を画面（UV の 0〜1）と見るので、描く範囲が深度の全体と一致するときだけ使う
        const uint32_t depthWidth = m_DepthTexture->GetWidth();
        const uint32_t depthHeight = m_DepthTexture->GetHeight();
        const RHI::Viewport &viewport = command.Viewport;
        if (viewport.x != 0.0f ||
            std::fabs(viewport.width - static_cast<float>(depthWidth)) > 0.5f ||
            std::fabs(std::fabs(viewport.height) - static_cast<float>(depthHeight)) > 0.5f)
        {
            logFallbackOnce("viewport");
            return false;
        }

        if (!m_HiZ.Resize(depthWidth, depthHeight))
        {
            logFallbackOnce("hiz");
            return false;
        }
        return true;
    }

    RHI::BufferPtr MegaGeometryPass::AcquireVisibilityBuffer(uint64_t key,
                                                             const MegaMeshInstance &instance,
                                                             const MegaGeometry::MegaMeshGPUData &gpuData,
                                                             bool &outNeedsClear)
    {
        outNeedsClear = false;

        InstanceVisibility *found = nullptr;
        for (InstanceVisibility &entry : m_InstanceVisibilities)
        {
            if (entry.Key == key)
            {
                found = &entry;
                break;
            }
        }

        const void *clusterBufferIdentity = gpuData.ClusterBuffer.get();
        if (found &&
            found->Buffer &&
            found->MeshId == instance.Handle.Id &&
            found->ClusterBufferIdentity == clusterBufferIdentity &&
            found->ClusterCount == gpuData.ClusterCount)
        {
            found->LastUsedFrame = m_OcclusionFrameCount;
            return found->Buffer;
        }

        // 追加されたインスタンス、またはメッシュの差し替え: ビットを捨てて0から始める。
        // 古いバッファは、GPUが使い終わるまで保持してから破棄する。
        if (!found)
        {
            m_InstanceVisibilities.push_back(InstanceVisibility{});
            found = &m_InstanceVisibilities.back();
            found->Key = key;
        }
        else if (found->Buffer)
        {
            m_RetiredBuffers.push_back(RetiredBuffer{found->Buffer, m_OcclusionFrameCount});
            found->Buffer.reset();
        }

        RHI::BufferDesc desc(static_cast<uint64_t>(gpuData.ClusterCount) * sizeof(uint32_t),
                             RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                             false,
                             "MegaGeometry_VisibleLastFrame");
        found->Buffer = m_Device->CreateBuffer(desc);
        found->MeshId = instance.Handle.Id;
        found->ClusterBufferIdentity = clusterBufferIdentity;
        found->ClusterCount = gpuData.ClusterCount;
        found->LastUsedFrame = m_OcclusionFrameCount;
        outNeedsClear = true;
        return found->Buffer;
    }

    void MegaGeometryPass::ReleaseStaleVisibilityBuffers()
    {
        // 一定のフレーム使われなかったインスタンスのバッファは、GPUがとうに使い終わっているので手放す
        for (size_t index = 0; index < m_InstanceVisibilities.size();)
        {
            InstanceVisibility &entry = m_InstanceVisibilities[index];
            if (m_OcclusionFrameCount - entry.LastUsedFrame > VisibilityStaleFrames)
            {
                m_InstanceVisibilities[index] = m_InstanceVisibilities.back();
                m_InstanceVisibilities.pop_back();
                continue;
            }
            ++index;
        }

        for (size_t index = 0; index < m_RetiredBuffers.size();)
        {
            if (m_OcclusionFrameCount - m_RetiredBuffers[index].RetiredFrame > VisibilityStaleFrames)
            {
                m_RetiredBuffers[index] = m_RetiredBuffers.back();
                m_RetiredBuffers.pop_back();
                continue;
            }
            ++index;
        }
    }

    void MegaGeometryPass::EnsureStatsSlots()
    {
        if (m_bStatsSlotsTried || !m_Device)
        {
            return;
        }
        m_bStatsSlotsTried = true;

        // シェーダーが storage buffer として直接数え、ホストが数フレーム後に読む（host-visible・host-coherent）。
        // 作れない・写像できないデバイスでは、統計だけ取らない。
        for (StatsSlot &slot : m_StatsSlots)
        {
            RHI::BufferDesc desc(StatsBufferBytes,
                                 RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                                 true,
                                 "MegaGeometry_OcclusionStats");
            slot.Buffer = m_Device->CreateBuffer(desc);
            if (slot.Buffer)
            {
                slot.Mapped = static_cast<const uint32_t *>(slot.Buffer->Map(0, 0));
            }
        }
    }

} // namespace NorvesLib::Core::Rendering
