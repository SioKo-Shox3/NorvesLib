#include "Rendering/SceneView.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    MeshProxy MakeProxy(uint64_t objectId,
                        uint64_t meshId,
                        uint64_t materialId,
                        float centerX = 0.0f,
                        float centerY = 0.0f,
                        float centerZ = 0.0f,
                        float radius = 1.0f)
    {
        MeshProxy proxy{};
        proxy.ObjectId = objectId;
        proxy.MeshHandle.Id = meshId;
        proxy.MaterialCount = 1;
        proxy.Materials[0].Id = materialId;
        proxy.WorldBounds.CenterX = centerX;
        proxy.WorldBounds.CenterY = centerY;
        proxy.WorldBounds.CenterZ = centerZ;
        proxy.WorldBounds.Radius = radius;
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

    {
        SceneView view;
        SceneViewSettings settings;
        settings.bEnableFrustumCulling = false;
        settings.bEnableDistanceCulling = false;
        settings.bEnableInstancing = false;
        assert(view.Initialize(settings));

        view.AddMeshProxy(MakeProxy(1, 100, 200));
        view.AddMeshProxy(MakeProxy(2, 100, 200));

        ViewportSnapshot snapshot = MakeSnapshot();
        assert(snapshot.bHasCamera);

        view.PrepareDrawCommandsForViewport(snapshot);

        const auto &commands = view.GetDrawCommands();
        assert(commands.size() == 2);
        assert(view.GetInstanceData().size() == 2);
        assert(commands[0].Type == DrawCommandType::DrawIndexed);
        assert(commands[1].Type == DrawCommandType::DrawIndexed);
        assert(!commands[0].Draw.bInstanced);
        assert(!commands[1].Draw.bInstanced);
        assert(commands[0].Draw.InstanceCount == 1);
        assert(commands[1].Draw.InstanceCount == 1);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[1].Draw.FirstInstance == 1);
        assert(view.GetStats().VisibleProxies == 2);
        assert(view.GetStats().BatchCount == 1);
        assert(view.GetStats().DrawCommandCount == 2);

        view.Shutdown();
    }

    {
        SceneView view;
        SceneViewSettings settings;
        settings.bEnableFrustumCulling = true;
        settings.bEnableDistanceCulling = false;
        settings.bEnableInstancing = false;
        assert(view.Initialize(settings));

        view.AddMeshProxy(MakeProxy(1, 100, 200, 0.0f, 0.5f, 0.0f, 1.0f));
        view.AddMeshProxy(MakeProxy(2, 100, 200, 0.0f, -1.0f, 0.0f, 7.1f));
        view.AddMeshProxy(MakeProxy(3, 100, 200, 4.0f, 1.0f, 0.0f, 0.2f));
        view.AddMeshProxy(MakeProxy(4, 100, 200, 0.0f, 2.5f, 8.0f, 0.5f));

        ViewportSnapshot snapshot = MakeSnapshot();
        assert(snapshot.bHasCamera);

        view.PrepareDrawCommandsForViewport(snapshot);

        const auto &commands = view.GetDrawCommands();
        assert(commands.size() == 3);
        assert(view.GetInstanceData().size() == 3);
        assert(commands[0].Type == DrawCommandType::DrawIndexed);
        assert(commands[1].Type == DrawCommandType::DrawIndexed);
        assert(commands[2].Type == DrawCommandType::DrawIndexed);
        assert(!commands[0].Draw.bInstanced);
        assert(!commands[1].Draw.bInstanced);
        assert(!commands[2].Draw.bInstanced);
        assert(commands[0].Draw.FirstInstance == 0);
        assert(commands[1].Draw.FirstInstance == 1);
        assert(commands[2].Draw.FirstInstance == 2);
        assert(view.GetStats().VisibleProxies == 3);
        assert(view.GetStats().CulledProxies == 1);
        assert(view.GetStats().DrawCommandCount == 3);

        view.Shutdown();
    }

    std::cout << "SceneViewViewportCommandTest passed\n";
    return 0;
}
