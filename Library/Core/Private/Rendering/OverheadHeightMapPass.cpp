// 雨を遮る天面を、フレームの持ち主から借りた定数と遮蔽物一覧で記録する。
#include "Rendering/OverheadHeightMapPass.h"

#include "Logging/LogMacros.h"
#include "Rendering/OverheadHeightMapMath.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        RHI::ResourceState GetTextureState(const ViewRenderContext& context, const RHI::TexturePtr& texture)
        {
            switch (context.CommandList->GetTextureLayout(texture.get()))
            {
            case RHI::TextureLayout::ShaderReadOnly:
                return RHI::ResourceState::ShaderResource;
            case RHI::TextureLayout::DepthStencilAttachment:
                return RHI::ResourceState::DepthWrite;
            case RHI::TextureLayout::DepthStencilReadOnly:
                return RHI::ResourceState::DepthRead;
            case RHI::TextureLayout::General:
                return RHI::ResourceState::UnorderedAccess;
            case RHI::TextureLayout::TransferSource:
                return RHI::ResourceState::CopySource;
            case RHI::TextureLayout::TransferDest:
                return RHI::ResourceState::CopyDest;
            default:
                return RHI::ResourceState::Undefined;
            }
        }

        bool SameWindow(const OverheadHeightMapWindow& lhs, const OverheadHeightMapWindow& rhs)
        {
            return lhs.OriginTexelX == rhs.OriginTexelX && lhs.OriginTexelZ == rhs.OriginTexelZ &&
                lhs.TopY == rhs.TopY && lhs.Resolution == rhs.Resolution &&
                lhs.TexelMeters == rhs.TexelMeters && lhs.CoverageMeters == rhs.CoverageMeters &&
                lhs.RangeMeters == rhs.RangeMeters;
        }

        bool IsOutsideWindow(const BoundingSphere& bounds, const OverheadHeightMapWindow& window)
        {
            if (!(bounds.Radius > 0.0f) || !std::isfinite(bounds.Radius) ||
                !std::isfinite(bounds.CenterX) || !std::isfinite(bounds.CenterZ))
            {
                return false;
            }
            const double minimumX = window.GetOriginX();
            const double minimumZ = window.GetOriginZ();
            const double maximumX = minimumX + window.CoverageMeters;
            const double maximumZ = minimumZ + window.CoverageMeters;
            const double x = bounds.CenterX;
            const double z = bounds.CenterZ;
            const double dx = x - std::clamp(x, minimumX, maximumX);
            const double dz = z - std::clamp(z, minimumZ, maximumZ);
            const double radius = bounds.Radius;
            return dx * dx + dz * dz > radius * radius;
        }
    }

    OverheadHeightMapPass::OverheadHeightMapPass()
        : m_StateData(Container::MakeUnique<OverheadHeightMapPassState>())
    {
    }

    OverheadHeightMapPass::~OverheadHeightMapPass()
    {
        Shutdown();
    }

    bool OverheadHeightMapPass::EnsureTargets()
    {
        if (m_Texture && m_Framebuffer)
        {
            return true;
        }
        RHI::TextureDesc texture;
        texture.Width = m_StateData->Settings.Resolution;
        texture.Height = m_StateData->Settings.Resolution;
        texture.TextureFormat = RHI::Format::D32_FLOAT;
        texture.Usage = RHI::ResourceUsage::DepthStencil | RHI::ResourceUsage::ShaderRead;
        texture.DebugName = "OverheadHeightMap";
        m_Texture = m_Device->CreateTexture(texture);
        if (!m_Texture)
        {
            return false;
        }
        RHI::FramebufferDesc framebuffer;
        framebuffer.renderPass = m_RenderPass;
        framebuffer.depthStencilTarget = m_Texture;
        framebuffer.width = texture.Width;
        framebuffer.height = texture.Height;
        m_Framebuffer = m_Device->CreateFramebuffer(framebuffer);
        return m_Framebuffer != nullptr;
    }

    bool OverheadHeightMapPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        try
        {
            m_Device = context.Device;
            if (m_Device && context.ShaderMgr)
            {
                m_VertexShader = context.ShaderMgr->LoadShader("OverheadHeightMap/Roof.vert", RHI::ShaderStage::Vertex);
                m_FragmentShader = context.ShaderMgr->LoadShader("OverheadHeightMap/Roof.frag", RHI::ShaderStage::Pixel);
                RHI::DescriptorSetDesc layout;
                RHI::DescriptorBinding uniform;
                uniform.binding = 0u;
                uniform.type = RHI::ResourceBindType::ConstantBuffer;
                uniform.stages = RHI::ShaderStage::Vertex;
                layout.bindings.push_back(uniform);
                RHI::DescriptorBinding proxies;
                proxies.binding = 1u;
                proxies.type = RHI::ResourceBindType::StructuredBuffer;
                proxies.stages = RHI::ShaderStage::Vertex;
                layout.bindings.push_back(proxies);
                m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(layout);

                RHI::SamplerDesc sampler;
                sampler.filterMin = RHI::FilterMode::Point;
                sampler.filterMag = RHI::FilterMode::Point;
                sampler.filterMip = RHI::FilterMode::Point;
                sampler.addressU = RHI::TextureAddressMode::Clamp;
                sampler.addressV = RHI::TextureAddressMode::Clamp;
                sampler.addressW = RHI::TextureAddressMode::Clamp;
                m_Sampler = m_Device->CreateSampler(sampler);

                RHI::RenderPassDesc renderPass;
                renderPass.hasDepthStencil = true;
                renderPass.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
                renderPass.depthStencilAttachment.isDepthStencil = true;
                renderPass.depthStencilAttachment.clear = true;
                renderPass.depthStencilAttachment.clearDepth = 1.0f;
                renderPass.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
                renderPass.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
                renderPass.depthStencilAttachment.initialState = RHI::ResourceState::DepthWrite;
                renderPass.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;
                m_RenderPass = m_Device->CreateRenderPass(renderPass);
                if (m_VertexShader && m_FragmentShader && m_DescriptorSetLayout && m_Sampler &&
                    m_RenderPass && EnsureTargets())
                {
                    RHI::GraphicsPipelineDesc pipeline;
                    pipeline.vertexShader = m_VertexShader;
                    pipeline.pixelShader = m_FragmentShader;
                    pipeline.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
                    pipeline.rasterState.cullMode = RHI::CullMode::None;
                    pipeline.depthStencilState.depthTestEnable = true;
                    pipeline.depthStencilState.depthWriteEnable = true;
                    pipeline.depthStencilState.depthCompareOp = RHI::CompareOp::Less;
                    pipeline.renderPass = m_RenderPass;
                    pipeline.descriptorSetLayouts.push_back(layout);
                    m_Pipeline = m_Device->CreateGraphicsPipeline(pipeline);
                    m_bInitialized = m_Pipeline != nullptr;
                }
            }
        }
        catch (const std::exception&)
        {
            m_bInitialized = false;
        }
        if (!m_bInitialized)
        {
            NORVES_LOG_ERROR("OverheadHeightMapPass", "雨の遮蔽の深度パスを初期化できませんでした");
            Shutdown();
        }
        return m_bInitialized;
    }

    void OverheadHeightMapPass::Shutdown()
    {
        m_PreparedDescriptorSet.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_RenderPass.reset();
        m_Texture.reset();
        m_Sampler.reset();
        m_DescriptorSetLayout.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_StateData->DerivedOccluders = Container::VariableArray<SkyOccluderProxy>{};
        m_StateData->CollectedOccluders = Container::VariableArray<SkyOccluderProxy>{};
        m_HeightMapHandle = {};
        m_StateData->RedrawState = {};
        m_StateData->PreparedState = {};
        m_StateData->CollectedWindow = {};
        m_LastUpdateSubmission = {};
        m_StateData->CollectedHash = 0u;
        m_StateData->BoxCount = 0u;
        m_StateData->PreparedBoxCount = 0u;
        m_StateData->LastCollectionSeconds = 0.0;
        m_StateData->bHasCollection = false;
        m_StateData->bWarnedTruncated = false;
        m_StateData->bPrepared = false;
        m_StateData->bRedraw = false;
        m_bInitialized = false;
        m_Device = nullptr;
    }

    void OverheadHeightMapPass::CollectOccluders(
        const ViewRenderContext& context, const OverheadHeightMapWindow& window)
    {
        auto& derived = m_StateData->DerivedOccluders;
        derived.clear();
        if (context.SnapshotMeshProxies && context.Resources.Meshes)
        {
            for (const MeshProxy& proxy : *context.SnapshotMeshProxies)
            {
                if (!proxy.bVisible || !proxy.bCastShadow ||
                    !HasFlag(proxy.LayerMask, RenderLayer::Default) || !proxy.MeshHandle.IsValid() ||
                    IsOutsideWindow(proxy.WorldBounds, window))
                {
                    continue;
                }
                BoundingBox bounds;
                SkyOccluderProxy box;
                if (context.Resources.Meshes->TryGetLocalBounds(proxy.MeshHandle, bounds))
                {
                    const float minimum[3] = {bounds.MinX, bounds.MinY, bounds.MinZ};
                    const float maximum[3] = {bounds.MaxX, bounds.MaxY, bounds.MaxZ};
                    if (MakeOverheadBoxFromLocalBounds(minimum, maximum, proxy.WorldTransform, box))
                    {
                        derived.push_back(box);
                    }
                }
            }
        }
        if (context.SnapshotMegaGeometryProxies && context.Resources.MegaGeometry)
        {
            for (const MegaGeometryProxy& proxy : *context.SnapshotMegaGeometryProxies)
            {
                if (!proxy.bVisible || !proxy.bCastShadow || !proxy.MegaMeshHandle.IsValid() ||
                    IsOutsideWindow(proxy.WorldBounds, window))
                {
                    continue;
                }
                const auto* data = context.Resources.MegaGeometry->GetMegaMeshGPUData(proxy.MegaMeshHandle);
                if (!data || data->Material.TerrainBlend.IsTerrainSurface())
                {
                    continue;
                }
                SkyOccluderProxy ellipsoid;
                const BoundingSphere& bounds = proxy.WorldBounds;
                if (MakeOverheadEllipsoidFromSphere(bounds.CenterX, bounds.CenterY, bounds.CenterZ, bounds.Radius, ellipsoid))
                {
                    derived.push_back(ellipsoid);
                }
            }
        }
        Container::Span<const SkyOccluderProxy> skyOccluders;
        if (context.SnapshotScene)
        {
            skyOccluders = {context.SnapshotScene->SkyOccluders.data(), context.SnapshotScene->SkyOccluders.size()};
        }
        bool bTruncated = false;
        // 収集が失敗した場合も、以前の窓と新しい途中結果を組み合わせない。
        m_StateData->bHasCollection = false;
        m_StateData->BoxCount = CollectOverheadOccluders(m_StateData->Settings, window, skyOccluders,
            {derived.data(), derived.size()}, m_StateData->CollectedOccluders, bTruncated);
        if (bTruncated && !m_StateData->bWarnedTruncated)
        {
            NORVES_LOG_WARNING("OverheadHeightMapPass", "雨の遮蔽物が上限を超えたため近いものを優先して切り詰めました");
            m_StateData->bWarnedTruncated = true;
        }
        m_StateData->CollectedHash = HashOverheadOccluders(
            {m_StateData->CollectedOccluders.data(), m_StateData->CollectedOccluders.size()});
        m_StateData->CollectedWindow = window;
        m_StateData->LastCollectionSeconds = context.TotalTime;
        m_StateData->bHasCollection = true;
    }

    bool OverheadHeightMapPass::PrepareRedraw(
        const ViewRenderContext& context, const OverheadHeightMapWindow& window)
    {
        if (!context.FrameResources)
        {
            return false;
        }
        if (!m_StateData->bHasCollection || !SameWindow(m_StateData->CollectedWindow, window))
        {
            CollectOccluders(context, window);
        }
        const uint32_t count = static_cast<uint32_t>(m_StateData->CollectedOccluders.size());
        const OverheadHeightMapGpuParams params = MakeOverheadHeightMapGpuParams(
            window, m_StateData->BoxCount, count - m_StateData->BoxCount);
        FrameResourceLane& lane = *context.FrameResources;
        const SkyOccluderProxy emptyProxy{};
        const uint64_t storageBytes = count == 0u ? sizeof(emptyProxy) : uint64_t{count} * sizeof(SkyOccluderProxy);
        const uint64_t storageLimit = m_Device->GetCapabilities().MaxStorageBufferRange;
        if (storageLimit != 0u && storageBytes > storageLimit)
        {
            return false;
        }
        const FrameUploadAllocation uniform = lane.AllocateConstants(params);
        const FrameUploadAllocation proxies = lane.AllocateUploadData(
            count == 0u ? &emptyProxy : m_StateData->CollectedOccluders.data(), storageBytes, FrameUploadUsage::Storage);
        const FrameDescriptorAllocation descriptor = lane.AllocateDescriptorSet(m_DescriptorSetLayout);
        if (!uniform.IsValid() || !proxies.IsValid() || !descriptor.IsValid())
        {
            return false;
        }
        descriptor.Set->BindConstantBuffer(0u, uniform.Buffer, uniform.GetOffset32(), uniform.GetSize32());
        descriptor.Set->BindStorageBuffer(1u, proxies.Buffer, proxies.GetOffset32(), proxies.GetSize32());
        descriptor.Set->Update();
        m_PreparedDescriptorSet = descriptor.Set;
        m_StateData->PreparedBoxCount = m_StateData->BoxCount;
        m_StateData->PreparedState = {true, window, context.TotalTime, m_StateData->CollectedHash, count};
        return true;
    }

    void OverheadHeightMapPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void OverheadHeightMapPass::Execute(ViewRenderContext& /*context*/)
    {
        // 描画グラフが記録と公開を管理する。
    }

    void OverheadHeightMapPass::Declare(RenderGraphBuilder& builder)
    {
        m_StateData->bPrepared = false;
        m_StateData->bRedraw = false;
        m_PreparedDescriptorSet.reset();
        m_HeightMapHandle = {};
        const ViewRenderContext* context = builder.GetContext();
        if (!m_bInitialized || !m_bEnabled || !context || !context->CommandList || !context->MainCamera)
        {
            return;
        }
        if (m_LastUpdateSubmission.IsSet())
        {
            const FrameSubmissionOutcome outcome = context->FrameResources
                ? context->FrameResources->GetSubmissionOutcome(m_LastUpdateSubmission)
                : FrameSubmissionOutcome::Unknown;
            if (outcome == FrameSubmissionOutcome::Aborted || outcome == FrameSubmissionOutcome::Unknown)
            {
                // 提出されなかった画像の状態と中身は、画像を作り直して破棄する。
                m_Framebuffer.reset();
                m_Texture.reset();
                m_StateData->RedrawState = {};
                m_StateData->bHasCollection = false;
            }
            if (outcome != FrameSubmissionOutcome::Recording)
            {
                m_LastUpdateSubmission = {};
            }
        }
        m_StateData->PreparedState = m_StateData->RedrawState;
        try
        {
            if (!EnsureTargets())
            {
                NORVES_LOG_WARNING("OverheadHeightMapPass", "雨の遮蔽の深度画像を作成できませんでした");
                return;
            }
            const CameraProxy& camera = *context->MainCamera;
            const OverheadHeightMapWindow candidate = ComputeOverheadHeightMapWindow(
                m_StateData->Settings, camera.PositionX, camera.PositionY, camera.PositionZ);
            if (m_StateData->RedrawState.bHasContent &&
                (context->TotalTime < m_StateData->LastCollectionSeconds ||
                    context->TotalTime - m_StateData->LastCollectionSeconds >= 0.25))
            {
                CollectOccluders(*context, m_StateData->RedrawState.Window);
            }
            const bool bChanged = m_StateData->bHasCollection &&
                SameWindow(m_StateData->CollectedWindow, m_StateData->RedrawState.Window) &&
                (m_StateData->CollectedHash != m_StateData->RedrawState.OccluderHash ||
                    m_StateData->CollectedOccluders.size() != m_StateData->RedrawState.OccluderCount);
            const OverheadHeightMapRedrawDecision decision = DecideOverheadHeightMapRedraw(
                m_StateData->Settings, m_StateData->RedrawState, candidate, context->TotalTime, bChanged);
            if (decision.bRedraw)
            {
                m_StateData->bRedraw = PrepareRedraw(*context, decision.Window);
                if (!m_StateData->bRedraw)
                {
                    NORVES_LOG_WARNING("OverheadHeightMapPass", "雨の遮蔽の深度画像の更新を準備できませんでした");
                }
            }
        }
        catch (const std::exception&)
        {
            m_StateData->bRedraw = false;
            NORVES_LOG_WARNING("OverheadHeightMapPass", "雨の遮蔽の深度画像の更新準備に失敗しました");
        }
        if (!m_StateData->bRedraw)
        {
            m_StateData->PreparedState = m_StateData->RedrawState;
        }
        if (!m_StateData->PreparedState.bHasContent)
        {
            return;
        }
        // コマンドリストの追跡値から、前のフレームで確定した状態を引き継ぐ。
        const RGResourceHandle handle = builder.ImportTexture(m_Texture,
            GetTextureState(*context, m_Texture), "OverheadHeightMap");
        if (!handle.IsValid() || !builder.PublishTexture(RenderGraphResourceNames::OverheadHeightMap, handle) ||
            !builder.TryGetTexture(RenderGraphResourceNames::OverheadHeightMap, m_HeightMapHandle))
        {
            return;
        }
        if (m_StateData->bRedraw)
        {
            builder.Write(handle, RHI::ResourceState::DepthWrite, RHI::ResourceState::ShaderResource);
        }
        else
        {
            builder.Read(handle, RHI::ResourceState::ShaderResource);
        }
        builder.ExportTexture(RenderGraphResourceNames::OverheadHeightMap, handle);
        builder.PreserveInsertionOrder();
        m_StateData->bPrepared = true;
    }

    void OverheadHeightMapPass::PublishContextResources(RenderGraphResources& resources, ViewRenderContext& context) const
    {
        if (!m_StateData->bPrepared || !m_HeightMapHandle.IsValid() || !resources.GetTexture(m_HeightMapHandle))
        {
            return;
        }
        const OverheadHeightMapWindow& window = m_StateData->PreparedState.Window;
        const uint32_t viewId = context.CurrentViewport ? context.CurrentViewport->ViewId : UINT32_MAX;
        const uint32_t viewportId = context.CurrentViewport ? context.CurrentViewport->ViewportId : UINT32_MAX;
        context.OverheadHeightMap.Publish(context.FrameNumber, viewId, viewportId, m_Texture, m_Sampler,
            window.GetOriginX(), window.GetOriginZ(), window.TexelMeters, window.CoverageMeters,
            window.Resolution, window.TopY, window.RangeMeters, m_StateData->PreparedState.bHasContent);
    }

    void OverheadHeightMapPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_StateData->bPrepared || !context.CommandList || !resources.GetTexture(m_HeightMapHandle) ||
            (m_StateData->bRedraw && (!context.FrameResources || !m_PreparedDescriptorSet)))
        {
            return;
        }
        if (m_StateData->bRedraw)
        {
            RHI::ICommandList& commands = *context.CommandList;
            const uint32_t resolution = m_StateData->PreparedState.Window.Resolution;
            RHI::Viewport viewport;
            viewport.width = static_cast<float>(resolution);
            viewport.height = static_cast<float>(resolution);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            RHI::ScissorRect scissor;
            scissor.right = static_cast<int32_t>(resolution);
            scissor.bottom = static_cast<int32_t>(resolution);
            commands.BeginRenderPass(m_RenderPass, m_Framebuffer);
            commands.SetViewport(viewport);
            commands.SetScissor(scissor);
            commands.SetPipeline(m_Pipeline);
            commands.SetDescriptorSet(m_PreparedDescriptorSet, 0u);
            if (m_StateData->PreparedBoxCount != 0u)
            {
                commands.DrawInstanced(6u, m_StateData->PreparedBoxCount, 0u, 0u);
            }
            const uint32_t ellipsoidCount = m_StateData->PreparedState.OccluderCount - m_StateData->PreparedBoxCount;
            if (ellipsoidCount != 0u)
            {
                commands.DrawInstanced(168u, ellipsoidCount, 0u, m_StateData->PreparedBoxCount);
            }
            commands.EndRenderPass();
            m_StateData->RedrawState = m_StateData->PreparedState;
            m_LastUpdateSubmission = context.FrameResources->GetGeneration();
        }
        PublishContextResources(resources, context);
    }

    bool OverheadHeightMapPass::DeclaresAllLayoutChanges() const
    {
        return m_bInitialized;
    }

    RGRecordPolicy OverheadHeightMapPass::GetRecordPolicy() const
    {
        return RGRecordPolicy::Parallel;
    }

    void OverheadHeightMapPass::PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context)
    {
        PublishContextResources(resources, context);
    }
} // namespace NorvesLib::Core::Rendering
