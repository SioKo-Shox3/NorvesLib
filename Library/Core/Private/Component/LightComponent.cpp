#include "Component/LightComponent.h"
#include "Object/WorldObject.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(LightComponent, Component)

    LightComponent::LightComponent()
        : Component()
    {
        LightTypeProp = Rendering::LightType::Directional;
        Intensity = 1.0f;
        bCastShadows = false;
        bLightVisible = true;
        ShadowBias = 0.005f;
        ShadowMapResolution = 1024;
        AffectedLayers = Rendering::RenderLayer::All;
    }

    LightComponent::LightComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        LightTypeProp = Rendering::LightType::Directional;
        Intensity = 1.0f;
        bCastShadows = false;
        bLightVisible = true;
        ShadowBias = 0.005f;
        ShadowMapResolution = 1024;
        AffectedLayers = Rendering::RenderLayer::All;
    }

    LightComponent::LightComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        LightTypeProp = Rendering::LightType::Directional;
        Intensity = 1.0f;
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
        m_LightColor[0] = r;
        m_LightColor[1] = g;
        m_LightColor[2] = b;
    }

    void LightComponent::GetLightColor(float& outR, float& outG, float& outB) const
    {
        outR = m_LightColor[0];
        outG = m_LightColor[1];
        outB = m_LightColor[2];
    }

    void LightComponent::SetIntensity(float intensity)
    {
        Intensity = intensity;
    }

    void LightComponent::SetLightDirection(float x, float y, float z)
    {
        m_LightDirection[0] = x;
        m_LightDirection[1] = y;
        m_LightDirection[2] = z;
    }

    void LightComponent::GetLightDirection(float& outX, float& outY, float& outZ) const
    {
        outX = m_LightDirection[0];
        outY = m_LightDirection[1];
        outZ = m_LightDirection[2];
    }

    // ========================================
    // LightProxy構築
    // ========================================

    void LightComponent::FillCommonLightProxy(Rendering::LightProxy& outProxy) const
    {
        outProxy.Type = LightTypeProp;
        outProxy.ColorR = m_LightColor[0];
        outProxy.ColorG = m_LightColor[1];
        outProxy.ColorB = m_LightColor[2];
        outProxy.Intensity = Intensity;
        outProxy.bCastShadows = bCastShadows;
        outProxy.bVisible = bLightVisible;
        outProxy.ShadowBias = ShadowBias;
        outProxy.ShadowMapResolution = ShadowMapResolution;
        outProxy.AffectedLayers = AffectedLayers;

        // ライト方向（ディレクショナルライト / スポットライト用）
        outProxy.DirectionX = m_LightDirection[0];
        outProxy.DirectionY = m_LightDirection[1];
        outProxy.DirectionZ = m_LightDirection[2];
    }

    bool LightComponent::BuildLightProxy(Rendering::LightProxy& outProxy) const
    {
        if (!IsEnabled() || !bLightVisible)
        {
            return false;
        }

        FillCommonLightProxy(outProxy);

        // LightIdはComponentIdを使用（一意性を保証）
        outProxy.LightId = GetComponentId();

        return true;
    }

} // namespace NorvesLib::Core::Component
