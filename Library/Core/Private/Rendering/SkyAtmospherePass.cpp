#include "Rendering/SkyAtmospherePass.h"

#include "Rendering/FrameResourceOwner.h"
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/IblGeneration.h"
#include "Rendering/SkyAtmosphereSourceCache.h"
#include "Rendering/SkyKeyframeWindow.h"
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

    struct SkyAtmospherePass::KeyframeState
    {
        SkyKeyframeWindow Window;
        SkyKeyframeBuilder Builder;
        Container::VariableArray<SkyKeyframeGpu> Keys;
        Container::VariableArray<uint64_t> UploadSerials;
        uint64_t NextUploadSerial = 0u;
        SkyAtmosphereParameters SetKey;
        bool bSetKeyValid = false;
        Container::TSharedPtr<const SkyAtmosphereModel> ReferenceModel;
        uint64_t SyncBuildCount = 0u;
        SkyKeyframeSelection Selection;
        Math::Vector3 ClearHorizontalIlluminance = Math::Vector3::Zero;
        SkyAerialProfile ClearAerialProfile;
        bool bFailureWarningLogged = false;
        bool bLateWarningLogged = false;
        SkyCloudBlendInputWeights LastBlendWeights;
        uint64_t LastBlendSerialA = 0u;
        uint64_t LastBlendSerialB = 0u;
        uint64_t SweepFrame = 0u;
        bool bSweepPreviousValid = false;
        float SweepPreviousAltitude = 0.0f;
        float SweepPreviousAzimuth = 0.0f;
        SkyKeyframeSelection SweepPreviousSelection;

        void RetireKey(uint32_t index, GpuRetireQueue& retireQueue)
        {
            auto& key = Keys[index];
            retireQueue.Retire(std::move(key.Radiance));
            retireQueue.Retire(std::move(key.Environment));
            retireQueue.Retire(std::move(key.Diffuse));
            retireQueue.Retire(std::move(key.Prefilter));
            key = {};
            UploadSerials[index] = 0u;
        }

        void RetireAllKeys(GpuRetireQueue& retireQueue)
        {
            for (size_t index = 0u; index < Keys.size(); ++index)
            {
                RetireKey(static_cast<uint32_t>(index), retireQueue);
            }
            Keys.clear();
            UploadSerials.clear();
            Selection = {};
        }

        void WarnFailure()
        {
            if (!bFailureWarningLogged)
            {
                NORVES_LOG_WARNING("SkyAtmospherePass", "空のキーフレームを生成または転送できませんでした");
                bFailureWarningLogged = true;
            }
        }
    };

    SkyAtmospherePass::SkyAtmospherePass(const SkyAtmospherePassSettings& settings)
        : m_Settings(settings)
        , m_SourceCache(Container::MakeUnique<SkyAtmosphereSourceCache>())
        , m_RetireQueue(Container::MakeUnique<GpuRetireQueue>())
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

        m_Keyframes = Container::MakeUnique<KeyframeState>();
        if (!m_Keyframes->Window.Configure({m_Settings.KeyframeAltitudesDegrees,
                                            m_Settings.KeyframeJumpThresholdDegrees, 2u}))
        {
            NORVES_LOG_WARNING("SkyAtmospherePass", "空のキーフレームの設定が不正なため、既定値で続けます");
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

        m_CloudBlend = Container::MakeUnique<SkyCloudBlendRecorder>();
        if (!m_CloudBlend->Initialize(m_Device, context.ShaderMgr))
        {
            // 記録器が失敗を一度警告する。パス自体は晴天入力で使い続ける。
            m_CloudBlend.reset();
        }
        m_bInitialized = true;
        return true;
    }

    void SkyAtmospherePass::Shutdown()
    {
        if (m_Keyframes)
        {
            // ワーカーが入力と結果を使い終わってから、常駐キーと専用状態を捨てる。
            m_Keyframes->Builder.WaitAll();
            Container::VariableArray<SkyKeyframeBuildResult> results;
            m_Keyframes->Builder.CollectCompleted(results);
            results.clear();
            m_Keyframes->RetireAllKeys(*m_RetireQueue);
            m_Keyframes.reset();
        }
        if (m_CloudBlend)
        {
            m_CloudBlend->Shutdown();
            m_CloudBlend.reset();
        }
        m_CloudBlendLane = nullptr;
        for (auto& texture : m_UnitOvercastTextures)
        {
            texture.reset();
        }
        for (auto& texture : m_CloudBlendOutputs)
        {
            texture.reset();
        }
        for (auto& handle : m_CloudBlendHandles)
        {
            handle = {};
        }
        std::fill_n(m_UnitOvercastAverageIrradiance, 3u, 0.0f);
        std::fill_n(m_CloudBlendAverageIrradiance, 3u, 0.0f);
        m_UnitOvercastIblBuildCount = 0u;
        m_bUnitOvercastIblAttempted = false;
        m_bUnitOvercastWarningLogged = false;
        m_bMissingOvercastWarningLogged = false;
        m_bCloudBlendDirty = true;
        m_bUseCloudBlend = false;
        m_TransmittanceTexture.reset();
        m_RadianceTexture.reset();
        m_SunDiskTexture.reset();
        m_IblEnvironmentTexture.reset();
        m_IblDiffuseIrradianceTexture.reset();
        m_IblPrefilteredSpecularTexture.reset();
        std::fill_n(m_IblAverageIrradiance, 3u, 0.0f);
        m_bIblValid = false;
        if (m_RetireQueue)
        {
            m_RetireQueue->Clear();
        }
        if (m_SourceCache)
        {
            m_SourceCache->Reset();
        }
        m_Sampler.reset();
        m_FragmentShader.reset();
        m_TransmittanceHandle = {};
        m_RadianceHandle = {};
        m_SunDiskHandle = {};
        m_Device = nullptr;
        m_LastParameters = SkyAtmosphereParameters{};
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

    uint64_t SkyAtmospherePass::GetSkyModelBuildCount() const
    {
        return (m_SourceCache ? m_SourceCache->GetModelBuildCount() : 0u) +
            (m_Keyframes ? m_Keyframes->Builder.GetBuildCount() + m_Keyframes->SyncBuildCount : 0u);
    }

    void SkyAtmospherePass::AdvanceRetireQueue(const ViewRenderContext& context)
    {
        // このキューはフレームの通し番号の系列で回す。完了したフレームまでに預けた物を捨て、
        // この後に預ける物はこのフレームの通し番号が完了するまで生かす
        // （前のフレームの記録が古い 3 枚を読んでいるかもしれない）。
        m_RetireQueue->BeginFrame(context.CompletedRenderFrameSerial);
        m_RetireQueue->CommitFrame(context.ResolveRenderFrameSerial());
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
        m_RetireQueue->Retire(std::move(outTexture));
        outTexture = m_Device->CreateTexture(textureDesc);
        return outTexture != nullptr;
    }

    bool SkyAtmospherePass::EnsureResources()
    {
        const bool bTransmittanceMatches = m_TransmittanceTexture &&
            m_TransmittanceTexture->GetWidth() == m_Settings.TransmittanceWidth &&
            m_TransmittanceTexture->GetHeight() == m_Settings.TransmittanceHeight &&
            m_TransmittanceTexture->GetFormat() == m_Settings.LutFormat;
        const RHI::ITexture* previousSunDisk = m_SunDiskTexture.get();
        const bool bCreated = CreateTexture(m_TransmittanceTexture,
                                            m_Settings.TransmittanceWidth,
                                            m_Settings.TransmittanceHeight,
                                            "SkyAtmosphere.Transmittance") &&
                              CreateTexture(m_SunDiskTexture, 1u, 1u, "SkyAtmosphere.SunDisk");
        if (!bTransmittanceMatches)
        {
            // 透過率 LUT が作り直されたら、次の準備で窓も作り直す。
            m_bLutValid = false;
        }
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
        // 雲の下の霞を照らす直達の太陽は、方向光と同じ係数で弱める。
        const float cloudDirectFactor = ComputeCloudDirectFactor(parameters);
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
                outProfile.SunIlluminance[index][channel] = static_cast<float>(sunIrradiance * sun[channel]) * cloudDirectFactor;
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
        // 空の太陽の方向光と同じ地表の透過率で減光・着色し、曇り度に応じて円盤を薄める（ComputeSunDiskVisibility）。
        const Math::Vector3& transmittance = m_LutSunGroundTransmittance;
        const float sunDiskVisibility = ComputeSunDiskVisibility(parameters);
        const uint16_t pixel[4] = {
            FloatToHalfRne(safeValue * std::clamp(transmittance.x, 0.0f, 1.0f) * sunDiskVisibility),
            FloatToHalfRne(safeValue * std::clamp(transmittance.y, 0.0f, 1.0f) * sunDiskVisibility),
            FloatToHalfRne(safeValue * std::clamp(transmittance.z, 0.0f, 1.0f) * sunDiskVisibility),
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
        if (!m_Keyframes)
        {
            return false;
        }
        auto& state = *m_Keyframes;
        const bool bFullReset = !m_bLutValid || !state.bSetKeyValid ||
            !AreSkyKeyframeSetKeysEqual(state.SetKey, parameters);
        if (bFullReset || state.Window.IsJump(parameters.SunAltitudeDegrees))
        {
            const auto startTime = std::chrono::steady_clock::now();
            state.bSetKeyValid = false;
            state.RetireAllKeys(*m_RetireQueue);
            if (bFullReset)
            {
                // 大気が異なるモデルの表は次の錨へ引き継がない。旧世代の仕事は回収時に捨てる。
                state.ReferenceModel.reset();
                m_bLutValid = false;
            }
            const uint32_t anchor = state.Window.Reset(parameters.SunAltitudeDegrees);
            state.Keys.resize(state.Window.GetKeyCount());
            state.UploadSerials.resize(state.Window.GetKeyCount(), 0u);
            SkyKeyframeCpuData data;
            // 錨は今の高度・方位そのもので作り、静止中の重みを 1、ずらしを 0 にする。
            if (!BuildSkyKeyframeCpuData(MakeSkyAtmosphereClearKey(parameters), state.ReferenceModel.get(),
                                        m_Settings.RadianceWidth, m_Settings.RadianceHeight, data) ||
                !UploadSkyKeyframe(m_Device, data, state.Keys[anchor]))
            {
                state.Window.MarkFailed(anchor);
                state.WarnFailure();
                return false;
            }
            state.Window.MarkResident(anchor);
            state.ReferenceModel = data.Model;
            ++state.SyncBuildCount;
            state.UploadSerials[anchor] = ++state.NextUploadSerial;
            if (bFullReset && !GenerateTransmittanceLut(*data.Model))
            {
                state.WarnFailure();
                return false;
            }
            state.SetKey = MakeSkyKeyframeSetKey(parameters);
            state.bSetKeyValid = true;
            m_bLutValid = true;
            m_bCloudBlendDirty = true;
            const double elapsedMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - startTime).count();
            NORVES_LOG_INFO("SkyAtmospherePass",
                            "空の錨と LUT を %.1f ms で生成しました (radiance %ux%u, transmittance %ux%u, "
                            "sun altitude %.2f, azimuth %.2f, reused_tables=%d)",
                            elapsedMilliseconds, m_Settings.RadianceWidth, m_Settings.RadianceHeight,
                            m_Settings.TransmittanceWidth, m_Settings.TransmittanceHeight,
                            parameters.SunAltitudeDegrees, parameters.SunAzimuthDegrees,
                            data.Model->ReusedSunIndependentTables() ? 1 : 0);
        }

        Container::VariableArray<SkyKeyframeBuildResult> results;
        state.Builder.CollectCompleted(results);
        for (const auto& result : results)
        {
            if (result.Generation != state.Window.GetGeneration())
            {
                continue;
            }
            if (!result.Data || !result.Data->bValid)
            {
                state.Window.MarkFailed(result.KeyIndex);
                state.WarnFailure();
                continue;
            }
            state.RetireKey(result.KeyIndex, *m_RetireQueue);
            if (UploadSkyKeyframe(m_Device, *result.Data, state.Keys[result.KeyIndex]))
            {
                state.Window.MarkResident(result.KeyIndex);
                state.UploadSerials[result.KeyIndex] = ++state.NextUploadSerial;
            }
            else
            {
                state.Window.MarkFailed(result.KeyIndex);
                state.WarnFailure();
            }
        }

        state.Selection = state.Window.Update(parameters.SunAltitudeDegrees);
        if (state.Selection.bValid && state.Selection.WeightB == 0.0f)
        {
            // 隣のキーも常駐している場合でも、寄与しない入力は a と同じキーへそろえる。
            state.Selection.KeyB = state.Selection.KeyA;
        }
        const auto& selection = state.Selection;
        if (!selection.bValid)
        {
            state.WarnFailure();
            return false;
        }
        if (selection.bClamped && state.Window.ConsumeLateWarning() && !state.bLateWarningLogged)
        {
            NORVES_LOG_WARNING("SkyAtmospherePass",
                "空のキーフレームの生成が間に合わないため、直前のキーフレームに留めます (altitude=%.3f)",
                parameters.SunAltitudeDegrees);
            state.bLateWarningLogged = true;
        }
        Container::VariableArray<uint32_t> indices;
        state.Window.CollectEvictions(indices);
        for (uint32_t index : indices)
        {
            state.RetireKey(index, *m_RetireQueue);
            state.Window.MarkEvicted(index);
        }
        state.Window.CollectRequests(indices);
        for (uint32_t index : indices)
        {
            auto keyParameters = MakeSkyAtmosphereClearKey(parameters);
            keyParameters.SunAltitudeDegrees = state.Window.GetKeyAltitude(index);
            keyParameters.SunAzimuthDegrees = 0.0f;
            state.Window.MarkPending(index);
            state.Builder.Submit(index, state.Window.GetGeneration(), keyParameters, state.ReferenceModel,
                                 m_Settings.RadianceWidth, m_Settings.RadianceHeight);
        }

        const auto& a = state.Keys[selection.KeyA];
        const auto& b = state.Keys[selection.KeyB];
        const float wa = selection.WeightA;
        const float wb = selection.WeightB;
        // 重み 0 の項を計算せず、錨の数値と表をそのまま写す。
        state.ClearHorizontalIlluminance = a.ClearHorizontalIlluminance;
        m_LutSunGroundTransmittance = a.SunGroundTransmittance;
        state.ClearAerialProfile = a.AerialProfile;
        if (wb != 0.0f)
        {
            state.ClearHorizontalIlluminance = a.ClearHorizontalIlluminance * wa + b.ClearHorizontalIlluminance * wb;
            m_LutSunGroundTransmittance = a.SunGroundTransmittance * wa + b.SunGroundTransmittance * wb;
            for (uint32_t index = 0u; index < SkyAerialProfileSize; ++index)
            {
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    state.ClearAerialProfile.SunIlluminance[index][channel] =
                        a.AerialProfile.SunIlluminance[index][channel] * wa + b.AerialProfile.SunIlluminance[index][channel] * wb;
                    state.ClearAerialProfile.MultipleScattering[index][channel] =
                        a.AerialProfile.MultipleScattering[index][channel] * wa + b.AerialProfile.MultipleScattering[index][channel] * wb;
                }
            }
        }
        state.ClearAerialProfile.SunCosine = MakeSunDirectionFromAltitudeAzimuth(
            parameters.SunAltitudeDegrees, parameters.SunAzimuthDegrees).y;
        state.ClearAerialProfile.bValid = a.AerialProfile.bValid && (wb == 0.0f || b.AerialProfile.bValid);
        m_AerialProfile = state.ClearAerialProfile;
        const float cloudDirectFactor = ComputeCloudDirectFactor(parameters);
        for (uint32_t index = 0u; index < SkyAerialProfileSize; ++index)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                m_AerialProfile.SunIlluminance[index][channel] *= cloudDirectFactor;
            }
        }
        const auto& dominant = wa >= wb ? a : b;
        m_RadianceTexture = dominant.Radiance;
        m_IblEnvironmentTexture = dominant.Environment;
        m_IblDiffuseIrradianceTexture = dominant.Diffuse;
        m_IblPrefilteredSpecularTexture = dominant.Prefilter;
        std::copy_n(dominant.AverageIrradiance, 3u, m_IblAverageIrradiance);
        m_bIblValid = dominant.IsValid();
        return GenerateSunDiskTexture(parameters, preExposure, frameResources);
    }

    void SkyAtmospherePass::GenerateUnitOvercastIbl(const SkyAtmosphereParameters& parameters)
    {
        const auto* source = m_SourceCache->AcquireUnitOvercastRadianceSource(
            parameters, m_Settings.RadianceWidth, m_Settings.RadianceHeight);
        const uint64_t buildCount = m_SourceCache->GetUnitOvercastBuildCount();
        if (source && m_bUnitOvercastIblAttempted && m_UnitOvercastIblBuildCount == buildCount)
        {
            // 作れなかった場合も、同じ源で毎フレーム再試行しない。
            return;
        }

        const auto startTime = std::chrono::steady_clock::now();
        for (auto& texture : m_UnitOvercastTextures)
        {
            if (texture)
            {
                m_bCloudBlendDirty = true;
                m_RetireQueue->Retire(std::move(texture));
            }
        }
        std::fill_n(m_UnitOvercastAverageIrradiance, 3u, 0.0f);
        if (source)
        {
            m_bUnitOvercastIblAttempted = true;
            m_UnitOvercastIblBuildCount = buildCount;
        }
        if (!source || !CreateIblResources(m_Device, *source,
                                           m_Settings.RadianceWidth, m_Settings.RadianceHeight,
                                           "SkyAtmosphere.UnitOvercastEnvironment",
                                           "SkyAtmosphere.UnitOvercastDiffuseIrradiance",
                                           "SkyAtmosphere.UnitOvercastPrefilteredSpecular",
                                           m_UnitOvercastTextures[0], m_UnitOvercastTextures[1],
                                           m_UnitOvercastTextures[2], m_UnitOvercastAverageIrradiance))
        {
            if (!m_bUnitOvercastWarningLogged)
            {
                NORVES_LOG_WARNING("SkyAtmospherePass", "単位曇天の IBL を生成できませんでした");
                m_bUnitOvercastWarningLogged = true;
            }
            return;
        }
        m_bCloudBlendDirty = true;
        const double elapsedMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - startTime).count();
        NORVES_LOG_INFO("SkyAtmospherePass",
                        "Sky unit overcast IBL generated in %.1f ms (unit_overcast_builds=%llu)",
                        elapsedMilliseconds, static_cast<unsigned long long>(buildCount));
    }

    bool SkyAtmospherePass::EnsureCloudBlendOutput(RHI::TexturePtr& output,
                                                  const RHI::TexturePtr& clear,
                                                  const char* name)
    {
        if (!clear)
        {
            return false;
        }
        if (output && output->GetWidth() == clear->GetWidth() &&
            output->GetHeight() == clear->GetHeight() && output->GetMipLevels() == clear->GetMipLevels())
        {
            return true;
        }
        m_bCloudBlendDirty = true;
        m_RetireQueue->Retire(std::move(output));
        output = CreateSkyCloudBlendOutputTexture(m_Device, clear->GetWidth(), clear->GetHeight(),
                                                  clear->GetMipLevels(), name);
        return output != nullptr;
    }

    bool SkyAtmospherePass::PrepareCloudBlend(const SkyAtmosphereParameters& parameters,
                                              FrameResourceLane& lane)
    {
        if (!m_Keyframes || !m_Keyframes->Selection.bValid)
        {
            return false;
        }
        auto& state = *m_Keyframes;
        const auto& selection = state.Selection;
        const auto& a = state.Keys[selection.KeyA];
        const auto& b = state.Keys[selection.KeyB];
        GenerateUnitOvercastIbl(parameters);
        const RHI::TexturePtr clearTexturesA[4] = {a.Radiance, a.Environment, a.Diffuse, a.Prefilter};
        const RHI::TexturePtr clearTexturesB[4] = {b.Radiance, b.Environment, b.Diffuse, b.Prefilter};
        const char* names[4] = {"SkyAtmosphere.Radiance", "SkyAtmosphere.Environment",
                                "SkyAtmosphere.DiffuseIrradiance", "SkyAtmosphere.PrefilteredSpecular"};
        const bool bUnitOvercastValid = m_UnitOvercastTextures[0] &&
            m_UnitOvercastTextures[1] && m_UnitOvercastTextures[2];
        SkyCloudBlendWeights weights = ComputeSkyCloudBlendWeightsFromIlluminance(state.ClearHorizontalIlluminance, parameters);
        if (!bUnitOvercastValid)
        {
            weights = {};
            if (parameters.CloudCover > 0.0f && !m_bMissingOvercastWarningLogged)
            {
                NORVES_LOG_WARNING("SkyAtmospherePass", "単位曇天の入力が無いため晴天を混ぜ先へ写します");
                m_bMissingOvercastWarningLogged = true;
            }
        }
        const SkyCloudBlendInputWeights inputWeights{
            weights.ClearWeight * selection.WeightA,
            weights.ClearWeight * selection.WeightB,
            weights.OvercastWeight,
            parameters.SunAzimuthDegrees == a.AzimuthDegrees ? 0.0f :
                (parameters.SunAzimuthDegrees - a.AzimuthDegrees) / 360.0f,
            parameters.SunAzimuthDegrees == b.AzimuthDegrees ? 0.0f :
                (parameters.SunAzimuthDegrees - b.AzimuthDegrees) / 360.0f};
        SkyCloudBlendTarget targets[4];
        for (size_t index = 0u; index < 4u; ++index)
        {
            if (!EnsureCloudBlendOutput(m_CloudBlendOutputs[index], clearTexturesA[index], names[index]))
            {
                return false;
            }
            targets[index] = {clearTexturesA[index], clearTexturesB[index],
                bUnitOvercastValid ? m_UnitOvercastTextures[index == 0u ? 0u : index - 1u] : clearTexturesA[index],
                m_CloudBlendOutputs[index]};
        }
        const auto& lastWeights = state.LastBlendWeights;
        const bool bInputsChanged = state.UploadSerials[selection.KeyA] != state.LastBlendSerialA ||
            state.UploadSerials[selection.KeyB] != state.LastBlendSerialB ||
            inputWeights.ClearWeightA != lastWeights.ClearWeightA ||
            inputWeights.ClearWeightB != lastWeights.ClearWeightB ||
            inputWeights.OvercastWeight.x != lastWeights.OvercastWeight.x ||
            inputWeights.OvercastWeight.y != lastWeights.OvercastWeight.y ||
            inputWeights.OvercastWeight.z != lastWeights.OvercastWeight.z ||
            inputWeights.ShiftA != lastWeights.ShiftA || inputWeights.ShiftB != lastWeights.ShiftB;
        if (m_bCloudBlendDirty || bInputsChanged)
        {
            // 失敗した準備は次のフレームで再試行し、今のフレームは重みの大きい晴天キーを公開する。
            m_bCloudBlendDirty = true;
            if (!m_CloudBlend->Prepare(lane, targets, 4u, inputWeights))
            {
                return false;
            }
            m_CloudBlendLane = &lane;
            state.LastBlendWeights = inputWeights;
            state.LastBlendSerialA = state.UploadSerials[selection.KeyA];
            state.LastBlendSerialB = state.UploadSerials[selection.KeyB];
            m_bCloudBlendDirty = false;
        }
        const bool bCopyClear = inputWeights.ClearWeightA == 1.0f && inputWeights.ClearWeightB == 0.0f &&
            weights.OvercastWeight.x == 0.0f && weights.OvercastWeight.y == 0.0f && weights.OvercastWeight.z == 0.0f;
        const float overcastWeights[3] = {weights.OvercastWeight.x, weights.OvercastWeight.y, weights.OvercastWeight.z};
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            if (bCopyClear)
            {
                m_CloudBlendAverageIrradiance[channel] = a.AverageIrradiance[channel];
            }
            else if (selection.WeightB == 0.0f)
            {
                // 重み 0 の項を省き、晴天と曇天の二項をこの順序で加算する。
                m_CloudBlendAverageIrradiance[channel] = weights.ClearWeight * a.AverageIrradiance[channel] +
                    overcastWeights[channel] * m_UnitOvercastAverageIrradiance[channel];
            }
            else
            {
                m_CloudBlendAverageIrradiance[channel] = inputWeights.ClearWeightA * a.AverageIrradiance[channel] +
                    inputWeights.ClearWeightB * b.AverageIrradiance[channel] +
                    overcastWeights[channel] * m_UnitOvercastAverageIrradiance[channel];
            }
        }
        return true;
    }

    void SkyAtmospherePass::LogSunSweep()
    {
        if (!m_LastParameters.bDebugLogSunSweep || !m_Keyframes || !m_Keyframes->Selection.bValid)
        {
            return;
        }
        auto& state = *m_Keyframes;
        const auto& selection = state.Selection;
        const auto& previous = state.SweepPreviousSelection;
        if (!state.bSweepPreviousValid || state.SweepPreviousAltitude != m_LastParameters.SunAltitudeDegrees ||
            state.SweepPreviousAzimuth != m_LastParameters.SunAzimuthDegrees ||
            previous.KeyA != selection.KeyA || previous.KeyB != selection.KeyB ||
            previous.WeightA != selection.WeightA || previous.WeightB != selection.WeightB)
        {
            const float* average = m_bUseCloudBlend ? m_CloudBlendAverageIrradiance : m_IblAverageIrradiance;
            const float meanIrradiance = 0.2126f * average[0] + 0.7152f * average[1] + 0.0722f * average[2];
            NORVES_LOG_INFO("SkyAtmospherePass", "SKY_SWEEP frame=%llu altitude=%.3f key_a=%.3f key_b=%.3f weight_a=%.4f weight_b=%.4f mean_irradiance=%.4f",
                static_cast<unsigned long long>(state.SweepFrame), m_LastParameters.SunAltitudeDegrees,
                state.Window.GetKeyAltitude(selection.KeyA), state.Window.GetKeyAltitude(selection.KeyB),
                selection.WeightA, selection.WeightB, meanIrradiance);
        }
        state.SweepPreviousAltitude = m_LastParameters.SunAltitudeDegrees;
        state.SweepPreviousAzimuth = m_LastParameters.SunAzimuthDegrees;
        state.SweepPreviousSelection = selection;
        state.bSweepPreviousValid = true;
    }

    void SkyAtmospherePass::DiscardCloudBlend()
    {
        if (m_CloudBlend && m_CloudBlend->HasPrepared())
        {
            m_bCloudBlendDirty = true;
            if (m_CloudBlendLane)
            {
                // 空集合の準備は借用を増やさず、記録しなかった定数・記述子への参照だけを捨てる。
                m_CloudBlend->Prepare(*m_CloudBlendLane, nullptr, 0u, SkyCloudBlendInputWeights{});
            }
        }
        m_CloudBlendLane = nullptr;
        m_bUseCloudBlend = false;
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
                                      bValid ? (m_bUseCloudBlend ? m_CloudBlendOutputs[0] : m_RadianceTexture) : RHI::TexturePtr{},
                                      bValid ? m_SunDiskTexture : RHI::TexturePtr{},
                                      bValid ? m_Sampler : RHI::SamplerPtr{},
                                      preExposure,
                                      bValid ? m_LastSunDiskPreExposedLuminance : 0.0f,
                                      bValid && m_bLastSunDiskSaturated,
                                      bValid);
        context.SkyAtmosphere.AerialProfile = bValid ? m_AerialProfile : SkyAerialProfile{};
        if (bValid && m_bIblValid)
        {
            context.SkyAtmosphere.Ibl.EnvironmentTexture = m_bUseCloudBlend ? m_CloudBlendOutputs[1] : m_IblEnvironmentTexture;
            context.SkyAtmosphere.Ibl.DiffuseIrradianceTexture = m_bUseCloudBlend ? m_CloudBlendOutputs[2] : m_IblDiffuseIrradianceTexture;
            context.SkyAtmosphere.Ibl.PrefilteredSpecularTexture = m_bUseCloudBlend ? m_CloudBlendOutputs[3] : m_IblPrefilteredSpecularTexture;
            std::copy_n(m_bUseCloudBlend ? m_CloudBlendAverageIrradiance : m_IblAverageIrradiance,
                        3u, context.SkyAtmosphere.Ibl.AverageIrradiance);
            context.SkyAtmosphere.Ibl.bValid = true;
        }
        else
        {
            context.SkyAtmosphere.Ibl.Reset();
        }
    }

    void SkyAtmospherePass::Declare(RenderGraphBuilder& builder)
    {
        // Execute まで進まなかった準備も、次のフレームへ持ち越さない。
        DiscardCloudBlend();
        for (auto& handle : m_CloudBlendHandles)
        {
            handle = {};
        }
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
        AdvanceRetireQueue(*context);

        m_LastParameters = SanitizeSkyAtmosphereParameters(ResolveSnapshot(*context));
        if (m_Keyframes)
        {
            if (m_LastParameters.bDebugLogSunSweep)
            {
                ++m_Keyframes->SweepFrame;
            }
            else
            {
                m_Keyframes->SweepFrame = 0u;
                m_Keyframes->bSweepPreviousValid = false;
            }
        }
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

        if (m_CloudBlend && m_CloudBlend->IsReady() && context->FrameResources)
        {
            m_bUseCloudBlend = PrepareCloudBlend(m_LastParameters, *context->FrameResources);
        }

        LogSunSweep();

        const RGResourceHandle transmittanceResource = builder.ImportTexture(
            m_TransmittanceTexture,
            RHI::ResourceState::ShaderResource,
            "SkyAtmosphere.Transmittance");
        const RGResourceHandle radianceResource = builder.ImportTexture(
            m_bUseCloudBlend ? m_CloudBlendOutputs[0] : m_RadianceTexture,
            RHI::ResourceState::ShaderResource,
            "SkyAtmosphere.Radiance");
        const RGResourceHandle sunDiskResource = builder.ImportTexture(
            m_SunDiskTexture,
            RHI::ResourceState::ShaderResource,
            "SkyAtmosphere.SunDisk");
        bool bBlendResourcesImported = true;
        if (m_bUseCloudBlend)
        {
            m_CloudBlendHandles[0] = radianceResource;
            const char* names[3] = {"SkyAtmosphere.Environment", "SkyAtmosphere.DiffuseIrradiance",
                                    "SkyAtmosphere.PrefilteredSpecular"};
            for (size_t index = 1u; m_bIblValid && index < 4u; ++index)
            {
                m_CloudBlendHandles[index] = builder.ImportTexture(m_CloudBlendOutputs[index],
                    RHI::ResourceState::ShaderResource, names[index - 1u]);
                bBlendResourcesImported = bBlendResourcesImported && m_CloudBlendHandles[index].IsValid();
            }
        }
        if (!transmittanceResource.IsValid() || !radianceResource.IsValid() ||
            !sunDiskResource.IsValid() || !bBlendResourcesImported)
        {
            m_bPrepared = false;
            m_bSunDiskCopyPending = false;
            DiscardCloudBlend();
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
        if (m_bUseCloudBlend)
        {
            for (size_t index = 0u; index < (m_bIblValid ? 4u : 1u); ++index)
            {
                if (m_CloudBlend->HasPrepared())
                {
                    // 前のフレームの読みの後に書き、記録内で全ミップを ShaderResource へ戻す。
                    builder.Write(m_CloudBlendHandles[index], RHI::ResourceState::UnorderedAccess,
                                  RHI::ResourceState::ShaderResource);
                }
                else
                {
                    builder.Read(m_CloudBlendHandles[index], RHI::ResourceState::ShaderResource);
                }
            }
        }
        else
        {
            builder.Read(m_RadianceHandle.ToResourceHandle(), RHI::ResourceState::ShaderResource);
        }
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
        const bool bResourcesResolved = AreGraphResourcesResolved(resources);
        if (m_CloudBlend && m_CloudBlend->HasPrepared())
        {
            if (bResourcesResolved && context.CommandList)
            {
                m_CloudBlend->Record(*context.CommandList);
                m_CloudBlendLane = nullptr;
            }
            else
            {
                // 書けない場合も、解決できた出力は宣言した最後の状態へ戻す。
                if (context.CommandList)
                {
                    for (const auto& handle : m_CloudBlendHandles)
                    {
                        const RHI::TexturePtr texture = handle.IsValid() ? resources.GetTexture(handle) : RHI::TexturePtr{};
                        if (texture)
                        {
                            context.CommandList->TextureBarrier(texture, RHI::ResourceState::UnorderedAccess,
                                                                RHI::ResourceState::ShaderResource);
                        }
                    }
                }
                DiscardCloudBlend();
            }
        }
        if (!m_bPrepared || !m_TransmittanceHandle.IsValid() ||
            !m_RadianceHandle.IsValid() || !m_SunDiskHandle.IsValid())
        {
            m_SunDiskUploadBuffer.reset();
            PublishContextResources(context, false);
            return;
        }

        const RHI::TexturePtr sunDiskTexture = resources.GetTexture(m_SunDiskHandle);
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
        if (m_bUseCloudBlend && m_bIblValid)
        {
            for (size_t index = 1u; index < 4u; ++index)
            {
                if (!m_CloudBlendHandles[index].IsValid() || !resources.GetTexture(m_CloudBlendHandles[index]))
                {
                    return false;
                }
            }
        }
        return m_bPrepared && m_TransmittanceHandle.IsValid() && m_RadianceHandle.IsValid() && m_SunDiskHandle.IsValid() &&
               resources.GetTexture(m_TransmittanceHandle) != nullptr &&
               resources.GetTexture(m_RadianceHandle) != nullptr &&
               resources.GetTexture(m_SunDiskHandle) != nullptr;
    }

    bool SkyAtmospherePass::DeclaresAllLayoutChanges() const
    {
        // 太陽の円盤は CopyDest、空の混ぜ先は UnorderedAccess で書き、宣言した ShaderResource へ
        // 記録内のバリアで戻す。中継のバッファのバリアは layout を持たない。
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
