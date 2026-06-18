#pragma once

#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Text/IdentityPool.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderGraph;
    struct ViewRenderContext;

    class RenderGraphBuilder
    {
    public:
        RenderGraphBuilder(RenderGraph* graph,
                           uint32_t passIndex,
                           const ViewRenderContext* context = nullptr);

        RGResourceHandle CreateTexture(const RGTextureDesc& desc);
        RGResourceHandle CreateBuffer(const RGBufferDesc& desc);
        RGResourceHandle CreateLogical(const char* debugName = nullptr);

        RGTextureHandle CreateTextureHandle(const RGTextureDesc& desc);
        RGBufferHandle CreateBufferHandle(const RGBufferDesc& desc);

        RGResourceHandle ImportTexture(RHI::TexturePtr texture,
                                       RHI::ResourceState initialState,
                                       const char* debugName = nullptr);
        RGResourceHandle ImportBuffer(RHI::BufferPtr buffer,
                                      RHI::ResourceState initialState,
                                      const char* debugName = nullptr);

        void Read(RGResourceHandle handle,
                  RHI::ResourceState state = RHI::ResourceState::ShaderResource);
        void Read(RGResourceHandle handle,
                  RHI::ResourceState state,
                  uint64_t offset,
                  uint64_t size);
        void Write(RGResourceHandle handle,
                   RHI::ResourceState state = RHI::ResourceState::RenderTarget);
        void Write(RGResourceHandle handle,
                   RHI::ResourceState state,
                   uint64_t offset,
                   uint64_t size);
        void Write(RGResourceHandle handle,
                   RHI::ResourceState state,
                   RHI::ResourceState finalState);
        void Write(RGResourceHandle handle,
                   RHI::ResourceState state,
                   RHI::ResourceState finalState,
                   uint64_t offset,
                   uint64_t size);
        bool PublishTexture(Identity name, RGTextureHandle handle);
        bool PublishBuffer(Identity name, RGBufferHandle handle);
        RGTextureHandle ReadTexture(Identity name,
                                    RHI::ResourceState state = RHI::ResourceState::ShaderResource);
        bool TryReadTexture(Identity name,
                            RGTextureHandle& outHandle,
                            RHI::ResourceState state = RHI::ResourceState::ShaderResource);
        bool TryLoadStoreColorAttachment(Identity name,
                                         RGTextureHandle& outHandle,
                                         RHI::AttachmentLoadOp loadOp,
                                         RHI::AttachmentStoreOp storeOp,
                                         RHI::ResourceState state,
                                         RHI::ResourceState finalState);
        bool TryUseAttachment(Identity name,
                              RGTextureHandle& outHandle,
                              RGAttachmentKind kind,
                              RGAttachmentMutability mutability,
                              RHI::AttachmentLoadOp loadOp,
                              RHI::AttachmentStoreOp storeOp,
                              RHI::ResourceState state,
                              RHI::ResourceState finalState);
        bool LoadStoreColorAttachment(RGResourceHandle handle,
                                      RHI::AttachmentLoadOp loadOp,
                                      RHI::AttachmentStoreOp storeOp,
                                      RHI::ResourceState state,
                                      RHI::ResourceState finalState);
        bool UseAttachment(RGResourceHandle handle,
                           RGAttachmentKind kind,
                           RGAttachmentMutability mutability,
                           RHI::AttachmentLoadOp loadOp,
                           RHI::AttachmentStoreOp storeOp,
                           RHI::ResourceState state,
                           RHI::ResourceState finalState);
        RGBufferHandle ReadBuffer(Identity name,
                                  RHI::ResourceState state = RHI::ResourceState::ShaderResource);
        RGBufferHandle ReadBuffer(Identity name,
                                  RHI::ResourceState state,
                                  uint64_t offset,
                                  uint64_t size);
        RGTextureHandle WriteTexture(Identity name,
                                     const RGTextureDesc& desc,
                                     RHI::ResourceState state = RHI::ResourceState::RenderTarget,
                                     RHI::ResourceState finalState = RHI::ResourceState::RenderTarget);
        RGTextureHandle WriteTextureAttachment(Identity name,
                                               const RGTextureDesc& desc,
                                               RGAttachmentKind kind,
                                               RHI::AttachmentLoadOp loadOp,
                                               RHI::AttachmentStoreOp storeOp,
                                               RHI::ResourceState state,
                                               RHI::ResourceState finalState);
        RGBufferHandle WriteBuffer(Identity name,
                                   const RGBufferDesc& desc,
                                   RHI::ResourceState state,
                                   RHI::ResourceState finalState);
        RGBufferHandle WriteBuffer(Identity name,
                                   const RGBufferDesc& desc,
                                   RHI::ResourceState state,
                                   RHI::ResourceState finalState,
                                   uint64_t offset,
                                   uint64_t size);
        bool TryGetTexture(Identity name, RGTextureHandle& outHandle) const;
        bool TryGetBuffer(Identity name, RGBufferHandle& outHandle) const;
        bool ExportTexture(Identity name, RGTextureHandle handle);
        void PreserveInsertionOrder();
        bool AddDependency(uint32_t beforePassIndex, uint32_t afterPassIndex);

        uint32_t GetPassIndex() const
        {
            return m_PassIndex;
        }

        const ViewRenderContext* GetContext() const
        {
            return m_Context;
        }

    private:
        RenderGraph* m_Graph = nullptr;
        uint32_t m_PassIndex = RGInvalidPassIndex;
        const ViewRenderContext* m_Context = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
