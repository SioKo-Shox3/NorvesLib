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
        getLightTypeProp().Set(Rendering::LightType::Directional);
        getIntensity().Set(1.0f);
        getIntensityUnit().Set(LightIntensityUnit::Lux);
        getLightColor().Set(Math::Vector3(1.0f, 1.0f, 1.0f));
        getbCastShadows().Set(false);
        getbLightVisible().Set(true);
        getShadowBias().Set(0.005f);
        getShadowMapResolution().Set(1024);
        getAffectedLayers().Set(Rendering::RenderLayer::All);
    }

    LightComponent::LightComponent(const FieldInitializer *initializer)
        : Component(initializer)
    {
        getLightTypeProp().Set(Rendering::LightType::Directional);
        getIntensity().Set(1.0f);
        getIntensityUnit().Set(LightIntensityUnit::Lux);
        getLightColor().Set(Math::Vector3(1.0f, 1.0f, 1.0f));
        getbCastShadows().Set(false);
        getbLightVisible().Set(true);
        getShadowBias().Set(0.005f);
        getShadowMapResolution().Set(1024);
        getAffectedLayers().Set(Rendering::RenderLayer::All);
    }

    LightComponent::LightComponent(const IUnknown *sourceObject)
        : Component(sourceObject)
    {
        getLightTypeProp().Set(Rendering::LightType::Directional);
        getIntensity().Set(1.0f);
        getIntensityUnit().Set(LightIntensityUnit::Lux);
        getLightColor().Set(Math::Vector3(1.0f, 1.0f, 1.0f));
        getbCastShadows().Set(false);
        getbLightVisible().Set(true);
        getShadowBias().Set(0.005f);
        getShadowMapResolution().Set(1024);
        getAffectedLayers().Set(Rendering::RenderLayer::All);
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
        getLightColor().Set(Math::Vector3(r, g, b));
        MarkRenderStateDirty();
    }

    void LightComponent::GetLightColor(float &outR, float &outG, float &outB) const
    {
        const Math::Vector3& lightColor = getLightColor().Get();
        outR = lightColor.x;
        outG = lightColor.y;
        outB = lightColor.z;
    }

    void LightComponent::SetIntensity(float intensity)
    {
        getIntensity().Set(intensity);
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

        if (getIntensityUnit().Get() != unit)
        {
            getIntensityUnit().Set(unit);
            MarkRenderStateDirty();
        }
        return true;
    }

    void LightComponent::LoadLightDirection(float (&out)[3]) const
    {
        out[0] = m_LightDirection[0];
        out[1] = m_LightDirection[1];
        out[2] = m_LightDirection[2];
        Math::Vector3 direction(out[0], out[1], out[2]);
        // 行が無い間（列を読めない間）は予備の値のまま返す
        if (Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsLightRender>().Type,
                                   static_cast<uint32_t>(offsetof(Ecs::EcsLightRender, Direction)),
                                   static_cast<uint32_t>(sizeof(direction)), &direction))
        {
            out[0] = direction.x;
            out[1] = direction.y;
            out[2] = direction.z;
        }
    }

    void LightComponent::StoreLightDirection(const float (&value)[3])
    {
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsLightRender>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            const Math::Vector3 direction(value[0], value[1], value[2]);
            Ecs::EcsWriteColumnPart(row, type, static_cast<uint32_t>(offsetof(Ecs::EcsLightRender, Direction)),
                                    static_cast<uint32_t>(sizeof(direction)), &direction);
            return;
        }
        m_LightDirection[0] = value[0];
        m_LightDirection[1] = value[1];
        m_LightDirection[2] = value[2];
    }

    void LightComponent::OnEcsRowAttached()
    {
        StoreLightDirection(m_LightDirection);
    }

    void LightComponent::OnEcsRowDetaching()
    {
        LoadLightDirection(m_LightDirection);
    }

    void LightComponent::SetLightDirection(float x, float y, float z)
    {
        const float value[3] = {x, y, z};
        StoreLightDirection(value);
        MarkRenderStateDirty();
    }

    void LightComponent::GetLightDirection(float &outX, float &outY, float &outZ) const
    {
        float value[3];
        LoadLightDirection(value);
        outX = value[0];
        outY = value[1];
        outZ = value[2];
    }

    // ========================================
    // LightProxy構築
    // ========================================

    bool LightComponent::FillCommonLightProxy(Rendering::LightProxy &outProxy) const
    {
        const float intensity = getIntensity().Get();
        const Rendering::LightType lightType = getLightTypeProp().Get();
        const LightIntensityUnit intensityUnit = getIntensityUnit().Get();
        const Math::Vector3& lightColor = getLightColor().Get();

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
        outProxy.bCastShadows = getbCastShadows().Get();
        outProxy.bVisible = getbLightVisible().Get();
        outProxy.ShadowBias = getShadowBias().Get();
        outProxy.ShadowMapResolution = getShadowMapResolution().Get();
        outProxy.AffectedLayers = getAffectedLayers().Get();

        // ライト方向（ディレクショナルライト / スポットライト用）
        float direction[3];
        LoadLightDirection(direction);
        outProxy.DirectionX = direction[0];
        outProxy.DirectionY = direction[1];
        outProxy.DirectionZ = direction[2];
        return true;
    }

    bool LightComponent::BuildLightProxy(Rendering::LightProxy &outProxy) const
    {
        if (!IsEnabled() || !getbLightVisible().Get())
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
