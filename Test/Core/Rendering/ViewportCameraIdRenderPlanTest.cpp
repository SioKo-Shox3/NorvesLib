#define _ALLOW_KEYWORD_MACROS
#define private public
#include "Rendering/RenderingCoordinator.h"
#undef private

#include "Rendering/SceneView.h"
#include "Rendering/View.h"
#include "Rendering/Viewport.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

namespace
{
    CameraProxy MakeCamera(float positionX, uint64_t cameraId = 0)
    {
        CameraProxy camera;
        camera.CameraId = cameraId;
        camera.PositionX = positionX;
        camera.Viewport.Width = 320.0f;
        camera.Viewport.Height = 200.0f;
        return camera;
    }

    Container::TSharedPtr<Viewport> MakeViewport(uint64_t cameraId, const CameraProxy &valueCamera)
    {
        auto viewport = Container::MakeShared<Viewport>();
        ViewportSettings settings;
        settings.X = 0.0f;
        settings.Y = 0.0f;
        settings.Width = 1.0f;
        settings.Height = 1.0f;
        assert(viewport->Initialize(settings));
        viewport->SetCamera(valueCamera);
        viewport->SetCameraId(cameraId);
        return viewport;
    }

    void PrepareCoordinator(RenderingCoordinator &coordinator, FramePacket &packet)
    {
        coordinator.m_bInitialized = true;
        coordinator.m_RenderWidth = 320;
        coordinator.m_RenderHeight = 200;
        coordinator.m_MaxDrawCallsPerFrame = 16;
        coordinator.m_CurrentPacket = &packet;
    }

    void PreparePreFrameCoordinator(RenderingCoordinator &coordinator)
    {
        coordinator.m_bInitialized = true;
        coordinator.m_bFrameSubmissionStarted = false;
        coordinator.m_CurrentPacket = nullptr;
    }
}

