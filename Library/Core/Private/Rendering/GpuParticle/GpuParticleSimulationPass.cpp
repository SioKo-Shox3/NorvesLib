// GPU の実行先のエミッタを常駐のプールで生成・更新する描画の拡張のパス
#include "Rendering/GpuParticle/GpuParticleSimulationPass.h"

#include "Rendering/GpuParticle/GpuParticleDraw.h"
#include "Rendering/GpuParticle/GpuParticleDrawPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderExtension/RenderExtension.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/Wind.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr const char* ExtensionName = "Ext.Engine.GpuParticleSimulation";
        constexpr uint32_t WorkgroupSize = 64u;
        constexpr uint32_t MaxWorkgroupCountX = 65535u;
        constexpr uint64_t ViewResourceRetireFrames = 600u;
        constexpr uint32_t MaxSubsteps = 8u;
        constexpr uint32_t PoolBytes = kGpuParticlePoolCapacity * sizeof(GpuParticle);
        constexpr uint32_t HeadBytes = kGpuParticleMaxEmitters * sizeof(uint32_t);

        RHI::DescriptorSetDesc MakeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            for (uint32_t index = 0; index < 10; ++index)
            {
                RHI::DescriptorBinding binding;
                binding.binding = index;
                binding.stages = RHI::ShaderStage::Compute;
                if (index == 0)
                {
                    binding.type = RHI::ResourceBindType::ConstantBuffer;
                }
                else if (index == 1 || index == 6)
                {
                    binding.type = RHI::ResourceBindType::RWBuffer;
                }
                else if (index < 7)
                {
                    binding.type = RHI::ResourceBindType::StructuredBuffer;
                }
                else
                {
                    binding.type = RHI::ResourceBindType::CombinedImageSampler;
                }
                desc.bindings.push_back(binding);
            }
            return desc;
        }

        void SynchronizeBuffers(RHI::ICommandList& commands,
                                const RHI::BufferPtr& pool,
                                const RHI::BufferPtr& heads)
        {
            commands.BufferBarrier(pool, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            commands.BufferBarrier(heads, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        }
    }

    GpuParticleSimulationPass::~GpuParticleSimulationPass()
    {
        Shutdown();
    }

    const char* GpuParticleSimulationPass::GetName() const
    {
        return ExtensionName;
    }

    bool GpuParticleSimulationPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            return false;
        }

        m_Device = context.Device;
        m_UpdateShader = context.ShaderMgr->LoadShader("Particles/ParticleUpdate.comp", RHI::ShaderStage::Compute);
        m_SpawnShader = context.ShaderMgr->LoadShader("Particles/ParticleSpawn.comp", RHI::ShaderStage::Compute);
        if (!m_UpdateShader || !m_SpawnShader)
        {
            return false;
        }

        const RHI::DescriptorSetDesc descriptorDesc = MakeDescriptorSetDesc();
        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(descriptorDesc);
        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.descriptorSetLayouts.push_back(descriptorDesc);
        pipelineDesc.computeShader = m_UpdateShader;
        m_UpdatePipeline = m_Device->CreateComputePipeline(pipelineDesc);
        pipelineDesc.computeShader = m_SpawnShader;
        m_SpawnPipeline = m_Device->CreateComputePipeline(pipelineDesc);

        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = m_Device->CreateSampler(samplerDesc);

        RHI::TextureDesc textureDesc;
        textureDesc.Width = 1;
        textureDesc.Height = 1;
        textureDesc.TextureFormat = RHI::Format::R32_FLOAT;
        textureDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        textureDesc.DebugName = "GpuParticleSimulation.Empty";
        m_EmptyTexture = m_Device->CreateTexture(textureDesc);
        textureDesc.DebugName = "GpuParticleSimulation.EmptyHeight";
        m_EmptyHeightTexture = m_Device->CreateTexture(textureDesc);
        if (!m_DescriptorSetLayout || !m_UpdatePipeline || !m_SpawnPipeline || !m_Sampler ||
            !m_EmptyTexture || !m_EmptyHeightTexture)
        {
            return false;
        }
        const float zero = 0.0f;
        m_EmptyTexture->Update(&zero, sizeof(zero), sizeof(zero));
        m_EmptyHeightTexture->Update(&zero, sizeof(zero), sizeof(zero));
        m_bInitialized = true;
        return true;
    }

    void GpuParticleSimulationPass::Shutdown()
    {
        m_ViewResources.clear();
        m_EmptyTexture.reset();
        m_EmptyHeightTexture.reset();
        m_Sampler.reset();
        m_DescriptorSetLayout.reset();
        m_UpdatePipeline.reset();
        m_SpawnPipeline.reset();
        m_UpdateShader.reset();
        m_SpawnShader.reset();
        m_DepthHandle = {};
        m_NormalHandle = {};
        m_PoolHandle = {};
        m_HeadsHandle = {};
        m_Device = nullptr;
        m_bReportedClampedTime = false;
        m_bInitialized = false;
    }

    void GpuParticleSimulationPass::Setup(ViewRenderContext&)
    {
    }

    void GpuParticleSimulationPass::Execute(ViewRenderContext&)
    {
        // RenderGraph 経由でのみ実行する。
    }

    GpuParticleSimulationPass::ViewResources* GpuParticleSimulationPass::FindOrCreateViewResources(
        const ViewRenderContext& context)
    {
        const uint32_t viewId = context.PhysicalLighting.ViewId;
        const uint32_t viewportId = context.PhysicalLighting.ViewportId;
        for (ViewResources& view : m_ViewResources)
        {
            if (view.ViewId == viewId && view.ViewportId == viewportId)
            {
                view.LastUsedFrameNumber = context.ExtensionFrame.FrameNumber;
                return &view;
            }
        }

        if (!m_Device)
        {
            return nullptr;
        }
        ViewResources view;
        view.ViewId = viewId;
        view.ViewportId = viewportId;
        view.LastUsedFrameNumber = context.ExtensionFrame.FrameNumber;
        const RHI::ResourceUsage usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst;
        view.Pool = m_Device->CreateBuffer(RHI::BufferDesc(PoolBytes, usage, false, "GpuParticleSimulation.Pool"));
        view.Heads = m_Device->CreateBuffer(RHI::BufferDesc(HeadBytes, usage, false, "GpuParticleSimulation.Heads"));
        if (!view.Pool || !view.Heads)
        {
            return nullptr;
        }
        m_ViewResources.push_back(std::move(view));
        return &m_ViewResources.back();
    }

    void GpuParticleSimulationPass::Declare(RenderGraphBuilder& builder)
    {
        const ViewRenderContext* context = builder.GetContext();
        if (context)
        {
            const uint64_t frameNumber = context->ExtensionFrame.FrameNumber;
            for (size_t index = 0; index < m_ViewResources.size();)
            {
                const uint64_t lastUsed = m_ViewResources[index].LastUsedFrameNumber;
                if (frameNumber > lastUsed && frameNumber - lastUsed >= ViewResourceRetireFrames)
                {
                    m_ViewResources.erase(m_ViewResources.begin() + static_cast<std::ptrdiff_t>(index));
                }
                else
                {
                    ++index;
                }
            }
        }
        m_DepthHandle = {};
        m_NormalHandle = {};
        m_PoolHandle = {};
        m_HeadsHandle = {};
        if (!m_bInitialized || !context || !context->SnapshotScene ||
            context->SnapshotScene->GpuParticles.IsEmpty())
        {
            return;
        }
        ViewResources* view = FindOrCreateViewResources(*context);
        if (!view)
        {
            return;
        }

        builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth, m_DepthHandle, RHI::ResourceState::ShaderResource);
        builder.TryReadTexture(RenderGraphResourceNames::GBufferNormal, m_NormalHandle, RHI::ResourceState::ShaderResource);
        m_PoolHandle = builder.ImportBuffer(view->Pool, RHI::ResourceState::UnorderedAccess, "GpuParticleSimulation.Pool");
        m_HeadsHandle = builder.ImportBuffer(view->Heads, RHI::ResourceState::UnorderedAccess, "GpuParticleSimulation.Heads");
        if (m_PoolHandle.IsValid() && m_HeadsHandle.IsValid())
        {
            builder.Write(m_PoolHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            builder.PublishBuffer(kGpuParticlePoolResourceName, m_PoolHandle);
            builder.Write(m_HeadsHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            builder.PreserveInsertionOrder();
        }
    }

    void GpuParticleSimulationPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bInitialized || !context.SnapshotScene || context.SnapshotScene->GpuParticles.IsEmpty() ||
            !context.CommandList || !context.FrameResources || !m_PoolHandle.IsValid() || !m_HeadsHandle.IsValid())
        {
            return;
        }
        ViewResources* view = FindOrCreateViewResources(context);
        RHI::BufferPtr pool = resources.GetBuffer(m_PoolHandle);
        RHI::BufferPtr heads = resources.GetBuffer(m_HeadsHandle);
        if (!view || !pool || !heads || pool != view->Pool || heads != view->Heads)
        {
            return;
        }

        const GpuParticleScene& scene = context.SnapshotScene->GpuParticles;
        Container::VariableArray<GpuEmitterParams> emitters;
        Container::VariableArray<GpuEmitterFrameExtra> extras;
        emitters.reserve(scene.Emitters.size());
        extras.reserve(scene.Emitters.size());
        uint32_t updateCount = 0;
        for (const GpuParticleEmitterFrame& emitter : scene.Emitters)
        {
            GpuEmitterParams params = emitter.Params;
            GpuEmitterFrameExtra extra{};
            extra.PoolSlot = emitter.PoolSlot;
            extra.UpdateOffset = updateCount;
            if (emitter.PoolSlot >= kGpuParticleMaxEmitters ||
                params.PoolOffset > kGpuParticlePoolCapacity ||
                params.Capacity > kGpuParticlePoolCapacity - params.PoolOffset ||
                params.Capacity > kGpuParticlePoolCapacity - updateCount)
            {
                // ジョブや衝突先の添字は写しの順のままなので、無効な行は容量だけを 0 にする。
                params.Capacity = 0;
            }
            else
            {
                updateCount += params.Capacity;
                if (params.Capacity != 0 && view->LastGeneration[emitter.PoolSlot] != emitter.AllocationGeneration)
                {
                    extra.Flags = kGpuEmitterExtraFlagReset;
                }
            }
            emitters.push_back(params);
            extras.push_back(extra);
        }

        double dt = scene.DeltaTime;
        double udt = scene.UnscaledDeltaTime;
        if (view->bHasTime && scene.ParticleTime >= view->ParticleTime && scene.UnscaledTime >= view->UnscaledTime)
        {
            dt = scene.ParticleTime - view->ParticleTime;
            udt = scene.UnscaledTime - view->UnscaledTime;
        }
        dt = std::isfinite(dt) ? std::max(dt, 0.0) : 0.0;
        udt = std::isfinite(udt) ? std::max(udt, 0.0) : 0.0;
        const double maxTime = static_cast<double>(kGpuParticleMaxSubstepSeconds) * MaxSubsteps;
        if ((dt > maxTime || udt > maxTime) && !m_bReportedClampedTime)
        {
            NORVES_LOG_WARNING("GpuParticleSimulationPass", "粒の更新時間が上限を超えたため、超過分を捨てます");
            m_bReportedClampedTime = true;
        }
        dt = std::min(dt, maxTime);
        udt = std::min(udt, maxTime);
        const uint32_t substeps = std::max(1u, std::min(MaxSubsteps, static_cast<uint32_t>(
            std::ceil(std::max(dt, udt) / static_cast<double>(kGpuParticleMaxSubstepSeconds)))));

        RHI::TexturePtr depth = m_DepthHandle.IsValid() ? resources.GetTexture(m_DepthHandle) : nullptr;
        RHI::TexturePtr normal = m_NormalHandle.IsValid() ? resources.GetTexture(m_NormalHandle) : nullptr;
        const CameraProxy* camera = context.GetActiveCamera();
        const bool bDepthCollision = depth && normal && camera && depth->GetWidth() != 0 && depth->GetHeight() != 0 &&
            depth->GetWidth() == normal->GetWidth() && depth->GetHeight() == normal->GetHeight();
        GpuParticleSimulationConstants constants{};
        CameraViewConstants cameraConstants;
        if (camera)
        {
            cameraConstants = CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
        }
        cameraConstants.CopyShaderViewProjection(constants.ViewProjection);
        cameraConstants.CopyShaderInverseViewProjection(constants.InverseViewProjection);
        cameraConstants.CopyCameraPosition(constants.CameraPosition, 3);
        constants.DeltaTime = static_cast<float>(dt / substeps);
        constants.UnscaledDeltaTime = static_cast<float>(udt / substeps);
        constants.SubstepCount = substeps;
        constants.EmitterCount = static_cast<uint32_t>(emitters.size());
        constants.UpdateCount = updateCount;
        constants.SpawnJobCount = static_cast<uint32_t>(scene.SpawnJobs.size());
        constants.JobSpawnCount = scene.JobSpawnCount;
        constants.BurstCount = static_cast<uint32_t>(scene.Bursts.Bursts.size());
        constants.TotalSpawnCount = scene.TotalSpawnCount;
        constants.DepthCollisionEnabled = bDepthCollision ? 1u : 0u;
        if (depth && depth->GetWidth() != 0 && depth->GetHeight() != 0)
        {
            constants.ScreenSize[0] = static_cast<float>(depth->GetWidth());
            constants.ScreenSize[1] = static_cast<float>(depth->GetHeight());
            constants.InvScreenSize[0] = 1.0f / constants.ScreenSize[0];
            constants.InvScreenSize[1] = 1.0f / constants.ScreenSize[1];
        }
        const WindParameters& wind = context.SnapshotScene->Wind;
        if (wind.bEnabled)
        {
            WindDirectionXZ(wind, constants.WindA[0], constants.WindA[1]);
            constants.WindA[2] = wind.BaseSpeed;
            constants.WindA[3] = wind.GustStrength;
        }
        constants.WindB[0] = wind.GustFrequencyHz;
        constants.WindB[1] = wind.GustScaleMeters;
        constants.WindB[2] = wind.Turbulence;
        constants.WindB[3] = WindClockSeconds(context.ExtensionFrame.GameTotalTime);

        FrameResourceLane& lane = *context.FrameResources;
        const uint32_t zero = 0;
        const auto upload = [&](const void* data, uint64_t size)
        {
            return lane.AllocateUploadData(size ? data : &zero, size ? size : sizeof(zero), FrameUploadUsage::Storage);
        };
        const FrameUploadAllocation emitterData = upload(emitters.data(), emitters.size() * sizeof(GpuEmitterParams));
        const FrameUploadAllocation extraData = upload(extras.data(), extras.size() * sizeof(GpuEmitterFrameExtra));
        const FrameUploadAllocation jobData = upload(scene.SpawnJobs.data(), scene.SpawnJobs.size() * sizeof(GpuSpawnJob));
        const FrameUploadAllocation burstData = upload(scene.Bursts.Bursts.data(), scene.Bursts.Bursts.size() * sizeof(GpuParticleBurst));
        if (!emitterData.IsValid() || !extraData.IsValid() || !jobData.IsValid() || !burstData.IsValid())
        {
            return;
        }

        RHI::DescriptorSetPtr sets[MaxSubsteps];
        for (uint32_t step = 0; step < substeps; ++step)
        {
            constants.SubstepIndex = step;
            const FrameUploadAllocation uniform = lane.AllocateConstants(constants);
            sets[step] = lane.AllocateDescriptorSet(m_DescriptorSetLayout).Set;
            if (!uniform.IsValid() || !sets[step])
            {
                return;
            }
            RHI::IDescriptorSet& set = *sets[step];
            set.BindConstantBuffer(0, uniform.Buffer, uniform.GetOffset32(), uniform.GetSize32());
            set.BindStorageBuffer(1, pool, 0, PoolBytes);
            set.BindStorageBuffer(2, emitterData.Buffer, emitterData.GetOffset32(), emitterData.GetSize32());
            set.BindStorageBuffer(3, extraData.Buffer, extraData.GetOffset32(), extraData.GetSize32());
            set.BindStorageBuffer(4, jobData.Buffer, jobData.GetOffset32(), jobData.GetSize32());
            set.BindStorageBuffer(5, burstData.Buffer, burstData.GetOffset32(), burstData.GetSize32());
            set.BindStorageBuffer(6, heads, 0, HeadBytes);
            set.BindTexture(7, bDepthCollision ? depth : m_EmptyTexture);
            set.BindTexture(8, bDepthCollision ? normal : m_EmptyTexture);
            set.BindTexture(9, m_EmptyHeightTexture);
            set.BindSampler(7, m_Sampler);
            set.BindSampler(8, m_Sampler);
            set.BindSampler(9, m_Sampler);
            set.Update();
        }

        RHI::ICommandList& commands = *context.CommandList;
        if (view->bNeedsClear)
        {
            commands.BufferBarrier(pool, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopyDest);
            commands.BufferBarrier(heads, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopyDest);
            commands.FillBuffer(pool, 0, PoolBytes, 0);
            commands.FillBuffer(heads, 0, HeadBytes, 0);
            commands.BufferBarrier(pool, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
            commands.BufferBarrier(heads, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
            view->bNeedsClear = false;
        }
        else
        {
            SynchronizeBuffers(commands, pool, heads);
        }

        const uint32_t updateGroups = updateCount / WorkgroupSize + (updateCount % WorkgroupSize != 0 ? 1u : 0u);
        const uint32_t spawnGroups = scene.TotalSpawnCount / WorkgroupSize +
            (scene.TotalSpawnCount % WorkgroupSize != 0 ? 1u : 0u);
        if (updateGroups > MaxWorkgroupCountX || spawnGroups > MaxWorkgroupCountX)
        {
            NORVES_LOG_WARNING("GpuParticleSimulationPass", "ディスパッチのグループ数が上限を超えたため、超過分を省きます update=%u spawn=%u",
                               updateGroups, spawnGroups);
        }
        if (updateCount != 0)
        {
            commands.SetPipeline(m_UpdatePipeline);
            for (uint32_t step = 0; step < substeps; ++step)
            {
                commands.SetDescriptorSet(sets[step]);
                commands.Dispatch(std::min(updateGroups, MaxWorkgroupCountX), 1, 1);
                SynchronizeBuffers(commands, pool, heads);
            }
        }
        if (scene.TotalSpawnCount != 0)
        {
            commands.SetPipeline(m_SpawnPipeline);
            commands.SetDescriptorSet(sets[substeps - 1u]);
            commands.Dispatch(std::min(spawnGroups, MaxWorkgroupCountX), 1, 1);
            SynchronizeBuffers(commands, pool, heads);
        }

        for (size_t index = 0; index < scene.Emitters.size(); ++index)
        {
            if (extras[index].Flags & kGpuEmitterExtraFlagReset)
            {
                const GpuParticleEmitterFrame& emitter = scene.Emitters[index];
                view->LastGeneration[emitter.PoolSlot] = emitter.AllocationGeneration;
            }
        }
        view->ParticleTime = scene.ParticleTime;
        view->UnscaledTime = scene.UnscaledTime;
        view->bHasTime = true;
    }

    Container::TUniquePtr<IViewPass> CreateGpuParticleSimulationPass(void*)
    {
        return Container::MakeUnique<GpuParticleSimulationPass>();
    }

    bool RegisterGpuParticleSimulationExtension(RenderExtensionRegistry& registry)
    {
        RenderExtensionDesc desc;
        desc.Name = ExtensionName;
        desc.Stage = RenderExtensionStage::GBuffer;
        desc.Order = 1 << 30;
        const bool bSimulationRegistered = registry.Register(desc, {CreateGpuParticleSimulationPass, nullptr});
        const bool bDrawRegistered = RegisterGpuParticleDrawExtensions(registry);
        return bSimulationRegistered && bDrawRegistered;
    }
}
