// GR10のJSON拒否、ブレンド、遷移、外部駆動を既存CPU bundleで検証する。
#include "Animation/AnimGraphInstance.h"
#include "Animation/SkeletonResource.h"
#include "Animation/SkeletalAssetResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "Object/Entity.h"
#include "Component/ScriptComponent.h"
#include "Engine/NorvesEngine.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
    void NearGraph(float a, float b)
    {
        GRAPH_CHECK(std::fabs(a - b) < 1e-4f);
    }
    struct GraphFixture : A::IClipResolver
    {
        ResourceRegistry Registry;
        C::TSharedPtr<SkeletonResource> Skeleton;
        C::TSharedPtr<SkinnedMeshResource> Mesh;
        C::TSharedPtr<AnimationClipResource> AClip, BClip;
        C::TSharedPtr<SkeletalAssetResource> Asset;
        GraphFixture()
        {
            GRAPH_CHECK(Registry.Initialize());
            Skeleton = Registry.CreateTransient<SkeletonResource>("GraphSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("GraphMesh");
            AClip = Registry.CreateTransient<AnimationClipResource>("GraphA");
            BClip = Registry.CreateTransient<AnimationClipResource>("GraphB");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("GraphAsset");
            GRAPH_CHECK(Skeleton && Mesh && AClip && BClip && Asset);
            C::VariableArray<S::SkeletalJoint> joints(2);
            joints[0].Name = "Root";
            joints[1].Name = "Child";
            joints[1].ParentIndex = 0;
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
} // namespace
void TestAnimGraphRuntime()
{
    TestGraphBlendAndLoad();
    TestGraphNodeKinds();
    TestDynamicAdditiveReference();
    TestGraphStateMachine();
    TestGraphPlaybackRegressions();
    TestAnimatorConnection();
    TestAnimatorScriptParameter();
    std::puts("AnimGraphRuntimeTest PASS");
}
