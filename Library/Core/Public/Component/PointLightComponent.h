#pragma once

#include "LightComponent.h"

namespace NorvesLib::Core::Component
{

    // ========================================
    // PointLightComponent
    // ========================================

    /**
     * @brief ポイントライトコンポーネント
     *
     * 点光源として機能するライトコンポーネント。
     * WorldObjectの位置から全方向に光を放射します。
     *
     * 特徴:
     * - WorldObjectの位置を自動的にライト位置として使用
     * - Range（影響範囲）と減衰パラメータの制御
     * - World::SyncToSceneView()でLightProxyが自動同期
     *
     * 使用例:
     * @code
     * auto* pointLight = new PointLightComponent();
     * pointLight->SetLightColor(1.0f, 0.9f, 0.3f);
     * pointLight->SetIntensity(3.0f);
     * pointLight->SetRange(8.0f);
     * worldObject->AddComponent(pointLight);
     * world.AddObject(worldObject);
     * // → World::SyncToSceneView()でSceneViewに自動登録
     * @endcode
     */
    class PointLightComponent : public LightComponent
    {
        REFLECTION_CLASS(PointLightComponent, LightComponent)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        PointLightComponent();

        /**
         * @brief 初期化子を使用したコンストラクタ
         */
        explicit PointLightComponent(const FieldInitializer* initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit PointLightComponent(const IUnknown* sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~PointLightComponent();

        // ========================================
        // ライフサイクル
        // ========================================

        virtual void Initialize() override;
        virtual void Tick(float deltaTime) override;

        // ========================================
        // ポイントライト固有プロパティ
        // ========================================

        /**
         * @brief 影響範囲を設定
         * @param range ライトの影響半径
         */
        void SetRange(float range) { Range = range; }

        /**
         * @brief 影響範囲を取得
         */
        float GetRange() const { return Range; }

        /**
         * @brief 定数減衰を設定
         */
        void SetAttenuationConstant(float value) { AttenuationConstant = value; }
        float GetAttenuationConstant() const { return AttenuationConstant; }

        /**
         * @brief 線形減衰を設定
         */
        void SetAttenuationLinear(float value) { AttenuationLinear = value; }
        float GetAttenuationLinear() const { return AttenuationLinear; }

        /**
         * @brief 二次減衰を設定
         */
        void SetAttenuationQuadratic(float value) { AttenuationQuadratic = value; }
        float GetAttenuationQuadratic() const { return AttenuationQuadratic; }

        // ========================================
        // LightProxy構築（オーバーライド）
        // ========================================

        /**
         * @brief PointLight用のLightProxyを構築
         * @param outProxy 出力先
         * @return 有効なProxyが生成できた場合true
         */
        virtual bool BuildLightProxy(Rendering::LightProxy& outProxy) const override;

    protected:
        // ========================================
        // リフレクションプロパティ
        // ========================================

        PROPERTY(float, Range)
        PROPERTY(float, AttenuationConstant)
        PROPERTY(float, AttenuationLinear)
        PROPERTY(float, AttenuationQuadratic)
    };

    // PointLightComponentへのスマートポインタ
    using PointLightComponentPtr = Container::TSharedPtr<PointLightComponent>;
    using PointLightComponentWeakPtr = Container::TWeakPtr<PointLightComponent>;

} // namespace NorvesLib::Core::Component
