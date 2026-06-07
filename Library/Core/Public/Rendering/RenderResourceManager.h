#pragma once

#include "RenderTypes.h"
#include "GpuResourceTypes.h"
#include "TextureAssetTypes.h"
#include "VertexLayout.h"
#include "MaterialTypes.h"
#include "NeuralMaterialResource.h"
#include "MegaGeometry/MegaGeometryTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Mutex.h"
#include "Thread/Atomic.h"
#include "Thread/Task.h"
#include "Delegate/Delegate.h"
#include <cstddef>
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IDevice;
    class IBuffer;
    class ITexture;
    class ISampler;
    class IShader;
    class IPipeline;
    class IRenderPass;
    class IFramebuffer;
}

namespace NorvesLib::Core::Rendering
{
    class GpuResourceStore;
    class TextureAsyncLoadQueue;
    class TextureAssetResolver;

    /**
     * @brief マテリアル作成情報
     */
    struct MaterialCreateData
    {
        TextureHandle AlbedoTexture;
        TextureHandle NormalTexture;
        TextureHandle MetallicTexture;
        TextureHandle RoughnessTexture;
        TextureHandle AOTexture;
        TextureHandle HeightTexture; ///< ディスプレイスメントマップ（POM用）

        float HeightScale = 0.05f; ///< POMの高さスケール（0.0～0.1程度が自然）

        float EmissiveColor[3] = {0.0f, 0.0f, 0.0f};
        float EmissiveStrength = 0.0f;

        BlendMode Blend = BlendMode::Opaque;
        ShadingModel Shading = ShadingModel::DefaultLit;
        bool bTwoSided = false;
        bool bCastShadows = true;

        Container::String DebugName;
    };

    /**
     * @brief マテリアルリソースデータ（内部用）
     */
    struct MaterialResourceData
    {
        TextureHandle AlbedoTexture;
        TextureHandle NormalTexture;
        TextureHandle MetallicTexture;
        TextureHandle RoughnessTexture;
        TextureHandle AOTexture;
        TextureHandle HeightTexture; ///< ディスプレイスメントマップ（POM用）

        float HeightScale = 0.05f; ///< POMの高さスケール

        float EmissiveColor[3] = {0.0f, 0.0f, 0.0f};
        float EmissiveStrength = 0.0f;

        BlendMode Blend = BlendMode::Opaque;
        ShadingModel Shading = ShadingModel::DefaultLit;
        bool bTwoSided = false;
        bool bCastShadows = true;

        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief モデルリソースデータ（内部用）
     */
    struct ModelResourceData
    {
        MegaGeometry::MegaMeshHandle MegaMesh;
        Container::String DebugName;
        Container::String SourcePath;
    };

    // ========================================
    // RenderResourceManager
    // ========================================

    /**
     * @brief レンダリングリソースマネージャー
     *
     * RHIリソースのライフサイクルを管理し、
     * ハンドルベースのアクセスを提供します。
     *
     * Game側からはこのクラスを通じてのみ
     * GPUリソースを作成・参照できます。
     */
    class RenderResourceManager
    {
    public:
        /**
         * @brief コンストラクタ
         */
        RenderResourceManager();

        /**
         * @brief デストラクタ
         */
        ~RenderResourceManager();

        /**
         * @brief 初期化
         * @param device RHIデバイス
         * @return 初期化成功時true
         */
        bool Initialize(Container::TSharedPtr<RHI::IDevice> device);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief 初期化済みかどうか
         */
        bool IsInitialized() const { return m_bInitialized; }

        // ========================================
        // バッファ操作
        // ========================================

        /**
         * @brief バッファを作成
         * @param createInfo 作成情報
         * @return バッファハンドル
         */
        BufferHandle CreateBuffer(const BufferCreateInfo &createInfo);

        /**
         * @brief バッファを作成してデータを転送
         * @param createInfo 作成情報
         * @param data 初期データ
         * @param dataSize データサイズ
         * @return バッファハンドル
         */
        BufferHandle CreateBuffer(const BufferCreateInfo &createInfo,
                                  const void *data, size_t dataSize);

