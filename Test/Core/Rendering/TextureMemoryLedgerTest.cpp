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
        std::cerr << "TextureMemoryLedgerTest failed: " << message << std::endl;
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

    RHI::TextureDesc Desc;
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
        return MakeShared<LedgerFakeTexture>(desc);
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
           "RGBA8 4096x4096 full mip chain must be 89,478,484 bytes");
    // 1x1 は1画素だけ。
    Expect(RHI::EstimateTextureSize(MakeDesc(1, 1, 1, RHI::Format::R8G8B8A8_UNORM)) == 4ull,
           "RGBA8 1x1 must be 4 bytes");
    // ミップ数が足りなくても、必要段より多く指定しても、1x1 で打ち切らず最小 1 を保つ。
    Expect(RHI::EstimateTextureSize(MakeDesc(2, 2, 4, RHI::Format::R8G8B8A8_UNORM)) == 16 + 4 + 4 + 4,
           "mips past 1x1 keep counting 1 texel");
    // 単一ミップ・形式ごとの1画素のバイト数。
    Expect(RHI::EstimateTextureSize(MakeDesc(8, 4, 1, RHI::Format::R16G16B16A16_FLOAT)) == 8ull * 4 * 8,
           "RGBA16F 8x4 single mip");
    Expect(RHI::EstimateTextureSize(MakeDesc(8, 4, 1, RHI::Format::R8_UNORM)) == 8ull * 4,
           "R8 8x4 single mip");
    // 配列数は掛ける。
    RHI::TextureDesc array = MakeDesc(4, 4, 1, RHI::Format::R8G8B8A8_UNORM);
    array.ArraySize = 6;
    Expect(RHI::EstimateTextureSize(array) == 4ull * 4 * 4 * 6, "array size multiplies");
}

void TestStoreLedger()
{
    RenderResources manager;
    auto device = MakeShared<LedgerFakeDevice>();
    Expect(manager.Initialize(device), "RenderResources must initialize with the fake device");

    Expect(manager.GetResourceStats().TextureBytes == 0, "empty store must count 0 bytes");

    const TextureHandle big =
        manager.Textures().CreateTexture(MakeInfo(4096, 4096, 13, TextureCreateInfo::Format::RGBA8_UNORM));
    Expect(big.IsValid(), "4096x4096 texture must be created");
    Expect(manager.GetResourceStats().TextureBytes == 89478484ull, "store must count the full mip chain");
    Expect(manager.GetResourceStats().TotalTextureMemory == 89478484ull, "TotalTextureMemory mirrors TextureBytes");

    const TextureHandle tiny =
        manager.Textures().CreateTexture(MakeInfo(1, 1, 1, TextureCreateInfo::Format::RGBA8_UNORM));
    Expect(tiny.IsValid(), "1x1 texture must be created");
    Expect(manager.GetResourceStats().TextureBytes == 89478484ull + 4ull, "1x1 adds 4 bytes");
    Expect(manager.GetResourceStats().TextureCount == 2, "two textures are counted");

    // 作成→解放で合計が戻る。
    manager.Textures().ReleaseTexture(tiny);
    Expect(manager.GetResourceStats().TextureBytes == 89478484ull, "releasing the 1x1 texture restores the total");
    manager.Textures().ReleaseTexture(big);
    Expect(manager.GetResourceStats().TextureBytes == 0, "releasing every texture returns to 0 bytes");

    // 外部登録のテクスチャ（スワップチェーン等）は所有しないので数えない。
    auto external = MakeShared<LedgerFakeTexture>(MakeDesc(1920, 1080, 1, RHI::Format::R8G8B8A8_UNORM));
    const TextureHandle externalHandle = manager.Textures().RegisterExternalTexture(external, "ExternalTexture");
    Expect(externalHandle.IsValid(), "external texture must register");
    Expect(manager.GetResourceStats().TextureBytes == 0, "external textures must not be counted");
    manager.Textures().ReleaseTexture(externalHandle);

    manager.Shutdown();
}

int RunTest()
{
    TestEstimate();
    TestStoreLedger();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "TextureMemoryLedgerTest passed" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
