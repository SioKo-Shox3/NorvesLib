#include "Rendering/SkyAtmospherePass.h"

#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kFp16Max = 65504.0f;
        // 太陽の円盤の 1×1 の R16G16B16A16_FLOAT の 1 画素の大きさ
        constexpr uint64_t kSunDiskPixelBytes = 4u * sizeof(uint16_t);

        uint16_t FloatToHalfRne(float value)
        {
            if (!std::isfinite(value))
            {
                return 0u;
            }

            value = std::clamp(value, 0.0f, kFp16Max * 0.9f);
            union
            {
                float f;
                uint32_t u;
            } bits;
            bits.f = value;

            const uint16_t sign = static_cast<uint16_t>((bits.u >> 16u) & 0x8000u);
            const uint32_t exponentBits = (bits.u >> 23u) & 0xFFu;
            const uint32_t mantissa = bits.u & 0x007FFFFFu;
            if (exponentBits == 0xFFu)
            {
                return sign | 0x7BFFu;
            }

            const int32_t exponent = static_cast<int32_t>(exponentBits) - 127;
            if (exponent > 15)
            {
                return sign | 0x7BFFu;
            }
            if (exponent >= -14)
            {
                const uint32_t truncated = mantissa >> 13u;
                const uint32_t remainder = mantissa & 0x1FFFu;
                const bool bRoundUp = remainder > 0x1000u ||
                                      (remainder == 0x1000u && (truncated & 1u) != 0u);
                uint32_t roundedMantissa = truncated + (bRoundUp ? 1u : 0u);
                int32_t roundedExponent = exponent;
                if (roundedMantissa >= 0x400u)
                {
                    roundedMantissa = 0u;
                    ++roundedExponent;
                }
                if (roundedExponent > 15)
                {
                    return sign | 0x7BFFu;
                }
                return sign |
                       static_cast<uint16_t>((roundedExponent + 15) << 10u) |
                       static_cast<uint16_t>(roundedMantissa);
            }
            if (exponent >= -25)
            {
                const uint32_t normalizedMantissa = mantissa | 0x00800000u;
                const uint32_t shift = static_cast<uint32_t>(-exponent - 1);
                const uint32_t truncated = normalizedMantissa >> shift;
                const uint32_t remainderMask = (1u << shift) - 1u;
                const uint32_t remainder = normalizedMantissa & remainderMask;
                const uint32_t halfway = 1u << (shift - 1u);
                const bool bRoundUp = remainder > halfway ||
                                      (remainder == halfway && (truncated & 1u) != 0u);
                return sign | static_cast<uint16_t>(truncated + (bRoundUp ? 1u : 0u));
            }
            return sign;
        }

        float SafePreExposure(const ViewRenderContext& context)
        {
            const CameraProxy* camera = context.GetActiveCamera();
            if (!camera || !std::isfinite(camera->PreExposure) || camera->PreExposure <= 0.0f)
            {
                return 1.0f;
            }
            return std::clamp(camera->PreExposure, 1.0e-6f, 1.0e6f);
        }

        Math::Vector3 DirectionFromEquirectangular(uint32_t x,
                                                   uint32_t y,
                                                   uint32_t width,
                                                   uint32_t height)
        {
            const float u = (static_cast<float>(x) + 0.5f) /
                            static_cast<float>(std::max(width, 1u));
            const float v = (static_cast<float>(y) + 0.5f) /
                            static_cast<float>(std::max(height, 1u));
            const float longitude = (u - 0.5f) * 2.0f * kPi;
            const float latitude = (v - 0.5f) * kPi;
            const float horizontal = std::cos(latitude);
            return Math::Vector3(horizontal * std::cos(longitude),
                                 -std::sin(latitude),
                                 horizontal * std::sin(longitude));
        }

        float Dot(const Math::Vector3& lhs, const Math::Vector3& rhs)
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        bool IsFiniteRadiance(const Math::Vector3& radiance)
        {
            return std::isfinite(radiance.x) &&
                   std::isfinite(radiance.y) &&
                   std::isfinite(radiance.z);
        }
    } // namespace

    SkyAtmospherePass::SkyAtmospherePass(const SkyAtmospherePassSettings& settings)
        : m_Settings(settings)
    {
    }

    SkyAtmospherePass::~SkyAtmospherePass()
    {
        Shutdown();
    }

    bool SkyAtmospherePass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("SkyAtmospherePass", "Device and ShaderManager are required");
            return false;
        }

        if (m_Settings.LutFormat != RHI::Format::R16G16B16A16_FLOAT ||
            m_Settings.TransmittanceWidth == 0u ||
            m_Settings.TransmittanceHeight == 0u ||
            m_Settings.RadianceWidth == 0u ||
            m_Settings.RadianceHeight == 0u ||
            !std::isfinite(m_Settings.SunDiskSafetyFraction) ||
            m_Settings.SunDiskSafetyFraction <= 0.0f ||
            m_Settings.SunDiskSafetyFraction > 1.0f)
        {
            NORVES_LOG_ERROR("SkyAtmospherePass", "Invalid LUT or sun-disk settings");
            return false;
        }

        m_Device = context.Device;
        m_FragmentShader = context.ShaderMgr->LoadShader("sky_atmosphere.frag",
                                                         RHI::ShaderStage::Pixel);
        if (!m_FragmentShader)
        {
            NORVES_LOG_ERROR("SkyAtmospherePass", "Failed to load sky atmosphere shader");
            return false;
        }

        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Linear;
        samplerDesc.filterMag = RHI::FilterMode::Linear;
        samplerDesc.filterMip = RHI::FilterMode::Linear;
        samplerDesc.addressU = RHI::TextureAddressMode::Wrap;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = m_Device->CreateSampler(samplerDesc);
        if (!m_Sampler || !EnsureResources())
        {
            NORVES_LOG_ERROR("SkyAtmospherePass", "Failed to create sky atmosphere resources");
            Shutdown();
            return false;
        }

        m_bInitialized = true;
        return true;
    }

    void SkyAtmospherePass::Shutdown()
    {
        m_TransmittanceTexture.reset();
        m_RadianceTexture.reset();
        m_SunDiskTexture.reset();
        m_Sampler.reset();
        m_FragmentShader.reset();
        m_TransmittanceHandle = {};
        m_RadianceHandle = {};
        m_SunDiskHandle = {};
        m_Device = nullptr;
        m_LastParameters = SkyAtmosphereParameters{};
        m_LutParameters = SkyAtmosphereParameters{};
        m_LutSunGroundTransmittance = Math::Vector3::Zero;
        m_AerialProfile = SkyAerialProfile{};
        m_bLutValid = false;
        m_LastSunDiskPreExposedLuminance = 0.0f;
        m_bLastSunDiskSaturated = false;
        m_SunDiskStagingBuffer.reset();
        m_SunDiskUploadBuffer.reset();
        m_SunDiskUploadOffset = 0;
        m_bSunDiskCopyPending = false;
        m_bSunDiskTextureInitialized = false;
        m_bSnapshotEnabled = false;
        m_bPrepared = false;
        m_bInitialized = false;
    }

    void SkyAtmospherePass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void SkyAtmospherePass::Execute(ViewRenderContext& context)
    {
        PublishContextResources(context, m_bPrepared);
    }

    const SkyAtmosphereParameters& SkyAtmospherePass::ResolveSnapshot(
        const ViewRenderContext& context) const
    {
        if (context.SnapshotScene)
        {
            return context.SnapshotScene->SkyAtmosphere;
        }
        return context.SkyAtmosphereSnapshot;
    }

    float SkyAtmospherePass::ResolvePreExposure(const ViewRenderContext& context) const
    {
        return SafePreExposure(context);
    }

    bool SkyAtmospherePass::CreateTexture(RHI::TexturePtr& outTexture,
                                          uint32_t width,
                                          uint32_t height,
                                          const char* debugName)
    {
        if (!m_Device || width == 0u || height == 0u)
        {
            return false;
        }

        if (outTexture &&
            outTexture->GetWidth() == width &&
            outTexture->GetHeight() == height &&
            outTexture->GetFormat() == m_Settings.LutFormat)
        {
            return true;
        }

        RHI::TextureDesc textureDesc;
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.MipLevels = 1u;
        textureDesc.ArraySize = 1u;
        textureDesc.TextureFormat = m_Settings.LutFormat;
        textureDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        textureDesc.DebugName = debugName;
        outTexture = m_Device->CreateTexture(textureDesc);
        // 作り直したテクスチャは中身が無いので、次の準備でLUTを作る。
        m_bLutValid = false;
        return outTexture != nullptr;
    }

    bool SkyAtmospherePass::EnsureResources()
    {
        const RHI::ITexture* previousSunDisk = m_SunDiskTexture.get();
        const bool bCreated = CreateTexture(m_TransmittanceTexture,
                                            m_Settings.TransmittanceWidth,
                                            m_Settings.TransmittanceHeight,
                                            "SkyAtmosphere.Transmittance") &&
                              CreateTexture(m_RadianceTexture,
                                            m_Settings.RadianceWidth,
                                            m_Settings.RadianceHeight,
                                            "SkyAtmosphere.Radiance") &&
                              CreateTexture(m_SunDiskTexture, 1u, 1u, "SkyAtmosphere.SunDisk");
        if (m_SunDiskTexture.get() != previousSunDisk)
        {
            m_bSunDiskTextureInitialized = false;
        }
        if (bCreated && !m_SunDiskStagingBuffer)
        {
            m_SunDiskStagingBuffer = m_Device->CreateBuffer(RHI::BufferDesc(
                kSunDiskPixelBytes,
                RHI::ResourceUsage::TransferSrc | RHI::ResourceUsage::TransferDst,
                false,
                "SkyAtmosphere.SunDiskStaging"));
        }
        return bCreated && m_SunDiskStagingBuffer != nullptr;
    }

    void BuildSkyTransmittanceLutTexels(const SkyAtmosphereModel& model,
                                        uint32_t width,
                                        uint32_t height,
                                        Container::VariableArray<uint16_t>& outTexels)
    {
        outTexels.clear();
        outTexels.resize(static_cast<size_t>(width) * height * 4u);

        for (uint32_t y = 0u; y < height; ++y)
        {
            // 最下段を地表（高度0）、最上段を大気の上端にする。背景とPTはv=0（地表）を引き、
            // 空の太陽の地表照度と空由来IBLの透過率（高度0）と揃う。
            const float altitude = height > 1u
                ? static_cast<float>(y) / static_cast<float>(height - 1u)
                : 0.0f;
            for (uint32_t x = 0u; x < width; ++x)
            {
                const float cosine = (static_cast<float>(x) + 0.5f) /
                                     static_cast<float>(width);
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                // 地表の太陽の照度（ComputeSunGroundIlluminance）と同じ密度の積分の表から引く。
                const Math::Vector3 transmittance = model.GetTransmittance(altitude, cosine);
                outTexels[offset + 0u] = FloatToHalfRne(transmittance.x);
                outTexels[offset + 1u] = FloatToHalfRne(transmittance.y);
                outTexels[offset + 2u] = FloatToHalfRne(transmittance.z);
                outTexels[offset + 3u] = FloatToHalfRne(1.0f);
            }
        }
    }

    void BuildSkyRadianceLutTexels(const SkyAtmosphereModel& model,
                                   uint32_t width,
                                   uint32_t height,
                                   Container::VariableArray<uint16_t>& outTexels)
    {
        const SkyAtmosphereParameters& parameters = model.GetParameters();
        const Math::Vector3 sunDirection =
            MakeSunDirectionFromAltitudeAzimuth(parameters.SunAltitudeDegrees,
                                                parameters.SunAzimuthDegrees);
        const float solarDiskRadius =
            std::sqrt(SolarDiskSolidAngleSteradians / kPi);
        outTexels.clear();
        outTexels.resize(static_cast<size_t>(width) * height * 4u);

        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const Math::Vector3 direction =
                    DirectionFromEquirectangular(x, y, width, height);
                // 観測点から見た空（視線の透過率と地平線より下の地面を含む）。
                const SkyRadianceSample sample = model.SampleSkyView(direction);
                const float cosineToSun = std::clamp(Dot(direction, sunDirection), -1.0f, 1.0f);
                const bool bInSunDisk = std::acos(cosineToSun) <= solarDiskRadius;
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                outTexels[offset + 0u] = FloatToHalfRne(
                    sample.bValid && IsFiniteRadiance(sample.Radiance) ? sample.Radiance.x : 0.0f);
                outTexels[offset + 1u] = FloatToHalfRne(
                    sample.bValid && IsFiniteRadiance(sample.Radiance) ? sample.Radiance.y : 0.0f);
                outTexels[offset + 2u] = FloatToHalfRne(
                    sample.bValid && IsFiniteRadiance(sample.Radiance) ? sample.Radiance.z : 0.0f);
                outTexels[offset + 3u] = FloatToHalfRne(bInSunDisk ? 1.0f : 0.0f);
            }
        }
    }

    void BuildSkyAerialProfile(const SkyAtmosphereModel& model, SkyAerialProfile& outProfile)
    {
        outProfile = SkyAerialProfile{};
        const SkyAtmosphereParameters& parameters = model.GetParameters();
        if (!parameters.bEnabled)
        {
            return;
        }
        const Math::Vector3 sunDirection =
            MakeSunDirectionFromAltitudeAzimuth(parameters.SunAltitudeDegrees, parameters.SunAzimuthDegrees);
        const double sunIrradiance = model.GetSunDiskIrradiance();
        outProfile.SunCosine = sunDirection.y;
        for (uint32_t index = 0u; index < SkyAerialProfileSize; ++index)
        {
            const double altitude = static_cast<double>(SkyAerialProfileMaxAltitudeMeters) * index /
                                    static_cast<double>(SkyAerialProfileSize - 1u);
            Math::Vector3 transmittance;
            Math::Vector3 multipleScattering;
            model.LookupSunTransmittanceAndMultipleScattering(altitude, sunDirection.y, transmittance, multipleScattering);
            const float sun[3] = {transmittance.x, transmittance.y, transmittance.z};
            const float psi[3] = {multipleScattering.x, multipleScattering.y, multipleScattering.z};
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                outProfile.SunIlluminance[index][channel] = static_cast<float>(sunIrradiance * sun[channel]);
                outProfile.MultipleScattering[index][channel] = static_cast<float>(sunIrradiance * psi[channel]);
            }
            outProfile.SunIlluminance[index][3] = 1.0f;
            outProfile.MultipleScattering[index][3] = 1.0f;
        }
        outProfile.bValid = std::isfinite(sunIrradiance) && sunIrradiance > 0.0;
    }

    bool SkyAtmospherePass::GenerateTransmittanceLut(const SkyAtmosphereModel& model)
    {
        if (!m_TransmittanceTexture)
        {
            return false;
        }

        const uint32_t width = m_Settings.TransmittanceWidth;
        const uint32_t height = m_Settings.TransmittanceHeight;
        Container::VariableArray<uint16_t> data;
        BuildSkyTransmittanceLutTexels(model, width, height, data);

        const uint32_t rowPitch = width * 4u * sizeof(uint16_t);
        m_TransmittanceTexture->Update(data.data(), rowPitch, rowPitch * height);
        return true;
    }

    bool SkyAtmospherePass::GenerateRadianceLut(const SkyAtmosphereModel& model)
    {
        if (!m_RadianceTexture)
        {
            return false;
        }

        const uint32_t width = m_Settings.RadianceWidth;
        const uint32_t height = m_Settings.RadianceHeight;
        Container::VariableArray<uint16_t> data;
        BuildSkyRadianceLutTexels(model, width, height, data);

        const uint32_t rowPitch = width * 4u * sizeof(uint16_t);
        m_RadianceTexture->Update(data.data(), rowPitch, rowPitch * height);
        return true;
    }

    bool SkyAtmospherePass::GenerateSunDiskTexture(
        const SkyAtmosphereParameters& parameters,
        float preExposure,
        FrameResourceLane* frameResources)
    {
        m_SunDiskUploadBuffer.reset();
        m_SunDiskUploadOffset = 0;
        m_bSunDiskCopyPending = false;
        if (!m_SunDiskTexture)
        {
            return false;
        }

        const float requested = ComputeSunDiskPreExposedLuminance(parameters, preExposure);
        const float safetyFraction = std::clamp(m_Settings.SunDiskSafetyFraction,
                                                0.0f,
                                                1.0f);
        const float safetyLimit = kFp16Max * safetyFraction;
        const bool bSaturated = !std::isfinite(requested) || requested > safetyLimit;
        const float safeValue = std::isfinite(requested)
                                    ? std::clamp(requested, 0.0f, safetyLimit)
                                    : requested > 0.0f ? safetyLimit : 0.0f;
        // 表示する円盤は、空の太陽の方向光と同じ地表の透過率で減光・着色する。
        const Math::Vector3& transmittance = m_LutSunGroundTransmittance;
        const uint16_t pixel[4] = {
            FloatToHalfRne(safeValue * std::clamp(transmittance.x, 0.0f, 1.0f)),
            FloatToHalfRne(safeValue * std::clamp(transmittance.y, 0.0f, 1.0f)),
            FloatToHalfRne(safeValue * std::clamp(transmittance.z, 0.0f, 1.0f)),
            FloatToHalfRne(1.0f)};
        if (m_bSunDiskTextureInitialized && frameResources != nullptr)
        {
            // フレームの持ち主から借りた領域に書き、Execute でこのフレームの記録の中へコピーを積む。
            // 前のフレームの読みとの順序は、グラフが宣言どおりに入れるバリアが守る（同期の提出はしない）。
            const FrameUploadAllocation allocation =
                frameResources->AllocateUploadData(pixel, sizeof(pixel), FrameUploadUsage::Storage);
            if (!allocation.IsValid())
            {
                NORVES_LOG_WARNING("SkyAtmospherePass",
                                   "Failed to borrow upload memory for the sun disk texture");
                return false;
            }
            m_SunDiskUploadBuffer = allocation.Buffer;
            m_SunDiskUploadOffset = allocation.Offset;
            m_bSunDiskCopyPending = true;
        }
        else
        {
            // 作った直後（layout が未定義）と、フレームの持ち主の無い文脈（検査）だけ同期で書く。
            m_SunDiskTexture->Update(pixel, sizeof(pixel), sizeof(pixel));
            m_bSunDiskTextureInitialized = true;
        }
        m_LastSunDiskPreExposedLuminance = safeValue;
        m_bLastSunDiskSaturated = bSaturated;
        return std::isfinite(safeValue) && safeValue <= safetyLimit;
    }

    bool SkyAtmospherePass::GenerateLutResources(
        const SkyAtmosphereParameters& parameters,
        float preExposure,
        FrameResourceLane* frameResources)
    {
        if (!m_bLutValid || !AreSkyAtmosphereParametersEqual(m_LutParameters, parameters))
        {
            // LUTは空のパラメータ（太陽の向きを含む）が変わったときだけ作る。
            m_bLutValid = false;
            const auto startTime = std::chrono::steady_clock::now();
            Container::TUniquePtr<SkyAtmosphereModel> created =
                Container::MakeUnique<SkyAtmosphereModel>(parameters, m_LutModel.get());
            m_LutModel = std::move(created);
            SkyAtmosphereModel& model = *m_LutModel;
            model.BuildSkyViewTable();
            if (!GenerateTransmittanceLut(model) || !GenerateRadianceLut(model))
            {
                return false;
            }
            const double elapsedMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - startTime).count();
            NORVES_LOG_INFO("SkyAtmospherePass",
                            "Sky LUT generated in %.1f ms (radiance %ux%u, transmittance %ux%u, "
                            "sun altitude %.2f, azimuth %.2f, reused_tables=%d)",
                            elapsedMilliseconds,
                            m_Settings.RadianceWidth,
                            m_Settings.RadianceHeight,
                            m_Settings.TransmittanceWidth,
                            m_Settings.TransmittanceHeight,
                            parameters.SunAltitudeDegrees,
                            parameters.SunAzimuthDegrees,
                            model.ReusedSunIndependentTables() ? 1 : 0);
            m_LutParameters = parameters;
            m_LutSunGroundTransmittance = model.GetSunGroundTransmittance();
            BuildSkyAerialProfile(model, m_AerialProfile);
            m_bLutValid = true;
        }
        return GenerateSunDiskTexture(parameters, preExposure, frameResources);
    }

    void SkyAtmospherePass::PublishContextResources(ViewRenderContext& context, bool bValid)
    {
        if (!m_bSnapshotEnabled)
        {
            context.SkyAtmosphere.Reset();
            return;
        }

        const float preExposure = ResolvePreExposure(context);
        context.SkyAtmosphere.Publish(m_LastParameters,
                                      bValid ? m_TransmittanceTexture : RHI::TexturePtr{},
                                      bValid ? m_RadianceTexture : RHI::TexturePtr{},
                                      bValid ? m_SunDiskTexture : RHI::TexturePtr{},
                                      bValid ? m_Sampler : RHI::SamplerPtr{},
                                      preExposure,
                                      bValid ? m_LastSunDiskPreExposedLuminance : 0.0f,
                                      bValid && m_bLastSunDiskSaturated,
                                      bValid);
        context.SkyAtmosphere.AerialProfile = bValid ? m_AerialProfile : SkyAerialProfile{};
    }

    void SkyAtmospherePass::Declare(RenderGraphBuilder& builder)
    {
        m_TransmittanceHandle = {};
        m_RadianceHandle = {};
        m_SunDiskHandle = {};
        m_bSnapshotEnabled = false;
        m_bPrepared = false;

        const ViewRenderContext* context = builder.GetContext();
        if (!context)
        {
            return;
        }

        m_LastParameters = SanitizeSkyAtmosphereParameters(ResolveSnapshot(*context));
        m_bSnapshotEnabled = m_LastParameters.bEnabled;
        if (!m_bSnapshotEnabled)
        {
            return;
        }

        const float preExposure = ResolvePreExposure(*context);
        m_bPrepared = EnsureResources() &&
                      GenerateLutResources(m_LastParameters, preExposure, context->FrameResources);
        if (!m_bPrepared)
        {
            NORVES_LOG_WARNING("SkyAtmospherePass",
                               "Sky LUT generation failed; LightingPass will use black sky");
            return;
        }

        const RGResourceHandle transmittanceResource = builder.ImportTexture(
            m_TransmittanceTexture,
            RHI::ResourceState::ShaderResource,
            "SkyAtmosphere.Transmittance");
        const RGResourceHandle radianceResource = builder.ImportTexture(
            m_RadianceTexture,
            RHI::ResourceState::ShaderResource,
            "SkyAtmosphere.Radiance");
        const RGResourceHandle sunDiskResource = builder.ImportTexture(
            m_SunDiskTexture,
            RHI::ResourceState::ShaderResource,
            "SkyAtmosphere.SunDisk");
        if (!transmittanceResource.IsValid() || !radianceResource.IsValid() ||
            !sunDiskResource.IsValid())
        {
            m_bPrepared = false;
            m_bSunDiskCopyPending = false;
            return;
        }

        builder.PublishTexture(RenderGraphResourceNames::SkyAtmosphereTransmittance,
                               transmittanceResource);
        builder.PublishTexture(RenderGraphResourceNames::SkyAtmosphereRadiance,
                               radianceResource);
        builder.PublishTexture(RenderGraphResourceNames::SkyAtmosphereSunDisk,
                               sunDiskResource);
        builder.TryGetTexture(RenderGraphResourceNames::SkyAtmosphereTransmittance,
                              m_TransmittanceHandle);
        builder.TryGetTexture(RenderGraphResourceNames::SkyAtmosphereRadiance,
                              m_RadianceHandle);
        builder.TryGetTexture(RenderGraphResourceNames::SkyAtmosphereSunDisk,
                              m_SunDiskHandle);
        builder.Read(m_TransmittanceHandle.ToResourceHandle(),
                     RHI::ResourceState::ShaderResource);
        builder.Read(m_RadianceHandle.ToResourceHandle(),
                     RHI::ResourceState::ShaderResource);
        if (m_bSunDiskCopyPending)
        {
            // このパスがコピーで書き、パスの中で読み取りの状態へ戻す。後の読み手はグラフがこの後へ並べる。
            builder.Write(m_SunDiskHandle.ToResourceHandle(),
                          RHI::ResourceState::CopyDest,
                          RHI::ResourceState::ShaderResource);
        }
        else
        {
            builder.Read(m_SunDiskHandle.ToResourceHandle(),
                         RHI::ResourceState::ShaderResource);
        }
        builder.PreserveInsertionOrder();
    }

    void SkyAtmospherePass::Execute(RenderGraphResources& resources,
                                    ViewRenderContext& context)
    {
        const bool bCopyPending = m_bSunDiskCopyPending;
        m_bSunDiskCopyPending = false;
        if (!m_bPrepared || !m_TransmittanceHandle.IsValid() ||
            !m_RadianceHandle.IsValid() || !m_SunDiskHandle.IsValid())
        {
            m_SunDiskUploadBuffer.reset();
            PublishContextResources(context, false);
            return;
        }

        const RHI::TexturePtr sunDiskTexture = resources.GetTexture(m_SunDiskHandle);
        const bool bResourcesResolved = AreGraphResourcesResolved(resources);
        if (bCopyPending && sunDiskTexture && context.CommandList)
        {
            // グラフが入れたバリアで CopyDest にある。宣言した最後の状態（ShaderResource）へはパスの中で戻す。
            if (m_SunDiskUploadBuffer && m_SunDiskStagingBuffer)
            {
                // 中継のバッファは前のフレームの texture へのコピーが読み終えてから書く
                context.CommandList->BufferBarrier(m_SunDiskStagingBuffer,
                                                   RHI::ResourceState::CopySource,
                                                   RHI::ResourceState::CopyDest,
                                                   0u,
                                                   kSunDiskPixelBytes);
                context.CommandList->CopyBuffer(m_SunDiskUploadBuffer,
                                                m_SunDiskStagingBuffer,
                                                kSunDiskPixelBytes,
                                                m_SunDiskUploadOffset,
                                                0u);
                context.CommandList->BufferBarrier(m_SunDiskStagingBuffer,
                                                   RHI::ResourceState::CopyDest,
                                                   RHI::ResourceState::CopySource,
                                                   0u,
                                                   kSunDiskPixelBytes);
                context.CommandList->CopyBufferToTexture(m_SunDiskStagingBuffer,
                                                         sunDiskTexture,
                                                         1u,
                                                         1u,
                                                         0u);
            }
            context.CommandList->TextureBarrier(sunDiskTexture,
                                                RHI::ResourceState::CopyDest,
                                                RHI::ResourceState::ShaderResource);
        }
        m_SunDiskUploadBuffer.reset();
        // 並列の記録では写した context へ書くので後のパスに届かない（届けるのは準備段の公開）。1 本の記録ではここが公開する
        PublishContextResources(context, bResourcesResolved);
    }

    bool SkyAtmospherePass::AreGraphResourcesResolved(RenderGraphResources& resources) const
    {
        return m_bPrepared && m_TransmittanceHandle.IsValid() && m_RadianceHandle.IsValid() && m_SunDiskHandle.IsValid() &&
               resources.GetTexture(m_TransmittanceHandle) != nullptr &&
               resources.GetTexture(m_RadianceHandle) != nullptr &&
               resources.GetTexture(m_SunDiskHandle) != nullptr;
    }

    bool SkyAtmospherePass::DeclaresAllLayoutChanges() const
    {
        // グラフの Texture は太陽の円盤だけを変え、宣言（CopyDest で書き、最終の ShaderResource）のとおりに
        // 状態を明示したバリアで戻す。中継のバッファのバリアは layout を持たない
        return true;
    }

    RGRecordPolicy SkyAtmospherePass::GetRecordPolicy() const
    {
        // Execute が書くのは自分のメンバ（中継のバッファの取り置き）だけで、LightingPass などが読む公開は準備段で済ませる
        return RGRecordPolicy::Parallel;
    }

    void SkyAtmospherePass::PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context)
    {
        // 後のパス（LightingPass・VolumetricsPass）が読む空の公開を、写す前の context へ Execute と同じ値で書く
        PublishContextResources(context, AreGraphResourcesResolved(resources));
    }

} // namespace NorvesLib::Core::Rendering
