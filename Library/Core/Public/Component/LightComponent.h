#pragma once

#include "Component.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/RenderTypes.h"
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
     * - DirectionalLightComponent: 平行光源（将来）
     * - SpotLightComponent: スポットライト（将来）
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
        explicit LightComponent(const FieldInitializer* initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit LightComponent(const IUnknown* sourceObject);

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
        virtual void Tick(float deltaTime) override;

        // ========================================
        // ライト共通プロパティ
        // ========================================

        /**
         * @brief ライトタイプを取得
         * @return ライトタイプ
         */
        Rendering::LightType GetLightType() const { return LightTypeProp; }

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
        void GetLightColor(float& outR, float& outG, float& outB) const;

        /**
         * @brief 強度を設定
         * @param intensity ライト強度
         */
        void SetIntensity(float intensity);

        /**
         * @brief 強度を取得
         */
        float GetIntensity() const { return Intensity; }

        /**
         * @brief シャドウキャスト設定
         */
        void SetCastShadows(bool bCast) { bCastShadows = bCast; }
        bool GetCastShadows() const { return bCastShadows; }

        /**
         * @brief ライト可視性設定
         */
        void SetLightVisible(bool bNewVisible) { bLightVisible = bNewVisible; }
        bool IsLightVisible() const { return bLightVisible; }

        /**
         * @brief シャドウバイアス設定
         */
        void SetShadowBias(float bias) { ShadowBias = bias; }
        float GetShadowBias() const { return ShadowBias; }

        /**
         * @brief シャドウマップ解像度設定
         */
        void SetShadowMapResolution(uint32_t resolution) { ShadowMapResolution = resolution; }
        uint32_t GetShadowMapResolution() const { return ShadowMapResolution; }

        /**
         * @brief 影響レイヤー設定
         */
        void SetAffectedLayers(Rendering::RenderLayer layers) { AffectedLayers = layers; }
        Rendering::RenderLayer GetAffectedLayers() const { return AffectedLayers; }

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
        void GetLightDirection(float& outX, float& outY, float& outZ) const;

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
        virtual bool BuildLightProxy(Rendering::LightProxy& outProxy) const;

    protected:
        /**
         * @brief 共通のLightProxyフィールドを設定するヘルパー
         * @param outProxy 出力先
         */
        void FillCommonLightProxy(Rendering::LightProxy& outProxy) const;

        // ========================================
        // リフレクションプロパティ
        // ========================================

        PROPERTY(Rendering::LightType, LightTypeProp)
        PROPERTY(float, Intensity)
        PROPERTY(bool, bCastShadows)
        PROPERTY(bool, bLightVisible)
        PROPERTY(float, ShadowBias)
        PROPERTY(uint32_t, ShadowMapResolution)
        PROPERTY(Rendering::RenderLayer, AffectedLayers)

        // ========================================
        // 内部キャッシュ（リフレクション対象外）
        // ========================================

        float m_LightColor[3] = {1.0f, 1.0f, 1.0f};
        float m_LightDirection[3] = {0.0f, -1.0f, 0.0f};
    };

    // LightComponentへのスマートポインタ
    using LightComponentPtr = Container::TSharedPtr<LightComponent>;
    using LightComponentWeakPtr = Container::TWeakPtr<LightComponent>;

} // namespace NorvesLib::Core::Component
