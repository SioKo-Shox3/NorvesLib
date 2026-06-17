#pragma once

#include "RHI/IGPUResourceAllocator.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderGraph;
    class RenderGraphBuilder;

    static constexpr uint32_t RGInvalidPassIndex = 0xFFFFFFFFu;

    enum class RGResourceKind : uint8_t
    {
        Invalid,
        Texture,
        Buffer,
        Logical,
    };

    enum class RGResourceLifetime : uint8_t
    {
        Invalid,
        Imported,
        Transient,
        Logical,
    };

    enum class RGAccessMode : uint8_t
    {
        Read,
        Write,
    };

    enum class RGBarrierKind : uint8_t
    {
        Texture,
        Buffer,
    };

    struct RGResourceHandle
    {
        uint32_t Index = 0xFFFFFFFFu;
        uint32_t Generation = 0;

        bool IsValid() const
        {
            return Index != 0xFFFFFFFFu;
        }

        bool operator==(const RGResourceHandle& other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }

        bool operator!=(const RGResourceHandle& other) const
        {
            return !(*this == other);
        }
    };

    class RGTextureHandle
    {
    public:
        RGTextureHandle() = default;

        bool IsValid() const
        {
            return m_Resource.IsValid();
        }

        RGResourceHandle ToResourceHandle() const
        {
            return m_Resource;
        }

        bool operator==(const RGTextureHandle& other) const
        {
            return m_Resource == other.m_Resource;
        }

        bool operator!=(const RGTextureHandle& other) const
        {
            return !(*this == other);
        }

    private:
        explicit RGTextureHandle(RGResourceHandle resource)
            : m_Resource(resource)
        {
        }

        RGResourceHandle m_Resource;

        friend class RenderGraph;
        friend class RenderGraphBuilder;
    };

    class RGBufferHandle
    {
    public:
        RGBufferHandle() = default;

        bool IsValid() const
        {
            return m_Resource.IsValid();
        }

        RGResourceHandle ToResourceHandle() const
        {
            return m_Resource;
        }

        bool operator==(const RGBufferHandle& other) const
        {
            return m_Resource == other.m_Resource;
        }

        bool operator!=(const RGBufferHandle& other) const
        {
            return !(*this == other);
        }

    private:
        explicit RGBufferHandle(RGResourceHandle resource)
            : m_Resource(resource)
        {
        }

        RGResourceHandle m_Resource;

        friend class RenderGraph;
        friend class RenderGraphBuilder;
    };

    struct RGTextureDesc
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Depth = 1;
        uint32_t MipLevels = 1;
        uint32_t ArraySize = 1;
        RHI::Format Format = RHI::Format::R8G8B8A8_UNORM;
        RHI::ResourceUsage Usage = RHI::ResourceUsage::ShaderRead;
        RHI::TextureDimension Dimension = RHI::TextureDimension::Texture2D;
        bool bIsCubemap = false;
        const char* DebugName = nullptr;

        static RGTextureDesc RenderTarget(uint32_t width,
                                          uint32_t height,
                                          RHI::Format format,
                                          const char* debugName = nullptr);

        static RGTextureDesc DepthStencil(uint32_t width,
                                          uint32_t height,
                                          RHI::Format format = RHI::Format::D24_UNORM_S8_UINT,
                                          const char* debugName = nullptr);

        RHI::TextureDesc ToRHI() const;
    };

    struct RGBufferDesc
    {
        uint64_t Size = 0;
        RHI::ResourceUsage Usage = RHI::ResourceUsage::None;
        bool bCPUAccessible = false;
        const char* DebugName = nullptr;

        RHI::BufferDesc ToRHI() const;
    };

    struct RGCompiledBarrier
    {
        RGBarrierKind Kind = RGBarrierKind::Texture;
        RGResourceHandle Resource;
        RHI::ResourceState BeforeState = RHI::ResourceState::Undefined;
        RHI::ResourceState AfterState = RHI::ResourceState::Undefined;
        uint32_t PassIndex = RGInvalidPassIndex;
        uint32_t CompiledOrderIndex = RGInvalidPassIndex;
        uint32_t MipLevel = 0;
        uint32_t ArrayIndex = 0;
        uint32_t MipCount = 0;
        uint32_t ArrayCount = 0;
        uint64_t BufferOffset = 0;
        uint64_t BufferSize = 0;
    };

} // namespace NorvesLib::Core::Rendering
