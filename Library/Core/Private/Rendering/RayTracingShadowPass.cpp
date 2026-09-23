// RT可視性を生成する描画passを実装する。
#include "Rendering/RayTracingShadowPass.h"
#include "Rendering/PathTracingPass.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <cmath>
#include <cstring>
#include <limits>
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

            const LightProxy* shadowLight = nullptr;
            uint32_t directionalLightCount = 0u;
            // Lightingは共通のシャドウ係数を全方向光へ適用するため、一灯だけRT対象にする。
            for (const LightProxy& light : *context.SnapshotLightProxies)
            {
                if (!light.IsValid() || light.Type != LightType::Directional)
                {
                    continue;
                }

                ++directionalLightCount;
                if (directionalLightCount > 1u || !light.bCastShadows)
                {
                    return false;
                }
                shadowLight = &light;
            }

            if (directionalLightCount != 1u || shadowLight == nullptr)
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
            !context.SnapshotRayTracingScene->TopLevel || !depthTexture || !normalTexture ||
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

    namespace
    {
        struct PathTracingParameters
        {
            float InverseViewProjection[16] = {};
            float CameraPosition[4] = {};
            uint32_t ImageState[4] = {};
        };

        struct PathTracingInstance
        {
            uint64_t VertexAddress = 0u;
            uint64_t IndexAddress = 0u;
            float BaseColor[4] = {};
            float Emission[4] = {};
            uint32_t Geometry[4] = {};
        };

        static_assert(sizeof(PathTracingParameters) == 96u);
        static_assert(sizeof(PathTracingInstance) == 64u);

        uint64_t HashPathBytes(uint64_t hash, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t index = 0u; index < size; ++index)
            {
                hash = (hash ^ bytes[index]) * 1099511628211ull;
            }
            return hash;
        }

        uint64_t HashPathGeometry(const RayTracingSceneSnapshot& scene)
        {
            uint64_t hash = 14695981039346656037ull;
            const uint64_t count = scene.Instances.size();
            hash = HashPathBytes(hash, &count, sizeof(count));
            for (const RayTracingSceneInstanceSnapshot& instance : scene.Instances)
            {
                hash = HashPathBytes(hash, instance.Instance.transform,
                                     sizeof(instance.Instance.transform));
                hash = HashPathBytes(hash, &instance.Material, sizeof(instance.Material));
                hash = HashPathBytes(hash, &instance.IndexOffset,
                                     sizeof(instance.IndexOffset));
                hash = HashPathBytes(hash, &instance.IndexCount,
                                     sizeof(instance.IndexCount));
                hash = HashPathBytes(hash, &instance.VertexOffset,
                                     sizeof(instance.VertexOffset));
                hash = HashPathBytes(hash, &instance.VertexCount,
                                     sizeof(instance.VertexCount));
                hash = HashPathBytes(hash, &instance.VertexStride,
                                     sizeof(instance.VertexStride));
                const uint64_t vertexAddress =
                    instance.AccelerationStructureVertexBuffer
                        ? instance.AccelerationStructureVertexBuffer->GetDeviceAddress()
                        : 0u;
                const uint64_t indexAddress =
                    instance.AccelerationStructureIndexBuffer
                        ? instance.AccelerationStructureIndexBuffer->GetDeviceAddress()
                        : 0u;
                hash = HashPathBytes(hash, &vertexAddress, sizeof(vertexAddress));
                hash = HashPathBytes(hash, &indexAddress, sizeof(indexAddress));
            }
            return hash;
        }

        RHI::DescriptorSetDesc CreatePathTracingDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::AccelerationStructure,
                RHI::ResourceBindType::ConstantBuffer,
                RHI::ResourceBindType::StructuredBuffer,
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::RWTexture};
            for (uint32_t index = 0u; index < 5u; ++index)
            {
                RHI::DescriptorBinding binding;
                binding.binding = index;
                binding.type = types[index];
                binding.stages = RHI::ShaderStage::AllRayTracing;
                desc.bindings.push_back(binding);
            }
            return desc;
        }
    }

    bool PathTracingPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr || !context.CommandList ||
            !context.Device->GetCapabilities().RayTracing.bAccelerationStructure ||
            !context.Device->GetCapabilities().RayTracing.bRayTracingPipeline ||
            !context.Device->GetCapabilities().bBufferDeviceAddress)
        {
            return false;
        }

        m_RayGenerationShader = context.ShaderMgr->LoadShader(
            "PathTracing/PathTracingRayGen.glsl", RHI::ShaderStage::RayGen);
        m_MissShader = context.ShaderMgr->LoadShader(
            "PathTracing/PathTracingMiss.glsl", RHI::ShaderStage::Miss);
        m_ClosestHitShader = context.ShaderMgr->LoadShader(
            "PathTracing/PathTracingClosestHit.glsl", RHI::ShaderStage::ClosestHit);
        if (!m_RayGenerationShader || !m_MissShader || !m_ClosestHitShader)
        {
            Shutdown();
            return false;
        }

        RHI::RayTracingPipelineDesc desc;
        RHI::RayTracingShaderGroupDesc rayGeneration;
        rayGeneration.type = RHI::RayTracingShaderGroupType::General;
        rayGeneration.generalShader = m_RayGenerationShader;
        desc.shaderGroups.push_back(rayGeneration);
        RHI::RayTracingShaderGroupDesc miss;
        miss.type = RHI::RayTracingShaderGroupType::General;
        miss.generalShader = m_MissShader;
        desc.shaderGroups.push_back(miss);
        RHI::RayTracingShaderGroupDesc closestHit;
        closestHit.type = RHI::RayTracingShaderGroupType::TrianglesHit;
        closestHit.closestHitShader = m_ClosestHitShader;
        desc.shaderGroups.push_back(closestHit);
        desc.descriptorSetLayouts.push_back(CreatePathTracingDescriptorSetDesc());
        if (!RHI::IsValidRayTracingPipelineDesc(desc))
        {
            Shutdown();
            return false;
        }
        m_Pipeline = context.Device->CreateRayTracingPipeline(desc);

        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = context.Device->CreateSampler(samplerDesc);
        if (!m_Pipeline || !m_Sampler)
        {
            Shutdown();
            return false;
        }
        m_bInitialized = true;
        return true;
    }

    void PathTracingPass::Shutdown()
    {
        m_Histories.clear();
        m_Pipeline.reset();
        m_RayGenerationShader.reset();
        m_MissShader.reset();
        m_ClosestHitShader.reset();
        m_Sampler.reset();
        m_OutputHandle = {};
        m_ActiveHistoryIndex = UINT32_MAX;
        m_bPrepared = false;
        m_bInitialized = false;
    }

    void PathTracingPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void PathTracingPass::Execute(ViewRenderContext& /*context*/)
    {
    }

    PathTracingPass::History* PathTracingPass::FindOrCreateHistory(
        const ViewRenderContext& context, uint32_t width, uint32_t height)
    {
        const uint32_t viewId = context.PhysicalLighting.ViewId;
        const uint32_t viewportId = context.PhysicalLighting.ViewportId;
        History* history = nullptr;
        for (History& entry : m_Histories)
        {
            if (entry.ViewId == viewId && entry.ViewportId == viewportId)
            {
                history = &entry;
                break;
            }
        }
        if (!history)
        {
            History entry;
            entry.ViewId = viewId;
            entry.ViewportId = viewportId;
            m_Histories.push_back(std::move(entry));
            history = &m_Histories.back();
        }

        if (history->Width != width || history->Height != height ||
            !history->Textures[0] || !history->Textures[1])
        {
            history->Textures[0].reset();
            history->Textures[1].reset();
            history->TextureStates[0] = RHI::ResourceState::Undefined;
            history->TextureStates[1] = RHI::ResourceState::Undefined;
            history->SampleCount = 0u;
            RHI::TextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.TextureFormat = RHI::Format::R32G32B32A32_FLOAT;
            desc.Usage = RHI::ResourceUsage::ShaderRead |
                         RHI::ResourceUsage::ShaderWrite |
                         RHI::ResourceUsage::TransferSrc;
            desc.DebugName = "PathTracing.Accumulation";
            history->Textures[0] = context.Device->CreateTexture(desc);
            history->Textures[1] = context.Device->CreateTexture(desc);
            if (!history->Textures[0] || !history->Textures[1])
            {
                return nullptr;
            }
            history->Width = width;
            history->Height = height;
        }
        if (!history->ParametersBuffer)
        {
            RHI::BufferDesc desc(sizeof(PathTracingParameters),
                                 RHI::ResourceUsage::ConstantBuffer, true,
                                 "PathTracing.Parameters");
            history->ParametersBuffer = context.Device->CreateBuffer(desc);
        }
        if (!history->DescriptorSet)
        {
            history->DescriptorSet = context.Device->CreateDescriptorSet(
                CreatePathTracingDescriptorSetDesc());
        }
        return history->ParametersBuffer && history->DescriptorSet ? history : nullptr;
    }

    bool PathTracingPass::PrepareInstances(const ViewRenderContext& context, History& history)
    {
        const RayTracingSceneSnapshot& scene = *context.SnapshotRayTracingScene;
        if (scene.Instances.empty() ||
            scene.Instances.size() > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
        Container::VariableArray<PathTracingInstance> instances;
        instances.resize(scene.Instances.size());
        for (const RayTracingSceneInstanceSnapshot& snapshot : scene.Instances)
        {
            const uint32_t index = snapshot.Instance.customIndex;
            if (index >= instances.size() || !snapshot.AccelerationStructureVertexBuffer ||
                !snapshot.AccelerationStructureIndexBuffer ||
                snapshot.VertexStride < 12u || snapshot.VertexStride % 4u != 0u ||
                snapshot.VertexCount < 3u || snapshot.IndexCount < 3u ||
                snapshot.IndexCount % 3u != 0u)
            {
                return false;
            }
            const uint64_t vertexOffset =
                static_cast<uint64_t>(snapshot.VertexOffset) * snapshot.VertexStride;
            const uint64_t indexOffset =
                static_cast<uint64_t>(snapshot.IndexOffset) * sizeof(uint32_t);
            const RHI::BufferPtr& vertexBuffer = snapshot.AccelerationStructureVertexBuffer;
            const RHI::BufferPtr& indexBuffer = snapshot.AccelerationStructureIndexBuffer;
            const uint64_t vertexAddress = vertexBuffer->GetDeviceAddress();
            const uint64_t indexAddress = indexBuffer->GetDeviceAddress();
            if (vertexAddress == 0u || indexAddress == 0u ||
                vertexOffset > vertexBuffer->GetSize() ||
                static_cast<uint64_t>(snapshot.VertexCount) * snapshot.VertexStride >
                    vertexBuffer->GetSize() - vertexOffset ||
                indexOffset > indexBuffer->GetSize() ||
                static_cast<uint64_t>(snapshot.IndexCount) * sizeof(uint32_t) >
                    indexBuffer->GetSize() - indexOffset ||
                vertexAddress > UINT64_MAX - vertexOffset ||
                indexAddress > UINT64_MAX - indexOffset)
            {
                return false;
            }
            PathTracingInstance& instance = instances[index];
            if (instance.Geometry[1] != 0u)
            {
                return false;
            }
            instance.VertexAddress = vertexAddress + vertexOffset;
            instance.IndexAddress = indexAddress + indexOffset;
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                if (!std::isfinite(snapshot.Material.BaseColor[channel]) ||
                    snapshot.Material.BaseColor[channel] < 0.0f)
                {
                    return false;
                }
                instance.BaseColor[channel] = snapshot.Material.BaseColor[channel];
            }
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                if (!std::isfinite(snapshot.Material.EmissiveColor[channel]) ||
                    snapshot.Material.EmissiveColor[channel] < 0.0f)
                {
                    return false;
                }
                instance.Emission[channel] = snapshot.Material.EmissiveColor[channel];
            }
            if (!std::isfinite(snapshot.Material.EmissiveLuminanceNits) ||
                snapshot.Material.EmissiveLuminanceNits < 0.0f)
            {
                return false;
            }
            instance.Emission[3] = snapshot.Material.EmissiveLuminanceNits;
            instance.Geometry[0] = snapshot.VertexStride;
            instance.Geometry[1] = snapshot.VertexCount;
            instance.Geometry[2] = snapshot.IndexCount;
            instance.Geometry[3] = index;
        }
        for (const PathTracingInstance& instance : instances)
        {
            if (instance.Geometry[1] == 0u)
            {
                return false;
            }
        }

        const uint64_t requiredSize = instances.size() * sizeof(PathTracingInstance);
        if (requiredSize > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
        if (!history.InstanceBuffer || history.InstanceBufferCapacity < requiredSize)
        {
            RHI::BufferDesc desc(requiredSize, RHI::ResourceUsage::StorageBuffer,
                                 true, "PathTracing.Instances");
            history.InstanceBuffer = context.Device->CreateBuffer(desc);
            history.InstanceBufferCapacity = history.InstanceBuffer
                                                 ? history.InstanceBuffer->GetSize()
                                                 : 0u;
        }
        if (!history.InstanceBuffer)
        {
            return false;
        }
        history.InstanceBuffer->Update(instances.data(), requiredSize);
        return true;
    }

    void PathTracingPass::Declare(RenderGraphBuilder& builder)
    {
        m_OutputHandle = {};
        m_ActiveHistoryIndex = UINT32_MAX;
        m_bPrepared = false;
        const ViewRenderContext* context = builder.GetContext();
        if (!m_bInitialized || !context || !context->Device ||
            !context->SnapshotRayTracingScene ||
            !context->SnapshotRayTracingScene->IsComplete() ||
            !context->GetActiveCamera())
        {
            return;
        }
        const uint32_t width = context->GetActiveRenderWidth();
        const uint32_t height = context->GetActiveRenderHeight();
        if (width == 0u || height == 0u)
        {
            return;
        }

        History* history = FindOrCreateHistory(*context, width, height);
        if (!history || !PrepareInstances(*context, *history))
        {
            return;
        }

        const CameraViewConstants camera = CameraViewConstants::BuildForDevice(
            *context->GetActiveCamera(), context->GetActiveAspectRatio(), context->Device);
        PathTracingParameters parameters;
        camera.CopyShaderInverseViewProjection(parameters.InverseViewProjection);
        camera.CopyCameraPosition(parameters.CameraPosition);
        uint64_t cameraSignature = 14695981039346656037ull;
        cameraSignature = HashPathBytes(cameraSignature,
                                        parameters.InverseViewProjection,
                                        sizeof(parameters.InverseViewProjection));
        cameraSignature = HashPathBytes(cameraSignature,
                                        parameters.CameraPosition,
                                        sizeof(parameters.CameraPosition));
        const uint64_t geometrySignature =
            HashPathGeometry(*context->SnapshotRayTracingScene);
        const bool bReset = history->SampleCount == 0u ||
            history->LastFrameNumber + 1u != context->FrameNumber ||
            history->SceneRevision != context->SceneRevision ||
            history->LightRevision != context->LightRevision ||
            history->CameraSignature != cameraSignature ||
            history->GeometrySignature != geometrySignature ||
            history->SampleCount == UINT32_MAX;
        if (bReset)
        {
            history->SampleCount = 0u;
        }
        m_TargetIndex = history->SampleCount == 0u ? 0u : 1u - history->CurrentIndex;
        const uint32_t previousIndex = 1u - m_TargetIndex;
        const RGResourceHandle previous = builder.ImportTexture(
            history->Textures[previousIndex], history->TextureStates[previousIndex],
            "PathTracing.Previous");
        const RGResourceHandle output = builder.ImportTexture(
            history->Textures[m_TargetIndex], history->TextureStates[m_TargetIndex],
            "PathTracing.Current");
        if (!previous.IsValid() || !output.IsValid())
        {
            return;
        }
        builder.Read(previous, RHI::ResourceState::ShaderResource);
        builder.Write(output, RHI::ResourceState::RayTracingStorage,
                      RHI::ResourceState::ShaderResource);
        if (!builder.PublishTexture(RenderGraphResourceNames::SceneColor, output) ||
            !builder.ExportTexture(RenderGraphResourceNames::SceneColor, output) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SceneColor, m_OutputHandle))
        {
            return;
        }
        builder.PreserveInsertionOrder();
        m_ActiveHistoryIndex = static_cast<uint32_t>(history - m_Histories.data());
        m_DeclaredCameraSignature = cameraSignature;
        m_DeclaredGeometrySignature = geometrySignature;
        m_bPrepared = true;
    }

    void PathTracingPass::Execute(RenderGraphResources& resources,
                                  ViewRenderContext& context)
    {
        if (!m_bPrepared || m_ActiveHistoryIndex >= m_Histories.size() ||
            !context.CommandList || !context.SnapshotRayTracingScene ||
            !resources.GetTexture(m_OutputHandle))
        {
            return;
        }
        History& history = m_Histories[m_ActiveHistoryIndex];
        PathTracingParameters parameters;
        const CameraViewConstants camera = CameraViewConstants::BuildForDevice(
            *context.GetActiveCamera(), context.GetActiveAspectRatio(), context.Device);
        camera.CopyShaderInverseViewProjection(parameters.InverseViewProjection);
        camera.CopyCameraPosition(parameters.CameraPosition);
        parameters.ImageState[0] = history.Width;
        parameters.ImageState[1] = history.Height;
        parameters.ImageState[2] = history.SampleCount;
        parameters.ImageState[3] = static_cast<uint32_t>(
            context.SnapshotRayTracingScene->Instances.size());
        history.ParametersBuffer->Update(&parameters, sizeof(parameters));

        RHI::DescriptorSetPtr descriptorSet = history.DescriptorSet;
        if (!descriptorSet->BindAccelerationStructure(
                0u, context.SnapshotRayTracingScene->TopLevel))
        {
            return;
        }
        descriptorSet->BindConstantBuffer(1u, history.ParametersBuffer,
                                          0u, sizeof(parameters));
        descriptorSet->BindStorageBuffer(2u, history.InstanceBuffer, 0u,
                                         static_cast<uint32_t>(
                                             context.SnapshotRayTracingScene->Instances.size() *
                                             sizeof(PathTracingInstance)));
        descriptorSet->BindTexture(3u, history.Textures[1u - m_TargetIndex]);
        descriptorSet->BindSampler(3u, m_Sampler);
        descriptorSet->BindStorageTexture(4u, history.Textures[m_TargetIndex]);
        descriptorSet->Update();

        context.CommandList->SetPipeline(m_Pipeline);
        context.CommandList->SetDescriptorSet(descriptorSet, 0u);
        if (!context.CommandList->TraceRays(history.Width, history.Height, 1u))
        {
            return;
        }
        context.CommandList->TextureBarrier(history.Textures[m_TargetIndex],
                                            RHI::ResourceState::RayTracingStorage,
                                            RHI::ResourceState::ShaderResource);
        history.TextureStates[0] = RHI::ResourceState::ShaderResource;
        history.TextureStates[1] = RHI::ResourceState::ShaderResource;
        history.CurrentIndex = m_TargetIndex;
        ++history.SampleCount;
        history.LastFrameNumber = context.FrameNumber;
        history.SceneRevision = context.SceneRevision;
        history.LightRevision = context.LightRevision;
        history.CameraSignature = m_DeclaredCameraSignature;
        history.GeometrySignature = m_DeclaredGeometrySignature;
    }

    uint32_t PathTracingPass::GetAccumulatedSampleCount() const
    {
        return m_ActiveHistoryIndex < m_Histories.size()
                   ? m_Histories[m_ActiveHistoryIndex].SampleCount
                   : 0u;
    }

    RHI::TexturePtr PathTracingPass::GetAccumulatedTexture() const
    {
        if (m_ActiveHistoryIndex >= m_Histories.size())
        {
            return {};
        }
        const History& history = m_Histories[m_ActiveHistoryIndex];
        return history.SampleCount > 0u
                   ? history.Textures[history.CurrentIndex]
                   : RHI::TexturePtr{};
    }
}
