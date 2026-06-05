#include "Rendering/ViewRenderContext.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

int main()
{
    std::cout << "ViewRenderContextActiveInputTest start\n";

    ViewRenderContext context;

    CameraProxy mainCamera;
    mainCamera.CameraId = 1;
    CameraProxy currentCamera;
    currentCamera.CameraId = 2;

    context.MainCamera = &mainCamera;
    assert(context.GetActiveCamera() == &mainCamera);
    context.CurrentCamera = &currentCamera;
    assert(context.GetActiveCamera() == &currentCamera);

    Container::VariableArray<DrawCommand> snapshotCommands;
    Container::VariableArray<DrawCommand> currentCommands;
    snapshotCommands.push_back(DrawCommand::CreateDraw());
    currentCommands.push_back(DrawCommand::CreateDrawIndexed());

    context.SnapshotDrawCommands = &snapshotCommands;
    assert(context.GetActiveDrawCommands() == &snapshotCommands);
    context.CurrentDrawCommands = &currentCommands;
    assert(context.GetActiveDrawCommands() == &currentCommands);

    Container::VariableArray<DrawCommand> snapshotOpaqueCommands;
    Container::VariableArray<DrawCommand> currentOpaqueCommands;
    context.SnapshotOpaqueCommands = &snapshotOpaqueCommands;
    assert(context.GetActiveOpaqueCommands() == &snapshotOpaqueCommands);
    context.CurrentOpaqueCommands = &currentOpaqueCommands;
    assert(context.GetActiveOpaqueCommands() == &currentOpaqueCommands);

    Container::VariableArray<DrawCommand> snapshotTransparentCommands;
    Container::VariableArray<DrawCommand> currentTransparentCommands;
    context.SnapshotTransparentCommands = &snapshotTransparentCommands;
    assert(context.GetActiveTransparentCommands() == &snapshotTransparentCommands);
    context.CurrentTransparentCommands = &currentTransparentCommands;
    assert(context.GetActiveTransparentCommands() == &currentTransparentCommands);

    context.RenderWidth = 1280;
    context.RenderHeight = 720;
    assert(context.GetActiveRenderWidth() == 1280);
    assert(context.GetActiveRenderHeight() == 720);

    RHI::Viewport fallbackLocalViewport = context.GetActiveLocalViewport();
    assert(fallbackLocalViewport.x == 0.0f);
    assert(fallbackLocalViewport.y == 0.0f);
    assert(fallbackLocalViewport.width == 1280.0f);
    assert(fallbackLocalViewport.height == 720.0f);

    ViewportSnapshot currentViewport;
    currentViewport.bEnabled = true;
    currentViewport.RenderWidth = 1280;
    currentViewport.RenderHeight = 720;
    currentViewport.PixelRect.X = 160.0f;
    currentViewport.PixelRect.Y = 90.0f;
    currentViewport.PixelRect.Width = 640.0f;
    currentViewport.PixelRect.Height = 360.0f;
    currentViewport.PixelRect.MinDepth = 0.2f;
    currentViewport.PixelRect.MaxDepth = 0.8f;
    currentViewport.Scissor.Left = 160;
    currentViewport.Scissor.Top = 90;
    currentViewport.Scissor.Right = 800;
    currentViewport.Scissor.Bottom = 450;

    context.CurrentViewport = &currentViewport;
    assert(context.GetActiveRenderWidth() == 640);
    assert(context.GetActiveRenderHeight() == 360);
    assert(context.GetActiveAspectRatio() > 1.77f && context.GetActiveAspectRatio() < 1.78f);

    RHI::Viewport localViewport = context.GetActiveLocalViewport();
    assert(localViewport.x == 0.0f);
    assert(localViewport.y == 0.0f);
    assert(localViewport.width == 640.0f);
    assert(localViewport.height == 360.0f);
    assert(localViewport.minDepth == 0.2f);
    assert(localViewport.maxDepth == 0.8f);

    RHI::ScissorRect localScissor = context.GetActiveLocalScissor();
    assert(localScissor.left == 0);
    assert(localScissor.top == 0);
    assert(localScissor.right == 640);
    assert(localScissor.bottom == 360);

    RHI::Viewport outputViewport = context.GetActiveOutputViewport();
    assert(outputViewport.x == 160.0f);
    assert(outputViewport.y == 90.0f);
    assert(outputViewport.width == 640.0f);
    assert(outputViewport.height == 360.0f);

    RHI::ScissorRect outputScissor = context.GetActiveOutputScissor();
    assert(outputScissor.left == 160);
    assert(outputScissor.top == 90);
    assert(outputScissor.right == 800);
    assert(outputScissor.bottom == 450);

    std::cout << "ViewRenderContextActiveInputTest passed\n";
    return 0;
}
