// テクスチャのVRAM台帳（形式とミップ込みの確保量）の契約テスト。
// 見積りの純関数（RHI::EstimateTextureSize）と、GpuResourceStore が ResourceStats::TextureBytes へ
// 数える合計（作成で増え、解放で戻る・外部登録は数えない）を、GPU を使わない偽デバイスで確かめる。
#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"
#include "RHI/ITexture.h"

#include <cstdint>
#include <iostream>

namespace NorvesLib
{
namespace
{

using Core::Container::MakeShared;
using Core::Rendering::RenderResources;
using Core::Rendering::TextureCreateInfo;
using Core::Rendering::TextureHandle;

int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "TextureMemoryLedgerTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

class LedgerFakeTexture final : public RHI::ITexture
{
public:
    explicit LedgerFakeTexture(const RHI::TextureDesc& desc)
        : Desc(desc)
    {
    }

    uint32_t GetWidth() const override { return Desc.Width; }
    uint32_t GetHeight() const override { return Desc.Height; }
    uint32_t GetDepth() const override { return Desc.Depth; }
    uint32_t GetMipLevels() const override { return Desc.MipLevels; }
    uint32_t GetArraySize() const override { return Desc.ArraySize; }
    RHI::Format GetFormat() const override { return Desc.TextureFormat; }
    RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
    bool IsCubemap() const override { return Desc.IsCubemap; }
    void Update(const void*, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
    bool IsSparse() const override { return Desc.bSparse; }
    uint64_t GetSparseBoundBytes() const override { return Desc.bSparse ? BoundBytes : 0; }

    RHI::TextureDesc Desc;
    uint64_t BoundBytes = 0;
};

class LedgerFakeSampler final : public RHI::ISampler
{
public:
    explicit LedgerFakeSampler(const RHI::SamplerDesc& desc)
        : Desc(desc)
    {
    }

    RHI::FilterMode GetFilterMin() const override { return Desc.filterMin; }
    RHI::FilterMode GetFilterMag() const override { return Desc.filterMag; }
    RHI::FilterMode GetFilterMip() const override { return Desc.filterMip; }
    RHI::TextureAddressMode GetAddressModeU() const override { return Desc.addressU; }
    RHI::TextureAddressMode GetAddressModeV() const override { return Desc.addressV; }
    RHI::TextureAddressMode GetAddressModeW() const override { return Desc.addressW; }
    uint32_t GetMaxAnisotropy() const override { return Desc.maxAnisotropy; }
    RHI::CompareFunc GetCompareFunc() const override { return Desc.compareFunc; }

    RHI::SamplerDesc Desc;
};

class LedgerFakeDevice final : public RHI::IDevice
{
public:
    RHI::BufferPtr CreateBuffer(const RHI::BufferDesc&) override { return {}; }

    RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
    {
        auto texture = MakeShared<LedgerFakeTexture>(desc);
        LastTexture = texture.get();
        return texture;
    }

    RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc& desc) override
    {
        return MakeShared<LedgerFakeSampler>(desc);
    }

    RHI::ShaderPtr CreateShader(const RHI::ShaderDesc&) override { return {}; }
    RHI::CommandListPtr CreateCommandList() override { return {}; }
    RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc&) override { return {}; }
    RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc&) override { return {}; }
    RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc&) override { return {}; }
    RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc&) override { return {}; }
    RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc&) override { return {}; }
    RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc&) override { return {}; }
    RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
    RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
    void WaitIdle() override {}
    RHI::API GetAPI() const override { return RHI::API::None; }
    const RHI::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }
    Math::Matrix4x4 AdjustProjectionForClipSpace(const Math::Matrix4x4& projection, bool) const override
    {
        return projection;
    }

    RHI::DeviceCapabilities Capabilities;
    LedgerFakeTexture* LastTexture = nullptr;
};

TextureCreateInfo MakeInfo(uint32_t width, uint32_t height, uint32_t mipLevels, TextureCreateInfo::Format format)
{
    TextureCreateInfo info;
    info.Width = width;
    info.Height = height;
    info.MipLevels = mipLevels;
    info.PixelFormat = format;
    info.DebugName = "LedgerTexture";
    return info;
}

