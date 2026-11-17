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

    enum class ProceduralMeshKind : uint8_t
    {
        None = 0,
        UVSphere = 1,
        Plane = 2
    };

    /** @brief 保存する手続きメッシュの種類と寸法、分割数。寸法の単位は m。 */
    struct ProceduralMeshSpec
    {
        ProceduralMeshKind Kind = ProceduralMeshKind::None;
        Math::Vector3 Size;
        uint32_t Segments[2] = {1, 1};

        static constexpr TypeKind SchemaTypeKind = TypeKind::String;
        static constexpr const char* SchemaTypeName = "ProceduralMeshSpec";

        bool SerializeToString(Container::String& outValue) const;
        bool DeserializeFromString(const Container::String& inValue);
        bool operator==(const ProceduralMeshSpec& other) const;
    };

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
        Rendering::MeshDataHandle GetMeshHandle() const { return LoadMeshHandle(); }

        /**
         * @brief メッシュが設定されているかどうか
         */
        bool HasMesh() const { return LoadMeshHandle().IsValid(); }

        /** @brief 保存する手続きメッシュの指定を変更する。 */
        void SetProceduralMeshSpec(const ProceduralMeshSpec& spec);
        /** @brief 保存する手続きメッシュの指定を取得する。 */
        const ProceduralMeshSpec& GetProceduralMeshSpec() const { return ProceduralMesh.Get(); }

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

        /** @brief 空の遮蔽のマップが遮蔽を受け持つ物かを設定する。 */
        void SetSkyMapProxy(bool bSkyMapProxy)
        {
            this->bSkyMapProxy = bSkyMapProxy;
            MarkRenderStateDirty();
        }
        bool IsSkyMapProxy() const { return bSkyMapProxy.Get(); }

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
         * @brief 段0を含むLODの鎖を設定。有効な段0のハンドルは現在のメッシュに反映する。
         * @param group 1～4段のメッシュと、有限・非負で段の順に狭義に小さくなるしきい値
         * @return 設定できた場合はtrue。不正な入力では何も変更せずfalse。
         * @details 段0のハンドルが無効なら現在のメッシュを使う。1段なら鎖を解除する。
         */
        bool SetLODChain(const Rendering::LODGroup& group);

        /**
         * @brief メッシュを保ったままLODの鎖としきい値を消し、段数を1に戻す。
         */
        void ClearLODChain();

        /**
         * @brief 段0を含むLODの段数を取得。
         */
        uint32_t GetLODLevelCount() const { return m_LODLevelCount; }

        /**
         * @brief 強制LODレベルを設定（-1で自動、範囲外は-1～最大段に丸める）
         */
        void SetForcedLODLevel(int32_t level)
        {
            const int32_t maxLevel = static_cast<int32_t>(Rendering::MAX_MESH_LOD_LEVELS) - 1;
            ForcedLODLevel = level < -1 ? -1 : (level > maxLevel ? maxLevel : level);
            MarkRenderStateDirty();
        }
        int32_t GetForcedLODLevel() const { return ForcedLODLevel; }

        /**
         * @brief 保存の互換のために残すLODレベルを取得。
         * @details 段の選択はビューごとにSceneViewのカリングが行い、この値は更新しない。
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

        /**
         * @brief インスタンスごとの材質値を設定
         * @param index 0 = P0、1 = P1、2 = 予約、3 = 被覆率の乗数（1 で不変）
         * @param value 0 以上ならインスタンスの値、負なら材質の値を使う
         */
        void SetShadingParams(uint32_t index, float value);

        /**
         * @brief インスタンスごとの材質値を取得。範囲外なら -1 を返す
         * @param index 0 = P0、1 = P1、2 = 予約、3 = 被覆率の乗数
         */
        float GetShadingParams(uint32_t index) const;

        /**
         * @brief ディゾルブの閾値をインスタンスの P0 に設定する。0 = 全面が残る、1 = 全面が消える。
         * 負と NaN は未指定で材質の P0 を使い、1 を超える値は 1 に収める。
         * P0 は PaleFlat の消滅度と同じ欄を共有する。
         */
        void SetDissolveThreshold(float threshold);

        /** @brief インスタンスの P0 のディゾルブ閾値を取得する。PaleFlat の消滅度と同じ欄を共有する。 */
        float GetDissolveThreshold() const;

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
        void OnPropertyApplied(const ClassProperty& property) override;
        void OnEcsRowAttached() override;
        void OnEcsRowDetaching() override;

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

        /** @brief 保存する形の指定。None のときは SetMeshHandle で外から付けた形を使い、保存しない。 */
        PROPERTY(ProceduralMeshSpec, ProceduralMesh)

        // 描画設定
        ECS_PROPERTY(Ecs::EcsMeshRender, bVisible, bool, bVisible)
        ECS_PROPERTY(Ecs::EcsMeshRender, bCastShadow, bool, bCastShadow)
        ECS_PROPERTY(Ecs::EcsMeshRender, bReceiveShadow, bool, bReceiveShadow)
        ECS_PROPERTY(Ecs::EcsMeshRender, Layer, Rendering::RenderLayer, RenderLayerProp)

        // 空の遮蔽のマップ（樹冠だけの組）が光線の代わりに遮蔽を受け持つ物か（手置きの樹木など）。
        // 立てると TLAS へ instance mask 0x04 で入り、DDGI・RTGI の光線に当たらない
        PROPERTY(bool, bSkyMapProxy)

        // LOD
        PROPERTY(int32_t, ForcedLODLevel) // -1 = 自動
        PROPERTY(uint8_t, CurrentLODLevel)

        // ========================================
        // 内部キャッシュ（リフレクション対象外）
        // ========================================

        // マテリアルオーバーライド
        Container::VariableArray<Rendering::MaterialHandle> m_Materials;

        // LODの鎖としきい値は保存・読み込みの対象外。段0はメッシュのハンドルに持つ。
        Container::FixedArray<Rendering::MeshDataHandle, Rendering::MAX_MESH_LOD_LEVELS - 1> m_LODChainMeshes;
        Container::FixedArray<float, Rendering::MAX_MESH_LOD_LEVELS> m_LODScreenSizeThresholds =
            Container::FixedArray<float, Rendering::MAX_MESH_LOD_LEVELS>(0.0f);
        uint32_t m_LODLevelCount = 1;

        // カスタムシェーダーデータ
        float m_CustomData[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float m_ShadingParams[4] = {-1.0f, -1.0f, -1.0f, -1.0f};

        // テクスチャ
        // キャッシュされたトランスフォーム
        Math::Matrix4x4 m_WorldTransform;
        Math::Matrix4x4 m_PreviousWorldTransform;

        // キャッシュされたバウンディング
        Rendering::BoundingSphere m_WorldBounds;

        // ダーティフラグ
        bool m_bTransformDirty = true;
        bool m_bBoundsDirty = true;

    private:
        Rendering::MeshDataHandle LoadMeshHandle() const;
        void StoreMeshHandle(Rendering::MeshDataHandle handle);
        void RefreshProceduralMesh();
        void ReleaseProceduralMesh();

        Rendering::MeshDataHandle m_MeshHandleFallback;
        ProceduralMeshSpec m_AppliedProceduralMeshSpec;
        bool m_bOwnsProceduralMesh = false;
    };

    // MeshComponentへのスマートポインタ
    using MeshComponentPtr = Container::TSharedPtr<MeshComponent>;
    using MeshComponentWeakPtr = Container::TWeakPtr<MeshComponent>;

} // namespace NorvesLib::Core::Component

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::MeshComponent, NorvesLib::Core::EClassCastFlags::MeshComponent)
