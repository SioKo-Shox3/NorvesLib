// RT可視性とラスタハードシャドウを同じ検証配置で比較する。
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "Engine/Engine.h"
#include "Rendering/RenderWorld.h"
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
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "RenderingRayTracingShadowVulkanTest";
    constexpr uint32_t RasterHardShadowMode = 246u;
    constexpr uint32_t RayTracingHardShadowMode = 247u;
    constexpr uint32_t RayTracingVisibilityMode = 248u;
    constexpr uint32_t RasterFallbackMode = 249u;
    constexpr double RayTracingMeanErrorLimitLsb = 4.0;
    constexpr double RayTracingOutlierFractionLimit = 0.025;
    constexpr double RasterFallbackMeanErrorLimitLsb = 0.05;
    constexpr float DynamicOccluderPositionX = 6.0f;
    constexpr uint32_t DynamicShadowRegionLeft = 50u;
    constexpr uint32_t DynamicShadowRegionRight = 74u;
    constexpr uint32_t DynamicLitRegionLeft = 196u;
    constexpr uint32_t DynamicLitRegionRight = 220u;

    enum class CaptureStage : uint8_t
    {
        RasterHardShadow,
        RayTracingHardShadow,
        RayTracingVisibility,
        RasterFallback,
        DynamicInitialVisibility,
        DynamicMovedVisibility,
        DynamicMovedRasterReference,
        DynamicMovedRayTracingHardShadow,
        DynamicRasterFallback,
        Complete
    };

    struct ImageDifference
    {
        double MeanErrorLsb = 0.0;
        double FractionOverTwelveLsb = 0.0;
        uint32_t MaximumErrorLsb = 0u;
    };

    bool IsRgba8Frame(const CapturedFrame& frame)
    {
        const bool bRgba8 = frame.Format == RHI::Format::R8G8B8A8_UNORM ||
                            frame.Format == RHI::Format::R8G8B8A8_SRGB ||
                            frame.Format == RHI::Format::B8G8R8A8_UNORM ||
                            frame.Format == RHI::Format::B8G8R8A8_SRGB;
        return frame.IsSuccess() && frame.Width == ValidationWidth &&
               frame.Height == ValidationHeight && frame.BytesPerPixel == 4u && bRgba8 &&
               frame.RowPitchBytes >= frame.Width * frame.BytesPerPixel &&
               frame.Pixels.size() >=
                   static_cast<size_t>(frame.RowPitchBytes) * frame.Height;
    }

    double MeanLuma(const uint8_t* pixels,
                    uint32_t rowPitch,
                    uint32_t bytesPerPixel,
                    uint32_t left,
                    uint32_t top,
                    uint32_t right,
                    uint32_t bottom)
    {
        uint64_t total = 0u;
        uint64_t count = 0u;
        for (uint32_t y = top; y < bottom; ++y)
        {
            for (uint32_t x = left; x < right; ++x)
            {
                const size_t offset = static_cast<size_t>(y) * rowPitch +
                                      static_cast<size_t>(x) * bytesPerPixel;
                total += pixels[offset] + pixels[offset + 1u] + pixels[offset + 2u];
                count += 3u;
            }
        }
        return count > 0u ? static_cast<double>(total) / static_cast<double>(count) : 0.0;
    }

    bool ValidateShadowContrastAt(const CapturedFrame& frame,
                                  uint32_t shadowLeft,
                                  uint32_t shadowRight,
                                  uint32_t litLeft,
                                  uint32_t litRight,
                                  Core::Container::String& outFailure)
    {
        const double shadowLuma = MeanLuma(frame.Pixels.data(),
                                           frame.RowPitchBytes,
                                           frame.BytesPerPixel,
                                           shadowLeft,
                                           116u,
                                           shadowRight,
                                           140u);
        const double litLuma = MeanLuma(frame.Pixels.data(),
                                        frame.RowPitchBytes,
                                        frame.BytesPerPixel,
                                        litLeft,
                                        116u,
                                        litRight,
                                        140u);
        if (shadowLuma + 25.0 >= litLuma || litLuma < 100.0)
        {
            outFailure = TEXT("ハードシャドウの暗部または照明領域を検証配置で確認できません");
            std::cerr << "R5_SHADOW_CONTRAST_FAILURE shadow_luma=" << shadowLuma
                      << " lit_luma=" << litLuma << '\n';
            return false;
        }
        std::cout << "R5_SHADOW_CONTRAST shadow_luma=" << shadowLuma
                  << " lit_luma=" << litLuma << '\n';
        return true;
    }

    bool ValidateShadowContrast(const CapturedFrame& frame,
                                Core::Container::String& outFailure)
    {
        return ValidateShadowContrastAt(frame, 116u, 140u, 36u, 60u, outFailure);
    }

    ImageDifference CompareImages(const CapturedFrame& frame,
                                  const Core::Container::VariableArray<uint8_t>& reference,
                                  uint32_t referenceRowPitch)
    {
        ImageDifference difference;
        uint64_t totalError = 0u;
        uint64_t channelCount = 0u;
        uint64_t outlierCount = 0u;
        const uint32_t channelCountPerPixel = 3u;
        for (uint32_t y = 0u; y < frame.Height; ++y)
        {
            for (uint32_t x = 0u; x < frame.Width; ++x)
            {
                const size_t currentOffset = static_cast<size_t>(y) * frame.RowPitchBytes +
                                             static_cast<size_t>(x) * frame.BytesPerPixel;
                const size_t referenceOffset = static_cast<size_t>(y) * referenceRowPitch +
                                               static_cast<size_t>(x) * frame.BytesPerPixel;
                for (uint32_t channel = 0u; channel < channelCountPerPixel; ++channel)
                {
                    const uint32_t current = frame.Pixels[currentOffset + channel];
                    const uint32_t baseline = reference[referenceOffset + channel];
                    const uint32_t error = current > baseline ? current - baseline : baseline - current;
                    totalError += error;
                    ++channelCount;
                    outlierCount += error > 12u ? 1u : 0u;
                    difference.MaximumErrorLsb =
                        std::max(difference.MaximumErrorLsb, error);
                }
            }
        }
        difference.MeanErrorLsb = channelCount > 0u
                                      ? static_cast<double>(totalError) /
                                            static_cast<double>(channelCount)
                                      : 0.0;
        difference.FractionOverTwelveLsb = channelCount > 0u
                                               ? static_cast<double>(outlierCount) /
                                                     static_cast<double>(channelCount)
                                               : 0.0;
        return difference;
    }

    class RayTracingShadowHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnPreInitialize(
            const Core::Container::VariableArray<Core::Container::String>& args) override
        {
            m_bScenarioParsed = false;
            m_Stage = CaptureStage::RasterHardShadow;
            m_bDynamicScenario = false;
            m_bDynamicFixtureStateValid = true;
            m_bHasPreviousFrame = false;
            m_DynamicInitialFrameNumber = 0u;
            return RenderingValidationApplicationHandler::OnPreInitialize(args) &&
                   m_bScenarioParsed &&
                   GetRunConfig().CaptureSource == FrameCaptureSourceKind::BackBuffer;
        }

        bool OnInitialize() override
        {
            return RenderingValidationApplicationHandler::OnInitialize() &&
                   GetFixture().ApplyR5RayTracingShadowFixture();
        }

    protected:
        bool ParseAdditionalArgument(const Core::Container::String& argument,
                                     Core::Container::String& outFailureReason) override
        {
            const bool bRasterRayTracingScenario =
                argument == TEXT("--scenario=raster-rt-shadow-ab");
            const bool bDynamicOccluderScenario =
                argument == TEXT("--scenario=dynamic-occluder-fallback");
            if (bRasterRayTracingScenario || bDynamicOccluderScenario)
            {
                if (m_bScenarioParsed)
                {
                    outFailureReason = TEXT("R5影シナリオが重複しています");
                    return false;
                }
                m_bScenarioParsed = true;
                m_bDynamicScenario = bDynamicOccluderScenario;
                if (m_bDynamicScenario)
                {
                    m_Stage = CaptureStage::DynamicInitialVisibility;
                }
                return true;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(
                argument, outFailureReason);
        }

        void ApplyCaptureStageState(Core::Rendering::RenderWorld& renderWorld) override
        {
            renderWorld.SetMainCamera(GetFixture().GetR5RayTracingShadowCamera());
            renderWorld.SetDebugViewModeAll(
                static_cast<Core::Rendering::DebugViewMode>(GetDebugViewMode()));
            if (m_bDynamicScenario)
            {
                const bool bOccluderMoved =
                    m_Stage == CaptureStage::DynamicMovedVisibility ||
                    m_Stage == CaptureStage::DynamicMovedRasterReference ||
                    m_Stage == CaptureStage::DynamicMovedRayTracingHardShadow ||
                    m_Stage == CaptureStage::DynamicRasterFallback;
                m_bDynamicFixtureStateValid =
                    GetFixture().SetR5RayTracingShadowOccluderPositionX(
                        bOccluderMoved ? DynamicOccluderPositionX : 0.0f);
            }
        }

        bool EvaluateCapturedFrame(const CapturedFrame& frame,
                                   Core::Container::String& outFailureReason) override
        {
            if (m_bDynamicScenario && !m_bDynamicFixtureStateValid)
            {
                outFailureReason = TEXT("動的R5影fixtureの遮蔽物位置を更新できません");
                return false;
            }
            if (!IsRgba8Frame(frame))
            {
                outFailureReason = TEXT("バックバッファcaptureが256x256のRGBA8画像ではありません");
                return false;
            }
            if (m_bHasPreviousFrame && frame.FrameNumber <= m_PreviousFrameNumber)
            {
                outFailureReason = TEXT("R5影captureのフレーム番号が増加していません");
                return false;
            }
            m_PreviousFrameNumber = frame.FrameNumber;
            m_bHasPreviousFrame = true;

            switch (m_Stage)
            {
            case CaptureStage::RasterHardShadow:
                if (!ValidateShadowContrast(frame, outFailureReason))
                {
                    return false;
                }
                SaveRasterReference(frame);
                std::cout << "R5_RASTER_HARD_SHADOW=PASS frame=" << frame.FrameNumber << '\n';
                return true;

            case CaptureStage::RayTracingHardShadow:
            {
                if (!ValidateShadowContrast(frame, outFailureReason))
                {
                    return false;
                }
                const ImageDifference difference = CompareImages(
                    frame, m_RasterReference, m_RasterReferenceRowPitch);
                std::cout << "R5_RASTER_RT_AB mean_lsb=" << difference.MeanErrorLsb
                          << " outlier_fraction=" << difference.FractionOverTwelveLsb
                          << " max_lsb=" << difference.MaximumErrorLsb << '\n';
                if (difference.MeanErrorLsb > RayTracingMeanErrorLimitLsb ||
                    difference.FractionOverTwelveLsb > RayTracingOutlierFractionLimit)
                {
                    outFailureReason = TEXT("RTハードシャドウとラスタハードシャドウの差がR5閾値を超えています");
                    return false;
                }
                std::cout << "R5_RASTER_RT_AB=PASS mean_limit_lsb="
                          << RayTracingMeanErrorLimitLsb
                          << " outlier_limit=" << RayTracingOutlierFractionLimit << '\n';
                return true;
            }

            case CaptureStage::RayTracingVisibility:
            {
                const double shadowVisibility = MeanLuma(frame.Pixels.data(),
                                                         frame.RowPitchBytes,
                                                         frame.BytesPerPixel,
                                                         116u,
                                                         116u,
                                                         140u,
                                                         140u);
                const double litVisibility = MeanLuma(frame.Pixels.data(),
                                                      frame.RowPitchBytes,
                                                      frame.BytesPerPixel,
                                                      36u,
                                                      116u,
                                                      60u,
                                                      140u);
                std::cout << "R5_RT_VISIBILITY shadow_luma=" << shadowVisibility
                          << " lit_luma=" << litVisibility << '\n';
                if (shadowVisibility > 40.0 || litVisibility < 170.0)
                {
                    outFailureReason = TEXT("RT可視性に遮蔽画素と非遮蔽画素の両方がありません");
                    return false;
                }
                std::cout << "R5_RT_VISIBILITY=PASS\n";
                return true;
            }

            case CaptureStage::RasterFallback:
            {
                if (!ValidateShadowContrast(frame, outFailureReason))
                {
                    return false;
                }
                const ImageDifference difference = CompareImages(
                    frame, m_RasterReference, m_RasterReferenceRowPitch);
                std::cout << "R5_RT_DISABLED_FALLBACK mean_lsb=" << difference.MeanErrorLsb
                          << " outlier_fraction=" << difference.FractionOverTwelveLsb
                          << " max_lsb=" << difference.MaximumErrorLsb << '\n';
                if (difference.MeanErrorLsb > RasterFallbackMeanErrorLimitLsb ||
                    difference.MaximumErrorLsb > 1u)
                {
                    outFailureReason = TEXT("RT無効時にラスタハードシャドウが維持されません");
                    return false;
                }
                std::cout << "R5_RT_DISABLED_FALLBACK=PASS\n";
                return true;
            }

            case CaptureStage::DynamicInitialVisibility:
            {
                const double shadowLuma = MeanLuma(frame.Pixels.data(),
                                                   frame.RowPitchBytes,
                                                   frame.BytesPerPixel,
                                                   116u,
                                                   116u,
                                                   140u,
                                                   140u);
                const double litLuma = MeanLuma(frame.Pixels.data(),
                                                frame.RowPitchBytes,
                                                frame.BytesPerPixel,
                                                36u,
                                                116u,
                                                60u,
                                                140u);
                std::cout << "R5_DYNAMIC_INITIAL frame=" << frame.FrameNumber
                          << " shadow_luma=" << shadowLuma
                          << " lit_luma=" << litLuma << '\n';
                if (shadowLuma > 40.0 || litLuma < 170.0)
                {
                    outFailureReason = TEXT("初期位置のRT影が解析範囲にありません");
                    return false;
                }
                m_DynamicInitialFrameNumber = frame.FrameNumber;
                std::cout << "R5_DYNAMIC_INITIAL=PASS\n";
                return true;
            }

            case CaptureStage::DynamicMovedVisibility:
            {
                const double previousShadowLuma = MeanLuma(frame.Pixels.data(),
                                                           frame.RowPitchBytes,
                                                           frame.BytesPerPixel,
                                                           116u,
                                                           116u,
                                                           140u,
                                                           140u);
                const double movedShadowLuma = MeanLuma(frame.Pixels.data(),
                                                        frame.RowPitchBytes,
                                                        frame.BytesPerPixel,
                                                        DynamicShadowRegionLeft,
                                                        116u,
                                                        DynamicShadowRegionRight,
                                                        140u);
                const double litLuma = MeanLuma(frame.Pixels.data(),
                                                frame.RowPitchBytes,
                                                frame.BytesPerPixel,
                                                DynamicLitRegionLeft,
                                                116u,
                                                DynamicLitRegionRight,
                                                140u);
                std::cout << "R5_DYNAMIC_TLAS_UPDATE frame=" << frame.FrameNumber
                          << " initial_frame=" << m_DynamicInitialFrameNumber
                          << " previous_luma=" << previousShadowLuma
                          << " moved_luma=" << movedShadowLuma
                          << " lit_luma=" << litLuma << '\n';
                if (frame.FrameNumber <= m_DynamicInitialFrameNumber ||
                    previousShadowLuma < 170.0 || movedShadowLuma > 40.0 ||
                    litLuma < 170.0)
                {
                    outFailureReason = TEXT("TLAS更新後のRT影が移動先へ反映されていません");
                    return false;
                }
                std::cout << "R5_DYNAMIC_TLAS_UPDATE=PASS\n";
                return true;
            }

            case CaptureStage::DynamicMovedRasterReference:
                if (!ValidateShadowContrastAt(frame,
                                              DynamicShadowRegionLeft,
                                              DynamicShadowRegionRight,
                                              DynamicLitRegionLeft,
                                              DynamicLitRegionRight,
                                              outFailureReason))
                {
                    return false;
                }
                SaveRasterReference(frame);
                std::cout << "R5_DYNAMIC_RASTER_REFERENCE=PASS frame="
                          << frame.FrameNumber << '\n';
                return true;

            case CaptureStage::DynamicMovedRayTracingHardShadow:
            {
                if (!ValidateShadowContrastAt(frame,
                                              DynamicShadowRegionLeft,
                                              DynamicShadowRegionRight,
                                              DynamicLitRegionLeft,
                                              DynamicLitRegionRight,
                                              outFailureReason))
                {
                    return false;
                }
                const ImageDifference difference = CompareImages(
                    frame, m_RasterReference, m_RasterReferenceRowPitch);
                std::cout << "R5_DYNAMIC_RASTER_RT_AB mean_lsb="
                          << difference.MeanErrorLsb
                          << " outlier_fraction=" << difference.FractionOverTwelveLsb
                          << " max_lsb=" << difference.MaximumErrorLsb << '\n';
                if (difference.MeanErrorLsb > RayTracingMeanErrorLimitLsb ||
                    difference.FractionOverTwelveLsb > RayTracingOutlierFractionLimit)
                {
                    outFailureReason = TEXT("移動後のRT影とラスタ影がR5閾値を超えて異なります");
                    return false;
                }
                std::cout << "R5_DYNAMIC_RASTER_RT_AB=PASS\n";
                return true;
            }

            case CaptureStage::DynamicRasterFallback:
            {
                if (!ValidateShadowContrastAt(frame,
                                              DynamicShadowRegionLeft,
                                              DynamicShadowRegionRight,
                                              DynamicLitRegionLeft,
                                              DynamicLitRegionRight,
                                              outFailureReason))
                {
                    return false;
                }
                const ImageDifference difference = CompareImages(
                    frame, m_RasterReference, m_RasterReferenceRowPitch);
                std::cout << "R5_DYNAMIC_RT_DISABLED_FALLBACK mean_lsb="
                          << difference.MeanErrorLsb
                          << " outlier_fraction=" << difference.FractionOverTwelveLsb
                          << " max_lsb=" << difference.MaximumErrorLsb << '\n';
                if (difference.MeanErrorLsb > RasterFallbackMeanErrorLimitLsb ||
                    difference.MaximumErrorLsb > 1u)
                {
                    outFailureReason = TEXT("移動後にRTを無効化するとラスタ影へ戻りません");
                    return false;
                }
                std::cout << "R5_DYNAMIC_RT_DISABLED_FALLBACK=PASS\n";
                return true;
            }

            case CaptureStage::Complete:
                break;
            }
            outFailureReason = TEXT("R5影captureが想定外の段階です");
            return false;
        }

        bool RequestFollowupCapture(const CapturedFrame&,
                                    Core::Rendering::FrameCaptureRequest& outRequest) override
        {
            if (m_Stage == CaptureStage::Complete)
            {
                return false;
            }
            outRequest.SourceKind = FrameCaptureSourceKind::BackBuffer;
            return true;
        }

        void AdvanceCaptureStage() override
        {
            if (m_bDynamicScenario)
            {
                switch (m_Stage)
                {
                case CaptureStage::DynamicInitialVisibility:
                    m_Stage = CaptureStage::DynamicMovedVisibility;
                    break;
                case CaptureStage::DynamicMovedVisibility:
                    m_Stage = CaptureStage::DynamicMovedRasterReference;
                    break;
                case CaptureStage::DynamicMovedRasterReference:
                    m_Stage = CaptureStage::DynamicMovedRayTracingHardShadow;
                    break;
                case CaptureStage::DynamicMovedRayTracingHardShadow:
                    m_Stage = CaptureStage::DynamicRasterFallback;
                    break;
                case CaptureStage::DynamicRasterFallback:
                    m_Stage = CaptureStage::Complete;
                    break;
                default:
                    m_Stage = CaptureStage::Complete;
                    break;
                }
                return;
            }

            switch (m_Stage)
            {
            case CaptureStage::RasterHardShadow:
                m_Stage = CaptureStage::RayTracingHardShadow;
                break;
            case CaptureStage::RayTracingHardShadow:
                m_Stage = CaptureStage::RayTracingVisibility;
                break;
            case CaptureStage::RayTracingVisibility:
                m_Stage = CaptureStage::RasterFallback;
                break;
            case CaptureStage::RasterFallback:
            case CaptureStage::DynamicInitialVisibility:
            case CaptureStage::DynamicMovedVisibility:
            case CaptureStage::DynamicMovedRasterReference:
            case CaptureStage::DynamicMovedRayTracingHardShadow:
            case CaptureStage::DynamicRasterFallback:
            case CaptureStage::Complete:
                m_Stage = CaptureStage::Complete;
                break;
            }
        }

    private:
        uint32_t GetDebugViewMode() const
        {
            switch (m_Stage)
            {
            case CaptureStage::RasterHardShadow:
                return RasterHardShadowMode;
            case CaptureStage::RayTracingHardShadow:
                return RayTracingHardShadowMode;
            case CaptureStage::RayTracingVisibility:
                return RayTracingVisibilityMode;
            case CaptureStage::RasterFallback:
                return RasterFallbackMode;
            case CaptureStage::DynamicInitialVisibility:
            case CaptureStage::DynamicMovedVisibility:
                return RayTracingVisibilityMode;
            case CaptureStage::DynamicMovedRasterReference:
                return RasterHardShadowMode;
            case CaptureStage::DynamicMovedRayTracingHardShadow:
                return RayTracingHardShadowMode;
            case CaptureStage::DynamicRasterFallback:
                return RasterFallbackMode;
            case CaptureStage::Complete:
                return 0u;
            }
            return 0u;
        }

        void SaveRasterReference(const CapturedFrame& frame)
        {
            m_RasterReference.resize(frame.Pixels.size());
            std::memcpy(m_RasterReference.data(), frame.Pixels.data(), frame.Pixels.size());
            m_RasterReferenceRowPitch = frame.RowPitchBytes;
        }

        bool m_bScenarioParsed = false;
        bool m_bDynamicScenario = false;
        bool m_bDynamicFixtureStateValid = true;
        bool m_bHasPreviousFrame = false;
        uint64_t m_PreviousFrameNumber = 0u;
        uint64_t m_DynamicInitialFrameNumber = 0u;
        CaptureStage m_Stage = CaptureStage::RasterHardShadow;
        Core::Container::VariableArray<uint8_t> m_RasterReference;
        uint32_t m_RasterReferenceRowPitch = 0u;
    };

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<RayTracingShadowHandler>();
    }
}

