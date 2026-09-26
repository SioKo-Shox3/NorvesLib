#include "Rendering/RenderResources.h"
#include "Rendering/ITextureHandleRegistrar.h"
#include "Rendering/NeuralMaterialResource.h"
#include "Resource/MaterialResource.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
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
        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
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

    class FakeRegistrar final : public ITextureHandleRegistrar
    {
    public:
        TextureHandle RegisterExternalTexture(
            NorvesLib::Core::Container::TSharedPtr<NorvesLib::RHI::ITexture> rhiTexture,
            const NorvesLib::Core::Container::String &debugName = NorvesLib::Core::Container::String()) override
        {
            (void)debugName;
            ++RegisterCalls;
            if (!rhiTexture || (FailOnRegisterCall != 0 && RegisterCalls == FailOnRegisterCall))
            {
                return TextureHandle::Invalid();
            }

            TextureHandle handle = MakeTextureHandle(NextHandleId++);
            RegisteredHandles.push_back(handle);
            ActiveHandles.push_back(handle);
            return handle;
        }

        void ReleaseTexture(TextureHandle handle) override
        {
            ReleasedHandles.push_back(handle);
            for (auto it = ActiveHandles.begin(); it != ActiveHandles.end(); ++it)
            {
                if (it->Id == handle.Id)
                {
                    ActiveHandles.erase(it);
                    return;
                }
            }
        }

        size_t CountReleased(TextureHandle handle) const
        {
            size_t count = 0;
            for (TextureHandle releasedHandle : ReleasedHandles)
            {
                if (releasedHandle.Id == handle.Id)
                {
                    ++count;
                }
            }
            return count;
        }

        size_t RegisterCalls = 0;
        size_t FailOnRegisterCall = 0;
        uint64_t NextHandleId = 1000;
        std::vector<TextureHandle> RegisteredHandles;
        std::vector<TextureHandle> ReleasedHandles;
        std::vector<TextureHandle> ActiveHandles;
    };

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
        createInfo.EmissiveColor[0] = 1.0f;
        createInfo.EmissiveColor[1] = 1.0f;
        createInfo.EmissiveColor[2] = 1.0f;
        createInfo.EmissiveLuminanceNits = 2.0f;
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
        assert(data->EmissiveLuminanceNits == expected.EmissiveLuminanceNits);
        assert(data->Blend == expected.Blend);
        assert(data->Shading == expected.Shading);
        assert(data->bTwoSided == expected.bTwoSided);
        assert(data->bCastShadows == expected.bCastShadows);
        assert(data->RefCount == 1);
        assert(data->DebugName == expected.DebugName);
    }

    struct InvalidEmissiveRow
    {
        const char *Name;
        float Red;
        float Green;
        float Blue;
        float LuminanceNits;
    };

    const InvalidEmissiveRow InvalidEmissiveRows[] = {
        {"nan rgb", std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f},
        {"inf rgb", 0.0f, std::numeric_limits<float>::infinity(), 0.0f, 1.0f},
        {"nan nits", 0.0f, 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()},
        {"inf nits", 0.0f, 0.0f, 0.0f, std::numeric_limits<float>::infinity()},
        {"negative rgb", -0.25f, 0.0f, 0.0f, 1.0f},
        {"negative nits", 0.25f, 0.25f, 0.25f, -1.0f},
        {"positive nits with tiny Y", 0.000001f, 0.0f, 0.0f, 1.0f},
        {"rgba16f product overflow", 1.0f, 0.0f, 0.0f, 20000.0f},
    };

    void SetInvalidEmissive(MaterialCreateData &createInfo, const InvalidEmissiveRow &row)
    {
        createInfo.EmissiveColor[0] = row.Red;
        createInfo.EmissiveColor[1] = row.Green;
        createInfo.EmissiveColor[2] = row.Blue;
        createInfo.EmissiveLuminanceNits = row.LuminanceNits;
    }

    void TestMaterialCreateRejectsInvalidEmissiveWithoutSideEffects()
    {
        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));

        MaterialCreateData validZero = MakeMaterialCreateData("ValidZero", 100);
        validZero.EmissiveColor[0] = 0.0f;
        validZero.EmissiveColor[1] = 0.0f;
        validZero.EmissiveColor[2] = 0.0f;
        validZero.EmissiveLuminanceNits = 0.0f;

        const MaterialHandle firstHandle = manager.Materials().Create(validZero);
        assert(firstHandle.IsValid());
        const MaterialResourceData *firstData = manager.Materials().GetData(firstHandle);
        AssertMaterialDataMatches(firstData, validZero);

        const ResourceStats statsBefore = manager.GetResourceStats();
        const size_t createdBufferCountBefore = device->CreatedBufferDescs.size();
        const size_t createdTextureCountBefore = device->CreatedTextureDescs.size();

        for (const InvalidEmissiveRow &row : InvalidEmissiveRows)
        {
            MaterialCreateData invalid = validZero;
            SetInvalidEmissive(invalid, row);

            const MaterialHandle rejectedHandle = manager.Materials().Create(invalid);
            assert(!rejectedHandle.IsValid());
            assert(manager.Materials().GetData(rejectedHandle) == nullptr);
            assert(manager.Materials().GetData(firstHandle) == firstData);
            AssertMaterialDataMatches(manager.Materials().GetData(firstHandle), validZero);

            const ResourceStats statsAfter = manager.GetResourceStats();
            assert(statsAfter.BufferCount == statsBefore.BufferCount);
            assert(statsAfter.TextureCount == statsBefore.TextureCount);
            assert(statsAfter.ShaderCount == statsBefore.ShaderCount);
            assert(statsAfter.SamplerCount == statsBefore.SamplerCount);
            assert(statsAfter.TotalBufferMemory == statsBefore.TotalBufferMemory);
            assert(statsAfter.TotalTextureMemory == statsBefore.TotalTextureMemory);
            assert(device->CreatedBufferDescs.size() == createdBufferCountBefore);
            assert(device->CreatedTextureDescs.size() == createdTextureCountBefore);
        }

        const MaterialHandle nextHandle = manager.Materials().Create(validZero);
        assert(nextHandle.IsValid());
        assert(nextHandle.Id == firstHandle.Id + 1);
    }

    void TestMaterialUpdateRejectsInvalidEmissiveWithoutSideEffects()
    {
        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));

        MaterialCreateData validZero = MakeMaterialCreateData("UpdateBaseline", 200);
        validZero.EmissiveColor[0] = 0.0f;
        validZero.EmissiveColor[1] = 0.0f;
        validZero.EmissiveColor[2] = 0.0f;
        validZero.EmissiveLuminanceNits = 0.0f;

        const MaterialHandle handle = manager.Materials().Create(validZero);
        assert(handle.IsValid());
        const MaterialResourceData *beforeData = manager.Materials().GetData(handle);
        AssertMaterialDataMatches(beforeData, validZero);

        const ResourceStats statsBefore = manager.GetResourceStats();
        const size_t createdBufferCountBefore = device->CreatedBufferDescs.size();
        const size_t createdTextureCountBefore = device->CreatedTextureDescs.size();

        for (const InvalidEmissiveRow &row : InvalidEmissiveRows)
        {
            MaterialCreateData invalid = validZero;
            SetInvalidEmissive(invalid, row);

            assert(!manager.Materials().Update(handle, invalid));
            assert(manager.Materials().GetData(handle) == beforeData);
            AssertMaterialDataMatches(manager.Materials().GetData(handle), validZero);

            const ResourceStats statsAfter = manager.GetResourceStats();
            assert(statsAfter.BufferCount == statsBefore.BufferCount);
            assert(statsAfter.TextureCount == statsBefore.TextureCount);
            assert(statsAfter.ShaderCount == statsBefore.ShaderCount);
            assert(statsAfter.SamplerCount == statsBefore.SamplerCount);
            assert(statsAfter.TotalBufferMemory == statsBefore.TotalBufferMemory);
            assert(statsAfter.TotalTextureMemory == statsBefore.TotalTextureMemory);
            assert(device->CreatedBufferDescs.size() == createdBufferCountBefore);
            assert(device->CreatedTextureDescs.size() == createdTextureCountBefore);
        }

        MaterialCreateData positive = validZero;
        positive.EmissiveColor[0] = 1.0f;
        positive.EmissiveColor[1] = 0.0f;
        positive.EmissiveColor[2] = 0.0f;
        positive.EmissiveLuminanceNits = 2.0f;
        assert(manager.Materials().Update(handle, positive));

        const MaterialResourceData *updatedData = manager.Materials().GetData(handle);
        assert(updatedData != nullptr);
        const float expectedCanonicalRed = 4.7036686f;
        assert(std::isfinite(updatedData->EmissiveColor[0]));
        assert(std::abs(updatedData->EmissiveColor[0] - expectedCanonicalRed) < 0.00001f);
        assert(updatedData->EmissiveColor[1] == 0.0f);
        assert(updatedData->EmissiveColor[2] == 0.0f);
        assert(updatedData->EmissiveLuminanceNits == 2.0f);
    }

    void TestMaterialCreateCanonicalizesPositiveEmissive()
    {
        RenderResources manager;

        MaterialCreateData createInfo = MakeMaterialCreateData("CanonicalCreate", 300);
        createInfo.EmissiveColor[0] = 1.0f;
        createInfo.EmissiveColor[1] = 0.0f;
        createInfo.EmissiveColor[2] = 0.0f;
        createInfo.EmissiveLuminanceNits = 2.0f;

        const MaterialHandle handle = manager.Materials().Create(createInfo);
        assert(handle.IsValid());
        const MaterialResourceData *data = manager.Materials().GetData(handle);
        assert(data != nullptr);

        const float expectedCanonicalRed = 4.7036686f;
        assert(std::isfinite(data->EmissiveColor[0]));
        assert(std::abs(data->EmissiveColor[0] - expectedCanonicalRed) < 0.00001f);
        assert(data->EmissiveColor[1] == 0.0f);
        assert(data->EmissiveColor[2] == 0.0f);
        assert(data->EmissiveLuminanceNits == 2.0f);
    }

    void TestMaterialResourceLoadRejectsInvalidEmissiveWithoutSideEffects()
    {
        for (const InvalidEmissiveRow &row : InvalidEmissiveRows)
        {
            NorvesLib::Core::MaterialResource resource;
            resource.Initialize();
            resource.SetEmissiveColor(row.Red, row.Green, row.Blue);
            resource.SetEmissiveLuminanceNits(row.LuminanceNits);

            const NorvesLib::Core::ResourceState stateBefore = resource.GetResourceState();
            float redBefore = 0.0f;
            float greenBefore = 0.0f;
            float blueBefore = 0.0f;
            resource.GetEmissiveColor(redBefore, greenBefore, blueBefore);
            const float nitsBefore = resource.GetEmissiveLuminanceNits();

            assert(!resource.Load());
            assert(resource.GetResourceState() == stateBefore);

            float redAfter = 0.0f;
            float greenAfter = 0.0f;
            float blueAfter = 0.0f;
            resource.GetEmissiveColor(redAfter, greenAfter, blueAfter);
            assert((std::isnan(redBefore) && std::isnan(redAfter)) || redAfter == redBefore);
            assert((std::isnan(greenBefore) && std::isnan(greenAfter)) || greenAfter == greenBefore);
            assert((std::isnan(blueBefore) && std::isnan(blueAfter)) || blueAfter == blueBefore);
            assert((std::isnan(nitsBefore) && std::isnan(resource.GetEmissiveLuminanceNits())) ||
                   resource.GetEmissiveLuminanceNits() == nitsBefore);
        }
    }

    void TestMaterialResourceLoadCanonicalizesPositiveEmissive()
    {
        NorvesLib::Core::MaterialResource resource;
        resource.Initialize();
        resource.SetEmissiveColor(1.0f, 0.0f, 0.0f);
        resource.SetEmissiveLuminanceNits(2.0f);

        assert(resource.Load());
        assert(resource.IsLoaded());

        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        resource.GetEmissiveColor(red, green, blue);
        const float expectedCanonicalRed = 4.7036686f;
        assert(std::isfinite(red));
        assert(std::abs(red - expectedCanonicalRed) < 0.00001f);
        assert(green == 0.0f);
        assert(blue == 0.0f);
        assert(resource.GetEmissiveLuminanceNits() == 2.0f);
    }

    void TestNeuralMaterialPartialRegistrationFailure()
    {
        auto device = MakeShared<FakeDevice>();
        NeuralMaterialDesc desc = NeuralMaterialDesc::DefaultPBR(8, 8);
        desc.DebugName = "PartialRegistrationFailure";

        NeuralMaterialResource resource;
        assert(resource.Initialize(device.get(), desc));

        FakeRegistrar registrar;
        registrar.FailOnRegisterCall = 2;
        assert(!resource.RegisterOutputTextures(registrar));
        assert(registrar.RegisterCalls == 2);
        assert(registrar.RegisteredHandles.size() == 1);
        assert(registrar.ReleasedHandles.size() == 1);
        assert(registrar.CountReleased(registrar.RegisteredHandles[0]) == 1);
        assert(registrar.ActiveHandles.empty());

        for (uint32_t slotIndex = 0; slotIndex < resource.GetOutputSlotCount(); ++slotIndex)
        {
            assert(!resource.GetOutputTextureHandle(slotIndex).IsValid());
        }

        resource.ReleaseOutputTextures(registrar);
        assert(registrar.ReleasedHandles.size() == 1);
        resource.Shutdown();
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "MaterialResourcesTest start\n";

    TestNeuralMaterialPartialRegistrationFailure();
    TestMaterialCreateRejectsInvalidEmissiveWithoutSideEffects();
    TestMaterialUpdateRejectsInvalidEmissiveWithoutSideEffects();
    TestMaterialCreateCanonicalizesPositiveEmissive();
    TestMaterialResourceLoadRejectsInvalidEmissiveWithoutSideEffects();
    TestMaterialResourceLoadCanonicalizesPositiveEmissive();

    RenderResources manager;

    NeuralMaterialDesc preInitializeNeuralDesc = NeuralMaterialDesc::DefaultPBR(4, 4);
    preInitializeNeuralDesc.DebugName = "PreInitializeNeural";
    assert(!manager.Materials().CreateNeural(preInitializeNeuralDesc).IsValid());

    const MaterialCreateData initialMaterial = MakeMaterialCreateData("PlainInitial", 10);
    const MaterialHandle materialHandle = manager.Materials().Create(initialMaterial);
    assert(materialHandle.IsValid());
    AssertMaterialDataMatches(manager.Materials().GetData(materialHandle), initialMaterial);

    const MaterialCreateData updatedMaterial = MakeMaterialCreateData("PlainUpdated", 30);
    assert(manager.Materials().Update(materialHandle, updatedMaterial));
    AssertMaterialDataMatches(manager.Materials().GetData(materialHandle), updatedMaterial);

    assert(!manager.Materials().Update(MaterialHandle::Invalid(), updatedMaterial));
    manager.Materials().Release(MaterialHandle::Invalid());
    AssertMaterialDataMatches(manager.Materials().GetData(materialHandle), updatedMaterial);

    manager.Materials().Release(materialHandle);
    assert(manager.Materials().GetData(materialHandle) == nullptr);
    manager.Materials().Release(materialHandle);

    auto device = MakeShared<FakeDevice>();
    assert(manager.Initialize(device));

    NeuralMaterialDesc neuralDesc = NeuralMaterialDesc::DefaultPBR(16, 8);
    neuralDesc.DebugName = "NeuralStore";

    const MaterialHandle neuralHandle = manager.Materials().CreateNeural(neuralDesc);
    assert(neuralHandle.IsValid());
    const MaterialResourceData *neuralMaterialData = manager.Materials().GetData(neuralHandle);
    assert(neuralMaterialData != nullptr);
    assert(neuralMaterialData->DebugName == neuralDesc.DebugName);
    assert(neuralMaterialData->AlbedoTexture.IsValid());
    assert(neuralMaterialData->NormalTexture.IsValid());
    assert(device->CreatedBufferDescs.size() == 1);
    assert(device->CreatedTextureDescs.size() == neuralDesc.OutputSlots.size());
    assert(manager.GetResourceStats().TextureCount == neuralDesc.OutputSlots.size());

    auto neuralResources = manager.Materials().GetNeuralResources();
    assert(neuralResources.size() == 1);
    assert(neuralResources[0]->IsInitialized());
    assert(neuralResources[0]->GetOutputSlotCount() == neuralDesc.OutputSlots.size());
    assert(neuralResources[0]->GetOutputTextureHandle(0).Id == neuralMaterialData->AlbedoTexture.Id);
    assert(neuralResources[0]->GetOutputTextureHandle(1).Id == neuralMaterialData->NormalTexture.Id);

    manager.Materials().Release(neuralHandle);
    assert(manager.Materials().GetData(neuralHandle) == nullptr);
    assert(manager.Materials().GetNeuralResources().empty());
    assert(manager.GetResourceStats().TextureCount == 0);

    const MaterialHandle clearHandle = manager.Materials().CreateNeural(neuralDesc);
    assert(clearHandle.IsValid());
    assert(manager.Materials().GetData(clearHandle) != nullptr);
    assert(manager.Materials().GetNeuralResources().size() == 1);
    assert(manager.GetResourceStats().TextureCount == neuralDesc.OutputSlots.size());

    manager.ClearAllResources();
    assert(manager.Materials().GetData(clearHandle) == nullptr);
    assert(manager.Materials().GetNeuralResources().empty());
    assert(manager.GetResourceStats().TextureCount == 0);

    manager.Shutdown();

    std::cout << "MaterialResourcesTest passed\n";
    return 0;
}
