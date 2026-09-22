// R6 RTGIの静止・移動・ライト追従・fallbackをHDR captureで検証する。
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/RenderWorld.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "R6RTGIAcceptanceVulkanTest";
    constexpr uint32_t WarmupRenderedFrames = 8u;
    constexpr uint32_t LightDeadlineRenderedFrames = 4u;
    constexpr double MinimumFallbackDifference = 0.0005;
    constexpr double MinimumMotionDifference = 0.0005;
    constexpr double MaximumStoppedDifference = 0.02;
    constexpr double MaximumStoppedFrameDifference = 0.02;
    constexpr double MinimumLightChange = 0.00025;
    constexpr double MinimumLightProgress = 0.80;
    constexpr float R6PointLightIntensity = 1200.0f;

    struct ImageMetrics
    {
        double MeanY = 0.0;
        double CenterY = 0.0;
        double CenterRed = 0.0;
        double CenterGreen = 0.0;
        double CenterBlue = 0.0;
        double MaximumY = 0.0;
    };

    struct StaticGolden
    {
        double MeanY = 0.0;
        double CenterY = 0.0;
        double CenterRed = 0.0;
        double CenterGreen = 0.0;
        double CenterBlue = 0.0;
        double Tolerance = 0.0;
        bool bValid = false;
    };

    enum class CaptureStage : uint8_t
    {
        IblFallback,
        R4FallbackWarmup,
        RTGIWarmup,
        StaticGolden,
        CameraMoved,
        ObjectMoved,
        MoveStopped,
        LightMoved,
        Complete
    };

    bool ParseGoldenValue(const char* key, double value, StaticGolden& golden)
    {
        if (std::strcmp(key, "mean_y") == 0)
        {
            golden.MeanY = value;
        }
        else if (std::strcmp(key, "center_y") == 0)
        {
            golden.CenterY = value;
        }
        else if (std::strcmp(key, "center_red") == 0)
        {
            golden.CenterRed = value;
        }
        else if (std::strcmp(key, "center_green") == 0)
        {
            golden.CenterGreen = value;
        }
        else if (std::strcmp(key, "center_blue") == 0)
        {
            golden.CenterBlue = value;
        }
        else if (std::strcmp(key, "tolerance") == 0)
        {
            golden.Tolerance = value;
        }
        else
        {
            return false;
        }
        return std::isfinite(value);
    }

    bool LoadStaticGolden(StaticGolden& outGolden)
    {
        std::ifstream input(
            NORVES_SOURCE_ROOT "/Docs/RenderingValidation/R6RTGIStaticGolden.tsv");
        if (!input)
        {
            std::cerr << "R6静止goldenを開けませんでした\n";
            return false;
        }

        bool bSchema = false;
        bool bMeanY = false;
        bool bCenterY = false;
        bool bCenterRed = false;
        bool bCenterGreen = false;
        bool bCenterBlue = false;
        bool bTolerance = false;
        char line[256] = {};
        while (input.getline(line, sizeof(line)))
        {
            if (static_cast<unsigned char>(line[0]) == 0xEFu &&
                static_cast<unsigned char>(line[1]) == 0xBBu &&
                static_cast<unsigned char>(line[2]) == 0xBFu)
            {
                std::memmove(line, line + 3, std::strlen(line + 3) + 1u);
            }
            if (line[0] == '\0' || line[0] == '#')
            {
                continue;
            }
            char* separator = std::strchr(line, '\t');
            if (separator == nullptr)
            {
                return false;
            }
            *separator = '\0';
            char* valueText = separator + 1;
            size_t valueLength = std::strlen(valueText);
            while (valueLength > 0u &&
                   (valueText[valueLength - 1u] == '\r' ||
                    valueText[valueLength - 1u] == ' ' ||
                    valueText[valueLength - 1u] == '\t'))
            {
                valueText[--valueLength] = '\0';
            }
            char* valueEnd = nullptr;
            const double value = std::strtod(valueText, &valueEnd);
            if (valueEnd == valueText || *valueEnd != '\0')
            {
                if (std::strcmp(line, "schema") == 0 &&
                    std::strcmp(valueText,
                                "NorvesLib.RenderingValidation.R6RTGIStaticGolden.v1") == 0)
                {
                    bSchema = true;
                    continue;
                }
                return false;
            }
            if (std::strcmp(line, "mean_y") == 0)
            {
                bMeanY = true;
            }
            else if (std::strcmp(line, "center_y") == 0)
            {
                bCenterY = true;
            }
            else if (std::strcmp(line, "center_red") == 0)
            {
                bCenterRed = true;
            }
            else if (std::strcmp(line, "center_green") == 0)
            {
                bCenterGreen = true;
            }
            else if (std::strcmp(line, "center_blue") == 0)
            {
                bCenterBlue = true;
            }
            else if (std::strcmp(line, "tolerance") == 0)
            {
                bTolerance = true;
            }
            else
            {
                return false;
            }
            if (!ParseGoldenValue(line, value, outGolden))
            {
                return false;
            }
        }
        outGolden.bValid = bSchema && bMeanY && bCenterY && bCenterRed &&
                           bCenterGreen && bCenterBlue && bTolerance &&
                           outGolden.Tolerance > 0.0;
        return outGolden.bValid;
    }

    double Luma(float red, float green, float blue)
    {
        return 0.2126 * static_cast<double>(red) +
               0.7152 * static_cast<double>(green) +
               0.0722 * static_cast<double>(blue);
    }

    bool AnalyzeImage(const RgbaFloatImage& image, ImageMetrics& outMetrics)
    {
        if (!image.Width || !image.Height ||
            image.Values.size() != static_cast<size_t>(image.Width) * image.Height * 4u ||
            !IsFiniteAndWithinRgba16Range(image))
        {
            return false;
        }

        const uint32_t centerLeft = image.Width / 4u;
        const uint32_t centerRight = image.Width * 3u / 4u;
        const uint32_t centerTop = image.Height / 4u;
        const uint32_t centerBottom = image.Height * 3u / 4u;
        uint64_t fullCount = 0u;
        uint64_t centerCount = 0u;
        for (uint32_t y = 0u; y < image.Height; ++y)
        {
            for (uint32_t x = 0u; x < image.Width; ++x)
            {
                const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                const float red = image.Values[offset + 0u];
                const float green = image.Values[offset + 1u];
                const float blue = image.Values[offset + 2u];
                const double pixelLuma = Luma(red, green, blue);
                outMetrics.MeanY += pixelLuma;
                outMetrics.MaximumY = std::max(outMetrics.MaximumY, pixelLuma);
                ++fullCount;
                if (x >= centerLeft && x < centerRight &&
                    y >= centerTop && y < centerBottom)
                {
                    outMetrics.CenterY += pixelLuma;
                    outMetrics.CenterRed += red;
                    outMetrics.CenterGreen += green;
                    outMetrics.CenterBlue += blue;
                    ++centerCount;
                }
            }
        }
        if (fullCount == 0u || centerCount == 0u)
        {
            return false;
        }
        outMetrics.MeanY /= static_cast<double>(fullCount);
        outMetrics.CenterY /= static_cast<double>(centerCount);
        outMetrics.CenterRed /= static_cast<double>(centerCount);
        outMetrics.CenterGreen /= static_cast<double>(centerCount);
        outMetrics.CenterBlue /= static_cast<double>(centerCount);
        return std::isfinite(outMetrics.MeanY) && std::isfinite(outMetrics.CenterY) &&
               std::isfinite(outMetrics.MaximumY);
    }

    double MeanAbsoluteDifference(const RgbaFloatImage& lhs, const RgbaFloatImage& rhs)
    {
        if (lhs.Width != rhs.Width || lhs.Height != rhs.Height ||
            lhs.Values.size() != rhs.Values.size())
        {
            return std::numeric_limits<double>::infinity();
        }
        double sum = 0.0;
        uint64_t count = 0u;
        for (uint32_t y = 0u; y < lhs.Height; ++y)
        {
            for (uint32_t x = 0u; x < lhs.Width; ++x)
            {
                const size_t offset = (static_cast<size_t>(y) * lhs.Width + x) * 4u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    sum += std::abs(static_cast<double>(lhs.Values[offset + channel]) -
                                    static_cast<double>(rhs.Values[offset + channel]));
                    ++count;
                }
            }
        }
        return count > 0u ? sum / static_cast<double>(count)
                          : std::numeric_limits<double>::infinity();
    }

    class R6RTGIAcceptanceHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnPreInitialize(const VariableArray<String>& args) override
        {
            m_Stage = CaptureStage::IblFallback;
            m_StageStartFrame = 0u;
            m_LastStageFrame = 0u;
            m_StageSample = 0u;
            m_bStateReady = true;
            m_bIblCaptured = false;
            m_bR4Captured = false;
            m_bStaticCaptured = false;
            m_bCameraMoved = false;
            m_bObjectMoved = false;
            m_bStopped = false;
            m_StopSample = 0u;
            m_LightSample = 0u;
            m_LightStartFrame = 0u;
            m_LightInitialY = 0.0;
            m_LightValues.fill(0.0);
            m_LightFrames.fill(0u);
            m_IblImage = {};
            m_R4Image = {};
            m_StaticImage = {};
            m_StopImage = {};
            m_StaticMetrics = {};
            m_StopMetrics = {};
            m_Golden = {};
            if (!LoadStaticGolden(m_Golden) ||
                !RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            return GetRunConfig().CaptureSource == FrameCaptureSourceKind::SceneColor;
        }

        bool OnInitialize() override
        {
            return RenderingValidationApplicationHandler::OnInitialize() &&
                   GetFixture().ApplyR4CornellFixture() &&
                   GetFixture().AddR6CornellDynamicObject();
        }

    protected:
        bool EvaluateCapturedFrame(const CapturedFrame& frame,
                                   String& outFailureReason) override
        {
            if (!m_bStateReady)
            {
                outFailureReason = TEXT("R6 Cornell fixtureの動的状態を更新できません");
                return false;
            }
            RgbaFloatImage image;
            if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success)
            {
                outFailureReason = TEXT("R6 HDR captureのRGBA16F読戻しに失敗しました");
                return false;
            }
            ImageMetrics metrics;
            if (!AnalyzeImage(image, metrics))
            {
                outFailureReason = TEXT("R6 HDR captureに有限な画素がありません");
                return false;
            }
            if (m_StageSample == 0u)
            {
                m_StageStartFrame = frame.FrameNumber;
            }
            else if (frame.FrameNumber <= m_LastStageFrame)
            {
                outFailureReason = TEXT("R6 captureのFrameNumberが単調増加していません");
                return false;
            }
            m_LastStageFrame = frame.FrameNumber;
            ++m_StageSample;

            switch (m_Stage)
            {
            case CaptureStage::IblFallback:
                m_IblImage = image;
                m_bIblCaptured = true;
                std::cout << "R6_FALLBACK_RT_DISABLED=PASS source=IBL"
                          << " frame=" << frame.FrameNumber
                          << " mean_y=" << metrics.MeanY << '\n';
                return true;

            case CaptureStage::R4FallbackWarmup:
                if (frame.FrameNumber - m_StageStartFrame > WarmupRenderedFrames)
                {
                    outFailureReason = TEXT("R4 fallbackの8 rendered-frame warmupを越えました");
                    return false;
                }
                if (IsWarmupComplete(frame.FrameNumber))
                {
                    m_R4Image = image;
                    m_bR4Captured = true;
                    const double delta = MeanAbsoluteDifference(m_IblImage, image);
                    std::cout << "R6_FALLBACK_R4=PASS frame=" << frame.FrameNumber
                              << " elapsed=" << (frame.FrameNumber - m_StageStartFrame)
                              << " mean_delta=" << delta << '\n';
                    if (delta < MinimumFallbackDifference)
                    {
                        outFailureReason = TEXT("R4 DDGI fallbackのHDR差分が正の対照に達しません");
                        return false;
                    }
                }
                return true;

            case CaptureStage::RTGIWarmup:
                if (frame.FrameNumber - m_StageStartFrame > WarmupRenderedFrames)
                {
                    outFailureReason = TEXT("RTGIの8 rendered-frame warmupを越えました");
                    return false;
                }
                if (IsWarmupComplete(frame.FrameNumber))
                {
                    m_StaticImage = image;
                    m_StaticMetrics = metrics;
                    m_bStaticCaptured = true;
                    const double iblDelta = MeanAbsoluteDifference(m_IblImage, image);
                    const double r4Delta = MeanAbsoluteDifference(m_R4Image, image);
                    std::cout << "R6_RTGI_WARMUP=PASS frame=" << frame.FrameNumber
                              << " elapsed=" << (frame.FrameNumber - m_StageStartFrame)
                              << " mean_y=" << metrics.MeanY
                              << " center_y=" << metrics.CenterY
                              << " center_rgb=" << metrics.CenterRed << ','
                              << metrics.CenterGreen << ',' << metrics.CenterBlue
                              << " ibl_delta=" << iblDelta
                              << " r4_delta=" << r4Delta
                              << " history_warmup=8\n";
                    if (!m_bIblCaptured || !m_bR4Captured ||
                        iblDelta < MinimumFallbackDifference ||
                        r4Delta < MinimumFallbackDifference)
                    {
                        outFailureReason = TEXT("RTGIがIBL/R4 fallbackと区別できません");
                        return false;
                    }
                }
                return true;

            case CaptureStage::StaticGolden:
            {
                const double goldenDelta = std::max(
                    std::max(std::abs(metrics.MeanY - m_Golden.MeanY),
                             std::abs(metrics.CenterY - m_Golden.CenterY)),
                    std::max(std::abs(metrics.CenterRed - m_Golden.CenterRed),
                             std::max(std::abs(metrics.CenterGreen - m_Golden.CenterGreen),
                                      std::abs(metrics.CenterBlue - m_Golden.CenterBlue))));
                const double staticDelta = MeanAbsoluteDifference(m_StaticImage, image);
                std::cout << "R6_STATIC_GOLDEN frame=" << frame.FrameNumber
                          << " mean_y=" << metrics.MeanY
                          << " center_y=" << metrics.CenterY
                          << " center_rgb=" << metrics.CenterRed << ','
                          << metrics.CenterGreen << ',' << metrics.CenterBlue
                          << " static_delta=" << staticDelta
                          << " golden_delta=" << goldenDelta << '\n';
                if (goldenDelta > m_Golden.Tolerance ||
                    staticDelta > m_Golden.Tolerance)
                {
                    outFailureReason = TEXT("R6静止RTGI goldenまたは8 frame安定値が閾値外です");
                    return false;
                }
                return true;
            }

            case CaptureStage::CameraMoved:
            {
                const double delta = MeanAbsoluteDifference(m_StaticImage, image);
                m_bCameraMoved = delta >= MinimumMotionDifference;
                std::cout << "R6_CAMERA_MOVE frame=" << frame.FrameNumber
                          << " mean_delta=" << delta
                          << " history_rejection=" << (m_bCameraMoved ? "observed" : "missing")
                          << '\n';
                if (!m_bCameraMoved)
                {
                    outFailureReason = TEXT("カメラ移動によるHDR変化または履歴棄却を観測できません");
                    return false;
                }
                return true;
            }

            case CaptureStage::ObjectMoved:
            {
                const double delta = MeanAbsoluteDifference(m_StaticImage, image);
                m_bObjectMoved = delta >= MinimumMotionDifference;
                std::cout << "R6_OBJECT_MOVE frame=" << frame.FrameNumber
                          << " mean_delta=" << delta
                          << " history_rejection=" << (m_bObjectMoved ? "observed" : "missing")
                          << '\n';
                if (!m_bObjectMoved)
                {
                    outFailureReason = TEXT("物体移動によるHDR変化または履歴棄却を観測できません");
                    return false;
                }
                return true;
            }

            case CaptureStage::MoveStopped:
            {
                const double delta = MeanAbsoluteDifference(m_StaticImage, image);
                const double frameDelta = m_StopSample > 0u
                    ? MeanAbsoluteDifference(m_StopImage, image)
                    : 0.0;
                m_StopImage = image;
                m_StopMetrics = metrics;
                ++m_StopSample;
                m_bStopped = delta <= MaximumStoppedDifference;
                std::cout << "R6_MOVE_THEN_STOP frame=" << frame.FrameNumber
                          << " sample=" << m_StopSample
                          << " static_delta=" << delta
                          << " frame_delta=" << frameDelta
                          << " residual=" << (m_bStopped ? "bounded" : "excessive") << '\n';
                if (!m_bStopped ||
                    (m_StopSample >= 2u && frameDelta > MaximumStoppedFrameDifference))
                {
                    outFailureReason = TEXT("移動後停止で静止goldenへの残留が収束しません");
                    return false;
                }
                if (m_StopSample >= 2u)
                {
                    m_LightStartFrame = frame.FrameNumber;
                    m_LightInitialY = metrics.CenterY;
                    std::cout << "R6_MOVE_THEN_STOP_INITIAL center_y=" << m_LightInitialY << '\n';
                }
                return true;
            }

            case CaptureStage::LightMoved:
            {
                if (m_LightSample >= m_LightValues.size())
                {
                    outFailureReason = TEXT("ライト移動のcapture数が上限を超えました");
                    return false;
                }
                const uint32_t sampleIndex = m_LightSample++;
                m_LightValues[sampleIndex] = metrics.CenterY;
                m_LightFrames[sampleIndex] = frame.FrameNumber;
                std::cout << "R6_LIGHT_MOVE frame=" << frame.FrameNumber
                          << " elapsed=" << (frame.FrameNumber - m_LightStartFrame)
                          << " sample=" << (sampleIndex + 1u)
                          << " center_y=" << metrics.CenterY << '\n';
                if (sampleIndex == 1u)
                {
                    const double firstChange = m_LightValues[0u] - m_LightInitialY;
                    const double finalChange = m_LightValues[1u] - m_LightInitialY;
                    const double progress = std::abs(firstChange) > 1.0e-12
                        ? std::abs(finalChange) / std::abs(firstChange)
                        : 0.0;
                    const uint64_t elapsed = frame.FrameNumber - m_LightStartFrame;
                    std::cout << "R6_LIGHT_FOLLOWUP=PASS elapsed=" << elapsed
                              << " progress=" << progress
                              << " change=" << finalChange << '\n';
                    if (m_LightFrames[1u] <= m_LightFrames[0u] ||
                        elapsed > LightDeadlineRenderedFrames ||
                        std::abs(firstChange) < MinimumLightChange ||
                        std::abs(finalChange) < MinimumLightChange ||
                        firstChange * finalChange <= 0.0 ||
                        progress < MinimumLightProgress)
                    {
                        outFailureReason = TEXT("ライト移動後4 rendered frame以内の追従を確認できません");
                        return false;
                    }
                }
                return true;
            }

            case CaptureStage::Complete:
                break;
            }
            outFailureReason = TEXT("R6 capture段階が不正です");
            return false;
        }

        bool RequestFollowupCapture(const CapturedFrame&,
                                    FrameCaptureRequest& outRequest) override
        {
            if (m_Stage == CaptureStage::Complete)
            {
                return false;
            }
            outRequest.SourceKind = FrameCaptureSourceKind::SceneColor;
            return true;
        }

        void ApplyCaptureStageState(RenderWorld& renderWorld) override
        {
            CameraProxy camera = GetFixture().GetR4CornellCamera();
            const bool bCameraMoved = m_Stage == CaptureStage::CameraMoved;
            camera.PositionX += bCameraMoved ? 0.35f : 0.0f;
            renderWorld.SetMainCamera(camera);

            const bool bRTGI = m_Stage == CaptureStage::RTGIWarmup ||
                               m_Stage == CaptureStage::StaticGolden ||
                               m_Stage == CaptureStage::CameraMoved ||
                               m_Stage == CaptureStage::ObjectMoved ||
                               m_Stage == CaptureStage::MoveStopped ||
                               m_Stage == CaptureStage::LightMoved;
            renderWorld.GetRenderingCoordinator().SetRTGIEnabled(bRTGI);
            renderWorld.SetDDGIVolumeParameters(MakeCornellVolume(
                m_Stage == CaptureStage::R4FallbackWarmup));

            const float objectOffset = m_Stage == CaptureStage::ObjectMoved ? 0.55f : 0.0f;
            m_bStateReady = GetFixture().SetR4CornellObjectOffsetX(objectOffset) && m_bStateReady;

            const bool bLightEnabled = bRTGI;
            const float lightOffset = m_Stage == CaptureStage::LightMoved ? 0.55f : 0.0f;
            m_bStateReady = GetFixture().SetR4CornellLightOffsetX(0.0f) && m_bStateReady;
            m_bStateReady = GetFixture().SetR4CornellPointLightState(
                lightOffset,
                bLightEnabled ? R6PointLightIntensity : 0.0f) && m_bStateReady;
        }

        void AdvanceCaptureStage() override
        {
            if (m_Stage == CaptureStage::R4FallbackWarmup ||
                m_Stage == CaptureStage::RTGIWarmup)
            {
                if (m_LastStageFrame - m_StageStartFrame < WarmupRenderedFrames)
                {
                    return;
                }
            }
            else if (m_Stage == CaptureStage::MoveStopped && m_StopSample < 2u)
            {
                return;
            }
            else if (m_Stage == CaptureStage::LightMoved && m_LightSample < 2u)
            {
                return;
            }

            m_StageSample = 0u;
            m_StageStartFrame = 0u;
            m_LastStageFrame = 0u;
            switch (m_Stage)
            {
            case CaptureStage::IblFallback:
                m_Stage = CaptureStage::R4FallbackWarmup;
                break;
            case CaptureStage::R4FallbackWarmup:
                m_Stage = CaptureStage::RTGIWarmup;
                break;
            case CaptureStage::RTGIWarmup:
                m_Stage = CaptureStage::StaticGolden;
                break;
            case CaptureStage::StaticGolden:
                m_Stage = CaptureStage::CameraMoved;
                break;
            case CaptureStage::CameraMoved:
                m_Stage = CaptureStage::ObjectMoved;
                break;
            case CaptureStage::ObjectMoved:
                m_Stage = CaptureStage::MoveStopped;
                m_StopSample = 0u;
                break;
            case CaptureStage::MoveStopped:
                m_Stage = CaptureStage::LightMoved;
                m_LightSample = 0u;
                break;
            case CaptureStage::LightMoved:
                m_Stage = CaptureStage::Complete;
                break;
            case CaptureStage::Complete:
                break;
            }
        }

    private:
        static DDGIVolumeParameters MakeCornellVolume(bool bEnabled)
        {
            DDGIVolumeParameters parameters;
            parameters.bEnabled = bEnabled;
            parameters.Origin = Math::Vector3(-0.1f, -0.1f, -0.1f);
            parameters.ProbeSpacing = Math::Vector3(0.82f, 0.82f, 0.82f);
            parameters.ProbeCountX = 8u;
            parameters.ProbeCountY = 8u;
            parameters.ProbeCountZ = 8u;
            return parameters;
        }

        bool IsWarmupComplete(uint64_t frameNumber) const
        {
            return m_StageSample > 0u && frameNumber - m_StageStartFrame == WarmupRenderedFrames;
        }

        CaptureStage m_Stage = CaptureStage::IblFallback;
        StaticGolden m_Golden;
        RgbaFloatImage m_IblImage;
        RgbaFloatImage m_R4Image;
        RgbaFloatImage m_StaticImage;
        RgbaFloatImage m_StopImage;
        ImageMetrics m_StaticMetrics;
        ImageMetrics m_StopMetrics;
        uint64_t m_StageStartFrame = 0u;
        uint64_t m_LastStageFrame = 0u;
        uint32_t m_StageSample = 0u;
        bool m_bStateReady = true;
        bool m_bIblCaptured = false;
        bool m_bR4Captured = false;
        bool m_bStaticCaptured = false;
        bool m_bCameraMoved = false;
        bool m_bObjectMoved = false;
        bool m_bStopped = false;
        uint32_t m_StopSample = 0u;
        uint32_t m_LightSample = 0u;
        uint64_t m_LightStartFrame = 0u;
        double m_LightInitialY = 0.0;
        FixedArray<double, 2> m_LightValues{};
        FixedArray<uint64_t, 2> m_LightFrames{};
    };

    TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return MakeShared<R6RTGIAcceptanceHandler>();
    }
}

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip(TestName, "環境変数によりGPU検証をスキップします");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
    }

    RHI::RHIDeviceDesc deviceDesc;
    deviceDesc.Api = RHI::GraphicsAPI::Vulkan;
    RHI::DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
    if (!device)
    {
        return ReportGpuTestSkip(TestName, "Vulkanデバイスを作成できません");
    }
    const RHI::DeviceCapabilities capabilities = device->GetCapabilities();
    device->WaitIdle();
    device.reset();
    if (!capabilities.RayTracing.bAccelerationStructure ||
        !capabilities.RayTracing.bRayQuery ||
        !capabilities.bBufferDeviceAddress || !capabilities.bShaderInt64)
    {
        return ReportGpuTestSkip(TestName, "R6 RTGIに必要なVulkan機能を利用できません");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("R6 RTGI GPU受入れ");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = false;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("R6RTGIAcceptanceVulkan.log");
    config.CreateHandler = &CreateHandler;
    bool bSceneSpecified = false;
    bool bCaptureSourceSpecified = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strncmp(argv[index], "--scene=", 8u) == 0)
        {
            bSceneSpecified = true;
        }
        if (std::strncmp(argv[index], "--capture-source=", 17u) == 0)
        {
            bCaptureSourceSpecified = true;
        }
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    if (!bSceneSpecified)
    {
        config.Arguments.push_back(TEXT("--scene=indoor"));
    }
    if (!bCaptureSourceSpecified)
    {
        config.Arguments.push_back(TEXT("--capture-source=scene-color"));
    }
    return Core::Boot::LaunchApplication(config);
}
