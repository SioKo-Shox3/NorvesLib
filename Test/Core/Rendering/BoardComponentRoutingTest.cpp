#include "Component/BoardComponent.h"
#include "Object/World.h"
#include "Rendering/CanvasView.h"
#include "Rendering/IBoardProxySink.h"
#define private public
#include "Rendering/RenderingCoordinator.h"
#undef private
#include "Engine/Engine.h"
#include "Rendering/Viewport.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    class TrackingBoardSink final : public IBoardProxySink
    {
    public:
        void UpdateBoardProxy(uint64_t componentId, const BoardProxy &proxy) override
        {
            ++UpdateCount;
            RemoveId = 0;
            LastUpdateId = componentId;
            auto it = IndexByComponentId.find(componentId);
            if (it != IndexByComponentId.end())
            {
                Proxies[it->second] = proxy;
                return;
            }

            const uint32_t index = static_cast<uint32_t>(Proxies.size());
            Proxies.push_back(proxy);
            IndexByComponentId[componentId] = index;
        }

        void RemoveBoardProxy(uint64_t componentId) override
        {
            ++RemoveCount;
            RemoveId = componentId;
            auto it = IndexByComponentId.find(componentId);
            if (it == IndexByComponentId.end())
            {
                return;
            }

            const uint32_t removeIndex = it->second;
            const uint32_t lastIndex = static_cast<uint32_t>(Proxies.size() - 1);
            IndexByComponentId.erase(it);
            if (removeIndex != lastIndex)
            {
                Proxies[removeIndex] = Proxies[lastIndex];
                IndexByComponentId[Proxies[removeIndex].ComponentId] = removeIndex;
            }
            Proxies.pop_back();
        }

        void RemoveStaleBoardProxies(const Container::UnorderedSet<uint64_t> &liveComponentIds) override
        {
            ++StaleCount;
            uint32_t index = 0;
            while (index < Proxies.size())
            {
                const uint64_t componentId = Proxies[index].ComponentId;
                if (liveComponentIds.find(componentId) != liveComponentIds.end())
                {
                    ++index;
                    continue;
                }

                const uint32_t lastIndex = static_cast<uint32_t>(Proxies.size() - 1);
                IndexByComponentId.erase(componentId);
                if (index != lastIndex)
                {
                    Proxies[index] = Proxies[lastIndex];
                    IndexByComponentId[Proxies[index].ComponentId] = index;
                }
                Proxies.pop_back();
            }
        }

        const BoardProxy *Find(uint64_t componentId) const
        {
            auto it = IndexByComponentId.find(componentId);
            if (it == IndexByComponentId.end())
            {
                return nullptr;
            }
            return &Proxies[it->second];
        }

        Container::VariableArray<BoardProxy> Proxies;
        Container::UnorderedMap<uint64_t, uint32_t> IndexByComponentId;
        uint32_t UpdateCount = 0;
        uint32_t RemoveCount = 0;
        uint32_t StaleCount = 0;
        uint64_t LastUpdateId = 0;
        uint64_t RemoveId = 0;
    };

    ViewportRenderPlan MakeViewportPlan(RenderLayer cullingMask)
    {
        ViewportRenderPlan plan;
        plan.bEnabled = true;
        plan.bHasCamera = true;
        plan.RenderWidth = 640;
        plan.RenderHeight = 480;
        plan.PixelRect.Width = 640.0f;
        plan.PixelRect.Height = 480.0f;
        plan.Camera.CullingMask = cullingMask;
        return plan;
    }

    BoardProxy MakeBoardProxy(uint64_t objectId, uint64_t componentId, RenderLayer layer)
    {
        BoardProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.LayerMask = layer;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.bVisible = true;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        return proxy;
    }

    MeshProxy MakeMeshProxy(uint64_t objectId)
    {
        MeshProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = objectId + 1000;
        proxy.MeshHandle.Id = 100;
        proxy.MaterialCount = 1;
        proxy.Materials[0].Id = 200;
        proxy.WorldBounds.Radius = 1.0f;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.bVisible = true;
        return proxy;
    }

    Container::TSharedPtr<Viewport> MakeViewport()
    {
        ViewportSettings viewportSettings;
        auto viewport = Container::MakeShared<Viewport>();
        assert(viewport->Initialize(viewportSettings));
        return viewport;
    }

    void TestBoardReflectionAndTextureOptionalProxy()
    {
        const IClass *boardClass = BoardComponent::StaticClass();
        assert(boardClass);
        assert(boardClass->IsChildOf(NorvesLib::Core::Component::Component::StaticClass()));
        assert(ClassRegistry::Get().FindClass(boardClass->GetClassId()) == boardClass);

        World world;
        world.Initialize();
        Entity *entity = world.SpawnObject<Entity>();
        assert(entity);
        entity->SetLocalPosition(12.0f, 34.0f, 0.0f);
        entity->SetLocalScale(80.0f, 24.0f, 1.0f);

        BoardComponent *board = world.CreateComponent<BoardComponent>(entity);
        assert(board);
        assert(CastTo<BoardComponent>(board) == board);
        world.UpdateWorldTransforms();
        board->RefreshRenderTransformCache();

        BoardProxy proxy;
        assert(board->BuildBoardProxy(proxy));
        assert(proxy.ComponentId == board->GetComponentId());
        assert(proxy.ObjectId == entity->GetObjectId());
        assert(!proxy.Texture.IsValid());
        assert(proxy.LayerMask == RenderLayer::UI);
        assert(proxy.Space == BoardSpace::ScreenSpace);
        assert(proxy.WorldTransform.m30 == 12.0f);
        assert(proxy.WorldTransform.m31 == 34.0f);

        world.Finalize();
        std::cout << "TestBoardReflectionAndTextureOptionalProxy passed\n";
    }

    void TestWorldRoutesScreenSpaceBoardsByComponentId()
    {
        World world;
        world.Initialize();
        TrackingBoardSink sink;
        world.SetScreenSpaceBoardSink(&sink);

        Entity *entity = world.SpawnObject<Entity>();
        assert(entity);
        entity->SetLocalPosition(7.0f, 9.0f, 0.0f);

        BoardComponent *first = world.CreateComponent<BoardComponent>(entity);
        BoardComponent *second = world.CreateComponent<BoardComponent>(entity);
        assert(first);
        assert(second);
        assert(first->GetComponentId() != second->GetComponentId());

        world.SyncToSceneView();
        assert(sink.Proxies.size() == 2);
        assert(sink.Find(first->GetComponentId()));
        assert(sink.Find(second->GetComponentId()));
        assert(sink.Find(first->GetComponentId())->ObjectId == entity->GetObjectId());
        assert(sink.Find(second->GetComponentId())->ObjectId == entity->GetObjectId());
        assert(sink.Find(first->GetComponentId())->WorldTransform.m30 == 7.0f);
        assert(sink.StaleCount == 1);

        TrackingBoardSink replacementSink;
        world.SetScreenSpaceBoardSink(&replacementSink);
        world.SyncToSceneView();
        assert(replacementSink.Proxies.size() == 2);
        assert(replacementSink.Find(first->GetComponentId()));
        assert(replacementSink.Find(second->GetComponentId()));

        world.Finalize();
        std::cout << "TestWorldRoutesScreenSpaceBoardsByComponentId passed\n";
    }

    void TestWorldSpaceRemovesScreenSpaceProxyAndStaleRemovalRuns()
    {
        World world;
        world.Initialize();
        TrackingBoardSink sink;
        world.SetScreenSpaceBoardSink(&sink);

        Entity *entity = world.SpawnObject<Entity>();
        assert(entity);
        BoardComponent *board = world.CreateComponent<BoardComponent>(entity);
        assert(board);

        world.SyncToSceneView();
        assert(sink.Find(board->GetComponentId()));

        board->SetBoardSpace(BoardSpace::WorldSpace);
        world.SyncToSceneView();
        assert(!sink.Find(board->GetComponentId()));
        assert(sink.RemoveId == board->GetComponentId());
        assert(sink.StaleCount == 2);

        board->SetBoardSpace(BoardSpace::ScreenSpace);
        world.SyncToSceneView();
        assert(sink.Find(board->GetComponentId()));

        board->SetVisible(false);
        world.SyncToSceneView();
        assert(!sink.Find(board->GetComponentId()));

        world.Finalize();
        std::cout << "TestWorldSpaceRemovesScreenSpaceProxyAndStaleRemovalRuns passed\n";
    }

    void TestCanvasViewBoardStoreAndDrawCommandSnapshotShape()
    {
        CanvasView canvas;
        ViewSettings settings;
        settings.Type = ViewType::UI;
        settings.Width = 640;
        settings.Height = 480;
        assert(canvas.Initialize(settings));

        BoardProxy uiBoard = MakeBoardProxy(100, 10, RenderLayer::UI);
        BoardProxy defaultBoard = MakeBoardProxy(101, 11, RenderLayer::Default);
        canvas.UpdateBoardProxy(uiBoard.ComponentId, uiBoard);
        canvas.UpdateBoardProxy(defaultBoard.ComponentId, defaultBoard);
        assert(canvas.GetBoardProxies().size() == 2);

        canvas.PrepareBoardDrawCommands(MakeViewportPlan(RenderLayer::UI));
        assert(canvas.GetBoardDrawCommands().size() == 1);
        assert(canvas.GetBoardInstanceData().size() == 1);
        const DrawCommand &command = canvas.GetBoardDrawCommands()[0];
        assert(command.Type == DrawCommandType::DrawInstanced);
        assert(command.Draw.VertexOffset == 6);
        assert(command.Draw.InstanceCount == 1);
        assert(command.Draw.FirstInstance == 0);
        assert(command.Draw.InstanceDataOffset == 0);
        assert(command.Draw.bInstanced);
        assert(command.Draw.ObjectId == uiBoard.ObjectId);
        assert(canvas.GetBoardInstanceData()[0].ObjectColor[3] == 0.75f);
        assert(canvas.GetBoardInstanceData()[0].CustomData[0] == 640.0f);
        assert(canvas.GetBoardInstanceData()[0].CustomData[1] == 480.0f);

        Container::VariableArray<DrawCommand> packetCommands = canvas.GetBoardDrawCommands();
        Container::VariableArray<GPUSceneInstanceData> packetInstances = canvas.GetBoardInstanceData();
        canvas.RemoveBoardProxy(uiBoard.ComponentId);
        assert(canvas.GetBoardProxies().size() == 1);
        assert(packetCommands.size() == 1);
        assert(packetInstances.size() == 1);
        assert(packetCommands[0].Draw.ObjectId == uiBoard.ObjectId);

        canvas.PrepareBoardDrawCommands(MakeViewportPlan(RenderLayer::UI));
        assert(canvas.GetBoardDrawCommands().empty());
        canvas.Shutdown();
        std::cout << "TestCanvasViewBoardStoreAndDrawCommandSnapshotShape passed\n";
    }

    void TestRenderingCoordinatorAppendsCanvasBoardsAfterSceneCommands()
    {
        RenderingCoordinator coordinator;
        coordinator.m_bInitialized = true;
        coordinator.m_RenderWidth = 640;
        coordinator.m_RenderHeight = 480;
        coordinator.m_MaxDrawCallsPerFrame = 16;

        FramePacket packet;
        packet.SetState(FramePacketState::Writing);
        coordinator.m_CurrentPacket = &packet;

        SceneViewSettings sceneSettings;
        sceneSettings.bEnableFrustumCulling = false;
        sceneSettings.bEnableDistanceCulling = false;
        sceneSettings.bEnableInstancing = false;
        auto sceneView = Container::MakeShared<SceneView>();
        assert(sceneView->Initialize(sceneSettings));
        sceneView->AddViewport(MakeViewport());
        sceneView->AddMeshProxy(MakeMeshProxy(1));
        sceneView->AddMeshProxy(MakeMeshProxy(2));

        ViewSettings canvasSettings;
        canvasSettings.Type = ViewType::UI;
        canvasSettings.Width = 640;
        canvasSettings.Height = 480;
        auto canvasView = Container::MakeShared<CanvasView>();
        assert(canvasView->Initialize(canvasSettings));
        BoardProxy board = MakeBoardProxy(300, 30, RenderLayer::UI);
        canvasView->UpdateBoardProxy(board.ComponentId, board);

        coordinator.m_MainSceneView = sceneView;
        coordinator.m_CanvasView = canvasView;
        coordinator.m_Screen.AddView(sceneView, 0);
        coordinator.m_Screen.AddView(canvasView, 1);
        coordinator.m_Views.push_back(sceneView);
        coordinator.m_Views.push_back(canvasView);

        coordinator.GenerateDrawCommands();

        assert(packet.InstanceData.size() == 3);
        assert(packet.DrawCommands.size() == 3);
        assert(packet.DrawCommands[0].Draw.ObjectId == 1);
        assert(packet.DrawCommands[1].Draw.ObjectId == 2);
        assert(packet.DrawCommands[2].Draw.ObjectId == board.ObjectId);
        assert(packet.DrawCommands[0].Draw.FirstInstance == 0);
        assert(packet.DrawCommands[1].Draw.FirstInstance == 1);
        assert(packet.DrawCommands[2].Draw.FirstInstance == 2);
        assert(packet.DrawCommands[2].Draw.InstanceDataOffset == 2);

        assert(packet.DrawCommandRange.First == 0);
        assert(packet.DrawCommandRange.Count == 2);
        assert(packet.OpaqueCommandRange.First == 0);
        assert(packet.OpaqueCommandRange.Count == 2);
        assert(packet.TransparentCommandRange.Count == 0);

        assert(packet.Views.size() == 2);
        assert(packet.Views[0].Viewports.size() == 1);
        assert(packet.Views[1].Viewports.size() == 1);
        const ViewportRenderPlan &sceneViewport = packet.Views[0].Viewports[0];
        const ViewportRenderPlan &canvasViewport = packet.Views[1].Viewports[0];
        assert(sceneViewport.DrawCommandRange.First == 0);
        assert(sceneViewport.DrawCommandRange.Count == 2);
        assert(sceneViewport.OpaqueCommandRange.First == 0);
        assert(sceneViewport.OpaqueCommandRange.Count == 2);
        assert(sceneViewport.TransparentCommandRange.Count == 0);
        assert(canvasViewport.DrawCommandRange.First == 2);
        assert(canvasViewport.DrawCommandRange.Count == 1);
        assert(canvasViewport.TransparentCommandRange.First == 2);
        assert(canvasViewport.TransparentCommandRange.Count == 1);
        assert(canvasViewport.OpaqueCommandRange.Count == 0);

        coordinator.m_CurrentPacket = nullptr;
        coordinator.m_Views.clear();
        coordinator.m_CanvasView.reset();
        coordinator.m_MainSceneView.reset();
        canvasView->Shutdown();
        sceneView->Shutdown();
        std::cout << "TestRenderingCoordinatorAppendsCanvasBoardsAfterSceneCommands passed\n";
    }

    void TestRenderingCoordinatorCanvasSinkLifecycle()
    {
        NorvesLib::Core::Engine::Engine engine;
        NorvesLib::Core::Engine::Engine *previousEngine = NorvesLib::Core::Engine::GEngine;
        NorvesLib::Core::Engine::GEngine = &engine;

        RenderingCoordinator coordinator;
        coordinator.m_bInitialized = true;
        coordinator.m_RenderWidth = 640;
        coordinator.m_RenderHeight = 480;
        coordinator.m_PacketManager.Initialize();

        auto firstCanvas = coordinator.CreateCanvasView();
        assert(firstCanvas);
        assert(engine.GetWorld().GetScreenSpaceBoardSink() == firstCanvas.get());
        assert(!coordinator.m_CompositeAlphaOverFragmentShader);
        assert(!coordinator.m_CompositeAlphaOverDescriptorSet);

        coordinator.DestroyView(firstCanvas);
        assert(!coordinator.GetCanvasView());
        assert(engine.GetWorld().GetScreenSpaceBoardSink() == nullptr);

        auto secondCanvas = coordinator.CreateCanvasView();
        assert(secondCanvas);
        assert(secondCanvas.get() != firstCanvas.get());
        assert(engine.GetWorld().GetScreenSpaceBoardSink() == secondCanvas.get());

        coordinator.Shutdown();
        assert(engine.GetWorld().GetScreenSpaceBoardSink() == nullptr);

        NorvesLib::Core::Engine::GEngine = previousEngine;
        std::cout << "TestRenderingCoordinatorCanvasSinkLifecycle passed\n";
    }
}

int main()
{
    std::cout << "BoardComponentRoutingTest start\n";

    TestBoardReflectionAndTextureOptionalProxy();
    TestWorldRoutesScreenSpaceBoardsByComponentId();
    TestWorldSpaceRemovesScreenSpaceProxyAndStaleRemovalRuns();
    TestCanvasViewBoardStoreAndDrawCommandSnapshotShape();
    TestRenderingCoordinatorAppendsCanvasBoardsAfterSceneCommands();
    TestRenderingCoordinatorCanvasSinkLifecycle();

    std::cout << "BoardComponentRoutingTest passed\n";
    return 0;
}
