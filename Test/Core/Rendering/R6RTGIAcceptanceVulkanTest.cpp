// R6 RTGIの静止・移動・ライト追従・fallbackをHDR captureで検証する。
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Engine/Engine.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
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
    constexpr double MaximumStoppedFrameDifference = 0.02;
    constexpr double MinimumLightProgress = 0.80;
    constexpr float R6PointLightIntensity = 1200.0f;
    constexpr float RTGIHistoryMaximumAge = 8.0f;
    constexpr uint32_t LightBaselineSamples = 4u;
    constexpr uint64_t LightBaselineSettleFrames = 16u;
    constexpr float LightStartOffset = -1.0f;
    constexpr float LightMovedOffset = 1.0f;
    // 追従検証の段階だけ使う点光源の光束。天井の面光源（45000 nits、約1.37 m²、約19万lm）と
    // 同程度にして、移動が間接光の色にじみへはっきり現れるようにする。
    constexpr float R6FollowLightIntensity = 200000.0f;
    constexpr uint64_t LightConvergedElapsedFrames = 24u;
    constexpr uint64_t LightObservationFrames = 32u;
    constexpr uint64_t CameraSettleRenderedFrames = 10u;
    constexpr uint64_t StopSettleRenderedFrames = 12u;
    constexpr float ObjectVisibleOffset = 0.55f;
    // カメラ移動は横0.35と前進1.0。前進で表面までの距離が5%以上変わり、距離の基準がずれると棄却される。
    constexpr float CameraMoveX = 0.35f;
    constexpr float CameraMoveForward = 1.0f;
    constexpr uint32_t ResidualSamples = 4u;
    // 物体の影響（物体あり−参照の画素差の中央値）は、参照の前半と後半の画素差（ノイズ床）の3倍以上を要求する。
    constexpr double ResidualSensitivityMultiple = 3.0;
    // 停止後−参照の画素差は物体の影響の半分以下を求める。残留がなければノイズ床程度（影響の1/3以下）になる。
    constexpr double MaximumResidualFraction = 0.5;
    constexpr uint64_t ObjectPresentSettleFrames = 12u;
    constexpr float ObjectMovedOffset = 1.0f;
    // カメラ・物体移動時に履歴を保つべき静止領域で、最大ageを維持する画素の割合の下限。
    // 視差で露出する遮蔽物の輪郭だけが棄却されるため、大半の画素は保持される。
    constexpr double MinimumKeptFraction = 0.9;
    // 追従率の分母となる最終変化量は、静止時の揺らぎ（標準偏差）の5倍以上を要求する。
    constexpr double LightSignalToNoise = 5.0;

    // 画面上の矩形（画像幅・高さに対する割合: 左, 右, 上, 下）。
    struct UvRect
    {
        double Left;
        double Right;
        double Top;
        double Bottom;
    };
    // 物体・カメラ移動の影響を受けない奥の壁の中央。
    constexpr UvRect StaticControlRoi = {200.0 / 512.0, 312.0 / 512.0, 150.0 / 512.0, 250.0 / 512.0};

    struct ImageMetrics
    {
        double MeanY = 0.0;
        double CenterY = 0.0;
        double CenterRed = 0.0;
        double CenterGreen = 0.0;
        double CenterBlue = 0.0;
        double MaximumY = 0.0;
    };

    enum class CaptureStage : uint8_t
    {
        IblFallback,
        R4FallbackWarmup,
        RTGIWarmup,
        StaticStability,
        StaticIndirectReference,
        CameraMoved,
        CameraSettled,
        ObjectMoved,
        ObjectPresentIndirect,
        MoveStopped,
        StopIndirect,
        LightBaseline,
        LightMoved,
        Complete
    };

    // R16_FLOATの履歴age captureを行優先のfloat配列へ復号する。
    bool DecodeCapturedR16Float(const CapturedFrame& frame, VariableArray<float>& outValues)
    {
        if (!frame.IsSuccess() || frame.Format != RHI::Format::R16_FLOAT ||
            frame.Width == 0u || frame.Height == 0u ||
            frame.RowPitchBytes < frame.Width * 2u ||
            frame.Pixels.size() < static_cast<size_t>(frame.RowPitchBytes) * frame.Height)
        {
            return false;
        }
        outValues.resize(static_cast<size_t>(frame.Width) * frame.Height);
        for (uint32_t y = 0u; y < frame.Height; ++y)
        {
            for (uint32_t x = 0u; x < frame.Width; ++x)
            {
                const size_t offset = static_cast<size_t>(y) * frame.RowPitchBytes + x * 2u;
                const uint16_t bits = static_cast<uint16_t>(
                    frame.Pixels[offset] | (static_cast<uint16_t>(frame.Pixels[offset + 1u]) << 8u));
                outValues[static_cast<size_t>(y) * frame.Width + x] = DecodeIeee754Binary16(bits);
            }
        }
        return true;
    }

    struct AgeRange
    {
        float Minimum = 0.0f;
        float Maximum = 0.0f;
        uint32_t Count = 0u;
        uint32_t KeptCount = 0u; ///< 最大ageを維持した画素数

        double KeptFraction() const
        {
            return Count > 0u ? static_cast<double>(KeptCount) / static_cast<double>(Count) : 0.0;
        }
    };

    AgeRange MeasureAge(const VariableArray<float>& ages, uint32_t width, uint32_t height,
                        const UvRect& roi)
    {
        AgeRange range;
        const uint32_t left = static_cast<uint32_t>(roi.Left * width);
        const uint32_t right = static_cast<uint32_t>(roi.Right * width);
        const uint32_t top = static_cast<uint32_t>(roi.Top * height);
        const uint32_t bottom = static_cast<uint32_t>(roi.Bottom * height);
        range.Minimum = std::numeric_limits<float>::infinity();
        range.Maximum = -std::numeric_limits<float>::infinity();
        for (uint32_t y = top; y < bottom && y < height; ++y)
        {
            for (uint32_t x = left; x < right && x < width; ++x)
            {
                const float age = ages[static_cast<size_t>(y) * width + x];
                if (!std::isfinite(age))
                {
                    range.Count = 0u;
                    return range;
                }
                range.Minimum = std::min(range.Minimum, age);
                range.Maximum = std::max(range.Maximum, age);
                ++range.Count;
                if (age >= RTGIHistoryMaximumAge)
                {
                    ++range.KeptCount;
                }
            }
        }
        return range;
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

    // 赤と緑の壁からの色にじみの差（デノイズ後間接光のR平均−G平均）。点光源が片側の壁へ
    // 寄るほど、その壁の色の一次反射が増えて値が変わる。少数試料の推定は分布が偏るため中央値は
    // 追従を遅く見せる。偏りのない平均を使い、外れ値の元になる面光源は追従検証の間だけ隠す。
    double MeasureIndirectChroma(const RgbaFloatImage& image)
    {
        double red = 0.0;
        double green = 0.0;
        uint64_t count = 0u;
        for (size_t offset = 0u; offset + 3u < image.Values.size(); offset += 4u)
        {
            red += image.Values[offset + 0u];
            green += image.Values[offset + 1u];
            ++count;
        }
        if (count == 0u)
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return (red - green) / static_cast<double>(count);
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
            m_ReferenceIndirectSums.clear();
            m_ReferenceFirstHalfSums.clear();
            m_ReferenceSecondHalfSums.clear();
            m_PresentIndirectSums.clear();
            m_StopIndirectSums.clear();
            m_PresentIndirectCount = 0u;
            m_ReferenceIndirectCount = 0u;
            m_StopIndirectCount = 0u;
            m_PreviousStageLastFrame = 0u;
            m_LightStartFrame = 0u;
            m_LightBaselineValues.fill(0.0);
            m_LightBaselineCount = 0u;
            m_LightDeadlineValue = 0.0;
            m_LightDeadlineElapsed = 0u;
            m_bLightDeadlineSampled = false;
            m_LightConvergedSum = 0.0;
            m_LightConvergedCount = 0u;
            m_LightLastElapsed = 0u;
            m_IblImage = {};
            m_R4Image = {};
            m_StaticImage = {};
            m_StaticMetrics = {};
            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
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

            if (m_Stage == CaptureStage::CameraMoved || m_Stage == CaptureStage::ObjectMoved ||
                m_Stage == CaptureStage::MoveStopped)
            {
                return EvaluateHistoryAge(frame, outFailureReason);
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

            case CaptureStage::StaticStability:
            {
                // 静止中もレイがフレームごとに変わるため、warmup直後との差は時間方向の揺らぎになる。
                // 移動後停止と同じ上限で揺らぎが有界であることを確認する。参照画像との比較は
                // R7の自前PTを使うR6-P5-REFで行う。
                const double staticDelta = MeanAbsoluteDifference(m_StaticImage, image);
                std::cout << "R6_STATIC_STABILITY frame=" << frame.FrameNumber
                          << " mean_y=" << metrics.MeanY
                          << " center_y=" << metrics.CenterY
                          << " center_rgb=" << metrics.CenterRed << ','
                          << metrics.CenterGreen << ',' << metrics.CenterBlue
                          << " static_delta=" << staticDelta << '\n';
                if (staticDelta > MaximumStoppedFrameDifference)
                {
                    outFailureReason = TEXT("R6静止RTGIの時間方向の揺らぎが上限を超えます");
                    return false;
                }
                return true;
            }

            case CaptureStage::CameraSettled:
                // カメラを元へ戻し、物体移動の前に履歴ageを最大まで回復させる。
                return true;

            case CaptureStage::StaticIndirectReference:
            case CaptureStage::ObjectPresentIndirect:
            case CaptureStage::StopIndirect:
            {
                // 物体が去った領域（offset 1.0の円）のデノイズ後間接光を画素ごとに加算し、
                // 参照（物体なし）・物体あり・停止後の3状態を比べる。
                if (m_Stage == CaptureStage::ObjectPresentIndirect &&
                    frame.FrameNumber - m_StageStartFrame < ObjectPresentSettleFrames)
                {
                    // 物体を置いたままの履歴が落ち着くまで待つ。
                    return true;
                }
                const bool bReference = m_Stage == CaptureStage::StaticIndirectReference;
                const bool bPresent = m_Stage == CaptureStage::ObjectPresentIndirect;
                VariableArray<double>& sums = bReference ? m_ReferenceIndirectSums :
                                              bPresent ? m_PresentIndirectSums : m_StopIndirectSums;
                uint32_t& count = bReference ? m_ReferenceIndirectCount :
                                  bPresent ? m_PresentIndirectCount : m_StopIndirectCount;
                if (count >= ResidualSamples || !AccumulateVacatedIndirect(image, sums))
                {
                    outFailureReason = TEXT("物体が去った領域の間接光を測れません");
                    return false;
                }
                if (bReference)
                {
                    // 参照の前半と後半を分けて持ち、ノイズ床を求める。
                    VariableArray<double>& halfSums = count < ResidualSamples / 2u
                        ? m_ReferenceFirstHalfSums
                        : m_ReferenceSecondHalfSums;
                    if (!AccumulateVacatedIndirect(image, halfSums))
                    {
                        outFailureReason = TEXT("物体が去った領域の間接光を測れません");
                        return false;
                    }
                }
                ++count;
                std::cout << (bReference ? "R6_STOP_REFERENCE" :
                              bPresent ? "R6_OBJECT_PRESENT" : "R6_STOP_RESIDUAL")
                          << " frame=" << frame.FrameNumber
                          << " sample=" << count << '\n';
                if (m_Stage == CaptureStage::StopIndirect && count == ResidualSamples)
                {
                    return EvaluateStopResidual(outFailureReason);
                }
                return true;
            }

            case CaptureStage::LightBaseline:
            {
                // 開始位置へ移した後の落ち着き期間を過ぎてから、デノイズ後間接光を複数回読み、
                // 基準値と静止時の揺らぎを求める。
                const double balance = MeasureIndirectChroma(image);
                if (!std::isfinite(balance) || m_LightBaselineCount >= LightBaselineSamples)
                {
                    outFailureReason = TEXT("ライト移動前の間接光基準を測れません");
                    return false;
                }
                if (frame.FrameNumber - m_StageStartFrame < LightBaselineSettleFrames)
                {
                    return true;
                }
                m_LightBaselineValues[m_LightBaselineCount++] = balance;
                m_LightStartFrame = frame.FrameNumber;
                std::cout << "R6_LIGHT_BASELINE frame=" << frame.FrameNumber
                          << " sample=" << m_LightBaselineCount
                          << " indirect_chroma=" << balance << '\n';
                return true;
            }

            case CaptureStage::LightMoved:
            {
                const double balance = MeasureIndirectChroma(image);
                const uint64_t elapsed = frame.FrameNumber - m_LightStartFrame;
                if (!std::isfinite(balance) || elapsed == 0u)
                {
                    outFailureReason = TEXT("ライト移動後の間接光を測れません");
                    return false;
                }
                std::cout << "R6_LIGHT_MOVE frame=" << frame.FrameNumber
                          << " elapsed=" << elapsed
                          << " indirect_chroma=" << balance << '\n';
                if (elapsed <= LightDeadlineRenderedFrames)
                {
                    // 期限内で最も新しい値を追従判定に使う。
                    m_LightDeadlineValue = balance;
                    m_LightDeadlineElapsed = elapsed;
                    m_bLightDeadlineSampled = true;
                }
                else if (elapsed >= LightConvergedElapsedFrames)
                {
                    m_LightConvergedSum += balance;
                    ++m_LightConvergedCount;
                }
                m_LightLastElapsed = elapsed;
                if (elapsed >= LightObservationFrames)
                {
                    return EvaluateLightFollow(outFailureReason);
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
            switch (m_Stage)
            {
            case CaptureStage::CameraMoved:
            case CaptureStage::ObjectMoved:
            case CaptureStage::MoveStopped:
                outRequest.SourceKind = FrameCaptureSourceKind::RTGIHistoryAge;
                break;
            case CaptureStage::StaticIndirectReference:
            case CaptureStage::ObjectPresentIndirect:
            case CaptureStage::StopIndirect:
            case CaptureStage::LightBaseline:
            case CaptureStage::LightMoved:
                outRequest.SourceKind = FrameCaptureSourceKind::RTGIDiffuseIndirect;
                break;
            default:
                outRequest.SourceKind = FrameCaptureSourceKind::SceneColor;
                break;
            }
            return true;
        }

        void ApplyCaptureStageState(RenderWorld& renderWorld) override
        {
            CameraProxy camera = GetFixture().GetR4CornellCamera();
            const bool bCameraMoved = m_Stage == CaptureStage::CameraMoved;
            camera.PositionX += bCameraMoved ? CameraMoveX : 0.0f;
            camera.PositionZ += bCameraMoved ? CameraMoveForward : 0.0f;
            renderWorld.SetMainCamera(camera);

            const bool bRTGI = m_Stage == CaptureStage::RTGIWarmup ||
                               m_Stage == CaptureStage::StaticStability ||
                               m_Stage == CaptureStage::StaticIndirectReference ||
                               m_Stage == CaptureStage::CameraMoved ||
                               m_Stage == CaptureStage::CameraSettled ||
                               m_Stage == CaptureStage::ObjectMoved ||
                               m_Stage == CaptureStage::ObjectPresentIndirect ||
                               m_Stage == CaptureStage::MoveStopped ||
                               m_Stage == CaptureStage::StopIndirect ||
                               m_Stage == CaptureStage::LightBaseline ||
                               m_Stage == CaptureStage::LightMoved;
            renderWorld.GetRenderingCoordinator().SetRTGIEnabled(bRTGI);
            renderWorld.SetDDGIVolumeParameters(MakeCornellVolume(
                m_Stage == CaptureStage::R4FallbackWarmup));

            // 既定位置（0）の球は短いブロックの後ろにほぼ隠れるため、露出の検証は
            // 移動前後とも見える位置（+0.55から+1.0）で行う。
            const float objectOffset = m_Stage == CaptureStage::CameraSettled ? ObjectVisibleOffset :
                                       m_Stage == CaptureStage::ObjectMoved ||
                                               m_Stage == CaptureStage::ObjectPresentIndirect
                                           ? ObjectMovedOffset :
                                                                              0.0f;
            m_bStateReady = GetFixture().SetR4CornellObjectOffsetX(objectOffset) && m_bStateReady;

            const bool bLightEnabled = bRTGI;
            // 点光源を部屋の片側（-1.0）で落ち着かせてから反対側（+1.0）へ移す。
            const float lightOffset = m_Stage == CaptureStage::LightBaseline ? LightStartOffset :
                                      m_Stage == CaptureStage::LightMoved ? LightMovedOffset :
                                                                            0.0f;
            m_bStateReady = GetFixture().SetR4CornellLightOffsetX(0.0f) && m_bStateReady;
            const bool bFollowStage = m_Stage == CaptureStage::LightBaseline ||
                                      m_Stage == CaptureStage::LightMoved;
            const float lightIntensity = !bLightEnabled ? 0.0f :
                                         bFollowStage ? R6FollowLightIntensity :
                                                        R6PointLightIntensity;
            m_bStateReady = GetFixture().SetR4CornellPointLightState(
                lightOffset, lightIntensity) && m_bStateReady;
            // 追従検証の間だけ天井の面光源を隠し、点光源を唯一の光源にする。面光源へ偶然当たる
            // レイの外れ値で、移動直後の少数試料の推定が大きく揺れるのを避ける。
            m_bStateReady = GetFixture().SetR4CornellEmitterVisible(!bFollowStage) && m_bStateReady;
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
            else if (m_Stage == CaptureStage::CameraSettled &&
                     m_LastStageFrame - m_StageStartFrame < CameraSettleRenderedFrames)
            {
                return;
            }
            else if (m_Stage == CaptureStage::MoveStopped &&
                     m_StopLastElapsed < StopSettleRenderedFrames)
            {
                return;
            }
            else if (m_Stage == CaptureStage::StaticIndirectReference &&
                     m_ReferenceIndirectCount < ResidualSamples)
            {
                return;
            }
            else if (m_Stage == CaptureStage::ObjectPresentIndirect &&
                     m_PresentIndirectCount < ResidualSamples)
            {
                return;
            }
            else if (m_Stage == CaptureStage::StopIndirect &&
                     m_StopIndirectCount < ResidualSamples)
            {
                return;
            }
            else if (m_Stage == CaptureStage::LightBaseline &&
                     m_LightBaselineCount < LightBaselineSamples)
            {
                return;
            }
            else if (m_Stage == CaptureStage::LightMoved &&
                     m_LightLastElapsed < LightObservationFrames)
            {
                return;
            }

            m_PreviousStageLastFrame = m_LastStageFrame;
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
                m_Stage = CaptureStage::StaticStability;
                break;
            case CaptureStage::StaticStability:
                m_Stage = CaptureStage::StaticIndirectReference;
                m_ReferenceIndirectCount = 0u;
                break;
            case CaptureStage::StaticIndirectReference:
                m_Stage = CaptureStage::CameraMoved;
                break;
            case CaptureStage::CameraMoved:
                m_Stage = CaptureStage::CameraSettled;
                break;
            case CaptureStage::CameraSettled:
                m_Stage = CaptureStage::ObjectMoved;
                break;
            case CaptureStage::ObjectMoved:
                m_Stage = CaptureStage::ObjectPresentIndirect;
                m_PresentIndirectCount = 0u;
                break;
            case CaptureStage::ObjectPresentIndirect:
                m_Stage = CaptureStage::MoveStopped;
                m_StopSample = 0u;
                m_StopLastElapsed = 0u;
                break;
            case CaptureStage::MoveStopped:
                m_Stage = CaptureStage::StopIndirect;
                m_StopIndirectCount = 0u;
                break;
            case CaptureStage::StopIndirect:
                m_Stage = CaptureStage::LightBaseline;
                m_LightBaselineCount = 0u;
                break;
            case CaptureStage::LightBaseline:
                m_Stage = CaptureStage::LightMoved;
                m_LightLastElapsed = 0u;
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
        RgbaFloatImage m_IblImage;
        RgbaFloatImage m_R4Image;
        RgbaFloatImage m_StaticImage;
        ImageMetrics m_StaticMetrics;
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
        uint64_t m_StopLastElapsed = 0u;
        VariableArray<double> m_ReferenceIndirectSums;
        VariableArray<double> m_ReferenceFirstHalfSums;
        VariableArray<double> m_ReferenceSecondHalfSums;
        VariableArray<double> m_PresentIndirectSums;
        VariableArray<double> m_StopIndirectSums;
        uint32_t m_PresentIndirectCount = 0u;
        uint32_t m_ReferenceIndirectCount = 0u;
        uint32_t m_StopIndirectCount = 0u;
        uint64_t m_PreviousStageLastFrame = 0u;
        uint64_t m_LightStartFrame = 0u;
        FixedArray<double, LightBaselineSamples> m_LightBaselineValues{};
        uint32_t m_LightBaselineCount = 0u;
        double m_LightDeadlineValue = 0.0;
        uint64_t m_LightDeadlineElapsed = 0u;
        bool m_bLightDeadlineSampled = false;
        double m_LightConvergedSum = 0.0;
        uint32_t m_LightConvergedCount = 0u;
        uint64_t m_LightLastElapsed = 0u;

        struct ScreenCircle
        {
            double X = 0.0;
            double Y = 0.0;
            double Radius = 0.0;
            bool bValid = false;
        };

        /**
         * @brief 動的球（半径0.28、中心(2.78+offsetX,0.72,2.6)）をCornellカメラで画素座標の円へ投影する
         *
         * 画素Yは画像の上端から数える。GBufferのvelocity検証と同じ、デバイス規約込みの行列を使う。
         */
        ScreenCircle ProjectCornellObject(float offsetX, uint32_t width, uint32_t height) const
        {
            ScreenCircle circle;
            const RHI::DevicePtr device =
                Core::Engine::GEngine->GetRenderWorld().GetRenderingCoordinator().GetDevice();
            const CameraViewConstants constants = CameraViewConstants::BuildForDevice(
                GetFixture().GetR4CornellCamera(),
                static_cast<float>(width) / static_cast<float>(height),
                device.get());
            const auto project = [&](float x, float y, float z, double& outX, double& outY) -> bool
            {
                const Math::Vector4 clip = constants.ViewProjectionMatrix * Math::Vector4(x, y, z, 1.0f);
                if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) ||
                    std::abs(clip.w) <= 1.0e-6f)
                {
                    return false;
                }
                outX = (static_cast<double>(clip.x / clip.w) * 0.5 + 0.5) * width;
                outY = (static_cast<double>(clip.y / clip.w) * 0.5 + 0.5) * height;
                return std::isfinite(outX) && std::isfinite(outY);
            };
            constexpr float Radius = 0.28f;
            const float centerX = 2.78f + offsetX;
            double edgeX = 0.0;
            double edgeY = 0.0;
            double topX = 0.0;
            double topY = 0.0;
            if (!project(centerX, 0.72f, 2.6f, circle.X, circle.Y) ||
                !project(centerX + Radius, 0.72f, 2.6f, edgeX, edgeY) ||
                !project(centerX, 0.72f + Radius, 2.6f, topX, topY))
            {
                return circle;
            }
            circle.Radius = std::max(std::hypot(edgeX - circle.X, edgeY - circle.Y),
                                     std::hypot(topX - circle.X, topY - circle.Y));
            circle.bValid = std::isfinite(circle.Radius) && circle.Radius > 1.0;
            return circle;
        }

        /**
         * @brief 物体が去った領域（offset 1.0の投影円の内側）の画素ごとの間接光輝度を加算する
         *
         * 領域と画素の並びは全段階で同じなので、段階ごとの画素平均を画素単位で比べられる。
         */
        bool AccumulateVacatedIndirect(const RgbaFloatImage& image, VariableArray<double>& inOutSums) const
        {
            const ScreenCircle vacated = ProjectCornellObject(ObjectMovedOffset, image.Width, image.Height);
            if (!vacated.bValid)
            {
                return false;
            }
            size_t index = 0u;
            for (uint32_t y = 0u; y < image.Height; ++y)
            {
                for (uint32_t x = 0u; x < image.Width; ++x)
                {
                    const double px = static_cast<double>(x) + 0.5 - vacated.X;
                    const double py = static_cast<double>(y) + 0.5 - vacated.Y;
                    if (std::hypot(px, py) > vacated.Radius - 1.5)
                    {
                        continue;
                    }
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const double luma = Luma(image.Values[offset + 0u], image.Values[offset + 1u],
                                             image.Values[offset + 2u]);
                    if (!std::isfinite(luma))
                    {
                        return false;
                    }
                    if (index >= inOutSums.size())
                    {
                        inOutSums.push_back(0.0);
                    }
                    inOutSums[index++] += luma;
                }
            }
            return index > 0u && index == inOutSums.size();
        }

        /**
         * @brief 2つの画素平均列の、画素ごとの絶対差の中央値
         *
         * 面光源へ当たったレイの外れ値に左右されないよう、平均ではなく中央値を使う。
         */
        static double MedianAbsoluteDifference(const VariableArray<double>& lhsSums, double lhsCount,
                                               const VariableArray<double>& rhsSums, double rhsCount)
        {
            if (lhsSums.empty() || lhsSums.size() != rhsSums.size() || lhsCount <= 0.0 || rhsCount <= 0.0)
            {
                return std::numeric_limits<double>::quiet_NaN();
            }
            VariableArray<double> differences;
            differences.reserve(lhsSums.size());
            for (size_t index = 0u; index < lhsSums.size(); ++index)
            {
                differences.push_back(std::abs(lhsSums[index] / lhsCount - rhsSums[index] / rhsCount));
            }
            std::sort(differences.begin(), differences.end());
            return differences[differences.size() / 2u];
        }

        /**
         * @brief 停止後に、物体が去った領域から物体の間接光の影響が抜けたかを判定する
         *
         * 物体あり−参照（停止先と同じ物体なしの状態）の画素差を物体の影響とし、参照の前半と後半の
         * 画素差をノイズ床とする。影響がノイズ床の3倍以上あることを前提に、停止後−参照の画素差が
         * 影響の半分以下であることを求める。棄却時にageだけを戻して古い放射輝度を残す不具合なら、
         * 停止後も物体の影響が残って落ちる。
         */
        bool EvaluateStopResidual(String& outFailureReason)
        {
            const double half = static_cast<double>(ResidualSamples / 2u);
            const double full = static_cast<double>(ResidualSamples);
            const double objectEffect = MedianAbsoluteDifference(
                m_PresentIndirectSums, full, m_ReferenceIndirectSums, full);
            const double residual = MedianAbsoluteDifference(
                m_StopIndirectSums, full, m_ReferenceIndirectSums, full);
            const double noise = MedianAbsoluteDifference(
                m_ReferenceFirstHalfSums, half, m_ReferenceSecondHalfSums, half);
            const bool bSensitive = std::isfinite(objectEffect) && std::isfinite(noise) &&
                                    objectEffect >= ResidualSensitivityMultiple * noise &&
                                    objectEffect > 0.0;
            const bool bSettled = std::isfinite(residual) &&
                                  residual <= MaximumResidualFraction * objectEffect;
            const bool bPassed = bSensitive && bSettled;
            std::cout << "R6_STOP_RESIDUAL_CHECK=" << (bPassed ? "PASS" : "FAIL")
                      << " pixels=" << m_ReferenceIndirectSums.size()
                      << " object_effect=" << objectEffect
                      << " residual=" << residual
                      << " noise_floor=" << noise
                      << " residual_fraction="
                      << (objectEffect > 0.0 ? residual / objectEffect : 0.0) << '\n';
            if (!bSensitive)
            {
                outFailureReason = TEXT("物体の間接光の影響がノイズ床に埋もれ、停止後の残留を判定できません");
                return false;
            }
            if (!bSettled)
            {
                outFailureReason = TEXT("移動後停止で物体が去った領域に物体の間接光が残っています");
                return false;
            }
            return true;
        }

        /**
         * @brief 履歴ageのreadbackで、物体移動で露出した領域の棄却とカメラ移動時の保持を判定する
         *
         * 棄却された画素はage 0から数え直すため、capture時点のageは移動からのフレーム数以下になる。
         * 棄却処理が働かなければ露出領域も静止領域と同じ最大ageのままになる。
         */
        bool EvaluateHistoryAge(const CapturedFrame& frame, String& outFailureReason)
        {
            VariableArray<float> ages;
            if (!DecodeCapturedR16Float(frame, ages))
            {
                outFailureReason = TEXT("R6履歴ageのR16F読戻しに失敗しました");
                return false;
            }
            const AgeRange control = MeasureAge(ages, frame.Width, frame.Height, StaticControlRoi);
            const bool bControlKept = control.KeptFraction() >= MinimumKeptFraction;
            if (m_Stage == CaptureStage::CameraMoved)
            {
                m_bCameraMoved = bControlKept;
                std::cout << "R6_CAMERA_MOVE frame=" << frame.FrameNumber
                          << " control_kept_fraction=" << control.KeptFraction()
                          << " control_age_min=" << control.Minimum
                          << " history_kept=" << (bControlKept ? "true" : "false") << '\n';
                if (!bControlKept)
                {
                    outFailureReason = TEXT("カメラ移動で再投影可能な静止領域の履歴が保持されません");
                    return false;
                }
                return true;
            }

            if (m_Stage == CaptureStage::MoveStopped)
            {
                // 停止後に物体が去った領域でも、履歴が再び受け入れられて最大ageへ戻ることを確認する。
                const ScreenCircle vacated = ProjectCornellObject(ObjectMovedOffset, frame.Width, frame.Height);
                AgeRange region;
                if (vacated.bValid)
                {
                    for (uint32_t y = 0u; y < frame.Height; ++y)
                    {
                        for (uint32_t x = 0u; x < frame.Width; ++x)
                        {
                            const double px = static_cast<double>(x) + 0.5 - vacated.X;
                            const double py = static_cast<double>(y) + 0.5 - vacated.Y;
                            if (std::hypot(px, py) > vacated.Radius - 1.5)
                            {
                                continue;
                            }
                            ++region.Count;
                            if (ages[static_cast<size_t>(y) * frame.Width + x] >= RTGIHistoryMaximumAge)
                            {
                                ++region.KeptCount;
                            }
                        }
                    }
                }
                const uint64_t elapsed = frame.FrameNumber - m_PreviousStageLastFrame;
                m_StopLastElapsed = elapsed;
                ++m_StopSample;
                const bool bSettled = elapsed >= StopSettleRenderedFrames;
                m_bStopped = region.Count > 0u && region.KeptFraction() >= MinimumKeptFraction &&
                             bControlKept;
                std::cout << "R6_MOVE_THEN_STOP frame=" << frame.FrameNumber
                          << " elapsed=" << elapsed
                          << " vacated_pixels=" << region.Count
                          << " vacated_kept_fraction=" << region.KeptFraction()
                          << " control_kept_fraction=" << control.KeptFraction()
                          << " history_rebuilt=" << (m_bStopped ? "true" : "false");
                std::cout << std::endl;
                if (bSettled && !m_bStopped)
                {
                    outFailureReason = TEXT("移動後停止で物体が去った領域の履歴が再蓄積されません");
                    return false;
                }
                return true;
            }

            // 物体移動の前後の球をエンジンと同じカメラ行列で投影し、旧位置の円の内側かつ
            // 新位置の円の外側（境界から余白を取る）を、露出して履歴を捨てるべき画素とする。
            const ScreenCircle previous = ProjectCornellObject(ObjectVisibleOffset, frame.Width, frame.Height);
            const ScreenCircle current = ProjectCornellObject(ObjectMovedOffset, frame.Width, frame.Height);
            if (!previous.bValid || !current.bValid)
            {
                outFailureReason = TEXT("物体の画面投影を計算できません");
                return false;
            }
            constexpr double BoundaryMarginPixels = 1.5;
            AgeRange exposed;
            exposed.Minimum = std::numeric_limits<float>::infinity();
            exposed.Maximum = -std::numeric_limits<float>::infinity();
            for (uint32_t y = 0u; y < frame.Height; ++y)
            {
                for (uint32_t x = 0u; x < frame.Width; ++x)
                {
                    const double px = static_cast<double>(x) + 0.5;
                    const double py = static_cast<double>(y) + 0.5;
                    const double previousDistance = std::hypot(px - previous.X, py - previous.Y);
                    const double currentDistance = std::hypot(px - current.X, py - current.Y);
                    if (previousDistance > previous.Radius - BoundaryMarginPixels ||
                        currentDistance < current.Radius + BoundaryMarginPixels)
                    {
                        continue;
                    }
                    const float age = ages[static_cast<size_t>(y) * frame.Width + x];
                    exposed.Minimum = std::min(exposed.Minimum, age);
                    exposed.Maximum = std::max(exposed.Maximum, age);
                    ++exposed.Count;
                }
            }
            const uint64_t elapsed = frame.FrameNumber - m_PreviousStageLastFrame;
            // 棄却された画素は移動フレームでage 0から数え直すため、capture時点でも経過フレーム数以下に留まる。
            constexpr uint32_t MinimumExposedPixels = 8u;
            const bool bExposedRejected = exposed.Count >= MinimumExposedPixels &&
                                          std::isfinite(exposed.Maximum) &&
                                          exposed.Maximum <= static_cast<float>(elapsed) &&
                                          exposed.Maximum < RTGIHistoryMaximumAge;
            m_bObjectMoved = bControlKept && bExposedRejected;
            std::cout << "R6_OBJECT_MOVE frame=" << frame.FrameNumber
                      << " elapsed=" << elapsed
                      << " previous_circle=" << previous.X << ',' << previous.Y << ',' << previous.Radius
                      << " current_circle=" << current.X << ',' << current.Y << ',' << current.Radius
                      << " exposed_pixels=" << exposed.Count
                      << " exposed_age_max=" << exposed.Maximum
                      << " control_kept_fraction=" << control.KeptFraction()
                      << " history_rejection=" << (bExposedRejected ? "observed" : "missing")
                      << '\n';
            if (!m_bObjectMoved)
            {
                outFailureReason = TEXT("物体移動で露出した領域の履歴棄却をageで確認できません");
                return false;
            }
            return true;
        }

        /**
         * @brief 収束後の変化量を分母に、期限内に80%へ到達したかを判定する
         */
        bool EvaluateLightFollow(String& outFailureReason)
        {
            double baselineMean = 0.0;
            for (double value : m_LightBaselineValues)
            {
                baselineMean += value;
            }
            baselineMean /= static_cast<double>(LightBaselineSamples);
            double variance = 0.0;
            for (double value : m_LightBaselineValues)
            {
                variance += (value - baselineMean) * (value - baselineMean);
            }
            const double noise = std::sqrt(variance / static_cast<double>(LightBaselineSamples - 1u));
            const double converged = m_LightConvergedCount > 0u
                ? m_LightConvergedSum / static_cast<double>(m_LightConvergedCount)
                : std::numeric_limits<double>::quiet_NaN();
            const double finalChange = converged - baselineMean;
            const double deadlineChange = m_LightDeadlineValue - baselineMean;
            const double progress = std::abs(finalChange) > 0.0
                ? deadlineChange / finalChange
                : 0.0;
            const bool bSignal = std::isfinite(finalChange) &&
                                 std::abs(finalChange) >= LightSignalToNoise * noise &&
                                 std::abs(finalChange) > 0.0;
            const bool bPassed = m_bLightDeadlineSampled && bSignal &&
                                 progress >= MinimumLightProgress;
            std::cout << "R6_LIGHT_FOLLOWUP=" << (bPassed ? "PASS" : "FAIL")
                      << " deadline_elapsed=" << m_LightDeadlineElapsed
                      << " progress=" << progress
                      << " final_change=" << finalChange
                      << " deadline_change=" << deadlineChange
                      << " baseline_noise=" << noise
                      << " converged_samples=" << m_LightConvergedCount << '\n';
            if (!bPassed)
            {
                outFailureReason = TEXT("ライト移動後4 rendered frame以内に収束変化量の80%へ追従しません");
                return false;
            }
            return true;
        }
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
