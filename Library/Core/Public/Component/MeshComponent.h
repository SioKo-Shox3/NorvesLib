#pragma once

#include "Component.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/MeshTypes.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SceneCollector.h"
#include "Rendering/MeshResourceManager.h"
#include "Math/Matrix4x4.h"
#include "Container/Containers.h"
#include <cstdint>

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
    class MeshComponent : public Component, public Rendering::ISceneCollectable
    {
    public:
        using Super = Component;

        /**
         * @brief デフォルトコンストラクタ
         */
        MeshComponent();

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

        // ========================================
        // メッシュ設定
        // ========================================

        /**
         * @brief メッシュを設定
         * @param mesh メッシュアセット
         */
        void SetMesh(Rendering::MeshPtr mesh);

        /**
         * @brief メッシュを取得
         * @return メッシュアセット
         */
        Rendering::MeshPtr GetMesh() const { return m_Mesh; }

        /**
         * @brief メッシュがあるかどうか
         */
        bool HasMesh() const { return m_Mesh != nullptr && m_Mesh->IsValid(); }

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
        void SetVisible(bool bVisible) { m_bVisible = bVisible; }
        bool IsVisible() const override { return m_bVisible && IsActive(); }

        /**
         * @brief シャドウキャスト設定
         */
        void SetCastShadow(bool bCast) { m_bCastShadow = bCast; }
        bool GetCastShadow() const { return m_bCastShadow; }

        /**
         * @brief シャドウレシーブ設定
         */
        void SetReceiveShadow(bool bReceive) { m_bReceiveShadow = bReceive; }
        bool GetReceiveShadow() const { return m_bReceiveShadow; }

        /**
         * @brief レンダーレイヤー設定
         */
        void SetRenderLayer(Rendering::RenderLayer layer) { m_RenderLayer = layer; }
        Rendering::RenderLayer GetRenderLayer() const override { return m_RenderLayer; }

        // ========================================
        // LOD設定
        // ========================================

        /**
         * @brief 強制LODレベルを設定（-1で自動）
         */
        void SetForcedLODLevel(int32_t level) { m_ForcedLODLevel = level; }
        int32_t GetForcedLODLevel() const { return m_ForcedLODLevel; }

        /**
         * @brief 現在のLODレベルを取得
         */
        uint8_t GetCurrentLODLevel() const { return m_CurrentLODLevel; }

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

        // ========================================
        // ISceneCollectable実装
        // ========================================

        /**
         * @brief MeshProxyを収集
         */
        virtual bool CollectMeshProxy(Rendering::MeshProxy &outProxy) const override;

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
        // メンバ変数
        // ========================================

        // メッシュアセット
        Rendering::MeshPtr m_Mesh;

        // マテリアルオーバーライド
        Container::VariableArray<Rendering::MaterialHandle> m_Materials;

        // 描画設定
        bool m_bVisible = true;
        bool m_bCastShadow = true;
        bool m_bReceiveShadow = true;
        Rendering::RenderLayer m_RenderLayer = Rendering::RenderLayer::Default;

        // LOD
        int32_t m_ForcedLODLevel = -1;  // -1 = 自動
        uint8_t m_CurrentLODLevel = 0;

        // カスタムシェーダーデータ
        float m_CustomData[4] = {0.0f, 0.0f, 0.0f, 0.0f};

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
