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
#include "RHI/ICommandList.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>

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
            // x=高度 0 の Mie の消散係数（1/m。空気遠近の密度の倍率を掛けた値）、y=Mie のスケールハイト（m）、z=ワールドの y = 0 の高度（m）
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

        // VolumetricsPass が詰める定数の、そのフレームで使えたもの
        struct VolumetricsParamsState
        {
            bool bAerialAvailable = false;
            bool bDirectionalAvailable = false;
        };

        // volumetrics.frag（と同じ式を使う volumetrics_probe.frag）の定数を詰める。VolumetricsPass と AtmosphereProbePass が
        // 同じ値で評価するよう、ここ 1 か所で作る
        void BuildVolumetricsParams(const ViewRenderContext& context,
                                    const CameraProxy& camera,
                                    const VolumetricFogParameters& fog,
                                    const SkyAtmosphereParameters& sky,
                                    const AerialPerspectiveParameters& aerial,
                                    const RHI::TexturePtr& skyRadianceTexture,
                                    const RHI::TexturePtr& skyTransmittanceTexture,
                                    bool bActualShadowMapAvailable,
                                    GPUVolumetricsParams& outParams,
                                    VolumetricsParamsState& outState)
        {
            outParams = GPUVolumetricsParams{};
            outState = VolumetricsParamsState{};
            const PhysicalLightingResources& physicalLighting = context.PhysicalLighting;
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(camera,
                                                    context.GetActiveAspectRatio(),
                                                    context.Device);
            cameraConstants.CopyShaderInverseViewProjection(outParams.InverseViewProjection);
            outParams.CameraPositionAndPreExposure[0] = camera.PositionX;
            outParams.CameraPositionAndPreExposure[1] = camera.PositionY;
            outParams.CameraPositionAndPreExposure[2] = camera.PositionZ;
            outParams.CameraPositionAndPreExposure[3] = SafePreExposure(&camera);

            const double cameraForwardLength = std::sqrt(
                static_cast<double>(camera.ForwardX) * camera.ForwardX +
                static_cast<double>(camera.ForwardY) * camera.ForwardY +
                static_cast<double>(camera.ForwardZ) * camera.ForwardZ);
            bool bCameraForwardValid = std::isfinite(cameraForwardLength) && cameraForwardLength > 1.0e-8;
            if (bCameraForwardValid)
            {
                const double inverseForwardLength = 1.0 / cameraForwardLength;
                outParams.CameraForwardAndScatteringEnabled[0] =
                    static_cast<float>(camera.ForwardX * inverseForwardLength);
                outParams.CameraForwardAndScatteringEnabled[1] =
                    static_cast<float>(camera.ForwardY * inverseForwardLength);
                outParams.CameraForwardAndScatteringEnabled[2] =
                    static_cast<float>(camera.ForwardZ * inverseForwardLength);
            }
            // 霧が無効で空気遠近だけ掛けるときは、霧の密度を 0 にして霧の分を消す
            outParams.FogParameters[0] = fog.bEnabled ? fog.DensityAtBaseHeight : 0.0f;
            outParams.FogParameters[1] = fog.BaseHeight;
            outParams.FogParameters[2] = fog.HeightFalloffPerUnit;
            outParams.FogParameters[3] = skyRadianceTexture ? 1.0f : 0.0f;

            const SceneProxy* scene = context.SnapshotScene;
            outParams.FallbackFogColor[0] = scene ? SafeFogColorChannel(scene->FogColorR) : 0.0f;
            outParams.FallbackFogColor[1] = scene ? SafeFogColorChannel(scene->FogColorG) : 0.0f;
            outParams.FallbackFogColor[2] = scene ? SafeFogColorChannel(scene->FogColorB) : 0.0f;
            outParams.FallbackFogColor[3] = 1.0f;

            // 空気遠近は空の放射輝度 LUT と透過率 LUT の両方があるときだけ掛ける
            outState.bAerialAvailable = aerial.bEnabled &&
                skyRadianceTexture && skyTransmittanceTexture &&
                skyTransmittanceTexture->GetHeight() > 0u;
            if (outState.bAerialAvailable)
            {
                const uint32_t transmittanceHeight = skyTransmittanceTexture->GetHeight();
                // 透過率 LUT の行は高度の割合 0〜1 を両端の texel の中心に置く（SkyAtmospherePass）。
                // 空の放射輝度 LUT と同じ観測点の高度（地表 GroundAltitudeMeters の ObserverAltitudeMeters 上）の行を引く。
                const float observerAltitudeFraction = std::clamp(
                    (sky.GroundAltitudeMeters + SkyAtmosphereModel::ObserverAltitudeMeters) /
                        sky.AtmosphereHeightMeters,
                    0.0f,
                    1.0f);
                const AtmosphereGroundExtinction extinction = GetAtmosphereGroundExtinction();
                outParams.AerialPerspective[0] = 1.0f;
                outParams.AerialPerspective[1] = aerial.DistanceScale;
                outParams.AerialPerspective[2] = aerial.StartDistanceMeters;
                outParams.AerialPerspective[3] =
                    (observerAltitudeFraction * static_cast<float>(transmittanceHeight - 1u) + 0.5f) /
                    static_cast<float>(transmittanceHeight);
                outParams.RayleighExtinctionAndScaleHeight[0] = extinction.Rayleigh.x;
                outParams.RayleighExtinctionAndScaleHeight[1] = extinction.Rayleigh.y;
                outParams.RayleighExtinctionAndScaleHeight[2] = extinction.Rayleigh.z;
                outParams.RayleighExtinctionAndScaleHeight[3] = sky.RayleighScaleHeightMeters;
                outParams.MieExtinctionAndScaleHeight[0] = extinction.Mie * aerial.MieDensityScale;
                outParams.MieExtinctionAndScaleHeight[1] = sky.MieScaleHeightMeters;
                outParams.MieExtinctionAndScaleHeight[2] = aerial.OriginAltitudeMeters;
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
            outState.bDirectionalAvailable = bCascadedShadowAvailable &&
                TryBuildDirectionalScatteringLight(context,
                                                   outParams.DirectionalLightDirectionAndAnisotropy,
                                                   outParams.DirectionalLightRadianceAndEnabled);
            if (outState.bDirectionalAvailable)
            {
                outParams.CameraForwardAndScatteringEnabled[3] = 1.0f;
                std::memcpy(outParams.CascadeView,
                            physicalLighting.CascadedShadow.View,
                            sizeof(outParams.CascadeView));
                std::memcpy(outParams.CascadeProjection,
                            physicalLighting.CascadedShadow.Projection,
                            sizeof(outParams.CascadeProjection));
                std::memcpy(outParams.CascadeSplitDistances,
                            physicalLighting.CascadedShadow.SplitDistances,
                            sizeof(float) * PhysicalLightingShadowSplitCount);
            }
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
        VolumetricsParamsState paramsState;
        BuildVolumetricsParams(context,
                               *activeCamera,
                               m_FogParameters,
                               m_SkyParameters,
                               m_AerialPerspective,
                               skyRadianceTexture,
                               skyTransmittanceTexture,
                               bActualShadowMapAvailable,
                               params,
                               paramsState);
        const bool bAerialPerspectiveAvailable = paramsState.bAerialAvailable;

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

#if NORVES_ENABLE_STATS
    namespace
    {
        // 大気の書き出しの視線（AtmosphereProbePass の説明の約束）
        constexpr float kAzimuthOffsetsDegrees[] = {0.0f, 90.0f, 180.0f, 270.0f};
        constexpr float kElevationsDegrees[] = {-5.0f, -2.0f, 0.0f, 2.0f, 5.0f, 10.0f, 20.0f, 30.0f, 45.0f, 60.0f, 90.0f};
        constexpr float kDistancesMeters[] = {300.0f, 1000.0f, 3000.0f, 10000.0f, 30000.0f};
        constexpr uint32_t kDirectionRayCount = static_cast<uint32_t>(
            std::size(kAzimuthOffsetsDegrees) * std::size(kElevationsDegrees) * std::size(kDistancesMeters));
        constexpr uint32_t kGroundRayCount =
            static_cast<uint32_t>(std::size(kAzimuthOffsetsDegrees) * std::size(kDistancesMeters));
        static_assert(kDirectionRayCount + kGroundRayCount <= FrameCaptureAtmosphereMaxRays);

        constexpr RHI::Format AtmosphereProbeFormat = RHI::Format::R32G32B32A32_FLOAT;

        // volumetrics_probe.frag の AtmosphereProbeRays（std140）
        struct alignas(16) GPUAtmosphereProbeRays
        {
            uint32_t RayCount[4];
            float Rays[FrameCaptureAtmosphereMaxRays][4];
        };

        RHI::DescriptorSetDesc CreateAtmosphereProbeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc = CreateVolumetricsDescriptorSetDesc();
            RHI::DescriptorBinding raysBinding;
            raysBinding.binding = 5u;
            raysBinding.type = RHI::ResourceBindType::ConstantBuffer;
            raysBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(raysBinding);
            return desc;
        }

        // 仰角・方位角[度]の向き（MakeSunDirectionFromAltitudeAzimuth と同じ回り方。地平線より下の仰角も受ける）
        void MakeElevationAzimuthDirection(double elevationDegrees, double azimuthDegrees, float (&outDirection)[3])
        {
            constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
            const double elevation = elevationDegrees * kRadiansPerDegree;
            const double azimuth = azimuthDegrees * kRadiansPerDegree;
            const double horizontal = std::cos(elevation);
            outDirection[0] = static_cast<float>(horizontal * std::cos(azimuth));
            outDirection[1] = static_cast<float>(std::sin(elevation));
            outDirection[2] = static_cast<float>(horizontal * std::sin(azimuth));
        }
    } // namespace

    bool AtmosphereProbePass::IsRequestedByEnvironment()
    {
        char* value = nullptr;
        size_t length = 0;
        bool bRequested = false;
        if (_dupenv_s(&value, &length, "NORVES_ATMOSPHERE_DUMP") == 0 && value)
        {
            bRequested = std::strcmp(value, "1") == 0;
        }
        std::free(value);
        return bRequested;
    }

    AtmosphereProbePass::~AtmosphereProbePass()
    {
        Shutdown();
    }

    bool AtmosphereProbePass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        m_bInitialized = true;
        if (!m_Device || !context.ShaderMgr)
        {
            return true;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("volumetrics_probe.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_WARNING("AtmosphereProbePass", "大気の書き出しのシェーダーの読み込みに失敗。このパスは何もしません");
            return true;
        }

        // サンプラーは VolumetricsPass と同じ設定（深度は点、空の LUT は線形）
        RHI::SamplerDesc pointSamplerDesc;
        pointSamplerDesc.filterMin = RHI::FilterMode::Point;
        pointSamplerDesc.filterMag = RHI::FilterMode::Point;
        pointSamplerDesc.filterMip = RHI::FilterMode::Point;
        pointSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        pointSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        pointSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_PointSampler = m_Device->CreateSampler(pointSamplerDesc);

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

        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateAtmosphereProbeDescriptorSetDesc());

        RHI::RenderPassDesc renderPassDesc;
        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = AtmosphereProbeFormat;
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = false;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::DontCare;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::RenderTarget;
        colorAttachment.finalState = RHI::ResourceState::ShaderResource;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;
        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (m_RenderPass && m_PointSampler && m_SkyRadianceSampler && m_SkyTransmittanceSampler &&
            m_DescriptorSetLayout)
        {
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
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
            pipelineDesc.renderPass = m_RenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(CreateAtmosphereProbeDescriptorSetDesc());
            m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        }
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("AtmosphereProbePass", "大気の書き出しのパイプラインの作成に失敗。このパスは何もしません");
        }
        return true;
    }

    void AtmosphereProbePass::Shutdown()
    {
        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_FramebufferTarget = nullptr;
        m_RenderPass.reset();
        m_DescriptorSetLayout.reset();
        m_SkyTransmittanceSampler.reset();
        m_SkyRadianceSampler.reset();
        m_PointSampler.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_TargetHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_SkyAtmosphereTransmittanceHandle = {};
        m_CascadedShadowMapHandle = {};
        m_LastInfo = FrameCaptureAtmosphereInfo{};
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void AtmosphereProbePass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void AtmosphereProbePass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void AtmosphereProbePass::BuildRays(const SkyAtmosphereParameters& sky)
    {
        uint32_t rayCount = 0;
        for (const float azimuthOffset : kAzimuthOffsetsDegrees)
        {
            const double azimuth = static_cast<double>(sky.SunAzimuthDegrees) + azimuthOffset;
            for (const float elevation : kElevationsDegrees)
            {
                for (const float distance : kDistancesMeters)
                {
                    FrameCaptureAtmosphereRay& ray = m_LastInfo.Rays[rayCount++];
                    ray.Kind = FrameCaptureAtmosphereRayKind::Direction;
                    ray.AzimuthOffsetDegrees = azimuthOffset;
                    ray.ElevationDegrees = elevation;
                    ray.NominalDistanceMeters = distance;
                    MakeElevationAzimuthDirection(elevation, azimuth, ray.Direction);
                    ray.DistanceMeters = distance;
                }
            }
        }
        for (const float azimuthOffset : kAzimuthOffsetsDegrees)
        {
            const double azimuth = static_cast<double>(sky.SunAzimuthDegrees) + azimuthOffset;
            for (const float distance : kDistancesMeters)
            {
                // カメラから GroundEyeHeightMeters 下の水平面の、水平距離 distance の点へ
                const double elevationDegrees =
                    -std::atan2(static_cast<double>(GroundEyeHeightMeters), static_cast<double>(distance)) *
                    (180.0 / 3.14159265358979323846);
                FrameCaptureAtmosphereRay& ray = m_LastInfo.Rays[rayCount++];
                ray.Kind = FrameCaptureAtmosphereRayKind::Ground;
                ray.AzimuthOffsetDegrees = azimuthOffset;
                ray.ElevationDegrees = static_cast<float>(elevationDegrees);
                ray.NominalDistanceMeters = distance;
                MakeElevationAzimuthDirection(elevationDegrees, azimuth, ray.Direction);
                ray.DistanceMeters = static_cast<float>(std::sqrt(
                    static_cast<double>(distance) * distance +
                    static_cast<double>(GroundEyeHeightMeters) * GroundEyeHeightMeters));
            }
        }
        m_LastInfo.RayCount = rayCount;
    }

    void AtmosphereProbePass::Declare(RenderGraphBuilder& builder)
    {
        m_TargetHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_SkyAtmosphereTransmittanceHandle = {};
        m_CascadedShadowMapHandle = {};
        m_LastInfo.bValid = false;

        // 初期化を済ませたのにパイプラインが無いときは何も宣言しない
        if (m_bInitialized && !m_Pipeline)
        {
            return;
        }

        // VolumetricsPass::Declare と同じ値（同じスナップショット）
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
        m_AerialPerspective.bEnabled = m_AerialPerspective.bEnabled && m_SkyParameters.bEnabled;
        // VolumetricsPass が何も描かない構成では書き出さない
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

        BuildRays(m_SkyParameters);
        RGTextureDesc desc = RGTextureDesc::RenderTarget(m_LastInfo.RayCount,
                                                         FrameCaptureAtmosphereProbeRows,
                                                         AtmosphereProbeFormat,
                                                         "Capture_AtmosphereProbe");
        desc.Usage = desc.Usage | RHI::ResourceUsage::TransferSrc;
        const RGTextureHandle targetHandle =
            builder.WriteTextureAttachment(RenderGraphResourceNames::CaptureAtmosphereProbe,
                                           desc,
                                           RGAttachmentKind::Color,
                                           RHI::AttachmentLoadOp::DontCare,
                                           RHI::AttachmentStoreOp::Store,
                                           RHI::ResourceState::RenderTarget,
                                           RHI::ResourceState::ShaderResource);
        if (!targetHandle.IsValid())
        {
            m_SkyAtmosphereRadianceHandle = {};
            m_SkyAtmosphereTransmittanceHandle = {};
            m_CascadedShadowMapHandle = {};
            return;
        }
        // 撮影の読み出しはグラフの実行の後なので、外へ出して寿命を延ばす
        builder.ExportTexture(RenderGraphResourceNames::CaptureAtmosphereProbe, targetHandle);
        m_TargetHandle = targetHandle.ToResourceHandle();
        m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void AtmosphereProbePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_LastInfo.bValid = false;
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        const CameraProxy* camera = context.GetActiveCamera();
        if (!context.CommandList || !context.FrameResources || !m_Device || !m_Pipeline || !camera ||
            !m_TargetHandle.IsValid() || !m_SceneDepthHandle.IsValid() || m_LastInfo.RayCount == 0u)
        {
            return;
        }

        const RHI::TexturePtr target = resources.GetTexture(m_TargetHandle);
        const RHI::TexturePtr sceneDepthTexture = resources.GetTexture(m_SceneDepthHandle);
        const RHI::TexturePtr skyRadianceTexture = m_SkyAtmosphereRadianceHandle.IsValid()
                                                       ? resources.GetTexture(m_SkyAtmosphereRadianceHandle)
                                                       : RHI::TexturePtr{};
        const RHI::TexturePtr skyTransmittanceTexture = m_SkyAtmosphereTransmittanceHandle.IsValid()
                                                            ? resources.GetTexture(m_SkyAtmosphereTransmittanceHandle)
                                                            : RHI::TexturePtr{};
        const RHI::TexturePtr cascadedShadowMapTexture = m_CascadedShadowMapHandle.IsValid()
                                                             ? resources.GetTexture(m_CascadedShadowMapHandle)
                                                             : RHI::TexturePtr{};
        if (!target || !sceneDepthTexture)
        {
            return;
        }

        // 影の引き方は VolumetricsPass::Execute と同じ
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
        if (!(bActualShadowMapAvailable || IsValidShadowMapArrayFallbackTexture(shadowMapTextureForSampling)) ||
            !shadowMapSamplerForSampling)
        {
            return;
        }

        GPUVolumetricsParams params{};
        VolumetricsParamsState paramsState;
        BuildVolumetricsParams(context,
                               *camera,
                               m_FogParameters,
                               m_SkyParameters,
                               m_AerialPerspective,
                               skyRadianceTexture,
                               skyTransmittanceTexture,
                               bActualShadowMapAvailable,
                               params,
                               paramsState);

        GPUAtmosphereProbeRays rays{};
        rays.RayCount[0] = m_LastInfo.RayCount;
        for (uint32_t rayIndex = 0; rayIndex < m_LastInfo.RayCount; ++rayIndex)
        {
            const FrameCaptureAtmosphereRay& ray = m_LastInfo.Rays[rayIndex];
            rays.Rays[rayIndex][0] = ray.Direction[0];
            rays.Rays[rayIndex][1] = ray.Direction[1];
            rays.Rays[rayIndex][2] = ray.Direction[2];
            rays.Rays[rayIndex][3] = ray.DistanceMeters;
        }

        FrameResourceLane* frameResources = context.FrameResources;
        const FrameUploadAllocation paramsAllocation = frameResources->AllocateConstants(params);
        const FrameUploadAllocation raysAllocation = frameResources->AllocateConstants(rays);
        const RHI::DescriptorSetPtr descriptorSet = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout).Set;
        if (!paramsAllocation.IsValid() || !raysAllocation.IsValid() || !descriptorSet)
        {
            NORVES_LOG_WARNING("AtmosphereProbePass", "定数か記述子セットを借りられないため、このフレームは大気を書き出しません");
            return;
        }

        if (!m_Framebuffer || m_FramebufferTarget != target.get())
        {
            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = m_RenderPass;
            framebufferDesc.colorTargets.push_back(target);
            framebufferDesc.width = target->GetWidth();
            framebufferDesc.height = target->GetHeight();
            m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
            m_FramebufferTarget = m_Framebuffer ? target.get() : nullptr;
        }
        if (!m_Framebuffer)
        {
            return;
        }

        descriptorSet->BindConstantBuffer(2u, paramsAllocation.Buffer, paramsAllocation.GetOffset32(),
                                          sizeof(GPUVolumetricsParams));
        descriptorSet->BindConstantBuffer(5u, raysAllocation.Buffer, raysAllocation.GetOffset32(),
                                          sizeof(GPUAtmosphereProbeRays));
        descriptorSet->BindTexture(0u, sceneDepthTexture);
        descriptorSet->BindSampler(0u, m_PointSampler);
        descriptorSet->BindTexture(1u, skyRadianceTexture ? skyRadianceTexture : sceneDepthTexture);
        descriptorSet->BindSampler(1u, skyRadianceTexture ? m_SkyRadianceSampler : m_PointSampler);
        descriptorSet->BindTexture(3u, shadowMapTextureForSampling);
        descriptorSet->BindSampler(3u, shadowMapSamplerForSampling);
        descriptorSet->BindTexture(4u, paramsState.bAerialAvailable ? skyTransmittanceTexture : sceneDepthTexture);
        descriptorSet->BindSampler(4u, paramsState.bAerialAvailable ? m_SkyTransmittanceSampler : m_PointSampler);
        descriptorSet->Update();

        RHI::Viewport viewport;
        viewport.width = static_cast<float>(target->GetWidth());
        viewport.height = static_cast<float>(target->GetHeight());
        RHI::ScissorRect scissor;
        scissor.right = static_cast<int32_t>(target->GetWidth());
        scissor.bottom = static_cast<int32_t>(target->GetHeight());
        RHI::ICommandList* commandList = context.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);
        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0);
        commandList->Draw(3, 0);
        commandList->EndRenderPass();

        // 評価に使った値（書き出しの JSON に残す）
        m_LastInfo.CameraPosition[0] = params.CameraPositionAndPreExposure[0];
        m_LastInfo.CameraPosition[1] = params.CameraPositionAndPreExposure[1];
        m_LastInfo.CameraPosition[2] = params.CameraPositionAndPreExposure[2];
        m_LastInfo.PreExposure = params.CameraPositionAndPreExposure[3];
        m_LastInfo.GroundEyeHeightMeters = GroundEyeHeightMeters;
        m_LastInfo.Sky = m_SkyParameters;
        m_LastInfo.Aerial = m_AerialPerspective;
        m_LastInfo.Fog = m_FogParameters;
        m_LastInfo.FallbackFogColor[0] = params.FallbackFogColor[0];
        m_LastInfo.FallbackFogColor[1] = params.FallbackFogColor[1];
        m_LastInfo.FallbackFogColor[2] = params.FallbackFogColor[2];
        m_LastInfo.bAerialAvailable = paramsState.bAerialAvailable;
        m_LastInfo.bSkyRadianceAvailable = params.FogParameters[3] > 0.5f;
        m_LastInfo.bDirectionalAvailable = paramsState.bDirectionalAvailable;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            m_LastInfo.DirectionalDirection[axis] = params.DirectionalLightDirectionAndAnisotropy[axis];
            m_LastInfo.DirectionalRadiance[axis] = params.DirectionalLightRadianceAndEnabled[axis];
            m_LastInfo.RayleighExtinction[axis] = params.RayleighExtinctionAndScaleHeight[axis];
        }
        m_LastInfo.DirectionalAnisotropy = params.DirectionalLightDirectionAndAnisotropy[3];
        m_LastInfo.MieExtinction = params.MieExtinctionAndScaleHeight[0];
        m_LastInfo.TransmittanceLutObserverV = params.AerialPerspective[3];
        m_LastInfo.bValid = true;
    }
#endif
} // namespace NorvesLib::Core::Rendering
