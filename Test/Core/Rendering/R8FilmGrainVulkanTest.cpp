// R8のフィルムグレインの契約を実GPUで確かめる。ToneMappingPassを一様な中間グレーのSceneColorへ掛け、
// 強さ0では出力が従来（グレインの設定なし）とbyte一致し、強さを指定するとsRGBの符号化値の平均の変化が
// 0.5/255以内・標準偏差が指定の±10%以内になり、隣のフレームで模様が変わり、同じフレームの再実行でbyte一致すること。
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ToneMappingPass.h"
#include "Rendering/ViewRenderContext.h"
#include "RenderingValidation/GpuTestEnvironment.h"

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
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

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
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "R8FilmGrainVulkanTest";
    constexpr uint32_t Width = 256u;
    constexpr uint32_t Height = 256u;
    constexpr uint32_t BytesPerPixel = 8u;
    // 中間グレー（scene-linear 0.18）。
    constexpr float MidGrey = 0.18f;
    // グレインの強さ（sRGBの符号化値での標準偏差）とseed、比べるフレーム番号。
    constexpr float GrainStrength = 4.0f / 255.0f;
    constexpr uint32_t GrainSeed = 1234u;
    constexpr uint64_t GrainFrame = 7u;
    // 合否の規則（R8-P6の完了条件）。
    constexpr double MaxMeanShift = 0.5 / 255.0;
    constexpr double StdDevTolerance = 0.1;
    // 隣のフレームの模様が独立であること: 雑音の相関係数の絶対値の上限と、値が変わる画素の割合の下限。
    constexpr double MaxAdjacentCorrelation = 0.05;
    constexpr double MinAdjacentChangedFraction = 0.9;

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture()
        {
            Vulkan::BeginVulkanValidationErrorCaptureForTesting();
        }

        ~VulkanValidationErrorCapture()
        {
            Vulkan::EndVulkanValidationErrorCaptureForTesting();
        }

        uint32_t GetHitCount() const
        {
            return Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting();
        }
    };

    struct GrainRun
    {
        bool bApplyGrainSettings = false;
        float Strength = 0.0f;
        uint32_t Seed = 0u;
        uint64_t FrameNumber = 0u;
    };

    float HalfToFloat(uint16_t value)
    {
        const uint32_t exponent = (value >> 10u) & 0x1Fu;
        const uint32_t mantissa = value & 0x3FFu;
        const float sign = (value & 0x8000u) != 0u ? -1.0f : 1.0f;
        if (exponent == 0u)
        {
            return sign * std::ldexp(static_cast<float>(mantissa), -24);
        }
        if (exponent == 31u)
        {
            return mantissa == 0u ? sign * std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::quiet_NaN();
        }
        return sign * std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    }

    uint16_t FloatToHalf(float value)
    {
        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t sign = (bits >> 16u) & 0x8000u;
        const int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xFFu) - 127 + 15;
        const uint32_t mantissa = bits & 0x7FFFFFu;
        if (exponent <= 0 || exponent >= 31)
        {
            return static_cast<uint16_t>(sign);
        }
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10u) | (mantissa >> 13u));
    }

    // 提示のsRGB形式と同じく [0,1] へ飽和させてから区分的sRGBで符号化する。
    double EncodeSrgbClamped(double linear)
    {
        const double clamped = std::clamp(linear, 0.0, 1.0);
        return clamped <= 0.0031308 ? clamped * 12.92 : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
    }

    bool RecordHostReadBarrier(const TSharedPtr<Vulkan::VulkanCommandList>& commandList,
                               const BufferPtr& readbackBuffer)
    {
        TSharedPtr<Vulkan::VulkanBuffer> vulkanBuffer = DynamicPointerCast<Vulkan::VulkanBuffer>(readbackBuffer);
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
        commandList->GetVkCommandBuffer().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                                          vk::PipelineStageFlagBits::eHost, {}, 0u, nullptr, 1u,
                                                          &barrier, 0u, nullptr);
        return true;
    }

    // ToneMappingPassを1回実行し、ToneMappedColor（RGBA16F）を読み戻す。
    bool RunToneMapping(const DevicePtr& device,
                        ShaderManager& shaderManager,
                        SceneRenderer& renderer,
                        const TexturePtr& sceneColor,
                        const GrainRun& run,
                        VariableArray<uint16_t>& outPixels)
    {
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("SceneColor", sceneColor);

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Capabilities = &device->GetCapabilities();
        context.Renderer = &renderer;
        context.SharedResources = &sharedResources;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.FrameNumber = run.FrameNumber;

        // 周辺減光は既定のSceneViewと同じく無効にする（一様な画像の統計を崩さない）。
        ToneMappingSettings settings;
        settings.VignetteIntensity = 0.0f;
        ToneMappingPass pass(settings);
        if (run.bApplyGrainSettings)
        {
            pass.SetFilmGrain(run.Strength, run.Seed);
        }
        if (!pass.Initialize(context))
        {
            std::cerr << "ToneMappingPassを初期化できませんでした\n";
            return false;
        }
        pass.Setup(context);

        const uint64_t readbackSize = static_cast<uint64_t>(Width) * Height * BytesPerPixel;
        BufferDesc readbackDesc(readbackSize, ResourceUsage::TransferDst, true, "R8FilmGrainVulkanTest.Readback");
        BufferPtr readback = device->CreateBuffer(readbackDesc);
        CommandListPtr commandList = device->CreateCommandList();
        TSharedPtr<Vulkan::VulkanCommandList> vulkanCommandList =
            DynamicPointerCast<Vulkan::VulkanCommandList>(commandList);
        if (!readback || !commandList || !vulkanCommandList)
        {
            std::cerr << "readback用の資源を作成できませんでした\n";
            return false;
        }

        context.CommandList = commandList.get();
        commandList->SetFrameIndex(0u);
        commandList->Begin();
        pass.Execute(context);

        TexturePtr toneMapped = sharedResources.GetTexturePtr("ToneMappedColor");
        if (!toneMapped || toneMapped->GetWidth() != Width || toneMapped->GetHeight() != Height)
        {
            std::cerr << "ToneMappedColorが登録されませんでした\n";
            commandList->End();
            return false;
        }
        commandList->TextureBarrier(toneMapped, ResourceState::ShaderResource, ResourceState::CopySource);
        commandList->BufferBarrier(readback, ResourceState::Undefined, ResourceState::CopyDest, 0u, readbackSize);
        commandList->CopyTextureToBuffer(toneMapped, readback, Width, Height, 0u);
        commandList->TextureBarrier(toneMapped, ResourceState::CopySource, ResourceState::ShaderResource);
        const bool bBarrier = RecordHostReadBarrier(vulkanCommandList, readback);
        commandList->End();
        if (!bBarrier)
        {
            std::cerr << "host読み取りのbarrierを記録できませんでした\n";
            return false;
        }
        commandList->Submit(true);

        const void* mapped = readback->Map(0u, readbackSize);
        if (!mapped)
        {
            std::cerr << "readbackをmapできませんでした\n";
            return false;
        }
        outPixels.resize(static_cast<size_t>(Width) * Height * 4u);
        std::memcpy(outPixels.data(), mapped, static_cast<size_t>(readbackSize));
        readback->Unmap();
        pass.Shutdown();
        return true;
    }

    // 緑の成分のsRGBの符号化値（画素ごと）。
    VariableArray<double> EncodedGreen(const VariableArray<uint16_t>& pixels)
    {
        VariableArray<double> values(pixels.size() / 4u);
        for (size_t pixel = 0u; pixel < values.size(); ++pixel)
        {
            values[pixel] = EncodeSrgbClamped(HalfToFloat(pixels[pixel * 4u + 1u]));
        }
        return values;
    }

    bool BytesEqual(const VariableArray<uint16_t>& first, const VariableArray<uint16_t>& second)
    {
        return first.size() == second.size() &&
               std::memcmp(first.data(), second.data(), first.size() * sizeof(uint16_t)) == 0;
    }

    int RunTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPUテストが環境変数でスキップされました");
        }
        String unavailableReason;
        if (!CanCreateVulkanDeviceForGpuTest(unavailableReason))
        {
            return ReportGpuTestSkip(TestName, unavailableReason.c_str());
        }

        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }
        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << "ShaderManagerを初期化できませんでした\n";
            return 1;
        }
        SceneRenderer renderer;
        if (!renderer.Initialize(device.get(), nullptr))
        {
            std::cerr << "SceneRendererを初期化できませんでした\n";
            return 1;
        }

        TextureDesc sceneColorDesc;
        sceneColorDesc.Width = Width;
        sceneColorDesc.Height = Height;
        sceneColorDesc.TextureFormat = Format::R16G16B16A16_FLOAT;
        sceneColorDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        sceneColorDesc.DebugName = "R8FilmGrainVulkanTest.SceneColor";
        TexturePtr sceneColor = device->CreateTexture(sceneColorDesc);
        if (!sceneColor)
        {
            std::cerr << "SceneColorを作成できませんでした\n";
            return 1;
        }
        const uint16_t grey = FloatToHalf(MidGrey);
        const uint16_t one = FloatToHalf(1.0f);
        VariableArray<uint16_t> input(static_cast<size_t>(Width) * Height * 4u);
        for (size_t pixel = 0u; pixel < static_cast<size_t>(Width) * Height; ++pixel)
        {
            input[pixel * 4u] = grey;
            input[pixel * 4u + 1u] = grey;
            input[pixel * 4u + 2u] = grey;
            input[pixel * 4u + 3u] = one;
        }
        const uint32_t rowPitch = Width * BytesPerPixel;
        sceneColor->Update(input.data(), rowPitch, rowPitch * Height);

        GrainRun baselineRun;
        baselineRun.FrameNumber = GrainFrame;
        GrainRun offRun = baselineRun;
        offRun.bApplyGrainSettings = true;
        offRun.Strength = 0.0f;
        offRun.Seed = GrainSeed;
        GrainRun onRun = offRun;
        onRun.Strength = GrainStrength;
        GrainRun nextFrameRun = onRun;
        nextFrameRun.FrameNumber = GrainFrame + 1u;

        VariableArray<uint16_t> baseline;
        VariableArray<uint16_t> off;
        VariableArray<uint16_t> on;
        VariableArray<uint16_t> onAgain;
        VariableArray<uint16_t> nextFrame;
        if (!RunToneMapping(device, shaderManager, renderer, sceneColor, baselineRun, baseline) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, offRun, off) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, onRun, on) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, onRun, onAgain) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, nextFrameRun, nextFrame))
        {
            return 1;
        }
        renderer.Shutdown();
        shaderManager.Shutdown();

        const bool bOffMatchesBaseline = BytesEqual(baseline, off);
        const bool bSameFrameIdentical = BytesEqual(on, onAgain);

        const VariableArray<double> baseValues = EncodedGreen(baseline);
        const VariableArray<double> onValues = EncodedGreen(on);
        const VariableArray<double> nextValues = EncodedGreen(nextFrame);
        const double count = static_cast<double>(onValues.size());
        double baseMean = 0.0;
        double onMean = 0.0;
        for (size_t index = 0u; index < onValues.size(); ++index)
        {
            baseMean += baseValues[index];
            onMean += onValues[index];
        }
        baseMean /= count;
        onMean /= count;
        double onVariance = 0.0;
        double nextMean = 0.0;
        for (size_t index = 0u; index < onValues.size(); ++index)
        {
            onVariance += (onValues[index] - onMean) * (onValues[index] - onMean);
            nextMean += nextValues[index];
        }
        onVariance /= count;
        nextMean /= count;
        const double onStdDev = std::sqrt(onVariance);
        double covariance = 0.0;
        double nextVariance = 0.0;
        uint32_t changedPixels = 0u;
        for (size_t index = 0u; index < onValues.size(); ++index)
        {
            covariance += (onValues[index] - onMean) * (nextValues[index] - nextMean);
            nextVariance += (nextValues[index] - nextMean) * (nextValues[index] - nextMean);
            changedPixels += on[index * 4u + 1u] != nextFrame[index * 4u + 1u] ? 1u : 0u;
        }
        nextVariance /= count;
        covariance /= count;
        const double correlation =
            onVariance > 0.0 && nextVariance > 0.0 ? covariance / std::sqrt(onVariance * nextVariance) : 1.0;
        const double changedFraction = static_cast<double>(changedPixels) / count;
        const double meanShift = std::abs(onMean - baseMean);
        const double stdDevRatio = onStdDev / GrainStrength;

        std::cout << "r8_film_grain off_matches_baseline=" << (bOffMatchesBaseline ? 1 : 0)
                  << " same_frame_identical=" << (bSameFrameIdentical ? 1 : 0) << '\n';
        std::cout << "r8_film_grain strength=" << GrainStrength * 255.0 << "/255 base_mean=" << baseMean * 255.0
                  << "/255 on_mean=" << onMean * 255.0 << "/255 mean_shift=" << meanShift * 255.0
                  << "/255 stddev=" << onStdDev * 255.0 << "/255 stddev_ratio=" << stdDevRatio << '\n';
        std::cout << "r8_film_grain adjacent_frame correlation=" << correlation
                  << " changed_fraction=" << changedFraction << '\n';

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';

        bool bPassed = true;
        if (!bOffMatchesBaseline)
        {
            std::cerr << "強さ0で出力がグレインの設定なしと一致しません\n";
            bPassed = false;
        }
        if (!bSameFrameIdentical)
        {
            std::cerr << "同じフレームの再実行で出力がbyte一致しません\n";
            bPassed = false;
        }
        if (meanShift > MaxMeanShift)
        {
            std::cerr << "中間グレーの平均が0.5/255を超えて変わりました\n";
            bPassed = false;
        }
        if (std::abs(stdDevRatio - 1.0) > StdDevTolerance)
        {
            std::cerr << "標準偏差が指定の±10%に収まりません\n";
            bPassed = false;
        }
        if (std::abs(correlation) > MaxAdjacentCorrelation || changedFraction < MinAdjacentChangedFraction)
        {
            std::cerr << "隣のフレームで模様が変わっていません\n";
            bPassed = false;
        }
        if (validationErrorCount != 0u)
        {
            std::cerr << "Vulkan validation errorを検出しました: " << validationErrorCount << '\n';
            bPassed = false;
        }
        std::cout << (bPassed ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return bPassed ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "R8FilmGrainVulkanTest threw an exception: " << exception.what() << '\n';
        return 1;
    }
}
