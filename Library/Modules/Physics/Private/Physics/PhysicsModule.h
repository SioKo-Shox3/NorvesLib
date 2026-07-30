#pragma once

#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsBroadphase.h"
#include "Physics/PhysicsTypes.h"
#include "Container/Containers.h"
#include "Scene/SceneQuery.h"
#include "Thread/Thread.h"

namespace NorvesLib::Modules::Physics
{
    class ColliderComponent;
    class RigidBodyComponent;
    class PhysicsModuleTestAccess;

    enum class EPhysicsDiagnostic : uint8_t
    {
        None,
        Duplicate,
    };

    class PhysicsModule final : public IPhysicsModule, private Core::Scene::IPhysicsSceneQueryProvider
    {
    public:
        PhysicsModule();
        ~PhysicsModule() override = default;

        Core::Identity GetModuleId() const override;
        const char* GetName() const override;

        bool Install(Core::Engine::Engine& engine) override;
        bool Initialize() override;
        void Tick(float deltaTime) override;
        void PreFixedTick(float fixedDeltaTime) override;
        void FixedTick(float fixedDeltaTime) override;
        void Shutdown() override;
        void Uninstall(Core::Engine::Engine& engine) override;

    private:
        struct ColliderSlot
        {
            ColliderComponent* Component = nullptr;
            Core::Entity* Owner = nullptr;
            uint32_t Generation = 1;
            bool bOccupied = false;
            bool bActive = false;
        };

        struct BodySlot
        {
            RigidBodyComponent* Component = nullptr;
            Core::Entity* Owner = nullptr;
            uint32_t Generation = 1;
            bool bOccupied = false;
            bool bActive = false;
            Math::Vector3 PendingImpulse;
            Math::Vector3 PreStepPosition;
            bool bHadPreStepSnapshot = false;
        };

        struct TriggerPairState
        {
            Core::Scene::ColliderHandle First;
            Core::Scene::ColliderHandle Second;
            Math::GeometryContact Contact;
        };

        enum class EPhysicsEventType : uint8_t
        {
            OverlapBegin,
            OverlapEnd,
            Hit,
        };

        struct PendingPhysicsEvent
        {
            EPhysicsEventType Type = EPhysicsEventType::OverlapBegin;
            Core::Scene::ColliderHandle First;
            Core::Scene::ColliderHandle Second;
            Math::GeometryContact Contact;
            float NormalImpulse = 0.0f;
        };

        friend class ColliderComponent;
        friend class RigidBodyComponent;
        friend class PhysicsModuleTestAccess;

        EPhysicsResult RegisterCollider(ColliderComponent& component);
        EPhysicsResult RegisterRigidBody(RigidBodyComponent& component);
        EPhysicsResult UnregisterCollider(ColliderComponent& component);
        EPhysicsResult UnregisterRigidBody(RigidBodyComponent& component);
        EPhysicsResult SetColliderSphere(ColliderComponent& component, float radius);
        EPhysicsResult SetColliderBox(ColliderComponent& component, const Math::Vector3& halfExtents);
        EPhysicsResult SetColliderCapsule(ColliderComponent& component, float radius, float halfHeight);
        EPhysicsResult SetColliderTrigger(ColliderComponent& component, bool bTrigger);
        EPhysicsResult SetBodyType(RigidBodyComponent& component, EPhysicsBodyType bodyType);
        EPhysicsResult SetBodyMass(RigidBodyComponent& component, float mass);
        EPhysicsResult SetBodyGravityScale(RigidBodyComponent& component, float gravityScale);
        EPhysicsResult SetBodyLinearVelocity(RigidBodyComponent& component, const Math::Vector3& velocity);
        EPhysicsResult AddBodyImpulse(RigidBodyComponent& component, const Math::Vector3& impulse);
        EPhysicsResult ValidateCollider(const ColliderComponent& component) const;
        EPhysicsResult ValidateRigidBody(const RigidBodyComponent& component) const;
        uint32_t GetCallbackCount(const ColliderComponent& component) const;
        EPhysicsResult GetColliderRegistrationResult(const ColliderComponent& component) const;
        float GetColliderRadius(const ColliderComponent& component) const;
        void PrepareColliderGenerationWrap(ColliderComponent& component);
        void PrepareBodyGenerationWrap(RigidBodyComponent& component);
        void PrepareOverlapBeginGenerationWrap(ColliderComponent& component, PhysicsCallbackHandle& handle);
        void ReconcileActiveStates();
        void IntegrateDynamics(float fixedDeltaTime);
        void ResolveContacts(float fixedDeltaTime);
        void BuildEventQueue();
        void DispatchEvents();
        void PublishSnapshot();
        void BuildBroadphase(PhysicsBroadphase& outBroadphase) const;
        void ResetTransientState();
        Math::Transform GetFreshWorldTransform(const Core::Entity& entity) const;
        Core::Scene::BodyHandle FindBodyHandle(const Core::Entity& owner) const;
        ColliderSlot* FindColliderSlot(Core::Scene::ColliderHandle handle);
        const ColliderSlot* FindColliderSlot(Core::Scene::ColliderHandle handle) const;
        BodySlot* FindBodySlot(Core::Scene::BodyHandle handle);
        void MoveDynamicBody(BodySlot& body, const Math::Vector3& displacement);
        void ReleaseColliderSlot(uint32_t index);
        void ReleaseBodySlot(uint32_t index);

        Core::Scene::EPhysicsSceneQueryResult Raycast(
            const Math::Ray& ray,
            float maxDistance,
            Core::Scene::PhysicsRaycastHit& outHit) const override;
        Core::Scene::EPhysicsSceneQueryResult OverlapSphere(
            const Math::Sphere& sphere,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const override;
        Core::Scene::EPhysicsSceneQueryResult OverlapBox(
            const Math::OBB& box,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const override;
        Core::Scene::EPhysicsSceneQueryResult OverlapCapsule(
            const Math::Capsule& capsule,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const override;
        Core::Scene::EPhysicsSceneQueryResult IsAlive(Core::Scene::ColliderHandle collider, bool& outAlive) const override;
        Core::Scene::EPhysicsSceneQueryResult IsAlive(Core::Scene::BodyHandle body, bool& outAlive) const override;

        bool IsOwnerThread() const;
        Core::Scene::EPhysicsSceneQueryResult GetReadinessResult() const;

        Core::Engine::Engine* m_Engine = nullptr;
        Thread::Thread::ThreadId m_OwnerThreadId;
        bool m_bBound = false;
        bool m_bInitialized = false;
        bool m_bHasPublishedSnapshot = false;
        Core::Container::VariableArray<ColliderSlot> m_ColliderSlots;
        Core::Container::VariableArray<BodySlot> m_BodySlots;
        Core::Container::VariableArray<uint32_t> m_FreeColliderSlotIndices;
        Core::Container::VariableArray<uint32_t> m_FreeBodySlotIndices;
        EPhysicsDiagnostic m_LastDiagnostic = EPhysicsDiagnostic::None;
        uint32_t m_DuplicateDiagnosticCount = 0;
        uint32_t m_DispatchedEventCount = 0;
        uint32_t m_PendingEventCount = 0;
        Core::Container::VariableArray<TriggerPairState> m_PreviousTriggerPairs;
        Core::Container::VariableArray<TriggerPairState> m_CurrentTriggerPairs;
        Core::Container::VariableArray<PendingPhysicsEvent> m_PendingEvents;
        PhysicsBroadphase m_WorkingBroadphase;
        PhysicsBroadphase m_PublishedBroadphase;
    };
} // namespace NorvesLib::Modules::Physics
