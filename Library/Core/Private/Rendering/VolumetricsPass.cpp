#include "Rendering/VolumetricsPass.h"
#include "VolumetricFogScattering.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct alignas(16) GPUVolumetricsParams
        {
            float InverseViewProjection[16];
            float CameraPositionAndPreExposure[4];
            float CameraForwardAndScatteringEnabled[4];
            float FogParameters[4];
            float FallbackFogColor[4];
            float DirectionalLightDirectionAndAnisotropy[4];
            float DirectionalLightRadianceAndEnabled[4];
            float CascadeView[PhysicalLightingShadowCascadeCount][16];
            float CascadeProjection[PhysicalLightingShadowCascadeCount][16];
            float CascadeSplitDistances[8];
            // x=空気遠近が有効なら 1、y=距離の倍率、z=掛け始める距離（m）、w=観測点の高度の透過率 LUT の v
            float AerialPerspective[4];
            // rgb=高度 0 の Rayleigh の消散係数（1/m）、w=Rayleigh のスケールハイト（m）
            float RayleighExtinctionAndScaleHeight[4];
            // x=高度 0 の Mie の消散係数（1/m）、y=Mie のスケールハイト（m）、z=ワールドの y = 0 の高度（m）
            float MieExtinctionAndScaleHeight[4];
        };

        static_assert(sizeof(GPUVolumetricsParams) == 752u);

        RHI::DescriptorSetDesc CreateVolumetricsDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;

            RHI::DescriptorBinding sceneDepthBinding;
            sceneDepthBinding.binding = 0u;
            sceneDepthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            sceneDepthBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(sceneDepthBinding);

            RHI::DescriptorBinding skyRadianceBinding;
            skyRadianceBinding.binding = 1u;
            skyRadianceBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            skyRadianceBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(skyRadianceBinding);

            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = 2u;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(paramsBinding);

            RHI::DescriptorBinding shadowMapBinding;
            shadowMapBinding.binding = 3u;
            shadowMapBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            shadowMapBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(shadowMapBinding);

            RHI::DescriptorBinding skyTransmittanceBinding;
            skyTransmittanceBinding.binding = 4u;
            skyTransmittanceBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            skyTransmittanceBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(skyTransmittanceBinding);
            return desc;
        }

        float SafePreExposure(const CameraProxy* camera)
        {
            if (!camera || !std::isfinite(camera->PreExposure) || camera->PreExposure <= 0.0f)
            {
                return 1.0f;
            }
            return std::clamp(camera->PreExposure, 1.0e-6f, 1.0e6f);
        }

        float SafeFogColorChannel(float value)
        {
            return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
        }

        bool IsValidCascadedShadowMapTexture(const RHI::TexturePtr& texture)
        {
            if (!texture || texture->GetWidth() == 0u || texture->GetHeight() == 0u ||
                texture->GetArraySize() != PhysicalLightingShadowCascadeCount ||
                texture->GetFormat() != RHI::Format::D32_FLOAT)
            {
                return false;
            }

            const RHI::ResourceUsage usage = texture->GetUsage();
            return (usage & RHI::ResourceUsage::ShaderRead) != RHI::ResourceUsage::None &&
                   (usage & RHI::ResourceUsage::DepthStencil) != RHI::ResourceUsage::None;
        }

        bool IsValidShadowMapArrayFallbackTexture(const RHI::TexturePtr& texture)
        {
            if (!texture || texture->GetWidth() == 0u || texture->GetHeight() == 0u ||
                texture->GetArraySize() != PhysicalLightingShadowCascadeCount ||
                texture->GetFormat() != RHI::Format::R8G8B8A8_UNORM)
            {
                return false;
            }

            return (texture->GetUsage() & RHI::ResourceUsage::ShaderRead) !=
                   RHI::ResourceUsage::None;
        }

        bool HasValidCascadedShadowValues(const PhysicalLightingResources& lighting)
        {
            if (!lighting.HasCompleteCascadedShadow())
            {
                return false;
            }

            const CascadedDirectionalShadowShaderValues& shadow = lighting.CascadedShadow;
            for (uint32_t cascadeIndex = 0u;
                 cascadeIndex < PhysicalLightingShadowCascadeCount;
                 ++cascadeIndex)
            {
                for (uint32_t matrixIndex = 0u; matrixIndex < 16u; ++matrixIndex)
                {
                    if (!std::isfinite(shadow.View[cascadeIndex][matrixIndex]) ||
                        !std::isfinite(shadow.Projection[cascadeIndex][matrixIndex]))
                    {
                        return false;
                    }
                }
            }

            for (uint32_t splitIndex = 0u;
                 splitIndex < PhysicalLightingShadowSplitCount;
                 ++splitIndex)
            {
                if (!std::isfinite(shadow.SplitDistances[splitIndex]) ||
                    (splitIndex > 0u &&
                     shadow.SplitDistances[splitIndex] <= shadow.SplitDistances[splitIndex - 1u]))
                {
                    return false;
                }
            }
            return true;
        }

        bool TryBuildDirectionalScatteringLight(const ViewRenderContext& context,
                                                float (&outDirectionAndAnisotropy)[4],
                                                float (&outRadianceAndEnabled)[4])
        {
            if (!context.SnapshotLightProxies || !HasValidCascadedShadowValues(context.PhysicalLighting))
            {
                return false;
            }

            for (const LightProxy& light : *context.SnapshotLightProxies)
            {
                if (light.LightId != context.PhysicalLighting.CascadedShadow.LightId ||
                    light.Type != LightType::Directional || !light.IsValid() ||
                    !std::isfinite(light.DirectionX) || !std::isfinite(light.DirectionY) ||
                    !std::isfinite(light.DirectionZ) ||
                    !std::isfinite(light.CanonicalIntensity) || light.CanonicalIntensity <= 0.0f ||
                    !std::isfinite(light.ColorR) || !std::isfinite(light.ColorG) ||
                    !std::isfinite(light.ColorB) || light.ColorR < 0.0f ||
                    light.ColorG < 0.0f || light.ColorB < 0.0f)
                {
                    continue;
                }

                const double directionLength = std::sqrt(
                    static_cast<double>(light.DirectionX) * light.DirectionX +
                    static_cast<double>(light.DirectionY) * light.DirectionY +
                    static_cast<double>(light.DirectionZ) * light.DirectionZ);
                if (!std::isfinite(directionLength) || directionLength <= 1.0e-8)
                {
                    continue;
                }

                const double intensity = std::min(
                    static_cast<double>(light.CanonicalIntensity), 1.0e7);
                const double radiance[3] = {
                    static_cast<double>(light.ColorR) * intensity,
                    static_cast<double>(light.ColorG) * intensity,
                    static_cast<double>(light.ColorB) * intensity};
                if (!std::isfinite(radiance[0]) || !std::isfinite(radiance[1]) ||
                    !std::isfinite(radiance[2]) ||
                    (std::max)({radiance[0], radiance[1], radiance[2]}) <= 0.0)
                {
                    continue;
                }

                const double inverseDirectionLength = 1.0 / directionLength;
                outDirectionAndAnisotropy[0] = static_cast<float>(light.DirectionX * inverseDirectionLength);
                outDirectionAndAnisotropy[1] = static_cast<float>(light.DirectionY * inverseDirectionLength);
                outDirectionAndAnisotropy[2] = static_cast<float>(light.DirectionZ * inverseDirectionLength);
                outDirectionAndAnisotropy[3] = VolumetricFogDetail::ScatteringAnisotropy;
                outRadianceAndEnabled[0] = static_cast<float>(std::min(radiance[0], 1.0e8));
                outRadianceAndEnabled[1] = static_cast<float>(std::min(radiance[1], 1.0e8));
                outRadianceAndEnabled[2] = static_cast<float>(std::min(radiance[2], 1.0e8));
                outRadianceAndEnabled[3] = 1.0f;
                return true;
            }
            return false;
        }
    } // namespace

    VolumetricsPass::~VolumetricsPass()
    {
        Shutdown();
    }

    bool VolumetricsPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Device or ShaderManager is null");
            return false;
        }

        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("volumetrics.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to load fullscreen volumetrics shaders");
            Shutdown();
            return false;
        }

        RHI::SamplerDesc depthSamplerDesc;
        depthSamplerDesc.filterMin = RHI::FilterMode::Point;
        depthSamplerDesc.filterMag = RHI::FilterMode::Point;
        depthSamplerDesc.filterMip = RHI::FilterMode::Point;
        depthSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        depthSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        depthSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_SceneDepthSampler = m_Device->CreateSampler(depthSamplerDesc);

        RHI::SamplerDesc skyRadianceSamplerDesc;
        skyRadianceSamplerDesc.filterMin = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.filterMag = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.filterMip = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.addressU = RHI::TextureAddressMode::Wrap;
        skyRadianceSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        skyRadianceSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_SkyRadianceSampler = m_Device->CreateSampler(skyRadianceSamplerDesc);

        RHI::SamplerDesc skyTransmittanceSamplerDesc = skyRadianceSamplerDesc;
        skyTransmittanceSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        m_SkyTransmittanceSampler = m_Device->CreateSampler(skyTransmittanceSamplerDesc);

        // 記述子セットのレイアウト（同じ形のパスと共有する 1 つをデバイスから引く。セットと定数は Execute ごとに借りる）
        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateVolumetricsDescriptorSetDesc());
        if (!m_SceneDepthSampler || !m_SkyRadianceSampler || !m_SkyTransmittanceSampler ||
            !m_DescriptorSetLayout)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "サンプラか記述子セットのレイアウトを作れませんでした");
            Shutdown();
            return false;
        }

        m_bInitialized = true;
        return true;
    }

    void VolumetricsPass::Shutdown()
    {
        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();
        m_UnownedDescriptorSets.clear();
        m_UnownedBuffers.clear();
        m_DescriptorSetLayout.reset();
        m_SkyTransmittanceSampler.reset();
        m_SkyRadianceSampler.reset();
        m_SceneDepthSampler.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_Device = nullptr;
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_SkyAtmosphereTransmittanceHandle = {};
        m_CascadedShadowMapHandle = {};
        m_CurrentWidth = 0u;
        m_CurrentHeight = 0u;
        m_CurrentFormat = RHI::Format::UNKNOWN;
        m_FramebufferSceneColorTexture = nullptr;
        m_bInitialized = false;
    }

    void VolumetricsPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void VolumetricsPass::Execute(ViewRenderContext& /*context*/)
    {
    }

    void VolumetricsPass::Declare(RenderGraphBuilder& builder)
    {
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_SkyAtmosphereTransmittanceHandle = {};
        m_CascadedShadowMapHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        const SceneProxy* snapshotScene = context ? context->SnapshotScene : nullptr;
        m_FogParameters = snapshotScene
                              ? SanitizeVolumetricFogParameters(snapshotScene->VolumetricFog)
                              : MakeDefaultVolumetricFogParameters();
        m_SkyParameters = snapshotScene
                              ? SanitizeSkyAtmosphereParameters(snapshotScene->SkyAtmosphere)
                              : SkyAtmosphereParameters{};
        m_AerialPerspective = snapshotScene
                                  ? SanitizeAerialPerspectiveParameters(snapshotScene->AerialPerspective)
                                  : AerialPerspectiveParameters{};
        // 空気遠近は空の LUT から求めるので、空が無効なら掛けない
        m_AerialPerspective.bEnabled = m_AerialPerspective.bEnabled && m_SkyParameters.bEnabled;
        if (!m_FogParameters.bEnabled && !m_AerialPerspective.bEnabled)
        {
            return;
        }

        RGTextureHandle sceneDepthHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth,
                                    sceneDepthHandle,
                                    RHI::ResourceState::ShaderResource))
        {
            return;
        }
        m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();

        RGTextureHandle skyRadianceHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::SkyAtmosphereRadiance,
                                   skyRadianceHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_SkyAtmosphereRadianceHandle = skyRadianceHandle.ToResourceHandle();
        }

        RGTextureHandle skyTransmittanceHandle;
        if (m_AerialPerspective.bEnabled &&
            builder.TryReadTexture(RenderGraphResourceNames::SkyAtmosphereTransmittance,
                                   skyTransmittanceHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_SkyAtmosphereTransmittanceHandle = skyTransmittanceHandle.ToResourceHandle();
        }

        RGTextureHandle cascadedShadowMapHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::ShadowMap,
                                   cascadedShadowMapHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_CascadedShadowMapHandle = cascadedShadowMapHandle.ToResourceHandle();
        }

        // SSRがあれば、その出力（照明の色の環境光の鏡面反射を画面の反射へ置き換えた色）へフォグを重ねる
        RGTextureHandle sceneColorHandle;
        const bool bHasSceneColor =
            builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SSRSceneColor,
                                                sceneColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource) ||
            builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                sceneColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource);
        if (!bHasSceneColor)
        {
            m_SceneDepthHandle = {};
            m_SkyAtmosphereRadianceHandle = {};
            m_SkyAtmosphereTransmittanceHandle = {};
            m_CascadedShadowMapHandle = {};
            return;
        }
        m_SceneColorHandle = sceneColorHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void VolumetricsPass::Execute(RenderGraphResources& resources,
                                  ViewRenderContext& context)
    {
        if ((!m_FogParameters.bEnabled && !m_AerialPerspective.bEnabled) ||
            !m_SceneColorHandle.IsValid() ||
            !m_SceneDepthHandle.IsValid())
        {
            return;
        }

        RHI::TexturePtr sceneColorTexture = resources.GetTexture(m_SceneColorHandle);
        RHI::TexturePtr sceneDepthTexture = resources.GetTexture(m_SceneDepthHandle);
        RHI::TexturePtr skyRadianceTexture = m_SkyAtmosphereRadianceHandle.IsValid()
                                                 ? resources.GetTexture(m_SkyAtmosphereRadianceHandle)
                                                 : RHI::TexturePtr{};
        RHI::TexturePtr skyTransmittanceTexture =
            m_SkyAtmosphereTransmittanceHandle.IsValid()
                ? resources.GetTexture(m_SkyAtmosphereTransmittanceHandle)
                : RHI::TexturePtr{};
        RHI::TexturePtr cascadedShadowMapTexture = m_CascadedShadowMapHandle.IsValid()
                                                       ? resources.GetTexture(m_CascadedShadowMapHandle)
                                                       : RHI::TexturePtr{};
        const PhysicalLightingResources& physicalLighting = context.PhysicalLighting;
        const bool bActualShadowMapAvailable =
            IsValidCascadedShadowMapTexture(cascadedShadowMapTexture) &&
            physicalLighting.ShadowMapTexture &&
            physicalLighting.ShadowMapTexture.get() == cascadedShadowMapTexture.get() &&
            physicalLighting.ShadowSampler;
        const RHI::TexturePtr& shadowMapTextureForSampling = bActualShadowMapAvailable
                                                                 ? cascadedShadowMapTexture
                                                                 : physicalLighting.ShadowMapFallbackTexture;
        const RHI::SamplerPtr& shadowMapSamplerForSampling = bActualShadowMapAvailable
                                                                 ? physicalLighting.ShadowSampler
                                                                 : physicalLighting.ShadowMapFallbackSampler;
        const bool bShadowSamplingTextureAvailable =
            bActualShadowMapAvailable ||
            IsValidShadowMapArrayFallbackTexture(shadowMapTextureForSampling);
        if (!sceneColorTexture)
        {
            return;
        }
        if (!PrepareResources(sceneColorTexture))
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }
        if (!sceneDepthTexture || !m_DescriptorSetLayout ||
            !bShadowSamplingTextureAvailable || !shadowMapSamplerForSampling)
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        const CameraProxy* activeCamera = context.GetActiveCamera();
        if (!activeCamera)
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        GPUVolumetricsParams params{};
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*activeCamera,
                                                context.GetActiveAspectRatio(),
                                                context.Device);
        cameraConstants.CopyShaderInverseViewProjection(params.InverseViewProjection);
        params.CameraPositionAndPreExposure[0] = activeCamera->PositionX;
        params.CameraPositionAndPreExposure[1] = activeCamera->PositionY;
        params.CameraPositionAndPreExposure[2] = activeCamera->PositionZ;
        params.CameraPositionAndPreExposure[3] = SafePreExposure(activeCamera);

        const double cameraForwardLength = std::sqrt(
            static_cast<double>(activeCamera->ForwardX) * activeCamera->ForwardX +
            static_cast<double>(activeCamera->ForwardY) * activeCamera->ForwardY +
            static_cast<double>(activeCamera->ForwardZ) * activeCamera->ForwardZ);
        bool bCameraForwardValid = std::isfinite(cameraForwardLength) && cameraForwardLength > 1.0e-8;
        if (bCameraForwardValid)
        {
            const double inverseForwardLength = 1.0 / cameraForwardLength;
            params.CameraForwardAndScatteringEnabled[0] =
                static_cast<float>(activeCamera->ForwardX * inverseForwardLength);
            params.CameraForwardAndScatteringEnabled[1] =
                static_cast<float>(activeCamera->ForwardY * inverseForwardLength);
            params.CameraForwardAndScatteringEnabled[2] =
                static_cast<float>(activeCamera->ForwardZ * inverseForwardLength);
        }
        // 霧が無効で空気遠近だけ掛けるときは、霧の密度を 0 にして霧の分を消す
        params.FogParameters[0] = m_FogParameters.bEnabled ? m_FogParameters.DensityAtBaseHeight : 0.0f;
        params.FogParameters[1] = m_FogParameters.BaseHeight;
        params.FogParameters[2] = m_FogParameters.HeightFalloffPerUnit;
        params.FogParameters[3] = skyRadianceTexture ? 1.0f : 0.0f;

        const SceneProxy* scene = context.SnapshotScene;
        params.FallbackFogColor[0] = scene ? SafeFogColorChannel(scene->FogColorR) : 0.0f;
        params.FallbackFogColor[1] = scene ? SafeFogColorChannel(scene->FogColorG) : 0.0f;
        params.FallbackFogColor[2] = scene ? SafeFogColorChannel(scene->FogColorB) : 0.0f;
        params.FallbackFogColor[3] = 1.0f;

        // 空気遠近は空の放射輝度 LUT と透過率 LUT の両方があるときだけ掛ける
        const bool bAerialPerspectiveAvailable = m_AerialPerspective.bEnabled &&
            skyRadianceTexture && skyTransmittanceTexture &&
            skyTransmittanceTexture->GetHeight() > 0u;
        if (bAerialPerspectiveAvailable)
        {
            const uint32_t transmittanceHeight = skyTransmittanceTexture->GetHeight();
            // 透過率 LUT の行は高度の割合 0〜1 を両端の texel の中心に置く（SkyAtmospherePass）。
            // 空の放射輝度 LUT と同じ観測点の高度の行を引く。
            const float observerAltitudeFraction = std::clamp(
                SkyAtmosphereModel::ObserverAltitudeMeters / m_SkyParameters.AtmosphereHeightMeters,
                0.0f,
                1.0f);
            const AtmosphereGroundExtinction extinction = GetAtmosphereGroundExtinction();
            params.AerialPerspective[0] = 1.0f;
            params.AerialPerspective[1] = m_AerialPerspective.DistanceScale;
            params.AerialPerspective[2] = m_AerialPerspective.StartDistanceMeters;
            params.AerialPerspective[3] =
                (observerAltitudeFraction * static_cast<float>(transmittanceHeight - 1u) + 0.5f) /
                static_cast<float>(transmittanceHeight);
            params.RayleighExtinctionAndScaleHeight[0] = extinction.Rayleigh.x;
            params.RayleighExtinctionAndScaleHeight[1] = extinction.Rayleigh.y;
            params.RayleighExtinctionAndScaleHeight[2] = extinction.Rayleigh.z;
            params.RayleighExtinctionAndScaleHeight[3] = m_SkyParameters.RayleighScaleHeightMeters;
            params.MieExtinctionAndScaleHeight[0] = extinction.Mie;
            params.MieExtinctionAndScaleHeight[1] = m_SkyParameters.MieScaleHeightMeters;
            params.MieExtinctionAndScaleHeight[2] = m_AerialPerspective.OriginAltitudeMeters;
        }

        const ViewportRenderPlan* activeViewport = context.CurrentViewport;
        const bool bPhysicalLightingMatches = activeViewport &&
            physicalLighting.Matches(context.FrameNumber,
                                     activeViewport->ViewId,
                                     activeViewport->ViewportId);
        const bool bCascadedShadowAvailable =
            bCameraForwardValid && bPhysicalLightingMatches && bActualShadowMapAvailable &&
            physicalLighting.HasCompleteCascadedShadow() &&
            HasValidCascadedShadowValues(physicalLighting);
        const bool bDirectionalLightAvailable = bCascadedShadowAvailable &&
            TryBuildDirectionalScatteringLight(context,
                                               params.DirectionalLightDirectionAndAnisotropy,
                                               params.DirectionalLightRadianceAndEnabled);
        if (bDirectionalLightAvailable)
        {
            params.CameraForwardAndScatteringEnabled[3] = 1.0f;
            std::memcpy(params.CascadeView,
                        physicalLighting.CascadedShadow.View,
                        sizeof(params.CascadeView));
            std::memcpy(params.CascadeProjection,
                        physicalLighting.CascadedShadow.Projection,
                        sizeof(params.CascadeProjection));
            std::memcpy(params.CascadeSplitDistances,
                        physicalLighting.CascadedShadow.SplitDistances,
                        sizeof(float) * PhysicalLightingShadowSplitCount);
        }

        // 定数と記述子セットはこの Execute の分をフレームの持ち主から借りる。持ち主の無い文脈（RenderingCoordinator を
        // 通さない検査）では使い回さない定数と記述子セットを作り、提出の完了を知る手段が無いのでパスを手放すまで持つ
        RHI::DescriptorSetPtr descriptorSet;
        RHI::BufferPtr paramsBuffer;
        uint32_t paramsOffset = 0u;
        if (FrameResourceLane* frameResources = context.FrameResources)
        {
            const FrameUploadAllocation paramsAllocation = frameResources->AllocateConstants(params);
            if (paramsAllocation.IsValid())
            {
                paramsBuffer = paramsAllocation.Buffer;
                paramsOffset = paramsAllocation.GetOffset32();
            }
            descriptorSet = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout).Set;
        }
        else if (m_Device)
        {
            paramsBuffer = m_Device->CreateBuffer(RHI::BufferDesc(sizeof(GPUVolumetricsParams),
                                                                  RHI::ResourceUsage::ConstantBuffer,
                                                                  true,
                                                                  "VolumetricsParams.Unowned"));
            if (paramsBuffer)
            {
                paramsBuffer->Update(&params, sizeof(params));
                m_UnownedBuffers.push_back(paramsBuffer);
            }
            descriptorSet = m_Device->CreateDescriptorSet(CreateVolumetricsDescriptorSetDesc());
            if (descriptorSet)
            {
                m_UnownedDescriptorSets.push_back(descriptorSet);
            }
        }
        if (!paramsBuffer || !descriptorSet)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "定数か記述子セットを借りられないため、このフレームは霧を描きません");
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        descriptorSet->BindConstantBuffer(2u, paramsBuffer, paramsOffset, sizeof(GPUVolumetricsParams));
        descriptorSet->BindTexture(0u, sceneDepthTexture);
        descriptorSet->BindSampler(0u, m_SceneDepthSampler);
        descriptorSet->BindTexture(1u,
                                   skyRadianceTexture ? skyRadianceTexture : sceneDepthTexture);
        descriptorSet->BindSampler(1u,
                                   skyRadianceTexture ? m_SkyRadianceSampler : m_SceneDepthSampler);
        descriptorSet->BindTexture(3u, shadowMapTextureForSampling);
        descriptorSet->BindSampler(3u, shadowMapSamplerForSampling);
        descriptorSet->BindTexture(4u,
                                   bAerialPerspectiveAvailable ? skyTransmittanceTexture : sceneDepthTexture);
        descriptorSet->BindSampler(4u,
                                   bAerialPerspectiveAvailable ? m_SkyTransmittanceSampler : m_SceneDepthSampler);
        descriptorSet->Update();

        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      m_Pipeline,
                                      descriptorSet);
    }

    void VolumetricsPass::EnqueueEmptyNativePass(
        ViewRenderContext& context,
        const RHI::TexturePtr& sceneColorTexture) const
    {
        if (m_RenderPass && m_Framebuffer &&
            m_FramebufferSceneColorTexture == sceneColorTexture.get())
        {
            context.EnqueueFullscreenPass(m_RenderPass,
                                          m_Framebuffer,
                                          context.GetActiveLocalViewport(),
                                          context.GetActiveLocalScissor(),
                                          nullptr,
                                          nullptr);
            return;
        }

        if (sceneColorTexture)
        {
            context.EnqueueTextureBarrier(sceneColorTexture,
                                          RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
        }
    }

    bool VolumetricsPass::PrepareResources(const RHI::TexturePtr& sceneColorTexture)
    {
        if (!m_Device || !sceneColorTexture)
        {
            return false;
        }

        const uint32_t width = sceneColorTexture->GetWidth();
        const uint32_t height = sceneColorTexture->GetHeight();
        const RHI::Format format = sceneColorTexture->GetFormat();
        if (width == 0u || height == 0u)
        {
            return false;
        }

        const bool bResourcesChanged =
            !m_RenderPass ||
            !m_Framebuffer ||
            !m_Pipeline ||
            m_CurrentWidth != width ||
            m_CurrentHeight != height ||
            m_CurrentFormat != format ||
            m_FramebufferSceneColorTexture != sceneColorTexture.get();
        if (!bResourcesChanged)
        {
            return true;
        }

        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();

        RHI::RenderPassDesc renderPassDesc;
        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = format;
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = false;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::RenderTarget;
        colorAttachment.finalState = RHI::ResourceState::ShaderResource;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;
        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to create render pass");
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        framebufferDesc.colorTargets.push_back(sceneColorTexture);
        framebufferDesc.width = width;
        framebufferDesc.height = height;
        m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to create framebuffer");
            m_RenderPass.reset();
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_VertexShader;
        pipelineDesc.pixelShader = m_FragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = RHI::BlendFactor::One;
        blendAttachment.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blendAttachment.colorBlendOp = RHI::BlendOp::Add;
        blendAttachment.srcAlphaBlendFactor = RHI::BlendFactor::Zero;
        blendAttachment.dstAlphaBlendFactor = RHI::BlendFactor::One;
        blendAttachment.alphaBlendOp = RHI::BlendOp::Add;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::R |
                                         RHI::ColorWriteMask::G |
                                         RHI::ColorWriteMask::B;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);
        pipelineDesc.renderPass = m_RenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(CreateVolumetricsDescriptorSetDesc());
        m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("VolumetricsPass", "Failed to create graphics pipeline");
            m_Framebuffer.reset();
            m_RenderPass.reset();
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_CurrentFormat = format;
        m_FramebufferSceneColorTexture = sceneColorTexture.get();
        return true;
    }
} // namespace NorvesLib::Core::Rendering
