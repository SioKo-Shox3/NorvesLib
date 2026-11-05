#include "GameMode/GameModeScope.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "Rendering/RenderResources.h"
#include "RenderingValidation/GpuTestEnvironment.h"

#include <cstdint>
#include <exception>
#include <iostream>

using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Test::RenderingValidation;
namespace RHI = NorvesLib::RHI;

namespace
{
    constexpr const char* TestName = "GameModeScopeResourceLeakVulkanTest";

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << ": " << message << '\n';
        }
        return condition;
    }

#if NORVES_ENABLE_STATS
    bool HasLeak(const NorvesLib::Core::Container::VariableArray<GameModeLeakEntry>& leaks,
                 const char* name, uint64_t baseline, uint64_t current)
    {
        for (const GameModeLeakEntry& leak : leaks)
        {
            if (leak.Name == name && leak.Baseline == baseline && leak.Current == current)
            {
                return true;
            }
        }
        return false;
    }
#endif

    int RunTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPU テストのスキップが指定されました");
        }

        RHI::RHIDeviceDesc deviceDesc;
        deviceDesc.Api = RHI::GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = false;
        RHI::DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != RHI::API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkan デバイスを利用できません");
        }

        RenderResources resources;
        resources.SetGeometryPoolBlockBytes(1ull << 20);
        if (!Expect(resources.Initialize(device), "描画資源を初期化できませんでした"))
        {
            return 1;
        }

        GameModeScope scope(nullptr, &resources);
        const GameModeScopeCounts baseline = scope.CaptureCounts();

        TextureCreateInfo textureInfo;
        textureInfo.Width = 4;
        textureInfo.Height = 4;
        textureInfo.DebugName = "ScopeLeakTexture";
        const TextureHandle textureA = resources.Textures().CreateTexture(textureInfo);
        if (!Expect(textureA.IsValid() && resources.Textures().GetRHITexture(textureA) != nullptr,
                    "１枚目のテクスチャを作成できませんでした"))
        {
            return 1;
        }
        scope.TrackTexture(textureA);

        const TextureHandle textureB = resources.Textures().CreateTexture(textureInfo);
        if (!Expect(textureB.IsValid() && resources.Textures().GetRHITexture(textureB) != nullptr,
                    "２枚目のテクスチャを作成できませんでした"))
        {
            return 1;
        }
        scope.TrackTexture(textureB);

        const float vertices[12] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f};
        const uint32_t indices[3] = {0, 1, 2};
        const MeshDataHandle mesh{77};
        if (!Expect(resources.Meshes().Register(mesh, vertices, sizeof(vertices), indices, 3),
                    "手続きメッシュを登録できませんでした"))
        {
            return 1;
        }
        scope.TrackMesh(mesh);
        const auto* meshData = resources.Meshes().GetGPUData(mesh);
        if (!Expect(meshData != nullptr && meshData->VertexBuffer && meshData->IndexBuffer,
                    "手続きメッシュの GPU バッファがありません"))
        {
            return 1;
        }

        MegaGeometry::MeshCluster cluster;
        cluster.IndexCount = 3;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.Radius = 0.75f;

        MegaGeometry::MegaMeshCreateInfo megaInfo;
        megaInfo.VertexData = vertices;
        megaInfo.VertexDataSize = sizeof(vertices);
        megaInfo.VertexCount = 3;
        megaInfo.VertexStride = 4 * sizeof(float);
        megaInfo.IndexData = indices;
        megaInfo.IndexCount = 3;
        megaInfo.Clusters.push_back(cluster);
        megaInfo.TotalBounds = cluster.Bounds;
        megaInfo.bBuildLODHierarchy = false;
        megaInfo.DebugName = "ScopeLeakMegaMesh";
        const auto megaMesh = resources.MegaGeometry().CreateMegaMesh(megaInfo);
        const auto* megaData = resources.MegaGeometry().GetMegaMeshGPUData(megaMesh);
        if (!Expect(megaMesh.IsValid() && megaData != nullptr && megaData->VertexBuffer &&
                        megaData->IndexBuffer && megaData->ClusterBuffer,
                    "モデル用の MegaMesh と GPU バッファを作成できませんでした"))
        {
            return 1;
        }

        const ModelHandle model = resources.MegaGeometry().RegisterModel(megaMesh, "ScopeLeakModel");
        if (!Expect(model.IsValid() && resources.MegaGeometry().GetModelMegaMeshHandle(model) == megaMesh,
                    "モデルを登録できませんでした"))
        {
            return 1;
        }
        scope.TrackModel(model);

        const GameModeScopeCounts created = scope.CaptureCounts();
        bool bPassed = true;
        bPassed &= Expect(created.Textures == baseline.Textures + 2, "テクスチャ数が２増えていません");
        bPassed &= Expect(created.Meshes == baseline.Meshes + 1, "メッシュ数が１増えていません");
        bPassed &= Expect(created.Models == baseline.Models + 1, "モデル数が１増えていません");
#if NORVES_ENABLE_STATS
        const auto leaks = scope.CollectLeaks();
        bPassed &= Expect(HasLeak(leaks, "Textures", baseline.Textures, created.Textures),
                          "テクスチャ数の増加を漏れとして検出できませんでした");
        bPassed &= Expect(HasLeak(leaks, "Meshes", baseline.Meshes, created.Meshes),
                          "メッシュ数の増加を漏れとして検出できませんでした");
        bPassed &= Expect(HasLeak(leaks, "Models", baseline.Models, created.Models),
                          "モデル数の増加を漏れとして検出できませんでした");
#endif

        // フレームを提出しないため、GPU の使用が完了した状態で登録と資源を解放する。
        device->WaitIdle();
        scope.Cleanup();
        const GameModeScopeCounts cleaned = scope.CaptureCounts();
        bPassed &= Expect(cleaned.Textures == baseline.Textures, "テクスチャ数が基準へ戻りませんでした");
        bPassed &= Expect(cleaned.Meshes == baseline.Meshes, "メッシュ数が基準へ戻りませんでした");
        bPassed &= Expect(cleaned.Models == baseline.Models, "モデル数が基準へ戻りませんでした");
        bPassed &= Expect(scope.CollectLeaks().empty(), "後始末後も資源数の差が残っています");
        bPassed &= Expect(scope.IsEmpty(), "後始末後も追跡が残っています");
        bPassed &= Expect(resources.Textures().GetRHITexture(textureA) == nullptr &&
                              resources.Textures().GetRHITexture(textureB) == nullptr,
                          "後始末後もテクスチャが登録されています");
        bPassed &= Expect(resources.Meshes().GetGPUData(mesh) == nullptr,
                          "後始末後も手続きメッシュが登録されています");
        bPassed &= Expect(!resources.MegaGeometry().GetModelMegaMeshHandle(model).IsValid() &&
                              resources.MegaGeometry().GetMegaMeshGPUData(megaMesh) == nullptr,
                          "後始末後もモデルまたは MegaMesh が登録されています");
        bPassed &= Expect(resources.GetPendingRetireCount() == 0,
                          "提出がないのに GPU 資源の破棄待ちが残っています");

        if (bPassed)
        {
            std::cout << TestName << ": 資源数が基準へ戻りました\n";
        }
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
        std::cerr << TestName << ": 例外が発生しました: " << exception.what() << '\n';
        return 1;
    }
}
