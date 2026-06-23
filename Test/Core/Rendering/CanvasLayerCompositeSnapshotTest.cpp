#include "Rendering/CanvasView.h"
#include "Math/Matrix4x4.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    ViewportRenderPlan MakeViewportPlan()
    {
        ViewportRenderPlan plan;
        plan.bEnabled = true;
        plan.bHasCamera = true;
        plan.RenderWidth = 640;
        plan.RenderHeight = 480;
        plan.PixelRect.Width = 640.0f;
        plan.PixelRect.Height = 480.0f;
        plan.Camera.CullingMask = RenderLayer::UI;
        return plan;
    }

    void InitializeCanvas(CanvasView &canvas)
    {
        ViewSettings settings;
        settings.Type = ViewType::UI;
        settings.Width = 640;
        settings.Height = 480;
        assert(canvas.Initialize(settings));
    }

    BoardProxy MakeBoardProxy(uint64_t objectId,
                              uint64_t componentId,
                              uint32_t layerPriority,
                              uint32_t orderInLayer,
                              TextureHandle texture)
    {
        BoardProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.LayerMask = RenderLayer::UI;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.Texture = texture;
        proxy.BlendModeProp = BlendMode::Translucent;
        proxy.LayerPriority = layerPriority;
        proxy.OrderInLayer = orderInLayer;
        proxy.SortKey = BoardProxy::ComputeSortKey(layerPriority, orderInLayer);
        proxy.bVisible = true;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        return proxy;
    }

    void TestLayerCompositeApiDefaultsAndClamp()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);

        assert(canvas.GetLayerCompositeMode(4u) == CanvasLayerCompositeMode::Inline);
        assert(canvas.GetLayerOpacity(4u) == 1.0f);

        canvas.SetLayerOpacity(4u, -0.5f);
        assert(canvas.GetLayerOpacity(4u) == 0.0f);

        canvas.SetLayerOpacity(4u, 1.5f);
        assert(canvas.GetLayerOpacity(4u) == 1.0f);

        canvas.SetLayerCompositeMode(4u, CanvasLayerCompositeMode::OwnRT);
        assert(canvas.GetLayerCompositeMode(4u) == CanvasLayerCompositeMode::OwnRT);

        canvas.SetLayerCompositeMode(4u, CanvasLayerCompositeMode::Inline);
        assert(canvas.GetLayerCompositeMode(4u) == CanvasLayerCompositeMode::Inline);
        assert(canvas.GetLayerOpacity(4u) == 1.0f);

        canvas.Shutdown();
        std::cout << "TestLayerCompositeApiDefaultsAndClamp passed\n";
    }

    void TestAllInlineKeepsExistingBatchingAndSnapshotsPacketRange()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle texture{42u};
        canvas.UpdateBoardProxy(10u, MakeBoardProxy(100u, 10u, 0u, 0u, texture));
        canvas.UpdateBoardProxy(11u, MakeBoardProxy(101u, 11u, 0u, 1u, texture));

        ViewportRenderPlan viewportPlan = MakeViewportPlan();
        canvas.PrepareBoardDrawCommands(viewportPlan, 17u);

        assert(canvas.GetBoardDrawCommands().size() == 1);
        assert(canvas.GetBoardDrawCommands()[0].Draw.InstanceCount == 2u);
        assert(viewportPlan.CanvasLayerComposites.size() == 1);
        assert(viewportPlan.CanvasLayerComposites[0].LayerPriority == 0u);
        assert(viewportPlan.CanvasLayerComposites[0].Mode == CanvasLayerCompositeMode::Inline);
        assert(viewportPlan.CanvasLayerComposites[0].Opacity == 1.0f);
        assert(viewportPlan.CanvasLayerComposites[0].DrawCommandRange.First == 17u);
        assert(viewportPlan.CanvasLayerComposites[0].DrawCommandRange.Count == 1u);

        viewportPlan.Clear();
        assert(viewportPlan.CanvasLayerComposites.empty());
        assert(viewportPlan.DrawCommandRange.IsEmpty());

        canvas.Shutdown();
        std::cout << "TestAllInlineKeepsExistingBatchingAndSnapshotsPacketRange passed\n";
    }

    void TestAllInlineAcrossLayerPrioritiesKeepsExistingBatching()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle texture{45u};
        canvas.UpdateBoardProxy(12u, MakeBoardProxy(102u, 12u, 0u, 0u, texture));
        canvas.UpdateBoardProxy(13u, MakeBoardProxy(103u, 13u, 1u, 0u, texture));

        ViewportRenderPlan viewportPlan = MakeViewportPlan();
        canvas.PrepareBoardDrawCommands(viewportPlan, 3u);

        assert(canvas.GetBoardDrawCommands().size() == 1);
        assert(canvas.GetBoardDrawCommands()[0].Draw.InstanceCount == 2u);
        assert(viewportPlan.CanvasLayerComposites.size() == 2);
        assert(viewportPlan.CanvasLayerComposites[0].LayerPriority == 0u);
        assert(viewportPlan.CanvasLayerComposites[0].Mode == CanvasLayerCompositeMode::Inline);
        assert(viewportPlan.CanvasLayerComposites[0].DrawCommandRange.First == 3u);
        assert(viewportPlan.CanvasLayerComposites[0].DrawCommandRange.Count == 1u);
        assert(viewportPlan.CanvasLayerComposites[1].LayerPriority == 1u);
        assert(viewportPlan.CanvasLayerComposites[1].Mode == CanvasLayerCompositeMode::Inline);
        assert(viewportPlan.CanvasLayerComposites[1].DrawCommandRange.First == 3u);
        assert(viewportPlan.CanvasLayerComposites[1].DrawCommandRange.Count == 1u);

        canvas.Shutdown();
        std::cout << "TestAllInlineAcrossLayerPrioritiesKeepsExistingBatching passed\n";
    }

    void TestSnapshotImmutabilityAfterPrepare()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle texture{43u};
        canvas.SetLayerCompositeMode(2u, CanvasLayerCompositeMode::OwnRT);
        canvas.SetLayerOpacity(2u, 0.25f);
        canvas.UpdateBoardProxy(20u, MakeBoardProxy(200u, 20u, 2u, 0u, texture));

        ViewportRenderPlan viewportPlan = MakeViewportPlan();
        canvas.PrepareBoardDrawCommands(viewportPlan, 9u);

        canvas.SetLayerCompositeMode(2u, CanvasLayerCompositeMode::Inline);
        canvas.SetLayerOpacity(2u, 1.0f);

        assert(viewportPlan.CanvasLayerComposites.size() == 1);
        assert(viewportPlan.CanvasLayerComposites[0].LayerPriority == 2u);
        assert(viewportPlan.CanvasLayerComposites[0].Mode == CanvasLayerCompositeMode::OwnRT);
        assert(viewportPlan.CanvasLayerComposites[0].Opacity == 0.25f);
        assert(viewportPlan.CanvasLayerComposites[0].DrawCommandRange.First == 9u);
        assert(viewportPlan.CanvasLayerComposites[0].DrawCommandRange.Count == 1u);

        canvas.Shutdown();
        std::cout << "TestSnapshotImmutabilityAfterPrepare passed\n";
    }

    void TestOwnRTSplitsLayerBatching()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle texture{44u};
        canvas.SetLayerCompositeMode(1u, CanvasLayerCompositeMode::OwnRT);
        canvas.UpdateBoardProxy(30u, MakeBoardProxy(300u, 30u, 0u, 0u, texture));
        canvas.UpdateBoardProxy(31u, MakeBoardProxy(301u, 31u, 1u, 0u, texture));

        ViewportRenderPlan viewportPlan = MakeViewportPlan();
        canvas.PrepareBoardDrawCommands(viewportPlan, 0u);

        assert(canvas.GetBoardDrawCommands().size() == 2);
        assert(canvas.GetBoardDrawCommands()[0].Draw.InstanceCount == 1u);
        assert(canvas.GetBoardDrawCommands()[1].Draw.InstanceCount == 1u);
        assert(viewportPlan.CanvasLayerComposites.size() == 2);
        assert(viewportPlan.CanvasLayerComposites[0].LayerPriority == 0u);
        assert(viewportPlan.CanvasLayerComposites[0].Mode == CanvasLayerCompositeMode::Inline);
        assert(viewportPlan.CanvasLayerComposites[0].DrawCommandRange.First == 0u);
        assert(viewportPlan.CanvasLayerComposites[0].DrawCommandRange.Count == 1u);
        assert(viewportPlan.CanvasLayerComposites[1].LayerPriority == 1u);
        assert(viewportPlan.CanvasLayerComposites[1].Mode == CanvasLayerCompositeMode::OwnRT);
        assert(viewportPlan.CanvasLayerComposites[1].DrawCommandRange.First == 1u);
        assert(viewportPlan.CanvasLayerComposites[1].DrawCommandRange.Count == 1u);

        canvas.Shutdown();
        std::cout << "TestOwnRTSplitsLayerBatching passed\n";
    }
} // namespace

int main()
{
    std::cout << "CanvasLayerCompositeSnapshotTest start\n";

    TestLayerCompositeApiDefaultsAndClamp();
    TestAllInlineKeepsExistingBatchingAndSnapshotsPacketRange();
    TestAllInlineAcrossLayerPrioritiesKeepsExistingBatching();
    TestSnapshotImmutabilityAfterPrepare();
    TestOwnRTSplitsLayerBatching();

    std::cout << "CanvasLayerCompositeSnapshotTest passed\n";
    return 0;
}
