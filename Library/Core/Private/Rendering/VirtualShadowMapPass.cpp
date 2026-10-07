#include "Rendering/VirtualShadowMapPass.h"

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr double BytesPerMegabyte = 1024.0 * 1024.0;

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

        const uint64_t poolBytes = VirtualShadowMap::PoolBytes(plan.Pages);
        const uint64_t freeListBytes = VirtualShadowMap::FreeListBytes(plan.Pages);
        const RHI::ResourceUsage storageUsage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst;

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
            m_Stats = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::STATS_BYTES, storageUsage, false, "VSM_Stats"));
            // 消去するページの一覧は、間接 dispatch の引数としても読まれる
            m_DirtyList = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::DirtyListBytes(plan.Pages),
                                                                 storageUsage | RHI::ResourceUsage::IndirectBuffer,
                                                                 false,
                                                                 "VSM_DirtyList"));
            bCreated = m_Pool && m_PageTable && m_RequestBits && m_FreeList && m_Stats && m_DirtyList;
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
            slot.Buffer = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::STATS_BYTES,
                                                                 RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                                                                 true,
                                                                 "VSM_StatsReadback"));
            slot.Mapped = slot.Buffer ? static_cast<const uint32_t*>(slot.Buffer->Map(0, 0)) : nullptr;
        }

        m_PoolPages = plan.Pages;
        m_bActive = true;
        if (m_Gpu)
        {
            m_Gpu->SetShadowMapPoolBytes(poolBytes);
        }

        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_pool pages=%u mb=%.3f",
                        plan.Pages,
                        static_cast<double>(poolBytes) / BytesPerMegabyte);
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_page_table mb=%.3f",
                        static_cast<double>(VirtualShadowMap::PageTableBytes()) / BytesPerMegabyte);
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
        m_DepthHandle = {};
        m_bDeclared = false;
        m_bActive = false;
        m_bInitialFilled = false;
        m_ExecuteCount = 0;
        m_bMarked = false;
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
        builder.PreserveInsertionOrder();
    }

    void VirtualShadowMapPass::HarvestStats(StatsSlot& slot)
    {
        if (!slot.bPending || !slot.Mapped)
        {
            return;
        }
        slot.bPending = false;
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
        if (!m_bActive || !m_bDeclared || !m_Pages || !context.CommandList)
        {
            return;
        }

        RHI::ICommandList* commandList = context.CommandList;
        ++m_ExecuteCount;

        // 数フレーム前（GPU が書き終えている）の統計を古い順に読み、このフレームが書く枠を空ける
        for (uint32_t offset = 1; offset <= StatsSlotCount; ++offset)
        {
            StatsSlot& pending = m_StatsSlots[(m_ExecuteCount + offset) % StatsSlotCount];
            if (pending.bPending && pending.ExecuteIndex + 2 <= m_ExecuteCount)
            {
                HarvestStats(pending);
            }
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
        m_Pages->Record(commandList, dispatch);
        m_bMarked = m_Pages->WasMarked();

        // 統計を読み戻しの枠へ写す（読むのは数フレーム後）
        StatsSlot& slot = m_StatsSlots[m_ExecuteCount % StatsSlotCount];
        if (slot.Buffer && slot.Mapped)
        {
            commandList->BufferBarrier(m_Stats, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopySource);
            commandList->BufferBarrier(slot.Buffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
            commandList->CopyBuffer(m_Stats, slot.Buffer, VirtualShadowMap::STATS_BYTES);
            commandList->BufferBarrier(slot.Buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::HostRead);
            commandList->BufferBarrier(m_Stats, RHI::ResourceState::CopySource, RHI::ResourceState::UnorderedAccess);
            slot.bPending = true;
            slot.ExecuteIndex = m_ExecuteCount;
        }

        // 宣言した状態（Common）へ戻す
        for (const RHI::BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }
    }

} // namespace NorvesLib::Core::Rendering
