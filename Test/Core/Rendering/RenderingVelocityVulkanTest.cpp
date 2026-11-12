#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Engine/Engine.h"
#include "Math/Vector3.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/VertexAnimation.h"
#include "RHI/RHITypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    enum class Scenario : uint8_t
    {
        Static,
        CameraMotion,
        ObjectMotion,
        AlphaClip,
        CameraObjectMotion,
        FirstFrameInvalidHistory,
        MoveThenStop,
        // 骨のアニメーションだけで動く骨付きの四角形（物体の変換とカメラは止まったまま）
        SkinnedMotion,
        VertexAnimation,
        VertexAnimationDrop
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

    // 骨付きの四角形の場面で、動いた後に置くアニメーションの時刻（秒）。骨は (0.5, 0, 0) m 動く。
    constexpr float SkinnedMovedTimeSeconds = 0.5f;
    constexpr uint32_t VertexAnimationMaximumCaptures = 96u;
    constexpr double VertexAnimationFrameSeconds = 1.0 / 60.0;

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

        constexpr float tolerance = 0.002f;
        std::cout << "velocity_sample=" << label
                  << " pixel=(" << expected.PixelX << "," << expected.PixelY << ")"
                  << " expected=(" << expected.ExpectedX << "," << expected.ExpectedY << ")"
                  << " actual=(" << actualX << "," << actualY << ")\n";
        if (std::abs(actualX - expected.ExpectedX) > tolerance ||
            std::abs(actualY - expected.ExpectedY) > tolerance)
        {
            outFailureReason = TEXT("analytic velocity sample exceeded the 0.002 tolerance");
            return false;
        }
        return true;
    }

    bool CheckZeroVelocitySample(
        const CapturedFrame& frame,
        uint32_t pixelX,
        uint32_t pixelY,
        const char* label,
        Core::Container::String& outFailureReason)
    {
        ExpectedVelocitySample expected;
        expected.PixelX = pixelX;
        expected.PixelY = pixelY;
        expected.ExpectedX = 0.0f;
        expected.ExpectedY = 0.0f;
        return CheckExpectedVelocitySample(frame, expected, label, outFailureReason);
    }

    // 板の中心を含む5×5画素を検査し、動く板は期待ベクトルに対する誤差を各画素で5%以内にする。
    bool CheckVertexAnimationRegion(
        const CapturedFrame& frame,
        const ExpectedVelocitySample& expected,
        const char* label,
        bool bIdentity,
        float time,
        float previousTime,
        Core::Container::String& outFailureReason)
    {
        float totalX = 0.0f;
        float totalY = 0.0f;
        float maximumError = 0.0f;
        const float expectedMagnitude = std::hypot(expected.ExpectedX, expected.ExpectedY);
        for (int32_t dy = -2; dy <= 2; ++dy)
        {
            for (int32_t dx = -2; dx <= 2; ++dx)
            {
                const int32_t x = static_cast<int32_t>(expected.PixelX) + dx;
                const int32_t y = static_cast<int32_t>(expected.PixelY) + dy;
                if (x < 0 || y < 0 || x >= static_cast<int32_t>(frame.Width) ||
                    y >= static_cast<int32_t>(frame.Height))
                {
                    outFailureReason = TEXT("頂点アニメのROIが画像の範囲外です");
                    return false;
                }
                ExpectedVelocitySample pixel = expected;
                pixel.PixelX = static_cast<uint32_t>(x);
                pixel.PixelY = static_cast<uint32_t>(y);
                float actualX = 0.0f;
                float actualY = 0.0f;
                if (!ReadVelocitySample(frame, pixel, actualX, actualY))
                {
                    outFailureReason = TEXT("頂点アニメのROIを読み取れません");
                    return false;
                }
                totalX += actualX;
                totalY += actualY;
                const float absoluteError = std::hypot(actualX - expected.ExpectedX, actualY - expected.ExpectedY);
                maximumError = std::max(maximumError, bIdentity ? absoluteError : absoluteError / expectedMagnitude);
            }
        }

        std::cout << std::setprecision(9) << "頂点アニメ ROI=" << label
                  << " 中心=(" << expected.PixelX << "," << expected.PixelY << ")"
                  << " 期待=(" << expected.ExpectedX << "," << expected.ExpectedY << ")"
                  << " 実測平均=(" << totalX / 25.0f << "," << totalY / 25.0f << ")";
        if (bIdentity)
        {
            std::cout << " 相対誤差=適用外 最大絶対誤差=" << maximumError;
        }
        else
        {
            std::cout << " 最大相対誤差=" << maximumError;
        }
        std::cout << " t=" << time << " tPrev=" << previousTime
                  << " GameTotalTime=" << frame.GameTotalTime
                  << " PreviousRenderedTotalTime=" << frame.PreviousRenderedTotalTime << "\n";

        if ((bIdentity && maximumError > 1.0e-4f) || (!bIdentity && maximumError > 0.05f))
        {
            outFailureReason = bIdentity
                ? TEXT("恒等の板のROIにゼロでないvelocityがあります")
                : TEXT("頂点アニメのROIのvelocityが解析解から5%を超えてずれました");
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

            if (m_Scenario == Scenario::AlphaClip)
            {
                if (GetRunConfig().Scene != SceneKind::Outdoor ||
                    !GetFixture().ApplyAlphaClipFixture(AlphaClipFixtureKind::Velocity))
                {
                    std::cerr << "RenderingVelocityVulkanTest: 切り抜きの場面を準備できなかった\n";
                    return false;
                }
            }
            else if (IsMotionScenario())
            {
                if (GetRunConfig().Scene != SceneKind::Outdoor ||
                    !GetFixture().ApplyR5RayTracingShadowFixture())
                {
                    std::cerr << (IsVertexAnimationScenario()
                        ? "RenderingVelocityVulkanTest: 頂点アニメの場面を用意できません\n"
                        : "RenderingVelocityVulkanTest: motion fixture setup failed\n");
                    return false;
                }
                if (m_Scenario == Scenario::SkinnedMotion && !GetFixture().AddR5SkinnedQuad())
                {
                    std::cerr << "RenderingVelocityVulkanTest: 骨付きの四角形を置けなかった\n";
                    return false;
                }
                if (IsVertexAnimationScenario())
                {
                    if (!GetFixture().ApplyR5VertexAnimationFixture())
                    {
                        std::cerr << "RenderingVelocityVulkanTest: 頂点アニメの板を用意できません\n";
                        return false;
                    }
                    Core::Engine::GEngine->GetRenderWorld().SetVertexAnimationTestDisplacementEnabled(true);
                }
            }
            return true;
        }

        bool ShouldAdvanceSimulation() const override
        {
            return IsVertexAnimationScenario() || !m_bCaptureRequested;
        }

        void OnPreRender() override
        {
            if (m_bExitRequested || Core::Engine::GEngine == nullptr)
            {
                return;
            }

            Core::Rendering::RenderWorld& renderWorld = Core::Engine::GEngine->GetRenderWorld();
            ApplyStageState(renderWorld);
            // 最初の履歴が揃うまで、頂点アニメの試験だけ要求を待つ。
            if (IsVertexAnimationScenario() && !m_bCaptureRequested && renderWorld.GetRenderedFrameCount() < 6u)
            {
                return;
            }
            if (!m_bCaptureRequested && !RequestVelocityCapture(renderWorld))
            {
                Fail(IsVertexAnimationScenario() ? "頂点アニメのvelocity取得要求が拒否されました"
                                                 : "velocity capture request was rejected");
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
                        std::cerr << (IsVertexAnimationScenario() ? "RenderingVelocityVulkanTest 失敗: "
                                                                   : "RenderingVelocityVulkanTest failed: ")
                                  << reason.c_str() << "\n";
                        Fail(IsVertexAnimationScenario() ? "頂点アニメのvelocity評価に失敗しました"
                                                         : "velocity capture evaluation failed");
                        return;
                    }

                    if (IsVertexAnimationScenario() && m_bRetryVertexAnimationCapture)
                    {
                        m_bRetryVertexAnimationCapture = false;
                        return;
                    }

                    bool bRequestFollowup = false;
                    if (m_Scenario == Scenario::Static && m_Stage == CaptureStage::Initial)
                    {
                        m_Stage = CaptureStage::Stable;
                        bRequestFollowup = true;
                    }
                    else if ((m_Scenario == Scenario::CameraMotion ||
                              m_Scenario == Scenario::ObjectMotion ||
                              m_Scenario == Scenario::AlphaClip ||
                              m_Scenario == Scenario::CameraObjectMotion ||
                              m_Scenario == Scenario::MoveThenStop ||
                              m_Scenario == Scenario::SkinnedMotion) &&
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

                    // 既定（on）が実際に解決を通ったこと、予備（off）が解決を通っていないことをログで確かめる。
                    // 装置が対応しないと既定も黙って予備へ落ち、速度の値だけでは区別できない
                    Core::Container::String pathReason;
                    if (!VerifyVisibilityBufferPath(pathReason))
                    {
                        std::cerr << (IsVertexAnimationScenario() ? "RenderingVelocityVulkanTest 失敗: "
                                                                   : "RenderingVelocityVulkanTest failed: ")
                                  << pathReason.c_str() << "\n";
                        Fail(IsVertexAnimationScenario() ? "ビジビリティバッファ経路の検査に失敗しました"
                                                         : "visibility buffer path check failed");
                        return;
                    }

                    m_Stage = CaptureStage::Complete;
                    m_bExitRequested = true;
                    Core::Engine::GEngine->RequestExit(0);
                    return;
                }

                if (renderedFrames >= m_CaptureRequestRenderedFrame + 32u)
                {
                    Fail(IsVertexAnimationScenario() ? "頂点アニメのvelocityを32フレーム以内に取得できませんでした"
                                                     : "velocity capture did not complete within 32 frames");
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
            if (argument == TEXT("--scenario=object-motion"))
            {
                m_Scenario = Scenario::ObjectMotion;
                return true;
            }
            if (argument == TEXT("--scenario=alpha-clip"))
            {
                m_Scenario = Scenario::AlphaClip;
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
            if (argument == TEXT("--scenario=skinned-motion"))
            {
                m_Scenario = Scenario::SkinnedMotion;
                return true;
            }
            if (argument == TEXT("--scenario=vertex-anim"))
            {
                m_Scenario = Scenario::VertexAnimation;
                return true;
            }
            if (argument == TEXT("--scenario=vertex-anim-drop"))
            {
                m_Scenario = Scenario::VertexAnimationDrop;
                return true;
            }
            // この2つのシナリオで内部的に追加する固定刻みをエンジンが読む。
            if (argument == TEXT("--fixed-frame-dt=1/60"))
            {
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

        bool IsCameraMotionScenario() const
        {
            return m_Scenario == Scenario::CameraMotion ||
                   m_Scenario == Scenario::CameraObjectMotion ||
                   m_Scenario == Scenario::MoveThenStop;
        }

        bool IsVertexAnimationScenario() const
        {
            return m_Scenario == Scenario::VertexAnimation || m_Scenario == Scenario::VertexAnimationDrop;
        }

        bool IsMotionScenario() const
        {
            return IsCameraMotionScenario() || m_Scenario == Scenario::ObjectMotion ||
                   m_Scenario == Scenario::AlphaClip || m_Scenario == Scenario::SkinnedMotion ||
                   IsVertexAnimationScenario();
        }

        bool IsObjectMotionScenario() const
        {
            return m_Scenario == Scenario::ObjectMotion ||
                   m_Scenario == Scenario::CameraObjectMotion ||
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

        CameraProxy GetAlphaClipVelocityCamera() const
        {
            CameraProxy camera = GetFixture().GetShadingModelGBufferCamera();
            camera.PositionY = AlphaClipVelocityPlate.Y;
            return camera;
        }

        void ApplyStageState(Core::Rendering::RenderWorld& renderWorld)
        {
            if (!IsMotionScenario())
            {
                GetFixture().ApplyCamera(renderWorld);
                return;
            }

            if (IsVertexAnimationScenario())
            {
                renderWorld.SetMainCamera(GetFixture().GetR5RayTracingShadowCamera());
                return;
            }

            const bool bMoved = m_Stage == CaptureStage::Moved ||
                                m_Stage == CaptureStage::Stopped;
            if (m_Scenario == Scenario::AlphaClip)
            {
                if (!GetFixture().SetAlphaClipVelocityPositionX(
                        AlphaClipVelocityPlate.X + (bMoved ? AlphaClipVelocityOffsetX : 0.0f)))
                {
                    Fail("切り抜きの板の位置を更新できなかった");
                    return;
                }
                renderWorld.SetMainCamera(GetAlphaClipVelocityCamera());
                return;
            }
            if (!GetFixture().SetR5RayTracingShadowReceiverPositionX(
                    IsObjectMotionScenario() && bMoved ? 1.5f : 0.0f))
            {
                Fail("motion fixture object update failed");
                return;
            }
            if (m_Scenario == Scenario::SkinnedMotion &&
                !GetFixture().SetR5SkinnedQuadAnimationTime(bMoved ? SkinnedMovedTimeSeconds : 0.0f))
            {
                Fail("骨付きの四角形のアニメーションの時刻を更新できなかった");
                return;
            }
            CameraProxy camera = GetFixture().GetR5RayTracingShadowCamera();
            camera.PositionX = IsCameraMotionScenario() && bMoved ? 0.35f : 0.0f;
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

            if (m_Scenario == Scenario::SkinnedMotion)
            {
                return EvaluateSkinnedMotion(frame, device.get(), outFailureReason);
            }
            if (m_Scenario == Scenario::AlphaClip)
            {
                return EvaluateAlphaClipMotion(frame, device.get(), outFailureReason);
            }

            const bool bCameraMotion = IsCameraMotionScenario();
            const bool bObjectMotion = IsObjectMotionScenario();
            CameraProxy previousCamera = GetFixture().GetR5RayTracingShadowCamera();
            CameraProxy currentCamera = previousCamera;
            currentCamera.PositionX = bCameraMotion ? 0.35f : 0.0f;

            const float currentObjectOffset = bObjectMotion ? 1.5f : 0.0f;
            ExpectedVelocitySample receiverSample;
            if (!BuildExpectedVelocitySample(previousCamera,
                                             currentCamera,
                                             Math::Vector3(6.0f, 0.0f, 20.0f),
                                             Math::Vector3(6.0f + currentObjectOffset, 0.0f, 20.0f),
                                             device.get(),
                                             receiverSample) ||
                !CheckExpectedVelocitySample(frame,
                                              receiverSample,
                                              bCameraMotion
                                                  ? (bObjectMotion
                                                         ? "receiver-camera-and-object-motion"
                                                         : "receiver-camera-motion")
                                                  : "receiver-object-motion",
                                              outFailureReason))
            {
                return false;
            }

            ExpectedVelocitySample centerSample;
            if (!BuildExpectedVelocitySample(previousCamera,
                                             currentCamera,
                                             Math::Vector3(0.0f, 0.0f, 20.0f),
                                             Math::Vector3(currentObjectOffset, 0.0f, 20.0f),
                                             device.get(),
                                             centerSample) ||
                !CheckExpectedVelocitySample(frame,
                                              centerSample,
                                              bCameraMotion
                                                  ? (bObjectMotion
                                                         ? "receiver-center-camera-and-object-motion"
                                                         : "receiver-center-camera-motion")
                                                  : "receiver-center-object-motion",
                                              outFailureReason))
            {
                return false;
            }

            if (!bCameraMotion && bObjectMotion &&
                !CheckZeroVelocitySample(frame,
                                         255u,
                                         0u,
                                         "background-object-motion",
                                         outFailureReason))
            {
                return false;
            }
            return true;
        }

        bool EvaluateAlphaClipMotion(
            const CapturedFrame& frame,
            const RHI::IDevice* device,
            Core::Container::String& outFailureReason)
        {
            const CameraProxy camera = GetAlphaClipVelocityCamera();
            ExpectedVelocitySample expected;
            if (!BuildExpectedVelocitySample(camera, camera,
                                             Math::Vector3(AlphaClipVelocityPlate.X, AlphaClipVelocityPlate.Y, 0.0f),
                                             Math::Vector3(AlphaClipVelocityPlate.X + AlphaClipVelocityOffsetX,
                                                           AlphaClipVelocityPlate.Y, 0.0f),
                                             device, expected))
            {
                outFailureReason = TEXT("切り抜きの板の期待速度を求められなかった");
                return false;
            }

            constexpr float motionTolerance = 0.002f;
            constexpr float staticToleranceSquared = 1.0e-8f;
            const float expectedMagnitudeSquared = expected.ExpectedX * expected.ExpectedX +
                                                   expected.ExpectedY * expected.ExpectedY;
            const float focal = static_cast<float>(ValidationHeight) * 0.5f /
                                std::tan(camera.FieldOfView * 3.14159265358979323846f / 360.0f);
            const float scale = focal / camera.PositionZ;
            const float halfExtentPixels = scale * AlphaClipVelocityPlate.HalfExtent;
            const float texelPixels = halfExtentPixels * 2.0f / 8.0f;
            if (texelPixels < 16.0f || expectedMagnitudeSquared <= 4.0f * motionTolerance * motionTolerance)
            {
                outFailureReason = TEXT("テクセルが小さいか、板の期待速度が背景と区別できない");
                return false;
            }

            struct SampleStats
            {
                uint32_t Count = 0u;
                float SumX = 0.0f;
                float SumY = 0.0f;
                float MaximumMagnitudeSquared = 0.0f;
            };
            SampleStats covered;
            SampleStats clipped;
            bool bPassed = true;
            // 8×8 の各テクセルの中心を調べ、境界での補間やラスタ化の影響を避ける。
            for (uint32_t y = 0u; y < 8u; ++y)
            {
                for (uint32_t x = 0u; x < 8u; ++x)
                {
                    const float worldX = AlphaClipVelocityPlate.X + AlphaClipVelocityOffsetX;
                    const float pixelX = ValidationWidth * 0.5f + scale * worldX +
                                         halfExtentPixels * ((static_cast<float>(x) + 0.5f) / 4.0f - 1.0f);
                    const float pixelY = ValidationHeight * 0.5f -
                                         scale * (AlphaClipVelocityPlate.Y - camera.PositionY) -
                                         halfExtentPixels * ((static_cast<float>(y) + 0.5f) / 4.0f - 1.0f);
                    if (pixelX < 0.0f || pixelY < 0.0f || pixelX >= ValidationWidth || pixelY >= ValidationHeight)
                    {
                        outFailureReason = TEXT("切り抜きのテクセル中心が画像の範囲外です");
                        return false;
                    }
                    ExpectedVelocitySample pixel = expected;
                    pixel.PixelX = static_cast<uint32_t>(pixelX);
                    pixel.PixelY = static_cast<uint32_t>(pixelY);
                    float actualX = 0.0f;
                    float actualY = 0.0f;
                    if (!ReadVelocitySample(frame, pixel, actualX, actualY))
                    {
                        outFailureReason = TEXT("切り抜きのテクセル中心の速度を読めなかった");
                        return false;
                    }
                    const float magnitudeSquared = actualX * actualX + actualY * actualY;
                    const bool bCovered = (x + y) % 2u == 0u;
                    SampleStats& stats = bCovered ? covered : clipped;
                    ++stats.Count;
                    stats.SumX += actualX;
                    stats.SumY += actualY;
                    stats.MaximumMagnitudeSquared = std::max(stats.MaximumMagnitudeSquared, magnitudeSquared);
                    const float differenceX = actualX - expected.ExpectedX;
                    const float differenceY = actualY - expected.ExpectedY;
                    const bool bSamplePassed = bCovered
                        ? std::abs(differenceX) <= motionTolerance && std::abs(differenceY) <= motionTolerance
                        : magnitudeSquared <= staticToleranceSquared &&
                              differenceX * differenceX + differenceY * differenceY >
                                  motionTolerance * motionTolerance;
                    if (!bSamplePassed)
                    {
                        std::cerr << "速度の不一致: テクセル=(" << x << "," << y << ") 被覆="
                                  << bCovered << " 画素=(" << pixel.PixelX << "," << pixel.PixelY
                                  << ") 速度=(" << actualX << "," << actualY << ")\n";
                        bPassed = false;
                    }
                }
            }
            std::cout << "切り抜き速度: 被覆数=" << covered.Count
                      << " 平均=(" << covered.SumX / covered.Count << "," << covered.SumY / covered.Count << ")"
                      << " 最大=" << std::sqrt(covered.MaximumMagnitudeSquared)
                      << " 切り抜き数=" << clipped.Count
                      << " 平均=(" << clipped.SumX / clipped.Count << "," << clipped.SumY / clipped.Count << ")"
                      << " 最大=" << std::sqrt(clipped.MaximumMagnitudeSquared)
                      << " 期待=(" << expected.ExpectedX << "," << expected.ExpectedY << ")"
                      << " テクセル画素=" << texelPixels << "\n";
            if (!bPassed || covered.Count != 32u || clipped.Count != 32u)
            {
                outFailureReason = TEXT("被覆または切り抜きのテクセルの速度が期待値と一致しない");
                return false;
            }
            return true;
        }

        // 骨の平行移動 (0.5, 0, 0) m だけで動いた四角形の中の点は、前のパレットで動かした位置からの動きを
        // velocity に持つ。前のパレットが使われなければ0になる。止まった受け手は0のまま。
        bool EvaluateSkinnedMotion(
            const CapturedFrame& frame,
            const RHI::IDevice* device,
            Core::Container::String& outFailureReason)
        {
            const CameraProxy& camera = GetFixture().GetR5RayTracingShadowCamera();
            ExpectedVelocitySample skinnedSample;
            if (!BuildExpectedVelocitySample(camera,
                                             camera,
                                             Math::Vector3(-3.0f, 0.0f, 10.0f),
                                             Math::Vector3(-3.0f + SkinnedMovedTimeSeconds, 0.0f, 10.0f),
                                             device,
                                             skinnedSample) ||
                !CheckExpectedVelocitySample(frame, skinnedSample, "skinned-bone-motion", outFailureReason))
            {
                return false;
            }
            if (std::abs(skinnedSample.ExpectedX) <= 0.002f)
            {
                outFailureReason = TEXT("骨付きの四角形の動きの期待値が小さすぎて0と区別できない");
                return false;
            }

            ExpectedVelocitySample receiverSample;
            if (!BuildExpectedVelocitySample(camera,
                                             camera,
                                             Math::Vector3(3.0f, 0.0f, 20.0f),
                                             Math::Vector3(3.0f, 0.0f, 20.0f),
                                             device,
                                             receiverSample) ||
                !CheckExpectedVelocitySample(frame, receiverSample, "receiver-static-skinned-motion", outFailureReason))
            {
                return false;
            }
            return true;
        }

        bool EvaluateVertexAnimation(
            const CapturedFrame& frame,
            Core::Container::String& outFailureReason)
        {
            ++m_VertexAnimationCaptures;
            RenderExtensionFrameInfo frameInfo{};
            frameInfo.GameTotalTime = frame.GameTotalTime;
            frameInfo.PreviousRenderedTotalTime = frame.PreviousRenderedTotalTime;
            float times[4]{};
            MakeVertexAnimationTimeConstants(frameInfo, true, times);
            const double delta = frame.GameTotalTime - frame.PreviousRenderedTotalTime;
            if (!std::isfinite(delta) || !std::isfinite(times[0]) || !std::isfinite(times[1]))
            {
                outFailureReason = TEXT("頂点アニメの時刻が有限値ではありません");
                return false;
            }

            auto retry = [&](const char* reason) -> bool
            {
                std::cout << std::setprecision(9) << "頂点アニメの画を取り直します: " << reason
                          << " 試行=" << m_VertexAnimationCaptures << " t=" << times[0]
                          << " tPrev=" << times[1] << " 差=" << delta << "\n";
                if (m_VertexAnimationCaptures >= VertexAnimationMaximumCaptures)
                {
                    outFailureReason = TEXT("頂点アニメの有効な画を上限回数内に取得できませんでした");
                    return false;
                }
                m_bRetryVertexAnimationCapture = true;
                return true;
            };
            if (delta <= 0.0 || times[0] <= times[1])
            {
                return retry("時刻が進んでいません");
            }
            if (m_Scenario == Scenario::VertexAnimationDrop && delta <= 1.5 * VertexAnimationFrameSeconds)
            {
                return retry("フレームの欠落を跨いでいません");
            }

            const auto device = Core::Engine::GEngine->GetRenderWorld().GetRenderingCoordinator().GetDevice();
            if (!device)
            {
                outFailureReason = TEXT("頂点アニメの解析解に必要なRHIデバイスがありません");
                return false;
            }
            const CameraProxy& camera = GetFixture().GetR5RayTracingShadowCamera();
            constexpr float angularFrequency = 2.0f * std::numbers::pi_v<float> * VertexAnimationTestSineFrequencyHz;
            const float currentOffset = VertexAnimationTestSineAmplitudeMeters * std::sin(angularFrequency * times[0]);
            const float previousOffset = VertexAnimationTestSineAmplitudeMeters * std::sin(angularFrequency * times[1]);
            ExpectedVelocitySample staticSample;
            ExpectedVelocitySample skinnedSample;
            ExpectedVelocitySample identitySample;
            if (!BuildExpectedVelocitySample(camera, camera, Math::Vector3(previousOffset, 0.0f, 8.0f),
                                             Math::Vector3(currentOffset, 0.0f, 8.0f), device.get(), staticSample) ||
                !BuildExpectedVelocitySample(camera, camera, Math::Vector3(-3.0f + previousOffset, 0.0f, 8.0f),
                                             Math::Vector3(-3.0f + currentOffset, 0.0f, 8.0f), device.get(), skinnedSample) ||
                !BuildExpectedVelocitySample(camera, camera, Math::Vector3(3.0f, 0.0f, 8.0f),
                                             Math::Vector3(3.0f, 0.0f, 8.0f), device.get(), identitySample))
            {
                outFailureReason = TEXT("頂点アニメの板の解析解を画像へ投影できません");
                return false;
            }
            const float expectedPixels = std::hypot(staticSample.ExpectedX * static_cast<float>(ValidationWidth),
                                                    staticSample.ExpectedY * static_cast<float>(ValidationHeight));
            if (expectedPixels < 2.0f)
            {
                return retry("期待される移動が2画素未満です");
            }
            return CheckVertexAnimationRegion(frame, staticSample, "static", false, times[0], times[1], outFailureReason) &&
                   CheckVertexAnimationRegion(frame, skinnedSample, "skinned", false, times[0], times[1], outFailureReason) &&
                   CheckVertexAnimationRegion(frame, identitySample, "identity", true, times[0], times[1], outFailureReason);
        }

        bool EvaluateVelocityFrame(
            const CapturedFrame& frame,
            Core::Container::String& outFailureReason)
        {
            VelocityStats stats;
            if (!AnalyzeVelocity(frame, stats))
            {
                outFailureReason = IsVertexAnimationScenario()
                    ? TEXT("取得したvelocityがR16G16_FLOATの256×256画像ではありません")
                    : TEXT("captured velocity storage is not R16G16_FLOAT 256x256");
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
                outFailureReason = IsVertexAnimationScenario()
                    ? TEXT("取得したvelocityに有限値でない値があります")
                    : TEXT("velocity readback contains non-finite values");
                return false;
            }

            if (IsVertexAnimationScenario())
            {
                return EvaluateVertexAnimation(frame, outFailureReason);
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
        bool m_bRetryVertexAnimationCapture = false;
        uint32_t m_VertexAnimationCaptures = 0u;
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
    bool bVertexAnimation = false;
    bool bVertexAnimationDrop = false;
    for (int index = 1; index < argc; ++index)
    {
        const Core::Container::String argument(argv[index]);
        bVertexAnimation = bVertexAnimation || argument == TEXT("--scenario=vertex-anim");
        bVertexAnimationDrop = bVertexAnimationDrop || argument == TEXT("--scenario=vertex-anim-drop");
        config.Arguments.push_back(argument);
    }
    if (bVertexAnimation || bVertexAnimationDrop)
    {
        // 1フレームの長さを固定し、欠落の判定と解析解を同じ時刻基準で行う。
        config.Arguments.push_back(TEXT("--fixed-frame-dt=1/60"));
        config.bEnableMultiThreadedRendering = bVertexAnimationDrop;
    }
    return Core::Boot::LaunchApplication(config);
}
