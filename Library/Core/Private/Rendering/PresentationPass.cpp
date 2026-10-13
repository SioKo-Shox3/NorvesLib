#include "Rendering/PresentationPass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/FrameResourceOwner.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IRenderPass.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // Blit のパイプラインのレイアウトと同じ形（binding 0: 入力のテクスチャとサンプラ、binding 1: 表示の符号化の定数）
        RHI::DescriptorSetDesc CreatePresentationBlitDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc descriptorSetDesc;

            RHI::DescriptorBinding textureBinding;
            textureBinding.binding = 0;
            textureBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            textureBinding.stages = RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(textureBinding);

            RHI::DescriptorBinding encodeBinding;
            encodeBinding.binding = 1;
            encodeBinding.type = RHI::ResourceBindType::ConstantBuffer;
            encodeBinding.stages = RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(encodeBinding);
            return descriptorSetDesc;
        }
    } // namespace

    RHI::DescriptorSetPtr PreparePresentationBlitDescriptorSet(ViewRenderContext& context,
                                                               const RHI::RenderPassPtr& renderPass,
                                                               const RHI::TexturePtr& sourceTexture,
                                                               const RHI::SamplerPtr& sampler,
                                                               const RHI::DescriptorSetPtr& unownedFallback)
    {
        if (!sourceTexture || !sampler)
        {
            return nullptr;
        }

        FrameResourceLane* frameResources = context.FrameResources;
        if (!frameResources)
        {
            // 持ち主の無い文脈は呼び出し元の使い回しのセット（binding 1 は呼び出し元が結んである）を書き換える
            if (!unownedFallback)
            {
                return nullptr;
            }
            unownedFallback->BindTexture(0, sourceTexture);
            unownedFallback->BindSampler(0, sampler);
            unownedFallback->Update();
            return unownedFallback;
        }

        if (!context.Device || !renderPass)
        {
            return nullptr;
        }

        // 符号化の経路はスワップチェーンの形式で決まる（sRGB の形式は機器が符号化し、UNORM はシェーダが OETF をかける）
        RHI::PresentationEncodeParams encodeParams;
        encodeParams.EncodePath =
            RHI::GetPresentationEncodePath(renderPass->GetColorAttachmentFormat(0)) == RHI::PresentationEncodePath::ShaderOETF
                ? 1u
                : 0u;

        const RHI::DescriptorSetLayoutPtr layout =
            context.Device->GetOrCreateDescriptorSetLayout(CreatePresentationBlitDescriptorSetDesc());
        if (!layout)
        {
            NORVES_LOG_ERROR("PresentationPass", "表示の Blit の記述子セットのレイアウトを作れません");
            return nullptr;
        }

        const FrameUploadAllocation encodeAllocation = frameResources->AllocateConstants(encodeParams);
        RHI::DescriptorSetPtr descriptorSet = frameResources->AllocateDescriptorSet(layout).Set;
        if (!encodeAllocation.IsValid() || !descriptorSet)
        {
            NORVES_LOG_ERROR("PresentationPass", "表示の Blit の定数か記述子セットを借りられません");
            return nullptr;
        }

        descriptorSet->BindTexture(0, sourceTexture);
        descriptorSet->BindSampler(0, sampler);
        descriptorSet->BindConstantBuffer(1,
                                          encodeAllocation.Buffer,
                                          encodeAllocation.GetOffset32(),
                                          static_cast<uint32_t>(sizeof(RHI::PresentationEncodeParams)));
        descriptorSet->Update();
        return descriptorSet;
    }

    void PresentationPass::SetRequest(const PresentationPassRequest& request)
    {
        m_Request = request;
        ResetResult();
    }

    void PresentationPass::ResetResult()
    {
        m_Result = PresentationPassResult{};
        m_InputHandle = RGTextureHandle{};
        m_BackBufferHandle = RGResourceHandle{};
        m_InputName = Identity{};
    }

    void PresentationPass::Declare(RenderGraphBuilder& builder)
    {
        ResetResult();

        RGTextureHandle inputHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::CompositeColor,
                                   inputHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputHandle = inputHandle;
            m_InputName = RenderGraphResourceNames::CompositeColor;
        }
        else if (builder.TryReadTexture(RenderGraphResourceNames::PresentationColor,
                                   inputHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputHandle = inputHandle;
            m_InputName = RenderGraphResourceNames::PresentationColor;
        }
        else if (builder.TryReadTexture(RenderGraphResourceNames::ToneMappedColor,
                                        inputHandle,
                                        RHI::ResourceState::ShaderResource))
        {
            m_InputHandle = inputHandle;
            m_InputName = RenderGraphResourceNames::ToneMappedColor;
        }

        const RHI::RenderPassPtr renderPass = m_Request.bClearPresentation
                                                  ? m_Request.ClearRenderPass
                                                  : m_Request.LoadRenderPass;
        const RHI::FramebufferPtr framebuffer = m_Request.bClearPresentation
                                                    ? m_Request.ClearFramebuffer
                                                    : m_Request.LoadFramebuffer;
        if (!m_InputHandle.IsValid() ||
            !m_Request.BackBufferTexture ||
            !renderPass ||
            !framebuffer ||
            !m_Request.BlitPipeline ||
            !m_Request.BlitSampler)
        {
            return;
        }

        const RHI::ResourceState initialState = m_Request.bClearPresentation
                                                   ? RHI::ResourceState::Undefined
                                                   : RHI::ResourceState::Present;
        m_BackBufferHandle = builder.ImportTexture(m_Request.BackBufferTexture,
                                                   initialState,
                                                   "SwapChainBackBuffer");
        if (!m_BackBufferHandle.IsValid())
        {
            return;
        }

        builder.LoadStoreColorAttachment(m_BackBufferHandle,
                                         m_Request.bClearPresentation
                                             ? RHI::AttachmentLoadOp::Clear
                                             : RHI::AttachmentLoadOp::Load,
                                         RHI::AttachmentStoreOp::Store,
                                         RHI::ResourceState::RenderTarget,
                                         RHI::ResourceState::Present);
    }

    void PresentationPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_Result = PresentationPassResult{};
        context.bPresentationGraphPassHandled = false;

        if (!m_InputHandle.IsValid() ||
            !m_BackBufferHandle.IsValid() ||
            !m_Request.BackBufferTexture ||
            !m_Request.BlitPipeline ||
            !m_Request.BlitSampler)
        {
            return;
        }

        const RHI::RenderPassPtr renderPass = m_Request.bClearPresentation
                                                  ? m_Request.ClearRenderPass
                                                  : m_Request.LoadRenderPass;
        const RHI::FramebufferPtr framebuffer = m_Request.bClearPresentation
                                                    ? m_Request.ClearFramebuffer
                                                    : m_Request.LoadFramebuffer;
        if (!renderPass || !framebuffer)
        {
            return;
        }

        RHI::TexturePtr inputTexture = resources.GetTexture(m_InputHandle);
        RHI::TexturePtr backBuffer = resources.GetTexture(m_BackBufferHandle);
        if (!inputTexture || backBuffer.get() != m_Request.BackBufferTexture.get())
        {
            return;
        }

        const RHI::Viewport viewport = context.GetActiveOutputViewport();
        const RHI::ScissorRect scissor = context.GetActiveOutputScissor();

        // 記述子セットと定数は Blit を遅らせるときもここで借りて書き終え、遅らせた記録（RecordDeferredBlit）は
        // 束縛だけにする
        const RHI::DescriptorSetPtr blitDescriptorSet = PreparePresentationBlitDescriptorSet(
            context, renderPass, inputTexture, m_Request.BlitSampler, m_Request.BlitDescriptorSet);
        if (!blitDescriptorSet)
        {
            return;
        }

        if (!m_bDeferBlit)
        {
            context.EnqueueFullscreenPass(renderPass,
                                          framebuffer,
                                          viewport,
                                          scissor,
                                          m_Request.BlitPipeline,
                                          blitDescriptorSet);
            m_Result.bBlitRecorded = true;
        }

        m_Result.bPresented = true;
        m_Result.InputName = m_InputName;
        m_Result.InputTexture = inputTexture;
        m_Result.BackBufferTexture = backBuffer;
        m_Result.RenderPass = renderPass;
        m_Result.Framebuffer = framebuffer;
        m_Result.Viewport = viewport;
        m_Result.Scissor = scissor;
        m_Result.LoadOp = m_Request.bClearPresentation
                              ? RHI::AttachmentLoadOp::Clear
                              : RHI::AttachmentLoadOp::Load;
        m_Result.BlitDescriptorSet = blitDescriptorSet;
        m_Result.BlitDescriptorGeneration =
            context.FrameResources ? context.FrameResources->GetGeneration() : FrameResourceGeneration{};
        context.bPresentationGraphPassHandled = true;
    }

    bool PresentationPass::RecordDeferredBlit(ViewRenderContext& context)
    {
        if (!m_Result.bPresented ||
            m_Result.bBlitRecorded ||
            !m_Result.RenderPass ||
            !m_Result.Framebuffer ||
            !m_Request.BlitPipeline ||
            !m_Request.BlitSampler)
        {
            return m_Result.bBlitRecorded;
        }

        // Execute が同じフレームで借りて書いたセットを束縛する。別のフレームなら（借りた物は戻っている）借り直す
        RHI::DescriptorSetPtr blitDescriptorSet = m_Result.BlitDescriptorSet;
        const bool bBorrowedThisFrame =
            !context.FrameResources || context.FrameResources->IsCurrent(m_Result.BlitDescriptorGeneration);
        if (!blitDescriptorSet || !bBorrowedThisFrame)
        {
            blitDescriptorSet = PreparePresentationBlitDescriptorSet(
                context, m_Result.RenderPass, m_Result.InputTexture, m_Request.BlitSampler, m_Request.BlitDescriptorSet);
            if (!blitDescriptorSet)
            {
                return false;
            }
        }
        context.EnqueueFullscreenPass(m_Result.RenderPass,
                                      m_Result.Framebuffer,
                                      m_Result.Viewport,
                                      m_Result.Scissor,
                                      m_Request.BlitPipeline,
                                      blitDescriptorSet);
        m_Result.bBlitRecorded = true;
        return true;
    }

    RHI::FramebufferPtr PresentationPass::AcquireOverlayFramebuffer(
        const RHI::DevicePtr& device,
        uint32_t frameSlotIndex,
        uint32_t frameSlotCount,
        const RHI::TexturePtr& targetTexture)
    {
        if (!device || !targetTexture ||
            targetTexture->GetFormat() != RHI::Format::R16G16B16A16_FLOAT ||
            frameSlotCount == 0 ||
            frameSlotIndex >= frameSlotCount)
        {
            return {};
        }

        if (!m_OverlayDevice)
        {
            m_OverlayDevice = device;
            m_OverlayFramebufferRing.resize(frameSlotCount);
        }
        else if (m_OverlayDevice.get() != device.get() ||
                 m_OverlayFramebufferRing.size() != frameSlotCount)
        {
            return {};
        }

        if (!m_OverlayRenderPass)
        {
            RHI::AttachmentDesc overlayColorAttachment;
            overlayColorAttachment.format = RHI::Format::R16G16B16A16_FLOAT;
            overlayColorAttachment.isDepthStencil = false;
            overlayColorAttachment.clear = false;
            overlayColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            overlayColorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            overlayColorAttachment.initialState = RHI::ResourceState::ShaderResource;
            overlayColorAttachment.finalState = RHI::ResourceState::ShaderResource;

            RHI::RenderPassDesc overlayRenderPassDesc;
            overlayRenderPassDesc.colorAttachments.push_back(overlayColorAttachment);
            m_OverlayRenderPass = device->CreateRenderPass(overlayRenderPassDesc);
            if (!m_OverlayRenderPass)
            {
                InvalidateOverlayResources();
                return {};
            }
        }

        OverlayFramebufferEntry& entry = m_OverlayFramebufferRing[frameSlotIndex];
        if (entry.Framebuffer &&
            entry.Texture.get() == targetTexture.get() &&
            entry.Width == targetTexture->GetWidth() &&
            entry.Height == targetTexture->GetHeight() &&
            entry.Format == targetTexture->GetFormat())
        {
            return entry.Framebuffer;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_OverlayRenderPass;
        framebufferDesc.colorTargets.push_back(targetTexture);
        framebufferDesc.width = targetTexture->GetWidth();
        framebufferDesc.height = targetTexture->GetHeight();
        RHI::FramebufferPtr framebuffer = device->CreateFramebuffer(framebufferDesc);
        if (!framebuffer)
        {
            return {};
        }

        entry.Texture = targetTexture;
        entry.Framebuffer = framebuffer;
        entry.Width = targetTexture->GetWidth();
        entry.Height = targetTexture->GetHeight();
        entry.Format = targetTexture->GetFormat();
        return framebuffer;
    }

    void PresentationPass::InvalidateOverlayResources()
    {
        m_OverlayFramebufferRing.clear();
        m_OverlayRenderPass.reset();
        m_OverlayDevice.reset();
    }

} // namespace NorvesLib::Core::Rendering
