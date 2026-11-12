// 雨の遮蔽の屋根の高さと位置、スキンメッシュと地形の除外を深度の読み戻しで確かめる。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Rendering/OverheadHeightMapMath.h"
#include "Rendering/OverheadHeightMapPass.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanCommandList.h"
#include "RHI/Vulkan/VulkanTexture.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    struct OverheadHeightMapVulkanTestAccess
    {
        static RHI::TexturePtr Texture(const OverheadHeightMapPass& pass) { return pass.m_Texture; }
        static const OverheadHeightMapPassState& State(const OverheadHeightMapPass& pass)
        {
            return *pass.m_StateData;
        }
    };
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "OverheadHeightMapVulkanTest";
    constexpr int SkipReturnCode = 125;
    constexpr uint32_t Resolution = 1024u;
    constexpr uint64_t ImageBytes = uint64_t{Resolution} * Resolution * sizeof(float);

    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << " 失敗: " << message << '\n';
        }
        return condition;
    }

    bool IsGpuTestSkipForced()
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || !value)
        {
            return false;
        }
        const bool forced = std::strcmp(value, "1") == 0;
        free(value);
        return forced;
    }

    SkyOccluderProxy MakeOccluder(float x, float y, float z, float rx, float ry, float rz, bool box)
    {
        SkyOccluderProxy proxy;
        proxy.CenterX = x;
        proxy.CenterY = y;
        proxy.CenterZ = z;
        proxy.HalfExtentX = rx;
        proxy.HalfExtentY = ry;
        proxy.HalfExtentZ = rz;
        SetSkyOccluderShape(proxy, box ? SkyOccluderShape::Box : SkyOccluderShape::Ellipsoid);
        SetSkyOccluderClass(proxy, box ? SkyOccluderClass::Solid : SkyOccluderClass::Canopy);
        SetSkyOccluderBlocksRain(proxy, true);
        return proxy;
    }

    struct Scene
    {
        SceneProxy Proxy;
        Container::VariableArray<MeshProxy> Meshes;
        Container::VariableArray<SkinnedMeshProxy> SkinnedMeshes;
        Container::VariableArray<MegaGeometryProxy> MegaMeshes;
        Container::TSharedPtr<SkinnedMeshAssetLease> SkinnedLease;
        SkyOccluderProxy Roof = MakeOccluder(10.0f, 3.0f, 5.0f, 4.0f, 0.1f, 3.0f, true);
        SkyOccluderProxy Pillar = MakeOccluder(-10.0f, 3.0f, -10.0f, 0.5f, 3.0f, 0.5f, true);
        SkyOccluderProxy Canopy = MakeOccluder(-8.0f, 5.0f, 12.0f, 3.0f, 2.0f, 3.0f, false);

        bool Initialize(RenderResources& resources)
        {
            Proxy.SkyOccluders.push_back(Roof);
            Proxy.SkyOccluders.push_back(Canopy);

            // 箱の全頂点を登録し、局所 AABB からのメッシュ経路を通す。
            Mesh3DVertex vertices[8]{};
            for (uint32_t corner = 0; corner < 8u; ++corner)
            {
                vertices[corner].Position[0] = (corner & 1u) ? 0.5f : -0.5f;
                vertices[corner].Position[1] = (corner & 2u) ? 6.0f : 0.0f;
                vertices[corner].Position[2] = (corner & 4u) ? 0.5f : -0.5f;
            }
            const uint32_t indices[] = {0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6,
                0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5, 0, 1, 5, 0, 5, 4, 2, 6, 7, 2, 7, 3};
            MeshProxy pillar;
            pillar.MeshHandle = MeshDataHandle{77u};
            pillar.WorldTransform.SetTranslationRow(Math::Vector3(-10.0f, 0.0f, -10.0f));
            pillar.WorldBounds = {-10.0f, 3.0f, -10.0f, 3.1f};
            if (!Check(resources.Meshes().Register(pillar.MeshHandle, vertices, sizeof(vertices), indices, 36u),
                       "柱の箱メッシュを登録できません"))
            {
                return false;
            }
            Meshes.push_back(pillar);

            SkinnedMeshProxy skinned;
            skinned.MeshHandle = {1u, 1u};
            skinned.ComponentId = 1u;
            skinned.bHasAnimatedBounds = true;
            skinned.AnimatedBounds = Math::AABB(Math::Vector3(18.0f, 0.0f, -22.0f),
                                               Math::Vector3(22.0f, 8.0f, -18.0f));
            skinned.WorldTransform.SetTranslationRow(Math::Vector3(20.0f, 0.0f, -20.0f));
            skinned.BonePalette.push_back(Math::Matrix4x4::Identity);
            Container::VariableArray<SkinnedMeshVertex> skinVertices(3u);
            skinVertices[1].Position[0] = 1.0f;
            skinVertices[2].Position[1] = 1.0f;
            for (auto& vertex : skinVertices)
            {
                vertex.BoneWeights[0] = 1.0f;
            }
            Container::VariableArray<uint32_t> skinIndices;
            skinIndices.push_back(0u);
            skinIndices.push_back(1u);
            skinIndices.push_back(2u);
            SkinnedLease = Container::MakeShared<SkinnedMeshAssetLease>(
                skinned.MeshHandle, std::move(skinVertices), std::move(skinIndices));
            skinned.AssetLease = SkinnedLease;
            SkinnedMeshes.push_back(skinned);

            // 1 三角形・1 クラスタの地形を、屋根と重ならない境界球で置く。
            const float terrainVertices[] = {0.0f, 0.0f, 0.0f, 1.0f,
                1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};
            const uint32_t terrainIndices[] = {0u, 1u, 2u};
            MegaGeometry::MeshCluster cluster;
            cluster.IndexCount = 3u;
            cluster.VertexCount = 3u;
            cluster.Bounds = {0.5f, 0.0f, 0.5f, 0.75f};
            MegaGeometry::MegaMeshCreateInfo info;
            info.VertexData = terrainVertices;
            info.VertexDataSize = sizeof(terrainVertices);
            info.VertexCount = 3u;
            info.VertexStride = 4u * sizeof(float);
            info.IndexData = terrainIndices;
            info.IndexCount = 3u;
            info.Clusters.push_back(cluster);
            info.TotalBounds = cluster.Bounds;
            info.bBuildLODHierarchy = false;
            info.Material.TerrainBlend.bTerrainSurface = true;
            info.DebugName = "OverheadHeightMapTest.Terrain";
            MegaGeometryProxy terrain;
            terrain.MegaMeshHandle = resources.MegaGeometry().CreateMegaMesh(info);
            terrain.WorldTransform.SetTranslationRow(Math::Vector3(-20.0f, 1.0f, -20.0f));
            terrain.WorldBounds = {-20.0f, 1.0f, -20.0f, 5.0f};
            if (!Check(terrain.MegaMeshHandle.IsValid() &&
                       resources.MegaGeometry().GetMegaMeshGPUData(terrain.MegaMeshHandle),
                       "地形のメガメッシュを作れません"))
            {
                return false;
            }
            MegaMeshes.push_back(terrain);
            return true;
        }
    };

    struct Image
    {
        OverheadHeightMapWindow Window;
        Container::VariableArray<float> Depths;

        float Depth(uint32_t i, uint32_t j) const { return Depths[size_t{j} * Window.Resolution + i]; }

        bool Texel(float x, float z, uint32_t& i, uint32_t& j) const
        {
            return Check(WorldToOverheadTexel(Window, x, z, i, j), "確認する点が窓の外です");
        }
    };

    bool CopyDepth(ICommandList& commands, const TexturePtr& texture, const BufferPtr& buffer)
    {
        auto* vkCommands = dynamic_cast<Vulkan::VulkanCommandList*>(&commands);
        auto vkTexture = DynamicPointerCast<Vulkan::VulkanTexture>(texture);
        auto vkBuffer = DynamicPointerCast<Vulkan::VulkanBuffer>(buffer);
        if (!Check(vkCommands && vkTexture && vkBuffer, "深度読み戻しの Vulkan 資源を取得できません"))
        {
            return false;
        }
        // RHI の CopyTextureToBuffer は色アスペクト固定なので、D32 は深度アスペクトを指定する。
        vk::BufferImageCopy region{};
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        region.imageSubresource.layerCount = 1u;
        region.imageExtent = vk::Extent3D{Resolution, Resolution, 1u};
        vkCommands->GetVkCommandBuffer().copyImageToBuffer(vkTexture->GetVkImage(),
            vk::ImageLayout::eTransferSrcOptimal, vkBuffer->GetVkBuffer(), 1u, &region);
        return true;
    }

    struct Fixture
    {
        DevicePtr Device;
        ShaderManager Shaders;
        DeviceFrameUploadPageSource PageSource;
        FrameResourceOwners Owners;
        RenderResources Resources;
        RenderGraph Graph;

        ~Fixture()
        {
            if (Device)
            {
                Device->WaitIdle();
            }
            Graph.Shutdown();
            Owners.Reset();
            Owners.ReleaseDescriptorPools();
            Owners.ReleaseUploadPages();
            Resources.Shutdown();
            Shaders.Shutdown();
            PageSource.SetDevice({});
        }

        bool Initialize()
        {
            PageSource.SetDevice(Device);
            Owners.ConfigureUpload(&PageSource, 1u << 20u);
            Resources.SetGeometryPoolBlockBytes(1ull << 20u);
            return Check(Owners.ConfigureDescriptors(Device.get()), "記述子プールを作れません") &&
                Check(Shaders.Initialize(Device.get(), NORVES_SOURCE_ROOT "/Assets/Shaders"),
                      "シェーダーの読み込みを初期化できません") &&
                Check(Resources.Initialize(Device), "描画資源を初期化できません") &&
                Check(Graph.Initialize(nullptr), "レンダーグラフを初期化できません");
        }

        bool Run(OverheadHeightMapPass& pass, const Scene& scene, Image& image)
        {
            ViewRenderContext context;
            context.Device = Device.get();
            context.ShaderMgr = &Shaders;
            if (!Check(pass.Initialize(context), "雨の遮蔽パスを初期化できません"))
            {
                return false;
            }
            BufferPtr readback = Device->CreateBuffer(BufferDesc(ImageBytes, ResourceUsage::TransferDst,
                true, "OverheadHeightMapTest.Readback"));
            CommandListPtr commands = Device->CreateCommandList();
            FrameResourceOwner* frame = Owners.BeginFrame(0u, 0u);
            if (!Check(readback && commands && frame, "読み戻しとフレーム資源を用意できません"))
            {
                Owners.AbortFrame();
                return false;
            }
            CameraProxy camera;
            camera.PositionX = 0.0f;
            camera.PositionY = 2.0f;
            camera.PositionZ = 0.0f;
            ViewportSnapshot viewport;
            viewport.ViewId = 7u;
            viewport.ViewportId = 11u;
            context.Capabilities = &Device->GetCapabilities();
            context.CommandList = commands.get();
            context.FrameResources = frame;
            context.FrameNumber = 1u;
            context.TotalTime = 0.01;
            context.MainCamera = &camera;
            context.CurrentViewport = &viewport;
            context.SnapshotScene = &scene.Proxy;
            context.SnapshotMeshProxies = &scene.Meshes;
            context.SnapshotSkinnedMeshProxies = &scene.SkinnedMeshes;
            context.SnapshotMegaGeometryProxies = &scene.MegaMeshes;
            context.Resources.Meshes = &Resources.Meshes();
            context.Resources.MegaGeometry = &Resources.MegaGeometry();
            commands->Begin();
            Graph.BeginFrame(context.FrameNumber);
            Graph.AddPass(&pass);
            if (!Check(Graph.Compile(context), "レンダーグラフをコンパイルできません"))
            {
                commands->End();
                Owners.AbortFrame();
                return false;
            }
            const RenderGraphExecutionResult result = Graph.ExecuteWithResult(context);
            const auto& state = OverheadHeightMapVulkanTestAccess::State(pass);
            const TexturePtr texture = OverheadHeightMapVulkanTestAccess::Texture(pass);
            if (!Check(result.bSuccess && result.ExecutedPassCount == 1u && state.RedrawState.bHasContent,
                       "最初の 1 回で屋根の中身が描かれません") ||
                !Check(context.OverheadHeightMap.Matches(context.FrameNumber, viewport.ViewId, viewport.ViewportId) &&
                       context.OverheadHeightMap.Texture == texture,
                       "屋根の画像が現在のフレームとビューとビューポートへ公開されません"))
            {
                commands->End();
                Owners.AbortFrame();
                return false;
            }
            image.Window = state.RedrawState.Window;
            // 描画の深度書き込みを待つ。レイアウトはパスが確定した読み取り用から移す。
            ExplicitTextureBarrierDesc barrier;
            barrier.BeforeState = ResourceState::DepthWrite;
            barrier.AfterState = ResourceState::CopySource;
            barrier.OldLayout = TextureLayout::ShaderReadOnly;
            barrier.NewLayout = TextureLayout::TransferSource;
            commands->TextureBarrierExplicit(texture, barrier);
            commands->CommitTextureLayout(texture.get(), TextureLayout::TransferSource);
            commands->BufferBarrier(readback, ResourceState::Undefined, ResourceState::CopyDest);
            if (!CopyDepth(*commands, texture, readback))
            {
                commands->End();
                Owners.AbortFrame();
                return false;
            }
            commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead);
            commands->TextureBarrier(texture, ResourceState::CopySource, ResourceState::ShaderResource);
            commands->End();
            commands->Submit(true);
            Device->WaitIdle();
            if (!Check(Owners.CommitSubmitted(1u), "フレームを提出できません"))
            {
                return false;
            }
            const void* mapped = readback->Map(0u, ImageBytes);
            if (!Check(mapped != nullptr, "屋根の深度を読み戻せません"))
            {
                return false;
            }
            image.Depths.resize(size_t{Resolution} * Resolution);
            std::memcpy(image.Depths.data(), mapped, static_cast<size_t>(ImageBytes));
            readback->Unmap();
            return true;
        }
    };

    bool CheckRoof(const Image& image, const SkyOccluderProxy& shape, float x, float z, const char* name)
    {
        uint32_t i = 0u;
        uint32_t j = 0u;
        if (!image.Texel(x, z, i, j))
        {
            return false;
        }
        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        // 1 テクセルの位置の差を、描いたドームと同じ形の 3×3 近傍の高さの幅へ変換する。
        for (int32_t dz = -1; dz <= 1; ++dz)
        {
            for (int32_t dx = -1; dx <= 1; ++dx)
            {
                float wx = 0.0f;
                float wz = 0.0f;
                float roofY = 0.0f;
                OverheadTexelCenterToWorld(image.Window, i + dx, j + dz, wx, wz);
                if (!Check(EvaluateOverheadRoofTopY(shape, wx, wz, roofY), "屋根の確認点の近傍が形の外です"))
                {
                    return false;
                }
                minimum = std::min(minimum, roofY);
                maximum = std::max(maximum, roofY);
            }
        }
        const float actual = OverheadRoofYFromDepth(image.Window, image.Depth(i, j));
        const bool passed = actual >= minimum - 1.0e-3f && actual <= maximum + 1.0e-3f;
        if (!passed)
        {
            std::cerr << name << " テクセル=(" << i << ',' << j << ") GPU の屋根の高さ=" << actual
                      << " 許容範囲=[" << minimum - 1.0e-3f << ',' << maximum + 1.0e-3f << "] m\n";
        }
        return Check(passed, "屋根の高さが 1 テクセル相当の範囲と一致しません");
    }

    bool CheckOpenTexel(const Image& image, uint32_t i, uint32_t j, const char* name)
    {
        const float depth = image.Depth(i, j);
        if (depth != 1.0f)
        {
            std::cerr << name << " テクセル=(" << i << ',' << j << ") GPU の深度=" << depth
                      << " 期待する深度=1\n";
        }
        return Check(depth == 1.0f, "開けた場所に屋根が写っています");
    }

    bool CheckOpen(const Image& image, float x, float z, const char* name)
    {
        uint32_t i = 0u;
        uint32_t j = 0u;
        return image.Texel(x, z, i, j) && CheckOpenTexel(image, i, j, name);
    }

    bool Verify(const Scene& scene, const Image& image)
    {
        const float roofPoints[][2] = {{8.0f, 4.0f}, {10.0f, 5.0f}, {12.0f, 6.0f}};
        const float pillarPoints[][2] = {{-10.15f, -10.15f}, {-10.0f, -10.0f}, {-9.85f, -9.85f}};
        const float canopyPoints[][2] = {{-8.0f, 12.0f}, {-7.8f, 12.2f}, {-6.5f, 12.0f},
            {-9.5f, 12.0f}, {-8.0f, 13.5f}, {-8.0f, 10.5f}};
        for (const auto& point : roofPoints)
        {
            if (!CheckRoof(image, scene.Roof, point[0], point[1], "箱の屋根")) return false;
        }
        for (const auto& point : pillarPoints)
        {
            if (!CheckRoof(image, scene.Pillar, point[0], point[1], "柱の天面")) return false;
        }
        for (const auto& point : canopyPoints)
        {
            if (!CheckRoof(image, scene.Canopy, point[0], point[1], "樹冠")) return false;
        }

        // 量子化後も辺から 2 テクセル以上離れるよう、2.5 テクセルの位置から選ぶ。
        const float margin = 2.5f * image.Window.TexelMeters;
        const float edges[][4] = {{6.0f + margin, 5.0f, 6.0f - margin, 5.0f},
            {14.0f - margin, 5.0f, 14.0f + margin, 5.0f},
            {10.0f, 2.0f + margin, 10.0f, 2.0f - margin},
            {10.0f, 8.0f - margin, 10.0f, 8.0f + margin}};
        for (const auto& edge : edges)
        {
            if (!CheckRoof(image, scene.Roof, edge[0], edge[1], "箱の辺の内側") ||
                !CheckOpen(image, edge[2], edge[3], "箱の辺の外側")) return false;
        }
        const float openPoints[][2] = {{0.0f, 0.0f}, {30.0f, 30.0f}, {-30.0f, 25.0f}, {25.0f, -30.0f}};
        for (const auto& point : openPoints)
        {
            if (!CheckOpen(image, point[0], point[1], "屋根の無い所")) return false;
        }

        const auto& skin = scene.SkinnedMeshes[0].AnimatedBounds;
        const auto& terrain = scene.MegaMeshes[0].WorldBounds;
        // スキンの AABB と地形の境界球の水平投影の内側は、間引かず全テクセルを調べる。
        for (uint32_t region = 0u; region < 2u; ++region)
        {
            for (uint32_t j = 0u; j < image.Window.Resolution; ++j)
            {
                for (uint32_t i = 0u; i < image.Window.Resolution; ++i)
                {
                    float x = 0.0f;
                    float z = 0.0f;
                    OverheadTexelCenterToWorld(image.Window, i, j, x, z);
                    const float dx = x - terrain.CenterX;
                    const float dz = z - terrain.CenterZ;
                    const bool inside = region == 0u
                        ? x >= skin.Min.x && x <= skin.Max.x && z >= skin.Min.z && z <= skin.Max.z
                        : dx * dx + dz * dz <= terrain.Radius * terrain.Radius;
                    if (inside && !CheckOpenTexel(image, i, j, region == 0u ? "スキンメッシュ" : "地形"))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            std::cout << TestName << " スキップ: 環境変数による指定\n";
            return SkipReturnCode;
        }
        RHIDeviceDesc desc;
        desc.Api = GraphicsAPI::Vulkan;
        desc.bEnableValidation = true;
        DevicePtr device;
        try
        {
            device = CreateRHIDevice(desc);
        }
        catch (const std::exception&)
        {
            std::cout << TestName << " スキップ: Vulkan デバイスを利用できません\n";
            return SkipReturnCode;
        }
        if (!device || device->GetAPI() != API::Vulkan ||
            !device->IsFormatSupported(Format::D32_FLOAT, FormatUsage::RenderTarget))
        {
            std::cout << TestName << " スキップ: Vulkan の D32 深度画像を利用できません\n";
            return SkipReturnCode;
        }
        Fixture fixture;
        fixture.Device = device;
        Scene scene;
        OverheadHeightMapPass pass;
        Image image;
        const bool passed = fixture.Initialize() && scene.Initialize(fixture.Resources) &&
            fixture.Run(pass, scene, image) && Verify(scene, image);
        std::cout << TestName << (passed ? " 合格\n" : " 不合格\n");
        return passed ? 0 : 1;
    }
}

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << " 例外が発生しました: " << exception.what() << '\n';
        return 1;
    }
}
