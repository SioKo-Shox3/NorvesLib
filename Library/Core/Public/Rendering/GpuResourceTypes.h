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
            D32_FLOAT,
            R16_UNORM,
            BC1_UNORM,
            BC1_SRGB,
            BC4_UNORM,
            BC5_UNORM,
            BC7_UNORM,
            BC7_SRGB
        } PixelFormat = Format::RGBA8_UNORM;

        /// 初期データが全ミップを含むか。BC 形式は常に全ミップを含む（実行時に縮小できない）。
        /// 含むとき、初期データはミップ0から順に詰めた1つの塊で、ミップごとにアップロードし、ミップ生成はしない。
        /// ミップごとの大きさは各形式のブロック単位（BC は 4x4 画素のブロックを切り上げ）で数える。
        bool bInitialDataHasAllMips = false;

        /// sparse（部分常駐）のテクスチャにするか。物理メモリを結ばずに全ミップを作り、タイルごとに結ぶ。
        /// 対応しない GPU・形式・用途（レンダーターゲット・深度・配列）では作成が失敗する。初期データは渡せない。
        bool bSparse = false;

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
        size_t Bytes = 0; // 形式の1画素のバイト数 × 全ミップの画素数 × 配列数。外部登録は 0（所有しない）。
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
        size_t TextureBytes = 0; // 所有するテクスチャの確保量の合計（TotalTextureMemory と同じ値）
    };
}