        /**
         * @brief バッファデータを更新
         * @param handle バッファハンドル
         * @param data データ
         * @param dataSize データサイズ
         * @param offset バッファ内オフセット
         * @return 更新成功時true
         */
        bool UpdateBuffer(BufferHandle handle, const void *data,
                          size_t dataSize, size_t offset = 0);

        /**
         * @brief バッファを解放
         * @param handle バッファハンドル
         */
        void ReleaseBuffer(BufferHandle handle);

        // ========================================
        // テクスチャ操作
        // ========================================

        /**
         * @brief テクスチャを作成
         * @param createInfo 作成情報
         * @return テクスチャハンドル
         */
        TextureHandle CreateTexture(const TextureCreateInfo &createInfo);

        /**
         * @brief テクスチャを作成してデータを転送
         * @param createInfo 作成情報
         * @param data ピクセルデータ
         * @param dataSize データサイズ
         * @return テクスチャハンドル
         */
        TextureHandle CreateTexture(const TextureCreateInfo &createInfo,
                                    const void *data, size_t dataSize);

        /**
         * @brief ファイルからテクスチャをロード（同期）
         * @param path ファイルパス
         * @return テクスチャハンドル
         */
        TextureHandle LoadTexture(const Container::String &path);

        bool SetTextureAssetRoot(const Container::String &assetRoot);
        bool LoadTextureAssetManifestFromJsonText(const Container::String &jsonText,
                                                  const Container::String &sourceName = Container::String());
        bool ResetTextureAssetManifest();
        bool SetTextureAssetFallbackMode(TextureAssetFallbackMode mode);

        [[nodiscard]] PreparedTextureAsset PrepareTextureAssetForWorker(
            const Container::String &requestPath,
            const Container::String &resolvedFallbackPath = {},
            const char *role = "worker",
            uint32_t requestId = 0);
        [[nodiscard]] bool IsPreparedTextureAssetCurrent(const PreparedTextureAsset &prepared) const;
        [[nodiscard]] TextureHandle FinalizePreparedTextureAsset(
            const PreparedTextureAsset &prepared,
            const char *role = "main_render",
            uint32_t requestId = 0);
        [[nodiscard]] bool TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
            const PreparedTextureAsset &prepared,
            PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
            Container::String *pOutReason = nullptr,
            const char *role = "worker",
            uint32_t requestId = 0) const;

        // ========================================
        // 非同期テクスチャ読み込み
        // ========================================

        /**
         * @brief 非同期テクスチャ読み込みの完了データ
         */
        struct AsyncTextureResult
        {
            Container::String Path;                      ///< リクエストパス
            Container::String ResolvedPath;              ///< 解決済みパス
            Container::String CacheKey;                  ///< キャッシュ/ペンディングキー
            Container::AnsiString LogicalPath;           ///< 正規化済み論理パス
            TextureCreateInfo CreateInfo;                ///< テクスチャ作成情報
            Container::VariableArray<uint8_t> PixelData; ///< デコード済みピクセルデータ
            Container::TSharedPtr<CookedTextureAsyncPayload> CookedTexture; ///< cooked texture payload
            TextureLoadSource Source = TextureLoadSource::LegacyFile;       ///< 読み込み元
            TextureAssetFallbackMode FallbackMode = TextureAssetFallbackMode::FailOnCookedFailure; ///< cooked failure fallback
            uint64_t AssetGeneration = 0;                ///< asset設定世代
            bool bSuccess = false;                       ///< 読み込み成功フラグ
        };

        /**
         * @brief 非同期テクスチャ読み込みリクエスト
         */
        struct AsyncTextureRequest
        {
            uint32_t RequestId = 0;                                  ///< リクエストID
            Container::String Path;                                  ///< テクスチャパス
            Container::String CacheKey;                              ///< キャッシュ/ペンディングキー
            Thread::TaskPtr Task;                                    ///< ジョブシステムタスク
            AsyncTextureResult Result;                               ///< 読み込み結果
            Container::VariableArray<NorvesLib::Core::Delegate<void, TextureHandle>> Callbacks; ///< 完了コールバック群
        };

