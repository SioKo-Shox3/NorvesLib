#include "Rendering/SceneView.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    MeshProxy MakeProxy(uint64_t objectId, uint64_t meshId, uint64_t materialId)
    {
        MeshProxy proxy;
        proxy.ObjectId = objectId;
        proxy.MeshHandle.Id = meshId;
        proxy.MaterialCount = 1;
        proxy.Materials[0].Id = materialId;
        proxy.WorldBounds.Radius = 1.0f;
        return proxy;
    }

    ViewportSnapshot MakeSnapshot()
    {
        ViewportSnapshot snapshot;
        snapshot.ViewId = 1;
        snapshot.ViewportId = 2;
        snapshot.RenderWidth = 1280;
        snapshot.RenderHeight = 720;
        snapshot.PixelRect.Width = 1280.0f;
        snapshot.PixelRect.Height = 720.0f;
        snapshot.Camera.PositionZ = 5.0f;
        snapshot.Camera.ForwardZ = -1.0f;
        snapshot.Camera.UpY = 1.0f;
        snapshot.Camera.FieldOfView = 60.0f;
        snapshot.Camera.NearPlane = 0.1f;
        snapshot.Camera.FarPlane = 1000.0f;
        snapshot.Camera.Viewport.Width = 1280.0f;
        snapshot.Camera.Viewport.Height = 720.0f;
        snapshot.bHasCamera = snapshot.Camera.IsValid();
        return snapshot;
    }
}

int main()
{
    std::cout << "SceneViewViewportCommandTest start\n";

    SceneView view;
    SceneViewSettings settings;
    settings.bEnableFrustumCulling = false;
    settings.bEnableDistanceCulling = false;
    assert(view.Initialize(settings));

    view.AddMeshProxy(MakeProxy(1, 100, 200));
    view.AddMeshProxy(MakeProxy(2, 100, 200));

    ViewportSnapshot snapshot = MakeSnapshot();
    assert(snapshot.bHasCamera);

    view.PrepareDrawCommandsForViewport(snapshot);

    const auto &commands = view.GetDrawCommands();
    assert(commands.size() == 1);
    assert(commands[0].Type == DrawCommandType::DrawIndexedInstanced);
    assert(commands[0].Draw.bInstanced);
    assert(commands[0].Draw.InstanceCount == 2);
    assert(view.GetStats().VisibleProxies == 2);
    assert(view.GetStats().BatchCount == 1);
    assert(view.GetStats().DrawCommandCount == 1);

    view.Shutdown();

    std::cout << "SceneViewViewportCommandTest passed\n";
    return 0;
}
