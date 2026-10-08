#pragma once
#include "Physics/CharacterMoveSettings.h"
#include "Physics/PhysicsTypes.h"
namespace NorvesLib::Modules::Physics
{
    enum class CharacterDriveMode : uint8_t
    {
        Fixed,
        Variable
    };
    enum class CharacterMovementMode : uint8_t
    {
        Walking,
        External
    };
    struct CharacterBodySettings
    {
        CharacterMoveSettings Movement;
        float GravityScale = 1;
    };
    struct CharacterBodyState
    {
        Math::Vector3 Velocity;
        // 足場アンカー自体の速度。壁で制限されたcharacterの実速度とは別。
        Math::Vector3 PlatformVelocity;
        Math::Vector3 GroundNormal = Math::Vector3::UnitY;
        Math::Vector3 GroundPoint;
        Core::Scene::ColliderHandle GroundCollider;
        Core::Scene::BodyHandle GroundBody;
        EPhysicsResult Result = EPhysicsResult::NotRegistered;
        Core::Scene::EPhysicsSceneQueryResult QueryResult = Core::Scene::EPhysicsSceneQueryResult::NotReady;
        uint64_t StepSerial = 0;
        bool bReady = false;
        bool bGrounded = false;
        bool bStuck = false;
    };
} // namespace NorvesLib::Modules::Physics
