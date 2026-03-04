#pragma once

#include "RenderTypes.h"
#include "VertexLayout.h"
#include "MaterialTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Mutex.h"
#include "Thread/Atomic.h"
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

    // ========================================
    // リソース作成情報
    // ========================================

    /**
     * @brief バッファ作成情報
     */
    struct BufferCreateInfo
    {
        size_t Size = 0;
        bool bHostVisible = false; // CPUからアクセス可能か

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
     * @brief テクスチャ作成情報
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
     * @brief シェーダー作成情報
     */
    struct ShaderCreateInfo
    {
        ShaderStage Stage = ShaderStage::Vertex;
        Container::String EntryPoint = "main";
        Container::VariableArray<uint8_t> ByteCode;
        Container::String DebugName;
    };

    // ========================================
    // 内部リソースデータ
    // ========================================

    /**
     * @brief バッファリソースデータ（内部用）
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
     * @brief テクスチャリソースデータ（内部用）
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
     * @brief サンプラーリソースデータ（内部用）
     */
    struct SamplerResourceData
    {
        Container::TSharedPtr<RHI::ISampler> RHISampler;
        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief シェーダーリソースデータ（内部用）
     */
    struct ShaderResourceData
    {
        Container::TSharedPtr<RHI::IShader> RHIShader;
        ShaderStage Stage;
        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief パイプラインリソースデータ（内部用）
     */
    struct PipelineResourceData
    {
        Container::TSharedPtr<RHI::IPipeline> RHIPipeline;
        uint32_t RefCount = 0;
        Container::String DebugName;
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
        RenderResourceManager() = default;

        /**
         * @brief デストラクタ
         */
        ~RenderResourceManager() = default;

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
         * @brief ファイルからテクスチャをロード
         * @param path ファイルパス
         * @return テクスチャハンドル
         */
        TextureHandle LoadTexture(const Container::String &path);

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
        struct ResourceStats
        {
            uint32_t BufferCount = 0;
            uint32_t TextureCount = 0;
            uint32_t ShaderCount = 0;
            uint32_t SamplerCount = 0;
            size_t TotalBufferMemory = 0;
            size_t TotalTextureMemory = 0;
        };
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

        // RHIデバイス
        Container::TSharedPtr<RHI::IDevice> m_Device;

        // リソースマップ
        Container::Map<uint64_t, BufferResourceData> m_Buffers;
        Container::Map<uint64_t, TextureResourceData> m_Textures;
        Container::Map<uint64_t, SamplerResourceData> m_Samplers;
        Container::Map<uint64_t, ShaderResourceData> m_Shaders;
        Container::Map<uint64_t, PipelineResourceData> m_Pipelines;
        Container::Map<uint64_t, VertexLayout> m_VertexLayouts;

        // テクスチャキャッシュ（パス→ハンドル）
        Container::Map<Container::String, TextureHandle> m_TextureCache;

        // シェーダーキャッシュ（パス→ハンドル）
        Container::Map<Container::String, ShaderHandle> m_ShaderCache;

        // メッシュGPUデータマップ（MeshDataHandle::Id → GPUバッファ）
        Container::Map<uint64_t, MeshGPUData> m_MeshGPUDataMap;

        // デフォルトサンプラー
        SamplerHandle m_DefaultSampler;
        SamplerHandle m_PointSampler;

        // スレッドセーフ用ミューテックス
        mutable Thread::Mutex m_ResourceMutex;

        // ハンドルID生成用
        Thread::Atomic<uint64_t> m_NextHandleId{1};

        // 初期化フラグ
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