RHI::TextureDesc MakeDesc(uint32_t width, uint32_t height, uint32_t mipLevels, RHI::Format format)
{
    RHI::TextureDesc desc;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mipLevels;
    desc.TextureFormat = format;
    return desc;
}

void TestEstimate()
{
    // RGBA8 4096x4096 の全ミップ(13段) = 4 * (4^13 - 1) / 3。
    Expect(RHI::EstimateTextureSize(MakeDesc(4096, 4096, 13, RHI::Format::R8G8B8A8_UNORM)) == 89478484ull,
           "RGBA8 4096x4096 の全ミップは 89,478,484 バイトでなければならない");
    // 1x1 は1画素だけ。
    Expect(RHI::EstimateTextureSize(MakeDesc(1, 1, 1, RHI::Format::R8G8B8A8_UNORM)) == 4ull,
           "RGBA8 1x1 は 4 バイトでなければならない");
    // ミップ数が足りなくても、必要段より多く指定しても、1x1 で打ち切らず最小 1 を保つ。
    Expect(RHI::EstimateTextureSize(MakeDesc(2, 2, 4, RHI::Format::R8G8B8A8_UNORM)) == 16 + 4 + 4 + 4,
           "1x1 を過ぎたミップも 1 画素として数え続ける");
    // 単一ミップ・形式ごとの1画素のバイト数。
    Expect(RHI::EstimateTextureSize(MakeDesc(8, 4, 1, RHI::Format::R16G16B16A16_FLOAT)) == 8ull * 4 * 8,
           "RGBA16F 8x4 の単一ミップ");
    Expect(RHI::EstimateTextureSize(MakeDesc(8, 4, 1, RHI::Format::R8_UNORM)) == 8ull * 4,
           "R8 8x4 の単一ミップ");
    // 配列数は掛ける。
    RHI::TextureDesc array = MakeDesc(4, 4, 1, RHI::Format::R8G8B8A8_UNORM);
    array.ArraySize = 6;
    Expect(RHI::EstimateTextureSize(array) == 4ull * 4 * 4 * 6, "配列数が掛け合わされる");
}

void TestStoreLedger()
{
    RenderResources manager;
    auto device = MakeShared<LedgerFakeDevice>();
    Expect(manager.Initialize(device), "偽デバイスで RenderResources が初期化できなければならない");

    Expect(manager.GetResourceStats().TextureBytes == 0, "空のストアは 0 バイトを数える");

    const TextureHandle big =
        manager.Textures().CreateTexture(MakeInfo(4096, 4096, 13, TextureCreateInfo::Format::RGBA8_UNORM));
    Expect(big.IsValid(), "4096x4096 のテクスチャが作成できなければならない");
    Expect(manager.GetResourceStats().TextureBytes == 89478484ull, "ストアは全ミップ分を数えなければならない");
    Expect(manager.GetResourceStats().TotalTextureMemory == 89478484ull, "TotalTextureMemory は TextureBytes と同値になる");

    const TextureHandle tiny =
        manager.Textures().CreateTexture(MakeInfo(1, 1, 1, TextureCreateInfo::Format::RGBA8_UNORM));
    Expect(tiny.IsValid(), "1x1 のテクスチャが作成できなければならない");
    Expect(manager.GetResourceStats().TextureBytes == 89478484ull + 4ull, "1x1 は 4 バイトを足す");
    Expect(manager.GetResourceStats().TextureCount == 2, "2 枚のテクスチャが数えられる");

    // 作成→解放で合計が戻る。
    manager.Textures().ReleaseTexture(tiny);
    Expect(manager.GetResourceStats().TextureBytes == 89478484ull, "1x1 を解放すると合計が戻る");
    manager.Textures().ReleaseTexture(big);
    Expect(manager.GetResourceStats().TextureBytes == 0, "全て解放すると 0 バイトに戻る");

    // 外部登録のテクスチャ（スワップチェーン等）は所有しないので数えない。
    auto external = MakeShared<LedgerFakeTexture>(MakeDesc(1920, 1080, 1, RHI::Format::R8G8B8A8_UNORM));
    const TextureHandle externalHandle = manager.Textures().RegisterExternalTexture(external, "ExternalTexture");
    Expect(externalHandle.IsValid(), "外部テクスチャが登録できなければならない");
    Expect(manager.GetResourceStats().TextureBytes == 0, "外部テクスチャは数えない");
    manager.Textures().ReleaseTexture(externalHandle);

    manager.Shutdown();
}

