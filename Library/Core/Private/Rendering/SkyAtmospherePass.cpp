#include "Rendering/SkyAtmospherePass.h"

#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kFp16Max = 65504.0f;

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
        m_LastSunDiskPreExposedLuminance = 0.0f;
        m_bLastSunDiskSaturated = false;
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
        return outTexture != nullptr;
    }

    bool SkyAtmospherePass::EnsureResources()
    {
        return CreateTexture(m_TransmittanceTexture,
                             m_Settings.TransmittanceWidth,
                             m_Settings.TransmittanceHeight,
                             "SkyAtmosphere.Transmittance") &&
               CreateTexture(m_RadianceTexture,
                             m_Settings.RadianceWidth,
                             m_Settings.RadianceHeight,
                             "SkyAtmosphere.Radiance") &&
               CreateTexture(m_SunDiskTexture, 1u, 1u, "SkyAtmosphere.SunDisk");
    }

    bool SkyAtmospherePass::GenerateTransmittanceLut(
        const SkyAtmosphereParameters& parameters)
    {
        if (!m_TransmittanceTexture)
        {
            return false;
        }

        const uint32_t width = m_Settings.TransmittanceWidth;
        const uint32_t height = m_Settings.TransmittanceHeight;
        Container::VariableArray<uint16_t> data;
        data.resize(static_cast<size_t>(width) * height * 4u);

        for (uint32_t y = 0u; y < height; ++y)
        {
            const float altitude = (static_cast<float>(y) + 0.5f) /
                                   static_cast<float>(height);
            for (uint32_t x = 0u; x < width; ++x)
            {
                const float cosine = (static_cast<float>(x) + 0.5f) /
                                     static_cast<float>(width);
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                // 地表の太陽の照度（ComputeSunGroundIlluminance）と同じ関数で求める。
                const Math::Vector3 transmittance =
                    ComputeAtmosphereTransmittance(parameters, altitude, cosine);
                data[offset + 0u] = FloatToHalfRne(transmittance.x);
                data[offset + 1u] = FloatToHalfRne(transmittance.y);
                data[offset + 2u] = FloatToHalfRne(transmittance.z);
                data[offset + 3u] = FloatToHalfRne(1.0f);
            }
        }

        const uint32_t rowPitch = width * 4u * sizeof(uint16_t);
        m_TransmittanceTexture->Update(data.data(), rowPitch, rowPitch * height);
        return true;
    }

    bool SkyAtmospherePass::GenerateRadianceLut(
        const SkyAtmosphereParameters& parameters)
    {
        if (!m_RadianceTexture)
        {
            return false;
        }

        const uint32_t width = m_Settings.RadianceWidth;
        const uint32_t height = m_Settings.RadianceHeight;
        const Math::Vector3 sunDirection =
            MakeSunDirectionFromAltitudeAzimuth(parameters.SunAltitudeDegrees,
                                                parameters.SunAzimuthDegrees);
        const float solarDiskRadius =
            std::sqrt(SolarDiskSolidAngleSteradians / kPi);
        Container::VariableArray<uint16_t> data;
        data.resize(static_cast<size_t>(width) * height * 4u);

        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const Math::Vector3 direction =
                    DirectionFromEquirectangular(x, y, width, height);
                const SkyRadianceSample sample =
                    EvaluateHillaireSkyReference(parameters, direction);
                const float cosineToSun = std::clamp(Dot(direction, sunDirection), -1.0f, 1.0f);
                const bool bInSunDisk = std::acos(cosineToSun) <= solarDiskRadius;
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                data[offset + 0u] = FloatToHalfRne(
                    sample.bValid && IsFiniteRadiance(sample.Radiance) ? sample.Radiance.x : 0.0f);
                data[offset + 1u] = FloatToHalfRne(
                    sample.bValid && IsFiniteRadiance(sample.Radiance) ? sample.Radiance.y : 0.0f);
                data[offset + 2u] = FloatToHalfRne(
                    sample.bValid && IsFiniteRadiance(sample.Radiance) ? sample.Radiance.z : 0.0f);
                data[offset + 3u] = FloatToHalfRne(bInSunDisk ? 1.0f : 0.0f);
            }
        }

        const uint32_t rowPitch = width * 4u * sizeof(uint16_t);
        m_RadianceTexture->Update(data.data(), rowPitch, rowPitch * height);
        return true;
    }

    bool SkyAtmospherePass::GenerateSunDiskTexture(
        const SkyAtmosphereParameters& parameters,
        float preExposure)
    {
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
        const uint16_t pixel[4] = {
            FloatToHalfRne(safeValue),
            FloatToHalfRne(safeValue),
            FloatToHalfRne(safeValue),
            FloatToHalfRne(1.0f)};
        m_SunDiskTexture->Update(pixel, sizeof(pixel), sizeof(pixel));
        m_LastSunDiskPreExposedLuminance = safeValue;
        m_bLastSunDiskSaturated = bSaturated;
        return std::isfinite(safeValue) && safeValue <= safetyLimit;
    }

    bool SkyAtmospherePass::GenerateLutResources(
        const SkyAtmosphereParameters& parameters,
        float preExposure)
    {
        return GenerateTransmittanceLut(parameters) &&
               GenerateRadianceLut(parameters) &&
               GenerateSunDiskTexture(parameters, preExposure);
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
        m_bPrepared = EnsureResources() && GenerateLutResources(m_LastParameters, preExposure);
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
        builder.Read(m_SunDiskHandle.ToResourceHandle(),
                     RHI::ResourceState::ShaderResource);
        builder.PreserveInsertionOrder();
    }

    void SkyAtmospherePass::Execute(RenderGraphResources& resources,
                                    ViewRenderContext& context)
    {
        if (!m_bPrepared || !m_TransmittanceHandle.IsValid() ||
            !m_RadianceHandle.IsValid() || !m_SunDiskHandle.IsValid())
        {
            PublishContextResources(context, false);
            return;
        }

        const bool bResourcesResolved = resources.GetTexture(m_TransmittanceHandle) != nullptr &&
                                        resources.GetTexture(m_RadianceHandle) != nullptr &&
                                        resources.GetTexture(m_SunDiskHandle) != nullptr;
        PublishContextResources(context, bResourcesResolved);
    }

} // namespace NorvesLib::Core::Rendering
