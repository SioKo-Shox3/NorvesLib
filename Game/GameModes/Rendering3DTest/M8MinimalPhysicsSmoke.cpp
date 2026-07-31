#include "M8MinimalPhysicsSmoke.h"

#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/GeometryTypes.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/PhysicsTypes.h"

#include <cmath>

namespace Game::GameModes
{
    namespace Math = NorvesLib::Math;

    namespace
    {
        constexpr uint64_t kMinimumRenderedFrames = 60;
        constexpr uint64_t kFailureRenderedFrames = 110;
        constexpr uint64_t kMinimumObservationFrameInterval = 5;
        constexpr uint32_t kRequiredStableObservations = 3;
        constexpr float kMinimumGravityDescent = 0.05f;
        constexpr float kMaximumSettledSpeed = 1.0f;
        constexpr float kRaycastMaximumDistance = 16.0f;
        constexpr float kRaycastDistanceTolerance = 0.01f;

        bool IsFiniteVector(const NorvesLib::Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool HasFallenFrom(const NorvesLib::Math::Vector3& initialPosition, const NorvesLib::Math::Vector3& position)
        {
            return position.y <= initialPosition.y - kMinimumGravityDescent;
        }
    }

    bool M8MinimalPhysicsSmoke::Enter(NorvesLib::Core::GameMode::GameModeContext& ctx)
    {
        using namespace NorvesLib::Core;
        using namespace NorvesLib::Modules::Physics;

        auto& world = ctx.WorldRef;
        m_pFloorEntity = world.SpawnObject<Entity>();
        m_pBoxEntity = world.SpawnObject<Entity>();
        m_pSphereEntity = world.SpawnObject<Entity>();
        m_pCapsuleEntity = world.SpawnObject<Entity>();
        if (!m_pFloorEntity || !m_pBoxEntity || !m_pSphereEntity || !m_pCapsuleEntity)
        {
            ReportFailure(ctx, "entity_create");
            return false;
        }

        ctx.ScopeRef.TrackObject(m_pFloorEntity);
        ctx.ScopeRef.TrackObject(m_pBoxEntity);
        ctx.ScopeRef.TrackObject(m_pSphereEntity);
        ctx.ScopeRef.TrackObject(m_pCapsuleEntity);

        m_pFloorEntity->SetLocalPosition(0.0f, -1.0f, 0.0f);
        m_pBoxEntity->SetLocalPosition(0.0f, 0.0f, 0.0f);
        m_pSphereEntity->SetLocalPosition(0.0f, 1.1f, 0.0f);
        m_pCapsuleEntity->SetLocalPosition(0.0f, 2.3f, 0.0f);
        m_InitialBoxPosition = m_pBoxEntity->GetWorldTransform().position;
        m_InitialSpherePosition = m_pSphereEntity->GetWorldTransform().position;
        m_InitialCapsulePosition = m_pCapsuleEntity->GetWorldTransform().position;

        ColliderComponent* floorCollider = world.CreateComponent<ColliderComponent>(m_pFloorEntity);
        ColliderComponent* boxCollider = world.CreateComponent<ColliderComponent>(m_pBoxEntity);
        ColliderComponent* sphereCollider = world.CreateComponent<ColliderComponent>(m_pSphereEntity);
        ColliderComponent* capsuleCollider = world.CreateComponent<ColliderComponent>(m_pCapsuleEntity);
        m_pBoxBody = world.CreateComponent<RigidBodyComponent>(m_pBoxEntity);
        m_pSphereBody = world.CreateComponent<RigidBodyComponent>(m_pSphereEntity);
        m_pCapsuleBody = world.CreateComponent<RigidBodyComponent>(m_pCapsuleEntity);
        if (!floorCollider || !boxCollider || !sphereCollider || !capsuleCollider
            || !m_pBoxBody || !m_pSphereBody || !m_pCapsuleBody)
        {
            ReportFailure(ctx, "component_create");
            return false;
        }

        if (floorCollider->SetBox(Math::Vector3(4.0f, 0.5f, 4.0f)) != EPhysicsResult::Success
            || boxCollider->SetBox(Math::Vector3(0.5f, 0.5f, 0.5f)) != EPhysicsResult::Success
            || sphereCollider->SetSphere(0.5f) != EPhysicsResult::Success
            || capsuleCollider->SetCapsule(0.5f, 0.5f) != EPhysicsResult::Success
            || m_pBoxBody->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success
            || m_pSphereBody->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success
            || m_pCapsuleBody->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success)
        {
            ReportFailure(ctx, "physics_configure");
            return false;
        }

        m_FloorCollider = floorCollider->GetColliderHandle();
        m_BoxCollider = boxCollider->GetColliderHandle();
        m_SphereCollider = sphereCollider->GetColliderHandle();
        m_CapsuleCollider = capsuleCollider->GetColliderHandle();
        m_BoxBody = m_pBoxBody->GetBodyHandle();
        m_SphereBody = m_pSphereBody->GetBodyHandle();
        m_CapsuleBody = m_pCapsuleBody->GetBodyHandle();
#if NORVES_ENABLE_COMPONENT_DATA_REGISTRY
        m_FloorEntityHandle = m_pFloorEntity->GetEntityHandle();
        m_BoxEntityHandle = m_pBoxEntity->GetEntityHandle();
        m_SphereEntityHandle = m_pSphereEntity->GetEntityHandle();
        m_CapsuleEntityHandle = m_pCapsuleEntity->GetEntityHandle();
#endif
        if (!m_FloorCollider.IsValid() || !m_BoxCollider.IsValid() || !m_SphereCollider.IsValid()
            || !m_CapsuleCollider.IsValid() || !m_BoxBody.IsValid() || !m_SphereBody.IsValid()
            || !m_CapsuleBody.IsValid()
#if NORVES_ENABLE_COMPONENT_DATA_REGISTRY
            || !m_FloorEntityHandle.IsValid() || !m_BoxEntityHandle.IsValid() || !m_SphereEntityHandle.IsValid()
            || !m_CapsuleEntityHandle.IsValid()
#endif
            )
        {
            ReportFailure(ctx, "invalid_handle");
            return false;
        }

        m_bActive = true;
#if NORVES_BUILD_DEBUG
        LOG_INFO("M8_PHYSICS_SMOKE stage=ready colliders=4 bodies=3");
#endif
        return true;
    }

