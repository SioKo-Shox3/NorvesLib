#pragma once

#include "RenderTypes.h"
#include "SceneProxy.h"
#include "DrawCommand.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief Per-viewport render input snapshot.
     *
     * The live Viewport object stays on the GameThread side as configuration.
     * The RenderThread reads only this value copy.
     */
    struct ViewportSnapshot
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

        Container::VariableArray<DrawCommand> DrawCommands;
        Container::VariableArray<DrawCommand> OpaqueCommands;
        Container::VariableArray<DrawCommand> TransparentCommands;

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
            DrawCommands.clear();
            OpaqueCommands.clear();
            TransparentCommands.clear();
        }
    };

    /**
     * @brief Per-view render input snapshot.
     */
    struct ViewFrameSnapshot
    {
        uint32_t ViewId = 0;
        uint8_t ViewType = 0;
        int32_t Priority = 0;
        bool bEnabled = true;

        Container::VariableArray<ViewportSnapshot> Viewports;

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
