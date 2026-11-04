// 敵定義の読込結果、不正な参照や値の拒否、失敗時の出力保持を反証する。
#include "Gameplay/Enemy/EnemyDefinition.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Enemy;
    using String = NorvesLib::Core::Container::String;
    using Identity = NorvesLib::Core::Identity;

#define ENEMY_DEFINITION_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "EnemyDefinitionLoad %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    constexpr const char* kPatterns = R"("patterns":[{"id":"slash","animState":"AttackSlash","attackProfileId":"shisha.human_01.slash","minDistance":0,"maxDistance":3,"weight":1,"cooldownSeconds":2,"phases":[{"id":"windup","seconds":0.5},{"id":"active","seconds":0.2,"hitWindow":true},{"id":"recovery","seconds":0.6}]}],)";

    String ValidDefinition()
    {
        return String(R"({"id":"shisha.human_01","archetype":"humanoid_medium","isFinal":true,"maxHealth":300,"staggerThreshold":100,"animMachine":"Main","animStates":{"idle":"Idle","alerted":"Alert","collapsed":"Collapse","recover":"Recover","defeated":"Defeat"},"alertDelaySeconds":0.5,"attackIntervalSeconds":0.8,"repeatPenalty":0.25,"attackProfiles":[{"id":"shisha.human_01.slash","damage":20,"stagger":4,"maxDurationSeconds":1,"hitStopSeconds":0.05}],)") +
            kPatterns + R"("openingPatternId":"slash","collapse":{"condition":"healthBelow","healthFraction":0.5,"partId":0,"hitCount":3,"recoverSeconds":2.5},"defeat":{"holdSeconds":1.5},"defeatResponses":[]})";
    }

    String Replace(const String& source, const char* before, const char* after)
    {
        String result = source;
        const String target(before);
        const size_t position = result.find(target);
        ENEMY_DEFINITION_CHECK(position != String::npos);
        result.replace(position, target.size(), after);
        return result;
    }

    void CheckValues(const EnemyDefinition& definition)
    {
        ENEMY_DEFINITION_CHECK(definition.SourceName == "shisha.enemy.json");
        ENEMY_DEFINITION_CHECK(definition.Id == "shisha.human_01" && definition.Archetype == "humanoid_medium");
        ENEMY_DEFINITION_CHECK(definition.bIsFinal && definition.MaxHealth == 300.0f && definition.StaggerThreshold == 100.0f);
        ENEMY_DEFINITION_CHECK(definition.AnimMachine == Identity("Main"));
        ENEMY_DEFINITION_CHECK(definition.AnimStates.Idle == Identity("Idle"));
        ENEMY_DEFINITION_CHECK(definition.AnimStates.Alerted == Identity("Alert"));
        ENEMY_DEFINITION_CHECK(definition.AnimStates.Collapsed == Identity("Collapse"));
        ENEMY_DEFINITION_CHECK(definition.AnimStates.Recover == Identity("Recover"));
        ENEMY_DEFINITION_CHECK(definition.AnimStates.Defeated == Identity("Defeat"));
        ENEMY_DEFINITION_CHECK(definition.AlertDelaySeconds == 0.5f && definition.AttackIntervalSeconds == 0.8f);
        ENEMY_DEFINITION_CHECK(definition.RepeatPenalty == 0.25f && definition.OpeningPatternId == "slash");
        ENEMY_DEFINITION_CHECK(definition.AttackProfiles.size() == 1 && definition.Patterns.size() == 1);
        const auto& profile = definition.AttackProfiles[0];
        ENEMY_DEFINITION_CHECK(profile.Id == "shisha.human_01.slash" && profile.Damage == 20.0f && profile.Stagger == 4.0f);
        ENEMY_DEFINITION_CHECK(profile.MaxDurationSeconds == 1.0f && profile.HitStopSeconds == 0.05f);
        ENEMY_DEFINITION_CHECK(FindAttackProfile(definition, profile.Id) == &profile);
        ENEMY_DEFINITION_CHECK(FindAttackProfile(definition, "missing") == nullptr);
        const auto attack = MakeAttackProfile(profile);
        ENEMY_DEFINITION_CHECK(attack && attack->ProfileId == Identity(profile.Id) && !attack->WindowEventName.IsValid());
        ENEMY_DEFINITION_CHECK(attack->Damage == profile.Damage && attack->Stagger == profile.Stagger);
        ENEMY_DEFINITION_CHECK(attack->MaxDurationSeconds == profile.MaxDurationSeconds && attack->HitStopSecondsHint == profile.HitStopSeconds);
        const auto& pattern = definition.Patterns[0];
        ENEMY_DEFINITION_CHECK(FindPatternIndex(definition, "slash") == 0 && FindPatternIndex(definition, "missing") == -1);
        ENEMY_DEFINITION_CHECK(pattern.Id == "slash" && pattern.AnimState == Identity("AttackSlash"));
        ENEMY_DEFINITION_CHECK(pattern.AttackProfileId == profile.Id && pattern.MinDistance == 0.0f && pattern.MaxDistance == 3.0f);
        ENEMY_DEFINITION_CHECK(pattern.Weight == 1.0f && pattern.CooldownSeconds == 2.0f && pattern.Phases.size() == 3);
        ENEMY_DEFINITION_CHECK(pattern.Phases[0].Id == "windup" && pattern.Phases[0].Seconds == 0.5f && !pattern.Phases[0].bHitWindow);
        ENEMY_DEFINITION_CHECK(pattern.Phases[1].Id == "active" && pattern.Phases[1].Seconds == 0.2f && pattern.Phases[1].bHitWindow);
        ENEMY_DEFINITION_CHECK(pattern.Phases[2].Id == "recovery" && pattern.Phases[2].Seconds == 0.6f && !pattern.Phases[2].bHitWindow);
        ENEMY_DEFINITION_CHECK(definition.Collapse.Condition == EEnemyCollapseCondition::HealthBelow);
        ENEMY_DEFINITION_CHECK(definition.Collapse.HealthFraction == 0.5f && definition.Collapse.PartId == 0 && definition.Collapse.HitCount == 3);
        ENEMY_DEFINITION_CHECK(definition.Collapse.RecoverSeconds == 2.5f && definition.DefeatHoldSeconds == 1.5f);
        ENEMY_DEFINITION_CHECK(definition.DefeatResponses.empty());
    }

    void CheckRejected(const String& text, const char* path, EnemyDefinition& out)
    {
        String error;
        ENEMY_DEFINITION_CHECK(!LoadEnemyDefinitionFromJson(text, "shisha.enemy.json", out, error));
        ENEMY_DEFINITION_CHECK(error.find("shisha.enemy.json") != String::npos && error.find(path) != String::npos);
        CheckValues(out);
    }
} // namespace

