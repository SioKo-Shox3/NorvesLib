#pragma once

#include "Rendering/DynamicBufferRing.h"
#include "Rendering/DynamicUniformAllocator.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ITexture;
} // namespace NorvesLib::RHI

namespace NorvesLib::Core::Rendering
{
    class DebugDrawPass : public IViewPass, public IRenderGraphPass
    {
    public:
        DebugDrawPass() = default;
        ~DebugDrawPass() override;

        const char* GetName() const override { return "DebugDrawPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        struct AttachmentSignature
        {
            RGAttachmentKind Kind = RGAttachmentKind::Color;
            RHI::Format Format = RHI::Format::UNKNOWN;
            RHI::AttachmentLoadOp LoadOp = RHI::AttachmentLoadOp::DontCare;
            RHI::AttachmentStoreOp StoreOp = RHI::AttachmentStoreOp::Store;
            RHI::ResourceState InitialState = RHI::ResourceState::Undefined;
            RHI::ResourceState FinalState = RHI::ResourceState::Undefined;
            RHI::ITexture* Target = nullptr;
            uint32_t Width = 0;
            uint32_t Height = 0;
            bool bDepthReadOnly = false;
        };

        struct RenderPassSignature
        {
            AttachmentSignature ToneMappedColor;
            AttachmentSignature SceneDepth;
            bool bValid = false;
        };

        bool PrepareResources(uint32_t width,
                              uint32_t height,
                              const RHI::TexturePtr& toneMappedColorTexture,
                              const RHI::TexturePtr& sceneDepthTexture);
        bool AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                       const AttachmentSignature& rhs) const;
        bool RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                       const RenderPassSignature& rhs) const;
        RenderPassSignature CreateRenderPassSignature(uint32_t width,
                                                      uint32_t height,
                                                      const RHI::TexturePtr& toneMappedColorTexture,
                                                      const RHI::TexturePtr& sceneDepthTexture) const;

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_LineVertexShader;
        RHI::ShaderPtr m_LineFragmentShader;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::TexturePtr m_ToneMappedColorTexture;
        RHI::TexturePtr m_SceneDepthTexture;
        DynamicUniformAllocator m_UniformAllocator;
        DynamicBufferRing m_VertexRing;
        RGResourceHandle m_ToneMappedColorHandle;
        RGResourceHandle m_SceneDepthHandle;
        RenderPassSignature m_RenderPassSignature;
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