int main()
{
    std::cout << "ViewportCameraIdRenderPlanTest start\n";

    {
        RenderingCoordinator coordinator;
        FramePacket packet;
        PrepareCoordinator(coordinator, packet);

        CameraProxy tableCamera = MakeCamera(42.0f);
        const uint64_t cameraId = coordinator.RegisterCamera(tableCamera);

        auto view = Container::MakeShared<View>();
        ViewSettings settings;
        settings.Type = ViewType::Custom;
        assert(view->Initialize(settings));
        view->AddViewport(MakeViewport(cameraId, MakeCamera(99.0f)));
        coordinator.m_Screen.AddView(view, 0);

        coordinator.GenerateDrawCommands();

        assert(packet.Views.size() == 1);
        assert(packet.Views[0].Viewports.size() == 1);
        const ViewportRenderPlan &viewportPlan = packet.Views[0].Viewports[0];
        assert(viewportPlan.bHasCamera);
        assert(viewportPlan.Camera.CameraId == cameraId);
        assert(viewportPlan.Camera.PositionX == 42.0f);
        assert(viewportPlan.Camera.PositionX != 99.0f);
        std::cout << "TestCameraIdLookupCopiesResolvedCameraIntoSnapshot passed\n";
    }

    {
        RenderingCoordinator coordinator;
        FramePacket packet;
        PrepareCoordinator(coordinator, packet);

        auto sceneView = Container::MakeShared<SceneView>();
        SceneViewSettings sceneSettings;
        sceneSettings.bEnableFrustumCulling = false;
        sceneSettings.bEnableDistanceCulling = false;
        assert(sceneView->Initialize(sceneSettings));
        coordinator.m_MainSceneView = sceneView;
        coordinator.m_Screen.AddView(sceneView, 0);

        coordinator.SetMainCamera(MakeCamera(7.0f));
        sceneView->AddViewport(MakeViewport(0, CameraProxy{}));
        sceneView->AddViewport(MakeViewport(999, MakeCamera(200.0f)));
        sceneView->AddViewport(MakeViewport(1000, CameraProxy{}));

        coordinator.GenerateDrawCommands();

        assert(packet.Views.size() == 1);
        assert(packet.Views[0].Viewports.size() == 3);
        assert(packet.Views[0].Viewports[0].bHasCamera);
        assert(packet.Views[0].Viewports[0].Camera.PositionX == 7.0f);
        assert(packet.Views[0].Viewports[1].bHasCamera);
        assert(packet.Views[0].Viewports[1].Camera.PositionX == 200.0f);
        assert(packet.Views[0].Viewports[2].bHasCamera);
        assert(packet.Views[0].Viewports[2].Camera.PositionX == 7.0f);
        std::cout << "TestMissingAndZeroCameraIdPreserveValueThenFallbackToMainCamera passed\n";
    }

    {
        RenderingCoordinator coordinator;
        PreparePreFrameCoordinator(coordinator);

        auto sceneView = Container::MakeShared<SceneView>();
        SceneViewSettings sceneSettings;
        sceneSettings.bEnableFrustumCulling = false;
        sceneSettings.bEnableDistanceCulling = false;
        assert(sceneView->Initialize(sceneSettings));
        auto mainViewport = MakeViewport(0, CameraProxy{});
        sceneView->AddViewport(mainViewport);
        coordinator.m_MainSceneView = sceneView;

        coordinator.SetMainCamera(MakeCamera(11.0f));
        assert(coordinator.m_MainCameraId == 1);
        assert(coordinator.GetMainCamera().CameraId == coordinator.m_MainCameraId);
        const CameraProxy *storedMain = coordinator.FindCamera(coordinator.m_MainCameraId);
        assert(storedMain);
        assert(storedMain->CameraId == coordinator.m_MainCameraId);
        assert(storedMain->PositionX == 11.0f);
        assert(mainViewport->GetCameraId() == coordinator.m_MainCameraId);
        assert(mainViewport->GetCamera().CameraId == coordinator.m_MainCameraId);
        assert(mainViewport->GetCamera().PositionX == 11.0f);

        coordinator.SetMainCamera(MakeCamera(12.0f, 999));
        assert(coordinator.m_MainCameraId == 1);
        assert(coordinator.GetMainCamera().CameraId == coordinator.m_MainCameraId);
        storedMain = coordinator.FindCamera(coordinator.m_MainCameraId);
        assert(storedMain);
        assert(storedMain->CameraId == coordinator.m_MainCameraId);
        assert(storedMain->PositionX == 12.0f);
        assert(mainViewport->GetCameraId() == coordinator.m_MainCameraId);
        assert(mainViewport->GetCamera().CameraId == coordinator.m_MainCameraId);
        assert(mainViewport->GetCamera().PositionX == 12.0f);
        assert(coordinator.FindCamera(999) == nullptr);
        std::cout << "TestSetMainCameraMirrorsAssignedIdToMainViewport passed\n";
    }

    {
        RenderingCoordinator coordinator;
        PreparePreFrameCoordinator(coordinator);
        coordinator.m_Width = 200;
        coordinator.m_Height = 100;
        coordinator.m_RenderWidth = 200;
        coordinator.m_RenderHeight = 100;

        auto canvasView = coordinator.CreateCanvasView();
        assert(canvasView);
        assert(coordinator.m_CanvasCameraId == 1);

        coordinator.m_RenderWidth = 640;
        coordinator.m_RenderHeight = 480;
        coordinator.m_bCanvasCameraSyncPending.Store(true);

        const CameraProxy *pendingCamera = coordinator.FindCamera(coordinator.m_CanvasCameraId);
        assert(pendingCamera);
        assert(pendingCamera->OrthoWidth == 200.0f);
        assert(pendingCamera->OrthoHeight == 100.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoWidth == 200.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoHeight == 100.0f);

        coordinator.m_PacketManager.Initialize();
        coordinator.BeginFrame();
        assert(!coordinator.m_bCanvasCameraSyncPending.Load());

        const CameraProxy *syncedCamera = coordinator.FindCamera(coordinator.m_CanvasCameraId);
        assert(syncedCamera);
        assert(syncedCamera->OrthoWidth == 640.0f);
        assert(syncedCamera->OrthoHeight == 480.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoWidth == 640.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoHeight == 480.0f);
        std::cout << "TestPendingCanvasCameraSyncConsumedByBeginFrame passed\n";
    }

    {
        RenderingCoordinator coordinator;
        PreparePreFrameCoordinator(coordinator);
        coordinator.m_Width = 200;
        coordinator.m_Height = 100;
        coordinator.m_RenderWidth = 200;
        coordinator.m_RenderHeight = 100;

        auto canvasView = coordinator.CreateCanvasView();
        assert(canvasView);
        assert(coordinator.m_CanvasCameraId == 1);

        const CameraProxy *initialCamera = coordinator.FindCamera(coordinator.m_CanvasCameraId);
        assert(initialCamera);
        assert(initialCamera->Projection == ProjectionType::Orthographic);
        assert(initialCamera->CullingMask == RenderLayer::UI);
        assert(initialCamera->OrthoWidth == 200.0f);
        assert(initialCamera->OrthoHeight == 100.0f);
        assert(canvasView->GetMainViewport());
        assert(canvasView->GetMainViewport()->GetCameraId() == coordinator.m_CanvasCameraId);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoWidth == 200.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoHeight == 100.0f);

        coordinator.SetRenderScale(0.5f);
        const CameraProxy *scaledCamera = coordinator.FindCamera(coordinator.m_CanvasCameraId);
        assert(scaledCamera);
        assert(scaledCamera->OrthoWidth == 100.0f);
        assert(scaledCamera->OrthoHeight == 50.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoWidth == 100.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoHeight == 50.0f);

        coordinator.Resize(300, 120);
        const CameraProxy *resizedCamera = coordinator.FindCamera(coordinator.m_CanvasCameraId);
        assert(resizedCamera);
        assert(resizedCamera->OrthoWidth == 150.0f);
        assert(resizedCamera->OrthoHeight == 60.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoWidth == 150.0f);
        assert(canvasView->GetMainViewport()->GetCamera().OrthoHeight == 60.0f);
        std::cout << "TestCanvasCameraTracksRenderScaleAndResize passed\n";
    }

    {
        RenderingCoordinator coordinator;
        PreparePreFrameCoordinator(coordinator);
        coordinator.m_RenderWidth = 320;
        coordinator.m_RenderHeight = 200;

        auto firstCanvas = coordinator.CreateCanvasView();
        assert(firstCanvas);
        const uint64_t firstCanvasCameraId = coordinator.m_CanvasCameraId;
        assert(firstCanvasCameraId == 1);
        assert(firstCanvas->GetMainViewport()->GetCameraId() == firstCanvasCameraId);

        coordinator.DestroyView(firstCanvas);
        assert(!coordinator.m_CanvasView);
        assert(coordinator.m_CanvasCameraId == 0);

        auto secondCanvas = coordinator.CreateCanvasView();
        assert(secondCanvas);
        const uint64_t secondCanvasCameraId = coordinator.m_CanvasCameraId;
        assert(secondCanvasCameraId != 0);
        assert(secondCanvasCameraId != firstCanvasCameraId);
        assert(secondCanvas->GetMainViewport()->GetCameraId() == secondCanvasCameraId);
        assert(secondCanvas->GetMainViewport()->GetCamera().CameraId == secondCanvasCameraId);
        std::cout << "TestCanvasDestroyRecreateUsesFreshCameraId passed\n";
    }

    std::cout << "ViewportCameraIdRenderPlanTest passed\n";
    return 0;
}
