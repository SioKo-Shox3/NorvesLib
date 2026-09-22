#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Engine/Engine.h"
#include "Math/Vector3.h"
#include "Rendering/CameraViewConstants.h"
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
        CameraMotion,
        CameraObjectMotion,
        FirstFrameInvalidHistory,
        MoveThenStop
    };

    enum class CaptureStage : uint8_t
    {
        Initial,
        Moved,
        Stable,
        Stopped,
        Complete
    };

    struct VelocityStats
    {
        float MaximumMagnitudeSquared = 0.0f;
        uint64_t NonFiniteCount = 0;
        uint64_t NonZeroCount = 0;
    };

    struct ExpectedVelocitySample
    {
        uint32_t PixelX = 0;
        uint32_t PixelY = 0;
        float ExpectedX = 0.0f;
        float ExpectedY = 0.0f;
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

    bool BuildExpectedVelocitySample(
        const CameraProxy& previousCamera,
        const CameraProxy& currentCamera,
        const Math::Vector3& previousWorldPoint,
        const Math::Vector3& currentWorldPoint,
        const RHI::IDevice* device,
        ExpectedVelocitySample& outSample)
    {
        const CameraViewConstants previousConstants = CameraViewConstants::BuildForDevice(
            previousCamera,
            static_cast<float>(ValidationWidth) / static_cast<float>(ValidationHeight),
            device);
        const CameraViewConstants currentConstants = CameraViewConstants::BuildForDevice(
            currentCamera,
            static_cast<float>(ValidationWidth) / static_cast<float>(ValidationHeight),
            device);
        const Math::Vector4 previousClip = previousConstants.ViewProjectionMatrix *
                                           Math::Vector4(previousWorldPoint.x,
                                                         previousWorldPoint.y,
                                                         previousWorldPoint.z,
                                                         1.0f);
        const Math::Vector4 currentClip = currentConstants.ViewProjectionMatrix *
                                          Math::Vector4(currentWorldPoint.x,
                                                        currentWorldPoint.y,
                                                        currentWorldPoint.z,
                                                        1.0f);
        if (!std::isfinite(previousClip.x) || !std::isfinite(previousClip.y) ||
            !std::isfinite(previousClip.w) || !std::isfinite(currentClip.x) ||
            !std::isfinite(currentClip.y) || !std::isfinite(currentClip.w) ||
            std::abs(previousClip.w) <= 1.0e-6f || std::abs(currentClip.w) <= 1.0e-6f)
        {
            return false;
        }

        const float previousNdcX = previousClip.x / previousClip.w;
        const float previousNdcY = previousClip.y / previousClip.w;
        const float currentNdcX = currentClip.x / currentClip.w;
        const float currentNdcY = currentClip.y / currentClip.w;
        const float pixelX = (currentNdcX * 0.5f + 0.5f) * static_cast<float>(ValidationWidth);
        if (!std::isfinite(previousNdcX) || !std::isfinite(previousNdcY) ||
            !std::isfinite(currentNdcX) ||
            !std::isfinite(currentNdcY) || !std::isfinite(pixelX) ||
            pixelX < 0.0f || pixelX >= static_cast<float>(ValidationWidth) ||
            std::abs(currentNdcY) > 0.02f)
        {
            return false;
        }

        outSample.PixelX = static_cast<uint32_t>(pixelX);
        outSample.PixelY = ValidationHeight / 2u;
        outSample.ExpectedX = (currentNdcX - previousNdcX) * 0.5f;
        outSample.ExpectedY = (currentNdcY - previousNdcY) * 0.5f;
        return std::isfinite(outSample.ExpectedX) && std::isfinite(outSample.ExpectedY);
    }

    bool ReadVelocitySample(
        const CapturedFrame& frame,
        const ExpectedVelocitySample& expected,
        float& outX,
        float& outY)
    {
        if (expected.PixelX >= frame.Width || expected.PixelY >= frame.Height)
        {
            return false;
        }
        const size_t offset = static_cast<size_t>(expected.PixelY) * frame.RowPitchBytes +
                              static_cast<size_t>(expected.PixelX) * 4u;
        if (offset + 4u > frame.Pixels.size())
        {
            return false;
        }
        outX = DecodeHalf(ReadHalf(frame.Pixels, offset));
        outY = DecodeHalf(ReadHalf(frame.Pixels, offset + 2u));
        return std::isfinite(outX) && std::isfinite(outY);
    }

    bool CheckExpectedVelocitySample(
        const CapturedFrame& frame,
        const ExpectedVelocitySample& expected,
        const char* label,
        Core::Container::String& outFailureReason)
    {
        float actualX = 0.0f;
        float actualY = 0.0f;
        if (!ReadVelocitySample(frame, expected, actualX, actualY))
        {
            outFailureReason = TEXT("analytic velocity sample is outside the captured image");
            return false;
        }

        constexpr float tolerance = 0.02f;
        std::cout << "velocity_sample=" << label
                  << " pixel=(" << expected.PixelX << "," << expected.PixelY << ")"
                  << " expected=(" << expected.ExpectedX << "," << expected.ExpectedY << ")"
                  << " actual=(" << actualX << "," << actualY << ")\n";
        if (std::abs(actualX - expected.ExpectedX) > tolerance ||
            std::abs(actualY - expected.ExpectedY) > tolerance)
        {
            outFailureReason = TEXT("analytic velocity sample exceeded the 0.02 tolerance");
            return false;
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

            if (IsMotionScenario())
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
            if (!m_bCaptureRequested && !RequestVelocityCapture(renderWorld))
            {
                Fail("velocity capture request was rejected");
                return;
            }
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

                    bool bRequestFollowup = false;
                    if (m_Scenario == Scenario::Static && m_Stage == CaptureStage::Initial)
                    {
                        m_Stage = CaptureStage::Stable;
                        bRequestFollowup = true;
                    }
                    else if ((m_Scenario == Scenario::CameraMotion ||
                              m_Scenario == Scenario::CameraObjectMotion ||
                              m_Scenario == Scenario::MoveThenStop) &&
                             m_Stage == CaptureStage::Initial)
                    {
                        m_Stage = CaptureStage::Moved;
                        bRequestFollowup = true;
                    }
                    else if (m_Scenario == Scenario::MoveThenStop &&
                             m_Stage == CaptureStage::Moved)
                    {
                        m_Stage = CaptureStage::Stopped;
                        bRequestFollowup = true;
                    }

                    if (bRequestFollowup)
                    {
                        ApplyStageState(renderWorld);
                        if (!RequestVelocityCapture(renderWorld))
                        {
                            Fail("velocity follow-up capture request was rejected");
                        }
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
            if (argument == TEXT("--scenario=camera-motion"))
            {
                m_Scenario = Scenario::CameraMotion;
                return true;
            }
            if (argument == TEXT("--scenario=first-frame-invalid-history"))
            {
                m_Scenario = Scenario::FirstFrameInvalidHistory;
                return true;
            }
            if (argument == TEXT("--scenario=move-then-stop"))
            {
                m_Scenario = Scenario::MoveThenStop;
                return true;
            }
            outFailureReason = TEXT("unsupported velocity scenario");
            return false;
        }

    private:
        bool RequestVelocityCapture(Core::Rendering::RenderWorld& renderWorld)
        {
            const Core::Rendering::FrameCaptureRequestResult request =
                renderWorld.RequestFrameCapture({FrameCaptureSourceKind::GBufferVelocity});
            if (!request.IsAccepted())
            {
                return false;
            }

            m_bCaptureRequested = true;
            m_CaptureRequestRenderedFrame = renderWorld.GetRenderedFrameCount();
            m_LastRequestId = request.RequestId;
            return true;
        }

        bool IsMotionScenario() const
        {
            return m_Scenario == Scenario::CameraMotion ||
                   m_Scenario == Scenario::CameraObjectMotion ||
                   m_Scenario == Scenario::MoveThenStop;
        }

        bool IsObjectMotionScenario() const
        {
            return m_Scenario == Scenario::CameraObjectMotion ||
                   m_Scenario == Scenario::MoveThenStop;
        }

        const char* GetStageName() const
        {
            switch (m_Stage)
            {
            case CaptureStage::Initial:
                return "initial";
            case CaptureStage::Moved:
                return "moved";
            case CaptureStage::Stable:
                return "stable";
            case CaptureStage::Stopped:
                return "stopped";
            case CaptureStage::Complete:
                return "complete";
            }
            return "unknown";
        }

        void ApplyStageState(Core::Rendering::RenderWorld& renderWorld)
        {
            if (!IsMotionScenario())
            {
                GetFixture().ApplyCamera(renderWorld);
                return;
            }

            const bool bMoved = m_Stage == CaptureStage::Moved ||
                                m_Stage == CaptureStage::Stopped;
            if (!GetFixture().SetR5RayTracingShadowReceiverPositionX(
                    IsObjectMotionScenario() && bMoved ? 1.5f : 0.0f))
            {
                Fail("motion fixture object update failed");
                return;
            }
            CameraProxy camera = GetFixture().GetR5RayTracingShadowCamera();
            camera.PositionX = bMoved ? 0.35f : 0.0f;
            renderWorld.SetMainCamera(camera);
        }

        bool EvaluateAnalyticMotion(
            const CapturedFrame& frame,
            Core::Rendering::RenderWorld& renderWorld,
            Core::Container::String& outFailureReason)
        {
            const auto device = renderWorld.GetRenderingCoordinator().GetDevice();
            if (!device)
            {
                outFailureReason = TEXT("analytic velocity sample has no RHI device");
                return false;
            }

            CameraProxy previousCamera = GetFixture().GetR5RayTracingShadowCamera();
            CameraProxy currentCamera = previousCamera;
            currentCamera.PositionX = 0.35f;

            const float currentObjectOffset = IsObjectMotionScenario() ? 1.5f : 0.0f;
            ExpectedVelocitySample receiverSample;
            if (!BuildExpectedVelocitySample(previousCamera,
                                             currentCamera,
                                             Math::Vector3(6.0f, 0.0f, 20.0f),
                                             Math::Vector3(6.0f + currentObjectOffset, 0.0f, 20.0f),
                                             device.get(),
                                             receiverSample) ||
                !CheckExpectedVelocitySample(frame,
                                              receiverSample,
                                              IsObjectMotionScenario()
                                                  ? "receiver-camera-and-object-motion"
                                                  : "receiver-camera-motion",
                                              outFailureReason))
            {
                return false;
            }

            ExpectedVelocitySample occluderSample;
            if (IsObjectMotionScenario() &&
                (!BuildExpectedVelocitySample(previousCamera,
                                              currentCamera,
                                              Math::Vector3(0.0f, 0.0f, 20.0f),
                                              Math::Vector3(1.5f, 0.0f, 20.0f),
                                              device.get(),
                                              occluderSample) ||
                 !CheckExpectedVelocitySample(frame,
                                               occluderSample,
                                               "receiver-center-camera-and-object-motion",
                                               outFailureReason)))
            {
                return false;
            }
            return true;
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
                      << GetStageName()
                      << " request=" << m_LastRequestId
                      << " frame=" << frame.FrameNumber
                      << " max_magnitude=" << std::sqrt(stats.MaximumMagnitudeSquared)
                      << " non_zero=" << stats.NonZeroCount
                      << " non_finite=" << stats.NonFiniteCount << "\n";

            if (stats.NonFiniteCount != 0u)
            {
                outFailureReason = TEXT("velocity readback contains non-finite values");
                return false;
            }

            if (IsMotionScenario() && m_Stage == CaptureStage::Moved)
            {
                if (stats.MaximumMagnitudeSquared <= 1.0e-6f || stats.NonZeroCount == 0u)
                {
                    outFailureReason = TEXT("camera/object motion did not produce velocity");
                    return false;
                }
                if (Core::Engine::GEngine == nullptr ||
                    !EvaluateAnalyticMotion(
                        frame,
                        Core::Engine::GEngine->GetRenderWorld(),
                        outFailureReason))
                {
                    return false;
                }
                return true;
            }

            if (m_Scenario == Scenario::MoveThenStop && m_Stage == CaptureStage::Stopped)
            {
                if (stats.MaximumMagnitudeSquared > 1.0e-8f || stats.NonZeroCount != 0u)
                {
                    outFailureReason = TEXT("velocity did not return to zero after motion stopped");
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
