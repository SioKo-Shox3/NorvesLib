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

    void AssertFloatArrayNear(const float* actual, const float* expected, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            assert(NearlyEqual(actual[i], expected[i]));
        }
    }

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
            world.SetTranslationRow(NorvesLib::Math::Vector3(static_cast<float>(i), 0.0f, 0.0f));
            batcher.AddMeshProxy(MakeProxy(i + 1, 100, 200, world));
        }

        NorvesLib::Math::Matrix4x4 otherWorld;
        otherWorld.SetTranslationRow(NorvesLib::Math::Vector3(10.0f, 0.0f, 0.0f));
        batcher.AddMeshProxy(MakeProxy(6, 101, 200, otherWorld));
        batcher.EndBatching();
        batcher.GenerateDrawCommands(commands, instanceData, false, 2);

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
        batcher.GenerateDrawCommands(commands, instanceData, false, 2);

        assert(commands.size() == 1);
        assert(instanceData.size() == 1);

        const DrawCommand &cmd = commands[0];
        assert(cmd.Draw.InstanceCount == 1);
        assert(cmd.Draw.FirstInstance == 0);
        assert(cmd.Draw.InstanceDataOffset == 0);
        assert(!cmd.Draw.bInstanced);

        const NorvesLib::Math::Matrix4x4 expectedNormal(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.25f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
        assert(NorvesLib::Math::MatrixUtils::ApproxEqual(cmd.Draw.NormalMatrix, expectedNormal, 0.0001f));

        const GPUSceneInstanceData &gpuData = instanceData[0];
        const float expectedNormalData[12] =
        {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.25f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f
        };
        AssertFloatArrayNear(gpuData.NormalMatrix, expectedNormalData, 12);
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
