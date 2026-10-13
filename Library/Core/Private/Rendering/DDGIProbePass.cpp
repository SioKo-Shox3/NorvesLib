// FramePacketのTLASを使うDDGIプローブレイ問い合わせを実装する。
#include "Rendering/DDGIProbePass.h"
#include "RHI/FrameResourceWriteTracker.h"

#include "Rendering/DDGIVolume.h"
#include "Rendering/FramePacket.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t DDGIProbeRayWorkgroupSize = 64u;
        constexpr uint32_t DDGIProbeAtlasTexelCount = 8u;
        constexpr uint32_t DDGIProbeAtlasInteriorTexelCount = 6u;
        constexpr uint32_t DDGIProbeAtlasMinimumArrayLayerCount = 2u;
        constexpr float DDGIProbeRayMinimumDistance = 0.001f;
        constexpr float DDGIProbeRayMaximumDistance = 10000.0f;
        constexpr float DDGIProbeShadowOriginOffset = 0.002f;
        constexpr uint32_t DDGIProbeMaximumInstanceCustomIndex = 0x00FFFFFFu;
        // この数より多くのフレームの間 Execute されないビュー・ビューポートの組は、消えたビューとして手放す
        // （同じ ID が後で別のビューに使い回されても、古い履歴を読ませない）
        constexpr uint64_t DDGIViewResourceRetireFrameCount = 8u;

        struct DDGIProbeRayQueryParameters
        {
            float VolumeOrigin[4] = {};
            float ProbeSpacing[4] = {};
            uint32_t ProbeCounts[4] = {};
            float RayLimits[4] = {};
            uint32_t SceneCounts[4] = {};
            float EnvironmentParameters[4] = {};
        };

        struct DDGIProbeIrradianceUpdateParameters
        {
            float VolumeOrigin[4] = {};
            float ProbeSpacing[4] = {};
            uint32_t ProbeCounts[4] = {};
            uint32_t AtlasInfo[4] = {};
            float RayLimits[4] = {};
            uint32_t PassInfo[4] = {};
        };

        struct DDGIProbeRayInstanceData
        {
            uint64_t VertexAddress = 0u;
            uint64_t IndexAddress = 0u;
            float BaseColor[4] = {};
            float EmissiveChromaticityAndLuminance[4] = {};
            uint32_t VertexStride = 0u;
            uint32_t VertexCount = 0u;
            uint32_t IndexCount = 0u;
            uint32_t CustomIndex = UINT32_MAX;
            float Transform[12] = {};
        };

        static_assert(sizeof(DDGIProbeRayQueryParameters) == 96u);
        static_assert(sizeof(DDGIProbeIrradianceUpdateParameters) == 96u);
        static_assert(sizeof(DDGIProbeRayInstanceData) == 112u);
        static_assert(sizeof(GPULightData) == 64u);

        bool IsFiniteNonNegative(float value)
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        bool TryBuildDDGIProbeInstanceData(
            const RayTracingSceneSnapshot& scene,
            Container::VariableArray<DDGIProbeRayInstanceData>& outInstances,
            Container::VariableArray<RHI::BufferPtr>& outGeometryBuffers)
        {
            outInstances.clear();
            outGeometryBuffers.clear();
            if (scene.Instances.size() > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }

            for (const RayTracingSceneInstanceSnapshot& snapshot : scene.Instances)
            {
                if (!snapshot.AccelerationStructureVertexBuffer ||
                    !snapshot.AccelerationStructureIndexBuffer ||
                    snapshot.VertexStride < sizeof(float) * 3u ||
                    snapshot.VertexCount < 3u ||
                    snapshot.IndexCount < 3u || snapshot.IndexCount % 3u != 0u ||
                    snapshot.Instance.customIndex > DDGIProbeMaximumInstanceCustomIndex)
                {
                    return false;
                }

                const uint64_t vertexOffsetBytes = snapshot.VertexBufferOffsetBytes +
                    static_cast<uint64_t>(snapshot.VertexOffset) * snapshot.VertexStride;
                const uint64_t vertexRangeBytes =
                    static_cast<uint64_t>(snapshot.VertexCount) * snapshot.VertexStride;
                const uint64_t indexOffsetBytes = snapshot.IndexBufferOffsetBytes +
                    static_cast<uint64_t>(snapshot.IndexOffset) * sizeof(uint32_t);
                const uint64_t indexRangeBytes =
                    static_cast<uint64_t>(snapshot.IndexCount) * sizeof(uint32_t);
                const RHI::BufferPtr& vertexBuffer = snapshot.AccelerationStructureVertexBuffer;
                const RHI::BufferPtr& indexBuffer = snapshot.AccelerationStructureIndexBuffer;
                if (vertexRangeBytes > std::numeric_limits<uint32_t>::max() ||
                    indexRangeBytes > std::numeric_limits<uint32_t>::max() ||
                    vertexOffsetBytes > vertexBuffer->GetSize() ||
                    vertexRangeBytes > vertexBuffer->GetSize() - vertexOffsetBytes ||
                    indexOffsetBytes > indexBuffer->GetSize() ||
                    indexRangeBytes > indexBuffer->GetSize() - indexOffsetBytes ||
                    vertexBuffer->GetDeviceAddress() == 0u ||
                    indexBuffer->GetDeviceAddress() == 0u ||
                    vertexBuffer->GetDeviceAddress() >
                        std::numeric_limits<uint64_t>::max() - vertexOffsetBytes ||
                    indexBuffer->GetDeviceAddress() >
                        std::numeric_limits<uint64_t>::max() - indexOffsetBytes)
                {
                    return false;
                }

                DDGIProbeRayInstanceData instanceData;
                instanceData.VertexAddress = vertexBuffer->GetDeviceAddress() + vertexOffsetBytes;
                instanceData.IndexAddress = indexBuffer->GetDeviceAddress() + indexOffsetBytes;
                // 命中面の反射率はGBuffer・パストレーサーと同じ規則（instance色×アルベドtexture、
                // 材質のBaseColorは使わない）で作る。探査光線はtextureを引かないので白とみなす。
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    if (!IsFiniteNonNegative(snapshot.Material.ObjectColor[channel]))
                    {
                        return false;
                    }
                    instanceData.BaseColor[channel] = snapshot.Material.ObjectColor[channel];
                }
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    if (!IsFiniteNonNegative(snapshot.Material.EmissiveColor[channel]))
                    {
                        return false;
                    }
                    instanceData.EmissiveChromaticityAndLuminance[channel] =
                        snapshot.Material.EmissiveColor[channel];
                }
                if (!IsFiniteNonNegative(snapshot.Material.EmissiveLuminanceNits))
                {
                    return false;
                }
                instanceData.EmissiveChromaticityAndLuminance[3] =
                    snapshot.Material.EmissiveLuminanceNits;
                instanceData.VertexStride = snapshot.VertexStride;
                instanceData.VertexCount = snapshot.VertexCount;
                instanceData.IndexCount = snapshot.IndexCount;
                instanceData.CustomIndex = snapshot.Instance.customIndex;
                for (uint32_t transformIndex = 0u; transformIndex < 12u; ++transformIndex)
                {
                    instanceData.Transform[transformIndex] =
                        snapshot.Instance.transform[transformIndex];
                }
                outInstances.push_back(instanceData);
                outGeometryBuffers.push_back(vertexBuffer);
                outGeometryBuffers.push_back(indexBuffer);
            }

            std::sort(outInstances.begin(), outInstances.end(),
                      [](const DDGIProbeRayInstanceData& lhs,
                         const DDGIProbeRayInstanceData& rhs)
                      {
                          return lhs.CustomIndex < rhs.CustomIndex;
                      });
            for (size_t index = 1u; index < outInstances.size(); ++index)
            {
                if (outInstances[index - 1u].CustomIndex == outInstances[index].CustomIndex)
                {
                    return false;
                }
            }
            return true;
        }

        RHI::DescriptorSetDesc CreateDDGIProbeRayQueryDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc descriptorSetDesc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::AccelerationStructure,
                RHI::ResourceBindType::ConstantBuffer,
                RHI::ResourceBindType::RWBuffer,
                RHI::ResourceBindType::StructuredBuffer,
                RHI::ResourceBindType::StructuredBuffer,
                RHI::ResourceBindType::CombinedImageSampler};
            for (uint32_t bindingIndex = 0u; bindingIndex < 6u; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = RHI::ShaderStage::Compute;
                descriptorSetDesc.bindings.push_back(binding);
            }
            return descriptorSetDesc;
        }

        RHI::DescriptorSetDesc CreateDDGIProbeIrradianceUpdateDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc descriptorSetDesc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::AccelerationStructure,
                RHI::ResourceBindType::ConstantBuffer,
                RHI::ResourceBindType::RWBuffer,
                RHI::ResourceBindType::StructuredBuffer,
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::RWTexture,
                RHI::ResourceBindType::RWTexture};
            for (uint32_t bindingIndex = 0u; bindingIndex < 8u; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = RHI::ShaderStage::Compute;
                descriptorSetDesc.bindings.push_back(binding);
            }
            return descriptorSetDesc;
        }

        bool IsSameDDGIVolume(const DDGIVolumeParameters& volume,
                              const float (&origin)[3],
                              const float (&spacing)[3],
                              const uint32_t (&counts)[3])
        {
            return volume.Origin.x == origin[0] &&
                   volume.Origin.y == origin[1] &&
                   volume.Origin.z == origin[2] &&
                   volume.ProbeSpacing.x == spacing[0] &&
                   volume.ProbeSpacing.y == spacing[1] &&
                   volume.ProbeSpacing.z == spacing[2] &&
                   volume.ProbeCountX == counts[0] &&
                   volume.ProbeCountY == counts[1] &&
                   volume.ProbeCountZ == counts[2];
        }
    }

    bool DDGIProbePass::EnsurePipeline(ViewRenderContext& context)
    {
        if (m_Device != nullptr && m_Device != context.Device)
        {
            Shutdown();
        }
        if (m_bUnavailable)
        {
            return false;
        }
        if (m_Pipeline && m_IrradianceUpdatePipeline && m_RayQueryDescriptorSetLayout &&
            m_IrradianceUpdateDescriptorSetLayout && m_AtlasSampler &&
            m_DefaultIrradianceAtlas && m_DefaultDistanceAtlas)
        {
            return true;
        }
        if (m_bPipelineAttempted || context.Device == nullptr ||
            context.ShaderMgr == nullptr || context.CommandList == nullptr)
        {
            return false;
        }

        const RHI::DeviceCapabilities& capabilities = context.Capabilities != nullptr
            ? *context.Capabilities
            : context.Device->GetCapabilities();
        if (!capabilities.RayTracing.bAccelerationStructure ||
            !capabilities.RayTracing.bRayQuery ||
            !capabilities.bBufferDeviceAddress ||
            !capabilities.bShaderInt64)
        {
            return false;
        }

        m_bPipelineAttempted = true;
        m_Device = context.Device;
        m_ComputeShader = context.ShaderMgr->LoadShader(
            "DDGI/ProbeRadiance.comp", RHI::ShaderStage::Compute);
        if (!m_ComputeShader)
        {
            DisableAfterResourceFailure("プローブレイ問い合わせシェーダーを読み込めません");
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = m_ComputeShader;
        pipelineDesc.descriptorSetLayouts.push_back(
            CreateDDGIProbeRayQueryDescriptorSetDesc());
        m_Pipeline = context.Device->CreateComputePipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            DisableAfterResourceFailure("プローブレイ問い合わせパイプラインを作成できません");
            return false;
        }

        m_IrradianceUpdateShader = context.ShaderMgr->LoadShader(
            "DDGI/ProbeIrradianceUpdate.comp", RHI::ShaderStage::Compute);
        if (!m_IrradianceUpdateShader)
        {
            DisableAfterResourceFailure("irradiance更新シェーダーを読み込めません");
            return false;
        }

        RHI::ComputePipelineDesc updatePipelineDesc;
        updatePipelineDesc.computeShader = m_IrradianceUpdateShader;
        updatePipelineDesc.descriptorSetLayouts.push_back(
            CreateDDGIProbeIrradianceUpdateDescriptorSetDesc());
        m_IrradianceUpdatePipeline = context.Device->CreateComputePipeline(updatePipelineDesc);
        if (!m_IrradianceUpdatePipeline)
        {
            DisableAfterResourceFailure("irradiance更新パイプラインを作成できません");
            return false;
        }

        // Execute ごとにフレームの持ち主から借りる記述子セットの共有のレイアウト
        m_RayQueryDescriptorSetLayout = context.Device->GetOrCreateDescriptorSetLayout(
            CreateDDGIProbeRayQueryDescriptorSetDesc());
        m_IrradianceUpdateDescriptorSetLayout = context.Device->GetOrCreateDescriptorSetLayout(
            CreateDDGIProbeIrradianceUpdateDescriptorSetDesc());
        if (!m_RayQueryDescriptorSetLayout || !m_IrradianceUpdateDescriptorSetLayout)
        {
            DisableAfterResourceFailure("記述子セットのレイアウトを作成できません");
            return false;
        }

        RHI::SamplerDesc atlasSamplerDesc;
        atlasSamplerDesc.filterMin = RHI::FilterMode::Linear;
        atlasSamplerDesc.filterMag = RHI::FilterMode::Linear;
        atlasSamplerDesc.filterMip = RHI::FilterMode::Point;
        atlasSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        atlasSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        atlasSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_AtlasSampler = context.Device->CreateSampler(atlasSamplerDesc);
        if (!m_AtlasSampler)
        {
            DisableAfterResourceFailure("irradiance atlas samplerを作成できません");
            return false;
        }

        RHI::TextureDesc defaultIrradianceDesc;
        defaultIrradianceDesc.Width = DDGIProbeAtlasTexelCount;
        defaultIrradianceDesc.Height = DDGIProbeAtlasTexelCount;
        defaultIrradianceDesc.ArraySize = DDGIProbeAtlasMinimumArrayLayerCount;
        defaultIrradianceDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        defaultIrradianceDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                     RHI::ResourceUsage::TransferDst;
        defaultIrradianceDesc.DebugName = "DDGIProbeUpdate.DefaultIrradiance";
        m_DefaultIrradianceAtlas = context.Device->CreateTexture(defaultIrradianceDesc);
        if (!m_DefaultIrradianceAtlas)
        {
            DisableAfterResourceFailure("既定irradiance atlasを作成できません");
            return false;
        }
        uint16_t emptyIrradiance[DDGIProbeAtlasTexelCount * DDGIProbeAtlasTexelCount * 4u] = {};
        m_DefaultIrradianceAtlas->Update(
            emptyIrradiance,
            DDGIProbeAtlasTexelCount * 4u * sizeof(uint16_t),
            sizeof(emptyIrradiance));

        RHI::TextureDesc defaultDistanceDesc;
        defaultDistanceDesc.Width = DDGIProbeAtlasTexelCount;
        defaultDistanceDesc.Height = DDGIProbeAtlasTexelCount;
        defaultDistanceDesc.ArraySize = DDGIProbeAtlasMinimumArrayLayerCount;
        defaultDistanceDesc.TextureFormat = RHI::Format::R16G16_FLOAT;
        defaultDistanceDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                   RHI::ResourceUsage::TransferDst;
        defaultDistanceDesc.DebugName = "DDGIProbeUpdate.DefaultDistance";
        m_DefaultDistanceAtlas = context.Device->CreateTexture(defaultDistanceDesc);
        if (!m_DefaultDistanceAtlas)
        {
            DisableAfterResourceFailure("既定distance atlasを作成できません");
            return false;
        }
        uint16_t emptyDistance[DDGIProbeAtlasTexelCount * DDGIProbeAtlasTexelCount * 2u] = {};
        m_DefaultDistanceAtlas->Update(
            emptyDistance,
            DDGIProbeAtlasTexelCount * 2u * sizeof(uint16_t),
            sizeof(emptyDistance));
        return true;
    }

    DDGIProbePass::ViewResources* DDGIProbePass::FindViewResources(uint32_t viewId, uint32_t viewportId)
    {
        for (ViewResources& resources : m_ViewResources)
        {
            if (resources.ViewId == viewId && resources.ViewportId == viewportId)
            {
                return &resources;
            }
        }
        return nullptr;
    }

    const DDGIProbePass::ViewResources* DDGIProbePass::FindViewResources(uint32_t viewId,
                                                                         uint32_t viewportId) const
    {
        for (const ViewResources& resources : m_ViewResources)
        {
            if (resources.ViewId == viewId && resources.ViewportId == viewportId)
            {
                return &resources;
            }
        }
        return nullptr;
    }

    void DDGIProbePass::RetireStaleViewResources(uint64_t frameNumber)
    {
        // 消えたビューの組を手放す。アトラス・結果のバッファの破棄はデバイスが提出の完了まで遅らせる
        for (size_t index = 0u; index < m_ViewResources.size();)
        {
            const ViewResources& resources = m_ViewResources[index];
            if (resources.LastExecuteFrameNumber != UINT64_MAX &&
                frameNumber > resources.LastExecuteFrameNumber &&
                frameNumber - resources.LastExecuteFrameNumber > DDGIViewResourceRetireFrameCount)
            {
                m_ViewResources.erase(m_ViewResources.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }
    }

    DDGIProbePass::ViewResources* DDGIProbePass::FindOrCreateViewResources(
        ViewRenderContext& context,
        uint32_t resultCount)
    {
        const uint32_t viewId = context.PhysicalLighting.ViewId;
        const uint32_t viewportId = context.PhysicalLighting.ViewportId;
        const uint64_t requiredResultBufferSize =
            static_cast<uint64_t>(resultCount) * sizeof(DDGIProbeRayQueryResult);

        ViewResources* viewResources = FindViewResources(viewId, viewportId);
        if (viewResources == nullptr)
        {
            ViewResources resources;
            resources.ViewId = viewId;
            resources.ViewportId = viewportId;
            resources.LastExecuteFrameNumber = context.FrameNumber;
            m_ViewResources.push_back(std::move(resources));
            viewResources = &m_ViewResources.back();
        }

        // 結果のバッファは GPU だけが書いて読む（CPU からは書かない）
        if (viewResources->ResultBuffer == nullptr ||
            viewResources->ResultBuffer->GetSize() < requiredResultBufferSize)
        {
            viewResources->ResultBuffer.reset();
            viewResources->ResultState = RHI::ResourceState::Undefined;

            RHI::BufferDesc resultBufferDesc;
            resultBufferDesc.Size = requiredResultBufferSize;
            resultBufferDesc.Usage = RHI::ResourceUsage::StorageBuffer |
                                     RHI::ResourceUsage::TransferSrc |
                                     RHI::ResourceUsage::TransferDst;
            resultBufferDesc.DebugName = "DDGIProbeRayQuery.Results";
            viewResources->ResultBuffer = context.Device->CreateBuffer(resultBufferDesc);
            if (viewResources->ResultBuffer == nullptr)
            {
                DisableAfterResourceFailure("プローブレイ結果bufferを作成できません");
                return nullptr;
            }
        }
        viewResources->ResultCount = resultCount;
        return viewResources;
    }

    bool DDGIProbePass::UploadFrameData(ViewRenderContext& context,
                                        const void* data,
                                        uint32_t size,
                                        bool bStorage,
                                        const char* unownedDebugName,
                                        RHI::BufferPtr& outBuffer,
                                        uint32_t& outOffset)
    {
        outBuffer.reset();
        outOffset = 0u;
        if (FrameResourceLane* frameResources = context.FrameResources)
        {
            const FrameUploadAllocation allocation = frameResources->AllocateUploadData(
                data,
                size,
                bStorage ? FrameUploadUsage::Storage : FrameUploadUsage::Constants);
            if (!allocation.IsValid())
            {
                return false;
            }
            outBuffer = allocation.Buffer;
            outOffset = allocation.GetOffset32();
            return true;
        }
        if (context.Device == nullptr)
        {
            return false;
        }
        // 持ち主の無い文脈では使い回さないバッファを作り、パスを手放すまで持つ
        outBuffer = context.Device->CreateBuffer(RHI::BufferDesc(
            size,
            bStorage ? (RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst)
                     : RHI::ResourceUsage::ConstantBuffer,
            true,
            unownedDebugName));
        if (!outBuffer)
        {
            return false;
        }
        outBuffer->Update(data, size);
        m_UnownedBuffers.push_back(outBuffer);
        return true;
    }

    RHI::DescriptorSetPtr DDGIProbePass::AllocateFrameDescriptorSet(ViewRenderContext& context,
                                                                    bool bRayQuery)
    {
        if (FrameResourceLane* frameResources = context.FrameResources)
        {
            return frameResources->AllocateDescriptorSet(
                bRayQuery ? m_RayQueryDescriptorSetLayout : m_IrradianceUpdateDescriptorSetLayout).Set;
        }
        if (context.Device == nullptr)
        {
            return nullptr;
        }
        RHI::DescriptorSetPtr descriptorSet = context.Device->CreateDescriptorSet(
            bRayQuery ? CreateDDGIProbeRayQueryDescriptorSetDesc()
                      : CreateDDGIProbeIrradianceUpdateDescriptorSetDesc());
        if (descriptorSet)
        {
            m_UnownedDescriptorSets.push_back(descriptorSet);
        }
        return descriptorSet;
    }

    bool DDGIProbePass::Execute(ViewRenderContext& context)
    {
        // LightingPass の中から呼ばれるので、守られていない上書きの見張りへはこのパスの名前で報告する
        RHI::FrameResourceWriteScope writeScope("DDGIProbePass");
        const uint32_t viewId = context.PhysicalLighting.ViewId;
        const uint32_t viewportId = context.PhysicalLighting.ViewportId;
        RetireStaleViewResources(context.FrameNumber);

        bool bViewHistoryWasValid = false;
        uint32_t previousAtlasProbeCount = 0u;
        if (ViewResources* existing = FindViewResources(viewId, viewportId))
        {
            // 前に記録したフレームを提出しなかったら、GPU はそのフレームの更新も状態の遷移も実行していない。
            // 記録した側の状態と入れ替えは GPU の実際と合わないので、履歴を捨てて状態を未定義から始め直す
            if (context.FrameResources != nullptr && existing->RecordedGeneration != 0u &&
                context.FrameResources->GetSubmissionOutcome(
                    {existing->RecordedGenerationSlot, existing->RecordedGeneration}) ==
                    FrameSubmissionOutcome::Aborted)
            {
                existing->bAtlasValid = false;
                existing->ResultState = RHI::ResourceState::Undefined;
                existing->IrradianceAtlasState = RHI::ResourceState::Undefined;
                existing->DistanceAtlasState = RHI::ResourceState::Undefined;
                existing->HistoryIrradianceAtlasState = RHI::ResourceState::Undefined;
                existing->HistoryDistanceAtlasState = RHI::ResourceState::Undefined;
            }
            existing->RecordedGenerationSlot = UINT32_MAX;
            existing->RecordedGeneration = 0u;
            existing->LastExecuteFrameNumber = context.FrameNumber;
            // この Execute が更新を終えるまで、getter は古いアトラスを返さない
            bViewHistoryWasValid = existing->bAtlasValid;
            previousAtlasProbeCount = existing->AtlasProbeCount;
            existing->bAtlasValid = false;
            existing->AtlasProbeCount = 0u;
        }

        if (context.CommandList == nullptr || context.Device == nullptr ||
            context.SnapshotScene == nullptr || context.SnapshotRayTracingScene == nullptr ||
            context.SnapshotRayTracingScene->TopLevel == nullptr ||
            !context.SnapshotRayTracingScene->HasShadowCasters() ||
            !context.PhysicalLighting.bLightingPublished ||
            !context.PhysicalLighting.LightBuffer ||
            !context.PhysicalLighting.EnvironmentRadianceTexture ||
            !context.PhysicalLighting.EnvironmentRadianceSampler)
        {
            return false;
        }

        const RHI::DeviceCapabilities& capabilities = context.Capabilities != nullptr
            ? *context.Capabilities
            : context.Device->GetCapabilities();
        if (!capabilities.RayTracing.bAccelerationStructure ||
            !capabilities.RayTracing.bRayQuery ||
            !capabilities.bBufferDeviceAddress ||
            !capabilities.bShaderInt64)
        {
            return false;
        }

        const PhysicalLightingResources& physicalLighting = context.PhysicalLighting;
        const uint64_t requiredLightBufferSize =
            static_cast<uint64_t>(physicalLighting.LogicalLightCount > 0u
                                      ? physicalLighting.LogicalLightCount
                                      : 1u) * sizeof(GPULightData);
        if (physicalLighting.LightBufferSizeBytes < requiredLightBufferSize ||
            physicalLighting.LightBuffer->GetSize() <
                static_cast<uint64_t>(physicalLighting.LightBufferOffset) + physicalLighting.LightBufferSizeBytes)
        {
            return false;
        }

        const DDGIVolumeParameters& volume = context.SnapshotScene->DDGIVolume;
        const uint32_t probeCount = GetDDGIProbeCount(volume);
        if (!IsDDGIVolumeValid(volume) || probeCount == 0u ||
            probeCount > DDGIMaxProbeCount)
        {
            return false;
        }

        const uint32_t resultCount = probeCount * DDGIProbeRayDirectionCount;
        const uint32_t resultBufferSize = resultCount * sizeof(DDGIProbeRayQueryResult);
        ViewResources* viewResources = nullptr;
        RHI::TexturePtr previousIrradianceAtlas;
        RHI::TexturePtr previousDistanceAtlas;
        bool bHasPreviousAtlas = false;
        RHI::DescriptorSetPtr rayQueryDescriptorSet;
        RHI::DescriptorSetPtr bounceDescriptorSet;
        RHI::DescriptorSetPtr updateDescriptorSet;
        Container::VariableArray<DDGIProbeRayInstanceData> instanceData;
        Container::VariableArray<RHI::BufferPtr> geometryBuffers;
        try
        {
            if (!TryBuildDDGIProbeInstanceData(*context.SnapshotRayTracingScene,
                                               instanceData,
                                               geometryBuffers))
            {
                return false;
            }
            if (!EnsurePipeline(context))
            {
                return false;
            }

            const uint64_t instanceDataByteCount =
                static_cast<uint64_t>(instanceData.empty() ? 1u : instanceData.size()) *
                sizeof(DDGIProbeRayInstanceData);
            if (instanceDataByteCount > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            const uint32_t instanceDataBufferSize = static_cast<uint32_t>(instanceDataByteCount);

            viewResources = FindOrCreateViewResources(context, resultCount);
            if (viewResources == nullptr || viewResources->ResultBuffer == nullptr)
            {
                DisableAfterResourceFailure("プローブレイ資源が不完全です");
                return false;
            }
            viewResources->GeometryBuffers = std::move(geometryBuffers);

            const uint32_t atlasArrayLayerCount = std::max(
                probeCount, DDGIProbeAtlasMinimumArrayLayerCount);
            // 照度atlasは全体（layer [0, probe数)）と間接光だけ（layer [probe数, 2×probe数)）の2組を持つ。
            const uint32_t irradianceArrayLayerCount = std::max(
                probeCount * 2u, DDGIProbeAtlasMinimumArrayLayerCount);
            const bool bAtlasResourcesMatch =
                viewResources->IrradianceAtlas && viewResources->DistanceAtlas &&
                viewResources->HistoryIrradianceAtlas && viewResources->HistoryDistanceAtlas &&
                viewResources->IrradianceAtlas->GetWidth() == DDGIProbeAtlasTexelCount &&
                viewResources->IrradianceAtlas->GetHeight() == DDGIProbeAtlasTexelCount &&
                viewResources->IrradianceAtlas->GetArraySize() == irradianceArrayLayerCount &&
                viewResources->IrradianceAtlas->GetFormat() == RHI::Format::R16G16B16A16_FLOAT &&
                viewResources->DistanceAtlas->GetWidth() == DDGIProbeAtlasTexelCount &&
                viewResources->DistanceAtlas->GetHeight() == DDGIProbeAtlasTexelCount &&
                viewResources->DistanceAtlas->GetArraySize() == atlasArrayLayerCount &&
                viewResources->DistanceAtlas->GetFormat() == RHI::Format::R16G16_FLOAT &&
                viewResources->HistoryIrradianceAtlas->GetWidth() == DDGIProbeAtlasTexelCount &&
                viewResources->HistoryIrradianceAtlas->GetHeight() == DDGIProbeAtlasTexelCount &&
                viewResources->HistoryIrradianceAtlas->GetArraySize() ==
                    irradianceArrayLayerCount &&
                viewResources->HistoryIrradianceAtlas->GetFormat() == RHI::Format::R16G16B16A16_FLOAT &&
                viewResources->HistoryDistanceAtlas->GetWidth() == DDGIProbeAtlasTexelCount &&
                viewResources->HistoryDistanceAtlas->GetHeight() == DDGIProbeAtlasTexelCount &&
                viewResources->HistoryDistanceAtlas->GetArraySize() == atlasArrayLayerCount &&
                viewResources->HistoryDistanceAtlas->GetFormat() == RHI::Format::R16G16_FLOAT;
            if (!bAtlasResourcesMatch)
            {
                viewResources->IrradianceAtlas.reset();
                viewResources->DistanceAtlas.reset();
                viewResources->HistoryIrradianceAtlas.reset();
                viewResources->HistoryDistanceAtlas.reset();
                viewResources->IrradianceAtlasState = RHI::ResourceState::Undefined;
                viewResources->DistanceAtlasState = RHI::ResourceState::Undefined;
                viewResources->HistoryIrradianceAtlasState = RHI::ResourceState::Undefined;
                viewResources->HistoryDistanceAtlasState = RHI::ResourceState::Undefined;
                viewResources->bAtlasValid = false;
                bViewHistoryWasValid = false;

                RHI::TextureDesc irradianceAtlasDesc;
                irradianceAtlasDesc.Width = DDGIProbeAtlasTexelCount;
                irradianceAtlasDesc.Height = DDGIProbeAtlasTexelCount;
                irradianceAtlasDesc.ArraySize = irradianceArrayLayerCount;
                irradianceAtlasDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
                irradianceAtlasDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                            RHI::ResourceUsage::ShaderWrite |
                                            RHI::ResourceUsage::TransferSrc;
                irradianceAtlasDesc.DebugName = "DDGIProbeUpdate.IrradianceAtlas";
                viewResources->IrradianceAtlas = context.Device->CreateTexture(irradianceAtlasDesc);

                RHI::TextureDesc distanceAtlasDesc;
                distanceAtlasDesc.Width = DDGIProbeAtlasTexelCount;
                distanceAtlasDesc.Height = DDGIProbeAtlasTexelCount;
                distanceAtlasDesc.ArraySize = atlasArrayLayerCount;
                distanceAtlasDesc.TextureFormat = RHI::Format::R16G16_FLOAT;
                distanceAtlasDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                          RHI::ResourceUsage::ShaderWrite |
                                          RHI::ResourceUsage::TransferSrc;
                distanceAtlasDesc.DebugName = "DDGIProbeUpdate.DistanceAtlas";
                viewResources->DistanceAtlas = context.Device->CreateTexture(distanceAtlasDesc);
                irradianceAtlasDesc.DebugName = "DDGIProbeUpdate.HistoryIrradianceAtlas";
                viewResources->HistoryIrradianceAtlas = context.Device->CreateTexture(irradianceAtlasDesc);
                distanceAtlasDesc.DebugName = "DDGIProbeUpdate.HistoryDistanceAtlas";
                viewResources->HistoryDistanceAtlas = context.Device->CreateTexture(distanceAtlasDesc);
                if (!viewResources->IrradianceAtlas || !viewResources->DistanceAtlas ||
                    !viewResources->HistoryIrradianceAtlas || !viewResources->HistoryDistanceAtlas)
                {
                    DisableAfterResourceFailure("irradiance/distance atlasの履歴を作成できません");
                    return false;
                }
            }

            // 前回この組で更新できたアトラスが今のvolumeと合えば、それを履歴として読み、もう一方へ書く
            const bool bViewHistoryMatches =
                bViewHistoryWasValid && previousAtlasProbeCount == probeCount &&
                IsSameDDGIVolume(volume,
                                 viewResources->VolumeOrigin,
                                 viewResources->ProbeSpacing,
                                 viewResources->ProbeCounts);
            if (bViewHistoryMatches)
            {
                // 前回のアトラスを読みながら別のアトラスへ書けるよう、現在と履歴を入れ替える
                std::swap(viewResources->IrradianceAtlas,
                          viewResources->HistoryIrradianceAtlas);
                std::swap(viewResources->DistanceAtlas,
                          viewResources->HistoryDistanceAtlas);
                std::swap(viewResources->IrradianceAtlasState,
                          viewResources->HistoryIrradianceAtlasState);
                std::swap(viewResources->DistanceAtlasState,
                          viewResources->HistoryDistanceAtlasState);
                previousIrradianceAtlas = viewResources->HistoryIrradianceAtlas;
                previousDistanceAtlas = viewResources->HistoryDistanceAtlas;
                bHasPreviousAtlas = true;
            }
            else
            {
                previousIrradianceAtlas = m_DefaultIrradianceAtlas;
                previousDistanceAtlas = m_DefaultDistanceAtlas;
            }

            DDGIProbeIrradianceUpdateParameters bounceParameters;
            DDGIProbeIrradianceUpdateParameters updateParameters;
            DDGIProbeIrradianceUpdateParameters* updateParameterSets[] = {
                &bounceParameters, &updateParameters};
            for (uint32_t passIndex = 0u; passIndex < 2u; ++passIndex)
            {
                DDGIProbeIrradianceUpdateParameters& updateParams = *updateParameterSets[passIndex];
                updateParams.VolumeOrigin[0] = volume.Origin.x;
                updateParams.VolumeOrigin[1] = volume.Origin.y;
                updateParams.VolumeOrigin[2] = volume.Origin.z;
                updateParams.ProbeSpacing[0] = volume.ProbeSpacing.x;
                updateParams.ProbeSpacing[1] = volume.ProbeSpacing.y;
                updateParams.ProbeSpacing[2] = volume.ProbeSpacing.z;
                updateParams.ProbeCounts[0] = volume.ProbeCountX;
                updateParams.ProbeCounts[1] = volume.ProbeCountY;
                updateParams.ProbeCounts[2] = volume.ProbeCountZ;
                updateParams.ProbeCounts[3] = probeCount;
                updateParams.AtlasInfo[0] = DDGIProbeAtlasTexelCount;
                updateParams.AtlasInfo[1] = DDGIProbeAtlasInteriorTexelCount;
                updateParams.AtlasInfo[2] = probeCount;
                updateParams.AtlasInfo[3] = DDGIProbeRayDirectionCount;
                updateParams.RayLimits[0] = DDGIProbeRayMinimumDistance;
                updateParams.RayLimits[1] = DDGIProbeRayMaximumDistance;
                updateParams.RayLimits[2] = DDGIProbeShadowOriginOffset;
                const double spacingX = volume.ProbeSpacing.x;
                const double spacingY = volume.ProbeSpacing.y;
                const double spacingZ = volume.ProbeSpacing.z;
                const double localMaxDistance = std::sqrt(
                    spacingX * spacingX + spacingY * spacingY + spacingZ * spacingZ) * 1.5;
                updateParams.RayLimits[3] = static_cast<float>(std::min(
                    static_cast<double>(DDGIProbeRayMaximumDistance), localMaxDistance));
                updateParams.PassInfo[0] = passIndex;
                updateParams.PassInfo[1] = bHasPreviousAtlas ? 1u : 0u;
                updateParams.PassInfo[2] = resultCount;
                updateParams.PassInfo[3] = static_cast<uint32_t>(instanceData.size());
            }

            DDGIProbeRayQueryParameters parameters;
            parameters.VolumeOrigin[0] = volume.Origin.x;
            parameters.VolumeOrigin[1] = volume.Origin.y;
            parameters.VolumeOrigin[2] = volume.Origin.z;
            parameters.ProbeSpacing[0] = volume.ProbeSpacing.x;
            parameters.ProbeSpacing[1] = volume.ProbeSpacing.y;
            parameters.ProbeSpacing[2] = volume.ProbeSpacing.z;
            parameters.ProbeCounts[0] = volume.ProbeCountX;
            parameters.ProbeCounts[1] = volume.ProbeCountY;
            parameters.ProbeCounts[2] = volume.ProbeCountZ;
            parameters.ProbeCounts[3] = DDGIProbeRayDirectionCount;
            parameters.RayLimits[0] = DDGIProbeRayMinimumDistance;
            parameters.RayLimits[1] = DDGIProbeRayMaximumDistance;
            parameters.RayLimits[2] = DDGIProbeShadowOriginOffset;
            parameters.RayLimits[3] = DDGIProbeRayMaximumDistance;
            parameters.SceneCounts[0] = static_cast<uint32_t>(instanceData.size());
            parameters.SceneCounts[1] = physicalLighting.LogicalLightCount;
            parameters.SceneCounts[2] = physicalLighting.bIBLEnabled ? 1u : 0u;
            parameters.EnvironmentParameters[0] =
                std::isfinite(physicalLighting.IBLIntensity) &&
                        physicalLighting.IBLIntensity > 0.0f
                    ? physicalLighting.IBLIntensity
                    : 0.0f;
            // y=静的HDR環境の放射輝度の上限（倍率を掛ける前。0は上限なし）
            parameters.EnvironmentParameters[1] =
                std::isfinite(physicalLighting.StaticEnvironmentMaxRadiance) &&
                        physicalLighting.StaticEnvironmentMaxRadiance > 0.0f
                    ? physicalLighting.StaticEnvironmentMaxRadiance
                    : 0.0f;

            // CPU が書く定数・instance の表・記述子セットは、この Execute の分をフレームの持ち主から借りる
            DDGIProbeRayInstanceData emptyInstanceData;
            const void* instanceDataSource = instanceData.empty()
                ? static_cast<const void*>(&emptyInstanceData)
                : static_cast<const void*>(instanceData.data());
            RHI::BufferPtr parametersBuffer;
            RHI::BufferPtr bounceParametersBuffer;
            RHI::BufferPtr updateParametersBuffer;
            RHI::BufferPtr instanceDataBuffer;
            uint32_t parametersOffset = 0u;
            uint32_t bounceParametersOffset = 0u;
            uint32_t updateParametersOffset = 0u;
            uint32_t instanceDataOffset = 0u;
            rayQueryDescriptorSet = AllocateFrameDescriptorSet(context, true);
            bounceDescriptorSet = AllocateFrameDescriptorSet(context, false);
            updateDescriptorSet = AllocateFrameDescriptorSet(context, false);
            if (!UploadFrameData(context, &parameters, sizeof(parameters), false,
                                 "DDGIProbeRayQuery.Parameters.Unowned",
                                 parametersBuffer, parametersOffset) ||
                !UploadFrameData(context, &bounceParameters, sizeof(bounceParameters), false,
                                 "DDGIProbeUpdate.BounceParameters.Unowned",
                                 bounceParametersBuffer, bounceParametersOffset) ||
                !UploadFrameData(context, &updateParameters, sizeof(updateParameters), false,
                                 "DDGIProbeUpdate.AtlasParameters.Unowned",
                                 updateParametersBuffer, updateParametersOffset) ||
                !UploadFrameData(context, instanceDataSource, instanceDataBufferSize, true,
                                 "DDGIProbeRayQuery.InstanceData.Unowned",
                                 instanceDataBuffer, instanceDataOffset) ||
                !rayQueryDescriptorSet || !bounceDescriptorSet || !updateDescriptorSet)
            {
                // 借りられなかったフレームは更新しない（次のフレームで借り直す。パスは無効にしない）
                NORVES_LOG_ERROR("DDGIProbePass",
                                 "定数か記述子セットを借りられないため、このフレームはDDGIを更新しません");
                return false;
            }

            if (!rayQueryDescriptorSet->BindAccelerationStructure(
                0u, context.SnapshotRayTracingScene->TopLevel))
            {
                return false;
            }
            rayQueryDescriptorSet->BindConstantBuffer(
                1u,
                parametersBuffer,
                parametersOffset,
                static_cast<uint32_t>(sizeof(DDGIProbeRayQueryParameters)));
            rayQueryDescriptorSet->BindStorageBuffer(
                2u,
                viewResources->ResultBuffer,
                0u,
                resultBufferSize);
            rayQueryDescriptorSet->BindStorageBuffer(
                3u,
                instanceDataBuffer,
                instanceDataOffset,
                instanceDataBufferSize);
            rayQueryDescriptorSet->BindStorageBuffer(
                4u,
                physicalLighting.LightBuffer,
                physicalLighting.LightBufferOffset,
                physicalLighting.LightBufferSizeBytes);
            rayQueryDescriptorSet->BindTexture(
                5u, physicalLighting.EnvironmentRadianceTexture);
            rayQueryDescriptorSet->BindSampler(
                5u, physicalLighting.EnvironmentRadianceSampler);
            rayQueryDescriptorSet->Update();

            const auto bindUpdateResources = [&](const RHI::DescriptorSetPtr& descriptorSet,
                                                 const RHI::BufferPtr& updateParametersSource,
                                                 uint32_t updateParametersSourceOffset)
            {
                if (!descriptorSet->BindAccelerationStructure(
                        0u, context.SnapshotRayTracingScene->TopLevel))
                {
                    return false;
                }
                descriptorSet->BindConstantBuffer(
                    1u,
                    updateParametersSource,
                    updateParametersSourceOffset,
                    static_cast<uint32_t>(sizeof(DDGIProbeIrradianceUpdateParameters)));
                descriptorSet->BindStorageBuffer(
                    2u, viewResources->ResultBuffer, 0u, resultBufferSize);
                descriptorSet->BindStorageBuffer(
                    3u,
                    instanceDataBuffer,
                    instanceDataOffset,
                    instanceDataBufferSize);
                descriptorSet->BindTexture(4u, previousIrradianceAtlas);
                descriptorSet->BindSampler(4u, m_AtlasSampler);
                descriptorSet->BindTexture(5u, previousDistanceAtlas);
                descriptorSet->BindSampler(5u, m_AtlasSampler);
                descriptorSet->BindStorageTexture(6u, viewResources->IrradianceAtlas);
                descriptorSet->BindStorageTexture(7u, viewResources->DistanceAtlas);
                descriptorSet->Update();
                return true;
            };
            if (!bindUpdateResources(bounceDescriptorSet, bounceParametersBuffer, bounceParametersOffset) ||
                !bindUpdateResources(updateDescriptorSet, updateParametersBuffer, updateParametersOffset))
            {
                return false;
            }

            if (!bHasPreviousAtlas)
            {
                if (m_DefaultIrradianceAtlasState == RHI::ResourceState::Undefined)
                {
                    context.CommandList->TextureBarrier(
                        m_DefaultIrradianceAtlas,
                        RHI::ResourceState::Undefined,
                        RHI::ResourceState::ShaderResource,
                        0u,
                        0u,
                        0u,
                        0u);
                    m_DefaultIrradianceAtlasState = RHI::ResourceState::ShaderResource;
                }
                if (m_DefaultDistanceAtlasState == RHI::ResourceState::Undefined)
                {
                    context.CommandList->TextureBarrier(
                        m_DefaultDistanceAtlas,
                        RHI::ResourceState::Undefined,
                        RHI::ResourceState::ShaderResource,
                        0u,
                        0u,
                        0u,
                        0u);
                    m_DefaultDistanceAtlasState = RHI::ResourceState::ShaderResource;
                }
            }
            else
            {
                if (viewResources->HistoryIrradianceAtlasState != RHI::ResourceState::ShaderResource)
                {
                    context.CommandList->TextureBarrier(
                        previousIrradianceAtlas,
                        viewResources->HistoryIrradianceAtlasState,
                        RHI::ResourceState::ShaderResource,
                        0u,
                        0u,
                        0u,
                        0u);
                    viewResources->HistoryIrradianceAtlasState = RHI::ResourceState::ShaderResource;
                }
                if (viewResources->HistoryDistanceAtlasState != RHI::ResourceState::ShaderResource)
                {
                    context.CommandList->TextureBarrier(
                        previousDistanceAtlas,
                        viewResources->HistoryDistanceAtlasState,
                        RHI::ResourceState::ShaderResource,
                        0u,
                        0u,
                        0u,
                        0u);
                    viewResources->HistoryDistanceAtlasState = RHI::ResourceState::ShaderResource;
                }
            }
        }
        catch (const std::exception& exception)
        {
            DisableAfterResourceFailure(exception.what());
            return false;
        }
        catch (...)
        {
            DisableAfterResourceFailure("不明な例外");
            return false;
        }

        // ここから GPU の命令を記録する。記録したフレームを提出しなかったら、次の Execute で履歴と状態を捨てる
        if (context.FrameResources != nullptr)
        {
            const FrameResourceGeneration generation = context.FrameResources->GetGeneration();
            viewResources->RecordedGenerationSlot = generation.SlotIndex;
            viewResources->RecordedGeneration = generation.Generation;
        }

        context.CommandList->BufferBarrier(
            viewResources->ResultBuffer,
            viewResources->ResultState,
            RHI::ResourceState::CopyDest,
            0u,
            resultBufferSize);
        context.CommandList->FillBuffer(
            viewResources->ResultBuffer,
            0u,
            resultBufferSize,
            UINT32_MAX);
        context.CommandList->BufferBarrier(
            viewResources->ResultBuffer,
            RHI::ResourceState::CopyDest,
            RHI::ResourceState::UnorderedAccess,
            0u,
            resultBufferSize);

        context.CommandList->SetPipeline(m_Pipeline);
        context.CommandList->SetDescriptorSet(rayQueryDescriptorSet);
        const uint32_t groupCountX =
            (resultCount + DDGIProbeRayWorkgroupSize - 1u) / DDGIProbeRayWorkgroupSize;
        context.CommandList->Dispatch(groupCountX, 1u, 1u);
        context.CommandList->BufferBarrier(
            viewResources->ResultBuffer,
            RHI::ResourceState::UnorderedAccess,
            RHI::ResourceState::ShaderResource,
            0u,
            resultBufferSize);
        viewResources->ResultState = RHI::ResourceState::ShaderResource;

        context.CommandList->TextureBarrier(
            viewResources->IrradianceAtlas,
            viewResources->IrradianceAtlasState,
            RHI::ResourceState::UnorderedAccess,
            0u,
            0u,
            0u,
            0u);
        context.CommandList->TextureBarrier(
            viewResources->DistanceAtlas,
            viewResources->DistanceAtlasState,
            RHI::ResourceState::UnorderedAccess,
            0u,
            0u,
            0u,
            0u);
        viewResources->IrradianceAtlasState = RHI::ResourceState::UnorderedAccess;
        viewResources->DistanceAtlasState = RHI::ResourceState::UnorderedAccess;

        if (bHasPreviousAtlas)
        {
            context.CommandList->BufferBarrier(
                viewResources->ResultBuffer,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::UnorderedAccess,
                0u,
                resultBufferSize);
            context.CommandList->SetPipeline(m_IrradianceUpdatePipeline);
            context.CommandList->SetDescriptorSet(bounceDescriptorSet);
            context.CommandList->Dispatch(groupCountX, 1u, 1u);
            context.CommandList->BufferBarrier(
                viewResources->ResultBuffer,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource,
                0u,
                resultBufferSize);
            viewResources->ResultState = RHI::ResourceState::ShaderResource;
        }

        context.CommandList->SetPipeline(m_IrradianceUpdatePipeline);
        context.CommandList->SetDescriptorSet(updateDescriptorSet);
        const uint32_t atlasTexelCount =
            probeCount * DDGIProbeAtlasTexelCount * DDGIProbeAtlasTexelCount;
        const uint32_t atlasGroupCountX =
            (atlasTexelCount + DDGIProbeRayWorkgroupSize - 1u) /
            DDGIProbeRayWorkgroupSize;
        context.CommandList->Dispatch(atlasGroupCountX, 1u, 1u);
        context.CommandList->TextureBarrier(
            viewResources->IrradianceAtlas,
            RHI::ResourceState::UnorderedAccess,
            RHI::ResourceState::ShaderResource,
            0u,
            0u,
            0u,
            0u);
        context.CommandList->TextureBarrier(
            viewResources->DistanceAtlas,
            RHI::ResourceState::UnorderedAccess,
            RHI::ResourceState::ShaderResource,
            0u,
            0u,
            0u,
            0u);
        viewResources->IrradianceAtlasState = RHI::ResourceState::ShaderResource;
        viewResources->DistanceAtlasState = RHI::ResourceState::ShaderResource;
        viewResources->AtlasProbeCount = probeCount;
        viewResources->VolumeOrigin[0] = volume.Origin.x;
        viewResources->VolumeOrigin[1] = volume.Origin.y;
        viewResources->VolumeOrigin[2] = volume.Origin.z;
        viewResources->ProbeSpacing[0] = volume.ProbeSpacing.x;
        viewResources->ProbeSpacing[1] = volume.ProbeSpacing.y;
        viewResources->ProbeSpacing[2] = volume.ProbeSpacing.z;
        viewResources->ProbeCounts[0] = volume.ProbeCountX;
        viewResources->ProbeCounts[1] = volume.ProbeCountY;
        viewResources->ProbeCounts[2] = volume.ProbeCountZ;
        viewResources->bAtlasValid = true;
        return true;
    }

    RHI::BufferPtr DDGIProbePass::GetResultBuffer(uint32_t viewId, uint32_t viewportId) const
    {
        const ViewResources* resources = FindViewResources(viewId, viewportId);
        return resources != nullptr ? resources->ResultBuffer : nullptr;
    }

    uint32_t DDGIProbePass::GetResultCount(uint32_t viewId, uint32_t viewportId) const
    {
        const ViewResources* resources = FindViewResources(viewId, viewportId);
        return resources != nullptr ? resources->ResultCount : 0u;
    }

    RHI::TexturePtr DDGIProbePass::GetIrradianceAtlas(uint32_t viewId, uint32_t viewportId) const
    {
        const ViewResources* resources = FindViewResources(viewId, viewportId);
        return resources != nullptr && resources->bAtlasValid ? resources->IrradianceAtlas : nullptr;
    }

    RHI::TexturePtr DDGIProbePass::GetDistanceAtlas(uint32_t viewId, uint32_t viewportId) const
    {
        const ViewResources* resources = FindViewResources(viewId, viewportId);
        return resources != nullptr && resources->bAtlasValid ? resources->DistanceAtlas : nullptr;
    }

    uint32_t DDGIProbePass::GetAtlasProbeCount(uint32_t viewId, uint32_t viewportId) const
    {
        const ViewResources* resources = FindViewResources(viewId, viewportId);
        return resources != nullptr && resources->bAtlasValid ? resources->AtlasProbeCount : 0u;
    }

    void DDGIProbePass::DisableAfterResourceFailure(const char* reason)
    {
        m_bUnavailable = true;
        if (!m_bFailureReported)
        {
            NORVES_LOG_WARNING("DDGIProbePass",
                               "DDGI probe更新の準備に失敗したため無効化します: %s",
                               reason != nullptr ? reason : "理由不明");
            m_bFailureReported = true;
        }
    }

    void DDGIProbePass::Shutdown()
    {
        m_ViewResources.clear();
        m_UnownedBuffers.clear();
        m_UnownedDescriptorSets.clear();
        m_Pipeline.reset();
        m_ComputeShader.reset();
        m_IrradianceUpdatePipeline.reset();
        m_IrradianceUpdateShader.reset();
        m_RayQueryDescriptorSetLayout.reset();
        m_IrradianceUpdateDescriptorSetLayout.reset();
        m_AtlasSampler.reset();
        m_DefaultIrradianceAtlas.reset();
        m_DefaultDistanceAtlas.reset();
        m_DefaultIrradianceAtlasState = RHI::ResourceState::Undefined;
        m_DefaultDistanceAtlasState = RHI::ResourceState::Undefined;
        m_Device = nullptr;
        m_bPipelineAttempted = false;
        m_bUnavailable = false;
        m_bFailureReported = false;
    }
} // namespace NorvesLib::Core::Rendering
