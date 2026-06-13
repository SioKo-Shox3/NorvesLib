#pragma once

#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    class IRenderGraphPass
    {
    public:
        virtual ~IRenderGraphPass() = default;

        virtual const char* GetName() const = 0;
        virtual void Declare(RenderGraphBuilder& builder) = 0;
        virtual void Execute(RenderGraphResources& resources, ViewRenderContext& context) = 0;
    };

} // namespace NorvesLib::Core::Rendering