int main()
{
    const String valid = ValidDefinition();
    EnemyDefinition definition;
    String error;
    ENEMY_DEFINITION_CHECK(LoadEnemyDefinitionFromJson(valid, "shisha.enemy.json", definition, error));
    ENEMY_DEFINITION_CHECK(error.empty());
    CheckValues(definition);

    CheckRejected(Replace(valid, R"("id":"shisha.human_01",)", ""), "id", definition);
    CheckRejected(Replace(valid, R"("maxHealth":300,)", ""), "maxHealth", definition);
    CheckRejected(Replace(valid, kPatterns, ""), "patterns", definition);
    CheckRejected(Replace(valid, R"(,"defeated":"Defeat")", ""), "animStates.defeated", definition);
    CheckRejected(Replace(valid, R"("openingPatternId":"slash")", R"("openingPatternId":"unknown")"), "openingPatternId", definition);
    CheckRejected(Replace(valid, R"("attackProfileId":"shisha.human_01.slash")", R"("attackProfileId":"unknown")"), "patterns[0].attackProfileId", definition);
    CheckRejected(Replace(valid, R"("weight":1)", R"("weight":-1)"), "patterns[0].weight", definition);
    CheckRejected(Replace(valid, R"("minDistance":0)", R"("minDistance":4)"), "patterns[0].maxDistance", definition);
    CheckRejected(Replace(valid, R"("minDistance":0)", R"("minDistance":-1)"), "patterns[0].minDistance", definition);
    CheckRejected(Replace(valid, R"("maxHealth":300)", R"("unknown":true,"maxHealth":300)"), "unknown", definition);
    CheckRejected(Replace(valid, R"("weight":1)", R"("unknown":true,"weight":1)"), "patterns[0].unknown", definition);

    // 同梱定義を実際の資産ルートから読み、識別子とパターン数を確かめる。
    EnemyDefinition bundledDefinition;
    ENEMY_DEFINITION_CHECK(LoadEnemyDefinitionFromAsset("Gameplay/enemies/shisha_human_01.enemy.json", bundledDefinition, error));
    ENEMY_DEFINITION_CHECK(bundledDefinition.Id == "shisha.human_01" && bundledDefinition.Patterns.size() == 2);

    std::puts("EnemyDefinitionLoadTest: PASS");
    return 0;
}
