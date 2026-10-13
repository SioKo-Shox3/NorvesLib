#include "Rendering/PresentationComposer.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/PresentationPass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"

namespace NorvesLib::Core::Rendering
{
    bool PresentationComposer::Compose(const PresentationComposeRequest &request) const
    {
        if (request.OutCaptureSource)
        {
            *request.OutCaptureSource = FrameCaptureSource{};
        }

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
        // Blit の記述子セットはこの合成の分をフレームの持ち主から借りる（持ち主の無い文脈は request の使い回しのセット）
        RHI::DescriptorSetPtr blitDescriptorSet;
        if (presentationTexture && request.BlitPipeline)
        {
            blitDescriptorSet = PreparePresentationBlitDescriptorSet(
                context, renderPass, presentationTexture, request.BlitSampler, request.BlitDescriptorSet);
        }
        const bool bCanBlitToSwapChain = static_cast<bool>(blitDescriptorSet);
        if (bCanBlitToSwapChain)
        {
            if (request.OutCaptureSource)
            {
                request.OutCaptureSource->Texture = presentationTexture;
                request.OutCaptureSource->CurrentState = RHI::ResourceState::ShaderResource;
                request.OutCaptureSource->RestoreState = RHI::ResourceState::ShaderResource;
            }
        }

        Container::VariableArray<FrameCommand> commands;
        commands.push_back(FrameCommand::CreateFullscreenPass(
            renderPass,
            framebuffer,
            viewport,
            scissor,
            bCanBlitToSwapChain ? request.BlitPipeline : nullptr,
            bCanBlitToSwapChain ? blitDescriptorSet : nullptr));
        request.Renderer->ExecuteFrameCommands(commands, request.CommandList);
        return true;
    }

    RHI::TexturePtr PresentationComposer::ResolvePresentationTexture(const ViewRenderContext &context)
    {
        if (context.CurrentGraphExecutionResult && context.CurrentGraphExecutionResult->bSuccess)
        {
            RHI::TexturePtr graphTexture;
            if (context.CurrentGraphExecutionResult->TryGetTexture(RenderGraphResourceNames::PresentationColor,
                                                                   graphTexture))
            {
                return graphTexture;
            }

            if (context.CurrentGraphExecutionResult->TryGetTexture(RenderGraphResourceNames::ToneMappedColor,
                                                                   graphTexture))
            {
                return graphTexture;
            }
        }

        if (!context.SharedResources)
        {
            return nullptr;
        }

        RHI::TexturePtr presentationTexture =
            context.SharedResources->GetTexturePtr(RenderGraphResourceNames::PresentationColor);
        if (!presentationTexture)
        {
            presentationTexture = context.SharedResources->GetTexturePtr(RenderGraphResourceNames::ToneMappedColor);
        }
        return presentationTexture;
    }

} // namespace NorvesLib::Core::Rendering
