#include "Rendering/InstanceBufferRing.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"
#include "Container/Containers.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::DynamicPointerCast;
using NorvesLib::Core::Container::MakeShared;
using NorvesLib::Core::Container::TSharedPtr;
using NorvesLib::Core::Container::TWeakPtr;
using NorvesLib::Core::Container::VariableArray;

namespace
{
    bool HasUsage(NorvesLib::RHI::ResourceUsage value, NorvesLib::RHI::ResourceUsage flag)
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
    }

    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        FakeBuffer(const NorvesLib::RHI::BufferDesc& desc, uint32_t* destroyedCount)
            : Desc(desc),
              Bytes(static_cast<size_t>(desc.Size)),
              DestroyedCount(destroyedCount)
        {
        }

        ~FakeBuffer() override
        {
            if (DestroyedCount)
            {
                ++(*DestroyedCount);
            }
        }

        uint64_t GetSize() const override
        {
            return Desc.Size;
        }

        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            const size_t byteOffset = static_cast<size_t>(offset);
            return byteOffset < Bytes.size() ? Bytes.data() + byteOffset : nullptr;
        }

        void Unmap() override
        {
        }

        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            ++UpdateCount;
            LastUpdateSize = size;
            LastUpdateOffset = offset;

            const size_t byteSize = static_cast<size_t>(size);
            const size_t byteOffset = static_cast<size_t>(offset);
            if (!data || byteOffset + byteSize > Bytes.size())
            {
                return;
            }

            std::memcpy(Bytes.data() + byteOffset, data, byteSize);
        }

        NorvesLib::RHI::ResourceUsage GetUsage() const override
        {
            return Desc.Usage;
        }

        NorvesLib::RHI::BufferDesc Desc;
        VariableArray<uint8_t> Bytes;
        uint32_t UpdateCount = 0;
        uint64_t LastUpdateSize = 0;
        uint64_t LastUpdateOffset = 0;
        uint32_t* DestroyedCount = nullptr;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc& desc) override
        {
            CreatedBufferDescs.push_back(desc);
            if (FailBufferCreateIndex == CreatedBufferDescs.size())
            {
                return {};
            }

            return MakeShared<FakeBuffer>(desc, &DestroyedBufferCount);
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::CommandListPtr CreateCommandList() override
        {
            return {};
        }

        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return {};
        }

        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return nullptr;
        }

        void WaitIdle() override
        {
        }

        NorvesLib::RHI::API GetAPI() const override
        {
            return NorvesLib::RHI::API::None;
        }

        const NorvesLib::RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return Capabilities;
        }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
        VariableArray<NorvesLib::RHI::BufferDesc> CreatedBufferDescs;
        size_t FailBufferCreateIndex = 0;
        uint32_t DestroyedBufferCount = 0;
    };

    GPUSceneInstanceData MakeInstance(float value)
    {
        GPUSceneInstanceData data = {};
        data.World[0] = value;
        data.World[5] = value + 1.0f;
        data.World[10] = value + 2.0f;
        data.World[15] = 1.0f;
        data.ObjectColor[0] = value;
        return data;
    }

    VariableArray<GPUSceneInstanceData> MakeInstances(uint32_t count)
    {
        VariableArray<GPUSceneInstanceData> data;
        data.reserve(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            data.push_back(MakeInstance(static_cast<float>(index + 1)));
        }
        return data;
    }

    TSharedPtr<FakeBuffer> AsFakeBuffer(const NorvesLib::RHI::BufferPtr& buffer)
    {
        return DynamicPointerCast<FakeBuffer>(buffer);
    }

    void AssertInstanceBufferDesc(const NorvesLib::RHI::BufferDesc& desc, uint32_t capacity)
    {
        assert(desc.Size == static_cast<uint64_t>(capacity) * sizeof(GPUSceneInstanceData));
        assert(HasUsage(desc.Usage, NorvesLib::RHI::ResourceUsage::StorageBuffer));
        assert(HasUsage(desc.Usage, NorvesLib::RHI::ResourceUsage::ShaderRead));
        assert(desc.CPUAccessible);
        assert(desc.DebugName != nullptr);
    }

    void TestInitialSlots()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 2, 0));
        assert(device.CreatedBufferDescs.size() == 2);
        AssertInstanceBufferDesc(device.CreatedBufferDescs[0], 1);
        AssertInstanceBufferDesc(device.CreatedBufferDescs[1], 1);
        ring.Shutdown();
        std::cout << "Initial slots passed\n";
    }

    void TestInitializeRejectsInvalidArguments()
    {
        FakeDevice device;
        InstanceBufferRing ring;

        assert(!ring.Initialize(nullptr, 2, 1));
        assert(device.CreatedBufferDescs.empty());

        assert(!ring.Initialize(&device, 0, 1));
        assert(device.CreatedBufferDescs.empty());

        ring.Shutdown();
        std::cout << "Initialize invalid arguments passed\n";
    }

    void TestInitialSlotFailureCleansUpCreatedBuffers()
    {
        FakeDevice device;
        device.FailBufferCreateIndex = 2;

        InstanceBufferRing ring;
        assert(!ring.Initialize(&device, 3, 1));
        assert(device.CreatedBufferDescs.size() == 2);
        assert(device.DestroyedBufferCount == 1);

        ring.Shutdown();
        std::cout << "Initial slot failure cleanup passed\n";
    }

    void TestCapacityUpload()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 2, 4));

        auto data = MakeInstances(3);
        NorvesLib::RHI::BufferPtr buffer = ring.Upload(0, data);
        auto fakeBuffer = AsFakeBuffer(buffer);
        assert(fakeBuffer);
        assert(fakeBuffer->UpdateCount == 1);
        assert(fakeBuffer->LastUpdateSize == 3 * sizeof(GPUSceneInstanceData));
        assert(fakeBuffer->LastUpdateOffset == 0);
        assert(std::memcmp(fakeBuffer->Bytes.data(), data.data(), static_cast<size_t>(fakeBuffer->LastUpdateSize)) == 0);
        assert(device.CreatedBufferDescs.size() == 2);

        ring.Shutdown();
        std::cout << "Capacity upload passed\n";
    }

    void TestSlotResizeIsIndependent()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 2, 2));

        auto slot0Initial = ring.Upload(0, MakeInstances(1));
        auto slot1Initial = ring.Upload(1, MakeInstances(1));
        auto slot0Resized = ring.Upload(0, MakeInstances(5));
        assert(slot0Resized);
        assert(slot0Resized != slot0Initial);
        assert(ring.Upload(1, VariableArray<GPUSceneInstanceData>()) == slot1Initial);
        assert(device.CreatedBufferDescs.size() == 3);
        AssertInstanceBufferDesc(device.CreatedBufferDescs[2], 8);

        ring.Shutdown();
        std::cout << "Slot resize isolation passed\n";
    }

    void TestResizeFailureKeepsOldBuffer()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 1, 2));

        NorvesLib::RHI::BufferPtr original = ring.Upload(0, MakeInstances(1));
        assert(original);
        device.FailBufferCreateIndex = device.CreatedBufferDescs.size() + 1;

        NorvesLib::RHI::BufferPtr failed = ring.Upload(0, MakeInstances(5));
        assert(!failed);

        NorvesLib::RHI::BufferPtr afterFailure = ring.Upload(0, VariableArray<GPUSceneInstanceData>());
        assert(afterFailure == original);
        assert(device.CreatedBufferDescs.size() == 2);

        ring.Shutdown();
        std::cout << "Resize failure keeps old buffer passed\n";
    }

    void TestEmptyUploadDoesNotUpdate()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 1, 2));

        NorvesLib::RHI::BufferPtr buffer = ring.Upload(0, MakeInstances(1));
        auto fakeBuffer = AsFakeBuffer(buffer);
        assert(fakeBuffer);
        assert(fakeBuffer->UpdateCount == 1);

        NorvesLib::RHI::BufferPtr emptyBuffer = ring.Upload(0, VariableArray<GPUSceneInstanceData>());
        assert(emptyBuffer == buffer);
        assert(fakeBuffer->UpdateCount == 1);

        ring.Shutdown();
        std::cout << "Empty upload passed\n";
    }

    void TestDelayedReleaseProgress()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 1, 1));

        NorvesLib::RHI::BufferPtr first = ring.Upload(0, MakeInstances(1));
        TWeakPtr<NorvesLib::RHI::IBuffer> weakFirst = first;
        assert(!weakFirst.expired());

        NorvesLib::RHI::BufferPtr resized = ring.Upload(0, MakeInstances(2));
        assert(resized);
        assert(resized != first);
        first.reset();
        assert(!weakFirst.expired());

        ring.Upload(0, VariableArray<GPUSceneInstanceData>());
        assert(weakFirst.expired());

        ring.Shutdown();
        std::cout << "Delayed release progress passed\n";
    }

    void TestDelayedReleaseKeepsOldBufferAcrossFramesInFlight()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 2, 1));

        NorvesLib::RHI::BufferPtr first = ring.Upload(0, MakeInstances(1));
        TWeakPtr<NorvesLib::RHI::IBuffer> weakFirst = first;
        assert(!weakFirst.expired());

        NorvesLib::RHI::BufferPtr resized = ring.Upload(0, MakeInstances(2));
        assert(resized);
        assert(resized != first);
        first.reset();
        assert(!weakFirst.expired());

        ring.Upload(1, VariableArray<GPUSceneInstanceData>());
        assert(!weakFirst.expired());

        ring.Upload(0, VariableArray<GPUSceneInstanceData>());
        assert(weakFirst.expired());

        ring.Shutdown();
        std::cout << "Delayed release frames-in-flight retention passed\n";
    }

    void TestInvalidUploadDoesNotAdvanceDeferredReleaseSerial()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 2, 1));

        NorvesLib::RHI::BufferPtr first = ring.Upload(0, MakeInstances(1));
        TWeakPtr<NorvesLib::RHI::IBuffer> weakFirst = first;
        assert(!weakFirst.expired());

        NorvesLib::RHI::BufferPtr resized = ring.Upload(0, MakeInstances(2));
        assert(resized);
        assert(resized != first);
        first.reset();
        assert(!weakFirst.expired());

        NorvesLib::RHI::BufferPtr invalid = ring.Upload(2, MakeInstances(1));
        assert(!invalid);
        assert(!weakFirst.expired());

        ring.Upload(1, VariableArray<GPUSceneInstanceData>());
        assert(!weakFirst.expired());

        ring.Upload(0, VariableArray<GPUSceneInstanceData>());
        assert(weakFirst.expired());

        ring.Shutdown();
        std::cout << "Invalid upload deferred serial passed\n";
    }

    void TestInvalidFrameIndex()
    {
        FakeDevice device;
        InstanceBufferRing ring;
        assert(ring.Initialize(&device, 1, 2));

        NorvesLib::RHI::BufferPtr valid = ring.Upload(0, MakeInstances(1));
        auto fakeBuffer = AsFakeBuffer(valid);
        assert(fakeBuffer);
        assert(fakeBuffer->UpdateCount == 1);

        NorvesLib::RHI::BufferPtr invalid = ring.Upload(1, MakeInstances(1));
        assert(!invalid);
        assert(fakeBuffer->UpdateCount == 1);

        ring.Shutdown();
        std::cout << "Invalid frame index passed\n";
    }
} // namespace

int main()
{
    std::cout << "InstanceBufferRingTest start\n";

    TestInitialSlots();
    TestInitializeRejectsInvalidArguments();
    TestInitialSlotFailureCleansUpCreatedBuffers();
    TestCapacityUpload();
    TestSlotResizeIsIndependent();
    TestResizeFailureKeepsOldBuffer();
    TestEmptyUploadDoesNotUpdate();
    TestDelayedReleaseProgress();
    TestDelayedReleaseKeepsOldBufferAcrossFramesInFlight();
    TestInvalidUploadDoesNotAdvanceDeferredReleaseSerial();
    TestInvalidFrameIndex();

    std::cout << "InstanceBufferRingTest passed\n";
    return 0;
}
