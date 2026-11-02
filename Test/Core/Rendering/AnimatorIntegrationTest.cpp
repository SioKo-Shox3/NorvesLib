// World で駆動した Animator と単一クリップの Sample を全関節で比較する。
// 途中の関節を根にした部分マスクの上書き・加算を独立した数値で検証する。
#include "Animation/AnimGraphInstance.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletalAssetResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace
{
    using namespace NorvesLib::Core;
    namespace A = NorvesLib::Core::Animation;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Skeletal;
    namespace M = NorvesLib::Math;

#define INTEGRATION_CHECK(expression)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::fprintf(stderr, "Animator 結合の検査に失敗: %s:%d %s\n", __FILE__, __LINE__, #expression);           \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

    void CheckValue(float actual, float expected, float scale, const char* label, size_t joint, size_t element)
    {
        if (!(std::fabs(actual - expected) <= 1e-5f * scale))
        {
            std::fprintf(stderr, "Animator 結合の不一致: %s 関節=%zu 要素=%zu 実際=%.9g 期待=%.9g 許容=%.9g\n",
                         label, joint, element, double(actual), double(expected), double(1e-5f * scale));
            std::abort();
        }
    }

    void CheckMatrices(const C::VariableArray<M::Matrix4x4>& actual,
                       const C::VariableArray<M::Matrix4x4>& expected, const char* label)
    {
        INTEGRATION_CHECK(actual.size() == expected.size());
        float scale = 1;
        for (const auto& matrix : expected)
            for (float value : matrix.values)
            {
                INTEGRATION_CHECK(std::isfinite(value));
                scale = std::fmax(scale, std::fabs(value));
            }
        for (size_t joint = 0; joint < expected.size(); ++joint)
            for (size_t element = 0; element < 16; ++element)
                CheckValue(actual[joint].values[element], expected[joint].values[element], scale, label, joint, element);
    }

    void AddChannel(S::SkeletalAnimationClip& clip, uint32_t joint, S::SkeletalAnimationPath path,
                    const S::SkeletalValue& first, const S::SkeletalValue& last)
    {
        S::SkeletalAnimationChannel channel;
        channel.JointIndex = joint;
        channel.Path = path;
        channel.Interpolation = S::SkeletalAnimationInterpolation::Linear;
        channel.Samples.push_back({0, first});
        channel.Samples.push_back({1, last});
        clip.Channels.push_back(std::move(channel));
    }

    struct IntegrationFixture final : A::IClipResolver
    {
        ResourceRegistry Registry;
        C::TSharedPtr<SkeletonResource> Skeleton;
        C::TSharedPtr<SkinnedMeshResource> Mesh;
        C::TSharedPtr<AnimationClipResource> AClip, BClip;
        C::TSharedPtr<SkeletalAssetResource> Asset;

        explicit IntegrationFixture(bool layered)
        {
            INTEGRATION_CHECK(Registry.Initialize());
            Skeleton = Registry.CreateTransient<SkeletonResource>("IntegrationSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("IntegrationMesh");
            AClip = Registry.CreateTransient<AnimationClipResource>("IntegrationA");
            BClip = Registry.CreateTransient<AnimationClipResource>("IntegrationB");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("IntegrationAsset");
            INTEGRATION_CHECK(Skeleton && Mesh && AClip && BClip && Asset);
            const uint32_t count = layered ? 4 : 5;
            C::VariableArray<S::SkeletalJoint> joints(count);
            const int32_t parents[] = {3, -1, 0, 1, -1};
            const char* names[] = {"Root", "Spine", "Neck", "Head"};
            for (uint32_t i = 0; i < count; ++i)
            {
                char name[32];
                std::snprintf(name, sizeof(name), "Joint%u", i);
                joints[i].Name = layered ? names[i] : name;
                joints[i].ParentIndex = layered ? int32_t(i) - 1 : parents[i];
                const auto inverseBind = layered ? M::Matrix4x4::Identity
                    : M::MatrixUtils::CreateWorldRowVector(
                          {float(i + 1), -.25f * float(i + 1), .5f * float(i)},
                          M::Quaternion(M::Vector3(0, 0, 1), .125f * float(i + 1)), M::Vector3::One);
                for (size_t element = 0; element < 16; ++element)
                    joints[i].InverseBindMatrix[element] = inverseBind.values[element];
            }
            Skeleton->SetJoints(std::move(joints));
            INTEGRATION_CHECK(Skeleton->Load() && Skeleton->GetPoseRuntime().bValid);

            C::VariableArray<S::SkeletalVertex> vertices(3);
            for (uint32_t i = 0; i < vertices.size(); ++i)
            {
                vertices[i].Position = {float(i), .5f * float(i), -.25f};
                vertices[i].Normal = {0, 1, 0};
                vertices[i].JointIndices[0] = i;
                vertices[i].JointIndices[1] = (i + 2) % count;
                vertices[i].JointWeights[0] = .25f;
                vertices[i].JointWeights[1] = .75f;
            }
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(C::VariableArray<uint32_t>{0, 1, 2});
            INTEGRATION_CHECK(Mesh->Load());

            S::SkeletalAnimationClip a, b;
            a.Name = "a";
            b.Name = "b";
            a.DurationSeconds = b.DurationSeconds = 1;
            if (layered)
            {
                for (uint32_t joint = 0; joint < count; ++joint)
                {
                    const float j = float(joint);
                    AddChannel(a, joint, S::SkeletalAnimationPath::Translation,
                               {1 + j, -2 - j, .5f * j, 0}, {3 + j, 2 - j, 2 + .5f * j, 0});
                    AddChannel(b, joint, S::SkeletalAnimationPath::Translation,
                               {10 + 2 * j, 4 + j, -3 - j, 0}, {16 + 2 * j, 2 + j, 1 - j, 0});
                }
            }
            else
            {
                // 関節 2 と 4 は全チャンネルを欠き、逆バインドと M から得るローカル姿勢を使う。
                for (uint32_t joint : {0u, 1u, 3u})
                {
                    const float j = float(joint);
                    AddChannel(a, joint, S::SkeletalAnimationPath::Translation,
                               {j, .25f, -.5f * j, 0}, {j + 2, -.75f, 1 + j, 0});
                    const M::Quaternion first(M::Vector3(0, 1, 0), .125f * (j + 1));
                    const M::Quaternion last(M::Vector3(0, 1, 0), .625f + .125f * j);
                    AddChannel(a, joint, S::SkeletalAnimationPath::Rotation,
                               {first.x, first.y, first.z, first.w}, {last.x, last.y, last.z, last.w});
                    AddChannel(a, joint, S::SkeletalAnimationPath::Scale,
                               {1, 1 + .125f * j, .875f, 0}, {1.25f, .75f, 1.125f + .125f * j, 0});
                }
            }
            AClip->SetClip(std::move(a));
            BClip->SetClip(std::move(b));
            INTEGRATION_CHECK(AClip->Load() && BClip->Load());
            Asset->SetClipResources(Mesh, Skeleton, {AClip, BClip});
            INTEGRATION_CHECK(Asset->Load());
        }

        C::TSharedPtr<AnimationClipResource> ResolveClip(C::StringView name) const override
        {
            return name == C::StringView("a") ? AClip
                 : name == C::StringView("b") ? BClip : C::TSharedPtr<AnimationClipResource>{};
        }

        C::TSharedPtr<AnimGraphResource> Graph(const char* json)
        {
            auto graph = Registry.CreateTransient<AnimGraphResource>("IntegrationGraph");
            A::AnimGraphReport report;
            INTEGRATION_CHECK(graph && graph->Compile(C::String(json), *Skeleton, *this, report) && graph->Load());
            return graph;
        }
    };

    struct ReadOnlyModifier final : A::IPoseModifier
    {
        int32_t Order() const override { return 0; }
        bool Apply(A::LocalPose&, A::PoseModifierContext& context) override
        {
            INTEGRATION_CHECK(context.GetJointModelMatrices() != nullptr);
            return true;
        }
    };

    void TestSingleClipSampleAgreement()
    {
        IntegrationFixture fixture(false);
        auto graph = fixture.Graph(R"({"version":1,"nodes":[{"id":"c","type":"clip","clip":"a"}],"root":"c"})");
        const auto transform = M::MatrixUtils::CreateWorldRowVector(
            {2, -3, 1.5f}, M::Quaternion(M::Vector3(1, 0, 0), .375f), {1.25f, .75f, 1.5f});
        for (bool animatorFirst : {false, true})
        {
            World world;
            world.Initialize();
            Entity* owner = world.SpawnEntity<Entity>();
            INTEGRATION_CHECK(owner);
            Component::AnimatorComponent* animator = nullptr;
            if (animatorFirst)
                animator = world.CreateComponent<Component::AnimatorComponent>(owner);
            auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(owner);
            if (!animator)
                animator = world.CreateComponent<Component::AnimatorComponent>(owner);
            INTEGRATION_CHECK(animator && mesh);
            mesh->SetSkeletalAsset(fixture.Asset);
            if (animatorFirst)
                mesh->SetMeshNodeGlobalTransform(transform);
            INTEGRATION_CHECK(animator->SetGraph(graph));
            if (!animatorFirst)
                mesh->SetMeshNodeGlobalTransform(transform);

            // 1 秒の末尾を越えた時刻も、2 進で正確な刻みから独立して折り返す。
            constexpr float dt = .125f;
            float elapsed = 0;
            auto tickAndCompare = [&]
            {
                world.Tick(dt);
                elapsed += dt;
                const float time = elapsed - std::floor(elapsed);
                A::SkeletalPoseSnapshot expected;
                INTEGRATION_CHECK(A::SkeletalAnimationSampler::Sample(
                    *fixture.Skeleton, *fixture.AClip, *fixture.Mesh, time, transform, expected));
                INTEGRATION_CHECK(mesh->IsPoseCurrent());
                const auto& actual = mesh->GetPoseSnapshot();
                INTEGRATION_CHECK(expected.BonePalette.size() == 5 && expected.JointModelMatrices.size() == 5);
                CheckMatrices(actual.BonePalette, expected.BonePalette, "ボーンパレット");
                CheckMatrices(actual.JointModelMatrices, expected.JointModelMatrices, "関節モデル行列");
            };
            for (unsigned step = 0; step < 10; ++step)
                tickAndCompare();
            INTEGRATION_CHECK(animator->AddModifier(C::MakeShared<ReadOnlyModifier>()));
            tickAndCompare();
            world.Finalize();
        }
    }

    struct ArrayStorage
    {
        size_t Size;
        size_t Capacity;
        const void* Data;
    };

    template <typename T>
    ArrayStorage CaptureStorage(const C::VariableArray<T>& array, size_t jointCount)
    {
        INTEGRATION_CHECK(array.size() == jointCount && array.capacity() >= jointCount && array.data() != nullptr);
        return {array.size(), array.capacity(), array.data()};
    }

    template <typename T>
    void CheckStorage(const C::VariableArray<T>& array, const ArrayStorage& expected, unsigned frame, const char* name)
    {
        if (array.size() != expected.Size || array.capacity() != expected.Capacity || array.data() != expected.Data)
        {
            std::fprintf(stderr,
                         "Animator 作業配列が変化: %s コマ=%u 大きさ=%zu/%zu 容量=%zu/%zu アドレス=%p/%p\n",
                         name, frame, array.size(), expected.Size, array.capacity(), expected.Capacity,
                         static_cast<const void*>(array.data()), expected.Data);
            std::abort();
        }
    }

    void TestSteadyPoseStorage()
    {
        IntegrationFixture fixture(true);
        auto graph = fixture.Graph(
            R"({"version":1,"params":[{"name":"Go","type":"bool","value":false},{"name":"Blend","type":"float","value":0.25}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"blend2","children":["a","b"],"weight":"Blend"},{"id":"machine","type":"stateMachine","states":[{"name":"Idle","node":"mix"},{"name":"Run","node":"b"}],"transitions":[{"from":"Idle","to":"Run","duration":0.25,"conditions":[{"param":"Go","op":"eq","value":true}]},{"from":"Run","to":"Idle","duration":0.25,"conditions":[{"param":"Go","op":"eq","value":false}]}]}],"root":"machine"})");
        World world;
        world.Initialize();
        Entity* owner = world.SpawnEntity<Entity>();
        INTEGRATION_CHECK(owner);
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(owner);
        auto* animator = world.CreateComponent<Component::AnimatorComponent>(owner);
        INTEGRATION_CHECK(mesh && animator);
        mesh->SetSkeletalAsset(fixture.Asset);
        INTEGRATION_CHECK(animator->SetGraph(graph));
        INTEGRATION_CHECK(animator->AddModifier(C::MakeShared<ReadOnlyModifier>()));
        const auto go = animator->FindParam(Identity("Go"));
        const auto blend = animator->FindParam(Identity("Blend"));
        INTEGRATION_CHECK(go != A::InvalidAnimParam && blend != A::InvalidAnimParam);
        constexpr float dt = 1.f / 60.f;
        A::AnimStateStatus status;
        INTEGRATION_CHECK(animator->GetState(Identity("machine"), status));
        INTEGRATION_CHECK(status.Current == Identity("Idle") && !status.bTransitioning);

        // 双方向の遷移を完了させ、各経路の作業配列を準備する。
        for (bool run : {true, false})
        {
            INTEGRATION_CHECK(animator->SetBool(go, run));
            for (unsigned frame = 0; frame < 40; ++frame)
                world.Tick(dt);
            INTEGRATION_CHECK(animator->GetState(Identity("machine"), status));
            INTEGRATION_CHECK(status.Current == Identity(run ? "Run" : "Idle") && !status.bTransitioning);
            INTEGRATION_CHECK(mesh->IsPoseCurrent());
        }
        const size_t jointCount = fixture.Skeleton->GetPoseRuntime().Parents.size();
        INTEGRATION_CHECK(jointCount >= 3);
        const auto& scratch = animator->GetModifierScratch();
        const auto& pose = mesh->GetPoseSnapshot();
        const ArrayStorage storage[] = {
            CaptureStorage(animator->GetLocalPose(), jointCount),
            CaptureStorage(animator->GetFinalPose(), jointCount),
            CaptureStorage(scratch.Local, jointCount),
            CaptureStorage(scratch.LocalMatrices, jointCount),
            CaptureStorage(scratch.GlobalMatrices, jointCount),
            CaptureStorage(animator->GetModifierModelMatrices(), jointCount),
            CaptureStorage(pose.BonePalette, jointCount),
            CaptureStorage(pose.JointModelMatrices, jointCount),
        };
        unsigned idleToRun = 0, runToIdle = 0;
        for (unsigned frame = 0; frame < 1000; ++frame)
        {
            if (frame % 40 == 0)
                INTEGRATION_CHECK(animator->SetBool(go, (frame / 40) % 2 == 0));
            INTEGRATION_CHECK(animator->SetFloat(blend, float(frame % 101) / 100.f));
            world.Tick(dt);
            INTEGRATION_CHECK(mesh->IsPoseCurrent());
            CheckStorage(animator->GetLocalPose(), storage[0], frame, "LocalPose");
            CheckStorage(animator->GetFinalPose(), storage[1], frame, "FinalPose");
            CheckStorage(animator->GetModifierScratch().Local, storage[2], frame, "ModifierScratch.Local");
            CheckStorage(animator->GetModifierScratch().LocalMatrices, storage[3], frame, "ModifierScratch.LocalMatrices");
            CheckStorage(animator->GetModifierScratch().GlobalMatrices, storage[4], frame, "ModifierScratch.GlobalMatrices");
            CheckStorage(animator->GetModifierModelMatrices(), storage[5], frame, "ModifierModelMatrices");
            CheckStorage(mesh->GetPoseSnapshot().BonePalette, storage[6], frame, "BonePalette");
            CheckStorage(mesh->GetPoseSnapshot().JointModelMatrices, storage[7], frame, "JointModelMatrices");
            INTEGRATION_CHECK(animator->GetState(Identity("machine"), status));
            if (status.bTransitioning)
            {
                if (status.Current == Identity("Idle") && status.Next == Identity("Run"))
                    ++idleToRun;
                else if (status.Current == Identity("Run") && status.Next == Identity("Idle"))
                    ++runToIdle;
                else
                    INTEGRATION_CHECK(false);
            }
        }
        INTEGRATION_CHECK(idleToRun > 0 && runToIdle > 0);
        std::printf("Animator 結合: 1000 コマ中の遷移は %u コマ (Idle→Run %u、Run→Idle %u)\n",
                    idleToRun + runToIdle, idleToRun, runToIdle);
        world.Finalize();
    }

    float DebugFloat(const Component::AnimatorDebugSnapshot& snapshot, Identity name)
    {
        for (const auto& param : snapshot.Parameters)
        {
            if (param.Name == name)
            {
                INTEGRATION_CHECK(param.Type == A::AnimParamType::Float);
                return param.Value.Float;
            }
        }
        INTEGRATION_CHECK(false);
        return 0;
    }

    void TestParameterTransitionsAndDriveSignals()
    {
        IntegrationFixture fixture(true);
        auto graph = fixture.Graph(
            R"({"version":1,"params":[{"name":"Speed","type":"float","value":0,"bind":"speed"},{"name":"Unbound","type":"float","value":0.25}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"machine","type":"stateMachine","states":[{"name":"Idle","node":"a"},{"name":"Run","node":"b"}],"transitions":[{"from":"Idle","to":"Run","duration":0.1,"conditions":[{"param":"Speed","op":"gt","value":0.5}]},{"from":"Run","to":"Idle","duration":0.1,"conditions":[{"param":"Speed","op":"le","value":0.5}]}]}],"root":"machine"})");
        World world;
        world.Initialize();
        Entity* owner = world.SpawnEntity<Entity>();
        INTEGRATION_CHECK(owner);
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(owner);
        auto* animator = world.CreateComponent<Component::AnimatorComponent>(owner);
        INTEGRATION_CHECK(mesh && animator);
        mesh->SetSkeletalAsset(fixture.Asset);
        INTEGRATION_CHECK(animator->SetGraph(graph));
        const auto speed = animator->FindParam(Identity("Speed"));
        const auto unbound = animator->FindParam(Identity("Unbound"));
        INTEGRATION_CHECK(speed != A::InvalidAnimParam && unbound != A::InvalidAnimParam);
        INTEGRATION_CHECK(animator->SetFloat(unbound, .375f));
        constexpr float dt = 1.f / 60.f;
        constexpr float duration = .1f;
        // 遷移開始の 1 コマと切り上げた遷移時間に、浮動小数点の丸め余裕を 1 コマ加える。
        const unsigned maxFrames = 1u + static_cast<unsigned>(std::ceil(duration / dt)) + 1u;
        A::AnimStateStatus status;
        INTEGRATION_CHECK(animator->GetState(Identity("machine"), status));
        INTEGRATION_CHECK(status.Current == Identity("Idle") && !status.bTransitioning);
        auto reachState = [&](Identity target)
        {
            bool reached = false;
            unsigned frames = 0;
            for (; frames < maxFrames;)
            {
                world.Tick(dt);
                ++frames;
                INTEGRATION_CHECK(mesh->IsPoseCurrent());
                INTEGRATION_CHECK(animator->GetState(Identity("machine"), status));
                if (status.Current == target && !status.bTransitioning)
                {
                    reached = true;
                    break;
                }
            }
            INTEGRATION_CHECK(reached);
            return frames;
        };
        INTEGRATION_CHECK(animator->SetFloat(speed, 1.f));
        const unsigned forwardFrames = reachState(Identity("Run"));
        INTEGRATION_CHECK(animator->SetFloat(speed, .5f));
        const unsigned reverseFrames = reachState(Identity("Idle"));

        Component::AnimatorDebugSnapshot snapshot;
        animator->BuildDebugSnapshot(snapshot);
        INTEGRATION_CHECK(DebugFloat(snapshot, Identity("Speed")) == .5f);
        INTEGRATION_CHECK(DebugFloat(snapshot, Identity("Unbound")) == .375f);
        A::AnimDriveSignals signals;
        signals.Speed = .75f;
        INTEGRATION_CHECK(animator->SetDriveSignals(signals));
        animator->BuildDebugSnapshot(snapshot);
        INTEGRATION_CHECK(DebugFloat(snapshot, Identity("Speed")) == .75f);
        INTEGRATION_CHECK(DebugFloat(snapshot, Identity("Unbound")) == .375f);
        world.Tick(dt);
        INTEGRATION_CHECK(mesh->IsPoseCurrent());
        INTEGRATION_CHECK(animator->GetState(Identity("machine"), status));
        INTEGRATION_CHECK(status.bTransitioning && status.Current == Identity("Idle") && status.Next == Identity("Run"));
        std::printf("Animator 結合: SetFloat の遷移は上限 %u コマ中 Idle→Run %u、Run→Idle %u\n",
                    maxFrames, forwardFrames, reverseFrames);
        world.Finalize();
    }

    void TestPartialLayeredMask()
    {
        IntegrationFixture fixture(true);
        for (bool additive : {false, true})
        {
            char json[768];
            std::snprintf(json, sizeof(json),
                R"({"version":1,"masks":[{"name":"Upper","root":"Spine","fadeDepth":2}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"layered","base":"a","layers":[{"node":"b","mask":"Upper","weight":0.5,"mode":"%s"}]}],"root":"mix"})",
                additive ? "additive" : "override");
            auto graph = fixture.Graph(json);
            A::AnimGraphInstance instance;
            INTEGRATION_CHECK(instance.Initialize(*graph, *fixture.Skeleton, *fixture.Mesh, M::Matrix4x4::Identity));
            constexpr float time = .375f;
            INTEGRATION_CHECK(instance.Update(time) && instance.Evaluate());
            const auto& pose = instance.GetLocalPose();
            INTEGRATION_CHECK(pose.size() == 4);
            bool hasZero = false, hasPartial = false, hasOne = false;
            float expected[4][3];
            float scale = 1;
            for (unsigned joint = 0; joint < 4; ++joint)
            {
                // 鎖の Spine が根（深さ 0）。外の Root は 0、枝は min(1, (d+1)/fadeDepth)。
                const int depth = int(joint) - 1;
                const float mask = depth < 0 ? 0.f : std::fmin(1.f, float(depth + 1) / 2.f);
                hasZero |= mask == 0;
                hasPartial |= mask > 0 && mask < 1;
                hasOne |= mask == 1;
                const float j = float(joint);
                const float base[] = {1 + j + 2 * time, -2 - j + 4 * time, .5f * j + 2 * time};
                const float first[] = {10 + 2 * j, 4 + j, -3 - j};
                const float layer[] = {first[0] + 6 * time, first[1] - 2 * time, first[2] + 4 * time};
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    expected[joint][axis] = base[axis] + .5f * mask * (layer[axis] - (additive ? first[axis] : base[axis]));
                    scale = std::fmax(scale, std::fabs(expected[joint][axis]));
                }
            }
            INTEGRATION_CHECK(hasZero && hasPartial && hasOne);
            for (unsigned joint = 0; joint < 4; ++joint)
            {
                const float actual[] = {pose[joint].Translation.x, pose[joint].Translation.y, pose[joint].Translation.z};
                for (unsigned axis = 0; axis < 3; ++axis)
                    CheckValue(actual[axis], expected[joint][axis], scale,
                               additive ? "部分マスクの加算" : "部分マスクの上書き", joint, axis);
            }
        }
    }
} // namespace

void TestAnimatorIntegration()
{
    TestSingleClipSampleAgreement();
    TestSteadyPoseStorage();
    TestParameterTransitionsAndDriveSignals();
    TestPartialLayeredMask();
}
