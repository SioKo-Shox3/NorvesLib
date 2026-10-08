// GR10のJSON拒否、ブレンド、遷移、外部駆動を既存CPU bundleで検証する。
#include "Animation/AnimGraphInstance.h"
#include "Animation/MotionAnalysis.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/HoldSlotComponent.h"
#include "Component/ScriptComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/SocketAttachmentComponent.h"
#include "Engine/NorvesEngine.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace
{
    using namespace NorvesLib::Core;
    namespace A = NorvesLib::Core::Animation;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Skeletal;
    namespace M = NorvesLib::Math;
#define GRAPH_CHECK(x)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "AnimGraph %s:%d %s\n", __FILE__, __LINE__, #x);                                      \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
    void NearGraph(float a, float b, const std::source_location& where=std::source_location::current())
    {
        if(!(std::fabs(a-b)<1e-4f))
        {
            std::fprintf(stderr,"AnimGraph near %s:%u actual=%.9g expected=%.9g\n",where.file_name(),where.line(),double(a),double(b));
            std::abort();
        }
    }
    struct GraphFixture : A::IClipResolver
    {
        ResourceRegistry Registry;
        C::TSharedPtr<SkeletonResource> Skeleton;
        C::TSharedPtr<SkinnedMeshResource> Mesh;
        C::TSharedPtr<AnimationClipResource> AClip, BClip;
        C::TSharedPtr<SkeletalAssetResource> Asset;
        explicit GraphFixture(uint32_t jointCount = 2)
        {
            GRAPH_CHECK(Registry.Initialize());
            Skeleton = Registry.CreateTransient<SkeletonResource>("GraphSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("GraphMesh");
            AClip = Registry.CreateTransient<AnimationClipResource>("GraphA");
            BClip = Registry.CreateTransient<AnimationClipResource>("GraphB");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("GraphAsset");
            GRAPH_CHECK(Skeleton && Mesh && AClip && BClip && Asset);
            C::VariableArray<S::SkeletalJoint> joints(jointCount);
            joints[0].Name = "Root";
            joints[1].Name = "Child";
            joints[1].ParentIndex = 0;
            for (uint32_t i = 2; i < jointCount; ++i)
            {
                char name[32];
                std::snprintf(name, sizeof(name), "Joint%u", i);
                joints[i].Name = name;
                joints[i].ParentIndex = int32_t(i - 1);
            }

            for (auto& joint : joints)
            {
                joint.InverseBindMatrix.fill(0);
                for (unsigned i = 0; i < 4; ++i)
                {
                    joint.InverseBindMatrix[i * 5] = 1;
                }
            }
            Skeleton->SetJoints(std::move(joints));
            GRAPH_CHECK(Skeleton->Load());
            C::VariableArray<S::SkeletalVertex> vertices(1);
            vertices[0].Normal = {0, 1, 0};
            vertices[0].JointIndices[0] = 1;
            vertices[0].JointWeights[0] = 1;
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(C::VariableArray<uint32_t>{0, 0, 0});
            GRAPH_CHECK(Mesh->Load());
            auto seed = [](AnimationClipResource& resource, const char* name, float x)
            {
                S::SkeletalAnimationClip clip;
                clip.Name = name;
                clip.DurationSeconds = 1;
                S::SkeletalAnimationChannel channel;
                channel.JointIndex = 0;
                channel.Path = S::SkeletalAnimationPath::Translation;
                channel.Samples.push_back({0, {x, 0, 0, 0}});
                channel.Samples.push_back({1, {x + 2, 0, 0, 0}});
                clip.Channels.push_back(std::move(channel));
                resource.SetClip(std::move(clip));
                GRAPH_CHECK(resource.Load());
            };
            seed(*AClip, "a", 0);
            seed(*BClip, "b", 10);
            Asset->SetClipResources(Mesh, Skeleton, {AClip, BClip});
            GRAPH_CHECK(Asset->Load());
        }
        C::TSharedPtr<AnimationClipResource> ResolveClip(C::StringView name) const override
        {
            return name == C::StringView("a")   ? AClip
                   : name == C::StringView("b") ? BClip
                                                : C::TSharedPtr<AnimationClipResource>{};
        }
        C::TSharedPtr<AnimGraphResource> Graph(const char* json)
        {
            auto graph = Registry.CreateTransient<AnimGraphResource>("Graph");
            A::AnimGraphReport report;
            GRAPH_CHECK(graph && graph->Compile(C::String(json), *Skeleton, *this, report) && graph->Load());
            return graph;
        }
        void Reject(const char* json, A::AnimGraphError error)
        {
            C::TSharedPtr<const A::AnimGraphData> data;
            A::AnimGraphReport report;
            GRAPH_CHECK(!A::CompileAnimGraph(C::String(json), *Skeleton, *this, data, report));
            GRAPH_CHECK(!data && report.Error == error);
        }
    };
    constexpr const char* BlendJson =
        R"({"version":1,"params":[{"name":"Speed","type":"float","value":0.5,"bind":"speed"}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"blend2","children":["a","b"],"weight":"Speed"}],"root":"mix"})";
    void TestGraphBlendAndLoad()
    {
        GraphFixture f;
        auto graph = f.Graph(BlendJson);
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        const auto* data = instance.GetLocalPose().data();
        const size_t capacity = instance.GetLocalPose().capacity();
        NearGraph(instance.GetLocalPose()[0].Translation.x, 5);
        GRAPH_CHECK(instance.Update(0.25f));
        NearGraph(instance.GetLocalPose()[0].Translation.x, 5);
        GRAPH_CHECK(instance.Evaluate());
        NearGraph(instance.GetLocalPose()[0].Translation.x, 5.5f);
        A::AnimDriveSignals signals;
        signals.Speed = 1;
        GRAPH_CHECK(instance.Parameters().ApplyDriveSignals(signals));
        GRAPH_CHECK(instance.Update(0));
        GRAPH_CHECK(instance.Evaluate());
        NearGraph(instance.GetLocalPose()[0].Translation.x, 10.5f);
        for (unsigned i = 0; i < 1000; ++i)
        {
            GRAPH_CHECK(instance.Update(1.f / 60) && instance.Evaluate());
            GRAPH_CHECK(instance.GetLocalPose().data() == data && instance.GetLocalPose().capacity() == capacity);
        }
        f.Reject(R"({"version":2})", A::AnimGraphError::UnsupportedVersion);
        f.Reject(R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"missing"}],"root":"a"})",
                 A::AnimGraphError::UnknownClip);
        f.Reject(R"({"version":1,"nodes":[{"id":"a","type":"blend2","children":["a","a"]}],"root":"a"})",
                 A::AnimGraphError::Cycle);
        f.Reject(
            R"({"version":1,"masks":[{"name":"Head","root":"Missing"}],"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})",
            A::AnimGraphError::UnknownJoint);
        f.Reject(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"mix","type":"blend2","children":["a","a"],"weight":"Missing"}],"root":"mix"})",
            A::AnimGraphError::UnknownParameter);
        f.Reject(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"m","type":"stateMachine","states":[{"name":"Idle","node":"a"}],"transitions":[{"from":"Idle","to":"Missing"}]}],"root":"m"})",
            A::AnimGraphError::InvalidTransition);
    }
    void TestGraphNodeKinds()
    {
        GraphFixture f;
        const char* graphs[] = {
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"blend1d","x":0.25,"samples":[{"position":0,"node":"a"},{"position":1,"node":"b"}]}],"root":"mix"})",
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"blend2d","x":0.25,"y":0.75,"axisX":[0,1],"axisY":[0,1],"children":["a","b","a","b"]}],"root":"mix"})",
            R"({"version":1,"masks":[{"name":"Body","root":"Root"}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"layered","base":"a","layers":[{"node":"b","mask":"Body","weight":0.25,"mode":"override"}]}],"root":"mix"})"};
        for (const auto* json : graphs)
        {
            auto graph = f.Graph(json);
            A::AnimGraphInstance instance;
            GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
            GRAPH_CHECK(instance.Update(.25f) && instance.Evaluate());
            NearGraph(instance.GetLocalPose()[0].Translation.x, 3);
        }
        auto additive = f.Graph(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"layered","base":"a","layers":[{"node":"b","mode":"additive"}]}],"root":"mix"})");
        A::AnimGraphInstance add;
        GRAPH_CHECK(add.Initialize(*additive, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(add.Update(.25f) && add.Evaluate());
        NearGraph(add.GetLocalPose()[0].Translation.x, 1);
        auto selected = f.Graph(
            R"({"version":1,"params":[{"name":"Choice","type":"int","value":1}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"select","param":"Choice","children":["a","b"]}],"root":"mix"})");
        A::AnimGraphInstance select;
        GRAPH_CHECK(select.Initialize(*selected, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(select.Update(.25f) && select.Evaluate());
        NearGraph(select.GetLocalPose()[0].Translation.x, 10.5f);
    }
    void TestGraphStateMachine()
    {
        GraphFixture f;
        auto graph = f.Graph(
            R"({"version":1,"params":[{"name":"Go","type":"trigger"}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"machine","type":"stateMachine","states":[{"name":"Idle","node":"a"},{"name":"Run","node":"b"}],"transitions":[{"from":"*","to":"Run","duration":0.5,"interrupt":"either","conditions":[{"param":"Go","op":"eq","value":true}]}]}],"root":"machine"})");
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Parameters().SetTrigger(0));
        GRAPH_CHECK(instance.Update(.25f) && instance.Evaluate());
        NearGraph(instance.GetLocalPose()[0].Translation.x, 5.5f);
        GRAPH_CHECK(!instance.Parameters().Get(0)->Bool);
        A::AnimStateStatus status;
        GRAPH_CHECK(instance.GetState(Identity("machine"), status) && status.bTransitioning);
        GRAPH_CHECK(instance.RequestState(Identity("machine"), Identity("Idle"), .5f));
        GRAPH_CHECK(instance.Evaluate());
        NearGraph(instance.GetLocalPose()[0].Translation.x, 5.5f);
        GRAPH_CHECK(instance.Update(.5f) && instance.Evaluate());
        GRAPH_CHECK(instance.GetState(Identity("machine"), status));
        GRAPH_CHECK(!status.bTransitioning && status.Current == Identity("Idle"));
    }
    // 条件の境界と補間曲線は、グラフ内部の計算式を呼ばず独立した数値で固定する。
    void TestGraphAcceptanceCases()
    {
        GraphFixture f;
        for (bool tie : {false, true})
        {
            char json[1024];
            std::snprintf(
                json, sizeof(json),
                R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"m","type":"stateMachine","states":[{"name":"Idle","node":"a"},{"name":"Low","node":"a"},{"name":"High","node":"b"}],"transitions":[{"from":"Idle","to":"Low","priority":%d,"duration":0},{"from":"Idle","to":"High","priority":9,"duration":0}]}],"root":"m"})",
                tie ? 9 : 1);
            auto graph = f.Graph(json);
            A::AnimGraphInstance instance;
            GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
            GRAPH_CHECK(instance.Update(0) && instance.Evaluate());
            A::AnimStateStatus status;
            GRAPH_CHECK(instance.GetState(Identity("m"), status));
            GRAPH_CHECK(status.Current == Identity(tie ? "Low" : "High"));
            NearGraph(instance.GetLocalPose()[0].Translation.x, tie ? 0.f : 10.f);
        }
        auto exitGraph = f.Graph(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"m","type":"stateMachine","states":[{"name":"Idle","node":"a"},{"name":"Run","node":"b"}],"transitions":[{"from":"Idle","to":"Run","duration":0,"exitTime":0.5}]}],"root":"m"})");
        A::AnimGraphInstance exit;
        GRAPH_CHECK(exit.Initialize(*exitGraph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(exit.Update(.5f) && exit.Evaluate());
        A::AnimStateStatus status;
        GRAPH_CHECK(exit.GetState(Identity("m"), status) && status.Current == Identity("Idle"));
        NearGraph(float(status.NormalizedTime), .5f);
        NearGraph(exit.GetLocalPose()[0].Translation.x, 1);
        GRAPH_CHECK(exit.Update(0) && exit.Evaluate());
        GRAPH_CHECK(exit.GetState(Identity("m"), status) && status.Current == Identity("Run"));
        NearGraph(exit.GetLocalPose()[0].Translation.x, 10);

        auto smooth = f.Graph(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"m","type":"stateMachine","states":[{"name":"Idle","node":"a"},{"name":"Run","node":"b"}],"transitions":[{"from":"Idle","to":"Run","duration":1,"curve":"smoothstep"}]}],"root":"m"})");
        A::AnimGraphInstance curve;
        GRAPH_CHECK(curve.Initialize(*smooth, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(curve.Update(.25f) && curve.Evaluate());
        GRAPH_CHECK(curve.GetState(Identity("m"), status));
        NearGraph(status.Transition, .15625f);
        NearGraph(curve.GetLocalPose()[0].Translation.x, 2.0625f);
        GRAPH_CHECK(curve.Update(.5f) && curve.Evaluate());
        GRAPH_CHECK(curve.GetState(Identity("m"), status));
        NearGraph(status.Transition, .84375f);
        NearGraph(curve.GetLocalPose()[0].Translation.x, 9.9375f);

        auto linear = f.Graph(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"m","type":"stateMachine","states":[{"name":"Idle","node":"a"},{"name":"Run","node":"b"}],"transitions":[{"from":"Idle","to":"Run","duration":1}]}],"root":"m"})");
        A::AnimGraphInstance whole, split, replay;
        for (auto* instance : {&whole, &split, &replay})
            GRAPH_CHECK(instance->Initialize(*linear, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(whole.Update(.5f) && whole.Evaluate());
        for (auto* instance : {&split, &replay})
            for (unsigned step = 0; step < 2; ++step)
                GRAPH_CHECK(instance->Update(.25f) && instance->Evaluate());
        A::PoseScratch scratch[3];
        A::SkeletalPoseSnapshot poses[3];
        A::AnimGraphInstance* instances[] = {&whole, &split, &replay};
        uint64_t hashes[3] = {};
        for (unsigned i = 0; i < 3; ++i)
        {
            GRAPH_CHECK(instances[i]->GetState(Identity("m"), status) && status.bTransitioning);
            GRAPH_CHECK(status.Current == Identity("Idle") && status.Next == Identity("Run"));
            NearGraph(status.Transition, .5f);
            GRAPH_CHECK(instances[i]->BuildPose(instances[i]->GetLocalPose(), scratch[i], poses[i]));
            NearGraph(poses[i].BonePalette[0].m30, 6);
            hashes[i] = 14695981039346656037ull;
            for (const auto& matrix : poses[i].BonePalette)
                for (float value : matrix.values)
                {
                    const uint32_t bits = std::bit_cast<uint32_t>(value);
                    for (unsigned byte = 0; byte < 4; ++byte)
                    {
                        hashes[i] ^= (bits >> (8 * byte)) & 255;
                        hashes[i] *= 1099511628211ull;
                    }
                }
        }
        GRAPH_CHECK(hashes[1] == hashes[2]);
        GRAPH_CHECK(poses[0].BonePalette.size() == poses[1].BonePalette.size());
        for (size_t joint = 0; joint < poses[0].BonePalette.size(); ++joint)
            for (unsigned element = 0; element < 16; ++element)
                NearGraph(poses[0].BonePalette[joint].values[element], poses[1].BonePalette[joint].values[element]);

        const char* modes[] = {"none", "current", "next", "either"};
        const char* expectedNext[] = {"B", "C", "D", "D"};
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            char json[1536];
            std::snprintf(
                json, sizeof(json),
                R"({"version":1,"params":[{"name":"Go","type":"bool","value":true},{"name":"Cut","type":"bool","value":false}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"m","type":"stateMachine","states":[{"name":"A","node":"a"},{"name":"B","node":"b"},{"name":"C","node":"a"},{"name":"D","node":"b"}],"transitions":[{"from":"A","to":"B","priority":30,"duration":1,"interrupt":"%s","conditions":[{"param":"Go","op":"eq","value":true}]},{"from":"A","to":"C","priority":10,"duration":1,"conditions":[{"param":"Cut","op":"eq","value":true}]},{"from":"B","to":"D","priority":20,"duration":1,"conditions":[{"param":"Cut","op":"eq","value":true}]}]}],"root":"m"})",
                modes[mode]);
            auto graph = f.Graph(json);
            A::AnimGraphInstance instance;
            GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
            GRAPH_CHECK(instance.Update(.25f) && instance.Evaluate());
            NearGraph(instance.GetLocalPose()[0].Translation.x, 3);
            GRAPH_CHECK(instance.Parameters().SetBool(0, false));
            GRAPH_CHECK(instance.Parameters().SetBool(1, true));
            GRAPH_CHECK(instance.Update(0) && instance.Evaluate());
            GRAPH_CHECK(instance.GetState(Identity("m"), status) && status.bTransitioning);
            GRAPH_CHECK(status.Next == Identity(expectedNext[mode]));
            NearGraph(instance.GetLocalPose()[0].Translation.x, 3);
        }
        float fireTimes[2] = {};
        const float steps[] = {1.f / 60, 1.f / 30};
        for (unsigned rate = 0; rate < 2; ++rate)
        {
            A::AnimGraphInstance timed;
            GRAPH_CHECK(timed.Initialize(*exitGraph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
            for (unsigned frame = 1; frame <= 60; ++frame)
            {
                GRAPH_CHECK(timed.Update(steps[rate]) && timed.GetState(Identity("m"), status));
                if (status.Current == Identity("Run"))
                {
                    fireTimes[rate] = steps[rate] * frame;
                    break;
                }
            }
            GRAPH_CHECK(fireTimes[rate] >= .5f && fireTimes[rate] <= .5f + steps[rate] + 1e-5f);
        }
        GRAPH_CHECK(std::fabs(fireTimes[0] - fireTimes[1]) <= 1.f / 30 + 1e-5f);

        auto grid = f.Graph(
            R"({"version":1,"params":[{"name":"X","type":"float"},{"name":"Y","type":"float"}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"blend2d","x":"X","y":"Y","axisX":[0,1],"axisY":[0,1],"children":["a","b","b","a"]}],"root":"mix"})");
        A::AnimGraphInstance gridInstance;
        GRAPH_CHECK(gridInstance.Initialize(*grid, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        const float samples[][3] = {{-2, 2, 10}, {.25f, -3, 2.5f}, {.25f, 3, 7.5f}};
        for (const auto& sample : samples)
        {
            GRAPH_CHECK(gridInstance.Parameters().SetFloat(0, sample[0]));
            GRAPH_CHECK(gridInstance.Parameters().SetFloat(1, sample[1]));
            GRAPH_CHECK(gridInstance.Update(0) && gridInstance.Evaluate());
            NearGraph(gridInstance.GetLocalPose()[0].Translation.x, sample[2]);
        }
    }
    struct OffsetModifier final : A::IPoseModifier
    {
        int32_t Index;
        C::VariableArray<int32_t>* Calls;
        OffsetModifier(int32_t index, C::VariableArray<int32_t>& calls) : Index(index), Calls(&calls)
        {
        }
        int32_t Order() const override
        {
            return Index;
        }
        bool Apply(A::LocalPose& pose, A::PoseModifierContext& context) override
        {
            Calls->push_back(Index);
            if (!context.GetJointModelMatrices())
            {
                return false;
            }
            pose[0].Translation.y += 1;
            return true;
        }
    };
    void TestAnimatorConnection()
    {
        GraphFixture f;
        auto graph = f.Graph(BlendJson);
        for (bool animatorFirst : {false, true})
        {
            World world;
            world.Initialize();
            Entity* owner = world.SpawnEntity<Entity>();
            GRAPH_CHECK(owner);
            Component::AnimatorComponent* animator = nullptr;
            if (animatorFirst)
            {
                animator = world.CreateComponent<Component::AnimatorComponent>(owner);
            }
            auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(owner);
            if (!animator)
            {
                animator = world.CreateComponent<Component::AnimatorComponent>(owner);
            }
            GRAPH_CHECK(animator && mesh);
            mesh->SetSkeletalAsset(f.Asset);
            GRAPH_CHECK(animator->SetGraph(graph));
            const float time = mesh->GetAnimationTimeSeconds();
            world.Tick(.1f);
            GRAPH_CHECK(mesh->GetPoseSerial() == 1);
            GRAPH_CHECK(mesh->GetAnimationTimeSeconds() == time && mesh->IsExternalAnimationDriven());
            M::Matrix4x4 joint;
            GRAPH_CHECK(mesh->TryGetJointModelMatrix(0, joint));
            NearGraph(joint.m30, 5.2f);
            C::VariableArray<int32_t> calls;
            calls.reserve(2);
            GRAPH_CHECK(animator->AddModifier(C::MakeShared<OffsetModifier>(2, calls)));
            GRAPH_CHECK(animator->AddModifier(C::MakeShared<OffsetModifier>(1, calls)));
            world.Tick(.1f);
            GRAPH_CHECK(calls.size() == 2 && calls[0] == 1 && calls[1] == 2);
            GRAPH_CHECK(mesh->TryGetJointModelMatrix(0, joint));
            NearGraph(joint.m31, 2);
            GRAPH_CHECK(animator->SetGraph({}));
            GRAPH_CHECK(!mesh->IsExternalAnimationDriven());
            world.Finalize();
        }
    }

    void TestAnimatorScriptParameter()
    {
        GraphFixture f;
        auto graph = f.Graph(BlendJson);
        World world;
        world.Initialize();
        GRAPH_CHECK(GEngine.GetScriptRuntime().Initialize(world) == EScriptRuntimeResult::Success);
        Entity* owner = world.SpawnEntity<Entity>();
        GRAPH_CHECK(owner);
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(owner);
        auto* animator = world.CreateComponent<Component::AnimatorComponent>(owner);
        GRAPH_CHECK(mesh && animator);
        mesh->SetSkeletalAsset(f.Asset);
        GRAPH_CHECK(animator->SetGraph(graph));
        auto* script = new Component::ScriptComponent();
        script->getScriptPath() = C::String("Scripts/Test/AnimatorParameterSetter.as");
        script->getScriptClassName() = C::String("AnimatorParameterSetter");
        GRAPH_CHECK(owner->AddComponent(script));
        world.Tick(.01f);
        Component::AnimatorDebugSnapshot snapshot;
        animator->BuildDebugSnapshot(snapshot);
        GRAPH_CHECK(snapshot.Parameters.size() == 1);
        NearGraph(snapshot.Parameters[0].Value.Float, .75f);
        world.Finalize();
        GRAPH_CHECK(GEngine.GetScriptRuntime().Shutdown() == EScriptRuntimeResult::Success);
    }

    void TestGraphPlaybackRegressions()
    {
        GraphFixture f;
        auto reverse = f.Graph(
            R"({"version":1,"nodes":[{"id":"reverse","type":"clip","clip":"a","rate":-1,"loop":false}],"root":"reverse"})");
        A::AnimGraphInstance backwards;
        GRAPH_CHECK(backwards.Initialize(*reverse, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        NearGraph(backwards.GetLocalPose()[0].Translation.x, 2);
        GRAPH_CHECK(backwards.Update(.25f) && backwards.Evaluate());
        NearGraph(backwards.GetLocalPose()[0].Translation.x, 1.5f);
        auto shared = f.Graph(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"m","type":"stateMachine","states":[{"name":"Idle","node":"a"},{"name":"Run","node":"a"}]}],"root":"m"})");
        A::AnimGraphInstance same;
        GRAPH_CHECK(same.Initialize(*shared, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(same.Update(.25f) && same.Evaluate());
        NearGraph(same.GetLocalPose()[0].Translation.x, .5f);
        GRAPH_CHECK(same.RequestState(Identity("m"), Identity("Run"), .5f) && same.Evaluate());
        NearGraph(same.GetLocalPose()[0].Translation.x, .5f);
        auto hidden = f.Graph(
            R"({"version":1,"params":[{"name":"Select","type":"int"}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"other","type":"clip","clip":"a"},{"id":"m","type":"stateMachine","states":[{"name":"Idle","node":"a"},{"name":"Run","node":"b"}]},{"id":"root","type":"select","param":"Select","children":["m","other"]}],"root":"root"})");
        A::AnimGraphInstance offscreen;
        GRAPH_CHECK(offscreen.Initialize(*hidden, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(offscreen.RequestState(Identity("m"), Identity("Run"), .5f));
        GRAPH_CHECK(offscreen.Update(.25f));
        GRAPH_CHECK(offscreen.Parameters().SetInt(0, 1) && offscreen.Update(0));
        GRAPH_CHECK(offscreen.RequestState(Identity("m"), Identity("Idle"), .5f));
        GRAPH_CHECK(offscreen.Parameters().SetInt(0, 0) && offscreen.Update(0) && offscreen.Evaluate());
        NearGraph(offscreen.GetLocalPose()[0].Translation.x, 5.5f);
        auto duplicate = f.Skeleton->GetJoints();
        duplicate[1].Name = duplicate[0].Name;
        f.Skeleton->SetJoints(std::move(duplicate));
        auto graph = f.Graph(BlendJson);
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        auto changed = f.AClip->GetClip();
        changed.Channels[0].Samples[0].Value.X = 99;
        f.AClip->SetClip(std::move(changed));
        M::Matrix4x4 moved = M::Matrix4x4::Identity;
        moved.m30 = 1;
        GRAPH_CHECK(!instance.SetMeshTransform(moved));
    }

    void TestDynamicAdditiveReference()
    {
        GraphFixture f;
        auto graph = f.Graph(
            R"({"version":1,"params":[{"name":"Choice","type":"int"}],"nodes":[{"id":"base","type":"clip","clip":"a"},{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"choice","type":"select","param":"Choice","children":["a","b"]},{"id":"layer","type":"layered","base":"base","layers":[{"node":"choice","mode":"additive"}]}],"root":"layer"})");
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Parameters().SetInt(0, 1) && instance.Update(0) && instance.Evaluate());
        NearGraph(instance.GetLocalPose()[0].Translation.x, 0);
        GRAPH_CHECK(instance.Update(.25f) && instance.Evaluate());
        NearGraph(instance.GetLocalPose()[0].Translation.x, 1);
    }

    void TestClipMetadataRuntime()
    {
        GraphFixture f;
        A::ClipMetadataReport report;
        A::ClipMetadata base, overlay;
        GRAPH_CHECK(A::ParseClipMetadata(
            C::String(
                R"({"events":[{"name":"step","phase":0.25},{"name":"attack","t":0.4,"end":0.8}],"markers":[{"name":"left","t":0.1},{"name":"right","t":0.6}],"loop":{"start":0.1,"end":0.9},"rootMotion":{"mode":"extract","x":false}})"),
            1, true, base, report));
        GRAPH_CHECK(base.Events.size() == 2 && base.Markers.size() == 2);
        NearGraph(base.Events[0].Time, .25f);
        GRAPH_CHECK(A::ApplyClipMetadataOverride(base, C::String(R"({"events":[],"rootMotion":{"yaw":false}})"), 1,
                                                 overlay, report));
        GRAPH_CHECK(overlay.Events.empty() && overlay.Markers.size() == 2 && !overlay.Root.bX && !overlay.Root.bYaw &&
                    overlay.Root.Mode == A::RootMotionMode::Extract);
        GRAPH_CHECK(!A::ParseClipMetadata(C::String(R"({"events":[{"name":"bad","t":0.5,"phase":0.5}]})"), 1, true,
                                          overlay, report));
        GRAPH_CHECK(!A::ParseClipMetadata(C::String(R"({"markers":[{"name":"same","t":0.2},{"name":"same","t":0.4}]})"),
                                          1, true, overlay, report));
        GRAPH_CHECK(A::RemapClipMetadataTime(base, 1, .3, .7, 2, overlay, report));
        GRAPH_CHECK(overlay.Events.size() == 1 && overlay.Markers.size() == 1);
        NearGraph(overlay.Events[0].Time, .2f);
        NearGraph(overlay.Events[0].EndTime, .8f);
        NearGraph(overlay.Markers[0].Time, .6f);
        A::ClipMetadata window;
        window.Events.push_back({Identity("window"), 0, 1});
        window.Root.NominalSpeed = 4;
        GRAPH_CHECK(A::RemapClipMetadataTime(window, 1, .7, .9, 2, overlay, report));
        NearGraph(overlay.Events[0].Time, 0);
        NearGraph(overlay.Events[0].EndTime, .4f);
        NearGraph(overlay.Root.NominalSpeed, 2);
        for (const char* bad :
             {R"({"events":[{"name":"x","phase":1.00000001}]})", R"({"events":[{"name":"x","t":1.00000001}]})",
              R"({"events":[{"name":"x","t":0,"minWeight":1.00000001}]})",
              R"({"rootMotion":{"nominalSpeed":-1.00000001}})", R"({"loop":{"start":0,"end":1.00000001}})"})
            GRAPH_CHECK(!A::ParseClipMetadata(C::String(bad), 1, true, overlay, report));
        const auto poseRevision = f.AClip->GetPoseRevision();
        const auto metadataRevision = f.AClip->GetMetadataRevision();
        GRAPH_CHECK(f.AClip->ApplyMetadataJson(C::String(R"({"events":[{"name":"step","t":0.5}]})"), report));
        GRAPH_CHECK(f.AClip->GetPoseRevision() == poseRevision && f.AClip->GetMetadataRevision() != metadataRevision &&
                    f.AClip->GetClip().Metadata.Events.empty());
        GRAPH_CHECK(f.AClip->ApplyMetadataJson(C::String(R"({"groundOffset":0.2})"), report));
        GRAPH_CHECK(f.AClip->GetClipMetadata().Events.empty());
    }
    void TestAnimationEventRuntime()
    {
        GraphFixture f;
        A::ClipMetadataReport report;
        auto apply = [&](AnimationClipResource& clip, const char* json) {
            GRAPH_CHECK(clip.ApplyMetadataJson(C::String(json), report));
        };
        apply(*f.AClip, R"({"events":[{"name":"attack","t":0.2,"end":0.8}]})");
        apply(*f.BClip, R"({"events":[{"name":"attack","t":0.3,"end":0.7}]})");
        A::AnimGraphData data;
        data.Clips = {f.AClip, f.BClip};
        A::AnimationEventQueue queue;
        C::VariableArray<A::AnimEventInfo> events;
        queue.OnEvent.Add([&](const A::AnimEventInfo& event) { events.push_back(event); });
        const Identity group("locomotion");
        C::VariableArray<A::AnimClipTraversal> traversals{{0, 0, 0, 1, 1, false, group}, {1, 1, 0, 1, 1, false, group}};
        queue.Update(data, traversals, 1);
        GRAPH_CHECK(events.empty());
        queue.Dispatch();
        GRAPH_CHECK(events.size() == 2 && events[0].Kind == A::AnimEventKind::Begin &&
                    events[1].Kind == A::AnimEventKind::End && events[0].WindowToken == events[1].WindowToken);
        queue.Reset();
        events.clear();
        traversals = {{0, 0, 0, .5, 1, false, {}}};
        queue.Update(data, traversals, .5f);
        traversals[0] = {0, 0, .5, .6, 0, false, {}};
        queue.Update(data, traversals, .1f);
        queue.Dispatch();
        GRAPH_CHECK(events.size() == 2 && events[0].Batch < events[1].Batch && events[1].bInterrupted);
        queue.Reset();
        events.clear();
        traversals = {{0, 0, .3, 10.3, 1, true, {}}};
        queue.Update(data, traversals, 10);
        queue.Dispatch();
        GRAPH_CHECK(events.size() == 18 && queue.ActiveWindowCount() == 0 && events.back().bInterrupted);
        queue.Reset();
        events.clear();
        apply(*f.AClip, R"({"events":[{"name":"step","t":1}]})");
        apply(*f.BClip, R"({"events":[{"name":"step","t":0}]})");
        traversals = {{0, 0, 0, 1, 1, true, group}, {1, 1, 0, 1, 1, true, group}};
        queue.Update(data, traversals, 1);
        queue.Dispatch();
        GRAPH_CHECK(events.size() == 1);
        queue.Reset();
        events.clear();
        traversals = {{0, 0, .1, -.1, 1, true, group, true}, {1, 1, .1, -.1, 1, true, group, true}};
        queue.Update(data, traversals, .2f);
        queue.Dispatch();
        GRAPH_CHECK(events.size() == 1 && events[0].Occurrence == 0);
        // move後の破棄と通常破棄で同じwindowのEndが重複しない。
        apply(*f.AClip, R"({"events":[{"name":"attack","t":0.2,"end":0.8}]})");
        events.clear();
        auto graph = f.Graph(R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})");
        {
            A::AnimGraphInstance source;
            GRAPH_CHECK(source.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
            source.Events().OnEvent.Add([&](const A::AnimEventInfo& event) { events.push_back(event); });
            GRAPH_CHECK(source.Update(.5f));
            source.Events().Dispatch();
            A::AnimGraphInstance moved(std::move(source));
            GRAPH_CHECK(events.size() == 1);
        }
        GRAPH_CHECK(events.size() == 2 && events[1].bInterrupted && events[0].WindowToken == events[1].WindowToken);
    }

    void TestRootMotionRegressions()
    {
        GraphFixture f;
        A::ClipMetadataReport report;
        const char* json = R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"b"}],"root":"a"})";
        auto graph = f.Graph(json);
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(f.BClip->ApplyMetadataJson(C::String(R"({"rootMotion":{"mode":"extract"}})"), report));
        GRAPH_CHECK(instance.Evaluate());
        NearGraph(instance.GetLocalPose()[0].Translation.x, 10);
        // 非ゼロの初期headingをEntity移動へ二重に適用しない。
        auto clip = f.BClip->GetClip();
        S::SkeletalAnimationChannel rotation;
        rotation.JointIndex = 0;
        rotation.Path = S::SkeletalAnimationPath::Rotation;
        rotation.Samples = {{0, {0, -.7071067811865475f, 0, .7071067811865475f}},
                            {1, {0, -.7071067811865475f, 0, .7071067811865475f}}};
        clip.Channels.push_back(rotation);
        clip.Metadata.Root.Mode = A::RootMotionMode::Extract;
        f.BClip->SetClip(S::SkeletalAnimationClip(clip));
        graph = f.Graph(json);
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Update(.5f) && instance.Evaluate());
        auto d = instance.ConsumeRootMotion();
        NearGraph(float(d.X), 1);
        NearGraph(float(d.Z), 0);
        NearGraph(instance.GetLocalPose()[0].Translation.x, 10);
        // 180度ちょうどの区間を二つ繋いでも一周分のyawが失われない。
        clip.Channels[0].Samples = {{0, {10, 0, 0, 0}}, {1, {10, 0, 0, 0}}};
        clip.Channels.back().Samples = {{0, {0, 0, 0, 1}}, {.5f, {0, 1, 0, 0}}, {1, {0, 0, 0, -1}}};
        f.BClip->SetClip(S::SkeletalAnimationClip(clip));
        graph = f.Graph(json);
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Update(1));
        d = instance.ConsumeRootMotion();
        NearGraph(float(d.Yaw), 6.283185307f);
        NearGraph(float(d.X), 0);
        NearGraph(float(d.Z), 0);
        // 軸ロックは軌跡へ適用し、Updateの分割で変位を変えない。
        clip.Channels.back().Samples = {{0, {0, 0, 0, 1}}};
        clip.Channels[0].Samples = {{0, {0, 0, 0, 0}}};
        clip.RootMotionJoint = 0;
        clip.RootMotion = {{0, 0, 0, 0}, {1, 1, 0, 1.5707963267948966}};
        for (unsigned mode = 0; mode < 2; ++mode)
        {
            clip.Metadata.Root.bX = mode == 0;
            clip.Metadata.Root.bYaw = mode != 0;
            f.BClip->SetClip(S::SkeletalAnimationClip(clip));
            graph = f.Graph(json);
            A::AnimGraphInstance split;
            GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity) &&
                        split.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
            GRAPH_CHECK(instance.Update(2) && split.Update(1) && split.Update(1));
            const auto a = instance.ConsumeRootMotion(), b = split.ConsumeRootMotion();
            NearGraph(float(a.X), float(b.X));
            NearGraph(float(a.Z), float(b.Z));
            NearGraph(float(a.Yaw), float(b.Yaw));
        }
    }
    void TestMotionAnalysisRuntime()
    {
        GraphFixture f;
        auto clip = f.AClip->GetClip();
        clip.Channels.clear();
        S::SkeletalAnimationChannel foot;
        foot.JointIndex = 1;
        foot.Path = S::SkeletalAnimationPath::Translation;
        foot.Interpolation = S::SkeletalAnimationInterpolation::Step;
        for (unsigned i = 0; i <= 60; ++i)
            foot.Samples.push_back({float(i) / 60, {0, i >= 12 && i < 24 ? .1f : .3f, 0, 0}});
        clip.Channels.push_back(foot);
        f.AClip->SetClip(S::SkeletalAnimationClip(clip));
        A::SkeletalPoseContext context;
        GRAPH_CHECK(A::SkeletalPoseBuilder::Prepare(*f.Skeleton, *f.AClip, *f.Mesh, M::Matrix4x4::Identity, context));
        const A::FootContactSpec spec{1, Identity("Foot.Left")};
        A::FootContactReport contact;
        GRAPH_CHECK(A::AnalyzeFootContacts(context, *f.AClip, {&spec, 1}, {}, contact));
        GRAPH_CHECK(contact.Windows.size() == 1 && std::fabs(contact.Windows[0].Start - .2f) <= 1.f / 60 + 1e-5f &&
                    std::fabs(contact.Windows[0].End - .4f) <= 1.f / 60 + 1e-5f);
        NearGraph(contact.GroundOffset, .1f);
        GRAPH_CHECK(contact.Confidence > .5f);
        A::ClipMetadata parsed;
        A::ClipMetadataReport metadataReport;
        GRAPH_CHECK(A::ParseClipMetadata(contact.DraftJson, 1, true, parsed, metadataReport) &&
                    A::SameClipMetadata(parsed, contact.Draft));
        clip.Channels.clear();
        clip.DurationSeconds = 3;
        S::SkeletalAnimationChannel rotation;
        rotation.JointIndex = 1;
        rotation.Path = S::SkeletalAnimationPath::Rotation;
        for (unsigned i = 0; i <= 180; ++i)
        {
            const double time = double(i) / 60, angle = .6 * std::sin(6.283185307179586 * time / .75);
            rotation.Samples.push_back({float(time), {0, 0, float(std::sin(angle * .5)), float(std::cos(angle * .5))}});
        }
        clip.Channels.push_back(rotation);
        f.AClip->SetClip(S::SkeletalAnimationClip(clip));
        GRAPH_CHECK(A::SkeletalPoseBuilder::Prepare(*f.Skeleton, *f.AClip, *f.Mesh, M::Matrix4x4::Identity, context));
        A::CycleDetectionOptions options;
        options.SampleRate = 60;
        options.MinimumPeriod = .5;
        options.MaximumPeriod = 1;
        A::CycleDetectionReport cycle;
        GRAPH_CHECK(A::DetectCycle(*f.Skeleton, context, *f.AClip, options, cycle));
        GRAPH_CHECK(cycle.bDetected && std::fabs(cycle.Period - .75) < .025);
        GRAPH_CHECK(A::ParseClipMetadata(cycle.DraftJson, 3, true, parsed, metadataReport) &&
                    A::SameClipMetadata(parsed, cycle.Draft));
        GRAPH_CHECK(f.AClip->GetClip().Metadata.Events.empty() && !f.AClip->GetClip().Metadata.Loop.bEnabled);
        auto otherSkeleton = f.Registry.CreateTransient<SkeletonResource>("OtherAnalysisSkeleton");
        otherSkeleton->SetJoints(C::VariableArray<S::SkeletalJoint>(f.Skeleton->GetJoints()));
        GRAPH_CHECK(otherSkeleton->Load());
        GRAPH_CHECK(!A::DetectCycle(*otherSkeleton, context, *f.AClip, options, cycle));
        S::SkeletalAnimationClip still;
        still.Name = "Still";
        still.DurationSeconds = .3f;
        foot.Samples = {{0, {0, .1f, 0, 0}}, {.3f, {0, .1f, 0, 0}}};
        still.Channels.push_back(foot);
        f.AClip->SetClip(std::move(still));
        GRAPH_CHECK(A::SkeletalPoseBuilder::Prepare(*f.Skeleton, *f.AClip, *f.Mesh, M::Matrix4x4::Identity, context));
        GRAPH_CHECK(A::AnalyzeFootContacts(context, *f.AClip, {&spec, 1}, {}, contact));
        GRAPH_CHECK(A::ParseClipMetadata(contact.DraftJson, .3f, true, parsed, metadataReport) &&
                    A::SameClipMetadata(parsed, contact.Draft));
        C::String longName;
        for (unsigned i = 0; i < 1025; ++i)
            longName += 'x';
        const A::FootContactSpec invalidSpec{1, Identity(longName)};
        GRAPH_CHECK(!A::AnalyzeFootContacts(context, *f.AClip, {&invalidSpec, 1}, {}, contact));
    }

    void TestSyncHitStopKeepsPointEventsStopped()
    {
        GraphFixture f;
        auto a = f.AClip->GetClip(), b = f.BClip->GetClip();
        a.Metadata.Markers = {{Identity("left"), 0}, {Identity("right"), .94034773111343384f}};
        b.Metadata.Markers = {{Identity("left"), 0}, {Identity("right"), .087595060467720032f}};
        b.Metadata.Events = {{Identity("hitStopEdge"), .87809914350509644f, -1, 0}};
        a.Metadata.Root.Mode = b.Metadata.Root.Mode = A::RootMotionMode::Extract;
        a.RootMotionJoint = b.RootMotionJoint = 0;
        a.RootMotion = {{0, 0, 0, 0}, {.5f, 1234.5, 1452.3, .7}, {1, 1236.5, 1452.3, .9}};
        b.RootMotion = {{0, 0, 0, 0}, {.5f, 33, 18, .7}, {1, 35, 18, .9}};
        f.AClip->SetClip(std::move(a));
        f.BClip->SetClip(std::move(b));
        auto graph = f.Graph(
            R"({"version":1,"params":[{"name":"blend","type":"float","value":0.75}],"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"gait"},{"id":"b","type":"clip","clip":"b","syncGroup":"gait"},{"id":"mix","type":"blend2","children":["a","b"],"weight":"blend"}],"root":"mix"})");
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        unsigned count = 0;
        instance.Events().OnEvent.Add([&](const A::AnimEventInfo& event) {
            if (event.Name == Identity("hitStopEdge"))
                ++count;
        });
        GRAPH_CHECK(instance.Update(.87809914350509644f));
        instance.Events().Dispatch();
        GRAPH_CHECK(count == 1);
        (void)instance.ConsumeRootMotion();
        for (unsigned frame = 0; frame < 100; ++frame)
        {
            GRAPH_CHECK(instance.Parameters().SetFloat(0, frame % 2 ? .75f : .25f));
            GRAPH_CHECK(instance.Update(0));
            instance.Events().Dispatch();
            GRAPH_CHECK(count == 1);
            for (const auto& traversal : instance.GetTraversals())
                GRAPH_CHECK(traversal.Previous == traversal.Current);
            const auto root = instance.ConsumeRootMotion();
            GRAPH_CHECK(root.X == 0 && root.Z == 0 && root.Yaw == 0);
        }
    }

    void TestAnimationSyncRuntime()
    {
        GraphFixture f;
        auto a = f.AClip->GetClip(), b = f.BClip->GetClip();
        a.Metadata.Markers = {{Identity("left"), 0}, {Identity("right"), .4f}};
        a.Metadata.Events = {{Identity("left"), 0}, {Identity("right"), .4f}};
        b.DurationSeconds = 2;
        b.Channels[0].Samples = {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}};
        b.Metadata.Markers = {{Identity("left"), .2f}, {Identity("right"), 1.4f}};
        b.Metadata.Events = {{Identity("left"), .2f}, {Identity("right"), 1.4f}};
        f.AClip->SetClip(S::SkeletalAnimationClip(a));
        f.BClip->SetClip(S::SkeletalAnimationClip(b));
        auto graph = f.Graph(
            R"({"version":1,"params":[{"name":"blend","type":"float","value":0.5}],"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"gait"},{"id":"b","type":"clip","clip":"b","syncGroup":"gait"},{"id":"mix","type":"blend2","children":["a","b"],"weight":"blend"}],"root":"mix"})");
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.FindSyncGroup(Identity("gait")) == 0 && instance.GetSyncPhases().size() == 1);
        C::VariableArray<A::AnimEventInfo> events;
        instance.Events().OnEvent.Add([&](const auto& e) { events.push_back(e); });
        A::AnimSyncMap am, bm;
        C::VariableArray<Identity> names{Identity("left"), Identity("right")};
        GRAPH_CHECK(A::BuildAnimSyncMap(a.Metadata, 1, names, am) && A::BuildAnimSyncMap(b.Metadata, 2, names, bm));
        for (double phase : {-2.1, -1., -.25, 0., .5, .999, 1., 2.5})
        {
            GRAPH_CHECK(std::fabs(am.TimeToPhase(am.PhaseToTime(phase)) - phase) < 1e-8 &&
                        std::fabs(bm.TimeToPhase(bm.PhaseToTime(phase)) - phase) < 1e-8);
        }
        GRAPH_CHECK(instance.Update(.5f));
        instance.Events().Dispatch();
        GRAPH_CHECK(events.size() == 1 && events[0].Name == Identity("right") &&
                    std::fabs(events[0].OffsetSeconds - .4) < 1e-5);
        const auto phaseBefore = instance.GetSyncPhases()[0];
        GRAPH_CHECK(instance.Parameters().SetFloat(0, .9f) && instance.Update(0));
        NearGraph(instance.GetSyncPhases()[0], phaseBefore);
        for (unsigned i = 0; i < 60; ++i)
        {
            GRAPH_CHECK(instance.Update(1.f / 60));
            const auto& ts = instance.GetTraversals();
            GRAPH_CHECK(ts.size() == 2);
            const double ap = am.TimeToPhase(ts[0].Current), bp = bm.TimeToPhase(ts[1].Current);
            GRAPH_CHECK(std::fabs(ap - bp) < 1e-6);
        }
        instance.Events().Dispatch();
        // metadata更新でも標準位相は保持し、不整合な集合は公開前に拒否する。
        A::ClipMetadataReport report;
        GRAPH_CHECK(f.BClip->ApplyMetadataJson(C::String(R"({"markers":[]})"), report));
        const auto saved = instance.GetSyncPhases()[0];
        GRAPH_CHECK(instance.Update(0));
        NearGraph(instance.GetSyncPhases()[0], saved);
        GRAPH_CHECK(f.BClip->ApplyMetadataJson(C::String(R"({"markers":[{"name":"unknown","t":0.1}]})"), report));
        GRAPH_CHECK(!instance.Update(0) && !instance.Evaluate());
        f.Reject(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"g"},{"id":"b","type":"clip","clip":"b","syncGroup":"g"},{"id":"mix","type":"blend2","children":["a","b"]}],"root":"mix"})",
            A::AnimGraphError::MarkerMismatch);
        f.BClip->ClearRuntimeMetadata();
        // 遷移先へのseekは当該フレームのroot/event区間へ混ぜない。
        auto machine = f.Graph(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"g"},{"id":"b","type":"clip","clip":"b","syncGroup":"g"},{"id":"m","type":"stateMachine","states":[{"name":"A","node":"a"},{"name":"B","node":"b"}]}],"root":"m"})");
        GRAPH_CHECK(instance.Initialize(*machine, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Update(.3f));
        const float beforeTransition = instance.GetSyncPhases()[0];
        GRAPH_CHECK(instance.RequestState(Identity("m"), Identity("B"), 0) && instance.Update(0));
        NearGraph(instance.GetSyncPhases()[0], beforeTransition);
        GRAPH_CHECK(instance.GetTraversals().size() == 1 &&
                    instance.GetTraversals()[0].Previous == instance.GetTraversals()[0].Current);

        auto marked = f.Graph(
            R"({"version":1,"params":[{"name":"go","type":"trigger"}],"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"g"},{"id":"b","type":"clip","clip":"b","syncGroup":"g"},{"id":"m","type":"stateMachine","states":[{"name":"A","node":"a"},{"name":"B","node":"b"}],"transitions":[{"from":"A","to":"B","duration":0,"sourceMarker":"left","targetMarker":"right","conditions":[{"param":"go","op":"eq","value":true}]}]}],"root":"m"})");
        GRAPH_CHECK(instance.Initialize(*marked, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Update(.1f) && instance.Parameters().SetTrigger(0) && instance.Update(0));
        const auto& transition = instance.GetTraversals()[0];
        GRAPH_CHECK(std::fabs(bm.TimeToPhase(transition.Current) - double(instance.GetSyncPhases()[0]) - .5) < 1e-6);
        GRAPH_CHECK(transition.Previous == transition.Current);
        instance.Events().Dispatch();

        GRAPH_CHECK(instance.Initialize(*marked, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(f.BClip->ApplyMetadataJson(C::String(R"({"markers":[]})"), report));
        GRAPH_CHECK(instance.Parameters().SetTrigger(0) && !instance.Update(0));
        A::AnimStateStatus unchanged;
        GRAPH_CHECK(instance.GetState(Identity("m"), unchanged) && unchanged.Current == Identity("A") &&
                    !unchanged.bTransitioning);
        f.BClip->ClearRuntimeMetadata();
        auto chain = f.Graph(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"g"},{"id":"b","type":"clip","clip":"b","syncGroup":"g"},{"id":"c","type":"clip","clip":"a","syncGroup":"g"},{"id":"m","type":"stateMachine","states":[{"name":"A","node":"a"},{"name":"B","node":"b"},{"name":"C","node":"c"}],"transitions":[{"from":"A","to":"B","duration":0,"sourceMarker":"left","targetMarker":"right"},{"from":"B","to":"C","duration":0,"sourceMarker":"left","targetMarker":"right"}]}],"root":"m"})");
        GRAPH_CHECK(instance.Initialize(*chain, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity) && instance.Update(0));
        GRAPH_CHECK(instance.GetState(Identity("m"), unchanged) && unchanged.Current == Identity("C"));
        GRAPH_CHECK(std::fabs(am.TimeToPhase(instance.GetTraversals()[0].Current) -
                              double(instance.GetSyncPhases()[0]) - 1) < 1e-6);
        instance.Events().Dispatch();
        f.Reject(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b","syncGroup":"g"},{"id":"m","type":"stateMachine","states":[{"name":"A","node":"a"},{"name":"B","node":"b"}],"transitions":[{"from":"A","to":"B","sourceMarker":"left","targetMarker":"right"}]}],"root":"m"})",
            A::AnimGraphError::MarkerMismatch);
        // fallbackの倍率も設定上限内に収める。
        auto bounded = f.Graph(
            R"({"version":1,"syncGroups":[{"name":"g","speed":1,"minRate":0,"maxRate":0.25}],"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"g"}],"root":"a"})");
        GRAPH_CHECK(instance.Initialize(*bounded, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity) &&
                    instance.Update(.5f));
        GRAPH_CHECK(instance.GetTraversals()[0].Current <= .125 + 1e-6);
        instance.Events().Dispatch();
        A::ClipMetadata three;
        three.Markers = {{Identity("a"), 0}, {Identity("c"), .3f}, {Identity("b"), .6f}};
        const C::VariableArray<Identity> ordered{Identity("a"), Identity("b"), Identity("c")};
        GRAPH_CHECK(!A::BuildAnimSyncMap(three, 1, ordered, am));
    }
    void TestStrideAndFootSpeed()
    {
        GraphFixture f;
        for (auto resource : {f.AClip, f.BClip})
        {
            auto clip = resource->GetClip();
            clip.DurationSeconds = resource == f.AClip ? 1 : 2;
            clip.Channels[0].Samples = {{0, {0, 0, 0, 0}}, {clip.DurationSeconds, {2 * clip.DurationSeconds, 0, 0, 0}}};
            S::SkeletalAnimationChannel foot;
            foot.JointIndex = 1;
            foot.Path = S::SkeletalAnimationPath::Translation;
            foot.Samples = {{0, {0, 0, 0, 0}}, {clip.DurationSeconds, {-2 * clip.DurationSeconds, 0, 0, 0}}};
            clip.Channels.push_back(foot);
            clip.Metadata.Root.Mode = A::RootMotionMode::Extract;
            resource->SetClip(std::move(clip));
        }
        auto graph = f.Graph(
            R"({"version":1,"params":[{"name":"speed","type":"float","bind":"speed"}],"syncGroups":[{"name":"g","speed":"speed","minRate":0,"maxRate":2}],"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"g"},{"id":"b","type":"clip","clip":"b","syncGroup":"g"},{"id":"mix","type":"blend2","children":["a","b"],"weight":0.5}],"root":"mix"})");
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        A::AnimDriveSignals signals;
        signals.Speed = 6;
        GRAPH_CHECK(instance.Parameters().ApplyDriveSignals(signals));
        A::PoseScratch scratch;
        C::VariableArray<M::Matrix4x4> models;
        double entityX = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            GRAPH_CHECK(instance.Update(.05f) && instance.Evaluate());
            entityX += instance.ConsumeRootMotion().X;
            GRAPH_CHECK(instance.BuildJointModelMatrices(instance.GetLocalPose(), scratch, models));
            NearGraph(float(entityX + models[1].m30), 0);
        }
        NearGraph(float(entityX), 1.2f);
        signals.Speed = 0;
        GRAPH_CHECK(instance.Parameters().ApplyDriveSignals(signals));
        const auto phase = instance.GetSyncPhases()[0];
        GRAPH_CHECK(instance.Update(.25f));
        NearGraph(instance.GetSyncPhases()[0], phase);
        NearGraph(float(instance.ConsumeRootMotion().X), 0);
        const auto* phaseData = instance.GetSyncPhases().data();
        const auto* traversalData = instance.GetTraversals().data();
        const auto traversalCapacity = instance.GetTraversals().capacity();
        const auto* poseData = instance.GetLocalPose().data();
        for (unsigned i = 0; i < 1000; ++i)
        {
            GRAPH_CHECK(instance.Update(.01f) && instance.Evaluate());
            GRAPH_CHECK(
                instance.GetSyncPhases().data() == phaseData && instance.GetTraversals().data() == traversalData &&
                instance.GetTraversals().capacity() == traversalCapacity && instance.GetLocalPose().data() == poseData);
        }
    }

    void BenchmarkAnimGraph()
    {
        if (!std::getenv("NORVES_POSE_BENCHMARK"))
            return;
        GraphFixture f(52);
        for (auto resource : {f.AClip, f.BClip})
        {
            auto clip = resource->GetClip();
            for (uint32_t i = 1; i < 52; ++i)
            {
                S::SkeletalAnimationChannel channel;
                channel.JointIndex = i;
                channel.Path = S::SkeletalAnimationPath::Rotation;
                channel.Samples = {{0, {0, 0, 0, 1}}, {1, {0, 0, .1f, .994987437f}}};
                clip.Channels.push_back(std::move(channel));
            }
            resource->SetClip(std::move(clip));
        }
        const char* json[] = {
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"blend2","children":["a","b"]}],"root":"mix"})",
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"c","type":"clip","clip":"a"},{"id":"d","type":"clip","clip":"b"},{"id":"ab","type":"blend2","children":["a","b"]},{"id":"cd","type":"blend2","children":["c","d"]},{"id":"m","type":"stateMachine","states":[{"name":"A","node":"ab"},{"name":"B","node":"cd"}]}],"root":"m"})",
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"layer","type":"layered","base":"a","layers":[{"node":"b","mode":"additive","weight":0.5}]}],"root":"layer"})"};
        const char* labels[] = {"two_clip", "four_clip_transition", "additive_layer"};
        constexpr unsigned iterations = 5000;
        using Clock = std::chrono::steady_clock;
        for (unsigned kind = 0; kind < 3; ++kind)
        {
            auto graph = f.Graph(json[kind]);
            A::AnimGraphInstance instance;
            GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
            if (kind == 1)
                GRAPH_CHECK(instance.RequestState(Identity("m"), Identity("B"), 1000));
            for (unsigned i = 0; i < 100; ++i)
                GRAPH_CHECK(instance.Update(1.f / 60) && instance.Evaluate());
            auto begin = Clock::now();
            for (unsigned i = 0; i < iterations; ++i)
                GRAPH_CHECK(instance.Update(1.f / 60));
            const double update = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
            begin = Clock::now();
            for (unsigned i = 0; i < iterations; ++i)
                GRAPH_CHECK(instance.Evaluate());
            const double evaluate = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
            std::printf(
                "ANIM_GRAPH_BENCHMARK case=%s joints=52 iterations=%u update_ms=%.3f evaluate_ms=%.3f synthetic=1\n",
                labels[kind], iterations, update, evaluate);
        }
    }

    void TestSocketsAndHoldSlots()
    {
        GraphFixture f;
        A::SocketReport report;
        const auto revision = f.Skeleton->GetPoseRevision();
        GRAPH_CHECK(f.Skeleton->ApplySocketsJson(
            C::String(R"({"version":1,"sockets":[{"name":"Mouth","parent":"Root","position":[1,0,0]}]})"), report));
        GRAPH_CHECK(f.Skeleton->GetPoseRevision() == revision && f.Skeleton->FindSocket(Identity("Mouth")));
        GRAPH_CHECK(
            !f.Skeleton->ApplySocketsJson(C::String(R"({"sockets":[{"name":"Bad","parent":"Missing"}]})"), report));
        GRAPH_CHECK(f.Skeleton->FindSocket(Identity("Mouth")));
        auto clip = f.AClip->GetClip();
        clip.Channels[0].Samples = {{0, {0, 0, 0, 0}}};
        S::SkeletalAnimationChannel rotation;
        rotation.JointIndex = 0;
        rotation.Path = S::SkeletalAnimationPath::Rotation;
        rotation.Samples = {{0, {0, 0, .7071067811865475f, .7071067811865475f}}};
        clip.Channels.push_back(rotation);
        f.AClip->SetClip(std::move(clip));
        World world;
        world.Initialize();
        auto* owner = world.SpawnEntity<Entity>();
        auto* item = world.SpawnEntity<Entity>();
        auto* second = world.SpawnEntity<Entity>();
        auto* third = world.SpawnEntity<Entity>();
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(owner);
        mesh->SetSkeletalAsset(f.Asset);
        mesh->SetPlaying(false);
        owner->SetLocalScale(M::Vector3(2, 3, 4));
        world.Tick(0);
        world.LateTick(0);
        M::Transform socket;
        GRAPH_CHECK(mesh->GetSocketWorldTransform(Identity("Mouth"), socket));
        A::SkeletalPoseSnapshot oracle;
        GRAPH_CHECK(
            A::SkeletalAnimationSampler::Sample(*f.Skeleton, *f.AClip, *f.Mesh, 0, M::Matrix4x4::Identity, oracle));
        const auto& palette = oracle.BonePalette[0];
        NearGraph(socket.position.x, (palette.m00 + palette.m30) * 2);
        NearGraph(socket.position.y, (palette.m01 + palette.m31) * 3);
        NearGraph(socket.position.z, (palette.m02 + palette.m32) * 4);
        NearGraph(socket.scale.x, 2);
        NearGraph(socket.scale.y, 3);
        NearGraph(socket.scale.z, 4);
        auto* slots = world.CreateComponent<Component::HoldSlotComponent>(owner);
        C::VariableArray<Component::HoldSlotDefinition> definitions{{Identity("Mouth"), 1, {Identity("Weapon")}},
                                                                    {Identity("Pair"), 2, {}}};
        GRAPH_CHECK(slots->SetSlots(definitions));
        const Identity weapon("Weapon"), wrong("Food");
        GRAPH_CHECK(slots->TryAcquire(Identity("Mouth"), item->GetObjectId(), {&wrong, 1}) ==
                    Component::HoldAcquireResult::DeniedTags);
        GRAPH_CHECK(slots->TryAcquire(Identity("Mouth"), item->GetObjectId(), {&weapon, 1}, 1, true) ==
                    Component::HoldAcquireResult::Granted);
        GRAPH_CHECK(!slots->Release(Identity("Mouth"), item->GetObjectId()));
        GRAPH_CHECK(slots->TryAcquire(Identity("Mouth"), item->GetObjectId(), {&weapon, 1}) ==
                    Component::HoldAcquireResult::DeniedTransition);
        GRAPH_CHECK(slots->TryAcquire(Identity("Mouth"), second->GetObjectId(), {&weapon, 1}) ==
                    Component::HoldAcquireResult::DeniedOccupied);
        GRAPH_CHECK(slots->SetTransitioning(Identity("Mouth"), item->GetObjectId(), false) &&
                    slots->Release(Identity("Mouth"), item->GetObjectId()));
        GRAPH_CHECK(slots->TryAcquire(Identity("Pair"), item->GetObjectId(), {}) ==
                    Component::HoldAcquireResult::Granted);
        GRAPH_CHECK(slots->TryAcquire(Identity("Pair"), second->GetObjectId(), {}) ==
                    Component::HoldAcquireResult::Granted);
        GRAPH_CHECK(slots->TryAcquire(Identity("Pair"), third->GetObjectId(), {}) ==
                    Component::HoldAcquireResult::DeniedOccupied);
        GRAPH_CHECK(slots->Release(Identity("Pair"), item->GetObjectId()) &&
                    slots->Release(Identity("Pair"), second->GetObjectId()));
        unsigned notified = 0;
        Delegate<void, const Component::HoldSlotEvent&> once;
        once = Delegate<void, const Component::HoldSlotEvent&>([&](const auto&) { slots->OnAcquired.Remove(once); });
        slots->OnAcquired.Add(once);
        slots->OnAcquired.Add([&](const auto&) { ++notified; });
        GRAPH_CHECK(slots->TryAcquire(Identity("Pair"), third->GetObjectId(), {}) ==
                        Component::HoldAcquireResult::Granted &&
                    notified == 1);
        world.Finalize();
    }
    void TestSocketAttachmentRuntime()
    {
        GraphFixture f;
        A::SocketReport report;
        GRAPH_CHECK(f.Skeleton->ApplySocketsJson(
            C::String(R"({"sockets":[{"name":"Mouth","parent":"Root","position":[1,0,0]}]})"), report));
        World world;
        world.Initialize();
        auto* sword = world.SpawnEntity<Entity>();
        auto* owner = world.SpawnEntity<Entity>();
        auto* attachment = world.CreateComponent<Component::SocketAttachmentComponent>(sword);
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(owner);
        mesh->SetSkeletalAsset(f.Asset);
        mesh->SetPlaying(false);
        auto* animator = world.CreateComponent<Component::AnimatorComponent>(owner);
        auto* slots = world.CreateComponent<Component::HoldSlotComponent>(owner);
        const Component::HoldSlotDefinition slot{Identity("Mouth"), 1, {}};
        GRAPH_CHECK(slots->SetSlots({&slot, 1}));
        owner->SetLocalPosition(10, 0, 0);
        GRAPH_CHECK(attachment->ApplySettingsJson(
            C::String(R"({"attachProfiles":[{"name":"Along"},{"name":"Side","position":[2,0,0]}]})"), report));
        GRAPH_CHECK(slots->TryAcquire(Identity("Mouth"), sword->GetObjectId(), {}) ==
                    Component::HoldAcquireResult::Granted);
        GRAPH_CHECK(attachment->SetVelocitySampling(.2f, 0) &&
                    attachment->Attach(owner->GetObjectId(), Identity("Mouth"), Identity("Mouth"), .1f));
        GRAPH_CHECK(!slots->Release(Identity("Mouth"), sword->GetObjectId()));
        world.Tick(.05f);
        world.LateTick(.05f);
        NearGraph(sword->GetWorldTransform().position.x, 5.5f);
        world.Tick(.05f);
        world.LateTick(.05f);
        NearGraph(sword->GetWorldTransform().position.x, 11);
        GRAPH_CHECK(attachment->GetAttachState() == Component::AttachState::Attached);
        GRAPH_CHECK(attachment->SetProfile(Identity("Side"), .2f));
        world.Tick(.1f);
        world.LateTick(.1f);
        NearGraph(sword->GetWorldTransform().position.x, 12);
        GRAPH_CHECK(attachment->SetProfile(Identity("Along"), .2f));
        world.Tick(0);
        world.LateTick(0);
        NearGraph(sword->GetWorldTransform().position.x, 12);
        world.Tick(.1f);
        world.LateTick(.1f);
        NearGraph(sword->GetWorldTransform().position.x, 11.5f);
        world.Tick(.1f);
        world.LateTick(.1f);
        owner->SetLocalPosition(11, 0, 0);
        world.Tick(.1f);
        world.LateTick(.1f);
        attachment->Detach();
        NearGraph(sword->GetWorldTransform().position.x, 12);
        NearGraph(attachment->GetReleaseVelocity().Linear.x, 10);
        GRAPH_CHECK(slots->GetOccupancy(Identity("Mouth")) == 0);
        GRAPH_CHECK(attachment->Attach(owner->GetObjectId(), Identity("Mouth"), Identity("Mouth"), .1f));
        GRAPH_CHECK(animator->OnEvent.GetSize() == 1);
        // 正規のcomponent削除ではOuterが先に解除されても、購読と予約を清算する。
        sword->RemoveComponent(attachment);
        GRAPH_CHECK(slots->GetOccupancy(Identity("Mouth")) == 0 && animator->OnEvent.IsEmpty());
        world.Finalize();
    }
    void TestSocketDependencyOrder()
    {
        GraphFixture f;
        A::SocketReport report;
        GRAPH_CHECK(f.Skeleton->ApplySocketsJson(
            C::String(R"({"sockets":[{"name":"Mouth","parent":"Root","position":[1,0,0]}]})"), report));
        World world;
        world.Initialize();
        auto* a = world.SpawnEntity<Entity>();
        auto* p = world.SpawnEntity<Entity>();
        auto* t = world.SpawnEntity<Entity>(p);
        auto* q = world.SpawnEntity<Entity>();
        auto* r = world.SpawnEntity<Entity>();
        for (auto* entity : {a, p, t, q, r})
        {
            auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(entity);
            mesh->SetSkeletalAsset(f.Asset);
            mesh->SetPlaying(false);
        }
        auto* aa = world.CreateComponent<Component::SocketAttachmentComponent>(a);
        auto* ta = world.CreateComponent<Component::SocketAttachmentComponent>(t);
        auto* pa = world.CreateComponent<Component::SocketAttachmentComponent>(p);
        q->SetLocalPosition(20, 0, 0);
        r->SetLocalPosition(10, 0, 0);
        GRAPH_CHECK(pa->Attach(r->GetObjectId(), Identity("Mouth"), {}, 0) &&
                    ta->Attach(q->GetObjectId(), Identity("Mouth"), {}, 0) &&
                    aa->Attach(t->GetObjectId(), Identity("Mouth"), {}, 0));
        world.Tick(.1f);
        world.LateTick(.1f);
        NearGraph(p->GetWorldTransform().position.x, 11);
        NearGraph(t->GetWorldTransform().position.x, 21);
        NearGraph(a->GetWorldTransform().position.x, 22);
        r->SetLocalPosition(15, 0, 0);
        q->SetLocalPosition(25, 0, 0);
        world.Tick(.1f);
        world.LateTick(.1f);
        NearGraph(p->GetWorldTransform().position.x, 16);
        NearGraph(t->GetWorldTransform().position.x, 26);
        NearGraph(a->GetWorldTransform().position.x, 27);
        pa->SetTickEnabled(false);
        r->SetLocalPosition(25, 0, 0);
        world.Tick(.1f);
        world.LateTick(.1f);
        NearGraph(p->GetWorldTransform().position.x, 16);
        NearGraph(t->GetWorldTransform().position.x, 26);
        NearGraph(a->GetWorldTransform().position.x, 27);
        pa->SetTickEnabled(true);
        world.Tick(.1f);
        world.LateTick(.1f);
        NearGraph(p->GetWorldTransform().position.x, 26);
        NearGraph(t->GetWorldTransform().position.x, 26);
        aa->Detach();
        GRAPH_CHECK(!aa->Attach(a->GetObjectId(), Identity("Mouth"), {}, 0));
        world.Finalize();
    }

    class SocketTimeProbe final : public Component::SocketAttachmentComponent
    {
      public:
        Engine::TimeChannel Channel = Engine::TimeChannel::World;
        Engine::TimeChannel GetTimeChannel() const noexcept override
        {
            return Channel;
        }
    };
    void TestSocketDependencyTimeDilation()
    {
        for (bool reverse : {false, true})
            for (bool frameTimes : {false, true})
                for (unsigned variant = 0; variant < 3; ++variant)
                {
                    GraphFixture f;
                    A::SocketReport report;
                    GRAPH_CHECK(f.Skeleton->ApplySocketsJson(
                        C::String(R"({"sockets":[{"name":"Mouth","parent":"Root"}]})"), report));
                    World world;
                    world.Initialize();
                    auto* first = world.SpawnEntity<Entity>();
                    auto* second = world.SpawnEntity<Entity>();
                    auto* a = reverse ? second : first;
                    auto* t = reverse ? first : second;
                    auto* q = world.SpawnEntity<Entity>();
                    for (auto* entity : {a, t, q})
                    {
                        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(entity);
                        mesh->SetSkeletalAsset(f.Asset);
                        mesh->SetPlaying(false);
                    }
                    GRAPH_CHECK(a->SetCustomTimeDilation(variant == 1 ? 0 : .25f) && t->SetCustomTimeDilation(.5f));
                    auto* aa = world.CreateComponent<SocketTimeProbe>(a);
                    auto* ta = world.CreateComponent<SocketTimeProbe>(t);
                    if (variant == 2)
                    {
                        aa->Channel = Engine::TimeChannel::Unscaled;
                        ta->Channel = Engine::TimeChannel::Animation;
                    }
                    q->SetPosition(10, 0, 0);
                    GRAPH_CHECK(ta->Attach(q->GetObjectId(), Identity("Mouth"), {}, 1));
                    GRAPH_CHECK(aa->Attach(t->GetObjectId(), Identity("Mouth"), {}, 0));
                    if (frameTimes)
                    {
                        Engine::FrameTimes times;
                        times.Unscaled = .2f;
                        times.World = .1f;
                        times.Animation = .06f;
                        world.Tick(times);
                        world.LateTick(times);
                    }
                    else
                    {
                        world.Tick(.1f);
                        world.LateTick(.1f);
                    }
                    const float expected = frameTimes && variant == 2 ? .3f : .5f;
                    NearGraph(t->GetWorldTransform().position.x, expected);
                    NearGraph(a->GetWorldTransform().position.x, expected);
                    world.Finalize();
                }
    }

    void TestHoldProfileScript()
    {
        GraphFixture f;
        A::SocketReport report;
        GRAPH_CHECK(
            f.Skeleton->ApplySocketsJson(C::String(R"({"sockets":[{"name":"Mouth","parent":"Root"}]})"), report));
        auto graph = f.Graph(
            R"({"version":1,"params":[{"name":"Profile","type":"int"}],"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})");
        World world;
        world.Initialize();
        GRAPH_CHECK(GEngine.GetScriptRuntime().Initialize(world) == EScriptRuntimeResult::Success);
        auto* owner = world.SpawnEntity<Entity>();
        auto* target = world.SpawnEntity<Entity>();
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(target);
        mesh->SetSkeletalAsset(f.Asset);
        auto* animator = world.CreateComponent<Component::AnimatorComponent>(target);
        GRAPH_CHECK(animator->SetGraph(graph));
        auto* attachment = world.CreateComponent<Component::SocketAttachmentComponent>(owner);
        GRAPH_CHECK(attachment->ApplySettingsJson(C::String(R"({"attachProfiles":[{"name":"Along"},{"name":"Side"}]})"),
                                                  report));
        attachment->BindProfileParameter(Identity("Profile"));
        GRAPH_CHECK(attachment->Attach(target->GetObjectId(), Identity("Mouth"), {}, 0));
        auto* script = new Component::ScriptComponent();
        script->getScriptPath() = C::String("Scripts/Test/HoldProfileSetter.as");
        script->getScriptClassName() = C::String("HoldProfileSetter");
        GRAPH_CHECK(owner->AddComponent(script));
        world.Tick(.01f);
        world.LateTick(.01f);
        int32_t value = -1;
        GRAPH_CHECK(attachment->GetProfileIndex() == 1 && animator->GetInt(0, value) && value == 1);
        script->SetTickEnabled(false);
        A::AnimEventInfo event;
        event.Name = Identity("Hold.Profile");
        event.IntValue = 0;
        animator->OnEvent.Broadcast(event);
        world.Tick(.01f);
        world.LateTick(.01f);
        GRAPH_CHECK(attachment->GetProfileIndex() == 0 && animator->GetInt(0, value) && value == 0);
        world.Finalize();
        GRAPH_CHECK(GEngine.GetScriptRuntime().Shutdown() == EScriptRuntimeResult::Success);
    }
    void TestRootMotionRuntime()
    {
        GraphFixture f;
        A::ClipMetadataReport report;
        GRAPH_CHECK(f.AClip->ApplyMetadataJson(C::String(R"({"rootMotion":{"mode":"extract"}})"), report));
        auto graph = f.Graph(R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})");
        A::AnimGraphInstance instance;
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        NearGraph(instance.GetNominalSpeed(0), 2);
        GRAPH_CHECK(instance.Update(.25f));
        GRAPH_CHECK(instance.Update(.25f));
        auto delta = instance.ConsumeRootMotion();
        NearGraph(float(delta.X), 1);
        NearGraph(float(instance.ConsumeRootMotion().X), 0);
        GRAPH_CHECK(instance.Evaluate());
        NearGraph(instance.GetLocalPose()[0].Translation.x, 0);
        GRAPH_CHECK(instance.Update(1));
        NearGraph(float(instance.ConsumeRootMotion().X), 2);
        auto reverse = f.Graph(R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a","rate":-1}],"root":"a"})");
        A::AnimGraphInstance backwards;
        GRAPH_CHECK(backwards.Initialize(*reverse, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(backwards.Update(.25f));
        NearGraph(float(backwards.ConsumeRootMotion().X), -.5f);
        GRAPH_CHECK(f.AClip->ApplyMetadataJson(
            C::String(R"({"rootMotion":{"mode":"extract"},"loop":{"start":0.25,"end":0.75}})"), report));
        graph = f.Graph(R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})");
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Update(.5f));
        NearGraph(float(instance.ConsumeRootMotion().X), 1);
        auto curve = f.AClip->GetClip();
        for (auto& key : curve.Channels[0].Samples)
            key.Value.X = 0;
        curve.RootMotionJoint = 0;
        curve.RootMotion = {{0, 0, 0, 0}, {1, 1, 0, 1.5707963267948966}};
        curve.Metadata.Root.Mode = A::RootMotionMode::Extract;
        f.AClip->SetClip(std::move(curve));
        graph = f.Graph(R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})");
        GRAPH_CHECK(instance.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Update(4));
        delta = instance.ConsumeRootMotion();
        NearGraph(float(delta.X), 0);
        NearGraph(float(delta.Z), 0);
        NearGraph(float(delta.Yaw), 6.2831853f);
        auto velocity = f.Graph(
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"m","type":"stateMachine","states":[{"name":"Walk","node":"a","rootMotion":"velocity"}]}],"root":"m"})");
        GRAPH_CHECK(instance.Initialize(*velocity, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        GRAPH_CHECK(instance.Update(.5f));
        NearGraph(float(instance.ConsumeRootMotion().X), 0);
    }
} // namespace
void TestAnimGraphRuntime()
{
    TestGraphBlendAndLoad();
    TestGraphNodeKinds();
    TestClipMetadataRuntime();
    TestAnimationEventRuntime();
    TestRootMotionRuntime();
    TestAnimationSyncRuntime();
    TestSyncHitStopKeepsPointEventsStopped();
    TestStrideAndFootSpeed();
    TestRootMotionRegressions();
    TestMotionAnalysisRuntime();
    TestDynamicAdditiveReference();
    TestGraphStateMachine();
    TestGraphAcceptanceCases();
    TestGraphPlaybackRegressions();
    TestAnimatorConnection();
    TestAnimatorScriptParameter();
    TestSocketsAndHoldSlots();
    TestSocketAttachmentRuntime();
    TestSocketDependencyOrder();
    TestSocketDependencyTimeDilation();
    TestHoldProfileScript();
    BenchmarkAnimGraph();
    std::puts("AnimGraphRuntimeTest PASS");
}
