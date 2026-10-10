#pragma once

#include "Component.h"
#include "Ecs/EcsRenderTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/MeshTypes.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/SceneProxy.h"
#include "Math/Matrix4x4.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class MaterialResources;
    class MeshResources;
}

namespace NorvesLib::Core::Component
{

    // ========================================
    // MeshComponent
    // ========================================

    /**
     * @brief メッシュコンポーネント
     *
     * メッシュを描画するためのコンポーネント。
     * Meshアセットへの参照を保持し、描画に必要なデータを
     * MeshProxyとしてRenderThreadに提供します。
     *
     * 特徴:
     * - Meshアセットの参照（生データへの直接アクセスなし）
     * - マテリアルのオーバーライド
     * - LOD設定
     * - 描画フラグの制御
     */
    class MeshComponent : public Component
    {
        REFLECTION_CLASS(MeshComponent, Component)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        MeshComponent();

        /**
         * @brief 初期化子を使用したコンストラクタ
         */
        explicit MeshComponent(const FieldInitializer *initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit MeshComponent(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~MeshComponent();

        // ========================================
        // ライフサイクル
        // ========================================

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;
        virtual void Tick(float deltaTime) override;

        Ecs::EcsComponentRowKind GetEcsRowKind() const override { return Ecs::EcsComponentRowKind::Mesh; }
        void CollectRenderResourceRefs(Container::VariableArray<Rendering::RenderResourceRef>& out) const override;

        // ========================================
        // メッシュ設定
        // ========================================

        /**
         * @brief メッシュを設定
         * @param mesh メッシュアセット
         */
        void SetMeshHandle(Rendering::MeshDataHandle handle);

        /**
         * @brief メッシュハンドルを取得
         * @return メッシュデータハンドル
         */
        Rendering::MeshDataHandle GetMeshHandle() const { return getMeshHandle().Get(); }

        /**
         * @brief メッシュが設定されているかどうか
         */
        bool HasMesh() const { return getMeshHandle().Get().IsValid(); }

        // ========================================
        // マテリアル設定
        // ========================================

        /**
         * @brief マテリアル数を取得
         */
        uint32_t GetMaterialCount() const { return static_cast<uint32_t>(m_Materials.size()); }

        /**
         * @brief マテリアルを設定
         * @param index スロットインデックス
         * @param material マテリアルハンドル
         */
        void SetMaterial(uint32_t index, Rendering::MaterialHandle material);

        /**
         * @brief マテリアルを取得
         * @param index スロットインデックス
         * @return マテリアルハンドル
         */
        Rendering::MaterialHandle GetMaterial(uint32_t index) const;

        /**
         * @brief 全マテリアルをクリア
         */
        void ClearMaterials();

        // ========================================
        // 描画設定
        // ========================================

        /**
         * @brief 可視性を設定
         */
        void SetVisible(bool bNewVisible)
        {
            getbVisible().Set(bNewVisible);
            MarkRenderStateDirty();
        }
        bool IsVisible() const { return getbVisible().Get() && IsActive(); }

        /**
         * @brief シャドウキャスト設定
         */
        void SetCastShadow(bool bCast)
        {
            getbCastShadow().Set(bCast);
            MarkRenderStateDirty();
        }
        bool GetCastShadow() const { return getbCastShadow().Get(); }

        /**
         * @brief シャドウレシーブ設定
         */
        void SetReceiveShadow(bool bReceive)
        {
            getbReceiveShadow().Set(bReceive);
            MarkRenderStateDirty();
        }
        bool GetReceiveShadow() const { return getbReceiveShadow().Get(); }

        /**
         * @brief レンダーレイヤー設定
         */
        void SetRenderLayer(Rendering::RenderLayer layer)
        {
            getRenderLayerProp().Set(layer);
            MarkRenderStateDirty();
        }
        Rendering::RenderLayer GetRenderLayer() const { return getRenderLayerProp().Get(); }

        // ========================================
        // LOD設定
        // ========================================

        /**
         * @brief 強制LODレベルを設定（-1で自動）
         */
        void SetForcedLODLevel(int32_t level)
        {
            ForcedLODLevel = level;
            MarkRenderStateDirty();
        }
        int32_t GetForcedLODLevel() const { return ForcedLODLevel; }

        /**
         * @brief 現在のLODレベルを取得
         */
        uint8_t GetCurrentLODLevel() const { return CurrentLODLevel; }

        // ========================================
        // カスタムデータ
        // ========================================

        /**
         * @brief カスタムシェーダーデータを設定
         * @param index インデックス (0-3)
         * @param value 値
         */
        void SetCustomData(uint32_t index, float value);

        /**
         * @brief カスタムシェーダーデータを取得
         * @param index インデックス (0-3)
         * @return 値
         */
        float GetCustomData(uint32_t index) const;

        // ========================================
        // バウンディング
        // ========================================

        /**
         * @brief ローカルバウンディングボックスを取得
         */
        const Rendering::BoundingBox &GetLocalBounds() const;

        /**
         * @brief ワールドバウンディングスフィアを取得
         */
        const Rendering::BoundingSphere &GetWorldBounds() const { return m_WorldBounds; }

        /**
         * @brief SceneView同期前にワールドトランスフォームとBoundsキャッシュを更新
         */
        void RefreshRenderTransformCache();

        /**
         * @brief RenderThreadへproxyを渡した後に前フレーム変換履歴を確定
         */
        void CommitRenderTransformHistory();

        // ========================================
        // SceneProxy生成
        // ========================================

        /**
         * @brief MeshProxyを構築して返す
         * @param outProxy 出力先
         * @return 有効なProxyが生成できた場合true
         */
        bool BuildMeshProxy(Rendering::MeshProxy &outProxy,
                            const Rendering::MaterialResources* materials = nullptr,
                            const Rendering::MeshResources* meshes = nullptr) const;

    protected:
        // ========================================
        // 内部メソッド
        // ========================================

        /**
         * @brief ワールドトランスフォームを更新
         */
        void UpdateWorldTransform();

        /**
         * @brief ワールドバウンディングを更新
         */
        void UpdateWorldBounds();

        /**
         * @brief MeshProxyを同期
         */
        void SyncMeshProxy();

        /**
         * @brief ワールド行列を計算
         */
        void CalculateWorldMatrix(Math::Matrix4x4 &outMatrix) const;

        // ========================================
        // リフレクションプロパティ
        // ========================================

        // 描画の値（メッシュの handle・可視・影・レイヤー）は World の木にいる間 ECS の行（EcsMeshRender）の列にあり、
        // 無い間は予備の値にある

        // メッシュリソース（CPU側データ - Resourceシステム経由）
        ECS_PROPERTY(Ecs::EcsMeshRender, MeshHandle, Rendering::MeshDataHandle, MeshHandle)

        // 描画設定
        ECS_PROPERTY(Ecs::EcsMeshRender, bVisible, bool, bVisible)
        ECS_PROPERTY(Ecs::EcsMeshRender, bCastShadow, bool, bCastShadow)
        ECS_PROPERTY(Ecs::EcsMeshRender, bReceiveShadow, bool, bReceiveShadow)
        ECS_PROPERTY(Ecs::EcsMeshRender, Layer, Rendering::RenderLayer, RenderLayerProp)

        // LOD
        PROPERTY(int32_t, ForcedLODLevel) // -1 = 自動
        PROPERTY(uint8_t, CurrentLODLevel)

        // ========================================
        // 内部キャッシュ（リフレクション対象外）
        // ========================================

        // マテリアルオーバーライド
        Container::VariableArray<Rendering::MaterialHandle> m_Materials;

        // カスタムシェーダーデータ
        float m_CustomData[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        // テクスチャ
        // キャッシュされたトランスフォーム
        Math::Matrix4x4 m_WorldTransform;
        Math::Matrix4x4 m_PreviousWorldTransform;

        // キャッシュされたバウンディング
        Rendering::BoundingSphere m_WorldBounds;

        // ダーティフラグ
        bool m_bTransformDirty = true;
        bool m_bBoundsDirty = true;
    };

    // MeshComponentへのスマートポインタ
    using MeshComponentPtr = Container::TSharedPtr<MeshComponent>;
    using MeshComponentWeakPtr = Container::TWeakPtr<MeshComponent>;

} // namespace NorvesLib::Core::Component

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::MeshComponent, NorvesLib::Core::EClassCastFlags::MeshComponent)
