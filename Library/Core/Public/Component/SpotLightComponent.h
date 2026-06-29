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
            Range = range;
            MarkRenderStateDirty();
        }

        /**
         * @brief 影響範囲を取得
         */
        float GetRange() const { return Range; }

        /**
         * @brief 定数減衰を設定
         */
        void SetAttenuationConstant(float value)
        {
            AttenuationConstant = value;
            MarkRenderStateDirty();
        }
        float GetAttenuationConstant() const { return AttenuationConstant; }

        /**
         * @brief 線形減衰を設定
         */
        void SetAttenuationLinear(float value)
        {
            AttenuationLinear = value;
            MarkRenderStateDirty();
        }
        float GetAttenuationLinear() const { return AttenuationLinear; }

        /**
         * @brief 二次減衰を設定
         */
        void SetAttenuationQuadratic(float value)
        {
            AttenuationQuadratic = value;
            MarkRenderStateDirty();
        }
        float GetAttenuationQuadratic() const { return AttenuationQuadratic; }

        /**
         * @brief 内側コーン角を設定（度）
         */
        void SetInnerConeAngle(float degrees)
        {
            InnerConeAngle = degrees;
            EnsureConeAngleOrder();
            MarkRenderStateDirty();
        }
        float GetInnerConeAngle() const { return InnerConeAngle; }

        /**
         * @brief 外側コーン角を設定（度）
         */
        void SetOuterConeAngle(float degrees)
        {
            OuterConeAngle = degrees;
            EnsureConeAngleOrder();
            MarkRenderStateDirty();
        }
        float GetOuterConeAngle() const { return OuterConeAngle; }

        // ========================================
        // LightProxy構築（オーバーライド）
        // ========================================

        /**
         * @brief SpotLight用のLightProxyを構築
         * @param outProxy 出力先
         * @return 有効なProxyが生成できた場合true
         */
        virtual bool BuildLightProxy(Rendering::LightProxy& outProxy) const override;

    private:
        void EnsureConeAngleOrder();

    protected:
        // ========================================
        // リフレクションプロパティ
        // ========================================

        PROPERTY(float, Range)
        PROPERTY(float, AttenuationConstant)
        PROPERTY(float, AttenuationLinear)
        PROPERTY(float, AttenuationQuadratic)
        PROPERTY(float, InnerConeAngle)
        PROPERTY(float, OuterConeAngle)
    };

    // SpotLightComponentへのスマートポインタ
    using SpotLightComponentPtr = Container::TSharedPtr<SpotLightComponent>;
    using SpotLightComponentWeakPtr = Container::TWeakPtr<SpotLightComponent>;

} // namespace NorvesLib::Core::Component

