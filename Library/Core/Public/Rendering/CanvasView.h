#pragma once

#include "Rendering/View.h"

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 2D canvas view rendered into a transparent frame texture.
     *
     * F1 keeps this view viewport-free; later phases add orthographic canvas
     * viewports and board drawing on top of the same frame-output contract.
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
