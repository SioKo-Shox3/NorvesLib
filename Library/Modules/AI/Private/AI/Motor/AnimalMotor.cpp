#include "AI/Motor/AnimalMotor.h"

#include "AI/Steering/SteeringAgentComponent.h"
#include "Component/AnimatorComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Physics/CharacterBodyComponent.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::AI
{
    namespace Locomotion = Core::Locomotion;

    namespace
    {
        Core::Component::AnimatorComponent* FindAnimator(Core::Entity& owner, uint64_t& childId)
        {
            auto* animator = owner.GetComponent<Core::Component::AnimatorComponent>();
            if (animator && !animator->IsPendingDestroy())
            {
                return animator;
            }
            if (childId != 0 && owner.GetWorld())
            {
                auto* child = owner.GetWorld()->FindEntityByObjectId(childId);
                if (child && !child->IsPendingDestroy() && child->GetParentEntity() == &owner)
                {
                    animator = child->GetComponent<Core::Component::AnimatorComponent>();
                    if (animator && !animator->IsPendingDestroy())
                    {
                        return animator;
                    }
                }
            }
            childId = 0;
            for (auto* child : owner.GetChildEntities())
            {
                if (!child->IsPendingDestroy())
                {
                    animator = child->GetComponent<Core::Component::AnimatorComponent>();
                    if (animator && !animator->IsPendingDestroy())
                    {
                        childId = child->GetObjectId();
                        return animator;
                    }
                }
            }
            return nullptr;
        }
    }

    Physics::CharacterBodyComponent* AnimalMotor::GetBoundBody(const Core::Entity& owner) const
    {
        if (!IsBound() || owner.IsPendingDestroy() || owner.GetObjectId() != m_BoundOwnerId)
        {
            return nullptr;
        }
        auto* body = owner.GetComponent<Physics::CharacterBodyComponent>();
        return body && !body->IsPendingDestroy() && body->GetComponentId() == m_BoundBodyId ? body : nullptr;
    }

    bool AnimalMotor::EnsureBinding(Core::Entity& owner, SteeringAgentComponent& agent)
    {
        auto* world = owner.GetWorld();
        auto* body = owner.GetComponent<Physics::CharacterBodyComponent>();
        if (!world || owner.IsPendingDestroy() || agent.IsPendingDestroy() ||
            owner.GetComponent<SteeringAgentComponent>() != &agent || !body || body->IsPendingDestroy())
        {
            if (IsBound())
            {
                Detach(&owner);
            }
            return false;
        }
        if (m_BoundOwnerId == owner.GetObjectId() && m_BoundBodyId == body->GetComponentId())
        {
            return true;
        }

        Detach(&owner);
        m_BoundOwnerId = owner.GetObjectId();
        m_BoundBodyId = body->GetComponentId();
        m_Before.Bind([world, ownerId = m_BoundOwnerId, agentId = agent.GetComponentId(),
                       bodyId = m_BoundBodyId, generation = m_BindingGeneration](float dt)
        {
            auto* entity = world->FindEntityByObjectId(ownerId);
            if (!entity || entity->IsPendingDestroy())
            {
                return;
            }
            auto* driver = entity->GetComponent<SteeringAgentComponent>();
            auto* current = entity->GetComponent<Physics::CharacterBodyComponent>();
            if (driver && !driver->IsPendingDestroy() && driver->GetComponentId() == agentId &&
                current && !current->IsPendingDestroy() && current->GetComponentId() == bodyId &&
                driver->m_Motor && driver->m_Motor->m_BindingGeneration == generation && driver->IsActive())
            {
                driver->StepMotor(dt);
            }
        });
        body->BeforeSimulation.Add(static_cast<const Core::Delegate<void, float>&>(m_Before));
        (void)body->SetDesiredVelocity(Math::Vector3::Zero);
        return true;
    }

    void AnimalMotor::Detach(Core::Entity* owner)
    {
        ++m_BindingGeneration;
        if (owner && owner->GetObjectId() == m_BoundOwnerId)
        {
            auto* body = owner->GetComponent<Physics::CharacterBodyComponent>();
            if (body && body->GetComponentId() == m_BoundBodyId)
            {
                body->BeforeSimulation.Remove(m_Before);
            }
        }
        m_Before.Clear();
        m_BoundOwnerId = 0;
        m_BoundBodyId = 0;
        m_AnimatorChildId = 0;
        m_State = {};
        m_Output = {};
    }

    void AnimalMotor::SetParams(const Locomotion::QuadrupedLocomotionParams& params)
    {
        if (Locomotion::QuadrupedLocomotionModel::IsValidParams(params))
        {
            m_Params = params;
        }
    }

    Locomotion::LocomotionIntent AnimalMotor::MakeIntent(const Math::Vector3& desiredVelocity,
                                                         const Locomotion::QuadrupedLocomotionParams& params)
    {
        Locomotion::LocomotionIntent intent;
        const float speed = std::hypot(desiredVelocity.x, desiredVelocity.z);
        if (!std::isfinite(speed) || speed < 1e-3f)
        {
            return intent;
        }
        intent.bSprint = speed > params.Gaits[1].SpeedMax;
        const float speedMax = intent.bSprint ? params.Gaits[3].SpeedMax : params.Gaits[1].SpeedMax;
        const float magnitude = std::min(speed / speedMax, 1.0f);
        intent.Move = Math::Vector3(desiredVelocity.x / speed, 0, desiredVelocity.z / speed) * magnitude;
        return intent;
    }

    void AnimalMotor::Step(Core::Entity& owner, float dt, const Math::Vector3& desiredVelocity)
    {
        auto* body = owner.GetComponent<Physics::CharacterBodyComponent>();
        if (!std::isfinite(dt) || dt <= 0 || !body || !body->GetState().bReady)
        {
            return;
        }
        const auto& observed = body->GetState();
        Locomotion::LocomotionGroundInfo ground;
        ground.bReady = observed.bReady;
        ground.bGrounded = observed.bGrounded;
        ground.Normal = observed.GroundNormal;
        ground.Velocity = observed.Velocity;
        ground.PlatformVelocity = observed.PlatformVelocity;
        ground.Gravity = 9.81f * body->GetSettings().GravityScale;

        auto candidate = m_State;
        const auto forward = owner.GetWorldTransform().rotation * Math::Vector3::UnitZ;
        candidate.Yaw = std::atan2(forward.x, forward.z);
        const auto intent = MakeIntent(desiredVelocity, m_Params);
        Locomotion::LocomotionOutput output;
        if (Locomotion::QuadrupedLocomotionModel::Step(candidate, m_Params, intent, ground, dt, output) !=
            Locomotion::LocomotionStepStatus::Success)
        {
            return;
        }
        if (body->SetDesiredVelocity(Math::Vector3::Zero) == Physics::EPhysicsResult::Success &&
            body->MoveDelta(output.PlanarDisplacement, output.DeltaYaw) == Physics::EPhysicsResult::Success)
        {
            m_State = candidate;
            m_Output = output;
        }
    }

    void AnimalMotor::PublishAnimator(Core::Entity& owner)
    {
        using namespace Core::literals;
        auto* body = GetBoundBody(owner);
        if (!body || !m_State.bInitialized)
        {
            return;
        }
        auto* animator = FindAnimator(owner, m_AnimatorChildId);
        if (!animator)
        {
            return;
        }
        Core::Animation::AnimDriveSignals signals;
        signals.Speed = m_Output.Speed;
        signals.Velocity = body->GetState().Velocity;
        signals.TurnRate = m_Output.TurnRate;
        signals.bGrounded = m_Output.bGrounded;
        signals.GroundNormal = body->GetState().GroundNormal;
        (void)animator->SetDriveSignals(signals);
        (void)animator->SetInt(animator->FindParam("GaitId"_id), m_Output.Gait);
        constexpr Core::Identity weights[] = {"GaitWeight0"_id, "GaitWeight1"_id, "GaitWeight2"_id, "GaitWeight3"_id};
        for (unsigned i = 0; i < 4; ++i)
        {
            (void)animator->SetFloat(animator->FindParam(weights[i]), m_Output.GaitWeights[i]);
        }
        (void)animator->SetFloat(animator->FindParam("PitchAngle"_id), m_Output.Pitch);
        (void)animator->SetFloat(animator->FindParam("BankAngle"_id), m_Output.Bank);
    }
}
