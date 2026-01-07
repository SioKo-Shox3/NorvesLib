#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Mutex.h"
#include "Rendering/RenderTypes.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IGPUResourceAllocator;
    class IBuffer;
    class ITexture;
}

namespace NorvesLib::Core
{
    class MeshResource;
    // 将来: class TextureResource;
}

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief キャッシュ用メッシュGPUデータ
     *
     * PersistentResourceCache内部で使用されるGPUリソースのキャッシュ構造体。
     * MeshTypes.hのMeshGPUDataとは異なり、直接RHIポインタを保持します。
     */
    struct CachedMeshGPUData
    {
        RHI::IBuffer *VertexBuffer = nullptr; ///< 頂点バッファ
        RHI::IBuffer *IndexBuffer = nullptr;  ///< インデックスバッファ
        uint32_t VertexCount = 0;             ///< 頂点数
        uint32_t IndexCount = 0;              ///< インデックス数
        uint32_t VertexStride = 0;            ///< 頂点ストライド
        uint64_t LastUsedFrame = 0;           ///< 最終使用フレーム

        bool IsValid() const { return VertexBuffer != nullptr && IndexBuffer != nullptr; }
    };

    /**
     * @brief キャッシュ用テクスチャGPUデータ
     */
    struct CachedTextureGPUData
    {
        RHI::ITexture *Texture = nullptr; ///< テクスチャ
        uint32_t Width = 0;               ///< 幅
        uint32_t Height = 0;              ///< 高さ
        uint64_t LastUsedFrame = 0;       ///< 最終使用フレーム

        bool IsValid() const { return Texture != nullptr; }
    };

    /**
     * @brief メッシュGPUハンドル
     */
    struct MeshGPUHandle
    {
        uint64_t ResourceId = 0;                 ///< リソースID
        const CachedMeshGPUData *Data = nullptr; ///< GPUデータへのポインタ

        bool IsValid() const { return ResourceId != 0 && Data != nullptr && Data->IsValid(); }
    };

    /**
     * @brief テクスチャGPUハンドル
     */
    struct TextureGPUHandle
    {
        uint64_t ResourceId = 0;                    ///< リソースID
        const CachedTextureGPUData *Data = nullptr; ///< GPUデータへのポインタ

        bool IsValid() const { return ResourceId != 0 && Data != nullptr && Data->IsValid(); }
    };

    /**
     * @brief 永続リソース（Mesh/Texture）のGPUキャッシュ
     *
     * アセットからロードされたリソースのGPU側データを管理します。
     * TransientResourcePoolとは別に、長期間保持するリソース用です。
     *
     * 責務:
     * - MeshResource/TextureResourceからGPUバッファへの変換
     * - ResourceID → GPUハンドルのマッピング
     * - 未使用リソースの解放（フレーム閾値ベース）
     *
     * 使用例:
     * ```cpp
     * auto handle = cache.GetOrUpload(meshResource);
     * if (handle.IsValid())
     * {
     *     // handle.Data->VertexBuffer を使って描画
     * }
     * ```
     */
    class PersistentResourceCache
    {
    public:
        /**
         * @brief コンストラクタ
         */
        PersistentResourceCache() = default;

        /**
         * @brief デストラクタ
         */
        ~PersistentResourceCache();

        /**
         * @brief 初期化します
         * @param allocator GPUリソースアロケーター
         * @return 初期化成功時true
         */
        bool Initialize(RHI::IGPUResourceAllocator *allocator);

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

        // ========================================
        // メッシュ
        // ========================================

        /**
         * @brief メッシュのGPUハンドルを取得します（なければアップロード）
         * @param mesh メッシュリソース
         * @return GPUハンドル
         */
        MeshGPUHandle GetOrUpload(const MeshResource *mesh);

        /**
         * @brief メッシュのGPUデータを解放します
         * @param mesh メッシュリソース
         */
        void Release(const MeshResource *mesh);

        /**
         * @brief リソースIDでメッシュのGPUデータを解放します
         * @param resourceId リソースID
         */
        void ReleaseMesh(uint64_t resourceId);

        // ========================================
        // TODO: テクスチャ（将来実装）
        // ========================================

        // TextureGPUHandle GetOrUpload(const TextureResource* texture);
        // void Release(const TextureResource* texture);

        // ========================================
        // ガベージコレクション
        // ========================================

        /**
         * @brief 未使用リソースを解放します
         * @param frameThreshold この閾値フレーム以上使用されていないリソースを解放
         * @return 解放されたリソース数
         */
        size_t ReleaseUnused(uint32_t frameThreshold = 300);

        /**
         * @brief すべてのリソースを解放します
         */
        void ReleaseAll();

        // ========================================
        // 統計情報
        // ========================================

        /**
         * @brief キャッシュされているメッシュ数を取得します
         * @return メッシュ数
         */
        size_t GetCachedMeshCount() const;

        /**
         * @brief GPU上のメモリ使用量を取得します
         * @return メモリ使用量（バイト）
         */
        size_t GetGPUMemoryUsage() const;

    private:
        /**
         * @brief メッシュをGPUにアップロードします
         * @param mesh メッシュリソース
         * @return GPUデータ
         */
        CachedMeshGPUData UploadMesh(const MeshResource *mesh);

        RHI::IGPUResourceAllocator *m_Allocator = nullptr;
        uint64_t m_CurrentFrame = 0;

        // メッシュキャッシュ（ResourceID → GPUデータ）
        Container::UnorderedMap<uint64_t, CachedMeshGPUData> m_MeshCache;

        // テクスチャキャッシュ（将来用）
        // Container::UnorderedMap<uint64_t, CachedTextureGPUData> m_TextureCache;

        // スレッドセーフ用ミューテックス
        mutable Thread::Mutex m_Mutex;

        // 初期化フラグ
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
