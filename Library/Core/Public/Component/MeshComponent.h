#pragma once

#include "Component.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/MeshTypes.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/SceneProxy.h"
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
        Rendering::MeshDataHandle GetMeshHandle() const { return MeshHandle; }

        /**
         * @brief メッシュが設定されているかどうか
         */
        bool HasMesh() const { return MeshHandle->IsValid(); }

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
        void SetVisible(bool bNewVisible) { bVisible = bNewVisible; }
        bool IsVisible() const { return bVisible && IsActive(); }

        /**
         * @brief シャドウキャスト設定
         */
        void SetCastShadow(bool bCast) { bCastShadow = bCast; }
        bool GetCastShadow() const { return bCastShadow; }

        /**
         * @brief シャドウレシーブ設定
         */
        void SetReceiveShadow(bool bReceive) { bReceiveShadow = bReceive; }
        bool GetReceiveShadow() const { return bReceiveShadow; }

        /**
         * @brief レンダーレイヤー設定
         */
        void SetRenderLayer(Rendering::RenderLayer layer) { RenderLayerProp = layer; }
        Rendering::RenderLayer GetRenderLayer() const { return RenderLayerProp; }

        // ========================================
        // LOD設定
        // ========================================

        /**
         * @brief 強制LODレベルを設定（-1で自動）
         */
        void SetForcedLODLevel(int32_t level) { ForcedLODLevel = level; }
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
        // エミッシブ設定
        // ========================================

        /**
         * @brief エミッシブカラーを設定
         * @param r 赤 (0-1)
         * @param g 緑 (0-1)
         * @param b 青 (0-1)
         */
        void SetEmissiveColor(float r, float g, float b);

        /**
         * @brief エミッシブ強度を設定
         * @param strength 強度（0=非発光、1以上でHDR値になりブルームの対象）
         */
        void SetEmissiveStrength(float strength);

        /**
         * @brief エミッシブ強度を取得
         */
        float GetEmissiveStrength() const { return m_EmissiveStrength; }

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
        // SceneProxy生成
        // ========================================

        /**
         * @brief MeshProxyを構築して返す
         * @param outProxy 出力先
         * @return 有効なProxyが生成できた場合true
         */
        bool BuildMeshProxy(Rendering::MeshProxy &outProxy) const;

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

        // メッシュリソース（CPU側データ - Resourceシステム経由）
        PROPERTY(Rendering::MeshDataHandle, MeshHandle)

        // 描画設定
        PROPERTY(bool, bVisible)
        PROPERTY(bool, bCastShadow)
        PROPERTY(bool, bReceiveShadow)
        PROPERTY(Rendering::RenderLayer, RenderLayerProp)

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

        // エミッシブデータ
        float m_EmissiveColor[3] = {0.0f, 0.0f, 0.0f};
        float m_EmissiveStrength = 0.0f;

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
