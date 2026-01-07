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
        Dedicated,  ///< 専用メモリ（永続リソース向け）
        Transient,  ///< 一時メモリ（フレーム内再利用可能）
        Aliased     ///< エイリアス可能（RenderGraph向け）
    };

    /**
     * @brief バッファ作成記述子
     */
    struct BufferDesc
    {
        uint64_t Size = 0;                              ///< バッファサイズ（バイト）
        ResourceUsage Usage = ResourceUsage::None;      ///< 使用用途
        bool CPUAccessible = false;                     ///< CPUからアクセス可能か
        const char* DebugName = nullptr;                ///< デバッグ用名前

        BufferDesc() = default;

        BufferDesc(uint64_t size, ResourceUsage usage, bool cpuAccessible = false, const char* debugName = nullptr)
            : Size(size)
            , Usage(usage)
            , CPUAccessible(cpuAccessible)
            , DebugName(debugName)
        {
        }
    };

    /**
     * @brief テクスチャ作成記述子
     */
    struct TextureDesc
    {
        uint32_t Width = 1;                             ///< 幅
        uint32_t Height = 1;                            ///< 高さ
        uint32_t Depth = 1;                             ///< 深さ（3Dテクスチャ用）
        uint32_t MipLevels = 1;                         ///< ミップレベル数
        uint32_t ArraySize = 1;                         ///< 配列サイズ
        Format TextureFormat = Format::R8G8B8A8_UNORM;  ///< フォーマット
        ResourceUsage Usage = ResourceUsage::ShaderRead; ///< 使用用途
        bool IsCubemap = false;                         ///< キューブマップか
        const char* DebugName = nullptr;                ///< デバッグ用名前

        TextureDesc() = default;

        static TextureDesc RenderTarget(uint32_t width, uint32_t height, Format textureFormat, const char* name = nullptr)
        {
            TextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.TextureFormat = textureFormat;
            desc.Usage = ResourceUsage::RenderTarget | ResourceUsage::ShaderRead;
            desc.DebugName = name;
            return desc;
        }

        static TextureDesc DepthStencil(uint32_t width, uint32_t height, Format textureFormat = Format::D24_UNORM_S8_UINT, const char* name = nullptr)
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
     * @brief バッファ確保結果
     */
    struct BufferAllocation
    {
        IBuffer* Buffer = nullptr;                      ///< バッファ
        uint64_t Offset = 0;                            ///< メモリ内オフセット
        uint64_t Size = 0;                              ///< サイズ
        AllocationType Type = AllocationType::Dedicated; ///< 確保タイプ

        bool IsValid() const { return Buffer != nullptr; }
    };

    /**
     * @brief テクスチャ確保結果
     */
    struct TextureAllocation
    {
        ITexture* Texture = nullptr;                    ///< テクスチャ
        uint64_t Size = 0;                              ///< メモリサイズ
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
        virtual BufferAllocation AllocateBuffer(const BufferDesc& desc, AllocationType type = AllocationType::Dedicated) = 0;

        /**
         * @brief バッファを解放します
         * @param allocation 解放するバッファ
         */
        virtual void FreeBuffer(BufferAllocation& allocation) = 0;

        // ========================================
        // テクスチャ操作
        // ========================================

        /**
         * @brief テクスチャを確保します
         * @param desc テクスチャ記述子
         * @param type 確保タイプ
         * @return テクスチャ確保結果
         */
        virtual TextureAllocation AllocateTexture(const TextureDesc& desc, AllocationType type = AllocationType::Dedicated) = 0;

        /**
         * @brief テクスチャを解放します
         * @param allocation 解放するテクスチャ
         */
        virtual void FreeTexture(TextureAllocation& allocation) = 0;

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
