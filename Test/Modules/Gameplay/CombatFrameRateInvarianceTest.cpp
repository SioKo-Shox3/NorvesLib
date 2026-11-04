// 同じ回転軌道を異なる刻みで進め、直接窓とクリップの窓で命中集合を保つ。
#include "CombatTestSupport.h"

#include "Animation/SkeletonResource.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/QuaternionUtils.h"
#include "Object/ResourceRegistry.h"

#include <utility>

namespace
{
    using namespace NorvesLib::Test::CombatSupport;
    namespace A = NorvesLib::Core::Animation;
    namespace S = NorvesLib::Core::Skeletal;

    constexpr float TipRadius = 1.0f;
    constexpr double AngularVelocity = 10.0 / TipRadius;
    constexpr float BladeRadius = 0.02f;
    constexpr float HurtRadius = 0.05f;
    constexpr double WindowBegins[] = {0.025, 0.265};
    constexpr double WindowEnds[] = {0.235, 0.475};
    constexpr float VictimRadii[] = {0.7f, 0.85f, 0.75f, 0.9f, 0.8f};
    constexpr float VictimAngles[] = {1.25f, 1.75f, 3.25f, 3.75f, 5.25f};
    constexpr size_t VictimCount = 5;

    struct HitKey
    {
        size_t Victim;
        uint32_t Part;
        uint64_t Attack;

        bool operator==(const HitKey&) const = default;
    };
    using HitSet = C::Container::VariableArray<HitKey>;
    constexpr HitKey Expected[] = {{0, 10, 0}, {1, 11, 0}, {2, 12, 1}, {3, 13, 1}};

    void CheckSameSet(const HitSet& expected, const HitSet& actual)
    {
        COMBAT_CHECK(expected.size() == actual.size());
        for (const auto& key : expected)
        {
            size_t matches = 0;
            for (const auto& other : actual)
            {
                matches += key == other ? 1 : 0;
            }
            COMBAT_CHECK(matches == 1);
        }
    }

    // 外部ファイルや GPU を使わず、Animator が要求する最小の資産とグラフを作る。
    struct ClipFixture final : A::IClipResolver
    {
        C::ResourceRegistry Registry;
        C::Container::TSharedPtr<C::AnimationClipResource> Clip;
        C::Container::TSharedPtr<C::SkeletalAssetResource> Asset;
        C::Container::TSharedPtr<C::AnimGraphResource> Graph;

        ClipFixture()
        {
            COMBAT_CHECK(Registry.Initialize());
            auto skeleton = Registry.CreateTransient<C::SkeletonResource>("CombatSkeleton");
            auto mesh = Registry.CreateTransient<C::SkinnedMeshResource>("CombatMesh");
            Clip = Registry.CreateTransient<C::AnimationClipResource>("CombatClip");
            Asset = Registry.CreateTransient<C::SkeletalAssetResource>("CombatAsset");
            Graph = Registry.CreateTransient<C::AnimGraphResource>("CombatGraph");
            COMBAT_CHECK(skeleton && mesh && Clip && Asset && Graph);

            C::Container::VariableArray<S::SkeletalJoint> joints(1);
            joints[0].Name = "Root";
            joints[0].InverseBindMatrix.fill(0);
            for (unsigned i = 0; i < 4; ++i)
            {
                joints[0].InverseBindMatrix[i * 5] = 1;
            }
            skeleton->SetJoints(std::move(joints));
            COMBAT_CHECK(skeleton->Load());
            C::Container::VariableArray<S::SkeletalVertex> vertices(1);
            vertices[0].Normal = {0, 1, 0};
            vertices[0].JointWeights[0] = 1;
            mesh->SetVertices(std::move(vertices));
            mesh->SetIndices(C::Container::VariableArray<uint32_t>{0, 0, 0});
            COMBAT_CHECK(mesh->Load());

            S::SkeletalAnimationClip clip;
            clip.Name = "swing";
            clip.DurationSeconds = 1;
            S::SkeletalAnimationChannel channel;
            channel.JointIndex = 0;
            channel.Path = S::SkeletalAnimationPath::Translation;
            channel.Samples = {{0, {0, 0, 0, 0}}, {1, {0, 0, 0, 0}}};
            clip.Channels.push_back(std::move(channel));
            Clip->SetClip(std::move(clip));
            COMBAT_CHECK(Clip->Load());
            A::ClipMetadataReport metadataReport;
            COMBAT_CHECK(Clip->ApplyMetadataJson(C::Container::String(
                R"({"events":[{"name":"Combat.Window","t":0.025,"end":0.235},{"name":"Combat.Window","t":0.265,"end":0.475}]})"),
                metadataReport));
            Asset->SetResources(mesh, skeleton, Clip);
            COMBAT_CHECK(Asset->Load());
            A::AnimGraphReport graphReport;
            COMBAT_CHECK(Graph->Compile(C::Container::String(
                R"({"version":1,"nodes":[{"id":"swing","type":"clip","clip":"swing","loop":false}],"root":"swing"})"),
                *skeleton, *this, graphReport));
            COMBAT_CHECK(Graph->Load());
        }

