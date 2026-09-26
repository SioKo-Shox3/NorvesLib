// ラスタの被写界深度（DepthOfFieldPass）を、同じCornellシーンのPTの薄レンズ参照と知覚差（LDR-FLIP）で比べる。
//
// 取得（GPU）: R4のCornell fixtureに、短い箱の上のピント面の球と、その縁へ前ボケが掛かる手前の球を置き、
// カメラの絞り（--r8-dof-aperture）とピント距離（--r8-dof-focus-distance）を指定してSceneColorを
// --r8-dof-dumpへfloat画像として書く。PTはR8-P3の連番の1フレームの経路（--path-tracing-aperture等、
// 1 spp × dispatchごとにレンズを引き直す）で薄レンズの参照を描く。
// 比較（CPU＋GPU）: --compare-dumps=<dir> で、PTのピンホール像（独立な3組の画素ごとの中央値）と1次命中の
// 距離を入力にDepthOfFieldPassを実GPUで実行し、PTの薄レンズ参照（3組の中央値）と比べる。入力をPTの
// ピンホール像にするのは、ラスタとPTの照明の近似差を除き、被写界深度の差だけを測るため。入力と参照は
// 同じ1次命中の幾何を持つが、薄レンズはピンホールの像に無い面（手前の面の縁の後ろ、未命中の画素の側の
// 面の縁の外側）も見る。そうした画素は一致画素の画素単位最大と8×8区画最大の判定から外して数を記録し、
// 除外の外に置いた局所欠陥を検出できることを比較のたびに確かめる。平均は全画素で判定する。
// 閾値は比較の前に規則で決める: PT参照と、f値を±20%変えたPTとの知覚差（FLIP平均・画素単位最大・8×8区画
// 最大）のうち小さい方。±40%の変化が三つとも閾値の外にあることを比較のたびに確かめる。
// 実際のラスタのパイプライン（ピント距離0と指定値）の取得は、passが働いていることの確認と参考値に使う。
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Engine/Engine.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/DepthOfFieldPass.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/PathTracingCamera.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RenderingValidation/CornellBoxData.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RasterPathTracingComparison.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingPerceptualDiff.h"
#include "RenderingValidation/RenderingValidationApplication.h"
#include "RenderingValidation/RenderingValidationScene.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanCommandList.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace NorvesLib::RHI::Vulkan
{
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "R8DepthOfFieldPathTracingReferenceVulkanTest";
    // R6受入れの静止段階と同じ点光源の強さ。
    constexpr float R6PointLightIntensity = 1200.0f;
    // ラスタはRTGIの時間方向の蓄積が収束してから取得する（R6の参照比較と同じ待ち）。
    constexpr uint64_t RasterConvergedRenderedFrames = 16u + 56u + 4u * 64u;
    // 比較の光学系。ピント距離はピント面の球の中心の深度（カメラz=-8 m、球z=1.69 m）。256画素の像では
    // 現実のf値だとCoCが1画素に満たないため、手前の球のCoCが約9画素、奥の壁が約14画素になるf値を使う。
    constexpr float NominalAperture = 0.025f;
    constexpr float FocusDistance = 9.69f;
    // 閾値の物差し（f値の±20%）と、閾値の外にあるべき変化（±40%）。
    constexpr double ApertureYardstick = 0.2;
    constexpr double ApertureSanity = 0.4;
    constexpr uint32_t BlockSize = 8u;
    // 局所欠陥の負の対照: 除外の外で参照の最も暗い1画素へ、参照の平均輝度のLeakScale倍の光を足す。
    // R6参照比較と同じ1倍では画素単位の閾値を越えないため、R7屋外参照比較と同じく、全体平均と8×8区画
    // 平均では閾値内に埋もれたまま画素単位最大だけが閾値の外に出る4倍にする。
    constexpr double LeakScale = 4.0;

    // CornellBoxの定数から、R4のCornell fixtureと同じ幾何のカメラを作る（露出は比較に使わない）。
    CameraProxy MakeCornellCamera()
    {
        CameraProxy camera = BuildLookAtCamera(
            Math::Vector3(CornellBox::CameraPosition[0], CornellBox::CameraPosition[1],
                          CornellBox::CameraPosition[2]),
            Math::Vector3(CornellBox::CameraTarget[0], CornellBox::CameraTarget[1],
                          CornellBox::CameraTarget[2]),
            CornellBox::ImageWidth, CornellBox::ImageHeight);
        camera.CameraId = 4u;
        camera.FieldOfView = CornellBox::CameraFieldOfViewDegrees;
        camera.NearPlane = CornellBox::CameraNearPlane;
        camera.FarPlane = CornellBox::CameraFarPlane;
        return camera;
    }

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

    bool ParseFloatArgument(const String& argument, const TCHAR* prefixText, float& outValue,
                            bool& outMatched)
    {
        const String prefix(prefixText);
        outMatched = argument.size() > prefix.size() && argument.substr(0, prefix.size()) == prefix;
        if (!outMatched)
        {
            return false;
        }
        const String value = argument.substr(prefix.size());
        char* end = nullptr;
        const double parsed = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed) || parsed < 0.0)
        {
            return false;
        }
        outValue = static_cast<float>(parsed);
        return true;
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
                   GetFixture().ApplyR4CornellFixture() && GetFixture().AddR8DepthOfFieldObjects();
        }

    protected:
        bool ParseAdditionalArgument(const String& argument, String& outFailureReason) override
        {
            const String prefix(TEXT("--r8-dof-dump="));
            if (argument.size() > prefix.size() && argument.substr(0, prefix.size()) == prefix)
            {
                m_DumpPath = argument.substr(prefix.size());
                return true;
            }
            bool bMatched = false;
            if (ParseFloatArgument(argument, TEXT("--r8-dof-aperture="), m_Aperture, bMatched) ||
                ParseFloatArgument(argument, TEXT("--r8-dof-focus-distance="), m_FocusDistance,
                                   bMatched))
            {
                return true;
            }
            if (bMatched)
            {
                outFailureReason = TEXT("--r8-dof-aperture と --r8-dof-focus-distance は0以上の数値です");
                return false;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(argument,
                                                                                 outFailureReason);
        }

        void ApplyCaptureStageState(RenderWorld& renderWorld) override
        {
            // R6受入れの静止段階と同じ照明（RTGI有効、DDGI無効、点光源、天井の面光源）で、カメラにだけ
            // 絞りとピント距離を与える。ピント距離0はピンホール。
            CameraProxy camera = GetFixture().GetR4CornellCamera();
            if (m_Aperture > 0.0f)
            {
                camera.Aperture = m_Aperture;
            }
            camera.FocusDistance = m_FocusDistance;
            renderWorld.SetMainCamera(camera);
            renderWorld.GetRenderingCoordinator().SetRTGIEnabled(true);
            renderWorld.SetDDGIVolumeParameters(MakeCornellVolume());
            m_bStateReady = GetFixture().SetR4CornellLightOffsetX(0.0f) && m_bStateReady;
            m_bStateReady =
                GetFixture().SetR4CornellPointLightState(0.0f, R6PointLightIntensity) && m_bStateReady;
            m_bStateReady = GetFixture().SetR4CornellEmitterVisible(true) && m_bStateReady;
            renderWorld.SetDebugViewModeAll(DebugViewMode::Normal);
        }

        bool EvaluateCapturedFrame(const CapturedFrame& frame, String& outFailureReason) override
        {
            if (!m_bStateReady)
            {
                outFailureReason = TEXT("R8被写界深度のCornell fixtureの状態を設定できません");
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
                outFailureReason = TEXT("R8被写界深度のSceneColorを読めないか有限でない値があります");
                return false;
            }
            if (!WriteRgbaFloatDump(m_DumpPath, image, frame.PathTracingSampleCount))
            {
                outFailureReason = TEXT("R8被写界深度のSceneColorを書き出せません");
                return false;
            }
            std::cout << "r8_dof_capture renderer="
                      << (GetRunConfig().bPathTracing ? "path-tracing" : "raster")
                      << " aperture=" << m_Aperture << " focus_distance=" << m_FocusDistance
                      << " frame=" << frame.FrameNumber
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
        float m_Aperture = 0.0f;
        float m_FocusDistance = 0.0f;
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

    String ResolveSourcePath(const char* relativePath)
    {
        String path(NORVES_SOURCE_ROOT);
        path += "/";
        path += relativePath;
        return path;
    }

    String DumpPath(const String& directory, const char* name)
    {
        String path = directory;
        path += TEXT("/");
        path += name;
        path += TEXT(".nlrgba");
        return path;
    }

    bool ReadDump(const String& directory, const char* name, RgbaFloatImage& outImage)
    {
        uint32_t samples = 0u;
        if (!ReadRgbaFloatDump(DumpPath(directory, name), outImage, samples) ||
            FindFirstNonFinite(outImage).Kind != NonFiniteKind::None)
        {
            std::cerr << "R8被写界深度の画像を読めません: " << name << '\n';
            return false;
        }
        std::cout << "r8_dof_image name=" << name << " samples=" << samples
                  << " mean_luminance=" << MeanLuminance(outImage) << '\n';
        return true;
    }

    // PTの放射輝度は、独立な3組（-b0〜-b2）の画素ごとの中央値（median of means）で読む。
    bool ReadMedianDump(const String& directory, const char* name, RgbaFloatImage& outImage)
    {
        const char* batchSuffixes[3] = {"-b0", "-b1", "-b2"};
        RgbaFloatImage batches[3];
        for (uint32_t batch = 0u; batch < 3u; ++batch)
        {
            String batchName(name);
            batchName += batchSuffixes[batch];
            // 中央値は1組の非有限値を隠すため、組ごとに検査する。
            if (!ReadDump(directory, batchName.c_str(), batches[batch]))
            {
                return false;
            }
        }
        outImage = MedianOfThree(batches[0], batches[1], batches[2]);
        return outImage.Width == batches[0].Width && outImage.Height == batches[0].Height;
    }

    // 列優先の4×4（シェーダーのmat4と同じ並び）とベクトルの積。
    void TransformPoint(const float matrix[16], const double input[4], double output[4])
    {
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            output[row] = 0.0;
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                output[row] += static_cast<double>(matrix[column * 4u + row]) * input[column];
            }
        }
    }

    // PTの1次命中の距離（光線に沿う長さ、不交差は0）を、ラスタと同じ深度（0が手前、1が空）へ直す。
    bool BuildDeviceDepth(const CameraProxy& camera, const RgbaFloatImage& pathDistance,
                          const RHI::IDevice* device, VariableArray<float>& outDepth)
    {
        const float aspect = static_cast<float>(pathDistance.Width) / pathDistance.Height;
        const CameraViewConstants constants = CameraViewConstants::BuildForDevice(camera, aspect, device);
        float view[16] = {};
        float projection[16] = {};
        float inverseViewProjection[16] = {};
        float position[4] = {};
        constants.CopyShaderView(view);
        constants.CopyShaderProjection(projection);
        constants.CopyShaderInverseViewProjection(inverseViewProjection);
        constants.CopyCameraPosition(position);
        outDepth.assign(static_cast<size_t>(pathDistance.Width) * pathDistance.Height, 1.0f);
        for (uint32_t y = 0u; y < pathDistance.Height; ++y)
        {
            for (uint32_t x = 0u; x < pathDistance.Width; ++x)
            {
                const size_t pixel = static_cast<size_t>(y) * pathDistance.Width + x;
                const double distance = pathDistance.Values[pixel * 4u];
                if (!(distance > 0.0))
                {
                    continue;
                }
                // 画素中心の光線の向きを、深度0.5の点を逆変換して求める。
                const double ndc[4] = {(x + 0.5) / pathDistance.Width * 2.0 - 1.0,
                                       (y + 0.5) / pathDistance.Height * 2.0 - 1.0, 0.5, 1.0};
                double world[4] = {};
                TransformPoint(inverseViewProjection, ndc, world);
                if (std::abs(world[3]) <= 1.0e-12)
                {
                    return false;
                }
                double direction[3] = {};
                double length = 0.0;
                for (uint32_t axis = 0u; axis < 3u; ++axis)
                {
                    direction[axis] = world[axis] / world[3] - position[axis];
                    length += direction[axis] * direction[axis];
                }
                length = std::sqrt(length);
                if (!(length > 0.0))
                {
                    return false;
                }
                const double hit[4] = {position[0] + direction[0] / length * distance,
                                       position[1] + direction[1] / length * distance,
                                       position[2] + direction[2] / length * distance, 1.0};
                double viewPoint[4] = {};
                double clip[4] = {};
                TransformPoint(view, hit, viewPoint);
                TransformPoint(projection, viewPoint, clip);
                if (!(std::abs(clip[3]) > 1.0e-12))
                {
                    return false;
                }
                outDepth[pixel] = static_cast<float>(std::clamp(clip[2] / clip[3], 0.0, 1.0));
            }
        }
        return true;
    }

    /**
     * @brief PTの薄レンズがピンホールの像に無い面を見る画素を、一致画素から外す
     *
     * 各画素でレンズの試料ごとに薄レンズの光線をたどり、ピンホールの像の上で光線の点が写る位置
     * （画素p + レンズの向き × CoCの半径の係数 × (1/ピント距離 - 1/z)）の深度と比べる。光線が最初に
     * ピンホールの可視面へ達した位置の深度が、直前の光線の点の深度より手前へ跳ぶなら、光線は手前の面の縁の
     * 後ろへ回り込んで隠れた面を見る（不一致）。像の外は縁の画素の面が続くとみなす（ラスタも縁の画素を
     * 使う）。加えて、ピンホールで未命中の画素と命中の画素の境界から、境界の命中画素のCoCの半径+1画素以内を
     * 外す（面の縁の外側はピンホールの像に無い）。1が一致、0が除外。
     */
    VariableArray<uint8_t> BuildLensVisibilityAgreement(const CameraProxy& camera,
                                                        const RgbaFloatImage& pathDistance,
                                                        uint32_t& outLensExcluded,
                                                        uint32_t& outMissExcluded)
    {
        constexpr uint32_t LensSampleCount = 32u;
        constexpr double StepPixels = 0.25;
        constexpr double DepthJumpTolerance = 0.03;
        const uint32_t width = pathDistance.Width;
        const uint32_t height = pathDistance.Height;
        outLensExcluded = 0u;
        outMissExcluded = 0u;
        VariableArray<uint8_t> agreement(static_cast<size_t>(width) * height, 1u);
        const DepthOfFieldLens lens = BuildDepthOfFieldLens(camera, height);
        if (!lens.bEnabled)
        {
            return agreement;
        }
        // 1次命中の距離を視線方向の深度へ直す（未命中は無限遠）。
        const double tanHalf = std::tan(static_cast<double>(camera.FieldOfView) * 3.14159265358979 / 360.0);
        const double aspect = static_cast<double>(width) / height;
        VariableArray<double> depth(static_cast<size_t>(width) * height,
                                    std::numeric_limits<double>::infinity());
        double minDepth = std::numeric_limits<double>::infinity();
        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const size_t pixel = static_cast<size_t>(y) * width + x;
                const double distance = pathDistance.Values[pixel * 4u];
                if (!(distance > 0.0))
                {
                    continue;
                }
                const double nx = ((x + 0.5) / width * 2.0 - 1.0) * tanHalf * aspect;
                const double ny = ((y + 0.5) / height * 2.0 - 1.0) * tanHalf;
                depth[pixel] = distance / std::sqrt(1.0 + nx * nx + ny * ny);
                minDepth = std::min(minDepth, depth[pixel]);
            }
        }
        if (!std::isfinite(minDepth))
        {
            return agreement;
        }
        // 入力の像の画素でのCoCの半径 = 0.5 × CocScale × FilmScale × F × |1/F - 1/z|。
        const double coefficient = 0.5 * lens.CocScale * lens.FilmScale * lens.FocusDistance;
        const double focusInverse = 1.0 / lens.FocusDistance;
        const double startInverse = 1.0 / (0.95 * minDepth);
        double lensSamples[LensSampleCount][2] = {};
        for (uint32_t sample = 0u; sample < LensSampleCount; ++sample)
        {
            const double radius = std::sqrt(PathTracingCameraDetail::Halton(sample + 1u, 2u));
            const double phase = 2.0 * 3.14159265358979 * PathTracingCameraDetail::Halton(sample + 1u, 3u);
            lensSamples[sample][0] = radius * std::cos(phase);
            lensSamples[sample][1] = radius * std::sin(phase);
        }
        const double centerX = 0.5 * width;
        const double centerY = 0.5 * height;
        auto depthAt = [&](double px, double py)
        {
            const int32_t ix = std::clamp(static_cast<int32_t>(std::floor(px)), 0, static_cast<int32_t>(width) - 1);
            const int32_t iy = std::clamp(static_cast<int32_t>(std::floor(py)), 0, static_cast<int32_t>(height) - 1);
            return depth[static_cast<size_t>(iy) * width + ix];
        };
        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                // 出力の画素は、ピントの倍率で拡大する前の入力の像の位置に対応する。
                const double startX = centerX + (x + 0.5 - centerX) * lens.FilmScale;
                const double startY = centerY + (y + 0.5 - centerY) * lens.FilmScale;
                bool bHidden = false;
                for (uint32_t sample = 0u; sample < LensSampleCount && !bHidden; ++sample)
                {
                    const double magnitude = coefficient * std::hypot(lensSamples[sample][0], lensSamples[sample][1]);
                    if (magnitude < 1.0e-9)
                    {
                        continue;
                    }
                    const double step = StepPixels / magnitude;
                    double previousDepth = 1.0 / startInverse;
                    for (double inverse = startInverse;; inverse = std::max(inverse - step, 0.0))
                    {
                        const double offset = coefficient * (focusInverse - inverse);
                        const double surface = depthAt(startX + lensSamples[sample][0] * offset,
                                                       startY + lensSamples[sample][1] * offset);
                        const double rayDepth = inverse > 0.0 ? 1.0 / inverse
                                                              : std::numeric_limits<double>::infinity();
                        if (rayDepth >= surface)
                        {
                            bHidden = surface < previousDepth * (1.0 - DepthJumpTolerance);
                            break;
                        }
                        if (inverse <= 0.0)
                        {
                            break;
                        }
                        previousDepth = rayDepth;
                    }
                }
                if (bHidden)
                {
                    agreement[static_cast<size_t>(y) * width + x] = 0u;
                    ++outLensExcluded;
                }
            }
        }
        // 未命中と命中の境界の近傍（出力の画素で、境界の命中画素のCoCの半径+1画素以内）。
        VariableArray<uint8_t> missNeighborhood(static_cast<size_t>(width) * height, 0u);
        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const size_t pixel = static_cast<size_t>(y) * width + x;
                if (!std::isfinite(depth[pixel]))
                {
                    continue;
                }
                const int32_t neighbors[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                bool bBoundary = false;
                for (const auto& neighbor : neighbors)
                {
                    const int32_t nx = static_cast<int32_t>(x) + neighbor[0];
                    const int32_t ny = static_cast<int32_t>(y) + neighbor[1];
                    bBoundary = bBoundary ||
                                (nx >= 0 && ny >= 0 && nx < static_cast<int32_t>(width) &&
                                 ny < static_cast<int32_t>(height) &&
                                 !std::isfinite(depth[static_cast<size_t>(ny) * width + nx]));
                }
                if (!bBoundary)
                {
                    continue;
                }
                const double radius =
                    0.5 * ComputeDepthOfFieldCocPixels(lens, static_cast<float>(depth[pixel])) + 1.0;
                const int32_t reach = static_cast<int32_t>(std::ceil(radius));
                for (int32_t dy = -reach; dy <= reach; ++dy)
                {
                    for (int32_t dx = -reach; dx <= reach; ++dx)
                    {
                        const int32_t nx = static_cast<int32_t>(x) + dx;
                        const int32_t ny = static_cast<int32_t>(y) + dy;
                        if (nx >= 0 && ny >= 0 && nx < static_cast<int32_t>(width) &&
                            ny < static_cast<int32_t>(height) && std::hypot(dx, dy) <= radius + 0.5)
                        {
                            missNeighborhood[static_cast<size_t>(ny) * width + nx] = 1u;
                        }
                    }
                }
            }
        }
        for (size_t pixel = 0u; pixel < agreement.size(); ++pixel)
        {
            if (missNeighborhood[pixel] != 0u && agreement[pixel] != 0u)
            {
                agreement[pixel] = 0u;
                ++outMissExcluded;
            }
        }
        return agreement;
    }

    // 除外した画素を参照の値へ置き換えた画像（一致画素の画素単位最大と8×8区画最大の判定に使う）。
    RgbaFloatImage NeutralizeExcluded(const RgbaFloatImage& candidate, const RgbaFloatImage& reference,
                                      const VariableArray<uint8_t>& agreement)
    {
        RgbaFloatImage result = candidate;
        for (size_t pixel = 0u; pixel < agreement.size(); ++pixel)
        {
            if (agreement[pixel] == 0u)
            {
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    result.Values[pixel * 4u + channel] = reference.Values[pixel * 4u + channel];
                }
            }
        }
        return result;
    }

    bool RecordHostReadBarrier(const TSharedPtr<RHI::Vulkan::VulkanCommandList>& commandList,
                               const RHI::BufferPtr& readbackBuffer)
    {
        TSharedPtr<RHI::Vulkan::VulkanBuffer> vulkanBuffer =
            DynamicPointerCast<RHI::Vulkan::VulkanBuffer>(readbackBuffer);
        if (!commandList || !vulkanBuffer)
        {
            return false;
        }
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = vulkanBuffer->GetVkBuffer();
        barrier.offset = 0u;
        barrier.size = VK_WHOLE_SIZE;
        commandList->GetVkCommandBuffer().pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, 0u, nullptr,
            1u, &barrier, 0u, nullptr);
        return true;
    }

    /**
     * @brief PTのピンホール像と深度へDepthOfFieldPassを実GPUで掛け、結果を読み戻す
     *
     * @return 0は成功、1は失敗、125はVulkanを利用できない（skip）
     */
    int ApplyRasterDepthOfField(const CameraProxy& camera, const RgbaFloatImage& pinhole,
                                const RgbaFloatImage& pathDistance, RgbaFloatImage& outImage,
                                uint32_t& outValidationErrors)
    {
        RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting();
        struct CaptureEnd
        {
            ~CaptureEnd()
            {
                RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting();
            }
        } captureEnd;
        RHI::RHIDeviceDesc deviceDesc;
        deviceDesc.Api = RHI::GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        RHI::DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device)
        {
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }
        int result = 1;
        {
            ShaderManager shaderManager;
            SceneRenderer renderer;
            if (!shaderManager.Initialize(device.get(), ResolveSourcePath("Assets/Shaders")) ||
                !renderer.Initialize(device.get(), nullptr))
            {
                std::cerr << "ShaderManagerかSceneRendererを初期化できませんでした\n";
                return 1;
            }
            VariableArray<float> depth;
            if (!BuildDeviceDepth(camera, pathDistance, device.get(), depth))
            {
                std::cerr << "PTの距離を深度へ直せません\n";
                return 1;
            }
            const uint32_t width = pinhole.Width;
            const uint32_t height = pinhole.Height;
            RHI::TextureDesc colorDesc;
            colorDesc.Width = width;
            colorDesc.Height = height;
            colorDesc.TextureFormat = RHI::Format::R32G32B32A32_FLOAT;
            colorDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::RenderTarget |
                              RHI::ResourceUsage::TransferDst | RHI::ResourceUsage::TransferSrc;
            colorDesc.DebugName = "R8DepthOfFieldPathTracingReferenceVulkanTest.SceneColor";
            RHI::TextureDesc depthDesc;
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.TextureFormat = RHI::Format::R32_FLOAT;
            depthDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
            depthDesc.DebugName = "R8DepthOfFieldPathTracingReferenceVulkanTest.SceneDepth";
            RHI::TexturePtr sceneColor = device->CreateTexture(colorDesc);
            RHI::TexturePtr sceneDepth = device->CreateTexture(depthDesc);
            if (!sceneColor || !sceneDepth)
            {
                std::cerr << "入力textureを作成できませんでした\n";
                return 1;
            }
            sceneColor->Update(pinhole.Values.data(), width * 16u, width * height * 16u);
            sceneDepth->Update(depth.data(), width * 4u, width * height * 4u);

            ViewRenderContext context;
            context.Device = device.get();
            context.ShaderMgr = &shaderManager;
            context.Capabilities = &device->GetCapabilities();
            context.Renderer = &renderer;
            context.RenderWidth = width;
            context.RenderHeight = height;
            context.ScreenWidth = width;
            context.ScreenHeight = height;
            context.MainCamera = &camera;

            DepthOfFieldPass pass;
            if (!pass.Initialize(context))
            {
                std::cerr << "DepthOfFieldPassを初期化できませんでした\n";
                return 1;
            }
            const uint64_t readbackSize = static_cast<uint64_t>(width) * height * 16u;
            RHI::BufferDesc readbackDesc(readbackSize, RHI::ResourceUsage::TransferDst, true,
                                         "R8DepthOfFieldPathTracingReferenceVulkanTest.Readback");
            RHI::BufferPtr readback = device->CreateBuffer(readbackDesc);
            RHI::CommandListPtr commandList = device->CreateCommandList();
            TSharedPtr<RHI::Vulkan::VulkanCommandList> vulkanCommandList =
                DynamicPointerCast<RHI::Vulkan::VulkanCommandList>(commandList);
            if (!readback || !commandList || !vulkanCommandList)
            {
                std::cerr << "readback用の資源を作成できませんでした\n";
                return 1;
            }
            context.CommandList = commandList.get();
            commandList->SetFrameIndex(0u);
            commandList->Begin();
            commandList->TextureBarrier(sceneColor, RHI::ResourceState::ShaderResource,
                                        RHI::ResourceState::RenderTarget);
            const bool bApplied = pass.Apply(context, sceneColor, sceneDepth);
            commandList->TextureBarrier(sceneColor, RHI::ResourceState::ShaderResource,
                                        RHI::ResourceState::CopySource);
            commandList->BufferBarrier(readback, RHI::ResourceState::Undefined,
                                       RHI::ResourceState::CopyDest, 0u, readbackSize);
            commandList->CopyTextureToBuffer(sceneColor, readback, width, height, 0u);
            const bool bBarrier = RecordHostReadBarrier(vulkanCommandList, readback);
            commandList->End();
            if (!bApplied || !bBarrier)
            {
                std::cerr << "DepthOfFieldPassを実行できませんでした\n";
                return 1;
            }
            commandList->Submit(true);
            const void* mapped = readback->Map(0u, readbackSize);
            if (!mapped)
            {
                std::cerr << "readbackをmapできませんでした\n";
                return 1;
            }
            outImage.Width = width;
            outImage.Height = height;
            outImage.Values.resize(static_cast<size_t>(width) * height * 4u);
            std::memcpy(outImage.Values.data(), mapped, static_cast<size_t>(readbackSize));
            readback->Unmap();
            pass.Shutdown();
            device->WaitIdle();
            renderer.Shutdown();
            shaderManager.Shutdown();
            result = 0;
        }
        outValidationErrors = RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting();
        return result;
    }

    int RunComparison(const String& directory)
    {
        CameraProxy camera = MakeCornellCamera();
        camera.Aperture = NominalAperture;
        camera.FocusDistance = FocusDistance;

        // CoCの式がPTの解析値（ComputePathTracingCocPixels）と一致すること。
        const DepthOfFieldLens lens = BuildDepthOfFieldLens(camera, ValidationHeight);
        double worstCocError = 0.0;
        for (const float depth : {8.0f, 8.2f, 9.0f, 9.69f, 10.5f, 12.0f, 13.59f})
        {
            const double raster = ComputeDepthOfFieldCocPixels(lens, depth);
            const double path = ComputePathTracingCocPixels(camera, depth, ValidationHeight);
            worstCocError = std::max(worstCocError, std::abs(raster - path) / std::max(path, 1.0e-3));
            std::cout << "coc_diameter_px depth_m=" << depth << " raster=" << raster
                      << " path_tracing=" << path << '\n';
        }
        if (!lens.bEnabled || worstCocError > 1.0e-5)
        {
            std::cerr << "ラスタのCoCがPTの薄レンズの解析値と一致しません\n";
            return 1;
        }
        CameraProxy pinholeCamera = camera;
        pinholeCamera.FocusDistance = 0.0f;
        if (BuildDepthOfFieldLens(pinholeCamera, ValidationHeight).bEnabled)
        {
            std::cerr << "ピント距離0で被写界深度が無効になりません\n";
            return 1;
        }

        RgbaFloatImage pinhole;
        RgbaFloatImage pathDistance;
        RgbaFloatImage reference;
        RgbaFloatImage plus;
        RgbaFloatImage minus;
        RgbaFloatImage sanityPlus;
        RgbaFloatImage sanityMinus;
        RgbaFloatImage rasterPinhole;
        RgbaFloatImage rasterPipeline;
        if (!ReadMedianDump(directory, "pt-pinhole", pinhole) ||
            !ReadDump(directory, "pt-distance", pathDistance) ||
            !ReadMedianDump(directory, "pt-dof", reference) ||
            !ReadMedianDump(directory, "pt-dof-plus20", plus) ||
            !ReadMedianDump(directory, "pt-dof-minus20", minus) ||
            !ReadMedianDump(directory, "pt-dof-plus40", sanityPlus) ||
            !ReadMedianDump(directory, "pt-dof-minus40", sanityMinus) ||
            !ReadDump(directory, "raster-pinhole", rasterPinhole) ||
            !ReadDump(directory, "raster-dof", rasterPipeline))
        {
            return 1;
        }
        const RgbaFloatImage* images[] = {&pinhole, &pathDistance, &plus, &minus, &sanityPlus,
                                          &sanityMinus, &rasterPinhole, &rasterPipeline};
        for (const RgbaFloatImage* image : images)
        {
            if (image->Width != reference.Width || image->Height != reference.Height ||
                reference.Width % BlockSize != 0u || reference.Height % BlockSize != 0u)
            {
                std::cerr << "R8被写界深度の画像の寸法が一致しません\n";
                return 1;
            }
        }

        RgbaFloatImage raster;
        uint32_t validationErrors = 0u;
        const int applyResult =
            ApplyRasterDepthOfField(camera, pinhole, pathDistance, raster, validationErrors);
        if (applyResult != 0)
        {
            return applyResult;
        }
        std::cout << "VUID_COUNT=" << validationErrors << '\n';
        if (FindFirstNonFinite(raster).Kind != NonFiniteKind::None)
        {
            std::cerr << "ラスタの被写界深度の出力に有限でない値があります\n";
            return 1;
        }
        // 差の分類（前ボケの半透明、面の境界の漏れ等）の調査に使えるよう、ラスタの出力も書き出す。
        if (!WriteRgbaFloatDump(DumpPath(directory, "raster-dof-on-pt-pinhole"), raster, 0u))
        {
            std::cerr << "ラスタの被写界深度の出力を書き出せません\n";
            return 1;
        }
        std::cout << "r8_dof_image name=raster-dof-on-pt-pinhole mean_luminance="
                  << MeanLuminance(raster) << '\n';

        // 閾値の物差し（PT同士）と平均は全画素で測る（空のマスク）。
        const VariableArray<uint8_t> allPixels;
        // ラスタの一致画素の画素単位最大と8×8区画最大は、PTの薄レンズがピンホールの像に無い面を見る画素を
        // 除いて測る（除いた画素は参照の値へ置き換える）。
        uint32_t lensExcluded = 0u;
        uint32_t missExcluded = 0u;
        const VariableArray<uint8_t> agreement =
            BuildLensVisibilityAgreement(camera, pathDistance, lensExcluded, missExcluded);
        const double excludedFraction =
            static_cast<double>(lensExcluded + missExcluded) / agreement.size();
        std::cout << "r8_dof_exclusion lens_hidden_surface_pixels=" << lensExcluded
                  << " pinhole_miss_neighborhood_pixels=" << missExcluded
                  << " fraction=" << excludedFraction << '\n';
        FlipMeasurement yardstickPlus;
        FlipMeasurement yardstickMinus;
        FlipMeasurement checkPlus;
        FlipMeasurement checkMinus;
        FlipMeasurement rasterMeasurement;
        FlipMeasurement pinholeMeasurement;
        FlipMeasurement pipelineMeasurement;
        FlipMeasurement pipelineEngaged;
        if (!MeasureFlip(reference, plus, allPixels, BlockSize, yardstickPlus) ||
            !MeasureFlip(reference, minus, allPixels, BlockSize, yardstickMinus) ||
            !MeasureFlip(reference, sanityPlus, allPixels, BlockSize, checkPlus) ||
            !MeasureFlip(reference, sanityMinus, allPixels, BlockSize, checkMinus) ||
            !MeasureFlip(reference, raster, allPixels, BlockSize, rasterMeasurement) ||
            !MeasureFlip(reference, pinhole, allPixels, BlockSize, pinholeMeasurement) ||
            !MeasureFlip(reference, rasterPipeline, allPixels, BlockSize, pipelineMeasurement) ||
            !MeasureFlip(rasterPinhole, rasterPipeline, allPixels, BlockSize, pipelineEngaged))
        {
            std::cerr << "FLIPを評価できません\n";
            return 1;
        }
        FlipMeasurement rasterExcluded;
        FlipMeasurement leakExcluded;
        // 負の対照: 除外の外（一致画素）で最も暗い1画素へ、参照の平均輝度のLeakScale倍の光を足した参照。
        const RgbaFloatImage leaky =
            AddLocalLeak(reference, MeanLuminance(reference), agreement, BlockSize, 1u, LeakScale);
        if (!MeasureFlip(reference, NeutralizeExcluded(raster, reference, agreement), agreement, BlockSize,
                         rasterExcluded) ||
            !MeasureFlip(reference, NeutralizeExcluded(leaky, reference, agreement), agreement, BlockSize,
                         leakExcluded))
        {
            std::cerr << "FLIPを評価できません\n";
            return 1;
        }
        const double meanLimit = std::min(yardstickPlus.Mean, yardstickMinus.Mean);
        const float pixelLimit = std::min(yardstickPlus.PixelMax, yardstickMinus.PixelMax);
        const float blockLimit = std::min(yardstickPlus.BlockMax, yardstickMinus.BlockMax);
        PrintFlipMeasurement("yardstick_aperture_plus20", yardstickPlus);
        PrintFlipMeasurement("yardstick_aperture_minus20", yardstickMinus);
        PrintFlipMeasurement("sanity_aperture_plus40", checkPlus);
        PrintFlipMeasurement("sanity_aperture_minus40", checkMinus);
        std::cout << "r8_dof_threshold mean_flip<=" << meanLimit << " pixel_max_flip<=" << pixelLimit
                  << " block8_max_flip<=" << blockLimit << '\n';
        const bool bSanity = checkPlus.Mean > meanLimit && checkMinus.Mean > meanLimit &&
                             checkPlus.PixelMax > pixelLimit && checkMinus.PixelMax > pixelLimit &&
                             checkPlus.BlockMax > blockLimit && checkMinus.BlockMax > blockLimit;

        // 差の分類の調査に使えるよう、ラスタと物差し（+20%）の画素ごとのFLIP誤差もRへ書き出す。
        for (const auto& [name, measurement] :
             {std::pair<const char*, const FlipMeasurement*>{"flip-raster", &rasterMeasurement},
              std::pair<const char*, const FlipMeasurement*>{"flip-plus20", &yardstickPlus}})
        {
            RgbaFloatImage errorImage;
            errorImage.Width = reference.Width;
            errorImage.Height = reference.Height;
            errorImage.Values.resize(static_cast<size_t>(reference.Width) * reference.Height * 4u, 0.0f);
            for (size_t index = 0u; index < measurement->AgreeingErrorMap.size(); ++index)
            {
                errorImage.Values[index * 4u] = measurement->AgreeingErrorMap[index];
            }
            if (!WriteRgbaFloatDump(DumpPath(directory, name), errorImage, 0u))
            {
                std::cerr << "FLIPの誤差の画像を書き出せません\n";
                return 1;
            }
        }
        PrintFlipMeasurement("r8_raster_dof_vs_path_tracing", rasterMeasurement);
        PrintFlipMeasurement("r8_raster_dof_vs_path_tracing_excluded", rasterExcluded);
        PrintAgreeingPixelsOverLimit(rasterExcluded, pixelLimit, raster.Width);
        PrintFlipMeasurement("negative_local_leak_excluded", leakExcluded);
        // 欠陥は平均と8×8区画では閾値内に埋もれ、一致画素の画素単位最大だけが閾値の外に出ること。
        const bool bLeakDetected = leakExcluded.Mean <= meanLimit && leakExcluded.BlockMax <= blockLimit &&
                                   leakExcluded.AgreeingPixelMax > pixelLimit;
        std::cout << "negative_local_leak_detected=" << (bLeakDetected ? 1 : 0) << '\n';
        PrintFlipMeasurement("info_pinhole_vs_path_tracing_dof", pinholeMeasurement);
        PrintFlipMeasurement("info_raster_pipeline_dof_vs_path_tracing_dof", pipelineMeasurement);
        PrintFlipMeasurement("info_raster_pipeline_dof_vs_raster_pinhole", pipelineEngaged);
        // 実際のラスタのパイプラインでもpassが働き、ピント距離0の画像から変わること。
        const bool bPipelineEngaged = pipelineEngaged.Mean > 0.0;
        std::cout << "raster_pipeline_dof_engaged=" << (bPipelineEngaged ? 1 : 0) << '\n';

        const bool bWithin = rasterMeasurement.Mean <= meanLimit &&
                             rasterExcluded.AgreeingPixelMax <= pixelLimit &&
                             rasterExcluded.BlockMax <= blockLimit;
        const bool bPassed = bSanity && bLeakDetected && bWithin && bPipelineEngaged && validationErrors == 0u;
        std::cout << "r8_dof_reference_comparison=" << (bPassed ? "PASS" : "FAIL")
                  << " sanity=" << (bSanity ? "PASS" : "FAIL")
                  << " within_threshold=" << (bWithin ? 1 : 0) << '\n';
        if (!bLeakDetected)
        {
            std::cerr << "除外の外に置いた局所欠陥を検出できません\n";
        }
        if (!bSanity)
        {
            std::cerr << "f値±40%の変化が閾値の外に出ず、物差しが変化量に対して単調ではありません\n";
        }
        if (validationErrors != 0u)
        {
            std::cerr << "Vulkan validation errorを検出しました: " << validationErrors << '\n';
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
            if (IsForcedGpuTestSkipRequested())
            {
                return ReportGpuTestSkip(TestName, "環境変数によりGPU検証をスキップします");
            }
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
    // ラスタはR6と同じRTGI（ray query）、PTはPathTracingPassの必要機能を求める。
    const bool bSupported = bPathTracing
        ? Core::Rendering::PathTracingPass::IsSupported(capabilities)
        : capabilities.RayTracing.bAccelerationStructure && capabilities.RayTracing.bRayQuery &&
              capabilities.bBufferDeviceAddress && capabilities.bShaderInt64;
    if (!bSupported)
    {
        return ReportGpuTestSkip(TestName, "RTGIまたはパストレーサーに必要なVulkan機能を利用できません");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("R8 被写界深度 PT参照比較");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = false;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("R8DepthOfFieldPathTracingReferenceVulkan.log");
    config.CreateHandler = &CreateHandler;
    config.Arguments.push_back(TEXT("--scene=indoor"));
    config.Arguments.push_back(TEXT("--capture-source=scene-color"));
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
