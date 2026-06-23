#include "Component/BoardComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/SceneProxy.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Component;
namespace Math = NorvesLib::Math;
namespace Rendering = NorvesLib::Core::Rendering;

namespace
{
    constexpr float FloatTolerance = 0.0001f;

    class TestBoardComponent : public BoardComponent
    {
    public:
        void SetInitialFrameForTest(uint32_t initialFrame)
        {
            InitialFrame = initialFrame;
        }
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

    Math::Vector4 RectAt(uint32_t index)
    {
        const auto rects = BoardComponent::ComputeSpriteSheetUVRects(128, 64, 32, 32);
        assert(index < rects.size());
        return rects[index];
    }

    void ConfigureFourFrameBoard(BoardComponent &board, float framesPerSecond = 2.0f)
    {
        board.SetFrameCount(4);
        assert(board.SetFlipbookGrid(128, 64, 32, 32, 0u));
        board.SetFramesPerSecond(framesPerSecond);
        board.SetLoop(true);
    }

    void TestDefaultBoardRemainsFullTexture()
    {
        BoardComponent board;
        assert(board.GetFrameCount() == 1u);
        assert(board.GetFramesPerSecond() == 0.0f);
        assert(board.IsLooping());
        assert(!board.IsPlaying());
        assert(board.GetUVRect() == BoardComponent::GetFullTextureUVRect());

        board.ClearRenderStateDirty();
        board.Tick(1.0f);
        assert(board.GetUVRect() == BoardComponent::GetFullTextureUVRect());
        assert(!board.IsRenderStateDirty());

        std::cout << "TestDefaultBoardRemainsFullTexture passed\n";
    }

    void TestValidGridMapsFramesWithFirstFrameOffset()
    {
        BoardComponent board;
        board.SetFrameCount(3);
        assert(board.SetFlipbookGrid(128, 64, 32, 32, 4u));
        AssertVector4Near(board.GetUVRect(), RectAt(4));

        board.SetFrame(2);
        assert(board.GetCurrentFrame() == 2u);
        AssertVector4Near(board.GetUVRect(), RectAt(6));

        std::cout << "TestValidGridMapsFramesWithFirstFrameOffset passed\n";
    }

    void TestInvalidGridPreservesManualUV()
    {
        BoardComponent board;
        const Math::Vector4 manualUV(0.2f, 0.3f, 0.4f, 0.5f);
        board.SetUVRect(manualUV);
        board.SetFrameCount(4);
        assert(!board.SetFlipbookGrid(64, 64, 32, 32, 2u));
        assert(board.GetUVRect() == manualUV);

        std::cout << "TestInvalidGridPreservesManualUV passed\n";
    }

    void TestInvalidGridAfterValidGridDoesNotRestoreStaleUV()
    {
        NorvesLib::Core::World world;
        world.Initialize();

        NorvesLib::Core::Entity* entity = world.SpawnObject<NorvesLib::Core::Entity>();
        assert(entity != nullptr);
        BoardComponent* board = world.CreateComponent<BoardComponent>(entity);
        assert(board != nullptr);

        ConfigureFourFrameBoard(*board);
        board->Play();

        const Math::Vector4 manualUV(0.2f, 0.3f, 0.4f, 0.5f);
        board->SetUVRect(manualUV);
        board->ClearRenderStateDirty();

        assert(!board->SetFlipbookGrid(64, 64, 32, 32, 2u));
        assert(!board->IsPlaying());
        assert(board->GetUVRect() == manualUV);

        board->PrepareFlipbookForRenderSync();
        assert(board->GetUVRect() == manualUV);
        assert(!board->IsRenderStateDirty());

        world.UpdateWorldTransforms();
        board->RefreshRenderTransformCache();

        Rendering::BoardProxy proxy;
        assert(board->BuildBoardProxy(proxy));
        assert(proxy.UVRect == manualUV);

        board->Tick(1.0f);
        board->PrepareFlipbookForRenderSync();
        assert(board->GetUVRect() == manualUV);

        world.Finalize();
        std::cout << "TestInvalidGridAfterValidGridDoesNotRestoreStaleUV passed\n";
    }

    void TestFractionalAccumulatorAdvancesOneFrame()
    {
        BoardComponent board;
        ConfigureFourFrameBoard(board, 2.0f);
        board.Play();

        board.Tick(0.49f);
        assert(board.GetCurrentFrame() == 0u);
        board.Tick(0.01f);
        assert(board.GetCurrentFrame() == 1u);
        AssertVector4Near(board.GetUVRect(), RectAt(1));

        std::cout << "TestFractionalAccumulatorAdvancesOneFrame passed\n";
    }

    void TestLargeDeltaAdvancesMultipleFrames()
    {
        BoardComponent board;
        board.SetFrameCount(6);
        assert(board.SetFlipbookGrid(128, 64, 32, 32, 0u));
        board.SetFramesPerSecond(4.0f);
        board.Play();

        board.Tick(0.75f);
        assert(board.GetCurrentFrame() == 3u);
        AssertVector4Near(board.GetUVRect(), RectAt(3));

        std::cout << "TestLargeDeltaAdvancesMultipleFrames passed\n";
    }

