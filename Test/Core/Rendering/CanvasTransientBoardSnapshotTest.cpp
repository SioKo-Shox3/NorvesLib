#include "Rendering/CanvasView.h"
#include "Rendering/FramePacket.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;

namespace
{
    BoardProxy MakeProxy(uint64_t id, uint32_t order)
    {
        BoardProxy proxy;
        proxy.ComponentId = id;
        proxy.ObjectId = id + 100;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.LayerMask = RenderLayer::UI;
        proxy.SortKey = BoardProxy::ComputeSortKey(0, order);
        proxy.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        return proxy;
    }

    ViewportRenderPlan MakePlan(uint32_t width = 640)
    {
        ViewportRenderPlan plan;
        plan.bEnabled = true;
        plan.bHasCamera = true;
        plan.RenderWidth = width;
        plan.RenderHeight = 480;
        plan.PixelRect.Width = static_cast<float>(width);
        plan.PixelRect.Height = 480.0f;
        plan.Camera.CullingMask = RenderLayer::UI;
        return plan;
    }
}

int main()
{
    CanvasView canvas;
    ViewSettings settings;
    settings.Width = 640;
    settings.Height = 480;
    assert(canvas.Initialize(settings));

    canvas.UpdateBoardProxy(300, MakeProxy(300, 1));
    canvas.UpdateBoardProxy(200, MakeProxy(200, 1));
    canvas.UpdateBoardProxy(100, MakeProxy(100, 1));
    canvas.UpdateBoardProxy(400, MakeProxy(400, 2));
    canvas.RemoveBoardProxy(200);

    Container::VariableArray<BoardProxy> transient;
    transient.push_back(MakeProxy(30, 1));
    transient.push_back(MakeProxy(20, 1));
    transient.push_back(MakeProxy(10, 0));
    canvas.SetTransientBoardProxies(transient);

    ViewportRenderPlan plan = MakePlan();
    canvas.SetBoardInstanceBatchingEnabled(false);
    canvas.PrepareBoardDrawCommands(plan, 0);
    assert(canvas.GetBoardProxies().size() == 3);
    assert(canvas.GetTransientBoardProxies().size() == 3);
    assert(canvas.GetBoardDrawCommands().size() == 6);
    assert(canvas.GetBoardDrawCommands()[0].Draw.ObjectId == 110);
    assert(canvas.GetBoardDrawCommands()[1].Draw.ObjectId == 400);
    assert(canvas.GetBoardDrawCommands()[2].Draw.ObjectId == 200);
    assert(canvas.GetBoardDrawCommands()[3].Draw.ObjectId == 130);
    assert(canvas.GetBoardDrawCommands()[4].Draw.ObjectId == 120);
    assert(canvas.GetBoardDrawCommands()[5].Draw.ObjectId == 500);

    FramePacket packet;
    packet.DrawCommands = canvas.GetBoardDrawCommands();
    packet.InstanceData = canvas.GetBoardInstanceData();
    assert(packet.DrawCommands.size() == 6);
    assert(packet.InstanceData.size() == 6);
    assert(packet.DrawCommands[1].Draw.ObjectId == 400);
    assert(packet.InstanceData[0].CustomData[0] == 640.0f);

    transient[0].ObjectId = 999;
    canvas.SetTransientBoardProxies(transient);
    canvas.RemoveBoardProxy(300);
    ViewportRenderPlan changedPlan = MakePlan(320);
    canvas.PrepareBoardDrawCommands(changedPlan, 0);
    assert(canvas.GetBoardDrawCommands().size() == 5);
    assert(canvas.GetBoardDrawCommands()[2].Draw.ObjectId == 999);
    assert(canvas.GetBoardInstanceData()[0].CustomData[0] == 320.0f);
    assert(packet.DrawCommands.size() == 6);
    assert(packet.DrawCommands[1].Draw.ObjectId == 400);
    assert(packet.InstanceData.size() == 6);
    assert(packet.InstanceData[0].CustomData[0] == 640.0f);

    canvas.SetBoardInstanceBatchingEnabled(true);
    plan = MakePlan();
    canvas.PrepareBoardDrawCommands(plan, 0);
    assert(canvas.GetBoardDrawCommands().size() == 1);
    assert(canvas.GetBoardDrawCommands()[0].Draw.InstanceCount == 5);
    assert(canvas.GetBoardDrawCommands()[0].Draw.FirstInstance == 0);

    canvas.SetTransientBoardProxies({});
    plan = MakePlan();
    canvas.PrepareBoardDrawCommands(plan, 0);
    assert(canvas.GetBoardDrawCommands()[0].Draw.InstanceCount == 2);
    assert(packet.DrawCommands[0].Draw.ObjectId == 110);
    assert(packet.InstanceData.size() == 6);
    canvas.Shutdown();
    std::cout << "CanvasTransientBoardSnapshotTest passed\n";
    return 0;
}
