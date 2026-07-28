#pragma once

#include "Physics/IPhysicsModule.h"
#include "Scene/SceneQuery.h"
#include "Thread/Thread.h"

namespace NorvesLib::Modules::Physics
{
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
    };
} // namespace NorvesLib::Modules::Physics
