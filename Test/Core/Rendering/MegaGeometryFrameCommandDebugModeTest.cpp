#include "Rendering/FrameCommand.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/ViewRenderPlan.h"

#include <cassert>
#include <cstddef>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

// 間接描画コマンドは VkDrawIndexedIndirectCommand と同じ配置でなければならない。
static_assert(sizeof(MegaGeometry::DrawIndexedIndirectCommand) == 20);
static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, IndexCount) == 0);
static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, InstanceCount) == 4);
static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, FirstIndex) == 8);
static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, VertexOffset) == 12);
static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, FirstInstance) == 16);

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

    FrameCommand lodCommand = FrameCommand::CreateMegaGeometryPass(nullptr,
                                                                   nullptr,
                                                                   camera,
                                                                   true,
                                                                   viewport,
                                                                   scissor,
                                                                   DebugViewMode::LODLevel);
    assert(lodCommand.Type == FrameCommandType::MegaGeometryPass);
    assert(lodCommand.MegaGeometry.DebugMode == DebugViewMode::LODLevel);

    ViewportRenderPlan viewportPlan;
    viewportPlan.DebugMode = DebugViewMode::LODLevel;
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
    assert(pendingCommands[0].MegaGeometry.DebugMode == DebugViewMode::LODLevel);

    std::cout << "MegaGeometryFrameCommandDebugModeTest passed\n";
    return 0;
}
