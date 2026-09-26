#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IAccelerationStructure.h"
#include <iostream>
#include <limits>

namespace
{
    using namespace NorvesLib::RHI;

    class TestBuffer final : public IBuffer
    {
    public:
        uint64_t GetSize() const override { return 64; }
        void* Map(uint64_t, uint64_t) override { return nullptr; }
        void Unmap() override {}
        void Update(const void*, uint64_t, uint64_t) override {}
        ResourceUsage GetUsage() const override { return ResourceUsage::BufferDeviceAddress; }
        uint64_t GetDeviceAddress() const override { return 0x1000; }
    };

    class TestAccelerationStructure final : public IAccelerationStructure
    {
    public:
        explicit TestAccelerationStructure(const AccelerationStructureDesc& desc)
            : m_desc(desc)
        {
        }

        const AccelerationStructureDesc& GetDesc() const override { return m_desc; }
        uint64_t GetSize() const override { return 4096; }
        uint64_t GetDeviceAddress() const override { return 0x2000; }

    private:
        AccelerationStructureDesc m_desc;
    };

    class UnsupportedDevice final : public IDevice
    {
    public:
        BufferPtr CreateBuffer(const BufferDesc&) override { return {}; }
        TexturePtr CreateTexture(const TextureDesc&) override { return {}; }
        SamplerPtr CreateSampler(const SamplerDesc&) override { return {}; }
        ShaderPtr CreateShader(const ShaderDesc&) override { return {}; }
        CommandListPtr CreateCommandList() override { return {}; }
        SwapChainPtr CreateSwapChain(const SwapChainDesc&) override { return {}; }
        RenderPassPtr CreateRenderPass(const RenderPassDesc&) override { return {}; }
        FramebufferPtr CreateFramebuffer(const FramebufferDesc&) override { return {}; }
        PipelinePtr CreateGraphicsPipeline(const GraphicsPipelineDesc&) override { return {}; }
        PipelinePtr CreateComputePipeline(const ComputePipelineDesc&) override { return {}; }
        DescriptorSetPtr CreateDescriptorSet(const DescriptorSetDesc&) override { return {}; }
        ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
        void WaitIdle() override {}
        API GetAPI() const override { return API::None; }
        const NorvesLib::RHI::DeviceCapabilities& GetCapabilities() const override { return m_capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection, bool) const override
        {
            return projection;
        }

    private:
        NorvesLib::RHI::DeviceCapabilities m_capabilities{};
    };

