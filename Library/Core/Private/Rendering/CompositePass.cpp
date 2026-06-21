#include "Rendering/CompositePass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"

namespace NorvesLib::Core::Rendering
{
    void CompositePass::SetRequest(const CompositePassRequest& request)
    {
        m_Request = request;
        ResetResult();
    }

    void CompositePass::ResetResult()
    {
        m_Result = CompositePassResult{};
        m_SceneHandle = RGResourceHandle{};
        m_CanvasHandle = RGResourceHandle{};
        m_OutputHandle = RGResourceHandle{};
    }

    void CompositePass::Declare(RenderGraphBuilder& builder)
    {
        ResetResult();

        if (!m_Request.SceneTexture)
        {
            return;
        }

        m_SceneHandle = builder.ImportTexture(m_Request.SceneTexture,
                                              RHI::ResourceState::ShaderResource,
                                              "Composite.SceneInput");
        if (!m_SceneHandle.IsValid())
        {
            return;
        }
        builder.Read(m_SceneHandle, RHI::ResourceState::ShaderResource);

        if (m_Request.CanvasTexture)
        {
            m_CanvasHandle = builder.ImportTexture(m_Request.CanvasTexture,
                                                   RHI::ResourceState::ShaderResource,
                                                   "Composite.CanvasInput");
            if (m_CanvasHandle.IsValid())
            {
                builder.Read(m_CanvasHandle, RHI::ResourceState::ShaderResource);
                builder.PublishTexture(RenderGraphResourceNames::CanvasColor, m_CanvasHandle);
            }
        }

        m_OutputHandle = m_SceneHandle;
        builder.PublishTexture(RenderGraphResourceNames::CompositeColor, m_OutputHandle);
        builder.ExportTexture(RenderGraphResourceNames::CompositeColor, m_OutputHandle);
        builder.PreserveInsertionOrder();
    }

    void CompositePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        (void)context;

        m_Result = CompositePassResult{};
        if (!m_OutputHandle.IsValid())
        {
            return;
        }

        m_Result.SceneTexture = resources.GetTexture(m_SceneHandle);
        if (m_CanvasHandle.IsValid())
        {
            m_Result.CanvasTexture = resources.GetTexture(m_CanvasHandle);
        }
        m_Result.OutputTexture = resources.GetTexture(m_OutputHandle);
        m_Result.bImportedCanvas = m_Result.CanvasTexture != nullptr;
        m_Result.bPublishedComposite = m_Result.OutputTexture != nullptr;
        m_Result.bScenePassthrough = true;
    }

} // namespace NorvesLib::Core::Rendering
