// 自動露出の輝度ヒストグラムの読み戻しを実GPUで確かめる。区間ごとに画素数を決めた灰色のSceneColorを
// RenderGraph越しにAutoExposurePassへ通し、同じフレームスロットの次のフレームで読み戻したヒストグラムの
// 全区間が、CPUで同じ規則（AutoExposureLuminanceToBin）で数えた期待値と一致すること。
// 読み戻し先はパスがコピーの後にホストの読み取りの状態へ移し、フェンスの待機の後にCPUで読む。
#include "Rendering/AutoExposure.h"
#include "Rendering/AutoExposurePass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
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

    constexpr const char* TestName = "AutoExposureHistogramVulkanTest";
    // 16×16のグループの端を跨ぐ寸法にする
    constexpr uint32_t Width = 40u;
    constexpr uint32_t Height = 24u;
    constexpr uint32_t BytesPerPixel = 8u;

    // 灰色の画素の値（log2 cd/m²）とその画素数。値は区間の中央付近に置き、黒（区間0）と
    // ヒストグラムの下端より暗い値（区間0へ寄せる）も含める。
    struct HistogramPattern
    {
        float Log2Luminance;
        bool bBlack;
        uint32_t PixelCount;
    };
    constexpr HistogramPattern Patterns[] = {
        {0.0f, true, 17u},
        {-12.0f, false, 23u},
        {-4.0f + 0.0625f, false, 101u},
        {-1.0f + 0.0625f, false, 64u},
        {2.5f + 0.0625f, false, 256u},
        {4.0f + 0.0625f, false, 199u},
        {7.25f + 0.0625f, false, 120u},
        {11.0f + 0.0625f, false, 88u},
        {15.5f + 0.0625f, false, 92u},
    };

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
            return mantissa == 0u ? sign * INFINITY : NAN;
        }
        return sign * std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    }

    // 正規化数と非正規化数の範囲の正の値を半精度へ（切り捨て）
    uint16_t FloatToHalf(float value)
    {
        if (!(value > 0.0f))
        {
            return 0u;
        }
        int exponent = 0;
        const float fraction = std::frexp(value, &exponent);
        const int halfExponent = exponent - 1 + 15;
        if (halfExponent >= 31)
        {
            return 0x7BFFu;
        }
        if (halfExponent <= 0)
        {
            const float subnormal = std::ldexp(value, 24);
            return static_cast<uint16_t>(std::min(subnormal, 1023.0f));
        }
        const uint32_t mantissa = static_cast<uint32_t>((fraction * 2.0f - 1.0f) * 1024.0f);
        return static_cast<uint16_t>((static_cast<uint32_t>(halfExponent) << 10u) | (mantissa & 0x3FFu));
    }

    // SceneColor を取り込み、AutoExposurePass が読む名前で公開する
    class SceneColorImportPass final : public IRenderGraphPass
    {
    public:
        explicit SceneColorImportPass(TexturePtr texture)
            : m_Texture(texture)
        {
        }

        const char* GetName() const override { return "AutoExposureHistogramTest.Import"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            const RGResourceHandle handle =
                builder.ImportTexture(m_Texture, ResourceState::ShaderResource, "AutoExposureHistogramTest.SceneColor");
            builder.Read(handle, ResourceState::ShaderResource);
            builder.PublishTexture(RenderGraphResourceNames::SceneColor, handle);
        }

        void Execute(RenderGraphResources&, ViewRenderContext&) override {}

    private:
        TexturePtr m_Texture;
    };

    bool RunFrame(const DevicePtr& device,
                  RenderGraph& graph,
                  SceneColorImportPass& importPass,
                  AutoExposurePass& pass,
                  ViewRenderContext& context,
                  uint64_t frameNumber)
    {
        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "コマンドリストを作れませんでした\n";
            return false;
        }
        // 読み戻しは同じフレームスロットが次に回ってきたときに行うので、スロットは常に0にする
        context.CommandList = commandList.get();
        context.FrameIndex = 0u;
        context.FrameNumber = frameNumber;
        context.TotalTime = static_cast<double>(frameNumber) / 60.0;
        commandList->SetFrameIndex(0u);
        commandList->Begin();
        graph.BeginFrame(frameNumber);
        graph.AddPass(&importPass);
        graph.AddPass(&pass);
        if (!graph.Compile(context))
        {
            commandList->End();
            std::cerr << "RenderGraphをコンパイルできませんでした\n";
            return false;
        }
        const RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        commandList->End();
        if (!result.bSuccess)
        {
            std::cerr << "RenderGraphを実行できませんでした\n";
            return false;
        }
        commandList->Submit(true);
        context.CommandList = nullptr;
        return true;
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

        // 画素を区間ごとの数だけ並べ、同じ規則で期待するヒストグラムを数える
        uint32_t expected[AutoExposureHistogramBinCount] = {};
        VariableArray<uint16_t> pixels(static_cast<size_t>(Width) * Height * 4u, 0u);
        const uint16_t one = FloatToHalf(1.0f);
        size_t pixelIndex = 0u;
        for (const HistogramPattern& pattern : Patterns)
        {
            const uint16_t value = pattern.bBlack ? 0u : FloatToHalf(std::exp2(pattern.Log2Luminance));
            const float decoded = HalfToFloat(value);
            // シェーダーと同じ Rec.709 の係数で灰色の輝度を求める
            const float luminance = decoded * 0.2126f + decoded * 0.7152f + decoded * 0.0722f;
            expected[AutoExposureLuminanceToBin(luminance)] += pattern.PixelCount;
            for (uint32_t count = 0u; count < pattern.PixelCount; ++count, ++pixelIndex)
            {
                pixels[pixelIndex * 4u] = value;
                pixels[pixelIndex * 4u + 1u] = value;
                pixels[pixelIndex * 4u + 2u] = value;
                pixels[pixelIndex * 4u + 3u] = one;
            }
        }
        if (pixelIndex != static_cast<size_t>(Width) * Height)
        {
            std::cerr << "並べた画素の数が画像の画素数と合いません: " << pixelIndex << '\n';
            return 1;
        }

        TextureDesc sceneColorDesc;
        sceneColorDesc.Width = Width;
        sceneColorDesc.Height = Height;
        sceneColorDesc.TextureFormat = Format::R16G16B16A16_FLOAT;
        sceneColorDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        sceneColorDesc.DebugName = "AutoExposureHistogramTest.SceneColor";
        TexturePtr sceneColor = device->CreateTexture(sceneColorDesc);
        if (!sceneColor)
        {
            std::cerr << "SceneColorを作れませんでした\n";
            return 1;
        }
        const uint32_t rowPitch = Width * BytesPerPixel;
        sceneColor->Update(pixels.data(), rowPitch, rowPitch * Height);

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Capabilities = &device->GetCapabilities();
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;

        RenderGraph graph;
        if (!graph.Initialize(nullptr))
        {
            std::cerr << "RenderGraphを初期化できませんでした\n";
            return 1;
        }
        SceneColorImportPass importPass(sceneColor);
        AutoExposurePass pass;
        // 1フレーム目で記録し、同じスロットの2フレーム目の始めに1フレーム目の結果を読む
        if (!RunFrame(device, graph, importPass, pass, context, 1u) ||
            !RunFrame(device, graph, importPass, pass, context, 2u))
        {
            return 1;
        }

        // 後の Shutdown で消えるので値で写す
        const AutoExposureMeasurement measurement = pass.GetLatestMeasurement();
        const AutoExposureHistogramReadback readback = pass.GetLatestHistogram();
        uint32_t mismatchedBins = 0u;
        uint64_t readbackTotal = 0u;
        for (uint32_t bin = 0u; bin < AutoExposureHistogramBinCount; ++bin)
        {
            readbackTotal += readback.Bins[bin];
            if (expected[bin] != 0u || readback.Bins[bin] != 0u)
            {
                std::cout << "auto_exposure_histogram bin=" << bin << " expected=" << expected[bin]
                          << " readback=" << readback.Bins[bin] << '\n';
            }
            if (readback.Bins[bin] != expected[bin])
            {
                ++mismatchedBins;
            }
        }
        std::cout << "auto_exposure_histogram readback_frame=" << readback.FrameNumber
                  << " readback_valid=" << (readback.bValid ? 1 : 0) << " total=" << readbackTotal
                  << " mismatched_bins=" << mismatchedBins << '\n';
        std::cout << "auto_exposure_histogram measurement_valid=" << (measurement.bValid ? 1 : 0)
                  << " frame=" << measurement.FrameNumber << " pixels=" << measurement.PixelCount
                  << " target_ev100=" << measurement.TargetEV100
                  << " adapted_ev100=" << measurement.AdaptedEV100 << '\n';

        pass.Shutdown();
        graph.Shutdown();
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';

        bool bPassed = true;
        if (!readback.bValid || readback.FrameNumber != 1u)
        {
            std::cerr << "1フレーム目のヒストグラムが読み戻されていません\n";
            bPassed = false;
        }
        if (mismatchedBins != 0u)
        {
            std::cerr << "読み戻したヒストグラムが期待と違う区間があります: " << mismatchedBins << '\n';
            bPassed = false;
        }
        if (!measurement.bValid || measurement.FrameNumber != 1u ||
            measurement.PixelCount != static_cast<uint64_t>(Width) * Height)
        {
            std::cerr << "読み戻したヒストグラムから測定が作られていません\n";
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
        std::cerr << "AutoExposureHistogramVulkanTestで例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
