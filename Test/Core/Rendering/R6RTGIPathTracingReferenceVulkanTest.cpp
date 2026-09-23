// R6 RTGIの静止収束SceneColorを、同じCornellシーンの自前PT参照と知覚差（LDR-FLIP）で比べる。
//
// 取得（GPU）: R6受入れの静止段階と同じCornell fixture・camera・光源で、ラスタ（RTGI）または
// パストレーサー（--renderer=path-tracing、輸送範囲は--path-tracing-transport）のSceneColorを取得し、
// --r6-reference-dumpへfloat画像として書き出す。
// 比較（CPU）: --compare-dumps=<dir> で4枚（raster-rtgi・pt-direct・pt-single・pt-full）を読み、
// R6の申告範囲（拡散1バウンス）に合わせたPT参照（pt-single）と比べる。
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Engine/Engine.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
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
    // RTGI履歴の最大age（8 rendered frame）の3倍待ってから取得する。
    constexpr uint64_t RasterConvergedRenderedFrames = 24u;
    // 画素単位の判定は8x8区画の平均で行う（縁のaliasingとPTの残留雑音を区画内で均す）。
    constexpr uint32_t BlockSize = 8u;
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
        float BlockMax = 0.0f;
        uint32_t BlockX = 0u;
        uint32_t BlockY = 0u;
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

    bool MeasureFlip(const RgbaFloatImage& reference, const RgbaFloatImage& candidate,
                     FlipMeasurement& outMeasurement)
    {
        PerceptualDifferenceMetrics full;
        PerceptualDifferenceMetrics blocks;
        if (CompareLdrFlip(ToneMapToLdr(reference), ToneMapToLdr(candidate), full) !=
                PerceptualDiffStatus::Success ||
            CompareLdrFlip(ToneMapToLdr(DownsampleBlocks(reference)),
                           ToneMapToLdr(DownsampleBlocks(candidate)), blocks) !=
                PerceptualDiffStatus::Success)
        {
            return false;
        }
        outMeasurement.Mean = full.MeanFlipError;
        outMeasurement.BlockMax = blocks.MaxFlipError;
        outMeasurement.BlockX = blocks.MaxFlipX;
        outMeasurement.BlockY = blocks.MaxFlipY;
        return true;
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
                  << " block8_max_flip=" << measurement.BlockMax
                  << " block=(" << measurement.BlockX << "," << measurement.BlockY << ")\n";
    }

    int RunComparison(const String& directory)
    {
        const char* names[4] = {"raster-rtgi", "pt-direct", "pt-single", "pt-full"};
        RgbaFloatImage images[4];
        for (uint32_t index = 0u; index < 4u; ++index)
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

        // 閾値: 参照（拡散1バウンスのPT）の間接光を一様に±20%変えた画像の知覚差のうち小さい方。
        FlipMeasurement plus;
        FlipMeasurement minus;
        FlipMeasurement sanityPlus;
        FlipMeasurement sanityMinus;
        FlipMeasurement rasterMeasurement;
        FlipMeasurement fullVsRaster;
        FlipMeasurement fullVsSingle;
        if (!MeasureFlip(single, ScaleIndirect(direct, single, 1.0 + IndirectYardstick), plus) ||
            !MeasureFlip(single, ScaleIndirect(direct, single, 1.0 - IndirectYardstick), minus) ||
            !MeasureFlip(single, ScaleIndirect(direct, single, 1.0 + IndirectSanity), sanityPlus) ||
            !MeasureFlip(single, ScaleIndirect(direct, single, 1.0 - IndirectSanity), sanityMinus) ||
            !MeasureFlip(single, raster, rasterMeasurement) ||
            !MeasureFlip(full, raster, fullVsRaster) ||
            !MeasureFlip(full, single, fullVsSingle))
        {
            std::cerr << "FLIPを評価できません\n";
            return 1;
        }
        const double meanLimit = std::min(plus.Mean, minus.Mean);
        const float blockLimit = std::min(plus.BlockMax, minus.BlockMax);
        PrintMeasurement("yardstick_indirect_plus20", plus);
        PrintMeasurement("yardstick_indirect_minus20", minus);
        PrintMeasurement("sanity_indirect_plus40", sanityPlus);
        PrintMeasurement("sanity_indirect_minus40", sanityMinus);
        std::cout << "r6_reference_threshold mean_flip<=" << meanLimit
                  << " block8_max_flip<=" << blockLimit << '\n';

        // 物差しが変化量に対して単調で、より大きな誤差を閾値の外に置くこと。
        const bool bSanity = sanityPlus.Mean > meanLimit && sanityMinus.Mean > meanLimit &&
                             sanityPlus.BlockMax > blockLimit && sanityMinus.BlockMax > blockLimit;

        const double directLuminance = MeanLuminance(direct);
        const double singleIndirect = MeanLuminance(single) - directLuminance;
        const double rasterIndirect = MeanLuminance(raster) - directLuminance;
        const double fullIndirect = MeanLuminance(full) - directLuminance;
        PrintMeasurement("r6_vs_single_diffuse_bounce", rasterMeasurement);
        PrintMeasurement("info_r6_vs_full_transport", fullVsRaster);
        PrintMeasurement("info_single_diffuse_bounce_vs_full_transport", fullVsSingle);
        std::cout << "indirect_mean_luminance single_diffuse_bounce=" << singleIndirect
                  << " r6_minus_pt_direct=" << rasterIndirect
                  << " full_transport=" << fullIndirect
                  << " r6_over_single=" << (singleIndirect > 0.0 ? rasterIndirect / singleIndirect : 0.0)
                  << " full_over_single=" << (singleIndirect > 0.0 ? fullIndirect / singleIndirect : 0.0)
                  << '\n';

        const bool bPassed = bSanity && rasterMeasurement.Mean <= meanLimit &&
                             rasterMeasurement.BlockMax <= blockLimit;
        std::cout << "r6_reference_comparison=" << (bPassed ? "PASS" : "FAIL")
                  << " sanity=" << (bSanity ? "PASS" : "FAIL") << '\n';
        if (!bSanity)
        {
            std::cerr << "閾値の物差しが変化量に対して単調ではありません\n";
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
