#pragma once

#include "Component.h"
#include "LightIntensityUnit.h"
#include "Ecs/EcsRenderTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/RenderTypes.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core::Component
{
    // ========================================
    // LightComponent
    // ========================================

    /**
     * @brief ライトコンポーネント基底クラス
     *
     * すべてのライトコンポーネントの基底クラス。
     * ライトの共通プロパティ（色、強度、可視性、シャドウ設定等）を管理し、
     * LightProxyを構築してSceneViewに同期します。
     *
     * 派生クラス:
     * - PointLightComponent: 点光源
     * - DirectionalLightComponent: 平行光源
     * - SpotLightComponent: スポットライト
     *
     * World::SyncToSceneView() でMeshComponentと同様に
     * LightProxyが自動的にSceneViewに登録・更新されます。
     */
    class LightComponent : public Component
    {
        REFLECTION_CLASS(LightComponent, Component)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        LightComponent();

        /**
         * @brief 初期化子を使用したコンストラクタ
         */
        explicit LightComponent(const FieldInitializer *initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit LightComponent(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~LightComponent();

        // ========================================
        // ライフサイクル
        // ========================================

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;

        // ========================================
        // ライト共通プロパティ
        // ========================================

        /**
         * @brief ライトタイプを取得
         * @return ライトタイプ
         */
        Rendering::LightType GetLightType() const { return getLightTypeProp().Get(); }

        /**
         * @brief ライトカラーを設定
         * @param r 赤 (0-1)
         * @param g 緑 (0-1)
         * @param b 青 (0-1)
         */
        void SetLightColor(float r, float g, float b);

        /**
         * @brief ライトカラーを取得
         * @param outR 赤
         * @param outG 緑
         * @param outB 青
         */
        void GetLightColor(float &outR, float &outG, float &outB) const;

        /**
         * @brief 強度を設定
         * @param intensity ライト強度
         */
        void SetIntensity(float intensity);

        /**
         * @brief 強度を取得
         */
        float GetIntensity() const { return getIntensity().Get(); }

        bool SetIntensityUnit(LightIntensityUnit unit);
        LightIntensityUnit GetIntensityUnit() const { return getIntensityUnit().Get(); }

        /**
         * @brief シャドウキャスト設定
         */
        void SetCastShadows(bool bCast)
        {
            getbCastShadows().Set(bCast);
            MarkRenderStateDirty();
        }
        bool GetCastShadows() const { return getbCastShadows().Get(); }

        /**
         * @brief ライト可視性設定
         */
        void SetLightVisible(bool bNewVisible)
        {
            getbLightVisible().Set(bNewVisible);
            MarkRenderStateDirty();
        }
        bool IsLightVisible() const { return getbLightVisible().Get(); }

        /**
         * @brief シャドウバイアス設定
         */
        void SetShadowBias(float bias)
        {
            getShadowBias().Set(bias);
            MarkRenderStateDirty();
        }
        float GetShadowBias() const { return getShadowBias().Get(); }

        /**
         * @brief シャドウマップ解像度設定
         */
        void SetShadowMapResolution(uint32_t resolution)
        {
            getShadowMapResolution().Set(resolution);
            MarkRenderStateDirty();
        }
        uint32_t GetShadowMapResolution() const { return getShadowMapResolution().Get(); }

        /**
         * @brief 影響レイヤー設定
         */
        void SetAffectedLayers(Rendering::RenderLayer layers)
        {
            getAffectedLayers().Set(layers);
            MarkRenderStateDirty();
        }
        Rendering::RenderLayer GetAffectedLayers() const { return getAffectedLayers().Get(); }

        /**
         * @brief ライト方向を設定（ディレクショナルライト用）
         * @param x X方向成分
         * @param y Y方向成分
         * @param z Z方向成分
         */
        void SetLightDirection(float x, float y, float z);

        /**
         * @brief ライト方向を取得
         * @param outX X方向成分
         * @param outY Y方向成分
         * @param outZ Z方向成分
         */
        void GetLightDirection(float &outX, float &outY, float &outZ) const;

    private:
        // ライト方向を列から読む（行が無い間は予備の値）
        void LoadLightDirection(float (&out)[3]) const;
        // ライト方向を列へ書く（行が無い間は予備の値へ書く）
        void StoreLightDirection(const float (&value)[3]);

    public:

        // ========================================
        // LightProxy構築
        // ========================================

        /**
         * @brief LightProxyを構築して返す
         * @param outProxy 出力先
         * @return 有効なProxyが生成できた場合true
         *
         * 派生クラスでオーバーライドし、ライトタイプ固有の情報を設定します。
         * 基底クラスでは共通プロパティ（色、強度、シャドウ等）を設定します。
         */
        virtual bool BuildLightProxy(Rendering::LightProxy &outProxy) const;

        Ecs::EcsComponentRowKind GetEcsRowKind() const override { return Ecs::EcsComponentRowKind::Light; }

    protected:
        // ライト方向は列（EcsLightRender::Direction）の一部で、保存の対象でないので ECS_PROPERTY にしない。
        // 行が付いた直後に予備の値を列へ写し、外す直前に列から予備の値へ戻す
        void OnEcsRowAttached() override;
        void OnEcsRowDetaching() override;

        /**
         * @brief 共通のLightProxyフィールドを設定するヘルパー
         * @param outProxy 出力先
         */
        bool FillCommonLightProxy(Rendering::LightProxy &outProxy) const;

        // ========================================
        // リフレクションプロパティ
        // ========================================

        // 光の値は World の木にいる間 ECS の行（EcsLightRender）の列にあり、無い間は予備の値にある
        ECS_PROPERTY(Ecs::EcsLightRender, Type, Rendering::LightType, LightTypeProp)
        ECS_PROPERTY(Ecs::EcsLightRender, Intensity, float, Intensity)
        ECS_PROPERTY(Ecs::EcsLightRender, Unit, LightIntensityUnit, IntensityUnit)
        ECS_PROPERTY(Ecs::EcsLightRender, Color, Math::Vector3, LightColor)
        ECS_PROPERTY(Ecs::EcsLightRender, bCastShadows, bool, bCastShadows)
        ECS_PROPERTY(Ecs::EcsLightRender, bVisible, bool, bLightVisible)
        ECS_PROPERTY(Ecs::EcsLightRender, ShadowBias, float, ShadowBias)
        ECS_PROPERTY(Ecs::EcsLightRender, ShadowMapResolution, uint32_t, ShadowMapResolution)
        ECS_PROPERTY(Ecs::EcsLightRender, AffectedLayers, Rendering::RenderLayer, AffectedLayers)

        // ========================================
        // 内部キャッシュ（リフレクション対象外）
        // ========================================

        // 行が無い間のライト方向（行がある間は列が正）
        float m_LightDirection[3] = {0.0f, -1.0f, 0.0f};
    };

    // LightComponentへのスマートポインタ
    using LightComponentPtr = Container::TSharedPtr<LightComponent>;
    using LightComponentWeakPtr = Container::TWeakPtr<LightComponent>;

} // namespace NorvesLib::Core::Component

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::LightComponent, NorvesLib::Core::EClassCastFlags::LightComponent)
