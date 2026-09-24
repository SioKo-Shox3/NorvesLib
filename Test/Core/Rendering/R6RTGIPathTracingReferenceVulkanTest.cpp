// R6 RTGIの静止収束SceneColorを、同じCornellシーンの自前PT参照と知覚差（LDR-FLIP）で比べる。
//
// 取得（GPU）: R6受入れの静止段階と同じCornell fixture・camera・光源で、ラスタ（RTGI）または
// パストレーサー（--renderer=path-tracing、輸送範囲は--path-tracing-transport）のSceneColorを取得し、
// --r6-reference-dumpへfloat画像として書き出す。
// 幾何（GPU）: --r6-reference-debug-view=normal|depth でラスタのGBufferの法線・距離の検証表示を、
// PTは--path-tracing-debug-output=shading-normal|hit-distance で1次命中の法線・距離を取得する（PTは
// 画素中心から出すため、累積する試料数によらず同じ交差になる）。ラスタには物体IDがないため、物体の
// 同一性は同じ位置（距離）と向き（法線）の一致で代える（距離1%以内で同じ向きの別物体は区別しない）。
// 比較（CPU）: --compare-dumps=<dir> でラスタ（直接光は解析BRDF）とPT参照（直接光のみ・拡散1バウンス・
// 多重散乱）と幾何の画像を読み、R6の申告範囲（拡散1バウンス）に合わせたPT参照（pt-single）と比べる。
// 判定は原寸のFLIP平均、原寸の画素単位FLIP最大、8x8区画平均画像のFLIP最大の三つ。画素単位最大は、
// ラスタのGBufferとPTの1次命中の距離（1%以内）と法線（内積0.99以上）が一致する画素だけで判定し、
// 一致しない画素（ラスタ化と光線交差で判定が分かれる縁など）は数と最大を記録する。ニューラルBRDFの
// ラスタ（raster-rtgi-neural）は診断として同じ値を記録する。
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Engine/Engine.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/RenderTypes.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingPerceptualDiff.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "R6RTGIPathTracingReferenceVulkanTest";
    // R6受入れの静止段階と同じ点光源の強さ。
    constexpr float R6PointLightIntensity = 1200.0f;
    // 静止カメラで収束させた画像を比べる。静止が16 frame続くと履歴の年齢の上限が1 frameに1ずつ64まで
    // 上がる（計56 frame）ため、その後に上限の年齢の4倍を待ってから取得する。
    constexpr uint64_t RasterConvergedRenderedFrames = 16u + 56u + 4u * 64u;
    // 補助の判定に使う区画の大きさ（縁のaliasingとPTの残留雑音を区画内で均した局所の差を見る）。
    constexpr uint32_t BlockSize = 8u;
    // 局所欠陥の負の対照: 参照の最も暗い1画素へ加える光漏れ（画像の平均輝度の倍率）。全体平均と
    // 8x8区画平均では閾値内に埋もれ、画素単位の最大だけが閾値の外に出る大きさにする。
    constexpr uint32_t LeakPatchSize = 1u;
    constexpr double LeakScale = 1.0;
    // 閾値の物差し: 参照の間接光成分を一様に±20%変えた画像と参照との知覚差。
    constexpr double IndirectYardstick = 0.2;
    // 物差しの単調性を確かめる、閾値の外側にあるべき変化量。
    constexpr double IndirectSanity = 0.4;

    DDGIVolumeParameters MakeCornellVolume()
    {
        DDGIVolumeParameters parameters;
        parameters.bEnabled = false;
        parameters.Origin = Math::Vector3(-0.1f, -0.1f, -0.1f);
        parameters.ProbeSpacing = Math::Vector3(0.82f, 0.82f, 0.82f);
        parameters.ProbeCountX = 8u;
        parameters.ProbeCountY = 8u;
        parameters.ProbeCountZ = 8u;
        return parameters;
    }

    class ReferenceHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnPreInitialize(const VariableArray<String>& args) override
        {
            m_DumpPath.clear();
            m_FirstFrame = 0u;
            m_CaptureCount = 0u;
            m_bDone = false;
            m_bStateReady = true;
            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            return GetRunConfig().CaptureSource == FrameCaptureSourceKind::SceneColor &&
                   !m_DumpPath.empty();
        }

        bool OnInitialize() override
        {
            return RenderingValidationApplicationHandler::OnInitialize() &&
                   GetFixture().ApplyR4CornellFixture() &&
                   GetFixture().AddR6CornellDynamicObject();
        }

    protected:
        bool ParseAdditionalArgument(const String& argument, String& outFailureReason) override
        {
            const String prefix(TEXT("--r6-reference-dump="));
            if (argument.size() > prefix.size() && argument.substr(0, prefix.size()) == prefix)
            {
                m_DumpPath = argument.substr(prefix.size());
                return true;
            }
            const String debugViewPrefix(TEXT("--r6-reference-debug-view="));
            if (argument.size() > debugViewPrefix.size() &&
                argument.substr(0, debugViewPrefix.size()) == debugViewPrefix)
            {
                const String value = argument.substr(debugViewPrefix.size());
                if (value == TEXT("normal"))
                {
                    m_DebugView = DebugViewMode::GBufferNormal;
                    return true;
                }
                if (value == TEXT("depth"))
                {
                    m_DebugView = DebugViewMode::GBufferDepth;
                    return true;
                }
                if (value == TEXT("direct"))
                {
                    // 検証表示254: 環境光・RTGIなしの解析BRDFの直接光（診断用）。
                    m_DebugView = static_cast<DebugViewMode>(254u);
                    return true;
                }
                outFailureReason = TEXT("--r6-reference-debug-view はnormal・depth・directのどれかです");
                return false;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(argument,
                                                                                 outFailureReason);
        }

        void ApplyCaptureStageState(RenderWorld& renderWorld) override
        {
            // R6受入れの静止段階（RTGI有効、DDGI無効、物体・点光源は既定位置、天井の面光源あり）と同じ状態。
            renderWorld.SetMainCamera(GetFixture().GetR4CornellCamera());
            renderWorld.GetRenderingCoordinator().SetRTGIEnabled(true);
            renderWorld.SetDDGIVolumeParameters(MakeCornellVolume());
            m_bStateReady = GetFixture().SetR4CornellObjectOffsetX(0.0f) && m_bStateReady;
            m_bStateReady = GetFixture().SetR4CornellLightOffsetX(0.0f) && m_bStateReady;
            m_bStateReady =
                GetFixture().SetR4CornellPointLightState(0.0f, R6PointLightIntensity) && m_bStateReady;
            m_bStateReady = GetFixture().SetR4CornellEmitterVisible(true) && m_bStateReady;
            // 幾何の取得では、ラスタのGBufferの法線・距離の検証表示をSceneColorへ書く。
            renderWorld.SetDebugViewModeAll(m_DebugView);
        }

        bool EvaluateCapturedFrame(const CapturedFrame& frame, String& outFailureReason) override
        {
            if (!m_bStateReady)
            {
                outFailureReason = TEXT("R6 Cornell fixtureの状態を設定できません");
                return false;
            }
            if (m_CaptureCount == 0u)
            {
                m_FirstFrame = frame.FrameNumber;
            }
            ++m_CaptureCount;
            // ラスタはRTGIの時間方向の蓄積が収束するまで取り直す。PTは最小試料数で収束を保証する。
            if (!GetRunConfig().bPathTracing &&
                frame.FrameNumber - m_FirstFrame < RasterConvergedRenderedFrames)
            {
                return true;
            }
            RgbaFloatImage image;
            if (DecodeCapturedRgbaFloat(frame, image) != FloatImageStatus::Success ||
                FindFirstNonFinite(image).Kind != NonFiniteKind::None)
            {
                outFailureReason = TEXT("R6参照比較のSceneColorを読めないか有限でない値があります");
                return false;
            }
            if (!WriteRgbaFloatDump(m_DumpPath, image, frame.PathTracingSampleCount))
            {
                outFailureReason = TEXT("R6参照比較のSceneColorを書き出せません");
                return false;
            }
            std::cout << "r6_reference_capture renderer="
                      << (GetRunConfig().bPathTracing ? "path-tracing" : "raster-rtgi")
                      << " frame=" << frame.FrameNumber
                      << " converged_frames=" << frame.FrameNumber - m_FirstFrame
                      << " path_tracing_samples=" << frame.PathTracingSampleCount
                      << " size=" << image.Width << "x" << image.Height << '\n';
            m_bDone = true;
            return true;
        }

        bool RequestFollowupCapture(const CapturedFrame&, FrameCaptureRequest& outRequest) override
        {
            if (m_bDone)
            {
                return false;
            }
            outRequest.SourceKind = FrameCaptureSourceKind::SceneColor;
            return true;
        }

    private:
        String m_DumpPath;
        DebugViewMode m_DebugView = DebugViewMode::Normal;
        uint64_t m_FirstFrame = 0u;
        uint32_t m_CaptureCount = 0u;
        bool m_bDone = false;
        bool m_bStateReady = true;
    };

    TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return MakeShared<ReferenceHandler>();
    }

    // ---------------------------------------------------------------------
    // 比較
    // ---------------------------------------------------------------------

    struct FlipMeasurement
    {
        double Mean = 0.0;
        float PixelMax = 0.0f;
        uint32_t PixelX = 0u;
        uint32_t PixelY = 0u;
        float BlockMax = 0.0f;
        uint32_t BlockX = 0u;
        uint32_t BlockY = 0u;
        // 幾何が一致する画素だけの画素単位最大と、一致しない画素の最大（記録用）。
        float AgreeingPixelMax = 0.0f;
        uint32_t AgreeingPixelX = 0u;
        uint32_t AgreeingPixelY = 0u;
        float DisagreeingPixelMax = 0.0f;
        uint32_t DisagreeingPixelX = 0u;
        uint32_t DisagreeingPixelY = 0u;
        // 一致しない画素を参照の値へ置き換えた画像の画素ごとのFLIP誤差（診断用）。
        VariableArray<float> AgreeingErrorMap;
    };

    uint8_t EncodeSrgbByte(double linear)
    {
        const double clamped = std::clamp(linear, 0.0, 1.0);
        const double encoded = clamped <= 0.0031308 ? 12.92 * clamped
                                                    : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
        return static_cast<uint8_t>(std::lround(std::clamp(encoded, 0.0, 1.0) * 255.0));
    }

    // プリエクスポージャ済みのHDRをx/(1+x)で[0,1]へ写し、sRGBの8bitにする（両画像で同じ写像）。
    Rgba8Image ToneMapToLdr(const RgbaFloatImage& image)
    {
        Rgba8Image result;
        result.Width = image.Width;
        result.Height = image.Height;
        result.RowPitchBytes = image.Width * 4u;
        result.Pixels.resize(static_cast<size_t>(image.Width) * image.Height * 4u);
        for (size_t pixel = 0u; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const double value = std::max(0.0, static_cast<double>(image.Values[pixel * 4u + channel]));
                result.Pixels[pixel * 4u + channel] = EncodeSrgbByte(value / (1.0 + value));
            }
            result.Pixels[pixel * 4u + 3u] = 255u;
        }
        return result;
    }

    RgbaFloatImage DownsampleBlocks(const RgbaFloatImage& image)
    {
        RgbaFloatImage result;
        result.Width = image.Width / BlockSize;
        result.Height = image.Height / BlockSize;
        result.Values.resize(static_cast<size_t>(result.Width) * result.Height * 4u);
        for (uint32_t by = 0u; by < result.Height; ++by)
        {
            for (uint32_t bx = 0u; bx < result.Width; ++bx)
            {
                double sum[4] = {};
                for (uint32_t y = by * BlockSize; y < (by + 1u) * BlockSize; ++y)
                {
                    for (uint32_t x = bx * BlockSize; x < (bx + 1u) * BlockSize; ++x)
                    {
                        for (uint32_t channel = 0u; channel < 4u; ++channel)
                        {
                            sum[channel] += image.Values[(static_cast<size_t>(y) * image.Width + x) * 4u + channel];
                        }
                    }
                }
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    result.Values[(static_cast<size_t>(by) * result.Width + bx) * 4u + channel] =
                        static_cast<float>(sum[channel] / (BlockSize * BlockSize));
                }
            }
        }
        return result;
    }

    // agreementは画素ごとの幾何の一致（1=一致）。空なら全画素を一致として扱う。FLIPは近傍の画素も
    // 見て誤差を出すため、一致しない画素の差が隣の一致する画素の値へ広がる。画素単位最大は、一致
    // しない画素を参照の値へ置き換えた画像で求め、一致しない画素の影響を判定から外す。平均と区画は
    // 置き換えない画像で求める。
    bool MeasureFlip(const RgbaFloatImage& reference, const RgbaFloatImage& candidate,
                     const VariableArray<uint8_t>& agreement, FlipMeasurement& outMeasurement)
    {
        PerceptualDifferenceMetrics full;
        PerceptualDifferenceMetrics blocks;
        VariableArray<float> errorMap;
        if (CompareLdrFlip(ToneMapToLdr(reference), ToneMapToLdr(candidate), full, &errorMap) !=
                PerceptualDiffStatus::Success ||
            CompareLdrFlip(ToneMapToLdr(DownsampleBlocks(reference)),
                           ToneMapToLdr(DownsampleBlocks(candidate)), blocks) !=
                PerceptualDiffStatus::Success)
        {
            return false;
        }
        outMeasurement.Mean = full.MeanFlipError;
        outMeasurement.PixelMax = full.MaxFlipError;
        outMeasurement.PixelX = full.MaxFlipX;
        outMeasurement.PixelY = full.MaxFlipY;
        outMeasurement.BlockMax = blocks.MaxFlipError;
        outMeasurement.BlockX = blocks.MaxFlipX;
        outMeasurement.BlockY = blocks.MaxFlipY;
        // 一致しない画素の誤差（記録用、置き換えない画像のFLIP）。
        for (size_t index = 0u; index < errorMap.size(); ++index)
        {
            if (!agreement.empty() && agreement[index] == 0u &&
                errorMap[index] > outMeasurement.DisagreeingPixelMax)
            {
                outMeasurement.DisagreeingPixelMax = errorMap[index];
                outMeasurement.DisagreeingPixelX = static_cast<uint32_t>(index % reference.Width);
                outMeasurement.DisagreeingPixelY = static_cast<uint32_t>(index / reference.Width);
            }
        }
        RgbaFloatImage neutralized = candidate;
        bool bAnyDisagreeing = false;
        for (size_t index = 0u; index < agreement.size(); ++index)
        {
            if (agreement[index] == 0u)
            {
                bAnyDisagreeing = true;
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    neutralized.Values[index * 4u + channel] = reference.Values[index * 4u + channel];
                }
            }
        }
        PerceptualDifferenceMetrics agreeing = full;
        outMeasurement.AgreeingErrorMap = errorMap;
        if (bAnyDisagreeing &&
            CompareLdrFlip(ToneMapToLdr(reference), ToneMapToLdr(neutralized), agreeing,
                           &outMeasurement.AgreeingErrorMap) != PerceptualDiffStatus::Success)
        {
            return false;
        }
        // 置き換えた画素にも近傍から誤差が広がるため、最大は一致する画素だけで求める。一致しない
        // 画素の誤差は0にして、以後の診断の集計からも外す。
        outMeasurement.AgreeingPixelMax = 0.0f;
        for (size_t index = 0u; index < outMeasurement.AgreeingErrorMap.size(); ++index)
        {
            if (!agreement.empty() && agreement[index] == 0u)
            {
                outMeasurement.AgreeingErrorMap[index] = 0.0f;
                continue;
            }
            if (outMeasurement.AgreeingErrorMap[index] > outMeasurement.AgreeingPixelMax)
            {
                outMeasurement.AgreeingPixelMax = outMeasurement.AgreeingErrorMap[index];
                outMeasurement.AgreeingPixelX = static_cast<uint32_t>(index % reference.Width);
                outMeasurement.AgreeingPixelY = static_cast<uint32_t>(index / reference.Width);
            }
        }
        return true;
    }

    // ラスタの検証表示（法線は0.5n+0.5、距離はd/(d+25)）とPTの1次命中（法線・距離）から、画素ごとの
    // 幾何の一致を作る。両方とも不交差（0）の画素は一致とする。
    VariableArray<uint8_t> BuildGeometryAgreement(const RgbaFloatImage& rasterNormal,
                                                  const RgbaFloatImage& rasterDepth,
                                                  const RgbaFloatImage& pathNormal,
                                                  const RgbaFloatImage& pathDistance,
                                                  uint32_t& outDisagreeingPixels)
    {
        const size_t pixelCount = static_cast<size_t>(rasterNormal.Width) * rasterNormal.Height;
        VariableArray<uint8_t> agreement(pixelCount, 0u);
        outDisagreeingPixels = 0u;
        for (size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const float* encodedNormal = rasterNormal.Values.data() + pixel * 4u;
            const float encodedDepth = rasterDepth.Values[pixel * 4u];
            const float* traceNormal = pathNormal.Values.data() + pixel * 4u;
            const double traceDistance = pathDistance.Values[pixel * 4u];
            const bool bRasterHit = encodedDepth > 0.0f;
            const bool bTraceHit = traceDistance > 0.0;
            bool bAgree = !bRasterHit && !bTraceHit;
            if (bRasterHit && bTraceHit && encodedDepth < 1.0f)
            {
                const double rasterDistance = 25.0 * encodedDepth / (1.0 - encodedDepth);
                double rasterN[3] = {};
                double rasterLength = 0.0;
                double traceLength = 0.0;
                double dot = 0.0;
                for (uint32_t axis = 0u; axis < 3u; ++axis)
                {
                    rasterN[axis] = 2.0 * encodedNormal[axis] - 1.0;
                    rasterLength += rasterN[axis] * rasterN[axis];
                    traceLength += static_cast<double>(traceNormal[axis]) * traceNormal[axis];
                    dot += rasterN[axis] * traceNormal[axis];
                }
                const double cosine = rasterLength > 0.0 && traceLength > 0.0
                    ? dot / std::sqrt(rasterLength * traceLength)
                    : -1.0;
                bAgree = std::abs(rasterDistance - traceDistance) <= 0.01 * traceDistance &&
                         cosine >= 0.99;
            }
            agreement[pixel] = bAgree ? 1u : 0u;
            outDisagreeingPixels += bAgree ? 0u : 1u;
        }
        return agreement;
    }

    // direct + scale × (single - direct)
    RgbaFloatImage ScaleIndirect(const RgbaFloatImage& direct, const RgbaFloatImage& single, double scale)
    {
        RgbaFloatImage result = single;
        for (size_t index = 0u; index < result.Values.size(); ++index)
        {
            if (index % 4u == 3u)
            {
                continue;
            }
            const double indirect = static_cast<double>(single.Values[index]) - direct.Values[index];
            result.Values[index] = static_cast<float>(direct.Values[index] + scale * indirect);
        }
        return result;
    }

    // 参照の内側（外周8画素を除く）で幾何が一致する最も暗い画素へ、画像の平均輝度のLeakScale倍の
    // 光を足す。
    RgbaFloatImage AddLocalLeak(const RgbaFloatImage& image, double meanLuminance,
                                const VariableArray<uint8_t>& agreement)
    {
        uint32_t bestX = BlockSize;
        uint32_t bestY = BlockSize;
        double bestLuminance = 1.0e30;
        for (uint32_t y = BlockSize; y + BlockSize + LeakPatchSize <= image.Height; ++y)
        {
            for (uint32_t x = BlockSize; x + BlockSize + LeakPatchSize <= image.Width; ++x)
            {
                double sum = 0.0;
                bool bAllAgree = true;
                for (uint32_t dy = 0u; dy < LeakPatchSize; ++dy)
                {
                    for (uint32_t dx = 0u; dx < LeakPatchSize; ++dx)
                    {
                        const size_t pixelIndex = static_cast<size_t>(y + dy) * image.Width + x + dx;
                        bAllAgree = bAllAgree && (agreement.empty() || agreement[pixelIndex] != 0u);
                        const size_t offset = pixelIndex * 4u;
                        sum += 0.2126 * image.Values[offset] + 0.7152 * image.Values[offset + 1u] +
                               0.0722 * image.Values[offset + 2u];
                    }
                }
                if (bAllAgree && sum < bestLuminance)
                {
                    bestLuminance = sum;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        RgbaFloatImage result = image;
        for (uint32_t dy = 0u; dy < LeakPatchSize; ++dy)
        {
            for (uint32_t dx = 0u; dx < LeakPatchSize; ++dx)
            {
                const size_t offset = (static_cast<size_t>(bestY + dy) * image.Width + bestX + dx) * 4u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    result.Values[offset + channel] +=
                        static_cast<float>(LeakScale * meanLuminance);
                }
            }
        }
        std::cout << "local_leak_patch x=" << bestX << " y=" << bestY
                  << " size=" << LeakPatchSize << " added=" << LeakScale * meanLuminance << '\n';
        return result;
    }

    double MeanLuminance(const RgbaFloatImage& image)
    {
        double sum = 0.0;
        for (size_t pixel = 0u; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
        {
            sum += 0.2126 * image.Values[pixel * 4u] + 0.7152 * image.Values[pixel * 4u + 1u] +
                   0.0722 * image.Values[pixel * 4u + 2u];
        }
        return sum / (static_cast<double>(image.Width) * image.Height);
    }

    void PrintMeasurement(const char* label, const FlipMeasurement& measurement)
    {
        std::cout << label << " mean_flip=" << measurement.Mean
                  << " pixel_max_flip=" << measurement.PixelMax
                  << " pixel=(" << measurement.PixelX << "," << measurement.PixelY << ")"
                  << " agreeing_pixel_max_flip=" << measurement.AgreeingPixelMax
                  << " agreeing_pixel=(" << measurement.AgreeingPixelX << ","
                  << measurement.AgreeingPixelY << ")"
                  << " disagreeing_pixel_max_flip=" << measurement.DisagreeingPixelMax
                  << " disagreeing_pixel=(" << measurement.DisagreeingPixelX << ","
                  << measurement.DisagreeingPixelY << ")"
                  << " block8_max_flip=" << measurement.BlockMax
                  << " block=(" << measurement.BlockX << "," << measurement.BlockY << ")\n";
    }

    int RunComparison(const String& directory)
    {
        const char* names[9] = {"raster-rtgi", "pt-direct", "pt-single", "pt-full",
                                "raster-rtgi-neural", "raster-normal", "raster-depth", "pt-normal",
                                "pt-depth"};
        RgbaFloatImage images[9];
        for (uint32_t index = 0u; index < 9u; ++index)
        {
            String path = directory;
            path += TEXT("/");
            path += names[index];
            path += TEXT(".nlrgba");
            uint32_t samples = 0u;
            if (!ReadRgbaFloatDump(path, images[index], samples) ||
                FindFirstNonFinite(images[index]).Kind != NonFiniteKind::None ||
                images[index].Width != images[0].Width || images[index].Height != images[0].Height ||
                images[index].Width % BlockSize != 0u || images[index].Height % BlockSize != 0u)
            {
                std::cerr << "R6参照比較の画像を読めません: " << names[index] << '\n';
                return 1;
            }
            std::cout << "r6_reference_image name=" << names[index] << " samples=" << samples
                      << " mean_luminance=" << MeanLuminance(images[index]) << '\n';
        }
        const RgbaFloatImage& raster = images[0];
        const RgbaFloatImage& direct = images[1];
        const RgbaFloatImage& single = images[2];
        const RgbaFloatImage& full = images[3];
        const RgbaFloatImage& rasterNeural = images[4];
        uint32_t disagreeingPixels = 0u;
        const VariableArray<uint8_t> agreement =
            BuildGeometryAgreement(images[5], images[6], images[7], images[8], disagreeingPixels);
        const VariableArray<uint8_t> allPixels;
        std::cout << "geometry_agreement disagreeing_pixels=" << disagreeingPixels
                  << " fraction=" << static_cast<double>(disagreeingPixels) / agreement.size() << '\n';

        // 閾値: 参照（拡散1バウンスのPT）の間接光を一様に±20%変えた画像の知覚差のうち小さい方。
        FlipMeasurement plus;
        FlipMeasurement minus;
        FlipMeasurement sanityPlus;
        FlipMeasurement sanityMinus;
        FlipMeasurement rasterMeasurement;
        FlipMeasurement fullVsRaster;
        FlipMeasurement fullVsSingle;
        FlipMeasurement localLeak;
        FlipMeasurement neuralMeasurement;
        // 閾値の物差しは従来どおり画像全体の値を使う（数値は変えない）。
        if (!MeasureFlip(single, ScaleIndirect(direct, single, 1.0 + IndirectYardstick), allPixels, plus) ||
            !MeasureFlip(single, ScaleIndirect(direct, single, 1.0 - IndirectYardstick), allPixels, minus) ||
            !MeasureFlip(single, ScaleIndirect(direct, single, 1.0 + IndirectSanity), allPixels, sanityPlus) ||
            !MeasureFlip(single, ScaleIndirect(direct, single, 1.0 - IndirectSanity), allPixels, sanityMinus) ||
            !MeasureFlip(single, raster, agreement, rasterMeasurement) ||
            !MeasureFlip(full, raster, agreement, fullVsRaster) ||
            !MeasureFlip(full, single, allPixels, fullVsSingle) ||
            !MeasureFlip(single, AddLocalLeak(single, MeanLuminance(single), agreement), agreement,
                         localLeak) ||
            !MeasureFlip(single, rasterNeural, agreement, neuralMeasurement))
        {
            std::cerr << "FLIPを評価できません\n";
            return 1;
        }
        const double meanLimit = std::min(plus.Mean, minus.Mean);
        const float pixelLimit = std::min(plus.PixelMax, minus.PixelMax);
        const float blockLimit = std::min(plus.BlockMax, minus.BlockMax);
        PrintMeasurement("yardstick_indirect_plus20", plus);
        PrintMeasurement("yardstick_indirect_minus20", minus);
        PrintMeasurement("sanity_indirect_plus40", sanityPlus);
        PrintMeasurement("sanity_indirect_minus40", sanityMinus);
        PrintMeasurement("negative_local_leak", localLeak);
        std::cout << "r6_reference_threshold mean_flip<=" << meanLimit
                  << " pixel_max_flip<=" << pixelLimit
                  << " block8_max_flip<=" << blockLimit << '\n';

        // 物差しが変化量に対して単調で、より大きな誤差を閾値の外に置くこと。1画素の光漏れは全体平均と
        // 8x8区画平均では閾値内に埋もれ、原寸の画素単位最大だけで閾値の外に出ること。
        const bool bLeakPixelOnly = localLeak.Mean <= meanLimit && localLeak.BlockMax <= blockLimit &&
                                    localLeak.AgreeingPixelMax > pixelLimit;
        const bool bSanity = sanityPlus.Mean > meanLimit && sanityMinus.Mean > meanLimit &&
                             sanityPlus.PixelMax > pixelLimit && sanityMinus.PixelMax > pixelLimit &&
                             sanityPlus.BlockMax > blockLimit && sanityMinus.BlockMax > blockLimit &&
                             bLeakPixelOnly;
        std::cout << "negative_local_leak mean_within_limit=" << (localLeak.Mean <= meanLimit ? 1 : 0)
                  << " block_within_limit=" << (localLeak.BlockMax <= blockLimit ? 1 : 0)
                  << " pixel_max_outside_limit=" << (localLeak.AgreeingPixelMax > pixelLimit ? 1 : 0) << '\n';

        const double directLuminance = MeanLuminance(direct);
        const double singleIndirect = MeanLuminance(single) - directLuminance;
        const double rasterIndirect = MeanLuminance(raster) - directLuminance;
        const double fullIndirect = MeanLuminance(full) - directLuminance;
        PrintMeasurement("r6_vs_single_diffuse_bounce", rasterMeasurement);
        // 画素単位の閾値を超える一致画素の数と、16画素以上離れた上位の位置（診断用）。
        {
            const uint32_t width = raster.Width;
            uint32_t overLimit = 0u;
            for (float error : rasterMeasurement.AgreeingErrorMap)
            {
                overLimit += error > pixelLimit ? 1u : 0u;
            }
            std::cout << "diagnostic_agreeing_pixels_over_limit count=" << overLimit << " fraction="
                      << static_cast<double>(overLimit) / rasterMeasurement.AgreeingErrorMap.size();
            VariableArray<uint32_t> peaks;
            for (uint32_t rank = 0u; rank < 8u; ++rank)
            {
                float best = 0.0f;
                uint32_t bestIndex = UINT32_MAX;
                for (uint32_t index = 0u; index < rasterMeasurement.AgreeingErrorMap.size(); ++index)
                {
                    const float error = rasterMeasurement.AgreeingErrorMap[index];
                    if (error <= pixelLimit || error <= best)
                    {
                        continue;
                    }
                    bool bNearPeak = false;
                    for (uint32_t peak : peaks)
                    {
                        const int32_t dx = static_cast<int32_t>(index % width) - static_cast<int32_t>(peak % width);
                        const int32_t dy = static_cast<int32_t>(index / width) - static_cast<int32_t>(peak / width);
                        bNearPeak = bNearPeak || (std::abs(dx) < 16 && std::abs(dy) < 16);
                    }
                    if (!bNearPeak)
                    {
                        best = error;
                        bestIndex = index;
                    }
                }
                if (bestIndex == UINT32_MAX)
                {
                    break;
                }
                peaks.push_back(bestIndex);
                std::cout << " peak=(" << bestIndex % width << "," << bestIndex / width << "):" << best;
            }
            std::cout << '\n';
        }
        PrintMeasurement("diagnostic_r6_neural_direct_vs_single_diffuse_bounce", neuralMeasurement);
        PrintMeasurement("info_r6_vs_full_transport", fullVsRaster);
        PrintMeasurement("info_single_diffuse_bounce_vs_full_transport", fullVsSingle);
        std::cout << "indirect_mean_luminance single_diffuse_bounce=" << singleIndirect
                  << " r6_minus_pt_direct=" << rasterIndirect
                  << " full_transport=" << fullIndirect
                  << " r6_over_single=" << (singleIndirect > 0.0 ? rasterIndirect / singleIndirect : 0.0)
                  << " full_over_single=" << (singleIndirect > 0.0 ? fullIndirect / singleIndirect : 0.0)
                  << '\n';

        const bool bPassed = bSanity && rasterMeasurement.Mean <= meanLimit &&
                             rasterMeasurement.AgreeingPixelMax <= pixelLimit &&
                             rasterMeasurement.BlockMax <= blockLimit;
        std::cout << "r6_reference_comparison=" << (bPassed ? "PASS" : "FAIL")
                  << " sanity=" << (bSanity ? "PASS" : "FAIL") << '\n';
        if (!bSanity)
        {
            std::cerr << "閾値の物差しが変化量に対して単調でないか、局所欠陥を検出できません\n";
        }
        return bPassed ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    for (int index = 1; index < argc; ++index)
    {
        if (std::strncmp(argv[index], "--compare-dumps=", 16u) == 0)
        {
            return RunComparison(Core::Container::String(argv[index] + 16));
        }
    }

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip(TestName, "環境変数によりGPU検証をスキップします");
    }
    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
    }
    bool bPathTracing = false;
    for (int index = 1; index < argc; ++index)
    {
        bPathTracing = bPathTracing || std::strcmp(argv[index], "--renderer=path-tracing") == 0;
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
    // ラスタはR6 RTGI（ray query）、PTはPathTracingPassの必要機能を求める。
    const bool bSupported = bPathTracing
        ? Core::Rendering::PathTracingPass::IsSupported(capabilities)
        : capabilities.RayTracing.bAccelerationStructure && capabilities.RayTracing.bRayQuery &&
              capabilities.bBufferDeviceAddress && capabilities.bShaderInt64;
    if (!bSupported)
    {
        return ReportGpuTestSkip(TestName, "R6 RTGIまたはパストレーサーに必要なVulkan機能を利用できません");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("R6 RTGI PT参照比較");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = false;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("R6RTGIPathTracingReferenceVulkan.log");
    config.CreateHandler = &CreateHandler;
    config.Arguments.push_back(TEXT("--scene=indoor"));
    config.Arguments.push_back(TEXT("--capture-source=scene-color"));
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
