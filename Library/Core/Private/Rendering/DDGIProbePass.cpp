// FramePacketのTLASを使うDDGIプローブレイ問い合わせを実装する。
#include "Rendering/DDGIProbePass.h"

#include "Rendering/DDGIVolume.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"
#include "Logging/LogMacros.h"

#include <cstdint>
#include <exception>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t DDGIProbeRayWorkgroupSize = 64u;
        constexpr float DDGIProbeRayMinimumDistance = 0.001f;
        constexpr float DDGIProbeRayMaximumDistance = 10000.0f;

        struct DDGIProbeRayQueryParameters
        {
            float VolumeOrigin[4] = {};
            float ProbeSpacing[4] = {};
            uint32_t ProbeCounts[4] = {};
            float RayLimits[4] = {};
        };

        static_assert(sizeof(DDGIProbeRayQueryParameters) == 64u);

        RHI::DescriptorSetDesc CreateDDGIProbeRayQueryDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc descriptorSetDesc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::AccelerationStructure,
                RHI::ResourceBindType::ConstantBuffer,
                RHI::ResourceBindType::RWBuffer};
            for (uint32_t bindingIndex = 0u; bindingIndex < 3u; ++bindingIndex)
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
            !capabilities.RayTracing.bRayQuery)
        {
            return false;
        }

        m_bPipelineAttempted = true;
        m_Device = context.Device;
        m_ComputeShader = context.ShaderMgr->LoadShader(
            "DDGI/ProbeRayQuery.comp", RHI::ShaderStage::Compute);
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
        uint32_t resultCount)
    {
        const uint32_t viewId = context.PhysicalLighting.ViewId;
        const uint32_t viewportId = context.PhysicalLighting.ViewportId;
        const uint64_t requiredResultBufferSize =
            static_cast<uint64_t>(resultCount) * sizeof(DDGIProbeRayQueryResult);

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
            context.FrameIndex >= FRAME_PACKET_BUFFER_COUNT)
        {
            return false;
        }

        const RHI::DeviceCapabilities& capabilities = context.Capabilities != nullptr
            ? *context.Capabilities
            : context.Device->GetCapabilities();
        if (!capabilities.RayTracing.bAccelerationStructure ||
            !capabilities.RayTracing.bRayQuery)
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
        try
        {
            if (!EnsurePipeline(context))
            {
                return false;
            }

            frameResources = FindOrCreateFrameResources(context, resultCount);
            if (frameResources == nullptr || frameResources->DescriptorSet == nullptr ||
                frameResources->ParametersBuffer == nullptr || frameResources->ResultBuffer == nullptr)
            {
                DisableAfterResourceFailure("プローブレイ資源が不完全です");
                return false;
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
            frameResources->ParametersBuffer->Update(&parameters, sizeof(parameters));

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
