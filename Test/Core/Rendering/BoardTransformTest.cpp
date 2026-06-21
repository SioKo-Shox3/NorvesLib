#include "Component/BoardComponent.h"
#include "Object/World.h"
#include "Rendering/CanvasView.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    constexpr float FloatTolerance = 0.0001f;
    const Math::Vector2 QuadVertices[6] =
    {
        Math::Vector2(0.0f, 0.0f),
        Math::Vector2(1.0f, 0.0f),
        Math::Vector2(1.0f, 1.0f),
        Math::Vector2(0.0f, 0.0f),
        Math::Vector2(1.0f, 1.0f),
        Math::Vector2(0.0f, 1.0f)
    };

    void AssertNear(float actual, float expected)
    {
        assert(std::fabs(actual - expected) <= FloatTolerance);
    }

    void AssertVectorNear(const Math::Vector2 &actual, const Math::Vector2 &expected)
    {
        AssertNear(actual.x, expected.x);
        AssertNear(actual.y, expected.y);
    }

    Math::Vector2 NormalizeOrZero(const Math::Vector2 &vector)
    {
        const float length = std::sqrt(vector.x * vector.x + vector.y * vector.y);
        if (length <= FloatTolerance)
        {
            return Math::Vector2::Zero;
        }

        return Math::Vector2(vector.x / length, vector.y / length);
    }

    Math::Vector2 EvaluateBoardVertexPosition(const BoardProxy &proxy, uint32_t vertexIndex)
    {
        Math::Vector2 uv = QuadVertices[vertexIndex];
        if (proxy.bFlipX)
        {
            uv.x = 1.0f - uv.x;
        }

        if (proxy.bFlipY)
        {
            uv.y = 1.0f - uv.y;
        }

        const Math::Vector2 local = uv - proxy.Pivot;
        Math::Vector2 axisX(proxy.WorldTransform.m00, proxy.WorldTransform.m01);
        Math::Vector2 axisY(proxy.WorldTransform.m10, proxy.WorldTransform.m11);
        if (proxy.SizePx.x > 0.0f && proxy.SizePx.y > 0.0f)
        {
            axisX = NormalizeOrZero(axisX) * proxy.SizePx.x;
            axisY = NormalizeOrZero(axisY) * proxy.SizePx.y;
        }

        const Math::Vector2 origin(proxy.WorldTransform.m30, proxy.WorldTransform.m31);
        return origin + axisX * local.x + axisY * local.y;
    }

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

    void TestDefaultProxyValuesPreserveF4Behavior()
    {
        World world;
        world.Initialize();

        Entity *entity = world.SpawnObject<Entity>();
        assert(entity);
        entity->SetLocalPosition(12.0f, 34.0f, 0.0f);
        entity->SetLocalScale(80.0f, 24.0f, 1.0f);

        BoardComponent *board = world.CreateComponent<BoardComponent>(entity);
        assert(board);

        world.UpdateWorldTransforms();
        board->RefreshRenderTransformCache();

        BoardProxy proxy;
        assert(board->BuildBoardProxy(proxy));
        assert(proxy.LayerMask == RenderLayer::UI);
        assert(proxy.Space == BoardSpace::ScreenSpace);
        assert(proxy.BlendModeProp == BlendMode::Translucent);
        assert(proxy.Tint == Math::Vector4(1.0f, 1.0f, 1.0f, 0.75f));
        assert(!proxy.bFlipX);
        assert(!proxy.bFlipY);
        assert(proxy.Pivot == Math::Vector2(0.0f, 0.0f));
        assert(proxy.SizePx == Math::Vector2(0.0f, 0.0f));
        assert(proxy.UVRect == Math::Vector4(0.0f, 0.0f, 1.0f, 1.0f));
        assert(proxy.SortKey == BoardProxy::ComputeSortKey(0u, 0u));
        AssertNear(proxy.WorldTransform.m30, 12.0f);
        AssertNear(proxy.WorldTransform.m31, 34.0f);
        AssertNear(proxy.WorldTransform.m00, 80.0f);
        AssertNear(proxy.WorldTransform.m11, 24.0f);

        world.Finalize();
        std::cout << "TestDefaultProxyValuesPreserveF4Behavior passed\n";
    }

    void TestSettersMarkRenderStateDirty()
    {
        BoardComponent board;

        board.ClearRenderStateDirty();
        assert(!board.IsRenderStateDirty());
        board.SetBlendMode(BlendMode::Additive);
        assert(board.IsRenderStateDirty());

        board.ClearRenderStateDirty();
        board.SetTint(Math::Vector4(0.2f, 0.4f, 0.6f, 0.8f));
        assert(board.IsRenderStateDirty());

        board.ClearRenderStateDirty();
        board.SetFlipX(true);
        assert(board.IsRenderStateDirty());

        board.ClearRenderStateDirty();
        board.SetFlipY(true);
        assert(board.IsRenderStateDirty());

        board.ClearRenderStateDirty();
        board.SetPivot(Math::Vector2(0.5f, 0.25f));
        assert(board.IsRenderStateDirty());

        board.ClearRenderStateDirty();
        board.SetSizePx(Math::Vector2(128.0f, 96.0f));
        assert(board.IsRenderStateDirty());

        std::cout << "TestSettersMarkRenderStateDirty passed\n";
    }

    void TestBuildBoardProxyReflectsF5Fields()
    {
        World world;
        world.Initialize();

        Entity *entity = world.SpawnObject<Entity>();
        assert(entity);
        entity->SetLocalPosition(20.0f, 30.0f, 0.0f);
        entity->SetLocalScale(40.0f, 50.0f, 1.0f);

        BoardComponent *board = world.CreateComponent<BoardComponent>(entity);
        assert(board);
        board->SetBlendMode(BlendMode::Additive);
        board->SetTint(Math::Vector4(0.25f, 0.5f, 0.75f, 0.6f));
        board->SetFlipX(true);
        board->SetFlipY(true);
        board->SetPivot(Math::Vector2(0.5f, 1.0f));
        board->SetSizePx(Math::Vector2(96.0f, 48.0f));
        board->SetUVRect(Math::Vector4(0.25f, 0.5f, 0.125f, 0.25f));

        world.UpdateWorldTransforms();
        board->RefreshRenderTransformCache();

        BoardProxy proxy;
        assert(board->BuildBoardProxy(proxy));
        assert(proxy.BlendModeProp == BlendMode::Additive);
        assert(proxy.Tint == Math::Vector4(0.25f, 0.5f, 0.75f, 0.6f));
        assert(proxy.bFlipX);
        assert(proxy.bFlipY);
        assert(proxy.Pivot == Math::Vector2(0.5f, 1.0f));
        assert(proxy.SizePx == Math::Vector2(96.0f, 48.0f));
        assert(proxy.UVRect == Math::Vector4(0.25f, 0.5f, 0.125f, 0.25f));

        world.Finalize();
        std::cout << "TestBuildBoardProxyReflectsF5Fields passed\n";
    }

    void TestPrepareBoardDrawCommandsPacksBoardInstanceData()
    {
        CanvasView canvas;
        ViewSettings settings;
        settings.Type = ViewType::UI;
        settings.Width = 640;
        settings.Height = 480;
        assert(canvas.Initialize(settings));

        BoardProxy opaqueBoard;
        opaqueBoard.ObjectId = 500;
        opaqueBoard.ComponentId = 50;
        opaqueBoard.LayerMask = RenderLayer::UI;
        opaqueBoard.Space = BoardSpace::ScreenSpace;
        opaqueBoard.BlendModeProp = BlendMode::Opaque;
        opaqueBoard.Tint = Math::Vector4(0.2f, 0.4f, 0.6f, 0.1f);
        opaqueBoard.bFlipX = true;
        opaqueBoard.bFlipY = false;
        opaqueBoard.Pivot = Math::Vector2(0.5f, 0.25f);
        opaqueBoard.SizePx = Math::Vector2(128.0f, 96.0f);
        opaqueBoard.SortKey = BoardProxy::ComputeSortKey(0u, 0u);

        BoardProxy translucentBoard = opaqueBoard;
        translucentBoard.ObjectId = 501;
        translucentBoard.ComponentId = 51;
        translucentBoard.BlendModeProp = BlendMode::Translucent;
        translucentBoard.Tint = Math::Vector4(0.7f, 0.1f, 0.3f, 0.6f);
        translucentBoard.bFlipX = false;
        translucentBoard.bFlipY = true;
        translucentBoard.Pivot = Math::Vector2(0.0f, 1.0f);
        translucentBoard.SizePx = Math::Vector2(0.0f, 0.0f);
        translucentBoard.SortKey = BoardProxy::ComputeSortKey(0u, 1u);

        canvas.UpdateBoardProxy(opaqueBoard.ComponentId, opaqueBoard);
        canvas.UpdateBoardProxy(translucentBoard.ComponentId, translucentBoard);
        canvas.PrepareBoardDrawCommands(MakeViewportPlan());

        assert(canvas.GetBoardDrawCommands().size() == 2);
        assert(canvas.GetBoardInstanceData().size() == 2);

        const DrawCommand &opaqueCommand = canvas.GetBoardDrawCommands()[0];
        const GPUSceneInstanceData &opaqueInstance = canvas.GetBoardInstanceData()[0];
        assert(opaqueCommand.Draw.MaterialBlendMode == BlendMode::Opaque);
        AssertNear(opaqueInstance.ObjectColor[0], 0.2f);
        AssertNear(opaqueInstance.ObjectColor[1], 0.4f);
        AssertNear(opaqueInstance.ObjectColor[2], 0.6f);
        AssertNear(opaqueInstance.ObjectColor[3], 1.0f);
        AssertNear(opaqueInstance.NormalMatrix[0], 128.0f);
        AssertNear(opaqueInstance.NormalMatrix[1], 96.0f);
        AssertNear(opaqueInstance.NormalMatrix[2], 0.5f);
        AssertNear(opaqueInstance.NormalMatrix[3], 0.25f);
        AssertNear(opaqueInstance.NormalMatrix[4], 1.0f);
        AssertNear(opaqueInstance.NormalMatrix[5], 0.0f);
        AssertNear(opaqueInstance.NormalMatrix[6], 0.0f);
        AssertNear(opaqueInstance.NormalMatrix[7], 0.0f);
        AssertNear(opaqueInstance.NormalMatrix[8], 1.0f);
        AssertNear(opaqueInstance.NormalMatrix[9], 1.0f);
        AssertNear(opaqueInstance.NormalMatrix[10], 0.0f);
        AssertNear(opaqueInstance.NormalMatrix[11], 0.0f);
        AssertNear(opaqueInstance.CustomData[0], 640.0f);
        AssertNear(opaqueInstance.CustomData[1], 480.0f);
        AssertNear(opaqueInstance.CustomData[2], 0.0f);
        AssertNear(opaqueInstance.CustomData[3], 0.0f);

        const DrawCommand &translucentCommand = canvas.GetBoardDrawCommands()[1];
        const GPUSceneInstanceData &translucentInstance = canvas.GetBoardInstanceData()[1];
        assert(translucentCommand.Draw.MaterialBlendMode == BlendMode::Translucent);
        AssertNear(translucentInstance.ObjectColor[0], 0.7f);
        AssertNear(translucentInstance.ObjectColor[1], 0.1f);
        AssertNear(translucentInstance.ObjectColor[2], 0.3f);
        AssertNear(translucentInstance.ObjectColor[3], 0.6f);
        AssertNear(translucentInstance.NormalMatrix[0], 0.0f);
        AssertNear(translucentInstance.NormalMatrix[1], 0.0f);
        AssertNear(translucentInstance.NormalMatrix[2], 0.0f);
        AssertNear(translucentInstance.NormalMatrix[3], 1.0f);
        AssertNear(translucentInstance.NormalMatrix[4], 0.0f);
        AssertNear(translucentInstance.NormalMatrix[5], 1.0f);
        AssertNear(translucentInstance.NormalMatrix[6], 0.0f);
        AssertNear(translucentInstance.NormalMatrix[7], 0.0f);
        AssertNear(translucentInstance.NormalMatrix[8], 1.0f);
        AssertNear(translucentInstance.NormalMatrix[9], 1.0f);
        AssertNear(translucentInstance.NormalMatrix[10], 0.0f);
        AssertNear(translucentInstance.NormalMatrix[11], 0.0f);

        canvas.Shutdown();
        std::cout << "TestPrepareBoardDrawCommandsPacksBoardInstanceData passed\n";
    }

    void TestBoardVertexTransformUsesRectMirroringForFlip()
    {
        BoardProxy proxy;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.SizePx = Math::Vector2(100.0f, 50.0f);
        proxy.Pivot = Math::Vector2(0.0f, 0.0f);
        proxy.bFlipX = true;
        proxy.bFlipY = false;

        float minX = 1000000.0f;
        float minY = 1000000.0f;
        float maxX = -1000000.0f;
        float maxY = -1000000.0f;
        for (uint32_t vertexIndex = 0; vertexIndex < 6; ++vertexIndex)
        {
            const Math::Vector2 position = EvaluateBoardVertexPosition(proxy, vertexIndex);
            minX = std::min(minX, position.x);
            minY = std::min(minY, position.y);
            maxX = std::max(maxX, position.x);
            maxY = std::max(maxY, position.y);
        }

        AssertNear(minX, 0.0f);
        AssertNear(minY, 0.0f);
        AssertNear(maxX, 100.0f);
        AssertNear(maxY, 50.0f);
        AssertVectorNear(EvaluateBoardVertexPosition(proxy, 0), Math::Vector2(100.0f, 0.0f));
        AssertVectorNear(EvaluateBoardVertexPosition(proxy, 1), Math::Vector2(0.0f, 0.0f));
        AssertVectorNear(EvaluateBoardVertexPosition(proxy, 2), Math::Vector2(0.0f, 50.0f));
        AssertVectorNear(EvaluateBoardVertexPosition(proxy, 5), Math::Vector2(100.0f, 50.0f));

        std::cout << "TestBoardVertexTransformUsesRectMirroringForFlip passed\n";
    }

    void TestBoardVertexTransformRespectsPivotWithSizeAndFlips()
    {
        BoardProxy proxy;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.WorldTransform.m30 = 10.0f;
        proxy.WorldTransform.m31 = 20.0f;
        proxy.SizePx = Math::Vector2(80.0f, 40.0f);
        proxy.Pivot = Math::Vector2(0.5f, 1.0f);
        proxy.bFlipX = true;
        proxy.bFlipY = true;

        AssertVectorNear(EvaluateBoardVertexPosition(proxy, 0), Math::Vector2(50.0f, 20.0f));
        AssertVectorNear(EvaluateBoardVertexPosition(proxy, 1), Math::Vector2(-30.0f, 20.0f));
        AssertVectorNear(EvaluateBoardVertexPosition(proxy, 2), Math::Vector2(-30.0f, -20.0f));
        AssertVectorNear(EvaluateBoardVertexPosition(proxy, 5), Math::Vector2(50.0f, -20.0f));

        std::cout << "TestBoardVertexTransformRespectsPivotWithSizeAndFlips passed\n";
    }
} // namespace

int main()
{
    std::cout << "BoardTransformTest start\n";

    TestDefaultProxyValuesPreserveF4Behavior();
    TestSettersMarkRenderStateDirty();
    TestBuildBoardProxyReflectsF5Fields();
    TestPrepareBoardDrawCommandsPacksBoardInstanceData();
    TestBoardVertexTransformUsesRectMirroringForFlip();
    TestBoardVertexTransformRespectsPivotWithSizeAndFlips();

    std::cout << "BoardTransformTest passed\n";
    return 0;
}