        /**
         * @brief ファイルからテクスチャを非同期ロード
         *
         * ファイルI/O+デコードをワーカースレッドで実行し、
         * FlushCompletedTextureLoads()呼び出し時にGPUアップロード+コールバック実行を行います。
         *
         * @param path ファイルパス
         * @param callback テクスチャ作成完了時に呼び出されるコールバック
         * @return リクエストID（0は失敗）
         */
        uint32_t LoadTextureAsync(const Container::String &path,
                                  NorvesLib::Core::Delegate<void, TextureHandle> callback = {});

        /**
         * @brief 完了した非同期テクスチャ読み込みを処理する
         *
         * メインスレッドから毎フレーム呼び出してください。
         * 完了したファイルI/OのGPUアップロードとコールバック実行を行います。
         *
         * @return 今回処理したテクスチャ数
         */
        uint32_t FlushCompletedTextureLoads();

        /**
         * @brief 非同期ロードのペンディング数を取得
         * @return 未完了のリクエスト数
         */
        uint32_t GetPendingAsyncLoadCount() const;

        /**
         * @brief 外部で作成済みのRHIテクスチャをハンドルとして登録
         *
         * NeuralMaterialResource等が独自に作成したテクスチャを
         * ハンドルシステムに統合し、GBufferPass等から参照可能にします。
         *
         * @param rhiTexture 登録するRHIテクスチャ
         * @param debugName デバッグ名
         * @return テクスチャハンドル
         */
        TextureHandle RegisterExternalTexture(
            Container::TSharedPtr<RHI::ITexture> rhiTexture,
            const Container::String &debugName = "");

        /**
         * @brief テクスチャを解放
         * @param handle テクスチャハンドル
         */
        void ReleaseTexture(TextureHandle handle);

        // ========================================
        // サンプラー操作
        // ========================================

        /**
         * @brief デフォルトサンプラーを取得（Linear, Wrap）
         */
        SamplerHandle GetDefaultSampler();

        /**
         * @brief ポイントサンプラーを取得
         */
        SamplerHandle GetPointSampler();

        /**
         * @brief サンプラーを解放
         * @param handle サンプラーハンドル
         */
        void ReleaseSampler(SamplerHandle handle);

        // ========================================
        // シェーダー操作
        // ========================================

        /**
         * @brief シェーダーを作成
         * @param createInfo 作成情報
         * @return シェーダーハンドル
         */
        ShaderHandle CreateShader(const ShaderCreateInfo &createInfo);

        /**
         * @brief ファイルからシェーダーをロード
         * @param path ファイルパス
         * @param stage シェーダーステージ
         * @return シェーダーハンドル
         */
        ShaderHandle LoadShader(const Container::String &path, ShaderStage stage);

        /**
         * @brief シェーダーを解放
         * @param handle シェーダーハンドル
         */
        void ReleaseShader(ShaderHandle handle);

        // ========================================
        // 頂点レイアウト操作
        // ========================================

        /**
         * @brief 頂点レイアウトを登録
         * @param layout レイアウト定義
         * @return レイアウトハンドル
         */
        VertexLayoutHandle RegisterVertexLayout(const VertexLayout &layout);

        /**
         * @brief 頂点レイアウトを取得
         * @param handle レイアウトハンドル
         * @return レイアウト定義へのポインタ（存在しない場合nullptr）
         */
        const VertexLayout *GetVertexLayout(VertexLayoutHandle handle) const;

        // ========================================
        // メッシュ操作
        // ========================================

        /**
         * @brief メッシュのGPUデータ（VB/IB/インデックス数）
         */
        struct MeshGPUData
        {
            Container::TSharedPtr<RHI::IBuffer> VertexBuffer;
            Container::TSharedPtr<RHI::IBuffer> IndexBuffer;
            uint32_t IndexCount = 0;
        };

        /**
         * @brief プロシージャルメッシュをGPUに登録
         * @param handle メッシュデータハンドル（呼び出し側でIDを決定）
         * @param vertices 頂点データ
         * @param vertexSize 頂点データの総バイト数
         * @param indices インデックスデータ
         * @param indexCount インデックス数
         * @return 登録成功時true
         */
        bool RegisterMesh(MeshDataHandle handle,
                          const void *vertices, size_t vertexSize,
                          const uint32_t *indices, uint32_t indexCount);

