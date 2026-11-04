#pragma once

#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Gameplay/Combat/AttackProfile.h"
#include "Text/IdentityPool.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Enemy
{
    struct EnemyAttackPhase
    {
        Core::Container::String Id;
        float Seconds = 0.0f;
        bool bHitWindow = false;
    };

    struct EnemyAttackPattern
    {
        Core::Container::String Id;
        Core::Identity AnimState;
        Core::Container::String AttackProfileId;
        float MinDistance = 0.0f;
        float MaxDistance = 0.0f;
        float Weight = 0.0f;
        float CooldownSeconds = 0.0f;
        Core::Container::VariableArray<EnemyAttackPhase> Phases;
    };

    struct EnemyAttackProfileDef
    {
        Core::Container::String Id;
        float Damage = 0.0f;
        float Stagger = 0.0f;
        float MaxDurationSeconds = 0.0f;
        float HitStopSeconds = 0.0f;
    };

    Core::Container::TSharedPtr<const Combat::AttackProfile> MakeAttackProfile(const EnemyAttackProfileDef& definition);

    enum class EEnemyCollapseCondition : uint8_t
    {
        Stagger,
        HealthBelow,
        PartHits
    };

    struct EnemyCollapseRule
    {
        EEnemyCollapseCondition Condition = EEnemyCollapseCondition::Stagger;
        float HealthFraction = 0.0f;
        uint32_t PartId = 0;
        uint32_t HitCount = 0;
        float RecoverSeconds = 2.0f;
    };

    struct EnemyAnimStates
    {
        Core::Identity Idle;
        Core::Identity Alerted;
        Core::Identity Collapsed;
        Core::Identity Recover;
        Core::Identity Defeated;
    };

    struct EnemyDefinition
    {
        Core::Container::String SourceName;
        Core::Container::String Id;
        Core::Container::String Archetype;
        bool bIsFinal = false;
        float MaxHealth = 0.0f;
        float StaggerThreshold = 0.0f;
        Core::Identity AnimMachine;
        EnemyAnimStates AnimStates;
        float AlertDelaySeconds = 0.0f;
        float AttackIntervalSeconds = 0.0f;
        float RepeatPenalty = 0.25f;
        Core::Container::VariableArray<EnemyAttackProfileDef> AttackProfiles;
        Core::Container::VariableArray<EnemyAttackPattern> Patterns;
        Core::Container::String OpeningPatternId;
        EnemyCollapseRule Collapse;
        float DefeatHoldSeconds = 1.5f;
        Core::Container::VariableArray<Core::Container::String> DefeatResponses;
    };

    const EnemyAttackProfileDef* FindAttackProfile(const EnemyDefinition& definition, const Core::Container::String& id);
    int32_t FindPatternIndex(const EnemyDefinition& definition, const Core::Container::String& id);

    /// 不正な定義は適用せず、入力元と該当キーを含むエラーを返す。
    bool LoadEnemyDefinitionFromJson(const Core::Container::String& text, const Core::Container::String& sourceName,
                                     EnemyDefinition& out, Core::Container::String& outError);
    /// Assets からの相対パスで定義を読み込む。
    bool LoadEnemyDefinitionFromAsset(const Core::Container::String& assetPath, EnemyDefinition& out,
                                      Core::Container::String& outError);
} // namespace NorvesLib::Modules::Gameplay::Enemy
