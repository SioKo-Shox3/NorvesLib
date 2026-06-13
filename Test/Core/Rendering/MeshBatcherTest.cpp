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
        MeshProxy proxy{};
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
    NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

    {
        NorvesLib::Math::Matrix4x4 worldA;
        NorvesLib::Math::Matrix4x4 worldB;
        worldB.m30 = 3.0f;

        batcher.BeginBatching();
        batcher.AddMeshProxy(MakeProxy(1, 100, 200, worldA));
        batcher.AddMeshProxy(MakeProxy(2, 100, 200, worldB));
        batcher.EndBatching();
        batcher.GenerateDrawCommands(commands, instanceData, false, 2);

        assert(commands.size() == 2);
        assert(instanceData.size() == 2);
        assert(commands[0].Type == DrawCommandType::DrawIndexed);
        assert(commands[1].Type == DrawCommandType::DrawIndexed);
        assert(!commands[0].Draw.bInstanced);
        assert(!commands[1].Draw.bInstanced);
        assert(commands[0].Draw.InstanceCount == 1);
        assert(commands[1].Draw.InstanceCount == 1);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[1].Draw.FirstInstance == 1);
        assert(commands[0].Draw.InstanceDataOffset == 0);
        assert(commands[1].Draw.InstanceDataOffset == 1);
        assert(commands[0].Draw.MeshHandle.Id == 100);
        assert(commands[1].Draw.MeshHandle.Id == 100);
        assert(commands[0].Draw.MaterialHandle.Id == 200);
        assert(commands[1].Draw.MaterialHandle.Id == 200);
        assert(commands[0].Draw.WorldMatrix.m30 == 0.0f);
        assert(commands[1].Draw.WorldMatrix.m30 == 3.0f);
        assert(instanceData[0].World[12] == 0.0f);
        assert(instanceData[1].World[12] == 3.0f);
        assert(commands[0].Draw.CustomData[0] == 1.0f);
        assert(commands[1].Draw.CustomData[0] == 2.0f);
        assert(instanceData[0].CustomData[0] == 1.0f);
        assert(instanceData[1].CustomData[0] == 2.0f);
        assert(batcher.GetStats().TotalProxies == 2);
        assert(batcher.GetStats().TotalBatches == 1);
        assert(batcher.GetStats().TotalDrawCommands == 2);
        assert(batcher.GetStats().InstancedDrawCalls == 0);
        assert(batcher.GetStats().SavedDrawCalls == 0);
    }

    {
        NorvesLib::Math::Matrix4x4 worldA;
        NorvesLib::Math::Matrix4x4 worldB;
        worldB.m30 = 7.0f;

        batcher.BeginBatching();
        batcher.AddMeshProxy(MakeProxy(3, 100, 200, worldA));
        batcher.AddMeshProxy(MakeProxy(4, 100, 201, worldB));
        batcher.EndBatching();
        batcher.GenerateDrawCommands(commands, instanceData, false, 2);

        assert(commands.size() == 2);
        assert(instanceData.size() == 2);
        assert(!commands[0].Draw.bInstanced);
        assert(!commands[1].Draw.bInstanced);
        assert(commands[0].Draw.InstanceCount == 1);
        assert(commands[1].Draw.InstanceCount == 1);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[1].Draw.FirstInstance == 1);
        assert(commands[0].Draw.InstanceDataOffset == 0);
        assert(commands[1].Draw.InstanceDataOffset == 1);
        assert(batcher.GetStats().TotalProxies == 2);
        assert(batcher.GetStats().TotalBatches == 2);
        assert(batcher.GetStats().TotalDrawCommands == 2);
        assert(batcher.GetStats().InstancedDrawCalls == 0);
        assert(batcher.GetStats().SavedDrawCalls == 0);
    }

    std::cout << "MeshBatcherTest passed\n";
    return 0;
}