        C::Container::TSharedPtr<C::AnimationClipResource> ResolveClip(C::Container::StringView name) const override
        {
            return name == C::Container::StringView("swing") ? Clip
                : C::Container::TSharedPtr<C::AnimationClipResource>{};
        }
    };

    // 粗い刻みの静止姿勢では全標的を外すことを、線分への距離で独立に確認する。
    void CheckCoarseSamplesMiss()
    {
        for (unsigned frame = 0; frame <= 12; ++frame)
        {
            const double angle = AngularVelocity * frame / 20.0;
            for (size_t victim = 0; victim < VictimCount; ++victim)
            {
                const double relative = VictimAngles[victim] - angle;
                const double along = VictimRadii[victim] * std::cos(relative);
                const double perpendicular = VictimRadii[victim] * std::sin(relative);
                const double nearest = along < 0.2 ? 0.2 : along > TipRadius ? TipRadius : along;
                const double distanceSquared = (along - nearest) * (along - nearest) + perpendicular * perpendicular;
                COMBAT_CHECK(distanceSquared > (BladeRadius + HurtRadius) * (BladeRadius + HurtRadius));
            }
        }
    }

    HitSet RunSwing(float dt, bool bAnimation)
    {
        auto& attacker = SpawnEntity();
        Combat::AttackProfile settings;
        settings.WindowEventName = C::Identity("Combat.Window");
        settings.RehitPolicy = Combat::ERehitPolicy::OncePerVictim;
        settings.PerVictimPolicy = Combat::EPerVictimPolicy::FirstPart;
        auto& hitbox = AddHitbox(attacker, Capsule({0.2f, 0, 0}, {TipRadius, 0, 0}, BladeRadius), MakeProfile(settings));
        C::Entity* victims[VictimCount] = {};
        Combat::HurtboxSetComponent* hurtboxes[VictimCount] = {};
        uint64_t actorIds[VictimCount] = {};
        EventRecorder hit, hurt, any;
        hit.Subscribe(hitbox.OnHit);
        any.Subscribe(GetGameplay().GetOnAnyHit());
        for (size_t i = 0; i < VictimCount; ++i)
        {
            // Y 軸の正回転で +X から -Z へ向かう弧。第二部位も刀身の厚み内にある。
            victims[i] = &SpawnEntity({VictimRadii[i] * std::cos(VictimAngles[i]), 0,
                                       -VictimRadii[i] * std::sin(VictimAngles[i])});
            hurtboxes[i] = &AddHurtbox(*victims[i], static_cast<uint32_t>(10 + i), Sphere(HurtRadius));
            AddPart(*hurtboxes[i], static_cast<uint32_t>(20 + i), Sphere(HurtRadius, {0, 0.025f, 0}));
            actorIds[i] = hurtboxes[i]->GetActorId();
            COMBAT_CHECK(actorIds[i] != 0);
            hurt.Subscribe(hurtboxes[i]->OnHurt);
        }

        C::Container::TUniquePtr<ClipFixture> fixture;
        C::Component::AnimatorComponent* animator = nullptr;
        C::Container::VariableArray<A::AnimEventInfo> animationEvents;
        if (bAnimation)
        {
            fixture = C::Container::MakeUnique<ClipFixture>();
            auto* mesh = GetWorld().CreateComponent<C::Component::SkinnedMeshComponent>(&attacker);
            COMBAT_CHECK(mesh != nullptr);
            mesh->SetSkeletalAsset(fixture->Asset);
            animator = &AddAnimator(attacker);
            COMBAT_CHECK(animator->SetGraph(fixture->Graph));
            animator->OnEvent.Add([&animationEvents](const A::AnimEventInfo& event)
            {
                animationEvents.push_back(event);
            });
        }

        uint64_t firstAttack = 0;
        size_t openedWindows = 0;
        double previousTime = 0;
        for (unsigned frame = 1; previousTime < 0.6; ++frame)
        {
            const double time = static_cast<double>(frame) * dt;
            SetRotation(attacker, M::QuaternionUtils::FromAxisAngle(M::Vector3::UnitY,
                                                                   static_cast<float>(AngularVelocity * time)));
            const uint64_t previousAttack = hitbox.GetAttackInstanceId();
            if (bAnimation)
            {
                animator->TickAnimationGroup(dt);
                animator->TickPoseFinalizeGroup();
            }
            else
            {
                for (size_t window = 0; window < 2; ++window)
                {
                    if (previousTime < WindowBegins[window] && time >= WindowBegins[window])
                    {
                        COMBAT_CHECK(hitbox.BeginAttack());
                    }
                    if (previousTime < WindowEnds[window] && time >= WindowEnds[window])
                    {
                        hitbox.EndAttack();
                    }
                }
            }
            const bool bExpectedOpen = (time >= WindowBegins[0] && time < WindowEnds[0]) ||
                                       (time >= WindowBegins[1] && time < WindowEnds[1]);
            COMBAT_CHECK(hitbox.IsWindowOpen() == bExpectedOpen);
            if (hitbox.IsWindowOpen() && hitbox.GetAttackInstanceId() != previousAttack)
            {
                if (openedWindows == 0)
                {
                    firstAttack = hitbox.GetAttackInstanceId();
                    COMBAT_CHECK(firstAttack != 0);
                }
                COMBAT_CHECK(hitbox.GetAttackInstanceId() - firstAttack == openedWindows);
                ++openedWindows;
            }
            StepFrame(dt);
            previousTime = time;
        }
        COMBAT_CHECK(openedWindows == 2 && !hitbox.IsWindowOpen());
        if (bAnimation)
        {
            COMBAT_CHECK(animationEvents.size() == 4);
            for (size_t i = 0; i < 4; ++i)
            {
                COMBAT_CHECK(animationEvents[i].Name == settings.WindowEventName);
                COMBAT_CHECK(animationEvents[i].Kind == (i % 2 == 0 ? A::AnimEventKind::Begin : A::AnimEventKind::End));
                COMBAT_CHECK(animationEvents[i].WindowToken != 0);
                COMBAT_CHECK(animationEvents[i].WindowToken == animationEvents[(i / 2) * 2].WindowToken);
                COMBAT_CHECK(!animationEvents[i].bInterrupted);
            }
            COMBAT_CHECK(animationEvents[0].WindowToken != animationEvents[2].WindowToken);
        }

        COMBAT_CHECK(hit.Count() == any.Count() && hurt.Count() == any.Count());
        HitSet result;
        for (const auto& event : any.Events)
        {
            size_t victim = 0;
            while (victim < VictimCount && actorIds[victim] != event.VictimActorId)
            {
                ++victim;
            }
            COMBAT_CHECK(victim < VictimCount && event.AttackInstanceId >= firstAttack);
            const HitKey key{victim, event.PartId, event.AttackInstanceId - firstAttack};
            result.push_back(key);
            // 部位が違っても同じ victim と攻撃番号への通知は一度だけ。
            for (const auto* recorder : {&hit, &hurt, &any})
            {
                size_t deliveries = 0;
                for (const auto& delivered : recorder->Events)
                {
                    if (delivered.VictimActorId == event.VictimActorId &&
                        delivered.AttackInstanceId == event.AttackInstanceId)
                    {
                        ++deliveries;
                        COMBAT_CHECK(delivered.PartId == event.PartId && delivered.FrameNumber == event.FrameNumber);
                    }
                }
                COMBAT_CHECK(deliveries == 1);
            }
        }
        const HitSet expected{Expected[0], Expected[1], Expected[2], Expected[3]};
        CheckSameSet(expected, result);
        hit.Unsubscribe(hitbox.OnHit);
        any.Unsubscribe(GetGameplay().GetOnAnyHit());
        for (size_t i = 0; i < VictimCount; ++i)
        {
            hurt.Unsubscribe(hurtboxes[i]->OnHurt);
            GetWorld().RemoveEntity(victims[i]);
        }
        GetWorld().RemoveEntity(&attacker);
        std::printf("刻み=1/%u 窓=%s 命中数=%zu\n", static_cast<unsigned>(std::lround(1.0f / dt)),
                    bAnimation ? "クリップ" : "直接", result.size());
        return result;
    }

