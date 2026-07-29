#include "Physics/PhysicsModule.h"

#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "CoreTypes.h"
#include "Engine/Engine.h"

#include <cmath>

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        constexpr const char* kPhysicsModuleName = "NorvesPhysicsModule";
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

        m_bInitialized = true;
        m_bHasPublishedSnapshot = false;
        return true;
    }

    void PhysicsModule::Tick(float /*deltaTime*/)
    {
    }

    void PhysicsModule::PreFixedTick(float /*fixedDeltaTime*/)
    {
    }

    void PhysicsModule::FixedTick(float /*fixedDeltaTime*/)
    {
        if (!IsOwnerThread() || !m_bInitialized)
        {
            return;
        }

        ReconcileActiveStates();
        m_bHasPublishedSnapshot = true;
    }

    void PhysicsModule::Shutdown()
    {
        if (!IsOwnerThread())
        {
            return;
        }

        m_bInitialized = false;
        m_bHasPublishedSnapshot = false;
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
            m_bInitialized = false;
            m_bHasPublishedSnapshot = false;
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
        m_bInitialized = false;
        m_bHasPublishedSnapshot = false;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::Raycast(
        const Math::Ray& /*ray*/,
        float /*maxDistance*/,
        Core::Scene::PhysicsRaycastHit& outHit) const
    {
        outHit = Core::Scene::PhysicsRaycastHit{};
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        return readiness == Core::Scene::EPhysicsSceneQueryResult::Success
            ? Core::Scene::EPhysicsSceneQueryResult::NoHit
            : readiness;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapSphere(
        const Math::Sphere& /*sphere*/,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        return readiness == Core::Scene::EPhysicsSceneQueryResult::Success
            ? Core::Scene::EPhysicsSceneQueryResult::NoHit
            : readiness;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapBox(
        const Math::OBB& /*box*/,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        return readiness == Core::Scene::EPhysicsSceneQueryResult::Success
            ? Core::Scene::EPhysicsSceneQueryResult::NoHit
            : readiness;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapCapsule(
        const Math::Capsule& /*capsule*/,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        return readiness == Core::Scene::EPhysicsSceneQueryResult::Success
            ? Core::Scene::EPhysicsSceneQueryResult::NoHit
            : readiness;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::IsAlive(
        Core::Scene::ColliderHandle /*collider*/,
        bool& outAlive) const
    {
        outAlive = false;
        return GetReadinessResult();
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::IsAlive(
        Core::Scene::BodyHandle /*body*/,
        bool& outAlive) const
    {
        outAlive = false;
        return GetReadinessResult();
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

        component.m_LinearVelocity += impulse;
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
                collider.bActive = collider.Component->m_bHasShape;
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
