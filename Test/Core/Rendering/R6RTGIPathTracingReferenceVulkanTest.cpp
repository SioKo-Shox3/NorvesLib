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
#include "RenderingValidation/RasterPathTracingComparison.h"
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
        if (!MeasureFlip(single, ScaleComponent(direct, single, 1.0 + IndirectYardstick), allPixels,
                         BlockSize, plus) ||
            !MeasureFlip(single, ScaleComponent(direct, single, 1.0 - IndirectYardstick), allPixels,
                         BlockSize, minus) ||
            !MeasureFlip(single, ScaleComponent(direct, single, 1.0 + IndirectSanity), allPixels,
                         BlockSize, sanityPlus) ||
            !MeasureFlip(single, ScaleComponent(direct, single, 1.0 - IndirectSanity), allPixels,
                         BlockSize, sanityMinus) ||
            !MeasureFlip(single, raster, agreement, BlockSize, rasterMeasurement) ||
            !MeasureFlip(full, raster, agreement, BlockSize, fullVsRaster) ||
            !MeasureFlip(full, single, allPixels, BlockSize, fullVsSingle) ||
            !MeasureFlip(single,
                         AddLocalLeak(single, MeanLuminance(single), agreement, BlockSize,
                                      LeakPatchSize, LeakScale),
                         agreement, BlockSize, localLeak) ||
            !MeasureFlip(single, rasterNeural, agreement, BlockSize, neuralMeasurement))
        {
            std::cerr << "FLIPを評価できません\n";
            return 1;
        }
        const double meanLimit = std::min(plus.Mean, minus.Mean);
        const float pixelLimit = std::min(plus.PixelMax, minus.PixelMax);
        const float blockLimit = std::min(plus.BlockMax, minus.BlockMax);
        PrintFlipMeasurement("yardstick_indirect_plus20", plus);
        PrintFlipMeasurement("yardstick_indirect_minus20", minus);
        PrintFlipMeasurement("sanity_indirect_plus40", sanityPlus);
        PrintFlipMeasurement("sanity_indirect_minus40", sanityMinus);
        PrintFlipMeasurement("negative_local_leak", localLeak);
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
        PrintFlipMeasurement("r6_vs_single_diffuse_bounce", rasterMeasurement);
        // 画素単位の閾値を超える一致画素の数と、16画素以上離れた上位の位置（診断用）。
        PrintAgreeingPixelsOverLimit(rasterMeasurement, pixelLimit, raster.Width);
        PrintFlipMeasurement("diagnostic_r6_neural_direct_vs_single_diffuse_bounce", neuralMeasurement);
        PrintFlipMeasurement("info_r6_vs_full_transport", fullVsRaster);
        PrintFlipMeasurement("info_single_diffuse_bounce_vs_full_transport", fullVsSingle);
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
