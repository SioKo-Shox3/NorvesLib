#include "Rendering/VirtualShadowMapPass.h"

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Math/MatrixUtils.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VirtualShadowMapCasters.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "Rendering/VirtualShadowMapRaster.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"

namespace NorvesLib::Core::Rendering
{
    /**
     * 投影物の塊の記録を作る作業の状態。展開の出力（インスタンス・間接描画の引数）は GPU が書くので全 Execute で 1 つを使い回し、
     * 塊の記録はホストが書くので、フレームの枠（FrameUseRing）ごとに Execute のたびに別のバッファを使う
     * （同じフレームの複数のビューポートが、提出前の記録を上書きしない）。
     */
    struct VirtualShadowMapCasterState
    {
        struct Use
        {
            RHI::BufferPtr Chunks;
            uint32_t ChunkCapacity = 0;
        };

        FrameUseRing<Use> Uses;
        RHI::BufferPtr Instances;
        RHI::BufferPtr Draws;
        /** @brief 今回の Execute が集めた塊の記録と内訳 */
        Container::VariableArray<VsmShadowChunk> Chunks;
        VirtualShadowMap::CasterStats Stats;
        /** @brief 作業配列（毎フレーム容量を使い回す） */
        VirtualShadowMap::ProceduralPlanScratch PlanScratch;
        Container::VariableArray<VirtualShadowMap::ProceduralChunkPlan> Plan;
        Container::VariableArray<MeshIndexChunk> SkinnedChunks;
        /** @brief 最後に出した内訳（変わったときだけ出す）と、出してからの回数 */
        VirtualShadowMap::CasterStats LoggedStats;
        bool bLogged = false;
        uint32_t ReportsSinceLog = 0;
    };

    namespace
    {
        constexpr double BytesPerMegabyte = 1024.0 * 1024.0;
        /** @brief 投影物の内訳が変わらなくても VSM_CASTERS を出す間隔（Execute の回数） */
        constexpr uint32_t CasterLogIntervalExecutions = 60;

        // スキニングの描画の持ち主（元の描画の SourceMeshComponentId と同じ ComponentId を持つプロキシ）。無ければ null
        const SkinnedMeshProxy* FindSkinnedProxy(const Container::VariableArray<SkinnedMeshProxy>& proxies, uint64_t componentId)
        {
            if (componentId == 0u)
            {
                return nullptr;
            }
            for (const SkinnedMeshProxy& proxy : proxies)
            {
                if (proxy.ComponentId == componentId)
                {
                    return &proxy;
                }
            }
            return nullptr;
        }

        // アニメーション後の境界（ローカル）にワールド行列をかけた、描画のワールドの境界。境界か行列が有限でなければ false
        bool BuildSkinnedWorldBounds(const SkinnedMeshProxy& proxy, VirtualShadowMap::CasterBounds& outBounds)
        {
            if (!proxy.bHasAnimatedBounds)
            {
                return false;
            }
            const Math::Vector3 localMin = proxy.AnimatedBounds.Min;
            const Math::Vector3 localMax = proxy.AnimatedBounds.Max;
            if (!std::isfinite(localMin.x) || !std::isfinite(localMin.y) || !std::isfinite(localMin.z) || !std::isfinite(localMax.x) ||
                !std::isfinite(localMax.y) || !std::isfinite(localMax.z) || localMin.x > localMax.x || localMin.y > localMax.y ||
                localMin.z > localMax.z)
            {
                return false;
            }
            for (const float value : proxy.WorldTransform.values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            const Math::Vector3 center = Math::MatrixUtils::TransformPointRowVector(proxy.WorldTransform, proxy.AnimatedBounds.Center());
            const Math::Vector3 half = Math::MatrixUtils::AbsUpper3x3TransformExtentsRowVector(proxy.WorldTransform, proxy.AnimatedBounds.HalfExtents());
            outBounds.Min[0] = center.x - half.x;
            outBounds.Min[1] = center.y - half.y;
            outBounds.Min[2] = center.z - half.z;
            outBounds.Max[0] = center.x + half.x;
            outBounds.Max[1] = center.y + half.y;
            outBounds.Max[2] = center.z + half.z;
            return true;
        }

        // 資源 1 つ分: 名前・バッファ・グラフに取り込んだ資源の控え
        struct DeclaredBuffer
        {
            Identity Name;
            const RHI::BufferPtr* Buffer = nullptr;
            RGResourceHandle* Handle = nullptr;
            const char* DebugName = nullptr;
        };
    } // namespace

