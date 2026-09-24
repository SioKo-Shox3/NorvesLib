// R4 DDGIのCornell画像を、同じシーンの自前PT参照とR4の規定の指標で再照合する（更新ルール5）。
//
// 取得（GPU）: R4受入れと同じCornell fixture・camera・光源（天井の面光源だけ、点光源なし）で、ラスタ
// （DDGI有効、RTGI無効）またはパストレーサー（--renderer=path-tracing）のSceneColorを取得し、
// --r4-reference-dumpへfloat画像として書き出す。
// 比較（CPU）: --compare-dumps=<dir> でraster-ddgiとpt-fullを読み、R4CornellAcceptance.tsvのROI・閾値
// （露出はdirect white ROIで1回決め、影・赤・緑ROIの平均輝度の相対誤差と、赤・緑ROIの優勢色度の差）で、
// 公開RGBEの代わりにPTを参照として判定する。
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "R4DDGIPathTracingReferenceVulkanTest";
    // DDGIのprobe更新（hysteresis）が落ち着くまで待ってから取得する。
    constexpr uint64_t RasterConvergedRenderedFrames = 128u;

    DDGIVolumeParameters MakeCornellVolume()
    {
        // R4受入れと同じprobe格子（原点-0.1、間隔0.82、8x8x8）。
        DDGIVolumeParameters parameters;
        parameters.bEnabled = true;
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
                   GetFixture().ApplyR4CornellFixture();
        }

    protected:
        bool ParseAdditionalArgument(const String& argument, String& outFailureReason) override
        {
            const String prefix(TEXT("--r4-reference-dump="));
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
            // R4受入れのCornell参照段階（DDGI有効、点光源なし、天井の面光源あり）と同じ状態。
            renderWorld.SetMainCamera(GetFixture().GetR4CornellCamera());
            renderWorld.GetRenderingCoordinator().SetRTGIEnabled(false);
            renderWorld.SetDDGIVolumeParameters(MakeCornellVolume());
            m_bStateReady = GetFixture().SetR4CornellLightOffsetX(0.0f) && m_bStateReady;
            m_bStateReady = GetFixture().SetR4CornellPointLightState(0.0f, 0.0f) && m_bStateReady;
            m_bStateReady = GetFixture().SetR4CornellEmitterVisible(true) && m_bStateReady;
        }

        bool EvaluateCapturedFrame(const CapturedFrame& frame, String& outFailureReason) override
        {
            if (!m_bStateReady)
            {
                outFailureReason = TEXT("R4 Cornell fixtureの状態を設定できません");
                return false;
            }
            if (m_CaptureCount == 0u)
            {
                m_FirstFrame = frame.FrameNumber;
            }
            ++m_CaptureCount;
            if (!GetRunConfig().bPathTracing &&
                frame.FrameNumber - m_FirstFrame < RasterConvergedRenderedFrames)
            {
                return true;
            }
            RgbaFloatImage image;
            if (DecodeCapturedRgbaFloat(frame, image) != FloatImageStatus::Success ||
                FindFirstNonFinite(image).Kind != NonFiniteKind::None)
            {
                outFailureReason = TEXT("R4参照比較のSceneColorを読めないか有限でない値があります");
                return false;
            }
            if (!WriteRgbaFloatDump(m_DumpPath, image, frame.PathTracingSampleCount))
            {
                outFailureReason = TEXT("R4参照比較のSceneColorを書き出せません");
                return false;
            }
            std::cout << "r4_reference_capture renderer="
                      << (GetRunConfig().bPathTracing ? "path-tracing" : "raster-ddgi")
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

    struct Region
    {
        uint32_t Left = 0u;
        uint32_t Top = 0u;
        uint32_t Right = 0u;
        uint32_t Bottom = 0u;
    };

    struct R4Criteria
    {
        Region DirectWhite;
        Region ShadowFloor;
        Region RedBounce;
        Region GreenBounce;
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        double MaximumRelativeYError = -1.0;
        double MaximumChromaDifference = -1.0;
    };

    // R4CornellAcceptance.tsvのROI（左上原点、right/bottomを含まない）と閾値を読む。
    bool LoadCriteria(R4Criteria& outCriteria)
    {
        std::ifstream input(NORVES_SOURCE_ROOT
                            "/Test/Core/Rendering/Thresholds/RenderingValidation/R4CornellAcceptance.tsv");
        if (!input)
        {
            std::cerr << "R4CornellAcceptance.tsvを開けません\n";
            return false;
        }
        struct Field
        {
            const char* Name;
            uint32_t* Target;
            bool bFound;
        } fields[] = {
            {"width", &outCriteria.Width, false},
            {"height", &outCriteria.Height, false},
            {"direct_white_left", &outCriteria.DirectWhite.Left, false},
            {"direct_white_top", &outCriteria.DirectWhite.Top, false},
            {"direct_white_right", &outCriteria.DirectWhite.Right, false},
            {"direct_white_bottom", &outCriteria.DirectWhite.Bottom, false},
            {"shadow_floor_left", &outCriteria.ShadowFloor.Left, false},
            {"shadow_floor_top", &outCriteria.ShadowFloor.Top, false},
            {"shadow_floor_right", &outCriteria.ShadowFloor.Right, false},
            {"shadow_floor_bottom", &outCriteria.ShadowFloor.Bottom, false},
            {"red_bounce_left", &outCriteria.RedBounce.Left, false},
            {"red_bounce_top", &outCriteria.RedBounce.Top, false},
            {"red_bounce_right", &outCriteria.RedBounce.Right, false},
            {"red_bounce_bottom", &outCriteria.RedBounce.Bottom, false},
            {"green_bounce_left", &outCriteria.GreenBounce.Left, false},
            {"green_bounce_top", &outCriteria.GreenBounce.Top, false},
            {"green_bounce_right", &outCriteria.GreenBounce.Right, false},
            {"green_bounce_bottom", &outCriteria.GreenBounce.Bottom, false}};
        char line[256] = {};
        while (input.getline(line, sizeof(line)))
        {
            char name[128] = {};
            double value = 0.0;
            if (line[0] == '#' ||
                sscanf_s(line, "%127s %lf", name, static_cast<unsigned>(sizeof(name)), &value) != 2)
            {
                continue;
            }
            if (std::strcmp(name, "max_relative_y_error") == 0)
            {
                outCriteria.MaximumRelativeYError = value;
            }
            else if (std::strcmp(name, "max_chroma_difference") == 0)
            {
                outCriteria.MaximumChromaDifference = value;
            }
            for (Field& field : fields)
            {
                if (std::strcmp(name, field.Name) == 0)
                {
                    *field.Target = static_cast<uint32_t>(value);
                    field.bFound = true;
                }
            }
        }
        for (const Field& field : fields)
        {
            if (!field.bFound)
            {
                std::cerr << "R4CornellAcceptance.tsvに" << field.Name << "がありません\n";
                return false;
            }
        }
        return outCriteria.MaximumRelativeYError > 0.0 && outCriteria.MaximumChromaDifference > 0.0;
    }

    struct MeanRgb
    {
        double Red = 0.0;
        double Green = 0.0;
        double Blue = 0.0;

        double Luma() const { return 0.2126 * Red + 0.7152 * Green + 0.0722 * Blue; }
        double Chroma(uint32_t channel) const
        {
            const double sum = Red + Green + Blue;
            return sum > 1.0e-12 ? (channel == 0u ? Red : Green) / sum : 0.0;
        }
    };

    MeanRgb RegionMean(const RgbaFloatImage& image, const Region& region)
    {
        MeanRgb sum;
        for (uint32_t y = region.Top; y < region.Bottom; ++y)
        {
            for (uint32_t x = region.Left; x < region.Right; ++x)
            {
                const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                sum.Red += image.Values[offset];
                sum.Green += image.Values[offset + 1u];
                sum.Blue += image.Values[offset + 2u];
            }
        }
        const double count =
            static_cast<double>(region.Right - region.Left) * (region.Bottom - region.Top);
        return {sum.Red / count, sum.Green / count, sum.Blue / count};
    }

    struct R4Result
    {
        double ExposureScale = 0.0;
        double RelativeYError[3] = {};
        double ChromaDifference[2] = {};
        bool bPassed = false;
    };

    // R4の指標（露出はdirect white ROIで1回決める）で候補を参照と比べる。
    R4Result EvaluateR4(const RgbaFloatImage& reference, const RgbaFloatImage& candidate,
                        const R4Criteria& criteria)
    {
        R4Result result;
        const double directReference = RegionMean(reference, criteria.DirectWhite).Luma();
        const double directCandidate = RegionMean(candidate, criteria.DirectWhite).Luma();
        if (!(directReference > 0.0) || !(directCandidate > 0.0))
        {
            return result;
        }
        result.ExposureScale = directReference / directCandidate;
        const Region* regions[3] = {&criteria.ShadowFloor, &criteria.RedBounce, &criteria.GreenBounce};
        bool bPassed = true;
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            const MeanRgb referenceMean = RegionMean(reference, *regions[index]);
            const MeanRgb candidateMean = RegionMean(candidate, *regions[index]);
            const double referenceY = referenceMean.Luma();
            result.RelativeYError[index] =
                std::abs(candidateMean.Luma() * result.ExposureScale - referenceY) / referenceY;
            bPassed = bPassed && result.RelativeYError[index] <= criteria.MaximumRelativeYError;
            if (index > 0u)
            {
                const uint32_t channel = index - 1u;
                result.ChromaDifference[channel] =
                    std::abs(candidateMean.Chroma(channel) - referenceMean.Chroma(channel));
                bPassed = bPassed &&
                          result.ChromaDifference[channel] <= criteria.MaximumChromaDifference;
            }
        }
        result.bPassed = bPassed;
        return result;
    }

    void PrintResult(const char* label, const R4Result& result)
    {
        std::cout << label << " exposure_scale=" << result.ExposureScale
                  << " shadow_relative_error=" << result.RelativeYError[0]
                  << " red_relative_error=" << result.RelativeYError[1]
                  << " green_relative_error=" << result.RelativeYError[2]
                  << " red_chroma_difference=" << result.ChromaDifference[0]
                  << " green_chroma_difference=" << result.ChromaDifference[1]
                  << " within_r4_limits=" << (result.bPassed ? 1 : 0) << '\n';
    }

    // 赤・緑ROIの色にじみを取り除いた画像（R/G/Bを輝度で置き換え）。色度の判定の負の対照に使う。
    RgbaFloatImage RemoveColorBleeding(const RgbaFloatImage& image, const R4Criteria& criteria)
    {
        RgbaFloatImage result = image;
        for (const Region* region : {&criteria.RedBounce, &criteria.GreenBounce})
        {
            for (uint32_t y = region->Top; y < region->Bottom; ++y)
            {
                for (uint32_t x = region->Left; x < region->Right; ++x)
                {
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const float luma = static_cast<float>(0.2126 * image.Values[offset] +
                                                          0.7152 * image.Values[offset + 1u] +
                                                          0.0722 * image.Values[offset + 2u]);
                    result.Values[offset] = luma;
                    result.Values[offset + 1u] = luma;
                    result.Values[offset + 2u] = luma;
                }
            }
        }
        return result;
    }

    int RunComparison(const String& directory)
    {
        R4Criteria criteria;
        if (!LoadCriteria(criteria))
        {
            return 1;
        }
        const char* names[2] = {"raster-ddgi", "pt-full"};
        RgbaFloatImage images[2];
        for (uint32_t index = 0u; index < 2u; ++index)
        {
            String path = directory;
            path += TEXT("/");
            path += names[index];
            path += TEXT(".nlrgba");
            uint32_t samples = 0u;
            if (!ReadRgbaFloatDump(path, images[index], samples) ||
                FindFirstNonFinite(images[index]).Kind != NonFiniteKind::None ||
                images[index].Width != criteria.Width || images[index].Height != criteria.Height)
            {
                std::cerr << "R4参照比較の画像を読めないか解像度がR4と違います: " << names[index] << '\n';
                return 1;
            }
            std::cout << "r4_reference_image name=" << names[index] << " samples=" << samples << '\n';
        }
        const RgbaFloatImage& raster = images[0];
        const RgbaFloatImage& pathTraced = images[1];
        std::cout << "r4_reference_threshold max_relative_y_error<=" << criteria.MaximumRelativeYError
                  << " max_chroma_difference<=" << criteria.MaximumChromaDifference << '\n';

        // 物差しの対照: PT自身は誤差0で通り、色にじみを消したPTは色度の判定で落ちる。
        const R4Result identical = EvaluateR4(pathTraced, pathTraced, criteria);
        const R4Result noBleeding =
            EvaluateR4(pathTraced, RemoveColorBleeding(pathTraced, criteria), criteria);
        PrintResult("control_identical", identical);
        PrintResult("control_no_color_bleeding", noBleeding);
        const bool bSanity = identical.bPassed && !noBleeding.bPassed;

        const R4Result ddgi = EvaluateR4(pathTraced, raster, criteria);
        PrintResult("r4_ddgi_vs_path_tracing", ddgi);
        const bool bPassed = bSanity && ddgi.bPassed;
        std::cout << "r4_reference_comparison=" << (bPassed ? "PASS" : "FAIL")
                  << " sanity=" << (bSanity ? "PASS" : "FAIL") << '\n';
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
    // ラスタはDDGI（ray query）、PTはPathTracingPassの必要機能を求める。
    const bool bSupported = bPathTracing
        ? Core::Rendering::PathTracingPass::IsSupported(capabilities)
        : capabilities.RayTracing.bAccelerationStructure && capabilities.RayTracing.bRayQuery &&
              capabilities.bBufferDeviceAddress && capabilities.bShaderInt64;
    if (!bSupported)
    {
        return ReportGpuTestSkip(TestName, "R4 DDGIまたはパストレーサーに必要なVulkan機能を利用できません");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("R4 DDGI PT参照比較");
    config.WindowWidth = 512u;
    config.WindowHeight = 512u;
    config.bResizable = false;
    config.bVSync = false;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("R4DDGIPathTracingReferenceVulkan.log");
    config.CreateHandler = &CreateHandler;
    config.Arguments.push_back(TEXT("--scene=indoor"));
    config.Arguments.push_back(TEXT("--capture-source=scene-color"));
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
