#include "Rendering/CanvasView.h"
#include "Math/Matrix4x4.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    ViewportRenderPlan MakeViewportPlan(RenderLayer cullingMask)
    {
        ViewportRenderPlan plan;
        plan.bEnabled = true;
        plan.bHasCamera = true;
        plan.RenderWidth = 800;
        plan.RenderHeight = 600;
        plan.PixelRect.Width = 800.0f;
        plan.PixelRect.Height = 600.0f;
        plan.Camera.CullingMask = cullingMask;
        return plan;
    }

    BoardProxy MakeBoardProxy(uint64_t objectId,
                              uint64_t componentId,
                              RenderLayer layerMask,
                              uint32_t layerPriority,
                              uint32_t orderInLayer)
    {
        BoardProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.LayerMask = layerMask;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.LayerPriority = layerPriority;
        proxy.OrderInLayer = orderInLayer;
        proxy.SortKey = BoardProxy::ComputeSortKey(layerPriority, orderInLayer);
        proxy.bVisible = true;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        return proxy;
    }

    void AssertCommand(const DrawCommand &command,
                       uint64_t expectedObjectId,
                       uint64_t expectedSortKey,
                       uint32_t expectedInstanceIndex)
    {
        assert(command.Type == DrawCommandType::DrawInstanced);
        assert(command.Draw.ObjectId == expectedObjectId);
        assert(command.SortKey == expectedSortKey);
        assert(command.Draw.FirstInstance == expectedInstanceIndex);
        assert(command.Draw.InstanceDataOffset == expectedInstanceIndex);
    }

    void TestMultiLayerBoardsSortAscendingAndCullBeforeDraw()
    {
        CanvasView canvas;
        ViewSettings settings;
        settings.Type = ViewType::UI;
        settings.Width = 800;
        settings.Height = 600;
        assert(canvas.Initialize(settings));
        canvas.SetBoardInstanceBatchingEnabled(false);

        const BoardProxy layer2Order0 = MakeBoardProxy(501u, 51u, RenderLayer::UI, 2u, 0u);
        const BoardProxy filteredDefaultLayer = MakeBoardProxy(502u, 52u, RenderLayer::Default, 0u, 0u);
        const BoardProxy layer1Order3 = MakeBoardProxy(503u, 53u, RenderLayer::UI, 1u, 3u);
        const BoardProxy layer0Order2 = MakeBoardProxy(504u, 54u, RenderLayer::UI, 0u, 2u);
        const BoardProxy layer0Order1 = MakeBoardProxy(505u, 55u, RenderLayer::UI, 0u, 1u);

        canvas.UpdateBoardProxy(layer2Order0.ComponentId, layer2Order0);
        canvas.UpdateBoardProxy(filteredDefaultLayer.ComponentId, filteredDefaultLayer);
        canvas.UpdateBoardProxy(layer1Order3.ComponentId, layer1Order3);
        canvas.UpdateBoardProxy(layer0Order2.ComponentId, layer0Order2);
        canvas.UpdateBoardProxy(layer0Order1.ComponentId, layer0Order1);

        assert(canvas.GetBoardProxies().size() == 5);

        canvas.PrepareBoardDrawCommands(MakeViewportPlan(RenderLayer::UI));

        assert(canvas.GetBoardDrawCommands().size() == 4);
        assert(canvas.GetBoardInstanceData().size() == 4);

        AssertCommand(canvas.GetBoardDrawCommands()[0],
                      layer0Order1.ObjectId,
                      BoardProxy::ComputeSortKey(0u, 1u),
                      0u);
        AssertCommand(canvas.GetBoardDrawCommands()[1],
                      layer0Order2.ObjectId,
                      BoardProxy::ComputeSortKey(0u, 2u),
                      1u);
        AssertCommand(canvas.GetBoardDrawCommands()[2],
                      layer1Order3.ObjectId,
                      BoardProxy::ComputeSortKey(1u, 3u),
                      2u);
        AssertCommand(canvas.GetBoardDrawCommands()[3],
                      layer2Order0.ObjectId,
                      BoardProxy::ComputeSortKey(2u, 0u),
                      3u);

        for (const DrawCommand &command : canvas.GetBoardDrawCommands())
        {
            assert(command.Draw.ObjectId != filteredDefaultLayer.ObjectId);
        }

        canvas.Shutdown();
        std::cout << "TestMultiLayerBoardsSortAscendingAndCullBeforeDraw passed\n";
    }
} // namespace

int main()
{
    std::cout << "MultiLayerOrderTest start\n";

    TestMultiLayerBoardsSortAscendingAndCullBeforeDraw();

    std::cout << "MultiLayerOrderTest passed\n";
    return 0;
}