    VirtualShadowMapPass::VirtualShadowMapPass(uint32_t requestedPoolPages)
        : m_RequestedPoolPages(requestedPoolPages)
    {
    }

    VirtualShadowMapPass::~VirtualShadowMapPass()
    {
        Shutdown();
    }

    void VirtualShadowMapPass::Fallback(VirtualShadowMap::FallbackReason reason)
    {
        ReleaseResources();
        m_bActive = false;
        m_FallbackReason = reason;
        // 照明は CSM のまま描く。理由は 1 回だけ出す（Initialize が 1 回しか呼ばない）
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VSM_FALLBACK reason=%s",
                        VirtualShadowMap::FallbackReasonName(reason));
    }

    void VirtualShadowMapPass::ReleaseResources()
    {
        m_Pool.reset();
        m_PageTable.reset();
        m_RequestBits.reset();
        m_FreeList.reset();
        m_Stats.reset();
        m_DirtyList.reset();
        m_Pages.reset();
        m_RasterReporter.reset();
        if (m_Raster)
        {
            m_Raster->Shutdown();
        }
        m_Raster.reset();
        m_Casters.reset();
        for (StatsSlot& slot : m_StatsSlots)
        {
            slot = StatsSlot{};
        }
        m_PoolPages = 0;
        if (m_Gpu)
        {
            m_Gpu->SetShadowMapPoolBytes(0);
        }
    }

    bool VirtualShadowMapPass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        m_Gpu = context.Resources.Gpu;
        m_bActive = false;
        m_FallbackReason = VirtualShadowMap::FallbackReason::None;
        m_bInitialFilled = false;
        if (!m_Device)
        {
            // デバイスが無いときは資源を作れない。描画全体は止めない
            Fallback(VirtualShadowMap::FallbackReason::PoolSize);
            m_bInitialized = true;
            return true;
        }

        const VirtualShadowMap::PoolPlan plan = VirtualShadowMap::PlanPool(m_Device->GetCapabilities(), m_RequestedPoolPages);
        if (!plan.IsSupported())
        {
            Fallback(plan.Reason);
            m_bInitialized = true;
            return true;
        }

        // 印付け・割り当て・消去の計算パイプライン。作れなければ VSM を使わず CSM で描く
        m_Pages = Container::MakeUnique<VirtualShadowMapPages>();
        if (!m_Pages->Initialize(m_Device, context.ShaderMgr))
        {
            Fallback(VirtualShadowMap::FallbackReason::Pipeline);
            m_bInitialized = true;
            return true;
        }

        // 展開・描画のパイプライン。作れなければ（装置が firstInstance の間接描画を使えないときを含む）VSM を使わず CSM で描く
        m_Raster = Container::MakeUnique<VirtualShadowMapRaster>();
        if (!m_Raster->Initialize(m_Device, context.ShaderMgr))
        {
            Fallback(VirtualShadowMap::FallbackReason::Pipeline);
            m_bInitialized = true;
            return true;
        }
        m_Casters = Container::MakeUnique<VirtualShadowMapCasterState>();

        m_RasterReporter = Container::MakeUnique<VirtualShadowMapRasterStatsReporter>();

        const uint64_t poolBytes = VirtualShadowMap::PoolBytes(plan.Pages);
        const uint64_t freeListBytes = VirtualShadowMap::FreeListBytes(plan.Pages);
        const RHI::ResourceUsage storageUsage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst;
        const RHI::ResourceUsage statsUsage = VirtualShadowMap::StatsBufferUsage();

