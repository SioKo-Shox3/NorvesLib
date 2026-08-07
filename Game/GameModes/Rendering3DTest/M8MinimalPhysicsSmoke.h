#pragma once

#include "Core/Public/Scene/SceneQuery.h"

namespace NorvesLib::Core
{
    class Entity;

    namespace GameMode
    {
        struct GameModeContext;
    }
}

namespace NorvesLib::Modules::Physics
{
    class ColliderComponent;
    class RigidBodyComponent;
}

namespace Game::GameModes
{
    class M8MinimalPhysicsSmoke
    {
    public:
        bool Enter(NorvesLib::Core::GameMode::GameModeContext& ctx);
        void Update(NorvesLib::Core::GameMode::GameModeContext& ctx);
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx);

    private:
        bool ObserveQuery(NorvesLib::Core::GameMode::GameModeContext& ctx);
        bool ObserveStack() const;
        bool ObserveGravityProgress();
        bool VerifyAlive(NorvesLib::Core::GameMode::GameModeContext& ctx) const;
        bool MatchesExpectedHit(
            NorvesLib::Core::Scene::ColliderHandle collider,
            NorvesLib::Core::Scene::BodyHandle body,
            NorvesLib::Core::EntityHandle entity,
            bool bHasEntity) const;
        void ReportFailure(NorvesLib::Core::GameMode::GameModeContext& ctx, const char* reason);

        NorvesLib::Core::Entity* m_pFloorEntity = nullptr;
        NorvesLib::Core::Entity* m_pBoxEntity = nullptr;
        NorvesLib::Core::Entity* m_pSphereEntity = nullptr;
        NorvesLib::Core::Entity* m_pCapsuleEntity = nullptr;
        NorvesLib::Modules::Physics::RigidBodyComponent* m_pBoxBody = nullptr;
        NorvesLib::Modules::Physics::RigidBodyComponent* m_pSphereBody = nullptr;
        NorvesLib::Modules::Physics::RigidBodyComponent* m_pCapsuleBody = nullptr;
        NorvesLib::Core::Scene::ColliderHandle m_FloorCollider;
        NorvesLib::Core::Scene::ColliderHandle m_BoxCollider;
        NorvesLib::Core::Scene::ColliderHandle m_SphereCollider;
        NorvesLib::Core::Scene::ColliderHandle m_CapsuleCollider;
        NorvesLib::Core::Scene::BodyHandle m_BoxBody;
        NorvesLib::Core::Scene::BodyHandle m_SphereBody;
        NorvesLib::Core::Scene::BodyHandle m_CapsuleBody;
        NorvesLib::Core::EntityHandle m_FloorEntityHandle;
        NorvesLib::Core::EntityHandle m_BoxEntityHandle;
        NorvesLib::Core::EntityHandle m_SphereEntityHandle;
        NorvesLib::Core::EntityHandle m_CapsuleEntityHandle;
        NorvesLib::Math::Vector3 m_InitialBoxPosition;
        NorvesLib::Math::Vector3 m_InitialSpherePosition;
        NorvesLib::Math::Vector3 m_InitialCapsulePosition;
        NorvesLib::Math::Vector3 m_PreviousBoxPosition;
        NorvesLib::Math::Vector3 m_PreviousSpherePosition;
        NorvesLib::Math::Vector3 m_PreviousCapsulePosition;
        uint64_t m_LastObservedRenderedFrame = 0;
        uint64_t m_LastPublishedSnapshotSequence = 0;
        uint32_t m_StableObservationCount = 0;
        bool m_bActive = false;
        bool m_bComplete = false;
        bool m_bObservedGravityProgress = false;
        bool m_bHasSnapshotBaseline = false;
    };
} // namespace Game::GameModes