        /**
         * @brief メッシュのGPUデータを取得
         * @param handle メッシュデータハンドル
         * @return GPUデータへのポインタ（未登録時nullptr）
         */
        const MeshGPUData *GetMeshGPUData(MeshDataHandle handle) const;

        /**
         * @brief メッシュを解除
         * @param handle メッシュデータハンドル
         */
        void UnregisterMesh(MeshDataHandle handle);

        // ========================================
        // マテリアル操作
        // ========================================

        /**
         * @brief マテリアルを作成
         * @param createInfo マテリアル作成情報
         * @return マテリアルハンドル
         */
        MaterialHandle CreateMaterial(const MaterialCreateData &createInfo);

        /**
         * @brief マテリアルデータを取得
         * @param handle マテリアルハンドル
         * @return マテリアルデータへのポインタ（未登録時nullptr）
         */
        const MaterialResourceData *GetMaterialData(MaterialHandle handle) const;

        /**
         * @brief マテリアルデータを更新
         * @param handle マテリアルハンドル
         * @param createInfo 新しいマテリアルデータ
         * @return 更新成功時true
         */
        bool UpdateMaterial(MaterialHandle handle, const MaterialCreateData &createInfo);

        /**
         * @brief マテリアルを解放
         * @param handle マテリアルハンドル
         */
        void ReleaseMaterial(MaterialHandle handle);

        // ========================================
        // ニューラルマテリアル操作
        // ========================================

        /**
         * @brief ニューラルマテリアルを作成
         *
         * NeuralMaterialResourceの生成・初期化・出力テクスチャ登録・マテリアル作成を
         * 一括で行い、MaterialHandleを返します。
         * GPUリソースのライフタイムはRenderResourceManagerが管理するため、
         * シーン側での明示的な解放は不要です（ClearAllResourcesで自動解放）。
         *
         * @param desc ニューラルマテリアル記述
         * @return マテリアルハンドル（失敗時Invalid）
         */
        MaterialHandle CreateNeuralMaterial(const NeuralMaterialDesc &desc);

        /**
         * @brief 登録済みニューラルマテリアルリソース一覧を取得（Rendering内部用）
         *
         * NeuralMaterialDecodePassのSetup()からプルモデルで呼び出されます。
         *
         * @return ニューラルマテリアルリソースのポインタ配列
         */
        Container::VariableArray<NeuralMaterialResource *> GetNeuralMaterialResources() const;

        // ========================================
        // MegaGeometry操作
        // ========================================

        /**
         * @brief MegaMeshを作成
         *
         * クラスタ化済みのメッシュデータからGPUバッファ（VB/IB/ClusterSSBO）を作成し、
         * MegaMeshHandleを返します。
         *
         * @param createInfo MegaMesh作成情報
         * @return MegaMeshハンドル（失敗時Invalid）
         */
        MegaGeometry::MegaMeshHandle CreateMegaMesh(const MegaGeometry::MegaMeshCreateInfo &createInfo);

        /**
         * @brief MegaMeshのGPUデータを取得
         * @param handle MegaMeshハンドル
         * @return GPUデータへのポインタ（無効な場合nullptr）
         */
        const MegaGeometry::MegaMeshGPUData *GetMegaMeshGPUData(MegaGeometry::MegaMeshHandle handle) const;

        /**
         * @brief MegaMeshを解放
         * @param handle MegaMeshハンドル
         */
        void ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle);

        // ========================================
        // モデル操作
        // ========================================