        // 大きな確保は失敗しうる（装置のメモリ不足・上限）。作れなければ VSM を使わず CSM で描く
        bool bCreated = false;
        try
        {
            m_Pool = m_Device->CreateBuffer(RHI::BufferDesc(
                poolBytes, storageUsage | RHI::ResourceUsage::BufferDeviceAddress, false, "VSM_PhysicalPool"));
            m_PageTable = m_Device->CreateBuffer(
                RHI::BufferDesc(VirtualShadowMap::PageTableBytes(), storageUsage, false, "VSM_PageTable"));
            m_RequestBits = m_Device->CreateBuffer(
                RHI::BufferDesc(VirtualShadowMap::RequestBitsBytes(), storageUsage, false, "VSM_RequestBits"));
            m_FreeList = m_Device->CreateBuffer(RHI::BufferDesc(freeListBytes, storageUsage, false, "VSM_FreeList"));
            m_Stats = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::STATS_BYTES, statsUsage, false, "VSM_Stats"));
            // 消去するページの一覧は、間接 dispatch の引数としても読まれる
            m_DirtyList = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::DirtyListBytes(plan.Pages),
                                                                 storageUsage | RHI::ResourceUsage::IndirectBuffer,
                                                                 false,
                                                                 "VSM_DirtyList"));
            // 展開の出力（GPU が書く）。投影物の塊の記録は、描くフレームで必要な大きさに合わせて作る
            m_Casters->Instances = m_Device->CreateBuffer(RHI::BufferDesc(
                VirtualShadowMap::RasterInstanceBytes(VirtualShadowMap::RASTER_INSTANCE_CAPACITY), VirtualShadowMap::RasterInstanceUsage(), false, "VsmRaster_Instances"));
            m_Casters->Draws = m_Device->CreateBuffer(RHI::BufferDesc(
                VirtualShadowMap::RasterDrawBytes(VirtualShadowMap::MAX_CASTER_CHUNKS), VirtualShadowMap::RasterDrawUsage(), false, "VsmRaster_Draws"));
            bCreated = m_Pool && m_PageTable && m_RequestBits && m_FreeList && m_Stats && m_DirtyList && m_Casters->Instances && m_Casters->Draws;
        }
        catch (...)
        {
            bCreated = false;
        }
        if (!bCreated)
        {
            Fallback(VirtualShadowMap::FallbackReason::PoolSize);
            m_bInitialized = true;
            return true;
        }

        // 空きページの一覧: 先頭が数（全ページ）、続いて空きページの番号（0 〜 pages-1）。最初は全ページが空き
        {
            Container::VariableArray<uint32_t> freeList;
            freeList.resize(static_cast<size_t>(plan.Pages) + 1u);
            freeList[0] = plan.Pages;
            for (uint32_t page = 0; page < plan.Pages; ++page)
            {
                freeList[static_cast<size_t>(page) + 1u] = page;
            }
            m_FreeList->Update(freeList.data(), freeListBytes);
        }

        // 統計の読み戻し先（host-visible）。作れない・写像できない装置では読み戻さない（記録は続く）
        for (StatsSlot& slot : m_StatsSlots)
        {
            slot.Buffer = m_Device->CreateBuffer(
                RHI::BufferDesc(VirtualShadowMap::STATS_BYTES, VirtualShadowMap::StatsReadbackUsage(), true, "VSM_StatsReadback"));
            slot.Mapped = slot.Buffer ? static_cast<const uint32_t*>(slot.Buffer->Map(0, 0)) : nullptr;
        }

        m_PoolPages = plan.Pages;
        m_bActive = true;
        const uint64_t rasterBytes = VirtualShadowMap::RasterInstanceBytes(VirtualShadowMap::RASTER_INSTANCE_CAPACITY) +
                                     VirtualShadowMap::RasterDrawBytes(VirtualShadowMap::MAX_CASTER_CHUNKS);
        if (m_Gpu)
        {
            m_Gpu->SetShadowMapPoolBytes(poolBytes + rasterBytes);
        }

        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_pool pages=%u mb=%.3f",
                        plan.Pages,
                        static_cast<double>(poolBytes) / BytesPerMegabyte);
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_page_table mb=%.3f",
                        static_cast<double>(VirtualShadowMap::PageTableBytes()) / BytesPerMegabyte);
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_raster mb=%.3f",
                        static_cast<double>(rasterBytes) / BytesPerMegabyte);
        m_bInitialized = true;
        return true;
    }

    void VirtualShadowMapPass::Shutdown()
    {
        ReleaseResources();
        m_PoolHandle = {};
        m_PageTableHandle = {};
        m_RequestBitsHandle = {};
        m_FreeListHandle = {};
        m_StatsHandle = {};
        m_DirtyListHandle = {};
        m_SkinnedVerticesHandle = {};
        m_DepthHandle = {};
        m_bDeclared = false;
        m_bActive = false;
        m_bInitialFilled = false;
        m_bMarked = false;
        m_bRasterRecorded = false;
        m_LastCasterChunkCount = 0;
        m_bStatsLogged = false;
        m_FramesSinceStatsLog = 0;
        m_Device = nullptr;
        m_Gpu = nullptr;
        m_bInitialized = false;
    }

    void VirtualShadowMapPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void VirtualShadowMapPass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void VirtualShadowMapPass::Declare(RenderGraphBuilder& builder)
    {
        m_PoolHandle = {};
        m_PageTableHandle = {};
        m_RequestBitsHandle = {};
        m_FreeListHandle = {};
        m_StatsHandle = {};
        m_DirtyListHandle = {};
        m_SkinnedVerticesHandle = {};
        m_DepthHandle = {};
        m_bDeclared = false;

        // 作れなかった構成（装置の非対応・確保の失敗）では何も宣言しない
        if (!m_bActive)
        {
            return;
        }

        // 深度が確定した後に並べるため、GBuffer の深度を読む（印付けの入力）・法線を読む（並びのための依存。中身は読まない）。
        // 無い構成（GBuffer を作らない）では読まず、資源の公開だけをする（印付けはしない）
        RGTextureHandle depth;
        RGTextureHandle normal;
        if (builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, depth) &&
            builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth, depth, RHI::ResourceState::ShaderResource))
        {
            m_DepthHandle = depth;
        }
        if (builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normal))
        {
            builder.TryReadTexture(RenderGraphResourceNames::GBufferNormal, normal, RHI::ResourceState::ShaderResource);
        }

        // 永続のバッファを取り込み、名前で公開する。状態の遷移は、実行の中で明示的に（Common ↔ CopyDest）行う
        const DeclaredBuffer buffers[] = {
            {RenderGraphResourceNames::VsmPhysicalPool, &m_Pool, &m_PoolHandle, "VSM_PhysicalPool"},
            {RenderGraphResourceNames::VsmPageTable, &m_PageTable, &m_PageTableHandle, "VSM_PageTable"},
            {RenderGraphResourceNames::VsmRequestBits, &m_RequestBits, &m_RequestBitsHandle, "VSM_RequestBits"},
            {RenderGraphResourceNames::VsmFreeList, &m_FreeList, &m_FreeListHandle, "VSM_FreeList"},
            {RenderGraphResourceNames::VsmStats, &m_Stats, &m_StatsHandle, "VSM_Stats"},
            {RenderGraphResourceNames::VsmDirtyList, &m_DirtyList, &m_DirtyListHandle, "VSM_DirtyList"},
        };
        bool bAllPublished = true;
        for (const DeclaredBuffer& declared : buffers)
        {
            *declared.Handle = builder.ImportBuffer(*declared.Buffer, RHI::ResourceState::Common, declared.DebugName);
            if (!declared.Handle->IsValid())
            {
                bAllPublished = false;
                continue;
            }
            builder.Write(*declared.Handle, RHI::ResourceState::Common, RHI::ResourceState::Common);
            bAllPublished = builder.PublishBuffer(declared.Name, *declared.Handle) && bAllPublished;
        }
        m_bDeclared = bAllPublished;

        // スキニングの変形した頂点は、投影物の描画が（記録の頂点のアドレスから）読む。SkinningComputePass が、このフレームに
        // 変形するインスタンスを持つときだけ宣言する（名前を持たないフレームに読むと、グラフのエラーになる）。変形の後に並べ、読める状態にする
        if (m_SkinningPass)
        {
            const RGResourceHandle skinnedVertices = m_SkinningPass->GetCurrentVerticesHandle();
            if (skinnedVertices.IsValid())
            {
                builder.Read(skinnedVertices, RHI::ResourceState::GenericRead);
                m_SkinnedVerticesHandle = skinnedVertices;
            }
        }
        builder.PreserveInsertionOrder();
    }

    void VirtualShadowMapPass::CollectCasters(ViewRenderContext& context)
    {
        VirtualShadowMapCasterState& state = *m_Casters;
        state.Chunks.clear();
        state.Stats = {};
        const VirtualShadowMapClipmap& clipmap = context.PhysicalLighting.SunClipmap;
        if (!clipmap.bEnabled)
        {
            return;
        }

        // ----- 手続きメッシュ: 影を落とす DrawCommand（CSM と同じ集め方。主カメラの錐台で省かれた物も含める） -----
        MeshResources* meshes = context.Resources.Meshes;
        const Container::VariableArray<GPUSceneInstanceData>* instanceData = context.SnapshotInstanceData;
        if (meshes && instanceData)
        {
            for (const DrawCommand& command : context.GetActiveDrawCommands())
            {
                const DrawParams& draw = command.Draw;
                if (!draw.bCastShadow || draw.PayloadKind != DrawPayloadKind::Mesh || !draw.MeshHandle.IsValid())
                {
                    continue;
                }
                const auto* gpuData = meshes->GetGPUData(draw.MeshHandle);
                if (!gpuData || !gpuData->VertexBuffer || !gpuData->IndexBuffer)
                {
                    continue;
                }

                // GBuffer・ビジビリティバッファの経路と同じ範囲の選び方
                const bool bHasRange = draw.IndexCount > 0;
                VirtualShadowMap::ProceduralDrawInput input;
                input.VertexAddress = gpuData->VertexBuffer->GetDeviceAddress();
                input.IndexAddress = gpuData->IndexBuffer->GetDeviceAddress();
                input.FirstIndex = bHasRange ? draw.IndexOffset : 0u;
                input.IndexCount = bHasRange ? draw.IndexCount : gpuData->IndexCount;
                input.VertexOffset = bHasRange ? draw.VertexOffset : 0u;
                input.MeshBounds = gpuData->bHasLocalBounds ? &gpuData->LocalBounds : nullptr;
                input.BlockBounds = gpuData->BlockBounds.empty() ? nullptr : gpuData->BlockBounds.data();
                input.BlockBoundsCount = static_cast<uint32_t>(gpuData->BlockBounds.size());
                if (!VirtualShadowMap::PlanProceduralChunks(input, state.PlanScratch, state.Plan))
                {
                    ++state.Stats.SkippedDraws;
                    continue;
                }

                const uint32_t instanceCount = std::max(1u, draw.InstanceCount);
                for (uint32_t instanceOffset = 0; instanceOffset < instanceCount; ++instanceOffset)
                {
                    const uint64_t instanceIndex = static_cast<uint64_t>(draw.FirstInstance) + instanceOffset;
                    if (instanceIndex >= instanceData->size())
                    {
                        ++state.Stats.SkippedDraws;
                        continue;
                    }
                    VirtualShadowMap::AppendProceduralInstance(
                        input, state.Plan, (*instanceData)[static_cast<size_t>(instanceIndex)].World, clipmap, state.Chunks, state.Stats);
                }
            }
        }

        // ----- スキニング: SkinningComputePass が変形した頂点（ワールド空間）。境界は描画の境界 -----
        if (m_SkinningPass && context.SnapshotSkinnedMeshProxies)
        {
            const Container::VariableArray<SkinningComputeInstance>& instances = m_SkinningPass->GetInstances();
            for (const SkinningComputeInstance& instance : instances)
            {
                if (!instance.IndexBuffer || instance.IndexCount < 3u)
                {
                    continue;
                }
                const uint64_t indexAddress = instance.IndexBuffer->GetDeviceAddress();
                const SkinnedMeshProxy* proxy = FindSkinnedProxy(*context.SnapshotSkinnedMeshProxies, instance.SourceMeshComponentId);
                if (proxy && !proxy->bCastShadow)
                {
                    continue;
                }
                VirtualShadowMap::CasterBounds bounds;
                if (instance.CurrentVertexAddress == 0u || indexAddress == 0u || !proxy || !BuildSkinnedWorldBounds(*proxy, bounds))
                {
                    ++state.Stats.SkippedDraws;
                    continue;
                }
                // 登録時に分けた塊を使い、無ければインデックスの全体を分ける
                Container::VariableArray<MeshIndexChunk>& chunks = state.SkinnedChunks;
                if (!context.SkinnedMeshes || !context.SkinnedMeshes->TryGetChunks(instance.MeshHandle, chunks) || chunks.empty())
                {
                    if (!BuildMeshIndexChunks(instance.IndexCount, nullptr, 0, chunks))
                    {
                        ++state.Stats.SkippedDraws;
                        continue;
                    }
                }
                VirtualShadowMap::AppendSkinnedInstance(instance.CurrentVertexAddress, indexAddress, bounds, chunks, clipmap, state.Chunks, state.Stats);
            }
        }
    }

    void VirtualShadowMapPass::ReportCasters()
    {
        VirtualShadowMapCasterState& state = *m_Casters;
        const VirtualShadowMap::CasterStats& stats = state.Stats;
        // 何も集めていない間は出さない（投影物の無い構成を毎回ログで埋めない）。一度出したら、0 に戻ったときも出す
        const bool bAnything = stats.ProceduralChunks != 0u || stats.SkinnedChunks != 0u || stats.CulledChunks != 0u ||
                               stats.DroppedChunks != 0u || stats.SkippedDraws != 0u;
        if (!state.bLogged && !bAnything)
        {
            return;
        }
        ++state.ReportsSinceLog;
        if (state.bLogged && stats == state.LoggedStats && state.ReportsSinceLog < CasterLogIntervalExecutions)
        {
            return;
        }
        state.LoggedStats = stats;
        state.bLogged = true;
        state.ReportsSinceLog = 0;
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VSM_CASTERS procedural_chunks=%u skinned_chunks=%u culled=%u dropped=%u skipped=%u",
                        stats.ProceduralChunks,
                        stats.SkinnedChunks,
                        stats.CulledChunks,
                        stats.DroppedChunks,
                        stats.SkippedDraws);
    }

    bool VirtualShadowMapPass::RecordRaster(ViewRenderContext& context, uint64_t /*frameSerial*/)
    {
        VirtualShadowMapCasterState& state = *m_Casters;
        const uint32_t chunkCount = static_cast<uint32_t>(state.Chunks.size());
        if (chunkCount == 0u || chunkCount > VirtualShadowMap::MAX_CASTER_CHUNKS || !state.Instances || !state.Draws)
        {
            return false;
        }

        // 塊の記録は、この Execute 専用のバッファへ書く（ホストが書く。足りなければ 2 倍ずつ広げる）
        VirtualShadowMapCasterState::Use& use = state.Uses.Acquire();
        if (!use.Chunks || use.ChunkCapacity < chunkCount)
        {
            const uint32_t wanted = std::max(chunkCount, std::min(VirtualShadowMap::MAX_CASTER_CHUNKS, std::max(256u, use.ChunkCapacity * 2u)));
            use.Chunks = m_Device->CreateBuffer(
                RHI::BufferDesc(VirtualShadowMap::RasterChunkBytes(wanted), VirtualShadowMap::RasterChunkUsage(), true, "VsmRaster_Chunks"));
            use.ChunkCapacity = use.Chunks ? wanted : 0u;
            if (!use.Chunks)
            {
                return false;
            }
        }
        use.Chunks->Update(state.Chunks.data(), static_cast<uint64_t>(chunkCount) * sizeof(VsmShadowChunk));

        RHI::ICommandList* commandList = context.CommandList;
        const RHI::BufferPtr rasterBuffers[] = {use.Chunks, state.Instances, state.Draws};
        for (const RHI::BufferPtr& buffer : rasterBuffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        }

        VirtualShadowMapRasterDispatch rasterDispatch;
        rasterDispatch.Clipmap = &context.PhysicalLighting.SunClipmap;
        rasterDispatch.PoolPages = m_PoolPages;
        rasterDispatch.Pool = m_Pool;
        rasterDispatch.PageTable = m_PageTable;
        rasterDispatch.Stats = m_Stats;
        rasterDispatch.Chunks = use.Chunks;
        rasterDispatch.ChunkCount = chunkCount;
        rasterDispatch.Instances = state.Instances;
        rasterDispatch.Draws = state.Draws;
        const bool bRecorded = m_Raster->Record(commandList, rasterDispatch);

        for (const RHI::BufferPtr& buffer : rasterBuffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }
        return bRecorded;
    }

    void VirtualShadowMapPass::HarvestStats(StatsSlot& slot)
    {
        if (!slot.bPending || !slot.Mapped)
        {
            return;
        }
        slot.bPending = false;
        // 展開の統計（投影物を描かない間は 0 のままで、何も出さない）
        if (m_RasterReporter)
        {
            m_RasterReporter->Report(slot.Mapped[VirtualShadowMap::StatRasterChunks],
                                     slot.Mapped[VirtualShadowMap::StatRasterInstances],
                                     slot.Mapped[VirtualShadowMap::StatRasterOverflow]);
        }
        const uint32_t stats[4] = {slot.Mapped[VirtualShadowMap::StatRequested],
                                   slot.Mapped[VirtualShadowMap::StatAllocated],
                                   slot.Mapped[VirtualShadowMap::StatOverflow],
                                   slot.Mapped[VirtualShadowMap::StatLevelsUsed]};
        ++m_FramesSinceStatsLog;
        bool bChanged = !m_bStatsLogged;
        for (uint32_t index = 0; index < 4u; ++index)
        {
            bChanged = bChanged || stats[index] != m_LoggedStats[index];
        }
        if (!bChanged && m_FramesSinceStatsLog < StatsLogIntervalFrames)
        {
            return;
        }
        for (uint32_t index = 0; index < 4u; ++index)
        {
            m_LoggedStats[index] = stats[index];
        }
        m_bStatsLogged = true;
        m_FramesSinceStatsLog = 0;
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VSM_PAGES requested=%u allocated=%u overflow=%u levels_used=0x%x",
                        stats[0],
                        stats[1],
                        stats[2],
                        stats[3]);
    }

    void VirtualShadowMapPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_bMarked = false;
        m_bRasterRecorded = false;
        m_LastCasterChunkCount = 0;
        if (!m_bActive || !m_bDeclared || !m_Pages || !m_Raster || !m_Casters || !context.CommandList)
        {
            return;
        }

        RHI::ICommandList* commandList = context.CommandList;

        // 統計の枠は飛行中のフレームの番号ごと。スワップチェーンのフェンスが、同じ番号の前のフレームの GPU の完了を待ってから
        // 次のフレームを始めるので、通し番号が変わった最初の Execute では、その枠へ写した前のフレームの統計を安全に読める。
        // 同じフレームの Execute（複数のビューポート）は通し番号が同じなので、提出前の枠を読まない（Execute の回数では数えない）
        const uint64_t frameSerial = context.ResolveRenderFrameSerial();
        StatsSlot& slot = m_StatsSlots[context.FrameIndex % StatsSlotCount];
        if (slot.bPending && slot.FrameSerial != frameSerial)
        {
            HarvestStats(slot);
        }

        // 最初の実行で、何も無い texel の深度（1.0）でプールを埋める（以後は dirty のページだけを消去する）。
        // 表・要求・統計・空きページ・消去の一覧は、毎フレームの記録が作り直す
        if (!m_bInitialFilled)
        {
            commandList->BufferBarrier(m_Pool, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
            commandList->FillBuffer(m_Pool, 0, VirtualShadowMap::PoolBytes(m_PoolPages), VirtualShadowMap::EMPTY_DEPTH_BITS);
            commandList->BufferBarrier(m_Pool, RHI::ResourceState::CopyDest, RHI::ResourceState::Common);
            m_bInitialFilled = true;
        }

        VirtualShadowMapPagesDispatch dispatch;
        dispatch.PoolPages = m_PoolPages;
        dispatch.Pool = m_Pool;
        dispatch.PageTable = m_PageTable;
        dispatch.RequestBits = m_RequestBits;
        dispatch.FreeList = m_FreeList;
        dispatch.Stats = m_Stats;
        dispatch.DirtyList = m_DirtyList;
        const CameraProxy* camera = context.GetActiveCamera();
        const RHI::TexturePtr depth = m_DepthHandle.IsValid() ? resources.GetTexture(m_DepthHandle) : RHI::TexturePtr{};
        if (depth && camera && camera->Projection == ProjectionType::Perspective)
        {
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyShaderInverseViewProjection(dispatch.InverseViewProjection);
            dispatch.CameraPosition[0] = camera->PositionX;
            dispatch.CameraPosition[1] = camera->PositionY;
            dispatch.CameraPosition[2] = camera->PositionZ;
            dispatch.FovYDegrees = camera->FieldOfView;
            dispatch.Depth = depth;
            dispatch.Clipmap = &context.PhysicalLighting.SunClipmap;
        }

        const RHI::BufferPtr buffers[] = {m_Pool, m_PageTable, m_RequestBits, m_FreeList, m_Stats, m_DirtyList};
        for (const RHI::BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        }

        m_Pages->BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        m_Raster->BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        m_Casters->Uses.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        m_Pages->Record(commandList, dispatch);
        m_bMarked = m_Pages->WasMarked();

        // 影を落とす投影物の塊を物理ページへ描く。印付けをしなかった（深度かクリップマップが無い）フレームは、割り当て済みのページが無いので何も描かない
        m_bRasterRecorded = false;
        m_LastCasterChunkCount = 0;
        if (m_bMarked && m_Raster->IsReady())
        {
            CollectCasters(context);
            ReportCasters();
            if (!m_Casters->Chunks.empty())
            {
                m_bRasterRecorded = RecordRaster(context, frameSerial);
                m_LastCasterChunkCount = m_bRasterRecorded ? static_cast<uint32_t>(m_Casters->Chunks.size()) : 0u;
            }
        }

        // 統計を読み戻しの枠へ写す（読むのは、同じ番号の次のフレームの最初の Execute）。
        // 同じフレームの 2 回目以降の Execute は、同じ枠を新しい統計で上書きする
        if (slot.Buffer && slot.Mapped)
        {
            VirtualShadowMap::RecordStatsReadback(*commandList, m_Stats, slot.Buffer);
            slot.bPending = true;
            slot.FrameSerial = frameSerial;
        }

        // 宣言した状態（Common）へ戻す
        for (const RHI::BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }
    }

} // namespace NorvesLib::Core::Rendering
