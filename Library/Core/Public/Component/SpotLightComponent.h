#pragma once

#include "LightComponent.h"

namespace NorvesLib::Core::Component
{

    // ========================================
    // SpotLightComponent
    // ========================================

    /**
     * @brief スポットライトコンポーネント
     *
     * Entityの位置から指定方向へ円錐状に光を放射するライトコンポーネント。
     * コーン角は度で公開し、LightProxy構築時にシェーダー用のcos値へ変換します。
     */
    class SpotLightComponent : public LightComponent
    {
        REFLECTION_CLASS(SpotLightComponent, LightComponent)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        SpotLightComponent();

        /**
         * @brief 初期化子を使用したコンストラクタ
         */
        explicit SpotLightComponent(const FieldInitializer* initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit SpotLightComponent(const IUnknown* sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~SpotLightComponent();

        // ========================================
        // ライフサイクル
        // ========================================

        virtual void Initialize() override;
        virtual void Tick(float deltaTime) override;

        // ========================================
        // スポットライト固有プロパティ
        // ========================================

        /**
         * @brief 影響範囲を設定
         * @param range ライトの影響半径
         */
        void SetRange(float range)
        {
            getRange().Set(range);
            MarkRenderStateDirty();
        }

        /**
         * @brief 影響範囲を取得
         */
        float GetRange() const { return getRange().Get(); }

        /**
         * @brief 定数減衰を設定
         */
        void SetAttenuationConstant(float value)
        {
            getAttenuationConstant().Set(value);
            MarkRenderStateDirty();
        }
        float GetAttenuationConstant() const { return getAttenuationConstant().Get(); }

        /**
         * @brief 線形減衰を設定
         */
        void SetAttenuationLinear(float value)
        {
            getAttenuationLinear().Set(value);
            MarkRenderStateDirty();
        }
        float GetAttenuationLinear() const { return getAttenuationLinear().Get(); }

        /**
         * @brief 二次減衰を設定
         */
        void SetAttenuationQuadratic(float value)
        {
            getAttenuationQuadratic().Set(value);
            MarkRenderStateDirty();
        }
        float GetAttenuationQuadratic() const { return getAttenuationQuadratic().Get(); }

        /**
         * @brief 内側コーン角を設定（度）
         */
        void SetInnerConeAngle(float degrees)
        {
            getInnerConeAngle().Set(degrees);
            MarkRenderStateDirty();
        }
        float GetInnerConeAngle() const { return getInnerConeAngle().Get(); }

        /**
         * @brief 外側コーン角を設定（度）
         */
        void SetOuterConeAngle(float degrees)
        {
            getOuterConeAngle().Set(degrees);
            MarkRenderStateDirty();
        }
        float GetOuterConeAngle() const { return getOuterConeAngle().Get(); }

        // ========================================
        // LightProxy構築（オーバーライド）
        // ========================================

        /**
         * @brief SpotLight用のLightProxyを構築
         * @param outProxy 出力先
         * @return 有効なProxyが生成できた場合true
         */
        virtual bool BuildLightProxy(Rendering::LightProxy& outProxy) const override;

        Ecs::EcsComponentRowKind GetEcsRowKind() const override { return Ecs::EcsComponentRowKind::SpotLight; }

    protected:
        // ========================================
        // リフレクションプロパティ
        // ========================================

        ECS_PROPERTY(Ecs::EcsLightFalloff, Range, float, Range)
        ECS_PROPERTY(Ecs::EcsLightFalloff, AttenuationConstant, float, AttenuationConstant)
        ECS_PROPERTY(Ecs::EcsLightFalloff, AttenuationLinear, float, AttenuationLinear)
        ECS_PROPERTY(Ecs::EcsLightFalloff, AttenuationQuadratic, float, AttenuationQuadratic)
        ECS_PROPERTY(Ecs::EcsSpotCone, InnerConeAngle, float, InnerConeAngle)
        ECS_PROPERTY(Ecs::EcsSpotCone, OuterConeAngle, float, OuterConeAngle)
    };

    // SpotLightComponentへのスマートポインタ
    using SpotLightComponentPtr = Container::TSharedPtr<SpotLightComponent>;
    using SpotLightComponentWeakPtr = Container::TWeakPtr<SpotLightComponent>;

} // namespace NorvesLib::Core::Component

