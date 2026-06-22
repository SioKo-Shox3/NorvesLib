#include "Component/BoardComponent.h"
#include "Object/World.h"
#include "Rendering/CanvasView.h"
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

    void AssertVector4Near(const Math::Vector4 &actual, const Math::Vector4 &expected)
    {
        AssertNear(actual.x, expected.x);
        AssertNear(actual.y, expected.y);
        AssertNear(actual.z, expected.z);
        AssertNear(actual.w, expected.w);
    }

    Math::Vector2 EvaluateBoardSampleUV(const BoardProxy &proxy, uint32_t vertexIndex)
    {
        const Math::Vector2 uv = QuadVertices[vertexIndex];
        return Math::Vector2(proxy.UVRect.x + uv.x * proxy.UVRect.z,
                             proxy.UVRect.y + uv.y * proxy.UVRect.w);
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

    void TestDefaultUVRectIsFullTexture()
    {
        BoardComponent board;
        assert(board.GetUVRect() == BoardComponent::GetFullTextureUVRect());
        assert(board.GetUVRect() == Math::Vector4(0.0f, 0.0f, 1.0f, 1.0f));
        std::cout << "TestDefaultUVRectIsFullTexture passed\n";
    }

    void TestSetUVRectMarksRenderStateDirty()
    {
        BoardComponent board;
        board.ClearRenderStateDirty();
        board.SetUVRect(Math::Vector4(0.25f, 0.5f, 0.125f, 0.25f));
        assert(board.IsRenderStateDirty());
        assert(board.GetUVRect() == Math::Vector4(0.25f, 0.5f, 0.125f, 0.25f));
        std::cout << "TestSetUVRectMarksRenderStateDirty passed\n";
    }

    void TestBuildBoardProxyCopiesUVRect()
    {
        World world;
        world.Initialize();

        Entity *entity = world.SpawnObject<Entity>();
        assert(entity);

        BoardComponent *board = world.CreateComponent<BoardComponent>(entity);
        assert(board);
        board->SetUVRect(Math::Vector4(0.125f, 0.25f, 0.5f, 0.75f));

        world.UpdateWorldTransforms();
        board->RefreshRenderTransformCache();

        BoardProxy proxy;
        assert(board->BuildBoardProxy(proxy));
        assert(proxy.UVRect == Math::Vector4(0.125f, 0.25f, 0.5f, 0.75f));

        world.Finalize();
        std::cout << "TestBuildBoardProxyCopiesUVRect passed\n";
    }

    void TestPrepareBoardDrawCommandsPacksUVRectIntoInstanceData()
    {
        CanvasView canvas;
        ViewSettings settings;
        settings.Type = ViewType::UI;
        settings.Width = 640;
        settings.Height = 480;
        assert(canvas.Initialize(settings));

        BoardProxy proxy;
        proxy.ObjectId = 77;
        proxy.ComponentId = 88;
        proxy.LayerMask = RenderLayer::UI;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.Texture = TextureHandle{1234};
        proxy.UVRect = Math::Vector4(0.25f, 0.5f, 0.125f, 0.25f);
        proxy.SortKey = BoardProxy::ComputeSortKey(0u, 0u);

        canvas.UpdateBoardProxy(proxy.ComponentId, proxy);
        ViewportRenderPlan viewportPlan = MakeViewportPlan();
        canvas.PrepareBoardDrawCommands(viewportPlan, 0u);

        assert(canvas.GetBoardDrawCommands().size() == 1);
        assert(canvas.GetBoardInstanceData().size() == 1);

        const DrawCommand &command = canvas.GetBoardDrawCommands()[0];
        const GPUSceneInstanceData &instanceData = canvas.GetBoardInstanceData()[0];
        assert(command.Draw.Texture == proxy.Texture);
        AssertNear(instanceData.NormalMatrix[6], 0.25f);
        AssertNear(instanceData.NormalMatrix[7], 0.5f);
        AssertNear(instanceData.NormalMatrix[8], 0.125f);
        AssertNear(instanceData.NormalMatrix[9], 0.25f);
        AssertNear(instanceData.NormalMatrix[10], 0.0f);
        AssertNear(instanceData.NormalMatrix[11], 0.0f);

        canvas.Shutdown();
        std::cout << "TestPrepareBoardDrawCommandsPacksUVRectIntoInstanceData passed\n";
    }

    void TestComputeSpriteSheetUVRectsReturnsRowMajorRects()
    {
        const auto rects = BoardComponent::ComputeSpriteSheetUVRects(128, 64, 32, 32);
        assert(rects.size() == 8);

        const Math::Vector4 expected[] =
        {
            Math::Vector4(0.0f, 0.0f, 0.25f, 0.5f),
            Math::Vector4(0.25f, 0.0f, 0.25f, 0.5f),
            Math::Vector4(0.5f, 0.0f, 0.25f, 0.5f),
            Math::Vector4(0.75f, 0.0f, 0.25f, 0.5f),
            Math::Vector4(0.0f, 0.5f, 0.25f, 0.5f),
            Math::Vector4(0.25f, 0.5f, 0.25f, 0.5f),
            Math::Vector4(0.5f, 0.5f, 0.25f, 0.5f),
            Math::Vector4(0.75f, 0.5f, 0.25f, 0.5f)
        };

        for (size_t index = 0; index < rects.size(); ++index)
        {
            AssertVector4Near(rects[index], expected[index]);
        }

        std::cout << "TestComputeSpriteSheetUVRectsReturnsRowMajorRects passed\n";
    }

    void TestComputeSpriteSheetUVRectsRejectsInvalidDimensions()
    {
        assert(BoardComponent::ComputeSpriteSheetUVRects(0, 64, 32, 32).empty());
        assert(BoardComponent::ComputeSpriteSheetUVRects(128, 0, 32, 32).empty());
        assert(BoardComponent::ComputeSpriteSheetUVRects(128, 64, 0, 32).empty());
        assert(BoardComponent::ComputeSpriteSheetUVRects(128, 64, 32, 0).empty());
        assert(BoardComponent::ComputeSpriteSheetUVRects(16, 64, 32, 32).empty());
        assert(BoardComponent::ComputeSpriteSheetUVRects(128, 16, 32, 32).empty());
        std::cout << "TestComputeSpriteSheetUVRectsRejectsInvalidDimensions passed\n";
    }

    void TestAsymmetricFlipUsesIndependentSampleUV()
    {
        BoardProxy proxy;
        proxy.UVRect = Math::Vector4(0.5f, 0.0f, 0.25f, 0.5f);
        proxy.bFlipX = true;

        const Math::Vector2 topLeftSample = EvaluateBoardSampleUV(proxy, 0);
        const Math::Vector2 topRightSample = EvaluateBoardSampleUV(proxy, 1);
        AssertNear(topLeftSample.x, 0.5f);
        AssertNear(topLeftSample.y, 0.0f);
        AssertNear(topRightSample.x, 0.75f);
        AssertNear(topRightSample.y, 0.0f);

        std::cout << "TestAsymmetricFlipUsesIndependentSampleUV passed\n";
    }
} // namespace

int main()
{
    std::cout << "SpriteSheetUVRectTest start\n";

    TestDefaultUVRectIsFullTexture();
    TestSetUVRectMarksRenderStateDirty();
    TestBuildBoardProxyCopiesUVRect();
    TestPrepareBoardDrawCommandsPacksUVRectIntoInstanceData();
    TestComputeSpriteSheetUVRectsReturnsRowMajorRects();
    TestComputeSpriteSheetUVRectsRejectsInvalidDimensions();
    TestAsymmetricFlipUsesIndependentSampleUV();

    std::cout << "SpriteSheetUVRectTest passed\n";
    return 0;
}
