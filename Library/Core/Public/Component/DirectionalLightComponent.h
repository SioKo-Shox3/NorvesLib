#pragma once

#include "LightComponent.h"

namespace NorvesLib::Core::Component
{

    // ========================================
    // DirectionalLightComponent
    // ========================================

    /**
     * @brief ディレクショナルライトコンポーネント
     *
     * 平行光源として機能するライトコンポーネント。
     * LightComponentの方向、色、強度を使用します。
     */
    class DirectionalLightComponent : public LightComponent
    {
        REFLECTION_CLASS(DirectionalLightComponent, LightComponent)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        DirectionalLightComponent();

        /**
         * @brief 初期化子を使用したコンストラクタ
         */
        explicit DirectionalLightComponent(const FieldInitializer* initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit DirectionalLightComponent(const IUnknown* sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~DirectionalLightComponent();

        // ========================================
        // ライフサイクル
        // ========================================

        virtual void Initialize() override;
        virtual void Tick(float deltaTime) override;

        // ========================================
        // LightProxy構築（オーバーライド）
        // ========================================

        /**
         * @brief DirectionalLight用のLightProxyを構築
         * @param outProxy 出力先
         * @return 有効なProxyが生成できた場合true
         */
        virtual bool BuildLightProxy(Rendering::LightProxy& outProxy) const override;
    };

    // DirectionalLightComponentへのスマートポインタ
    using DirectionalLightComponentPtr = Container::TSharedPtr<DirectionalLightComponent>;
    using DirectionalLightComponentWeakPtr = Container::TWeakPtr<DirectionalLightComponent>;

} // namespace NorvesLib::Core::Component

