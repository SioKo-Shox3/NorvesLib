#include "Rendering/GBufferProgramOverlayPass.h"

#include "Logging/LogMacros.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/ViewRenderContext.h"

namespace NorvesLib::Core::Rendering
{
    bool GBufferProgramOverlayPass::Initialize(ViewRenderContext& context)
    {
        if (!context.Device)
        {
            return false;
        }
        m_Targets.Initialize(context.Device);
        m_bInitialized = true;
        return true;
    }

    void GBufferProgramOverlayPass::Shutdown()
    {
        m_Targets.Reset();
        m_bInitialized = false;
        m_bDeclared = false;
    }

    void GBufferProgramOverlayPass::Setup(ViewRenderContext& context)
    {
        (void)context;
    }

    void GBufferProgramOverlayPass::Execute(ViewRenderContext& context)
    {
        (void)context;
    }

    void GBufferProgramOverlayPass::Declare(RenderGraphBuilder& builder)
    {
        m_bDeclared = false;
        const ViewRenderContext* context = builder.GetContext();
        if (!context || !m_GBufferPass || !m_GBufferPass->IsDrawingSkippedForVisibility(*context))
        {
            return;
        }
        if (!m_GBufferPass->HasProgramDraws(*context))
        {
            if (!m_bLoggedEmpty)
            {
                m_bLoggedEmpty = true;
                NORVES_LOG_INFO("GBufferProgramOverlayPass",
                                "GBUFFER_PROGRAM_OVERLAY program_draws=0 resource_accesses=0 対象がないため何も宣言しません");
            }
            return;
        }
        m_bDeclared = m_Targets.Declare(builder);
        if (m_bDeclared)
        {
            // グラフの記録が始まる前に RenderThread で表とテクスチャを準備する。
            m_GBufferPass->PrepareProgramOverlay(*context);
            builder.PreserveInsertionOrder();
        }
    }

    void GBufferProgramOverlayPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bDeclared || (!m_bInitialized && !Initialize(context)) || !m_Targets.Prepare(resources))
        {
            return;
        }
        // 色 5 枚と深度の形式・サンプル数は GBufferPass と同じで、Load/Store の違いは互換性に影響しない。
        m_GBufferPass->RecordProgramOverlay(context, m_Targets.GetRenderPass(), m_Targets.GetFramebuffer());
    }
} // namespace NorvesLib::Core::Rendering