    void M8MinimalPhysicsSmoke::Update(NorvesLib::Core::GameMode::GameModeContext& ctx)
    {
        if (!m_bActive || m_bComplete)
        {
            return;
        }

        const uint64_t renderedFrames = ctx.EngineRef.GetRenderWorld().GetRenderedFrameCount();
        if (renderedFrames == 0 || renderedFrames - m_LastObservedRenderedFrame < kMinimumObservationFrameInterval)
        {
            return;
        }
        m_LastObservedRenderedFrame = renderedFrames;

        const bool bQueryStable = VerifyAlive(ctx) && ObserveQuery(ctx);
        const bool bStackStable = ObserveStack();
        const bool bGravityProgress = ObserveGravityProgress();
        if (renderedFrames >= kMinimumRenderedFrames && bQueryStable && bStackStable && bGravityProgress)
        {
            ++m_StableObservationCount;
            if (m_StableObservationCount >= kRequiredStableObservations)
            {
                m_bComplete = true;
#if NORVES_BUILD_DEBUG
                LOG_INFO("M8_PHYSICS_SMOKE stage=complete query_stable=1 stack_stable=1 rendered_positive=1 exit_code=0");
#endif
                return;
            }
        }
        else
        {
            m_StableObservationCount = 0;
        }

        if (renderedFrames >= kFailureRenderedFrames)
        {
            ReportFailure(ctx, m_bObservedGravityProgress ? "unstable_observation" : "no_gravity_progress");
        }
    }

    void M8MinimalPhysicsSmoke::Leave(NorvesLib::Core::GameMode::GameModeContext& ctx)
    {
        (void)ctx;
        m_bActive = false;
        m_pFloorEntity = nullptr;
        m_pBoxEntity = nullptr;
        m_pSphereEntity = nullptr;
        m_pCapsuleEntity = nullptr;
        m_pBoxBody = nullptr;
        m_pSphereBody = nullptr;
        m_pCapsuleBody = nullptr;
        m_bObservedGravityProgress = false;
    }

