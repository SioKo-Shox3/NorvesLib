#pragma once

#include "Rendering/RenderGraph/IRenderGraphPass.h"

namespace NorvesLib::Core::Rendering
{
    class IViewPass;
    class PostProcessStack;

    class LegacyViewPassAdapter final : public IRenderGraphPass
    {
    public:
        explicit LegacyViewPassAdapter(IViewPass* pass);
        explicit LegacyViewPassAdapter(PostProcessStack* postProcessStack);

        const char* GetName() const override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        IViewPass* m_ViewPass = nullptr;
        PostProcessStack* m_PostProcessStack = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
