#include "Rendering/RenderExtension/RenderExtensionHelpers.h"

#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // gbuffer.frag の出力と同じ順に添付する。
        const Identity& ColorTargetName(uint32_t index)
        {
            static constexpr Identity kNames[] = {
                RenderGraphResourceNames::GBufferAlbedo,
                RenderGraphResourceNames::GBufferNormal,
                RenderGraphResourceNames::GBufferMaterial,
                RenderGraphResourceNames::GBufferEmissive,
                RenderGraphResourceNames::GBufferVelocity,
            };
            return kNames[index];
        }
    } // namespace

    void GBufferTargetBinder::Initialize(RHI::IDevice* device)
    {
        if (m_Device != device)
        {
            Reset();
            m_Device = device;
        }
    }

    bool GBufferTargetBinder::Declare(RenderGraphBuilder& builder)
    {
        m_bTargetsReady = false;
        for (RGTextureHandle& handle : m_ColorHandles)
        {
            handle = {};
        }
        m_DepthHandle = {};

        // 途中まで添付を宣言しないよう、先に 6 枚すべての存在を確かめる。
        RGTextureHandle available;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            if (!builder.TryGetTexture(ColorTargetName(index), available))
            {
                return false;
            }
        }
        if (!builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, available))
        {
            return false;
        }

        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            if (!builder.TryUseAttachment(ColorTargetName(index), m_ColorHandles[index], RGAttachmentKind::Color,
                                          RGAttachmentMutability::Write, RHI::AttachmentLoadOp::Load,
                                          RHI::AttachmentStoreOp::Store, RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource))
            {
                for (RGTextureHandle& handle : m_ColorHandles)
                {
                    handle = {};
                }
                return false;
            }
        }
        if (!builder.TryUseAttachment(RenderGraphResourceNames::GBufferDepth, m_DepthHandle,
                                      RGAttachmentKind::DepthStencil, RGAttachmentMutability::Write,
                                      RHI::AttachmentLoadOp::Load, RHI::AttachmentStoreOp::Store,
                                      RHI::ResourceState::DepthWrite, RHI::ResourceState::ShaderResource))
        {
            for (RGTextureHandle& handle : m_ColorHandles)
            {
                handle = {};
            }
            return false;
        }
        return true;
    }

    bool GBufferTargetBinder::Prepare(RenderGraphResources& resources)
    {
        m_bTargetsReady = false;
        if (!m_Device || !m_DepthHandle.IsValid())
        {
            return false;
        }

        RHI::TexturePtr colorTargets[kColorTargetCount];
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            if (!m_ColorHandles[index].IsValid())
            {
                return false;
            }
            colorTargets[index] = resources.GetTexture(m_ColorHandles[index]);
            if (!colorTargets[index])
            {
                return false;
            }
        }
        RHI::TexturePtr depthTarget = resources.GetTexture(m_DepthHandle);
        if (!depthTarget)
        {
            return false;
        }

        // render pass は形式、framebuffer は形式とテクスチャの組で再利用する。
        bool bSameFormats = m_RenderPass && m_TargetFormats[kColorTargetCount] == depthTarget->GetFormat();
        for (uint32_t index = 0; index < kColorTargetCount && bSameFormats; ++index)
        {
            bSameFormats = m_TargetFormats[index] == colorTargets[index]->GetFormat();
        }
        bool bSameTargets = bSameFormats && m_Framebuffer && m_FramebufferTargets[kColorTargetCount] == depthTarget.get();
        for (uint32_t index = 0; index < kColorTargetCount && bSameTargets; ++index)
        {
            bSameTargets = m_FramebufferTargets[index] == colorTargets[index].get();
        }
        if (bSameTargets)
        {
            m_bTargetsReady = true;
            return true;
        }

        m_Framebuffer.reset();
        for (const RHI::ITexture*& target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        if (!bSameFormats)
        {
            m_RenderPass.reset();
            for (RHI::Format& format : m_TargetFormats)
            {
                format = RHI::Format::UNKNOWN;
            }

            RHI::RenderPassDesc renderPassDesc;
            for (uint32_t index = 0; index < kColorTargetCount; ++index)
            {
                RHI::AttachmentDesc colorAttachment;
                colorAttachment.format = colorTargets[index]->GetFormat();
                colorAttachment.isDepthStencil = false;
                colorAttachment.clear = false;
                colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
                colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
                colorAttachment.initialState = RHI::ResourceState::RenderTarget;
                colorAttachment.finalState = RHI::ResourceState::ShaderResource;
                renderPassDesc.colorAttachments.push_back(colorAttachment);
            }
            renderPassDesc.hasDepthStencil = true;
            renderPassDesc.depthStencilAttachment.format = depthTarget->GetFormat();
            renderPassDesc.depthStencilAttachment.isDepthStencil = true;
            renderPassDesc.depthStencilAttachment.clear = false;
            renderPassDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            renderPassDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            renderPassDesc.depthStencilAttachment.initialState = RHI::ResourceState::DepthWrite;
            renderPassDesc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;
            m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
            if (!m_RenderPass)
            {
                return false;
            }
            for (uint32_t index = 0; index < kColorTargetCount; ++index)
            {
                m_TargetFormats[index] = colorTargets[index]->GetFormat();
            }
            m_TargetFormats[kColorTargetCount] = depthTarget->GetFormat();
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        for (const RHI::TexturePtr& target : colorTargets)
        {
            framebufferDesc.colorTargets.push_back(target);
        }
        framebufferDesc.depthStencilTarget = depthTarget;
        framebufferDesc.width = depthTarget->GetWidth();
        framebufferDesc.height = depthTarget->GetHeight();
        m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
        if (!m_Framebuffer)
        {
            return false;
        }
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            m_FramebufferTargets[index] = colorTargets[index].get();
        }
        m_FramebufferTargets[kColorTargetCount] = depthTarget.get();
        m_bTargetsReady = true;
        return true;
    }

    RHI::RenderPassPtr GBufferTargetBinder::GetRenderPass() const
    {
        return m_bTargetsReady ? m_RenderPass : RHI::RenderPassPtr{};
    }

    RHI::FramebufferPtr GBufferTargetBinder::GetFramebuffer() const
    {
        return m_bTargetsReady ? m_Framebuffer : RHI::FramebufferPtr{};
    }

    void GBufferTargetBinder::Reset()
    {
        m_bTargetsReady = false;
        m_Framebuffer.reset();
        m_RenderPass.reset();
        for (RGTextureHandle& handle : m_ColorHandles)
        {
            handle = {};
        }
        m_DepthHandle = {};
        for (const RHI::ITexture*& target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        for (RHI::Format& format : m_TargetFormats)
        {
            format = RHI::Format::UNKNOWN;
        }
        m_Device = nullptr;
    }

    void ReactiveTargetBinder::Initialize(RHI::IDevice* device)
    {
        if (m_Device != device)
        {
            Reset();
            m_Device = device;
        }
    }

    bool ReactiveTargetBinder::Declare(RenderGraphBuilder& builder, bool bWithDepth)
    {
        m_bTargetsReady = false;
        m_Texture.reset();
        m_SceneColorHandle = {};
        m_ReactiveHandle = {};
        m_DepthHandle = {};
        m_bWithDepth = false;
        m_bClear = false;

        const Identity sceneColorName = ResolveHdrChainHead(builder);
        if (!sceneColorName.IsValid() || !builder.TryGetTexture(sceneColorName, m_SceneColorHandle))
        {
            return false;
        }

        // 添付を宣言する前に、必要な名前がそろうか確かめる。
        RGTextureHandle available;
        if (bWithDepth && !builder.TryGetTexture(RenderGraphResourceNames::SceneDepth, available))
        {
            return false;
        }

        if (builder.TryGetTexture(RenderGraphResourceNames::SceneReactive, available))
        {
            if (!builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneReactive,
                                                     m_ReactiveHandle,
                                                     RHI::AttachmentLoadOp::Load,
                                                     RHI::AttachmentStoreOp::Store,
                                                     RHI::ResourceState::RenderTarget,
                                                     RHI::ResourceState::ShaderResource))
            {
                return false;
            }
        }
        else
        {
            const ViewRenderContext* context = builder.GetContext();
            const uint32_t width = context ? context->GetActiveRenderWidth() : 0;
            const uint32_t height = context ? context->GetActiveRenderHeight() : 0;
            if (width == 0 || height == 0)
            {
                return false;
            }

            m_ReactiveHandle = builder.CreateTextureHandle(
                RGTextureDesc::RenderTarget(width, height, RHI::Format::R8_UNORM, "Scene.Reactive"));
            if (!m_ReactiveHandle.IsValid() ||
                !builder.UseAttachment(m_ReactiveHandle.ToResourceHandle(),
                                       RGAttachmentKind::Color,
                                       RGAttachmentMutability::Write,
                                       RHI::AttachmentLoadOp::Clear,
                                       RHI::AttachmentStoreOp::Store,
                                       RHI::ResourceState::RenderTarget,
                                       RHI::ResourceState::ShaderResource) ||
                !builder.PublishTexture(RenderGraphResourceNames::SceneReactive, m_ReactiveHandle))
            {
                m_ReactiveHandle = {};
                return false;
            }
            m_bClear = true;
        }

        if (bWithDepth &&
            !builder.TryUseAttachment(RenderGraphResourceNames::SceneDepth,
                                      m_DepthHandle,
                                      RGAttachmentKind::DepthStencil,
                                      RGAttachmentMutability::ReadOnly,
                                      RHI::AttachmentLoadOp::Load,
                                      RHI::AttachmentStoreOp::Store,
                                      RHI::ResourceState::DepthRead,
                                      RHI::ResourceState::DepthRead))
        {
            m_ReactiveHandle = {};
            return false;
        }
        m_bWithDepth = bWithDepth;
        return true;
    }

    bool ReactiveTargetBinder::Prepare(RenderGraphResources& resources)
    {
        m_bTargetsReady = false;
        m_Texture.reset();
        if (!m_Device || !m_SceneColorHandle.IsValid() || !m_ReactiveHandle.IsValid() ||
            (m_bWithDepth && !m_DepthHandle.IsValid()))
        {
            return false;
        }

        // シーンカラーには GPU でアクセスせず、実際の寸法だけを確かめる。
        const RHI::TexturePtr sceneColor = resources.GetTexture(m_SceneColorHandle);
        const RHI::TexturePtr reactive = resources.GetTexture(m_ReactiveHandle);
        if (!sceneColor || !reactive || reactive->GetFormat() != RHI::Format::R8_UNORM ||
            reactive->GetWidth() == 0 || reactive->GetHeight() == 0 ||
            reactive->GetWidth() != sceneColor->GetWidth() || reactive->GetHeight() != sceneColor->GetHeight())
        {
            return false;
        }

        RHI::TexturePtr depth;
        if (m_bWithDepth)
        {
            depth = resources.GetTexture(m_DepthHandle);
            if (!depth || depth->GetWidth() != reactive->GetWidth() || depth->GetHeight() != reactive->GetHeight())
            {
                return false;
            }
        }

        // render pass は形式・loadOp・深度の有無、framebuffer はさらにテクスチャの組で再利用する。
        const bool bSameFormats = m_RenderPass && m_TargetFormats[0] == reactive->GetFormat() &&
                                  m_bRenderPassClears == m_bClear && m_bRenderPassHasDepth == m_bWithDepth &&
                                  (!m_bWithDepth || m_TargetFormats[1] == depth->GetFormat());
        const bool bSameTargets = bSameFormats && m_Framebuffer &&
                                  m_FramebufferTargets[0] == reactive.get() &&
                                  m_FramebufferTargets[1] == depth.get();
        if (bSameTargets)
        {
            m_Texture = reactive;
            m_bTargetsReady = true;
            return true;
        }

        m_Framebuffer.reset();
        m_FramebufferTargets[0] = nullptr;
        m_FramebufferTargets[1] = nullptr;
        if (!bSameFormats)
        {
            m_RenderPass.reset();
            m_TargetFormats[0] = RHI::Format::UNKNOWN;
            m_TargetFormats[1] = RHI::Format::UNKNOWN;

            RHI::RenderPassDesc renderPassDesc;
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = reactive->GetFormat();
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = m_bClear;
            colorAttachment.clearColor[0] = 0.0f;
            colorAttachment.clearColor[1] = 0.0f;
            colorAttachment.clearColor[2] = 0.0f;
            colorAttachment.clearColor[3] = 0.0f;
            colorAttachment.loadOp = m_bClear ? RHI::AttachmentLoadOp::Clear : RHI::AttachmentLoadOp::Load;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            renderPassDesc.colorAttachments.push_back(colorAttachment);

            if (m_bWithDepth)
            {
                renderPassDesc.hasDepthStencil = true;
                renderPassDesc.depthStencilAttachment.format = depth->GetFormat();
                renderPassDesc.depthStencilAttachment.isDepthStencil = true;
                renderPassDesc.depthStencilAttachment.clear = false;
                renderPassDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
                renderPassDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
                renderPassDesc.depthStencilAttachment.initialState = RHI::ResourceState::DepthRead;
                renderPassDesc.depthStencilAttachment.finalState = RHI::ResourceState::DepthRead;
            }

            m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
            if (!m_RenderPass)
            {
                return false;
            }
            m_TargetFormats[0] = reactive->GetFormat();
            m_TargetFormats[1] = depth ? depth->GetFormat() : RHI::Format::UNKNOWN;
            m_bRenderPassHasDepth = m_bWithDepth;
            m_bRenderPassClears = m_bClear;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        framebufferDesc.colorTargets.push_back(reactive);
        framebufferDesc.depthStencilTarget = depth;
        framebufferDesc.width = reactive->GetWidth();
        framebufferDesc.height = reactive->GetHeight();
        m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
        if (!m_Framebuffer)
        {
            return false;
        }
        m_FramebufferTargets[0] = reactive.get();
        m_FramebufferTargets[1] = depth.get();
        m_Texture = reactive;
        m_bTargetsReady = true;
        return true;
    }

    RHI::RenderPassPtr ReactiveTargetBinder::GetRenderPass() const
    {
        return m_bTargetsReady ? m_RenderPass : RHI::RenderPassPtr{};
    }

    RHI::FramebufferPtr ReactiveTargetBinder::GetFramebuffer() const
    {
        return m_bTargetsReady ? m_Framebuffer : RHI::FramebufferPtr{};
    }

    RHI::TexturePtr ReactiveTargetBinder::GetTexture() const
    {
        return m_bTargetsReady ? m_Texture : RHI::TexturePtr{};
    }

    void ReactiveTargetBinder::Reset()
    {
        m_bTargetsReady = false;
        m_Texture.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();
        m_SceneColorHandle = {};
        m_ReactiveHandle = {};
        m_DepthHandle = {};
        m_FramebufferTargets[0] = nullptr;
        m_FramebufferTargets[1] = nullptr;
        m_TargetFormats[0] = RHI::Format::UNKNOWN;
        m_TargetFormats[1] = RHI::Format::UNKNOWN;
        m_bWithDepth = false;
        m_bClear = false;
        m_bRenderPassHasDepth = false;
        m_bRenderPassClears = false;
        m_Device = nullptr;
    }

    Identity ResolveHdrChainHead(const RenderGraphBuilder& builder)
    {
        RGTextureHandle available;
        if (builder.TryGetTexture(RenderGraphResourceNames::SSRSceneColor, available))
        {
            return RenderGraphResourceNames::SSRSceneColor;
        }
        if (builder.TryGetTexture(RenderGraphResourceNames::SceneColor, available))
        {
            return RenderGraphResourceNames::SceneColor;
        }
        return {};
    }
} // namespace NorvesLib::Core::Rendering
