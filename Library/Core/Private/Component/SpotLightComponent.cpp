#include "Component/SpotLightComponent.h"
#include "Object/Entity.h"
#include "Math/MathTypes.h"
#include <cmath>

namespace
{
    constexpr float MinConeAngleGapDegrees = 0.01f;
    constexpr float MaxConeAngleDegrees = 179.0f;

    float ClampConeAngleDegrees(float degrees)
    {
        if (degrees < 0.0f)
        {
            return 0.0f;
        }

        if (degrees > MaxConeAngleDegrees)
        {
            return MaxConeAngleDegrees;
        }

        return degrees;
    }

    void NormalizeConeAngles(float& innerDegrees, float& outerDegrees)
    {
        innerDegrees = ClampConeAngleDegrees(innerDegrees);
        outerDegrees = ClampConeAngleDegrees(outerDegrees);

        if (outerDegrees <= innerDegrees)
        {
            outerDegrees = innerDegrees + MinConeAngleGapDegrees;
            if (outerDegrees > MaxConeAngleDegrees)
            {
                outerDegrees = MaxConeAngleDegrees;
                innerDegrees = MaxConeAngleDegrees - MinConeAngleGapDegrees;
            }
        }
    }

    float DegreesToCos(float degrees)
    {
        return cosf(degrees * NorvesLib::Math::Constants::PI / 180.0f);
    }
}

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(SpotLightComponent, LightComponent)

    SpotLightComponent::SpotLightComponent()
        : LightComponent()
    {
        LightTypeProp = Rendering::LightType::Spot;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
        InnerConeAngle = 12.5f;
        OuterConeAngle = 17.5f;
    }

    SpotLightComponent::SpotLightComponent(const FieldInitializer* initializer)
        : LightComponent(initializer)
    {
        LightTypeProp = Rendering::LightType::Spot;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
        InnerConeAngle = 12.5f;
        OuterConeAngle = 17.5f;
    }

    SpotLightComponent::SpotLightComponent(const IUnknown* sourceObject)
        : LightComponent(sourceObject)
    {
        LightTypeProp = Rendering::LightType::Spot;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
        InnerConeAngle = 12.5f;
        OuterConeAngle = 17.5f;
    }

    SpotLightComponent::~SpotLightComponent()
    {
    }

    void SpotLightComponent::Initialize()
    {
        LightComponent::Initialize();
    }

    void SpotLightComponent::Tick(float deltaTime)
    {
        (void)deltaTime;
        // スポットライトは位置をオーナーのEntityからSyncToSceneView時に取得するため
        // Tick内での特別な処理は不要
    }

    void SpotLightComponent::EnsureConeAngleOrder()
    {
        NormalizeConeAngles(InnerConeAngle, OuterConeAngle);
    }

    // ========================================
    // LightProxy構築
    // ========================================

    bool SpotLightComponent::BuildLightProxy(Rendering::LightProxy& outProxy) const
    {
        // 基底クラスの共通チェックとフィールド設定
        if (!LightComponent::BuildLightProxy(outProxy))
        {
            return false;
        }

        // スポットライト固有
        outProxy.Type = Rendering::LightType::Spot;
        outProxy.Range = Range;
        outProxy.AttenuationConstant = AttenuationConstant;
        outProxy.AttenuationLinear = AttenuationLinear;
        outProxy.AttenuationQuadratic = AttenuationQuadratic;

        // オーナーのEntityから位置を取得
        const Entity* owner = GetOwner();
        if (owner)
        {
            const auto& pos = owner->GetPosition();
            outProxy.PositionX = pos.x;
            outProxy.PositionY = pos.y;
            outProxy.PositionZ = pos.z;
        }

        float innerDegrees = InnerConeAngle;
        float outerDegrees = OuterConeAngle;
        NormalizeConeAngles(innerDegrees, outerDegrees);

        outProxy.InnerConeAngle = DegreesToCos(innerDegrees);
        outProxy.OuterConeAngle = DegreesToCos(outerDegrees);

        return true;
    }

} // namespace NorvesLib::Core::Component

