#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingGoldenImage.h"
#include "RenderingValidation/RenderingPerceptualDiff.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Engine/Engine.h"
#include "Math/MatrixUtils.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    const char* SceneName(SceneKind scene)
    {
        return scene == SceneKind::Indoor ? "indoor" : "outdoor";
    }

    // 予備の経路（--visibility-buffer=off。GBuffer をラスタで描く）は、解決が GBuffer を書く既定とは画素が少し違う
    // （解析的な微分・三角形の接線の基底）ので、別の基準画像を持つ。しきい値は同じ表の同じシーンの行を使う。
    const TCHAR* BaselineFileName(SceneKind scene, bool bGBufferFallback)
    {
        if (bGBufferFallback)
        {
            return scene == SceneKind::Indoor ? TEXT("IndoorGBufferFallback.png") : TEXT("OutdoorGBufferFallback.png");
        }
        return scene == SceneKind::Indoor ? TEXT("Indoor.png") : TEXT("Outdoor.png");
    }

    const char* StatusName(GoldenImageStatus status)
    {
        switch (status)
        {
        case GoldenImageStatus::Success:
            return "Success";
        case GoldenImageStatus::CaptureNotSuccessful:
            return "CaptureNotSuccessful";
        case GoldenImageStatus::UnsupportedFormat:
            return "UnsupportedFormat";
        case GoldenImageStatus::InvalidDimensions:
            return "InvalidDimensions";
        case GoldenImageStatus::InvalidPixelData:
            return "InvalidPixelData";
        case GoldenImageStatus::EncodeFailed:
            return "EncodeFailed";
        case GoldenImageStatus::DecodeFailed:
            return "DecodeFailed";
        case GoldenImageStatus::FileOpenFailed:
            return "FileOpenFailed";
        case GoldenImageStatus::FileWriteFailed:
            return "FileWriteFailed";
        }
        return "Unknown";
    }

    Core::Container::String BaselinePath(SceneKind scene, bool bGBufferFallback)
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/");
        path += BaselineFileName(scene, bGBufferFallback);
        return path;
    }

    Core::Container::String StagingPath(SceneKind scene, bool bGBufferFallback)
    {
        Core::Container::String path(NORVES_BINARY_ROOT);
        path += TEXT("/RenderingValidation/BaselineStaging/");
        path += BaselineFileName(scene, bGBufferFallback);
        path += TEXT(".tmp");
        return path;
    }

    Core::Container::String ThresholdPath()
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/VisualThresholds.tsv");
        return path;
    }

    // GTAOの部屋の大きさの検査（Cornell、5.5 m四方の部屋）で撮るAOの画像の保存先
    Core::Container::String GtaoCornellCapturePath()
    {
        Core::Container::String path(NORVES_BINARY_ROOT);
        path += TEXT("/RenderingValidation/GTAO/CornellAmbientOcclusion.png");
        return path;
    }

    void EnsureGtaoCaptureDirectories()
    {
        Core::Container::String validationRoot(NORVES_BINARY_ROOT);
        validationRoot += TEXT("/RenderingValidation");
        Core::Container::String gtaoRoot = validationRoot;
        gtaoRoot += TEXT("/GTAO");
        CreateDirectory(validationRoot.c_str(), nullptr);
        CreateDirectory(gtaoRoot.c_str(), nullptr);
    }

    Core::Container::String CanopyPath(bool bStaging)
    {
        Core::Container::String path(bStaging ? NORVES_BINARY_ROOT : NORVES_SOURCE_ROOT);
        path += bStaging ? TEXT("/RenderingValidation/BaselineStaging/SkyOcclusionCanopy.png.tmp")
                         : TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/SkyOcclusionCanopy.png");
        return path;
    }

    Core::Container::String CanopyEnabledPath()
    {
        Core::Container::String path(NORVES_BINARY_ROOT);
        path += TEXT("/RenderingValidation/SkyOcclusion/CanopyEnabled.png");
        return path;
    }

    void EnsureCanopyDirectories()
    {
        Core::Container::String root(NORVES_BINARY_ROOT);
        root += TEXT("/RenderingValidation");
        CreateDirectory(root.c_str(), nullptr);
        Core::Container::String staging = root;
        staging += TEXT("/BaselineStaging");
        CreateDirectory(staging.c_str(), nullptr);
        root += TEXT("/SkyOcclusion");
        CreateDirectory(root.c_str(), nullptr);
    }

    double DecodeSrgbByte(uint8_t value)
    {
        const double encoded = static_cast<double>(value) / 255.0;
        return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
    }

    constexpr float R3DensityValues[] = {0.0025f, 0.005f, 0.01f};
    constexpr uint32_t R3DensityCount =
        static_cast<uint32_t>(sizeof(R3DensityValues) / sizeof(R3DensityValues[0]));
    constexpr double R3MaximumMeanFlipError = 0.02;
    constexpr uint8_t R3MaximumChannelDelta = 8u;

    const char* R3DensityCaseName(uint32_t index)
    {
        switch (index)
        {
        case 0u:
            return "low";
        case 1u:
            return "medium";
        case 2u:
            return "high";
        default:
            return "invalid";
        }
    }

    const TCHAR* R3DensityBaselineFileName(uint32_t index)
    {
        switch (index)
        {
        case 0u:
            return TEXT("R3FogDensityLow.png");
        case 1u:
            return TEXT("R3FogDensityMedium.png");
        case 2u:
            return TEXT("R3FogDensityHigh.png");
        default:
            return TEXT("R3FogDensityInvalid.png");
        }
    }

    Core::Container::String R3DensityBaselinePath(uint32_t index)
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Baselines/RenderingValidation/");
        path += R3DensityBaselineFileName(index);
        return path;
    }

    Core::Container::String R3DensityStagingPath(uint32_t index)
    {
        Core::Container::String path(NORVES_BINARY_ROOT);
        path += TEXT("/RenderingValidation/BaselineStaging/");
        path += R3DensityBaselineFileName(index);
        path += TEXT(".tmp");
        return path;
    }

    Core::Container::String R3DensityThresholdPath()
    {
        Core::Container::String path(NORVES_SOURCE_ROOT);
        path += TEXT("/Test/Core/Rendering/Thresholds/RenderingValidation/R3DensitySweep.tsv");
        return path;
    }

    void EnsureR3BaselineStagingDirectories()
    {
        Core::Container::String validationRoot(NORVES_BINARY_ROOT);
        validationRoot += TEXT("/RenderingValidation");
        Core::Container::String stagingRoot = validationRoot;
        stagingRoot += TEXT("/BaselineStaging");
        CreateDirectory(validationRoot.c_str(), nullptr);
        CreateDirectory(stagingRoot.c_str(), nullptr);
    }

    bool LoadR3DensityGoldenThresholds(VisualGoldenThresholds& outThresholds)
    {
        FileStream::FileStreamUniquePtr stream = FileStream::FileStream::CreateUnique(
            R3DensityThresholdPath(), FileStream::FileMode::Read, FileStream::FileAccess::Read);
        if (stream == nullptr)
        {
            return false;
        }
        const Core::Container::String contents = stream->ReadString();
        if (contents.find(TEXT("schema=NorvesLib.RenderingValidation.R3DensitySweep.v1")) ==
                Core::Container::String::npos ||
            contents.find(TEXT("golden_mean_flip_max=0.020000 golden_max_channel_delta=8")) ==
                Core::Container::String::npos)
        {
            return false;
        }
        outThresholds.MaximumMeanFlipError = R3MaximumMeanFlipError;
        outThresholds.MaximumChannelDelta = R3MaximumChannelDelta;
        return true;
    }

    class GoldenHandler final : public RenderingValidationApplicationHandler
    {
    protected:
        bool OnPreInitialize(
            const Core::Container::VariableArray<Core::Container::String>& args) override
        {
            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            if (GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::BackBuffer)
            {
                LOG_ERROR("RenderingGoldenImageTest は BackBuffer capture のみを受け付けます");
                return false;
            }
            // 既定は解決を通ったこと、--visibility-buffer=off は通っていないことを、画像の比較の後にログで確かめる
            RequireVisibilityBufferPath();
            if (m_bR3DensityScenario && GetRunConfig().Scene != SceneKind::Outdoor)
            {
                LOG_ERROR("R3 density-sweep には outdoor scene が必要です");
                return false;
            }
            if (m_bR3DensityScenario &&
                (m_bWriteBaselineStaging || m_bMeasureVisual))
            {
                LOG_ERROR("R3 density-sweep は既存のgolden image modeと併用できません");
                return false;
            }
            if (m_bWriteR3DensityBaselineStaging && !m_bR3DensityScenario)
            {
                LOG_ERROR("R3 baseline stagingには density-sweep scenario が必要です");
                return false;
            }
            if (m_bGtaoCornellScenario && GetRunConfig().Scene != SceneKind::Indoor)
            {
                LOG_ERROR("GTAOの部屋の大きさの検査には indoor scene が必要です");
                return false;
            }
            if (m_bCanopyScenario &&
                (GetRunConfig().Scene != SceneKind::Outdoor || GetRunConfig().bPathTracing ||
                 GetRunConfig().bVisibilityBufferOff || m_bR3DensityScenario || m_bGtaoCornellScenario ||
                 m_bMeasureVisual || m_bWriteR3DensityBaselineStaging))
            {
                LOG_ERROR("樹冠の検査には屋外の既定描画経路が必要で、他の場面とは併用できません");
                return false;
            }
            return true;
        }

        bool OnInitialize() override
        {
            if (!RenderingValidationApplicationHandler::OnInitialize())
            {
                return false;
            }
            if (m_bCanopyScenario)
            {
                const auto& coordinator = Core::Engine::GEngine->GetRenderWorld().GetRenderingCoordinator();
                if (coordinator.IsRTGIEnabled() || coordinator.GetDDGIVolumeParameters().bEnabled)
                {
                    LOG_ERROR("樹冠の検査では RTGI と DDGI を無効にする必要があります");
                    return false;
                }
                return GetFixture().ApplySkyOcclusionCanopyFixture();
            }
            if (m_bR3DensityScenario)
            {
                return GetFixture().ApplyR3ShadowedShaftsFixture(true, true, true);
            }
            if (m_bGtaoCornellScenario)
            {
                return GetFixture().ApplyR4CornellFixture();
            }
            if (!GetFixture().ApplyTransparentPhysicalLightingObjectPresence())
            {
                LOG_ERROR("golden object-presence fixture preparation failed");
                return false;
            }
            return true;
        }

        bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& outFailureReason) override
        {
            if (argument == TEXT("--sky-occlusion-scenario=canopy"))
            {
                if (m_bCanopyScenario)
                {
                    outFailureReason = TEXT("樹冠の場面が重複しています");
                    return false;
                }
                m_bCanopyScenario = true;
                return true;
            }
            if (argument == TEXT("--ao-scenario=cornell-room"))
            {
                if (m_bGtaoCornellScenario || m_bR3DensityScenario || m_bWriteBaselineStaging ||
                    m_bMeasureVisual || m_bWriteR3DensityBaselineStaging)
                {
                    outFailureReason = TEXT("GTAOの部屋の大きさの検査は他のgolden image modeと併用できません");
                    return false;
                }
                m_bGtaoCornellScenario = true;
                return true;
            }
            if (argument == TEXT("--r3-scenario=density-sweep"))
            {
                if (m_bR3DensityScenario || m_bWriteBaselineStaging || m_bMeasureVisual ||
                    m_bWriteR3DensityBaselineStaging)
                {
                    outFailureReason = TEXT("重複または競合するR3 goldenシナリオ指定です");
                    return false;
                }
                m_bR3DensityScenario = true;
                return true;
            }
            if (argument == TEXT("--write-r3-density-baseline-staging"))
            {
                if (m_bWriteR3DensityBaselineStaging || m_bWriteBaselineStaging || m_bMeasureVisual)
                {
                    outFailureReason = TEXT("重複または競合するR3 baseline staging指定です");
                    return false;
                }
                m_bWriteR3DensityBaselineStaging = true;
                return true;
            }
            if (argument == TEXT("--write-baseline-staging"))
            {
                if (m_bWriteBaselineStaging || m_bMeasureVisual || m_bR3DensityScenario ||
                    m_bWriteR3DensityBaselineStaging)
                {
                    outFailureReason = TEXT("重複または競合するgolden image mode指定です");
                    LOG_ERROR("golden image argument rejected: duplicate or conflicting mode");
                    return false;
                }
                m_bWriteBaselineStaging = true;
                return true;
            }
            if (argument == TEXT("--measure-visual"))
            {
                if (m_bMeasureVisual || m_bWriteBaselineStaging || m_bR3DensityScenario ||
                    m_bWriteR3DensityBaselineStaging)
                {
                    outFailureReason = TEXT("duplicate or conflicting golden image mode");
                    LOG_ERROR("golden image argument rejected: duplicate or conflicting mode");
                    return false;
                }
                m_bMeasureVisual = true;
                return true;
            }
            outFailureReason = TEXT("unsupported golden image argument");
            LOG_ERROR("golden image argument rejected: unsupported option");
            return false;
        }

        void ApplyCaptureStageState(Core::Rendering::RenderWorld& renderWorld) override
        {
            if (m_bCanopyScenario)
            {
                renderWorld.SetMainCamera(GetFixture().GetSkyOcclusionCanopyCamera());
                Core::Rendering::SkyOcclusionParameters parameters =
                    Core::Rendering::MakeDefaultSkyOcclusionParameters();
                parameters.GroundHeightMeters = -1.0f;
                parameters.bUseCenterOverride = true;
                parameters.CenterOverrideX = 4.25f;
                parameters.CenterOverrideZ = 0.0f;
                parameters.bEnabled = m_CanopyStage == 1u;
                renderWorld.SetSkyOcclusionParameters(parameters);
                return;
            }
            if (m_bGtaoCornellScenario)
            {
                // 部屋の内側をCornellの検証カメラで撮り、雑音除去後のGTAOの可視率をそのまま表示する
                renderWorld.SetMainCamera(GetFixture().GetR4CornellCamera());
                renderWorld.SetDebugViewModeAll(Core::Rendering::DebugViewMode::AmbientOcclusion);
                return;
            }
            if (!m_bR3DensityScenario || m_R3DensityStage >= R3DensityCount)
            {
                return;
            }
            if (!GetFixture().ApplyR3ShadowedShaftsFixture(true, true, true))
            {
                m_bR3DensityStageApplyFailed = true;
                LOG_ERROR("R3 density-sweep fixtureの状態を適用できませんでした");
                return;
            }
            renderWorld.SetMainCamera(GetFixture().GetR3ShadowedShaftsCamera());
            Core::Rendering::SkyAtmosphereParameters sky =
                Core::Rendering::MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = false;
            renderWorld.SetSkyAtmosphere(sky);
            Core::Rendering::VolumetricFogParameters fog =
                Core::Rendering::MakeDefaultVolumetricFogParameters();
            fog.bEnabled = true;
            fog.DensityAtBaseHeight = R3DensityValues[m_R3DensityStage];
            renderWorld.SetVolumetricFogParameters(fog);
        }

        void AdvanceCaptureStage() override
        {
            if (m_bCanopyScenario && m_bCanopyStageFinished && !m_bCanopyDone)
            {
                m_CanopyStage = 1u;
                m_bCanopyStageFinished = false;
                m_bCanopyHasFirstFrame = false;
            }
            if (m_bR3DensityScenario && m_R3DensityStage < R3DensityCount)
            {
                ++m_R3DensityStage;
            }
        }

        bool RequestFollowupCapture(
            const Core::Rendering::CapturedFrame& frame,
            Core::Rendering::FrameCaptureRequest& outRequest) override
        {
            if (m_bCanopyScenario)
            {
                if (m_bCanopyDone)
                {
                    return false;
                }
                outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::BackBuffer;
                return true;
            }
            (void)frame;
            if (!m_bR3DensityScenario || m_R3DensityStage >= R3DensityCount)
            {
                return false;
            }
            outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::BackBuffer;
            return true;
        }

        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason) override
        {
            if (m_bCanopyScenario)
            {
                return EvaluateCanopyFrame(frame, outFailureReason);
            }
            if (m_bR3DensityScenario)
            {
                return EvaluateR3DensityGoldenFrame(frame, outFailureReason);
            }
            if (m_bGtaoCornellScenario)
            {
                return EvaluateGtaoCornellFrame(frame, outFailureReason);
            }
            Core::Container::VariableArray<uint8_t> candidatePng;
            const GoldenImageStatus encodeStatus = EncodeCapturedFramePng(frame, candidatePng);
            if (encodeStatus != GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("captured frame PNG encode failed");
                LOG_ERROR(
                    "golden image encode failed: scene=%s request=%llu frame=%llu status=%s",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    StatusName(encodeStatus));
                return false;
            }

            if (m_bWriteBaselineStaging)
            {
                const GoldenImageStatus saveStatus = SavePng(
                    StagingPath(GetRunConfig().Scene, GetRunConfig().bVisibilityBufferOff),
                    Core::Container::Span<const uint8_t>(candidatePng));
                if (saveStatus != GoldenImageStatus::Success)
                {
                    outFailureReason = TEXT("fixed staging PNG write failed");
                    LOG_ERROR(
                        "golden image staging write failed: scene=%s request=%llu frame=%llu status=%s",
                        SceneName(GetRunConfig().Scene),
                        static_cast<unsigned long long>(frame.RequestId),
                        static_cast<unsigned long long>(frame.FrameNumber),
                        StatusName(saveStatus));
                    return false;
                }
                LOG_INFO(
                    "golden image staging written: scene=%s request=%llu frame=%llu",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber));
                return true;
            }

            Rgba8Image reference;
            GoldenImageStatus status = LoadPng(BaselinePath(GetRunConfig().Scene, GetRunConfig().bVisibilityBufferOff), reference);
            if (status != GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("golden baseline PNG load failed");
                LOG_ERROR(
                    "golden baseline load failed: scene=%s request=%llu frame=%llu status=%s",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    StatusName(status));
                return false;
            }

            Rgba8Image candidate;
            status = DecodePng(Core::Container::Span<const uint8_t>(candidatePng), candidate);
            if (status != GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("captured frame PNG decode failed");
                LOG_ERROR(
                    "golden candidate decode failed: scene=%s request=%llu frame=%llu status=%s",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    StatusName(status));
                return false;
            }

            if (m_bMeasureVisual)
            {
                double targetSum = 0.0;
                double backgroundSum = 0.0;
                uint32_t targetCount = 0u;
                uint32_t backgroundCount = 0u;
                for (uint32_t y = 112u; y <= 143u; ++y)
                {
                    for (uint32_t x = 22u; x <= 53u; ++x)
                    {
                        const size_t offset = static_cast<size_t>(y) * candidate.RowPitchBytes +
                                               static_cast<size_t>(x) * 4u;
                        targetSum += 0.2126 * candidate.Pixels[offset + 0u] +
                                     0.7152 * candidate.Pixels[offset + 1u] +
                                     0.0722 * candidate.Pixels[offset + 2u];
                        ++targetCount;
                    }
                    for (uint32_t x = 58u; x <= 89u; ++x)
                    {
                        const size_t offset = static_cast<size_t>(y) * candidate.RowPitchBytes +
                                               static_cast<size_t>(x) * 4u;
                        backgroundSum += 0.2126 * candidate.Pixels[offset + 0u] +
                                         0.7152 * candidate.Pixels[offset + 1u] +
                                         0.0722 * candidate.Pixels[offset + 2u];
                        ++backgroundCount;
                    }
                }
                if (targetCount != 1024u || backgroundCount != 1024u)
                {
                    outFailureReason = TEXT("object-presence ROI sample count is invalid");
                    return false;
                }
                const double targetMean = targetSum / static_cast<double>(targetCount);
                const double backgroundMean = backgroundSum / static_cast<double>(backgroundCount);
                const double delta = std::abs(targetMean - backgroundMean);
                std::cout << std::fixed << std::setprecision(9)
                          << "NORVESLIB_OBJECT_PRESENCE scene=" << SceneName(GetRunConfig().Scene)
                          << " target_mean_y8=" << targetMean
                          << " background_mean_y8=" << backgroundMean
                          << " delta_y8=" << delta << std::endl;
                if (!std::isfinite(targetMean) || !std::isfinite(backgroundMean) ||
                    !std::isfinite(delta) || targetMean < 0.0 || targetMean > 255.0 ||
                    backgroundMean < 0.0 || backgroundMean > 255.0 || delta < 8.0)
                {
                    outFailureReason = TEXT("object-presence Y8 delta is below 8");
                    return false;
                }
                PerceptualDifferenceMetrics visualMetrics;
                const PerceptualDiffStatus visualStatus = CompareLdrFlip(reference, candidate, visualMetrics);
                if (visualStatus != PerceptualDiffStatus::Success)
                {
                    outFailureReason = TEXT("LDR-FLIP measurement failed");
                    LOG_ERROR(
                        "golden visual measurement failed: scene=%s request=%llu frame=%llu status=%u",
                        SceneName(GetRunConfig().Scene),
                        static_cast<unsigned long long>(frame.RequestId),
                        static_cast<unsigned long long>(frame.FrameNumber),
                        static_cast<unsigned int>(visualStatus));
                    return false;
                }
                std::cout << std::fixed << std::setprecision(9)
                          << "NORVESLIB_VISUAL_MEASUREMENT scene=" << SceneName(GetRunConfig().Scene)
                          << " mean_flip=" << visualMetrics.MeanFlipError
                          << " max_flip=" << visualMetrics.MaxFlipError
                          << " raw_max=" << static_cast<unsigned int>(visualMetrics.Raw.MaxChannelDelta)
                          << std::endl;
                return true;
            }

            VisualGoldenThresholds thresholds;
            ArtificialDifferenceSpec artificialDifference;
            if (!LoadVisualGoldenThresholds(
                    ThresholdPath(), GetRunConfig().Scene, thresholds, artificialDifference))
            {
                outFailureReason = TEXT("visual threshold row is missing or invalid");
                LOG_ERROR(
                    "golden visual threshold load failed: scene=%s request=%llu frame=%llu",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber));
                return false;
            }

            PerceptualDifferenceMetrics metrics;
            const PerceptualDiffStatus visualStatus = CompareLdrFlip(reference, candidate, metrics);
            if (visualStatus != PerceptualDiffStatus::Success)
            {
                outFailureReason = TEXT("LDR-FLIP comparison failed");
                LOG_ERROR(
                    "golden visual comparison failed: scene=%s request=%llu frame=%llu status=%u",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    static_cast<unsigned int>(visualStatus));
                return false;
            }
            if (!MeetsVisualGoldenThresholds(metrics, thresholds))
            {
                outFailureReason = TEXT("golden image visual comparison failed");
                LOG_ERROR(
                    "golden visual mismatch: scene=%s request=%llu frame=%llu "
                    "mean_flip=%.9f mean_limit=%.9f max_flip=%.9f max_flip_coordinate=(%u,%u) "
                    "raw_max=%u raw_max_coordinate=(%u,%u) differing_pixels=%llu",
                    SceneName(GetRunConfig().Scene),
                    static_cast<unsigned long long>(frame.RequestId),
                    static_cast<unsigned long long>(frame.FrameNumber),
                    metrics.MeanFlipError,
                    thresholds.MaximumMeanFlipError,
                    static_cast<double>(metrics.MaxFlipError),
                    metrics.MaxFlipX,
                    metrics.MaxFlipY,
                    static_cast<unsigned int>(metrics.Raw.MaxChannelDelta),
                    metrics.Raw.MaxDifferenceX,
                    metrics.Raw.MaxDifferenceY,
                    static_cast<unsigned long long>(metrics.Raw.DifferingPixelCount));
                return false;
            }

            std::cout << std::fixed << std::setprecision(9)
                      << "NORVESLIB_VISUAL_METRICS scene=" << SceneName(GetRunConfig().Scene)
                      << " mean_flip=" << metrics.MeanFlipError
                      << " max_flip=" << metrics.MaxFlipError
                      << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                      << std::endl;
            LOG_INFO(
                "golden image matched: scene=%s request=%llu frame=%llu mean_flip=%.9f raw_max=%u",
                SceneName(GetRunConfig().Scene),
                static_cast<unsigned long long>(frame.RequestId),
                static_cast<unsigned long long>(frame.FrameNumber),
                metrics.MeanFlipError,
                static_cast<unsigned int>(metrics.Raw.MaxChannelDelta));
            return true;
        }

        bool EvaluateR3DensityGoldenFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason)
        {
            if (m_bR3DensityStageApplyFailed || m_R3DensityStage >= R3DensityCount)
            {
                outFailureReason = TEXT("R3 density-sweep のcapture状態が不正です");
                return false;
            }
            if (frame.RequestId != GetLastAcceptedRequestId() ||
                (m_bR3DensityHasFrameNumber && frame.FrameNumber <= m_R3DensityLastFrameNumber))
            {
                outFailureReason = TEXT("R3 density-sweep のcapture順序が不正です");
                return false;
            }

            Core::Container::VariableArray<uint8_t> candidatePng;
            const GoldenImageStatus encodeStatus = EncodeCapturedFramePng(frame, candidatePng);
            if (encodeStatus != GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("R3 density-sweep のPNG encodeに失敗しました");
                return false;
            }
            Rgba8Image candidate;
            if (DecodePng(Core::Container::Span<const uint8_t>(candidatePng), candidate) !=
                GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("R3 density-sweep のPNG decodeに失敗しました");
                return false;
            }
            uint8_t maximumChannel = 0u;
            uint64_t saturatedPixelCount = 0u;
            for (uint32_t y = 0u; y < candidate.Height; ++y)
            {
                const size_t rowOffset = static_cast<size_t>(y) * candidate.RowPitchBytes;
                for (uint32_t x = 0u; x < candidate.Width; ++x)
                {
                    const size_t offset = rowOffset + static_cast<size_t>(x) * 4u;
                    maximumChannel = std::max(maximumChannel, candidate.Pixels[offset + 0u]);
                    maximumChannel = std::max(maximumChannel, candidate.Pixels[offset + 1u]);
                    maximumChannel = std::max(maximumChannel, candidate.Pixels[offset + 2u]);
                    if (candidate.Pixels[offset + 0u] == 255u ||
                        candidate.Pixels[offset + 1u] == 255u ||
                        candidate.Pixels[offset + 2u] == 255u)
                    {
                        ++saturatedPixelCount;
                    }
                }
            }
            if (saturatedPixelCount != 0u)
            {
                outFailureReason = TEXT("R3 density-sweep のBackBufferに飽和画素があります");
                return false;
            }

            if (m_bWriteR3DensityBaselineStaging)
            {
                EnsureR3BaselineStagingDirectories();
                const GoldenImageStatus saveStatus = SavePng(
                    R3DensityStagingPath(m_R3DensityStage),
                    Core::Container::Span<const uint8_t>(candidatePng));
                if (saveStatus != GoldenImageStatus::Success)
                {
                    outFailureReason = TEXT("R3 density-sweep baseline stagingの保存に失敗しました");
                    return false;
                }
                std::cout << std::fixed << std::setprecision(6)
                          << "R3_DENSITY_GOLDEN_STAGE case="
                          << R3DensityCaseName(m_R3DensityStage)
                          << " density=" << R3DensityValues[m_R3DensityStage]
                          << " baseline_staged=1 maximum_channel="
                          << static_cast<unsigned int>(maximumChannel)
                          << " saturated_rgb_pixels=" << saturatedPixelCount << "\n";
            }
            else
            {
                Rgba8Image reference;
                if (LoadPng(R3DensityBaselinePath(m_R3DensityStage), reference) !=
                    GoldenImageStatus::Success)
                {
                    outFailureReason = TEXT("R3 density-sweep baseline PNGを読み込めません");
                    return false;
                }
                VisualGoldenThresholds thresholds;
                if (!LoadR3DensityGoldenThresholds(thresholds))
                {
                    outFailureReason = TEXT("R3 density-sweep thresholdが不正です");
                    return false;
                }
                PerceptualDifferenceMetrics metrics;
                const PerceptualDiffStatus compareStatus =
                    CompareLdrFlip(reference, candidate, metrics);
                if (compareStatus != PerceptualDiffStatus::Success ||
                    !MeetsVisualGoldenThresholds(metrics, thresholds))
                {
                    outFailureReason = TEXT("R3 density-sweep golden比較が閾値を満たしません");
                    std::cout << std::fixed << std::setprecision(9)
                              << "R3_DENSITY_GOLDEN_STAGE case="
                              << R3DensityCaseName(m_R3DensityStage)
                              << " density=" << R3DensityValues[m_R3DensityStage]
                              << " mean_flip=" << metrics.MeanFlipError
                              << " mean_flip_max=" << thresholds.MaximumMeanFlipError
                              << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                              << " raw_max_limit="
                              << static_cast<unsigned int>(thresholds.MaximumChannelDelta)
                              << " maximum_channel=" << static_cast<unsigned int>(maximumChannel)
                              << " saturated_rgb_pixels=" << saturatedPixelCount
                              << " passed=0\n";
                    return false;
                }
                std::cout << std::fixed << std::setprecision(9)
                          << "R3_DENSITY_GOLDEN_STAGE case="
                          << R3DensityCaseName(m_R3DensityStage)
                          << " density=" << R3DensityValues[m_R3DensityStage]
                          << " mean_flip=" << metrics.MeanFlipError
                          << " mean_flip_max=" << thresholds.MaximumMeanFlipError
                          << " raw_max=" << static_cast<unsigned int>(metrics.Raw.MaxChannelDelta)
                          << " raw_max_limit="
                          << static_cast<unsigned int>(thresholds.MaximumChannelDelta)
                          << " maximum_channel=" << static_cast<unsigned int>(maximumChannel)
                          << " saturated_rgb_pixels=" << saturatedPixelCount
                          << " passed=1\n";
            }

            m_bR3DensityHasFrameNumber = true;
            m_R3DensityLastFrameNumber = frame.FrameNumber;
            ++m_R3DensityAcceptedCaseCount;
            if (m_R3DensityStage + 1u == R3DensityCount)
            {
                const bool bAllCasesAccepted = m_R3DensityAcceptedCaseCount == R3DensityCount;
                std::cout << "R3_DENSITY_GOLDEN_SWEEP="
                          << (bAllCasesAccepted ? "PASS" : "FAIL")
                          << " cases=" << m_R3DensityAcceptedCaseCount
                          << " mode=" << (m_bWriteR3DensityBaselineStaging ? "staging" : "compare")
                          << " non_saturated=1\n";
                if (!bAllCasesAccepted)
                {
                    outFailureReason = TEXT("R3 density-sweep の3段階goldenが完了しませんでした");
                    return false;
                }
            }
            return true;
        }

        bool EvaluateCanopyFrame(const Core::Rendering::CapturedFrame& frame,
                                 Core::Container::String& outFailureReason)
        {
            if (frame.RequestId != GetLastAcceptedRequestId() ||
                (m_bCanopyHasPreviousFrame && frame.FrameNumber <= m_CanopyPreviousFrame))
            {
                outFailureReason = TEXT("樹冠の撮影順序が不正です");
                return false;
            }
            m_bCanopyHasPreviousFrame = true;
            m_CanopyPreviousFrame = frame.FrameNumber;
            if (!m_bCanopyHasFirstFrame)
            {
                m_bCanopyHasFirstFrame = true;
                m_CanopyFirstFrame = frame.FrameNumber;
            }
            // 初回の取得後も 32 描画フレーム待つ。遮蔽マップの全段の更新、環境 IBL の生成、
            // GTAO の時間蓄積が最初のフレームで完了したと仮定しないため、両方の段で待つ。
            constexpr uint64_t SettledFrames = 32u;
            if (frame.FrameNumber - m_CanopyFirstFrame < SettledFrames)
            {
                return true;
            }
            std::cout << "SKY_OCCLUSION_CANOPY name="
                      << (m_CanopyStage == 0u ? "settled_frames_disabled" : "settled_frames_enabled")
                      << " value=" << (frame.FrameNumber - m_CanopyFirstFrame)
                      << " limit=32.. result=PASS\n";

            Core::Container::VariableArray<uint8_t> png;
            Rgba8Image image;
            if (EncodeCapturedFramePng(frame, png) != GoldenImageStatus::Success ||
                DecodePng(Core::Container::Span<const uint8_t>(png), image) != GoldenImageStatus::Success ||
                image.Width != ValidationWidth || image.Height != ValidationHeight)
            {
                outFailureReason = TEXT("樹冠の画像を読み取れません");
                return false;
            }
            const auto points = GetFixture().GetSkyOcclusionCanopyPoints();
            const auto& camera = GetFixture().GetSkyOcclusionCanopyCamera();
            const auto view = Core::Rendering::CameraViewConstants::BuildViewMatrix(camera);
            const auto projection = Core::Rendering::CameraViewConstants::BuildProjectionMatrix(
                camera, static_cast<float>(image.Width) / static_cast<float>(image.Height));
            // Vulkan の通常ビューと同じ Y・Z の補正を投影行列へ掛ける。
            const auto viewProjection = projection * Math::MatrixUtils::CreateScale(1.0f, -1.0f, -1.0f) * view;
            auto project = [&](const Math::Vector3& point, double& x, double& y) -> bool
            {
                const Math::Vector4 clip = Math::MatrixUtils::TransformPoint(viewProjection, point);
                if (!(clip.w > 0.0f))
                {
                    return false;
                }
                x = (static_cast<double>(clip.x / clip.w) * 0.5 + 0.5) * image.Width;
                y = (static_cast<double>(clip.y / clip.w) * 0.5 + 0.5) * image.Height;
                return std::isfinite(x) && std::isfinite(y) && x >= 0.0 && y >= 0.0 &&
                       x < image.Width && y < image.Height;
            };
            auto luminance = [&](uint32_t x, uint32_t y) -> double
            {
                const size_t offset = static_cast<size_t>(y) * image.RowPitchBytes + static_cast<size_t>(x) * 4u;
                return 0.2126 * DecodeSrgbByte(image.Pixels[offset]) +
                       0.7152 * DecodeSrgbByte(image.Pixels[offset + 1u]) +
                       0.0722 * DecodeSrgbByte(image.Pixels[offset + 2u]);
            };
            auto boxMean = [&](const Math::Vector3& point, int radius, double& mean) -> bool
            {
                double x = 0.0, y = 0.0;
                if (!project(point, x, y))
                {
                    return false;
                }
                const int cx = static_cast<int>(x), cy = static_cast<int>(y);
                if (cx - radius < 0 || cy - radius < 0 ||
                    cx + radius >= static_cast<int>(image.Width) || cy + radius >= static_cast<int>(image.Height))
                {
                    return false;
                }
                double sum = 0.0;
                for (int iy = cy - radius; iy <= cy + radius; ++iy)
                {
                    for (int ix = cx - radius; ix <= cx + radius; ++ix)
                    {
                        sum += luminance(static_cast<uint32_t>(ix), static_cast<uint32_t>(iy));
                    }
                }
                const int side = radius * 2 + 1;
                mean = sum / static_cast<double>(side * side);
                return std::isfinite(mean);
            };
            auto sphereMean = [&](const Math::Vector3& center, double& mean, uint8_t& maximum) -> bool
            {
                double x = 0.0, y = 0.0, edgeX = 0.0, edgeY = 0.0;
                const Math::Vector3 right(camera.RightX, camera.RightY, camera.RightZ);
                if (!project(center, x, y) || !project(center + right * points.SphereRadius, edgeX, edgeY))
                {
                    return false;
                }
                // 鏡球は半径の約 0.7 倍以内が空を映すため、境目を避けて 0.6 倍以内を平均する。
                const double radius = std::hypot(edgeX - x, edgeY - y) * 0.6;
                if (radius < 1.5 || x - radius < 0.0 || y - radius < 0.0 ||
                    x + radius >= image.Width || y + radius >= image.Height)
                {
                    return false;
                }
                double sum = 0.0;
                uint32_t count = 0u;
                maximum = 0u;
                for (int iy = static_cast<int>(std::floor(y - radius)); iy <= static_cast<int>(std::ceil(y + radius)); ++iy)
                {
                    for (int ix = static_cast<int>(std::floor(x - radius)); ix <= static_cast<int>(std::ceil(x + radius)); ++ix)
                    {
                        const double dx = ix + 0.5 - x, dy = iy + 0.5 - y;
                        if (dx * dx + dy * dy <= radius * radius)
                        {
                            sum += luminance(static_cast<uint32_t>(ix), static_cast<uint32_t>(iy));
                            const size_t offset = static_cast<size_t>(iy) * image.RowPitchBytes + static_cast<size_t>(ix) * 4u;
                            maximum = std::max(maximum, image.Pixels[offset]);
                            maximum = std::max(maximum, image.Pixels[offset + 1u]);
                            maximum = std::max(maximum, image.Pixels[offset + 2u]);
                            ++count;
                        }
                    }
                }
                if (count == 0u)
                {
                    return false;
                }
                mean = sum / count;
                return std::isfinite(mean);
            };
            double under = 0.0, open = 0.0;
            if (!boxMean(points.GroundUnder, 2, under) || !boxMean(points.GroundOpen, 2, open) || open <= 0.0)
            {
                outFailureReason = TEXT("樹冠の地面の測定点を画像へ写せません");
                return false;
            }
            if (m_CanopyStage == 0u)
            {
                m_CanopyDisabledUnder = under;
                uint8_t maximum = 0u;
                if (!sphereMean(points.SphereUnder, m_CanopyDisabledSphereUnder, maximum))
                {
                    outFailureReason = TEXT("樹冠の鏡球の測定点を画像へ写せません");
                    return false;
                }
                EnsureCanopyDirectories();
                if (m_bWriteBaselineStaging)
                {
                    if (SavePng(CanopyPath(true), Core::Container::Span<const uint8_t>(png)) !=
                        GoldenImageStatus::Success)
                    {
                        outFailureReason = TEXT("樹冠の基準画像を書き出せません");
                        return false;
                    }
                    m_bCanopyDone = true;
                    return true;
                }
                Rgba8Image reference;
                VisualGoldenThresholds thresholds;
                ArtificialDifferenceSpec artificial;
                PerceptualDifferenceMetrics metrics;
                if (LoadPng(CanopyPath(false), reference) != GoldenImageStatus::Success ||
                    !LoadVisualGoldenThresholds(ThresholdPath(), SceneKind::Outdoor, thresholds, artificial) ||
                    CompareLdrFlip(reference, image, metrics) != PerceptualDiffStatus::Success)
                {
                    outFailureReason = TEXT("樹冠の基準画像または比較条件を読めません");
                    return false;
                }
                const bool passed = MeetsVisualGoldenThresholds(metrics, thresholds);
                std::cout << std::fixed << std::setprecision(9)
                          << "SKY_OCCLUSION_CANOPY name=baseline_mean_flip value=" << metrics.MeanFlipError
                          << " limit=" << thresholds.MaximumMeanFlipError
                          << " result=" << (passed ? "PASS" : "FAIL") << '\n'
                          << "SKY_OCCLUSION_CANOPY name=baseline_max_channel_delta value="
                          << static_cast<unsigned>(metrics.Raw.MaxChannelDelta)
                          << " limit=" << static_cast<unsigned>(thresholds.MaximumChannelDelta)
                          << " result=" << (passed ? "PASS" : "FAIL") << '\n';
                if (!passed)
                {
                    outFailureReason = TEXT("空の遮蔽が無効な樹冠の画像が基準と異なります");
                    return false;
                }
                m_bCanopyStageFinished = true;
                return true;
            }
            EnsureCanopyDirectories();
            if (SavePng(CanopyEnabledPath(), Core::Container::Span<const uint8_t>(png)) !=
                GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("空の遮蔽が有効な画像を保存できません");
                return false;
            }
            double sphereUnder = 0.0, sphereOpen = 0.0;
            uint8_t underMaximum = 0u, openMaximum = 0u;
            if (!sphereMean(points.SphereUnder, sphereUnder, underMaximum) ||
                !sphereMean(points.SphereOpen, sphereOpen, openMaximum) || sphereOpen <= 0.0)
            {
                outFailureReason = TEXT("樹冠の鏡球の測定点を画像へ写せません");
                return false;
            }
            const double sphereRatio = sphereUnder / sphereOpen;
            const bool sphereRatioPassed = std::isfinite(sphereRatio) && sphereRatio <= 0.30;
            const double disabledSphereRatio = sphereUnder / m_CanopyDisabledSphereUnder;
            const double ratio = under / open;
            const bool ratioPassed = std::isfinite(ratio) && ratio >= 0.25 && ratio <= 0.60;
            const double disabledRatio = m_CanopyDisabledUnder > 0.0 ? under / m_CanopyDisabledUnder : 1.0;
            const bool changed = std::isfinite(disabledRatio) && disabledRatio < 0.99;
            std::cout << std::fixed << std::setprecision(9)
                      << "SKY_OCCLUSION_CANOPY name=ground_under value=" << under << " limit=none result=INFO\n"
                      << "SKY_OCCLUSION_CANOPY name=ground_open value=" << open << " limit=0.15..0.5 result=INFO\n"
                      << "SKY_OCCLUSION_CANOPY name=ground_ratio value=" << ratio << " limit=0.25..0.60 result="
                      << (ratioPassed ? "PASS" : "FAIL") << '\n'
                      << "SKY_OCCLUSION_CANOPY name=enabled_over_disabled value=" << disabledRatio
                      << " limit=<0.99 result=" << (changed ? "PASS" : "FAIL") << '\n'
                      << "SKY_OCCLUSION_CANOPY name=sphere_ratio value=" << sphereRatio
                      << " limit=0.30 result=" << (sphereRatioPassed ? "PASS" : "FAIL") << '\n'
                      << "SKY_OCCLUSION_CANOPY name=sphere_under_enabled_over_disabled value=" << disabledSphereRatio
                      << " limit=none result=INFO\n"
                      << "SKY_OCCLUSION_CANOPY name=sphere_open_max_channel value="
                      << static_cast<unsigned>(openMaximum) << " limit=<255 result=INFO\n";
            const Math::Vector3 line = points.EdgeInside - points.EdgeOutside;
            const double length = std::sqrt(static_cast<double>(line.x * line.x + line.y * line.y + line.z * line.z));
            const uint32_t steps = static_cast<uint32_t>(std::lround(length / 0.5));
            if (steps == 0u || std::abs(length - steps * 0.5) > 1.0e-4)
            {
                outFailureReason = TEXT("林縁の標本間隔が 0.5 m ではありません");
                return false;
            }
            double previous = 0.0, maximumRise = 0.0, maximumChange = 0.0;
            for (uint32_t i = 0u; i <= steps; ++i)
            {
                double value = 0.0;
                if (!boxMean(points.EdgeOutside + line * (static_cast<float>(i) / steps), 1, value) || value <= 0.0)
                {
                    outFailureReason = TEXT("林縁の地面の標本を読み取れません");
                    return false;
                }
                std::cout << "SKY_OCCLUSION_CANOPY name=edge_sample_" << i << " value=" << value
                          << " limit=none result=INFO\n";
                if (i > 0u)
                {
                    const double delta = (value - previous) / previous;
                    maximumRise = std::max(maximumRise, delta);
                    maximumChange = std::max(maximumChange, std::abs(delta));
                }
                previous = value;
            }
            const bool monotonic = maximumRise <= 0.005;
            const bool smooth = maximumChange <= 0.15;
            std::cout << "SKY_OCCLUSION_CANOPY name=edge_max_rise value=" << maximumRise
                      << " limit=0.005 result=" << (monotonic ? "PASS" : "FAIL") << '\n'
                      << "SKY_OCCLUSION_CANOPY name=edge_max_relative_change value=" << maximumChange
                      << " limit=0.15 result=" << (smooth ? "PASS" : "FAIL") << '\n';
            if (!ratioPassed || !changed || !monotonic || !smooth || !sphereRatioPassed)
            {
                outFailureReason = TEXT("樹冠の地面の遮蔽率・林縁の連続性・樹冠の下の球の鏡面の比のいずれかが基準を満たしません");
                return false;
            }
            m_bCanopyDone = true;
            return true;
        }

        // 雑音除去後のGTAOの可視率を、Cornellの部屋の面ごとの領域で測る。
        // 部屋の大きさの壁・床・天井の中ほどは遮蔽がほとんど無く（壁全体が黒くならない）、
        // 奥の壁と左右の壁の境目・箱の接地部は中ほどより暗い（遮蔽が出ている）ことを確かめる。
        bool EvaluateGtaoCornellFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason)
        {
            Core::Container::VariableArray<uint8_t> capturePng;
            Rgba8Image image;
            if (EncodeCapturedFramePng(frame, capturePng) != GoldenImageStatus::Success ||
                DecodePng(Core::Container::Span<const uint8_t>(capturePng), image) !=
                    GoldenImageStatus::Success ||
                image.Width != ValidationWidth || image.Height != ValidationHeight)
            {
                outFailureReason = TEXT("GTAOの検査のcaptureを読めません");
                return false;
            }
            EnsureGtaoCaptureDirectories();
            if (SavePng(GtaoCornellCapturePath(), Core::Container::Span<const uint8_t>(capturePng)) !=
                GoldenImageStatus::Success)
            {
                outFailureReason = TEXT("GTAOの検査の画像を保存できません");
                return false;
            }

            auto visibilityAt = [&image](uint32_t x, uint32_t y) -> double
            {
                const size_t offset = static_cast<size_t>(y) * image.RowPitchBytes + static_cast<size_t>(x) * 4u;
                return DecodeSrgbByte(image.Pixels[offset + 0u]);
            };
            auto regionMean = [&visibilityAt](uint32_t left, uint32_t top, uint32_t right, uint32_t bottom) -> double
            {
                double sum = 0.0;
                uint32_t count = 0u;
                for (uint32_t y = top; y < bottom; ++y)
                {
                    for (uint32_t x = left; x < right; ++x)
                    {
                        sum += visibilityAt(x, y);
                        ++count;
                    }
                }
                return count > 0u ? sum / static_cast<double>(count) : 0.0;
            };

            // 32×32画素の升ごとの平均（診断用）
            for (uint32_t blockY = 0u; blockY < 8u; ++blockY)
            {
                std::cout << "GTAO_CORNELL_GRID row=" << blockY;
                for (uint32_t blockX = 0u; blockX < 8u; ++blockX)
                {
                    std::cout << ' ' << std::fixed << std::setprecision(3)
                              << regionMean(blockX * 32u, blockY * 32u, blockX * 32u + 32u, blockY * 32u + 32u);
                }
                std::cout << std::endl;
            }
            double imageMinimum = 1.0;
            for (uint32_t y = 0u; y < image.Height; ++y)
            {
                for (uint32_t x = 0u; x < image.Width; ++x)
                {
                    imageMinimum = std::min(imageMinimum, visibilityAt(x, y));
                }
            }
            const double imageMean = regionMean(0u, 0u, image.Width, image.Height);
            std::cout << std::fixed << std::setprecision(4)
                      << "GTAO_CORNELL image_mean=" << imageMean
                      << " image_min=" << imageMinimum << std::endl;

            // 領域は256×256の検証カメラの画像の画素（左, 上, 右, 下）
            struct Region
            {
                const char* Name;
                uint32_t Left;
                uint32_t Top;
                uint32_t Right;
                uint32_t Bottom;
                bool bOpenSurface; // true: 面の中ほど（遮蔽なしを期待）、false: 隅・接地部（遮蔽を期待）
            };
            constexpr Region Regions[] = {
                {"back_wall", 72u, 64u, 184u, 100u, true},
                {"left_wall", 12u, 90u, 44u, 150u, true},
                {"right_wall", 212u, 90u, 244u, 150u, true},
                {"ceiling", 72u, 12u, 184u, 40u, true},
                {"floor_front", 84u, 236u, 116u, 250u, true},
                {"edge_back_left", 52u, 90u, 60u, 150u, false},
                {"edge_back_right", 198u, 90u, 206u, 150u, false},
                {"short_block_contact", 130u, 230u, 186u, 238u, false},
            };
            // 部屋の大きさの面の中ほどは、遮蔽がほとんど無い（旧SSAOはここをほぼ0にしていた）
            constexpr double MinimumOpenSurfaceVisibility = 0.95;
            // 隅・接地部は、面の中ほどよりはっきり暗い
            constexpr double MaximumCreaseVisibility = 0.9;
            bool bPassed = std::isfinite(imageMean);
            for (const Region& region : Regions)
            {
                const double mean = regionMean(region.Left, region.Top, region.Right, region.Bottom);
                const bool bRegionPassed = std::isfinite(mean) &&
                                           (region.bOpenSurface ? mean >= MinimumOpenSurfaceVisibility
                                                                : mean <= MaximumCreaseVisibility);
                std::cout << std::fixed << std::setprecision(4)
                          << "GTAO_CORNELL_REGION name=" << region.Name
                          << " kind=" << (region.bOpenSurface ? "open" : "crease")
                          << " mean=" << mean
                          << " limit=" << (region.bOpenSurface ? MinimumOpenSurfaceVisibility
                                                                : MaximumCreaseVisibility)
                          << " passed=" << (bRegionPassed ? 1 : 0) << std::endl;
                bPassed = bPassed && bRegionPassed;
            }
            std::cout << "GTAO_CORNELL_ROOM_SCALE=" << (bPassed ? "PASS" : "FAIL") << std::endl;
            if (!bPassed)
            {
                outFailureReason = TEXT("Cornellの部屋でGTAOの面の中ほどが暗いか、隅・接地部に遮蔽がありません");
                return false;
            }
            return true;
        }

    private:
        bool m_bCanopyScenario = false;
        bool m_bCanopyStageFinished = false;
        bool m_bCanopyDone = false;
        bool m_bCanopyHasFirstFrame = false;
        bool m_bCanopyHasPreviousFrame = false;
        uint32_t m_CanopyStage = 0u;
        uint64_t m_CanopyFirstFrame = 0u;
        uint64_t m_CanopyPreviousFrame = 0u;
        double m_CanopyDisabledUnder = 0.0;
        double m_CanopyDisabledSphereUnder = 0.0;
        bool m_bGtaoCornellScenario = false;
        bool m_bWriteBaselineStaging = false;
        bool m_bMeasureVisual = false;
        bool m_bR3DensityScenario = false;
        bool m_bWriteR3DensityBaselineStaging = false;
        bool m_bR3DensityStageApplyFailed = false;
        uint32_t m_R3DensityStage = 0u;
        uint32_t m_R3DensityAcceptedCaseCount = 0u;
        bool m_bR3DensityHasFrameNumber = false;
        uint64_t m_R3DensityLastFrameNumber = 0u;
    };

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<GoldenHandler>();
    }
}

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip("RenderingGoldenImageTest", "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip("RenderingGoldenImageTest", "no Vulkan device is available");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("Rendering Golden Image Validation");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = true;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("RenderingValidation.log");
    config.CreateHandler = &CreateHandler;
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
