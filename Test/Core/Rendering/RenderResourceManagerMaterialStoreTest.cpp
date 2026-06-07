#include "Rendering/RenderResourceManager.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"

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
            const size_t byteOffset = static_cast<size_t>(offset);
            return byteOffset < Bytes.size() ? Bytes.data() + byteOffset : nullptr;
        }

        void Unmap() override {}

        void Update(const void *data, uint64_t size, uint64_t offset = 0) override
        {
            LastUpdateSize = size;
            LastUpdateOffset = offset;
            const size_t byteSize = static_cast<size_t>(size);
            const size_t byteOffset = static_cast<size_t>(offset);
            if (data == nullptr || byteOffset + byteSize > Bytes.size())
            {
                return;
            }

            std::memcpy(Bytes.data() + byteOffset, data, byteSize);
        }

        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }

        NorvesLib::RHI::BufferDesc Desc;
        std::vector<uint8_t> Bytes;
        uint64_t LastUpdateSize = 0;
        uint64_t LastUpdateOffset = 0;
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

        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc &) override { return {}; }
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
        NorvesLib::Core::Container::TSharedPtr<FakeBuffer> LastBuffer;
        NorvesLib::Core::Container::TSharedPtr<FakeTexture> LastTexture;
    };

    TextureHandle MakeTextureHandle(uint64_t id)
    {
        TextureHandle handle;
        handle.Id = id;
        return handle;
    }

    MaterialCreateData MakeMaterialCreateData(const char *debugName, uint64_t baseTextureId)
    {
        MaterialCreateData createInfo;
        createInfo.AlbedoTexture = MakeTextureHandle(baseTextureId + 1);
        createInfo.NormalTexture = MakeTextureHandle(baseTextureId + 2);
        createInfo.MetallicTexture = MakeTextureHandle(baseTextureId + 3);
        createInfo.RoughnessTexture = MakeTextureHandle(baseTextureId + 4);
        createInfo.AOTexture = MakeTextureHandle(baseTextureId + 5);
        createInfo.HeightTexture = MakeTextureHandle(baseTextureId + 6);
        createInfo.HeightScale = 0.08f;
        createInfo.EmissiveColor[0] = 0.25f;
        createInfo.EmissiveColor[1] = 0.5f;
        createInfo.EmissiveColor[2] = 0.75f;
        createInfo.EmissiveStrength = 2.0f;
        createInfo.Blend = BlendMode::Translucent;
        createInfo.Shading = ShadingModel::Unlit;
        createInfo.bTwoSided = true;
        createInfo.bCastShadows = false;
        createInfo.DebugName = debugName;
        return createInfo;
    }

    void AssertMaterialDataMatches(const MaterialResourceData *data, const MaterialCreateData &expected)
    {
        assert(data != nullptr);
        assert(data->AlbedoTexture.Id == expected.AlbedoTexture.Id);
        assert(data->NormalTexture.Id == expected.NormalTexture.Id);
        assert(data->MetallicTexture.Id == expected.MetallicTexture.Id);
        assert(data->RoughnessTexture.Id == expected.RoughnessTexture.Id);
        assert(data->AOTexture.Id == expected.AOTexture.Id);
        assert(data->HeightTexture.Id == expected.HeightTexture.Id);
        assert(data->HeightScale == expected.HeightScale);
        assert(data->EmissiveColor[0] == expected.EmissiveColor[0]);
        assert(data->EmissiveColor[1] == expected.EmissiveColor[1]);
        assert(data->EmissiveColor[2] == expected.EmissiveColor[2]);
        assert(data->EmissiveStrength == expected.EmissiveStrength);
        assert(data->Blend == expected.Blend);
        assert(data->Shading == expected.Shading);
        assert(data->bTwoSided == expected.bTwoSided);
        assert(data->bCastShadows == expected.bCastShadows);
        assert(data->RefCount == 1);
        assert(data->DebugName == expected.DebugName);
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "RenderResourceManagerMaterialStoreTest start\n";

    RenderResourceManager manager;

    NeuralMaterialDesc preInitializeNeuralDesc = NeuralMaterialDesc::DefaultPBR(4, 4);
    preInitializeNeuralDesc.DebugName = "PreInitializeNeural";
    assert(!manager.CreateNeuralMaterial(preInitializeNeuralDesc).IsValid());

    const MaterialCreateData initialMaterial = MakeMaterialCreateData("PlainInitial", 10);
    const MaterialHandle materialHandle = manager.CreateMaterial(initialMaterial);
    assert(materialHandle.IsValid());
    AssertMaterialDataMatches(manager.GetMaterialData(materialHandle), initialMaterial);

    const MaterialCreateData updatedMaterial = MakeMaterialCreateData("PlainUpdated", 30);
    assert(manager.UpdateMaterial(materialHandle, updatedMaterial));
    AssertMaterialDataMatches(manager.GetMaterialData(materialHandle), updatedMaterial);

    assert(!manager.UpdateMaterial(MaterialHandle::Invalid(), updatedMaterial));
    manager.ReleaseMaterial(MaterialHandle::Invalid());
    AssertMaterialDataMatches(manager.GetMaterialData(materialHandle), updatedMaterial);

    manager.ReleaseMaterial(materialHandle);
    assert(manager.GetMaterialData(materialHandle) == nullptr);
    manager.ReleaseMaterial(materialHandle);

    auto device = MakeShared<FakeDevice>();
    assert(manager.Initialize(device));

    NeuralMaterialDesc neuralDesc = NeuralMaterialDesc::DefaultPBR(16, 8);
    neuralDesc.DebugName = "NeuralStore";

    const MaterialHandle neuralHandle = manager.CreateNeuralMaterial(neuralDesc);
    assert(neuralHandle.IsValid());
    const MaterialResourceData *neuralMaterialData = manager.GetMaterialData(neuralHandle);
    assert(neuralMaterialData != nullptr);
    assert(neuralMaterialData->DebugName == neuralDesc.DebugName);
    assert(neuralMaterialData->AlbedoTexture.IsValid());
    assert(neuralMaterialData->NormalTexture.IsValid());
    assert(device->CreatedBufferDescs.size() == 1);
    assert(device->CreatedTextureDescs.size() == neuralDesc.OutputSlots.size());

    auto neuralResources = manager.GetNeuralMaterialResources();
    assert(neuralResources.size() == 1);
    assert(neuralResources[0]->IsInitialized());
    assert(neuralResources[0]->GetOutputSlotCount() == neuralDesc.OutputSlots.size());
    assert(neuralResources[0]->GetOutputTextureHandle(0).Id == neuralMaterialData->AlbedoTexture.Id);
    assert(neuralResources[0]->GetOutputTextureHandle(1).Id == neuralMaterialData->NormalTexture.Id);

    manager.ReleaseMaterial(neuralHandle);
    assert(manager.GetMaterialData(neuralHandle) == nullptr);
    assert(manager.GetNeuralMaterialResources().empty());

    const MaterialHandle clearHandle = manager.CreateNeuralMaterial(neuralDesc);
    assert(clearHandle.IsValid());
    assert(manager.GetMaterialData(clearHandle) != nullptr);
    assert(manager.GetNeuralMaterialResources().size() == 1);

    manager.ClearAllResources();
    assert(manager.GetMaterialData(clearHandle) == nullptr);
    assert(manager.GetNeuralMaterialResources().empty());
    assert(manager.GetResourceStats().TextureCount == 0);

    manager.Shutdown();

    std::cout << "RenderResourceManagerMaterialStoreTest passed\n";
    return 0;
}
