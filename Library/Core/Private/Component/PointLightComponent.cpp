#include "Component/PointLightComponent.h"
#include "Object/WorldObject.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(PointLightComponent, LightComponent)

    PointLightComponent::PointLightComponent()
        : LightComponent()
    {
        LightTypeProp = Rendering::LightType::Point;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
    }

    PointLightComponent::PointLightComponent(const FieldInitializer* initializer)
        : LightComponent(initializer)
    {
        LightTypeProp = Rendering::LightType::Point;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
    }

    PointLightComponent::PointLightComponent(const IUnknown* sourceObject)
        : LightComponent(sourceObject)
    {
        LightTypeProp = Rendering::LightType::Point;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
    }

    PointLightComponent::~PointLightComponent()
    {
    }

    void PointLightComponent::Initialize()
    {
        LightComponent::Initialize();
    }

    void PointLightComponent::Tick(float deltaTime)
    {
        (void)deltaTime;
        // ポイントライトは位置をオーナーのWorldObjectからSyncToSceneView時に取得するため
        // Tick内での特別な処理は不要
    }

    // ========================================
    // LightProxy構築
    // ========================================

    bool PointLightComponent::BuildLightProxy(Rendering::LightProxy& outProxy) const
    {
        // 基底クラスの共通チェックとフィールド設定
        if (!LightComponent::BuildLightProxy(outProxy))
        {
            return false;
        }

        // ポイントライト固有
        outProxy.Type = Rendering::LightType::Point;
        outProxy.Range = Range;
        outProxy.AttenuationConstant = AttenuationConstant;
        outProxy.AttenuationLinear = AttenuationLinear;
        outProxy.AttenuationQuadratic = AttenuationQuadratic;

        // オーナーのWorldObjectから位置を取得
        const WorldObject* owner = GetOwner();
        if (owner)
        {
            const auto& pos = owner->GetPosition();
            outProxy.PositionX = pos.x;
            outProxy.PositionY = pos.y;
            outProxy.PositionZ = pos.z;
        }

        return true;
    }

} // namespace NorvesLib::Core::Component
