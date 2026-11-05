// 戦闘判定データの値、入力検証、失敗時の出力保持を確かめる。
#include "Gameplay/Combat/CombatData.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Combat;
    using String = NorvesLib::Core::Container::String;
    using Identity = NorvesLib::Core::Identity;
    using Vector3 = NorvesLib::Math::Vector3;

#define COMBAT_DATA_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "CombatProfileLoadTest %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    const char* const kFactions = R"({"factions":{"player":0,"enemy":1,"neutral":31}})";
    const char* const kAttack = R"({"id":"testAttack","damage":25,"stagger":5,"hitStopSeconds":0.0625,"rehit":"interval","rehitIntervalSeconds":0.25,"perVictim":"allParts","hitFactions":["enemy","neutral"],"hitboxes":[{"joint":"hand_r","windowEvent":"blade","maxDurationSeconds":0.5,"shape":{"type":"capsule","pointA":[1,2,3],"pointB":[4,5,6],"radius":0.125}},{"windowEvent":"landing","shape":{"type":"sphere","center":[-1,0,1],"radius":2}}]})";
    const char* const kHurtboxes = R"({"id":"testBody","weakJoint":{"minVertexCount":3,"minWeightSum":0.5},"parts":[{"partId":7,"joint":"Head","surface":"Metal","faction":"enemy","damageScale":1.5,"priority":-2,"flags":["weak","anchorable"],"shape":{"type":"box","center":[1,2,3],"halfExtents":[0.5,1,2]}},{"partId":8,"surface":"Metal","faction":"player","shape":{"type":"sphere","center":[0,0,0],"radius":0.25}},{"partId":9,"surface":"Metal","faction":"neutral","shape":{"type":"capsule","pointA":[0,0,0],"pointB":[0,1,0],"radius":0.5}}]})";

    String Replace(const String& source, const char* before, const char* after)
    {
        String result = source;
        const String target(before);
        const size_t position = result.find(target);
        COMBAT_DATA_CHECK(position != String::npos);
        result.replace(position, target.size(), after);
        return result;
    }

    void CheckError(const String& error, const char* source, const char* path)
    {
        COMBAT_DATA_CHECK(error.find(String(source) + ": " + path + ": ") == 0);
    }

    void CheckFactionValues(const CombatFactionTable& factions)
    {
        COMBAT_DATA_CHECK(factions.Names.size() == 3 && factions.Bits.size() == 3);
        uint32_t bit = 0;
        COMBAT_DATA_CHECK(factions.FindBit("player", bit) && bit == 1u);
        COMBAT_DATA_CHECK(factions.FindBit("enemy", bit) && bit == 2u);
        COMBAT_DATA_CHECK(factions.FindBit("neutral", bit) && bit == (uint32_t{1} << 31));
        COMBAT_DATA_CHECK(!factions.FindBit("missing", bit) && bit == (uint32_t{1} << 31));
    }

    void CheckAttackValues(const CombatAttackDefinition& attack)
    {
        COMBAT_DATA_CHECK(attack.SourceName == "unit.attack.json" && attack.Id == "testAttack");
        COMBAT_DATA_CHECK(attack.Hitboxes.size() == 2);
        const auto& blade = attack.Hitboxes[0];
        const auto& landing = attack.Hitboxes[1];
        COMBAT_DATA_CHECK(blade.JointName == Identity("hand_r") && !landing.JointName.IsValid());
        COMBAT_DATA_CHECK(blade.Shape.Type == ECombatShapeType::Capsule);
        COMBAT_DATA_CHECK(blade.Shape.PointA == Vector3(1, 2, 3) && blade.Shape.PointB == Vector3(4, 5, 6));
        COMBAT_DATA_CHECK(blade.Shape.Radius == 0.125f);
        COMBAT_DATA_CHECK(landing.Shape.Type == ECombatShapeType::Sphere);
        COMBAT_DATA_CHECK(landing.Shape.Center == Vector3(-1, 0, 1) && landing.Shape.Radius == 2.0f);
        COMBAT_DATA_CHECK(blade.Profile && landing.Profile && blade.Profile != landing.Profile);
        const AttackProfile& profile = *blade.Profile;
        COMBAT_DATA_CHECK(profile.ProfileId == Identity("testAttack#0") && landing.Profile->ProfileId == Identity("testAttack#1"));
        COMBAT_DATA_CHECK(profile.WindowEventName == Identity("blade") && landing.Profile->WindowEventName == Identity("landing"));
        COMBAT_DATA_CHECK(profile.MaxDurationSeconds == 0.5f && landing.Profile->MaxDurationSeconds == 1.0f);
        COMBAT_DATA_CHECK(profile.RehitPolicy == ERehitPolicy::Interval && profile.RehitIntervalSeconds == 0.25f);
        COMBAT_DATA_CHECK(profile.PerVictimPolicy == EPerVictimPolicy::AllParts);
        COMBAT_DATA_CHECK(profile.HitMask == (2u | (uint32_t{1} << 31)));
        COMBAT_DATA_CHECK(profile.Damage == 25.0f && profile.Stagger == 5.0f && profile.HitStopSecondsHint == 0.0625f);
        COMBAT_DATA_CHECK(landing.Profile->RehitPolicy == profile.RehitPolicy && landing.Profile->RehitIntervalSeconds == profile.RehitIntervalSeconds);
        COMBAT_DATA_CHECK(landing.Profile->PerVictimPolicy == profile.PerVictimPolicy && landing.Profile->HitMask == profile.HitMask);
        COMBAT_DATA_CHECK(landing.Profile->Damage == profile.Damage && landing.Profile->Stagger == profile.Stagger &&
                          landing.Profile->HitStopSecondsHint == profile.HitStopSecondsHint);
    }

    void CheckHurtboxValues(const CombatHurtboxDefinition& body)
    {
        COMBAT_DATA_CHECK(body.SourceName == "unit.hurtboxes.json" && body.Id == "testBody");
        COMBAT_DATA_CHECK(body.WeakJoint.MinVertexCount == 3 && body.WeakJoint.MinWeightSum == 0.5f);
        COMBAT_DATA_CHECK(body.Parts.size() == 3);
        const HurtboxPart& head = body.Parts[0];
        COMBAT_DATA_CHECK(head.PartId == 7 && head.JointName == Identity("Head") && head.SurfaceName == Identity("Metal"));
        COMBAT_DATA_CHECK(head.Faction == 2u && head.DamageScale == 1.5f && head.Priority == -2);
        COMBAT_DATA_CHECK(head.Flags == (EHurtboxFlags::Weak | EHurtboxFlags::Anchorable));
        COMBAT_DATA_CHECK(head.Shape.Type == ECombatShapeType::Box && head.Shape.Center == Vector3(1, 2, 3));
        COMBAT_DATA_CHECK(head.Shape.HalfExtents == Vector3(0.5f, 1, 2));
        const HurtboxPart& sphere = body.Parts[1];
        COMBAT_DATA_CHECK(sphere.PartId == 8 && !sphere.JointName.IsValid() && sphere.Faction == 1u);
        COMBAT_DATA_CHECK(sphere.DamageScale == 1.0f && sphere.Priority == 0 && sphere.Flags == EHurtboxFlags::None);
        COMBAT_DATA_CHECK(sphere.Shape.Type == ECombatShapeType::Sphere && sphere.Shape.Radius == 0.25f);
        const HurtboxPart& capsule = body.Parts[2];
        COMBAT_DATA_CHECK(capsule.PartId == 9 && capsule.Faction == (uint32_t{1} << 31));
        COMBAT_DATA_CHECK(capsule.Shape.Type == ECombatShapeType::Capsule && capsule.Shape.PointB == Vector3(0, 1, 0));
        COMBAT_DATA_CHECK(capsule.Shape.Radius == 0.5f);
    }

    void RejectFactions(const String& text, const char* path, CombatFactionTable& out)
    {
        String error;
        COMBAT_DATA_CHECK(!LoadCombatFactionsFromJson(text, "unit.factions.json", out, error));
        CheckError(error, "unit.factions.json", path);
        CheckFactionValues(out);
    }

    void RejectAttack(const String& text, const char* path, const CombatFactionTable& factions, CombatAttackDefinition& out)
    {
        const auto* original = out.Hitboxes[0].Profile.get();
        String error;
        COMBAT_DATA_CHECK(!LoadCombatAttackFromJson(text, "unit.attack.json", factions, out, error));
        CheckError(error, "unit.attack.json", path);
        CheckAttackValues(out);
        COMBAT_DATA_CHECK(out.Hitboxes[0].Profile.get() == original);
    }

    void RejectHurtboxes(const String& text, const char* path, const CombatFactionTable& factions, CombatHurtboxDefinition& out)
    {
        String error;
        COMBAT_DATA_CHECK(!LoadCombatHurtboxesFromJson(text, "unit.hurtboxes.json", factions, out, error));
        CheckError(error, "unit.hurtboxes.json", path);
        CheckHurtboxValues(out);
    }

    void CheckBundledAssets()
    {
        String error;
        CombatFactionTable factions;
        COMBAT_DATA_CHECK(LoadCombatFactionsFromAsset("Combat/factions.json", factions, error));
        uint32_t bit = 0;
        COMBAT_DATA_CHECK(factions.FindBit("player", bit) && bit == 1u);
        COMBAT_DATA_CHECK(factions.FindBit("enemy", bit) && bit == 2u);
        COMBAT_DATA_CHECK(factions.FindBit("neutral", bit) && bit == 4u);

        CombatAttackDefinition attack;
        COMBAT_DATA_CHECK(LoadCombatAttackFromAsset("Combat/slash_horizontal.attack.json", factions, attack, error));
        COMBAT_DATA_CHECK(attack.Hitboxes.size() == 1 && attack.Hitboxes[0].Profile);
        COMBAT_DATA_CHECK(attack.Hitboxes[0].Shape.Type == ECombatShapeType::Capsule);
        COMBAT_DATA_CHECK(attack.Hitboxes[0].Profile->RehitPolicy == ERehitPolicy::OncePerVictim);
        COMBAT_DATA_CHECK(attack.Hitboxes[0].Profile->ProfileId == Identity("slash_horizontal"));
        COMBAT_DATA_CHECK(LoadCombatAttackFromAsset("Combat/spin_slash.attack.json", factions, attack, error));
        COMBAT_DATA_CHECK(attack.Hitboxes.size() == 1 && attack.Hitboxes[0].Profile->RehitPolicy == ERehitPolicy::Interval);
        COMBAT_DATA_CHECK(attack.Hitboxes[0].Profile->RehitIntervalSeconds > 0.0f);
        COMBAT_DATA_CHECK(LoadCombatAttackFromAsset("Combat/leap_slash.attack.json", factions, attack, error));
        COMBAT_DATA_CHECK(attack.Hitboxes.size() == 2);
        COMBAT_DATA_CHECK(attack.Hitboxes[0].Profile->WindowEventName != attack.Hitboxes[1].Profile->WindowEventName);
        COMBAT_DATA_CHECK(attack.Hitboxes[0].Shape.Type == ECombatShapeType::Capsule &&
                          attack.Hitboxes[1].Shape.Type == ECombatShapeType::Sphere);
        COMBAT_DATA_CHECK(LoadCombatAttackFromAsset("Combat/bite.attack.json", factions, attack, error));
        COMBAT_DATA_CHECK(attack.Hitboxes.size() == 1 && attack.Hitboxes[0].Shape.Type == ECombatShapeType::Sphere);
        COMBAT_DATA_CHECK(attack.Hitboxes[0].JointName == Identity("Head"));

        CombatHurtboxDefinition body;
        COMBAT_DATA_CHECK(LoadCombatHurtboxesFromAsset("Combat/humanoid.hurtboxes.json", factions, body, error));
        COMBAT_DATA_CHECK(body.Id == "humanoid" && body.Parts.size() == 6);
        COMBAT_DATA_CHECK(body.Parts[0].PartId == 0 && body.Parts[0].SurfaceName == Identity("Flesh"));
        COMBAT_DATA_CHECK(body.Parts[1].PartId == 1 && body.Parts[1].SurfaceName == Identity("Metal"));
        COMBAT_DATA_CHECK(LoadCombatHurtboxesFromAsset("Combat/quadruped.hurtboxes.json", factions, body, error));
        COMBAT_DATA_CHECK(body.Id == "quadruped" && body.Parts.size() == 6);
    }
} // namespace

