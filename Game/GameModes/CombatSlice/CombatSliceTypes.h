#pragma once

#include "Container/String.h"
#include "Gameplay/World/EncounterZoneTypes.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace Game::GameModes
{
    enum class CombatSliceSmoke : uint8_t
    {
        Combat,
        Encounter,
        Enemy
    };

    struct CombatSliceOptions
    {
        CombatSliceSmoke Smoke = CombatSliceSmoke::Combat;
        bool bBench = false;
        NorvesLib::Core::Container::String CapturePath;
    };

    struct CombatSliceSceneIds
    {
        uint64_t Floor = 0;
        uint64_t Light = 0;
        uint64_t Camera = 0;
        uint64_t Zone = 0;
        uint64_t Bed = 0;
        uint64_t Prompt = 0;
        uint64_t Dog = 0;
        uint64_t Sword = 0;
        uint64_t Shisha = 0;
    };

    namespace CombatSliceLayout
    {
        // 配置は定数初期化し、使用時にエンジンのベクトルへ変換する。
        struct Position
        {
            float x;
            float y;
            float z;

            NorvesLib::Math::Vector3 ToVector3() const
            {
                return {x, y, z};
            }

            operator NorvesLib::Math::Vector3() const
            {
                return ToVector3();
            }
        };

        // Y が上。距離は m、時間は秒、角度は度で表す。
        inline constexpr Position ShishaPosition{0, 0, 0};
        inline constexpr Position ShishaForward{0, 0, -1};
        inline constexpr Position DogStrikePosition{0, 0, -0.8f};
        inline constexpr Position DogStartPosition{0, 0, -7.0f};
        inline constexpr Position DogForward{0, 0, 1};
        inline constexpr Position DogHeadLocal{0, 0.85f, 0.45f};
        inline constexpr const char* DogHeadJoint = "Head";
        inline constexpr const char* DogMouthSocket = "Mouth";
        inline constexpr Position DogMouthOffset{0, 0, 0};
        inline constexpr float BladeStart = 0.05f;
        inline constexpr float BladeEnd = 0.95f;
        inline constexpr float BladeRadius = 0.06f;

        inline constexpr Position ShishaRootLocal{0, 0, 0};
        inline constexpr Position ShishaHeadLocal{0, 1.65f, 0};
        inline constexpr Position ShishaHandLeftLocal{-0.3f, 1.15f, 0.2f};
        inline constexpr Position ShishaHandRightLocal{0.3f, 1.15f, 0.2f};
        inline constexpr Position ShishaLegLeftLocal{-0.15f, 0.5f, 0};
        inline constexpr Position ShishaLegRightLocal{0.15f, 0.5f, 0};

        inline constexpr const char* EnemyId = "shisha.combat_slice";
        inline constexpr const char* EnemyDefinitionPath = "Gameplay/enemies/shisha_combat_slice.enemy.json";
        inline constexpr const char* ProgressionPath = "Gameplay/combat_slice.progression.json";
        inline constexpr const char* SwordAttackPath = "Combat/combat_slice_sword.attack.json";
        inline constexpr const char* HurtboxPath = "Combat/humanoid.hurtboxes.json";
        inline constexpr const char* FactionsPath = "Combat/factions.json";

        inline constexpr const char* ZoneId = "combat_slice.zone";
        inline constexpr Position ZoneCenter = ShishaPosition;
        inline constexpr auto ZoneShape = NorvesLib::Modules::Gameplay::World::EEncounterZoneShape::CylinderY;
        inline constexpr float ZoneRadius = 3.0f;
        inline constexpr float ZoneHalfHeight = 2.0f;
        inline constexpr float ZoneEnterMargin = 0.25f;
        inline constexpr float ZoneExitMargin = 0.5f;
        inline constexpr float ZoneResetDelaySeconds = 1.0f;

        inline constexpr Position BedPosition{-3, 0, -4.5f};
        inline constexpr const char* BedActionId = "combat_slice.rest";
        inline constexpr const char* BedPromptTextKey = "REST";
        inline constexpr float BedEnterRadius = 1.2f;
        inline constexpr float BedExitRadius = 1.6f;
        inline constexpr float BedFacingConeDegrees = 360.0f;
        inline constexpr bool bBedDisabledWhileEngaged = true;
        inline constexpr bool bBedRequiresFreeMouth = false;
    } // namespace CombatSliceLayout
} // namespace Game::GameModes
