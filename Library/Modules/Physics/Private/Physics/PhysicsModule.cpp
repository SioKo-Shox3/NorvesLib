#include "Physics/PhysicsModule.h"

#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "CoreTypes.h"
#include "Engine/Engine.h"
#include "Object/Entity.h"
#include "Object/IUnknown.h"
#include "Math/VectorUtils.h"

#include <cmath>

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        constexpr const char* kPhysicsModuleName = "NorvesPhysicsModule";

        bool IsFiniteVector(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFiniteTransform(const Math::Transform& transform)
        {
            return IsFiniteVector(transform.position)
                && IsFiniteVector(transform.scale)
                && std::isfinite(transform.rotation.x)
                && std::isfinite(transform.rotation.y)
                && std::isfinite(transform.rotation.z)
                && std::isfinite(transform.rotation.w)
                && std::fabs(transform.scale.x) > 0.0f
                && std::fabs(transform.scale.y) > 0.0f
                && std::fabs(transform.scale.z) > 0.0f
                && Math::VectorUtils::LengthSquared(Math::Vector3(
                    transform.rotation.x,
                    transform.rotation.y,
                    transform.rotation.z))
                    + transform.rotation.w * transform.rotation.w > Math::Constants::EPSILON;
        }

        bool IsFiniteRay(const Math::Ray& ray)
        {
            return IsFiniteVector(ray.Origin) && IsFiniteVector(ray.Direction);
        }

        bool NormalizeFiniteNonZeroDirection(const Math::Vector3& direction, Math::Vector3& outDirection)
        {
            const float maximumComponent = std::fmaxf(
                std::fabs(direction.x),
                std::fmaxf(std::fabs(direction.y), std::fabs(direction.z)));
            if (maximumComponent == 0.0f)
            {
                return false;
            }

            const Math::Vector3 scaledDirection = direction / maximumComponent;
            const float length = std::sqrt(Math::VectorUtils::LengthSquared(scaledDirection));
            if (length == 0.0f)
            {
                return false;
            }

            outDirection = scaledDirection / length;
            return true;
        }

        bool IsFiniteSphere(const Math::Sphere& sphere)
        {
            return IsFiniteVector(sphere.Center) && std::isfinite(sphere.Radius) && sphere.Radius >= 0.0f;
        }

        bool IsFiniteBox(const Math::OBB& box)
        {
            return IsFiniteVector(box.Center) && IsFiniteVector(box.HalfExtents)
                && box.HalfExtents.x >= 0.0f && box.HalfExtents.y >= 0.0f && box.HalfExtents.z >= 0.0f
                && IsFiniteVector(box.Axes[0]) && IsFiniteVector(box.Axes[1]) && IsFiniteVector(box.Axes[2]);
        }

        bool IsFiniteCapsule(const Math::Capsule& capsule)
        {
            return IsFiniteVector(capsule.PointA) && IsFiniteVector(capsule.PointB)
                && std::isfinite(capsule.Radius) && capsule.Radius >= 0.0f;
        }
    } // namespace

    PhysicsModule::PhysicsModule()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId())
    {
    }

    Core::Identity PhysicsModule::GetModuleId() const
    {
        return Core::Identity(kPhysicsModuleName);
    }

    const char* PhysicsModule::GetName() const
    {
        return kPhysicsModuleName;
    }

    bool PhysicsModule::Install(Core::Engine::Engine& engine)
    {
        if (!IsOwnerThread())
        {
            return false;
        }

        if (m_bBound || m_Engine != nullptr)
        {
            return false;
        }

        if (engine.GetSceneQuery().BindPhysicsProvider(*this) != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return false;
        }

        m_Engine = &engine;
        m_bBound = true;
        return true;
    }

    bool PhysicsModule::Initialize()
    {
        if (!IsOwnerThread() || !m_bBound || m_Engine == nullptr)
        {
            return false;
        }

        ResetTransientState();
        m_bInitialized = true;
        return true;
    }

    void PhysicsModule::Tick(float /*deltaTime*/)
    {
    }

    void PhysicsModule::PreFixedTick(float fixedDeltaTime)
    {
        if (!IsOwnerThread() || !m_bInitialized || !std::isfinite(fixedDeltaTime) || fixedDeltaTime <= 0.0f)
        {
            return;
        }

        for (BodySlot& body : m_BodySlots)
        {
            body.bHadPreStepSnapshot = false;
            if (body.bOccupied && body.bActive && body.Owner != nullptr)
            {
                body.PreStepPosition = GetFreshWorldTransform(*body.Owner).position;
                body.bHadPreStepSnapshot = true;
            }
        }
    }

    void PhysicsModule::FixedTick(float fixedDeltaTime)
    {
        if (!IsOwnerThread() || !m_bInitialized || !std::isfinite(fixedDeltaTime) || fixedDeltaTime <= 0.0f)
        {
            return;
        }
        if (m_bFixedTickInProgress)
        {
            return;
        }

        m_bFixedTickInProgress = true;
        ReconcileActiveStates();
        IntegrateDynamics(fixedDeltaTime);
        BuildBroadphase(m_WorkingBroadphase);
        ResolveContacts(fixedDeltaTime);
        BuildEventQueue();
        PublishSnapshot();
        m_bHasPublishedSnapshot = true;
        DispatchEvents();
        m_bFixedTickInProgress = false;
    }

    void PhysicsModule::Shutdown()
    {
        if (!IsOwnerThread())
        {
            return;
        }

        ResetTransientState();
        m_bInitialized = false;
    }

    void PhysicsModule::Uninstall(Core::Engine::Engine& engine)
    {
        if (!IsOwnerThread())
        {
            return;
        }

        if (!m_bBound)
        {
            m_Engine = nullptr;
            ResetTransientState();
            m_bInitialized = false;
            return;
        }

        if (m_Engine != &engine)
        {
            return;
        }

        const Core::Scene::EPhysicsSceneQueryResult result =
            engine.GetSceneQuery().UnbindPhysicsProvider(*this);
        if (result != Core::Scene::EPhysicsSceneQueryResult::Success
            && result != Core::Scene::EPhysicsSceneQueryResult::Unavailable)
        {
            return;
        }

        // Unavailable は provider が既に存在しないため dangling binding はない。
        m_Engine = nullptr;
        m_bBound = false;
        ResetTransientState();
        m_bInitialized = false;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::Raycast(
        const Math::Ray& ray,
        float maxDistance,
        Core::Scene::PhysicsRaycastHit& outHit) const
    {
        outHit = Core::Scene::PhysicsRaycastHit{};
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        if (readiness != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return readiness;
        }
        Math::Vector3 normalizedDirection;
        if (!IsFiniteRay(ray) || !std::isfinite(maxDistance) || maxDistance < 0.0f
            || !NormalizeFiniteNonZeroDirection(ray.Direction, normalizedDirection))
        {
            return Core::Scene::EPhysicsSceneQueryResult::InvalidArgument;
        }

        const Math::Ray normalizedRay(ray.Origin, normalizedDirection);
        return m_PublishedBroadphase.Raycast(normalizedRay, maxDistance, outHit)
            ? Core::Scene::EPhysicsSceneQueryResult::Success
            : Core::Scene::EPhysicsSceneQueryResult::NoHit;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapSphere(
        const Math::Sphere& sphere,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        if (readiness != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return readiness;
        }
        if (!IsFiniteSphere(sphere))
        {
            return Core::Scene::EPhysicsSceneQueryResult::InvalidArgument;
        }
        m_PublishedBroadphase.OverlapSphere(sphere, outHits);
        return outHits.empty() ? Core::Scene::EPhysicsSceneQueryResult::NoHit : Core::Scene::EPhysicsSceneQueryResult::Success;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapBox(
        const Math::OBB& box,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        if (readiness != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return readiness;
        }
        if (!IsFiniteBox(box))
        {
            return Core::Scene::EPhysicsSceneQueryResult::InvalidArgument;
        }
        m_PublishedBroadphase.OverlapBox(box, outHits);
        return outHits.empty() ? Core::Scene::EPhysicsSceneQueryResult::NoHit : Core::Scene::EPhysicsSceneQueryResult::Success;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapCapsule(
        const Math::Capsule& capsule,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        if (readiness != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return readiness;
        }
        if (!IsFiniteCapsule(capsule))
        {
            return Core::Scene::EPhysicsSceneQueryResult::InvalidArgument;
        }
        m_PublishedBroadphase.OverlapCapsule(capsule, outHits);
        return outHits.empty() ? Core::Scene::EPhysicsSceneQueryResult::NoHit : Core::Scene::EPhysicsSceneQueryResult::Success;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::IsAlive(
        Core::Scene::ColliderHandle collider,
        bool& outAlive) const
    {
        outAlive = false;
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        if (readiness != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return readiness;
        }
        outAlive = collider.IsValid() && collider.Index < m_ColliderSlots.size()
            && m_ColliderSlots[collider.Index].bOccupied
            && m_ColliderSlots[collider.Index].Generation == collider.Generation;
        return Core::Scene::EPhysicsSceneQueryResult::Success;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::IsAlive(
        Core::Scene::BodyHandle body,
        bool& outAlive) const
    {
        outAlive = false;
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        if (readiness != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return readiness;
        }
        outAlive = body.IsValid() && body.Index < m_BodySlots.size()
            && m_BodySlots[body.Index].bOccupied
            && m_BodySlots[body.Index].Generation == body.Generation;
        return Core::Scene::EPhysicsSceneQueryResult::Success;
    }

    EPhysicsResult PhysicsModule::RegisterCollider(ColliderComponent& component)
    {
        if (!IsOwnerThread())
        {
            return EPhysicsResult::WrongThread;
        }
        if (component.m_ColliderHandle.IsValid())
        {
            return ValidateCollider(component);
        }

        Core::Entity* owner = component.GetOwner();
        if (!owner)
        {
            return EPhysicsResult::InvalidState;
        }
        for (const ColliderSlot& slot : m_ColliderSlots)
        {
            if (slot.bOccupied && slot.Owner == owner)
            {
                m_LastDiagnostic = EPhysicsDiagnostic::Duplicate;
                ++m_DuplicateDiagnosticCount;
                return EPhysicsResult::Duplicate;
            }
        }

        uint32_t index = 0;
        if (!m_FreeColliderSlotIndices.empty())
        {
            index = m_FreeColliderSlotIndices.back();
            m_FreeColliderSlotIndices.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_ColliderSlots.size());
            m_ColliderSlots.emplace_back();
        }

        ColliderSlot& slot = m_ColliderSlots[index];
        slot.Component = &component;
        slot.Owner = owner;
        slot.bOccupied = true;
        slot.bActive = false;
        component.m_ColliderHandle = Core::Scene::ColliderHandle{index, slot.Generation};
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::RegisterRigidBody(RigidBodyComponent& component)
    {
        if (!IsOwnerThread())
        {
            return EPhysicsResult::WrongThread;
        }
        if (component.m_BodyHandle.IsValid())
        {
            return ValidateRigidBody(component);
        }

        Core::Entity* owner = component.GetOwner();
        if (!owner)
        {
            return EPhysicsResult::InvalidState;
        }
        for (const BodySlot& slot : m_BodySlots)
        {
            if (slot.bOccupied && slot.Owner == owner)
            {
                m_LastDiagnostic = EPhysicsDiagnostic::Duplicate;
                ++m_DuplicateDiagnosticCount;
                return EPhysicsResult::Duplicate;
            }
        }

        uint32_t index = 0;
        if (!m_FreeBodySlotIndices.empty())
        {
            index = m_FreeBodySlotIndices.back();
            m_FreeBodySlotIndices.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_BodySlots.size());
            m_BodySlots.emplace_back();
        }

        BodySlot& slot = m_BodySlots[index];
        slot.Component = &component;
        slot.Owner = owner;
        slot.bOccupied = true;
        slot.bActive = false;
        component.m_BodyHandle = Core::Scene::BodyHandle{index, slot.Generation};
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::UnregisterCollider(ColliderComponent& component)
    {
        const EPhysicsResult result = ValidateCollider(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }

        ReleaseColliderSlot(component.m_ColliderHandle.Index);
        component.m_ColliderHandle = Core::Scene::ColliderHandle{};
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::UnregisterRigidBody(RigidBodyComponent& component)
    {
        const EPhysicsResult result = ValidateRigidBody(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }

        ReleaseBodySlot(component.m_BodyHandle.Index);
        component.m_BodyHandle = Core::Scene::BodyHandle{};
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::SetColliderSphere(ColliderComponent& component, float radius)
    {
        const EPhysicsResult result = ValidateCollider(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }
        if (!std::isfinite(radius) || radius <= 0.0f)
        {
            return EPhysicsResult::InvalidArgument;
        }

        component.m_Radius = radius;
        component.m_Shape = ColliderComponent::EColliderShape::Sphere;
        component.m_bHasShape = true;
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::SetColliderBox(ColliderComponent& component, const Math::Vector3& halfExtents)
    {
        const EPhysicsResult result = ValidateCollider(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }
        if (!std::isfinite(halfExtents.x) || !std::isfinite(halfExtents.y) || !std::isfinite(halfExtents.z)
            || halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
        {
            return EPhysicsResult::InvalidArgument;
        }

        component.m_HalfExtents = halfExtents;
        component.m_Shape = ColliderComponent::EColliderShape::Box;
        component.m_bHasShape = true;
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::SetColliderCapsule(ColliderComponent& component, float radius, float halfHeight)
    {
        const EPhysicsResult result = ValidateCollider(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }
        if (!std::isfinite(radius) || !std::isfinite(halfHeight) || radius <= 0.0f || halfHeight < 0.0f)
        {
            return EPhysicsResult::InvalidArgument;
        }

        component.m_Radius = radius;
        component.m_CapsuleHalfHeight = halfHeight;
        component.m_Shape = ColliderComponent::EColliderShape::Capsule;
        component.m_bHasShape = true;
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::SetColliderTrigger(ColliderComponent& component, bool bTrigger)
    {
        const EPhysicsResult result = ValidateCollider(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }

        component.m_bTrigger = bTrigger;
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::SetBodyType(RigidBodyComponent& component, EPhysicsBodyType bodyType)
    {
        const EPhysicsResult result = ValidateRigidBody(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }
        if (bodyType != EPhysicsBodyType::Static && bodyType != EPhysicsBodyType::Dynamic
            && bodyType != EPhysicsBodyType::Kinematic)
        {
            return EPhysicsResult::InvalidArgument;
        }
        if (bodyType == EPhysicsBodyType::Dynamic && component.GetOwner()->GetParentEntity() != nullptr)
        {
            return EPhysicsResult::InvalidState;
        }

        component.m_BodyType = bodyType;
        if (bodyType != EPhysicsBodyType::Dynamic)
        {
            if (BodySlot* body = FindBodySlot(component.m_BodyHandle))
            {
                body->PendingImpulse = Math::Vector3();
            }
        }
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::SetBodyMass(RigidBodyComponent& component, float mass)
    {
        const EPhysicsResult result = ValidateRigidBody(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }
        if (!std::isfinite(mass) || mass <= 0.0f)
        {
            return EPhysicsResult::InvalidArgument;
        }

        component.m_Mass = mass;
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::SetBodyGravityScale(RigidBodyComponent& component, float gravityScale)
    {
        const EPhysicsResult result = ValidateRigidBody(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }
        if (!std::isfinite(gravityScale))
        {
            return EPhysicsResult::InvalidArgument;
        }

        component.m_GravityScale = gravityScale;
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::SetBodyLinearVelocity(
        RigidBodyComponent& component,
        const Math::Vector3& velocity)
    {
        const EPhysicsResult result = ValidateRigidBody(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }
        if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
        {
            return EPhysicsResult::InvalidArgument;
        }

        component.m_LinearVelocity = velocity;
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::AddBodyImpulse(RigidBodyComponent& component, const Math::Vector3& impulse)
    {
        const EPhysicsResult result = ValidateRigidBody(component);
        if (result != EPhysicsResult::Success)
        {
            return result;
        }
        if (!std::isfinite(impulse.x) || !std::isfinite(impulse.y) || !std::isfinite(impulse.z))
        {
            return EPhysicsResult::InvalidArgument;
        }

        BodySlot* body = FindBodySlot(component.m_BodyHandle);
        if (!body)
        {
            return EPhysicsResult::NotRegistered;
        }

        if (component.m_BodyType != EPhysicsBodyType::Dynamic)
        {
            return EPhysicsResult::InvalidState;
        }

        body->PendingImpulse += impulse;
        return EPhysicsResult::Success;
    }

    EPhysicsResult PhysicsModule::ValidateCollider(const ColliderComponent& component) const
    {
        if (!IsOwnerThread())
        {
            return EPhysicsResult::WrongThread;
        }
        const Core::Scene::ColliderHandle handle = component.m_ColliderHandle;
        if (!handle.IsValid() || handle.Index >= m_ColliderSlots.size())
        {
            return EPhysicsResult::NotRegistered;
        }

        const ColliderSlot& slot = m_ColliderSlots[handle.Index];
        return slot.bOccupied && slot.Generation == handle.Generation && slot.Component == &component
            ? EPhysicsResult::Success
            : EPhysicsResult::NotRegistered;
    }

    EPhysicsResult PhysicsModule::ValidateRigidBody(const RigidBodyComponent& component) const
    {
        if (!IsOwnerThread())
        {
            return EPhysicsResult::WrongThread;
        }
        const Core::Scene::BodyHandle handle = component.m_BodyHandle;
        if (!handle.IsValid() || handle.Index >= m_BodySlots.size())
        {
            return EPhysicsResult::NotRegistered;
        }

        const BodySlot& slot = m_BodySlots[handle.Index];
        return slot.bOccupied && slot.Generation == handle.Generation && slot.Component == &component
            ? EPhysicsResult::Success
            : EPhysicsResult::NotRegistered;
    }

    uint32_t PhysicsModule::GetCallbackCount(const ColliderComponent& component) const
    {
        uint32_t count = 0;
        for (const ColliderComponent::CallbackSlot& slot : component.m_OverlapBeginCallbacks)
        {
            count += slot.bOccupied ? 1u : 0u;
        }
        for (const ColliderComponent::CallbackSlot& slot : component.m_OverlapEndCallbacks)
        {
            count += slot.bOccupied ? 1u : 0u;
        }
        for (const ColliderComponent::CallbackSlot& slot : component.m_HitCallbacks)
        {
            count += slot.bOccupied ? 1u : 0u;
        }
        return count;
    }

    EPhysicsResult PhysicsModule::GetColliderRegistrationResult(const ColliderComponent& component) const
    {
        return component.m_LastRegistrationResult;
    }

    float PhysicsModule::GetColliderRadius(const ColliderComponent& component) const
    {
        return component.m_Radius;
    }

    void PhysicsModule::PrepareColliderGenerationWrap(ColliderComponent& component)
    {
        ColliderSlot& slot = m_ColliderSlots[component.m_ColliderHandle.Index];
        slot.Generation = UINT32_MAX;
        component.m_ColliderHandle.Generation = UINT32_MAX;
    }

    void PhysicsModule::PrepareBodyGenerationWrap(RigidBodyComponent& component)
    {
        BodySlot& slot = m_BodySlots[component.m_BodyHandle.Index];
        slot.Generation = UINT32_MAX;
        component.m_BodyHandle.Generation = UINT32_MAX;
    }

    void PhysicsModule::PrepareOverlapBeginGenerationWrap(
        ColliderComponent& component,
        PhysicsCallbackHandle& handle)
    {
        ColliderComponent::CallbackSlot& slot = component.m_OverlapBeginCallbacks[handle.Index];
        slot.Generation = UINT32_MAX;
        handle.Generation = UINT32_MAX;
    }

    void PhysicsModule::ReconcileActiveStates()
    {
        for (ColliderSlot& collider : m_ColliderSlots)
        {
            if (collider.bOccupied)
            {
                collider.bActive = collider.Component->m_bHasShape
                    && collider.Component->IsActive()
                    && collider.Owner->IsActive()
                    && !collider.Component->HasFlag(Core::OF_PendingDestroy)
                    && !collider.Owner->IsPendingDestroy()
                    && IsFiniteTransform(GetFreshWorldTransform(*collider.Owner));
            }
        }

        for (BodySlot& body : m_BodySlots)
        {
            if (!body.bOccupied)
            {
                continue;
            }

            body.bActive = false;
            if (body.Component->m_BodyType == EPhysicsBodyType::Dynamic
                && body.Owner->GetParentEntity() != nullptr)
            {
                continue;
            }
            for (const ColliderSlot& collider : m_ColliderSlots)
            {
                if (collider.bOccupied && collider.Owner == body.Owner && collider.bActive)
                {
                    body.bActive = true;
                    break;
                }
            }
        }
    }

    void PhysicsModule::IntegrateDynamics(float fixedDeltaTime)
    {
        const Math::Vector3 gravity(0.0f, -9.81f, 0.0f);
        for (BodySlot& body : m_BodySlots)
        {
            if (!body.bOccupied)
            {
                continue;
            }

            if (!body.bActive)
            {
                body.PendingImpulse = Math::Vector3();
                body.bHadPreStepSnapshot = false;
                continue;
            }

            if (body.Component->m_BodyType == EPhysicsBodyType::Kinematic)
            {
                if (body.bHadPreStepSnapshot)
                {
                    body.Component->m_LinearVelocity =
                        (GetFreshWorldTransform(*body.Owner).position - body.PreStepPosition) / fixedDeltaTime;
                }
                else
                {
                    body.Component->m_LinearVelocity = Math::Vector3();
                }
                body.PendingImpulse = Math::Vector3();
                body.bHadPreStepSnapshot = false;
                continue;
            }

            if (body.Component->m_BodyType != EPhysicsBodyType::Dynamic)
            {
                body.PendingImpulse = Math::Vector3();
                body.bHadPreStepSnapshot = false;
                continue;
            }

            body.Component->m_LinearVelocity += body.PendingImpulse / body.Component->m_Mass;
            body.PendingImpulse = Math::Vector3();
            body.bHadPreStepSnapshot = false;
            body.Component->m_LinearVelocity += gravity * (body.Component->m_GravityScale * fixedDeltaTime);
            MoveDynamicBody(body, body.Component->m_LinearVelocity * fixedDeltaTime);
        }
    }

    void PhysicsModule::ResolveContacts(float /*fixedDeltaTime*/)
    {
        m_CurrentTriggerPairs.clear();
        const Core::Container::VariableArray<PhysicsShapeProxy>& proxies = m_WorkingBroadphase.GetProxies();
        for (const PhysicsCandidatePair& candidate : m_WorkingBroadphase.GetCandidatePairs())
        {
            const PhysicsShapeProxy* firstProxy = nullptr;
            const PhysicsShapeProxy* secondProxy = nullptr;
            for (const PhysicsShapeProxy& proxy : proxies)
            {
                if (proxy.Collider == candidate.First)
                {
                    firstProxy = &proxy;
                }
                else if (proxy.Collider == candidate.Second)
                {
                    secondProxy = &proxy;
                }
            }
            if (!firstProxy || !secondProxy)
            {
                continue;
            }

            Math::GeometryContact contact;
            if (!PhysicsBroadphase::ComputeContact(*firstProxy, *secondProxy, contact))
            {
                continue;
            }

            ColliderSlot* firstCollider = FindColliderSlot(candidate.First);
            ColliderSlot* secondCollider = FindColliderSlot(candidate.Second);
            if (!firstCollider || !secondCollider)
            {
                continue;
            }
            if (firstCollider->Component->m_bTrigger || secondCollider->Component->m_bTrigger)
            {
                m_CurrentTriggerPairs.push_back(TriggerPairState{candidate.First, candidate.Second, contact});
                continue;
            }

            BodySlot* firstBody = FindBodySlot(firstProxy->Body);
            BodySlot* secondBody = FindBodySlot(secondProxy->Body);
            const bool bFirstDynamic = firstBody && firstBody->bActive
                && firstBody->Component->m_BodyType == EPhysicsBodyType::Dynamic;
            const bool bSecondDynamic = secondBody && secondBody->bActive
                && secondBody->Component->m_BodyType == EPhysicsBodyType::Dynamic;
            const bool bFirstKinematic = firstBody && firstBody->bActive
                && firstBody->Component->m_BodyType == EPhysicsBodyType::Kinematic;
            const bool bSecondKinematic = secondBody && secondBody->bActive
                && secondBody->Component->m_BodyType == EPhysicsBodyType::Kinematic;
            const float firstInverseMass = bFirstDynamic ? 1.0f / firstBody->Component->m_Mass : 0.0f;
            const float secondInverseMass = bSecondDynamic ? 1.0f / secondBody->Component->m_Mass : 0.0f;
            const float inverseMassSum = firstInverseMass + secondInverseMass;
            if (inverseMassSum <= 0.0f)
            {
                continue;
            }

            const Math::Vector3 firstVelocity = (bFirstDynamic || bFirstKinematic)
                ? firstBody->Component->m_LinearVelocity
                : Math::Vector3();
            const Math::Vector3 secondVelocity = (bSecondDynamic || bSecondKinematic)
                ? secondBody->Component->m_LinearVelocity
                : Math::Vector3();
            const float normalVelocity = Math::VectorUtils::Dot(secondVelocity - firstVelocity, contact.Normal);
            float normalImpulse = 0.0f;
            if (normalVelocity < 0.0f)
            {
                normalImpulse = -normalVelocity / inverseMassSum;
                if (bFirstDynamic)
                {
                    firstBody->Component->m_LinearVelocity -= contact.Normal * (normalImpulse * firstInverseMass);
                }
                if (bSecondDynamic)
                {
                    secondBody->Component->m_LinearVelocity += contact.Normal * (normalImpulse * secondInverseMass);
                }
            }

            if (contact.Depth > 0.0f)
            {
                const Math::Vector3 correction = contact.Normal * (contact.Depth / inverseMassSum);
                if (bFirstDynamic)
                {
                    MoveDynamicBody(*firstBody, correction * -firstInverseMass);
                }
                if (bSecondDynamic)
                {
                    MoveDynamicBody(*secondBody, correction * secondInverseMass);
                }
            }
            if (normalImpulse > 0.0f)
            {
                m_PendingEvents.push_back(PendingPhysicsEvent{
                    EPhysicsEventType::Hit,
                    candidate.First,
                    candidate.Second,
                    contact,
                    normalImpulse});
            }
        }
    }

    void PhysicsModule::BuildEventQueue()
    {
        for (const TriggerPairState& current : m_CurrentTriggerPairs)
        {
            bool bWasPresent = false;
            for (const TriggerPairState& previous : m_PreviousTriggerPairs)
            {
                if (previous.First == current.First && previous.Second == current.Second)
                {
                    bWasPresent = true;
                    break;
                }
            }
            if (!bWasPresent)
            {
                m_PendingEvents.push_back(PendingPhysicsEvent{
                    EPhysicsEventType::OverlapBegin,
                    current.First,
                    current.Second,
                    current.Contact,
                    0.0f});
            }
        }
        for (const TriggerPairState& previous : m_PreviousTriggerPairs)
        {
            bool bIsPresent = false;
            for (const TriggerPairState& current : m_CurrentTriggerPairs)
            {
                if (current.First == previous.First && current.Second == previous.Second)
                {
                    bIsPresent = true;
                    break;
                }
            }
            if (!bIsPresent)
            {
                Math::GeometryContact contact = previous.Contact;
                contact.Depth = 0.0f;
                m_PendingEvents.push_back(PendingPhysicsEvent{
                    EPhysicsEventType::OverlapEnd,
                    previous.First,
                    previous.Second,
                    contact,
                    0.0f});
            }
        }
        m_PreviousTriggerPairs = m_CurrentTriggerPairs;
        m_PendingEventCount = static_cast<uint32_t>(m_PendingEvents.size());
    }

    void PhysicsModule::DispatchEvents()
    {
        struct DispatchReceiver
        {
            Core::Scene::ColliderHandle SelfHandle;
            Core::Scene::ColliderHandle OtherHandle;
            PhysicsContactEvent Event;
            Core::Container::VariableArray<Core::Delegate<void, const PhysicsContactEvent&>> Callbacks;
        };

        for (const PendingPhysicsEvent& pending : m_PendingEvents)
        {
            Core::Container::VariableArray<DispatchReceiver> receivers;
            receivers.reserve(2);
            for (uint32_t receiverIndex = 0; receiverIndex < 2; ++receiverIndex)
            {
                const Core::Scene::ColliderHandle selfHandle = receiverIndex == 0 ? pending.First : pending.Second;
                const Core::Scene::ColliderHandle otherHandle = receiverIndex == 0 ? pending.Second : pending.First;
                ColliderSlot* self = FindColliderSlot(selfHandle);
                ColliderSlot* other = FindColliderSlot(otherHandle);
                if (!self || !self->bActive || self->Owner->IsPendingDestroy())
                {
                    continue;
                }
                if (pending.Type != EPhysicsEventType::OverlapEnd
                    && (!other || !other->bActive || other->Owner->IsPendingDestroy()))
                {
                    continue;
                }

                DispatchReceiver receiver;
                receiver.SelfHandle = selfHandle;
                receiver.OtherHandle = otherHandle;
                receiver.Event.Self = selfHandle;
                receiver.Event.Other = otherHandle;
                receiver.Event.Contact = pending.Contact;
                receiver.Event.NormalImpulse = pending.NormalImpulse;
                if (receiverIndex != 0)
                {
                    receiver.Event.Contact.Normal *= -1.0f;
                }

                const Core::Container::VariableArray<ColliderComponent::CallbackSlot>* callbacks = nullptr;
                if (pending.Type == EPhysicsEventType::OverlapBegin)
                {
                    callbacks = &self->Component->m_OverlapBeginCallbacks;
                }
                else if (pending.Type == EPhysicsEventType::OverlapEnd)
                {
                    callbacks = &self->Component->m_OverlapEndCallbacks;
                }
                else
                {
                    callbacks = &self->Component->m_HitCallbacks;
                }

                for (const ColliderComponent::CallbackSlot& callback : *callbacks)
                {
                    if (callback.bOccupied && callback.Callback.IsBound())
                    {
                        receiver.Callbacks.push_back(callback.Callback);
                    }
                }
                receivers.push_back(std::move(receiver));
            }

            for (const DispatchReceiver& receiver : receivers)
            {
                for (const Core::Delegate<void, const PhysicsContactEvent>& callback : receiver.Callbacks)
                {
                    ColliderSlot* self = FindColliderSlot(receiver.SelfHandle);
                    ColliderSlot* other = FindColliderSlot(receiver.OtherHandle);
                    if (!self || !self->bActive || self->Owner->IsPendingDestroy())
                    {
                        break;
                    }
                    if (pending.Type != EPhysicsEventType::OverlapEnd
                        && (!other || !other->bActive || other->Owner->IsPendingDestroy()))
                    {
                        break;
                    }
                    callback(receiver.Event);
                    ++m_DispatchedEventCount;
                }
            }
        }
        m_PendingEvents.clear();
        m_PendingEventCount = 0;
    }

    void PhysicsModule::PublishSnapshot()
    {
        BuildBroadphase(m_PublishedBroadphase);
    }

    void PhysicsModule::BuildBroadphase(PhysicsBroadphase& outBroadphase) const
    {
        Core::Container::VariableArray<PhysicsShapeProxy> proxies;
        for (const ColliderSlot& slot : m_ColliderSlots)
        {
            if (!slot.bOccupied || !slot.bActive)
            {
                continue;
            }

            const Math::Transform transform = GetFreshWorldTransform(*slot.Owner);
            PhysicsShapeProxy proxy;
            proxy.Collider = slot.Component->m_ColliderHandle;
            proxy.Body = FindBodyHandle(*slot.Owner);
            proxy.Entity = slot.Owner->GetEntityHandle();
            proxy.bHasEntity = proxy.Entity.IsValid();
            if (slot.Component->m_Shape == ColliderComponent::EColliderShape::Sphere)
            {
                proxy.Shape = EPhysicsProxyShape::Sphere;
                proxy.Sphere = Math::Sphere(
                    transform.TransformPoint(Math::Vector3()),
                    slot.Component->m_Radius * std::fmaxf(
                        std::fabs(transform.scale.x),
                        std::fmaxf(std::fabs(transform.scale.y), std::fabs(transform.scale.z))));
            }
            else if (slot.Component->m_Shape == ColliderComponent::EColliderShape::Box)
            {
                proxy.Shape = EPhysicsProxyShape::Box;
                proxy.Box = Math::OBB(
                    transform.TransformPoint(Math::Vector3()),
                    Math::Vector3(
                        slot.Component->m_HalfExtents.x * std::fabs(transform.scale.x),
                        slot.Component->m_HalfExtents.y * std::fabs(transform.scale.y),
                        slot.Component->m_HalfExtents.z * std::fabs(transform.scale.z)),
                    Math::VectorUtils::Normalize(transform.rotation * Math::Vector3::UnitX),
                    Math::VectorUtils::Normalize(transform.rotation * Math::Vector3::UnitY),
                    Math::VectorUtils::Normalize(transform.rotation * Math::Vector3::UnitZ));
            }
            else if (slot.Component->m_Shape == ColliderComponent::EColliderShape::Capsule)
            {
                proxy.Shape = EPhysicsProxyShape::Capsule;
                proxy.Capsule = Math::Capsule(
                    transform.TransformPoint(Math::Vector3(0.0f, -slot.Component->m_CapsuleHalfHeight, 0.0f)),
                    transform.TransformPoint(Math::Vector3(0.0f, slot.Component->m_CapsuleHalfHeight, 0.0f)),
                    slot.Component->m_Radius * std::fmaxf(
                        std::fabs(transform.scale.x),
                        std::fabs(transform.scale.z)));
            }
            else
            {
                continue;
            }
            proxies.push_back(proxy);
        }
        outBroadphase.SetProxies(std::move(proxies));
    }

    void PhysicsModule::ResetTransientState()
    {
        for (BodySlot& body : m_BodySlots)
        {
            body.PendingImpulse = Math::Vector3();
            body.PreStepPosition = Math::Vector3();
            body.bHadPreStepSnapshot = false;
        }

        m_PreviousTriggerPairs.clear();
        m_CurrentTriggerPairs.clear();
        m_PendingEvents.clear();
        m_WorkingBroadphase.SetProxies(Core::Container::VariableArray<PhysicsShapeProxy>());
        m_PublishedBroadphase.SetProxies(Core::Container::VariableArray<PhysicsShapeProxy>());
        m_DuplicateDiagnosticCount = 0;
        m_DispatchedEventCount = 0;
        m_PendingEventCount = 0;
        m_LastDiagnostic = EPhysicsDiagnostic::None;
        m_bHasPublishedSnapshot = false;
    }

    Math::Transform PhysicsModule::GetFreshWorldTransform(const Core::Entity& entity) const
    {
        const Math::Transform localTransform = entity.GetLocalTransform();
        const Core::Entity* parent = entity.GetParentEntity();
        if (parent == nullptr)
        {
            return localTransform;
        }
        return GetFreshWorldTransform(*parent) * localTransform;
    }

    Core::Scene::BodyHandle PhysicsModule::FindBodyHandle(const Core::Entity& owner) const
    {
        for (const BodySlot& slot : m_BodySlots)
        {
            if (slot.bOccupied && slot.bActive && slot.Owner == &owner)
            {
                return slot.Component->m_BodyHandle;
            }
        }
        return Core::Scene::BodyHandle{};
    }

    PhysicsModule::ColliderSlot* PhysicsModule::FindColliderSlot(Core::Scene::ColliderHandle handle)
    {
        if (!handle.IsValid() || handle.Index >= m_ColliderSlots.size())
        {
            return nullptr;
        }
        ColliderSlot& slot = m_ColliderSlots[handle.Index];
        return slot.bOccupied && slot.Generation == handle.Generation ? &slot : nullptr;
    }

    const PhysicsModule::ColliderSlot* PhysicsModule::FindColliderSlot(Core::Scene::ColliderHandle handle) const
    {
        if (!handle.IsValid() || handle.Index >= m_ColliderSlots.size())
        {
            return nullptr;
        }
        const ColliderSlot& slot = m_ColliderSlots[handle.Index];
        return slot.bOccupied && slot.Generation == handle.Generation ? &slot : nullptr;
    }

    PhysicsModule::BodySlot* PhysicsModule::FindBodySlot(Core::Scene::BodyHandle handle)
    {
        if (!handle.IsValid() || handle.Index >= m_BodySlots.size())
        {
            return nullptr;
        }
        BodySlot& slot = m_BodySlots[handle.Index];
        return slot.bOccupied && slot.Generation == handle.Generation ? &slot : nullptr;
    }

    void PhysicsModule::MoveDynamicBody(BodySlot& body, const Math::Vector3& displacement)
    {
        if (!body.bActive || body.Component->m_BodyType != EPhysicsBodyType::Dynamic
            || body.Owner->GetParentEntity() != nullptr)
        {
            return;
        }
        body.Owner->SetLocalPosition(body.Owner->GetLocalTransform().position + displacement);
    }

    void PhysicsModule::ReleaseColliderSlot(uint32_t index)
    {
        ColliderSlot& slot = m_ColliderSlots[index];
        slot.Component = nullptr;
        slot.Owner = nullptr;
        slot.bOccupied = false;
        slot.bActive = false;
        ++slot.Generation;
        if (slot.Generation == 0)
        {
            slot.Generation = 1;
        }
        m_FreeColliderSlotIndices.push_back(index);
    }

    void PhysicsModule::ReleaseBodySlot(uint32_t index)
    {
        BodySlot& slot = m_BodySlots[index];
        slot.Component = nullptr;
        slot.Owner = nullptr;
        slot.bOccupied = false;
        slot.bActive = false;
        slot.PendingImpulse = Math::Vector3();
        slot.PreStepPosition = Math::Vector3();
        slot.bHadPreStepSnapshot = false;
        ++slot.Generation;
        if (slot.Generation == 0)
        {
            slot.Generation = 1;
        }
        m_FreeBodySlotIndices.push_back(index);
    }

    bool PhysicsModule::IsOwnerThread() const
    {
        return m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::GetReadinessResult() const
    {
        if (!IsOwnerThread())
        {
            return Core::Scene::EPhysicsSceneQueryResult::WrongThread;
        }

        if (!m_bInitialized || !m_bHasPublishedSnapshot)
        {
            return Core::Scene::EPhysicsSceneQueryResult::NotReady;
        }

        return Core::Scene::EPhysicsSceneQueryResult::Success;
    }

    IPhysicsModule* RegisterPhysicsModule(Core::Module::ModuleRegistry& registry)
    {
        if (IPhysicsModule* existing = FindPhysicsModule(registry))
        {
            return existing;
        }

        return dynamic_cast<IPhysicsModule*>(
            registry.Register(Core::Container::MakeUnique<PhysicsModule>()));
    }

    IPhysicsModule* FindPhysicsModule(Core::Module::ModuleRegistry& registry)
    {
        return dynamic_cast<IPhysicsModule*>(registry.FindModule(Core::Identity(kPhysicsModuleName)));
    }
} // namespace NorvesLib::Modules::Physics
