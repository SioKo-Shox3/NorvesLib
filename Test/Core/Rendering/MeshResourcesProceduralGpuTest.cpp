#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
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
            if (FailBufferCreateIndex == CreatedBufferDescs.size())
            {
                return {};
            }

            LastBuffer = MakeShared<FakeBuffer>(desc);
            CreatedBuffers.push_back(LastBuffer);
            return LastBuffer;
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc &) override { return {}; }
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
        std::vector<NorvesLib::Core::Container::TSharedPtr<FakeBuffer>> CreatedBuffers;
        NorvesLib::Core::Container::TSharedPtr<FakeBuffer> LastBuffer;
        size_t FailBufferCreateIndex = 0;
    };

    MeshDataHandle MakeMeshHandle(uint64_t id)
    {
        MeshDataHandle handle;
        handle.Id = id;
        return handle;
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "MeshResourcesProceduralGpuTest start\n";

    RenderResources manager;
    const MeshDataHandle meshHandle = MakeMeshHandle(77);
    const MeshDataHandle invalidHandle = MeshDataHandle::Invalid();
    const float verticesA[6] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    const uint32_t indicesA[3] = {0, 1, 0};

    assert(!manager.Meshes().Register(meshHandle, verticesA, sizeof(verticesA), indicesA, 3));
    assert(manager.Meshes().GetGPUData(meshHandle) == nullptr);
    assert(manager.GetResourceStats().BufferCount == 0);

    auto device = MakeShared<FakeDevice>();
    assert(manager.Initialize(device));

    assert(!manager.Meshes().Register(invalidHandle, verticesA, sizeof(verticesA), indicesA, 3));
    assert(!manager.Meshes().Register(meshHandle, nullptr, sizeof(verticesA), indicesA, 3));
    assert(!manager.Meshes().Register(meshHandle, verticesA, sizeof(verticesA), nullptr, 3));
    assert(!manager.Meshes().Register(meshHandle, verticesA, sizeof(verticesA), indicesA, 0));
    assert(device->CreatedBufferDescs.empty());

    assert(manager.Meshes().Register(meshHandle, verticesA, sizeof(verticesA), indicesA, 3));
    assert(device->CreatedBufferDescs.size() == 2);
    assert(device->CreatedBufferDescs[0].Usage == NorvesLib::RHI::ResourceUsage::VertexBuffer);
    assert(device->CreatedBufferDescs[1].Usage == NorvesLib::RHI::ResourceUsage::IndexBuffer);
    assert(manager.GetResourceStats().BufferCount == 0);

    const ProceduralMeshGPUData *gpuData = manager.Meshes().GetGPUData(meshHandle);
    assert(gpuData != nullptr);
    assert(gpuData->VertexBuffer);
    assert(gpuData->IndexBuffer);
    assert(gpuData->IndexCount == 3);
    assert(gpuData->VertexBuffer->GetSize() == sizeof(verticesA));
    assert(gpuData->IndexBuffer->GetSize() == sizeof(indicesA));

    const auto firstVertexBuffer = gpuData->VertexBuffer;
    const float verticesB[9] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f};
    const uint32_t indicesB[6] = {0, 1, 2, 2, 1, 0};
    assert(manager.Meshes().Register(meshHandle, verticesB, sizeof(verticesB), indicesB, 6));
    assert(device->CreatedBufferDescs.size() == 4);

    gpuData = manager.Meshes().GetGPUData(meshHandle);
    assert(gpuData != nullptr);
    assert(gpuData->IndexCount == 6);
    assert(gpuData->VertexBuffer);
    assert(gpuData->VertexBuffer != firstVertexBuffer);
    assert(gpuData->VertexBuffer->GetSize() == sizeof(verticesB));
    assert(gpuData->IndexBuffer->GetSize() == sizeof(indicesB));
    assert(manager.GetResourceStats().BufferCount == 0);

    device->FailBufferCreateIndex = device->CreatedBufferDescs.size() + 1;
    assert(!manager.Meshes().Register(meshHandle, verticesA, sizeof(verticesA), indicesA, 3));
    assert(manager.Meshes().GetGPUData(meshHandle) == nullptr);
    assert(manager.GetResourceStats().BufferCount == 0);
    device->FailBufferCreateIndex = 0;

    assert(manager.Meshes().Register(meshHandle, verticesA, sizeof(verticesA), indicesA, 3));
    assert(manager.Meshes().GetGPUData(meshHandle) != nullptr);

    manager.Meshes().Unregister(invalidHandle);
    assert(manager.Meshes().GetGPUData(meshHandle) != nullptr);

    manager.Meshes().Unregister(meshHandle);
    assert(manager.Meshes().GetGPUData(meshHandle) == nullptr);

    assert(manager.Meshes().Register(meshHandle, verticesA, sizeof(verticesA), indicesA, 3));
    assert(manager.Meshes().GetGPUData(meshHandle) != nullptr);
    manager.ClearAllResources();
    assert(manager.Meshes().GetGPUData(meshHandle) == nullptr);
    assert(manager.GetResourceStats().BufferCount == 0);

    assert(manager.Meshes().Register(meshHandle, verticesA, sizeof(verticesA), indicesA, 3));
    assert(manager.Meshes().GetGPUData(meshHandle) != nullptr);
    manager.Shutdown();
    assert(manager.Meshes().GetGPUData(meshHandle) == nullptr);
    assert(manager.GetResourceStats().BufferCount == 0);

    std::cout << "MeshResourcesProceduralGpuTest passed\n";
    return 0;
}
