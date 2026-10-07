#pragma once

#include "RHITypes.h"
#include "IBuffer.h"
#include "ITexture.h"
#include <cstdint>

namespace NorvesLib::RHI
{

    // 前方宣言
    class IDevice;

    /**
     * @brief メモリ確保タイプ
     */
    enum class AllocationType : uint8_t
    {
        Dedicated, ///< 専用メモリ（永続リソース向け）
        Transient, ///< 一時メモリ（フレーム内再利用可能）
        Aliased    ///< エイリアス可能（RenderGraph向け）
    };

    /**
     * @brief バッファ作成記述子
     */
    struct BufferDesc
    {
        uint64_t Size = 0;                         ///< バッファサイズ（バイト）
        ResourceUsage Usage = ResourceUsage::None; ///< 使用用途
        bool CPUAccessible = false;                ///< CPUからアクセス可能か
        bool bExcludeDeviceLocal = false;          ///< DeviceLocal のメモリを選ばない（ステージング用。選べなければ作成に失敗する）
        const char *DebugName = nullptr;           ///< デバッグ用名前

        BufferDesc() = default;

        BufferDesc(uint64_t size, ResourceUsage usage, bool cpuAccessible = false, const char *debugName = nullptr)
            : Size(size), Usage(usage), CPUAccessible(cpuAccessible), DebugName(debugName)
        {
        }
    };

    /**
     * @brief テクスチャ作成記述子
     */
    struct TextureDesc
    {
        uint32_t Width = 1;                                       ///< 幅
        uint32_t Height = 1;                                      ///< 高さ
        uint32_t Depth = 1;                                       ///< 深さ（3Dテクスチャ用）
        uint32_t MipLevels = 1;                                   ///< ミップレベル数
        uint32_t ArraySize = 1;                                   ///< 配列サイズ
        Format TextureFormat = Format::R8G8B8A8_UNORM;            ///< フォーマット
        ResourceUsage Usage = ResourceUsage::ShaderRead;          ///< 使用用途
        TextureDimension Dimension = TextureDimension::Texture2D; ///< テクスチャ次元
        bool IsCubemap = false;                                   ///< キューブマップか
        bool bSparse = false;                                     ///< sparse（物理メモリを結ばずに作り、タイルごとに結ぶ）か
        const char *DebugName = nullptr;                          ///< デバッグ用名前

        TextureDesc() = default;

        static TextureDesc RenderTarget(uint32_t width, uint32_t height, Format textureFormat, const char *name = nullptr)
        {
            TextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.TextureFormat = textureFormat;
            desc.Usage = ResourceUsage::RenderTarget | ResourceUsage::ShaderRead;
            desc.DebugName = name;
            return desc;
        }

        static TextureDesc DepthStencil(uint32_t width, uint32_t height, Format textureFormat = Format::D24_UNORM_S8_UINT, const char *name = nullptr)
        {
            TextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.TextureFormat = textureFormat;
            desc.Usage = ResourceUsage::DepthStencil | ResourceUsage::ShaderRead;
            desc.DebugName = name;
            return desc;
        }
    };

    /**
     * @brief フォーマットの1画素あたりのバイト数を返す
     * @note ブロック圧縮フォーマットは1画素のバイト数で表せないので 0 を返す（GetFormatBlockInfo を使う）。
     *       未知のフォーマットは 4 を返す。
     */
    inline size_t GetFormatBytesPerPixel(Format format)
    {
        switch (format)
        {
        case Format::R8_UNORM:
            return 1;
        case Format::R8G8_UNORM:
            return 2;
        case Format::R8G8B8A8_UNORM:
        case Format::R8G8B8A8_SRGB:
        case Format::B8G8R8A8_UNORM:
        case Format::B8G8R8A8_SRGB:
        case Format::R32_FLOAT:
        case Format::R32_UINT:
        case Format::D24_UNORM_S8_UINT:
        case Format::D32_FLOAT:
            return 4;
        case Format::R16_FLOAT:
        case Format::R16_UNORM:
        case Format::D16_UNORM:
            return 2;
        case Format::R16G16_FLOAT:
            return 4;
        case Format::R16G16B16A16_FLOAT:
            return 8;
        case Format::R32G32_FLOAT:
        case Format::R32G32_UINT:
            return 8;
        case Format::R32G32B32_FLOAT:
            return 12;
        case Format::R32G32B32A32_FLOAT:
            return 16;
        case Format::BC1_UNORM:
        case Format::BC1_SRGB:
        case Format::BC4_UNORM:
        case Format::BC5_UNORM:
        case Format::BC7_UNORM:
        case Format::BC7_SRGB:
            return 0;
        default:
            return 4;
        }
    }

    /**
     * @brief 形式の1ブロックの大きさ
     *
     * 非圧縮の形式は 1x1 画素を1ブロックとして扱う（BlockBytes は1画素のバイト数）。
     */
    struct FormatBlockInfo
    {
        uint32_t BlockWidth = 1;  ///< ブロックの幅（画素）
        uint32_t BlockHeight = 1; ///< ブロックの高さ（画素）
        uint32_t BlockBytes = 0;  ///< 1ブロックのバイト数
    };

