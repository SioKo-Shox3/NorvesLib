#include "Rendering/VirtualShadowMapPass.h"

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ViewRenderContext.h"
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
            bCreated = m_Pool && m_PageTable && m_RequestBits && m_FreeList && m_Stats;
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
        m_bDeclared = false;
        m_bActive = false;
        m_bInitialFilled = false;
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
        m_bDeclared = false;

        // 作れなかった構成（装置の非対応・確保の失敗）では何も宣言しない
        if (!m_bActive)
        {
            return;
        }

        // 深度が確定した後に並べるため、GBuffer の深度・法線を読む（このタスクでは中身を使わない）。
        // 無い構成（GBuffer を作らない）では読まず、資源の公開だけをする
        RGTextureHandle depth;
        RGTextureHandle normal;
        if (builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, depth))
        {
            builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth, depth, RHI::ResourceState::ShaderResource);
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

    void VirtualShadowMapPass::Execute(RenderGraphResources& /*resources*/, ViewRenderContext& context)
    {
        if (!m_bActive || !m_bDeclared || !context.CommandList)
        {
            return;
        }

        RHI::ICommandList* commandList = context.CommandList;
        auto fill = [commandList](const RHI::BufferPtr& buffer, uint64_t bytes, uint32_t value)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
            commandList->FillBuffer(buffer, 0, bytes, value);
            commandList->BufferBarrier(buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::Common);
        };

        // 最初の実行で、何も無い texel の深度（1.0）でプールを、割り当てなしでページの表を埋める
        if (!m_bInitialFilled)
        {
            fill(m_Pool, VirtualShadowMap::PoolBytes(m_PoolPages), VirtualShadowMap::EMPTY_DEPTH_BITS);
            fill(m_PageTable, VirtualShadowMap::PageTableBytes(), 0u);
            m_bInitialFilled = true;
        }

        // 今フレームの要求と統計は毎フレーム 0 から数える
        fill(m_RequestBits, VirtualShadowMap::RequestBitsBytes(), 0u);
        fill(m_Stats, VirtualShadowMap::STATS_BYTES, 0u);
    }

} // namespace NorvesLib::Core::Rendering
