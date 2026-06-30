#include "Rendering/CanvasView.h"
#include "Math/Matrix4x4.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;

namespace
{
    struct CommandSnapshot
    {
        uint64_t ObjectId = 0;
        uint64_t SortKey = 0;
        uint32_t FirstInstance = 0;
        uint32_t InstanceDataOffset = 0;
    };

    ViewportRenderPlan MakeViewportPlan(RenderLayer cullingMask)
    {
        ViewportRenderPlan plan;
        plan.bEnabled = true;
        plan.bHasCamera = true;
        plan.RenderWidth = 640;
        plan.RenderHeight = 480;
        plan.PixelRect.Width = 640.0f;
        plan.PixelRect.Height = 480.0f;
        plan.Camera.CullingMask = cullingMask;
        return plan;
    }

    BoardProxy MakeBoardProxy(uint64_t objectId,
                              uint64_t componentId,
                              uint32_t layerPriority,
                              uint32_t orderInLayer,
                              float x)
    {
        BoardProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.LayerMask = RenderLayer::UI;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.LayerPriority = layerPriority;
        proxy.OrderInLayer = orderInLayer;
        proxy.SortKey = BoardProxy::ComputeSortKey(layerPriority, orderInLayer);
        proxy.bVisible = true;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.WorldTransform.SetTranslationRow(Math::Vector3(x, 0.0f, 0.0f));
        return proxy;
    }

    Container::VariableArray<CommandSnapshot> CaptureCommandSnapshot(const CanvasView &canvas)
    {
        Container::VariableArray<CommandSnapshot> snapshot;
        snapshot.reserve(canvas.GetBoardDrawCommands().size());

        for (const DrawCommand &command : canvas.GetBoardDrawCommands())
        {
            CommandSnapshot entry;
            entry.ObjectId = command.Draw.ObjectId;
            entry.SortKey = command.SortKey;
            entry.FirstInstance = command.Draw.FirstInstance;
            entry.InstanceDataOffset = command.Draw.InstanceDataOffset;
            snapshot.push_back(entry);
        }

        return snapshot;
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

    void TestBoardOrderingStaysStableAcrossSwapErase()
    {
        assert(BoardProxy::ComputeSortKey(5u, 9u) == ((5ull << 32u) | 9ull));

        CanvasView canvas;
        ViewSettings settings;
        settings.Type = ViewType::UI;
        settings.Width = 640;
        settings.Height = 480;
        assert(canvas.Initialize(settings));
        canvas.SetBoardInstanceBatchingEnabled(false);

        const uint32_t layerPriority = 2u;
        const uint32_t orderInLayer = 4u;
        const uint64_t sortKey = BoardProxy::ComputeSortKey(layerPriority, orderInLayer);

        const BoardProxy boardA = MakeBoardProxy(101u, 11u, layerPriority, orderInLayer, 10.0f);
        const BoardProxy boardB = MakeBoardProxy(102u, 12u, layerPriority, orderInLayer, 20.0f);
        const BoardProxy boardC = MakeBoardProxy(103u, 13u, layerPriority, orderInLayer, 30.0f);
        const BoardProxy boardD = MakeBoardProxy(104u, 14u, layerPriority, orderInLayer, 40.0f);

        canvas.UpdateBoardProxy(boardA.ComponentId, boardA);
        canvas.UpdateBoardProxy(boardB.ComponentId, boardB);
        canvas.UpdateBoardProxy(boardC.ComponentId, boardC);
        canvas.RemoveBoardProxy(boardA.ComponentId);
        canvas.UpdateBoardProxy(boardD.ComponentId, boardD);

        assert(canvas.GetBoardProxies().size() == 3);
        assert(canvas.GetBoardProxies()[0].ComponentId == boardC.ComponentId);
        assert(canvas.GetBoardProxies()[1].ComponentId == boardB.ComponentId);
        assert(canvas.GetBoardProxies()[2].ComponentId == boardD.ComponentId);

        ViewportRenderPlan viewportPlan = MakeViewportPlan(RenderLayer::UI);
        canvas.PrepareBoardDrawCommands(viewportPlan, 0u);

        assert(canvas.GetBoardDrawCommands().size() == 3);
        assert(canvas.GetBoardInstanceData().size() == 3);
        AssertCommand(canvas.GetBoardDrawCommands()[0], boardB.ObjectId, sortKey, 0u);
        AssertCommand(canvas.GetBoardDrawCommands()[1], boardC.ObjectId, sortKey, 1u);
        AssertCommand(canvas.GetBoardDrawCommands()[2], boardD.ObjectId, sortKey, 2u);

        const Container::VariableArray<CommandSnapshot> firstSnapshot = CaptureCommandSnapshot(canvas);

        canvas.PrepareBoardDrawCommands(viewportPlan, 0u);

        assert(canvas.GetBoardDrawCommands().size() == firstSnapshot.size());
        assert(canvas.GetBoardInstanceData().size() == firstSnapshot.size());

        const Container::VariableArray<CommandSnapshot> secondSnapshot = CaptureCommandSnapshot(canvas);
        assert(secondSnapshot.size() == firstSnapshot.size());
        for (uint32_t index = 0; index < secondSnapshot.size(); ++index)
        {
            assert(secondSnapshot[index].ObjectId == firstSnapshot[index].ObjectId);
            assert(secondSnapshot[index].SortKey == firstSnapshot[index].SortKey);
            assert(secondSnapshot[index].FirstInstance == firstSnapshot[index].FirstInstance);
            assert(secondSnapshot[index].InstanceDataOffset == firstSnapshot[index].InstanceDataOffset);
        }

        canvas.Shutdown();
        std::cout << "TestBoardOrderingStaysStableAcrossSwapErase passed\n";
    }
} // namespace

int main()
{
    std::cout << "BoardSortKeyStableTest start\n";

    TestBoardOrderingStaysStableAcrossSwapErase();

    std::cout << "BoardSortKeyStableTest passed\n";
    return 0;
}
