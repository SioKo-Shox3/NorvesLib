#pragma once

#include "Rendering/View.h"

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 2D canvas view rendered into a transparent frame texture.
     *
     * Canvas owns exactly one full-rect viewport. RenderingCoordinator assigns
     * the shared orthographic camera; CanvasView does not access camera tables.
     */
    class CanvasView : public View
    {
    public:
        CanvasView();
        ~CanvasView() override;

        bool Initialize(const ViewSettings& settings) override;
        void Render(ViewRenderContext& context) override;
    };

} // namespace NorvesLib::Core::Rendering