void TestSparseLedger()
{
    RenderResources manager;
    auto device = MakeShared<LedgerFakeDevice>();
    Expect(manager.Initialize(device), "偽デバイスで RenderResources が初期化できなければならない");

    TextureCreateInfo sparseInfo = MakeInfo(4096, 4096, 13, TextureCreateInfo::Format::BC7_UNORM);
    sparseInfo.bSparse = true;

    // sparse に対応しないデバイスでは作成が失敗し、台帳にも載らない。
    Expect(!manager.Textures().CreateTexture(sparseInfo).IsValid(), "sparse に対応しないデバイスでは作成が失敗しなければならない");
    Expect(manager.GetResourceStats().TextureCount == 0, "失敗した作成はテクスチャに数えない");

    device->Capabilities.bTextureCompressionBC = true;
    RHI::SparseCapabilities& sparse = device->Capabilities.Sparse;
    sparse.bSparseBinding = true;
    sparse.bResidencyImage2D = true;
    sparse.FormatCount = 1;
    sparse.Formats[0].TextureFormat = RHI::Format::BC7_UNORM;
    sparse.Formats[0].bSupported = true;
    sparse.Formats[0].bStandardBlockShape = true;
    sparse.Formats[0].GranularityWidth = 256;
    sparse.Formats[0].GranularityHeight = 256;

    // 形式・用途・初期データが sparse に合わないものは断る。
    TextureCreateInfo unsupportedFormat = sparseInfo;
    unsupportedFormat.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
    Expect(!manager.Textures().CreateTexture(unsupportedFormat).IsValid(), "標準ブロック形状を照会していない形式の sparse は断らなければならない");
    TextureCreateInfo arrayInfo = sparseInfo;
    arrayInfo.ArraySize = 2;
    Expect(!manager.Textures().CreateTexture(arrayInfo).IsValid(), "配列の sparse は断らなければならない");
    TextureCreateInfo renderTargetInfo = sparseInfo;
    renderTargetInfo.bRenderTarget = true;
    Expect(!manager.Textures().CreateTexture(renderTargetInfo).IsValid(), "レンダーターゲットの sparse は断らなければならない");
    const uint8_t initialData[16] = {};
    Expect(!manager.Textures().CreateTexture(sparseInfo, initialData, sizeof(initialData)).IsValid(),
           "sparse に初期データは渡せない");
    Expect(manager.GetResourceStats().TextureCount == 0, "断られた作成はテクスチャに数えない");

    // 作成した時点では何も結んでいないので 0 バイト。結んだ量が台帳に載り、外すと戻る。
    const TextureHandle handle = manager.Textures().CreateTexture(sparseInfo);
    Expect(handle.IsValid(), "sparse に対応するデバイスでは作成できなければならない");
    Expect(device->LastTexture != nullptr && device->LastTexture->Desc.bSparse, "RHI へ sparse の印が渡る");
    Expect(manager.GetResourceStats().TextureCount == 1, "sparse のテクスチャも枚数には数える");
    Expect(manager.GetResourceStats().TextureBytes == 0, "結んでいない sparse は 0 バイトを数える");

    if (device->LastTexture != nullptr)
    {
        device->LastTexture->BoundBytes = 3ull * 65536;
    }
    Expect(manager.GetResourceStats().TextureBytes == 3ull * 65536, "結んだ量（3 ページ）が台帳に載る");

    // 通常のテクスチャと並べても、sparse は結んだ量だけを足す。
    const TextureHandle plain =
        manager.Textures().CreateTexture(MakeInfo(1, 1, 1, TextureCreateInfo::Format::RGBA8_UNORM));
    Expect(plain.IsValid(), "通常の 1x1 が作成できなければならない");
    Expect(manager.GetResourceStats().TextureBytes == 3ull * 65536 + 4ull, "通常は全量、sparse は結んだ量を足す");

    manager.Textures().ReleaseTexture(plain);
    manager.Textures().ReleaseTexture(handle);
    Expect(manager.GetResourceStats().TextureBytes == 0, "全て解放すると 0 バイトに戻る");

    manager.Shutdown();
}

int RunTest()
{
    TestEstimate();
    TestStoreLedger();
    TestSparseLedger();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "TextureMemoryLedgerTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
