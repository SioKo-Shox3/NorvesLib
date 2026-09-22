#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Engine/Engine.h"
#include "Rendering/RenderWorld.h"
#include "RHI/RHITypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    enum class Scenario : uint8_t
    {
        Static,
        CameraObjectMotion,
        FirstFrameInvalidHistory
    };

    enum class CaptureStage : uint8_t
    {
        Initial,
        Moved,
        Complete
    };

    struct VelocityStats
    {
        float MaximumMagnitudeSquared = 0.0f;
        uint64_t NonFiniteCount = 0;
        uint64_t NonZeroCount = 0;
    };

    uint16_t ReadHalf(const Core::Container::VariableArray<uint8_t>& pixels, size_t offset)
    {
        return static_cast<uint16_t>(pixels[offset]) |
            static_cast<uint16_t>(pixels[offset + 1u] << 8u);
    }

    float DecodeHalf(uint16_t bits)
    {
        const bool negative = (bits & 0x8000u) != 0u;
        const uint32_t exponent = (bits >> 10u) & 0x1Fu;
        const uint32_t mantissa = bits & 0x03FFu;
        float value = 0.0f;
        if (exponent == 0u)
        {
            value = std::ldexp(static_cast<float>(mantissa), -24);
        }
        else if (exponent == 0x1Fu)
        {
            value = mantissa == 0u
                ? std::numeric_limits<float>::infinity()
                : std::numeric_limits<float>::quiet_NaN();
        }
        else
        {
            value = std::ldexp(
                1.0f + static_cast<float>(mantissa) / 1024.0f,
                static_cast<int>(exponent) - 15);
        }
        return negative ? -value : value;
    }

    bool AnalyzeVelocity(const CapturedFrame& frame, VelocityStats& outStats)
    {
        outStats = VelocityStats{};
        if (!frame.IsSuccess() || frame.Format != RHI::Format::R16G16_FLOAT ||
            frame.Width != ValidationWidth || frame.Height != ValidationHeight ||
            frame.BytesPerPixel != 4u ||
            frame.RowPitchBytes != ValidationWidth * 4u ||
            frame.Pixels.size() != static_cast<size_t>(ValidationWidth) *
                                      static_cast<size_t>(ValidationHeight) * 4u)
        {
            return false;
        }

        for (size_t offset = 0; offset < frame.Pixels.size(); offset += 4u)
        {
            const float velocityX = DecodeHalf(ReadHalf(frame.Pixels, offset));
            const float velocityY = DecodeHalf(ReadHalf(frame.Pixels, offset + 2u));
            if (!std::isfinite(velocityX) || !std::isfinite(velocityY))
            {
                ++outStats.NonFiniteCount;
                continue;
            }

            const float magnitudeSquared = velocityX * velocityX + velocityY * velocityY;
            outStats.MaximumMagnitudeSquared = std::max(
                outStats.MaximumMagnitudeSquared,
                magnitudeSquared);
            if (magnitudeSquared > 1.0e-8f)
            {
                ++outStats.NonZeroCount;
            }
        }
        return true;
    }

    class VelocityHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnPreInitialize(
            const Core::Container::VariableArray<Core::Container::String>& args) override
        {
            m_Scenario = Scenario::Static;
            m_Stage = CaptureStage::Initial;
            m_bCaptureRequested = false;
            m_bExitRequested = false;
            return RenderingValidationApplicationHandler::OnPreInitialize(args);
        }

        bool OnInitialize() override
        {
            if (!RenderingValidationApplicationHandler::OnInitialize())
            {
                return false;
            }

            if (m_Scenario == Scenario::CameraObjectMotion)
            {
                if (GetRunConfig().Scene != SceneKind::Outdoor ||
                    !GetFixture().ApplyR5RayTracingShadowFixture())
                {
                    std::cerr << "RenderingVelocityVulkanTest: motion fixture setup failed\n";
                    return false;
                }
            }
            return true;
        }

        bool ShouldAdvanceSimulation() const override
        {
            return !m_bCaptureRequested;
        }

        void OnPreRender() override
        {
            if (m_bExitRequested || Core::Engine::GEngine == nullptr)
            {
                return;
            }

            Core::Rendering::RenderWorld& renderWorld = Core::Engine::GEngine->GetRenderWorld();
            ApplyStageState(renderWorld);
            if (m_bCaptureRequested)
            {
                return;
            }

            const Core::Rendering::FrameCaptureRequestResult request =
                renderWorld.RequestFrameCapture({FrameCaptureSourceKind::GBufferVelocity});
            if (!request.IsAccepted())
            {
                Fail("velocity capture request was rejected");
                return;
            }

            m_bCaptureRequested = true;
            m_CaptureRequestRenderedFrame = renderWorld.GetRenderedFrameCount();
            m_LastRequestId = request.RequestId;
        }

        void OnPostRender() override
        {
            if (m_bExitRequested || Core::Engine::GEngine == nullptr)
            {
                return;
            }

            Core::Rendering::RenderWorld& renderWorld = Core::Engine::GEngine->GetRenderWorld();
            const uint64_t renderedFrames = renderWorld.GetRenderedFrameCount();
            if (m_bCaptureRequested)
            {
                Core::Rendering::CapturedFrame frame;
                if (renderWorld.TryConsumeCapturedFrame(frame))
                {
                    m_bCaptureRequested = false;
                    Core::Container::String reason;
                    if (!EvaluateVelocityFrame(frame, reason))
                    {
                        std::cerr << "RenderingVelocityVulkanTest failed: "
                                  << reason.c_str() << "\n";
                        Fail("velocity capture evaluation failed");
                        return;
                    }

                    if (m_Scenario == Scenario::CameraObjectMotion &&
                        m_Stage == CaptureStage::Initial)
                    {
                        m_Stage = CaptureStage::Moved;
                        ApplyStageState(renderWorld);
                        const Core::Rendering::FrameCaptureRequestResult followup =
                            renderWorld.RequestFrameCapture({FrameCaptureSourceKind::GBufferVelocity});
                        if (!followup.IsAccepted())
                        {
                            Fail("velocity follow-up capture request was rejected");
                            return;
                        }
                        m_bCaptureRequested = true;
                        m_CaptureRequestRenderedFrame = renderedFrames;
                        m_LastRequestId = followup.RequestId;
                        return;
                    }

                    m_Stage = CaptureStage::Complete;
                    m_bExitRequested = true;
                    Core::Engine::GEngine->RequestExit(0);
                    return;
                }

                if (renderedFrames >= m_CaptureRequestRenderedFrame + 32u)
                {
                    Fail("velocity capture did not complete within 32 frames");
                }
            }
        }

        void OnPreShutdown() override
        {
            RenderingValidationApplicationHandler::OnPreShutdown();
        }

    protected:
        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason) override
        {
            return EvaluateVelocityFrame(frame, outFailureReason);
        }

        bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& outFailureReason) override
        {
            if (argument == TEXT("--scenario=static"))
            {
                m_Scenario = Scenario::Static;
                return true;
            }
            if (argument == TEXT("--scenario=camera-object-motion"))
            {
                m_Scenario = Scenario::CameraObjectMotion;
                return true;
            }
            if (argument == TEXT("--scenario=first-frame-invalid-history"))
            {
                m_Scenario = Scenario::FirstFrameInvalidHistory;
                return true;
            }
            outFailureReason = TEXT("unsupported velocity scenario");
            return false;
        }

    private:
        void ApplyStageState(Core::Rendering::RenderWorld& renderWorld)
        {
            if (m_Scenario != Scenario::CameraObjectMotion)
            {
                GetFixture().ApplyCamera(renderWorld);
                return;
            }

            const bool bMoved = m_Stage == CaptureStage::Moved;
            if (!GetFixture().SetR5RayTracingShadowOccluderPositionX(bMoved ? 1.5f : 0.0f))
            {
                Fail("motion fixture object update failed");
                return;
            }
            CameraProxy camera = GetFixture().GetR5RayTracingShadowCamera();
            camera.PositionX = bMoved ? 0.35f : 0.0f;
            renderWorld.SetMainCamera(camera);
        }

        bool EvaluateVelocityFrame(
            const CapturedFrame& frame,
            Core::Container::String& outFailureReason)
        {
            VelocityStats stats;
            if (!AnalyzeVelocity(frame, stats))
            {
                outFailureReason = TEXT("captured velocity storage is not R16G16_FLOAT 256x256");
                return false;
            }
            std::cout << "velocity_stage="
                      << (m_Stage == CaptureStage::Initial ? "initial" : "moved")
                      << " request=" << m_LastRequestId
                      << " max_magnitude=" << std::sqrt(stats.MaximumMagnitudeSquared)
                      << " non_zero=" << stats.NonZeroCount
                      << " non_finite=" << stats.NonFiniteCount << "\n";

            if (stats.NonFiniteCount != 0u)
            {
                outFailureReason = TEXT("velocity readback contains non-finite values");
                return false;
            }

            if (m_Scenario == Scenario::CameraObjectMotion && m_Stage == CaptureStage::Moved)
            {
                if (stats.MaximumMagnitudeSquared <= 1.0e-6f || stats.NonZeroCount == 0u)
                {
                    outFailureReason = TEXT("camera/object motion did not produce velocity");
                    return false;
                }
                return true;
            }

            if (stats.MaximumMagnitudeSquared > 1.0e-8f || stats.NonZeroCount != 0u)
            {
                outFailureReason = TEXT("initial or static velocity must be zero");
                return false;
            }
            return true;
        }

        void Fail(const char* message)
        {
            if (!m_bExitRequested && Core::Engine::GEngine != nullptr)
            {
                std::cerr << "RenderingVelocityVulkanTest: " << message << "\n";
                m_bExitRequested = true;
                Core::Engine::GEngine->RequestExit(1);
            }
        }

        Scenario m_Scenario = Scenario::Static;
        CaptureStage m_Stage = CaptureStage::Initial;
        bool m_bCaptureRequested = false;
        bool m_bExitRequested = false;
        uint64_t m_CaptureRequestRenderedFrame = 0;
        uint64_t m_LastRequestId = 0;
    };

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<VelocityHandler>();
    }
}

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip("RenderingVelocityVulkanTest", "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip("RenderingVelocityVulkanTest", "no Vulkan device is available");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("Rendering Velocity Validation");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = true;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("RenderingVelocity.log");
    config.CreateHandler = &CreateHandler;
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
