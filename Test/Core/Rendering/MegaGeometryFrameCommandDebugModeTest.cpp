#include "Rendering/FrameCommand.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/ViewRenderPlan.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

int main()
{
    std::cout << "MegaGeometryFrameCommandDebugModeTest start\n";

    CameraProxy camera;
    NorvesLib::RHI::Viewport viewport;
    viewport.width = 128.0f;
    viewport.height = 64.0f;

    NorvesLib::RHI::ScissorRect scissor;
    scissor.right = 128;
    scissor.bottom = 64;

    FrameCommand command = FrameCommand::CreateMegaGeometryPass(nullptr,
                                                                nullptr,
                                                                camera,
                                                                true,
                                                                viewport,
                                                                scissor,
                                                                DebugViewMode::Wireframe);
    assert(command.Type == FrameCommandType::MegaGeometryPass);
    assert(command.MegaGeometry.DebugMode == DebugViewMode::Wireframe);

    ViewportRenderPlan viewportPlan;
    viewportPlan.DebugMode = DebugViewMode::Wireframe;
    viewportPlan.PixelRect.Width = 320.0f;
    viewportPlan.PixelRect.Height = 180.0f;
    viewportPlan.Scissor.Right = 320;
    viewportPlan.Scissor.Bottom = 180;

    NorvesLib::Core::Container::VariableArray<FrameCommand> pendingCommands;
    ViewRenderContext context;
    context.CurrentViewport = &viewportPlan;
    context.PendingFrameCommands = &pendingCommands;

    context.EnqueueMegaGeometryPass(nullptr);

    assert(pendingCommands.size() == 1);
    assert(pendingCommands[0].Type == FrameCommandType::MegaGeometryPass);
    assert(pendingCommands[0].MegaGeometry.DebugMode == DebugViewMode::Wireframe);

    std::cout << "MegaGeometryFrameCommandDebugModeTest passed\n";
    return 0;
}
