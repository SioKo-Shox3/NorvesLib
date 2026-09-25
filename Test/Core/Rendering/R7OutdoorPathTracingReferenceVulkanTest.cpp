// R7-O3: 空・霧を含む屋外シーンのラスタとPTを、R2と同じ3時刻（朝・昼・夕）で知覚差（LDR-FLIP）で比べる。
//
// 取得（GPU）: 屋外の検証シーン（地面と5つの球）の方向光を消し、空（R2の時刻）と高さフォグ（R3の中間の
// 密度）を有効にして、空の太陽だけで照らす。ラスタ（RTGI、直接光は解析BRDF）またはPT
// （--renderer=path-tracing）のSceneColorを--r7-outdoor-dumpへ書く。時刻は--r7-outdoor-time。
// 露出は晴天の屋外（f/16、1/125 s、ISO 100、EV100はおよそ15）。
// 幾何（GPU）: --r7-outdoor-time=geometry は空と霧を切り、ラスタのGBufferの法線・距離の検証表示
// （--r7-outdoor-debug-view=normal|depth）、PTの1次命中の法線・距離を取得する。
// 比較（CPU）: --compare-dumps=<dir> で各時刻のラスタとPT（拡散2バウンス・全輸送・直接光のみ）を読み、
// ラスタが実装する輸送（R6と同じ規則。RTGIの拡散2バウンスで、散乱光線の命中面も拡散葉だけ）のPT参照
// （pt-two）と比べる。PTの直接光のみは、1次光線の空と霧、1次命中の発光と光源標本（太陽円盤を含む）までを
// 数える。閾値の物差しは、参照のうち直接光のみに入らない成分（面が受ける空の光と相互反射）を一様に±20%
// 変えた画像の知覚差のうち小さい方。判定は原寸のFLIP平均、一致画素の原寸の画素単位FLIP最大、8x8区画平均の
// FLIP最大の三つで、3時刻すべてで閾値内なら合格。画素単位最大の一致画素は、幾何（法線・距離）と太陽の可視
// （ラスタのCSMの係数とPTの光線、--r7-outdoor-debug-view=sun-visibility と --path-tracing-debug-output=
// sun-visibility）がともに一致する画素で、影の縁で判定が分かれる画素は数と最大を記録する。全輸送（pt-full）
// との差は、3回目以降のバウンスと光沢のある相互反射という既知差として量を記録する。
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Component/CameraComponent.h"
#include "Engine/Engine.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RasterPathTracingComparison.h"
#include "RenderingValidation/RenderingFloatImage.h"
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

    constexpr const char* TestName = "R7OutdoorPathTracingReferenceVulkanTest";

    struct OutdoorTime
    {
        const char* Name = nullptr;
        float SunAltitudeDegrees = 0.0f;
        float SunAzimuthDegrees = 0.0f;
    };

    // R2の時刻sweepと同じ3点。
    constexpr OutdoorTime OutdoorTimes[] = {
        {"morning", 8.0f, -35.0f},
        {"noon", 45.0f, 0.0f},
        {"evening", 15.0f, 35.0f}};
    constexpr uint32_t OutdoorTimeCount = static_cast<uint32_t>(sizeof(OutdoorTimes) / sizeof(OutdoorTimes[0]));
    // R3の3段階の密度の中間。
    constexpr float FogDensity = 0.005f;
    // 静止カメラで収束させた画像を比べる。RTGIの静止時の履歴延長（16 frameの後に64まで）の後、上限の
    // 年齢の4倍を待つ（R6参照比較と同じ）。空由来のIBLの更新もこの間に済む。
    constexpr uint64_t RasterConvergedRenderedFrames = 16u + 56u + 4u * 64u;
    constexpr uint32_t BlockSize = 8u;
    // 局所欠陥の負の対照: 参照の最も暗い1画素へ、画像の平均輝度のLeakScale倍の光を足す。朝の画像は
    // 平均輝度が低く、R6参照比較と同じ1倍では画素単位の閾値を越えない（欠陥を検出できない）ため、
    // 全体平均と8x8区画平均では閾値内に埋もれたまま画素単位最大だけが閾値の外に出る4倍にする。
    constexpr uint32_t LeakPatchSize = 1u;
    constexpr double LeakScale = 4.0;
    // 閾値の物差し: 参照のうち直接光のみに入らない成分（空の光と相互反射）を一様に±20%変えた画像と
    // 参照との知覚差。
    // 太陽の可視の差がこれを超える画素を、影の縁で判定が分かれた画素として画素単位最大から除く。
    // PTの可視は64試料の平均で、割合0.5での標準偏差は1/16。その3倍（約0.19）を超える差は標本の揺らぎ
    // ではなく判定の分かれとみなす。
    constexpr float SunVisibilityTolerance = 0.2f;
    constexpr double SkyAndBounceYardstick = 0.2;
    // 物差しの単調性を確かめる、閾値の外側にあるべき変化量。
    constexpr double SkyAndBounceSanity = 0.4;

    const OutdoorTime* FindOutdoorTime(const String& name)
    {
        for (const OutdoorTime& time : OutdoorTimes)
        {
            if (name == String(time.Name))
            {
                return &time;
            }
        }
        return nullptr;
    }

    class ReferenceHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnPreInitialize(const VariableArray<String>& args) override
        {
            m_DumpPath.clear();
            m_pTime = nullptr;
            m_bGeometry = false;
            m_FirstFrame = 0u;
            m_CaptureCount = 0u;
            m_bDone = false;
            m_bStateReady = true;
            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            return GetRunConfig().CaptureSource == FrameCaptureSourceKind::SceneColor &&
                   !m_DumpPath.empty() && (m_pTime != nullptr || m_bGeometry);
        }

    protected:
        bool ParseAdditionalArgument(const String& argument, String& outFailureReason) override
        {
            const String dumpPrefix(TEXT("--r7-outdoor-dump="));
            if (argument.size() > dumpPrefix.size() && argument.substr(0, dumpPrefix.size()) == dumpPrefix)
            {
                m_DumpPath = argument.substr(dumpPrefix.size());
                return true;
            }
            const String timePrefix(TEXT("--r7-outdoor-time="));
            if (argument.size() > timePrefix.size() && argument.substr(0, timePrefix.size()) == timePrefix)
            {
                const String value = argument.substr(timePrefix.size());
                m_bGeometry = value == TEXT("geometry");
                m_pTime = FindOutdoorTime(value);
                if (!m_bGeometry && m_pTime == nullptr)
                {
                    outFailureReason = TEXT("--r7-outdoor-time はmorning・noon・evening・geometryのどれかです");
                    return false;
                }
                return true;
            }
            const String debugViewPrefix(TEXT("--r7-outdoor-debug-view="));
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
                if (value == TEXT("hard-shadow"))
                {
                    // 検証表示246: ランバートとCSMのハード影だけの直接光（影の診断用）。
                    m_DebugView = static_cast<DebugViewMode>(246u);
                    return true;
                }
                if (value == TEXT("sun-visibility"))
                {
                    // 検証表示245: 空の太陽の可視（CSMの係数。面が背を向ければ0）。
                    m_DebugView = static_cast<DebugViewMode>(245u);
                    return true;
                }
                outFailureReason =
                    TEXT("--r7-outdoor-debug-view はnormal・depth・hard-shadow・sun-visibilityのどれかです");
                return false;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(argument,
                                                                                 outFailureReason);
        }

        void ApplyCaptureStageState(RenderWorld& renderWorld) override
        {
            CameraProxy camera = GetFixture().GetCamera();
            m_bStateReady = Core::Component::CameraComponent::TryBuildExposureSnapshot(
                                16.0f, 1.0f / 125.0f, 100.0f, 0.0f, camera) &&
                            m_bStateReady;
            renderWorld.SetMainCamera(camera);
            // 空の太陽だけで照らす（シーンの方向光を残すと太陽が二つになる）。
            m_bStateReady = GetFixture().SetBaseLightActive(false) && m_bStateReady;
            renderWorld.GetRenderingCoordinator().SetRTGIEnabled(true);

            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = !m_bGeometry;
            if (m_pTime != nullptr)
            {
                sky.SunAltitudeDegrees = m_pTime->SunAltitudeDegrees;
                sky.SunAzimuthDegrees = m_pTime->SunAzimuthDegrees;
            }
            renderWorld.SetSkyAtmosphere(sky);
            VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
            // 太陽の可視の検証表示は、Lighting後に霧が掛ける減衰で値が変わらないよう霧を切る。
            fog.bEnabled = !m_bGeometry && m_DebugView != static_cast<DebugViewMode>(245u);
            fog.DensityAtBaseHeight = FogDensity;
            renderWorld.SetVolumetricFogParameters(fog);
            // 幾何の取得では、ラスタのGBufferの法線・距離の検証表示をSceneColorへ書く。
            renderWorld.SetDebugViewModeAll(m_DebugView);
        }

        bool EvaluateCapturedFrame(const CapturedFrame& frame, String& outFailureReason) override
        {
            if (!m_bStateReady)
            {
                outFailureReason = TEXT("R7屋外比較のシーンの状態を設定できません");
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
                outFailureReason = TEXT("R7屋外比較のSceneColorを読めないか有限でない値があります");
                return false;
            }
            if (!WriteRgbaFloatDump(m_DumpPath, image, frame.PathTracingSampleCount))
            {
                outFailureReason = TEXT("R7屋外比較のSceneColorを書き出せません");
                return false;
            }
            std::cout << "r7_outdoor_capture renderer="
                      << (GetRunConfig().bPathTracing ? "path-tracing" : "raster")
                      << " time=" << (m_pTime != nullptr ? m_pTime->Name : "geometry")
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
        const OutdoorTime* m_pTime = nullptr;
        bool m_bGeometry = false;
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

    bool LoadImage(const String& directory, const String& name, RgbaFloatImage& outImage)
    {
        String path = directory;
        path += TEXT("/");
        path += name;
        path += TEXT(".nlrgba");
        uint32_t samples = 0u;
        if (!ReadRgbaFloatDump(path, outImage, samples) ||
            FindFirstNonFinite(outImage).Kind != NonFiniteKind::None ||
            outImage.Width % BlockSize != 0u || outImage.Height % BlockSize != 0u)
        {
            std::cerr << "R7屋外比較の画像を読めません: " << name.c_str() << '\n';
            return false;
        }
        std::cout << "r7_outdoor_image name=" << name.c_str() << " samples=" << samples
                  << " mean_luminance=" << MeanLuminance(outImage) << '\n';
        return true;
    }

    // 1時刻の比較。閾値は参照（PTの拡散2バウンス）の空の光と相互反射を±20%変えた画像から求める。
    bool CompareTime(const OutdoorTime& time,
                     const RgbaFloatImage& raster,
                     const RgbaFloatImage& reference,
                     const RgbaFloatImage& full,
                     const RgbaFloatImage& direct,
                     const VariableArray<uint8_t>& agreement,
                     bool& outSanity)
    {
        const VariableArray<uint8_t> allPixels;
        FlipMeasurement plus;
        FlipMeasurement minus;
        FlipMeasurement sanityPlus;
        FlipMeasurement sanityMinus;
        FlipMeasurement localLeak;
        FlipMeasurement rasterMeasurement;
        FlipMeasurement fullMeasurement;
        FlipMeasurement referenceVsFull;
        FlipMeasurement directMeasurement;
        if (!MeasureFlip(reference, ScaleComponent(direct, reference, 1.0 + SkyAndBounceYardstick),
                         allPixels, BlockSize, plus) ||
            !MeasureFlip(reference, ScaleComponent(direct, reference, 1.0 - SkyAndBounceYardstick),
                         allPixels, BlockSize, minus) ||
            !MeasureFlip(reference, ScaleComponent(direct, reference, 1.0 + SkyAndBounceSanity),
                         allPixels, BlockSize, sanityPlus) ||
            !MeasureFlip(reference, ScaleComponent(direct, reference, 1.0 - SkyAndBounceSanity),
                         allPixels, BlockSize, sanityMinus) ||
            !MeasureFlip(reference,
                         AddLocalLeak(reference, MeanLuminance(reference), agreement, BlockSize,
                                      LeakPatchSize, LeakScale),
                         agreement, BlockSize, localLeak) ||
            !MeasureFlip(reference, raster, agreement, BlockSize, rasterMeasurement) ||
            !MeasureFlip(full, raster, agreement, BlockSize, fullMeasurement) ||
            !MeasureFlip(full, reference, agreement, BlockSize, referenceVsFull) ||
            !MeasureFlip(reference, direct, agreement, BlockSize, directMeasurement))
        {
            std::cerr << "FLIPを評価できません: " << time.Name << '\n';
            return false;
        }
        const double meanLimit = std::min(plus.Mean, minus.Mean);
        const float pixelLimit = std::min(plus.PixelMax, minus.PixelMax);
        const float blockLimit = std::min(plus.BlockMax, minus.BlockMax);
        const String prefix = String("r7_outdoor_") + String(time.Name);
        PrintFlipMeasurement((prefix + String("_yardstick_sky_bounce_plus20")).c_str(), plus);
        PrintFlipMeasurement((prefix + String("_yardstick_sky_bounce_minus20")).c_str(), minus);
        PrintFlipMeasurement((prefix + String("_sanity_sky_bounce_plus40")).c_str(), sanityPlus);
        PrintFlipMeasurement((prefix + String("_sanity_sky_bounce_minus40")).c_str(), sanityMinus);
        PrintFlipMeasurement((prefix + String("_negative_local_leak")).c_str(), localLeak);
        std::cout << prefix.c_str() << "_threshold mean_flip<=" << meanLimit
                  << " pixel_max_flip<=" << pixelLimit << " block8_max_flip<=" << blockLimit << '\n';

        const bool bLeakPixelOnly = localLeak.Mean <= meanLimit && localLeak.BlockMax <= blockLimit &&
                                    localLeak.AgreeingPixelMax > pixelLimit;
        outSanity = sanityPlus.Mean > meanLimit && sanityMinus.Mean > meanLimit &&
                    sanityPlus.PixelMax > pixelLimit && sanityMinus.PixelMax > pixelLimit &&
                    sanityPlus.BlockMax > blockLimit && sanityMinus.BlockMax > blockLimit &&
                    bLeakPixelOnly;

        PrintFlipMeasurement((prefix + String("_raster_vs_pt_two")).c_str(), rasterMeasurement);
        PrintAgreeingPixelsOverLimit(rasterMeasurement, pixelLimit, raster.Width);
        // 既知差: 全輸送との差（3回目以降のバウンスと光沢のある相互反射）。判定には使わない。
        PrintFlipMeasurement((prefix + String("_known_raster_vs_pt_full")).c_str(), fullMeasurement);
        PrintFlipMeasurement((prefix + String("_known_pt_two_vs_pt_full")).c_str(), referenceVsFull);
        // 参考: 直接光のみの画像との差（面が受ける空の光と相互反射の寄与の大きさ）。
        PrintFlipMeasurement((prefix + String("_info_pt_direct_vs_pt_two")).c_str(), directMeasurement);
        const double directLuminance = MeanLuminance(direct);
        std::cout << prefix.c_str() << "_mean_luminance raster=" << MeanLuminance(raster)
                  << " pt_two=" << MeanLuminance(reference) << " pt_full=" << MeanLuminance(full)
                  << " pt_direct=" << directLuminance
                  << " raster_sky_bounce=" << MeanLuminance(raster) - directLuminance
                  << " pt_two_sky_bounce=" << MeanLuminance(reference) - directLuminance
                  << " pt_full_sky_bounce=" << MeanLuminance(full) - directLuminance << '\n';

        const bool bPassed = outSanity && rasterMeasurement.Mean <= meanLimit &&
                             rasterMeasurement.AgreeingPixelMax <= pixelLimit &&
                             rasterMeasurement.BlockMax <= blockLimit;
        std::cout << prefix.c_str() << "_comparison=" << (bPassed ? "PASS" : "FAIL")
                  << " sanity=" << (outSanity ? "PASS" : "FAIL") << '\n';
        return bPassed;
    }

    int RunComparison(const String& directory)
    {
        RgbaFloatImage geometry[4];
        const char* geometryNames[4] = {"raster-normal", "raster-depth", "pt-normal", "pt-depth"};
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            if (!LoadImage(directory, String(geometryNames[index]), geometry[index]))
            {
                return 1;
            }
        }
        uint32_t disagreeingPixels = 0u;
        const VariableArray<uint8_t> geometryAgreement =
            BuildGeometryAgreement(geometry[0], geometry[1], geometry[2], geometry[3], disagreeingPixels);
        std::cout << "geometry_agreement disagreeing_pixels=" << disagreeingPixels
                  << " fraction=" << static_cast<double>(disagreeingPixels) / geometryAgreement.size()
                  << '\n';

        bool bAllPassed = true;
        bool bAllSanity = true;
        for (const OutdoorTime& time : OutdoorTimes)
        {
            RgbaFloatImage raster;
            RgbaFloatImage reference;
            RgbaFloatImage full;
            RgbaFloatImage direct;
            RgbaFloatImage rasterVisibility;
            RgbaFloatImage pathVisibility;
            const String suffix = String("-") + String(time.Name);
            if (!LoadImage(directory, String("raster") + suffix, raster) ||
                !LoadImage(directory, String("pt-two") + suffix, reference) ||
                !LoadImage(directory, String("pt-full") + suffix, full) ||
                !LoadImage(directory, String("pt-direct") + suffix, direct) ||
                !LoadImage(directory, String("raster-sun-visibility") + suffix, rasterVisibility) ||
                !LoadImage(directory, String("pt-sun-visibility") + suffix, pathVisibility))
            {
                return 1;
            }
            const RgbaFloatImage* sameSize[] = {&reference, &full, &direct, &rasterVisibility,
                                                &pathVisibility};
            if (raster.Width != geometry[0].Width || raster.Height != geometry[0].Height)
            {
                return 1;
            }
            for (const RgbaFloatImage* image : sameSize)
            {
                if (image->Width != raster.Width || image->Height != raster.Height)
                {
                    return 1;
                }
            }
            // 画素単位最大の一致画素: 幾何と太陽の可視がともに一致する画素。
            VariableArray<uint8_t> agreement = geometryAgreement;
            const uint32_t visibilityDisagreeing = ExcludeSunVisibilityDisagreement(
                rasterVisibility, pathVisibility, SunVisibilityTolerance, agreement);
            std::cout << "r7_outdoor_" << time.Name
                      << "_sun_visibility_disagreeing_pixels=" << visibilityDisagreeing
                      << " fraction=" << static_cast<double>(visibilityDisagreeing) / agreement.size()
                      << '\n';
            bool bSanity = false;
            const bool bPassed = CompareTime(time, raster, reference, full, direct, agreement, bSanity);
            bAllPassed = bAllPassed && bPassed;
            bAllSanity = bAllSanity && bSanity;
        }
        std::cout << "r7_outdoor_comparison=" << (bAllPassed ? "PASS" : "FAIL")
                  << " sanity=" << (bAllSanity ? "PASS" : "FAIL") << '\n';
        if (!bAllSanity)
        {
            std::cerr << "閾値の物差しが変化量に対して単調でないか、局所欠陥を検出できません\n";
        }
        return bAllPassed ? 0 : 1;
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
        return ReportGpuTestSkip(TestName, "RTGIまたはパストレーサーに必要なVulkan機能を利用できません");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("R7 屋外PT参照比較");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = false;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("R7OutdoorPathTracingReferenceVulkan.log");
    config.CreateHandler = &CreateHandler;
    config.Arguments.push_back(TEXT("--scene=outdoor"));
    config.Arguments.push_back(TEXT("--capture-source=scene-color"));
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