int main()
{
    String error;
    CombatFactionTable factions;
    COMBAT_DATA_CHECK(LoadCombatFactionsFromJson(kFactions, "unit.factions.json", factions, error) && error.empty());
    CheckFactionValues(factions);
    RejectFactions(R"({"factions":{"player":0,"enemy":0}})", "factions.enemy", factions);
    RejectFactions(R"({"factions":{"player":32}})", "factions.player", factions);
    RejectFactions(R"({"factions":{"player":0,"player":1}})", "factions.player", factions);
    RejectFactions(R"({"factions":{},"extra":true})", "extra", factions);

    CombatAttackDefinition attack;
    COMBAT_DATA_CHECK(LoadCombatAttackFromJson(kAttack, "unit.attack.json", factions, attack, error) && error.empty());
    CheckAttackValues(attack);
    CombatAttackDefinition alternate;
    COMBAT_DATA_CHECK(LoadCombatAttackFromJson(R"({"id":"single","damage":0,"rehit":"oncePerPart","rehitIntervalSeconds":0,"perVictim":"highestPriority","hitFactions":["player"],"hitboxes":[{"windowEvent":"active","shape":{"type":"sphere","center":[0,0,0],"radius":1}}]})", "single.attack.json", factions, alternate, error));
    COMBAT_DATA_CHECK(alternate.Hitboxes.size() == 1 && alternate.Hitboxes[0].Profile);
    COMBAT_DATA_CHECK(alternate.Hitboxes[0].Profile->ProfileId == Identity("single"));
    COMBAT_DATA_CHECK(alternate.Hitboxes[0].Profile->RehitPolicy == ERehitPolicy::OncePerPart && alternate.Hitboxes[0].Profile->RehitIntervalSeconds == 0.0f);
    COMBAT_DATA_CHECK(alternate.Hitboxes[0].Profile->PerVictimPolicy == EPerVictimPolicy::HighestPriority && alternate.Hitboxes[0].Profile->HitMask == 1u);
    COMBAT_DATA_CHECK(alternate.Hitboxes[0].Profile->MaxDurationSeconds == 1.0f && alternate.Hitboxes[0].Profile->Stagger == 0.0f);
    RejectAttack(Replace(kAttack, R"("damage":25,)", ""), "damage", factions, attack);
    RejectAttack(Replace(kAttack, R"("damage":25)", R"("damage":25,"unexpected":true)"), "unexpected", factions, attack);
    RejectAttack(Replace(kAttack, R"("radius":0.125)", R"("radius":0)"), "hitboxes[0].shape.radius", factions, attack);
    RejectAttack(Replace(kAttack, R"("radius":0.125)", R"("radius":-1)"), "hitboxes[0].shape.radius", factions, attack);
    RejectAttack(Replace(kAttack, R"("windowEvent":"blade",)", ""), "hitboxes[0].windowEvent", factions, attack);
    RejectAttack(Replace(kAttack, R"("windowEvent":"blade")", R"("windowEvent":"")"), "hitboxes[0].windowEvent", factions, attack);
    RejectAttack(Replace(kAttack, R"("enemy","neutral")", R"("unknown","neutral")"), "hitFactions[0]", factions, attack);
    RejectAttack(Replace(kAttack, R"("hitFactions":["enemy","neutral"])", R"("hitFactions":[])"), "hitFactions", factions, attack);
    RejectAttack(Replace(kAttack, R"("rehitIntervalSeconds":0.25,)", ""), "rehitIntervalSeconds", factions, attack);
    RejectAttack(Replace(kAttack, R"("rehitIntervalSeconds":0.25)", R"("rehitIntervalSeconds":0)"), "rehitIntervalSeconds", factions, attack);
    RejectAttack(Replace(kAttack, R"("damage":25)", R"("damage":1e400)"), "damage", factions, attack);
    RejectAttack(Replace(kAttack, R"("type":"capsule")", R"("type":"box")"), "hitboxes[0].shape.type", factions, attack);
    RejectAttack(Replace(kAttack, R"("pointA":[1,2,3])", R"("pointA":[1,2])"), "hitboxes[0].shape.pointA", factions, attack);

    CombatHurtboxDefinition body;
    COMBAT_DATA_CHECK(LoadCombatHurtboxesFromJson(kHurtboxes, "unit.hurtboxes.json", factions, body, error) && error.empty());
    CheckHurtboxValues(body);
    RejectHurtboxes(Replace(kHurtboxes, R"("faction":"enemy")", R"("faction":"unknown")"), "parts[0].faction", factions, body);
    RejectHurtboxes(Replace(kHurtboxes, R"("partId":8)", R"("partId":7)"), "parts[1].partId", factions, body);
    RejectHurtboxes(Replace(kHurtboxes, R"("halfExtents":[0.5,1,2])", R"("halfExtents":[0.5,0,2])"), "parts[0].shape.halfExtents[1]", factions, body);
    RejectHurtboxes(Replace(kHurtboxes, R"("flags":["weak","anchorable"])", R"("flags":["unknown"])"), "parts[0].flags[0]", factions, body);
    RejectHurtboxes(Replace(kHurtboxes, R"("minWeightSum":0.5)", R"("minWeightSum":-1)"), "weakJoint.minWeightSum", factions, body);

    COMBAT_DATA_CHECK(!LoadCombatFactionsFromAsset("Combat/missing.json", factions, error));
    CheckError(error, "Combat/missing.json", "$");
    CheckFactionValues(factions);
    COMBAT_DATA_CHECK(!LoadCombatAttackFromAsset("Combat/missing.json", factions, attack, error));
    CheckError(error, "Combat/missing.json", "$");
    CheckAttackValues(attack);
    COMBAT_DATA_CHECK(!LoadCombatHurtboxesFromAsset("Combat/missing.json", factions, body, error));
    CheckError(error, "Combat/missing.json", "$");
    CheckHurtboxValues(body);

    CheckBundledAssets();
    std::puts("CombatProfileLoadTest PASS");
    return 0;
}
