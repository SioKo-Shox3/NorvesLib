#include "Rendering/RenderFrameExecutor.h"
#include "Rendering/FramePacket.h"
#include "Rendering/View.h"
#include "Rendering/ViewRenderContext.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

namespace
{
    ViewportRenderPlan MakeViewportPlan(uint32_t viewId, uint32_t viewportId)
    {
        ViewportRenderPlan plan;
        plan.ViewId = viewId;
        plan.ViewportId = viewportId;
        plan.RenderWidth = 1280;
        plan.RenderHeight = 720;
        plan.PixelRect.Width = 1280.0f;
        plan.PixelRect.Height = 720.0f;
        plan.Scissor.Right = 1280;
        plan.Scissor.Bottom = 720;
        return plan;
    }

    ViewRenderPlan MakeViewPlan(uint32_t viewId, ViewType type)
    {
        ViewRenderPlan plan;
        plan.ViewId = viewId;
        plan.ViewType = static_cast<uint8_t>(type);
        plan.Viewports.push_back(MakeViewportPlan(viewId, viewId + 100));
        return plan;
    }
}

int main()
{
    std::cout << "RenderFrameExecutorPlanTest start\n";

    {
        ViewRenderContext context;
        Container::VariableArray<DrawCommand> frameCommands;
        frameCommands.push_back(DrawCommand::CreateDraw());
        frameCommands.push_back(DrawCommand::CreateDrawIndexed());
        frameCommands.push_back(DrawCommand::CreateDraw());
        context.SnapshotDrawCommandSource = &frameCommands;

        ViewportRenderPlan viewport = MakeViewportPlan(2, 3);
        viewport.bHasCamera = true;
        viewport.Camera.CameraId = 42;
        viewport.OpaqueCommandRange = {1, 1};
        viewport.TransparentCommandRange = {2, 1};
        viewport.DrawCommandRange = {1, 2};

        RenderFrameExecutor::ApplyViewportRenderPlan(context, &viewport);
        assert(context.CurrentViewport == &viewport);
        assert(context.CurrentCamera == &viewport.Camera);
        assert(context.CurrentDrawCommands.Data == frameCommands.data() + 1);
        assert(context.CurrentDrawCommands.Count == 2);
        assert(context.CurrentOpaqueCommands.Data == frameCommands.data() + 1);
        assert(context.CurrentOpaqueCommands.Count == 1);
        assert(context.CurrentTransparentCommands.Data == frameCommands.data() + 2);
        assert(context.CurrentTransparentCommands.Count == 1);

        RenderFrameExecutor::ApplyViewportRenderPlan(context, nullptr);
        assert(context.CurrentViewport == nullptr);
        assert(context.CurrentCamera == nullptr);
        assert(context.CurrentDrawCommands.empty());
        assert(context.CurrentOpaqueCommands.empty());
        assert(context.CurrentTransparentCommands.empty());
    }

    {
        FramePacket packet;
        packet.Views.push_back(MakeViewPlan(0, ViewType::Debug));
        packet.Views.push_back(MakeViewPlan(1, ViewType::Scene));

        const ViewportRenderPlan *primary = RenderFrameExecutor::FindPrimaryViewportRenderPlan(packet);
        assert(primary != nullptr);
        assert(primary->ViewId == 1);
    }

    {
        FramePacket packet;
        packet.Views.push_back(MakeViewPlan(0, ViewType::Debug));
        packet.Views.push_back(MakeViewPlan(1, ViewType::Custom));

        const ViewportRenderPlan *primary = RenderFrameExecutor::FindPrimaryViewportRenderPlan(packet);
        assert(primary != nullptr);
        assert(primary->ViewId == 0);
    }

    std::cout << "RenderFrameExecutorPlanTest passed\n";
    return 0;
}
