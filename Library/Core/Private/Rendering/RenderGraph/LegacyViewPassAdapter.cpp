#include "Rendering/RenderGraph/LegacyViewPassAdapter.h"
#include "Rendering/IViewPass.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/ViewRenderContext.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    LegacyViewPassAdapter::LegacyViewPassAdapter(IViewPass* pass)
        : m_ViewPass(pass)
    {
    }

    LegacyViewPassAdapter::LegacyViewPassAdapter(PostProcessStack* postProcessStack)
        : m_PostProcessStack(postProcessStack)
    {
    }

    const char* LegacyViewPassAdapter::GetName() const
    {
        if (m_ViewPass)
        {
            return m_ViewPass->GetName();
        }

        return "PostProcessStack";
    }

    void LegacyViewPassAdapter::Declare(RenderGraphBuilder& builder)
    {
        RGResourceHandle orderResource = builder.CreateLogical(GetName());
        builder.Write(orderResource);
        builder.PreserveInsertionOrder();
    }

    void LegacyViewPassAdapter::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        (void)resources;

        if (m_ViewPass)
        {
            if (!m_ViewPass->IsEnabled())
            {
                return;
            }

            if (!m_ViewPass->IsInitialized())
            {
                if (!m_ViewPass->Initialize(context))
                {
                    NORVES_LOG_ERROR("LegacyViewPassAdapter",
                                     "Failed to initialize pass: %s",
                                     m_ViewPass->GetName());
                    return;
                }
            }

            m_ViewPass->Setup(context);
            m_ViewPass->Execute(context);
            return;
        }

        if (!m_PostProcessStack || m_PostProcessStack->GetPassCount() == 0)
        {
            return;
        }

        if (!m_PostProcessStack->IsInitialized())
        {
            if (!m_PostProcessStack->Initialize(context))
            {
                NORVES_LOG_ERROR("LegacyViewPassAdapter", "Failed to initialize PostProcessStack");
                return;
            }
        }

        m_PostProcessStack->Setup(context);
        m_PostProcessStack->Execute(context);
    }

} // namespace NorvesLib::Core::Rendering
