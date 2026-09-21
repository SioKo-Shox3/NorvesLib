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
        };

        static_assert(sizeof(DDGIProbeRayQueryParameters) == 96u);
        static_assert(sizeof(DDGIProbeRayInstanceData) == 64u);
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
        if (m_Pipeline)
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
        if (context.CommandList == nullptr || context.Device == nullptr ||
            context.SnapshotScene == nullptr || context.SnapshotRayTracingScene == nullptr ||
            context.SnapshotRayTracingScene->TopLevel == nullptr ||
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
            const uint32_t instanceDataBufferSize = static_cast<uint32_t>(
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
        m_Device = nullptr;
        m_bPipelineAttempted = false;
        m_bUnavailable = false;
        m_bFailureReported = false;
    }
} // namespace NorvesLib::Core::Rendering
