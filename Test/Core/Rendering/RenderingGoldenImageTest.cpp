#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingGoldenImage.h"
#include "RenderingValidation/RenderingPerceptualDiff.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "Logging/LogMacros.h"

#include <iomanip>
#include <iostream>

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

    class GoldenHandler final : public RenderingValidationApplicationHandler
    {
    protected:
        bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& outFailureReason) override
        {
            if (argument == TEXT("--write-baseline-staging"))
            {
                if (m_bWriteBaselineStaging || m_bMeasureVisual)
                {
                    outFailureReason = TEXT("duplicate or conflicting golden image mode");
                    LOG_ERROR("golden image argument rejected: duplicate or conflicting mode");
                    return false;
                }
                m_bWriteBaselineStaging = true;
                return true;
            }
            if (argument == TEXT("--measure-visual"))
            {
                if (m_bMeasureVisual || m_bWriteBaselineStaging)
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

        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason) override
        {
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

    private:
        bool m_bWriteBaselineStaging = false;
        bool m_bMeasureVisual = false;
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
