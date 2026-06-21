#pragma once

#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
    struct CompositePassRequest
    {
        RHI::TexturePtr SceneTexture;
        RHI::TexturePtr CanvasTexture;
    };

    struct CompositePassResult
    {
        bool bPublishedComposite = false;
        bool bImportedCanvas = false;
        bool bScenePassthrough = true;
        RHI::TexturePtr SceneTexture;
        RHI::TexturePtr CanvasTexture;
        RHI::TexturePtr OutputTexture;
    };

    /**
     * @brief Stage-2 pass that publishes the final composite color.
     *
     * F1 imports physical per-view frame outputs. It safely aliases scene to
     * Composite.Color; later board phases can replace Execute with alpha-over.
     */
    class CompositePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override
        {
            return "CompositePass";
        }

        void SetRequest(const CompositePassRequest& request);
        void ResetResult();

        const CompositePassResult& GetLastResult() const
        {
            return m_Result;
        }

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        CompositePassRequest m_Request;
        CompositePassResult m_Result;
        RGResourceHandle m_SceneHandle;
        RGResourceHandle m_CanvasHandle;
        RGResourceHandle m_OutputHandle;
    };

} // namespace NorvesLib::Core::Rendering
