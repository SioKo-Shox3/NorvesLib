#include "Rendering/RenderResourceManager.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::MakeShared;

namespace
{
    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc &desc)
            : Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return Desc.Width; }
        uint32_t GetHeight() const override { return Desc.Height; }
        uint32_t GetDepth() const override { return Desc.Depth; }
        uint32_t GetMipLevels() const override { return Desc.MipLevels; }
        uint32_t GetArraySize() const override { return Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
        bool IsCubemap() const override { return Desc.IsCubemap; }

        void Update(const void *data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

        NorvesLib::RHI::TextureDesc Desc;
    };

    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const NorvesLib::RHI::BufferDesc &desc)
            : Desc(desc),
              Bytes(static_cast<size_t>(desc.Size))
        {
        }

        uint64_t GetSize() const override { return Desc.Size; }

        void *Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < Bytes.size() ? Bytes.data() + offset : nullptr;
        }

        void Unmap() override {}

        void Update(const void *data, uint64_t size, uint64_t offset = 0) override
        {
            LastUpdateSize = size;
            LastUpdateOffset = offset;
            if (data == nullptr || offset + size > Bytes.size())
            {
                return;
            }

            std::memcpy(Bytes.data() + offset, data, static_cast<size_t>(size));
        }

        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }

        NorvesLib::RHI::BufferDesc Desc;
        std::vector<uint8_t> Bytes;
        uint64_t LastUpdateSize = 0;
        uint64_t LastUpdateOffset = 0;
    };

    class FakeSampler final : public NorvesLib::RHI::ISampler
    {
    public:
        explicit FakeSampler(const NorvesLib::RHI::SamplerDesc &desc)
            : Desc(desc)
        {
        }

        NorvesLib::RHI::FilterMode GetFilterMin() const override { return Desc.filterMin; }
        NorvesLib::RHI::FilterMode GetFilterMag() const override { return Desc.filterMag; }
        NorvesLib::RHI::FilterMode GetFilterMip() const override { return Desc.filterMip; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeU() const override { return Desc.addressU; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeV() const override { return Desc.addressV; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeW() const override { return Desc.addressW; }
        uint32_t GetMaxAnisotropy() const override { return Desc.maxAnisotropy; }
        NorvesLib::RHI::CompareFunc GetCompareFunc() const override { return Desc.compareFunc; }

        NorvesLib::RHI::SamplerDesc Desc;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc &desc) override
        {
            CreatedBufferDescs.push_back(desc);
            LastBuffer = MakeShared<FakeBuffer>(desc);
            return LastBuffer;
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc &desc) override
        {
            CreatedTextureDescs.push_back(desc);
            LastTexture = MakeShared<FakeTexture>(desc);
            return LastTexture;
        }

        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc &desc) override
        {
            CreatedSamplerDescs.push_back(desc);
            LastSampler = MakeShared<FakeSampler>(desc);
            return LastSampler;
        }

        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc &) override { return {}; }
        NorvesLib::RHI::CommandListPtr CreateCommandList() override { return {}; }
        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc &) override { return {}; }
        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc &) override { return {}; }
        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc &) override { return {}; }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc &) override { return {}; }
        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        void WaitIdle() override {}
        NorvesLib::RHI::API GetAPI() const override { return NorvesLib::RHI::API::None; }
        const NorvesLib::RHI::DeviceCapabilities &GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4 &projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
        std::vector<NorvesLib::RHI::BufferDesc> CreatedBufferDescs;
        std::vector<NorvesLib::RHI::TextureDesc> CreatedTextureDescs;
        std::vector<NorvesLib::RHI::SamplerDesc> CreatedSamplerDescs;
        NorvesLib::Core::Container::TSharedPtr<FakeBuffer> LastBuffer;
        NorvesLib::Core::Container::TSharedPtr<FakeTexture> LastTexture;
        NorvesLib::Core::Container::TSharedPtr<FakeSampler> LastSampler;
    };

    TextureCreateInfo MakeTextureCreateInfo(const char *debugName)
    {
        TextureCreateInfo createInfo;
        createInfo.Width = 4;
        createInfo.Height = 2;
        createInfo.MipLevels = 1;
        createInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        createInfo.DebugName = debugName;
        return createInfo;
    }

    NorvesLib::RHI::TextureDesc MakeExternalTextureDesc()
    {
        NorvesLib::RHI::TextureDesc desc;
        desc.Width = 8;
        desc.Height = 4;
        desc.Depth = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.TextureFormat = NorvesLib::RHI::Format::R8G8B8A8_UNORM;
        desc.Usage = NorvesLib::RHI::ResourceUsage::ShaderRead;
        desc.DebugName = "ExternalTexture";
        return desc;
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "RenderResourceManagerResourceContractTest start\n";

    RenderResourceManager manager;
    const TextureCreateInfo createInfo = MakeTextureCreateInfo("OwnedTexture");
    const TextureHandle preInitializeHandle = manager.CreateTexture(createInfo);
    assert(!preInitializeHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 0);

    auto device = MakeShared<FakeDevice>();
    assert(manager.Initialize(device));

    BufferCreateInfo bufferInfo;
    bufferInfo.Size = 64;
    bufferInfo.bHostVisible = true;
    bufferInfo.UsageType = BufferCreateInfo::Usage::Vertex;
    bufferInfo.DebugName = "ContractBuffer";

    const uint32_t bufferData[4] = {1, 2, 3, 4};
    const BufferHandle bufferHandle = manager.CreateBuffer(bufferInfo, bufferData, sizeof(bufferData));
    assert(bufferHandle.IsValid());
    assert(device->CreatedBufferDescs.size() == 1);
    assert(device->CreatedBufferDescs[0].Size == bufferInfo.Size);
    assert(device->CreatedBufferDescs[0].Usage == NorvesLib::RHI::ResourceUsage::VertexBuffer);
    assert(manager.GetResourceStats().BufferCount == 1);
    assert(manager.GetResourceStats().TotalBufferMemory == bufferInfo.Size);
    assert(manager.GetRHIBuffer(bufferHandle) == device->LastBuffer.get());
    assert(device->LastBuffer->LastUpdateSize == sizeof(bufferData));

    const uint32_t updatedData[2] = {7, 8};
    assert(manager.UpdateBuffer(bufferHandle, updatedData, sizeof(updatedData)));
    assert(device->LastBuffer->LastUpdateSize == sizeof(updatedData));

    manager.ReleaseBuffer(BufferHandle::Invalid());
    assert(manager.GetResourceStats().BufferCount == 1);
    manager.ReleaseBuffer(bufferHandle);
    assert(manager.GetResourceStats().BufferCount == 0);
    assert(manager.GetRHIBuffer(bufferHandle) == nullptr);

    const SamplerHandle defaultSampler = manager.GetDefaultSampler();
    assert(defaultSampler.IsValid());
    assert(device->CreatedSamplerDescs.size() == 1);
    assert(device->CreatedSamplerDescs[0].filterMin == NorvesLib::RHI::FilterMode::Anisotropic);
    assert(manager.GetResourceStats().SamplerCount == 1);
    assert(manager.GetDefaultSampler().Id == defaultSampler.Id);
    assert(device->CreatedSamplerDescs.size() == 1);

    const SamplerHandle pointSampler = manager.GetPointSampler();
    assert(pointSampler.IsValid());
    assert(device->CreatedSamplerDescs.size() == 2);
    assert(device->CreatedSamplerDescs[1].filterMin == NorvesLib::RHI::FilterMode::Point);
    assert(manager.GetResourceStats().SamplerCount == 2);
    manager.ReleaseSampler(defaultSampler);
    assert(manager.GetResourceStats().SamplerCount == 1);
    manager.ReleaseSampler(pointSampler);
    assert(manager.GetResourceStats().SamplerCount == 0);

    VertexLayout layout = VertexLayout::CreateStandard();
    const VertexLayoutHandle layoutHandle = manager.RegisterVertexLayout(layout);
    assert(layoutHandle.IsValid());
    const VertexLayout *registeredLayout = manager.GetVertexLayout(layoutHandle);
    assert(registeredLayout != nullptr);
    assert(registeredLayout->Stride == layout.Stride);
    assert(registeredLayout->HasSemantic(VertexSemantic::Position));
    assert(registeredLayout->HasSemantic(VertexSemantic::Normal));
    assert(manager.GetVertexLayout(VertexLayoutHandle::Invalid()) == nullptr);

    const TextureHandle ownedHandle = manager.CreateTexture(createInfo);
    assert(ownedHandle.IsValid());
    assert(device->CreatedTextureDescs.size() == 1);
    assert(manager.GetResourceStats().TextureCount == 1);

    NorvesLib::RHI::ITexture *ownedRaw = manager.GetRHITexture(ownedHandle);
    auto ownedShared = manager.GetRHITexturePtr(ownedHandle);
    assert(ownedRaw != nullptr);
    assert(ownedShared);
    assert(ownedShared.get() == ownedRaw);
    assert(ownedRaw == device->LastTexture.get());

    manager.ReleaseTexture(TextureHandle::Invalid());
    assert(manager.GetResourceStats().TextureCount == 1);

    manager.ReleaseTexture(ownedHandle);
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.GetRHITexture(ownedHandle) == nullptr);
    assert(!manager.GetRHITexturePtr(ownedHandle));

    auto externalTexture = MakeShared<FakeTexture>(MakeExternalTextureDesc());
    const TextureHandle externalHandle = manager.RegisterExternalTexture(externalTexture, "ExternalTexture");
    assert(externalHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 1);
    assert(manager.GetRHITexture(externalHandle) == externalTexture.get());
    assert(manager.GetRHITexturePtr(externalHandle).get() == externalTexture.get());

    manager.ReleaseTexture(externalHandle);
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.GetRHITexture(externalHandle) == nullptr);

    const TextureHandle shutdownHandle = manager.CreateTexture(createInfo);
    assert(shutdownHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 1);
    manager.Shutdown();
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.GetRHITexture(shutdownHandle) == nullptr);
    assert(!manager.GetRHITexturePtr(shutdownHandle));

    std::cout << "RenderResourceManagerResourceContractTest passed\n";
    return 0;
}
