#include "Rendering/Screen.h"
#include "Rendering/View.h"
#include "Rendering/Viewport.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

int main()
{
    std::cout << "ViewScreenTopologyTest start\n";

    auto backgroundView = Container::MakeShared<View>();
    auto sceneView = Container::MakeShared<View>();
    auto overlayView = Container::MakeShared<View>();

    ViewSettings viewSettings;
    assert(backgroundView->Initialize(viewSettings));
    assert(sceneView->Initialize(viewSettings));
    assert(overlayView->Initialize(viewSettings));

    Screen screen;
    screen.AddView(sceneView, 10);
    screen.AddView(backgroundView, -10);
    screen.AddView(overlayView, 20);

    const auto &views = screen.GetViews();
    assert(views.size() == 3);
    assert(views[0] == backgroundView);
    assert(views[1] == sceneView);
    assert(views[2] == overlayView);

    screen.RemoveView(sceneView);
    assert(screen.GetViews().size() == 2);
    assert(screen.GetViews()[0] == backgroundView);
    assert(screen.GetViews()[1] == overlayView);

    auto viewport = Container::MakeShared<Viewport>();
    ViewportSettings viewportSettings;
    viewportSettings.X = 0.25f;
    viewportSettings.Y = 0.5f;
    viewportSettings.Width = 0.5f;
    viewportSettings.Height = 0.25f;
    assert(viewport->Initialize(viewportSettings));

    const uint32_t viewportIndex = backgroundView->AddViewport(viewport);
    assert(viewportIndex == 0);
    assert(backgroundView->GetViewportCount() == 1);
    assert(backgroundView->GetMainViewport() == viewport);

    uint32_t pixelX = 0;
    uint32_t pixelY = 0;
    uint32_t pixelWidth = 0;
    uint32_t pixelHeight = 0;
    viewport->GetPixelRect(1920, 1080, pixelX, pixelY, pixelWidth, pixelHeight);
    assert(pixelX == 480);
    assert(pixelY == 540);
    assert(pixelWidth == 960);
    assert(pixelHeight == 270);

    CameraProxy camera;
    camera.PositionX = 1.0f;
    camera.PositionY = 2.0f;
    camera.PositionZ = 3.0f;
    camera.Viewport.Width = 960.0f;
    camera.Viewport.Height = 270.0f;
    viewport->SetCamera(camera);
    assert(viewport->GetCamera().PositionX == 1.0f);
    assert(viewport->GetCamera().PositionY == 2.0f);
    assert(viewport->GetCamera().PositionZ == 3.0f);

    backgroundView->Shutdown();
    sceneView->Shutdown();
    overlayView->Shutdown();

    std::cout << "ViewScreenTopologyTest passed\n";
    return 0;
}
