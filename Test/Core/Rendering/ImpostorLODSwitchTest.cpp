#include "Rendering/SceneView.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    ViewportSnapshot MakeViewport(float cameraZ)
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
        viewport.Camera.PositionZ = cameraZ;
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
        viewport.Camera.CullingMask = RenderLayer::Default;
        viewport.Camera.Viewport.Width = 640.0f;
        viewport.Camera.Viewport.Height = 480.0f;
        return viewport;
    }

    MeshProxy MakeSourceMesh(uint64_t componentId)
    {
        MeshProxy proxy;
        proxy.ObjectId = 11u;
        proxy.ComponentId = componentId;
        proxy.MeshHandle.Id = 200u;
        proxy.Materials[0].Id = 300u;
        proxy.MaterialBlendModes[0] = BlendMode::Opaque;
        proxy.MaterialCount = 1u;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.WorldTransform.SetTranslationRow(Math::Vector3(0.0f, 0.0f, -2.0f));
        proxy.WorldBounds.CenterX = 0.0f;
        proxy.WorldBounds.CenterY = 0.0f;
        proxy.WorldBounds.CenterZ = -2.0f;
        proxy.WorldBounds.Radius = 1.0f;
        proxy.LayerMask = RenderLayer::Default;
        proxy.bVisible = true;
        return proxy;
    }

    BoardProxy MakeImpostorBoard(uint64_t componentId,
                                 uint64_t sourceMeshComponentId,
                                 float switchDistance)
    {
        BoardProxy proxy;
        proxy.ObjectId = 12u;
        proxy.ComponentId = componentId;
        proxy.SourceMeshComponentId = sourceMeshComponentId;
        proxy.RenderSubtype = BoardRenderSubtype::Impostor;
        proxy.Space = BoardSpace::WorldSpace;
        proxy.LayerMask = RenderLayer::Default;
        proxy.BlendModeProp = BlendMode::Translucent;
        proxy.SizeWorld = Math::Vector2(1.0f, 1.0f);
        proxy.Pivot = Math::Vector2(0.5f, 0.5f);
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.WorldTransform.SetTranslationRow(Math::Vector3(0.0f, 0.0f, -2.0f));
        proxy.WorldBounds.CenterX = 0.0f;
        proxy.WorldBounds.CenterY = 0.0f;
        proxy.WorldBounds.CenterZ = -2.0f;
        proxy.WorldBounds.Radius = 1.0f;
        proxy.LODSwitchDistance = switchDistance;
        proxy.ImpostorCellResolution = 64u;
        proxy.ImpostorAxisCellCountX = 4u;
        proxy.ImpostorAxisCellCountY = 4u;
        proxy.ImpostorAtlasWidth = 256u;
        proxy.ImpostorAtlasHeight = 256u;
        proxy.bVisible = true;
        return proxy;
    }

    void InitializeSceneView(SceneView &sceneView)
    {
        SceneViewSettings settings;
        settings.bEnableFrustumCulling = false;
        settings.bEnableDistanceCulling = false;
        settings.bEnableInstancing = false;
        assert(sceneView.Initialize(settings));
    }

    void TestPerViewportNearFarSwitchAndRestore()
    {
        constexpr uint64_t sourceMeshComponentId = 101u;
        SceneView sceneView;
        InitializeSceneView(sceneView);
        sceneView.AddMeshProxy(MakeSourceMesh(sourceMeshComponentId));
        sceneView.UpdateBoardProxy(MakeImpostorBoard(201u, sourceMeshComponentId, 4.0f));

        sceneView.PrepareDrawCommandsForViewport(MakeViewport(0.0f));
        assert(sceneView.GetMeshProxies().size() == 1);
        assert(sceneView.GetBoardProxies().size() == 1);
        assert(sceneView.GetVisibleMeshProxies().size() == 1);
        assert(sceneView.GetOpaqueCommands().size() == 1);
        assert(sceneView.GetTransparentCommands().empty());

        sceneView.PrepareDrawCommandsForViewport(MakeViewport(4.0f));
        assert(sceneView.GetMeshProxies().size() == 1);
        assert(sceneView.GetBoardProxies().size() == 1);
        assert(sceneView.GetVisibleMeshProxies().empty());
        assert(sceneView.GetOpaqueCommands().empty());
        assert(sceneView.GetTransparentCommands().size() == 1);

        const DrawCommand &impostorCommand = sceneView.GetTransparentCommands()[0];
        assert(impostorCommand.Draw.PayloadKind == DrawPayloadKind::Board);
        assert(impostorCommand.Draw.BoardSubtype == BoardRenderSubtype::Impostor);
        assert(impostorCommand.Draw.SourceMeshComponentId == sourceMeshComponentId);
        assert(sceneView.GetInstanceData().size() == 1);
        const float* impostorMetadata = sceneView.GetInstanceData()[0].NormalMatrix;
        assert(impostorMetadata[10] == 4.0f);
        assert(impostorMetadata[11] == 4.0f);
        assert(sceneView.GetInstanceData()[0].CustomData[0] == 64.0f);
        assert(sceneView.GetInstanceData()[0].CustomData[1] == 256.0f);
        assert(sceneView.GetInstanceData()[0].CustomData[2] == 256.0f);
        assert(sceneView.GetInstanceData()[0].CustomData[3] == 4.0f);

        sceneView.PrepareDrawCommandsForViewport(MakeViewport(0.0f));
        assert(sceneView.GetVisibleMeshProxies().size() == 1);
        assert(sceneView.GetOpaqueCommands().size() == 1);
        assert(sceneView.GetTransparentCommands().empty());

        sceneView.Shutdown();
        std::cout << "TestPerViewportNearFarSwitchAndRestore passed\n";
    }

    void TestZeroSwitchDistanceIsAlwaysEligible()
    {
        constexpr uint64_t sourceMeshComponentId = 301u;
        SceneView sceneView;
        InitializeSceneView(sceneView);
        sceneView.AddMeshProxy(MakeSourceMesh(sourceMeshComponentId));
        sceneView.UpdateBoardProxy(MakeImpostorBoard(401u, sourceMeshComponentId, 0.0f));

        sceneView.PrepareDrawCommandsForViewport(MakeViewport(0.0f));
        assert(sceneView.GetVisibleMeshProxies().empty());
        assert(sceneView.GetOpaqueCommands().empty());
        assert(sceneView.GetTransparentCommands().size() == 1);
        assert(sceneView.GetTransparentCommands()[0].Draw.BoardSubtype == BoardRenderSubtype::Impostor);

        sceneView.Shutdown();
        std::cout << "TestZeroSwitchDistanceIsAlwaysEligible passed\n";
    }
}

int main()
{
    std::cout << "ImpostorLODSwitchTest start\n";
    TestPerViewportNearFarSwitchAndRestore();
    TestZeroSwitchDistanceIsAlwaysEligible();
    std::cout << "ImpostorLODSwitchTest passed\n";
    return 0;
}
