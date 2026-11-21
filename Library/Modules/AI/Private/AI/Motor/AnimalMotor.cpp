#include "AI/Motor/AnimalMotor.h"

#include "AI/Steering/SteeringAgentComponent.h"
#include "AI/Steering/SteeringSystem.h"
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
        // プレハブの Initialize では ObjectId が 0 のため、最初の固定歩で操舵の系が接続する。
        if (!world || owner.GetObjectId() == 0 || owner.IsPendingDestroy() || agent.IsPendingDestroy() ||
            owner.GetComponent<SteeringAgentComponent>() != &agent || !body || body->IsPendingDestroy())
        {
            if (IsBound())
            {
                Detach(&owner);
            }
            return false;
        }
        // 所有者・体・操舵 Component の ID はどれも書き換えられるので、どれかが変われば結び直す。
        if (IsBound() && (m_BoundOwnerId != owner.GetObjectId() || m_BoundBodyId != body->GetComponentId() ||
                          m_BoundAgentId != agent.GetComponentId()))
        {
            Detach(&owner);
        }
        if (!m_BoundOwnerHandle.IsValid())
        {
            // 行の作成予約中は所有者を引き直せない。解除できるようになる歩まで購読を保留する。
            m_BoundOwnerHandle = world->GetEntityHandle(owner);
            if (!m_BoundOwnerHandle.IsValid())
            {
                return false;
            }
        }
        if (IsBound())
        {
            return true;
        }

        static uint64_t s_NextConnectionId = 1;
        m_ConnectionId = s_NextConnectionId++;
        m_BoundOwnerId = owner.GetObjectId();
        m_BoundBodyId = body->GetComponentId();
        m_BoundAgentId = agent.GetComponentId();
        m_Before.Bind([world, ownerId = m_BoundOwnerId, agentId = m_BoundAgentId,
                       bodyId = m_BoundBodyId, connectionId = m_ConnectionId](float dt)
        {
            auto* entity = world->FindEntityByObjectId(ownerId);
            if (!entity || entity->IsPendingDestroy())
            {
                return;
            }
            auto* driver = entity->GetComponent<SteeringAgentComponent>();
            auto* current = entity->GetComponent<Physics::CharacterBodyComponent>();
            if (driver && driver->GetComponentId() == agentId &&
                current && !current->IsPendingDestroy() && current->GetComponentId() == bodyId &&
                driver->m_Motor && driver->m_Motor->m_ConnectionId == connectionId && SteeringSystem::CanSteer(*driver))
            {
                driver->StepMotor(dt);
            }
        });
        body->BeforeSimulation.Add(static_cast<const Core::Delegate<void, float>&>(m_Before));
        (void)body->SetDesiredVelocity(Math::Vector3::Zero);
        return true;
    }

    void AnimalMotor::Detach(Core::Entity* owner, const Core::World* fallbackWorld)
    {
        m_ConnectionId = 0;
        if (!owner && fallbackWorld)
        {
            owner = fallbackWorld->ResolveEntityHandle(m_BoundOwnerHandle);
        }
        // 体の ID が変わっていても、Bind ごとの識別子に一致する購読だけを外す。
        if (owner && IsBound())
        {
            for (auto* component : owner->GetComponents())
            {
                if (auto* body = Core::CastTo<Physics::CharacterBodyComponent>(component))
                {
                    body->BeforeSimulation.Remove(m_Before);
                }
            }
        }
        m_Before.Clear();
        m_BoundOwnerHandle = {};
        m_BoundOwnerId = 0;
        m_BoundBodyId = 0;
        m_BoundAgentId = 0;
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
