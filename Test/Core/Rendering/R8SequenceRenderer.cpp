// R8の屋内・屋外の決定論的なアニメーションを、PTの連番の経路（絞り・シャッター1/48 s・フレーム長1/24 s）で
// 描き、フレームごとのEXRへ書き出すexe。フレーム番号iから時刻 i/24 のカメラと物体の変換を決める。
// 屋内: CornellBoxの中でカメラが前後にdollyし、球が左右に滑る。屋外: 空・霧の屋外シーンでカメラが注視点の
// 周りを回り、地面の上の球が左右に往復する。書き出しの前に最初のフレームの1つ前の姿勢を1回描き、最初の
// フレームの動きぼけの前の値にする。
// 使い方: R8SequenceRenderer --r8-seq-scene=indoor|outdoor --r8-seq-out=<directory> [--r8-seq-first=0]
//         [--r8-seq-count=8] [--r8-seq-width=256] [--r8-seq-height=144] [--r8-seq-spp=64] [--r8-seq-seed=0]
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Component/CameraComponent.h"
#include "Engine/Engine.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/PathTracingExrOutput.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"
#include "RenderingValidation/CornellBoxData.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingValidationApplication.h"
#include "RenderingValidation/RenderingValidationScene.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <Windows.h>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "R8SequenceRenderer";
    constexpr double Pi = 3.14159265358979323846;
    constexpr double FrameRate = 24.0;
    // 屋内のCornellの点光源（R6の動的検証と同じ強さ）。
    constexpr float R6PointLightIntensity = 1200.0f;
    // 屋外の空と霧（R7屋外参照比較の中間の密度）。
    constexpr float OutdoorSunAltitudeDegrees = 30.0f;
    constexpr float OutdoorSunAzimuthDegrees = 20.0f;
    constexpr float OutdoorFogDensity = 0.005f;
    // 光学系。ピント距離は動く物体までのおよその距離。
    constexpr float IndoorAperture = 2.8f;
    constexpr float IndoorFocusDistance = 6.0f;
    constexpr float OutdoorAperture = 8.0f;
    constexpr float OutdoorFocusDistance = 12.0f;
    // 1フレームの累積は8回の描画に分ける（切り替えの後の古い累積の取得を見分けるため4回以上にする）。
    constexpr uint32_t DrawsPerSequenceFrame = 8u;
    // 切り替えの後、累積の描画数に加えて待つ描画数（取得の遅れの分）。
    constexpr uint64_t SettleFrames = 2u;

    struct SequenceOptions
    {
        bool bOutdoor = false;
        const char* OutputDirectory = nullptr;
        uint32_t FirstFrame = 0u;
        uint32_t FrameCount = 8u;
        uint32_t Width = 256u;
        uint32_t Height = 144u;
        uint32_t SamplesPerPixel = 64u;
        uint32_t Seed = 0u;

        const char* SceneName() const { return bOutdoor ? "outdoor" : "indoor"; }
        uint32_t SamplesPerFrame() const
        {
            const uint32_t perFrame = SamplesPerPixel / DrawsPerSequenceFrame;
            return perFrame == 0u ? 1u : perFrame;
        }
    };

    SequenceOptions g_Options;

    const char* MatchOption(const char* argument, const char* prefix)
    {
        const size_t length = std::strlen(prefix);
        return std::strncmp(argument, prefix, length) == 0 ? argument + length : nullptr;
    }

    bool ParseUnsigned(const char* text, uint32_t minimum, uint32_t maximum, uint32_t& outValue)
    {
        char* end = nullptr;
        const unsigned long long value = std::strtoull(text, &end, 10);
        if (end == text || *end != '\0' || value < minimum || value > maximum)
        {
            return false;
        }
        outValue = static_cast<uint32_t>(value);
        return true;
    }

    bool ParseOptions(int argc, char** argv, SequenceOptions& outOptions)
    {
        bool bScene = false;
        for (int index = 1; index < argc; ++index)
        {
            const char* argument = argv[index];
            if (const char* value = MatchOption(argument, "--r8-seq-scene="))
            {
                bScene = std::strcmp(value, "indoor") == 0 || std::strcmp(value, "outdoor") == 0;
                outOptions.bOutdoor = std::strcmp(value, "outdoor") == 0;
                if (!bScene)
                {
                    return false;
                }
            }
            else if (const char* value = MatchOption(argument, "--r8-seq-out="))
            {
                outOptions.OutputDirectory = value;
            }
            else if (const char* value = MatchOption(argument, "--r8-seq-first="))
            {
                if (!ParseUnsigned(value, 0u, 999999u, outOptions.FirstFrame))
                {
                    return false;
                }
            }
            else if (const char* value = MatchOption(argument, "--r8-seq-count="))
            {
                if (!ParseUnsigned(value, 1u, 100000u, outOptions.FrameCount))
                {
                    return false;
                }
            }
            else if (const char* value = MatchOption(argument, "--r8-seq-width="))
            {
                if (!ParseUnsigned(value, 16u, 7680u, outOptions.Width))
                {
                    return false;
                }
            }
            else if (const char* value = MatchOption(argument, "--r8-seq-height="))
            {
                if (!ParseUnsigned(value, 16u, 4320u, outOptions.Height))
                {
                    return false;
                }
            }
            else if (const char* value = MatchOption(argument, "--r8-seq-spp="))
            {
                if (!ParseUnsigned(value, DrawsPerSequenceFrame, 1u << 20u, outOptions.SamplesPerPixel))
                {
                    return false;
                }
            }
            else if (const char* value = MatchOption(argument, "--r8-seq-seed="))
            {
                if (!ParseUnsigned(value, 0u, 255u, outOptions.Seed))
                {
                    return false;
                }
            }
        }
        return bScene && outOptions.OutputDirectory != nullptr && outOptions.OutputDirectory[0] != '\0' &&
               outOptions.FirstFrame + outOptions.FrameCount <= 1000000u;
    }

    // アニメーションのフレーム番号（書き出しの前の1枚は最初のフレームの1つ前で、負になることがある）の時刻。
    double AnimationTime(int64_t frame)
    {
        return static_cast<double>(frame) / FrameRate;
    }

    // baseの露出・ID・クリップ面を保ったまま、位置と向き・縦横比を置き換える。
    CameraProxy PoseCamera(const CameraProxy& base, const Math::Vector3& position, const Math::Vector3& target,
                           float fieldOfView)
    {
        const CameraProxy pose = BuildLookAtCamera(position, target, g_Options.Width, g_Options.Height);
        CameraProxy camera = base;
        camera.PositionX = pose.PositionX;
        camera.PositionY = pose.PositionY;
        camera.PositionZ = pose.PositionZ;
        camera.ForwardX = pose.ForwardX;
        camera.ForwardY = pose.ForwardY;
        camera.ForwardZ = pose.ForwardZ;
        camera.RightX = pose.RightX;
        camera.RightY = pose.RightY;
        camera.RightZ = pose.RightZ;
        camera.UpX = pose.UpX;
        camera.UpY = pose.UpY;
        camera.UpZ = pose.UpZ;
        camera.AspectRatio = pose.AspectRatio;
        camera.Viewport = pose.Viewport;
        camera.FieldOfView = fieldOfView;
        return camera;
    }

    // 屋内: 箱の開口の内側だけが写る位置から、10秒周期で0.8 m前後にdollyする。
    CameraProxy MakeIndoorCamera(const CameraProxy& base, double time)
    {
        const double dolly = 0.4 * (1.0 - std::cos(2.0 * Pi * time / 10.0));
        const Math::Vector3 position(CornellBox::CameraPosition[0], CornellBox::CameraPosition[1],
                                     static_cast<float>(-3.6 + dolly));
        const Math::Vector3 target(CornellBox::CameraTarget[0], 2.3f, 5.59f);
        CameraProxy camera = PoseCamera(base, position, target, CornellBox::CameraFieldOfViewDegrees);
        camera.NearPlane = CornellBox::CameraNearPlane;
        camera.FarPlane = CornellBox::CameraFarPlane;
        return camera;
    }

    // 屋内: 球は4秒周期で左右に0.6 m滑る。
    float IndoorObjectOffsetX(double time)
    {
        return static_cast<float>(0.6 * std::sin(2.0 * Pi * time / 4.0));
    }

    // 屋外: 既定の視点（(7,5,9)から原点）の水平距離のまま、20秒で1周する速さで注視点の周りを回る。
    CameraProxy MakeOutdoorCamera(const CameraProxy& base, double time)
    {
        const double radius = std::sqrt(7.0 * 7.0 + 9.0 * 9.0);
        const double angle = std::atan2(9.0, 7.0) + 2.0 * Pi * time / 20.0;
        const Math::Vector3 position(static_cast<float>(radius * std::cos(angle)), 5.0f,
                                     static_cast<float>(radius * std::sin(angle)));
        return PoseCamera(base, position, Math::Vector3(0.0f, -0.5f, 0.0f), 60.0f);
    }

    // 屋外: 球は5秒周期で左右に3 m往復する。
    void OutdoorSpherePosition(double time, float outPosition[3])
    {
        outPosition[0] = static_cast<float>(3.0 * std::sin(2.0 * Pi * time / 5.0));
        outPosition[1] = -0.5f;
        outPosition[2] = 1.5f;
    }

    class SequenceHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnPreInitialize(const VariableArray<String>& args) override
        {
            m_CameraSequence = 1u;
            m_ObjectSequence = 1u;
            m_bCameraSwitchPending = false;
            m_CameraSwitchRendered = 0u;
            m_WrittenFrames = 0u;
            m_bDone = false;
            m_bStateReady = true;
            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            return GetRunConfig().bPathTracing && GetRunConfig().CaptureSource == FrameCaptureSourceKind::SceneColor;
        }

        bool OnInitialize() override
        {
            if (!RenderingValidationApplicationHandler::OnInitialize())
            {
                return false;
            }
            const bool bScene = g_Options.bOutdoor
                                    ? GetFixture().AddR8OutdoorMovingSphere()
                                    : GetFixture().ApplyR4CornellFixture() && GetFixture().AddR6CornellDynamicObject();
            if (!bScene)
            {
                std::cerr << "R8連番のシーンを用意できません\n";
                return false;
            }
            return WriteManifest();
        }

        void OnPostRender() override
        {
            // capture評価の直後の状態の適用は、次のフレームの同期より前の物体を動かしてしまうため無視する。
            m_bInPostRender = true;
            RenderingValidationApplicationHandler::OnPostRender();
            m_bInPostRender = false;
        }

    protected:
        bool ParseAdditionalArgument(const String& argument, String& outFailureReason) override
        {
            const String prefix(TEXT("--r8-seq-"));
            if (argument.size() > prefix.size() && argument.substr(0, prefix.size()) == prefix)
            {
                return true;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(argument, outFailureReason);
        }

        // 連番のフレーム（SequenceFrame）sは、アニメーションのフレーム「最初-2+s」を描く（s=1は最初の1つ前）。
        // 物体の変換は次のフレームの同期で描画へ入るため、物体を先に次のsへ進め、カメラは次の呼び出しで進める。
        // こうするとsの最初のパケットから、カメラと物体がそろってsの姿勢になる。
        void ApplyCaptureStageState(RenderWorld& renderWorld) override
        {
            if (m_bInPostRender)
            {
                return;
            }
            if (m_bCameraSwitchPending)
            {
                m_CameraSequence = m_ObjectSequence;
                m_bCameraSwitchPending = false;
                m_CameraSwitchRendered = renderWorld.GetRenderedFrameCount();
            }
            const double cameraTime = AnimationTime(AnimationFrame(m_CameraSequence));
            const double objectTime = AnimationTime(AnimationFrame(m_ObjectSequence));
            if (g_Options.bOutdoor)
            {
                CameraProxy base = GetFixture().GetCamera();
                m_bStateReady = Core::Component::CameraComponent::TryBuildExposureSnapshot(
                                    16.0f, 1.0f / 125.0f, 100.0f, 0.0f, base) &&
                                m_bStateReady;
                CameraProxy camera = MakeOutdoorCamera(base, cameraTime);
                camera.SequenceFrame = m_CameraSequence;
                renderWorld.SetMainCamera(camera);
                // 空の太陽だけで照らす（シーンの方向光を残すと太陽が二つになる）。
                m_bStateReady = GetFixture().SetBaseLightActive(false) && m_bStateReady;
                SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
                sky.bEnabled = true;
                sky.SunAltitudeDegrees = OutdoorSunAltitudeDegrees;
                sky.SunAzimuthDegrees = OutdoorSunAzimuthDegrees;
                renderWorld.SetSkyAtmosphere(sky);
                VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
                fog.bEnabled = true;
                fog.DensityAtBaseHeight = OutdoorFogDensity;
                renderWorld.SetVolumetricFogParameters(fog);
                float spherePosition[3] = {};
                OutdoorSpherePosition(objectTime, spherePosition);
                m_bStateReady = GetFixture().SetR8OutdoorMovingSpherePosition(
                                    spherePosition[0], spherePosition[1], spherePosition[2]) &&
                                m_bStateReady;
            }
            else
            {
                CameraProxy camera = MakeIndoorCamera(GetFixture().GetR4CornellCamera(), cameraTime);
                camera.SequenceFrame = m_CameraSequence;
                renderWorld.SetMainCamera(camera);
                m_bStateReady =
                    GetFixture().SetR4CornellObjectOffsetX(IndoorObjectOffsetX(objectTime)) && m_bStateReady;
                m_bStateReady = GetFixture().SetR4CornellLightOffsetX(0.0f) && m_bStateReady;
                m_bStateReady =
                    GetFixture().SetR4CornellPointLightState(0.0f, R6PointLightIntensity) && m_bStateReady;
                m_bStateReady = GetFixture().SetR4CornellEmitterVisible(true) && m_bStateReady;
            }
            renderWorld.SetDebugViewModeAll(DebugViewMode::Normal);
            if (m_CameraSequence != m_ObjectSequence)
            {
                m_bCameraSwitchPending = true;
            }
        }

        bool EvaluateCapturedFrame(const CapturedFrame& frame, String& outFailureReason) override
        {
            if (!m_bStateReady)
            {
                outFailureReason = TEXT("R8連番のシーンの状態を設定できません");
                return false;
            }
            // 切り替えの途中か、切り替えの後に累積し直す描画数が過ぎる前の取得は、前のフレームの累積なので使わない。
            const uint64_t rendered = Core::Engine::GEngine->GetRenderWorld().GetRenderedFrameCount();
            const uint64_t accumulationFrames =
                (g_Options.SamplesPerPixel + g_Options.SamplesPerFrame() - 1u) / g_Options.SamplesPerFrame();
            if (m_CameraSequence != m_ObjectSequence || m_bCameraSwitchPending ||
                rendered < m_CameraSwitchRendered + accumulationFrames + SettleFrames)
            {
                return true;
            }
            if (m_CameraSequence >= 2u && !WriteFrame(frame, outFailureReason))
            {
                return false;
            }
            if (m_WrittenFrames >= g_Options.FrameCount)
            {
                m_bDone = true;
                return true;
            }
            ++m_ObjectSequence;
            return true;
        }

        bool RequestFollowupCapture(const CapturedFrame&, FrameCaptureRequest& outRequest) override
        {
            if (m_bDone)
            {
                std::cout << "r8_sequence_done scene=" << g_Options.SceneName() << " frames=" << m_WrittenFrames
                          << '\n';
                return false;
            }
            outRequest.SourceKind = FrameCaptureSourceKind::SceneColor;
            return true;
        }

    private:
        int64_t AnimationFrame(uint64_t sequence) const
        {
            return static_cast<int64_t>(g_Options.FirstFrame) + static_cast<int64_t>(sequence) - 2;
        }

        bool WriteManifest() const
        {
            char path[MAX_PATH] = {};
            std::snprintf(path, sizeof(path), "%s/%s_manifest_frames%06u-%06u.txt", g_Options.OutputDirectory,
                          g_Options.SceneName(), g_Options.FirstFrame,
                          g_Options.FirstFrame + g_Options.FrameCount - 1u);
            FILE* file = nullptr;
            if (fopen_s(&file, path, "wb") != 0 || !file)
            {
                std::cerr << "manifestを書けません: " << path << '\n';
                return false;
            }
            const bool bOutdoor = g_Options.bOutdoor;
            std::fprintf(file,
                         "scene=%s\nwidth=%u\nheight=%u\nspp=%u\nsamples_per_frame=%u\nfirst_frame=%u\n"
                         "frame_count=%u\nseed=%u\nframe_rate=24\nframe_duration=1/24\nshutter=1/48\n"
                         "aperture=%g\nfocus_distance=%g\n",
                         g_Options.SceneName(), g_Options.Width, g_Options.Height, g_Options.SamplesPerPixel,
                         g_Options.SamplesPerFrame(), g_Options.FirstFrame, g_Options.FrameCount, g_Options.Seed,
                         static_cast<double>(bOutdoor ? OutdoorAperture : IndoorAperture),
                         static_cast<double>(bOutdoor ? OutdoorFocusDistance : IndoorFocusDistance));
            std::fclose(file);
            return true;
        }

        bool WriteFrame(const CapturedFrame& frame, String& outFailureReason)
        {
            RgbaFloatImage image;
            if (DecodeCapturedRgbaFloat(frame, image) != FloatImageStatus::Success)
            {
                outFailureReason = TEXT("R8連番のSceneColorを読めません");
                return false;
            }
            if (image.Width != g_Options.Width || image.Height != g_Options.Height)
            {
                outFailureReason = TEXT("R8連番のSceneColorの寸法が指定と違います");
                return false;
            }
            const uint32_t animationFrame = static_cast<uint32_t>(AnimationFrame(m_CameraSequence));
            String writtenPath;
            if (!WritePathTracingExrFrame(g_Options.OutputDirectory, g_Options.SceneName(), g_Options.Seed,
                                          g_Options.SamplesPerPixel, animationFrame, image.Width, image.Height,
                                          image.Values.data(), image.Values.size(), &writtenPath))
            {
                outFailureReason = TEXT("R8連番のEXRを書き出せません（非有限の画素を含む可能性があります）");
                return false;
            }
            // 実際の累積試料数（取得は指定値以上で届く）を記録する。
            char logPath[MAX_PATH] = {};
            std::snprintf(logPath, sizeof(logPath), "%s/%s_frames.txt", g_Options.OutputDirectory,
                          g_Options.SceneName());
            FILE* log = nullptr;
            if (fopen_s(&log, logPath, "ab") == 0 && log)
            {
                std::fprintf(log, "frame=%u samples=%u\n", animationFrame, frame.PathTracingSampleCount);
                std::fclose(log);
            }
            ++m_WrittenFrames;
            std::cout << "r8_sequence_frame scene=" << g_Options.SceneName() << " frame=" << animationFrame
                      << " samples=" << frame.PathTracingSampleCount << " written=" << m_WrittenFrames << "/"
                      << g_Options.FrameCount << '\n';
            return true;
        }

        uint64_t m_CameraSequence = 1u;
        uint64_t m_ObjectSequence = 1u;
        bool m_bCameraSwitchPending = false;
        uint64_t m_CameraSwitchRendered = 0u;
        uint32_t m_WrittenFrames = 0u;
        bool m_bDone = false;
        bool m_bStateReady = true;
        bool m_bInPostRender = false;
    };

    TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return MakeShared<SequenceHandler>();
    }
} // namespace

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    if (!ParseOptions(argc, argv, g_Options))
    {
        std::cerr << "usage: R8SequenceRenderer --r8-seq-scene=indoor|outdoor --r8-seq-out=<directory>"
                     " [--r8-seq-first=0] [--r8-seq-count=8] [--r8-seq-width=256] [--r8-seq-height=144]"
                     " [--r8-seq-spp=64] [--r8-seq-seed=0]\n";
        return 2;
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
    {
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
        if (!Core::Rendering::PathTracingPass::IsSupported(capabilities))
        {
            return ReportGpuTestSkip(TestName, "パストレーサーに必要なVulkan機能を利用できません");
        }
    }
    CreateDirectoryA(g_Options.OutputDirectory, nullptr);
    const DWORD attributes = GetFileAttributesA(g_Options.OutputDirectory);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u)
    {
        std::cerr << "出力directoryを作れません: " << g_Options.OutputDirectory << '\n';
        return 1;
    }

    char samples[32] = {};
    char samplesPerFrame[48] = {};
    char aperture[48] = {};
    char focusDistance[48] = {};
    char sampleBatch[48] = {};
    std::snprintf(samples, sizeof(samples), "--path-tracing-samples=%u", g_Options.SamplesPerPixel);
    std::snprintf(samplesPerFrame, sizeof(samplesPerFrame), "--path-tracing-samples-per-frame=%u",
                  g_Options.SamplesPerFrame());
    std::snprintf(aperture, sizeof(aperture), "--path-tracing-aperture=%g",
                  static_cast<double>(g_Options.bOutdoor ? OutdoorAperture : IndoorAperture));
    std::snprintf(focusDistance, sizeof(focusDistance), "--path-tracing-focus-distance=%g",
                  static_cast<double>(g_Options.bOutdoor ? OutdoorFocusDistance : IndoorFocusDistance));
    std::snprintf(sampleBatch, sizeof(sampleBatch), "--path-tracing-sample-batch=%u", g_Options.Seed);

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("R8 連番の書き出し");
    config.WindowWidth = g_Options.Width;
    config.WindowHeight = g_Options.Height;
    config.bResizable = false;
    config.bVSync = false;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("R8SequenceRenderer.log");
    config.CreateHandler = &CreateHandler;
    config.Arguments.push_back(g_Options.bOutdoor ? TEXT("--scene=outdoor") : TEXT("--scene=indoor"));
    config.Arguments.push_back(TEXT("--renderer=path-tracing"));
    config.Arguments.push_back(TEXT("--capture-source=scene-color"));
    config.Arguments.push_back(Core::Container::String(samples));
    config.Arguments.push_back(Core::Container::String(samplesPerFrame));
    config.Arguments.push_back(TEXT("--path-tracing-frame-duration=1/24"));
    config.Arguments.push_back(TEXT("--path-tracing-shutter=1/48"));
    config.Arguments.push_back(Core::Container::String(aperture));
    config.Arguments.push_back(Core::Container::String(focusDistance));
    config.Arguments.push_back(Core::Container::String(sampleBatch));
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
