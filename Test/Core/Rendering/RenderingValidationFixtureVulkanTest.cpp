#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    class SmokeHandler final : public RenderingValidationApplicationHandler
    {
    protected:
        bool EvaluateCapturedFrame(const Core::Rendering::CapturedFrame& frame,
                                   Core::Container::String& reason) override
        {
            const bool bValid = frame.IsSuccess() && frame.Width == ValidationWidth &&
                                frame.Height == ValidationHeight && frame.BytesPerPixel == 4u &&
                                frame.Pixels.size() == ValidationWidth * ValidationHeight * 4u;
            if (!bValid)
            {
                reason = TEXT("captured frame dimensions or storage are invalid");
            }
            return bValid;
        }
    };

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<SmokeHandler>();
    }
}

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip("RenderingValidationFixtureVulkanTest", "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip("RenderingValidationFixtureVulkanTest", "no Vulkan device is available");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("Rendering Validation Fixture");
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
