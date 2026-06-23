#include "Component/MeshComponent.h"
#include "Object/World.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneProxy.h"
#include <cassert>
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

    MaterialHandle MakeMaterialHandle(RenderResources& renderResources, BlendMode blendMode)
    {
        MaterialCreateData createInfo;
        createInfo.Blend = blendMode;
        return renderResources.Materials().Create(createInfo);
    }

    MeshProxy MakeProxy(uint64_t objectId,
                        uint64_t meshId,
                        uint64_t materialId,
                        BlendMode blendMode,
                        float sortDepth)
    {
        MeshProxy proxy{};
        proxy.ObjectId = objectId;
        proxy.MeshHandle.Id = meshId;
        proxy.MaterialCount = 1;
        proxy.Materials[0].Id = materialId;
        proxy.MaterialBlendModes[0] = blendMode;
        proxy.SortDepth = sortDepth;
        proxy.WorldTransform.m30 = sortDepth;
        proxy.WorldBounds.CenterX = sortDepth;
        proxy.WorldBounds.Radius = 1.0f;
        return proxy;
    }

    void GenerateSortedCommands(const Container::VariableArray<MeshProxy> &proxies,
                                bool bAllowInstancing,
                                uint32_t minInstanceCount,
                                Container::VariableArray<DrawCommand> &outCommands,
                                Container::VariableArray<DrawCommand> &outOpaqueCommands,
                                Container::VariableArray<DrawCommand> &outTransparentCommands)
    {
        MeshBatcher batcher;
        Container::VariableArray<GPUSceneInstanceData> instanceData;

        batcher.BeginBatching();
        for (const MeshProxy &proxy : proxies)
        {
            batcher.AddMeshProxy(proxy);
        }

        batcher.EndBatching();
        batcher.GenerateDrawCommands(outCommands, instanceData, bAllowInstancing, minInstanceCount);

        for (DrawCommand &command : outCommands)
        {
            command.CalculateSortKey(command.Draw.SortDepth, command.Draw.MaterialBlendMode);
        }

        DrawCommandSorter::SortAndSeparate(outCommands, outOpaqueCommands, outTransparentCommands);
        DrawCommandSorter::Sort(outOpaqueCommands, DrawCommandSorter::SortMode::FrontToBack);
        DrawCommandSorter::Sort(outTransparentCommands, DrawCommandSorter::SortMode::BackToFront);
    }

    void TestBlendModeClassification()
    {
        Container::VariableArray<MeshProxy> proxies;
        proxies.push_back(MakeProxy(1, 100, 200, BlendMode::Opaque, 4.0f));
        proxies.push_back(MakeProxy(2, 100, 201, BlendMode::Masked, 5.0f));
        proxies.push_back(MakeProxy(3, 100, 202, BlendMode::Translucent, 6.0f));
        proxies.push_back(MakeProxy(4, 100, 203, BlendMode::Additive, 7.0f));
        proxies.push_back(MakeProxy(5, 100, 204, BlendMode::Modulate, 8.0f));

        Container::VariableArray<DrawCommand> commands;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        GenerateSortedCommands(proxies, false, 2, commands, opaqueCommands, transparentCommands);

        assert(commands.size() == 5);
        assert(opaqueCommands.size() == 2);
        assert(transparentCommands.size() == 3);
        assert(opaqueCommands[0].Draw.MaterialBlendMode == BlendMode::Opaque);
        assert(opaqueCommands[1].Draw.MaterialBlendMode == BlendMode::Masked);
        assert(transparentCommands[0].Draw.MaterialBlendMode == BlendMode::Modulate);
        assert(transparentCommands[1].Draw.MaterialBlendMode == BlendMode::Additive);
        assert(transparentCommands[2].Draw.MaterialBlendMode == BlendMode::Translucent);
    }

    void TestOpaqueCommandsSortFrontToBack()
    {
        Container::VariableArray<MeshProxy> proxies;
        proxies.push_back(MakeProxy(10, 100, 220, BlendMode::Opaque, 9.0f));
        proxies.push_back(MakeProxy(11, 100, 220, BlendMode::Opaque, 2.0f));
        proxies.push_back(MakeProxy(12, 100, 210, BlendMode::Masked, 5.0f));
        proxies.push_back(MakeProxy(13, 100, 230, BlendMode::Opaque, 5.0f));

        Container::VariableArray<DrawCommand> commands;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        GenerateSortedCommands(proxies, false, 2, commands, opaqueCommands, transparentCommands);

        assert(transparentCommands.empty());
        assert(opaqueCommands.size() == 4);
        assert(opaqueCommands[0].Draw.ObjectId == 11);
        assert(opaqueCommands[1].Draw.ObjectId == 12);
        assert(opaqueCommands[2].Draw.ObjectId == 13);
        assert(opaqueCommands[3].Draw.ObjectId == 10);
    }

    void TestTransparentCommandsSortBackToFront()
    {
        Container::VariableArray<MeshProxy> proxies;
        proxies.push_back(MakeProxy(20, 100, 320, BlendMode::Translucent, 3.0f));
        proxies.push_back(MakeProxy(21, 100, 330, BlendMode::Additive, 9.0f));
        proxies.push_back(MakeProxy(22, 100, 310, BlendMode::Modulate, 6.0f));
        proxies.push_back(MakeProxy(23, 100, 340, BlendMode::Translucent, 6.0f));

        Container::VariableArray<DrawCommand> commands;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        GenerateSortedCommands(proxies, false, 2, commands, opaqueCommands, transparentCommands);

        assert(opaqueCommands.empty());
        assert(transparentCommands.size() == 4);
        assert(transparentCommands[0].Draw.ObjectId == 21);
        assert(transparentCommands[1].Draw.ObjectId == 22);
        assert(transparentCommands[2].Draw.ObjectId == 23);
        assert(transparentCommands[3].Draw.ObjectId == 20);
    }

    void TestBoardPayloadAlwaysClassifiesTransparent()
    {
        Container::VariableArray<DrawCommand> commands;

        DrawCommand opaqueBoard = DrawCommand::CreateDraw();
        opaqueBoard.Draw.PayloadKind = DrawPayloadKind::Board;
        opaqueBoard.Draw.MaterialBlendMode = BlendMode::Opaque;
        opaqueBoard.Draw.ObjectId = 1000u;
        opaqueBoard.Draw.SortDepth = 1.0f;
        commands.push_back(opaqueBoard);

        DrawCommand maskedBoard = DrawCommand::CreateDraw();
        maskedBoard.Draw.PayloadKind = DrawPayloadKind::Board;
        maskedBoard.Draw.MaterialBlendMode = BlendMode::Masked;
        maskedBoard.Draw.ObjectId = 1001u;
        maskedBoard.Draw.SortDepth = 2.0f;
        commands.push_back(maskedBoard);

        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        DrawCommandSorter::SortAndSeparate(commands, opaqueCommands, transparentCommands);
        DrawCommandSorter::Sort(transparentCommands, DrawCommandSorter::SortMode::BackToFront);

        assert(opaqueCommands.empty());
        assert(transparentCommands.size() == 2);
        assert(transparentCommands[0].Draw.ObjectId == 1001u);
        assert(transparentCommands[1].Draw.ObjectId == 1000u);
    }

    void TestTransparentBatchesStayNonInstanced()
    {
        Container::VariableArray<MeshProxy> proxies;
        proxies.push_back(MakeProxy(30, 500, 600, BlendMode::Translucent, 1.0f));
        proxies.push_back(MakeProxy(31, 500, 600, BlendMode::Translucent, 2.0f));
        proxies.push_back(MakeProxy(32, 500, 600, BlendMode::Translucent, 3.0f));

        Container::VariableArray<DrawCommand> commands;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        GenerateSortedCommands(proxies, true, 2, commands, opaqueCommands, transparentCommands);

        assert(commands.size() == 3);
        assert(transparentCommands.size() == 3);
        for (const DrawCommand &command : commands)
        {
            assert(!command.Draw.bInstanced);
            assert(command.Draw.InstanceCount == 1);
            assert(command.Draw.MaterialBlendMode == BlendMode::Translucent);
        }
    }

    void TestBuildMeshProxyFallsBackToOpaque()
    {
        RenderResources renderResources;

        World world;
        world.Initialize();

        Entity *object = world.SpawnObject<Entity>();
        assert(object != nullptr);

        MeshComponent *mesh = world.CreateComponent<MeshComponent>(object);
        assert(mesh != nullptr);
        mesh->SetMeshHandle(MakeMeshHandle(700));
        mesh->RefreshRenderTransformCache();

        MeshProxy proxy{};
        MaterialHandle translucent = MakeMaterialHandle(renderResources, BlendMode::Translucent);
        mesh->SetMaterial(0, translucent);
        assert(mesh->BuildMeshProxy(proxy, nullptr));
        assert(proxy.MaterialBlendModes[0] == BlendMode::Opaque);

        MaterialHandle missingHandle;
        missingHandle.Id = 9999;
        mesh->SetMaterial(0, missingHandle);
        assert(mesh->BuildMeshProxy(proxy, &renderResources.Materials()));
        assert(proxy.MaterialBlendModes[0] == BlendMode::Opaque);

        mesh->SetMaterial(0, translucent);
        assert(mesh->BuildMeshProxy(proxy, &renderResources.Materials()));
        assert(proxy.MaterialBlendModes[0] == BlendMode::Translucent);

        mesh->ClearMaterials();
        assert(mesh->BuildMeshProxy(proxy, &renderResources.Materials()));
        assert(proxy.MaterialCount == 1);
        assert(proxy.MaterialBlendModes[0] == BlendMode::Opaque);

        world.Finalize();
    }

    void TestDirectProxyMaterialCountClampsToMaxSlots()
    {
        constexpr uint32_t overflowMaterialCount = MAX_MATERIAL_SLOTS + 3u;

        Container::VariableArray<MeshProxy> proxies;
        MeshProxy proxy{};
        proxy.ObjectId = 9000;
        proxy.MeshHandle = MakeMeshHandle(800);
        proxy.MaterialCount = overflowMaterialCount;
        proxy.SortDepth = 4.0f;
        proxy.WorldTransform.m30 = 4.0f;
        proxy.WorldBounds.CenterX = 4.0f;
        proxy.WorldBounds.Radius = 1.0f;

        for (uint32_t i = 0; i < MAX_MATERIAL_SLOTS; ++i)
        {
            proxy.Materials[i].Id = 900 + i;
            proxy.MaterialBlendModes[i] = BlendMode::Opaque;
        }

        proxies.push_back(proxy);

        Container::VariableArray<DrawCommand> commands;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        GenerateSortedCommands(proxies, false, 2, commands, opaqueCommands, transparentCommands);

        assert(commands.size() == MAX_MATERIAL_SLOTS);
        assert(opaqueCommands.size() == MAX_MATERIAL_SLOTS);
        assert(transparentCommands.empty());
    }
} // namespace

int main()
{
    std::cout << "DrawCommandSorterTest start\n";

    TestBlendModeClassification();
    TestOpaqueCommandsSortFrontToBack();
    TestTransparentCommandsSortBackToFront();
    TestBoardPayloadAlwaysClassifiesTransparent();
    TestTransparentBatchesStayNonInstanced();
    TestBuildMeshProxyFallsBackToOpaque();
    TestDirectProxyMaterialCountClampsToMaxSlots();

    std::cout << "DrawCommandSorterTest passed\n";
    return 0;
}
