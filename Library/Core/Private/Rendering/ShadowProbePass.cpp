#include "Rendering/ShadowProbePass.h"

// 測定の道具なので、統計が有効な構成（Debug・RelWithDebInfo）だけで実装する。Release には入れない。
#if NORVES_ENABLE_STATS

#include "Logging/LogMacros.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VirtualShadowMapPass.h"
#include "Rendering/VirtualShadowMapSample.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ITexture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // シェーダーの ProbeParams（std140）と同じ並び
        struct GPUShadowProbeParams
        {
            float invViewProjection[16];
            float viewProjection[16];
            float lightView[PhysicalLightingShadowCascadeCount][16];
            float lightProjection[PhysicalLightingShadowCascadeCount][16];
            float shadowSplitDistances[8];
            float cameraPosition[4];
            float cameraForward[4];
            uint32_t screen[4];  // x = 幅、y = 高さ、z = 格子の横の数、w = 縦の数
            uint32_t control[4]; // x = モード（0 = 標本を固定、1 = 測る）、y = カスケード数、z = 影の有効フラグ、w = 格子の間隔
            float tuning[4];     // x = 深度の一致の許容
            GPUVsmSampleParams vsm; // 太陽の VSM を読むパラメータ（control[0] = 0 なら VSM は測らない）
        };
        static_assert(sizeof(GPUShadowProbeParams) == 752 + sizeof(GPUVsmSampleParams), "shadow_probe.comp の ProbeParams と同じ大きさにすること");

        // 標本 1 点（位置 + 法線）のバイト数（シェーダーの ProbePoint）
        constexpr uint64_t ProbePointBytes = 32;

        constexpr uint32_t ModeCapture = 0;
        constexpr uint32_t ModeMeasure = 1;

        RHI::DescriptorSetDesc MakeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::ConstantBuffer,       // 0 パラメータ
                RHI::ResourceBindType::CombinedImageSampler, // 1 GBuffer.Depth
                RHI::ResourceBindType::CombinedImageSampler, // 2 GBuffer.Normal
                RHI::ResourceBindType::CombinedImageSampler, // 3 CSM の影の地図
                RHI::ResourceBindType::RWBuffer,             // 4 標本
                RHI::ResourceBindType::RWBuffer,             // 5 前のフレームの可視度
                RHI::ResourceBindType::RWBuffer,             // 6 統計
                RHI::ResourceBindType::RWBuffer,             // 7 VSM のページの表
                RHI::ResourceBindType::RWBuffer,             // 8 VSM の物理ページのプール
            };
            for (uint32_t bindingIndex = 0; bindingIndex < 9u; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(binding);
            }
            return desc;
        }

        // 照明が使えるのと同じ条件で、CSM の公開値が使えるか（4 カスケード・有限の行列・増える分割）
        bool HasUsableCascadedShadow(const PhysicalLightingResources& lighting)
        {
            const CascadedDirectionalShadowShaderValues& cascaded = lighting.CascadedShadow;
            if (!lighting.bShadowPublished || !lighting.ShadowMapTexture || !cascaded.bEnabled ||
                cascaded.CascadeCount != PhysicalLightingShadowCascadeCount ||
                lighting.ShadowMapTexture->GetArraySize() != PhysicalLightingShadowCascadeCount)
            {
                return false;
            }
            for (uint32_t cascadeIndex = 0; cascadeIndex < PhysicalLightingShadowCascadeCount; ++cascadeIndex)
            {
                for (uint32_t element = 0; element < 16u; ++element)
                {
                    if (!std::isfinite(cascaded.View[cascadeIndex][element]) ||
                        !std::isfinite(cascaded.Projection[cascadeIndex][element]))
                    {
                        return false;
                    }
                }
            }
            for (uint32_t splitIndex = 0; splitIndex < PhysicalLightingShadowSplitCount; ++splitIndex)
            {
                if (!std::isfinite(cascaded.SplitDistances[splitIndex]) ||
                    (splitIndex > 0u && cascaded.SplitDistances[splitIndex] <= cascaded.SplitDistances[splitIndex - 1u]))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    ShadowProbePass::ShadowProbePass() = default;

    ShadowProbePass::~ShadowProbePass()
    {
        Shutdown();
    }

    bool ShadowProbePass::CreatePipeline(ViewRenderContext& context)
    {
        if (!context.Device || !context.ShaderMgr)
        {
            return false;
        }
        m_Shader = context.ShaderMgr->LoadShader("shadow_probe.comp", RHI::ShaderStage::Compute);
        if (!m_Shader)
        {
            NORVES_LOG_WARNING("ShadowProbePass", "影の標本の計算シェーダーの読み込みに失敗");
            return false;
        }

        // GBuffer の深度・法線は texelFetch で読むのでフィルターしない（影の地図の標本化は照明の公開した sampler を使う）
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_PointSampler = context.Device->CreateSampler(samplerDesc);
        if (!m_PointSampler)
        {
            NORVES_LOG_WARNING("ShadowProbePass", "影の標本のサンプラーの作成に失敗");
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = m_Shader;
        pipelineDesc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc());
        m_Pipeline = context.Device->CreateComputePipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("ShadowProbePass", "影の標本の計算パイプラインの作成に失敗");
            return false;
        }
        return true;
    }

    bool ShadowProbePass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        // 計算パイプラインを作れなくても描画全体は止めない（Declare が何も宣言せず、このパスは何もしない）
        if (!CreatePipeline(context))
        {
            NORVES_LOG_WARNING("ShadowProbePass", "影の標本を使えないので、このパスは何もしない");
            m_Pipeline.reset();
        }
        m_bInitialized = true;
        return true;
    }

    void ShadowProbePass::Shutdown()
    {
        LogSummary();
        m_Uses.Clear();
        for (StatsSlot& slot : m_Slots)
        {
            slot = StatsSlot{};
        }
        m_ProbeBuffer.reset();
        m_StateBuffer.reset();
        m_Pipeline.reset();
        m_PointSampler.reset();
        m_Shader.reset();
        m_Device = nullptr;
        m_Grid = {};
        m_bStatsMapped = false;
        m_DepthHandle = {};
        m_NormalHandle = {};
        m_ShadowMapHandle = {};
        m_SceneColorHandle = {};
        m_VsmPageTableHandle = {};
        m_VsmPoolHandle = {};
        m_bDeclared = false;
        m_bInitialized = false;
    }

    void ShadowProbePass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void ShadowProbePass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void ShadowProbePass::Declare(RenderGraphBuilder& builder)
    {
        m_DepthHandle = {};
        m_NormalHandle = {};
        m_ShadowMapHandle = {};
        m_SceneColorHandle = {};
        m_VsmPageTableHandle = {};
        m_VsmPoolHandle = {};
        m_bDeclared = false;

        // 初期化を済ませたのにパイプラインが無いときは何も宣言しない
        if (m_bInitialized && !m_Pipeline)
        {
            return;
        }

        // GBuffer・CSM の影の地図が無い構成（影を落とす灯が無い・GBuffer を作らない）では何も宣言せず、何も測らない。
        // Scene.Color は照明が書く。読むことで、照明の後に並べる（結果の色は使わない）
        // 一部だけ読む宣言を残さないよう、4 つとも資源があることを先に確かめる
        RGTextureHandle depth;
        RGTextureHandle normal;
        RGTextureHandle shadowMap;
        RGTextureHandle sceneColor;
        if (!builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, depth) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normal) ||
            !builder.TryGetTexture(RenderGraphResourceNames::ShadowMap, shadowMap) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SceneColor, sceneColor))
        {
            return;
        }
        if (!builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth, depth, RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::GBufferNormal, normal, RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::ShadowMap, shadowMap, RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::SceneColor, sceneColor, RHI::ResourceState::ShaderResource))
        {
            return;
        }

        m_DepthHandle = depth;
        m_NormalHandle = normal;
        m_ShadowMapHandle = shadowMap;
        m_SceneColorHandle = sceneColor;

        // 太陽の VSM（--shadow-method=vsm）のページの表・物理ページのプール。VirtualShadowMapPass が公開したときだけ読む
        if (builder.HasBuffer(RenderGraphResourceNames::VsmPageTable) && builder.HasBuffer(RenderGraphResourceNames::VsmPhysicalPool))
        {
            const RGBufferHandle vsmPageTable = builder.ReadBuffer(RenderGraphResourceNames::VsmPageTable, RHI::ResourceState::ShaderResource);
            const RGBufferHandle vsmPool = builder.ReadBuffer(RenderGraphResourceNames::VsmPhysicalPool, RHI::ResourceState::ShaderResource);
            if (vsmPageTable.IsValid() && vsmPool.IsValid())
            {
                m_VsmPageTableHandle = vsmPageTable.ToResourceHandle();
                m_VsmPoolHandle = vsmPool.ToResourceHandle();
            }
        }
        m_bDeclared = true;
        builder.PreserveInsertionOrder();
    }

    bool ShadowProbePass::EnsureBuffers(const ShadowProbe::Grid& grid)
    {
        if (!m_Device || !grid.IsValid())
        {
            return false;
        }
        if (m_ProbeBuffer && m_StateBuffer)
        {
            // 画面の大きさが変わったら標本の対応が崩れるので、以後は測らない（測定は大きさが固定の撮影で行う）
            if (grid.CountX != m_Grid.CountX || grid.CountY != m_Grid.CountY)
            {
                NORVES_LOG_WARNING("ShadowProbePass", "画面の大きさが変わったので、影の標本の測定を止めます");
                return false;
            }
            return true;
        }

        const uint64_t count = grid.Count();
        m_ProbeBuffer = m_Device->CreateBuffer(RHI::BufferDesc(count * ProbePointBytes,
                                                               RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                                                               false,
                                                               "ShadowProbe_Points"));
        // 前のフレームの可視度。前半が CSM、後半が VSM
        m_StateBuffer = m_Device->CreateBuffer(RHI::BufferDesc(count * 2u * sizeof(float),
                                                               RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                                                               false,
                                                               "ShadowProbe_State"));
        if (!m_ProbeBuffer || !m_StateBuffer)
        {
            m_ProbeBuffer.reset();
            m_StateBuffer.reset();
            return false;
        }
        m_Grid = grid;

        // 統計の読み戻し先。シェーダーが storage buffer として直接数え、ホストが数フレーム後に読む（host-visible）。
        // 作れない・写像できないデバイスでは測れない（Execute が何もしない）
        for (StatsSlot& slot : m_Slots)
        {
            slot.Buffer = m_Device->CreateBuffer(RHI::BufferDesc(ShadowProbe::STATS_BYTES,
                                                                 RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                                                                 true,
                                                                 "ShadowProbe_Stats"));
            slot.Mapped = slot.Buffer ? static_cast<const uint32_t*>(slot.Buffer->Map(0, 0)) : nullptr;
            if (!slot.Mapped)
            {
                return false;
            }
        }
        m_bStatsMapped = true;
        return true;
    }

    bool ShadowProbePass::AcquireUse(Use*& outUse)
    {
        Use& use = m_Uses.Acquire();
        if (!use.Uniform)
        {
            use.Uniform = m_Device->CreateBuffer(
                RHI::BufferDesc(sizeof(GPUShadowProbeParams), RHI::ResourceUsage::ConstantBuffer, true, "ShadowProbeParams"));
        }
        if (!use.DescriptorSet)
        {
            use.DescriptorSet = m_Device->CreateDescriptorSet(MakeDescriptorSetDesc());
        }
        outUse = &use;
        return use.Uniform && use.DescriptorSet;
    }

    void ShadowProbePass::HarvestSlot(StatsSlot& slot)
    {
        if (!slot.bPending || !slot.Mapped)
        {
            return;
        }
        if (slot.bCapture)
        {
            m_Totals.AddCaptureFrame(slot.Mapped);
        }
        else
        {
            m_Totals.AddMeasuredFrame(slot.Mapped, slot.bVsm);
        }
        slot.bPending = false;
    }

    void ShadowProbePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bDeclared || !m_Pipeline || !m_Device || !context.CommandList)
        {
            return;
        }
        ++m_ExecuteCount;
        // 標本を固定するのは、決定的な撮影ではエポック（読み込み完了）の後の最初に測れるフレーム（エポックの最初のフレームが
        // 影の地図の欠けなどで測れなくても、次に測れるフレームで固定する）。そうでなければ起動から一定の実行の後。
        // エポックは読み込みが落ち着くまで何度も始め直されるので、始まるたびに標本の固定と合計を最初からやり直す
        // （読み込み前のシーンに固定した標本や、そこで測った値を残さない）
        if (context.bDeterministicCapture && context.bTemporalEpochStart)
        {
            m_bEpochSeen = true;
            m_bCaptured = false;
            m_Totals = ShadowProbe::Totals{};
            for (StatsSlot& pendingSlot : m_Slots)
            {
                pendingSlot.bPending = false;
            }
        }
        const bool bCaptureFrame =
            !m_bCaptured && (context.bDeterministicCapture ? m_bEpochSeen
                                                           : m_ExecuteCount >= ShadowProbe::FALLBACK_CAPTURE_EXECUTE_COUNT);
        if (!m_bCaptured && !bCaptureFrame)
        {
            return;
        }

        const RHI::TexturePtr depth = resources.GetTexture(m_DepthHandle);
        const RHI::TexturePtr normal = resources.GetTexture(m_NormalHandle);
        const RHI::TexturePtr shadowMap = resources.GetTexture(m_ShadowMapHandle);
        const PhysicalLightingResources& lighting = context.PhysicalLighting;
        const CameraProxy* camera = context.GetActiveCamera();
        if (!depth || !normal || !shadowMap || !camera || !HasUsableCascadedShadow(lighting))
        {
            return;
        }
        const ShadowProbe::Grid grid = ShadowProbe::ComputeGrid(depth->GetWidth(), depth->GetHeight());
        if (!EnsureBuffers(grid) || !m_bStatsMapped)
        {
            return;
        }

        // 数フレーム前の統計を読み戻す。同じフレームの別のビューポートや直前のフレームが書いた統計は、
        // GPU が書き終えていないかもしれないので、読まず・上書きせず、この記録は取らない
        StatsSlot& slot = m_Slots[m_ExecuteCount % StatsSlotCount];
        if (slot.bPending)
        {
            if (slot.ExecuteIndex + 2 > m_ExecuteCount)
            {
                return;
            }
            HarvestSlot(slot);
        }

        Use* use = nullptr;
        m_Uses.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        if (!AcquireUse(use))
        {
            return;
        }

        GPUShadowProbeParams params = {};
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
        cameraConstants.CopyShaderInverseViewProjection(params.invViewProjection);
        cameraConstants.CopyShaderViewProjection(params.viewProjection);
        cameraConstants.CopyCameraPosition(params.cameraPosition);
        std::memcpy(params.lightView, lighting.CascadedShadow.View, sizeof(params.lightView));
        std::memcpy(params.lightProjection, lighting.CascadedShadow.Projection, sizeof(params.lightProjection));
        std::memcpy(params.shadowSplitDistances,
                    lighting.CascadedShadow.SplitDistances,
                    sizeof(float) * PhysicalLightingShadowSplitCount);
        const float forwardLength =
            std::sqrt(camera->ForwardX * camera->ForwardX + camera->ForwardY * camera->ForwardY + camera->ForwardZ * camera->ForwardZ);
        if (!std::isfinite(forwardLength) || forwardLength <= 1.0e-5f)
        {
            return;
        }
        params.cameraForward[0] = camera->ForwardX / forwardLength;
        params.cameraForward[1] = camera->ForwardY / forwardLength;
        params.cameraForward[2] = camera->ForwardZ / forwardLength;
        params.screen[0] = depth->GetWidth();
        params.screen[1] = depth->GetHeight();
        params.screen[2] = grid.CountX;
        params.screen[3] = grid.CountY;
        params.control[0] = bCaptureFrame ? ModeCapture : ModeMeasure;
        params.control[1] = PhysicalLightingShadowCascadeCount;
        params.control[2] = 1u;
        params.control[3] = ShadowProbe::GRID_STEP;
        params.tuning[0] = ShadowProbe::DEPTH_TOLERANCE;

        // 太陽の VSM（--shadow-method=vsm）も測る。クリップマップ・ページの表・プールが揃ったときだけ（標本を固定するフレームは測らない）
        RHI::BufferPtr vsmPageTable = m_VsmPageTableHandle.IsValid() ? resources.GetBuffer(m_VsmPageTableHandle) : RHI::BufferPtr{};
        RHI::BufferPtr vsmPool = m_VsmPoolHandle.IsValid() ? resources.GetBuffer(m_VsmPoolHandle) : RHI::BufferPtr{};
        bool bVsm = false;
        if (!bCaptureFrame && vsmPageTable && vsmPool && camera->Projection == ProjectionType::Perspective &&
            vsmPageTable->GetSize() >= VirtualShadowMap::PageTableBytes() && vsmPool->GetSize() >= VirtualShadowMap::PAGE_BYTES)
        {
            const float cameraPosition[3] = {camera->PositionX, camera->PositionY, camera->PositionZ};
            const uint64_t poolPages = vsmPool->GetSize() / VirtualShadowMap::PAGE_BYTES;
            bVsm = BuildVirtualShadowMapSampleParams(&lighting.SunClipmap,
                                                     cameraPosition,
                                                     camera->FieldOfView,
                                                     static_cast<float>(depth->GetHeight()),
                                                     static_cast<uint32_t>(std::min<uint64_t>(poolPages, VirtualShadowMap::MAX_POOL_PAGES)),
                                                     params.vsm);
        }
        if (!bVsm)
        {
            std::memset(&params.vsm, 0, sizeof(params.vsm));
            // 使われない（control.x = 0）ので、別のバッファを置く
            vsmPageTable = m_ProbeBuffer;
            vsmPool = m_ProbeBuffer;
        }
        use->Uniform->Update(&params, sizeof(params));

        use->DescriptorSet->BindConstantBuffer(0, use->Uniform, 0, static_cast<uint32_t>(sizeof(params)));
        use->DescriptorSet->BindTexture(1, depth);
        use->DescriptorSet->BindSampler(1, m_PointSampler);
        use->DescriptorSet->BindTexture(2, normal);
        use->DescriptorSet->BindSampler(2, m_PointSampler);
        use->DescriptorSet->BindTexture(3, shadowMap);
        use->DescriptorSet->BindSampler(3, lighting.ShadowSampler ? lighting.ShadowSampler : m_PointSampler);
        use->DescriptorSet->BindStorageBuffer(4, m_ProbeBuffer, 0, static_cast<uint32_t>(m_ProbeBuffer->GetSize()));
        use->DescriptorSet->BindStorageBuffer(5, m_StateBuffer, 0, static_cast<uint32_t>(m_StateBuffer->GetSize()));
        use->DescriptorSet->BindStorageBuffer(6, slot.Buffer, 0, ShadowProbe::STATS_BYTES);
        use->DescriptorSet->BindStorageBuffer(7, vsmPageTable, 0, static_cast<uint32_t>(std::min<uint64_t>(vsmPageTable->GetSize(), 0xFFFFFFFFull)));
        use->DescriptorSet->BindStorageBuffer(8, vsmPool, 0, static_cast<uint32_t>(std::min<uint64_t>(vsmPool->GetSize(), 0xFFFFFFFFull)));
        use->DescriptorSet->Update();

        RHI::ICommandList* commandList = context.CommandList;
        // 1 フレームぶんの統計を 0 から数える（前に使ったのはホストが読んだ後）
        commandList->BufferBarrier(slot.Buffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
        commandList->FillBuffer(slot.Buffer, 0, ShadowProbe::STATS_BYTES, 0);
        commandList->BufferBarrier(slot.Buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
        commandList->BufferBarrier(m_ProbeBuffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        commandList->BufferBarrier(m_StateBuffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);

        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(use->DescriptorSet, 0);
        commandList->Dispatch((grid.CountX + ShadowProbe::GROUP_SIZE - 1) / ShadowProbe::GROUP_SIZE,
                              (grid.CountY + ShadowProbe::GROUP_SIZE - 1) / ShadowProbe::GROUP_SIZE,
                              1u);

        // 統計の書き込みをホストの読み取りへ、標本・前の可視度を次のフレームへ見せる
        commandList->BufferBarrier(slot.Buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::HostRead);
        commandList->BufferBarrier(m_ProbeBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        commandList->BufferBarrier(m_StateBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);

        slot.ExecuteIndex = m_ExecuteCount;
        slot.bPending = true;
        slot.bCapture = bCaptureFrame;
        slot.bVsm = bVsm;
        if (bCaptureFrame)
        {
            m_bCaptured = true;
        }
    }

    void ShadowProbePass::LogSummary()
    {
        if (m_bLogged || !m_bInitialized)
        {
            return;
        }
        m_bLogged = true;
        // 書き終えたと分かる（2 実行以上前の）統計を読み戻してから出す
        for (StatsSlot& slot : m_Slots)
        {
            if (slot.bPending && slot.ExecuteIndex + 2 <= m_ExecuteCount)
            {
                HarvestSlot(slot);
            }
        }
        const ShadowProbe::Totals& totals = m_Totals;
        NORVES_LOG_INFO("ShadowProbePass",
                        "SHADOW_PROBE method=csm frames=%llu probes=%llu pairs=%llu mean_abs_delta=%.6f changed_ratio=%.6f flip_ratio=%.6f partial_ratio=%.6f mean_texel_mm=%.3f",
                        static_cast<unsigned long long>(totals.Frames),
                        static_cast<unsigned long long>(totals.Probes),
                        static_cast<unsigned long long>(totals.Pairs),
                        totals.MeanAbsDelta(),
                        totals.ChangedRatio(),
                        totals.FlipRatio(),
                        totals.PartialRatio(),
                        totals.MeanTexelMm());
        // 見えていた標本のうち影の範囲（最初の分割〜最後の分割）の外にあった割合（mean_texel_mm はその分も最後のカスケードで数える）
        NORVES_LOG_INFO("ShadowProbePass",
                        "SHADOW_PROBE_DETAIL method=csm visible=%llu out_of_range_ratio=%.6f",
                        static_cast<unsigned long long>(totals.Visible),
                        totals.OutOfRangeRatio());
        // --shadow-method=vsm のとき（VSM を測ったフレームがあるとき）だけ、VSM の行と CSM との一致を出す
        if (totals.VsmFrames != 0)
        {
            NORVES_LOG_INFO("ShadowProbePass",
                            "SHADOW_PROBE method=vsm frames=%llu probes=%llu pairs=%llu mean_abs_delta=%.6f changed_ratio=%.6f flip_ratio=%.6f partial_ratio=%.6f mean_texel_mm=%.3f",
                            static_cast<unsigned long long>(totals.VsmFrames),
                            static_cast<unsigned long long>(totals.Probes),
                            static_cast<unsigned long long>(totals.VsmPairs),
                            totals.VsmMeanAbsDelta(),
                            totals.VsmChangedRatio(),
                            totals.VsmFlipRatio(),
                            totals.VsmPartialRatio(),
                            totals.VsmMeanTexelMm());
            NORVES_LOG_INFO("ShadowProbePass",
                            "SHADOW_PROBE_AGREE both_definite=%llu agree=%llu ratio=%.6f finer_ratio=%.6f",
                            static_cast<unsigned long long>(totals.BothDefinite),
                            static_cast<unsigned long long>(totals.Agree),
                            totals.AgreeRatio(),
                            totals.FinerRatio());
            NORVES_LOG_INFO("ShadowProbePass",
                            "SHADOW_PROBE_DETAIL method=vsm visible=%llu fallback_ratio=%.6f",
                            static_cast<unsigned long long>(totals.VsmVisible),
                            totals.FallbackRatio());
        }
    }

} // namespace NorvesLib::Core::Rendering

#endif // NORVES_ENABLE_STATS