    void CheckOtherWindowName()
    {
        auto& attacker = SpawnEntity();
        Combat::AttackProfile settings;
        settings.WindowEventName = C::Identity("Combat.Window");
        auto& hitbox = AddHitbox(attacker, Capsule({0.2f, 0, 0}, {TipRadius, 0, 0}, BladeRadius), MakeProfile(settings));
        auto& animator = AddAnimator(attacker);
        A::AnimEventInfo other;
        other.Name = C::Identity("Combat.OtherWindow");
        other.Kind = A::AnimEventKind::Begin;
        other.WindowToken = 1;
        animator.OnEvent.Broadcast(other);
        COMBAT_CHECK(!hitbox.IsWindowOpen() && hitbox.GetAttackInstanceId() == 0);
        GetWorld().RemoveEntity(&attacker);
    }
} // 無名名前空間

int main()
{
    ConfigureFailureReporting();
    CheckCoarseSamplesMiss();
    CheckOtherWindowName();
    HitSet baseline;
    for (const float dt : {1.0f / 20.0f, 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 144.0f})
    {
        const auto direct = RunSwing(dt, false);
        const auto animated = RunSwing(dt, true);
        CheckSameSet(direct, animated);
        if (baseline.empty())
        {
            baseline = direct;
        }
        CheckSameSet(baseline, direct);
        CheckSameSet(baseline, animated);
    }
    std::puts("CombatFrameRateInvarianceTest PASS");
    return 0;
}