    void TestLoopingWraps()
    {
        BoardComponent board;
        board.SetFrameCount(3);
        assert(board.SetFlipbookGrid(128, 64, 32, 32, 0u));
        board.SetFramesPerSecond(1.0f);
        board.SetFrame(2);
        board.Play();

        board.Tick(1.0f);
        assert(board.GetCurrentFrame() == 0u);
        assert(board.IsPlaying());
        AssertVector4Near(board.GetUVRect(), RectAt(0));

        std::cout << "TestLoopingWraps passed\n";
    }

    void TestNonLoopingStopsAtFinalFrame()
    {
        BoardComponent board;
        board.SetFrameCount(3);
        assert(board.SetFlipbookGrid(128, 64, 32, 32, 0u));
        board.SetFramesPerSecond(10.0f);
        board.SetLoop(false);
        board.Play();

        board.Tick(1.0f);
        assert(board.GetCurrentFrame() == 2u);
        assert(!board.IsPlaying());
        AssertVector4Near(board.GetUVRect(), RectAt(2));

        board.Play();
        assert(board.IsPlaying());
        assert(board.GetCurrentFrame() == 0u);
        AssertVector4Near(board.GetUVRect(), RectAt(0));

        std::cout << "TestNonLoopingStopsAtFinalFrame passed\n";
    }

    void TestNegativeAndNonFiniteDeltasDoNothing()
    {
        BoardComponent board;
        ConfigureFourFrameBoard(board, 2.0f);
        board.Play();

        board.Tick(-1.0f);
        assert(board.GetCurrentFrame() == 0u);
        board.Tick(std::numeric_limits<float>::infinity());
        assert(board.GetCurrentFrame() == 0u);
        board.Tick(std::numeric_limits<float>::quiet_NaN());
        assert(board.GetCurrentFrame() == 0u);
        AssertVector4Near(board.GetUVRect(), RectAt(0));

        std::cout << "TestNegativeAndNonFiniteDeltasDoNothing passed\n";
    }

    void TestPauseStopAndSetFrameControls()
    {
        TestBoardComponent board;
        ConfigureFourFrameBoard(board, 2.0f);
        board.Play();
        board.Tick(0.5f);
        assert(board.GetCurrentFrame() == 1u);

        board.Pause();
        board.Tick(2.0f);
        assert(board.GetCurrentFrame() == 1u);

        board.SetFrame(10u);
        assert(board.GetCurrentFrame() == 3u);
        AssertVector4Near(board.GetUVRect(), RectAt(3));

        board.SetFrame(1u);
        board.Play();
        board.Tick(0.24f);
        assert(board.GetCurrentFrame() == 1u);
        board.Tick(0.26f);
        assert(board.GetCurrentFrame() == 2u);

        board.SetInitialFrameForTest(2u);
        board.Stop();
        assert(!board.IsPlaying());
        assert(board.GetCurrentFrame() == 2u);
        AssertVector4Near(board.GetUVRect(), RectAt(2));

        std::cout << "TestPauseStopAndSetFrameControls passed\n";
    }

    void TestDirtyStateChangesOnlyOnEffectiveUVChange()
    {
        BoardComponent board;
        ConfigureFourFrameBoard(board, 2.0f);
        board.ClearRenderStateDirty();
        board.Play();
        assert(!board.IsRenderStateDirty());

        board.Tick(0.1f);
        assert(!board.IsRenderStateDirty());

        board.Tick(0.4f);
        assert(board.IsRenderStateDirty());
        assert(board.GetCurrentFrame() == 1u);

        board.ClearRenderStateDirty();
        board.Tick(0.0f);
        board.Tick(-0.1f);
        assert(!board.IsRenderStateDirty());

        board.SetFrame(1u);
        assert(!board.IsRenderStateDirty());
        board.SetFrame(2u);
        assert(board.IsRenderStateDirty());

        std::cout << "TestDirtyStateChangesOnlyOnEffectiveUVChange passed\n";
    }
} // namespace

int main()
{
    std::cout << "FlipbookAdvanceTest start\n";

    TestDefaultBoardRemainsFullTexture();
    TestValidGridMapsFramesWithFirstFrameOffset();
    TestInvalidGridPreservesManualUV();
    TestInvalidGridAfterValidGridDoesNotRestoreStaleUV();
    TestFractionalAccumulatorAdvancesOneFrame();
    TestLargeDeltaAdvancesMultipleFrames();
    TestLoopingWraps();
    TestNonLoopingStopsAtFinalFrame();
    TestNegativeAndNonFiniteDeltasDoNothing();
    TestPauseStopAndSetFrameControls();
    TestDirtyStateChangesOnlyOnEffectiveUVChange();

    std::cout << "FlipbookAdvanceTest passed\n";
    return 0;
}
