#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"
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

    class HdrHandler final : public RenderingValidationApplicationHandler
    {
    public:
        Core::Rendering::FrameCaptureSourceKind GetCaptureSourceForTest() const
        {
            return GetRunConfig().CaptureSource;
        }

    protected:
        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason) override
        {
            if (frame.Format != RHI::Format::R16G16B16A16_FLOAT)
            {
                reason = TEXT("captured SceneColor format is not RGBA16F");
                return false;
            }

            RgbaFloatImage image;
            if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success)
            {
                reason = TEXT("captured SceneColor RGBA16F decode failed");
                return false;
            }
            if (image.Width != ValidationWidth || image.Height != ValidationHeight)
            {
                reason = TEXT("captured SceneColor dimensions are invalid");
                return false;
            }
            if (!IsFiniteImage(image))
            {
                const NonFiniteLocation location = FindFirstNonFinite(image);
                LOG_ERROR(
                    "HDR SceneColor contains a non-finite value: x=%u y=%u channel=%u kind=%u",
                    location.X,
                    location.Y,
                    location.Channel,
                    static_cast<unsigned int>(location.Kind));
                reason = TEXT("captured SceneColor contains a non-finite value");
                return false;
            }
            return true;
        }
    };

    bool ValidateCaptureSourceArgumentContract()
    {
        Core::Container::VariableArray<Core::Container::String> defaultArgs;
        defaultArgs.push_back(TEXT("--scene=indoor"));
        HdrHandler defaultHandler;
        if (!defaultHandler.OnPreInitialize(defaultArgs) ||
            defaultHandler.GetCaptureSourceForTest() !=
                Core::Rendering::FrameCaptureSourceKind::PresentationColor)
        {
            return false;
        }

        Core::Container::VariableArray<Core::Container::String> sceneColorArgs;
        sceneColorArgs.push_back(TEXT("--scene=outdoor"));
        sceneColorArgs.push_back(TEXT("--capture-source=scene-color"));
        HdrHandler sceneColorHandler;
        if (!sceneColorHandler.OnPreInitialize(sceneColorArgs) ||
            sceneColorHandler.GetCaptureSourceForTest() !=
                Core::Rendering::FrameCaptureSourceKind::SceneColor)
        {
            return false;
        }

        Core::Container::VariableArray<Core::Container::String> invalidArgs;
        invalidArgs.push_back(TEXT("--scene=indoor"));
        invalidArgs.push_back(TEXT("--capture-source=invalid"));
        HdrHandler invalidHandler;
        if (invalidHandler.OnPreInitialize(invalidArgs))
        {
            return false;
        }

        Core::Container::VariableArray<Core::Container::String> duplicateArgs;
        duplicateArgs.push_back(TEXT("--scene=indoor"));
        duplicateArgs.push_back(TEXT("--capture-source=presentation"));
        duplicateArgs.push_back(TEXT("--capture-source=scene-color"));
        HdrHandler duplicateHandler;
        return !duplicateHandler.OnPreInitialize(duplicateArgs);
    }

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<HdrHandler>();
    }
}

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    if (!ValidateCaptureSourceArgumentContract())
    {
        return 1;
    }

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip("RenderingHdrSceneCaptureTest", "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip("RenderingHdrSceneCaptureTest", "no Vulkan device is available");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("Rendering HDR SceneColor Validation");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = true;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("RenderingHdrSceneCapture.log");
    config.CreateHandler = &CreateHandler;
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
