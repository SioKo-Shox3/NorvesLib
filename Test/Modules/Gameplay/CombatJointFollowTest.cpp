// 合成骨格だけを使い、関節への追従と未評価の姿勢の除外を CPU で検証する。
#include "CombatTestSupport.h"

#include "Animation/SkeletalAssetResource.h"
#include "Component/SkinnedMeshComponent.h"
#include "Gameplay/Combat/CombatRuntime.h"
#include "Gameplay/GameplayModule.h"
#include "Math/MatrixUtils.h"
#include "Object/ResourceRegistry.h"

#include <utility>

using namespace NorvesLib::Test::CombatSupport;

namespace
{
    namespace S = C::Skeletal;

    C::Container::TSharedPtr<C::SkeletalAssetResource> MakeAsset(C::ResourceRegistry& registry)
    {
        auto skeleton = registry.CreateTransient<C::SkeletonResource>("CombatJointSkeleton");
        auto mesh = registry.CreateTransient<C::SkinnedMeshResource>("CombatJointMesh");
        auto clip = registry.CreateTransient<C::AnimationClipResource>("CombatJointClip");
        auto asset = registry.CreateTransient<C::SkeletalAssetResource>("CombatJointAsset");
        COMBAT_CHECK(skeleton && mesh && clip && asset);

        C::Container::VariableArray<S::SkeletalJoint> joints(3);
        joints[0].Name = "Root";
        joints[1].Name = "Arm";
        joints[2].Name = "Tip";
        for (size_t index = 0; index < joints.size(); ++index)
        {
            joints[index].ParentIndex = index == 0 ? -1 : 0;
            joints[index].InverseBindMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        }
        skeleton->SetJoints(std::move(joints));
        COMBAT_CHECK(skeleton->Load());

        S::SkeletalAnimationClip animation;
        animation.Name = "MoveAndTurn";
        animation.DurationSeconds = 1.0f;
        for (uint32_t joint = 1; joint < 3; ++joint)
        {
            S::SkeletalAnimationChannel translation;
            translation.JointIndex = joint;
            translation.Path = S::SkeletalAnimationPath::Translation;
            translation.Samples.push_back({0.0f, {2.0f, 1.0f, -0.5f, 0.0f}});
            translation.Samples.push_back({1.0f, {4.0f, -1.0f, 1.5f, 0.0f}});
            S::SkeletalAnimationChannel rotation;
            rotation.JointIndex = joint;
            rotation.Path = S::SkeletalAnimationPath::Rotation;
            rotation.Samples.push_back({0.0f, {0.0f, 0.0f, 0.0f, 1.0f}});
            const float halfAngle = 0.6f;
            rotation.Samples.push_back({1.0f, {0.0f, 0.0f, std::sin(halfAngle), std::cos(halfAngle)}});
            animation.Channels.push_back(std::move(translation));
            animation.Channels.push_back(std::move(rotation));
        }
        clip->SetClip(std::move(animation));
        COMBAT_CHECK(clip->Load());

        C::Container::VariableArray<S::SkeletalVertex> vertices(3);
        vertices[0].Position = {0.0f, 0.0f, 0.0f};
        vertices[1].Position = {1.0f, 0.0f, 0.0f};
        vertices[2].Position = {0.0f, 1.0f, 0.0f};
        vertices[0].JointIndices[0] = 1;
        vertices[0].JointWeights[0] = 1.0f;
        vertices[1].JointIndices[0] = 1;
        vertices[1].JointWeights[0] = 0.999f;
        vertices[1].JointIndices[1] = 2;
        vertices[1].JointWeights[1] = 0.001f;
        vertices[2].JointIndices[0] = 0;
        vertices[2].JointWeights[0] = 1.0f;
        for (auto& vertex : vertices)
        {
            vertex.Normal = {0.0f, 0.0f, 1.0f};
        }
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices({0, 1, 2});
        COMBAT_CHECK(mesh->Load());
        asset->SetResources(mesh, skeleton, clip);
        COMBAT_CHECK(asset->Load());
        return asset;
    }

    const Combat::CombatVolumeSnapshot* FindVolume(const Combat::CombatRuntime& runtime,
                                                   bool bHitbox, uint64_t registrationIndex,
                                                   uint32_t partIndex = 0)
    {
        for (const auto& snapshot : runtime.GetLastEvaluatedVolumes())
        {
            if (snapshot.bHitbox == bHitbox && snapshot.RegistrationIndex == registrationIndex &&
                snapshot.PartIndex == partIndex)
            {
                return &snapshot;
            }
        }
        return nullptr;
    }

