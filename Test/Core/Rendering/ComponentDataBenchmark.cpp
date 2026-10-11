#include "Component/MeshComponent.h"
#include "Engine/NorvesEngine.h"
#include "Object/World.h"
#include "Rendering/SceneView.h"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;

namespace
{
    MeshDataHandle MakeMeshHandle(uint64_t id)
    {
        MeshDataHandle handle;
        handle.Id = id;
        return handle;
    }
}

int main()
{
    constexpr int EntityCount = 1000;
    constexpr int Iterations = 200;

    ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
    if (!registry.IsAvailable())
    {
        std::cout << "ComponentDataBenchmark skipped: registry unavailable\n";
        return 0;
    }

    registry.SetEnabled(true);

    World world;
    world.Initialize();
    SceneView view;
    SceneViewSettings settings;
    assert(view.Initialize(settings));
    world.SetSceneView(&view);

    for (int index = 0; index < EntityCount; ++index)
    {
        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity);
        entity->SetPosition(static_cast<float>(index), 0.0f, 0.0f);

        MeshComponent* mesh = world.CreateComponent<MeshComponent>(entity);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(static_cast<uint64_t>(index + 1)));
    }

    world.SyncToSceneView();

    const auto start = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < Iterations; ++iteration)
    {
        world.SyncToSceneView();
    }
    const auto end = std::chrono::steady_clock::now();
    const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "ComponentDataBenchmark entities=" << EntityCount
              << " iterations=" << Iterations
              << " elapsedMs=" << elapsedMs
              << " transformData=" << registry.GetTransformData().size()
              << " meshData=" << registry.GetMeshData().size()
              << "\n";

    world.Finalize();
    registry.SetEnabled(false);
    return 0;
}
