#pragma once

#include "Container/Containers.h"
#include "Physics/CharacterMover.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsBroadphase.h"
#include "Physics/PhysicsTypes.h"
#include "Scene/SceneQuery.h"
#include "Thread/Thread.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::Physics
{
    class ColliderComponent;
    class CharacterBodyComponent;
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
        void RegisterReflectedTypes() override;
        bool Initialize() override;
        void Tick(float deltaTime) override;
        void StepPreFixed(float fixedDeltaTime) override;
        void StepFixed(float fixedDeltaTime) override;
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
            bool bRetired = false;
        };

        struct BodySlot
        {
            RigidBodyComponent* Component = nullptr;
            Core::Entity* Owner = nullptr;
            uint32_t Generation = 1;
            bool bOccupied = false;
            bool bActive = false;
            bool bRetired = false;
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
        friend class CharacterBodyComponent;
        EPhysicsResult ValidateCharacterAccess(const CharacterBodyComponent& character) const;
        void RegisterCharacter(CharacterBodyComponent& character);
        void UnregisterCharacter(CharacterBodyComponent& character);
        // World の段に物理のシステムを登録する（同じ World へは 1 度だけ）。CharacterBody の可変の処理の登録もここ
        void RegisterWorldSystems(Core::World& world);
        // world に束ねられていて、初期化済みのときだけ自分を返す（段のシステムの呼び先の解決）
        static PhysicsModule* FindBoundModule(const Core::World& world);
        // 固定ステップの CharacterBody の要求（一覧の順 = 作成の順）。物理の一歩の前に段 FixedTick で呼ぶ。
        // collectionSerial は段が走っている収集の番号で、参加の番号がそれより後のものは呼ばない
        void RequestFixedCharacters(const Core::World& world, float fixedDeltaTime, uint64_t collectionSerial);
        void ProcessCharacterBodies(float dt, CharacterBodyComponent* onlyVariable = nullptr);
        void ProcessVariableCharacter(CharacterBodyComponent& character, float dt);
        void DispatchCharacterEvents();
        friend class RigidBodyComponent;
        friend class PhysicsModuleTestAccess;

        EPhysicsResult RegisterCollider(ColliderComponent& component);
        EPhysicsResult RegisterRigidBody(RigidBodyComponent& component);
        EPhysicsResult UnregisterCollider(ColliderComponent& component);
        EPhysicsResult UnregisterRigidBody(RigidBodyComponent& component);
        EPhysicsResult SetColliderSphere(ColliderComponent& component, float radius);
        EPhysicsResult SetColliderBox(ColliderComponent& component, const Math::Vector3& halfExtents);
        EPhysicsResult SetColliderCapsule(ColliderComponent& component, float radius, float halfHeight);
        EPhysicsResult SetColliderLocalPose(ColliderComponent& component, const Math::Transform& localPose);
        EPhysicsResult SetColliderTrigger(ColliderComponent& component, bool bTrigger);
        EPhysicsResult SetColliderLayer(ColliderComponent& component, Core::Scene::PhysicsCollisionMask layer);
        EPhysicsResult SetColliderMask(ColliderComponent& component, Core::Scene::PhysicsCollisionMask mask);
        EPhysicsResult SetColliderUserData(ColliderComponent& component, uint64_t userData);
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
        bool IsBodyLifecycleActive(const BodySlot& body) const;
        bool IsColliderLifecycleActive(const ColliderSlot& collider) const;
        void DiscardBodyStepState(BodySlot& body);
        void ReconcileActiveStates();
        void UpdateRenderInterpolationDefault(BodySlot& body);
        void IntegrateDynamics(float fixedDeltaTime);
        void ResolveContacts(float fixedDeltaTime);
        void BuildEventQueue();
        void DispatchEvents();
        void PublishSnapshot();
        void BuildBroadphase(PhysicsBroadphase& outBroadphase, bool bRefreshLifecycle = false) const;
        void ResetTransientState();
        Math::Transform GetFreshWorldTransform(const Core::Entity& entity) const;
        Core::Scene::BodyHandle FindBodyHandle(const Core::Entity& owner, bool bRefreshLifecycle = false) const;
        ColliderSlot* FindColliderSlot(Core::Scene::ColliderHandle handle);
        const ColliderSlot* FindColliderSlot(Core::Scene::ColliderHandle handle) const;
        BodySlot* FindBodySlot(Core::Scene::BodyHandle handle);
        void MoveDynamicBody(BodySlot& body, const Math::Vector3& displacement);
        void ReleaseColliderSlot(uint32_t index);
        void ReleaseBodySlot(uint32_t index);

        Core::Scene::EPhysicsSceneQueryResult RefreshDynamicSnapshot() override;
        Core::Scene::EPhysicsSceneQueryResult ExecuteBatch(Core::Container::Span<const Core::Scene::PhysicsQueryDesc> queries,
            Core::Container::VariableArray<Core::Scene::PhysicsQueryHit>& outHits,
            Core::Container::VariableArray<Core::Scene::PhysicsQueryBatchResult>& outResults) const override;
        Core::Scene::EPhysicsSceneQueryResult ExecuteQuery(const Core::Scene::PhysicsQueryDesc& query,
            Core::Container::VariableArray<Core::Scene::PhysicsQueryHit>& outHits) const override;
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
        Core::Scene::EPhysicsSceneQueryResult GetPublishedSnapshotSequence(uint64_t& outSequence) const override;

        bool IsOwnerThread() const;
        Core::Scene::EPhysicsSceneQueryResult GetReadinessResult() const;

        Core::Engine::Engine* m_Engine = nullptr;
        Thread::Thread::ThreadId m_OwnerThreadId;
        bool m_bBound = false;
        bool m_bInitialized = false;
        bool m_bFixedTickInProgress = false;
        // 要求を出している間の削除は null にしておき、走査の後で詰める
        bool m_bRequestingCharacters = false;
        // 参加の番号 = 登録したときの「次の収集」の番号（スケジューラの順序付きの呼び先と同じ規則）。
        // 収集の中で足したものは次の収集から固定の要求に加わる（旧の Tick の先頭の一覧と同じ）
        struct CharacterEntry
        {
            CharacterBodyComponent* Character = nullptr;
            uint64_t JoinSerial = 0;
        };
        Core::Container::VariableArray<CharacterEntry> m_Characters;
        bool m_bVariableCharacterInProgress = false;
        bool m_bCharacterInputInProgress = false;
        bool m_bHasPublishedSnapshot = false;
        uint64_t m_PublishedSnapshotSequence = 0;
        Core::Container::VariableArray<ColliderSlot> m_ColliderSlots;
        Core::Container::VariableArray<BodySlot> m_BodySlots;
        Core::Container::VariableArray<PhysicsShapeProxy> m_CharacterQueryProxies;
        CharacterMoveScratch m_CharacterScratch;
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