    void CheckNear(const M::Vector3& actual, const M::Vector3& expected)
    {
        COMBAT_CHECK((actual - expected).Length() <= 1.0e-4f);
    }

    void CheckEndpoints(const Combat::CombatPose& pose, const Combat::CombatShape& shape,
                        const M::Matrix4x4& expected)
    {
        CheckNear(pose.Position + pose.Rotation * shape.PointA,
                  M::MatrixUtils::TransformPointRowVector(expected, shape.PointA));
        CheckNear(pose.Position + pose.Rotation * shape.PointB,
                  M::MatrixUtils::TransformPointRowVector(expected, shape.PointB));
    }
}

int main()
{
    ConfigureFailureReporting();
    auto& world = GetWorld();
    auto* module = dynamic_cast<G::GameplayModule*>(&GetGameplay());
    COMBAT_CHECK(module != nullptr);
    auto& runtime = module->GetCombatRuntime();
    C::ResourceRegistry registry;
    COMBAT_CHECK(registry.Initialize());
    const auto asset = MakeAsset(registry);

    auto& owner = SpawnEntity({5.0f, -3.0f, 2.0f});
    SetRotation(owner, M::Quaternion(M::Vector3(1.0f, 2.0f, 3.0f), 0.7f));
    auto& hurtbox = AddHurtboxSet(owner);
    hurtbox.SetWeakJointThreshold({1, 0.01f});
    COMBAT_CHECK(hurtbox.GetWeakJointThreshold().MinVertexCount == 1);
    const auto capsule = Capsule({-0.3f, 0.1f, -0.2f}, {0.7f, 1.2f, 0.4f}, 0.25f);
    Combat::HurtboxPart part;
    part.PartId = 17;
    part.Shape = capsule;
    part.JointName = C::Identity("Arm");
    COMBAT_CHECK(hurtbox.AddPart(part) == 0);
    auto& attacker = SpawnEntity();
    auto& hitbox = AddHitbox(attacker, capsule, MakeProfile());
    COMBAT_CHECK(hitbox.SetBinding(&owner));
    COMBAT_CHECK(hitbox.SetJointBinding(C::Identity("Arm")));
    COMBAT_CHECK(hitbox.GetJointName() == part.JointName);

    // 関節名を先に設定しても、SkinnedMeshComponent の生成順に依存しない。
    const auto initialUnavailable = runtime.GetDiagnostics().JointUnavailableCount;
    runtime.Evaluate(world, 0.0f);
    COMBAT_CHECK(FindVolume(runtime, false, hurtbox.GetRegistrationIndex()) == nullptr);
    COMBAT_CHECK(FindVolume(runtime, true, hitbox.GetRegistrationIndex()) == nullptr);
    COMBAT_CHECK(runtime.GetDiagnostics().JointUnavailableCount == initialUnavailable + 2);
    auto* skinned = world.CreateComponent<C::Component::SkinnedMeshComponent>(&owner);
    COMBAT_CHECK(skinned != nullptr);
    skinned->SetSkeletalAsset(asset);
    skinned->SetPlaying(false);
    skinned->SetLooping(false);
    const int32_t armIndex = skinned->FindJointIndex(part.JointName);
    COMBAT_CHECK(armIndex >= 0);

    for (const float time : {0.0f, 0.5f, 1.0f})
    {
        skinned->SetAnimationTimeSeconds(time);
        COMBAT_CHECK(skinned->EvaluatePose());
        runtime.Evaluate(world, 0.0f);
        M::Matrix4x4 model;
        COMBAT_CHECK(skinned->TryGetJointModelMatrix(static_cast<uint32_t>(armIndex), model));
        const auto& ownerTransform = owner.GetWorldTransform();
        const auto expected = model * M::MatrixUtils::CreateWorldRowVector(
            ownerTransform.position, ownerTransform.rotation, ownerTransform.scale);
        const auto* hurt = FindVolume(runtime, false, hurtbox.GetRegistrationIndex());
        const auto* hit = FindVolume(runtime, true, hitbox.GetRegistrationIndex());
        COMBAT_CHECK(hurt != nullptr && hit != nullptr);
        COMBAT_CHECK(hurt->PartId == part.PartId && !hurt->bWindowOpen);
        CheckEndpoints(hurt->Volume.Current, capsule, expected);
        CheckEndpoints(hit->Volume.Current, capsule, expected);
        COMBAT_CHECK(hit->Volume.Shape.Radius == capsule.Radius);
    }
    COMBAT_CHECK(runtime.GetDiagnostics().WeakJointWarningCount == 0);

    // 通常の hitbox が十分に重なっていても、dirty な関節の部位を原点で判定しない。
    auto& probeOwner = SpawnEntity();
    auto& probe = AddHitbox(probeOwner, Sphere(100.0f), MakeProfile());
    EventRecorder recorder;
    recorder.Subscribe(runtime.OnAnyHit);
    COMBAT_CHECK(probe.BeginAttack() && hitbox.BeginAttack());
    skinned->SetAnimationTimeSeconds(0.25f);
    const auto dirtyUnavailable = runtime.GetDiagnostics().JointUnavailableCount;
    runtime.Evaluate(world, 0.0f);
    COMBAT_CHECK(!skinned->IsPoseCurrent());
    COMBAT_CHECK(recorder.Count() == 0);
    COMBAT_CHECK(FindVolume(runtime, false, hurtbox.GetRegistrationIndex()) == nullptr);
    COMBAT_CHECK(FindVolume(runtime, true, hitbox.GetRegistrationIndex()) == nullptr);
    COMBAT_CHECK(runtime.GetDiagnostics().JointUnavailableCount == dirtyUnavailable + 2);
    COMBAT_CHECK(hitbox.IsWindowOpen());
    COMBAT_CHECK(skinned->EvaluatePose());
    runtime.Evaluate(world, 0.0f);
    COMBAT_CHECK(recorder.Count() == 2);
    const auto* recovered = FindVolume(runtime, false, hurtbox.GetRegistrationIndex());
    COMBAT_CHECK(recovered != nullptr);
    CheckNear(recovered->Volume.Previous.Position, recovered->Volume.Current.Position);
    CheckNear(recovered->Volume.Previous.Rotation * capsule.PointB,
              recovered->Volume.Current.Rotation * capsule.PointB);
    probe.EndAttack();
    hitbox.EndAttack();

    // 存在しない関節は写しにも通知にも含めず、利用できない形として数える。
    part.JointName = C::Identity("MissingJoint");
    part.PartId = 404;
    const auto missingPart = hurtbox.AddPart(part);
    COMBAT_CHECK(missingPart == 1);
    recorder.Clear();
    COMBAT_CHECK(probe.BeginAttack());
    const auto missingUnavailable = runtime.GetDiagnostics().JointUnavailableCount;
    runtime.Evaluate(world, 0.0f);
    COMBAT_CHECK(FindVolume(runtime, false, hurtbox.GetRegistrationIndex(), missingPart) == nullptr);
    COMBAT_CHECK(runtime.GetDiagnostics().JointUnavailableCount == missingUnavailable + 1);
    COMBAT_CHECK(recorder.Count() == 1 && recorder.Events[0].PartId == 17);
    probe.EndAttack();

    // 弱い関節の警告は、同じ資産の解決結果を使う間は繰り返さない。
    part.JointName = C::Identity("Tip");
    part.PartId = 18;
    COMBAT_CHECK(hurtbox.AddPart(part) == 2);
    const auto warnings = runtime.GetDiagnostics().WeakJointWarningCount;
    runtime.Evaluate(world, 0.0f);
    COMBAT_CHECK(runtime.GetDiagnostics().WeakJointWarningCount == warnings + 1);
    COMBAT_CHECK(FindVolume(runtime, false, hurtbox.GetRegistrationIndex(), 2) != nullptr);
    for (int index = 0; index < 3; ++index)
    {
        skinned->SetAnimationTimeSeconds(0.1f * static_cast<float>(index));
        COMBAT_CHECK(skinned->EvaluatePose());
        runtime.Evaluate(world, 0.0f);
        COMBAT_CHECK(runtime.GetDiagnostics().WeakJointWarningCount == warnings + 1);
    }
    const auto diagnostics = runtime.GetDiagnostics();
    runtime.ResetTransientState();
    COMBAT_CHECK(runtime.GetLastEvaluatedVolumes().empty());
    COMBAT_CHECK(runtime.GetDiagnostics().JointUnavailableCount == diagnostics.JointUnavailableCount);
    COMBAT_CHECK(runtime.GetDiagnostics().WeakJointWarningCount == diagnostics.WeakJointWarningCount);
    recorder.Unsubscribe(runtime.OnAnyHit);
    COMBAT_CHECK(world.RemoveEntity(&probeOwner));
    COMBAT_CHECK(world.RemoveEntity(&attacker));
    COMBAT_CHECK(world.RemoveEntity(&owner));
    std::puts("CombatJointFollowTest PASS");
    return 0;
}
