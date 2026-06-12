#include "Rendering/DrawCommand.h"
#include "Rendering/SceneProxy.h"
#include "Math/MatrixUtils.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    bool NearlyEqual(float a, float b, float epsilon = 0.0001f)
    {
        return std::fabs(a - b) <= epsilon;
    }

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
        proxy.CustomData[1] = static_cast<float>(meshId);
        proxy.CustomData[2] = static_cast<float>(materialId);
        proxy.CustomData[3] = 1.0f;
        return proxy;
    }

    void TestBatcherFlattensInstanceData()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        batcher.BeginBatching();
        for (uint64_t i = 0; i < 5; ++i)
        {
            NorvesLib::Math::Matrix4x4 world;
            world.m30 = static_cast<float>(i);
            batcher.AddMeshProxy(MakeProxy(i + 1, 100, 200, world));
        }

        NorvesLib::Math::Matrix4x4 otherWorld;
        otherWorld.m30 = 10.0f;
        batcher.AddMeshProxy(MakeProxy(6, 101, 200, otherWorld));
        batcher.EndBatching();
        batcher.GenerateDrawCommands(commands, instanceData);

        assert(instanceData.size() == 6);
        assert(commands.size() == 6);

        for (uint32_t i = 0; i < 6; ++i)
        {
            assert(commands[i].Draw.InstanceCount == 1);
            assert(commands[i].Draw.FirstInstance == i);
            assert(commands[i].Draw.InstanceDataOffset == i);
            assert(!commands[i].Draw.bInstanced);
            assert(instanceData[i].CustomData[0] == static_cast<float>(i + 1));
        }
    }

    void TestNormalMatrixForNonUniformScale()
    {
        MeshBatcher batcher;
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;
        NorvesLib::Core::Container::VariableArray<GPUSceneInstanceData> instanceData;

        const NorvesLib::Math::Matrix4x4 world =
            NorvesLib::Math::MatrixUtils::CreateScale(2.0f, 4.0f, 0.5f);

        batcher.BeginBatching();
        batcher.AddMeshProxy(MakeProxy(1, 100, 200, world));
        batcher.EndBatching();
        batcher.GenerateDrawCommands(commands, instanceData);

        assert(commands.size() == 1);
        assert(instanceData.size() == 1);

        const DrawCommand &cmd = commands[0];
        assert(cmd.Draw.InstanceCount == 1);
        assert(cmd.Draw.FirstInstance == 0);
        assert(cmd.Draw.InstanceDataOffset == 0);
        assert(!cmd.Draw.bInstanced);

        assert(NearlyEqual(cmd.Draw.NormalMatrix.m00, 0.5f));
        assert(NearlyEqual(cmd.Draw.NormalMatrix.m11, 0.25f));
        assert(NearlyEqual(cmd.Draw.NormalMatrix.m22, 2.0f));
        assert(NearlyEqual(cmd.Draw.NormalMatrix.m33, 1.0f));

        const GPUSceneInstanceData &gpuData = instanceData[0];
        assert(NearlyEqual(gpuData.NormalMatrix[0], 0.5f));
        assert(NearlyEqual(gpuData.NormalMatrix[1], 0.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[2], 0.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[3], 0.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[4], 0.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[5], 0.25f));
        assert(NearlyEqual(gpuData.NormalMatrix[6], 0.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[7], 0.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[8], 0.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[9], 0.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[10], 2.0f));
        assert(NearlyEqual(gpuData.NormalMatrix[11], 0.0f));
    }

    void TestRebaseDrawCommandInstanceRange()
    {
        NorvesLib::Core::Container::VariableArray<DrawCommand> commands;

        DrawCommand first = DrawCommand::CreateDrawIndexed();
        first.Draw.FirstInstance = 0;
        first.Draw.InstanceDataOffset = 0;
        commands.push_back(first);

        DrawCommand second = DrawCommand::CreateDraw();
        second.Draw.FirstInstance = 3;
        second.Draw.InstanceDataOffset = 3;
        commands.push_back(second);

        DrawCommand compute = DrawCommand::CreateDispatch(1, 2, 3);
        compute.Draw.FirstInstance = 11;
        compute.Draw.InstanceDataOffset = 12;
        commands.push_back(compute);

        RebaseDrawCommandInstanceRange(commands, 0);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[0].Draw.InstanceDataOffset == 0);
        assert(commands[1].Draw.FirstInstance == 3);
        assert(commands[1].Draw.InstanceDataOffset == 3);
        assert(commands[2].Draw.FirstInstance == 11);
        assert(commands[2].Draw.InstanceDataOffset == 12);

        RebaseDrawCommandInstanceRange(commands, 7);
        assert(commands[0].Draw.FirstInstance == 7);
        assert(commands[0].Draw.InstanceDataOffset == 7);
        assert(commands[1].Draw.FirstInstance == 10);
        assert(commands[1].Draw.InstanceDataOffset == 10);
        assert(commands[2].Draw.FirstInstance == 11);
        assert(commands[2].Draw.InstanceDataOffset == 12);
    }
}

int main()
{
    std::cout << "InstanceDataFlattenTest start\n";

    TestBatcherFlattensInstanceData();
    TestNormalMatrixForNonUniformScale();
    TestRebaseDrawCommandInstanceRange();

    std::cout << "InstanceDataFlattenTest passed\n";
    return 0;
}
