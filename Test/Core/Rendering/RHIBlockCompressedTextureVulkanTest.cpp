// 圧縮済みの全ミップを GpuResourceStore 経由で GPU へ上げ、計算シェーダーでミップごとに読み戻して確かめる。
// 既知の単色ブロックを手で組んだ BC1・BC3・BC4・BC5・BC6H・BC7 と、全ミップを渡した R16 の 8x8・2段のテクスチャを作り、
// ミップ0の4ブロックとミップ1の1ブロックの色が期待（BC6H は相対許容 1e-3、ほかは 2/255）と一致すること。
// 上げる量が足りない初期データでは、作成が失敗してハンドルが無効になること。
#include "Rendering/GpuResourceStore.h"
#include "Rendering/GpuResourceTypes.h"
#include "Rendering/ShaderManager.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
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

    constexpr const char* TestName = "RHIBlockCompressedTextureVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t TextureSize = 8u;
    constexpr uint32_t MipCount = 2u;
    constexpr uint32_t ProbeCount = 5u;
    constexpr uint32_t ProbeBufferBytes = 8u * 4u * sizeof(float);
    // 期待の色との許容差（2/255）
    constexpr float Tolerance = 2.0f / 255.0f;

    bool IsGpuTestSkipForced()
    {
        char* forceSkip = nullptr;
        size_t forceSkipLength = 0;
        if (_dupenv_s(&forceSkip, &forceSkipLength, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || forceSkip == nullptr)
        {
            return false;
        }
        const bool bForceSkip = std::strcmp(forceSkip, "1") == 0;
        free(forceSkip);
        return bForceSkip;
    }

    int SkipGpuTest(const char* reason)
    {
        std::cout << TestName << " skipped: " << reason << std::endl;
        return GpuTestSkipReturnCode;
    }

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    // ---- 手で組むブロック（インデックスはすべて 0 なので、画素はすべて端点0の色になる） ----

    // BC1: 端点0（RGB565）・端点1・全画素のインデックス0。端点1 を 0 にして端点0 > 端点1 の4色モードにする。
    void PackBC1Block(uint8_t* out, uint32_t r5, uint32_t g6, uint32_t b5)
    {
        const uint16_t color = static_cast<uint16_t>((r5 << 11u) | (g6 << 5u) | b5);
        std::memset(out, 0, 8);
        out[0] = static_cast<uint8_t>(color & 0xFFu);
        out[1] = static_cast<uint8_t>(color >> 8u);
    }

    // BC4: 端点0（8ビット）・端点1 = 0・全画素のインデックス0。
    void PackBC4Block(uint8_t* out, uint32_t value)
    {
        std::memset(out, 0, 8);
        out[0] = static_cast<uint8_t>(value);
    }

    // BC3: アルファ端点を同じ値、色端点を同じ RGB565 にし、全画素のインデックスを 0 にする。
    void PackBC3Block(uint8_t* out, uint32_t r5, uint32_t g6, uint32_t b5, uint32_t alpha)
    {
        PackBC4Block(out, alpha);
        out[1] = static_cast<uint8_t>(alpha);
        PackBC1Block(out + 8u, r5, g6, b5);
        out[10] = out[8];
        out[11] = out[9];
    }

    // BC6H・BC7 のビット書き込み（下位ビットから詰める）
    class BitWriter
    {
    public:
        explicit BitWriter(uint8_t* out) : m_Out(out) { std::memset(out, 0, 16); }

        void Put(uint32_t value, uint32_t bitCount)
        {
            for (uint32_t bit = 0; bit < bitCount; ++bit, ++m_Position)
            {
                if (((value >> bit) & 1u) != 0u)
                {
                    m_Out[m_Position / 8u] = static_cast<uint8_t>(m_Out[m_Position / 8u] | (1u << (m_Position % 8u)));
                }
            }
        }

    private:
        uint8_t* m_Out;
        uint32_t m_Position = 0;
    };

    // BC6H モード11: 1サブセット・RGB 各10ビットの独立した端点2つ・4ビットインデックス。
    void PackBC6HMode11Block(uint8_t* out, const uint32_t rgb[3])
    {
        BitWriter writer(out);
        writer.Put(0b00011u, 5u);
        for (uint32_t endpoint = 0; endpoint < 2u; ++endpoint)
        {
            for (uint32_t channel = 0; channel < 3u; ++channel)
            {
                writer.Put(rgb[channel], 10u);
            }
        }
        // 残りの 63 ビット（先頭画素だけ3ビット、ほかは4ビット）のインデックスはすべて 0。
    }

    // BC7 モード6（1サブセット・RGBA 各7ビット端点＋P ビット・4ビットインデックス）。端点0 = 端点1 = 目的の色、
    // インデックスはすべて 0。8ビットの値 t は上位7ビットを端点、最下位を P ビットに入れて復元される。
    void PackBC7Mode6Block(uint8_t* out, const uint8_t rgba[4])
    {
        BitWriter writer(out);
        writer.Put(1u << 6u, 7u); // モード6: 下位6ビットが 0、続く1ビットが 1
        for (uint32_t channel = 0; channel < 4u; ++channel)
        {
            writer.Put(rgba[channel] >> 1u, 7u); // 端点0
            writer.Put(rgba[channel] >> 1u, 7u); // 端点1
        }
        writer.Put(rgba[0] & 1u, 1u); // 端点0 の P ビット
        writer.Put(rgba[0] & 1u, 1u); // 端点1 の P ビット
        // 残りの 63 ビットのインデックスは 0
    }

    // ---- 期待値 ----

    struct ExpectedColor
    {
        float Rgba[4];
    };

    struct FormatCase
    {
        const char* Name;
        TextureCreateInfo::Format Format;
        VariableArray<uint8_t> Data;
        ExpectedColor Expected[ProbeCount];
        bool bPassAllMipsFlag;
    };

    // BC1: ミップ0 は 2x2 ブロック（左上・右上・左下・右下）、ミップ1 は 1 ブロック。
    FormatCase MakeBC1Case()
    {
        const uint32_t colors[ProbeCount][3] = {{31u, 0u, 0u}, {0u, 63u, 0u}, {0u, 0u, 31u}, {16u, 32u, 8u}, {31u, 63u, 0u}};
        FormatCase testCase{"BC1_UNORM", TextureCreateInfo::Format::BC1_UNORM, {}, {}, false};
        testCase.Data.resize(ProbeCount * 8u);
        for (uint32_t block = 0; block < ProbeCount; ++block)
        {
            PackBC1Block(testCase.Data.data() + block * 8u, colors[block][0], colors[block][1], colors[block][2]);
            testCase.Expected[block] = {{colors[block][0] / 31.0f, colors[block][1] / 63.0f, colors[block][2] / 31.0f, 1.0f}};
        }
        return testCase;
    }

    float SrgbToLinear(float value)
    {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    FormatCase MakeBC3Case(bool bSrgb)
    {
        const uint32_t colors[ProbeCount][4] = {
            {31u, 0u, 0u, 255u}, {0u, 63u, 0u, 128u}, {0u, 0u, 31u, 64u}, {16u, 32u, 8u, 200u}, {1u, 20u, 24u, 30u}};
        FormatCase testCase{bSrgb ? "BC3_SRGB" : "BC3_UNORM",
                            bSrgb ? TextureCreateInfo::Format::BC3_SRGB : TextureCreateInfo::Format::BC3_UNORM,
                            {}, {}, false};
        testCase.Data.resize(ProbeCount * 16u);
        for (uint32_t block = 0; block < ProbeCount; ++block)
        {
            PackBC3Block(testCase.Data.data() + block * 16u, colors[block][0], colors[block][1], colors[block][2], colors[block][3]);
            // RGB565 を8ビットへ展開してから正規化する。sRGB の変換は RGB だけに適用し、アルファは線形のまま。
            float rgb[3] = {
                static_cast<float>((colors[block][0] << 3u) | (colors[block][0] >> 2u)) / 255.0f,
                static_cast<float>((colors[block][1] << 2u) | (colors[block][1] >> 4u)) / 255.0f,
                static_cast<float>((colors[block][2] << 3u) | (colors[block][2] >> 2u)) / 255.0f};
            if (bSrgb)
            {
                for (uint32_t channel = 0; channel < 3u; ++channel)
                {
                    rgb[channel] = SrgbToLinear(rgb[channel]);
                }
            }
            testCase.Expected[block] = {{rgb[0], rgb[1], rgb[2], colors[block][3] / 255.0f}};
        }
        return testCase;
    }

    // 符号なし10ビット端点 q の unquantize は q=0 なら0、q=1023 なら65535、ほかは ((q<<16)+32768)>>10。
    // 索引0の重みは0なので補間後も u のまま。finish_unquantize の (u*31)>>6 が half のビット列になる。
    // 例: q=495 -> u=31712 -> half=15360 (0x3c00) -> 1.0。q=1023 -> half=0x7bff -> 65504.0。
    // https://learn.microsoft.com/en-us/windows/win32/direct3d11/bc6h-format
    float DecodeBC6HUnsignedEndpoint(uint32_t quantized)
    {
        const uint32_t unquantized = quantized == 0u ? 0u :
            (quantized == 1023u ? 65535u : ((quantized << 16u) + 32768u) >> 10u);
        const uint32_t halfBits = (unquantized * 31u) >> 6u;
        const uint32_t exponent = halfBits >> 10u;
        const uint32_t mantissa = halfBits & 1023u;
        // 非正規化数は mantissa * 2^-24、正規化数は (1024+mantissa) * 2^(exponent-25)。
        return exponent == 0u ? std::ldexp(static_cast<float>(mantissa), -24) :
            std::ldexp(static_cast<float>(1024u + mantissa), static_cast<int>(exponent) - 25);
    }

    FormatCase MakeBC6HCase()
    {
        const uint32_t endpoints[ProbeCount][3] = {
            {495u, 462u, 0u}, {528u, 561u, 431u}, {600u, 400u, 500u}, {300u, 650u, 700u}, {1023u, 100u, 550u}};
        FormatCase testCase{"BC6H_UFLOAT", TextureCreateInfo::Format::BC6H_UFLOAT, {}, {}, false};
        testCase.Data.resize(ProbeCount * 16u);
        for (uint32_t block = 0; block < ProbeCount; ++block)
        {
            PackBC6HMode11Block(testCase.Data.data() + block * 16u, endpoints[block]);
            testCase.Expected[block] = {{DecodeBC6HUnsignedEndpoint(endpoints[block][0]),
                                         DecodeBC6HUnsignedEndpoint(endpoints[block][1]),
                                         DecodeBC6HUnsignedEndpoint(endpoints[block][2]), 1.0f}};
        }
        return testCase;
    }

    FormatCase MakeBC4Case()
    {
        const uint32_t values[ProbeCount] = {255u, 64u, 128u, 200u, 30u};
        FormatCase testCase{"BC4_UNORM", TextureCreateInfo::Format::BC4_UNORM, {}, {}, false};
        testCase.Data.resize(ProbeCount * 8u);
        for (uint32_t block = 0; block < ProbeCount; ++block)
        {
            PackBC4Block(testCase.Data.data() + block * 8u, values[block]);
            testCase.Expected[block] = {{values[block] / 255.0f, 0.0f, 0.0f, 1.0f}};
        }
        return testCase;
    }

    FormatCase MakeBC5Case()
    {
        const uint32_t values[ProbeCount][2] = {{255u, 0u}, {0u, 255u}, {128u, 64u}, {33u, 200u}, {90u, 250u}};
        FormatCase testCase{"BC5_UNORM", TextureCreateInfo::Format::BC5_UNORM, {}, {}, false};
        testCase.Data.resize(ProbeCount * 16u);
        for (uint32_t block = 0; block < ProbeCount; ++block)
        {
            PackBC4Block(testCase.Data.data() + block * 16u, values[block][0]);
            PackBC4Block(testCase.Data.data() + block * 16u + 8u, values[block][1]);
            testCase.Expected[block] = {{values[block][0] / 255.0f, values[block][1] / 255.0f, 0.0f, 1.0f}};
        }
        return testCase;
    }

    FormatCase MakeBC7Case()
    {
        const uint8_t colors[ProbeCount][4] = {
            {255u, 0u, 0u, 255u}, {0u, 255u, 0u, 128u}, {0u, 0u, 255u, 64u}, {90u, 170u, 50u, 255u}, {200u, 10u, 120u, 32u}};
        FormatCase testCase{"BC7_UNORM", TextureCreateInfo::Format::BC7_UNORM, {}, {}, false};
        testCase.Data.resize(ProbeCount * 16u);
        for (uint32_t block = 0; block < ProbeCount; ++block)
        {
            PackBC7Mode6Block(testCase.Data.data() + block * 16u, colors[block]);
            // P ビットは4チャンネルで共有なので、復元される値は上位7ビット＋チャンネル0の最下位ビット
            const uint32_t pBit = colors[block][0] & 1u;
            float expected[4];
            for (uint32_t channel = 0; channel < 4u; ++channel)
            {
                expected[channel] = static_cast<float>(((colors[block][channel] >> 1u) << 1u) | pBit) / 255.0f;
            }
            testCase.Expected[block] = {{expected[0], expected[1], expected[2], expected[3]}};
        }
        return testCase;
    }

    // R16: ミップ0（8x8）とミップ1（4x4）をつなげた 16 ビットの値。各ミップの先頭からの距離で値が決まるので、
    // ミップの境界やミップごとの行の長さを取り違えると期待と合わない。
    FormatCase MakeR16Case()
    {
        FormatCase testCase{"R16_UNORM", TextureCreateInfo::Format::R16_UNORM, {}, {}, true};
        testCase.Data.resize((8u * 8u + 4u * 4u) * sizeof(uint16_t));
        uint16_t* texels = reinterpret_cast<uint16_t*>(testCase.Data.data());
        for (uint32_t y = 0; y < 8u; ++y)
        {
            for (uint32_t x = 0; x < 8u; ++x)
            {
                texels[y * 8u + x] = static_cast<uint16_t>((x + 8u * y) * 1000u);
            }
        }
        for (uint32_t y = 0; y < 4u; ++y)
        {
            for (uint32_t x = 0; x < 4u; ++x)
            {
                texels[64u + y * 4u + x] = static_cast<uint16_t>(45000u + (x + 4u * y) * 100u);
            }
        }
        const uint32_t probes[ProbeCount] = {1u + 8u * 1u, 5u + 8u * 1u, 1u + 8u * 5u, 5u + 8u * 5u, 64u + 2u + 4u * 2u};
        for (uint32_t index = 0; index < ProbeCount; ++index)
        {
            testCase.Expected[index] = {{texels[probes[index]] / 65535.0f, 0.0f, 0.0f, 1.0f}};
        }
        return testCase;
    }

    // ---- 読み戻し ----

    DescriptorSetDesc MakeProbeDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        const ResourceBindType types[] = {ResourceBindType::CombinedImageSampler, ResourceBindType::RWBuffer};
        for (uint32_t bindingIndex = 0; bindingIndex < 2u; ++bindingIndex)
        {
            DescriptorBinding binding;
            binding.binding = bindingIndex;
            binding.type = types[bindingIndex];
            binding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    struct ProbeResources
    {
        DevicePtr Device;
        PipelinePtr Pipeline;
        SamplerPtr Sampler;
        BufferPtr ResultBuffer;
        BufferPtr ReadbackBuffer;
    };

    // 5か所の色を読み戻す。成功したら outColors（5×RGBA）へ入れる。
    bool ReadProbeColors(ProbeResources& resources, const TexturePtr& texture, float outColors[ProbeCount][4])
    {
        DescriptorSetPtr descriptorSet = resources.Device->CreateDescriptorSet(MakeProbeDescriptorSetDesc());
        if (!descriptorSet)
        {
            std::cerr << "ディスクリプタセットを作れませんでした\n";
            return false;
        }
        descriptorSet->BindTexture(0u, texture);
        descriptorSet->BindSampler(0u, resources.Sampler);
        descriptorSet->BindStorageBuffer(1u, resources.ResultBuffer, 0u, ProbeBufferBytes);
        descriptorSet->Update();

        CommandListPtr commandList = resources.Device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "コマンドリストを作れませんでした\n";
            return false;
        }
        commandList->Begin();
        commandList->BufferBarrier(resources.ResultBuffer, ResourceState::Undefined, ResourceState::UnorderedAccess,
                                   0u, ProbeBufferBytes);
        commandList->SetPipeline(resources.Pipeline);
        commandList->SetDescriptorSet(descriptorSet);
        commandList->Dispatch(1u, 1u, 1u);
        commandList->BufferBarrier(resources.ResultBuffer, ResourceState::UnorderedAccess, ResourceState::CopySource,
                                   0u, ProbeBufferBytes);
        commandList->CopyBuffer(resources.ResultBuffer, resources.ReadbackBuffer, ProbeBufferBytes);
        commandList->BufferBarrier(resources.ReadbackBuffer, ResourceState::CopyDest, ResourceState::HostRead,
                                   0u, ProbeBufferBytes);
        commandList->End();
        commandList->Submit(true);
        resources.Device->WaitIdle();

        const void* mapped = resources.ReadbackBuffer->Map(0u, ProbeBufferBytes);
        if (mapped == nullptr)
        {
            std::cerr << "読み戻しのバッファを写像できませんでした\n";
            return false;
        }
        std::memcpy(outColors, mapped, ProbeCount * 4u * sizeof(float));
        resources.ReadbackBuffer->Unmap();
        return true;
    }

    bool RunFormatCase(GpuResourceStore& store, ProbeResources& resources, const FormatCase& testCase)
    {
        TextureCreateInfo createInfo;
        createInfo.Width = TextureSize;
        createInfo.Height = TextureSize;
        createInfo.MipLevels = MipCount;
        createInfo.PixelFormat = testCase.Format;
        createInfo.bInitialDataHasAllMips = testCase.bPassAllMipsFlag;
        createInfo.DebugName = testCase.Name;

        const TextureHandle handle = store.CreateTexture(createInfo, testCase.Data.data(), testCase.Data.size());
        if (!handle.IsValid())
        {
            std::cerr << testCase.Name << ": テクスチャを作れませんでした\n";
            return false;
        }
        TexturePtr texture = store.GetRHITexturePtr(handle);
        if (!texture || texture->GetMipLevels() != MipCount)
        {
            std::cerr << testCase.Name << ": ミップ数が期待と違います\n";
            return false;
        }

        float colors[ProbeCount][4] = {};
        if (!ReadProbeColors(resources, texture, colors))
        {
            return false;
        }

        bool bMatches = true;
        for (uint32_t probe = 0; probe < ProbeCount; ++probe)
        {
            float maxDifference = 0.0f;
            bool bBC6HMatches = true;
            for (uint32_t channel = 0; channel < 4u; ++channel)
            {
                const float difference = std::fabs(colors[probe][channel] - testCase.Expected[probe].Rgba[channel]);
                maxDifference = std::fmax(maxDifference, difference);
                if (testCase.Format == TextureCreateInfo::Format::BC6H_UFLOAT)
                {
                    // 10ビット端点の量子化は期待値の復号式に含む。正規化 half の1 ULP以下の相対誤差を
                    // 収める 1e-3 をチャンネルごとに許容し、期待が0なら厳密に0を求める。NaN も不合格にする。
                    const float tolerance = std::fabs(testCase.Expected[probe].Rgba[channel]) * 1e-3f;
                    bBC6HMatches = bBC6HMatches && difference <= tolerance;
                }
            }
            std::cout << testCase.Name << " probe=" << probe << " readback=(" << colors[probe][0] << ", "
                      << colors[probe][1] << ", " << colors[probe][2] << ", " << colors[probe][3] << ") expected=("
                      << testCase.Expected[probe].Rgba[0] << ", " << testCase.Expected[probe].Rgba[1] << ", "
                      << testCase.Expected[probe].Rgba[2] << ", " << testCase.Expected[probe].Rgba[3]
                      << ") max_diff=" << maxDifference << '\n';
            if (testCase.Format == TextureCreateInfo::Format::BC6H_UFLOAT ? !bBC6HMatches : !(maxDifference <= Tolerance))
            {
                bMatches = false;
            }
        }
        store.ReleaseTexture(handle);
        return bMatches;
    }

    // 全ミップに足りない初期データでは、作成が失敗してハンドルが無効になる
    bool RunShortDataCase(GpuResourceStore& store)
    {
        TextureCreateInfo createInfo;
        createInfo.Width = TextureSize;
        createInfo.Height = TextureSize;
        createInfo.MipLevels = MipCount;
        createInfo.PixelFormat = TextureCreateInfo::Format::BC7_UNORM;
        createInfo.DebugName = "BC7ShortData";

        const FormatCase full = MakeBC7Case();
        // ミップ1の1ブロックぶん足りない
        const TextureHandle handle = store.CreateTexture(createInfo, full.Data.data(), full.Data.size() - 16u);
        if (handle.IsValid())
        {
            std::cerr << "足りない初期データでテクスチャが作られました\n";
            store.ReleaseTexture(handle);
            return false;
        }
        return true;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 was set.");
        }

        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return SkipGpuTest("Vulkanデバイスを利用できません");
        }
        if (!device->GetCapabilities().bTextureCompressionBC)
        {
            return SkipGpuTest("デバイスが textureCompressionBC に対応していません");
        }

        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << "ShaderManagerを初期化できませんでした\n";
            return 1;
        }

        ProbeResources resources;
        resources.Device = device;
        {
            ShaderPtr shader = shaderManager.LoadShader("texture_mip_probe.comp", RHI::ShaderStage::Compute);
            if (!shader)
            {
                std::cerr << "texture_mip_probe.comp を読み込めませんでした\n";
                return 1;
            }
            ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = shader;
            pipelineDesc.descriptorSetLayouts.push_back(MakeProbeDescriptorSetDesc());
            resources.Pipeline = device->CreateComputePipeline(pipelineDesc);

            SamplerDesc samplerDesc;
            samplerDesc.filterMin = FilterMode::Point;
            samplerDesc.filterMag = FilterMode::Point;
            samplerDesc.filterMip = FilterMode::Point;
            samplerDesc.addressU = TextureAddressMode::Clamp;
            samplerDesc.addressV = TextureAddressMode::Clamp;
            samplerDesc.addressW = TextureAddressMode::Clamp;
            resources.Sampler = device->CreateSampler(samplerDesc);

            BufferDesc resultDesc;
            resultDesc.Size = ProbeBufferBytes;
            resultDesc.Usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferSrc | ResourceUsage::TransferDst;
            resultDesc.DebugName = "BlockCompressedProbeResults";
            resources.ResultBuffer = device->CreateBuffer(resultDesc);
            resources.ReadbackBuffer = device->CreateBuffer(
                BufferDesc(ProbeBufferBytes, ResourceUsage::TransferDst, true, "BlockCompressedProbeReadback"));
        }
        if (!resources.Pipeline || !resources.Sampler || !resources.ResultBuffer || !resources.ReadbackBuffer)
        {
            std::cerr << "確認用の資源を作れませんでした\n";
            return 1;
        }

        Thread::Atomic<uint64_t> nextHandleId(1);
        GpuResourceStore store(device, nextHandleId);

        bool bPassed = true;
        const FormatCase cases[] = {MakeBC1Case(), MakeBC3Case(false), MakeBC3Case(true), MakeBC4Case(), MakeBC5Case(),
                                    MakeBC6HCase(), MakeBC7Case(), MakeR16Case()};
        for (const FormatCase& testCase : cases)
        {
            const bool bCasePassed = RunFormatCase(store, resources, testCase);
            std::cout << testCase.Name << (bCasePassed ? " PASS" : " FAIL") << '\n';
            bPassed = bPassed && bCasePassed;
        }

        const bool bShortDataPassed = RunShortDataCase(store);
        std::cout << "ShortData" << (bShortDataPassed ? " PASS" : " FAIL") << '\n';
        bPassed = bPassed && bShortDataPassed;

        device->WaitIdle();
        resources = ProbeResources{};
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
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
        std::cerr << TestName << "で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
