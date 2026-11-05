#include "Component/ViewClearanceComponent.h"

#include "Object/Entity.h"
#include "Object/World.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(ViewClearanceComponent, Component)

    ViewClearanceComponent::ViewClearanceComponent()
        : Component()
    {
        SetDefaults();
    }

    ViewClearanceComponent::ViewClearanceComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    ViewClearanceComponent::ViewClearanceComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    ViewClearanceComponent::~ViewClearanceComponent() = default;

    void ViewClearanceComponent::SetDefaults()
    {
        const Rendering::ViewClearanceParameters defaults = Rendering::MakeDefaultViewClearanceParameters();
        TargetRadius = defaults.TargetRadius;
        CameraRadius = defaults.CameraRadius;
        FeatherRatio = defaults.FeatherRatio;
        NearFadeStart = defaults.NearFadeStart;
        NearFadeEnd = defaults.NearFadeEnd;
        MinHeightScale = defaults.MinHeightScale;
        bAffectGrass = defaults.bAffectGrass;
        bAffectOccluders = defaults.bAffectOccluders;
        bHeroSilhouette = defaults.bHeroSilhouette;
        SetTickGroup(ETickGroup::Camera);
    }

    void ViewClearanceComponent::Initialize()
    {
        Component::Initialize();
    }

    void ViewClearanceComponent::Finalize()
    {
        Component::Finalize();
    }

    void ViewClearanceComponent::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }
        Component::BeginPlay();

        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (world && !world->FindComponentTick(StaticClass()))
        {
            ComponentTickRegistration tick;
            tick.Name = "ViewClearance.Strength";
            tick.Run = [](Component& component, float deltaTime)
            { static_cast<ViewClearanceComponent&>(component).TickStrength(deltaTime); };
            tick.TimeChannel = Engine::TimeChannel::Unscaled;
            world->RegisterComponentTick(StaticClass(), tick);
        }
        SetTickEnabled(true);
    }

    void ViewClearanceComponent::EndPlay()
    {
        Component::EndPlay();
    }

    void ViewClearanceComponent::SetMode(ViewClearanceMode mode, bool bImmediate)
    {
        m_Mode = mode;
        if (bImmediate)
        {
            m_Strength = m_Mode == ViewClearanceMode::Gameplay ? 1.0f : 0.0f;
            MarkRenderStateDirty();
        }
    }

    void ViewClearanceComponent::TickStrength(float deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            return;
        }

        const float targetStrength = m_Mode == ViewClearanceMode::Gameplay ? 1.0f : 0.0f;
        const float step = std::min(deltaTime, StrengthBlendSeconds) / StrengthBlendSeconds;
        const float nextStrength = m_Strength < targetStrength
            ? std::min(m_Strength + step, targetStrength)
            : std::max(m_Strength - step, targetStrength);
        if (nextStrength != m_Strength)
        {
            m_Strength = nextStrength;
            MarkRenderStateDirty();
        }
    }

    bool ViewClearanceComponent::BuildViewClearance(
        const Math::Vector3& cameraPosition, const Math::Vector3& targetPosition,
        Rendering::ViewClearanceParameters& outParameters) const
    {
        Rendering::ViewClearanceParameters parameters = Rendering::MakeDefaultViewClearanceParameters();
        parameters.bEnabled = IsActive();
        parameters.Strength = m_Strength;
        parameters.CameraPosition = cameraPosition;
        parameters.TargetPosition = targetPosition;
        parameters.TargetRadius = TargetRadius.Get();
        parameters.CameraRadius = CameraRadius.Get();
        parameters.FeatherRatio = FeatherRatio.Get();
        parameters.NearFadeStart = NearFadeStart.Get();
        parameters.NearFadeEnd = NearFadeEnd.Get();
        parameters.MinHeightScale = MinHeightScale.Get();
        parameters.bAffectGrass = bAffectGrass.Get();
        parameters.bAffectOccluders = bAffectOccluders.Get();
        parameters.bHeroSilhouette = bHeroSilhouette.Get();
        outParameters = Rendering::SanitizeViewClearanceParameters(parameters);
        return outParameters.bEnabled;
    }
} // namespace NorvesLib::Core::Component
