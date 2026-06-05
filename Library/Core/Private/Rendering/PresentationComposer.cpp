#include "Rendering/PresentationComposer.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"

namespace NorvesLib::Core::Rendering
{
    bool PresentationComposer::Compose(const PresentationComposeRequest &request) const
    {
        if (!request.Context || !request.Renderer || !request.CommandList)
        {
            return false;
        }

        const RHI::RenderPassPtr renderPass = request.bClearPresentation
                                                  ? request.ClearRenderPass
                                                  : request.LoadRenderPass;
        const RHI::FramebufferPtr framebuffer = request.bClearPresentation
                                                    ? request.ClearFramebuffer
                                                    : request.LoadFramebuffer;
        if (!renderPass || !framebuffer)
        {
            return false;
        }

        ViewRenderContext &context = *request.Context;
        const RHI::Viewport viewport = context.GetActiveOutputViewport();
        const RHI::ScissorRect scissor = context.GetActiveOutputScissor();

        request.CommandList->SetViewport(viewport);
        request.CommandList->SetScissor(scissor);

        const RHI::TexturePtr presentationTexture = ResolvePresentationTexture(context);
        const bool bCanBlitToSwapChain =
            presentationTexture && request.BlitPipeline && request.BlitDescriptorSet;
        if (bCanBlitToSwapChain)
        {
            request.BlitDescriptorSet->BindTexture(0, presentationTexture);
            request.BlitDescriptorSet->BindSampler(0, request.BlitSampler);
            request.BlitDescriptorSet->Update();
        }

        Container::VariableArray<FrameCommand> commands;
        commands.push_back(FrameCommand::CreateFullscreenPass(
            renderPass,
            framebuffer,
            viewport,
            scissor,
            bCanBlitToSwapChain ? request.BlitPipeline : nullptr,
            bCanBlitToSwapChain ? request.BlitDescriptorSet : nullptr));
        request.Renderer->ExecuteFrameCommands(commands, request.CommandList);
        return true;
    }

    RHI::TexturePtr PresentationComposer::ResolvePresentationTexture(const ViewRenderContext &context)
    {
        if (!context.SharedResources)
        {
            return nullptr;
        }

        RHI::TexturePtr presentationTexture = context.SharedResources->GetTexturePtr("PresentationColor");
        if (!presentationTexture)
        {
            presentationTexture = context.SharedResources->GetTexturePtr("ToneMappedColor");
        }
        return presentationTexture;
    }

} // namespace NorvesLib::Core::Rendering
