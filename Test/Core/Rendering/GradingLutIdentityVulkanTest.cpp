// R8の試験チャートをSceneColorとしてToneMappingPassへ入れ、カメラの見た目の3D LUTを確かめる。
// 恒等のLUTを掛けても出力がビット単位で変わらないこと、見た目のLUTの出力がLUTファイルをCPUで三線形補間した値と一致すること、
// ACES 2.0 SDR LUT の演算子と読めないLUTでは掛からないことを実GPUで確かめる。
#include "Rendering/SceneProxy.h"
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
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "GradingLutIdentityVulkanTest";
    constexpr uint32_t BytesPerPixel = 8u;
    constexpr uint32_t ImageHeaderSize = 16u;
    constexpr uint32_t LutHeaderSize = 32u;

    // Scripts/BakeLookLut.py が焼くLUT（`Assets/` 相対）
    constexpr const char* IdentityLutAssetPath = "Textures/LookLuts/Identity.lut3d";
    constexpr const char* WarmFilmLutAssetPath = "Textures/LookLuts/WarmFilm.lut3d";
    constexpr const char* MissingLutAssetPath = "Textures/LookLuts/DoesNotExist.lut3d";

    // 合否の閾値。いずれも [0,1] へ飽和させた区分的sRGB符号化後の値で、全画素・全成分の最大差。
    // 恒等のLUTは閾値を持たず、読み戻したRGBA16Fの全画素がLUTなしとビット単位で一致することを求める。
    // 見た目のLUT: CPUの三線形補間との差（GPUの補間の重みの精度と半精度の出力の丸めの分）。
    constexpr double MaxLookEncodedDifference = 1.0 / 255.0;
    // 見た目のLUTが実際に掛かったこと: LUTなしとの最大差がこれ以上。
    constexpr double MinLookEffectEncodedDifference = 3.0 / 255.0;

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

    struct HostLut
    {
        uint32_t Size = 0u;
        VariableArray<uint16_t> Texels; // RGBA half（RGB は格子点の座標からの符号化値の差分）、R が最も速く変わる順
    };

    struct ComparisonResult
    {
        double MaxDifference = 0.0;
        double MeanDifference = 0.0;
        uint32_t Encoded8BitMismatches = 0u;
        uint32_t WorstX = 0u;
        uint32_t WorstY = 0u;
        uint32_t WorstChannel = 0u;
        float WorstActual = 0.0f;
        float WorstExpected = 0.0f;
    };

    String ResolveSourcePath(const char* relativePath)
    {
        String path(NORVES_SOURCE_ROOT);
        path += "/";
        path += relativePath;
        return path;
    }

    bool ReadFileBytes(const String& path, VariableArray<uint8_t>& outBytes)
    {
        std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            std::cerr << "ファイルを開けません: " << path.c_str() << '\n';
            return false;
        }
        const std::streamoff fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        outBytes.resize(static_cast<size_t>(fileSize));
        return static_cast<bool>(file.read(reinterpret_cast<char*>(outBytes.data()), fileSize));
    }

    bool ReadChart(HostImage& outImage)
    {
        VariableArray<uint8_t> bytes;
        if (!ReadFileBytes(ResolveSourcePath("Test/Core/Rendering/Baselines/RenderingValidation/R8AcesChart.rgba16f"),
                           bytes))
        {
            return false;
        }
        if (bytes.size() < ImageHeaderSize || std::memcmp(bytes.data(), "NRGBA16F", 8u) != 0)
        {
            std::cerr << "チャートの見出しが一致しません\n";
            return false;
        }
        std::memcpy(&outImage.Width, bytes.data() + 8, sizeof(uint32_t));
        std::memcpy(&outImage.Height, bytes.data() + 12, sizeof(uint32_t));
        const uint64_t payloadSize = static_cast<uint64_t>(outImage.Width) * outImage.Height * BytesPerPixel;
        if (outImage.Width == 0u || outImage.Height == 0u || bytes.size() != ImageHeaderSize + payloadSize)
        {
            std::cerr << "チャートの大きさが見出しと一致しません\n";
            return false;
        }
        outImage.Pixels.assign(bytes.begin() + ImageHeaderSize, bytes.end());
        return true;
    }

    // ToneMappingPass が読むのと同じファイルを読み、見出しを確かめる。
    bool ReadLookLut(const char* assetPath, HostLut& outLut)
    {
        String relativePath("Assets/");
        relativePath += assetPath;
        VariableArray<uint8_t> bytes;
        if (!ReadFileBytes(ResolveSourcePath(relativePath.c_str()), bytes))
        {
            return false;
        }
        uint32_t size = 0u;
        uint32_t channels = 0u;
        uint32_t format = 0u;
        if (bytes.size() >= LutHeaderSize)
        {
            std::memcpy(&size, bytes.data() + 8, sizeof(uint32_t));
            std::memcpy(&channels, bytes.data() + 12, sizeof(uint32_t));
            std::memcpy(&format, bytes.data() + 16, sizeof(uint32_t));
        }
        const uint64_t payloadSize = static_cast<uint64_t>(size) * size * size * BytesPerPixel;
        if (bytes.size() < LutHeaderSize || std::memcmp(bytes.data(), "NLUTLK02", 8u) != 0 || size != 32u ||
            channels != 4u || format != 1u || bytes.size() != LutHeaderSize + payloadSize)
        {
            std::cerr << "LUTの見出しが一致しません: " << assetPath << '\n';
            return false;
        }
        outLut.Size = size;
        outLut.Texels.resize(static_cast<size_t>(size) * size * size * 4u);
        std::memcpy(outLut.Texels.data(), bytes.data() + LutHeaderSize, static_cast<size_t>(payloadSize));
        return true;
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
        return sign * std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    }

    double EncodeSrgbClamped(double linear)
    {
        const double clamped = std::clamp(linear, 0.0, 1.0);
        return clamped <= 0.0031308 ? clamped * 12.92 : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
    }

    double DecodeSrgbClamped(double encoded)
    {
        const double clamped = std::clamp(encoded, 0.0, 1.0);
        return clamped <= 0.04045 ? clamped / 12.92 : std::pow((clamped + 0.055) / 1.055, 2.4);
    }

    // tonemapping.frag の ApplyLookLut（強さ1）をCPUで行う: 符号化値の座標で格子の差分を三線形補間して符号化値へ足し、
    // リニアへ戻したものと元の符号化値をリニアへ戻したものとの差を入力へ足す。
    void ApplyLookLutOnCpu(const HostLut& lut, const double displayLinear[3], double outLinear[3])
    {
        const uint32_t size = lut.Size;
        double inputEncoded[3];
        double position[3];
        uint32_t base[3];
        double fraction[3];
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            inputEncoded[axis] = EncodeSrgbClamped(displayLinear[axis]);
            position[axis] = inputEncoded[axis] * static_cast<double>(size - 1u);
            base[axis] = std::min(static_cast<uint32_t>(std::floor(position[axis])), size - 2u);
            fraction[axis] = position[axis] - static_cast<double>(base[axis]);
        }
        double offset[3] = {0.0, 0.0, 0.0};
        for (uint32_t corner = 0u; corner < 8u; ++corner)
        {
            const uint32_t dx = corner & 1u;
            const uint32_t dy = (corner >> 1u) & 1u;
            const uint32_t dz = (corner >> 2u) & 1u;
            const double weight = (dx != 0u ? fraction[0] : 1.0 - fraction[0]) *
                                  (dy != 0u ? fraction[1] : 1.0 - fraction[1]) *
                                  (dz != 0u ? fraction[2] : 1.0 - fraction[2]);
            const size_t index =
                ((static_cast<size_t>(base[2] + dz) * size + (base[1] + dy)) * size + (base[0] + dx)) * 4u;
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                offset[channel] += weight * HalfToFloat(lut.Texels[index + channel]);
            }
        }
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            outLinear[channel] = displayLinear[channel] + DecodeSrgbClamped(inputEncoded[channel] + offset[channel]) -
                                 DecodeSrgbClamped(inputEncoded[channel]);
        }
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

    // カメラの見た目のLUTを指定してToneMappingPassを1回実行し、ToneMappedColor（RGBA16F）を読み戻す。
    bool RunToneMapping(const DevicePtr& device,
                        ShaderManager& shaderManager,
                        SceneRenderer& renderer,
                        const TexturePtr& sceneColor,
                        ToneMappingOperator toneMapOperator,
                        const char* lookLutAssetPath,
                        VariableArray<uint16_t>& outPixels)
    {
        const uint32_t width = sceneColor->GetWidth();
        const uint32_t height = sceneColor->GetHeight();

        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("SceneColor", sceneColor);

        // 起動画面と同じくカメラのグレーディングの差し替えを有効にし、その後にLUTが掛かる経路を通す。
        CameraProxy camera;
        camera.GradingOverride.bEnabled = true;
        camera.GradingOverride.Contrast = 1.25f;
        camera.GradingOverride.ContrastPivot = 0.5f;
        camera.GradingOverride.Saturation = 1.25f;
        camera.GradingOverride.Temperature = 0.1f;
        camera.LookLut.AssetPath = lookLutAssetPath;
        camera.LookLut.Intensity = 1.0f;

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Capabilities = &device->GetCapabilities();
        context.Renderer = &renderer;
        context.SharedResources = &sharedResources;
        context.MainCamera = &camera;
        context.RenderWidth = width;
        context.RenderHeight = height;
        context.ScreenWidth = width;
        context.ScreenHeight = height;

        // 周辺減光はLUTの後に掛かるので無効にし、LUTの結果をそのまま読む。
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
        BufferDesc readbackDesc(readbackSize, ResourceUsage::TransferDst, true, "GradingLutIdentityVulkanTest.Readback");
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

    // actual と expected（表示のリニア値）を符号化後の値で比べる。
    template <typename ExpectedFn>
    ComparisonResult Compare(const VariableArray<uint16_t>& actualPixels,
                             uint32_t width,
                             uint32_t height,
                             ExpectedFn expectedAt)
    {
        ComparisonResult result;
        double sum = 0.0;
        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const size_t base = (static_cast<size_t>(y) * width + x) * 4u;
                double expected[3];
                expectedAt(base, expected);
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const float actual = HalfToFloat(actualPixels[base + channel]);
                    const double actualEncoded = std::isfinite(actual) ? EncodeSrgbClamped(actual) : -1.0;
                    const double expectedEncoded = EncodeSrgbClamped(expected[channel]);
                    // NaN は最大差として扱う
                    const double difference = actualEncoded >= 0.0 ? std::abs(actualEncoded - expectedEncoded) : 1.0;
                    sum += difference;
                    const long actualCode = std::lround(std::max(actualEncoded, 0.0) * 255.0);
                    const long expectedCode = std::lround(expectedEncoded * 255.0);
                    result.Encoded8BitMismatches += actualCode != expectedCode ? 1u : 0u;
                    if (difference > result.MaxDifference)
                    {
                        result.MaxDifference = difference;
                        result.WorstX = x;
                        result.WorstY = y;
                        result.WorstChannel = channel;
                        result.WorstActual = actual;
                        result.WorstExpected = static_cast<float>(expected[channel]);
                    }
                }
            }
        }
        result.MeanDifference = sum / (static_cast<double>(width) * height * 3.0);
        return result;
    }

    void PrintComparison(const char* label, const ComparisonResult& result)
    {
        std::cout << label << " max=" << result.MaxDifference * 255.0 << "/255"
                  << " mean=" << result.MeanDifference * 255.0 << "/255"
                  << " encoded_8bit_mismatches=" << result.Encoded8BitMismatches << " worst=(" << result.WorstX
                  << "," << result.WorstY << ") ch=" << result.WorstChannel << " actual=" << result.WorstActual
                  << " expected=" << result.WorstExpected << '\n';
    }

    bool PixelsEqual(const VariableArray<uint16_t>& lhs, const VariableArray<uint16_t>& rhs)
    {
        return lhs.size() == rhs.size() && std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(uint16_t)) == 0;
    }

    // 読み戻した RGBA16F のうちビットが異なる half の数（大きさが違えば多い方の全数）。
    size_t CountDifferentHalfs(const VariableArray<uint16_t>& lhs, const VariableArray<uint16_t>& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return std::max(lhs.size(), rhs.size());
        }
        size_t count = 0u;
        for (size_t index = 0u; index < lhs.size(); ++index)
        {
            count += lhs[index] != rhs[index] ? 1u : 0u;
        }
        return count;
    }

    // LUTの RGB の差分のうち 0（+0 と -0）でない half の数。
    size_t CountNonZeroOffsets(const HostLut& lut)
    {
        size_t count = 0u;
        for (size_t index = 0u; index < lut.Texels.size(); ++index)
        {
            if (index % 4u != 3u && (lut.Texels[index] & 0x7FFFu) != 0u)
            {
                ++count;
            }
        }
        return count;
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
        HostLut identityLut;
        HostLut warmLut;
        if (!ReadChart(chart) || !ReadLookLut(IdentityLutAssetPath, identityLut) ||
            !ReadLookLut(WarmFilmLutAssetPath, warmLut))
        {
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
        sceneColorDesc.DebugName = "GradingLutIdentityVulkanTest.SceneColor";
        TexturePtr sceneColor = device->CreateTexture(sceneColorDesc);
        if (!sceneColor)
        {
            std::cerr << "SceneColorを作成できませんでした\n";
            return 1;
        }
        const uint32_t rowPitch = chart.Width * BytesPerPixel;
        sceneColor->Update(chart.Pixels.data(), rowPitch, rowPitch * chart.Height);

        VariableArray<uint16_t> nonePixels;
        VariableArray<uint16_t> identityPixels;
        VariableArray<uint16_t> warmPixels;
        VariableArray<uint16_t> missingPixels;
        VariableArray<uint16_t> aces20NonePixels;
        VariableArray<uint16_t> aces20WarmPixels;
        if (!RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::ACES, nullptr,
                            nonePixels) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::ACES,
                            IdentityLutAssetPath, identityPixels) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::ACES,
                            WarmFilmLutAssetPath, warmPixels) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::ACES,
                            MissingLutAssetPath, missingPixels) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::Aces20Lut, nullptr,
                            aces20NonePixels) ||
            !RunToneMapping(device, shaderManager, renderer, sceneColor, ToneMappingOperator::Aces20Lut,
                            WarmFilmLutAssetPath, aces20WarmPixels))
        {
            return 1;
        }

        const uint32_t width = chart.Width;
        const uint32_t height = chart.Height;
        auto noneAt = [&](size_t base, double out[3]) {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                out[channel] = HalfToFloat(nonePixels[base + channel]);
            }
        };
        // 恒等のLUT: 読み戻した RGBA16F をLUTなしの出力とビット単位で比べる（符号化後の差も合わせて出す）。
        // ファイルの中身が恒等であること（差分が全て0）も確かめる。
        const size_t identityDifferentHalfs = CountDifferentHalfs(identityPixels, nonePixels);
        const ComparisonResult identityResult = Compare(identityPixels, width, height, noneAt);
        const size_t identityFileNonZeroOffsets = CountNonZeroOffsets(identityLut);
        // 見た目のLUT: LUTなしの出力へCPUでLUTを引いた値と比べる。LUTなしとの差で掛かったことを確かめる。
        const ComparisonResult warmResult = Compare(warmPixels, width, height, [&](size_t base, double out[3]) {
            double input[3];
            noneAt(base, input);
            ApplyLookLutOnCpu(warmLut, input, out);
        });
        const ComparisonResult warmEffect = Compare(warmPixels, width, height, noneAt);
        const bool bMissingMatchesNone = PixelsEqual(missingPixels, nonePixels);
        const bool bAces20Unaffected = PixelsEqual(aces20WarmPixels, aces20NonePixels);

        std::cout << "pixels=" << width * height << " lut_size=" << warmLut.Size << '\n';
        std::cout << "thresholds identity=bit_exact look_vs_cpu<=" << MaxLookEncodedDifference * 255.0
                  << "/255 look_effect>=" << MinLookEffectEncodedDifference * 255.0 << "/255\n";
        PrintComparison("identity_lut_vs_no_lut", identityResult);
        std::cout << "identity_lut_vs_no_lut different_halfs=" << identityDifferentHalfs << " of "
                  << nonePixels.size() << '\n';
        std::cout << "identity_lut_file nonzero_offsets=" << identityFileNonZeroOffsets << '\n';
        PrintComparison("warm_film_lut_vs_cpu_reference", warmResult);
        PrintComparison("warm_film_lut_vs_no_lut", warmEffect);
        std::cout << "missing_lut_matches_no_lut=" << (bMissingMatchesNone ? 1 : 0) << '\n';
        std::cout << "aces20_lut_operator_ignores_look_lut=" << (bAces20Unaffected ? 1 : 0) << '\n';

        renderer.Shutdown();
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';

        bool bPassed = true;
        if (identityDifferentHalfs != 0u || identityResult.MaxDifference != 0.0 ||
            identityResult.Encoded8BitMismatches != 0u)
        {
            std::cerr << "恒等のLUTを掛けた出力がLUTなしの出力とビット単位で一致しません\n";
            bPassed = false;
        }
        if (identityFileNonZeroOffsets != 0u)
        {
            std::cerr << "恒等のLUTファイルの中身が恒等になっていません\n";
            bPassed = false;
        }
        if (warmResult.MaxDifference > MaxLookEncodedDifference)
        {
            std::cerr << "見た目のLUTの出力がCPUでLUTを引いた値と一致しません\n";
            bPassed = false;
        }
        if (warmEffect.MaxDifference < MinLookEffectEncodedDifference)
        {
            std::cerr << "見た目のLUTが掛かっていません（LUTなしとの差が小さすぎます）\n";
            bPassed = false;
        }
        if (!bMissingMatchesNone)
        {
            std::cerr << "読めないLUTを指定したときにLUTなしの出力になっていません\n";
            bPassed = false;
        }
        if (!bAces20Unaffected)
        {
            std::cerr << "ACES 2.0 SDR LUTの演算子に見た目のLUTが掛かっています\n";
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
}

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "GradingLutIdentityVulkanTest で例外が発生しました: " << exception.what() << '\n';
        return 1;
    }
}
