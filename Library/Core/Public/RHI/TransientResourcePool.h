#pragma once

#include "IGPUResourceAllocator.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::RHI
{

    /**
     * @brief レンダーターゲットキー（プール検索用）
     */
    struct RenderTargetKey
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        Format Format = Format::UNKNOWN;

        bool operator==(const RenderTargetKey& other) const
        {
            return Width == other.Width && Height == other.Height && Format == other.Format;
        }

        bool operator<(const RenderTargetKey& other) const
        {
            if (Width != other.Width) return Width < other.Width;
            if (Height != other.Height) return Height < other.Height;
            return static_cast<int>(Format) < static_cast<int>(other.Format);
        }
    };

    /**
     * @brief フレーム内一時リソースのプール
     *
     * レンダーターゲット、中間バッファなど
     * フレーム内で再利用可能なリソースを管理します。
     *
     * 責務:
     * - 一時リソースのプール管理
     * - サイズ/フォーマット別のリソース再利用
     * - フレーム終了時のリソース返却
     * - 将来のResourceProducerの基盤
     *
     * 使用例:
     * ```cpp
     * pool->BeginFrame(frameIndex);
     * 
     * auto* rt = pool->AcquireRenderTarget(1920, 1080, Format::R8G8B8A8_UNORM, "GBuffer_Albedo");
     * // レンダリング...
     * 
     * pool->EndFrame();  // 使用中リソースがプールに返却される
     * ```
     */
    class TransientResourcePool
    {
    public:
        /**
         * @brief コンストラクタ
         */
        TransientResourcePool() = default;

        /**
         * @brief デストラクタ
         */
        ~TransientResourcePool();

        /**
         * @brief 初期化します
         * @param allocator GPUリソースアロケーター
         * @return 初期化成功時true
         */
        bool Initialize(IGPUResourceAllocator* allocator);

        /**
         * @brief 終了処理を行います
         */
        void Shutdown();

        // ========================================
        // フレーム管理
        // ========================================

        /**
         * @brief フレーム開始処理
         * @param frameIndex フレーム番号
         */
        void BeginFrame(uint64_t frameIndex);

        /**
         * @brief フレーム終了処理（使用中リソースを返却）
         */
        void EndFrame();

        // ========================================
        // レンダーターゲット
        // ========================================

        /**
         * @brief レンダーターゲットを取得します（プールから再利用 or 新規作成）
         * @param width 幅
         * @param height 高さ
         * @param format フォーマット
         * @param debugName デバッグ用名前
         * @return レンダーターゲット
         */
        ITexture* AcquireRenderTarget(uint32_t width, uint32_t height, Format format, const char* debugName = nullptr);

        /**
         * @brief デプスステンシルバッファを取得します
         * @param width 幅
         * @param height 高さ
         * @param format フォーマット（デフォルトはD24_UNORM_S8_UINT）
         * @param debugName デバッグ用名前
         * @return デプスステンシルバッファ
         */
        ITexture* AcquireDepthStencil(uint32_t width, uint32_t height, 
                                       Format format = Format::D24_UNORM_S8_UINT, 
                                       const char* debugName = nullptr);

        // ========================================
        // バッファ
        // ========================================

        /**
         * @brief 一時バッファを取得します
         * @param size サイズ（バイト）
         * @param usage 使用用途
         * @param debugName デバッグ用名前
         * @return バッファ
         */
        IBuffer* AcquireBuffer(uint64_t size, ResourceUsage usage, const char* debugName = nullptr);

        // ========================================
        // メモリ管理
        // ========================================

        /**
         * @brief 最大プールメモリを設定します
         * @param bytes 最大サイズ（バイト）
         */
        void SetMaxPoolMemory(size_t bytes) { m_MaxPoolMemory = bytes; }

        /**
         * @brief 余剰リソースを解放します
         */
        void Trim();

        /**
         * @brief すべてのリソースを解放します
         */
        void ReleaseAll();

        // ========================================
        // 統計情報
        // ========================================

        /**
         * @brief プール内のレンダーターゲット数を取得します
         * @return レンダーターゲット数
         */
        size_t GetPooledRenderTargetCount() const;

        /**
         * @brief プール内のバッファ数を取得します
         * @return バッファ数
         */
        size_t GetPooledBufferCount() const;

        /**
         * @brief プールのメモリ使用量を取得します
         * @return メモリ使用量（バイト）
         */
        size_t GetPoolMemoryUsage() const;

    private:
        /**
         * @brief プール内リソース情報
         */
        template<typename T>
        struct PooledResource
        {
            T* Resource = nullptr;
            uint64_t LastUsedFrame = 0;
            size_t Size = 0;
        };

        IGPUResourceAllocator* m_Allocator = nullptr;
        uint64_t m_CurrentFrame = 0;
        size_t m_MaxPoolMemory = 256 * 1024 * 1024;  // 256MB

        // レンダーターゲットプール
        Core::Container::Map<RenderTargetKey, Core::Container::VariableArray<PooledResource<ITexture>>> m_RTPool;

        // バッファプール（サイズ別）
        Core::Container::Map<uint64_t, Core::Container::VariableArray<PooledResource<IBuffer>>> m_BufferPool;

        // 現在のフレームで使用中のリソース
        Core::Container::VariableArray<TextureAllocation> m_UsedRenderTargets;
        Core::Container::VariableArray<BufferAllocation> m_UsedBuffers;

        // 初期化フラグ
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::RHI
