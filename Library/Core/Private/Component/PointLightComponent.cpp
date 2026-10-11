#include "Component/PointLightComponent.h"
#include "Object/Entity.h"
#include "Logging/LogMacros.h"
#include "Math/MathTypes.h"
#include <cmath>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(PointLightComponent, LightComponent)

    PointLightComponent::PointLightComponent()
        : LightComponent()
    {
        getLightTypeProp().Set(Rendering::LightType::Point);
        getIntensityUnit().Set(LightIntensityUnit::Candela);
        getRange().Set(10.0f);
        getAttenuationConstant().Set(1.0f);
        getAttenuationLinear().Set(0.09f);
        getAttenuationQuadratic().Set(0.032f);
    }

    PointLightComponent::PointLightComponent(const FieldInitializer* initializer)
        : LightComponent(initializer)
    {
        getLightTypeProp().Set(Rendering::LightType::Point);
        getIntensityUnit().Set(LightIntensityUnit::Candela);
        getRange().Set(10.0f);
        getAttenuationConstant().Set(1.0f);
        getAttenuationLinear().Set(0.09f);
        getAttenuationQuadratic().Set(0.032f);
    }

    PointLightComponent::PointLightComponent(const IUnknown* sourceObject)
        : LightComponent(sourceObject)
    {
        getLightTypeProp().Set(Rendering::LightType::Point);
        getIntensityUnit().Set(LightIntensityUnit::Candela);
        getRange().Set(10.0f);
        getAttenuationConstant().Set(1.0f);
        getAttenuationLinear().Set(0.09f);
        getAttenuationQuadratic().Set(0.032f);
    }

    PointLightComponent::~PointLightComponent()
    {
    }

    void PointLightComponent::Initialize()
    {
        LightComponent::Initialize();
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
        outProxy.Range = getRange().Get();
        outProxy.AttenuationConstant = getAttenuationConstant().Get();
        outProxy.AttenuationLinear = getAttenuationLinear().Get();
        outProxy.AttenuationQuadratic = getAttenuationQuadratic().Get();

        if (getIntensityUnit().Get() == LightIntensityUnit::Lumen)
        {
            outProxy.CanonicalIntensity = getIntensity().Get() / (4.0f * Math::Constants::PI);
        }

        // オーナーのEntityから位置を取得
        const Entity* owner = GetOwner();
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
