#pragma once

#include "Component.h"
#include "Rendering/ViewClearance.h"
#include <cstdint>

namespace NorvesLib::Core::Component
{
    enum class ViewClearanceMode : uint8_t
    {
        Gameplay,
        Cinematic
    };

    /**
     * @brief カメラと主人公の間の切り抜きと近接フェードの設定を保持します。
     *
     * 値と実行時の強さはカメラ単位で独立し、カットシーン用の別カメラも個別に切り替えられます。
     * タイムラインのトラックやショット定義は、SetMode と Strength の取得口を利用します。
     */
    class ViewClearanceComponent : public Component
    {
        REFLECTION_CLASS(ViewClearanceComponent, Component)

    public:
        ViewClearanceComponent();
        explicit ViewClearanceComponent(const FieldInitializer* initializer);
        explicit ViewClearanceComponent(const IUnknown* sourceObject);
        virtual ~ViewClearanceComponent();

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;

        static constexpr float StrengthBlendSeconds = 0.3f;

        void SetMode(ViewClearanceMode mode, bool bImmediate = false);
        ViewClearanceMode GetMode() const { return m_Mode; }
        float GetStrength() const { return m_Strength; }

        /** 強さを現在のモードの目標へ線形に近づけます。 */
        void TickStrength(float deltaTime);

        /** カメラと主人公のワールド座標を含む、検証済みの描画設定を構築します。 */
        bool BuildViewClearance(const Math::Vector3& cameraPosition,
                                const Math::Vector3& targetPosition,
                                Rendering::ViewClearanceParameters& outParameters) const;

    protected:
        PROPERTY(float, TargetRadius)
        PROPERTY(float, CameraRadius)
        PROPERTY(float, FeatherRatio)
        PROPERTY(float, NearFadeStart)
        PROPERTY(float, NearFadeEnd)
        PROPERTY(float, MinHeightScale)
        PROPERTY(bool, bAffectGrass)
        PROPERTY(bool, bAffectOccluders)
        PROPERTY(bool, bHeroSilhouette)

    private:
        void SetDefaults();

        ViewClearanceMode m_Mode = ViewClearanceMode::Gameplay;
        float m_Strength = 1.0f;
    };

    using ViewClearanceComponentPtr = Container::TSharedPtr<ViewClearanceComponent>;
    using ViewClearanceComponentWeakPtr = Container::TWeakPtr<ViewClearanceComponent>;
} // namespace NorvesLib::Core::Component
