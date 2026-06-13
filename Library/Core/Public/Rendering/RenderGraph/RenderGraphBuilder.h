#pragma once

#include "Rendering/RenderGraph/RenderGraphTypes.h"
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

        RGResourceHandle ImportTexture(RHI::TexturePtr texture,
                                       RHI::ResourceState initialState,
                                       const char* debugName = nullptr);
        RGResourceHandle ImportBuffer(RHI::BufferPtr buffer,
                                      RHI::ResourceState initialState,
                                      const char* debugName = nullptr);

        void Read(RGResourceHandle handle,
                  RHI::ResourceState state = RHI::ResourceState::ShaderResource);
        void Write(RGResourceHandle handle,
                   RHI::ResourceState state = RHI::ResourceState::RenderTarget);
        void Write(RGResourceHandle handle,
                   RHI::ResourceState state,
                   RHI::ResourceState finalState);
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
