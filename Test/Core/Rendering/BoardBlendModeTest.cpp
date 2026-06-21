#include "Rendering/CanvasView.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace RHI = NorvesLib::RHI;

namespace
{
    constexpr float FloatTolerance = 0.0001f;

    struct Color4
    {
        float R = 0.0f;
        float G = 0.0f;
        float B = 0.0f;
        float A = 0.0f;
    };

    void AssertNear(float actual, float expected)
    {
        assert(std::fabs(actual - expected) <= FloatTolerance);
    }

    void AssertColorNear(const Color4 &actual, const Color4 &expected)
    {
        AssertNear(actual.R, expected.R);
        AssertNear(actual.G, expected.G);
        AssertNear(actual.B, expected.B);
        AssertNear(actual.A, expected.A);
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

    BoardProxy MakeBoardProxy(uint64_t objectId,
                              uint64_t componentId,
                              uint32_t orderInLayer,
                              BlendMode blendMode)
    {
        BoardProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.LayerMask = RenderLayer::UI;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.BlendModeProp = blendMode;
        proxy.LayerPriority = 0;
        proxy.OrderInLayer = orderInLayer;
        proxy.SortKey = BoardProxy::ComputeSortKey(0u, orderInLayer);
        return proxy;
    }

    Color4 Premultiply(const Color4 &color)
    {
        return Color4{color.R * color.A, color.G * color.A, color.B * color.A, color.A};
    }

    Color4 CompositePremultipliedCanvasOverScene(const Color4 &sceneStraight,
                                                 const Color4 &canvasPremultiplied)
    {
        Color4 result;
        result.A = canvasPremultiplied.A + sceneStraight.A * (1.0f - canvasPremultiplied.A);

        const float premulR =
            canvasPremultiplied.R + sceneStraight.R * sceneStraight.A * (1.0f - canvasPremultiplied.A);
        const float premulG =
            canvasPremultiplied.G + sceneStraight.G * sceneStraight.A * (1.0f - canvasPremultiplied.A);
        const float premulB =
            canvasPremultiplied.B + sceneStraight.B * sceneStraight.A * (1.0f - canvasPremultiplied.A);

        if (result.A > 0.0f)
        {
            result.R = premulR / result.A;
            result.G = premulG / result.A;
            result.B = premulB / result.A;
        }

        return result;
    }

    Color4 CompositeStraightCanvasOverScene(const Color4 &sceneStraight, const Color4 &canvasStraight)
    {
        return CompositePremultipliedCanvasOverScene(sceneStraight, Premultiply(canvasStraight));
    }

    void TestBlendModeNormalizationAndDescriptors()
    {
        assert(CanvasView::NormalizeBoardBlendMode(BlendMode::Opaque) == BlendMode::Opaque);
        assert(CanvasView::NormalizeBoardBlendMode(BlendMode::Translucent) == BlendMode::Translucent);
        assert(CanvasView::NormalizeBoardBlendMode(BlendMode::Additive) == BlendMode::Additive);
        assert(CanvasView::NormalizeBoardBlendMode(BlendMode::Masked) == BlendMode::Translucent);
        assert(CanvasView::NormalizeBoardBlendMode(BlendMode::Modulate) == BlendMode::Translucent);

        const RHI::BlendAttachmentDesc opaqueDesc =
            CanvasView::CreateBoardBlendAttachmentDesc(BlendMode::Opaque);
        assert(!opaqueDesc.blendEnable);
        assert(opaqueDesc.colorWriteMask == RHI::ColorWriteMask::All);

        const RHI::BlendAttachmentDesc translucentDesc =
            CanvasView::CreateBoardBlendAttachmentDesc(BlendMode::Translucent);
        assert(translucentDesc.blendEnable);
        assert(translucentDesc.srcColorBlendFactor == RHI::BlendFactor::One);
        assert(translucentDesc.dstColorBlendFactor == RHI::BlendFactor::InvSrcAlpha);
        assert(translucentDesc.srcAlphaBlendFactor == RHI::BlendFactor::One);
        assert(translucentDesc.dstAlphaBlendFactor == RHI::BlendFactor::InvSrcAlpha);
        assert(translucentDesc.colorBlendOp == RHI::BlendOp::Add);
        assert(translucentDesc.alphaBlendOp == RHI::BlendOp::Add);

        const RHI::BlendAttachmentDesc additiveDesc =
            CanvasView::CreateBoardBlendAttachmentDesc(BlendMode::Additive);
        assert(additiveDesc.blendEnable);
        assert(additiveDesc.srcColorBlendFactor == RHI::BlendFactor::One);
        assert(additiveDesc.dstColorBlendFactor == RHI::BlendFactor::One);
        assert(additiveDesc.srcAlphaBlendFactor == RHI::BlendFactor::Zero);
        assert(additiveDesc.dstAlphaBlendFactor == RHI::BlendFactor::One);
        assert(additiveDesc.colorBlendOp == RHI::BlendOp::Add);
        assert(additiveDesc.alphaBlendOp == RHI::BlendOp::Add);

        std::cout << "TestBlendModeNormalizationAndDescriptors passed\n";
    }

    void TestInterleavedBlendModesKeepPainterOrder()
    {
        CanvasView canvas;
        ViewSettings settings;
        settings.Type = ViewType::UI;
        settings.Width = 640;
        settings.Height = 480;
        assert(canvas.Initialize(settings));

        const BoardProxy board0 = MakeBoardProxy(100u, 10u, 0u, BlendMode::Opaque);
        const BoardProxy board1 = MakeBoardProxy(101u, 11u, 1u, BlendMode::Translucent);
        const BoardProxy board2 = MakeBoardProxy(102u, 12u, 2u, BlendMode::Additive);
        const BoardProxy board3 = MakeBoardProxy(103u, 13u, 3u, BlendMode::Masked);
        const BoardProxy board4 = MakeBoardProxy(104u, 14u, 4u, BlendMode::Modulate);

        canvas.UpdateBoardProxy(board0.ComponentId, board0);
        canvas.UpdateBoardProxy(board1.ComponentId, board1);
        canvas.UpdateBoardProxy(board2.ComponentId, board2);
        canvas.UpdateBoardProxy(board3.ComponentId, board3);
        canvas.UpdateBoardProxy(board4.ComponentId, board4);
        canvas.PrepareBoardDrawCommands(MakeViewportPlan());

        assert(canvas.GetBoardDrawCommands().size() == 5);
        assert(canvas.GetBoardDrawCommands()[0].Draw.ObjectId == 100u);
        assert(canvas.GetBoardDrawCommands()[1].Draw.ObjectId == 101u);
        assert(canvas.GetBoardDrawCommands()[2].Draw.ObjectId == 102u);
        assert(canvas.GetBoardDrawCommands()[3].Draw.ObjectId == 103u);
        assert(canvas.GetBoardDrawCommands()[4].Draw.ObjectId == 104u);
        assert(canvas.GetBoardDrawCommands()[0].Draw.MaterialBlendMode == BlendMode::Opaque);
        assert(canvas.GetBoardDrawCommands()[1].Draw.MaterialBlendMode == BlendMode::Translucent);
        assert(canvas.GetBoardDrawCommands()[2].Draw.MaterialBlendMode == BlendMode::Additive);
        assert(canvas.GetBoardDrawCommands()[3].Draw.MaterialBlendMode == BlendMode::Translucent);
        assert(canvas.GetBoardDrawCommands()[4].Draw.MaterialBlendMode == BlendMode::Translucent);

        canvas.Shutdown();
        std::cout << "TestInterleavedBlendModesKeepPainterOrder passed\n";
    }

    void TestPremultipliedCompositeReferenceCases()
    {
        const Color4 sceneOpaque{0.2f, 0.4f, 0.6f, 1.0f};
        const Color4 boardDefaultStraight{1.0f, 1.0f, 1.0f, 0.75f};
        const Color4 boardDefaultPremultiplied = Premultiply(boardDefaultStraight);
        const Color4 legacyF4 = CompositeStraightCanvasOverScene(sceneOpaque, boardDefaultStraight);
        const Color4 premultipliedF5 =
            CompositePremultipliedCanvasOverScene(sceneOpaque, boardDefaultPremultiplied);
        AssertColorNear(premultipliedF5, legacyF4);

        const Color4 sceneTranslucent{0.1f, 0.4f, 0.8f, 0.25f};
        const Color4 canvasTranslucentPremultiplied = Premultiply(Color4{0.7f, 0.2f, 0.1f, 0.5f});
        const Color4 blended =
            CompositePremultipliedCanvasOverScene(sceneTranslucent, canvasTranslucentPremultiplied);
        AssertNear(blended.A, 0.625f);
        AssertNear(blended.R, 0.58f);
        AssertNear(blended.G, 0.24f);
        AssertNear(blended.B, 0.24f);

        const Color4 additivePremultiplied = Premultiply(Color4{0.8f, 0.5f, 0.1f, 0.25f});
        const Color4 additiveCanvas{additivePremultiplied.R, additivePremultiplied.G, additivePremultiplied.B, 0.0f};
        const Color4 additiveComposite =
            CompositePremultipliedCanvasOverScene(sceneOpaque, additiveCanvas);
        AssertNear(additiveComposite.A, 1.0f);
        AssertNear(additiveComposite.R, 0.4f);
        AssertNear(additiveComposite.G, 0.525f);
        AssertNear(additiveComposite.B, 0.625f);

        std::cout << "TestPremultipliedCompositeReferenceCases passed\n";
    }
} // namespace

int main()
{
    std::cout << "BoardBlendModeTest start\n";

    TestBlendModeNormalizationAndDescriptors();
    TestInterleavedBlendModesKeepPainterOrder();
    TestPremultipliedCompositeReferenceCases();

    std::cout << "BoardBlendModeTest passed\n";
    return 0;
}
