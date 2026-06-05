#include "Rendering/ViewRenderContext.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

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

    std::cout << "ViewRenderContextActiveInputTest passed\n";
    return 0;
}