    bool M8MinimalPhysicsSmoke::ObserveQuery(NorvesLib::Core::GameMode::GameModeContext& ctx)
    {
        using namespace NorvesLib::Core::Scene;

        const NorvesLib::Math::Vector3 rayOrigin(0.0f, 8.0f, 0.0f);
        const NorvesLib::Math::Ray ray(rayOrigin, NorvesLib::Math::Vector3(0.0f, -1.0f, 0.0f));
        PhysicsRaycastHit raycastHit;
        const EPhysicsSceneQueryResult raycastResult = ctx.EngineRef.GetSceneQuery().Raycast(
            ray,
            kRaycastMaximumDistance,
            raycastHit);
        if (raycastResult != EPhysicsSceneQueryResult::Success || !std::isfinite(raycastHit.Distance)
            || raycastHit.Distance < 0.0f || raycastHit.Distance > kRaycastMaximumDistance
            || !MatchesExpectedHit(raycastHit.Collider, raycastHit.Body, raycastHit.Entity, raycastHit.bHasEntity)
            || !IsFiniteVector(raycastHit.Point) || !IsFiniteVector(raycastHit.Normal))
        {
            return false;
        }

        const float pointDistance = std::sqrt((raycastHit.Point - rayOrigin).LengthSquared());
        if (!std::isfinite(pointDistance) || std::fabs(pointDistance - raycastHit.Distance) > kRaycastDistanceTolerance)
        {
            return false;
        }

        NorvesLib::Core::Container::VariableArray<PhysicsOverlapHit> overlapHits;
        const EPhysicsSceneQueryResult overlapResult = ctx.EngineRef.GetSceneQuery().OverlapSphere(
            NorvesLib::Math::Sphere(NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f), 8.0f),
            overlapHits);
        if (overlapResult != EPhysicsSceneQueryResult::Success || overlapHits.size() != 4)
        {
            return false;
        }

        bool bFloorSeen = false;
        bool bBoxSeen = false;
        bool bSphereSeen = false;
        bool bCapsuleSeen = false;
        for (const PhysicsOverlapHit& overlapHit : overlapHits)
        {
            if (!MatchesExpectedHit(overlapHit.Collider, overlapHit.Body, overlapHit.Entity, overlapHit.bHasEntity))
            {
                return false;
            }

            if (overlapHit.Collider == m_FloorCollider)
            {
                if (bFloorSeen)
                {
                    return false;
                }
                bFloorSeen = true;
            }
            else if (overlapHit.Collider == m_BoxCollider)
            {
                if (bBoxSeen)
                {
                    return false;
                }
                bBoxSeen = true;
            }
            else if (overlapHit.Collider == m_SphereCollider)
            {
                if (bSphereSeen)
                {
                    return false;
                }
                bSphereSeen = true;
            }
            else if (overlapHit.Collider == m_CapsuleCollider)
            {
                if (bCapsuleSeen)
                {
                    return false;
                }
                bCapsuleSeen = true;
            }
            else
            {
                return false;
            }
        }
        return bFloorSeen && bBoxSeen && bSphereSeen && bCapsuleSeen;
    }

    bool M8MinimalPhysicsSmoke::ObserveStack() const
    {
        if (!m_pBoxEntity || !m_pSphereEntity || !m_pCapsuleEntity
            || !m_pBoxBody || !m_pSphereBody || !m_pCapsuleBody)
        {
            return false;
        }

        const NorvesLib::Math::Vector3& boxPosition = m_pBoxEntity->GetWorldTransform().position;
        const NorvesLib::Math::Vector3& spherePosition = m_pSphereEntity->GetWorldTransform().position;
        const NorvesLib::Math::Vector3& capsulePosition = m_pCapsuleEntity->GetWorldTransform().position;
        const NorvesLib::Math::Vector3 boxVelocity = m_pBoxBody->GetLinearVelocity();
        const NorvesLib::Math::Vector3 sphereVelocity = m_pSphereBody->GetLinearVelocity();
        const NorvesLib::Math::Vector3 capsuleVelocity = m_pCapsuleBody->GetLinearVelocity();
        return IsFiniteVector(boxPosition) && IsFiniteVector(spherePosition) && IsFiniteVector(capsulePosition)
            && IsFiniteVector(boxVelocity) && IsFiniteVector(sphereVelocity) && IsFiniteVector(capsuleVelocity)
            && boxPosition.y >= -0.6f && boxPosition.y <= 0.25f
            && spherePosition.y >= 0.35f && spherePosition.y <= 1.5f
            && capsulePosition.y >= 1.2f && capsulePosition.y <= 3.0f
            && spherePosition.y >= boxPosition.y - 0.05f
            && capsulePosition.y >= spherePosition.y - 0.05f
            && boxVelocity.LengthSquared() <= kMaximumSettledSpeed * kMaximumSettledSpeed
            && sphereVelocity.LengthSquared() <= kMaximumSettledSpeed * kMaximumSettledSpeed
            && capsuleVelocity.LengthSquared() <= kMaximumSettledSpeed * kMaximumSettledSpeed;
    }