        /**
         * @brief モデルリソースを登録
         * @param megaMeshHandle モデルに対応するMegaMeshハンドル
         * @param debugName デバッグ名
         * @param sourcePath ソースファイルパス
         * @return モデルハンドル
         */
        ModelHandle RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                  const Container::String &debugName = "",
                                  const Container::String &sourcePath = "");

        /**
         * @brief モデルに対応するMegaMeshハンドルを取得
         * @param handle モデルハンドル
         * @return MegaMeshハンドル（存在しない場合Invalid）
         */
        MegaGeometry::MegaMeshHandle GetModelMegaMeshHandle(ModelHandle handle) const;

        /**
         * @brief モデルリソースを解放
         * @param handle モデルハンドル
         */
        void ReleaseModel(ModelHandle handle);

        // ========================================
        // 内部リソースアクセス（Rendering内部用）
        // ========================================

        /**
         * @brief RHIバッファを取得（Rendering内部用）
         * @param handle バッファハンドル
         * @return RHIバッファポインタ
         */
        RHI::IBuffer *GetRHIBuffer(BufferHandle handle) const;

        /**
         * @brief RHIテクスチャを取得（Rendering内部用）
         * @param handle テクスチャハンドル
         * @return RHIテクスチャポインタ
         */
        RHI::ITexture *GetRHITexture(TextureHandle handle) const;

        /**
         * @brief RHIテクスチャの共有ポインタを取得（DescriptorSetバインド用）
         * @param handle テクスチャハンドル
         * @return RHIテクスチャ共有ポインタ
         */
        Container::TSharedPtr<RHI::ITexture> GetRHITexturePtr(TextureHandle handle) const;

        /**
         * @brief RHIシェーダーを取得（Rendering内部用）
         * @param handle シェーダーハンドル
         * @return RHIシェーダーポインタ
         */
        RHI::IShader *GetRHIShader(ShaderHandle handle) const;

        // ========================================
        // リソース管理
        // ========================================

        /**
         * @brief 未使用リソースをクリーンアップ
         */
        void CleanupUnusedResources();

        /**
         * @brief 全リソースをクリア
         */
        void ClearAllResources();

        /**
         * @brief リソース統計を取得
         */
        using ResourceStats = ::NorvesLib::Core::Rendering::ResourceStats;
        ResourceStats GetResourceStats() const;

    private:
        // コピー・ムーブ禁止
        RenderResourceManager(const RenderResourceManager &) = delete;
        RenderResourceManager &operator=(const RenderResourceManager &) = delete;

        // ハンドルID生成
        template <typename HandleType>
        HandleType AllocateHandle()
        {
            HandleType handle;
            handle.Id = m_NextHandleId.FetchAdd(1, std::memory_order_relaxed);
            return handle;
        }

        // ハンドルID生成用
        Thread::Atomic<uint64_t> m_NextHandleId{1};

        // RHIデバイス
        Container::TSharedPtr<RHI::IDevice> m_Device;

        // 低レベルGPUリソース
        Container::TUniquePtr<GpuResourceStore> m_GpuResources;

        // テクスチャキャッシュ（パス→ハンドル）
        Container::Map<Container::String, TextureHandle> m_TextureCache;

        // シェーダーキャッシュ（パス→ハンドル）
        Container::Map<Container::String, ShaderHandle> m_ShaderCache;

        // メッシュGPUデータマップ（MeshDataHandle::Id → GPUバッファ）
        Container::Map<uint64_t, MeshGPUData> m_MeshGPUDataMap;

        // マテリアルデータマップ（MaterialHandle::Id → マテリアル情報）
        Container::Map<uint64_t, MaterialResourceData> m_Materials;

        // ニューラルマテリアルリソース（MaterialHandle::Id → NeuralMaterialResource）
        Container::Map<uint64_t, Container::TSharedPtr<NeuralMaterialResource>> m_NeuralMaterials;

        // MegaMesh GPUデータマップ（MegaMeshHandle::Id → GPUデータ）
        Container::Map<uint64_t, MegaGeometry::MegaMeshGPUData> m_MegaMeshGPUDataMap;

        // モデルデータマップ（ModelHandle::Id → モデル情報）
        Container::Map<uint64_t, ModelResourceData> m_Models;

        // スレッドセーフ用ミューテックス
        mutable Thread::Mutex m_ResourceMutex;

        // 非同期テクスチャ読み込みキュー
        Container::TUniquePtr<TextureAsyncLoadQueue> m_TextureAsyncLoads;

        // Texture asset resolution state. Lock order when nested: texture asset -> async queue -> resource.
        Container::TUniquePtr<TextureAssetResolver> m_TextureAssetResolver;
        mutable Thread::Mutex m_TextureAssetMutex;

        TextureAssetResolver &GetTextureAssetResolverLocked();
        TextureHandle RegisterUploadedTexture(Container::TSharedPtr<RHI::ITexture> rhiTexture,
                                              const TextureCreateInfo &createInfo);

        // 初期化フラグ
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
