#pragma once

#include "Rendering/RenderGraph/RenderGraphTypes.h"

namespace NorvesLib::Core::Rendering
{
    class RenderGraph;

    class RenderGraphResources
    {
    public:
        explicit RenderGraphResources(RenderGraph* graph);

        RHI::TexturePtr GetTexture(RGResourceHandle handle);
        RHI::BufferPtr GetBuffer(RGResourceHandle handle);
        RHI::ITexture* GetTextureRaw(RGResourceHandle handle);
        RHI::IBuffer* GetBufferRaw(RGResourceHandle handle);
        RHI::TexturePtr GetTexture(RGTextureHandle handle);
        RHI::BufferPtr GetBuffer(RGBufferHandle handle);
        RHI::ITexture* GetTextureRaw(RGTextureHandle handle);
        RHI::IBuffer* GetBufferRaw(RGBufferHandle handle);

    private:
        RenderGraph* m_Graph = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
