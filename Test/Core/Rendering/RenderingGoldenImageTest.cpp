#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingGoldenImage.h"
#include "RenderingValidation/RenderingPerceptualDiff.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"

#include <cmath>
#include <iomanip>
#include <iostream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    const char* SceneName(SceneKind scene)
    {
        return scene == SceneKind::Indoor ? "indoor" : "outdoor";
    }

    const TCHAR* BaselineFileName(SceneKind scene)
    {
        return scene == SceneKind::Indoor ? TEXT("Indoor.png") : TEXT("Outdoor.png");
    }

    const char* StatusName(GoldenImageStatus status)
    {
        switch (status)
        {
        case GoldenImageStatus::Success:
            return "Success";
        case GoldenImageStatus::CaptureNotSuccessful:
            return "CaptureNotSuccessful";
        case GoldenImageStatus::UnsupportedFormat:
            return "UnsupportedFormat";
        case GoldenImageStatus::InvalidDimensions:
            return "InvalidDimensions";
        case GoldenImageStatus::InvalidPixelData:
            return "InvalidPixelData";
        case GoldenImageStatus::EncodeFailed:
            return "EncodeFailed";
        case GoldenImageStatus::DecodeFailed:
            return "DecodeFailed";
        case GoldenImageStatus::FileOpenFailed:
            return "FileOpenFailed";
        case GoldenImageStatus::FileWriteFailed:
            return "FileWriteFailed";
        }
        return "Unknown";
    }

    Core::Container::String BaselinePath(SceneKind scene)
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/");
        path += BaselineFileName(scene);
        return path;
    }

    Core::Container::String StagingPath(SceneKind scene)
    {
        Core::Container::String path(NORVES_BINARY_ROOT);
        path += TEXT("/RenderingValidation/BaselineStaging/");
        path += BaselineFileName(scene);
        path += TEXT(".tmp");
        return path;
    }

    Core::Container::String ThresholdPath()
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/VisualThresholds.tsv");
        return path;
    }

    constexpr float R3DensityValues[] = {0.0025f, 0.005f, 0.01f};
    constexpr uint32_t R3DensityCount =
        static_cast<uint32_t>(sizeof(R3DensityValues) / sizeof(R3DensityValues[0]));
    constexpr double R3MaximumMeanFlipError = 0.02;
    constexpr uint8_t R3MaximumChannelDelta = 8u;

    const char* R3DensityCaseName(uint32_t index)
    {
        switch (index)
        {
        case 0u:
            return "low";
        case 1u:
            return "medium";
        case 2u:
            return "high";
        default:
            return "invalid";
        }
    }

    const TCHAR* R3DensityBaselineFileName(uint32_t index)
    {
        switch (index)
        {
        case 0u:
            return TEXT("R3FogDensityLow.png");
        case 1u:
            return TEXT("R3FogDensityMedium.png");
        case 2u:
            return TEXT("R3FogDensityHigh.png");
        default:
            return TEXT("R3FogDensityInvalid.png");
        }
    }

    Core::Container::String R3DensityBaselinePath(uint32_t index)
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/");
        path += R3DensityBaselineFileName(index);
        return path;
    }

    Core::Container::String R3DensityStagingPath(uint32_t index)
    {
        Core::Container::String path(NORVES_BINARY_ROOT);
        path += TEXT("/RenderingValidation/BaselineStaging/");
        path += R3DensityBaselineFileName(index);
        path += TEXT(".tmp");
        return path;
    }

    Core::Container::String R3DensityThresholdPath()
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Thresholds/RenderingValidation/R3DensitySweep.tsv");
        return path;
    }

    void EnsureR3BaselineStagingDirectories()
    {
        Core::Container::String validationRoot(NORVES_BINARY_ROOT);
        validationRoot += TEXT("/RenderingValidation");
        Core::Container::String stagingRoot = validationRoot;
        stagingRoot += TEXT("/BaselineStaging");
        CreateDirectory(validationRoot.c_str(), nullptr);
        CreateDirectory(stagingRoot.c_str(), nullptr);
    }

    bool LoadR3DensityGoldenThresholds(VisualGoldenThresholds& outThresholds)
    {
        FileStream::FileStreamUniquePtr stream = FileStream::FileStream::CreateUnique(
            R3DensityThresholdPath(), FileStream::FileMode::Read, FileStream::FileAccess::Read);
        if (stream == nullptr)
        {
            return false;
        }
        const Core::Container::String contents = stream->ReadString();
        if (contents.find(TEXT("schema=NorvesLib.RenderingValidation.R3DensitySweep.v1")) ==
                Core::Container::String::npos ||
            contents.find(TEXT("golden_mean_flip_max=0.020000 golden_max_channel_delta=8")) ==
                Core::Container::String::npos)
        {
            return false;
        }
        outThresholds.MaximumMeanFlipError = R3MaximumMeanFlipError;
        outThresholds.MaximumChannelDelta = R3MaximumChannelDelta;
        return true;
    }

    class GoldenHandler final : public RenderingValidationApplicationHandler
    {
    protected:
        bool OnPreInitialize(
            const Core::Container::VariableArray<Core::Container::String>& args) override
        {
            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            if (GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::BackBuffer)
            {
                LOG_ERROR("RenderingGoldenImageTest は BackBuffer capture のみを受け付けます");
                return false;
            }
            if (m_bR3DensityScenario && GetRunConfig().Scene != SceneKind::Outdoor)
            {
                LOG_ERROR("R3 density-sweep には outdoor scene が必要です");
                return false;
            }
            if (m_bR3DensityScenario &&
                (m_bWriteBaselineStaging || m_bMeasureVisual))
            {
                LOG_ERROR("R3 density-sweep は既存のgolden image modeと併用できません");
                return false;
            }
            if (m_bWriteR3DensityBaselineStaging && !m_bR3DensityScenario)
            {
                LOG_ERROR("R3 baseline stagingには density-sweep scenario が必要です");
                return false;
            }
            return true;
        }

        bool OnInitialize() override
        {
            if (!RenderingValidationApplicationHandler::OnInitialize())
            {
                return false;
            }
            if (m_bR3DensityScenario)
            {
                return GetFixture().ApplyR3ShadowedShaftsFixture(true, true, true);
            }
            if (!GetFixture().ApplyTransparentPhysicalLightingObjectPresence())
            {
                LOG_ERROR("golden object-presence fixture preparation failed");
                return false;
            }
            return true;
        }

        bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& outFailureReason) override
        {
            if (argument == TEXT("--r3-scenario=density-sweep"))
            {
                if (m_bR3DensityScenario || m_bWriteBaselineStaging || m_bMeasureVisual ||
                    m_bWriteR3DensityBaselineStaging)
                {
                    outFailureReason = TEXT("重複または競合するR3 goldenシナリオ指定です");
                    return false;
                }
                m_bR3DensityScenario = true;
                return true;
            }
            if (argument == TEXT("--write-r3-density-baseline-staging"))
            {
                if (m_bWriteR3DensityBaselineStaging || m_bWriteBaselineStaging || m_bMeasureVisual)
                {
                    outFailureReason = TEXT("重複または競合するR3 baseline staging指定です");
                    return false;
                }
                m_bWriteR3DensityBaselineStaging = true;
                return true;
            }
            if (argument == TEXT("--write-baseline-staging"))
            {
                if (m_bWriteBaselineStaging || m_bMeasureVisual || m_bR3DensityScenario ||
                    m_bWriteR3DensityBaselineStaging)
                {
                    outFailureReason = TEXT("重複または競合するgolden image mode指定です");
                    LOG_ERROR("golden image argument rejected: duplicate or conflicting mode");
                    return false;
                }
                m_bWriteBaselineStaging = true;
                return true;
            }
            if (argument == TEXT("--measure-visual"))
            {
                if (m_bMeasureVisual || m_bWriteBaselineStaging || m_bR3DensityScenario ||
                    m_bWriteR3DensityBaselineStaging)
                {
                    outFailureReason = TEXT("duplicate or conflicting golden image mode");
                    LOG_ERROR("golden image argument rejected: duplicate or conflicting mode");
                    return false;
                }
                m_bMeasureVisual = true;
                return true;
            }
            outFailureReason = TEXT("unsupported golden image argument");
            LOG_ERROR("golden image argument rejected: unsupported option");
            return false;
        }

        void ApplyCaptureStageState(Core::Rendering::RenderWorld& renderWorld) override
        {
            if (!m_bR3DensityScenario || m_R3DensityStage >= R3DensityCount)
            {
                return;
            }
            if (!GetFixture().ApplyR3ShadowedShaftsFixture(true, true, true))
            {
                m_bR3DensityStageApplyFailed = true;
                LOG_ERROR("R3 density-sweep fixtureの状態を適用できませんでした");
                return;
            }
            renderWorld.SetMainCamera(GetFixture().GetR3ShadowedShaftsCamera());
            Core::Rendering::SkyAtmosphereParameters sky =
                Core::Rendering::MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = false;
            renderWorld.SetSkyAtmosphere(sky);
            Core::Rendering::VolumetricFogParameters fog =
                Core::Rendering::MakeDefaultVolumetricFogParameters();
            fog.bEnabled = true;
            fog.DensityAtBaseHeight = R3DensityValues[m_R3DensityStage];
            renderWorld.SetVolumetricFogParameters(fog);
        }

        void AdvanceCaptureStage() override
        {
            if (m_bR3DensityScenario && m_R3DensityStage < R3DensityCount)
            {
                ++m_R3DensityStage;
            }
        }

        bool RequestFollowupCapture(
            const Core::Rendering::CapturedFrame& frame,
            Core::Rendering::FrameCaptureRequest& outRequest) override
        {
            (void)frame;
            if (!m_bR3DensityScenario || m_R3DensityStage >= R3DensityCount)
            {
                return false;
            }
            outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::BackBuffer;
            return true;
        }

        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason) override
        {
            if (m_bR3DensityScenario)
            {
                return EvaluateR3DensityGoldenFrame(frame, outFailureReason);
            }
            Core::Container::VariableArray<uint8_t> candidatePng;
            const GoldenImageStatus encodeStatus = EncodeCapturedFramePng(frame, candidatePng);
            if (encodeStatus != GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("captured frame PNG encode failed");
                LOG_ERROR(
                    "golden image encode failed: scene=%s request=%llu frame=%llu status=%s",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    StatusName(encodeStatus));
                return false;
            }

            if (m_bWriteBaselineStaging)
            {
                const GoldenImageStatus saveStatus = SavePng(
                    StagingPath(GetRunConfig().Scene),
                    Core::Container::Span<const uint8_t>(candidatePng));
                if (saveStatus != GoldenImageStatus::Success)
                {
                    outFailureReason = TEXT("fixed staging PNG write failed");
                    LOG_ERROR(
                        "golden image staging write failed: scene=%s request=%llu frame=%llu status=%s",
                        SceneName(GetRunConfig().Scene),
                        static_cast<unsigned long long>(frame.RequestId),
                        static_cast<unsigned long long>(frame.FrameNumber),
                        StatusName(saveStatus));
                    return false;
                }
                LOG_INFO(
                    "golden image staging written: scene=%s request=%llu frame=%llu",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber));
                return true;
            }

            Rgba8Image reference;
            GoldenImageStatus status = LoadPng(BaselinePath(GetRunConfig().Scene), reference);
            if (status != GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("golden baseline PNG load failed");
                LOG_ERROR(
                    "golden baseline load failed: scene=%s request=%llu frame=%llu status=%s",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    StatusName(status));
                return false;
            }

            Rgba8Image candidate;
            status = DecodePng(Core::Container::Span<const uint8_t>(candidatePng), candidate);
            if (status != GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("captured frame PNG decode failed");
                LOG_ERROR(
                    "golden candidate decode failed: scene=%s request=%llu frame=%llu status=%s",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    StatusName(status));
                return false;
            }

            if (m_bMeasureVisual)
            {
                double targetSum = 0.0;
                double backgroundSum = 0.0;
                uint32_t targetCount = 0u;
                uint32_t backgroundCount = 0u;
                for (uint32_t y = 112u; y <= 143u; ++y)
                {
                    for (uint32_t x = 22u; x <= 53u; ++x)
                    {
                        const size_t offset = static_cast<size_t>(y) * candidate.RowPitchBytes +
                                               static_cast<size_t>(x) * 4u;
                        targetSum += 0.2126 * candidate.Pixels[offset + 0u] +
                                     0.7152 * candidate.Pixels[offset + 1u] +
                                     0.0722 * candidate.Pixels[offset + 2u];
                        ++targetCount;
                    }
                    for (uint32_t x = 58u; x <= 89u; ++x)
                    {
                        const size_t offset = static_cast<size_t>(y) * candidate.RowPitchBytes +
                                               static_cast<size_t>(x) * 4u;
                        backgroundSum += 0.2126 * candidate.Pixels[offset + 0u] +
                                         0.7152 * candidate.Pixels[offset + 1u] +
                                         0.0722 * candidate.Pixels[offset + 2u];
                        ++backgroundCount;
                    }
                }
                if (targetCount != 1024u || backgroundCount != 1024u)
                {
                    outFailureReason = TEXT("object-presence ROI sample count is invalid");
                    return false;
                }
                const double targetMean = targetSum / static_cast<double>(targetCount);
                const double backgroundMean = backgroundSum / static_cast<double>(backgroundCount);
                const double delta = std::abs(targetMean - backgroundMean);
                std::cout << std::fixed << std::setprecision(9)
                          << "NORVESLIB_OBJECT_PRESENCE scene=" << SceneName(GetRunConfig().Scene)
                          << " target_mean_y8=" << targetMean
                          << " background_mean_y8=" << backgroundMean
                          << " delta_y8=" << delta << std::endl;
                if (!std::isfinite(targetMean) || !std::isfinite(backgroundMean) ||
                    !std::isfinite(delta) || targetMean < 0.0 || targetMean > 255.0 ||
                    backgroundMean < 0.0 || backgroundMean > 255.0 || delta < 8.0)
                {
                    outFailureReason = TEXT("object-presence Y8 delta is below 8");
                    return false;
                }
                PerceptualDifferenceMetrics visualMetrics;
                const PerceptualDiffStatus visualStatus = CompareLdrFlip(reference, candidate, visualMetrics);
                if (visualStatus != PerceptualDiffStatus::Success)
                {
                    outFailureReason = TEXT("LDR-FLIP measurement failed");
                    LOG_ERROR(
                        "golden visual measurement failed: scene=%s request=%llu frame=%llu status=%u",
                        SceneName(GetRunConfig().Scene),
                        static_cast<unsigned long long>(frame.RequestId),
                        static_cast<unsigned long long>(frame.FrameNumber),
                        static_cast<unsigned int>(visualStatus));
                    return false;
                }
                std::cout << std::fixed << std::setprecision(9)
                          << "NORVESLIB_VISUAL_MEASUREMENT scene=" << SceneName(GetRunConfig().Scene)
                          << " mean_flip=" << visualMetrics.MeanFlipError
                          << " max_flip=" << visualMetrics.MaxFlipError
                          << " raw_max=" << static_cast<unsigned int>(visualMetrics.Raw.MaxChannelDelta)
                          << std::endl;
                return true;
            }

            VisualGoldenThresholds thresholds;
            ArtificialDifferenceSpec artificialDifference;
            if (!LoadVisualGoldenThresholds(
                    ThresholdPath(), GetRunConfig().Scene, thresholds, artificialDifference))
            {
                outFailureReason = TEXT("visual threshold row is missing or invalid");
                LOG_ERROR(
                    "golden visual threshold load failed: scene=%s request=%llu frame=%llu",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber));
                return false;
            }

            PerceptualDifferenceMetrics metrics;
            const PerceptualDiffStatus visualStatus = CompareLdrFlip(reference, candidate, metrics);
            if (visualStatus != PerceptualDiffStatus::Success)
            {
                outFailureReason = TEXT("LDR-FLIP comparison failed");
                LOG_ERROR(
                    "golden visual comparison failed: scene=%s request=%llu frame=%llu status=%u",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    static_cast<unsigned int>(visualStatus));
                return false;
            }
            if (!MeetsVisualGoldenThresholds(metrics, thresholds))
            {
                outFailureReason = TEXT("golden image visual comparison failed");
                LOG_ERROR(
                    "golden visual mismatch: scene=%s request=%llu frame=%llu "
                    "mean_flip=%.9f mean_limit=%.9f max_flip=%.9f max_flip_coordinate=(%u,%u) "
                    "raw_max=%u raw_max_coordinate=(%u,%u) differing_pixels=%llu",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    metrics.MeanFlipError,
                    thresholds.MaximumMeanFlipError,
                    static_cast<double>(metrics.MaxFlipError),
                    metrics.MaxFlipX,
                    metrics.MaxFlipY,
                    static_cast<unsigned int>(metrics.Raw.MaxChannelDelta),
                    metrics.Raw.MaxDifferenceX,
                    metrics.Raw.MaxDifferenceY,
                    static_cast<unsigned long long>(metrics.Raw.DifferingPixelCount));
                return false;
            }

            std::cout << std::fixed << std::setprecision(9)
                      << "NORVESLIB_VISUAL_METRICS scene=" << SceneName(GetRunConfig().Scene)
                      << " mean_flip=" << metrics.MeanFlipError
                      << " max_flip=" << metrics.MaxFlipError
                      << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                      << std::endl;
            LOG_INFO(
                "golden image matched: scene=%s request=%llu frame=%llu mean_flip=%.9f raw_max=%u",
                SceneName(GetRunConfig().Scene),
                static_cast<unsigned long long>(frame.RequestId),
                static_cast<unsigned long long>(frame.FrameNumber),
                metrics.MeanFlipError,
                static_cast<unsigned int>(metrics.Raw.MaxChannelDelta));
            return true;
        }

        bool EvaluateR3DensityGoldenFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason)
        {
            if (m_bR3DensityStageApplyFailed || m_R3DensityStage >= R3DensityCount)
            {
                outFailureReason = TEXT("R3 density-sweep のcapture状態が不正です");
                return false;
            }
            if (frame.RequestId != GetLastAcceptedRequestId() ||
                (m_bR3DensityHasFrameNumber && frame.FrameNumber <= m_R3DensityLastFrameNumber))
            {
                outFailureReason = TEXT("R3 density-sweep のcapture順序が不正です");
                return false;
            }

            Core::Container::VariableArray<uint8_t> candidatePng;
            const GoldenImageStatus encodeStatus = EncodeCapturedFramePng(frame, candidatePng);
            if (encodeStatus != GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("R3 density-sweep のPNG encodeに失敗しました");
                return false;
            }
            Rgba8Image candidate;
            if (DecodePng(Core::Container::Span<const uint8_t>(candidatePng), candidate) !=
                GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("R3 density-sweep のPNG decodeに失敗しました");
                return false;
            }
            uint8_t maximumChannel = 0u;
            uint64_t saturatedPixelCount = 0u;
            for (uint32_t y = 0u; y < candidate.Height; ++y)
            {
                const size_t rowOffset = static_cast<size_t>(y) * candidate.RowPitchBytes;
                for (uint32_t x = 0u; x < candidate.Width; ++x)
                {
                    const size_t offset = rowOffset + static_cast<size_t>(x) * 4u;
                    maximumChannel = std::max(maximumChannel, candidate.Pixels[offset + 0u]);
                    maximumChannel = std::max(maximumChannel, candidate.Pixels[offset + 1u]);
                    maximumChannel = std::max(maximumChannel, candidate.Pixels[offset + 2u]);
                    if (candidate.Pixels[offset + 0u] == 255u ||
                        candidate.Pixels[offset + 1u] == 255u ||
                        candidate.Pixels[offset + 2u] == 255u)
                    {
                        ++saturatedPixelCount;
                    }
                }
            }
            if (saturatedPixelCount != 0u)
            {
                outFailureReason = TEXT("R3 density-sweep のBackBufferに飽和画素があります");
                return false;
            }

            if (m_bWriteR3DensityBaselineStaging)
            {
                EnsureR3BaselineStagingDirectories();
                const GoldenImageStatus saveStatus = SavePng(
                    R3DensityStagingPath(m_R3DensityStage),
                    Core::Container::Span<const uint8_t>(candidatePng));
                if (saveStatus != GoldenImageStatus::Success)
                {
                    outFailureReason = TEXT("R3 density-sweep baseline stagingの保存に失敗しました");
                    return false;
                }
                std::cout << std::fixed << std::setprecision(6)
                          << "R3_DENSITY_GOLDEN_STAGE case="
                          << R3DensityCaseName(m_R3DensityStage)
                          << " density=" << R3DensityValues[m_R3DensityStage]
                          << " baseline_staged=1 maximum_channel="
                          << static_cast<unsigned int>(maximumChannel)
                          << " saturated_rgb_pixels=" << saturatedPixelCount << "\n";
            }
            else
            {
                Rgba8Image reference;
                if (LoadPng(R3DensityBaselinePath(m_R3DensityStage), reference) !=
                    GoldenImageStatus::Success)
                {
                    outFailureReason = TEXT("R3 density-sweep baseline PNGを読み込めません");
                    return false;
                }
                VisualGoldenThresholds thresholds;
                if (!LoadR3DensityGoldenThresholds(thresholds))
                {
                    outFailureReason = TEXT("R3 density-sweep thresholdが不正です");
                    return false;
                }
                PerceptualDifferenceMetrics metrics;
                const PerceptualDiffStatus compareStatus =
                    CompareLdrFlip(reference, candidate, metrics);
                if (compareStatus != PerceptualDiffStatus::Success ||
                    !MeetsVisualGoldenThresholds(metrics, thresholds))
                {
                    outFailureReason = TEXT("R3 density-sweep golden比較が閾値を満たしません");
                    std::cout << std::fixed << std::setprecision(9)
                              << "R3_DENSITY_GOLDEN_STAGE case="
                              << R3DensityCaseName(m_R3DensityStage)
                              << " density=" << R3DensityValues[m_R3DensityStage]
                              << " mean_flip=" << metrics.MeanFlipError
                              << " mean_flip_max=" << thresholds.MaximumMeanFlipError
                              << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                              << " raw_max_limit="
                              << static_cast<unsigned int>(thresholds.MaximumChannelDelta)
                              << " maximum_channel=" << static_cast<unsigned int>(maximumChannel)
                              << " saturated_rgb_pixels=" << saturatedPixelCount
                              << " passed=0\n";
                    return false;
                }
                std::cout << std::fixed << std::setprecision(9)
                          << "R3_DENSITY_GOLDEN_STAGE case="
                          << R3DensityCaseName(m_R3DensityStage)
                          << " density=" << R3DensityValues[m_R3DensityStage]
                          << " mean_flip=" << metrics.MeanFlipError
                          << " mean_flip_max=" << thresholds.MaximumMeanFlipError
                          << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                          << " raw_max_limit="
                          << static_cast<unsigned int>(thresholds.MaximumChannelDelta)
                          << " maximum_channel=" << static_cast<unsigned int>(maximumChannel)
                          << " saturated_rgb_pixels=" << saturatedPixelCount
                          << " passed=1\n";
            }

            m_bR3DensityHasFrameNumber = true;
            m_R3DensityLastFrameNumber = frame.FrameNumber;
            ++m_R3DensityAcceptedCaseCount;
            if (m_R3DensityStage + 1u == R3DensityCount)
            {
                const bool bAllCasesAccepted = m_R3DensityAcceptedCaseCount == R3DensityCount;
                std::cout << "R3_DENSITY_GOLDEN_SWEEP="
                          << (bAllCasesAccepted ? "PASS" : "FAIL")
                          << " cases=" << m_R3DensityAcceptedCaseCount
                          << " mode=" << (m_bWriteR3DensityBaselineStaging ? "staging" : "compare")
                          << " non_saturated=1\n";
                if (!bAllCasesAccepted)
                {
                    outFailureReason = TEXT("R3 density-sweep の3段階goldenが完了しませんでした");
                    return false;
                }
            }
            return true;
        }

    private:
        bool m_bWriteBaselineStaging = false;
        bool m_bMeasureVisual = false;
        bool m_bR3DensityScenario = false;
        bool m_bWriteR3DensityBaselineStaging = false;
        bool m_bR3DensityStageApplyFailed = false;
        uint32_t m_R3DensityStage = 0u;
        uint32_t m_R3DensityAcceptedCaseCount = 0u;
        bool m_bR3DensityHasFrameNumber = false;
        uint64_t m_R3DensityLastFrameNumber = 0u;
    };

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<GoldenHandler>();
    }
}

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip("RenderingGoldenImageTest", "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip("RenderingGoldenImageTest", "no Vulkan device is available");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("Rendering Golden Image Validation");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = true;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("RenderingValidation.log");
    config.CreateHandler = &CreateHandler;
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
