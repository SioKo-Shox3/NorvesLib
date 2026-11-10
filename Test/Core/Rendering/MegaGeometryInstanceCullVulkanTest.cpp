// 本番の範囲・席の判定から BVH の段1へ渡る集合を、倍精度の幾何の参照と照合する。
// 視錐台・距離・画素による取りこぼし、重複、空の間接 dispatch の誤った書き込みを検出する。
// 記述子・表・列の契約は本番の公開定義を使い、各ケースで資源と記録の道具を共有する。
// (c) 前と今の HZB による2パスで可視球を取りこぼさず、両姿勢で深く隠れた球だけは葉へ渡さない。
// (d) 前の HZB の契約が不一致なら遮蔽を使わず、(e) 回す列が溢れても全根の見直しで可視球を救う。
#include "Rendering/CameraViewConstants.h"
#include "Rendering/HiZPyramidPass.h"
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
        resources.Pass2 = MakeBuffer(fixture.Device, BvhPass2BufferWords(uniform.BvhDeferCapacity,
            uniform.BvhDeferNodeCapacity, uniform.BvhDeferClusterCapacity) * sizeof(uint32_t), storage | ResourceUsage::IndirectBuffer);
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

    DispatchResources MakeDispatch(Fixture& fixture, const CullResources& resources, const CullUniformData& uniform,
                                   const TexturePtr& currentHiZ = nullptr, const TexturePtr& previousHiZ = nullptr)
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
                const TexturePtr texture = binding.binding == CullHiZBinding
                    ? (uniform.CullPass == CULL_PASS_SECOND && currentHiZ ? currentHiZ : fixture.Black)
                    : (uniform.bPrevHiZEnabled != 0 && previousHiZ ? previousHiZ : fixture.Black);
                dispatch.Set->BindTexture(binding.binding, texture);
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

    struct TwoPassResult
    {
        CullResult First, Second;
        VariableArray<uint32_t> Header;
    };

    // 列は両パスで共有する。読み戻しを挟んでも頭を消すのは1パス目だけで、2パス目はカウンタだけを戻す。
    TwoPassResult RunTwoPassCull(Fixture& fixture, const Scene& scene, const CullUniformData& base,
                                const MegaGeometryPass::CullOcclusionInputs& inputs,
                                const TexturePtr& currentHiZ, const TexturePtr& previousHiZ)
    {
        const CullResources resources = MakeResources(fixture, scene, base);
        CullUniformData first = base;
        first.CullPass = CULL_PASS_FIRST;
        MegaGeometryPass::ApplyCullOcclusion(first, first.CullPass, inputs);
        CullUniformData range = first;
        range.BvhStage = BvhStageRanges;
        CullUniformData seats = first;
        seats.BvhStage = 0;
        seats.bBvhSeatsFromRanges = BvhSeatsFromRanges;
        seats.BvhNextBase = base.BvhNodeCapacity;
        CullUniformData roots = base;
        roots.CullPass = CULL_PASS_SECOND;
        MegaGeometryPass::ApplyCullOcclusion(roots, roots.CullPass, inputs);
        roots.BvhStage = 0;
        roots.bBvhSeatsFromRanges = BvhSeatsFromRoots;
        roots.BvhNextBase = base.BvhNodeCapacity;
        CullUniformData deferredRanges = roots;
        deferredRanges.bBvhSeatsFromRanges = BvhSeatsFromDeferred;
        CullUniformData deferredNodes = roots;
        deferredNodes.BvhStage = BvhStageDeferredNodes;
        const DispatchResources rangeDispatch = MakeDispatch(fixture, resources, range, currentHiZ, previousHiZ);
        const DispatchResources seatDispatch = MakeDispatch(fixture, resources, seats, currentHiZ, previousHiZ);
        const DispatchResources rootDispatch = MakeDispatch(fixture, resources, roots, currentHiZ, previousHiZ);
        const DispatchResources deferredRangeDispatch = MakeDispatch(fixture, resources, deferredRanges, currentHiZ, previousHiZ);
        const DispatchResources deferredNodeDispatch = MakeDispatch(fixture, resources, deferredNodes, currentHiZ, previousHiZ);
        CommandListPtr command = fixture.Device->CreateCommandList();
        Require(command != nullptr, "2パスのコマンドリストを作れませんでした");
        command->Begin();
        for (const BufferPtr& buffer : {resources.Instances, resources.Meshes, resources.Nodes, resources.Ranges})
        {
            command->BufferBarrier(buffer, ResourceState::Common, ResourceState::GenericRead);
        }
        command->BufferBarrier(resources.Queue, ResourceState::Common, ResourceState::UnorderedAccess);
        for (const BufferPtr& buffer : {resources.Counters, resources.Pass2, resources.Stats, resources.DrawCounts, resources.Dummy})
        {
            command->BufferBarrier(buffer, ResourceState::Common, ResourceState::CopyDest);
            const uint64_t bytes = buffer == resources.Pass2 ? BvhPass2HeaderWords * sizeof(uint32_t) : buffer->GetSize();
            command->FillBuffer(buffer, 0, bytes, 0);
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
            command->BufferBarrier(resources.Stats, ResourceState::UnorderedAccess, ResourceState::UnorderedAccess);
        };
        barrier();
        command->BufferBarrier(resources.Pass2, ResourceState::UnorderedAccess, ResourceState::UnorderedAccess);
        command->BufferBarrier(resources.Counters, ResourceState::UnorderedAccess, ResourceState::IndirectArgument);
        command->SetDescriptorSet(seatDispatch.Set);
        Require(command->DispatchIndirect(resources.Counters, BvhArgsBaseWords * sizeof(uint32_t)),
            "1パス目の席の間接 dispatch を記録できませんでした");
        const BufferPtr firstQueue = RecordReadback(fixture, command, resources.Queue);
        const BufferPtr firstCounters = RecordReadback(fixture, command, resources.Counters);

        command->BufferBarrier(resources.Queue, ResourceState::CopySource, ResourceState::UnorderedAccess);
        command->BufferBarrier(resources.Counters, ResourceState::CopySource, ResourceState::CopyDest);
        command->FillBuffer(resources.Counters, 0, BvhCounterBytes, 0);
        command->BufferBarrier(resources.Counters, ResourceState::CopyDest, ResourceState::UnorderedAccess);
        command->BufferBarrier(resources.Counters, ResourceState::CopyDest, ResourceState::IndirectArgument);
        command->BufferBarrier(resources.Pass2, ResourceState::UnorderedAccess, ResourceState::UnorderedAccess);
        command->BufferBarrier(resources.Pass2, ResourceState::UnorderedAccess, ResourceState::IndirectArgument);
        command->BufferBarrier(resources.Stats, ResourceState::UnorderedAccess, ResourceState::UnorderedAccess);
        command->SetDescriptorSet(rootDispatch.Set);
        // 全根の引数はバッファ先頭（本番の DispatchIndirect と同じ）。列のオフセットは公開定義で求める。
        Require(command->DispatchIndirect(resources.Pass2, 0), "2パス目の根の間接 dispatch を記録できませんでした");
        barrier();
        command->SetDescriptorSet(deferredRangeDispatch.Set);
        Require(command->DispatchIndirect(resources.Pass2, BvhDeferArgsWord * sizeof(uint32_t)),
            "回した範囲の間接 dispatch を記録できませんでした");
        barrier();
        command->SetDescriptorSet(deferredNodeDispatch.Set);
        Require(command->DispatchIndirect(resources.Pass2, BvhDeferNodeArgsWord * sizeof(uint32_t)),
            "回した節の間接 dispatch を記録できませんでした");
        const BufferPtr secondQueue = RecordReadback(fixture, command, resources.Queue);
        const BufferPtr secondCounters = RecordReadback(fixture, command, resources.Counters);
        const BufferPtr header = RecordReadback(fixture, command, resources.Pass2);
        command->End();
        command->Submit(true);
        fixture.Device->WaitIdle();
        TwoPassResult result;
        result.First.Queue = ReadWords(firstQueue);
        result.First.Counters = ReadWords(firstCounters);
        result.Second.Queue = ReadWords(secondQueue);
        result.Second.Counters = ReadWords(secondCounters);
        result.Header = ReadWords(header);
        result.Header.resize(BvhPass2HeaderWords);
        return result;
    }

    struct Point3
    {
        double X, Y, Z;
        Point3 operator-(const Point3& other) const { return {X - other.X, Y - other.Y, Z - other.Z}; }
        double Dot(const Point3& other) const { return X * other.X + Y * other.Y + Z * other.Z; }
        Point3 Cross(const Point3& other) const
        {
            return {Y * other.Z - Z * other.Y, Z * other.X - X * other.Z, X * other.Y - Y * other.X};
        }
    };

    struct Occluder
    {
        double HalfWidth = 24, HalfHeight = 14, Z = -50;
    };

    void Project(const ReferenceCamera& camera, const Point3& point, double clip[4])
    {
        for (int row = 0; row < 4; ++row)
        {
            clip[row] = camera.VP[row][0] * point.X + camera.VP[row][1] * point.Y
                + camera.VP[row][2] * point.Z + camera.VP[row][3];
        }
    }

    // 画素中心を通る直線と z=面の平面の交点を、投影の二つの一次方程式から倍精度で求める。
    // HZB の照会式は使わず、合成深度と参照のどちらもワールドに固定された同じ四角形を扱う。
    bool IntersectPlane(const ReferenceCamera& camera, double ndcX, double ndcY, const Occluder& panel,
                        Point3& point, double& depth)
    {
        double equations[2][3];
        const double ndc[] = {ndcX, ndcY};
        for (int row = 0; row < 2; ++row)
        {
            equations[row][0] = camera.VP[row][0] - ndc[row] * camera.VP[3][0];
            equations[row][1] = camera.VP[row][1] - ndc[row] * camera.VP[3][1];
            equations[row][2] = -(camera.VP[row][2] - ndc[row] * camera.VP[3][2]) * panel.Z
                - camera.VP[row][3] + ndc[row] * camera.VP[3][3];
        }
        const double determinant = equations[0][0] * equations[1][1] - equations[0][1] * equations[1][0];
        if (std::abs(determinant) < 1e-12)
        {
            return false;
        }
        point = {(equations[0][2] * equations[1][1] - equations[0][1] * equations[1][2]) / determinant,
            (equations[0][0] * equations[1][2] - equations[0][2] * equations[1][0]) / determinant, panel.Z};
        double clip[4];
        Project(camera, point, clip);
        if (!(clip[3] > 0))
        {
            return false;
        }
        depth = clip[2] / clip[3];
        return depth > 0 && depth < 1;
    }

    TexturePtr MakeOccluderDepth(Fixture& fixture, const ReferenceCamera& camera, const Occluder& panel)
    {
        VariableArray<float> depth;
        depth.resize(static_cast<size_t>(ScreenWidth) * ScreenHeight);
        for (uint32_t y = 0; y < ScreenHeight; ++y)
        {
            for (uint32_t x = 0; x < ScreenWidth; ++x)
            {
                Point3 point{};
                double z = 1;
                const bool bHit = IntersectPlane(camera, 2.0 * (x + 0.5) / ScreenWidth - 1,
                    2.0 * (y + 0.5) / ScreenHeight - 1, panel, point, z)
                    && std::abs(point.X) <= panel.HalfWidth && std::abs(point.Y) <= panel.HalfHeight;
                // 本番と同じ Less 深度。何にも当たらない画素は 1、近い面ほど小さい。
                depth[static_cast<size_t>(y) * ScreenWidth + x] = bHit ? static_cast<float>(z) : 1.0f;
            }
        }
        TextureDesc desc;
        desc.Width = ScreenWidth;
        desc.Height = ScreenHeight;
        desc.MipLevels = 1;
        desc.TextureFormat = Format::R32_FLOAT;
        desc.Usage = ResourceUsage::ShaderRead;
        desc.DebugName = "InstanceCullOccluderDepth";
        TexturePtr texture = fixture.Device->CreateTexture(desc);
        Require(texture != nullptr, "合成深度を作れませんでした");
        const uint32_t pitch = ScreenWidth * sizeof(float);
        texture->Update(depth.data(), pitch, pitch * ScreenHeight, 0, 0);
        return texture;
    }

    void BuildOcclusionPyramids(Fixture& fixture, HiZPyramid& previous, HiZPyramid& current,
                               const CameraViewConstants& previousCamera, const CameraViewConstants& currentCamera,
                               const Occluder& previousPanel, const Occluder& currentPanel)
    {
        const TexturePtr previousDepth = MakeOccluderDepth(fixture, ReferenceCamera(previousCamera), previousPanel);
        const TexturePtr currentDepth = MakeOccluderDepth(fixture, ReferenceCamera(currentCamera), currentPanel);
        CommandListPtr command = fixture.Device->CreateCommandList();
        Require(command != nullptr, "HZB のコマンドリストを作れませんでした");
        command->Begin();
        Require(previous.Build(command.get(), previousDepth), "前の HZB を作れませんでした");
        Require(current.Build(command.get(), currentDepth), "今の HZB を作れませんでした");
        command->End();
        command->Submit(true);
        fixture.Device->WaitIdle();
    }

    // 仕様の参照: 球全体が面より奥の半空間と、視点・四角形の各辺による四つの内半空間に
    // 入るときだけ隠れている。相対1e-4の帯にかかる球は見える側へ入れ、比較から除外しない。
    bool IsVisibleAroundPanel(const ReferenceCamera& camera, const Sphere& sphere, const Occluder& panel)
    {
        const Point3 eye{camera.Position[0], camera.Position[1], camera.Position[2]};
        const Point3 center{sphere.Center[0], sphere.Center[1], sphere.Center[2]};
        const double distance = std::sqrt((center - eye).Dot(center - eye));
        const double margin = sphere.Radius + 1e-4 * std::max(1.0, distance + sphere.Radius);
        if (panel.Z - center.Z <= margin)
        {
            return true;
        }
        const Point3 corners[] = {{-panel.HalfWidth, -panel.HalfHeight, panel.Z},
            {panel.HalfWidth, -panel.HalfHeight, panel.Z}, {panel.HalfWidth, panel.HalfHeight, panel.Z},
            {-panel.HalfWidth, panel.HalfHeight, panel.Z}};
        const Point3 panelCenter{0, 0, panel.Z};
        for (int edge = 0; edge < 4; ++edge)
        {
            Point3 normal = (corners[edge] - eye).Cross(corners[(edge + 1) % 4] - eye);
            if (normal.Dot(panelCenter - eye) < 0)
            {
                normal = {-normal.X, -normal.Y, -normal.Z};
            }
            if (normal.Dot(center - eye) <= margin * std::sqrt(normal.Dot(normal)))
            {
                return true;
            }
        }
        return false;
    }

    // 球上の NDC 深度の最小値は、z-d*w=0 の平面が球に接する二つの d のうち小さい方。
    // 射影の接平面を解く幾何の参照であり、シェーダーの球・HZB の近似式とは独立している。
    double NearestSphereDepth(const ReferenceCamera& camera, const Sphere& sphere)
    {
        double clip[4];
        Project(camera, {sphere.Center[0], sphere.Center[1], sphere.Center[2]}, clip);
        double zz = 0, zw = 0, ww = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            zz += camera.VP[2][axis] * camera.VP[2][axis];
            zw += camera.VP[2][axis] * camera.VP[3][axis];
            ww += camera.VP[3][axis] * camera.VP[3][axis];
        }
        const double rr = sphere.Radius * sphere.Radius;
        const double a = clip[3] * clip[3] - rr * ww;
        const double b = -2 * clip[2] * clip[3] + 2 * rr * zw;
        const double c = clip[2] * clip[2] - rr * zz;
        Require(a > 0, "深度の参照の球が視点をまたいでいます");
        return (-b - std::sqrt(std::max(0.0, b * b - 4 * a * c))) / (2 * a);
    }

    // 深く隠れる契約: AABB の8頂点の投影矩形を max(幅, 高さ)+16画素ずつ全辺で広げる。
    // 広げた矩形が画面と面の投影の内側にあり、面深度の4隅の最大が球の最前深度より1e-5手前なら十分な余裕がある。
    bool IsDeeplyHidden(const ReferenceCamera& camera, const Sphere& sphere, const Occluder& panel)
    {
        if (IsVisibleAroundPanel(camera, sphere, panel))
        {
            return false;
        }
        double low[2] = {1e100, 1e100}, high[2] = {-1e100, -1e100};
        for (int corner = 0; corner < 8; ++corner)
        {
            double coordinates[3];
            for (int axis = 0; axis < 3; ++axis)
            {
                coordinates[axis] = sphere.Center[axis] + ((corner & (1 << axis)) ? sphere.Radius : -sphere.Radius);
            }
            double clip[4];
            Project(camera, {coordinates[0], coordinates[1], coordinates[2]}, clip);
            if (!(clip[3] > 0))
            {
                return false;
            }
            for (int axis = 0; axis < 2; ++axis)
            {
                const double pixel = (clip[axis] / clip[3] + 1) * 0.5 * (axis == 0 ? ScreenWidth : ScreenHeight);
                low[axis] = std::min(low[axis], pixel);
                high[axis] = std::max(high[axis], pixel);
            }
        }
        const double expansion = std::max(high[0] - low[0], high[1] - low[1]) + 16;
        double maxPanelDepth = 0;
        for (int corner = 0; corner < 4; ++corner)
        {
            const double x = (corner & 1) ? high[0] + expansion : low[0] - expansion;
            const double y = (corner & 2) ? high[1] + expansion : low[1] - expansion;
            Point3 point{};
            double depth = 1;
            if (x <= 0 || x >= ScreenWidth || y <= 0 || y >= ScreenHeight
                || !IntersectPlane(camera, 2 * x / ScreenWidth - 1, 2 * y / ScreenHeight - 1, panel, point, depth)
                || std::abs(point.X) >= panel.HalfWidth || std::abs(point.Y) >= panel.HalfHeight)
            {
                return false;
            }
            maxPanelDepth = std::max(maxPanelDepth, depth);
        }
        return maxPanelDepth + 1e-5 < NearestSphereDepth(camera, sphere);
    }

    Scene MakeOcclusionScene()
    {
        Scene scene;
        InitializeMeshes(scene);
        // 64範囲を、手前・深く隠れる・縁・横の4種類にする。各範囲はコンパクトな8x8席。
        for (uint32_t group = 0; group < 64; ++group)
        {
            const uint32_t index = group / 4;
            const float gx = (static_cast<float>(index % 4) - 1.5f) * 4;
            const float gy = (static_cast<float>(index / 4) - 1.5f) * 3;
            for (uint32_t seat = 0; seat < BvhRangeSeats; ++seat)
            {
                float x = gx, y = gy, z = -90;
                switch (group % 4)
                {
                case 0: z = -30; break;
                case 1: break;
                case 2: x = (index % 2 ? -1.0f : 1.0f) * (40 + static_cast<float>((index / 2) % 4) * 1.4f); break;
                case 3: x = index % 2 ? -62.0f : 62.0f; z = -100; break;
                }
                x += (static_cast<float>(seat % 8) - 3.5f) * 0.08f;
                y += (static_cast<float>(seat / 8) - 3.5f) * 0.08f;
                AddInstance(scene, 0, Math::Vector3(x, y, z), Math::Vector3(0.175f, 0.175f, 0.175f), 0);
            }
        }
        return scene;
    }

    CameraViewConstants MakeOcclusionCamera(const DevicePtr& device, uint32_t pose, bool bPrevious)
    {
        const float sign = pose % 2 ? -1.0f : 1.0f;
        CameraProxy camera;
        camera.PositionX = sign * (bPrevious ? -3.5f : 3.5f);
        camera.PositionY = (static_cast<float>((pose / 2) % 4) - 1.5f) * (bPrevious ? 0.6f : -0.6f);
        camera.PositionZ = bPrevious ? 0.5f : -0.5f;
        const double yaw = (static_cast<double>(pose / 4) + 1) * 0.008 * (bPrevious ? -1 : 1);
        const double pitch = (static_cast<double>(pose % 4) - 1.5) * 0.006 * (bPrevious ? 1 : -1);
        camera.ForwardX = static_cast<float>(std::sin(yaw) * std::cos(pitch));
        camera.ForwardY = static_cast<float>(std::sin(pitch));
        camera.ForwardZ = static_cast<float>(-std::cos(yaw) * std::cos(pitch));
        camera.UpX = camera.UpZ = 0;
        camera.UpY = 1;
        camera.FieldOfView = 60;
        camera.NearPlane = 0.5f;
        camera.FarPlane = 1024;
        return CameraViewConstants::BuildForDevice(camera, static_cast<float>(ScreenWidth) / ScreenHeight, device.get());
    }

    CullUniformData MakeOcclusionUniform(const CameraViewConstants& camera, const Scene& scene)
    {
        CullUniformData uniform = MakeUniform(camera, static_cast<uint32_t>(scene.Instances.size()), 0);
        uniform.BvhDeferCapacity = uniform.BvhRangeCount;
        uniform.BvhDeferNodeCapacity = uniform.BvhDeferClusterCapacity = uniform.InstanceCount;
        // 画素の判定を無効にし、全席が視錐台内であることは各姿勢で独立に確認する。
        uniform.PixelCullWidth = uniform.PixelCullHeight = 0;
        return uniform;
    }

    MegaGeometryPass::CullOcclusionInputs MakeOcclusionInputs(const CameraViewConstants& previousCamera,
                                                             const HiZPyramid& previous, const HiZPyramid& current,
                                                             bool bUsePrevious = true)
    {
        MegaGeometryPass::CullOcclusionInputs inputs;
        inputs.DepthWidth = ScreenWidth;
        inputs.DepthHeight = ScreenHeight;
        inputs.bCurrentHiZ = current.IsReady();
        inputs.CurrentHiZMipCount = current.GetMipCount();
        inputs.bPrevHiZ = bUsePrevious;
        inputs.PrevHiZMipCount = previous.GetMipCount();
        inputs.PrevViewProjection = previousCamera.ViewProjectionMatrix;
        return inputs;
    }

    struct OcclusionCounts
    {
        uint32_t Visible = 0, Deep = 0, FalseCulled = 0, DeepDrawn = 0;
        uint32_t Hidden = 0, FalseKept = 0, Duplicated = 0, Recovered = 0;
    };

    OcclusionCounts CompareOcclusion(const Scene& scene, const CullUniformData& uniform, const TwoPassResult& result,
                                    const CameraViewConstants& previousCamera, const CameraViewConstants& currentCamera,
                                    uint32_t pose, const char* label, bool bRequireDeepHidden = true)
    {
        const auto first = LeafSet(result.First, uniform);
        const auto second = LeafSet(result.Second, uniform);
        const ReferenceCamera previous(previousCamera), current(currentCamera);
        const Occluder panel;
        OcclusionCounts counts;
        for (uint32_t seat = 0; seat < uniform.InstanceCount; ++seat)
        {
            const Sphere& sphere = scene.Spheres[seat];
            Require(Classify(current, sphere, uniform).IsVisible(), "遮蔽以外で落ちる席が入力に含まれています");
            const bool bVisible = IsVisibleAroundPanel(current, sphere, panel);
            const bool bDeep = IsDeeplyHidden(previous, sphere, panel) && IsDeeplyHidden(current, sphere, panel);
            const bool bDrawn = first[seat] || second[seat];
            counts.Visible += bVisible ? 1u : 0u;
            counts.Deep += bDeep ? 1u : 0u;
            counts.Hidden += !bVisible ? 1u : 0u;
            counts.FalseKept += !bVisible && bDrawn ? 1u : 0u;
            counts.Duplicated += first[seat] && second[seat] ? 1u : 0u;
            counts.Recovered += !first[seat] && second[seat] ? 1u : 0u;
            const bool bFalseCulled = bVisible && !bDrawn;
            const bool bDeepDrawn = bDeep && bDrawn;
            counts.FalseCulled += bFalseCulled ? 1u : 0u;
            counts.DeepDrawn += bDeepDrawn ? 1u : 0u;
            if ((bFalseCulled || (bRequireDeepHidden && bDeepDrawn))
                && counts.FalseCulled + (bRequireDeepHidden ? counts.DeepDrawn : 0u) <= 8)
            {
                std::cerr << label << " 姿勢=" << pose << " 席=" << seat << " 中心=(" << sphere.Center[0]
                    << ',' << sphere.Center[1] << ',' << sphere.Center[2] << ") 半径=" << sphere.Radius
                    << " 偽のカリング=" << bFalseCulled << " 深く隠れるのに残る=" << bDeepDrawn << '\n';
            }
        }
        std::cout << label << " 姿勢=" << pose << " 席=" << uniform.InstanceCount << " 見える=" << counts.Visible
            << " 深く隠れる=" << counts.Deep << " 1パス目=" << result.First.Counters[BvhLeafCounter]
            << " 2パス目=" << result.Second.Counters[BvhLeafCounter] << " 偽のカリング=" << counts.FalseCulled
            << " 深く隠れるのに残る=" << counts.DeepDrawn << " 偽の残し=" << counts.FalseKept << '/' << counts.Hidden
            << " 割合=" << (counts.Hidden ? 100.0 * counts.FalseKept / counts.Hidden : 0) << "% 重複=" << counts.Duplicated
            << " 2パス目で救済=" << counts.Recovered << " 曖昧の除外=0\n";
        return counts;
    }

    bool CheckTwoPassOcclusion(Fixture& fixture, ShaderManager& manager)
    {
        HiZPyramid previous, current;
        Require(previous.Initialize(fixture.Device.get(), &manager) && current.Initialize(fixture.Device.get(), &manager),
            "2パスの HZB を初期化できませんでした");
        const Scene scene = MakeOcclusionScene();
        const Occluder panel;
        uint32_t totalHidden = 0, totalKept = 0, totalRecovered = 0, totalDuplicated = 0;
        bool bPassed = true;
        for (uint32_t pose = 0; pose < 16; ++pose)
        {
            const auto previousCamera = MakeOcclusionCamera(fixture.Device, pose, true);
            const auto currentCamera = MakeOcclusionCamera(fixture.Device, pose, false);
            BuildOcclusionPyramids(fixture, previous, current, previousCamera, currentCamera, panel, panel);
            const auto uniform = MakeOcclusionUniform(currentCamera, scene);
            const auto inputs = MakeOcclusionInputs(previousCamera, previous, current);
            const auto result = RunTwoPassCull(fixture, scene, uniform, inputs, current.GetTexture(), previous.GetTexture());
            Require(result.Header[BvhPass2FallbackWord] == 0, "通常の容量で回す列が溢れています");
            const auto counts = CompareOcclusion(scene, uniform, result, previousCamera, currentCamera, pose, "(c)");
            Require(counts.Visible > 0 && counts.Deep > 0, "見える席か深く隠れる席がありません");
            bPassed = counts.FalseCulled == 0 && counts.DeepDrawn == 0 && bPassed;
            totalHidden += counts.Hidden;
            totalKept += counts.FalseKept;
            totalRecovered += counts.Recovered;
            totalDuplicated += counts.Duplicated;
            if (pose == 0)
            {
                Require(result.Header[BvhDeferCountWord] > 2, "溢れの場面で回す範囲が不足しています");
                auto limitedUniform = uniform;
                limitedUniform.BvhDeferCapacity = 2;
                const auto overflow = RunTwoPassCull(fixture, scene, limitedUniform, inputs, current.GetTexture(), previous.GetTexture());
                // 全根の引数は間接バッファの先頭。公開の頭の範囲内だけを読む。
                bool bRescan = overflow.Header[BvhPass2FallbackWord] != 0;
                for (uint32_t component = 0; component < 3; ++component)
                {
                    bRescan = bRescan || overflow.Header[component] != 0;
                }
                const auto overflowCounts = CompareOcclusion(scene, limitedUniform, overflow, previousCamera, currentCamera, pose, "(e)", false);
                std::cout << "(e) 回した範囲=" << overflow.Header[BvhDeferCountWord] << " 容量=" << limitedUniform.BvhDeferCapacity
                    << " 全根の見直し=" << bRescan << " 溢れ=" << overflow.Header[BvhPass2FallbackWord] << '\n';
                bPassed = bRescan && overflowCounts.FalseCulled == 0 && bPassed;
            }
        }
        std::cout << "(c) 合計 偽の残し=" << totalKept << '/' << totalHidden << " 割合="
            << (totalHidden ? 100.0 * totalKept / totalHidden : 0) << "% 重複=" << totalDuplicated
            << " 2パス目で救済=" << totalRecovered << '\n';
        Require(totalRecovered > 0, "前の HZB で回されて2パス目で描かれた席がありません");

        // (d) 実在する前の HZB は全席を覆う面にする。契約が不一致なら、この深度ではなく黒が結ばれる。
        const auto previousCamera = MakeOcclusionCamera(fixture.Device, 0, true);
        const auto currentCamera = MakeOcclusionCamera(fixture.Device, 0, false);
        const Occluder fullPanel{1000, 1000, -10};
        BuildOcclusionPyramids(fixture, previous, current, previousCamera, currentCamera, fullPanel, panel);
        const auto uniform = MakeOcclusionUniform(currentCamera, scene);
        for (const auto& sphere : scene.Spheres)
        {
            Require(!IsVisibleAroundPanel(ReferenceCamera(previousCamera), sphere, fullPanel), "前の面が全席を隠していません");
        }
        MegaHiZContract previousContract;
        previousContract.bValid = true;
        previousContract.RenderFrame = 40;
        previousContract.GameFrame = 60;
        previousContract.DepthWidth = ScreenWidth;
        previousContract.DepthHeight = ScreenHeight;
        previousContract.ViewProjection = previousCamera.ViewProjectionMatrix;
        MegaHiZContract currentContract = previousContract;
        currentContract.RenderFrame++;
        currentContract.GameFrame++;
        currentContract.ViewProjection = currentCamera.ViewProjectionMatrix;
        Require(EvaluatePreviousHiZ(previousContract, currentContract, false) == MegaPrevHiZStatus::Ready,
            "契約不一致の試験の元が Ready ではありません");
        const MegaPrevHiZStatus expected[] = {MegaPrevHiZStatus::Resize, MegaPrevHiZStatus::Stale, MegaPrevHiZStatus::Epoch};
        const char* reasons[] = {"寸法", "記録の飛び", "エポック"};
        for (uint32_t test = 0; test < 3; ++test)
        {
            MegaHiZContract changed = currentContract;
            if (test == 0)
            {
                changed.DepthWidth++;
            }
            if (test == 1)
            {
                changed.RenderFrame++;
            }
            const auto status = EvaluatePreviousHiZ(previousContract, changed, test == 2);
            Require(status == expected[test], "前の HZB の契約の判定理由が違います");
            const bool bUsePrevious = MegaGeometryPass::CanUsePreviousHiZ(status, true);
            Require(!bUsePrevious, "契約が不一致なのに前の HZB が有効です");
            const auto inputs = MakeOcclusionInputs(previousCamera, previous, current, bUsePrevious);
            const auto result = RunTwoPassCull(fixture, scene, uniform, inputs, current.GetTexture(), previous.GetTexture());
            const auto first = LeafSet(result.First, uniform);
            LeafSet(result.Second, uniform);
            uint32_t mismatch = 0;
            for (uint32_t seat = 0; seat < uniform.InstanceCount; ++seat)
            {
                const bool bExpected = Classify(ReferenceCamera(currentCamera), scene.Spheres[seat], uniform).IsVisible();
                mismatch += (first[seat] != 0) != bExpected ? 1u : 0u;
            }
            uint32_t nonzeroHeader = result.Header[BvhDeferCountWord] + result.Header[BvhDeferNodeCountWord];
            for (uint32_t component = 0; component < 3; ++component)
            {
                nonzeroHeader += result.Header[component];
            }
            std::cout << "(d) 理由=" << reasons[test] << " 席=" << uniform.InstanceCount
                << " 前のHZB使用=" << bUsePrevious << " 1パス目=" << result.First.Counters[BvhLeafCounter]
                << " 2パス目=" << result.Second.Counters[BvhLeafCounter] << " 不一致=" << mismatch
                << " 回した範囲=" << result.Header[BvhDeferCountWord] << " 回した節=" << result.Header[BvhDeferNodeCountWord]
                << " 頭の非零の和=" << nonzeroHeader << '\n';
            bPassed = mismatch == 0 && nonzeroHeader == 0 && result.Second.Counters[BvhLeafCounter] == 0 && bPassed;
        }
        return bPassed;
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
            SamplerDesc sampler;
            sampler.filterMin = sampler.filterMag = sampler.filterMip = FilterMode::Linear;
            sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressMode::Clamp;
            fixture.Sampler = device->CreateSampler(sampler);
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
            bPassed = CheckTwoPassOcclusion(fixture, manager) && bPassed;
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