    bool M8MinimalPhysicsSmoke::ObserveGravityProgress()
    {
        if (m_bObservedGravityProgress || !m_pBoxEntity || !m_pSphereEntity || !m_pCapsuleEntity)
        {
            return m_bObservedGravityProgress;
        }

        m_bObservedGravityProgress = HasFallenFrom(m_InitialBoxPosition, m_pBoxEntity->GetWorldTransform().position)
            || HasFallenFrom(m_InitialSpherePosition, m_pSphereEntity->GetWorldTransform().position)
            || HasFallenFrom(m_InitialCapsulePosition, m_pCapsuleEntity->GetWorldTransform().position);
        return m_bObservedGravityProgress;
    }

    bool M8MinimalPhysicsSmoke::VerifyAlive(NorvesLib::Core::GameMode::GameModeContext& ctx) const
    {
        bool bAlive = false;
        auto& query = ctx.EngineRef.GetSceneQuery();
        return query.IsAlive(m_FloorCollider, bAlive) == NorvesLib::Core::Scene::EPhysicsSceneQueryResult::Success && bAlive
            && query.IsAlive(m_BoxCollider, bAlive) == NorvesLib::Core::Scene::EPhysicsSceneQueryResult::Success && bAlive
            && query.IsAlive(m_SphereCollider, bAlive) == NorvesLib::Core::Scene::EPhysicsSceneQueryResult::Success && bAlive
            && query.IsAlive(m_CapsuleCollider, bAlive) == NorvesLib::Core::Scene::EPhysicsSceneQueryResult::Success && bAlive
            && query.IsAlive(m_BoxBody, bAlive) == NorvesLib::Core::Scene::EPhysicsSceneQueryResult::Success && bAlive
            && query.IsAlive(m_SphereBody, bAlive) == NorvesLib::Core::Scene::EPhysicsSceneQueryResult::Success && bAlive
            && query.IsAlive(m_CapsuleBody, bAlive) == NorvesLib::Core::Scene::EPhysicsSceneQueryResult::Success && bAlive;
    }

    bool M8MinimalPhysicsSmoke::MatchesExpectedHit(
        NorvesLib::Core::Scene::ColliderHandle collider,
        NorvesLib::Core::Scene::BodyHandle body,
        NorvesLib::Core::EntityHandle entity,
        bool bHasEntity) const
    {
#if NORVES_ENABLE_COMPONENT_DATA_REGISTRY
        if (!bHasEntity || !entity.IsValid())
        {
            return false;
        }
#else
        if (bHasEntity || entity.IsValid())
        {
            return false;
        }
#endif
        if (collider == m_FloorCollider)
        {
            return !body.IsValid()
#if NORVES_ENABLE_COMPONENT_DATA_REGISTRY
                && entity == m_FloorEntityHandle
#endif
                ;
        }
        if (collider == m_BoxCollider)
        {
            return body == m_BoxBody
#if NORVES_ENABLE_COMPONENT_DATA_REGISTRY
                && entity == m_BoxEntityHandle
#endif
                ;
        }
        if (collider == m_SphereCollider)
        {
            return body == m_SphereBody
#if NORVES_ENABLE_COMPONENT_DATA_REGISTRY
                && entity == m_SphereEntityHandle
#endif
                ;
        }
        if (collider == m_CapsuleCollider)
        {
            return body == m_CapsuleBody
#if NORVES_ENABLE_COMPONENT_DATA_REGISTRY
                && entity == m_CapsuleEntityHandle
#endif
                ;
        }
        return false;
    }

    void M8MinimalPhysicsSmoke::ReportFailure(
        NorvesLib::Core::GameMode::GameModeContext& ctx,
        const char* reason)
    {
        if (m_bComplete)
        {
            return;
        }

        m_bActive = false;
#if NORVES_BUILD_DEBUG
        LOG_ERROR("M8_PHYSICS_SMOKE stage=failure reason=%s exit_code=1", reason);
#endif
        ctx.EngineRef.RequestExit(1);
    }
} // namespace Game::GameModes
