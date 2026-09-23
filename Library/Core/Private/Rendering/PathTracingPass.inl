// FramePacketの値スナップショットから独立したPT描画と履歴を実装する。
#include "Rendering/PathTracingPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FramePacket.h"
#include "Rendering/PathTracingCamera.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VolumetricFog.h"
#include "VolumetricFogScattering.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct PathTracingParameters
        {
            float InverseViewProjection[16] = {};
            float CameraPosition[4] = {};
            uint32_t ImageState[4] = {};
            float SkySunDirectionAndCosRadius[4] = {};
            float SkyState[4] = {};
            float FogDensityHeightFalloffAndEnabled[4] = {};
            float FogColorAndPreExposure[4] = {};
            float FogLightDirectionAndAnisotropy[4] = {};
            float FogLightRadianceAndEnabled[4] = {};
            float ExposureAndDebug[4] = {}; ///< x=カメラのプリエクスポージャ、y=検証出力
        };

        struct PathTracingInstance
        {
            uint64_t VertexAddress = 0u;
            uint64_t IndexAddress = 0u;
            float BaseColor[4] = {};
            float Emission[4] = {};
            uint32_t Geometry[4] = {};
            float ObjectColor[4] = {}; ///< GBufferと同じ規則のinstance色
            uint32_t Textures[4] = {}; ///< アルベド・法線・metallic・roughnessの材質texture表番号
        };

        static_assert(sizeof(PathTracingParameters) == 208u);
        static_assert(sizeof(PathTracingInstance) == 96u);

        // 材質texture表の先頭に置く既定texture。GBufferの既定値と同じ並びと値にする。
        constexpr uint32_t PathDefaultAlbedoTextureIndex = 0u;
        constexpr uint32_t PathDefaultNormalTextureIndex = 1u;
        constexpr uint32_t PathDefaultMetallicTextureIndex = 2u;
        constexpr uint32_t PathDefaultRoughnessTextureIndex = 3u;
        constexpr uint32_t PathMaterialTextureBinding = 8u;

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
                hash = HashPathBytes(hash, instance.PreviousTransform,
                                     sizeof(instance.PreviousTransform));
                hash = HashPathBytes(hash, &instance.bHasPreviousTransform,
                                     sizeof(instance.bHasPreviousTransform));
                hash = HashPathBytes(hash, &instance.Instance.customIndex,
                                     sizeof(instance.Instance.customIndex));
                hash = HashPathBytes(hash, &instance.Instance.mask,
                                     sizeof(instance.Instance.mask));
                hash = HashPathBytes(
                    hash, &instance.Instance.shaderBindingTableRecordOffset,
                    sizeof(instance.Instance.shaderBindingTableRecordOffset));
                hash = HashPathBytes(
                    hash, &instance.Instance.disableTriangleFacingCull,
                    sizeof(instance.Instance.disableTriangleFacingCull));
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

        uint64_t HashPathSky(const SkyAtmosphereParameters& sky, float preExposure)
        {
            uint64_t hash = 14695981039346656037ull;
            hash = HashPathBytes(hash, &sky.bEnabled, sizeof(sky.bEnabled));
            if (!sky.bEnabled)
            {
                return hash;
            }
            hash = HashPathBytes(hash, &sky.SunAltitudeDegrees, sizeof(float));
            hash = HashPathBytes(hash, &sky.SunAzimuthDegrees, sizeof(float));
            hash = HashPathBytes(hash, &sky.SunLuminanceNits, sizeof(float));
            hash = HashPathBytes(hash, &sky.PlanetRadiusMeters, sizeof(float));
            hash = HashPathBytes(hash, &sky.AtmosphereHeightMeters, sizeof(float));
            hash = HashPathBytes(hash, &sky.RayleighScaleHeightMeters, sizeof(float));
            hash = HashPathBytes(hash, &sky.MieScaleHeightMeters, sizeof(float));
            hash = HashPathBytes(hash, &sky.MieAnisotropy, sizeof(float));
            hash = HashPathBytes(hash, &sky.GroundAlbedo.x, sizeof(float));
            hash = HashPathBytes(hash, &sky.GroundAlbedo.y, sizeof(float));
            hash = HashPathBytes(hash, &sky.GroundAlbedo.z, sizeof(float));
            return HashPathBytes(hash, &preExposure, sizeof(preExposure));
        }

        float SafePathPreExposure(float value)
        {
            return std::isfinite(value) && value > 0.0f
                       ? std::clamp(value, 1.0e-6f, 1.0e6f)
                       : 1.0f;
        }

        void FillPathFogParameters(const ViewRenderContext& context,
                                   float preExposure,
                                   PathTracingParameters& parameters)
        {
            const SceneProxy* scene = context.SnapshotScene;
            const VolumetricFogParameters fog = scene
                ? SanitizeVolumetricFogParameters(scene->VolumetricFog)
                : MakeDefaultVolumetricFogParameters();
            if (!fog.bEnabled || fog.DensityAtBaseHeight <= 0.0f)
            {
                return;
            }

            parameters.FogDensityHeightFalloffAndEnabled[0] =
                fog.DensityAtBaseHeight;
            parameters.FogDensityHeightFalloffAndEnabled[1] = fog.BaseHeight;
            parameters.FogDensityHeightFalloffAndEnabled[2] =
                fog.HeightFalloffPerUnit;
            parameters.FogDensityHeightFalloffAndEnabled[3] = 1.0f;
            const float color[] = {
                scene->FogColorR, scene->FogColorG, scene->FogColorB};
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                parameters.FogColorAndPreExposure[channel] =
                    std::isfinite(color[channel])
                        ? std::max(color[channel], 0.0f)
                        : 0.0f;
            }
            parameters.FogColorAndPreExposure[3] = preExposure;

            const auto* lights = context.SnapshotLightProxies
                ? context.SnapshotLightProxies
                : &scene->LightProxies;
            const LightProxy* selected = nullptr;
            const uint64_t preferredId =
                context.PhysicalLighting.CascadedShadow.LightId;
            for (const LightProxy& light : *lights)
            {
                if (light.Type != LightType::Directional || !light.IsValid() ||
                    !std::isfinite(light.DirectionX) ||
                    !std::isfinite(light.DirectionY) ||
                    !std::isfinite(light.DirectionZ) ||
                    !std::isfinite(light.CanonicalIntensity) ||
                    !std::isfinite(light.ColorR) ||
                    !std::isfinite(light.ColorG) ||
                    !std::isfinite(light.ColorB) ||
                    light.ColorR < 0.0f || light.ColorG < 0.0f ||
                    light.ColorB < 0.0f)
                {
                    continue;
                }
                const double length = std::sqrt(
                    static_cast<double>(light.DirectionX) * light.DirectionX +
                    static_cast<double>(light.DirectionY) * light.DirectionY +
                    static_cast<double>(light.DirectionZ) * light.DirectionZ);
                if (!std::isfinite(length) || length <= 1.0e-8)
                {
                    continue;
                }
                selected = &light;
                if (light.LightId == preferredId)
                {
                    break;
                }
            }
            if (!selected)
            {
                return;
            }
            const double length = std::sqrt(
                static_cast<double>(selected->DirectionX) * selected->DirectionX +
                static_cast<double>(selected->DirectionY) * selected->DirectionY +
                static_cast<double>(selected->DirectionZ) * selected->DirectionZ);
            const double inverseLength = 1.0 / length;
            const float direction[] = {
                selected->DirectionX, selected->DirectionY, selected->DirectionZ};
            const float colorChannels[] = {
                selected->ColorR, selected->ColorG, selected->ColorB};
            const double intensity = std::min(
                static_cast<double>(selected->CanonicalIntensity), 1.0e7);
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                parameters.FogLightDirectionAndAnisotropy[channel] =
                    static_cast<float>(direction[channel] * inverseLength);
                parameters.FogLightRadianceAndEnabled[channel] =
                    static_cast<float>(std::min(
                        static_cast<double>(colorChannels[channel]) * intensity,
                        1.0e8));
            }
            parameters.FogLightDirectionAndAnisotropy[3] =
                VolumetricFogDetail::ScatteringAnisotropy;
            parameters.FogLightRadianceAndEnabled[3] =
                std::max({parameters.FogLightRadianceAndEnabled[0],
                          parameters.FogLightRadianceAndEnabled[1],
                          parameters.FogLightRadianceAndEnabled[2]}) > 0.0f
                    ? 1.0f
                    : 0.0f;
        }

        uint64_t HashPathFog(const PathTracingParameters& parameters)
        {
            uint64_t hash = 14695981039346656037ull;
            hash = HashPathBytes(hash, parameters.FogDensityHeightFalloffAndEnabled,
                                 sizeof(parameters.FogDensityHeightFalloffAndEnabled));
            hash = HashPathBytes(hash, parameters.FogColorAndPreExposure,
                                 sizeof(parameters.FogColorAndPreExposure));
            hash = HashPathBytes(hash, parameters.FogLightDirectionAndAnisotropy,
                                 sizeof(parameters.FogLightDirectionAndAnisotropy));
            return HashPathBytes(hash, parameters.FogLightRadianceAndEnabled,
                                 sizeof(parameters.FogLightRadianceAndEnabled));
        }

        RHI::DescriptorSetDesc CreatePathTracingDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::AccelerationStructure,
                RHI::ResourceBindType::ConstantBuffer,
                RHI::ResourceBindType::StructuredBuffer,
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::RWTexture,
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::CombinedImageSampler};
            for (uint32_t index = 0u; index < 8u; ++index)
            {
                RHI::DescriptorBinding binding;
                binding.binding = index;
                binding.type = types[index];
                binding.stages = RHI::ShaderStage::AllRayTracing;
                desc.bindings.push_back(binding);
            }
            RHI::DescriptorBinding materialTextures;
            materialTextures.binding = PathMaterialTextureBinding;
            materialTextures.type = RHI::ResourceBindType::CombinedImageSampler;
            materialTextures.stages = RHI::ShaderStage::AllRayTracing;
            materialTextures.count = PathTracingMaterialTextureCapacity;
            desc.bindings.push_back(materialTextures);
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
            !context.Device->GetCapabilities().bBufferDeviceAddress ||
            !context.Device->GetCapabilities().bSampledImageArrayNonUniformIndexing)
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

        // 材質textureはGBufferと同じsamplerと既定textureで標本化する。
        RHI::SamplerDesc materialSamplerDesc;
        materialSamplerDesc.filterMin = RHI::FilterMode::Anisotropic;
        materialSamplerDesc.filterMag = RHI::FilterMode::Anisotropic;
        materialSamplerDesc.filterMip = RHI::FilterMode::Anisotropic;
        materialSamplerDesc.addressU = RHI::TextureAddressMode::Wrap;
        materialSamplerDesc.addressV = RHI::TextureAddressMode::Wrap;
        materialSamplerDesc.addressW = RHI::TextureAddressMode::Wrap;
        materialSamplerDesc.maxAnisotropy = 4;
        m_MaterialSampler = context.Device->CreateSampler(materialSamplerDesc);
        const auto createDefault1x1 = [&context](const char* debugName, uint8_t red, uint8_t green,
                                                 uint8_t blue) -> RHI::TexturePtr
        {
            RHI::TextureDesc desc;
            desc.Width = 1u;
            desc.Height = 1u;
            desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            desc.Usage = RHI::ResourceUsage::ShaderRead;
            desc.DebugName = debugName;
            RHI::TexturePtr texture = context.Device->CreateTexture(desc);
            if (texture)
            {
                const uint8_t pixel[4] = {red, green, blue, 255u};
                texture->Update(pixel, 4u, 4u);
            }
            return texture;
        };
        m_DefaultWhiteTexture = createDefault1x1("PathTracing.DefaultWhite", 255u, 255u, 255u);
        m_DefaultFlatNormalTexture = createDefault1x1("PathTracing.DefaultFlatNormal", 128u, 128u, 255u);
        m_DefaultBlackTexture = createDefault1x1("PathTracing.DefaultBlack", 0u, 0u, 0u);
        m_DefaultMidGrayTexture = createDefault1x1("PathTracing.DefaultMidGray", 128u, 128u, 128u);
        if (!m_Pipeline || !m_Sampler || !m_MaterialSampler || !m_DefaultWhiteTexture ||
            !m_DefaultFlatNormalTexture || !m_DefaultBlackTexture || !m_DefaultMidGrayTexture)
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
        m_MaterialSampler.reset();
        m_DefaultWhiteTexture.reset();
        m_DefaultFlatNormalTexture.reset();
        m_DefaultBlackTexture.reset();
        m_DefaultMidGrayTexture.reset();
        m_BoundMaterialTextureCount = 0u;
        m_OutputHandle = {};
        m_SkyRadianceHandle = {};
        m_SkyTransmittanceHandle = {};
        m_SunDiskHandle = {};
        m_ActiveHistoryIndex = UINT32_MAX;
        m_ActiveFrameResourceIndex = UINT32_MAX;
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
        return history;
    }

    PathTracingPass::FrameResources* PathTracingPass::FindOrCreateFrameResources(
        const ViewRenderContext& context, History& history)
    {
        FrameResources* frameResources = nullptr;
        for (FrameResources& entry : history.FrameSlots)
        {
            if (entry.FrameIndex == context.FrameIndex)
            {
                frameResources = &entry;
                break;
            }
        }
        if (!frameResources)
        {
            FrameResources entry;
            entry.FrameIndex = context.FrameIndex;
            history.FrameSlots.push_back(std::move(entry));
            frameResources = &history.FrameSlots.back();
        }
        if (!frameResources->ParametersBuffer)
        {
            RHI::BufferDesc desc(sizeof(PathTracingParameters),
                                 RHI::ResourceUsage::ConstantBuffer, true,
                                 "PathTracing.Parameters");
            frameResources->ParametersBuffer = context.Device->CreateBuffer(desc);
        }
        if (!frameResources->DescriptorSet)
        {
            frameResources->DescriptorSet = context.Device->CreateDescriptorSet(
                CreatePathTracingDescriptorSetDesc());
        }
        return frameResources->ParametersBuffer && frameResources->DescriptorSet
                   ? frameResources
                   : nullptr;
    }

    bool PathTracingPass::PrepareInstances(const ViewRenderContext& context,
                                           FrameResources& frameResources)
    {
        const RayTracingSceneSnapshot& scene = *context.SnapshotRayTracingScene;
        if (scene.Instances.empty() ||
            scene.Instances.size() > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
        Container::VariableArray<PathTracingInstance> instances;
        instances.resize(scene.Instances.size());

        // 材質texture表。先頭4要素にGBufferと同じ既定textureを置き、有効なhandleは重複を除いて追加する。
        Container::VariableArray<RHI::TexturePtr>& textureTable = frameResources.MaterialTextures;
        textureTable.clear();
        textureTable.push_back(m_DefaultWhiteTexture);
        textureTable.push_back(m_DefaultFlatNormalTexture);
        textureTable.push_back(m_DefaultBlackTexture);
        textureTable.push_back(m_DefaultMidGrayTexture);
        const auto resolveTextureIndex = [&](TextureHandle handle, uint32_t defaultIndex) -> uint32_t
        {
            if (!handle.IsValid() || !context.Resources.Textures)
            {
                return defaultIndex;
            }
            RHI::TexturePtr texture = context.Resources.Textures->GetRHITexturePtr(handle);
            if (!texture)
            {
                return defaultIndex;
            }
            for (uint32_t index = 0u; index < textureTable.size(); ++index)
            {
                if (textureTable[index] == texture)
                {
                    return index;
                }
            }
            if (textureTable.size() >= PathTracingMaterialTextureCapacity)
            {
                if (!m_bMaterialTextureOverflowReported)
                {
                    NORVES_LOG_WARNING("PathTracingPass",
                                       "Material texture table is full; remaining textures use defaults");
                    m_bMaterialTextureOverflowReported = true;
                }
                return defaultIndex;
            }
            textureTable.push_back(texture);
            return static_cast<uint32_t>(textureTable.size() - 1u);
        };

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
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                if (!std::isfinite(snapshot.Material.ObjectColor[channel]) ||
                    snapshot.Material.ObjectColor[channel] < 0.0f)
                {
                    return false;
                }
                instance.ObjectColor[channel] = snapshot.Material.ObjectColor[channel];
            }
            instance.Textures[0] = resolveTextureIndex(snapshot.Material.AlbedoTexture,
                                                       PathDefaultAlbedoTextureIndex);
            instance.Textures[1] = resolveTextureIndex(snapshot.Material.NormalTexture,
                                                       PathDefaultNormalTextureIndex);
            instance.Textures[2] = resolveTextureIndex(snapshot.Material.MetallicTexture,
                                                       PathDefaultMetallicTextureIndex);
            instance.Textures[3] = resolveTextureIndex(snapshot.Material.RoughnessTexture,
                                                       PathDefaultRoughnessTextureIndex);
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
        // handleの解放や差し替えで解決結果が既定textureへ変わっても履歴を捨てられるよう、
        // 解決後のtexture実体と各instanceの表番号を署名にする。
        uint64_t textureSignature = 14695981039346656037ull;
        for (const RHI::TexturePtr& texture : textureTable)
        {
            const RHI::ITexture* pointer = texture.get();
            textureSignature = HashPathBytes(textureSignature, &pointer, sizeof(pointer));
        }
        for (const PathTracingInstance& instance : instances)
        {
            textureSignature = HashPathBytes(textureSignature, instance.Textures,
                                             sizeof(instance.Textures));
        }
        m_DeclaredMaterialTextureSignature = textureSignature;

        const uint64_t requiredSize = instances.size() * sizeof(PathTracingInstance);
        if (requiredSize > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
        if (!frameResources.InstanceBuffer ||
            frameResources.InstanceBufferCapacity < requiredSize)
        {
            RHI::BufferDesc desc(requiredSize, RHI::ResourceUsage::StorageBuffer,
                                 true, "PathTracing.Instances");
            frameResources.InstanceBuffer = context.Device->CreateBuffer(desc);
            frameResources.InstanceBufferCapacity = frameResources.InstanceBuffer
                                                        ? frameResources.InstanceBuffer->GetSize()
                                                        : 0u;
        }
        if (!frameResources.InstanceBuffer)
        {
            return false;
        }
        frameResources.InstanceBuffer->Update(instances.data(), requiredSize);
        return true;
    }

    void PathTracingPass::Declare(RenderGraphBuilder& builder)
    {
        m_OutputHandle = {};
        m_SkyRadianceHandle = {};
        m_SkyTransmittanceHandle = {};
        m_SunDiskHandle = {};
        m_ActiveHistoryIndex = UINT32_MAX;
        m_ActiveFrameResourceIndex = UINT32_MAX;
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
        if (!history)
        {
            return;
        }
        FrameResources* frameResources = FindOrCreateFrameResources(*context, *history);
        if (!frameResources || !PrepareInstances(*context, *frameResources))
        {
            return;
        }
        m_BoundMaterialTextureCount = static_cast<uint32_t>(frameResources->MaterialTextures.size());

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
        const CameraProxy& activeCamera = *context->GetActiveCamera();
        cameraSignature = HashPathBytes(cameraSignature, &activeCamera.Aperture,
                                        sizeof(activeCamera.Aperture));
        cameraSignature = HashPathBytes(cameraSignature, &activeCamera.ShutterSpeed,
                                        sizeof(activeCamera.ShutterSpeed));
        cameraSignature = HashPathBytes(cameraSignature, &activeCamera.FocusDistance,
                                        sizeof(activeCamera.FocusDistance));
        // 発光と環境はプリエクスポージャを掛けて累積するため、露出の変化でも履歴を捨てる。
        cameraSignature = HashPathBytes(cameraSignature, &activeCamera.PreExposure,
                                        sizeof(activeCamera.PreExposure));
        if (const CameraProxy* previousCamera = context->GetPreviousCamera())
        {
            const CameraViewConstants previous = CameraViewConstants::BuildForDevice(
                *previousCamera, context->GetActiveAspectRatio(), context->Device);
            previous.CopyShaderInverseViewProjection(parameters.InverseViewProjection);
            previous.CopyCameraPosition(parameters.CameraPosition);
            cameraSignature = HashPathBytes(cameraSignature,
                                            parameters.InverseViewProjection,
                                            sizeof(parameters.InverseViewProjection));
            cameraSignature = HashPathBytes(cameraSignature,
                                            parameters.CameraPosition,
                                            sizeof(parameters.CameraPosition));
        }
        const uint64_t geometrySignature =
            HashPathGeometry(*context->SnapshotRayTracingScene);
        const SkyAtmosphereParameters sky = SanitizeSkyAtmosphereParameters(
            context->SnapshotScene ? context->SnapshotScene->SkyAtmosphere :
                                     context->SkyAtmosphereSnapshot);
        const float preExposure = SafePathPreExposure(activeCamera.PreExposure);
        FillPathFogParameters(*context, preExposure, parameters);
        const uint64_t skySignature = HashPathSky(sky, preExposure);
        const uint64_t fogSignature = HashPathFog(parameters);
        const bool bReset = history->SampleCount == 0u ||
            history->SceneRevision != context->SceneRevision ||
            history->LightRevision != context->LightRevision ||
            history->CameraSignature != cameraSignature ||
            history->GeometrySignature != geometrySignature ||
            history->SkySignature != skySignature ||
            history->FogSignature != fogSignature ||
            history->DebugOutput != m_DebugOutput ||
            history->MaterialTextureSignature != m_DeclaredMaterialTextureSignature ||
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
        if (builder.TryGetTexture(RenderGraphResourceNames::SkyAtmosphereRadiance,
                                  m_SkyRadianceHandle) &&
            builder.TryGetTexture(RenderGraphResourceNames::SkyAtmosphereTransmittance,
                                  m_SkyTransmittanceHandle) &&
            builder.TryGetTexture(RenderGraphResourceNames::SkyAtmosphereSunDisk,
                                  m_SunDiskHandle))
        {
            builder.Read(m_SkyRadianceHandle.ToResourceHandle(),
                         RHI::ResourceState::ShaderResource);
            builder.Read(m_SkyTransmittanceHandle.ToResourceHandle(),
                         RHI::ResourceState::ShaderResource);
            builder.Read(m_SunDiskHandle.ToResourceHandle(),
                         RHI::ResourceState::ShaderResource);
        }
        if (!builder.PublishTexture(RenderGraphResourceNames::SceneColor, output) ||
            !builder.ExportTexture(RenderGraphResourceNames::SceneColor, output) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SceneColor, m_OutputHandle))
        {
            return;
        }
        builder.PreserveInsertionOrder();
        m_ActiveHistoryIndex = static_cast<uint32_t>(history - m_Histories.data());
        m_ActiveFrameResourceIndex = static_cast<uint32_t>(
            frameResources - history->FrameSlots.data());
        m_DeclaredCameraSignature = cameraSignature;
        m_DeclaredGeometrySignature = geometrySignature;
        m_DeclaredSkySignature = skySignature;
        m_DeclaredFogSignature = fogSignature;
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
        if (m_ActiveFrameResourceIndex >= history.FrameSlots.size())
        {
            return;
        }
        FrameResources& frameResources =
            history.FrameSlots[m_ActiveFrameResourceIndex];
        const auto restoreTextureStates = [&]()
        {
            context.CommandList->TextureBarrier(history.Textures[m_TargetIndex],
                                                RHI::ResourceState::RayTracingStorage,
                                                RHI::ResourceState::ShaderResource);
            history.TextureStates[0] = RHI::ResourceState::ShaderResource;
            history.TextureStates[1] = RHI::ResourceState::ShaderResource;
        };
        PathTracingParameters parameters;
        const PathTracingCameraSample cameraSample = SamplePathTracingCamera(
            *context.GetActiveCamera(), context.GetPreviousCamera(),
            context.SnapshotDeltaTime, history.SampleCount);
        CameraProxy opticalCamera = cameraSample.Camera;
        if (cameraSample.bThinLens)
        {
            const float focalLength = PathTracingCameraDetail::FocalLength(
                opticalCamera.FieldOfView);
            const float filmScale = 1.0f - focalLength / opticalCamera.FocusDistance;
            opticalCamera.FieldOfView = 360.0f / PathTracingCameraDetail::Pi *
                std::atan(std::tan(opticalCamera.FieldOfView *
                                   PathTracingCameraDetail::Pi / 360.0f) *
                          filmScale);
        }
        const CameraViewConstants camera = CameraViewConstants::BuildForDevice(
            opticalCamera, context.GetActiveAspectRatio(), context.Device);
        camera.CopyShaderInverseViewProjection(parameters.InverseViewProjection);
        camera.CopyCameraPosition(parameters.CameraPosition);
        if (cameraSample.bThinLens &&
            std::isfinite(cameraSample.Camera.FarPlane) &&
            cameraSample.Camera.FarPlane > 0.0f)
        {
            const float farScale = 1.0f - cameraSample.Camera.FarPlane /
                cameraSample.Camera.FocusDistance;
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                const float homogeneous = parameters.InverseViewProjection[column * 4u + 3u];
                for (uint32_t axis = 0u; axis < 3u; ++axis)
                {
                    parameters.InverseViewProjection[column * 4u + axis] +=
                        cameraSample.LensOffset[axis] * farScale * homogeneous;
                }
            }
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                parameters.CameraPosition[axis] += cameraSample.LensOffset[axis];
            }
        }
        parameters.ImageState[0] = history.Width;
        parameters.ImageState[1] = history.Height;
        parameters.ImageState[3] = static_cast<uint32_t>(
            context.SnapshotRayTracingScene->Instances.size());
        const SkyAtmosphereParameters sky = SanitizeSkyAtmosphereParameters(
            context.SnapshotScene ? context.SnapshotScene->SkyAtmosphere :
                                    context.SkyAtmosphereSnapshot);
        const bool bSkyValid = sky.bEnabled && context.SkyAtmosphere.bValid &&
            m_SkyRadianceHandle.IsValid() && m_SkyTransmittanceHandle.IsValid() &&
            m_SunDiskHandle.IsValid() && context.SkyAtmosphere.RadianceTexture &&
            context.SkyAtmosphere.TransmittanceTexture &&
            context.SkyAtmosphere.SunDiskTexture && context.SkyAtmosphere.Sampler;
        if (history.SampleCount > 0u && history.bSkyValid != bSkyValid)
        {
            history.SampleCount = 0u;
        }
        parameters.ImageState[2] = history.SampleCount;
        if (bSkyValid)
        {
            const Math::Vector3 sunDirection = MakeSunDirectionFromAltitudeAzimuth(
                sky.SunAltitudeDegrees, sky.SunAzimuthDegrees);
            parameters.SkySunDirectionAndCosRadius[0] = sunDirection.x;
            parameters.SkySunDirectionAndCosRadius[1] = sunDirection.y;
            parameters.SkySunDirectionAndCosRadius[2] = sunDirection.z;
            parameters.SkySunDirectionAndCosRadius[3] = std::cos(
                std::sqrt(SolarDiskSolidAngleSteradians / 3.14159265358979323846f));
            parameters.SkyState[0] = context.SkyAtmosphere.PreExposure;
            parameters.SkyState[1] = ComputeSunDiskIrradiance(sky) *
                context.SkyAtmosphere.PreExposure;
            parameters.SkyState[2] = 1.0f;
        }
        parameters.SkyState[3] = sky.bEnabled ? 1.0f : 0.0f;
        FillPathFogParameters(context,
                              SafePathPreExposure(context.GetActiveCamera()->PreExposure),
                              parameters);
        parameters.ExposureAndDebug[0] = SafePathPreExposure(context.GetActiveCamera()->PreExposure);
        parameters.ExposureAndDebug[1] = static_cast<float>(static_cast<uint32_t>(m_DebugOutput));
        frameResources.ParametersBuffer->Update(&parameters, sizeof(parameters));

        RHI::DescriptorSetPtr descriptorSet = frameResources.DescriptorSet;
        RHI::AccelerationStructurePtr topLevel =
            context.SnapshotRayTracingScene->TopLevel;
        if (cameraSample.ShutterTime < 1.0f)
        {
            RHI::AccelerationStructureBuildDesc motionBuild;
            motionBuild.type = RHI::AccelerationStructureType::TopLevel;
            bool bHasMotion = false;
            for (const RayTracingSceneInstanceSnapshot& snapshot :
                 context.SnapshotRayTracingScene->Instances)
            {
                RHI::AccelerationStructureInstanceDesc instance = snapshot.Instance;
                if (snapshot.bHasPreviousTransform)
                {
                    float transform[12] = {};
                    if (InterpolatePathTracingTransform(snapshot.PreviousTransform,
                                                        snapshot.Instance.transform,
                                                        cameraSample.ShutterTime,
                                                        transform))
                    {
                        if (std::memcmp(transform, instance.transform,
                                        sizeof(transform)) != 0)
                        {
                            bHasMotion = true;
                        }
                        std::memcpy(instance.transform, transform,
                                    sizeof(transform));
                    }
                }
                instance.bottomLevel = snapshot.BottomLevel;
                motionBuild.instances.push_back(instance);
            }
            if (bHasMotion)
            {
                const uint32_t instanceCount = static_cast<uint32_t>(
                    motionBuild.instances.size());
                if (!frameResources.MotionTopLevel ||
                    frameResources.MotionInstanceCapacity != instanceCount)
                {
                    RHI::AccelerationStructureDesc desc;
                    desc.type = RHI::AccelerationStructureType::TopLevel;
                    desc.maxInstanceCount = instanceCount;
                    frameResources.MotionTopLevel =
                        context.Device->CreateAccelerationStructure(desc);
                    frameResources.MotionInstanceCapacity = frameResources.MotionTopLevel
                                                                 ? instanceCount : 0u;
                }
                if (!frameResources.MotionTopLevel)
                {
                    restoreTextureStates();
                    return;
                }
                motionBuild.destination = frameResources.MotionTopLevel;
                if (!context.CommandList->BuildAccelerationStructure(motionBuild))
                {
                    restoreTextureStates();
                    return;
                }
                topLevel = frameResources.MotionTopLevel;
            }
        }
        if (!descriptorSet->BindAccelerationStructure(0u, topLevel))
        {
            restoreTextureStates();
            return;
        }
        descriptorSet->BindConstantBuffer(1u, frameResources.ParametersBuffer,
                                          0u, sizeof(parameters));
        descriptorSet->BindStorageBuffer(2u, frameResources.InstanceBuffer, 0u,
                                         static_cast<uint32_t>(
                                             context.SnapshotRayTracingScene->Instances.size() *
                                             sizeof(PathTracingInstance)));
        descriptorSet->BindTexture(3u, history.Textures[1u - m_TargetIndex]);
        descriptorSet->BindSampler(3u, m_Sampler);
        descriptorSet->BindStorageTexture(4u, history.Textures[m_TargetIndex]);
        const RHI::TexturePtr& fallback = history.Textures[1u - m_TargetIndex];
        descriptorSet->BindTexture(5u, bSkyValid ? context.SkyAtmosphere.RadianceTexture : fallback);
        descriptorSet->BindTexture(6u, bSkyValid ? context.SkyAtmosphere.TransmittanceTexture : fallback);
        descriptorSet->BindTexture(7u, bSkyValid ? context.SkyAtmosphere.SunDiskTexture : fallback);
        for (uint32_t binding = 5u; binding <= 7u; ++binding)
        {
            descriptorSet->BindSampler(binding, bSkyValid ? context.SkyAtmosphere.Sampler : m_Sampler);
        }
        // 配列の全要素を埋める。表にない要素は既定の白textureにする。
        for (uint32_t element = 0u; element < PathTracingMaterialTextureCapacity; ++element)
        {
            const RHI::TexturePtr& texture = element < frameResources.MaterialTextures.size()
                ? frameResources.MaterialTextures[element]
                : m_DefaultWhiteTexture;
            if (!descriptorSet->BindTextureArrayElement(PathMaterialTextureBinding, element,
                                                        texture, m_MaterialSampler))
            {
                restoreTextureStates();
                return;
            }
        }
        descriptorSet->Update();

        context.CommandList->SetPipeline(m_Pipeline);
        context.CommandList->SetDescriptorSet(descriptorSet, 0u);
        if (!context.CommandList->TraceRays(history.Width, history.Height, 1u))
        {
            restoreTextureStates();
            return;
        }
        restoreTextureStates();
        history.CurrentIndex = m_TargetIndex;
        ++history.SampleCount;
        history.SceneRevision = context.SceneRevision;
        history.LightRevision = context.LightRevision;
        history.CameraSignature = m_DeclaredCameraSignature;
        history.GeometrySignature = m_DeclaredGeometrySignature;
        history.SkySignature = m_DeclaredSkySignature;
        history.FogSignature = m_DeclaredFogSignature;
        history.DebugOutput = m_DebugOutput;
        history.MaterialTextureSignature = m_DeclaredMaterialTextureSignature;
        history.bSkyValid = bSkyValid;
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
