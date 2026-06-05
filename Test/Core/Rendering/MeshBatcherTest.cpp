#include "Rendering/DrawCommand.h"
#include "Rendering/SceneProxy.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    MeshProxy MakeProxy(uint64_t objectId,
                        uint64_t meshId,
                        uint64_t materialId,
                        const NorvesLib::Math::Matrix4x4 &world)
    {
        MeshProxy proxy;
        proxy.ObjectId = objectId;
        proxy.MeshHandle.Id = meshId;
        proxy.MaterialCount = 1;
        proxy.Materials[0].Id = materialId;
        proxy.WorldTransform = world;
        proxy.WorldBounds.Radius = 1.0f;
        proxy.CustomData[0] = static_cast<float>(objectId);
        return proxy;
    }
}

int main()
{
    std::cout << "MeshBatcherTest start\n";

    MeshBatcher batcher;
    NorvesLib::Core::Container::VariableArray<DrawCommand> commands;

    {
        NorvesLib::Math::Matrix4x4 worldA;
        NorvesLib::Math::Matrix4x4 worldB;
        worldB.m30 = 3.0f;

        batcher.BeginBatching();
        batcher.AddMeshProxy(MakeProxy(1, 100, 200, worldA));
        batcher.AddMeshProxy(MakeProxy(2, 100, 200, worldB));
        batcher.EndBatching();
        batcher.GenerateDrawCommands(commands);

        assert(commands.size() == 1);
        assert(commands[0].Type == DrawCommandType::DrawIndexedInstanced);
        assert(commands[0].Draw.bInstanced);
        assert(commands[0].Draw.InstanceCount == 2);
        assert(commands[0].Draw.MeshHandle.Id == 100);
        assert(commands[0].Draw.MaterialHandle.Id == 200);
        assert(batcher.GetStats().TotalProxies == 2);
        assert(batcher.GetStats().TotalBatches == 1);
        assert(batcher.GetStats().TotalDrawCommands == 1);
        assert(batcher.GetStats().InstancedDrawCalls == 1);
        assert(batcher.GetStats().SavedDrawCalls == 1);
    }

    {
        NorvesLib::Math::Matrix4x4 worldA;
        NorvesLib::Math::Matrix4x4 worldB;
        worldB.m30 = 7.0f;

        batcher.BeginBatching();
        batcher.AddMeshProxy(MakeProxy(3, 100, 200, worldA));
        batcher.AddMeshProxy(MakeProxy(4, 100, 201, worldB));
        batcher.EndBatching();
        batcher.GenerateDrawCommands(commands);

        assert(commands.size() == 2);
        assert(!commands[0].Draw.bInstanced);
        assert(!commands[1].Draw.bInstanced);
        assert(commands[0].Draw.InstanceCount == 1);
        assert(commands[1].Draw.InstanceCount == 1);
        assert(batcher.GetStats().TotalProxies == 2);
        assert(batcher.GetStats().TotalBatches == 2);
        assert(batcher.GetStats().TotalDrawCommands == 2);
        assert(batcher.GetStats().InstancedDrawCalls == 0);
        assert(batcher.GetStats().SavedDrawCalls == 0);
    }

    std::cout << "MeshBatcherTest passed\n";
    return 0;
}
