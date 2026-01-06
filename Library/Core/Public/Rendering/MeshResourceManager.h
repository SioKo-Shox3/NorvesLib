#pragma once

#include "RenderTypes.h"
#include "MeshTypes.h"
#include "RenderResourceManager.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Mutex.h"
#include "Thread/Atomic.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    // 前方宣言
    class RenderResourceManager;

    // ========================================
    // Meshアセット（Game側向け）
    // ========================================

    /**
     * @brief メッシュアセット
     *
     * Game側がメッシュを参照するためのクラス。
     * 実際のGPUリソースへの直接アクセスは提供しません。
     *
     * MeshResourceManager経由でのみ作成可能です。
     */
    class Mesh
    {
    public:
        /**
         * @brief メッシュデータハンドルを取得
         */
        MeshDataHandle GetHandle() const { return m_Handle; }

        /**
         * @brief メッシュが有効かどうか
         */
        bool IsValid() const { return m_Handle.IsValid(); }

        /**
         * @brief 頂点数を取得
         */
        uint32_t GetVertexCount() const { return m_VertexCount; }

        /**
         * @brief インデックス数を取得
         */
        uint32_t GetIndexCount() const { return m_IndexCount; }

        /**
         * @brief サブメッシュ数を取得
         */
        uint32_t GetSubMeshCount() const { return static_cast<uint32_t>(m_SubMeshes.size()); }

        /**
         * @brief サブメッシュ情報を取得
         * @param index サブメッシュインデックス
         * @return サブメッシュ情報への参照
         */
        const SubMesh &GetSubMesh(uint32_t index) const { return m_SubMeshes[index]; }

        /**
         * @brief マテリアルスロット数を取得
         */
        uint32_t GetMaterialSlotCount() const { return static_cast<uint32_t>(m_MaterialSlots.size()); }

        /**
         * @brief マテリアルスロットを取得
         * @param index スロットインデックス
         * @return マテリアルスロット情報への参照
         */
        const MaterialSlot &GetMaterialSlot(uint32_t index) const { return m_MaterialSlots[index]; }

        /**
         * @brief スロット名からマテリアルスロットインデックスを取得
         * @param name スロット名
         * @return スロットインデックス（見つからない場合は-1）
         */
        int32_t FindMaterialSlotIndex(const Container::String &name) const
        {
            for (uint32_t i = 0; i < m_MaterialSlots.size(); ++i)
            {
                if (m_MaterialSlots[i].Name == name)
                {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        /**
         * @brief バウンディングボックスを取得
         */
        const BoundingBox &GetBounds() const { return m_Bounds; }

        /**
         * @brief バウンディングスフィアを取得
         */
        const BoundingSphere &GetBoundingSphere() const { return m_BoundingSphere; }

        /**
         * @brief 頂点レイアウトを取得
         */
        const VertexLayout &GetVertexLayout() const { return m_VertexLayout; }

        /**
         * @brief プリミティブトポロジーを取得
         */
        PrimitiveTopology GetTopology() const { return m_Topology; }

        /**
         * @brief メッシュ名を取得
         */
        const Container::String &GetName() const { return m_Name; }

    private:
        friend class MeshResourceManager;

        // MeshResourceManagerからのみ構築可能
        Mesh() = default;

        MeshDataHandle m_Handle;
        uint32_t m_VertexCount = 0;
        uint32_t m_IndexCount = 0;
        Container::VariableArray<SubMesh> m_SubMeshes;
        Container::VariableArray<MaterialSlot> m_MaterialSlots;
        VertexLayout m_VertexLayout;
        PrimitiveTopology m_Topology = PrimitiveTopology::TriangleList;
        BoundingBox m_Bounds;
        BoundingSphere m_BoundingSphere;
        Container::String m_Name;
    };

    // Meshへのスマートポインタ
    using MeshPtr = Container::TSharedPtr<Mesh>;
    using MeshWeakPtr = Container::TWeakPtr<Mesh>;

    // ========================================
    // MeshResourceManager
    // ========================================

    /**
     * @brief メッシュリソースマネージャー
     *
     * メッシュのロード、作成、キャッシュを管理します。
     * Game側からはハンドル経由でのみメッシュデータにアクセスできます。
     */
    class MeshResourceManager
    {
    public:
        /**
         * @brief コンストラクタ
         */
        MeshResourceManager() = default;

        /**
         * @brief デストラクタ
         */
        ~MeshResourceManager() = default;

        /**
         * @brief 初期化
         * @param resourceManager RenderResourceManagerへの参照
         * @return 初期化成功時true
         */
        bool Initialize(RenderResourceManager *resourceManager);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief 初期化済みかどうか
         */
        bool IsInitialized() const { return m_bInitialized; }

        // ========================================
        // メッシュ作成
        // ========================================

        /**
         * @brief メッシュを作成
         * @param createInfo 作成情報
         * @return 作成されたメッシュ
         */
        MeshPtr CreateMesh(const MeshCreateInfo &createInfo);

        /**
         * @brief ファイルからメッシュをロード
         * @param path ファイルパス
         * @return ロードされたメッシュ
         */
        MeshPtr LoadMesh(const Container::String &path);

        // ========================================
        // プリミティブメッシュ作成
        // ========================================

        /**
         * @brief ボックスメッシュを作成
         * @param width 幅
         * @param height 高さ
         * @param depth 奥行き
         * @return 作成されたメッシュ
         */
        MeshPtr CreateBox(float width = 1.0f, float height = 1.0f, float depth = 1.0f);

        /**
         * @brief 球メッシュを作成
         * @param radius 半径
         * @param segments セグメント数
         * @param rings リング数
         * @return 作成されたメッシュ
         */
        MeshPtr CreateSphere(float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);

        /**
         * @brief 平面メッシュを作成
         * @param width 幅
         * @param height 高さ
         * @param subdivisionsX X方向の分割数
         * @param subdivisionsY Y方向の分割数
         * @return 作成されたメッシュ
         */
        MeshPtr CreatePlane(float width = 1.0f, float height = 1.0f,
                            uint32_t subdivisionsX = 1, uint32_t subdivisionsY = 1);

        /**
         * @brief 円柱メッシュを作成
         * @param radius 半径
         * @param height 高さ
         * @param segments セグメント数
         * @return 作成されたメッシュ
         */
        MeshPtr CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);

        /**
         * @brief 円錐メッシュを作成
         * @param radius 底面半径
         * @param height 高さ
         * @param segments セグメント数
         * @return 作成されたメッシュ
         */
        MeshPtr CreateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);

        // ========================================
        // メッシュ管理
        // ========================================

        /**
         * @brief メッシュをアンロード
         * @param mesh アンロードするメッシュ
         */
        void UnloadMesh(MeshPtr mesh);

        /**
         * @brief 未使用メッシュをクリーンアップ
         */
        void CleanupUnusedMeshes();

        /**
         * @brief キャッシュをクリア
         */
        void ClearCache();

        // ========================================
        // 内部アクセス（Rendering内部用）
        // ========================================

        /**
         * @brief GPUデータを取得（Rendering内部用）
         * @param handle メッシュデータハンドル
         * @return GPUデータへのポインタ（存在しない場合nullptr）
         */
        const MeshGPUData *GetMeshGPUData(MeshDataHandle handle) const;

        /**
         * @brief メッシュ統計を取得
         */
        struct MeshStats
        {
            uint32_t TotalMeshCount = 0;
            uint32_t CachedMeshCount = 0;
            size_t TotalVertexMemory = 0;
            size_t TotalIndexMemory = 0;
        };
        MeshStats GetMeshStats() const;

    private:
        // コピー・ムーブ禁止
        MeshResourceManager(const MeshResourceManager &) = delete;
        MeshResourceManager &operator=(const MeshResourceManager &) = delete;

        // ハンドル生成
        MeshDataHandle AllocateHandle()
        {
            MeshDataHandle handle;
            handle.Id = m_NextHandleId.FetchAdd(1, std::memory_order_relaxed);
            return handle;
        }

        // メッシュGPUデータを登録
        MeshDataHandle RegisterMeshGPUData(const MeshCreateInfo &createInfo);

        // リソースマネージャー
        RenderResourceManager *m_ResourceManager = nullptr;

        // メッシュGPUデータマップ
        Container::Map<uint64_t, MeshGPUData> m_MeshDataMap;

        // メッシュキャッシュ（パス→Mesh）
        Container::Map<Container::String, MeshWeakPtr> m_MeshCache;

        // スレッドセーフ用ミューテックス
        mutable Thread::Mutex m_MeshMutex;

        // ハンドルID生成用
        Thread::Atomic<uint64_t> m_NextHandleId{1};

        // 初期化フラグ
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
