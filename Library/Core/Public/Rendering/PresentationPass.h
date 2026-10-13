#pragma once

#include "Container/Containers.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/ICommandList.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    struct PresentationPassRequest
    {
        RHI::TexturePtr BackBufferTexture;

        RHI::RenderPassPtr ClearRenderPass;
        RHI::RenderPassPtr LoadRenderPass;
        RHI::FramebufferPtr ClearFramebuffer;
        RHI::FramebufferPtr LoadFramebuffer;
        RHI::PipelinePtr BlitPipeline;
        /** フレームの持ち主の無い文脈（RenderingCoordinator を通さない呼び出し）でだけ使う記述子セット。持ち主がいれば Blit ごとに借りる */
        RHI::DescriptorSetPtr BlitDescriptorSet;
        RHI::SamplerPtr BlitSampler;

        bool bClearPresentation = true;
    };

    /**
     * @brief 表示の Blit の記述子セットを用意する（PresentationPass と PresentationComposer が共有する）
     *
     * 文脈にフレームの持ち主（ViewRenderContext::FrameResources）がいれば、Blit のレイアウト（binding 0: 入力の
     * テクスチャとサンプラ、binding 1: 表示の符号化の定数）のセットと符号化の定数をこの呼び出しの分だけ借りて書く。
     * 符号化の経路は renderPass の色の形式（スワップチェーンの形式）から決める。借りた物は借りたフレームの回収まで
     * 使えるので、呼び出し側は前のフレームの GPU との重なりを気にしなくてよい。
     * 持ち主の無い文脈では unownedFallback（呼び出し元が binding 1 を結んだ使い回しのセット）を書き換えて返す。
     * どちらも用意できなければ nullptr。
     */
    RHI::DescriptorSetPtr PreparePresentationBlitDescriptorSet(ViewRenderContext& context,
                                                               const RHI::RenderPassPtr& renderPass,
                                                               const RHI::TexturePtr& sourceTexture,
                                                               const RHI::SamplerPtr& sampler,
                                                               const RHI::DescriptorSetPtr& unownedFallback);

    struct PresentationPassResult
    {
        bool bPresented = false;
        bool bBlitRecorded = false;
        Identity InputName;
        RHI::TexturePtr InputTexture;
        RHI::TexturePtr BackBufferTexture;
        RHI::RenderPassPtr RenderPass;
        RHI::FramebufferPtr Framebuffer;
        RHI::Viewport Viewport;
        RHI::ScissorRect Scissor;
        RHI::AttachmentLoadOp LoadOp = RHI::AttachmentLoadOp::Clear;
        /** Execute で用意した Blit の記述子セット（遅らせた Blit はこれを束縛するだけ） */
        RHI::DescriptorSetPtr BlitDescriptorSet;
        /** BlitDescriptorSet を借りたフレームの世代（持ち主の無い文脈では未設定） */
        FrameResourceGeneration BlitDescriptorGeneration;
    };

    /**
     * Primary RenderGraph path for presenting a rendered view to the swapchain.
     */
    class PresentationPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override
        {
            return "PresentationPass";
        }

        void SetRequest(const PresentationPassRequest& request);
        void ResetResult();

        bool WasPresented() const
        {
            return m_Result.bPresented;
        }

        /**
         * True when graph presentation handled the frame and legacy fallback is unnecessary.
         */
        bool WasHandled() const
        {
            return WasPresented();
        }

        const PresentationPassResult& GetLastResult() const
        {
            return m_Result;
        }

        void SetDeferBlit(bool bDefer)
        {
            m_bDeferBlit = bDefer;
        }

        bool WasBlitRecorded() const
        {
            return m_Result.bBlitRecorded;
        }

        /**
         * @brief 遅らせた Blit を記録する
         *
         * 記述子セットと定数は同じフレームの Execute が借りて書き終えたものを束縛するだけで、ここでは書かない
         * （overlay などの後のパスが束縛した後に、同じフレームの借りた領域へ書き足さない）。Execute と別のフレームの
         * 呼び出しなら借り直す。用意できなければ false。
         */
        bool RecordDeferredBlit(ViewRenderContext& context);

        RHI::FramebufferPtr AcquireOverlayFramebuffer(const RHI::DevicePtr& device,
                                                       uint32_t frameSlotIndex,
                                                       uint32_t frameSlotCount,
                                                       const RHI::TexturePtr& targetTexture);

        RHI::RenderPassPtr GetOverlayRenderPass() const
        {
            return m_OverlayRenderPass;
        }

        void InvalidateOverlayResources();

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        struct OverlayFramebufferEntry
        {
            RHI::TexturePtr Texture;
            RHI::FramebufferPtr Framebuffer;
            uint32_t Width = 0;
            uint32_t Height = 0;
            RHI::Format Format = RHI::Format::UNKNOWN;
        };

        PresentationPassRequest m_Request;
        PresentationPassResult m_Result;
        RGTextureHandle m_InputHandle;
        RGResourceHandle m_BackBufferHandle;
        Identity m_InputName;
        bool m_bDeferBlit = false;
        RHI::DevicePtr m_OverlayDevice;
        RHI::RenderPassPtr m_OverlayRenderPass;
        Container::VariableArray<OverlayFramebufferEntry> m_OverlayFramebufferRing;
    };

} // namespace NorvesLib::Core::Rendering
