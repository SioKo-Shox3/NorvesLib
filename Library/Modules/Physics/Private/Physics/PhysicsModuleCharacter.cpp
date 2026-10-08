#include "CoreTypes.h"
#include "Object/Entity.h"
#include "Physics/CharacterBodyComponent.h"
#include "Physics/CharacterPlatformMotion.h"
#include "Physics/ColliderComponent.h"
#include "Physics/PhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include <cmath>
namespace NorvesLib::Modules::Physics
{
    namespace
    {
        bool FiniteCharacterVector(const Math::Vector3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    } // namespace
    EPhysicsResult PhysicsModule::ValidateCharacterAccess(const CharacterBodyComponent& character) const
    {
        if (!IsOwnerThread())
            return EPhysicsResult::WrongThread;
        if (!m_bInitialized)
            return EPhysicsResult::NotRegistered;
        const auto* owner = character.GetOwner();
        if (!owner || !owner->GetWorld() || owner->IsPendingDestroy() || character.HasFlag(Core::OF_PendingDestroy))
            return EPhysicsResult::NotRegistered;
        if (owner->GetComponent<CharacterBodyComponent>() != &character)
            return EPhysicsResult::Duplicate;
        return EPhysicsResult::Success;
    }
    void PhysicsModule::ProcessCharacterBodies(float dt, CharacterBodyComponent* onlyVariable)
    {
        // 全ComponentのFixedTickと通常body積分の後。足場の登録順で1frame遅れない。
        // Teleportを先に全件反映し、他のcharacterも更新後のproxyを見る。
        for (auto& body : m_BodySlots)
        {
            if (!body.bOccupied || !body.Owner)
                continue;
            auto* c = body.Owner->GetComponent<CharacterBodyComponent>();
            if (!c || (onlyVariable ? c != onlyVariable : c->m_DriveMode != CharacterDriveMode::Fixed))
                continue;
            c->m_bTeleportedStep = false;
            if (!body.bActive || !c->IsActive() || !c->IsTickEnabled() || !body.Owner->IsTickEnabled() ||
                c->HasFlag(Core::OF_PendingDestroy) || !c->m_bFixedRequest)
            {
                c->m_bFixedRequest = false;
                c->ResetSimulationState();
                continue;
            }
            auto* collider = body.Owner->GetComponent<ColliderComponent>();
            if (body.Owner->GetParentEntity() != nullptr || body.Component->m_BodyType != EPhysicsBodyType::Kinematic ||
                !collider || ValidateCollider(*collider) != EPhysicsResult::Success ||
                collider->m_Shape != ColliderComponent::EColliderShape::Capsule || collider->m_bTrigger ||
                !IsColliderLifecycleActive(m_ColliderSlots[collider->m_ColliderHandle.Index]))
            {
                c->m_bFixedRequest = false;
                c->ResetSimulationState();
                c->m_State.Result = EPhysicsResult::InvalidState;
                continue;
            }
            if ((c->m_BoundBody.IsValid() && !(c->m_BoundBody == body.Component->m_BodyHandle)) ||
                (c->m_BoundCollider.IsValid() && !(c->m_BoundCollider == collider->m_ColliderHandle)))
                c->ResetMotion(false);
            c->m_BoundBody = body.Component->m_BodyHandle;
            c->m_BoundCollider = collider->m_ColliderHandle;
            if (c->m_bTeleport)
            {
                body.Owner->SetPosition(c->m_PendingTeleport);
                body.Owner->ResetRenderInterpolation();
                body.Component->m_LinearVelocity = {};
                body.PreStepPosition = c->m_PendingTeleport;
                const auto serial = c->m_State.StepSerial;
                c->m_State = {};
                c->m_State.StepSerial = serial;
                c->m_VerticalSpeed = 0;
                c->m_bAnchor = false;
                c->m_bJump = false;
                c->m_bLaunch = false;
                c->m_bLandedEvent = false;
                c->m_bTeleport = false;
                c->m_bTeleportedStep = true;
            }
        }
        for (auto& body : m_BodySlots)
        {
            if (!body.bOccupied || !body.bActive || !body.Owner)
                continue;
            auto* c = body.Owner->GetComponent<CharacterBodyComponent>();
            if (!c || (onlyVariable ? c != onlyVariable : c->m_DriveMode != CharacterDriveMode::Fixed) ||
                !c->m_bFixedRequest)
                continue;
            c->m_bFixedRequest = false;
            if (c->m_Mode == CharacterMovementMode::External)
            {
                c->ResetMotion(false);
                c->m_State.bReady = true;
                c->m_State.Result = EPhysicsResult::Success;
                c->m_State.Velocity = body.Component->m_LinearVelocity;
                if (c->m_State.StepSerial != UINT64_MAX)
                    ++c->m_State.StepSerial;
                continue;
            }
            BuildBroadphase(m_WorkingBroadphase, true);
            const PhysicsShapeProxy* self = nullptr;
            for (const auto& proxy : m_WorkingBroadphase.GetProxies())
                if (proxy.Collider == c->m_BoundCollider)
                {
                    self = &proxy;
                    break;
                }
            const auto initial = GetFreshWorldTransform(*body.Owner);
            if (!self || self->Shape != EPhysicsProxyShape::Capsule)
            {
                c->ResetSimulationState();
                c->m_State.Result = EPhysicsResult::InvalidState;
                continue;
            }
            const auto axis = self->Capsule.PointB - self->Capsule.PointA;
            const auto center = (self->Capsule.PointA + self->Capsule.PointB) * .5f;
            if (std::fabs(axis.x) > 1e-4f || std::fabs(axis.z) > 1e-4f ||
                std::fabs(center.x - initial.position.x) > 1e-4f || std::fabs(center.z - initial.position.z) > 1e-4f)
            {
                c->ResetSimulationState();
                c->m_State.Result = EPhysicsResult::InvalidState;
                continue;
            }
            m_CharacterQueryProxies.clear();
            for (const auto& proxy : m_WorkingBroadphase.GetProxies())
                if (Core::Scene::CanPhysicsLayersInteract(self->Layer, self->Mask, proxy.Layer, proxy.Mask))
                    m_CharacterQueryProxies.push_back(proxy);
            CharacterMoveRequest request;
            request.Shape = self->Capsule;
            request.Filter.LayerMask = self->Mask;
            request.Filter.IgnoreColliders[0] = self->Collider;
            request.Filter.IgnoreBodies[0] = self->Body;
            request.bWasGrounded = c->m_State.bGrounded;
            float platformYawDelta = 0;
            if (c->m_bAnchor && c->m_State.GroundCollider.IsValid())
            {
                const auto handle = c->m_State.GroundCollider;
                if (handle.Index < m_ColliderSlots.size())
                {
                    const auto& slot = m_ColliderSlots[handle.Index];
                    Math::Vector3 anchor;
                    float yaw;
                    if (slot.bOccupied && slot.Generation == handle.Generation && IsColliderLifecycleActive(slot) &&
                        !slot.Component->m_bTrigger &&
                        Core::Scene::CanPhysicsLayersInteract(self->Layer, self->Mask, slot.Component->m_CollisionLayer,
                                                              slot.Component->m_CollisionMask) &&
                        ResolveCharacterAnchor(GetFreshWorldTransform(*slot.Owner), slot.Component->m_LocalPose,
                                               c->m_AnchorLocal, anchor, yaw))
                    {
                        request.PlatformDisplacement = anchor - c->m_PreviousAnchorWorld;
                        request.PlatformCollider = handle;
                        platformYawDelta = std::remainder(yaw - c->m_PlatformYaw, Math::Constants::TWO_PI);
                    }
                    else
                        c->m_bAnchor = false;
                }
                else
                    c->m_bAnchor = false;
            }
            float vertical = c->m_State.bGrounded && c->m_VerticalSpeed < 0 ? 0 : c->m_VerticalSpeed;
            const bool jumping = c->m_bJump && (c->m_State.bGrounded || c->m_bLaunch);
            if (jumping)
            {
                vertical = c->m_JumpSpeed;
                request.bWasGrounded = false;
            }
            const float gravity = 9.81f * c->m_Settings.GravityScale;
            request.Displacement = c->m_DesiredVelocity * dt + c->m_PendingDisplacement;
            request.Displacement.y += vertical * dt - .5f * gravity * dt * dt;
            request.bAllowGroundSnap = !jumping && !c->m_bTeleportedStep;
            request.bAllowStep = !jumping;
            const float requestedYaw = c->m_PendingYaw + platformYawDelta;
            c->m_PendingDisplacement = {};
            c->m_PendingYaw = 0;
            c->m_bJump = false;
            c->m_bLaunch = false;
            CharacterMoveResult moved;
            const auto status = CharacterMover::Move(m_CharacterQueryProxies, c->m_Settings.Movement, request,
                                                     m_CharacterScratch, moved);
            if (status != Core::Scene::EPhysicsSceneQueryResult::Success)
            {
                c->ResetMotion(false);
                c->m_State.Result = EPhysicsResult::InvalidState;
                c->m_State.QueryResult = status;
                continue;
            }
            auto target = initial;
            target.position += moved.Displacement;
            target.rotation = Math::Quaternion(Math::Vector3::UnitY, requestedYaw) * target.rotation;
            const auto velocity = body.Component->m_LinearVelocity + moved.Displacement / dt;
            if (!FiniteCharacterVector(target.position) || !FiniteCharacterVector(velocity))
            {
                c->ResetMotion(false);
                c->m_State.Result = EPhysicsResult::InvalidArgument;
                continue;
            }
            const bool landed = !c->m_State.bGrounded && moved.bGrounded;
            body.Owner->SetWorldTransform(target);
            // IntegrateDynamicsが記録した通常kinematic移動に、characterの移動分だけ足す。
            body.Component->m_LinearVelocity = velocity;
            c->m_VerticalSpeed = moved.bGrounded || moved.bHitCeiling || moved.bStuck ? 0 : vertical - gravity * dt;
            c->m_State.bReady = true;
            c->m_State.Result = EPhysicsResult::Success;
            c->m_State.QueryResult = status;
            c->m_State.bGrounded = moved.bGrounded;
            c->m_State.bStuck = moved.bStuck;
            c->m_State.Velocity = velocity;
            c->m_State.PlatformVelocity = moved.GroundCollider == request.PlatformCollider
                                              ? request.PlatformDisplacement / dt
                                              : Math::Vector3::Zero;
            c->m_State.GroundNormal = moved.GroundNormal;
            c->m_State.GroundPoint = moved.GroundPoint;
            c->m_State.GroundCollider = moved.GroundCollider;
            c->m_State.GroundBody = moved.GroundBody;
            if (c->m_State.StepSerial != UINT64_MAX)
                ++c->m_State.StepSerial;
            c->m_bLandedEvent = landed;
            c->m_bAnchor = false;
            if (moved.bGrounded && moved.GroundCollider.Index < m_ColliderSlots.size())
            {
                const auto& slot = m_ColliderSlots[moved.GroundCollider.Index];
                if (slot.bOccupied && slot.Generation == moved.GroundCollider.Generation &&
                    IsColliderLifecycleActive(slot))
                {
                    c->m_bAnchor =
                        CaptureCharacterAnchor(GetFreshWorldTransform(*slot.Owner), slot.Component->m_LocalPose,
                                               moved.GroundPoint, c->m_AnchorLocal, c->m_PlatformYaw);
                    c->m_PreviousAnchorWorld = moved.GroundPoint;
                }
            }
        }
    }
    void PhysicsModule::ProcessVariableCharacter(CharacterBodyComponent& character, float dt)
    {
        if (ValidateCharacterAccess(character) != EPhysicsResult::Success || m_bFixedTickInProgress ||
            m_bVariableCharacterInProgress || m_bCharacterInputInProgress ||
            character.m_DriveMode != CharacterDriveMode::Variable || !std::isfinite(dt) || dt <= 0)
            return;
        struct Guard
        {
            bool& Flag;
            Guard(bool& flag) : Flag(flag)
            {
                Flag = true;
            }
            ~Guard()
            {
                Flag = false;
            }
        } guard(m_bVariableCharacterInProgress);
        ReconcileActiveStates();
        // 可変経路では通常kinematic積分を重ねず、今回の実変位だけを観測速度とする。
        for (auto& body : m_BodySlots)
            if (body.bOccupied && body.Owner == character.GetOwner())
                body.Component->m_LinearVelocity = {};
        ProcessCharacterBodies(dt, &character);
        PhysicsBroadphase candidate;
        BuildBroadphase(candidate, true);
        m_PublishedBroadphase = std::move(candidate);
        // 可変の初回snapshotも公開するが、固定stepのsequenceは増やさない。
        m_bHasPublishedSnapshot = true;
        DispatchCharacterEvents();
    }
    void PhysicsModule::DispatchCharacterEvents()
    {
        for (size_t i = 0; i < m_BodySlots.size(); ++i)
        {
            auto& body = m_BodySlots[i];
            if (!body.bOccupied || !body.Owner || !IsBodyLifecycleActive(body))
                continue;
            auto* c = body.Owner->GetComponent<CharacterBodyComponent>();
            if (!c || !c->m_bLandedEvent || !c->IsActive() || c->HasFlag(Core::OF_PendingDestroy))
                continue;
            c->m_bLandedEvent = false;
            const auto event = c->m_State;
            const auto listeners = c->OnLanded;
            // callbackがcomponentを外しても、以後そのpointerを読まない。
            listeners.Broadcast(event);
        }
    }
} // namespace NorvesLib::Modules::Physics