int main(int argc, char** argv)
{
    if (NorvesLib::Test::RenderingValidation::IsForcedGpuTestSkipRequested())
    {
        return NorvesLib::Test::RenderingValidation::ReportGpuTestSkip(
            TestName, "環境変数によりGPU検証をスキップします");
    }

    NorvesLib::Core::Container::String reason;
    if (!NorvesLib::Test::RenderingValidation::CanCreateVulkanDeviceForGpuTest(reason))
    {
        return NorvesLib::Test::RenderingValidation::ReportGpuTestSkip(
            TestName, "Vulkan デバイスを利用できません");
    }

    NorvesLib::RHI::RHIDeviceDesc deviceDesc;
    deviceDesc.Api = NorvesLib::RHI::GraphicsAPI::Vulkan;
    NorvesLib::RHI::DevicePtr device = NorvesLib::RHI::CreateRHIDevice(deviceDesc);
    if (!device)
    {
        return NorvesLib::Test::RenderingValidation::ReportGpuTestSkip(
            TestName, "Vulkan デバイスを作成できません");
    }
    const NorvesLib::RHI::RayTracingCapabilities capabilities =
        device->GetCapabilities().RayTracing;
    device->WaitIdle();
    device.reset();
    if (!capabilities.bAccelerationStructure || !capabilities.bRayTracingPipeline)
    {
        return NorvesLib::Test::RenderingValidation::ReportGpuTestSkip(
            TestName, "レイトレーシングパイプラインを利用できません");
    }

    bool bSceneSpecified = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strncmp(argv[index], "--scene=", 8u) == 0)
        {
            bSceneSpecified = true;
            break;
        }
    }

    NorvesLib::Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("R5 レイトレーシング影の検証");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = true;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = true;
    config.Api = NorvesLib::RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("RenderingRayTracingShadowVulkan.log");
    config.CreateHandler = &CreateHandler;
    if (!bSceneSpecified)
    {
        config.Arguments.push_back(TEXT("--scene=outdoor"));
    }
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(NorvesLib::Core::Container::String(argv[index]));
    }
    return NorvesLib::Core::Boot::LaunchApplication(config);
}
