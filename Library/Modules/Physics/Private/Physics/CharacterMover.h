#pragma once
#include "Physics/PhysicsBroadphase.h"

namespace NorvesLib::Modules::Physics
{
    // Yを上とするworld-spaceカプセル。体格・重力・速度の決定は呼出側が所有する。
    struct CharacterMoveSettings
    {
        float SkinWidth = .002f;
        float StepHeight = .25f;
        float GroundSnapDistance = .08f;
        float MaximumSlopeDegrees = 50;
        float MaximumDepenetrationDistance = 1;
        uint32_t SlideIterations = 6;
        uint32_t DepenetrationIterations = 8;
    };
    struct CharacterMoveRequest
    {
        Math::Capsule Shape;
        Math::Vector3 Displacement;
        Math::Vector3 PlatformDisplacement;
        Core::Scene::PhysicsQueryFilter Filter;
        bool bWasGrounded = false;
        bool bAllowGroundSnap = true;
        bool bAllowStep = true;
        CharacterMoveRequest()
        {
            Filter.TriggerPolicy = Core::Scene::EPhysicsQueryTriggerPolicy::Exclude;
        }
    };
    struct CharacterMoveResult
    {
        Math::Capsule Shape;
        Math::Vector3 Displacement;
        Math::Vector3 GroundNormal = Math::Vector3::UnitY;
        Core::Scene::ColliderHandle GroundCollider;
        Core::Scene::BodyHandle GroundBody;
        bool bGrounded = false;
        bool bHitCeiling = false;
        bool bStepped = false;
        bool bStuck = false;
    };
    struct CharacterMoveScratch
    {
        Core::Container::VariableArray<Core::Scene::PhysicsQueryHit> Hits;
    };
    class CharacterMover final
    {
      public:
        // 入力proxyは同期呼出し中不変。失敗時はoutを変更しない。scratchだけが可変の作業領域。
        // 足場アンカーの解決、速度積分、イベント、Entityへの適用はここでは行わない。
        static Core::Scene::EPhysicsSceneQueryResult Move(Core::Container::Span<const PhysicsShapeProxy> proxies,
                                                          const CharacterMoveSettings& settings,
                                                          const CharacterMoveRequest& request,
                                                          CharacterMoveScratch& scratch, CharacterMoveResult& out);
    };
} // namespace NorvesLib::Modules::Physics
