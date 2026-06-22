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
                              uint32_t orderInLayer,
                              TextureHandle texture,
                              BlendMode blendMode,
                              float tintMarker)
    {
        BoardProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.LayerMask = RenderLayer::UI;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.Texture = texture;
        proxy.BlendModeProp = blendMode;
        proxy.LayerPriority = 0;
        proxy.OrderInLayer = orderInLayer;
        proxy.SortKey = BoardProxy::ComputeSortKey(0u, orderInLayer);
        proxy.bVisible = true;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.Tint = Math::Vector4(tintMarker, 1.0f - tintMarker, 0.5f, 0.75f);
        return proxy;
    }

    void AssertRun(const DrawCommand &command,
                   uint64_t expectedObjectId,
                   TextureHandle expectedTexture,
                   BlendMode expectedBlendMode,
                   uint32_t expectedFirstInstance,
                   uint32_t expectedInstanceCount)
    {
        assert(command.Type == DrawCommandType::DrawInstanced);
        assert(command.Draw.PayloadKind == DrawPayloadKind::Board);
        assert(command.Draw.bInstanced);
        assert(command.Draw.VertexOffset == 6u);
        assert(command.Draw.ObjectId == expectedObjectId);
        assert(command.Draw.Texture == expectedTexture);
        assert(command.Draw.MaterialBlendMode == expectedBlendMode);
        assert(command.Draw.FirstInstance == expectedFirstInstance);
        assert(command.Draw.InstanceDataOffset == expectedFirstInstance);
        assert(command.Draw.InstanceCount == expectedInstanceCount);
    }

    void AddBoard(CanvasView &canvas,
                  uint64_t objectId,
                  uint64_t componentId,
                  uint32_t orderInLayer,
                  TextureHandle texture,
                  BlendMode blendMode,
                  float tintMarker)
    {
        const BoardProxy proxy = MakeBoardProxy(objectId,
                                                componentId,
                                                orderInLayer,
                                                texture,
                                                blendMode,
                                                tintMarker);
        canvas.UpdateBoardProxy(proxy.ComponentId, proxy);
    }

    void PrepareCanvas(CanvasView &canvas)
    {
        ViewportRenderPlan viewportPlan = MakeViewportPlan();
        canvas.PrepareBoardDrawCommands(viewportPlan, 0u);
    }

    void TestConsecutiveSameKeyMerges()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle texture{101u};
        AddBoard(canvas, 10u, 100u, 0u, texture, BlendMode::Translucent, 0.1f);
        AddBoard(canvas, 11u, 101u, 1u, texture, BlendMode::Translucent, 0.2f);
        AddBoard(canvas, 12u, 102u, 2u, texture, BlendMode::Translucent, 0.3f);

        PrepareCanvas(canvas);

        assert(canvas.GetBoardDrawCommands().size() == 1);
        assert(canvas.GetBoardInstanceData().size() == 3);
        AssertRun(canvas.GetBoardDrawCommands()[0], 10u, texture, BlendMode::Translucent, 0u, 3u);
        assert(canvas.GetBoardInstanceData()[0].ObjectColor[0] == 0.1f);
        assert(canvas.GetBoardInstanceData()[1].ObjectColor[0] == 0.2f);
        assert(canvas.GetBoardInstanceData()[2].ObjectColor[0] == 0.3f);

        canvas.Shutdown();
        std::cout << "TestConsecutiveSameKeyMerges passed\n";
    }

    void TestSeparatedSameKeyDoesNotRegroup()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle texture{102u};
        AddBoard(canvas, 20u, 200u, 0u, texture, BlendMode::Translucent, 0.1f);
        AddBoard(canvas, 21u, 201u, 1u, texture, BlendMode::Translucent, 0.2f);
        AddBoard(canvas, 22u, 202u, 2u, texture, BlendMode::Additive, 0.3f);
        AddBoard(canvas, 23u, 203u, 3u, texture, BlendMode::Translucent, 0.4f);

        PrepareCanvas(canvas);

        assert(canvas.GetBoardDrawCommands().size() == 3);
        AssertRun(canvas.GetBoardDrawCommands()[0], 20u, texture, BlendMode::Translucent, 0u, 2u);
        AssertRun(canvas.GetBoardDrawCommands()[1], 22u, texture, BlendMode::Additive, 2u, 1u);
        AssertRun(canvas.GetBoardDrawCommands()[2], 23u, texture, BlendMode::Translucent, 3u, 1u);

        canvas.Shutdown();
        std::cout << "TestSeparatedSameKeyDoesNotRegroup passed\n";
    }

    void TestMaskedAndModulateNormalizeIntoTranslucentBatch()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle texture{103u};
        AddBoard(canvas, 30u, 300u, 0u, texture, BlendMode::Translucent, 0.1f);
        AddBoard(canvas, 31u, 301u, 1u, texture, BlendMode::Masked, 0.2f);
        AddBoard(canvas, 32u, 302u, 2u, texture, BlendMode::Modulate, 0.3f);

        PrepareCanvas(canvas);

        assert(canvas.GetBoardDrawCommands().size() == 1);
        AssertRun(canvas.GetBoardDrawCommands()[0], 30u, texture, BlendMode::Translucent, 0u, 3u);

        canvas.Shutdown();
        std::cout << "TestMaskedAndModulateNormalizeIntoTranslucentBatch passed\n";
    }

    void TestBlendPipelineVariantsSplitRuns()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle texture{104u};
        AddBoard(canvas, 40u, 400u, 0u, texture, BlendMode::Translucent, 0.1f);
        AddBoard(canvas, 41u, 401u, 1u, texture, BlendMode::Additive, 0.2f);
        AddBoard(canvas, 42u, 402u, 2u, texture, BlendMode::Additive, 0.3f);
        AddBoard(canvas, 43u, 403u, 3u, texture, BlendMode::Opaque, 0.4f);
        AddBoard(canvas, 44u, 404u, 4u, texture, BlendMode::Opaque, 0.5f);

        PrepareCanvas(canvas);

        assert(canvas.GetBoardDrawCommands().size() == 3);
        AssertRun(canvas.GetBoardDrawCommands()[0], 40u, texture, BlendMode::Translucent, 0u, 1u);
        AssertRun(canvas.GetBoardDrawCommands()[1], 41u, texture, BlendMode::Additive, 1u, 2u);
        AssertRun(canvas.GetBoardDrawCommands()[2], 43u, texture, BlendMode::Opaque, 3u, 2u);

        canvas.Shutdown();
        std::cout << "TestBlendPipelineVariantsSplitRuns passed\n";
    }

    void TestDifferentTexturesSplitRuns()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle textureA{105u};
        const TextureHandle textureB{106u};
        AddBoard(canvas, 50u, 500u, 0u, textureA, BlendMode::Translucent, 0.1f);
        AddBoard(canvas, 51u, 501u, 1u, textureB, BlendMode::Translucent, 0.2f);

        PrepareCanvas(canvas);

        assert(canvas.GetBoardDrawCommands().size() == 2);
        AssertRun(canvas.GetBoardDrawCommands()[0], 50u, textureA, BlendMode::Translucent, 0u, 1u);
        AssertRun(canvas.GetBoardDrawCommands()[1], 51u, textureB, BlendMode::Translucent, 1u, 1u);

        canvas.Shutdown();
        std::cout << "TestDifferentTexturesSplitRuns passed\n";
    }

    void TestInvalidTexturesMergeForFallbackPath()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        const TextureHandle invalidTexture = TextureHandle::Invalid();
        AddBoard(canvas, 60u, 600u, 0u, invalidTexture, BlendMode::Translucent, 0.1f);
        AddBoard(canvas, 61u, 601u, 1u, invalidTexture, BlendMode::Translucent, 0.2f);

        PrepareCanvas(canvas);

        assert(canvas.GetBoardDrawCommands().size() == 1);
        AssertRun(canvas.GetBoardDrawCommands()[0], 60u, invalidTexture, BlendMode::Translucent, 0u, 2u);

        canvas.Shutdown();
        std::cout << "TestInvalidTexturesMergeForFallbackPath passed\n";
    }

    void TestDisabledBatchingEmitsOneCommandPerBoard()
    {
        CanvasView canvas;
        InitializeCanvas(canvas);
        canvas.SetBoardInstanceBatchingEnabled(false);
        const TextureHandle texture{107u};
        AddBoard(canvas, 70u, 700u, 0u, texture, BlendMode::Translucent, 0.1f);
        AddBoard(canvas, 71u, 701u, 1u, texture, BlendMode::Translucent, 0.2f);
        AddBoard(canvas, 72u, 702u, 2u, texture, BlendMode::Translucent, 0.3f);

        PrepareCanvas(canvas);

        assert(canvas.GetBoardDrawCommands().size() == 3);
        assert(canvas.GetBoardInstanceData().size() == 3);
        AssertRun(canvas.GetBoardDrawCommands()[0], 70u, texture, BlendMode::Translucent, 0u, 1u);
        AssertRun(canvas.GetBoardDrawCommands()[1], 71u, texture, BlendMode::Translucent, 1u, 1u);
        AssertRun(canvas.GetBoardDrawCommands()[2], 72u, texture, BlendMode::Translucent, 2u, 1u);

        canvas.Shutdown();
        std::cout << "TestDisabledBatchingEmitsOneCommandPerBoard passed\n";
    }
} // namespace

int main()
{
    std::cout << "BoardInstanceBatchingTest start\n";

    TestConsecutiveSameKeyMerges();
    TestSeparatedSameKeyDoesNotRegroup();
    TestMaskedAndModulateNormalizeIntoTranslucentBatch();
    TestBlendPipelineVariantsSplitRuns();
    TestDifferentTexturesSplitRuns();
    TestInvalidTexturesMergeForFallbackPath();
    TestDisabledBatchingEmitsOneCommandPerBoard();

    std::cout << "BoardInstanceBatchingTest passed\n";
    return 0;
}
