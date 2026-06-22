#pragma once

#include "RenderTypes.h"
#include "SceneProxy.h"
#include "DrawCommand.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    enum class CanvasLayerCompositeMode : uint8_t
    {
        Inline,
        OwnRT
    };

    struct CanvasLayerCompositeSnapshot
    {
        uint32_t LayerPriority = 0;
        CanvasLayerCompositeMode Mode = CanvasLayerCompositeMode::Inline;
        float Opacity = 1.0f;
        CommandRange DrawCommandRange;
    };

    /**
     * @brief Per-viewport render input plan.
     *
     * Live Viewport objects remain on the GameThread side as configuration.
     * RenderThread code reads this immutable per-frame value copy.
     */
    struct ViewportRenderPlan
    {
        uint32_t ViewId = 0;
        uint32_t ViewportId = 0;

        bool bEnabled = true;
        bool bHasCamera = false;

        ViewportRect NormalizedRect;
        ViewportRect PixelRect;
        ScissorRect Scissor;

        uint32_t RenderWidth = 0;
        uint32_t RenderHeight = 0;

        CameraProxy Camera;

        DebugViewMode DebugMode = DebugViewMode::Normal;

        CommandRange DrawCommandRange;
        CommandRange OpaqueCommandRange;
        CommandRange TransparentCommandRange;

        Container::VariableArray<CanvasLayerCompositeSnapshot> CanvasLayerComposites;

        bool HasDrawableExtent() const
        {
            return bEnabled && RenderWidth > 0 && RenderHeight > 0 &&
                   PixelRect.Width > 0.0f && PixelRect.Height > 0.0f;
        }

        void Clear()
        {
            ViewId = 0;
            ViewportId = 0;
            bEnabled = true;
            bHasCamera = false;
            NormalizedRect = ViewportRect{};
            PixelRect = ViewportRect{};
            Scissor = ScissorRect{};
            RenderWidth = 0;
            RenderHeight = 0;
            Camera = CameraProxy{};
            DebugMode = DebugViewMode::Normal;
            DrawCommandRange = CommandRange{};
            OpaqueCommandRange = CommandRange{};
            TransparentCommandRange = CommandRange{};
            CanvasLayerComposites.clear();
        }
    };

    /**
     * @brief Per-view render input plan.
     */
    struct ViewRenderPlan
    {
        uint32_t ViewId = 0;
        uint8_t ViewType = 0;
        int32_t Priority = 0;
        bool bEnabled = true;

        Container::VariableArray<ViewportRenderPlan> Viewports;

        void Clear()
        {
            ViewId = 0;
            ViewType = 0;
            Priority = 0;
            bEnabled = true;
            Viewports.clear();
        }
    };

} // namespace NorvesLib::Core::Rendering
