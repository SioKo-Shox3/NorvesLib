#include "AI/Steering/SteeringAgentComponent.h"

#include "AI/AIModule.h"
#include "AI/Motor/AnimalMotor.h"
#include "Module/ModuleRegistry.h"

#include <cmath>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        AIModule* Module()
        {
            return dynamic_cast<AIModule*>(FindAIModule(Core::Module::GetModuleRegistry()));
        }

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsValidDistance(float distance)
        {
            return std::isfinite(distance) && distance >= 0.0f;
        }
    }

    IMPLEMENT_CLASS(SteeringAgentComponent, Core::Component::Component)

    SteeringAgentComponent::SteeringAgentComponent()
    {
        Dimension = ESteeringDimension::Ground;
        MaxSpeed = 6.0f;
        MaxAcceleration = 8.0f;
        TurnRateBySpeed = Math::Vector4(6.0f, 4.0f, 3.0f, 2.0f);
        NavProfileName = "Medium";
        MotorProfilePath = "AI/Locomotion/Wolf.json";
        WanderSeed = 1u;
    }

    SteeringAgentComponent::~SteeringAgentComponent()
    {
        DetachMotorAndUnregister();
    }

    void SteeringAgentComponent::Initialize()
    {
        Core::Component::Component::Initialize();
        if (auto* module = Module())
        {
            module->RegisterAgent(*this);
        }
        ReloadMotorProfile();
        EnsureMotorBinding();
    }

    void SteeringAgentComponent::DetachMotorAndUnregister()
    {
        if (!m_bRegistered && (!m_Motor || !m_Motor->IsBound()))
        {
            return;
        }
        auto* module = Module();
        if (m_Motor)
        {
            m_Motor->Detach(GetOwner(), module ? module->GetEngineWorld() : nullptr);
        }
        if (m_bRegistered && module)
        {
            module->UnregisterAgent(*this);
        }
    }

    void SteeringAgentComponent::EndPlay()
    {
        DetachMotorAndUnregister();
        Core::Component::Component::EndPlay();
    }

    void SteeringAgentComponent::Finalize()
    {
        DetachMotorAndUnregister();
        Core::Component::Component::Finalize();
    }

    void SteeringAgentComponent::OnPropertyApplied(const Core::ClassProperty& property)
    {
        Core::Component::Component::OnPropertyApplied(property);
        if (&property == s_PropertyPtr_MotorProfilePath)
        {
            ReloadMotorProfile();
        }
    }

    AnimalMotor& SteeringAgentComponent::GetOrCreateMotor()
    {
        if (!m_Motor)
        {
            m_Motor = Core::Container::MakeUnique<AnimalMotor>();
        }
        return *m_Motor;
    }

    bool SteeringAgentComponent::EnsureMotorBinding()
    {
        auto* owner = GetOwner();
        return owner && GetOrCreateMotor().EnsureBinding(*owner, *this);
    }

    void SteeringAgentComponent::StepMotor(float dt)
    {
        auto* owner = GetOwner();
        if (m_Motor && owner)
        {
            m_Motor->Step(*owner, dt, m_SteeringVelocity);
        }
    }

    void SteeringAgentComponent::PublishMotorVisual()
    {
        auto* owner = GetOwner();
        if (m_Motor && owner)
        {
            m_Motor->PublishAnimator(*owner);
        }
    }

    void SteeringAgentComponent::ReloadMotorProfile()
    {
        if (auto* module = Module())
        {
            GetOrCreateMotor().SetParams(module->GetLocomotionProfile(MotorProfilePath.Get()));
        }
    }

    bool SteeringAgentComponent::IsMotorBound() const
    {
        const auto* owner = GetOwner();
        return m_Motor && owner && m_Motor->IsBoundTo(*owner);
    }

    const Core::Locomotion::LocomotionOutput* SteeringAgentComponent::GetMotorOutput() const
    {
        return IsMotorBound() ? &m_Motor->GetOutput() : nullptr;
    }

    bool SteeringAgentComponent::AcceptRequest(SteeringRequest request)
    {
        request.Revision = m_Request.Revision == UINT32_MAX ? 1u : m_Request.Revision + 1u;
        m_Request = request;
        return true;
    }

    bool SteeringAgentComponent::RequestIdle()
    {
        return AcceptRequest({.Kind = ESteeringRequest::Idle});
    }

    bool SteeringAgentComponent::RequestArrive(const Math::Vector3& target, float stopRadius)
    {
        return IsFinite(target) && IsValidDistance(stopRadius) &&
               AcceptRequest({.Kind = ESteeringRequest::Arrive, .TargetPosition = target, .StopRadius = stopRadius});
    }

    bool SteeringAgentComponent::RequestMoveTo(const Math::Vector3& goal, float stopRadius)
    {
        return IsFinite(goal) && IsValidDistance(stopRadius) &&
               AcceptRequest({.Kind = ESteeringRequest::MoveTo, .TargetPosition = goal, .StopRadius = stopRadius});
    }

    bool SteeringAgentComponent::RequestPursue(Core::EntityHandle target)
    {
        return target.IsValid() && AcceptRequest({.Kind = ESteeringRequest::Pursue, .TargetEntity = target});
    }

    bool SteeringAgentComponent::RequestFlee(Core::EntityHandle threat)
    {
        return threat.IsValid() && AcceptRequest({.Kind = ESteeringRequest::Flee, .TargetEntity = threat});
    }

    bool SteeringAgentComponent::RequestKeepDistance(Core::EntityHandle target, float minDistance, float maxDistance)
    {
        if (!target.IsValid() || !IsValidDistance(minDistance) || !IsValidDistance(maxDistance) || minDistance > maxDistance)
        {
            return false;
        }
        return AcceptRequest({.Kind = ESteeringRequest::KeepDistance, .TargetEntity = target,
                              .MinDistance = minDistance, .MaxDistance = maxDistance});
    }

    bool SteeringAgentComponent::RequestWander()
    {
        return AcceptRequest({.Kind = ESteeringRequest::Wander});
    }

    bool SteeringAgentComponent::RequestFollowTrail(Core::EntityHandle leader, float followDistance)
    {
        return leader.IsValid() && IsValidDistance(followDistance) &&
               AcceptRequest({.Kind = ESteeringRequest::FollowTrail, .TargetEntity = leader, .FollowDistance = followDistance});
    }

    void SteeringAgentComponent::SetSteeringVelocity(const Math::Vector3& velocity)
    {
        m_SteeringVelocity = IsFinite(velocity) ? velocity : Math::Vector3{};
    }
}
