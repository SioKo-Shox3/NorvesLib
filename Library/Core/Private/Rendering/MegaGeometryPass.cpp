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
#include "Debug/Stats.h"
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
#include <algorithm>
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

        // グループの BVH のたどり（cluster_bvh_cull.comp）。BvhStageClusters は葉のクラスタの判定の段の印、
        // BvhLeafSlots は葉の列の1要素を受け持つスレッドの数（葉が持つクラスタの最大数）、
        // BvhCounterCount はカウンタの数（添え字 s は段 s の入力の数、BvhLeafCounter は葉の列の数）
        constexpr uint32_t BvhStageClusters = 0xFFFFFFFFu;
        constexpr uint32_t BvhLeafSlots = MegaGeometry::GROUP_BVH_MAX_LEAF_CLUSTERS;
        constexpr uint32_t BvhCounterCount = 32;
        constexpr uint32_t BvhCounterBytes = BvhCounterCount * sizeof(uint32_t);
        constexpr uint32_t BvhMinQueueEntries = 64;

        // 手放したバッファ（作り直した「見えた」ビット・IndirectDraw のバッファなど）を破棄するまでのフレーム数。
        // フレームの飛行数は2以下なので、GPUはこれより前に使い終わっている
        constexpr uint64_t RetiredBufferFrames = 8;

        // コマンドリストの GPU タイムスタンプの区間（統計が有効な構成の trace の Type=GPU 行になる。それ以外では何もしない）
        class ScopedGpuTimestamp
        {
        public:
            ScopedGpuTimestamp(RHI::ICommandList *commandList, const char *scopeName)
                : m_CommandList(commandList)
            {
                if (m_CommandList)
                {
                    m_Handle = m_CommandList->BeginGPUTimestampScope(scopeName);
                }
            }

            ~ScopedGpuTimestamp()
            {
                if (m_CommandList && m_Handle.IsValid())
                {
                    m_CommandList->EndGPUTimestampScope(m_Handle);
                }
            }

            ScopedGpuTimestamp(const ScopedGpuTimestamp &) = delete;
            ScopedGpuTimestamp &operator=(const ScopedGpuTimestamp &) = delete;

        private:
            RHI::ICommandList *m_CommandList = nullptr;
            RHI::GPUTimestampScopeHandle m_Handle;
        };

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

        // グループの BVH のたどりを切って、平らなクラスタの列だけで判定する（撮り比べ用）
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *bvhValue = std::getenv("NORVES_MEGA_BVH");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (bvhValue != nullptr && (std::strcmp(bvhValue, "0") == 0 || std::strcmp(bvhValue, "off") == 0))
        {
            m_Settings.bUseGroupBVH = false;
        }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *statsEveryFrameValue = std::getenv("NORVES_MEGA_STATS_EVERY_FRAME");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (statsEveryFrameValue != nullptr && std::strcmp(statsEveryFrameValue, "1") == 0)
        {
            m_Settings.bStatsEveryFrame = true;
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

        // まとめたカリング・描画は、メッシュのクラスタ配列をデバイスアドレスで引き、コマンドの firstInstance で
        // 描画情報を引く。どちらかが無いデバイスではパスを無効にする
        {
            const auto &initCaps = m_Device->GetCapabilities();
            if (!initCaps.bBufferDeviceAddress || !initCaps.bDrawIndirectFirstInstance)
            {
                NORVES_LOG_ERROR("MegaGeometryPass",
                                 "MEGA_BATCH_UNSUPPORTED buffer_device_address=%d draw_indirect_first_instance=%d "
                                 "まとめたカリング・描画に対応しないため、パスは無効化されます",
                                 initCaps.bBufferDeviceAddress ? 1 : 0,
                                 initCaps.bDrawIndirectFirstInstance ? 1 : 0);
                m_bInitialized = true;
                return true;
            }
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
        cullPipelineDesc.descriptorSetLayouts.push_back(BuildCullDescriptorSetDesc());
        m_CullPipeline = m_Device->CreateComputePipeline(cullPipelineDesc);
        if (!m_CullPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリングパイプラインの作成に失敗");
            return false;
        }

        // グループの BVH をたどるカリング（BVH を持つメッシュ用）。作れなければ BVH は使わず、平らなクラスタの列で判定する
        if (context.ShaderMgr)
        {
            m_BvhCullShader = context.ShaderMgr->LoadShader(
                "cluster_bvh_cull.comp", RHI::ShaderStage::Compute);
        }
        if (m_BvhCullShader)
        {
            RHI::ComputePipelineDesc bvhPipelineDesc;
            bvhPipelineDesc.computeShader = m_BvhCullShader;
            bvhPipelineDesc.descriptorSetLayouts.push_back(BuildCullDescriptorSetDesc());
            m_BvhCullPipeline = m_Device->CreateComputePipeline(bvhPipelineDesc);
        }
        if (!m_BvhCullPipeline)
        {
            NORVES_LOG_WARNING("MegaGeometryPass",
                               "MEGA_BVH_UNAVAILABLE BVH をたどるカリングを作れないため、平らなクラスタの列で判定します");
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
        m_BvhCullPipeline.reset();
        m_BvhCullShader.reset();
        m_BvhQueueBuffer.reset();
        m_BvhCounterBuffer.reset();
        m_BvhQueueCapacity = 0;
        m_LoggedBvhInstances = 0xFFFFFFFFu;
        m_LoggedBvhLevels = 0xFFFFFFFFu;
        m_LoggedFlatInstances = 0xFFFFFFFFu;
        m_IndirectDrawBuffer.reset();
        m_DrawCountBuffer.reset();
        m_DrawInfoBuffer.reset();
        m_CommandCapacity = 0;
        m_CounterCapacity = 0;
        m_IndirectDrawBufferHandle = {};
        m_DrawCountBufferHandle = {};
        m_MegaGeometryCompleteHandle = {};
        for (FrameSlot &slot : m_FrameSlots)
        {
            slot = FrameSlot{};
        }
        m_DummyVisibilityBuffer.reset();
        m_DummyStatsBuffer.reset();
        m_VisibilityBuffer.reset();
        m_VisibilityEntries.clear();
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
        m_bStatsEpochActive = false;
        m_bDropVisibilityContinuity = false;
        m_bOcclusionFallbackLogged = false;
        m_bBatchUnsupportedLogged = false;
        m_HiZ.Shutdown();
        m_bHiZReady = false;

        m_DrawPipeline.reset();
        m_DrawWireframePipeline.reset();
        m_DrawVertexShader.reset();
        m_DrawFragmentShader.reset();

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
                instance.ComponentId = proxy.ComponentId;
                instance.Handle = proxy.MegaMeshHandle;
                std::memcpy(instance.WorldMatrix, &proxy.WorldTransform, sizeof(float) * 16);
                std::memcpy(instance.PreviousWorldMatrix, &proxy.PreviousWorldTransform, sizeof(float) * 16);
                m_Instances.push_back(instance);
            }
        }

        // スナップショットから外れたインスタンスの見えたビットは、全インスタンスが消えて記録を省くフレームでも
        // 引き継げないものにする（記録の中だけで連続性を見ると、空のフレームを挟んだ再追加が引き継ぎに見える）。
        // LastUsedFrame を0にすると、次の記録の連続の条件（LastUsedFrame + 1 == 記録のフレーム数）を満たさない。
        for (VisibilityEntry &entry : m_VisibilityEntries)
        {
            bool bPresent = false;
            for (size_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
            {
                const uint64_t objectId = m_Instances[instanceIndex].ObjectId;
                const uint64_t key = objectId != 0 ? objectId : (0x8000000000000000ull | instanceIndex);
                if (key == entry.Key)
                {
                    bPresent = true;
                    break;
                }
            }

            if (!bPresent)
            {
                entry.LastUsedFrame = 0;
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

    namespace
    {
        // 材質の区間にまとめられるか（同じ材質値・同じテクスチャのハンドルか）
        bool IsSameMaterial(const MegaGeometry::MegaMeshMaterial &a, const MegaGeometry::MegaMeshMaterial &b)
        {
            for (uint32_t index = 0; index < 4; ++index)
            {
                if (a.BaseColor[index] != b.BaseColor[index])
                {
                    return false;
                }
            }
            for (uint32_t index = 0; index < 3; ++index)
            {
                if (a.EmissiveColor[index] != b.EmissiveColor[index])
                {
                    return false;
                }
            }
            return a.EmissiveLuminanceNits == b.EmissiveLuminanceNits &&
                   a.AlbedoTexture == b.AlbedoTexture &&
                   a.NormalTexture == b.NormalTexture &&
                   a.MetallicTexture == b.MetallicTexture &&
                   a.RoughnessTexture == b.RoughnessTexture &&
                   a.AOTexture == b.AOTexture &&
                   a.ORMTexture == b.ORMTexture &&
                   a.HeightTexture == b.HeightTexture &&
                   a.bNormalTwoChannel == b.bNormalTwoChannel &&
                   a.HeightScale == b.HeightScale &&
                   a.bHasHeightMap == b.bHasHeightMap &&
                   a.DisplacementUVSpacing == b.DisplacementUVSpacing;
        }

        // 材質の区間ごとの定数（megageometry.vert/frag の MVPData と一致。ワールド変換はインスタンスの表にある）
        struct SectionUniformData
        {
            float View[16];
            float Projection[16];
            float CameraPosition[4];
            float ObjectColor[4];
            float EmissiveChromaticityAndLuminanceNits[4];
            float PomParams[4];
            float PreviousView[16];
            float PreviousProjection[16];
            float FrameParams[4]; // x=前のカメラがあるか（1/0）, y=発光に掛けるプリエクスポージャ, z=変位の頂点の間隔（UV）, w=描画の番号がLODの段か（1/0）
            float MaterialParams[4]; // x=ORMの1枚を metallic の枠に張ったか（1/0）, y=法線が2チャンネル（BC5）か（1/0）, z=材質のテクスチャが sparse（VT）か（1/0）, w=VT のフィードバックのパラメータ（アルベド。0 は書かない）
            float VtFeedbackParams[4]; // VT のフィードバックのパラメータ: x=法線, y=ORM（metallic の枠）, z=高さ（0 は書かない）, w=未使用
        };
        static_assert(sizeof(SectionUniformData) <= 512u);

        constexpr uint32_t SectionUniformBufferBytes = 512;
        constexpr uint32_t IndirectCommandBytes = static_cast<uint32_t>(sizeof(MegaGeometry::DrawIndexedIndirectCommand));
    } // namespace

    void MegaGeometryPass::RecordFrameCommand(const MegaGeometryPassCommand &command, RHI::ICommandList *commandList)
    {
        // CPU の記録時間（trace の Type=Scope 行）と、記録した区間の GPU 時間（Type=GPU 行 "MegaGeometry"）
        NORVES_PROFILE_SCOPE("MegaGeometryPass.RecordFrameCommand");
        ScopedGpuTimestamp gpuTimestamp(commandList, "MegaGeometry");

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
        bool bTwoPass = CanUseTwoPassOcclusion(command);
        // 2パスでないフレームも数える（見えたビットは連続した2パスのフレームの間でだけ引き継ぐため）
        ++m_OcclusionFrameCount;
        // 決定的な撮影のエポック（読み込み完了）の最初のフレームから、統計の行に相対フレームの番号を付ける
        // 見えたビットも時間的な状態なので、エポックの最初のフレームは引き継がずに0から始める（読み込み完了までの
        // フレーム数の違いが、エポックの最初のフレームの1パス目の数に出ないようにする）
        m_bDropVisibilityContinuity = command.bDeterministicCapture && command.bTemporalEpochStart;
        if (m_bDropVisibilityContinuity)
        {
            m_bStatsEpochActive = true;
        }
        const int64_t epochFrame =
            (command.bDeterministicCapture && m_bStatsEpochActive) ? static_cast<int64_t>(command.TemporalFrameIndex) : -1;

        // ========================================
        // 描くインスタンスと、材質の区間の収集
        // ========================================
        // 区間: 同じ材質（値とテクスチャ）で、メッシュを置くプールの塊が同じインスタンスの集まり。区間ごとに
        // IndirectDraw コマンドの連続した範囲・カウンタ・描画のディスクリプタセットを持ち、1回の間接描画で描く。
        struct Section
        {
            const MegaGeometry::MegaMeshGPUData *Representative = nullptr; // 材質・プールの塊の持ち主
            uint64_t ClusterTotal = 0;                                     // 区間のインスタンスのクラスタ数の合計
            uint32_t Capacity = 0;                                         // 1パスのコマンドの最大数
            uint32_t CommandBase = 0;                                      // 1パス目の範囲での先頭（コマンドの位置）
        };
        struct Drawable
        {
            size_t InstanceIndex = 0;
            const MegaGeometry::MegaMeshGPUData *GpuData = nullptr;
        };

        VariableArray<Section> sections;
        VariableArray<Drawable> drawables;
        VariableArray<GPUMegaInstance> instanceTable;
        VariableArray<VisibilityRequest> visibilityRequests;
        drawables.reserve(m_Instances.size());
        instanceTable.reserve(m_Instances.size());

        // グループの BVH を持つメッシュ（NVMESH v1.1）は BVH をたどって判定し、それ以外は平らなクラスタの列で判定する。
        // BVH のインスタンスをインスタンスの表の先頭に並べる（平らな判定のワークグループは、後ろのインスタンスだけが持つ）
        const auto canTraverseBvh = [this](const MegaGeometry::MegaMeshGPUData *data) -> bool
        {
            return m_Settings.bUseGroupBVH && m_BvhCullPipeline && m_BvhQueueBuffer && m_BvhCounterBuffer && data &&
                   data->GroupBVHNodeCount > 0 && data->GroupBVHBufferBytes > 0 &&
                   !data->GroupBVHLevelNodeCounts.empty() &&
                   data->GroupBVHLevelNodeCounts.size() <= MegaGeometry::GROUP_BVH_MAX_LEVELS;
        };
        VariableArray<size_t> instanceOrder;
        instanceOrder.reserve(m_Instances.size());
        {
            VariableArray<size_t> flatInstances;
            for (size_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
            {
                const auto *orderData = command.MegaGeometry->GetReadyMegaMeshGPUData(m_Instances[instanceIndex].Handle);
                (canTraverseBvh(orderData) ? instanceOrder : flatInstances).push_back(instanceIndex);
            }
            instanceOrder.insert(instanceOrder.end(), flatInstances.begin(), flatInstances.end());
        }

        uint64_t totalGroups = 0;
        // BVH のたどりの規模: BVH を持つインスタンスの数と、段ごとの節の数・葉の数のインスタンスの合計（列の大きさになる）
        uint32_t bvhInstanceCount = 0;
        VariableArray<uint64_t> bvhLevelCapacity;
        uint64_t bvhLeafCapacity = 0;
        for (const size_t instanceIndex : instanceOrder)
        {
            const auto &instance = m_Instances[instanceIndex];
            // 区画へのコピーが GPU で完了したメッシュだけを描く（書き込み中の区画は読まない）
            const auto *gpuData = command.MegaGeometry->GetReadyMegaMeshGPUData(instance.Handle);
            if (!gpuData || gpuData->ClusterCount == 0)
            {
                continue;
            }

            // クラスタ配列はデバイスアドレスで、頂点・インデックスはプールの塊の先頭からの基点で引く。
            // 基点は頂点・インデックスの単位で表すので、領域の先頭が頂点・インデックスの大きさの倍数であること
            // （プールの区画は 256 バイト整列）
            const uint64_t clusterAddress =
                gpuData->ClusterBuffer ? gpuData->ClusterBuffer->GetDeviceAddress() : 0ull;
            const bool bAddressable = clusterAddress != 0 && gpuData->VertexBuffer && gpuData->IndexBuffer;
            const bool bAligned =
                gpuData->VertexBufferOffsetBytes % sizeof(Mesh3DVertex) == 0 &&
                gpuData->IndexBufferOffsetBytes % sizeof(uint32_t) == 0;
            const uint64_t vertexBase = gpuData->VertexBufferOffsetBytes / sizeof(Mesh3DVertex);
            const uint64_t indexBase = gpuData->IndexBufferOffsetBytes / sizeof(uint32_t);
            if (!bAddressable || !bAligned || vertexBase > 0x7FFFFFFFull || indexBase > 0xFFFFFFFFull)
            {
                if (!m_bBatchUnsupportedLogged)
                {
                    NORVES_LOG_ERROR("MegaGeometryPass",
                                     "MEGA_BATCH_INSTANCE_SKIPPED mesh=\"%s\" addressable=%d aligned=%d "
                                     "メッシュの区画をまとめた描画で引けないため、このメッシュは描きません",
                                     gpuData->DebugName.empty() ? "" : gpuData->DebugName.c_str(),
                                     bAddressable ? 1 : 0,
                                     bAligned ? 1 : 0);
                    m_bBatchUnsupportedLogged = true;
                }
                continue;
            }

            // 区間を探す（無ければ作る）
            uint32_t sectionIndex = 0;
            for (; sectionIndex < sections.size(); ++sectionIndex)
            {
                const auto *representative = sections[sectionIndex].Representative;
                if (representative->VertexBuffer.get() == gpuData->VertexBuffer.get() &&
                    representative->IndexBuffer.get() == gpuData->IndexBuffer.get() &&
                    IsSameMaterial(representative->Material, gpuData->Material))
                {
                    break;
                }
            }
            if (sectionIndex == sections.size())
            {
                Section section;
                section.Representative = gpuData;
                sections.push_back(section);
            }
            sections[sectionIndex].ClusterTotal += gpuData->ClusterCount;

            const uint64_t clusterAddressWithOffset = clusterAddress + gpuData->ClusterBufferOffsetBytes;
            GPUMegaInstance entry{};
            std::memcpy(entry.WorldMatrix, instance.WorldMatrix, sizeof(float) * 16);
            std::memcpy(entry.PreviousWorldMatrix, instance.PreviousWorldMatrix, sizeof(float) * 16);
            if (gpuData->LODBounds.IsValid())
            {
                entry.LODSphere[0] = gpuData->LODBounds.CenterX;
                entry.LODSphere[1] = gpuData->LODBounds.CenterY;
                entry.LODSphere[2] = gpuData->LODBounds.CenterZ;
                entry.LODSphere[3] = gpuData->LODBounds.Radius;
            }
            entry.ClusterAddressLow = static_cast<uint32_t>(clusterAddressWithOffset & 0xFFFFFFFFull);
            entry.ClusterAddressHigh = static_cast<uint32_t>(clusterAddressWithOffset >> 32);
            entry.ClusterCount = gpuData->ClusterCount;
            entry.FirstGroup = static_cast<uint32_t>(totalGroups);
            entry.SectionIndex = sectionIndex;
            entry.VertexBase = static_cast<uint32_t>(vertexBase);
            entry.IndexBase = static_cast<uint32_t>(indexBase);
            entry.PageTableBase = gpuData->PageTableBase;
            if (canTraverseBvh(gpuData))
            {
                // BVH をたどるインスタンスは、平らな判定のワークグループを持たない
                const uint64_t bvhAddress = clusterAddress + gpuData->GroupBVHBufferOffsetBytes;
                entry.BvhAddressLow = static_cast<uint32_t>(bvhAddress & 0xFFFFFFFFull);
                entry.BvhAddressHigh = static_cast<uint32_t>(bvhAddress >> 32);
                entry.BvhNodeCount = gpuData->GroupBVHNodeCount;
                ++bvhInstanceCount;
                if (bvhLevelCapacity.size() < gpuData->GroupBVHLevelNodeCounts.size())
                {
                    bvhLevelCapacity.resize(gpuData->GroupBVHLevelNodeCounts.size(), 0ull);
                }
                for (size_t level = 0; level < gpuData->GroupBVHLevelNodeCounts.size(); ++level)
                {
                    bvhLevelCapacity[level] += gpuData->GroupBVHLevelNodeCounts[level];
                }
                bvhLeafCapacity += gpuData->GroupBVHLeafCount;
            }
            else
            {
                totalGroups += (static_cast<uint64_t>(gpuData->ClusterCount) + 63u) / 64u;
            }

            VisibilityRequest request;
            request.Key = instance.ObjectId != 0 ? instance.ObjectId : (0x8000000000000000ull | instanceIndex);
            request.MeshId = instance.Handle.Id;
            request.ComponentId = instance.ComponentId;
            request.ClusterBufferIdentity = gpuData->ClusterBuffer.get();
            request.ClusterBufferOffsetBytes = gpuData->ClusterBufferOffsetBytes;
            request.ClusterCount = gpuData->ClusterCount;

            Drawable drawable;
            drawable.InstanceIndex = instanceIndex;
            drawable.GpuData = gpuData;
            drawables.push_back(drawable);
            instanceTable.push_back(entry);
            visibilityRequests.push_back(request);
        }

        if (instanceTable.empty() || totalGroups > 0xFFFFFFFFull)
        {
            recordEmptyRenderPass();
            return;
        }

        // BVH のたどりの列の配置: 段 k（1以上）の入力の列、続けて葉の列。段 0 の入力はインスタンスの表の先頭
        // （根の節）なので列を持たない。各節の親は1つなので、段 k の列は「段 k の節の数のインスタンスの合計」を超えない
        const uint32_t bvhLevelCount = static_cast<uint32_t>(bvhLevelCapacity.size());
        VariableArray<uint32_t> bvhQueueBase(static_cast<size_t>(bvhLevelCount) + 1u, 0u);
        uint64_t bvhQueueEntries = 0;
        for (uint32_t level = 1; level < bvhLevelCount; ++level)
        {
            bvhQueueBase[level] = static_cast<uint32_t>(bvhQueueEntries);
            bvhQueueEntries += bvhLevelCapacity[level];
            if (bvhQueueEntries > 0x3FFFFFFFull)
            {
                break;
            }
        }
        bvhQueueBase[bvhLevelCount] = static_cast<uint32_t>(bvhQueueEntries);
        bvhQueueEntries += bvhLeafCapacity;
        if (bvhQueueEntries > 0x3FFFFFFFull || bvhLeafCapacity * BvhLeafSlots > 0xFFFFFFFFull)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "MEGA_BVH_QUEUE_TOO_LARGE entries=%llu",
                             static_cast<unsigned long long>(bvhQueueEntries));
            recordEmptyRenderPass();
            return;
        }
        if (bvhInstanceCount != m_LoggedBvhInstances || bvhLevelCount != m_LoggedBvhLevels ||
            static_cast<uint32_t>(instanceTable.size()) - bvhInstanceCount != m_LoggedFlatInstances)
        {
            m_LoggedBvhInstances = bvhInstanceCount;
            m_LoggedBvhLevels = bvhLevelCount;
            m_LoggedFlatInstances = static_cast<uint32_t>(instanceTable.size()) - bvhInstanceCount;
            NORVES_LOG_INFO("MegaGeometryPass",
                            "MEGA_BVH bvh_instances=%u flat_instances=%u levels=%u leaf_capacity=%llu queue_entries=%llu "
                            "flat_groups=%llu dispatches_per_pass=%u",
                            bvhInstanceCount,
                            static_cast<uint32_t>(instanceTable.size()) - bvhInstanceCount,
                            bvhLevelCount,
                            static_cast<unsigned long long>(bvhLeafCapacity),
                            static_cast<unsigned long long>(bvhQueueEntries),
                            static_cast<unsigned long long>(totalGroups),
                            (totalGroups > 0 ? 1u : 0u) + (bvhInstanceCount > 0 ? bvhLevelCount + 1u : 0u));
        }

        // 「前のフレームで見えた」ビットの配置（2パスのときだけ）。作れなければ従来の1回の判定で描く
        if (bTwoPass)
        {
            if (UpdateVisibilityLayout(cmdList, visibilityRequests))
            {
                for (size_t index = 0; index < instanceTable.size(); ++index)
                {
                    instanceTable[index].VisibleOffset = visibilityRequests[index].Offset;
                }
            }
            else
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "見えたビットのバッファを作れませんでした。遮蔽カリングを使わずに描きます");
                bTwoPass = false;
            }
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
                    // エポックが始まっていれば相対フレームが30の倍数のフレームで出す（撮影の間で同じ相対フレームを突き合わせられる）
                    const bool bSampleFrame = slot.EpochFrame >= 0 ? (slot.EpochFrame % 30 == 0) : (slot.Frame % 30 == 0);
                    if (!m_bStatsLoggedOnce || bSampleFrame || m_Settings.bStatsEveryFrame)
                    {
                        NORVES_LOG_INFO("MegaGeometryPass",
                                        "MEGA_OCCLUSION frame=%llu epoch_frame=%lld pass1=%u pass2_tested=%u pass2_drawn=%u occluded=%u",
                                        static_cast<unsigned long long>(slot.RenderFrame),
                                        static_cast<long long>(slot.EpochFrame),
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

        // ========================================
        // 区間のコマンドの範囲と、GPUバッファ・フレームスロットの用意
        // ========================================
        const uint32_t sectionCount = static_cast<uint32_t>(sections.size());
        const uint32_t passCount = bTwoPass ? 2u : 1u;
        uint64_t commandsPerPass = 0;
        for (Section &section : sections)
        {
            section.Capacity = static_cast<uint32_t>(std::min<uint64_t>(section.ClusterTotal, m_Settings.MaxDrawCount));
            section.CommandBase = static_cast<uint32_t>(commandsPerPass);
            commandsPerPass += section.Capacity;
        }
        const uint64_t commandsTotal = commandsPerPass * passCount;
        if (commandsTotal == 0 || commandsTotal > 0x7FFFFFFFull / IndirectCommandBytes)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "MEGA_BATCH_COMMAND_COUNT_INVALID commands=%llu", static_cast<unsigned long long>(commandsTotal));
            recordEmptyRenderPass();
            return;
        }

        FrameSlot &frameSlot = m_FrameSlots[m_OcclusionFrameCount % FrameSlotCount];
        if (!EnsureBatchBuffers(static_cast<uint32_t>(commandsTotal), sectionCount * passCount) ||
            !EnsureFrameSlot(frameSlot, static_cast<uint32_t>(instanceTable.size()), sectionCount * passCount, sectionCount) ||
            (bvhInstanceCount > 0 &&
             (!EnsureBvhBuffers(static_cast<uint32_t>(bvhQueueEntries)) ||
              !EnsureBvhStageResources(frameSlot, 0, bvhLevelCount + 1u) ||
              (bTwoPass && !EnsureBvhStageResources(frameSlot, 1, bvhLevelCount + 1u)))))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "まとめた描画の資源を用意できませんでした");
            recordEmptyRenderPass();
            return;
        }

        // ページの表（常駐の状態）をこのフレームのスロットへ写す。フレームの間は変わらないので、2パスの判定が食い違わない
        if (!SyncPageTable(frameSlot, *command.MegaGeometry))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "ページの表のバッファを用意できませんでした");
            recordEmptyRenderPass();
            return;
        }

        // ページの要求を書くバッファ（このフレームのもの。獲得できなければ容量 0 で、統計用の代わりのバッファを束ねる）。
        // 要求が指す表の位置を後で引き直せるよう、このフレームのシェーダーが見る表の版を結び付ける
        command.MegaGeometry->SetCurrentPageTableVersion(frameSlot.PageTableVersion);
        RHI::BufferPtr pageRequestBuffer = command.MegaGeometry->GetCurrentPageRequestBuffer();
        const uint32_t pageRequestCapacity = pageRequestBuffer ? command.MegaGeometry->GetCurrentPageRequestCapacity() : 0u;
        if (!pageRequestBuffer || pageRequestCapacity == 0)
        {
            pageRequestBuffer = m_DummyStatsBuffer;
        }

        // インスタンスの表と区間の表を書く（ホストが書き、カリングと頂点シェーダーが読む）
        frameSlot.InstanceBuffer->Update(instanceTable.data(), instanceTable.size() * sizeof(GPUMegaInstance));
        {
            VariableArray<uint32_t> sectionTable;
            sectionTable.reserve(static_cast<size_t>(sectionCount) * passCount * 2u);
            for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
            {
                for (const Section &section : sections)
                {
                    sectionTable.push_back(static_cast<uint32_t>(passIndex * commandsPerPass) + section.CommandBase);
                    sectionTable.push_back(section.Capacity);
                }
            }
            frameSlot.SectionBuffer->Update(sectionTable.data(), sectionTable.size() * sizeof(uint32_t));
        }

        // ========================================
        // 区間ごとの描画入力（定数・テクスチャ）を準備
        // ========================================
        for (uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
        {
            const auto *gpuData = sections[sectionIndex].Representative;
            const SectionDraw &sectionDraw = frameSlot.Sections[sectionIndex];

            SectionUniformData uniform{};
            cameraConstants.CopyShaderView(uniform.View);
            cameraConstants.CopyShaderProjection(uniform.Projection);
            cameraConstants.CopyCameraPosition(uniform.CameraPosition);
            previousCameraConstants.CopyShaderView(uniform.PreviousView);
            previousCameraConstants.CopyShaderProjection(uniform.PreviousProjection);
            uniform.FrameParams[0] = command.bHasPreviousCamera ? 1.0f : 0.0f;
            // 発光はプリエクスポージャ後の値で GBuffer_Emissive へ書く（GBufferPass・LightingPass と同じ値）。
            uniform.FrameParams[1] = ResolveSceneColorPreExposure(&cam);

            // マテリアル値を設定
            const auto &mat = gpuData->Material;
            uniform.ObjectColor[0] = mat.BaseColor[0];
            uniform.ObjectColor[1] = mat.BaseColor[1];
            uniform.ObjectColor[2] = mat.BaseColor[2];
            uniform.ObjectColor[3] = mat.BaseColor[3];
            uniform.EmissiveChromaticityAndLuminanceNits[0] = mat.EmissiveColor[0];
            uniform.EmissiveChromaticityAndLuminanceNits[1] = mat.EmissiveColor[1];
            uniform.EmissiveChromaticityAndLuminanceNits[2] = mat.EmissiveColor[2];
            uniform.EmissiveChromaticityAndLuminanceNits[3] = mat.EmissiveLuminanceNits;
            uniform.PomParams[0] = mat.HeightScale;
            uniform.PomParams[1] = mat.bHasHeightMap ? 1.0f : 0.0f;
            uniform.PomParams[2] = static_cast<float>(static_cast<uint8_t>(command.DebugMode));
            uniform.PomParams[3] = bMegaGeometryDebugPayloadSupported ? 1.0f : 0.0f;
            uniform.FrameParams[2] = mat.DisplacementUVSpacing > 0.0f ? mat.DisplacementUVSpacing : 0.0f;
            uniform.FrameParams[3] = bLODLevelPayload ? 1.0f : 0.0f;

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
            uniform.MaterialParams[0] = orm ? 1.0f : 0.0f;
            uniform.MaterialParams[1] = mat.bNormalTwoChannel ? 1.0f : 0.0f;

            // PBRテクスチャ
            auto albedo = resolveTexture(mat.AlbedoTexture, m_DefaultWhiteTexture);
            auto normal = resolveTexture(mat.NormalTexture, m_DefaultFlatNormalTexture);
            auto metallic = orm ? orm : resolveTexture(mat.MetallicTexture, m_DefaultBlackTexture);
            auto roughness = orm ? orm : resolveTexture(mat.RoughnessTexture, m_DefaultWhiteTexture);
            auto ao = orm ? orm : resolveTexture(mat.AOTexture, m_DefaultWhiteTexture);
            auto height = resolveTexture(mat.HeightTexture, m_DefaultBlackTexture);

            // 張るテクスチャに sparse（VT）が1枚でもあれば、シェーダーは常駐しないタイルを読まず粗いミップへ逃げる。
            uniform.MaterialParams[2] = AnySparseTexture(albedo, normal, metallic, roughness, ao, height) ? 1.0f : 0.0f;
            // VT の要求を書く先（このフレームのバッファ。対応しないデバイスでは null で、シェーダーに binding は入らない）。
            // アルベドが VT のとき、シェーダーがこのフレームの要求を書く（24bit 以下の整数は float に正確に載る）
            const TextureResources::VirtualTextureFeedbackTarget feedbackTarget =
                command.Textures ? command.Textures->GetVirtualTextureFeedbackTarget()
                                 : TextureResources::VirtualTextureFeedbackTarget{};
            uniform.MaterialParams[3] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.AlbedoTexture, albedo.get(), feedbackTarget));
            // 法線・ORM・高さも VT のとき、それぞれの表の番号で要求を書く（ORM の枠は metallic に張ったテクスチャ）
            uniform.VtFeedbackParams[0] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.NormalTexture, normal.get(), feedbackTarget));
            uniform.VtFeedbackParams[1] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.ORMTexture, orm.get(), feedbackTarget));
            uniform.VtFeedbackParams[2] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.HeightTexture, height.get(), feedbackTarget));

            sectionDraw.Uniform->Update(&uniform, sizeof(SectionUniformData));

            const RHI::DescriptorSetPtr &descriptorSet = sectionDraw.DescriptorSet;
            descriptorSet->BindConstantBuffer(0, sectionDraw.Uniform, 0,
                                              static_cast<uint32_t>(sizeof(SectionUniformData)));

            // PBRテクスチャバインド
            descriptorSet->BindTexture(1, albedo);
            descriptorSet->BindSampler(1, m_DefaultLinearSampler);
            descriptorSet->BindTexture(2, normal);
            descriptorSet->BindSampler(2, m_DefaultLinearSampler);
            descriptorSet->BindTexture(3, metallic);
            descriptorSet->BindSampler(3, m_DefaultLinearSampler);
            descriptorSet->BindTexture(4, roughness);
            descriptorSet->BindSampler(4, m_DefaultLinearSampler);
            descriptorSet->BindTexture(5, ao);
            descriptorSet->BindSampler(5, m_DefaultLinearSampler);
            descriptorSet->BindTexture(6, height);
            descriptorSet->BindSampler(6, m_DefaultLinearSampler);
            BindVirtualTextureFeedback(*descriptorSet, VirtualTextureFeedbackBindingIndex, feedbackTarget);

            // 頂点シェーダーが引くインスタンスの表と描画情報
            descriptorSet->BindStorageBuffer(8, frameSlot.InstanceBuffer, 0,
                                             static_cast<uint32_t>(instanceTable.size() * sizeof(GPUMegaInstance)));
            descriptorSet->BindStorageBuffer(9, m_DrawInfoBuffer, 0,
                                             static_cast<uint32_t>(m_DrawInfoBuffer->GetSize()));

            descriptorSet->Update();
        }

        // ========================================
        // クラスタカリングの記録（パスごと）
        // ========================================
        // カリング用ユニフォームの、パスによらない部分
        CullUniformData baseUniform{};
        cameraConstants.CopyShaderView(baseUniform.ViewMatrix);
        cameraConstants.CopyShaderProjection(baseUniform.ProjectionMatrix);
        cameraConstants.CopyCameraPosition(baseUniform.CameraPosition);
        baseUniform.CameraPosition[3] = 0.0f;
        frustumPlanes.CopyToShaderData(baseUniform.FrustumPlanes);
        baseUniform.InstanceCount = static_cast<uint32_t>(instanceTable.size());
        baseUniform.TotalGroupCount = static_cast<uint32_t>(totalGroups);
        baseUniform.LODBias = m_Settings.LODBias;
        baseUniform.ScreenHeight = static_cast<float>(m_CurrentHeight);
        // projectionFactor = screenHeight / (2 * tan(fov/2))
        const float halfFovTan = std::tan(cameraConstants.FieldOfViewRadians * 0.5f);
        baseUniform.ProjectionFactor = (halfFovTan > 1e-6f)
                                           ? static_cast<float>(m_CurrentHeight) / (2.0f * halfFovTan)
                                           : 1.0f;
        baseUniform.DebugPayloadMode = debugPayloadMode;
        // 「見えた」印: 前のフレームの2パス目が書いた値（今のフレームの番号）と、今のフレームが書く値。
        // 0（バッファを0で埋めた直後・見えなかった）とは決して一致しない（番号は1から数える）
        baseUniform.VisibleReadStamp = static_cast<uint32_t>(m_OcclusionFrameCount);
        baseUniform.VisibleWriteStamp = static_cast<uint32_t>(m_OcclusionFrameCount) + 1u;
        baseUniform.BvhRootCount = bvhInstanceCount;
        baseUniform.PageRequestCapacity = pageRequestCapacity;

        // メッシュ共通のLOD球を持つメッシュの選ばれる段を、変わったときに記録する
        for (const Drawable &drawable : drawables)
        {
            if (drawable.GpuData->LODBounds.IsValid())
            {
                LogUniformLODSelection(m_Instances[drawable.InstanceIndex], *drawable.GpuData, baseUniform);
            }
        }

        RHI::BufferPtr statsBuffer = m_DummyStatsBuffer;
        if (statsSlot)
        {
            statsBuffer = statsSlot->Buffer;
            // 1フレームぶんの統計を0から数える（前に使ったのはホストが読んだ後）
            cmdList->BufferBarrier(statsBuffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(statsBuffer, 0, StatsBufferBytes, 0);
            cmdList->BufferBarrier(statsBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
            statsSlot->Frame = m_OcclusionFrameCount;
            statsSlot->RenderFrame = command.FrameNumber;
            statsSlot->EpochFrame = epochFrame;
            statsSlot->bPending = true;
        }

        // 区間のカウンタ（全パス分）を0にし、コマンド・描画情報をカリングが書ける状態にする。
        // GPU側のカウントを参照できない環境（DrawIndexedIndirect）では、積まれなかったコマンドが
        // instanceCount=0 の空振りになるよう、コマンドも0で埋める
        cmdList->BufferBarrier(m_DrawCountBuffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
        cmdList->FillBuffer(m_DrawCountBuffer, 0, static_cast<uint64_t>(sectionCount) * passCount * sizeof(uint32_t), 0);
        cmdList->BufferBarrier(m_DrawCountBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
        if (caps.bDrawIndirectCount)
        {
            cmdList->BufferBarrier(m_IndirectDrawBuffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        }
        else
        {
            cmdList->BufferBarrier(m_IndirectDrawBuffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(m_IndirectDrawBuffer, 0, commandsTotal * IndirectCommandBytes, 0);
            cmdList->BufferBarrier(m_IndirectDrawBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
        }
        cmdList->BufferBarrier(m_DrawInfoBuffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);

        // BVH のたどりの列・カウンタを、このフレームで使い始めたか（最初のパスの前に Common から使える状態にする）
        bool bBvhBuffersUsed = false;

        // cullPass: 0=従来の1回の判定、1=1パス目、2=2パス目。hiZTexture は2パス目の遮蔽の判定が読む HZB（null なら判定しない）
        auto recordCull = [&](uint32_t cullPass, const RHI::TexturePtr &hiZTexture) -> void
        {
            const bool bSecond = cullPass == CULL_PASS_SECOND;
            const uint32_t passIndex = bSecond ? 1u : 0u;
            ScopedGpuTimestamp cullTimestamp(cmdList, bSecond ? "MegaGeometryCull2" : "MegaGeometryCull1");

            // ----------------------------------------
            // 1. カリングユニフォーム更新
            // ----------------------------------------
            CullUniformData uniformData = baseUniform;
            uniformData.CullPass = cullPass;
            uniformData.bStatsEnabled = (statsSlot && cullPass != CULL_PASS_SINGLE) ? 1u : 0u;
            uniformData.SectionBase = passIndex * sectionCount;

            // 遮蔽の判定（2パス目だけ）。HZB の元の深度の解像度を渡す（HZB のミップ0 はその半分）
            if (bSecond && hiZTexture && m_DepthTexture)
            {
                uniformData.HiZWidth = m_DepthTexture->GetWidth();
                uniformData.HiZHeight = m_DepthTexture->GetHeight();
                uniformData.HiZMipCount = m_HiZ.GetMipCount();
                uniformData.bHiZEnabled = 1;
            }
            frameSlot.CullUniform[passIndex]->Update(&uniformData, sizeof(CullUniformData));

            // ----------------------------------------
            // 2. カリングディスクリプタセット（平らな判定と BVH の各段で、UBO だけが違う）
            // ----------------------------------------
            const auto bindCullDescriptors = [&](const RHI::DescriptorSetPtr &descriptorSet,
                                                 const RHI::BufferPtr &cullUniformBuffer) -> void
            {
                descriptorSet->BindConstantBuffer(0, cullUniformBuffer, 0,
                                                  static_cast<uint32_t>(sizeof(CullUniformData)));
                descriptorSet->BindStorageBuffer(1, frameSlot.InstanceBuffer, 0,
                                                 static_cast<uint32_t>(instanceTable.size() * sizeof(GPUMegaInstance)));
                descriptorSet->BindStorageBuffer(2, m_IndirectDrawBuffer, 0,
                                                 static_cast<uint32_t>(m_IndirectDrawBuffer->GetSize()));
                descriptorSet->BindStorageBuffer(3, m_DrawCountBuffer, 0,
                                                 static_cast<uint32_t>(m_DrawCountBuffer->GetSize()));

                // binding 4 の HZB は2パス目の遮蔽の判定（bHiZEnabled=1）だけが読む
                descriptorSet->BindTexture(4, (bSecond && hiZTexture) ? hiZTexture : m_DefaultBlackTexture);
                descriptorSet->BindSampler(4, m_DefaultLinearSampler);

                // binding 5・6: 「前のフレームで見えた」印と統計。従来の経路は触らないので代わりのバッファを結ぶ
                if (bTwoPass && cullPass != CULL_PASS_SINGLE)
                {
                    descriptorSet->BindStorageBuffer(5, m_VisibilityBuffer, 0,
                                                     static_cast<uint32_t>(m_VisibilityBuffer->GetSize()));
                }
                else
                {
                    descriptorSet->BindStorageBuffer(5, m_DummyVisibilityBuffer, 0,
                                                     static_cast<uint32_t>(m_DummyVisibilityBuffer->GetSize()));
                }
                descriptorSet->BindStorageBuffer(6, statsBuffer, 0, StatsBufferBytes);
                descriptorSet->BindStorageBuffer(7, frameSlot.SectionBuffer, 0,
                                                 static_cast<uint32_t>(sectionCount * passCount * 2u * sizeof(uint32_t)));
                descriptorSet->BindStorageBuffer(8, m_DrawInfoBuffer, 0,
                                                 static_cast<uint32_t>(m_DrawInfoBuffer->GetSize()));
                // binding 9・10: BVH のたどりの列とカウンタ（BVH を使わない経路は触らない）
                descriptorSet->BindStorageBuffer(9, m_BvhQueueBuffer, 0,
                                                 static_cast<uint32_t>(m_BvhQueueBuffer->GetSize()));
                descriptorSet->BindStorageBuffer(10, m_BvhCounterBuffer, 0, BvhCounterBytes);
                // binding 11・12: ページの表（このフレームの常駐）と、ページの要求の列
                descriptorSet->BindStorageBuffer(11, frameSlot.PageTableBuffer, 0,
                                                 static_cast<uint32_t>(frameSlot.PageTableBuffer->GetSize()));
                descriptorSet->BindStorageBuffer(12, pageRequestBuffer, 0,
                                                 static_cast<uint32_t>(pageRequestBuffer->GetSize()));
                descriptorSet->Update();
            };

            // 1次元のスレッド数を、x 方向の上限を避けて2次元のワークグループで出す
            const auto dispatchThreads = [&](uint64_t threads) -> void
            {
                const uint64_t groups = (threads + 63u) / 64u;
                const uint32_t dispatchX = static_cast<uint32_t>(std::min<uint64_t>(groups, 65535u));
                const uint32_t dispatchY = static_cast<uint32_t>((groups + dispatchX - 1u) / dispatchX);
                cmdList->Dispatch(dispatchX, dispatchY, 1);
            };

            // ----------------------------------------
            // 3. 平らな判定（BVH を持たないインスタンス。全部を1回の dispatch で）
            // ----------------------------------------
            if (totalGroups > 0)
            {
                const RHI::DescriptorSetPtr &cullDescriptorSet = frameSlot.CullDescriptorSet[passIndex];
                bindCullDescriptors(cullDescriptorSet, frameSlot.CullUniform[passIndex]);
                cmdList->SetPipeline(m_CullPipeline);
                cmdList->SetDescriptorSet(cullDescriptorSet, 0);
                dispatchThreads(totalGroups * 64u);
            }

            // ----------------------------------------
            // 3b. BVH をたどる判定（BVH を持つインスタンス）。節を段ごとの dispatch で判定して枝を切り、
            //     残った葉のクラスタを平らな判定と同じ判定にかける
            // ----------------------------------------
            if (bvhInstanceCount > 0)
            {
                if (totalGroups > 0)
                {
                    // 平らな判定の書き込み（描画コマンド・カウンタ・描画情報・見えた印・統計）を、BVH の判定へ見せる
                    cmdList->BufferBarrier(m_IndirectDrawBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_DrawCountBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_DrawInfoBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    if (bTwoPass && cullPass != CULL_PASS_SINGLE)
                    {
                        cmdList->BufferBarrier(m_VisibilityBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    }
                    if (uniformData.bStatsEnabled != 0)
                    {
                        cmdList->BufferBarrier(statsBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    }
                    if (pageRequestCapacity != 0)
                    {
                        // ページの要求の印と列に、BVH の葉のクラスタの判定が続けて書く
                        cmdList->BufferBarrier(frameSlot.PageTableBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                        cmdList->BufferBarrier(pageRequestBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    }
                }

                // 列とカウンタを使える状態にし、カウンタをパスごとに0へ戻す
                cmdList->BufferBarrier(m_BvhCounterBuffer,
                                       bBvhBuffersUsed ? RHI::ResourceState::UnorderedAccess : RHI::ResourceState::Common,
                                       RHI::ResourceState::CopyDest);
                if (!bBvhBuffersUsed)
                {
                    cmdList->BufferBarrier(m_BvhQueueBuffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
                    bBvhBuffersUsed = true;
                }
                cmdList->FillBuffer(m_BvhCounterBuffer, 0, BvhCounterBytes, 0);
                cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);

                cmdList->SetPipeline(m_BvhCullPipeline);
                for (uint32_t stage = 0; stage <= bvhLevelCount; ++stage)
                {
                    const bool bClusterStage = stage == bvhLevelCount;
                    uint64_t stageThreads = 0;
                    if (bClusterStage)
                    {
                        stageThreads = bvhLeafCapacity * BvhLeafSlots;
                    }
                    else
                    {
                        stageThreads = stage == 0 ? static_cast<uint64_t>(bvhInstanceCount) : bvhLevelCapacity[stage];
                    }
                    if (stageThreads == 0)
                    {
                        continue;
                    }

                    CullUniformData stageUniform = uniformData;
                    stageUniform.BvhStage = bClusterStage ? BvhStageClusters : stage;
                    stageUniform.BvhInputBase = (!bClusterStage && stage >= 1) ? bvhQueueBase[stage] : 0u;
                    stageUniform.BvhNextBase = stage + 1 < bvhLevelCount ? bvhQueueBase[stage + 1] : bvhQueueBase[bvhLevelCount];
                    stageUniform.BvhLeafBase = bvhQueueBase[bvhLevelCount];
                    const BvhStageDraw &stageDraw = frameSlot.BvhStages[passIndex][stage];
                    stageDraw.Uniform->Update(&stageUniform, sizeof(CullUniformData));
                    bindCullDescriptors(stageDraw.DescriptorSet, stageDraw.Uniform);
                    cmdList->SetDescriptorSet(stageDraw.DescriptorSet, 0);
                    dispatchThreads(stageThreads);

                    // 次の段が、この段が積んだ列とカウンタを読む
                    cmdList->BufferBarrier(m_BvhQueueBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                }
            }

            // ----------------------------------------
            // 4. バリア: Compute UAV → IndirectArgument（描画情報は頂点シェーダーが読む）
            // ----------------------------------------
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::IndirectArgument);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::IndirectArgument);
            cmdList->BufferBarrier(m_DrawInfoBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::GenericRead);
            if (cullPass == CULL_PASS_FIRST)
            {
                // 1パス目の読み取りを、2パス目の書き込みより前に済ませる
                cmdList->BufferBarrier(m_VisibilityBuffer,
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

        // 描画（GBuffer render pass をパスごとに1回だけ開き、材質の区間ごとに1回の間接描画を発行する）
        auto recordDraws = [&](const RHI::RenderPassPtr &renderPass,
                               const RHI::FramebufferPtr &framebuffer,
                               uint32_t passIndex) -> void
        {
            ScopedGpuTimestamp drawTimestamp(cmdList, passIndex == 0 ? "MegaGeometryDraw1" : "MegaGeometryDraw2");

            cmdList->BeginRenderPass(renderPass, framebuffer);
            cmdList->SetViewport(command.Viewport);
            cmdList->SetScissor(command.Scissor);
            cmdList->SetPipeline(SelectDrawPipeline(command.DebugMode));

            for (uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
            {
                const Section &section = sections[sectionIndex];
                if (section.Capacity == 0)
                {
                    continue;
                }

                cmdList->SetDescriptorSet(frameSlot.Sections[sectionIndex].DescriptorSet, 0);

                // 頂点/インデックスバッファ設定（プールの塊の先頭から。メッシュの位置はコマンドの基点が持つ）
                const auto *gpuData = section.Representative;
                cmdList->SetVertexBuffer(gpuData->VertexBuffer, 0, 0);
                cmdList->SetIndexBuffer(gpuData->IndexBuffer, 0);

                // IndirectDraw発行
                // DrawIndirectCount対応の場合はGPU側カウントを参照し、
                // 実際に可視なクラスタ数だけドローコールを発行する。
                // 非対応の場合は区間のコマンドの最大数をそのまま使用（積まれなかった分は instanceCount=0 で空振り）。
                const uint64_t commandOffsetBytes =
                    (static_cast<uint64_t>(passIndex) * commandsPerPass + section.CommandBase) * IndirectCommandBytes;
                if (caps.bDrawIndirectCount)
                {
                    cmdList->DrawIndexedIndirectCount(
                        m_IndirectDrawBuffer, commandOffsetBytes,
                        m_DrawCountBuffer, static_cast<uint64_t>(passIndex * sectionCount + sectionIndex) * sizeof(uint32_t),
                        section.Capacity,
                        IndirectCommandBytes);
                }
                else
                {
                    cmdList->DrawIndexedIndirect(
                        m_IndirectDrawBuffer, commandOffsetBytes,
                        section.Capacity,
                        IndirectCommandBytes);
                }
            }

            cmdList->EndRenderPass();
        };

        if (!bTwoPass)
        {
            // 従来の経路: 全インスタンスを1回の判定（遮蔽の判定なし）で選び、1回の render pass で描く
            recordCull(CULL_PASS_SINGLE, nullptr);
            recordDraws(m_GBufferRenderPass, m_GBufferFramebuffer, 0);
        }
        else
        {
            // 1パス目: 前のフレームで見えたクラスタだけを描く。前のフレームの2パス目の書き込みを見せる
            cmdList->BufferBarrier(m_VisibilityBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::UnorderedAccess);
            recordCull(CULL_PASS_FIRST, nullptr);
            recordDraws(m_GBufferRenderPass, m_GBufferFramebuffer, 0);

            // GBufferPass の不透明＋1パス目の深度から HZB を作る（深度は1パス目の終わりで ShaderResource）。
            // 作れなかったときは遮蔽の判定をしない（1パス目で描かなかったクラスタを全て描く）
            const bool bHiZBuilt = m_HiZ.Build(cmdList, m_DepthTexture);

            // 1パス目が描いたクラスタの使用の印（ページの表の要求の印と要求の列）を、2パス目の書き込みへ見せる。
            // 同じフレームの印での重複の省略と、列の件数の加算が、パスをまたいで続くため
            if (pageRequestCapacity != 0)
            {
                cmdList->BufferBarrier(frameSlot.PageTableBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                cmdList->BufferBarrier(pageRequestBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            }

            // 2パス目: 判定を通った全クラスタを HZB で判定し、1パス目で描かなかった見えるものを描く
            recordCull(CULL_PASS_SECOND, bHiZBuilt ? m_HiZ.GetTexture() : RHI::TexturePtr{});
            recordDraws(m_SecondGBufferRenderPass, m_SecondGBufferFramebuffer, 1);

            if (statsSlot)
            {
                // 統計のシェーダーの書き込みを、ホストの読み取りへ見せる
                cmdList->BufferBarrier(statsSlot->Buffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::HostRead);
            }
        }

        // ページの要求の列へのシェーダーの書き込みを、ホストの読み取りへ見せる（数フレーム後に読み戻す）
        if (pageRequestCapacity != 0)
        {
            command.MegaGeometry->RecordPageRequestHostBarrier(*cmdList);
        }

        // BVH のたどりの列・カウンタを次のフレーム用に戻す
        if (bBvhBuffersUsed)
        {
            cmdList->BufferBarrier(m_BvhQueueBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
            cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }

        // IndirectDrawバッファ・カウンタ・描画情報を次のフレーム用に戻す
        cmdList->BufferBarrier(m_IndirectDrawBuffer,
                               RHI::ResourceState::IndirectArgument,
                               RHI::ResourceState::Common);
        cmdList->BufferBarrier(m_DrawCountBuffer,
                               RHI::ResourceState::IndirectArgument,
                               RHI::ResourceState::Common);
        cmdList->BufferBarrier(m_DrawInfoBuffer,
                               RHI::ResourceState::GenericRead,
                               RHI::ResourceState::Common);

        ReleaseStaleBuffers();
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

    namespace
    {
        uint32_t NextPowerOfTwo(uint32_t value)
        {
            if (value <= 1u)
            {
                return 1u;
            }
            --value;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
            return value + 1u;
        }
    } // namespace

    // ========================================
    // カリング用GPUリソース作成
    // ========================================

    bool MegaGeometryPass::CreateCullResources(RHI::IDevice *device)
    {
        if (!device)
        {
            return false;
        }

        // IndirectDrawコマンド・区間のカウンタ・描画情報（足りなくなったら EnsureBatchBuffers が作り直す）
        constexpr uint32_t InitialCommandCapacity = 4096;
        constexpr uint32_t InitialCounterCapacity = 64;
        if (!EnsureBatchBuffers(InitialCommandCapacity, InitialCounterCapacity))
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

        // BVH のたどりの列・カウンタ（平らな判定のディスクリプタセットも binding 9・10 に結ぶので、常に作っておく）
        return EnsureBvhBuffers(BvhMinQueueEntries);
    }

    bool MegaGeometryPass::EnsureBvhBuffers(uint32_t queueEntries)
    {
        if (!m_Device)
        {
            return false;
        }

        if (m_BvhQueueBuffer && m_BvhCounterBuffer && queueEntries <= m_BvhQueueCapacity)
        {
            return true;
        }

        const uint32_t newCapacity = NextPowerOfTwo(std::max({queueEntries, m_BvhQueueCapacity, BvhMinQueueEntries}));
        RHI::BufferDesc queueDesc(static_cast<uint64_t>(newCapacity) * 2u * sizeof(uint32_t),
                                  RHI::ResourceUsage::StorageBuffer,
                                  false,
                                  "MegaGeometry_BvhQueue");
        RHI::BufferPtr queueBuffer = m_Device->CreateBuffer(queueDesc);
        RHI::BufferPtr counterBuffer = m_BvhCounterBuffer;
        if (!counterBuffer)
        {
            RHI::BufferDesc counterDesc(BvhCounterBytes,
                                        RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                                        false,
                                        "MegaGeometry_BvhCounters");
            counterBuffer = m_Device->CreateBuffer(counterDesc);
        }
        if (!queueBuffer || !counterBuffer)
        {
            return false;
        }

        // 古い列は、直前のフレームのGPUがまだ使っているかもしれないので、しばらく保持してから破棄する
        if (m_BvhQueueBuffer)
        {
            m_RetiredBuffers.push_back(RetiredBuffer{m_BvhQueueBuffer, m_OcclusionFrameCount});
        }
        m_BvhQueueBuffer = queueBuffer;
        m_BvhCounterBuffer = counterBuffer;
        m_BvhQueueCapacity = newCapacity;
        return true;
    }

    bool MegaGeometryPass::EnsureBvhStageResources(FrameSlot &slot, uint32_t passIndex, uint32_t stageCount)
    {
        if (!m_Device)
        {
            return false;
        }

        const RHI::DescriptorSetDesc cullDsDesc = BuildCullDescriptorSetDesc();
        while (slot.BvhStages[passIndex].size() < stageCount)
        {
            BvhStageDraw stageDraw;
            RHI::BufferDesc desc(sizeof(CullUniformData),
                                 RHI::ResourceUsage::ConstantBuffer,
                                 true,
                                 "MegaGeometry_BvhCullUBO");
            stageDraw.Uniform = m_Device->CreateBuffer(desc);
            stageDraw.DescriptorSet = m_Device->CreateDescriptorSet(cullDsDesc);
            if (!stageDraw.Uniform || !stageDraw.DescriptorSet)
            {
                return false;
            }
            slot.BvhStages[passIndex].push_back(stageDraw);
        }
        return true;
    }

    bool MegaGeometryPass::EnsureBatchBuffers(uint32_t commandCapacity, uint32_t counterCapacity)
    {
        if (!m_Device)
        {
            return false;
        }

        if (m_IndirectDrawBuffer && m_DrawCountBuffer && m_DrawInfoBuffer &&
            commandCapacity <= m_CommandCapacity && counterCapacity <= m_CounterCapacity)
        {
            return true;
        }

        const uint32_t newCommandCapacity = NextPowerOfTwo(std::max(commandCapacity, m_CommandCapacity));
        const uint32_t newCounterCapacity = NextPowerOfTwo(std::max(counterCapacity, m_CounterCapacity));

        // IndirectDrawコマンドバッファ (SSBO + IndirectBuffer)
        RHI::BufferDesc indirectDesc(
            static_cast<uint64_t>(newCommandCapacity) * sizeof(MegaGeometry::DrawIndexedIndirectCommand),
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer | RHI::ResourceUsage::TransferDst,
            false,
            "MegaGeometry_IndirectDraw");
        RHI::BufferPtr indirectBuffer = m_Device->CreateBuffer(indirectDesc);

        // 区間ごとのカウンタ（atomic counter用 SSBO。DrawIndexedIndirectCount のカウントバッファも兼ねる）
        RHI::BufferDesc countDesc(
            static_cast<uint64_t>(newCounterCapacity) * sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer | RHI::ResourceUsage::TransferDst,
            false,
            "MegaGeometry_DrawCount");
        RHI::BufferPtr countBuffer = m_Device->CreateBuffer(countDesc);

        // コマンドごとの描画情報（インスタンスの番号・payload）。カリングが書き、頂点シェーダーが読む
        RHI::BufferDesc drawInfoDesc(
            static_cast<uint64_t>(newCommandCapacity) * 2u * sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer,
            false,
            "MegaGeometry_DrawInfo");
        RHI::BufferPtr drawInfoBuffer = m_Device->CreateBuffer(drawInfoDesc);

        if (!indirectBuffer || !countBuffer || !drawInfoBuffer)
        {
            return false;
        }

        // 古いバッファは、直前のフレームのGPUがまだ使っているかもしれないので、しばらく保持してから破棄する
        for (RHI::BufferPtr *old : {&m_IndirectDrawBuffer, &m_DrawCountBuffer, &m_DrawInfoBuffer})
        {
            if (*old)
            {
                m_RetiredBuffers.push_back(RetiredBuffer{*old, m_OcclusionFrameCount});
            }
        }
        m_IndirectDrawBuffer = indirectBuffer;
        m_DrawCountBuffer = countBuffer;
        m_DrawInfoBuffer = drawInfoBuffer;
        m_CommandCapacity = newCommandCapacity;
        m_CounterCapacity = newCounterCapacity;
        return true;
    }

    RHI::DescriptorSetDesc MegaGeometryPass::BuildCullDescriptorSetDesc()
    {
        RHI::DescriptorSetDesc desc;
        auto addBinding = [&desc](uint32_t binding, RHI::ResourceBindType type) -> void
        {
            RHI::DescriptorBinding descriptorBinding;
            descriptorBinding.binding = binding;
            descriptorBinding.type = type;
            descriptorBinding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(descriptorBinding);
        };
        addBinding(0, RHI::ResourceBindType::ConstantBuffer);        // カリング用ユニフォーム
        addBinding(1, RHI::ResourceBindType::StructuredBuffer);      // インスタンスの表
        addBinding(2, RHI::ResourceBindType::RWBuffer);              // IndirectDrawコマンド
        addBinding(3, RHI::ResourceBindType::RWBuffer);              // 区間ごとのカウンタ
        addBinding(4, RHI::ResourceBindType::CombinedImageSampler);  // Hi-Z
        addBinding(5, RHI::ResourceBindType::RWBuffer);              // 「前のフレームで見えた」ビット
        addBinding(6, RHI::ResourceBindType::RWBuffer);              // 統計
        addBinding(7, RHI::ResourceBindType::StructuredBuffer);      // 区間の表
        addBinding(8, RHI::ResourceBindType::RWBuffer);              // 描画情報
        addBinding(9, RHI::ResourceBindType::RWBuffer);              // BVH のたどりの列
        addBinding(10, RHI::ResourceBindType::RWBuffer);             // BVH のたどりのカウンタ
        addBinding(11, RHI::ResourceBindType::RWBuffer);             // ページの表（常駐 + 要求の印）
        addBinding(12, RHI::ResourceBindType::RWBuffer);             // ページの要求の列
        return desc;
    }

    RHI::DescriptorSetDesc MegaGeometryPass::BuildDrawDescriptorSetDesc() const
    {
        // GBufferPassと同じ（set=0, binding 0=UBO, 1-6=textures）に、頂点シェーダーが引く
        // インスタンスの表（8）と描画情報（9）を足した形
        RHI::DescriptorSetDesc desc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
        desc.bindings.push_back(uboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(texBinding);
        }
        if (UsesVirtualTextureFeedbackBinding(m_Device))
        {
            AddVirtualTextureFeedbackBinding(desc, VirtualTextureFeedbackBindingIndex);
        }

        for (uint32_t binding : {8u, 9u})
        {
            RHI::DescriptorBinding bufferBinding;
            bufferBinding.binding = binding;
            bufferBinding.type = RHI::ResourceBindType::StructuredBuffer;
            bufferBinding.stages = RHI::ShaderStage::Vertex;
            desc.bindings.push_back(bufferBinding);
        }
        return desc;
    }

    bool MegaGeometryPass::EnsureFrameSlot(FrameSlot &slot,
                                           uint32_t instanceCount,
                                           uint32_t sectionTableEntries,
                                           uint32_t sectionCount)
    {
        if (!m_Device)
        {
            return false;
        }

        // 直前に使ったのは FrameSlotCount フレーム前で、そのGPUの仕事は終わっているので、作り直して置き換えてよい
        if (!slot.InstanceBuffer || slot.InstanceCapacity < instanceCount)
        {
            const uint32_t capacity = std::max(64u, NextPowerOfTwo(instanceCount));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * sizeof(GPUMegaInstance),
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "MegaGeometry_InstanceTable");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.InstanceBuffer = buffer;
            slot.InstanceCapacity = capacity;
        }

        if (!slot.SectionBuffer || slot.SectionCapacity < sectionTableEntries)
        {
            const uint32_t capacity = std::max(16u, NextPowerOfTwo(sectionTableEntries));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * 2u * sizeof(uint32_t),
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "MegaGeometry_SectionTable");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.SectionBuffer = buffer;
            slot.SectionCapacity = capacity;
        }

        const RHI::DescriptorSetDesc cullDsDesc = BuildCullDescriptorSetDesc();
        for (uint32_t passIndex = 0; passIndex < 2; ++passIndex)
        {
            if (!slot.CullUniform[passIndex])
            {
                RHI::BufferDesc desc(sizeof(CullUniformData),
                                     RHI::ResourceUsage::ConstantBuffer,
                                     true,
                                     "MegaGeometry_CullUBO");
                slot.CullUniform[passIndex] = m_Device->CreateBuffer(desc);
            }
            if (!slot.CullDescriptorSet[passIndex])
            {
                slot.CullDescriptorSet[passIndex] = m_Device->CreateDescriptorSet(cullDsDesc);
            }
            if (!slot.CullUniform[passIndex] || !slot.CullDescriptorSet[passIndex])
            {
                return false;
            }
        }

        if (slot.Sections.size() < sectionCount)
        {
            const RHI::DescriptorSetDesc drawDsDesc = BuildDrawDescriptorSetDesc();
            while (slot.Sections.size() < sectionCount)
            {
                RHI::BufferDesc desc(SectionUniformBufferBytes,
                                     RHI::ResourceUsage::ConstantBuffer,
                                     true,
                                     "MegaGeometry_DrawUBO");
                SectionDraw sectionDraw;
                sectionDraw.Uniform = m_Device->CreateBuffer(desc);
                sectionDraw.DescriptorSet = m_Device->CreateDescriptorSet(drawDsDesc);
                if (!sectionDraw.Uniform || !sectionDraw.DescriptorSet)
                {
                    return false;
                }
                slot.Sections.push_back(sectionDraw);
            }
        }

        return true;
    }

    bool MegaGeometryPass::SyncPageTable(FrameSlot &slot, MegaGeometryResources &resources)
    {
        if (!m_Device)
        {
            return false;
        }

        // バッファが無いスロットは、どの版とも一致しない版から始めて、必ず1回は書く
        uint64_t version = slot.PageTableBuffer ? slot.PageTableVersion : ~0ull;
        Container::VariableArray<MegaGeometry::GeometryPageTable::Entry> entries;
        const bool bChanged = resources.CopyPageTableIfChanged(version, entries);
        if (slot.PageTableBuffer && !bChanged)
        {
            return true;
        }

        // 直前に使ったのは FrameSlotCount フレーム前で、そのGPUの仕事は終わっているので、作り直して置き換えてよい
        const uint32_t entryCount = static_cast<uint32_t>(entries.size());
        if (!slot.PageTableBuffer || slot.PageTableCapacity < entryCount)
        {
            const uint32_t capacity = std::max(64u, NextPowerOfTwo(entryCount));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * sizeof(MegaGeometry::GeometryPageTable::Entry),
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "MegaGeometry_PageTable");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.PageTableBuffer = buffer;
            slot.PageTableCapacity = capacity;
        }
        if (entryCount > 0)
        {
            slot.PageTableBuffer->Update(entries.data(),
                                         static_cast<size_t>(entryCount) * sizeof(MegaGeometry::GeometryPageTable::Entry));
        }
        slot.PageTableVersion = version;
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

        // ディスクリプタセットレイアウト（GBufferPassの set=0, binding 0=UBO, 1-6=textures に、
        // インスタンスの表・描画情報を足した形）
        pipelineDesc.descriptorSetLayouts.push_back(BuildDrawDescriptorSetDesc());

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

        // 判定は保存される深度がNDCの深度そのもの（範囲が0〜1）であることを前提にする
        if (viewport.minDepth != 0.0f || viewport.maxDepth != 1.0f)
        {
            logFallbackOnce("depth_range");
            return false;
        }

        if (!m_HiZ.Resize(depthWidth, depthHeight))
        {
            logFallbackOnce("hiz");
            return false;
        }
        return true;
    }

    bool MegaGeometryPass::UpdateVisibilityLayout(RHI::ICommandList *commandList,
                                                  VariableArray<VisibilityRequest> &requests)
    {
        // 区画はインスタンスの並びで詰める
        uint64_t totalElements = 0;
        for (VisibilityRequest &request : requests)
        {
            request.Offset = static_cast<uint32_t>(totalElements);
            totalElements += request.ClusterCount;
        }
        if (totalElements == 0 || totalElements > 0x3FFFFFFFull || !m_Device || !commandList)
        {
            return false;
        }

        // 直前のフレームに描かれていたインスタンスだけが、見えたビットを引き継げる。
        // 1フレームでも描かれなかった（スナップショットから外れた・メッシュが無かった・2パスでなかった）インスタンスや、
        // コンポーネントが作り直されたインスタンスは、同じ ObjectId・メッシュでも別物として0から始める。
        // クラスタのバッファは全メッシュが共有するプールの塊なので、区画の位置も一致の条件に入れる
        auto isContinuing = [this](const VisibilityEntry &entry, const VisibilityRequest &request) -> bool
        {
            return !m_bDropVisibilityContinuity && entry.LastUsedFrame + 1 == m_OcclusionFrameCount &&
                   entry.ComponentId == request.ComponentId &&
                   entry.MeshId == request.MeshId &&
                   entry.ClusterBufferIdentity == request.ClusterBufferIdentity &&
                   entry.ClusterBufferOffsetBytes == request.ClusterBufferOffsetBytes &&
                   entry.ClusterCount == request.ClusterCount;
        };
        auto toEntry = [this](const VisibilityRequest &request) -> VisibilityEntry
        {
            VisibilityEntry entry;
            entry.Key = request.Key;
            entry.MeshId = request.MeshId;
            entry.ComponentId = request.ComponentId;
            entry.ClusterBufferIdentity = request.ClusterBufferIdentity;
            entry.ClusterBufferOffsetBytes = request.ClusterBufferOffsetBytes;
            entry.ClusterCount = request.ClusterCount;
            entry.Offset = request.Offset;
            entry.LastUsedFrame = m_OcclusionFrameCount;
            return entry;
        };

        // 配置（鍵とクラスタ数の並び）が前のフレームと同じか
        bool bSameLayout = m_VisibilityBuffer && m_VisibilityEntries.size() == requests.size();
        for (size_t index = 0; bSameLayout && index < requests.size(); ++index)
        {
            bSameLayout = m_VisibilityEntries[index].Key == requests[index].Key &&
                          m_VisibilityEntries[index].ClusterCount == requests[index].ClusterCount;
        }

        if (bSameLayout)
        {
            // 配置は同じ。引き継げないインスタンス（作り直し・メッシュの差し替え・途切れ）の区画だけ0に戻す
            bool bClearing = false;
            for (size_t index = 0; index < requests.size(); ++index)
            {
                if (!isContinuing(m_VisibilityEntries[index], requests[index]))
                {
                    if (!bClearing)
                    {
                        commandList->BufferBarrier(m_VisibilityBuffer,
                                                   RHI::ResourceState::UnorderedAccess,
                                                   RHI::ResourceState::CopyDest);
                        bClearing = true;
                    }
                    commandList->FillBuffer(m_VisibilityBuffer,
                                            static_cast<uint64_t>(requests[index].Offset) * sizeof(uint32_t),
                                            static_cast<uint64_t>(requests[index].ClusterCount) * sizeof(uint32_t),
                                            0);
                }
            }
            if (bClearing)
            {
                commandList->BufferBarrier(m_VisibilityBuffer,
                                           RHI::ResourceState::CopyDest,
                                           RHI::ResourceState::UnorderedAccess);
            }
            for (size_t index = 0; index < requests.size(); ++index)
            {
                m_VisibilityEntries[index] = toEntry(requests[index]);
            }
            return true;
        }

        // 配置が変わった（インスタンスの追加・削除・並びの変更・メッシュの差し替え）: ぴったりの大きさで作り直し、
        // 0で埋めてから、引き継げるインスタンスの区画だけを古いバッファから写す
        RHI::BufferDesc desc(totalElements * sizeof(uint32_t),
                             RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst |
                                 RHI::ResourceUsage::TransferSrc,
                             false,
                             "MegaGeometry_VisibleLastFrame");
        RHI::BufferPtr newBuffer = m_Device->CreateBuffer(desc);
        if (!newBuffer)
        {
            return false;
        }

        // 古い配置を鍵の順に引けるようにする
        VariableArray<uint32_t> oldOrder;
        oldOrder.reserve(m_VisibilityEntries.size());
        for (uint32_t index = 0; index < m_VisibilityEntries.size(); ++index)
        {
            oldOrder.push_back(index);
        }
        std::sort(oldOrder.begin(), oldOrder.end(),
                  [this](uint32_t left, uint32_t right) -> bool
                  { return m_VisibilityEntries[left].Key < m_VisibilityEntries[right].Key; });

        struct CopyRegion
        {
            uint64_t SourceOffsetBytes = 0;
            uint64_t DestinationOffsetBytes = 0;
            uint64_t SizeBytes = 0;
        };
        VariableArray<CopyRegion> copies;
        if (m_VisibilityBuffer)
        {
            for (const VisibilityRequest &request : requests)
            {
                size_t low = 0;
                size_t high = oldOrder.size();
                while (low < high)
                {
                    const size_t mid = low + (high - low) / 2;
                    if (m_VisibilityEntries[oldOrder[mid]].Key < request.Key)
                    {
                        low = mid + 1;
                    }
                    else
                    {
                        high = mid;
                    }
                }
                if (low < oldOrder.size())
                {
                    const VisibilityEntry &old = m_VisibilityEntries[oldOrder[low]];
                    if (old.Key == request.Key && isContinuing(old, request))
                    {
                        CopyRegion region;
                        region.SourceOffsetBytes = static_cast<uint64_t>(old.Offset) * sizeof(uint32_t);
                        region.DestinationOffsetBytes = static_cast<uint64_t>(request.Offset) * sizeof(uint32_t);
                        region.SizeBytes = static_cast<uint64_t>(request.ClusterCount) * sizeof(uint32_t);
                        copies.push_back(region);
                    }
                }
            }
        }

        commandList->BufferBarrier(newBuffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
        commandList->FillBuffer(newBuffer, 0, totalElements * sizeof(uint32_t), 0);
        if (!copies.empty())
        {
            // 前のフレームの2パス目の書き込みを、コピー元として見せる。0埋めの後にコピーが書く
            commandList->BufferBarrier(m_VisibilityBuffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::CopySource);
            commandList->BufferBarrier(newBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::CopyDest);
            for (const CopyRegion &region : copies)
            {
                commandList->CopyBuffer(m_VisibilityBuffer, newBuffer, region.SizeBytes,
                                        region.SourceOffsetBytes, region.DestinationOffsetBytes);
            }
        }
        commandList->BufferBarrier(newBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);

        // 古いバッファは、GPUが使い終わるまで保持してから破棄する
        if (m_VisibilityBuffer)
        {
            m_RetiredBuffers.push_back(RetiredBuffer{m_VisibilityBuffer, m_OcclusionFrameCount});
        }
        m_VisibilityBuffer = newBuffer;
        m_VisibilityEntries.clear();
        m_VisibilityEntries.reserve(requests.size());
        for (const VisibilityRequest &request : requests)
        {
            m_VisibilityEntries.push_back(toEntry(request));
        }
        return true;
    }

    void MegaGeometryPass::ReleaseStaleBuffers()
    {
        // 手放したバッファは、一定のフレーム後にはGPUがとうに使い終わっているので破棄する
        for (size_t index = 0; index < m_RetiredBuffers.size();)
        {
            if (m_OcclusionFrameCount - m_RetiredBuffers[index].RetiredFrame > RetiredBufferFrames)
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
