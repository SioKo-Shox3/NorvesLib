#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingGoldenImage.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "Logging/LogMacros.h"

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

    class GoldenHandler final : public RenderingValidationApplicationHandler
    {
    protected:
        bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& outFailureReason) override
        {
            if (argument != TEXT("--write-baseline-staging"))
            {
                outFailureReason = TEXT("unsupported golden image argument");
                LOG_ERROR("golden image argument rejected: only --write-baseline-staging is supported");
                return false;
            }
            if (m_bWriteBaselineStaging)
            {
                outFailureReason = TEXT("duplicate --write-baseline-staging argument");
                LOG_ERROR("golden image argument rejected: --write-baseline-staging was duplicated");
                return false;
            }
            m_bWriteBaselineStaging = true;
            return true;
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

            RawImageDifferenceMetrics metrics;
            status = CompareRgba8(reference, candidate, metrics);
            const RawGoldenThresholds strictThresholds{0u, 0u};
            if (status != GoldenImageStatus::Success || !MeetsRawGoldenThresholds(metrics, strictThresholds))
            {
                outFailureReason = TEXT("golden image strict comparison failed");
                LOG_ERROR(
                    "golden image mismatch: scene=%s request=%llu frame=%llu status=%s "
                    "differing_pixels=%llu max_delta=%u max_coordinate=(%u,%u) mean_delta=%.9f",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    StatusName(status),
                    static_cast<unsigned long long>(metrics.DifferingPixelCount),
                    static_cast<unsigned int>(metrics.MaxChannelDelta),
                    metrics.MaxDifferenceX,
                    metrics.MaxDifferenceY,
                    metrics.MeanAbsoluteChannelDelta);
                return false;
            }

            LOG_INFO(
                "golden image matched: scene=%s request=%llu frame=%llu differing_pixels=0 max_delta=0",
                SceneName(GetRunConfig().Scene),
                static_cast<unsigned long long>(frame.RequestId),
                static_cast<unsigned long long>(frame.FrameNumber));
            return true;
        }

    private:
        bool m_bWriteBaselineStaging = false;
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
