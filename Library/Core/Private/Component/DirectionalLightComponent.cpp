#include "Component/DirectionalLightComponent.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(DirectionalLightComponent, LightComponent)

    DirectionalLightComponent::DirectionalLightComponent()
        : LightComponent()
    {
        getLightTypeProp().Set(Rendering::LightType::Directional);
        getIntensityUnit().Set(LightIntensityUnit::Lux);
    }

    DirectionalLightComponent::DirectionalLightComponent(const FieldInitializer* initializer)
        : LightComponent(initializer)
    {
        getLightTypeProp().Set(Rendering::LightType::Directional);
        getIntensityUnit().Set(LightIntensityUnit::Lux);
    }

    DirectionalLightComponent::DirectionalLightComponent(const IUnknown* sourceObject)
        : LightComponent(sourceObject)
    {
        getLightTypeProp().Set(Rendering::LightType::Directional);
        getIntensityUnit().Set(LightIntensityUnit::Lux);
    }

    DirectionalLightComponent::~DirectionalLightComponent()
    {
    }

    void DirectionalLightComponent::Initialize()
    {
        LightComponent::Initialize();
    }

    void DirectionalLightComponent::Tick(float deltaTime)
    {
        (void)deltaTime;
    }

    // ========================================
    // LightProxy構築
    // ========================================

    bool DirectionalLightComponent::BuildLightProxy(Rendering::LightProxy& outProxy) const
    {
        // 基底クラスの共通チェックとフィールド設定
        if (!LightComponent::BuildLightProxy(outProxy))
        {
            return false;
        }

        outProxy.Type = Rendering::LightType::Directional;

        return true;
    }

} // namespace NorvesLib::Core::Component

