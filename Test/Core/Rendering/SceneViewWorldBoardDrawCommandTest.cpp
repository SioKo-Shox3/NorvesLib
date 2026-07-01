#include "Rendering/SceneView.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    ViewportSnapshot MakeViewport(RenderLayer cullingMask)
    {
        ViewportSnapshot viewport;
        viewport.bEnabled = true;
        viewport.bHasCamera = true;
        viewport.RenderWidth = 640;
        viewport.RenderHeight = 480;
        viewport.PixelRect.Width = 640.0f;
        viewport.PixelRect.Height = 480.0f;
        viewport.Camera.PositionX = 0.0f;
        viewport.Camera.PositionY = 0.0f;
        viewport.Camera.PositionZ = 0.0f;
        viewport.Camera.ForwardX = 0.0f;
        viewport.Camera.ForwardY = 0.0f;
        viewport.Camera.ForwardZ = -1.0f;
        viewport.Camera.UpX = 0.0f;
        viewport.Camera.UpY = 1.0f;
        viewport.Camera.UpZ = 0.0f;
        viewport.Camera.RightX = 1.0f;
        viewport.Camera.RightY = 0.0f;
        viewport.Camera.RightZ = 0.0f;
        viewport.Camera.AspectRatio = 640.0f / 480.0f;
        viewport.Camera.CullingMask = cullingMask;
        viewport.Camera.Viewport.Width = 640.0f;
        viewport.Camera.Viewport.Height = 480.0f;
        return viewport;
    }

    BoardProxy MakeWorldBoard(uint64_t objectId,
                              uint64_t componentId,
                              float x,
                              float y,
                              float z,
                              RenderLayer layer,
                              BlendMode blendMode)
    {
        BoardProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.LayerMask = layer;
        proxy.Space = BoardSpace::WorldSpace;
        proxy.BlendModeProp = blendMode;
        proxy.SizeWorld = Math::Vector2(1.0f, 1.0f);
        proxy.Pivot = Math::Vector2(0.5f, 0.5f);
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.WorldTransform.SetTranslationRow(Math::Vector3(x, y, z));
        proxy.WorldBounds.CenterX = x;
        proxy.WorldBounds.CenterY = y;
        proxy.WorldBounds.CenterZ = z;
        proxy.WorldBounds.Radius = 0.75f;
        proxy.bVisible = true;
        return proxy;
    }

    MeshProxy MakeTransparentMesh(uint64_t objectId, float z)
    {
        MeshProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = objectId + 1000u;
        proxy.MeshHandle.Id = objectId + 2000u;
        proxy.Materials[0].Id = objectId + 3000u;
        proxy.MaterialBlendModes[0] = BlendMode::Translucent;
        proxy.MaterialCount = 1;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.WorldTransform.SetTranslationRow(Math::Vector3(0.0f, 0.0f, z));
        proxy.WorldBounds.CenterZ = z;
        proxy.WorldBounds.Radius = 1.0f;
        proxy.LayerMask = RenderLayer::Default;
        proxy.bVisible = true;
        return proxy;
    }

    void InitializeSceneView(SceneView &sceneView,
                             bool bEnableFrustumCulling,
                             bool bEnableDistanceCulling,
                             float maxDrawDistance = 10000.0f)
    {
        SceneViewSettings settings;
        settings.bEnableFrustumCulling = bEnableFrustumCulling;
        settings.bEnableDistanceCulling = bEnableDistanceCulling;
        settings.bEnableInstancing = false;
        settings.MaxDrawDistance = maxDrawDistance;
        assert(sceneView.Initialize(settings));
    }

    void TestNoCameraAndCullingMaskFilterWorldBoards()
    {
        SceneView sceneView;
        InitializeSceneView(sceneView, false, false);
        sceneView.UpdateBoardProxy(MakeWorldBoard(1, 10, 0.0f, 0.0f, -2.0f, RenderLayer::Default, BlendMode::Opaque));

        ViewportSnapshot noCamera = MakeViewport(RenderLayer::Default);
        noCamera.bHasCamera = false;
        sceneView.PrepareDrawCommandsForViewport(noCamera);
        assert(sceneView.GetTransparentCommands().empty());

        sceneView.PrepareDrawCommandsForViewport(MakeViewport(RenderLayer::UI));
        assert(sceneView.GetTransparentCommands().empty());

        sceneView.Shutdown();
        std::cout << "TestNoCameraAndCullingMaskFilterWorldBoards passed\n";
    }

    void TestDistanceCullsWorldBoards()
    {
        SceneView sceneView;
        InitializeSceneView(sceneView, false, true, 5.0f);
        sceneView.UpdateBoardProxy(MakeWorldBoard(2, 20, 0.0f, 0.0f, -20.0f, RenderLayer::Default, BlendMode::Translucent));
        sceneView.PrepareDrawCommandsForViewport(MakeViewport(RenderLayer::Default));
        assert(sceneView.GetTransparentCommands().empty());

        sceneView.UpdateBoardProxy(MakeWorldBoard(2, 20, 0.0f, 0.0f, -2.0f, RenderLayer::Default, BlendMode::Translucent));
        sceneView.PrepareDrawCommandsForViewport(MakeViewport(RenderLayer::Default));
        assert(sceneView.GetTransparentCommands().size() == 1);

        sceneView.Shutdown();
        std::cout << "TestDistanceCullsWorldBoards passed\n";
    }

    void TestFrustumCullsWorldBoards()
    {
        SceneView sceneView;
        InitializeSceneView(sceneView, true, false);
        sceneView.UpdateBoardProxy(MakeWorldBoard(5, 50, 0.0f, 0.0f, 2.0f, RenderLayer::Default, BlendMode::Translucent));
        sceneView.PrepareDrawCommandsForViewport(MakeViewport(RenderLayer::Default));
        assert(sceneView.GetTransparentCommands().empty());

        sceneView.UpdateBoardProxy(MakeWorldBoard(5, 50, 0.0f, 0.0f, -2.0f, RenderLayer::Default, BlendMode::Translucent));
        sceneView.PrepareDrawCommandsForViewport(MakeViewport(RenderLayer::Default));
        assert(sceneView.GetTransparentCommands().size() == 1);

        sceneView.Shutdown();
        std::cout << "TestFrustumCullsWorldBoards passed\n";
    }

    void TestWorldBoardCommandsUseBoardPayloadAndTransparentRange()
    {
        SceneView sceneView;
        InitializeSceneView(sceneView, false, false);
        sceneView.AddMeshProxy(MakeTransparentMesh(30, -10.0f));
        sceneView.UpdateBoardProxy(MakeWorldBoard(3, 30, 0.0f, 0.0f, -2.0f, RenderLayer::Default, BlendMode::Opaque));
        sceneView.PrepareDrawCommandsForViewport(MakeViewport(RenderLayer::Default));

        assert(sceneView.GetInstanceData().size() == 2);
        assert(sceneView.GetOpaqueCommands().empty());
        assert(sceneView.GetTransparentCommands().size() == 2);
        assert(sceneView.GetTransparentCommands()[0].Draw.PayloadKind == DrawPayloadKind::Mesh);
        assert(sceneView.GetTransparentCommands()[1].Draw.PayloadKind == DrawPayloadKind::Board);

        const DrawCommand &boardCommand = sceneView.GetTransparentCommands()[1];
        assert(boardCommand.Type == DrawCommandType::DrawInstanced);
        assert(boardCommand.Draw.VertexOffset == 6);
        assert(boardCommand.Draw.InstanceCount == 1);
        assert(boardCommand.Draw.FirstInstance == 1);
        assert(boardCommand.Draw.InstanceDataOffset == 1);
        assert(boardCommand.Draw.MaterialBlendMode == BlendMode::Opaque);

        sceneView.UpdateBoardProxy(MakeWorldBoard(4, 40, 0.0f, 0.0f, -3.0f, RenderLayer::Default, BlendMode::Masked));
        sceneView.RemoveBoardProxy(30);
        sceneView.PrepareDrawCommandsForViewport(MakeViewport(RenderLayer::Default));
        assert(sceneView.GetOpaqueCommands().empty());
        assert(sceneView.GetTransparentCommands().size() == 2);
        const DrawCommand &maskedBoardCommand = sceneView.GetTransparentCommands()[1];
        assert(maskedBoardCommand.Draw.PayloadKind == DrawPayloadKind::Board);
        assert(maskedBoardCommand.Draw.MaterialBlendMode == BlendMode::Masked);

        sceneView.Shutdown();
        std::cout << "TestWorldBoardCommandsUseBoardPayloadAndTransparentRange passed\n";
    }
}

int main()
{
    std::cout << "SceneViewWorldBoardDrawCommandTest start\n";
    TestNoCameraAndCullingMaskFilterWorldBoards();
    TestDistanceCullsWorldBoards();
    TestFrustumCullsWorldBoards();
    TestWorldBoardCommandsUseBoardPayloadAndTransparentRange();
    std::cout << "SceneViewWorldBoardDrawCommandTest passed\n";
    return 0;
}
