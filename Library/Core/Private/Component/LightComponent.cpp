#include "Component/LightComponent.h"
#include "Object/Entity.h"
#include "Logging/LogMacros.h"
#include <cmath>

namespace NorvesLib::Core::Component
{
    namespace
    {
        bool IsIntensityUnitAllowed(Rendering::LightType lightType, LightIntensityUnit unit)
        {
            if (lightType == Rendering::LightType::Directional)
            {
                return unit == LightIntensityUnit::Lux;
            }

            if (lightType == Rendering::LightType::Point || lightType == Rendering::LightType::Spot)
            {
                return unit == LightIntensityUnit::Candela || unit == LightIntensityUnit::Lumen;
            }

            return false;
        }
    }

    IMPLEMENT_CLASS(LightComponent, Component)

    LightComponent::LightComponent()
        : Component()
    {
        LightTypeProp = Rendering::LightType::Directional;
        Intensity = 1.0f;
        IntensityUnit = LightIntensityUnit::Lux;
        LightColor = Math::Vector3(1.0f, 1.0f, 1.0f);
        bCastShadows = false;
        bLightVisible = true;
        ShadowBias = 0.005f;
        ShadowMapResolution = 1024;
        AffectedLayers = Rendering::RenderLayer::All;
    }

    LightComponent::LightComponent(const FieldInitializer *initializer)
        : Component(initializer)
    {
        LightTypeProp = Rendering::LightType::Directional;
        Intensity = 1.0f;
        IntensityUnit = LightIntensityUnit::Lux;
        LightColor = Math::Vector3(1.0f, 1.0f, 1.0f);
        bCastShadows = false;
        bLightVisible = true;
        ShadowBias = 0.005f;
        ShadowMapResolution = 1024;
        AffectedLayers = Rendering::RenderLayer::All;
    }

    LightComponent::LightComponent(const IUnknown *sourceObject)
        : Component(sourceObject)
    {
        LightTypeProp = Rendering::LightType::Directional;
        Intensity = 1.0f;
        IntensityUnit = LightIntensityUnit::Lux;
        LightColor = Math::Vector3(1.0f, 1.0f, 1.0f);
        bCastShadows = false;
        bLightVisible = true;
        ShadowBias = 0.005f;
        ShadowMapResolution = 1024;
        AffectedLayers = Rendering::RenderLayer::All;
    }

    LightComponent::~LightComponent()
    {
    }

    void LightComponent::Initialize()
    {
        Component::Initialize();
    }

    void LightComponent::Finalize()
    {
        Component::Finalize();
    }

    void LightComponent::BeginPlay()
    {
        Component::BeginPlay();
    }

    void LightComponent::EndPlay()
    {
        Component::EndPlay();
    }

    void LightComponent::Tick(float deltaTime)
    {
        (void)deltaTime;
    }

    // ========================================
    // ライト共通プロパティ
    // ========================================

    void LightComponent::SetLightColor(float r, float g, float b)
    {
        LightColor = Math::Vector3(r, g, b);
        MarkRenderStateDirty();
    }

    void LightComponent::GetLightColor(float &outR, float &outG, float &outB) const
    {
        const Math::Vector3& lightColor = LightColor.Get();
        outR = lightColor.x;
        outG = lightColor.y;
        outB = lightColor.z;
    }

    void LightComponent::SetIntensity(float intensity)
    {
        Intensity = intensity;
        MarkRenderStateDirty();
    }

    bool LightComponent::SetIntensityUnit(LightIntensityUnit unit)
    {
        if (unit != LightIntensityUnit::Lux &&
            unit != LightIntensityUnit::Candela &&
            unit != LightIntensityUnit::Lumen)
        {
            NORVES_LOG_WARNING("Light", "Rejected unknown light intensity unit");
            return false;
        }

        if (IntensityUnit.Get() != unit)
        {
            IntensityUnit = unit;
            MarkRenderStateDirty();
        }
        return true;
    }

    void LightComponent::SetLightDirection(float x, float y, float z)
    {
        m_LightDirection[0] = x;
        m_LightDirection[1] = y;
        m_LightDirection[2] = z;
        MarkRenderStateDirty();
    }

    void LightComponent::GetLightDirection(float &outX, float &outY, float &outZ) const
    {
        outX = m_LightDirection[0];
        outY = m_LightDirection[1];
        outZ = m_LightDirection[2];
    }

    // ========================================
    // LightProxy構築
    // ========================================

    bool LightComponent::FillCommonLightProxy(Rendering::LightProxy &outProxy) const
    {
        const float intensity = Intensity.Get();
        const Rendering::LightType lightType = LightTypeProp.Get();
        const LightIntensityUnit intensityUnit = IntensityUnit.Get();
        const Math::Vector3& lightColor = LightColor.Get();

        if (!std::isfinite(intensity) || intensity < 0.0f)
        {
            NORVES_LOG_WARNING("Light", "Rejected non-finite or negative light intensity");
            return false;
        }

        if (!IsIntensityUnitAllowed(lightType, intensityUnit))
        {
            NORVES_LOG_WARNING("Light", "Rejected invalid intensity unit for light type");
            return false;
        }

        if (!std::isfinite(lightColor.x) || !std::isfinite(lightColor.y) ||
            !std::isfinite(lightColor.z) || lightColor.x < 0.0f ||
            lightColor.y < 0.0f || lightColor.z < 0.0f)
        {
            NORVES_LOG_WARNING("Light", "Rejected invalid light color");
            return false;
        }

        const float luminance = 0.2126f * lightColor.x +
                                0.7152f * lightColor.y +
                                0.0722f * lightColor.z;
        if (!std::isfinite(luminance) || luminance <= 1.0e-6f)
        {
            NORVES_LOG_WARNING("Light", "Rejected light color with non-positive luminance");
            return false;
        }

        outProxy.Type = lightType;
        outProxy.ColorR = lightColor.x / luminance;
        outProxy.ColorG = lightColor.y / luminance;
        outProxy.ColorB = lightColor.z / luminance;
        outProxy.CanonicalIntensity = intensity;
        outProxy.bCastShadows = bCastShadows;
        outProxy.bVisible = bLightVisible;
        outProxy.ShadowBias = ShadowBias;
        outProxy.ShadowMapResolution = ShadowMapResolution;
        outProxy.AffectedLayers = AffectedLayers;

        // ライト方向（ディレクショナルライト / スポットライト用）
        outProxy.DirectionX = m_LightDirection[0];
        outProxy.DirectionY = m_LightDirection[1];
        outProxy.DirectionZ = m_LightDirection[2];
        return true;
    }

    bool LightComponent::BuildLightProxy(Rendering::LightProxy &outProxy) const
    {
        if (!IsEnabled() || !bLightVisible)
        {
            return false;
        }

        if (!FillCommonLightProxy(outProxy))
        {
            return false;
        }

        // LightIdはComponentIdを使用（一意性を保証）
        outProxy.LightId = GetComponentId();

        return true;
    }

} // namespace NorvesLib::Core::Component
