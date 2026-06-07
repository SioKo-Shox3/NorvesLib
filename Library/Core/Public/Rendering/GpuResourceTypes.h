#pragma once

#include "Rendering/MaterialTypes.h"
#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IBuffer;
    class ITexture;
    class ISampler;
    class IShader;
    class IPipeline;
}

namespace NorvesLib::Core::Rendering
{
    // ========================================
    // Resource creation info
    // ========================================

    /**
     * @brief Buffer creation info.
     */
    struct BufferCreateInfo
    {
        size_t Size = 0;
        bool bHostVisible = false; // CPU accessible.

        enum class Usage
        {
            Vertex,
            Index,
            Constant,
            Structured,
            Storage
        } UsageType = Usage::Vertex;

        Container::String DebugName;
    };

    /**
     * @brief Texture creation info.
     */
    struct TextureCreateInfo
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Depth = 1;
        uint32_t MipLevels = 1;
        uint32_t ArraySize = 1;

        enum class Format
        {
            RGBA8_UNORM,
            RGBA8_SRGB,
            RGBA16_FLOAT,
            RGBA32_FLOAT,
            R8_UNORM,
            RG8_UNORM,
            D24_S8,
            D32_FLOAT
        } PixelFormat = Format::RGBA8_UNORM;

        TextureType Type = TextureType::Texture2D;

        bool bRenderTarget = false;
        bool bDepthStencil = false;

        Container::String DebugName;
    };

    /**
     * @brief Shader creation info.
     */
    struct ShaderCreateInfo
    {
        ShaderStage Stage = ShaderStage::Vertex;
        Container::String EntryPoint = "main";
        Container::VariableArray<uint8_t> ByteCode;
        Container::String DebugName;
    };

    // ========================================
    // Internal resource data
    // ========================================

    /**
     * @brief Buffer resource data.
     */
    struct BufferResourceData
    {
        Container::TSharedPtr<RHI::IBuffer> RHIBuffer;
        size_t Size = 0;
        BufferCreateInfo::Usage Usage;
        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief Texture resource data.
     */
    struct TextureResourceData
    {
        Container::TSharedPtr<RHI::ITexture> RHITexture;
        uint32_t Width = 0;
        uint32_t Height = 0;
        TextureCreateInfo::Format Format;
        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief Sampler resource data.
     */
    struct SamplerResourceData
    {
        Container::TSharedPtr<RHI::ISampler> RHISampler;
        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief Shader resource data.
     */
    struct ShaderResourceData
    {
        Container::TSharedPtr<RHI::IShader> RHIShader;
        ShaderStage Stage;
        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief Pipeline resource data.
     */
    struct PipelineResourceData
    {
        Container::TSharedPtr<RHI::IPipeline> RHIPipeline;
        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief Resource stats.
     */
    struct ResourceStats
    {
        uint32_t BufferCount = 0;
        uint32_t TextureCount = 0;
        uint32_t ShaderCount = 0;
        uint32_t SamplerCount = 0;
        size_t TotalBufferMemory = 0;
        size_t TotalTextureMemory = 0;
    };
}
