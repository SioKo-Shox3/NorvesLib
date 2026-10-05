#include "Rendering/RenderResources.h"
#include "Component/MeshComponent.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/MeshTypes.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/SceneView.h"
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
#include <cmath>
#include <cstdlib>
#include <cstring>
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

    // 回転と非一様スケールが重なった物体でも、BuildMeshProxy の境界球が変換後のローカルAABBの角をすべて包む
    void TestMeshProxyBoundsContainRotatedNonUniformScale(RenderResources &resources)
    {
        const MeshDataHandle longMeshHandle = MakeMeshHandle(80);
        Mesh3DVertex longVertices[3] = {};
        longVertices[0].Position[0] = -16.0f;
        longVertices[0].Position[2] = -1.0f;
        longVertices[1].Position[0] = 16.0f;
        longVertices[1].Position[2] = 1.0f;
        longVertices[2].Position[1] = 0.5f;
        const uint32_t longIndices[3] = {0, 1, 2};
        assert(resources.Meshes().Register(longMeshHandle, longVertices, sizeof(longVertices), longIndices, 3));

        // World を SceneView より先に壊すため、SceneView を先に宣言する
        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        NorvesLib::Core::World world;
        world.Initialize();
        world.SetSceneView(&view);

        NorvesLib::Core::Entity *object = world.SpawnObject<NorvesLib::Core::Entity>();
        assert(object);
        NorvesLib::Core::Component::MeshComponent *mesh =
            world.CreateComponent<NorvesLib::Core::Component::MeshComponent>(object);
        assert(mesh);
        mesh->SetMeshHandle(longMeshHandle);
        const float halfAngle = 0.5f * 0.78539816f;
        object->SetRotation(0.0f, 0.0f, std::sin(halfAngle), std::cos(halfAngle));
        object->SetScale(3.0f, 0.5f, 1.0f);
        object->SetPosition(5.0f, -1.0f, 2.0f);

        world.SyncToSceneView(nullptr, &resources.Meshes());
        assert(view.GetMeshProxies().size() == 1);
        const MeshProxy &proxy = view.GetMeshProxies()[0];
        const BoundingSphere &bounds = proxy.WorldBounds;
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            const NorvesLib::Math::Vector3 localCorner((corner & 1u) != 0 ? 16.0f : -16.0f,
                                                       (corner & 2u) != 0 ? 0.5f : 0.0f,
                                                       (corner & 4u) != 0 ? 1.0f : -1.0f);
            const NorvesLib::Math::Vector3 worldCorner =
                NorvesLib::Math::MatrixUtils::TransformPointRowVector(proxy.WorldTransform, localCorner);
            const float dx = worldCorner.x - bounds.CenterX;
            const float dy = worldCorner.y - bounds.CenterY;
            const float dz = worldCorner.z - bounds.CenterZ;
            assert(std::sqrt(dx * dx + dy * dy + dz * dz) <= bounds.Radius * 1.0001f + 1.0e-4f);
        }
        std::cout << "MeshProxy bounds radius=" << bounds.Radius << "\n" << std::flush;

        world.SetSceneView(nullptr);
        resources.Meshes().Unregister(longMeshHandle);
    }

    // 塊が全三角形をちょうど1回ずつ覆い、各塊が128三角形以下で、サブメッシュの境目をまたがない
    void ExpectChunksCoverTrianglesOnce(const NorvesLib::Core::Container::VariableArray<MeshIndexChunk> &chunks,
                                        uint32_t indexCount,
                                        const SubMesh *subMeshes,
                                        uint32_t subMeshCount)
    {
        const uint32_t triangleCount = indexCount / 3;
        std::vector<uint32_t> coverCount(triangleCount, 0);
        uint32_t nextFirstIndex = 0;
        for (const MeshIndexChunk &chunk : chunks)
        {
            assert(chunk.IndexCount > 0);
            assert(chunk.FirstIndex % 3 == 0 && chunk.IndexCount % 3 == 0);
            assert(chunk.IndexCount / 3 <= MESH_CHUNK_MAX_TRIANGLES);
            assert(chunk.FirstIndex == nextFirstIndex);
            assert(chunk.FirstIndex + chunk.IndexCount <= triangleCount * 3);
            for (uint32_t index = chunk.FirstIndex; index < chunk.FirstIndex + chunk.IndexCount; index += 3)
            {
                ++coverCount[index / 3];
            }
            nextFirstIndex = chunk.FirstIndex + chunk.IndexCount;
            for (uint32_t i = 0; i < subMeshCount; ++i)
            {
                const uint32_t start = subMeshes[i].IndexStart;
                const uint32_t end = start + subMeshes[i].IndexCount;
                // 塊の内側（始まりより後ろ・終わりより前）にサブメッシュの境目があってはならない
                assert(!(start > chunk.FirstIndex && start < chunk.FirstIndex + chunk.IndexCount));
                assert(!(end > chunk.FirstIndex && end < chunk.FirstIndex + chunk.IndexCount));
            }
        }
        for (uint32_t triangle = 0; triangle < triangleCount; ++triangle)
        {
            assert(coverCount[triangle] == 1);
        }
        assert(nextFirstIndex == triangleCount * 3);
    }

    void TestMeshIndexChunksCoverEveryTriangleOnce(RenderResources &resources)
    {
        const MeshDataHandle handle = MakeMeshHandle(90);
        Mesh3DVertex vertices[3] = {};
        const auto makeIndices = [](uint32_t indexCount)
        {
            std::vector<uint32_t> indices(indexCount);
            for (uint32_t i = 0; i < indexCount; ++i)
            {
                indices[i] = i % 3;
            }
            return indices;
        };

        // 三角形の数が 1・128・129・300 のとき（128 ちょうどで割れ、129 で端の1三角形の塊ができる）
        const uint32_t triangleCounts[] = {1, 128, 129, 300};
        const uint32_t expectedChunkCounts[] = {1, 1, 2, 3};
        for (size_t caseIndex = 0; caseIndex < 4; ++caseIndex)
        {
            const uint32_t indexCount = triangleCounts[caseIndex] * 3;
            const std::vector<uint32_t> indices = makeIndices(indexCount);
            assert(resources.Meshes().Register(handle, vertices, sizeof(vertices), indices.data(), indexCount));
            const ProceduralMeshGPUData *gpuData = resources.Meshes().GetGPUData(handle);
            assert(gpuData != nullptr);
            assert(gpuData->Chunks.size() == expectedChunkCounts[caseIndex]);
            ExpectChunksCoverTrianglesOnce(gpuData->Chunks, indexCount, nullptr, 0);
        }

        // 3で割り切れない余りのインデックスは三角形にならないので覆わない（7個 → 2三角形）
        {
            const std::vector<uint32_t> indices = makeIndices(7);
            assert(resources.Meshes().Register(handle, vertices, sizeof(vertices), indices.data(), 7));
            const ProceduralMeshGPUData *gpuData = resources.Meshes().GetGPUData(handle);
            assert(gpuData != nullptr);
            ExpectChunksCoverTrianglesOnce(gpuData->Chunks, 7, nullptr, 0);
            assert(gpuData->Chunks.size() == 1 && gpuData->Chunks[0].IndexCount == 6);
        }

        // サブメッシュの境目（三角形 50 と 150）で塊を区切る。[0,50) [50,150) [150,200) はどれも128以下なので3塊
        {
            const uint32_t indexCount = 200 * 3;
            const std::vector<uint32_t> indices = makeIndices(indexCount);
            const SubMesh subMeshes[3] = {SubMesh(0, 150, 0, 0), SubMesh(150, 300, 0, 1), SubMesh(450, 150, 0, 2)};
            assert(resources.Meshes().Register(
                handle, vertices, sizeof(vertices), indices.data(), indexCount, subMeshes, 3));
            const ProceduralMeshGPUData *gpuData = resources.Meshes().GetGPUData(handle);
            assert(gpuData != nullptr);
            assert(gpuData->Chunks.size() == 3);
            ExpectChunksCoverTrianglesOnce(gpuData->Chunks, indexCount, subMeshes, 3);
        }

        resources.Meshes().Unregister(handle);
        std::cout << "MeshIndexChunks cover every triangle once\n" << std::flush;
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
    // 描画の用途に加えて、計算シェーダー（storage）とアドレス参照（BDA）から読める用途を持つ
    const auto hasUsage = [](NorvesLib::RHI::ResourceUsage usage, NorvesLib::RHI::ResourceUsage bits)
    { return (usage & bits) == bits; };
    const NorvesLib::RHI::ResourceUsage computeReadable = NorvesLib::RHI::ResourceUsage::StorageBuffer |
                                                          NorvesLib::RHI::ResourceUsage::ShaderRead |
                                                          NorvesLib::RHI::ResourceUsage::BufferDeviceAddress;
    assert(hasUsage(device->CreatedBufferDescs[0].Usage, NorvesLib::RHI::ResourceUsage::VertexBuffer));
    assert(hasUsage(device->CreatedBufferDescs[0].Usage, computeReadable));
    assert(hasUsage(device->CreatedBufferDescs[1].Usage, NorvesLib::RHI::ResourceUsage::IndexBuffer));
    assert(hasUsage(device->CreatedBufferDescs[1].Usage, computeReadable));
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

    const MeshDataHandle subMeshHandle = MakeMeshHandle(78);
    const SubMesh subMeshes[2] = {SubMesh(1, 3, 0, 0), SubMesh(4, 2, 7, 1)};
    NorvesLib::Core::Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS> ranges;
    uint32_t rangeCount = 99;
    assert(!manager.Meshes().TryGetSubMeshRanges(subMeshHandle, ranges, rangeCount));
    assert(rangeCount == 0);
    assert(manager.Meshes().Register(subMeshHandle, verticesB, sizeof(verticesB), indicesB, 6, subMeshes, 2));
    assert(manager.Meshes().TryGetSubMeshRanges(subMeshHandle, ranges, rangeCount));
    assert(rangeCount == 2);
    assert(ranges[0].IndexStart == 1);
    assert(ranges[0].IndexCount == 3);
    assert(ranges[0].VertexStart == 0);
    assert(ranges[0].MaterialIndex == 0);
    assert(ranges[1].IndexStart == 4);
    assert(ranges[1].IndexCount == 2);
    assert(ranges[1].VertexStart == 7);
    assert(ranges[1].MaterialIndex == 1);
    manager.Meshes().Unregister(subMeshHandle);
    rangeCount = 99;
    assert(!manager.Meshes().TryGetSubMeshRanges(subMeshHandle, ranges, rangeCount));
    assert(rangeCount == 0);

    // ローカルのAABBは Mesh3DVertex の並びで登録したときだけ位置から求め、それ以外の大きさや非有限の位置では返さない
    BoundingBox localBounds;
    assert(!manager.Meshes().TryGetLocalBounds(meshHandle, localBounds));
    const MeshDataHandle boundsHandle = MakeMeshHandle(79);
    Mesh3DVertex boundsVertices[3] = {};
    boundsVertices[0].Position[0] = -30.0f;
    boundsVertices[0].Position[2] = 4.0f;
    boundsVertices[1].Position[0] = 2.0f;
    boundsVertices[1].Position[1] = -1.0f;
    boundsVertices[2].Position[1] = 3.0f;
    boundsVertices[2].Position[2] = -30.0f;
    assert(manager.Meshes().Register(boundsHandle, boundsVertices, sizeof(boundsVertices), indicesA, 3));
    assert(manager.Meshes().TryGetLocalBounds(boundsHandle, localBounds));
    assert(localBounds.MinX == -30.0f && localBounds.MaxX == 2.0f);
    assert(localBounds.MinY == -1.0f && localBounds.MaxY == 3.0f);
    assert(localBounds.MinZ == -30.0f && localBounds.MaxZ == 4.0f);
    TestMeshProxyBoundsContainRotatedNonUniformScale(manager);
    TestMeshIndexChunksCoverEveryTriangleOnce(manager);
    boundsVertices[1].Position[1] = std::numeric_limits<float>::infinity();
    assert(manager.Meshes().Register(boundsHandle, boundsVertices, sizeof(boundsVertices), indicesA, 3));
    assert(!manager.Meshes().TryGetLocalBounds(boundsHandle, localBounds));
    manager.Meshes().Unregister(boundsHandle);
    assert(!manager.Meshes().TryGetLocalBounds(boundsHandle, localBounds));

    const size_t bufferCountBeforeReregister = device->CreatedBufferDescs.size();
    assert(manager.Meshes().Register(meshHandle, verticesB, sizeof(verticesB), indicesB, 6));
    assert(device->CreatedBufferDescs.size() == bufferCountBeforeReregister + 2);

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