    /**
     * @brief 形式の1ブロックの幅・高さ・バイト数を返す
     * @note BC1/BC4 は 4x4 画素で 8 バイト、BC5/BC7 は 4x4 画素で 16 バイト。
     */
    inline FormatBlockInfo GetFormatBlockInfo(Format format)
    {
        switch (format)
        {
        case Format::BC1_UNORM:
        case Format::BC1_SRGB:
        case Format::BC4_UNORM:
            return {4, 4, 8};
        case Format::BC5_UNORM:
        case Format::BC7_UNORM:
        case Format::BC7_SRGB:
            return {4, 4, 16};
        default:
            return {1, 1, static_cast<uint32_t>(GetFormatBytesPerPixel(format))};
        }
    }

    /**
     * @brief テクスチャの確保量（バイト）を、形式の1ブロックのバイト数 × 全ミップのブロック数 × 配列数で見積もる
     * @note 幅・高さ・深さは各ミップで半分（最小 1）にし、ブロック圧縮では各ミップを最小 1 ブロックに切り上げて数える。
     *       実装側のアライメントや余白は含めない。
     */
    inline size_t EstimateTextureSize(const TextureDesc &desc)
    {
        size_t total = 0;
        uint32_t width = desc.Width > 0 ? desc.Width : 1;
        uint32_t height = desc.Height > 0 ? desc.Height : 1;
        uint32_t depth = desc.Depth > 0 ? desc.Depth : 1;
        const uint32_t mipLevels = desc.MipLevels > 0 ? desc.MipLevels : 1;
        const size_t arraySize = desc.ArraySize > 0 ? desc.ArraySize : 1;
        const FormatBlockInfo block = GetFormatBlockInfo(desc.TextureFormat);

        for (uint32_t mipLevel = 0; mipLevel < mipLevels; ++mipLevel)
        {
            const size_t blocksX = (static_cast<size_t>(width) + block.BlockWidth - 1) / block.BlockWidth;
            const size_t blocksY = (static_cast<size_t>(height) + block.BlockHeight - 1) / block.BlockHeight;
            total += blocksX *
                     blocksY *
                     static_cast<size_t>(depth) *
                     arraySize *
                     block.BlockBytes;

            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
            depth = depth > 1 ? depth / 2 : 1;
        }

        return total;
    }

    /**
     * @brief バッファ確保結果
     */
    struct BufferAllocation
    {
        IBuffer *Buffer = nullptr;                       ///< バッファ
        uint64_t Offset = 0;                             ///< メモリ内オフセット
        uint64_t Size = 0;                               ///< サイズ
        AllocationType Type = AllocationType::Dedicated; ///< 確保タイプ

        bool IsValid() const { return Buffer != nullptr; }
    };

    /**
     * @brief テクスチャ確保結果
     */
    struct TextureAllocation
    {
        ITexture *Texture = nullptr;                     ///< テクスチャ
        uint64_t Size = 0;                               ///< メモリサイズ
        AllocationType Type = AllocationType::Dedicated; ///< 確保タイプ

        bool IsValid() const { return Texture != nullptr; }
    };

    /**
     * @brief GPUリソースアロケーターインターフェース
     *
     * GPUメモリ確保の抽象化層。
     * VulkanではVMA、D3D12ではD3D12MAなどのメモリアロケータと連携。
     *
     * 責務:
     * - GPUバッファ/テクスチャの作成
     * - メモリ確保タイプの管理
     * - 将来のRenderGraphでのリソースエイリアシング基盤
     *
     * 使用例:
     * ```cpp
     * BufferDesc desc(1024, ResourceUsage::VertexBuffer, false, "MyVertexBuffer");
     * auto allocation = allocator->AllocateBuffer(desc, AllocationType::Dedicated);
     * if (allocation.IsValid())
     * {
     *     // バッファを使用
     * }
     * allocator->FreeBuffer(allocation);
     * ```
     */
    class IGPUResourceAllocator
    {
    public:
        virtual ~IGPUResourceAllocator() = default;

        // ========================================
        // バッファ操作
        // ========================================

        /**
         * @brief バッファを確保します
         * @param desc バッファ記述子
         * @param type 確保タイプ
         * @return バッファ確保結果
         */
        virtual BufferAllocation AllocateBuffer(const BufferDesc &desc, AllocationType type = AllocationType::Dedicated) = 0;

        /**
         * @brief バッファを解放します
         * @param allocation 解放するバッファ
         */
        virtual void FreeBuffer(BufferAllocation &allocation) = 0;

        // ========================================
        // テクスチャ操作
        // ========================================

        /**
         * @brief テクスチャを確保します
         * @param desc テクスチャ記述子
         * @param type 確保タイプ
         * @return テクスチャ確保結果
         */
        virtual TextureAllocation AllocateTexture(const TextureDesc &desc, AllocationType type = AllocationType::Dedicated) = 0;

        /**
         * @brief テクスチャを解放します
         * @param allocation 解放するテクスチャ
         */
        virtual void FreeTexture(TextureAllocation &allocation) = 0;

        // ========================================
        // 統計情報
        // ========================================

        /**
         * @brief 確保されたメモリ量を取得します
         * @return 確保されたメモリ量（バイト）
         */
        virtual size_t GetAllocatedMemory() const = 0;

        /**
         * @brief 使用中のメモリ量を取得します
         * @return 使用中のメモリ量（バイト）
         */
        virtual size_t GetUsedMemory() const = 0;

        /**
         * @brief 未使用リソースを解放します
         */
        virtual void Trim() = 0;
    };

} // namespace NorvesLib::RHI