    class UnsupportedCommandList final : public ICommandList
    {
    public:
        void Begin() override {}
        void End() override {}
        void Submit(bool) override {}
        void BeginRenderPass(RenderPassPtr, FramebufferPtr) override {}
        void EndRenderPass() override {}
        void SetViewport(const Viewport&) override {}
        void SetScissor(const ScissorRect&) override {}
        void SetPipeline(PipelinePtr) override {}
        void SetVertexBuffer(BufferPtr, uint64_t, uint32_t) override {}
        void SetIndexBuffer(BufferPtr, uint64_t, IndexType) override {}
        void SetConstantBuffer(BufferPtr, uint32_t, ShaderStage) override {}
        void SetTexture(TexturePtr, uint32_t, ShaderStage) override {}
        void SetSampler(SamplerPtr, uint32_t, ShaderStage) override {}
        void SetDescriptorSet(DescriptorSetPtr, uint32_t) override {}
        void DrawIndexed(uint32_t, uint32_t, int32_t) override {}
        void Draw(uint32_t, uint32_t) override {}
        void DrawIndexedInstanced(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
        void DrawInstanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
        void DrawIndexedIndirect(BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void DrawIndexedIndirectCount(BufferPtr, uint64_t, BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void FillBuffer(BufferPtr, uint64_t, uint64_t, uint32_t) override {}
        void Dispatch(uint32_t, uint32_t, uint32_t) override {}
        void CopyBuffer(BufferPtr, BufferPtr, uint64_t, uint64_t, uint64_t) override {}
        void CopyBufferToTexture(BufferPtr, TexturePtr, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t) override {}
        void CopyTextureToBuffer(TexturePtr, BufferPtr, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t) override {}
        void CopyTexture(TexturePtr, TexturePtr, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
        void GenerateMipmaps(TexturePtr) override {}
        void BufferBarrier(BufferPtr, ResourceState, ResourceState, uint64_t, uint64_t) override {}
        void TextureBarrier(TexturePtr, ResourceState, ResourceState, uint32_t, uint32_t, uint32_t, uint32_t) override {}
    };

    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "契約違反: " << message << '\n';
        }
        return condition;
    }
}

int main()
{
    using namespace NorvesLib::RHI;

    AccelerationStructureDesc invalidDesc;
    if (!Check(!IsValidAccelerationStructureDesc(invalidDesc), "空の加速構造記述子を拒否する"))
    {
        return 1;
    }

    AccelerationStructureDesc blasDesc;
    blasDesc.type = AccelerationStructureType::BottomLevel;
    AccelerationStructureGeometryCapacityDesc triangleCapacity;
    triangleCapacity.type = AccelerationStructureGeometryType::Triangles;
    triangleCapacity.maxPrimitiveCount = 2;
    blasDesc.geometryCapacities.push_back(triangleCapacity);
    blasDesc.allowUpdate = true;
    if (!Check(IsValidAccelerationStructureDesc(blasDesc), "BLAS容量を受け入れる"))
    {
        return 1;
    }

    auto blas = MakeShared<TestAccelerationStructure>(blasDesc);
    AccelerationStructureGeometryDesc geometry;
    geometry.triangles.vertexBuffer = MakeShared<TestBuffer>();
    geometry.triangles.vertexCount = 4;
    geometry.triangles.vertexStride = 12;
    geometry.triangles.indexBuffer = MakeShared<TestBuffer>();
    geometry.triangles.indexCount = 6;
    geometry.triangles.indexFormat = IndexType::Uint16;

    AccelerationStructureBuildDesc buildDesc;
    buildDesc.type = AccelerationStructureType::BottomLevel;
    buildDesc.destination = blas;
    buildDesc.geometries.push_back(geometry);
    if (!Check(IsValidAccelerationStructureGeometryDesc(geometry), "有効な三角形ジオメトリを受け入れる") ||
        !Check(IsValidAccelerationStructureBuildDesc(buildDesc, AccelerationStructureBuildMode::Build), "有効なBLAS構築記述子を受け入れる"))
    {
        return 1;
    }

    AccelerationStructureGeometryDesc invalidGeometry;
    invalidGeometry.triangles.vertexCount = 3;
    invalidGeometry.triangles.vertexStride = 12;
    if (!Check(!IsValidAccelerationStructureGeometryDesc(invalidGeometry), "入力バッファのないジオメトリを拒否する"))
    {
        return 1;
    }

    AccelerationStructureGeometryDesc aabbGeometry;
    aabbGeometry.type = AccelerationStructureGeometryType::AABBs;
    aabbGeometry.aabbs.buffer = MakeShared<TestBuffer>();
    aabbGeometry.aabbs.primitiveCount = 1;
    AccelerationStructureGeometryDesc invalidAabbGeometry = aabbGeometry;
    invalidAabbGeometry.aabbs.offset = 48;
    if (!Check(IsValidAccelerationStructureGeometryDesc(aabbGeometry), "有効なAABBジオメトリを受け入れる") ||
        !Check(!IsValidAccelerationStructureGeometryDesc(invalidAabbGeometry), "範囲外のAABB入力を拒否する"))
    {
        return 1;
    }

    AccelerationStructureGeometryCapacityDesc aabbCapacity;
    aabbCapacity.type = AccelerationStructureGeometryType::AABBs;
    aabbCapacity.maxPrimitiveCount = 1;
    AccelerationStructureDesc aabbBlasDesc;
    aabbBlasDesc.type = AccelerationStructureType::BottomLevel;
    aabbBlasDesc.geometryCapacities.push_back(aabbCapacity);
    AccelerationStructureDesc mixedGeometryBlasDesc;
    mixedGeometryBlasDesc.type = AccelerationStructureType::BottomLevel;
    mixedGeometryBlasDesc.geometryCapacities.push_back(triangleCapacity);
    mixedGeometryBlasDesc.geometryCapacities.push_back(aabbCapacity);
    auto mixedBlas = MakeShared<TestAccelerationStructure>(mixedGeometryBlasDesc);
    AccelerationStructureBuildDesc mixedBuildDesc;
    mixedBuildDesc.type = AccelerationStructureType::BottomLevel;
    mixedBuildDesc.destination = mixedBlas;
    mixedBuildDesc.geometries.push_back(geometry);
    mixedBuildDesc.geometries.push_back(aabbGeometry);
    AccelerationStructureDesc sameTypeGeometryBlasDesc = blasDesc;
    sameTypeGeometryBlasDesc.geometryCapacities.push_back(triangleCapacity);
    auto sameTypeGeometryBlas = MakeShared<TestAccelerationStructure>(sameTypeGeometryBlasDesc);
    AccelerationStructureBuildDesc sameTypeGeometryBuildDesc;
    sameTypeGeometryBuildDesc.type = AccelerationStructureType::BottomLevel;
    sameTypeGeometryBuildDesc.destination = sameTypeGeometryBlas;
    sameTypeGeometryBuildDesc.geometries.push_back(geometry);
    sameTypeGeometryBuildDesc.geometries.push_back(geometry);
    auto aabbBlas = MakeShared<TestAccelerationStructure>(aabbBlasDesc);
    AccelerationStructureBuildDesc aabbBuildDesc;
    aabbBuildDesc.type = AccelerationStructureType::BottomLevel;
    aabbBuildDesc.destination = aabbBlas;
    aabbBuildDesc.geometries.push_back(aabbGeometry);
    if (!Check(IsValidAccelerationStructureDesc(aabbBlasDesc), "AABB容量を受け入れる") ||
        !Check(IsValidAccelerationStructureBuildDesc(aabbBuildDesc), "有効なAABB BLAS構築を受け入れる") ||
        !Check(IsValidAccelerationStructureDesc(sameTypeGeometryBlasDesc), "同じ種別の複数geometry容量を受け入れる") ||
        !Check(IsValidAccelerationStructureBuildDesc(sameTypeGeometryBuildDesc), "同じ種別の複数BLAS geometryを受け入れる") ||
        !Check(!IsValidAccelerationStructureDesc(mixedGeometryBlasDesc), "三角形とAABBの混在容量を拒否する") ||
        !Check(!IsValidAccelerationStructureBuildDesc(mixedBuildDesc), "三角形とAABBの混在BLAS構築を拒否する"))
    {
        return 1;
    }

    AccelerationStructureGeometryDesc nonIndexedGeometry;
    nonIndexedGeometry.triangles.vertexBuffer = MakeShared<TestBuffer>();
    nonIndexedGeometry.triangles.vertexCount = 3;
    nonIndexedGeometry.triangles.vertexStride = 12;
    if (!Check(IsValidAccelerationStructureGeometryDesc(nonIndexedGeometry), "非インデックス三角形を受け入れる"))
    {
        return 1;
    }

    AccelerationStructureGeometryDesc misalignedGeometry = geometry;
    misalignedGeometry.triangles.vertexOffset = 2;
    if (!Check(!IsValidAccelerationStructureGeometryDesc(misalignedGeometry), "整列していない頂点オフセットを拒否する"))
    {
        return 1;
    }

    AccelerationStructureGeometryDesc overCapacityGeometry = geometry;
    overCapacityGeometry.triangles.indexCount = 9;
    AccelerationStructureBuildDesc overCapacityBuildDesc;
    overCapacityBuildDesc.type = AccelerationStructureType::BottomLevel;
    overCapacityBuildDesc.destination = blas;
    overCapacityBuildDesc.geometries.push_back(overCapacityGeometry);
    if (!Check(!IsValidAccelerationStructureBuildDesc(overCapacityBuildDesc), "容量を超えるBLASジオメトリを拒否する"))
    {
        return 1;
    }

    AccelerationStructureBuildDesc updateDesc;
    updateDesc.type = AccelerationStructureType::BottomLevel;
    updateDesc.mode = AccelerationStructureBuildMode::Update;
    updateDesc.destination = blas;
    updateDesc.source = blas;
    updateDesc.geometries.push_back(geometry);
    if (!Check(IsValidAccelerationStructureBuildDesc(updateDesc, AccelerationStructureBuildMode::Update), "有効なBLAS更新記述子を受け入れる") ||
        !Check(!IsValidAccelerationStructureBuildDesc(updateDesc, AccelerationStructureBuildMode::Build), "Update記述子をBuildとして拒否する") ||
        !Check(!IsValidAccelerationStructureBuildDesc(buildDesc, AccelerationStructureBuildMode::Update), "Build記述子をUpdateとして拒否する"))
    {
        return 1;
    }

    AccelerationStructureDesc undersizedBlasDesc = blasDesc;
    undersizedBlasDesc.geometryCapacities[0].maxPrimitiveCount = 1;
    auto undersizedBlas = MakeShared<TestAccelerationStructure>(undersizedBlasDesc);
    AccelerationStructureBuildDesc undersizedSourceUpdateDesc = updateDesc;
    undersizedSourceUpdateDesc.source = undersizedBlas;
    if (!Check(!IsValidAccelerationStructureBuildDesc(undersizedSourceUpdateDesc), "source容量を超えるBLAS更新を拒否する"))
    {
        return 1;
    }

    AccelerationStructureDesc tlasDesc;
    tlasDesc.type = AccelerationStructureType::TopLevel;
    tlasDesc.maxInstanceCount = 1;
    tlasDesc.allowUpdate = true;
    auto tlas = MakeShared<TestAccelerationStructure>(tlasDesc);
    AccelerationStructureInstanceDesc instance;
    instance.bottomLevel = blas;
    AccelerationStructureBuildDesc tlasBuildDesc;
    tlasBuildDesc.type = AccelerationStructureType::TopLevel;
    tlasBuildDesc.destination = tlas;
    tlasBuildDesc.instances.push_back(instance);
    if (!Check(IsValidAccelerationStructureDesc(tlasDesc), "TLAS容量を受け入れる") ||
        !Check(IsValidAccelerationStructureInstanceDesc(instance), "有効なTLASインスタンスを受け入れる") ||
        !Check(IsValidAccelerationStructureBuildDesc(tlasBuildDesc), "有効なTLAS構築記述子を受け入れる"))
    {
        return 1;
    }

    AccelerationStructureBuildDesc tlasUpdateDesc = tlasBuildDesc;
    tlasUpdateDesc.mode = AccelerationStructureBuildMode::Update;
    tlasUpdateDesc.source = tlas;
    AccelerationStructureDesc largerTlasDesc = tlasDesc;
    largerTlasDesc.maxInstanceCount = 2;
    auto largerTlas = MakeShared<TestAccelerationStructure>(largerTlasDesc);
    AccelerationStructureBuildDesc overCapacityTlasUpdateDesc = tlasUpdateDesc;
    overCapacityTlasUpdateDesc.destination = largerTlas;
    overCapacityTlasUpdateDesc.instances.push_back(instance);
    if (!Check(IsValidAccelerationStructureDesc(largerTlasDesc), "大きいTLAS容量を受け入れる") ||
        !Check(IsValidAccelerationStructureBuildDesc(tlasUpdateDesc), "有効なTLAS更新記述子を受け入れる") ||
        !Check(!IsValidAccelerationStructureBuildDesc(overCapacityTlasUpdateDesc), "source容量を超えるTLAS更新を拒否する"))
    {
        return 1;
    }

    AccelerationStructureInstanceDesc invalidTransform = instance;
    invalidTransform.transform[3] = std::numeric_limits<float>::infinity();
    AccelerationStructureInstanceDesc invalidCustomIndex = instance;
    invalidCustomIndex.customIndex = 0x01000000u;
    if (!Check(!IsValidAccelerationStructureInstanceDesc(invalidTransform), "非有限な変換を拒否する") ||
        !Check(!IsValidAccelerationStructureInstanceDesc(invalidCustomIndex), "範囲外のインスタンスIDを拒否する"))
    {
        return 1;
    }

    AccelerationStructureBuildDesc invalidUpdateDesc;
    invalidUpdateDesc.type = AccelerationStructureType::BottomLevel;
    invalidUpdateDesc.mode = AccelerationStructureBuildMode::Update;
    invalidUpdateDesc.destination = blas;
    invalidUpdateDesc.geometries.push_back(geometry);
    if (!Check(!IsValidAccelerationStructureBuildDesc(invalidUpdateDesc), "sourceのない更新記述子を拒否する"))
    {
        return 1;
    }

    UnsupportedDevice unsupportedDevice;
    UnsupportedCommandList unsupportedCommandList;
    if (!Check(!unsupportedDevice.GetCapabilities().RayTracing.bAccelerationStructure,
               "テスト用デバイスをRT非対応にする") ||
        !Check(!unsupportedDevice.CreateAccelerationStructure(blasDesc),
               "RT非対応時はnullptrを返す") ||
        !Check(!unsupportedDevice.CreateAccelerationStructure(invalidDesc),
               "不正な作成記述子はnullptrを返す") ||
        !Check(!unsupportedCommandList.BuildAccelerationStructure(buildDesc),
               "RT非対応時のBLAS/TLAS構築はfalseを返す") ||
        !Check(!unsupportedCommandList.BuildAccelerationStructure(updateDesc),
               "RT非対応時のUpdate入力付きBuild要求はfalseを返す") ||
        !Check(!unsupportedCommandList.UpdateAccelerationStructure(updateDesc),
               "RT非対応時のBLAS/TLAS更新はfalseを返す") ||
        !Check(!unsupportedCommandList.UpdateAccelerationStructure(buildDesc),
               "RT非対応時のBuild入力付きUpdate要求はfalseを返す") ||
        !Check(!unsupportedCommandList.UpdateAccelerationStructure(invalidUpdateDesc),
               "不正な更新入力はfalseを返す"))
    {
        return 1;
    }

    AccelerationStructureBuildDesc invalidBuildDesc;
    if (!Check(!IsValidAccelerationStructureBuildDesc(invalidBuildDesc), "宛先のない構築記述子を拒否する") ||
        !Check(!unsupportedCommandList.BuildAccelerationStructure(invalidBuildDesc), "不正な構築入力はfalseを返す"))
    {
        return 1;
    }

    std::cout << "RHIRayTracingApiContractTest passed: BLAS/TLAS記述子、不正入力、RT非対応時の戻り値を確認しました\n";
    return 0;
}
