// 本番の範囲・席の判定から BVH の段1へ渡る集合を、倍精度の幾何の参照と照合する。
// 視錐台・距離・画素による取りこぼし、重複、空の間接 dispatch の誤った書き込みを検出する。
// 記述子・表・列の契約は本番の公開定義を使い、後続の遮蔽ケースでも資源と記録の道具を共有できる。
#include "Rendering/CameraViewConstants.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/MegaGeometry/MegaGeometryCullUniforms.h"
#include "Rendering/ShaderManager.h"
#include "Math/MatrixUtils.h"
#include "Math/Quaternion.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
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
    using namespace NorvesLib::Core::Rendering::MegaGeometry;
    using namespace NorvesLib::Core::Rendering::MegaGeometry::BvhCullLayout;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "MegaGeometryInstanceCullVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t ScreenWidth = 1280;
    constexpr uint32_t ScreenHeight = 720;
    constexpr uint32_t MeshCount = 4;
    constexpr uint32_t SectionCount = MeshCount;
    constexpr double PixelBoundaryAmbiguityPixels = 4e-3;
    constexpr uint32_t CullGroupThreads = 64; // 本番シェーダーの local_size_x（束縛や列の配置とは独立）

    void Require(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            throw std::runtime_error(message);
        }
    }

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    bool IsGpuTestSkipForced()
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || value == nullptr)
        {
            return false;
        }
        const bool bSkip = std::strcmp(value, "1") == 0;
        free(value);
        return bSkip;
    }

    int Skip(const char* message)
    {
        std::cout << TestName << " スキップ: " << message << '\n';
        return GpuTestSkipReturnCode;
    }

    struct Sphere
    {
        double Center[3] = {};
        double Radius = 0;
    };

    struct Scene
    {
        VariableArray<MegaGeometryPass::GPUMegaInstance> Instances;
        VariableArray<Sphere> Spheres;
        MegaGeometryPass::GPUMegaMesh Meshes[MeshCount] = {};
    };

    // 本番の行ベクトルのワールド行列をそのまま memcpy した表から、倍精度で球を求める。
    // GPU の float の演算を模写せず、入力されたアフィン変換の幾何を評価する。
    Sphere WorldSphere(const Math::Matrix4x4& world, const float local[4])
    {
        Sphere result;
        const Math::Vector4 translation = world.GetRow(3);
        const double position[] = {translation.x, translation.y, translation.z};
        double maxScale = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            const Math::Vector4 column = world.GetColumn(axis);
            result.Center[axis] = position[axis] + static_cast<double>(column.x) * local[0]
                + static_cast<double>(column.y) * local[1] + static_cast<double>(column.z) * local[2];
            const Math::Vector4 row = world.GetRow(axis);
            const double x = row.x;
            const double y = row.y;
            const double z = row.z;
            maxScale = std::max(maxScale, std::sqrt(x * x + y * y + z * z));
        }
        result.Radius = local[3] * maxScale;
        return result;
    }

    void InitializeMeshes(Scene& scene)
    {
        const float spheres[MeshCount][4] = {
            {0, 0, 0, 2}, {1.25f, -0.5f, 0.25f, 0.75f},
            {-0.75f, 0.25f, 1.5f, 3}, {0.125f, 0.25f, -0.5f, 0.015625f}};
        for (uint32_t mesh = 0; mesh < MeshCount; ++mesh)
        {
            std::memcpy(scene.Meshes[mesh].BoundsSphere, spheres[mesh], sizeof(spheres[mesh]));
            scene.Meshes[mesh].BvhNodeCount = 1;
            scene.Meshes[mesh].ClusterCount = 1;
            scene.Meshes[mesh].SectionIndex = mesh;
        }
    }

    void AddInstance(Scene& scene, uint32_t mesh, const Math::Vector3& translation,
                     const Math::Vector3& scale, float angle)
    {
        // 純粋な回転を Math の API で作り、行ベクトルの S * R * T にする。
        // 非一様拡大でも各行は直交し、球の半径は3軸の拡大率の最大となる。
        const Math::Matrix4x4 rotation = Math::MatrixUtils::CreateWorldRowVector(
            Math::Vector3(0, 0, 0), Math::Quaternion(Math::Vector3(0, 1, 0), angle), Math::Vector3(1, 1, 1));
        Math::Matrix4x4 world = Math::MatrixUtils::CreateScale(scale) * rotation;
        world.SetTranslationRow(translation);
        MegaGeometryPass::GPUMegaInstance row{};
        std::memcpy(row.WorldMatrix, &world, sizeof(row.WorldMatrix));
        std::memcpy(row.PreviousWorldMatrix, &world, sizeof(row.PreviousWorldMatrix));
        row.MeshIndex = mesh;
        scene.Instances.push_back(row);
        scene.Spheres.push_back(WorldSphere(world, scene.Meshes[mesh].BoundsSphere));
    }

    Scene MakeRandomScene()
    {
        Scene scene;
        InitializeMeshes(scene);
        std::mt19937 random(0x73AC219Bu);
        auto range = [&random](float low, float high)
        {
            // mt19937 の整数から直接作り、標準ライブラリの分布の実装差を避ける。
            return low + (high - low) * static_cast<float>(random() >> 8u) / 16777216.0f;
        };
        Math::Vector3 center;
        for (uint32_t seat = 0; seat < 100000; ++seat)
        {
            const uint32_t group = seat / BvhRangeSeats;
            const uint32_t category = group % 8u;
            if (seat % BvhRangeSeats == 0)
            {
                center = Math::Vector3(range(-50, 50), range(-30, 30), range(-400, -50));
                if (category == 1) { center.x = -700; }
                if (category == 2) { center.x = 700; }
                if (category == 3) { center.z = 150; }
                if (category == 4) { center.z = -800; }
                if (category == 5) { center.z = -350; }
                if (category == 6) { center.y = 600; }
            }
            const uint32_t mesh = category == 5 ? MeshCount - 1u : static_cast<uint32_t>(random() % MeshCount);
            const float scale = category == 5 ? range(0.02f, 0.2f) : range(0.25f, 3.0f);
            AddInstance(scene, mesh,
                center + Math::Vector3(range(-12, 12), range(-12, 12), range(-12, 12)),
                Math::Vector3(scale, scale * range(0.5f, 1), scale * range(0.5f, 1)), range(-3, 3));
        }
        return scene;
    }

    // float への変換の後、常にさらに外側の隣接値へ進めて球を包む。
    float RoundOutward(double value, bool bUpper)
    {
        return std::nextafter(static_cast<float>(value), bUpper
            ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
    }

    struct RangeBounds
    {
        float Min[4];
        float Max[4];
    };
    static_assert(sizeof(RangeBounds) == RangeBoundsBytes);

    VariableArray<RangeBounds> MakeRangeBounds(const Scene& scene)
    {
        VariableArray<RangeBounds> result;
        const uint32_t count = static_cast<uint32_t>(scene.Spheres.size());
        result.resize((count + BvhRangeSeats - 1u) / BvhRangeSeats);
        for (uint32_t group = 0; group < result.size(); ++group)
        {
            double low[3] = {1e100, 1e100, 1e100};
            double high[3] = {-1e100, -1e100, -1e100};
            for (uint32_t seat = group * BvhRangeSeats; seat < std::min(count, (group + 1u) * BvhRangeSeats); ++seat)
            {
                const Sphere& sphere = scene.Spheres[seat];
                for (int axis = 0; axis < 3; ++axis)
                {
                    low[axis] = std::min(low[axis], sphere.Center[axis] - sphere.Radius);
                    high[axis] = std::max(high[axis], sphere.Center[axis] + sphere.Radius);
                }
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                result[group].Min[axis] = RoundOutward(low[axis], false);
                result[group].Max[axis] = RoundOutward(high[axis], true);
            }
            result[group].Min[3] = static_cast<float>(RangeFlagHasRows);
            result[group].Max[3] = 0;
        }
        return result;
    }

    CameraViewConstants MakeCamera(const DevicePtr& device, bool bAway = false)
    {
        CameraProxy camera;
        camera.PositionX = 0;
        camera.PositionY = 0;
        // 全表より負の Z に視点を置き、同じ -Z へ向ければ全席が後ろになる。
        camera.PositionZ = bAway ? -4096.0f : 0.0f;
        camera.ForwardX = 0;
        camera.ForwardY = 0;
        camera.ForwardZ = -1;
        camera.UpX = 0;
        camera.UpY = 1;
        camera.UpZ = 0;
        camera.FieldOfView = 60;
        camera.NearPlane = 0.5f;
        camera.FarPlane = 1024;
        return CameraViewConstants::BuildForDevice(camera, static_cast<float>(ScreenWidth) / ScreenHeight, device.get());
    }

    CullUniformData MakeUniform(const CameraViewConstants& camera, uint32_t count, float distance)
    {
        CullUniformData uniform{};
        camera.CopyShaderView(uniform.ViewMatrix);
        camera.CopyShaderProjection(uniform.ProjectionMatrix);
        camera.CopyCameraPosition(uniform.CameraPosition);
        Math::MatrixUtils::ExtractClipSpaceFrustumPlanes(camera.ViewProjectionMatrix,
            Math::ClipSpaceDepthRange::ZeroToOne).CopyToShaderData(uniform.FrustumPlanes);
        uniform.InstanceCount = uniform.BvhRootCount = count;
        uniform.BvhRangeCount = (count + BvhRangeSeats - 1u) / BvhRangeSeats;
        uniform.BvhNodeCapacity = uniform.BvhLeafCapacity = count;
        uniform.BvhInputBase = uniform.BvhNextBase = 0;
        uniform.BvhLeafBase = count * 2u; // 節の列0・列1・葉の列をそれぞれ count 要素ずつ確保する。
        uniform.InstanceCullDistance = distance;
        uniform.PixelCullWidth = static_cast<float>(ScreenWidth);
        uniform.PixelCullHeight = static_cast<float>(ScreenHeight);
        uniform.CullPass = CULL_PASS_SINGLE;
        uniform.LODBias = 1;
        uniform.ScreenHeight = static_cast<float>(ScreenHeight);
        const float halfFovTan = std::tan(camera.FieldOfViewRadians * 0.5f);
        uniform.ProjectionFactor = halfFovTan > 1e-6f ? uniform.ScreenHeight / (2 * halfFovTan) : 1;
        uniform.bStatsEnabled = 1;
        return uniform;
    }

    struct ReferenceCamera
    {
        double VP[4][4] = {};
        double Planes[6][4] = {};
        double Position[3] = {};

        explicit ReferenceCamera(const CameraViewConstants& camera)
        {
            for (int row = 0; row < 4; ++row)
            {
                const Math::Vector4 source = camera.ViewProjectionMatrix.GetRow(row);
                VP[row][0] = source.x;
                VP[row][1] = source.y;
                VP[row][2] = source.z;
                VP[row][3] = source.w;
            }
            for (int plane = 0; plane < 6; ++plane)
            {
                for (int component = 0; component < 4; ++component)
                {
                    Planes[plane][component] = plane == 4 ? VP[2][component]
                        : VP[3][component] + (plane % 2 == 0 ? 1.0 : -1.0) * VP[plane / 2][component];
                }
                const double length = std::sqrt(Planes[plane][0] * Planes[plane][0]
                    + Planes[plane][1] * Planes[plane][1] + Planes[plane][2] * Planes[plane][2]);
                Require(length > 0, "参照の視錐台の平面が退化しています");
                for (double& component : Planes[plane])
                {
                    component /= length;
                }
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                Position[axis] = camera.CameraPosition[axis];
            }
        }
    };

    struct ReferenceResult
    {
        bool bFrustum = true;
        bool bDistance = true;
        bool bPixel = true;
        bool bAmbiguous = false;
        bool IsVisible() const { return bFrustum && bDistance && bPixel; }
    };

    // 契約: 接する球を含む6半空間、球の最短距離、8頂点の矩形内の画素中心の存在。
    // 境界の帯は平面・距離が相対1e-5、画素が4e-3画素（各軸の中心が1個以下のとき）。接触の専用ケースには適用しない。
    ReferenceResult Classify(const ReferenceCamera& camera, const Sphere& sphere, const CullUniformData& uniform)
    {
        ReferenceResult result;
        double distanceSquared = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            const double delta = sphere.Center[axis] - camera.Position[axis];
            distanceSquared += delta * delta;
        }
        const double distance = std::sqrt(distanceSquared);
        const double tolerance = 1e-5 * std::max(1.0, distance + sphere.Radius);
        for (const auto& plane : camera.Planes)
        {
            const double separation = plane[0] * sphere.Center[0] + plane[1] * sphere.Center[1]
                + plane[2] * sphere.Center[2] + plane[3] + sphere.Radius;
            result.bFrustum = result.bFrustum && separation >= 0;
            result.bAmbiguous = result.bAmbiguous || std::abs(separation) <= tolerance;
        }
        if (uniform.InstanceCullDistance > 0)
        {
            const double separation = uniform.InstanceCullDistance - distance + sphere.Radius;
            result.bDistance = separation >= 0;
            result.bAmbiguous = result.bAmbiguous || std::abs(separation) <= tolerance;
        }
        if (uniform.PixelCullWidth <= 0 || uniform.PixelCullHeight <= 0)
        {
            return result;
        }
        double low[2] = {1e100, 1e100};
        double high[2] = {-1e100, -1e100};
        for (int corner = 0; corner < 8; ++corner)
        {
            double point[4] = {0, 0, 0, 1};
            for (int axis = 0; axis < 3; ++axis)
            {
                point[axis] = sphere.Center[axis] + ((corner & (1 << axis)) != 0 ? sphere.Radius : -sphere.Radius);
            }
            double clip[4] = {};
            for (int row = 0; row < 4; ++row)
            {
                for (int component = 0; component < 4; ++component)
                {
                    clip[row] += camera.VP[row][component] * point[component];
                }
            }
            if (!(clip[3] > 0))
            {
                return result;
            }
            // シェーダーが保守的に判定を止める w の帯も、比較から除く。
            if (clip[3] <= 1e-6)
            {
                result.bAmbiguous = true;
            }
            for (int axis = 0; axis < 2; ++axis)
            {
                const double pixel = (clip[axis] / clip[3] + 1) * 0.5
                    * (axis == 0 ? uniform.PixelCullWidth : uniform.PixelCullHeight);
                low[axis] = std::min(low[axis], pixel);
                high[axis] = std::max(high[axis], pixel);
            }
        }
        for (int axis = 0; axis < 2; ++axis)
        {
            const double first = low[axis] - 1.0 / 32.0 - 0.5;
            const double last = high[axis] + 1.0 / 32.0 - 0.5;
            const double centerCount = std::floor(last) - std::ceil(first) + 1;
            result.bPixel = result.bPixel && centerCount > 0;
            // 中心が2個以上あれば端が1つずれても残る。視錐台・距離を通る細い矩形だけを曖昧にする。
            if (result.bFrustum && result.bDistance && centerCount <= 1)
            {
                result.bAmbiguous = result.bAmbiguous
                    || std::abs(first - std::round(first)) <= PixelBoundaryAmbiguityPixels
                    || std::abs(last - std::round(last)) <= PixelBoundaryAmbiguityPixels;
            }
        }
        return result;
    }

    BufferPtr MakeBuffer(const DevicePtr& device, uint64_t bytes, ResourceUsage usage, const void* data = nullptr)
    {
        BufferPtr buffer = device->CreateBuffer(BufferDesc(bytes, usage, data != nullptr, "InstanceCullTestBuffer"));
        Require(buffer != nullptr, "試験のバッファを作れませんでした");
        if (data != nullptr)
        {
            buffer->Update(data, bytes);
        }
        return buffer;
    }

    struct Fixture
    {
        DevicePtr Device;
        PipelinePtr Pipeline;
        TexturePtr Black;
        SamplerPtr Sampler;
    };

    struct CullResources
    {
        BufferPtr Instances, Meshes, Nodes, Ranges, Queue, Counters, Pass2, Stats, DrawCounts, Dummy;
    };

    CullResources MakeResources(Fixture& fixture, const Scene& scene, const CullUniformData& uniform)
    {
        CullResources resources;
        const ResourceUsage storage = ResourceUsage::StorageBuffer | ResourceUsage::TransferSrc | ResourceUsage::TransferDst;
        GPUGroupBVHNode nodes[MeshCount] = {};
        for (uint32_t mesh = 0; mesh < MeshCount; ++mesh)
        {
            const float* sphere = scene.Meshes[mesh].BoundsSphere;
            nodes[mesh] = {sphere[0], sphere[1], sphere[2], sphere[3], 1e20f, 0, 1, GPU_GROUP_BVH_NODE_FLAG_LEAF};
        }
        // 最大誤差1e20、拡大率<=3、投影係数<1000、nearest>=1e-4、stretch<4。
        // 投影誤差は有限のまま十分大きく、根の LOD では切られない。
        resources.Nodes = MakeBuffer(fixture.Device, sizeof(nodes), storage | ResourceUsage::BufferDeviceAddress, nodes);
        const uint64_t address = resources.Nodes->GetDeviceAddress();
        Require(address != 0, "BVH のデバイスアドレスを取得できませんでした");
        MegaGeometryPass::GPUMegaMesh meshes[MeshCount];
        std::memcpy(meshes, scene.Meshes, sizeof(meshes));
        for (uint32_t mesh = 0; mesh < MeshCount; ++mesh)
        {
            const uint64_t nodeAddress = address + mesh * sizeof(GPUGroupBVHNode);
            meshes[mesh].BvhAddressLow = static_cast<uint32_t>(nodeAddress);
            meshes[mesh].BvhAddressHigh = static_cast<uint32_t>(nodeAddress >> 32u);
        }
        resources.Instances = MakeBuffer(fixture.Device, scene.Instances.size() * sizeof(scene.Instances[0]), storage, scene.Instances.data());
        resources.Meshes = MakeBuffer(fixture.Device, sizeof(meshes), storage, meshes);
        const auto ranges = MakeRangeBounds(scene);
        resources.Ranges = MakeBuffer(fixture.Device, ranges.size() * sizeof(RangeBounds), storage, ranges.data());
        resources.Queue = MakeBuffer(fixture.Device, static_cast<uint64_t>(uniform.BvhLeafBase + uniform.BvhLeafCapacity) * 2u * sizeof(uint32_t), storage);
        resources.Counters = MakeBuffer(fixture.Device, BvhCounterBytes, storage | ResourceUsage::IndirectBuffer);
        resources.Pass2 = MakeBuffer(fixture.Device, BvhPass2HeaderWords * sizeof(uint32_t), storage | ResourceUsage::IndirectBuffer);
        resources.Stats = MakeBuffer(fixture.Device, StatsBufferBytes, storage);
        resources.DrawCounts = MakeBuffer(fixture.Device, SectionCount * sizeof(uint32_t), storage);
        resources.Dummy = MakeBuffer(fixture.Device, StatsBufferBytes, storage);
        return resources;
    }

    struct DispatchResources
    {
        BufferPtr Uniform;
        DescriptorSetPtr Set;
    };

    DispatchResources MakeDispatch(Fixture& fixture, const CullResources& resources, const CullUniformData& uniform)
    {
        DispatchResources dispatch;
        dispatch.Uniform = MakeBuffer(fixture.Device, sizeof(uniform), ResourceUsage::ConstantBuffer, &uniform);
        const DescriptorSetDesc layout = MegaGeometryPass::BuildCullDescriptorSetDesc();
        dispatch.Set = fixture.Device->CreateDescriptorSet(layout);
        Require(dispatch.Set != nullptr, "記述子セットを作れませんでした");
        // 本番のレイアウトを列挙し、未使用の束縛も必ず実資源で結ぶ。
        for (const DescriptorBinding& binding : layout.bindings)
        {
            if (binding.binding == CullUniformBinding)
            {
                dispatch.Set->BindConstantBuffer(binding.binding, dispatch.Uniform, 0, sizeof(uniform));
            }
            else if (binding.binding == CullHiZBinding || binding.binding == PrevHiZBinding)
            {
                dispatch.Set->BindTexture(binding.binding, fixture.Black);
                dispatch.Set->BindSampler(binding.binding, fixture.Sampler);
            }
            else
            {
                BufferPtr buffer = resources.Dummy;
                switch (binding.binding)
                {
                case CullInstanceTableBinding: buffer = resources.Instances; break;
                case MegaMeshTableBindingIndex: buffer = resources.Meshes; break;
                case RangeBoundsBinding: buffer = resources.Ranges; break;
                case BvhQueueBinding: buffer = resources.Queue; break;
                case BvhCounterBinding: buffer = resources.Counters; break;
                case BvhPass2Binding: buffer = resources.Pass2; break;
                case CullStatsBinding: buffer = resources.Stats; break;
                case CullDrawCountBinding: buffer = resources.DrawCounts; break;
                default: break;
                }
                dispatch.Set->BindStorageBuffer(binding.binding, buffer, 0, static_cast<uint32_t>(buffer->GetSize()));
            }
        }
        dispatch.Set->Update();
        return dispatch;
    }

    BufferPtr RecordReadback(Fixture& fixture, const CommandListPtr& command, const BufferPtr& source)
    {
        BufferPtr readback = fixture.Device->CreateBuffer(BufferDesc(source->GetSize(), ResourceUsage::TransferDst, true, "InstanceCullReadback"));
        Require(readback != nullptr, "読み戻し先を作れませんでした");
        command->BufferBarrier(source, ResourceState::UnorderedAccess, ResourceState::CopySource);
        command->CopyBuffer(source, readback, source->GetSize());
        command->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead);
        return readback;
    }

    VariableArray<uint32_t> ReadWords(const BufferPtr& readback)
    {
        VariableArray<uint32_t> words;
        words.resize(static_cast<size_t>(readback->GetSize() / sizeof(uint32_t)));
        const void* mapped = readback->Map(0, readback->GetSize());
        Require(mapped != nullptr, "読み戻し先を写像できませんでした");
        std::memcpy(words.data(), mapped, static_cast<size_t>(readback->GetSize()));
        readback->Unmap();
        return words;
    }

    struct CullResult
    {
        VariableArray<uint32_t> Counters, Queue, DrawCounts;
    };

    CullResult RunCull(Fixture& fixture, const Scene& scene, const CullUniformData& base, bool bEmptyClusters = false)
    {
        const CullResources resources = MakeResources(fixture, scene, base);
        CullUniformData range = base;
        range.BvhStage = BvhStageRanges;
        CullUniformData seats = base;
        seats.BvhStage = 0;
        seats.bBvhSeatsFromRanges = BvhSeatsFromRanges;
        seats.BvhNextBase = base.BvhNodeCapacity;
        CullUniformData clusters = base;
        clusters.BvhStage = BvhStageClusters;
        // 記録中の書き換えを避け、全 dispatch の定数とセットを別々に保つ。
        const DispatchResources rangeDispatch = MakeDispatch(fixture, resources, range);
        const DispatchResources seatDispatch = MakeDispatch(fixture, resources, seats);
        const DispatchResources clusterDispatch = MakeDispatch(fixture, resources, clusters);
        CommandListPtr command = fixture.Device->CreateCommandList();
        Require(command != nullptr, "コマンドリストを作れませんでした");
        command->Begin();
        for (const BufferPtr& buffer : {resources.Instances, resources.Meshes, resources.Nodes, resources.Ranges})
        {
            command->BufferBarrier(buffer, ResourceState::Common, ResourceState::GenericRead);
        }
        command->BufferBarrier(resources.Queue, ResourceState::Common, ResourceState::UnorderedAccess);
        for (const BufferPtr& buffer : {resources.Counters, resources.Pass2, resources.Stats, resources.DrawCounts, resources.Dummy})
        {
            command->BufferBarrier(buffer, ResourceState::Common, ResourceState::CopyDest);
            command->FillBuffer(buffer, 0, buffer->GetSize(), 0);
            command->BufferBarrier(buffer, ResourceState::CopyDest, ResourceState::UnorderedAccess);
        }
        command->BufferBarrier(resources.Counters, ResourceState::CopyDest, ResourceState::IndirectArgument);
        command->SetPipeline(fixture.Pipeline);
        command->SetDescriptorSet(rangeDispatch.Set);
        command->Dispatch((range.BvhRangeCount + CullGroupThreads - 1u) / CullGroupThreads, 1, 1);
        auto barrier = [&]()
        {
            command->BufferBarrier(resources.Queue, ResourceState::UnorderedAccess, ResourceState::UnorderedAccess);
            command->BufferBarrier(resources.Counters, ResourceState::UnorderedAccess, ResourceState::UnorderedAccess);
            command->BufferBarrier(resources.Counters, ResourceState::UnorderedAccess, ResourceState::IndirectArgument);
            command->BufferBarrier(resources.Stats, ResourceState::UnorderedAccess, ResourceState::UnorderedAccess);
        };
        barrier();
        command->SetDescriptorSet(seatDispatch.Set);
        Require(command->DispatchIndirect(resources.Counters, BvhArgsBaseWords * sizeof(uint32_t)), "席の間接 dispatch を記録できませんでした");
        if (bEmptyClusters)
        {
            barrier();
            command->SetDescriptorSet(clusterDispatch.Set);
            Require(command->DispatchIndirect(resources.Counters,
                (BvhArgsBaseWords + BvhLeafCounter * BvhArgsStrideWords) * sizeof(uint32_t)), "葉の間接 dispatch を記録できませんでした");
        }
        const BufferPtr queueReadback = RecordReadback(fixture, command, resources.Queue);
        const BufferPtr counterReadback = RecordReadback(fixture, command, resources.Counters);
        const BufferPtr drawReadback = RecordReadback(fixture, command, resources.DrawCounts);
        command->End();
        command->Submit(true);
        fixture.Device->WaitIdle();
        return {ReadWords(counterReadback), ReadWords(queueReadback), ReadWords(drawReadback)};
    }

    VariableArray<uint8_t> LeafSet(const CullResult& result, const CullUniformData& uniform)
    {
        Require(result.Counters[BvhLeafCounter] <= uniform.BvhLeafCapacity, "葉の列が溢れています");
        VariableArray<uint8_t> seen;
        seen.resize(uniform.InstanceCount);
        for (uint8_t& value : seen)
        {
            value = 0;
        }
        const uint32_t leafCount = std::min(result.Counters[BvhLeafCounter], uniform.BvhLeafCapacity);
        for (uint32_t leaf = 0; leaf < leafCount; ++leaf)
        {
            const uint32_t word = (uniform.BvhLeafBase + leaf) * 2u;
            const uint32_t instance = result.Queue[word];
            Require(instance < uniform.InstanceCount, "葉の列のインスタンス番号が範囲外です");
            Require((result.Queue[word + 1u] & BvhEntryNodeMask) == 0, "葉の列に根以外の節が入っています");
            Require(seen[instance] == 0, "同じ席が葉の列へ重複して積まれています");
            seen[instance] = 1;
        }
        return seen;
    }

    bool CompareRandom(Fixture& fixture, const Scene& scene, const CameraViewConstants& camera)
    {
        const CullUniformData uniform = MakeUniform(camera, static_cast<uint32_t>(scene.Instances.size()), 512);
        const CullResult result = RunCull(fixture, scene, uniform);
        const auto seen = LeafSet(result, uniform);
        const ReferenceCamera reference(camera);
        uint32_t ambiguous = 0, mismatch = 0, visible = 0;
        uint32_t frustumRejected = 0, distanceRejected = 0, pixelRejected = 0;
        for (uint32_t seat = 0; seat < uniform.InstanceCount; ++seat)
        {
            const Sphere& sphere = scene.Spheres[seat];
            const ReferenceResult expected = Classify(reference, sphere, uniform);
            if (expected.bAmbiguous)
            {
                ++ambiguous;
                continue;
            }
            visible += expected.IsVisible() ? 1u : 0u;
            frustumRejected += !expected.bFrustum ? 1u : 0u;
            distanceRejected += expected.bFrustum && !expected.bDistance ? 1u : 0u;
            pixelRejected += expected.bFrustum && expected.bDistance && !expected.bPixel ? 1u : 0u;
            if ((seen[seat] != 0) != expected.IsVisible())
            {
                ++mismatch;
                if (mismatch <= 8)
                {
                    std::cerr << "席=" << seat << " 中心=(" << sphere.Center[0] << ',' << sphere.Center[1] << ',' << sphere.Center[2]
                        << ") 半径=" << sphere.Radius << " GPU可視=" << static_cast<int>(seen[seat])
                        << " 参照: 視錐台=" << expected.bFrustum << " 距離=" << expected.bDistance << " 画素=" << expected.bPixel << '\n';
                }
            }
        }
        std::cout << "(a) 席=" << uniform.InstanceCount << " 可視=" << result.Counters[BvhLeafCounter]
            << " 参照可視（比較対象）=" << visible << " 曖昧=" << ambiguous << " 不一致=" << mismatch << '\n';
        std::cout << "比較対象の除外: 視錐台=" << frustumRejected << " 距離=" << distanceRejected << " 画素=" << pixelRejected << '\n';
        Require(visible > 100 && frustumRejected > 100 && distanceRejected > 100 && pixelRejected > 100,
            "各判定を通る席・落ちる席が不足しています");
        Require(ambiguous * 100u < uniform.InstanceCount, "曖昧な席が全体の1%以上です");
        return mismatch == 0;
    }

    bool CheckTangencies(Fixture& fixture, const CameraViewConstants& camera)
    {
        const CullUniformData uniform = MakeUniform(camera, 1, 0);
        bool bPassed = true;
        const int planes[] = {static_cast<int>(Math::ClipSpaceFrustumPlane::Near), static_cast<int>(Math::ClipSpaceFrustumPlane::Far)};
        uint32_t alignedPlaneCount = 0;
        for (const int plane : planes)
        {
            const float* p = uniform.FrustumPlanes[plane];
            std::cout << "接触用平面=" << plane << " 値=(" << p[0] << ',' << p[1] << ',' << p[2] << ',' << p[3] << ")\n";
            if (!(p[0] == 0 && p[1] == 0 && std::abs(p[2]) == 1))
            {
                std::cout << "接触用平面=" << plane << " がZ軸にそろわないので省く\n";
                continue;
            }
            ++alignedPlaneCount;
            const float inward = p[2] > 0 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity();
            float z = (-p[3] - 2.0f) * p[2];
            double separation = static_cast<double>(p[2]) * z + static_cast<double>(p[3]) + 2.0;
            // 厳密な接触点がfloatで表せなくても、内側で最も近い隣接値なら偽のカリングを検出できる。
            while (separation < 0)
            {
                z = std::nextafter(z, inward);
                separation = static_cast<double>(p[2]) * z + static_cast<double>(p[3]) + 2.0;
            }
            const double ulp = std::abs(static_cast<double>(std::nextafter(z, inward)) - static_cast<double>(z));
            const auto previousPrecision = std::cout.precision(std::numeric_limits<double>::max_digits10);
            std::cout << "接触用平面=" << plane << " 中心z=" << z << " 内側への距離s=" << separation << " 1ulp=" << ulp << '\n';
            std::cout.precision(previousPrecision);
            Require(separation >= 0 && separation <= 2 * ulp, "接触球の中心が平面の内側の2ulp以内に収まりません");
            Scene scene;
            InitializeMeshes(scene);
            AddInstance(scene, 0, Math::Vector3(0, 0, z), Math::Vector3(1, 1, 1), 0);
            const auto result = RunCull(fixture, scene, uniform);
            const auto seen = LeafSet(result, uniform);
            const uint32_t mismatch = seen[0] == 1 ? 0u : 1u;
            std::cout << (plane == planes[0] ? "近平面の接触" : "遠平面の接触")
                << " 席=1 可視=" << result.Counters[BvhLeafCounter] << " 曖昧=0 不一致=" << mismatch << '\n';
            bPassed = mismatch == 0 && bPassed;
        }
        Require(alignedPlaneCount > 0, "Z軸にそろった接触用平面が1つもありません");
        Scene scene;
        InitializeMeshes(scene);
        AddInstance(scene, 0, Math::Vector3(0, 0, -514), Math::Vector3(1, 1, 1), 0);
        const CullUniformData distanceUniform = MakeUniform(camera, 1, 512);
        Require(Classify(ReferenceCamera(camera), scene.Spheres[0], distanceUniform).IsVisible(), "距離の接触球が参照で不可視です");
        const auto result = RunCull(fixture, scene, distanceUniform);
        const auto seen = LeafSet(result, distanceUniform);
        std::cout << "距離の接触 席=1 可視=" << result.Counters[BvhLeafCounter] << " 曖昧=0 不一致=" << (seen[0] == 1 ? 0 : 1) << '\n';
        return seen[0] == 1 && bPassed;
    }

    bool CheckAllOutside(Fixture& fixture, const Scene& scene)
    {
        const auto camera = MakeCamera(fixture.Device, true);
        const auto uniform = MakeUniform(camera, static_cast<uint32_t>(scene.Instances.size()), 0);
        const auto result = RunCull(fixture, scene, uniform, true);
        LeafSet(result, uniform);
        uint32_t mismatch = result.Counters[0] == 0 ? 0u : 1u;
        mismatch += result.Counters[BvhLeafCounter] == 0 ? 0u : 1u;
        for (uint32_t component = 0; component < 3; ++component)
        {
            mismatch += result.Counters[BvhArgsBaseWords + component] == 0 ? 0u : 1u;
            mismatch += result.Counters[BvhArgsBaseWords + BvhLeafCounter * BvhArgsStrideWords + component] == 0 ? 0u : 1u;
        }
        for (const uint32_t count : result.DrawCounts)
        {
            mismatch += count == 0 ? 0u : 1u;
        }
        const ReferenceCamera reference(camera);
        for (const Sphere& sphere : scene.Spheres)
        {
            Require(!Classify(reference, sphere, uniform).bFrustum, "全部が視錐台外という入力条件を満たしていません");
        }
        std::cout << "(b) 席=" << uniform.InstanceCount << " 可視=" << result.Counters[BvhLeafCounter]
            << " 曖昧=0 不一致=" << mismatch << " 通過範囲=" << result.Counters[0] << '\n';
        return mismatch == 0;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return Skip("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が設定されています");
        }
        VulkanValidationErrorCapture capture;
        RHIDeviceDesc desc;
        desc.Api = GraphicsAPI::Vulkan;
        desc.bEnableValidation = true;
        DevicePtr device = RHI::CreateRHIDevice(desc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return Skip("Vulkanデバイスを利用できません");
        }
        if (!device->GetCapabilities().bBufferDeviceAddress)
        {
            return Skip("BVHに必要なデバイスアドレスを利用できません");
        }
        ShaderManager manager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        Require(manager.Initialize(device.get(), shaderRoot), "シェーダー管理を初期化できませんでした");
        bool bPassed = true;
        {
            Fixture fixture;
            fixture.Device = device;
            ShaderPtr shader = manager.LoadShader("cluster_bvh_cull.comp", RHI::ShaderStage::Compute);
            Require(shader != nullptr, "本番のカリングシェーダーを読み込めませんでした");
            ComputePipelineDesc pipeline;
            pipeline.computeShader = shader;
            pipeline.descriptorSetLayouts.push_back(MegaGeometryPass::BuildCullDescriptorSetDesc());
            fixture.Pipeline = device->CreateComputePipeline(pipeline);
            fixture.Sampler = device->CreateSampler(SamplerDesc{});
            TextureDesc texture;
            texture.Width = texture.Height = texture.MipLevels = 1;
            texture.TextureFormat = Format::R32_FLOAT;
            texture.Usage = ResourceUsage::ShaderRead;
            texture.DebugName = "InstanceCullBlackHZB";
            fixture.Black = device->CreateTexture(texture);
            Require(fixture.Pipeline && fixture.Sampler && fixture.Black, "判定用の資源を作れませんでした");
            const float black = 0;
            fixture.Black->Update(&black, sizeof(black), sizeof(black), 0, 0);
            const auto camera = MakeCamera(device);
            const Scene scene = MakeRandomScene();
            bPassed = CompareRandom(fixture, scene, camera) && bPassed;
            bPassed = CheckTangencies(fixture, camera) && bPassed;
            bPassed = CheckAllOutside(fixture, scene) && bPassed;
            device->WaitIdle();
        }
        manager.Shutdown();
        device->WaitIdle();
        device = nullptr;
        const uint32_t validationErrors = capture.GetHitCount();
        std::cout << "Vulkan検証エラー=" << validationErrors << '\n';
        bPassed = validationErrors == 0 && bPassed;
        std::cout << (bPassed ? "結果=成功" : "結果=失敗") << '\n';
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
        std::cerr << "MegaGeometryInstanceCullVulkanTest で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
