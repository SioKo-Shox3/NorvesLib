#pragma once

#include "Object/Resource.h"
#include "Object/Reflection.h"
#include "Rendering/MeshTypes.h"
#include "Rendering/VertexLayout.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core
{

    /**
     * @brief メッシュリソース
     *
     * PackageからデシリアライズされたメッシュデータをCPU側で保持します。
     * GPUへのアップロードはPersistentResourceCache/SceneRendererが担当します。
     *
     * 責任者: GEngine（ResourceRegistry経由）
     * 寿命管理: 参照カウント方式
     *
     * 使用例:
     * ```cpp
     * // Packageからデシリアライズ
     * auto meshResource = package->Deserialize<MeshResource>();
     *
     * // プリミティブ作成
     * auto box = MeshResource::CreateBox(1.0f, 1.0f, 1.0f);
     * ```
     */
    class MeshResource : public Resource
    {
        REFLECTION_CLASS(MeshResource, Resource)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        MeshResource();

        /**
         * @brief 初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit MeshResource(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit MeshResource(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~MeshResource();

        /**
         * @brief リソースを初期化します
         */
        void Initialize() override;

        /**
         * @brief リソースの破棄前処理を行います
         */
        void Finalize() override;

        // ========================================
        // Resource実装
        // ========================================

        /**
         * @brief リソースをロードします
         * @return ロードに成功した場合true
         */
        bool Load() override;

        /**
         * @brief リソースをアンロードします
         */
        void Unload() override;

        /**
         * @brief リソースのメモリサイズを取得します（バイト単位）
         * @return メモリサイズ
         */
        size_t GetMemorySize() const override;

        // ========================================
        // ジオメトリデータアクセス
        // ========================================

        /**
         * @brief 頂点データを取得します
         * @return 頂点データへの参照
         */
        const Container::VariableArray<uint8_t> &GetVertexData() const { return m_VertexData; }

        /**
         * @brief 頂点データを設定します
         * @param data 頂点データ
         */
        void SetVertexData(Container::VariableArray<uint8_t> &&data) { m_VertexData = std::move(data); }

        /**
         * @brief インデックスデータを取得します
         * @return インデックスデータへの参照
         */
        const Container::VariableArray<uint32_t> &GetIndexData() const { return m_IndexData; }

        /**
         * @brief インデックスデータを設定します
         * @param data インデックスデータ
         */
        void SetIndexData(Container::VariableArray<uint32_t> &&data) { m_IndexData = std::move(data); }

        /**
         * @brief 頂点数を取得します
         * @return 頂点数
         */
        uint32_t GetVertexCount() const { return m_VertexCount; }

        /**
         * @brief インデックス数を取得します
         * @return インデックス数
         */
        uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_IndexData.size()); }

        // ========================================
        // メタデータアクセス
        // ========================================

        /**
         * @brief 頂点レイアウトを取得します
         * @return 頂点レイアウトへの参照
         */
        const Rendering::VertexLayout &GetVertexLayout() const { return m_VertexLayout; }

        /**
         * @brief 頂点レイアウトを設定します
         * @param layout 頂点レイアウト
         */
        void SetVertexLayout(const Rendering::VertexLayout &layout) { m_VertexLayout = layout; }

        /**
         * @brief サブメッシュ配列を取得します
         * @return サブメッシュ配列への参照
         */
        const Container::VariableArray<Rendering::SubMesh> &GetSubMeshes() const { return m_SubMeshes; }

        /**
         * @brief サブメッシュを追加します
         * @param subMesh サブメッシュ
         */
        void AddSubMesh(const Rendering::SubMesh &subMesh) { m_SubMeshes.push_back(subMesh); }

        /**
         * @brief サブメッシュ数を取得します
         * @return サブメッシュ数
         */
        uint32_t GetSubMeshCount() const { return static_cast<uint32_t>(m_SubMeshes.size()); }

        /**
         * @brief マテリアルスロット配列を取得します
         * @return マテリアルスロット配列への参照
         */
        const Container::VariableArray<Rendering::MaterialSlot> &GetMaterialSlots() const { return m_MaterialSlots; }

        /**
         * @brief マテリアルスロットを追加します
         * @param slot マテリアルスロット
         */
        void AddMaterialSlot(const Rendering::MaterialSlot &slot) { m_MaterialSlots.push_back(slot); }

        /**
         * @brief バウンディングボックスを取得します
         * @return バウンディングボックスへの参照
         */
        const Rendering::BoundingBox &GetBounds() const { return m_Bounds; }

        /**
         * @brief バウンディングボックスを設定します
         * @param bounds バウンディングボックス
         */
        void SetBounds(const Rendering::BoundingBox &bounds) { m_Bounds = bounds; }

        /**
         * @brief バウンディングスフィアを取得します
         * @return バウンディングスフィアへの参照
         */
        const Rendering::BoundingSphere &GetBoundingSphere() const { return m_BoundingSphere; }

        /**
         * @brief バウンディングスフィアを設定します
         * @param sphere バウンディングスフィア
         */
        void SetBoundingSphere(const Rendering::BoundingSphere &sphere) { m_BoundingSphere = sphere; }

        /**
         * @brief プリミティブトポロジーを取得します
         * @return トポロジー
         */
        Rendering::PrimitiveTopology GetTopology() const { return m_Topology; }

        /**
         * @brief プリミティブトポロジーを設定します
         * @param topology トポロジー
         */
        void SetTopology(Rendering::PrimitiveTopology topology) { m_Topology = topology; }

        // ========================================
        // プリミティブ生成ファクトリ
        // ========================================

        /**
         * @brief ボックスメッシュを作成します
         * @param width 幅
         * @param height 高さ
         * @param depth 奥行き
         * @return 作成されたメッシュリソース
         */
        static Container::TSharedPtr<MeshResource> CreateBox(
            float width = 1.0f, float height = 1.0f, float depth = 1.0f);

        /**
         * @brief 球メッシュを作成します
         * @param radius 半径
         * @param segments セグメント数
         * @param rings リング数
         * @return 作成されたメッシュリソース
         */
        static Container::TSharedPtr<MeshResource> CreateSphere(
            float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);

        /**
         * @brief 平面メッシュを作成します
         * @param width 幅
         * @param height 高さ
         * @param subdivisionsX X方向の分割数
         * @param subdivisionsY Y方向の分割数
         * @return 作成されたメッシュリソース
         */
        static Container::TSharedPtr<MeshResource> CreatePlane(
            float width = 1.0f, float height = 1.0f,
            uint32_t subdivisionsX = 1, uint32_t subdivisionsY = 1);

        /**
         * @brief 円柱メッシュを作成します
         * @param radius 半径
         * @param height 高さ
         * @param segments セグメント数
         * @return 作成されたメッシュリソース
         */
        static Container::TSharedPtr<MeshResource> CreateCylinder(
            float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);

        /**
         * @brief 円錐メッシュを作成します
         * @param radius 底面半径
         * @param height 高さ
         * @param segments セグメント数
         * @return 作成されたメッシュリソース
         */
        static Container::TSharedPtr<MeshResource> CreateCone(
            float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);

        /**
         * @brief MeshCreateInfoからMeshResourceを作成します
         * @param createInfo 作成情報
         * @return 作成されたメッシュリソース
         */
        static Container::TSharedPtr<MeshResource> CreateFromInfo(
            const Rendering::MeshCreateInfo &createInfo);

    private:
        // ジオメトリデータ（CPU側）
        Container::VariableArray<uint8_t> m_VertexData; ///< 頂点データ
        Container::VariableArray<uint32_t> m_IndexData; ///< インデックスデータ
        uint32_t m_VertexCount = 0;                     ///< 頂点数

        // メタデータ
        Rendering::VertexLayout m_VertexLayout;                                               ///< 頂点レイアウト
        Container::VariableArray<Rendering::SubMesh> m_SubMeshes;                             ///< サブメッシュ配列
        Container::VariableArray<Rendering::MaterialSlot> m_MaterialSlots;                    ///< マテリアルスロット配列
        Rendering::BoundingBox m_Bounds;                                                      ///< バウンディングボックス
        Rendering::BoundingSphere m_BoundingSphere;                                           ///< バウンディングスフィア
        Rendering::PrimitiveTopology m_Topology = Rendering::PrimitiveTopology::TriangleList; ///< トポロジー
    };

    // スマートポインタエイリアス
    using MeshResourcePtr = Container::TSharedPtr<MeshResource>;
    using MeshResourceWeakPtr = Container::TWeakPtr<MeshResource>;

} // namespace NorvesLib::Core
