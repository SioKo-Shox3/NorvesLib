// FramePacketのTLASを使うDDGIプローブレイ問い合わせを実装する。
#include "Rendering/DDGIProbePass.h"

#include "Rendering/DDGIVolume.h"
#include "Rendering/FramePacket.h"
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

                const uint64_t vertexOffsetBytes =
                    static_cast<uint64_t>(snapshot.VertexOffset) * snapshot.VertexStride;
                const uint64_t vertexRangeBytes =
                    static_cast<uint64_t>(snapshot.VertexCount) * snapshot.VertexStride;
                const uint64_t indexOffsetBytes =
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
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    if (!IsFiniteNonNegative(snapshot.Material.BaseColor[channel]))
                    {
                        return false;
                    }
                    instanceData.BaseColor[channel] = snapshot.Material.BaseColor[channel];
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
        if (m_Pipeline && m_IrradianceUpdatePipeline && m_AtlasSampler &&
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

    DDGIProbePass::FrameResources* DDGIProbePass::FindOrCreateFrameResources(
        ViewRenderContext& context,
        uint32_t resultCount,
        uint32_t instanceDataCount)
    {
        const uint32_t viewId = context.PhysicalLighting.ViewId;
        const uint32_t viewportId = context.PhysicalLighting.ViewportId;
        const uint64_t requiredResultBufferSize =
            static_cast<uint64_t>(resultCount) * sizeof(DDGIProbeRayQueryResult);
        const uint32_t allocatedInstanceCount = instanceDataCount > 0u ? instanceDataCount : 1u;
        const uint64_t requiredInstanceDataBufferSize =
            static_cast<uint64_t>(allocatedInstanceCount) * sizeof(DDGIProbeRayInstanceData);
        if (requiredInstanceDataBufferSize > std::numeric_limits<uint32_t>::max())
        {
            return nullptr;
        }

        FrameResources* frameResources = nullptr;
        for (FrameResources& resources : m_FrameResources)
        {
            if (resources.FrameIndex == context.FrameIndex && resources.ViewId == viewId &&
                resources.ViewportId == viewportId)
            {
                frameResources = &resources;
                break;
            }
        }

        if (frameResources == nullptr)
        {
            FrameResources resources;
            resources.FrameIndex = context.FrameIndex;
            resources.ViewId = viewId;
            resources.ViewportId = viewportId;
            m_FrameResources.push_back(std::move(resources));
            frameResources = &m_FrameResources.back();
        }

        if (frameResources->ResultBuffer == nullptr ||
            frameResources->ResultBuffer->GetSize() < requiredResultBufferSize)
        {
            frameResources->DescriptorSet.reset();
            frameResources->ResultBuffer.reset();
            frameResources->ResultState = RHI::ResourceState::Undefined;

            RHI::BufferDesc resultBufferDesc;
            resultBufferDesc.Size = requiredResultBufferSize;
            resultBufferDesc.Usage = RHI::ResourceUsage::StorageBuffer |
                                     RHI::ResourceUsage::TransferSrc |
                                     RHI::ResourceUsage::TransferDst;
            resultBufferDesc.DebugName = "DDGIProbeRayQuery.Results";
            frameResources->ResultBuffer = context.Device->CreateBuffer(resultBufferDesc);
            if (frameResources->ResultBuffer == nullptr)
            {
                DisableAfterResourceFailure("プローブレイ結果bufferを作成できません");
                return nullptr;
            }
        }

        if (frameResources->ParametersBuffer == nullptr)
        {
            RHI::BufferDesc parametersBufferDesc(
                sizeof(DDGIProbeRayQueryParameters),
                RHI::ResourceUsage::ConstantBuffer,
                true,
                "DDGIProbeRayQuery.Parameters");
            frameResources->ParametersBuffer = context.Device->CreateBuffer(parametersBufferDesc);
            if (frameResources->ParametersBuffer == nullptr)
            {
                DisableAfterResourceFailure("プローブレイparameter bufferを作成できません");
                return nullptr;
            }
        }

        if (frameResources->InstanceDataBuffer == nullptr ||
            frameResources->InstanceDataBuffer->GetSize() < requiredInstanceDataBufferSize)
        {
            frameResources->DescriptorSet.reset();
            frameResources->InstanceDataBuffer.reset();

            RHI::BufferDesc instanceDataBufferDesc(
                requiredInstanceDataBufferSize,
                RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                true,
                "DDGIProbeRayQuery.InstanceData");
            frameResources->InstanceDataBuffer = context.Device->CreateBuffer(instanceDataBufferDesc);
            if (frameResources->InstanceDataBuffer == nullptr)
            {
                DisableAfterResourceFailure("プローブ材質/geometry snapshot bufferを作成できません");
                return nullptr;
            }
        }

        if (frameResources->DescriptorSet == nullptr)
        {
            frameResources->DescriptorSet = context.Device->CreateDescriptorSet(
                CreateDDGIProbeRayQueryDescriptorSetDesc());
            if (frameResources->DescriptorSet == nullptr)
            {
                DisableAfterResourceFailure("プローブレイdescriptor setを作成できません");
                return nullptr;
            }
        }
        frameResources->ResultCount = resultCount;
        return frameResources;
    }

    bool DDGIProbePass::Execute(ViewRenderContext& context)
    {
        bool bFrameResourceWasValid = false;
        uint32_t previousAtlasProbeCount = 0u;
        for (FrameResources& resources : m_FrameResources)
        {
            if (resources.FrameIndex == context.FrameIndex &&
                resources.ViewId == context.PhysicalLighting.ViewId &&
                resources.ViewportId == context.PhysicalLighting.ViewportId)
            {
                bFrameResourceWasValid = resources.bAtlasValid;
                previousAtlasProbeCount = resources.AtlasProbeCount;
                resources.bAtlasValid = false;
                resources.AtlasProbeCount = 0u;
                break;
            }
        }

        if (context.CommandList == nullptr || context.Device == nullptr ||
            context.SnapshotScene == nullptr || context.SnapshotRayTracingScene == nullptr ||
            context.SnapshotRayTracingScene->TopLevel == nullptr ||
            !context.SnapshotRayTracingScene->HasShadowCasters() ||
            context.FrameIndex >= FRAME_PACKET_BUFFER_COUNT ||
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
            physicalLighting.LightBuffer->GetSize() < physicalLighting.LightBufferSizeBytes)
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
        FrameResources* frameResources = nullptr;
        RHI::TexturePtr previousIrradianceAtlas;
        RHI::TexturePtr previousDistanceAtlas;
        RHI::ResourceState* previousIrradianceAtlasState = nullptr;
        RHI::ResourceState* previousDistanceAtlasState = nullptr;
        bool bHasPreviousAtlas = false;
        uint32_t instanceDataBufferSize = 0u;
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

            frameResources = FindOrCreateFrameResources(
                context, resultCount, static_cast<uint32_t>(instanceData.size()));
            if (frameResources == nullptr || frameResources->DescriptorSet == nullptr ||
                frameResources->ParametersBuffer == nullptr ||
                frameResources->InstanceDataBuffer == nullptr ||
                frameResources->ResultBuffer == nullptr)
            {
                DisableAfterResourceFailure("プローブレイ資源が不完全です");
                return false;
            }
            frameResources->GeometryBuffers = std::move(geometryBuffers);

            const uint32_t atlasArrayLayerCount = std::max(
                probeCount, DDGIProbeAtlasMinimumArrayLayerCount);
            const bool bAtlasResourcesMatch =
                frameResources->IrradianceAtlas && frameResources->DistanceAtlas &&
                frameResources->HistoryIrradianceAtlas && frameResources->HistoryDistanceAtlas &&
                frameResources->IrradianceAtlas->GetWidth() == DDGIProbeAtlasTexelCount &&
                frameResources->IrradianceAtlas->GetHeight() == DDGIProbeAtlasTexelCount &&
                frameResources->IrradianceAtlas->GetArraySize() == atlasArrayLayerCount &&
                frameResources->IrradianceAtlas->GetFormat() == RHI::Format::R16G16B16A16_FLOAT &&
                frameResources->DistanceAtlas->GetWidth() == DDGIProbeAtlasTexelCount &&
                frameResources->DistanceAtlas->GetHeight() == DDGIProbeAtlasTexelCount &&
                frameResources->DistanceAtlas->GetArraySize() == atlasArrayLayerCount &&
                frameResources->DistanceAtlas->GetFormat() == RHI::Format::R16G16_FLOAT &&
                frameResources->HistoryIrradianceAtlas->GetWidth() == DDGIProbeAtlasTexelCount &&
                frameResources->HistoryIrradianceAtlas->GetHeight() == DDGIProbeAtlasTexelCount &&
                frameResources->HistoryIrradianceAtlas->GetArraySize() == atlasArrayLayerCount &&
                frameResources->HistoryIrradianceAtlas->GetFormat() == RHI::Format::R16G16B16A16_FLOAT &&
                frameResources->HistoryDistanceAtlas->GetWidth() == DDGIProbeAtlasTexelCount &&
                frameResources->HistoryDistanceAtlas->GetHeight() == DDGIProbeAtlasTexelCount &&
                frameResources->HistoryDistanceAtlas->GetArraySize() == atlasArrayLayerCount &&
                frameResources->HistoryDistanceAtlas->GetFormat() == RHI::Format::R16G16_FLOAT;
            if (!bAtlasResourcesMatch)
            {
                frameResources->IrradianceAtlas.reset();
                frameResources->DistanceAtlas.reset();
                frameResources->HistoryIrradianceAtlas.reset();
                frameResources->HistoryDistanceAtlas.reset();
                frameResources->IrradianceAtlasState = RHI::ResourceState::Undefined;
                frameResources->DistanceAtlasState = RHI::ResourceState::Undefined;
                frameResources->HistoryIrradianceAtlasState = RHI::ResourceState::Undefined;
                frameResources->HistoryDistanceAtlasState = RHI::ResourceState::Undefined;
                frameResources->bAtlasValid = false;
                bFrameResourceWasValid = false;

                RHI::TextureDesc irradianceAtlasDesc;
                irradianceAtlasDesc.Width = DDGIProbeAtlasTexelCount;
                irradianceAtlasDesc.Height = DDGIProbeAtlasTexelCount;
                irradianceAtlasDesc.ArraySize = atlasArrayLayerCount;
                irradianceAtlasDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
                irradianceAtlasDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                            RHI::ResourceUsage::ShaderWrite |
                                            RHI::ResourceUsage::TransferSrc;
                irradianceAtlasDesc.DebugName = "DDGIProbeUpdate.IrradianceAtlas";
                frameResources->IrradianceAtlas = context.Device->CreateTexture(irradianceAtlasDesc);

                RHI::TextureDesc distanceAtlasDesc;
                distanceAtlasDesc.Width = DDGIProbeAtlasTexelCount;
                distanceAtlasDesc.Height = DDGIProbeAtlasTexelCount;
                distanceAtlasDesc.ArraySize = atlasArrayLayerCount;
                distanceAtlasDesc.TextureFormat = RHI::Format::R16G16_FLOAT;
                distanceAtlasDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                          RHI::ResourceUsage::ShaderWrite |
                                          RHI::ResourceUsage::TransferSrc;
                distanceAtlasDesc.DebugName = "DDGIProbeUpdate.DistanceAtlas";
                frameResources->DistanceAtlas = context.Device->CreateTexture(distanceAtlasDesc);
                irradianceAtlasDesc.DebugName = "DDGIProbeUpdate.HistoryIrradianceAtlas";
                frameResources->HistoryIrradianceAtlas = context.Device->CreateTexture(irradianceAtlasDesc);
                distanceAtlasDesc.DebugName = "DDGIProbeUpdate.HistoryDistanceAtlas";
                frameResources->HistoryDistanceAtlas = context.Device->CreateTexture(distanceAtlasDesc);
                if (!frameResources->IrradianceAtlas || !frameResources->DistanceAtlas ||
                    !frameResources->HistoryIrradianceAtlas || !frameResources->HistoryDistanceAtlas)
                {
                    DisableAfterResourceFailure("irradiance/distance atlasの履歴を作成できません");
                    return false;
                }
            }

            if (frameResources->BounceDescriptorSet == nullptr)
            {
                frameResources->BounceDescriptorSet = context.Device->CreateDescriptorSet(
                    CreateDDGIProbeIrradianceUpdateDescriptorSetDesc());
            }
            if (frameResources->UpdateDescriptorSet == nullptr)
            {
                frameResources->UpdateDescriptorSet = context.Device->CreateDescriptorSet(
                    CreateDDGIProbeIrradianceUpdateDescriptorSetDesc());
            }
            if (frameResources->BounceDescriptorSet == nullptr ||
                frameResources->UpdateDescriptorSet == nullptr)
            {
                DisableAfterResourceFailure("irradiance更新descriptor setを作成できません");
                return false;
            }

            if (frameResources->BounceParametersBuffer == nullptr)
            {
                RHI::BufferDesc parametersBufferDesc(
                    sizeof(DDGIProbeIrradianceUpdateParameters),
                    RHI::ResourceUsage::ConstantBuffer,
                    true,
                    "DDGIProbeUpdate.BounceParameters");
                frameResources->BounceParametersBuffer =
                    context.Device->CreateBuffer(parametersBufferDesc);
            }
            if (frameResources->UpdateParametersBuffer == nullptr)
            {
                RHI::BufferDesc parametersBufferDesc(
                    sizeof(DDGIProbeIrradianceUpdateParameters),
                    RHI::ResourceUsage::ConstantBuffer,
                    true,
                    "DDGIProbeUpdate.AtlasParameters");
                frameResources->UpdateParametersBuffer =
                    context.Device->CreateBuffer(parametersBufferDesc);
            }
            if (!frameResources->BounceParametersBuffer ||
                !frameResources->UpdateParametersBuffer)
            {
                DisableAfterResourceFailure("irradiance更新parameter bufferを作成できません");
                return false;
            }

            const bool bFrameResourceHistoryMatches =
                bFrameResourceWasValid && previousAtlasProbeCount == probeCount &&
                IsSameDDGIVolume(volume,
                                 frameResources->VolumeOrigin,
                                 frameResources->ProbeSpacing,
                                 frameResources->ProbeCounts);
            if (bFrameResourceHistoryMatches)
            {
                // 1スロットのswap chainでも、前回atlasを読みながら別atlasへ書けるようにする。
                std::swap(frameResources->IrradianceAtlas,
                          frameResources->HistoryIrradianceAtlas);
                std::swap(frameResources->DistanceAtlas,
                          frameResources->HistoryDistanceAtlas);
                std::swap(frameResources->IrradianceAtlasState,
                          frameResources->HistoryIrradianceAtlasState);
                std::swap(frameResources->DistanceAtlasState,
                          frameResources->HistoryDistanceAtlasState);
                previousIrradianceAtlas = frameResources->HistoryIrradianceAtlas;
                previousDistanceAtlas = frameResources->HistoryDistanceAtlas;
                previousIrradianceAtlasState = &frameResources->HistoryIrradianceAtlasState;
                previousDistanceAtlasState = &frameResources->HistoryDistanceAtlasState;
                bHasPreviousAtlas = true;
            }

            if (!bHasPreviousAtlas)
            {
                for (FrameResources& candidate : m_FrameResources)
                {
                    if (&candidate == frameResources || !candidate.bAtlasValid ||
                        candidate.FrameNumber == UINT64_MAX ||
                        candidate.FrameNumber + 1u != context.FrameNumber ||
                        candidate.ViewId != frameResources->ViewId ||
                        candidate.ViewportId != frameResources->ViewportId ||
                        candidate.AtlasProbeCount != probeCount ||
                        !IsSameDDGIVolume(volume,
                                          candidate.VolumeOrigin,
                                          candidate.ProbeSpacing,
                                          candidate.ProbeCounts) ||
                        !candidate.IrradianceAtlas || !candidate.DistanceAtlas)
                    {
                        continue;
                    }
                    previousIrradianceAtlas = candidate.IrradianceAtlas;
                    previousDistanceAtlas = candidate.DistanceAtlas;
                    previousIrradianceAtlasState = &candidate.IrradianceAtlasState;
                    previousDistanceAtlasState = &candidate.DistanceAtlasState;
                    bHasPreviousAtlas = true;
                    break;
                }
            }

            if (!bHasPreviousAtlas)
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
            frameResources->BounceParametersBuffer->Update(
                &bounceParameters, sizeof(bounceParameters));
            frameResources->UpdateParametersBuffer->Update(
                &updateParameters, sizeof(updateParameters));

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
            frameResources->ParametersBuffer->Update(&parameters, sizeof(parameters));

            DDGIProbeRayInstanceData emptyInstanceData;
            const void* instanceDataSource = instanceData.empty()
                ? static_cast<const void*>(&emptyInstanceData)
                : static_cast<const void*>(instanceData.data());
            instanceDataBufferSize = static_cast<uint32_t>(
                (instanceData.empty() ? 1u : instanceData.size()) *
                sizeof(DDGIProbeRayInstanceData));
            frameResources->InstanceDataBuffer->Update(
                instanceDataSource, instanceDataBufferSize);

            if (!frameResources->DescriptorSet->BindAccelerationStructure(
                0u, context.SnapshotRayTracingScene->TopLevel))
            {
                return false;
            }
            frameResources->DescriptorSet->BindConstantBuffer(
                1u,
                frameResources->ParametersBuffer,
                0u,
                static_cast<uint32_t>(sizeof(DDGIProbeRayQueryParameters)));
            frameResources->DescriptorSet->BindStorageBuffer(
                2u,
                frameResources->ResultBuffer,
                0u,
                resultBufferSize);
            frameResources->DescriptorSet->BindStorageBuffer(
                3u,
                frameResources->InstanceDataBuffer,
                0u,
                instanceDataBufferSize);
            frameResources->DescriptorSet->BindStorageBuffer(
                4u,
                physicalLighting.LightBuffer,
                0u,
                physicalLighting.LightBufferSizeBytes);
            frameResources->DescriptorSet->BindTexture(
                5u, physicalLighting.EnvironmentRadianceTexture);
            frameResources->DescriptorSet->BindSampler(
                5u, physicalLighting.EnvironmentRadianceSampler);
            frameResources->DescriptorSet->Update();

            const auto bindUpdateResources = [&](const RHI::DescriptorSetPtr& descriptorSet,
                                                 const RHI::BufferPtr& updateParametersBuffer)
            {
                if (!descriptorSet->BindAccelerationStructure(
                        0u, context.SnapshotRayTracingScene->TopLevel))
                {
                    return false;
                }
                descriptorSet->BindConstantBuffer(
                    1u,
                    updateParametersBuffer,
                    0u,
                    static_cast<uint32_t>(sizeof(DDGIProbeIrradianceUpdateParameters)));
                descriptorSet->BindStorageBuffer(
                    2u, frameResources->ResultBuffer, 0u, resultBufferSize);
                descriptorSet->BindStorageBuffer(
                    3u,
                    frameResources->InstanceDataBuffer,
                    0u,
                    instanceDataBufferSize);
                descriptorSet->BindTexture(4u, previousIrradianceAtlas);
                descriptorSet->BindSampler(4u, m_AtlasSampler);
                descriptorSet->BindTexture(5u, previousDistanceAtlas);
                descriptorSet->BindSampler(5u, m_AtlasSampler);
                descriptorSet->BindStorageTexture(6u, frameResources->IrradianceAtlas);
                descriptorSet->BindStorageTexture(7u, frameResources->DistanceAtlas);
                descriptorSet->Update();
                return true;
            };
            if (!bindUpdateResources(frameResources->BounceDescriptorSet,
                                     frameResources->BounceParametersBuffer) ||
                !bindUpdateResources(frameResources->UpdateDescriptorSet,
                                     frameResources->UpdateParametersBuffer))
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

            if (bHasPreviousAtlas)
            {
                if (previousIrradianceAtlasState == nullptr ||
                    previousDistanceAtlasState == nullptr)
                {
                    return false;
                }
                if (*previousIrradianceAtlasState != RHI::ResourceState::ShaderResource)
                {
                    context.CommandList->TextureBarrier(
                        previousIrradianceAtlas,
                        *previousIrradianceAtlasState,
                        RHI::ResourceState::ShaderResource,
                        0u,
                        0u,
                        0u,
                        0u);
                    *previousIrradianceAtlasState = RHI::ResourceState::ShaderResource;
                }
                if (*previousDistanceAtlasState != RHI::ResourceState::ShaderResource)
                {
                    context.CommandList->TextureBarrier(
                        previousDistanceAtlas,
                        *previousDistanceAtlasState,
                        RHI::ResourceState::ShaderResource,
                        0u,
                        0u,
                        0u,
                        0u);
                    *previousDistanceAtlasState = RHI::ResourceState::ShaderResource;
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

        context.CommandList->BufferBarrier(
            frameResources->ResultBuffer,
            frameResources->ResultState,
            RHI::ResourceState::CopyDest,
            0u,
            resultBufferSize);
        context.CommandList->FillBuffer(
            frameResources->ResultBuffer,
            0u,
            resultBufferSize,
            UINT32_MAX);
        context.CommandList->BufferBarrier(
            frameResources->ResultBuffer,
            RHI::ResourceState::CopyDest,
            RHI::ResourceState::UnorderedAccess,
            0u,
            resultBufferSize);

        context.CommandList->SetPipeline(m_Pipeline);
        context.CommandList->SetDescriptorSet(frameResources->DescriptorSet);
        const uint32_t groupCountX =
            (resultCount + DDGIProbeRayWorkgroupSize - 1u) / DDGIProbeRayWorkgroupSize;
        context.CommandList->Dispatch(groupCountX, 1u, 1u);
        context.CommandList->BufferBarrier(
            frameResources->ResultBuffer,
            RHI::ResourceState::UnorderedAccess,
            RHI::ResourceState::ShaderResource,
            0u,
            resultBufferSize);
        frameResources->ResultState = RHI::ResourceState::ShaderResource;

        context.CommandList->TextureBarrier(
            frameResources->IrradianceAtlas,
            frameResources->IrradianceAtlasState,
            RHI::ResourceState::UnorderedAccess,
            0u,
            0u,
            0u,
            0u);
        context.CommandList->TextureBarrier(
            frameResources->DistanceAtlas,
            frameResources->DistanceAtlasState,
            RHI::ResourceState::UnorderedAccess,
            0u,
            0u,
            0u,
            0u);
        frameResources->IrradianceAtlasState = RHI::ResourceState::UnorderedAccess;
        frameResources->DistanceAtlasState = RHI::ResourceState::UnorderedAccess;

        if (bHasPreviousAtlas)
        {
            context.CommandList->BufferBarrier(
                frameResources->ResultBuffer,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::UnorderedAccess,
                0u,
                resultBufferSize);
            context.CommandList->SetPipeline(m_IrradianceUpdatePipeline);
            context.CommandList->SetDescriptorSet(frameResources->BounceDescriptorSet);
            context.CommandList->Dispatch(groupCountX, 1u, 1u);
            context.CommandList->BufferBarrier(
                frameResources->ResultBuffer,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource,
                0u,
                resultBufferSize);
            frameResources->ResultState = RHI::ResourceState::ShaderResource;
        }

        context.CommandList->SetPipeline(m_IrradianceUpdatePipeline);
        context.CommandList->SetDescriptorSet(frameResources->UpdateDescriptorSet);
        const uint32_t atlasTexelCount =
            probeCount * DDGIProbeAtlasTexelCount * DDGIProbeAtlasTexelCount;
        const uint32_t atlasGroupCountX =
            (atlasTexelCount + DDGIProbeRayWorkgroupSize - 1u) /
            DDGIProbeRayWorkgroupSize;
        context.CommandList->Dispatch(atlasGroupCountX, 1u, 1u);
        context.CommandList->TextureBarrier(
            frameResources->IrradianceAtlas,
            RHI::ResourceState::UnorderedAccess,
            RHI::ResourceState::ShaderResource,
            0u,
            0u,
            0u,
            0u);
        context.CommandList->TextureBarrier(
            frameResources->DistanceAtlas,
            RHI::ResourceState::UnorderedAccess,
            RHI::ResourceState::ShaderResource,
            0u,
            0u,
            0u,
            0u);
        frameResources->IrradianceAtlasState = RHI::ResourceState::ShaderResource;
        frameResources->DistanceAtlasState = RHI::ResourceState::ShaderResource;
        frameResources->AtlasProbeCount = probeCount;
        frameResources->VolumeOrigin[0] = volume.Origin.x;
        frameResources->VolumeOrigin[1] = volume.Origin.y;
        frameResources->VolumeOrigin[2] = volume.Origin.z;
        frameResources->ProbeSpacing[0] = volume.ProbeSpacing.x;
        frameResources->ProbeSpacing[1] = volume.ProbeSpacing.y;
        frameResources->ProbeSpacing[2] = volume.ProbeSpacing.z;
        frameResources->ProbeCounts[0] = volume.ProbeCountX;
        frameResources->ProbeCounts[1] = volume.ProbeCountY;
        frameResources->ProbeCounts[2] = volume.ProbeCountZ;
        frameResources->FrameNumber = context.FrameNumber;
        frameResources->bAtlasValid = true;
        return true;
    }

    RHI::BufferPtr DDGIProbePass::GetResultBuffer(
        uint32_t frameIndex,
        uint32_t viewId,
        uint32_t viewportId) const
    {
        for (const FrameResources& resources : m_FrameResources)
        {
            if (resources.FrameIndex == frameIndex && resources.ViewId == viewId &&
                resources.ViewportId == viewportId)
            {
                return resources.ResultBuffer;
            }
        }
        return nullptr;
    }

    uint32_t DDGIProbePass::GetResultCount(
        uint32_t frameIndex,
        uint32_t viewId,
        uint32_t viewportId) const
    {
        for (const FrameResources& resources : m_FrameResources)
        {
            if (resources.FrameIndex == frameIndex && resources.ViewId == viewId &&
                resources.ViewportId == viewportId)
            {
                return resources.ResultCount;
            }
        }
        return 0u;
    }

    RHI::TexturePtr DDGIProbePass::GetIrradianceAtlas(
        uint32_t frameIndex,
        uint32_t viewId,
        uint32_t viewportId) const
    {
        for (const FrameResources& resources : m_FrameResources)
        {
            if (resources.FrameIndex == frameIndex && resources.ViewId == viewId &&
                resources.ViewportId == viewportId && resources.bAtlasValid)
            {
                return resources.IrradianceAtlas;
            }
        }
        return nullptr;
    }

    RHI::TexturePtr DDGIProbePass::GetDistanceAtlas(
        uint32_t frameIndex,
        uint32_t viewId,
        uint32_t viewportId) const
    {
        for (const FrameResources& resources : m_FrameResources)
        {
            if (resources.FrameIndex == frameIndex && resources.ViewId == viewId &&
                resources.ViewportId == viewportId && resources.bAtlasValid)
            {
                return resources.DistanceAtlas;
            }
        }
        return nullptr;
    }

    uint32_t DDGIProbePass::GetAtlasProbeCount(
        uint32_t frameIndex,
        uint32_t viewId,
        uint32_t viewportId) const
    {
        for (const FrameResources& resources : m_FrameResources)
        {
            if (resources.FrameIndex == frameIndex && resources.ViewId == viewId &&
                resources.ViewportId == viewportId && resources.bAtlasValid)
            {
                return resources.AtlasProbeCount;
            }
        }
        return 0u;
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
        m_FrameResources.clear();
        m_Pipeline.reset();
        m_ComputeShader.reset();
        m_IrradianceUpdatePipeline.reset();
        m_IrradianceUpdateShader.reset();
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
