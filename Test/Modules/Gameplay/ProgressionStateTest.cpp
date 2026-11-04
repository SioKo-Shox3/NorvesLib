// 安定 ID と進行条件の検証、読込失敗時の状態保持、解放通知の一回性と状態の往復を反証する。
#include "Gameplay/Progression/GameplayStableId.h"
#include "Gameplay/Progression/ProgressionState.h"
#include "EnemyTestSupport.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Modules::Gameplay;
    using String = NorvesLib::Core::Container::String;

#define PROGRESSION_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "ProgressionState %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    constexpr const char* kDefinition =
        R"({"version":1,"finalShishaId":"shisha.final","requiredShishaIds":["shisha.forest_01","shisha.river"]})";

    void StableIds()
    {
        PROGRESSION_CHECK(IsValidGameplayStableId("shisha.forest_01"));
        PROGRESSION_CHECK(IsValidGameplayStableId("a"));
        const char* invalid[] = {"", ".a", "a.", "a..b", "a.B", "a-b", "a b"};
        for (const char* id : invalid)
        {
            PROGRESSION_CHECK(!IsValidGameplayStableId(id));
            PROGRESSION_CHECK(!IsValidGameplayStableId(String(id)));
        }
        PROGRESSION_CHECK(!IsValidGameplayStableId(static_cast<const char*>(nullptr)));
        PROGRESSION_CHECK(IsValidGameplayStableId(String(MaxGameplayStableIdLength, 'a')));
        PROGRESSION_CHECK(!IsValidGameplayStableId(String(MaxGameplayStableIdLength + 1, 'a')));
        String embeddedNull("a");
        embeddedNull.append(1, '\0');
        embeddedNull.append("b");
        PROGRESSION_CHECK(!IsValidGameplayStableId(embeddedNull));
    }

    void ProgressionAndRoundTrip()
    {
        ProgressionState state;
        unsigned unlocked = 0;
        state.OnFinalUnlocked.Add([&]() { ++unlocked; });
        PROGRESSION_CHECK(state.LoadDefinitionFromJson(kDefinition));
        state.MarkDefeated("shisha.unknown");
        state.MarkDefeated("shisha.Bad");
        PROGRESSION_CHECK(state.GetIgnoredIdCount() == 2);
        PROGRESSION_CHECK(!state.IsDefeated("shisha.unknown") && !state.IsDefeated("shisha.Bad"));

        state.MarkDefeated("shisha.forest_01");
        PROGRESSION_CHECK(state.IsDefeated("shisha.forest_01"));
        PROGRESSION_CHECK(!state.IsFinalUnlocked() && unlocked == 0);
        const String partial = state.SaveStateToJson();

        const char* invalidDefinitions[] = {
            "{",
            R"({"version":1,"finalShishaId":"shisha.forest_01","requiredShishaIds":["shisha.forest_01"]})",
            R"({"version":1,"finalShishaId":"shisha.final","requiredShishaIds":["shisha.river","shisha.river"]})",
            R"({"version":1,"finalShishaId":"shisha.final","requiredShishaIds":["shisha.Bad"]})",
            R"({"version":1,"finalShishaId":4,"requiredShishaIds":[]})"};
        for (const char* definition : invalidDefinitions)
        {
            PROGRESSION_CHECK(!state.LoadDefinitionFromJson(definition));
            PROGRESSION_CHECK(state.SaveStateToJson() == partial);
            PROGRESSION_CHECK(!state.IsFinalUnlocked() && unlocked == 0);
        }

        state.MarkDefeated("shisha.river");
        PROGRESSION_CHECK(state.IsFinalUnlocked() && unlocked == 1);
        state.MarkDefeated("shisha.river");
        PROGRESSION_CHECK(state.LoadStateFromJson(partial));
        PROGRESSION_CHECK(!state.IsFinalUnlocked() && unlocked == 1);
        state.MarkDefeated("shisha.river");
        state.MarkDefeated("shisha.final");
        PROGRESSION_CHECK(state.IsFinalUnlocked() && unlocked == 1);
        const String saved = state.SaveStateToJson();

        const char* invalidStates[] = {
            "{",
            R"({"version":1,"defeatedEnemyIds":["shisha.forest_01",4]})",
            R"({"version":1,"defeatedEnemyIds":["shisha.forest_01","shisha.forest_01"]})",
            R"({"version":1,"defeatedEnemyIds":["shisha..bad"]})"};
        for (const char* invalid : invalidStates)
        {
            PROGRESSION_CHECK(!state.LoadStateFromJson(invalid));
            PROGRESSION_CHECK(state.SaveStateToJson() == saved && state.IsFinalUnlocked());
        }

        ProgressionState restored;
        unsigned restoredNotifications = 0;
        restored.OnFinalUnlocked.Add([&]() { ++restoredNotifications; });
        PROGRESSION_CHECK(restored.LoadDefinitionFromJson(kDefinition));
        PROGRESSION_CHECK(restored.LoadStateFromJson(saved));
        PROGRESSION_CHECK(restored.IsDefeated("shisha.forest_01") && restored.IsDefeated("shisha.river"));
        PROGRESSION_CHECK(restored.IsDefeated("shisha.final") && !restored.IsDefeated("shisha.unknown"));
        PROGRESSION_CHECK(restored.IsFinalUnlocked() && restoredNotifications == 0);
        PROGRESSION_CHECK(restored.SaveStateToJson() == saved);
        PROGRESSION_CHECK(restored.LoadStateFromJson(partial));
        restored.MarkDefeated("shisha.river");
        PROGRESSION_CHECK(restored.IsFinalUnlocked() && restoredNotifications == 0);
    }

    void EmptyAssetDefinition()
    {
        std::FILE* file = nullptr;
        PROGRESSION_CHECK(fopen_s(&file, NORVES_SOURCE_ROOT "/Assets/Gameplay/progression.json", "rb") == 0);
        PROGRESSION_CHECK(file != nullptr);
        String text;
        char buffer[256];
        size_t count = 0;
        while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0)
        {
            text.append(buffer, count);
        }
        PROGRESSION_CHECK(!std::ferror(file));
        PROGRESSION_CHECK(std::fclose(file) == 0);
        ProgressionState state;
        PROGRESSION_CHECK(state.LoadDefinitionFromJson(text));
        PROGRESSION_CHECK(!state.IsFinalUnlocked());
    }
    void FinalEnemyDormantUntilUnlocked()
    {
        using namespace NorvesLib::Test::EnemySupport;
        auto definition = MakeDefinition();
        definition.Id = "shisha.final";
        definition.bIsFinal = true;
        Fixture fixture(definition);
        auto& progression = fixture.Gameplay.GetProgressionState();
        PROGRESSION_CHECK(progression.LoadDefinitionFromJson(
            R"({"version":1,"finalShishaId":"shisha.final","requiredShishaIds":["shisha.first","shisha.second"]})"));
        PROGRESSION_CHECK(progression.LoadStateFromJson(R"({"version":1,"defeatedEnemyIds":[]})"));
        fixture.AddZone("zone.final", definition.Id.c_str(), 0.0f);
        auto* zone = fixture.Zones.back()->GetComponent<W::EncounterZoneComponent>();
        PROGRESSION_CHECK(zone != nullptr);
        zone->getbRequiresProgression() = true;
        fixture.AddSensor(0.0f);
        fixture.Evaluate();
        fixture.Enemy->TickEnemy(1.0f);
        fixture.Enemy->TickEnemy(1.0f);
        PROGRESSION_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Dormant);
        PROGRESSION_CHECK(fixture.Gameplay.GetEncounterService().GetZoneState("zone.final") != W::EEncounterZoneState::Engaged);
        W::EncounterZoneEvent early;
        early.EnemyId = definition.Id;
        early.ZoneId = "zone.final";
        fixture.Gameplay.GetEncounterService().OnZoneEntered.Broadcast(early);
        progression.MarkDefeated("shisha.first");
        fixture.Enemy->TickEnemy(0.0f);
        PROGRESSION_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Dormant);
        progression.MarkDefeated("shisha.second");
        fixture.Enemy->TickEnemy(0.0f);
        fixture.Enemy->TickEnemy(0.0f);
        fixture.Enemy->TickEnemy(0.0f);
        PROGRESSION_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Idle);

        const auto unlockedState = progression.SaveStateToJson();
        PROGRESSION_CHECK(progression.LoadStateFromJson(R"({"version":1,"defeatedEnemyIds":[]})"));
        Fixture restored(definition);
        restored.Enemy->TickEnemy(0.0f);
        PROGRESSION_CHECK(restored.Enemy->GetState() == E::EEnemyState::Dormant);
        unsigned notifications = 0;
        C::Delegate<void> onUnlocked = [&]() { ++notifications; };
        progression.OnFinalUnlocked.Add(onUnlocked);
        PROGRESSION_CHECK(progression.LoadStateFromJson(unlockedState));
        restored.Enemy->TickEnemy(0.0f);
        restored.Enemy->TickEnemy(0.0f);
        PROGRESSION_CHECK(restored.Enemy->GetState() == E::EEnemyState::Idle && notifications == 0);
        progression.OnFinalUnlocked.Remove(onUnlocked);
    }

    void EmptyDefeatResponsesComplete()
    {
        using namespace NorvesLib::Test::EnemySupport;
        auto definition = MakeDefinition();
        definition.Id = "enemy.responses";
        PROGRESSION_CHECK(definition.DefeatResponses.empty());
        Fixture fixture(definition);
        auto& progression = fixture.Gameplay.GetProgressionState();
        PROGRESSION_CHECK(progression.LoadDefinitionFromJson(
            R"({"version":1,"finalShishaId":"shisha.final","requiredShishaIds":["enemy.responses"]})"));
        PROGRESSION_CHECK(progression.LoadStateFromJson(R"({"version":1,"defeatedEnemyIds":[]})"));
        fixture.StartIdle();
        unsigned completed = 0;
        fixture.Enemy->OnDefeatCompleted.Add([&](const C::Container::String& enemyId)
        {
            PROGRESSION_CHECK(enemyId == definition.Id && progression.IsDefeated(enemyId));
            ++completed;
        });
        fixture.Health->ApplyDamage({100.0f, 0.0f});
        fixture.Animator.bFinished = true;
        fixture.Enemy->TickEnemy(0.0f);
        fixture.Gameplay.Tick(0.0f);
        fixture.Enemy->TickEnemy(0.0f);
        fixture.Gameplay.Tick(0.0f);
        fixture.Enemy->TickEnemy(0.0f);
        PROGRESSION_CHECK(completed == 1 && progression.IsDefeated(definition.Id));
    }
} // namespace

int main()
{
    StableIds();
    ProgressionAndRoundTrip();
    EmptyAssetDefinition();
    FinalEnemyDormantUntilUnlocked();
    EmptyDefeatResponsesComplete();
    std::puts("ProgressionStateTest: PASS");
    return 0;
}
