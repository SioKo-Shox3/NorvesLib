// RT可視性を生成する描画passを実装する。
#include "Rendering/RayTracingShadowPass.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/DirectionalShadowLightMatrices.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct RayTracingShadowParameters
        {
            float InverseViewProjection[16] = {};
            float LightDirectionAndBias[4] = {};
        };

        static_assert(sizeof(RayTracingShadowParameters) == 80u);

        RHI::DescriptorSetDesc CreateRayTracingShadowDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc descriptorSetDesc;
            const RHI::ShaderStage stages = RHI::ShaderStage::AllRayTracing;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::AccelerationStructure,
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::ConstantBuffer,
                RHI::ResourceBindType::RWTexture};
            for (uint32_t bindingIndex = 0u; bindingIndex < 5u; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = stages;
                descriptorSetDesc.bindings.push_back(binding);
            }
            return descriptorSetDesc;
        }

        bool TryGetDirectionalLightDirection(const ViewRenderContext& context,
                                             float (&outDirection)[3])
        {
            if (context.SnapshotLightProxies == nullptr)
            {
                return false;
            }

            // LightingはCSMと同じ規則で選んだ一灯だけへRT影の係数を掛ける。
            const LightProxy* shadowLight =
                SelectShadowedDirectionalLight(context.SnapshotLightProxies);
            if (shadowLight == nullptr)
            {
                return false;
            }

            const float lengthSquared = shadowLight->DirectionX * shadowLight->DirectionX +
                                        shadowLight->DirectionY * shadowLight->DirectionY +
                                        shadowLight->DirectionZ * shadowLight->DirectionZ;
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-10f)
            {
                return false;
            }

            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            if (!std::isfinite(inverseLength))
            {
                return false;
            }

            outDirection[0] = -shadowLight->DirectionX * inverseLength;
            outDirection[1] = -shadowLight->DirectionY * inverseLength;
            outDirection[2] = -shadowLight->DirectionZ * inverseLength;
            return true;
        }
    }

    bool RayTracingShadowPass::EnsurePipeline(ViewRenderContext& context)
    {
        if (m_Pipeline)
        {
            return true;
        }
        if (m_bPipelineAttempted || m_bPipelineUnavailable || context.Device == nullptr ||
            context.ShaderMgr == nullptr || context.CommandList == nullptr)
        {
            return false;
        }
        m_bPipelineAttempted = true;

        const RHI::RayTracingCapabilities& capabilities = context.Device->GetCapabilities().RayTracing;
        if (!capabilities.bAccelerationStructure || !capabilities.bRayTracingPipeline)
        {
            m_bPipelineUnavailable = true;
            return false;
        }

        m_RayGenerationShader = context.ShaderMgr->LoadShader(
            "RayTracing/RayTracingShadowRayGen.glsl", RHI::ShaderStage::RayGen);
        m_MissShader = context.ShaderMgr->LoadShader(
            "RayTracing/RayTracingShadowMiss.glsl", RHI::ShaderStage::Miss);
        m_ClosestHitShader = context.ShaderMgr->LoadShader(
            "RayTracing/RayTracingShadowClosestHit.glsl", RHI::ShaderStage::ClosestHit);
        if (!m_RayGenerationShader || !m_MissShader || !m_ClosestHitShader)
        {
            m_bPipelineUnavailable = true;
            return false;
        }

        RHI::RayTracingPipelineDesc pipelineDesc;
        RHI::RayTracingShaderGroupDesc rayGenerationGroup;
        rayGenerationGroup.type = RHI::RayTracingShaderGroupType::General;
        rayGenerationGroup.generalShader = m_RayGenerationShader;
        pipelineDesc.shaderGroups.push_back(rayGenerationGroup);

        RHI::RayTracingShaderGroupDesc missGroup;
        missGroup.type = RHI::RayTracingShaderGroupType::General;
        missGroup.generalShader = m_MissShader;
        pipelineDesc.shaderGroups.push_back(missGroup);

        RHI::RayTracingShaderGroupDesc closestHitGroup;
        closestHitGroup.type = RHI::RayTracingShaderGroupType::TrianglesHit;
        closestHitGroup.closestHitShader = m_ClosestHitShader;
        pipelineDesc.shaderGroups.push_back(closestHitGroup);
        pipelineDesc.descriptorSetLayouts.push_back(CreateRayTracingShadowDescriptorSetDesc());

        if (!RHI::IsValidRayTracingPipelineDesc(pipelineDesc))
        {
            m_bPipelineUnavailable = true;
            return false;
        }
        m_Pipeline = context.Device->CreateRayTracingPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            m_bPipelineUnavailable = true;
            return false;
        }

        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = context.Device->CreateSampler(samplerDesc);
        if (!m_Sampler)
        {
            m_Pipeline.reset();
            m_bPipelineUnavailable = true;
            return false;
        }

        return true;
    }

    RayTracingShadowPass::FrameResources* RayTracingShadowPass::FindOrCreateFrameResources(
        ViewRenderContext& context,
        uint32_t width,
        uint32_t height)
    {
        const uint32_t viewId = context.PhysicalLighting.ViewId;
        const uint32_t viewportId = context.PhysicalLighting.ViewportId;
        for (FrameResources& resources : m_FrameResources)
        {
            if (resources.FrameIndex == context.FrameIndex && resources.ViewId == viewId &&
                resources.ViewportId == viewportId)
            {
                if (resources.Width != width || resources.Height != height)
                {
                    resources.VisibilityTexture.reset();
                    resources.Width = 0u;
                    resources.Height = 0u;
                    resources.VisibilityState = RHI::ResourceState::Undefined;
                }
                if (!resources.VisibilityTexture)
                {
                    RHI::TextureDesc visibilityDesc;
                    visibilityDesc.Width = width;
                    visibilityDesc.Height = height;
                    visibilityDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
                    visibilityDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                           RHI::ResourceUsage::ShaderWrite;
                    visibilityDesc.DebugName = "RayTracingShadow.Visibility";
                    resources.VisibilityTexture = context.Device->CreateTexture(visibilityDesc);
                    if (!resources.VisibilityTexture)
                    {
                        return nullptr;
                    }
                    resources.Width = width;
                    resources.Height = height;
                    resources.VisibilityState = RHI::ResourceState::Undefined;
                }
                return &resources;
            }
        }

        FrameResources resources;
        resources.FrameIndex = context.FrameIndex;
        resources.ViewId = viewId;
        resources.ViewportId = viewportId;
        resources.Width = width;
        resources.Height = height;

        RHI::TextureDesc visibilityDesc;
        visibilityDesc.Width = width;
        visibilityDesc.Height = height;
        visibilityDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
        visibilityDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
        visibilityDesc.DebugName = "RayTracingShadow.Visibility";
        resources.VisibilityTexture = context.Device->CreateTexture(visibilityDesc);
        if (!resources.VisibilityTexture)
        {
            return nullptr;
        }

        RHI::BufferDesc parametersDesc(sizeof(RayTracingShadowParameters),
                                       RHI::ResourceUsage::ConstantBuffer,
                                       true,
                                       "RayTracingShadow.Parameters");
        resources.ParametersBuffer = context.Device->CreateBuffer(parametersDesc);
        if (!resources.ParametersBuffer)
        {
            return nullptr;
        }

        resources.DescriptorSet = context.Device->CreateDescriptorSet(
            CreateRayTracingShadowDescriptorSetDesc());
        if (!resources.DescriptorSet)
        {
            return nullptr;
        }

        m_FrameResources.push_back(std::move(resources));
        return &m_FrameResources.back();
    }

    bool RayTracingShadowPass::Execute(ViewRenderContext& context,
                                      const RHI::TexturePtr& depthTexture,
                                      const RHI::TexturePtr& normalTexture,
                                      bool bEnabled,
                                      RHI::TexturePtr& outVisibilityTexture)
    {
        outVisibilityTexture.reset();
        if (!bEnabled || !EnsurePipeline(context) || !context.SnapshotRayTracingScene ||
            !context.SnapshotRayTracingScene->TopLevel ||
            !context.SnapshotRayTracingScene->HasShadowCasters() || !depthTexture || !normalTexture ||
            depthTexture->GetWidth() == 0u || depthTexture->GetHeight() == 0u ||
            depthTexture->GetWidth() != normalTexture->GetWidth() ||
            depthTexture->GetHeight() != normalTexture->GetHeight() ||
            context.FrameIndex >= FRAME_PACKET_BUFFER_COUNT)
        {
            return false;
        }

        float lightDirection[3] = {};
        if (!TryGetDirectionalLightDirection(context, lightDirection))
        {
            return false;
        }

        const CameraProxy* activeCamera = context.GetActiveCamera();
        if (activeCamera == nullptr)
        {
            return false;
        }

        FrameResources* frameResources = FindOrCreateFrameResources(
            context, depthTexture->GetWidth(), depthTexture->GetHeight());
        if (frameResources == nullptr || !frameResources->DescriptorSet ||
            !frameResources->ParametersBuffer || !frameResources->VisibilityTexture)
        {
            if (!m_bFailureReported)
            {
                NORVES_LOG_WARNING("RayTracingShadowPass",
                                   "RT影のフレーム資源を作成できずラスタ影へ戻ります");
                m_bFailureReported = true;
            }
            return false;
        }

        RayTracingShadowParameters parameters;
        const CameraViewConstants cameraConstants = CameraViewConstants::BuildForDevice(
            *activeCamera, context.GetActiveAspectRatio(), context.Device);
        cameraConstants.CopyShaderInverseViewProjection(parameters.InverseViewProjection);
        parameters.LightDirectionAndBias[0] = lightDirection[0];
        parameters.LightDirectionAndBias[1] = lightDirection[1];
        parameters.LightDirectionAndBias[2] = lightDirection[2];
        parameters.LightDirectionAndBias[3] = 0.005f;
        frameResources->ParametersBuffer->Update(&parameters, sizeof(parameters));

        RHI::DescriptorSetPtr descriptorSet = frameResources->DescriptorSet;
        if (!descriptorSet->BindAccelerationStructure(
                0u, context.SnapshotRayTracingScene->TopLevel))
        {
            return false;
        }
        descriptorSet->BindTexture(1u, depthTexture);
        descriptorSet->BindSampler(1u, m_Sampler);
        descriptorSet->BindTexture(2u, normalTexture);
        descriptorSet->BindSampler(2u, m_Sampler);
        descriptorSet->BindConstantBuffer(
            3u,
            frameResources->ParametersBuffer,
            0u,
            static_cast<uint32_t>(sizeof(RayTracingShadowParameters)));
        descriptorSet->BindStorageTexture(4u, frameResources->VisibilityTexture);
        descriptorSet->Update();

        const RHI::ResourceState previousState = frameResources->VisibilityState;
        context.CommandList->TextureBarrier(frameResources->VisibilityTexture,
                                            previousState,
                                            RHI::ResourceState::RayTracingStorage);
        context.CommandList->SetPipeline(m_Pipeline);
        context.CommandList->SetDescriptorSet(descriptorSet, 0u);
        if (!context.CommandList->TraceRays(frameResources->Width, frameResources->Height, 1u))
        {
            context.CommandList->TextureBarrier(frameResources->VisibilityTexture,
                                                RHI::ResourceState::RayTracingStorage,
                                                RHI::ResourceState::ShaderResource);
            frameResources->VisibilityState = RHI::ResourceState::ShaderResource;
            return false;
        }
        context.CommandList->TextureBarrier(frameResources->VisibilityTexture,
                                            RHI::ResourceState::RayTracingStorage,
                                            RHI::ResourceState::ShaderResource);
        frameResources->VisibilityState = RHI::ResourceState::ShaderResource;
        outVisibilityTexture = frameResources->VisibilityTexture;
        return true;
    }

    void RayTracingShadowPass::Shutdown()
    {
        m_FrameResources.clear();
        m_Sampler.reset();
        m_Pipeline.reset();
        m_RayGenerationShader.reset();
        m_MissShader.reset();
        m_ClosestHitShader.reset();
        m_bPipelineAttempted = false;
        m_bPipelineUnavailable = false;
        m_bFailureReported = false;
    }

}
