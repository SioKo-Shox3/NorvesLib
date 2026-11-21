#include "AI/Steering/SteeringAgentComponent.h"

#include "AI/AIModule.h"
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
        UnregisterFromModule();
    }

    void SteeringAgentComponent::Initialize()
    {
        Core::Component::Component::Initialize();
        if (auto* module = Module())
        {
            module->RegisterAgent(*this);
        }
    }

    void SteeringAgentComponent::UnregisterFromModule()
    {
        if (m_bRegistered)
        {
            if (auto* module = Module())
            {
                module->UnregisterAgent(*this);
            }
        }
    }

    void SteeringAgentComponent::EndPlay()
    {
        UnregisterFromModule();
        Core::Component::Component::EndPlay();
    }

    void SteeringAgentComponent::Finalize()
    {
        UnregisterFromModule();
        Core::Component::Component::Finalize();
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
