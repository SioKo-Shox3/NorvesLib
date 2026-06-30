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
        return proxy;
    }

    MeshProxy MakeProxy(uint64_t objectId,
                        uint64_t meshId,
                        uint64_t materialId,
                        BlendMode blendMode = BlendMode::Opaque)
    {
        NorvesLib::Math::Matrix4x4 world;
        world.SetTranslationRow(NorvesLib::Math::Vector3(static_cast<float>(objectId), 0.0f, 0.0f));
        MeshProxy proxy = MakeProxy(objectId, meshId, materialId, world);
        proxy.MaterialBlendModes[0] = blendMode;
        return proxy;
    }

    void Generate(MeshBatcher &batcher,
                  NorvesLib::Core::Container::VariableArray<DrawCommand> &commands,
                  NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> &instanceData,
                  bool bAllowInstancing = true,
                  uint32_t minInstanceCount = 2)
    {
        batcher.EndBatching();
        batcher.GenerateDrawCommands(commands, instanceData, bAllowInstancing, minInstanceCount);
    }

    void TestThresholdCreatesSingleInstancedCommand()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        batcher.BeginBatching();
        for (uint64_t i = 0; i < 3; ++i)
        {
            batcher.AddMeshProxy(MakeProxy(i + 1, 100, 200));
        }
        Generate(batcher, commands, instanceData, true, 3);

        assert(commands.size() == 1);
        assert(instanceData.size() == 3);
        assert(commands[0].Type == DrawCommandType::DrawIndexedInstanced);
        assert(commands[0].Draw.bInstanced);
        assert(commands[0].Draw.InstanceCount == 3);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[0].Draw.InstanceDataOffset == 0);
        assert(batcher.GetStats().TotalDrawCommands == 1);
        assert(batcher.GetStats().InstancedDrawCalls == 1);
        assert(batcher.GetStats().SavedDrawCalls == 2);
    }

    void TestThresholdBelowExpandsCommands()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        batcher.BeginBatching();
        batcher.AddMeshProxy(MakeProxy(1, 100, 200));
        batcher.AddMeshProxy(MakeProxy(2, 100, 200));
        Generate(batcher, commands, instanceData, true, 3);

        assert(commands.size() == 2);
        assert(instanceData.size() == 2);
        assert(!commands[0].Draw.bInstanced);
        assert(!commands[1].Draw.bInstanced);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[1].Draw.FirstInstance == 1);
        assert(commands[0].Draw.InstanceDataOffset == 0);
        assert(commands[1].Draw.InstanceDataOffset == 1);
        assert(batcher.GetStats().InstancedDrawCalls == 0);
        assert(batcher.GetStats().SavedDrawCalls == 0);
    }

    void TestDisabledInstancingExpandsCommands()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        batcher.BeginBatching();
        for (uint64_t i = 0; i < 4; ++i)
        {
            batcher.AddMeshProxy(MakeProxy(i + 1, 100, 200));
        }
        Generate(batcher, commands, instanceData, false, 2);

        assert(commands.size() == 4);
        assert(instanceData.size() == 4);
        for (uint32_t i = 0; i < 4; ++i)
        {
            assert(!commands[i].Draw.bInstanced);
            assert(commands[i].Draw.InstanceCount == 1);
            assert(commands[i].Draw.FirstInstance == i);
            assert(commands[i].Draw.InstanceDataOffset == i);
        }
        assert(batcher.GetStats().InstancedDrawCalls == 0);
        assert(batcher.GetStats().SavedDrawCalls == 0);
    }

    void TestMaterialSeparation()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        batcher.BeginBatching();
        batcher.AddMeshProxy(MakeProxy(1, 100, 200));
        batcher.AddMeshProxy(MakeProxy(2, 100, 201));
        batcher.AddMeshProxy(MakeProxy(3, 100, 200));
        batcher.AddMeshProxy(MakeProxy(4, 100, 201));
        Generate(batcher, commands, instanceData, true, 2);

        assert(commands.size() == 2);
        assert(instanceData.size() == 4);
        assert(commands[0].Draw.bInstanced);
        assert(commands[1].Draw.bInstanced);
        assert(commands[0].Draw.InstanceCount == 2);
        assert(commands[1].Draw.InstanceCount == 2);
        assert(commands[0].Draw.MaterialHandle.Id != commands[1].Draw.MaterialHandle.Id);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[0].Draw.InstanceDataOffset == 0);
        assert(commands[1].Draw.FirstInstance == 2);
        assert(commands[1].Draw.InstanceDataOffset == 2);
    }

    void TestSubMeshSeparation()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        MeshProxy first = MakeProxy(1, 100, 200);
        first.MaterialCount = 2;
        first.Materials[1].Id = 201;
        MeshProxy second = MakeProxy(2, 100, 200);
        second.MaterialCount = 2;
        second.Materials[1].Id = 201;

        batcher.BeginBatching();
        batcher.AddMeshProxy(first);
        batcher.AddMeshProxy(second);
        Generate(batcher, commands, instanceData, true, 2);

        assert(commands.size() == 2);
        assert(instanceData.size() == 4);
        assert(commands[0].Draw.bInstanced);
        assert(commands[1].Draw.bInstanced);
        assert(commands[0].Draw.SubMeshIndex != commands[1].Draw.SubMeshIndex);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[1].Draw.FirstInstance == 2);
    }

    void TestCastShadowSeparation()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        MeshProxy shadowA = MakeProxy(1, 100, 200);
        MeshProxy shadowB = MakeProxy(2, 100, 200);
        MeshProxy noShadowA = MakeProxy(3, 100, 200);
        MeshProxy noShadowB = MakeProxy(4, 100, 200);
        noShadowA.bCastShadow = false;
        noShadowB.bCastShadow = false;

        batcher.BeginBatching();
        batcher.AddMeshProxy(shadowA);
        batcher.AddMeshProxy(noShadowA);
        batcher.AddMeshProxy(shadowB);
        batcher.AddMeshProxy(noShadowB);
        Generate(batcher, commands, instanceData, true, 2);

        assert(commands.size() == 2);
        assert(instanceData.size() == 4);
        assert(commands[0].Draw.bInstanced);
        assert(commands[1].Draw.bInstanced);
        assert(commands[0].Draw.bCastShadow != commands[1].Draw.bCastShadow);
        assert(commands[0].Draw.InstanceCount == 2);
        assert(commands[1].Draw.InstanceCount == 2);
    }

    void TestObjectColorFlatten()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        MeshProxy proxy = MakeProxy(1, 100, 200);
        proxy.CustomData[0] = 0.0f;
        proxy.CustomData[1] = 0.25f;
        proxy.CustomData[2] = 0.0f;
        proxy.CustomData[3] = 0.5f;

        batcher.BeginBatching();
        batcher.AddMeshProxy(proxy);
        Generate(batcher, commands, instanceData, true, 2);

        assert(commands.size() == 1);
        assert(instanceData.size() == 1);
        assert(instanceData[0].ObjectColor[0] == 1.0f);
        assert(instanceData[0].ObjectColor[1] == 0.25f);
        assert(instanceData[0].ObjectColor[2] == 1.0f);
        assert(instanceData[0].ObjectColor[3] == 0.5f);
        assert(instanceData[0].CustomData[0] == 0.0f);
        assert(instanceData[0].CustomData[1] == 0.25f);
        assert(instanceData[0].CustomData[2] == 0.0f);
        assert(instanceData[0].CustomData[3] == 0.5f);
    }

    void TestTransparentBatchNeverInstances()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        batcher.BeginBatching();
        batcher.AddMeshProxy(MakeProxy(1, 100, 200, BlendMode::Translucent));
        batcher.AddMeshProxy(MakeProxy(2, 100, 200, BlendMode::Translucent));
        batcher.AddMeshProxy(MakeProxy(3, 100, 200, BlendMode::Translucent));
        Generate(batcher, commands, instanceData, true, 2);

        assert(commands.size() == 3);
        assert(instanceData.size() == 3);
        for (const DrawCommand &command : commands)
        {
            assert(!command.Draw.bInstanced);
            assert(command.Draw.InstanceCount == 1);
            assert(command.Draw.MaterialBlendMode == BlendMode::Translucent);
        }
        assert(batcher.GetStats().InstancedDrawCalls == 0);
        assert(batcher.GetStats().SavedDrawCalls == 0);
    }
}

int main()
{
    std::cout << "MeshBatcherInstancingTest start\n";

    TestThresholdCreatesSingleInstancedCommand();
    TestThresholdBelowExpandsCommands();
    TestDisabledInstancingExpandsCommands();
    TestMaterialSeparation();
    TestSubMeshSeparation();
    TestCastShadowSeparation();
    TestObjectColorFlatten();
    TestTransparentBatchNeverInstances();

    std::cout << "MeshBatcherInstancingTest passed\n";
    return 0;
}
