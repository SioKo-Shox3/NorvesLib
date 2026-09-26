// R8の試験チャートをSceneColorとしてToneMappingPassへ入れ、ACES 2.0 SDR LUTの出力をOCIO基準画像と照合する。
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
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
void ArmVulkanTextureCreateFailureForTesting(const char *debugName) noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "R8AcesLutToneMappingVulkanTest";
    constexpr uint32_t BytesPerPixel = 8u;
    constexpr uint32_t ImageHeaderSize = 16u;

    // 合否の閾値。[0,1] へ飽和させた区分的sRGB符号化後の値で、全画素・全成分の最大差をこれ以内にする。
    constexpr double MaxEncodedDifference = 2.0 / 255.0;

    // 3D textureの作成・転送・標本化で検証レイヤが出したエラーを数える。
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

    struct HostImage
    {
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        VariableArray<uint8_t> Pixels;
    };

    struct ComparisonResult
    {
        double MaxDifference = 0.0;
        double MeanDifference = 0.0;
        uint32_t PixelsOverThreshold = 0u;
        uint32_t WorstX = 0u;
        uint32_t WorstY = 0u;
        uint32_t WorstChannel = 0u;
        float WorstGpu = 0.0f;
        float WorstReference = 0.0f;
    };

    String ResolveSourcePath(const char* relativePath)
    {
        String path(NORVES_SOURCE_ROOT);
        path += "/";
        path += relativePath;
        return path;
    }

    bool ReadImage(const char* relativePath,
                   const char* expectedMagic,
                   uint32_t bytesPerPixel,
                   HostImage& outImage)
    {
        const String path = ResolveSourcePath(relativePath);
        std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            std::cerr << "画像を開けません: " << path.c_str() << '\n';
            return false;
        }

        const std::streamoff fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        char header[ImageHeaderSize] = {};
        if (fileSize < static_cast<std::streamoff>(ImageHeaderSize) || !file.read(header, ImageHeaderSize))
        {
            std::cerr << "画像の見出しが短すぎます: " << path.c_str() << '\n';
            return false;
        }
        if (std::memcmp(header, expectedMagic, 8u) != 0)
        {
            std::cerr << "画像の magic が一致しません: " << path.c_str() << '\n';
            return false;
        }

        std::memcpy(&outImage.Width, header + 8, sizeof(uint32_t));
        std::memcpy(&outImage.Height, header + 12, sizeof(uint32_t));
        const uint64_t payloadSize =
            static_cast<uint64_t>(outImage.Width) * outImage.Height * bytesPerPixel;
        if (outImage.Width == 0u || outImage.Height == 0u ||
            static_cast<uint64_t>(fileSize) != ImageHeaderSize + payloadSize)
        {
            std::cerr << "画像の大きさが見出しと一致しません: " << path.c_str() << '\n';
            return false;
        }

        outImage.Pixels.resize(static_cast<size_t>(payloadSize));
        return static_cast<bool>(
            file.read(reinterpret_cast<char*>(outImage.Pixels.data()), static_cast<std::streamsize>(payloadSize)));
    }

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
        return sign * std::ldexp(static_cast<float>(mantissa | 0x400u),
                                 static_cast<int>(exponent) - 25);
    }

    // 提示のsRGB形式と同じく [0,1] へ飽和させてから区分的sRGBで符号化する。
    double EncodeSrgbClamped(double linear)
    {
        const double clamped = std::clamp(linear, 0.0, 1.0);
        return clamped <= 0.0031308 ? clamped * 12.92
                                    : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
    }

    bool RecordHostReadBarrier(const TSharedPtr<Vulkan::VulkanCommandList>& commandList,
                               const BufferPtr& readbackBuffer)
    {
        TSharedPtr<Vulkan::VulkanBuffer> vulkanBuffer =
            DynamicPointerCast<Vulkan::VulkanBuffer>(readbackBuffer);
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
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eHost,
            {},
            0u,
            nullptr,
            1u,
            &barrier,
            0u,
            nullptr);
        return true;
    }

    // ToneMappingPassを1回実行し、ToneMappedColor（RGBA16F）を読み戻す。
    bool RunToneMapping(const DevicePtr& device,
                        ShaderManager& shaderManager,
                        SceneRenderer& renderer,
                        const TexturePtr& sceneColor,
                        ToneMappingOperator toneMapOperator,
                        VariableArray<uint16_t>& outPixels)
    {
        const uint32_t width = sceneColor->GetWidth();
        const uint32_t height = sceneColor->GetHeight();

        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("SceneColor", sceneColor);

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Capabilities = &device->GetCapabilities();
        context.Renderer = &renderer;
        context.SharedResources = &sharedResources;
        context.RenderWidth = width;
        context.RenderHeight = height;
        context.ScreenWidth = width;
        context.ScreenHeight = height;

        // 既定のグレーディング（Contrast 1.05 / Saturation 1.1）は残し、LUT演算子が掛けないことを確かめる。
        // 周辺減光は既定のSceneViewと同じく無効にする。
        ToneMappingSettings settings;
        settings.Operator = toneMapOperator;
        settings.VignetteIntensity = 0.0f;
        ToneMappingPass pass(settings);
        if (!pass.Initialize(context))
        {
            std::cerr << "ToneMappingPassを初期化できませんでした\n";
            return false;
        }
        pass.Setup(context);

        const uint64_t readbackSize = static_cast<uint64_t>(width) * height * BytesPerPixel;
        BufferDesc readbackDesc(readbackSize, ResourceUsage::TransferDst, true,
                                "R8AcesLutToneMappingVulkanTest.Readback");
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
        if (!toneMapped || toneMapped->GetWidth() != width || toneMapped->GetHeight() != height)
        {
            std::cerr << "ToneMappedColorが登録されませんでした\n";
            commandList->End();
            return false;
        }

        commandList->TextureBarrier(toneMapped, ResourceState::ShaderResource, ResourceState::CopySource);
        commandList->BufferBarrier(readback, ResourceState::Undefined, ResourceState::CopyDest, 0u, readbackSize);
        commandList->CopyTextureToBuffer(toneMapped, readback, width, height, 0u);
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
        outPixels.resize(static_cast<size_t>(width) * height * 4u);
        std::memcpy(outPixels.data(), mapped, static_cast<size_t>(readbackSize));
        readback->Unmap();

        pass.Shutdown();
        return true;
    }

    ComparisonResult Compare(const VariableArray<uint16_t>& gpuPixels, const HostImage& reference)
    {
        ComparisonResult result;
        double sum = 0.0;
        const float* referencePixels = reinterpret_cast<const float*>(reference.Pixels.data());
        for (uint32_t y = 0u; y < reference.Height; ++y)
        {
            for (uint32_t x = 0u; x < reference.Width; ++x)
            {
                const size_t base = (static_cast<size_t>(y) * reference.Width + x) * 4u;
                bool bOver = false;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const float gpu = HalfToFloat(gpuPixels[base + channel]);
                    const float expected = referencePixels[base + channel];
                    // NaN は最大差として扱う
                    const double difference =
                        std::isfinite(gpu) ? std::abs(EncodeSrgbClamped(gpu) - EncodeSrgbClamped(expected)) : 1.0;
                    sum += difference;
                    bOver = bOver || difference > MaxEncodedDifference;
                    if (difference > result.MaxDifference)
                    {
                        result.MaxDifference = difference;
                        result.WorstX = x;
                        result.WorstY = y;
                        result.WorstChannel = channel;
                        result.WorstGpu = gpu;
                        result.WorstReference = expected;
                    }
                }
                result.PixelsOverThreshold += bOver ? 1u : 0u;
            }
        }
        result.MeanDifference = sum / (static_cast<double>(reference.Width) * reference.Height * 3.0);
        return result;
    }

    void PrintComparison(const char* label, const ComparisonResult& result)
    {
        std::cout << label
                  << " max=" << result.MaxDifference * 255.0 << "/255"
                  << " mean=" << result.MeanDifference * 255.0 << "/255"
                  << " pixels_over=" << result.PixelsOverThreshold
                  << " worst=(" << result.WorstX << "," << result.WorstY << ") ch=" << result.WorstChannel
                  << " gpu=" << result.WorstGpu << " reference=" << result.WorstReference << '\n';
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

        HostImage chart;
        HostImage reference;
        if (!ReadImage("Test/Core/Rendering/Baselines/RenderingValidation/R8AcesChart.rgba16f",
                       "NRGBA16F", 8u, chart) ||
            !ReadImage("Test/Core/Rendering/Baselines/RenderingValidation/R8AcesReference.rgba32f",
                       "NRGBA32F", 16u, reference))
        {
            return 1;
        }
        if (chart.Width != reference.Width || chart.Height != reference.Height)
        {
            std::cerr << "チャートと基準画像の大きさが一致しません\n";
            return 1;
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
        if (!shaderManager.Initialize(device.get(), ResolveSourcePath("Assets/Shaders")))
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
        sceneColorDesc.Width = chart.Width;
        sceneColorDesc.Height = chart.Height;
        sceneColorDesc.TextureFormat = Format::R16G16B16A16_FLOAT;
        sceneColorDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        sceneColorDesc.DebugName = "R8AcesLutToneMappingVulkanTest.SceneColor";
        TexturePtr sceneColor = device->CreateTexture(sceneColorDesc);
        if (!sceneColor)
        {
            std::cerr << "SceneColorを作成できませんでした\n";
            return 1;
        }
        const uint32_t rowPitch = chart.Width * BytesPerPixel;
        sceneColor->Update(chart.Pixels.data(), rowPitch, rowPitch * chart.Height);

        VariableArray<uint16_t> lutPixels;
        VariableArray<uint16_t> filmicPixels;
        if (!RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::Aces20Lut, lutPixels) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::ACES, filmicPixels))
        {
            return 1;
        }

        // LUTのtexture作成が例外で失敗しても、passは例外を外へ出さずACES Filmicへ退避して描き続ける。
        VariableArray<uint16_t> failedLutPixels;
        Vulkan::ArmVulkanTextureCreateFailureForTesting("Aces20SdrRec709Lut");
        const bool bFailedLutRun =
            RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::Aces20Lut, failedLutPixels);
        Vulkan::ArmVulkanTextureCreateFailureForTesting(nullptr);
        const bool bFallbackMatchesFilmic =
            bFailedLutRun && failedLutPixels.size() == filmicPixels.size() &&
            std::memcmp(failedLutPixels.data(), filmicPixels.data(), filmicPixels.size() * sizeof(uint16_t)) == 0;
        std::cout << "lut_create_failure_fallback_matches_filmic=" << (bFallbackMatchesFilmic ? 1 : 0) << '\n';

        const ComparisonResult lutResult = Compare(lutPixels, reference);
        const ComparisonResult filmicResult = Compare(filmicPixels, reference);
        std::cout << "threshold=" << MaxEncodedDifference * 255.0 << "/255 pixels="
                  << chart.Width * chart.Height << '\n';
        PrintComparison("aces20_lut", lutResult);
        PrintComparison("aces_filmic_control", filmicResult);

        renderer.Shutdown();
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';

        bool bPassed = true;
        if (!bFallbackMatchesFilmic)
        {
            std::cerr << "LUT作成の失敗時にACES Filmicへ退避した出力になっていません\n";
            bPassed = false;
        }
        if (validationErrorCount != 0u)
        {
            std::cerr << "Vulkan validation errorを検出しました: " << validationErrorCount << '\n';
            bPassed = false;
        }
        if (lutResult.MaxDifference > MaxEncodedDifference)
        {
            std::cerr << "ACES 2.0 SDR LUTの出力がOCIO基準画像から2/255を超えて離れています\n";
            bPassed = false;
        }
        // 比較が空振りしていないことを、別の演算子が同じ閾値で不合格になることで確かめる。
        if (filmicResult.MaxDifference <= MaxEncodedDifference)
        {
            std::cerr << "ACES Filmicも閾値内に収まり、比較が演算子を区別できていません\n";
            bPassed = false;
        }
        std::cout << (bPassed ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return bPassed ? 0 : 1;
    }
}

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "R8AcesLutToneMappingVulkanTest threw an exception: " << exception.what() << '\n';
        return 1;
    }
}
