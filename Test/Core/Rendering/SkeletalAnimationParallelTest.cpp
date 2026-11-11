// 直列と並列の姿勢評価が、全コマで同じビット列を生成する契約を確かめる。
// イベント配送の順序と回数、および配送中に後方の Entity を破棄予約しても安全なことを確かめる。
#include "Animation/AnimGraphInstance.h"
#include "Animation/SkeletalAssetResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "Thread/JobSystem.h"
#include <bit>
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
    constexpr uint32_t MeshCount = 64;
    constexpr uint32_t DestroyIndex = 10;
    constexpr uint32_t FrameCount = 40;

#define PARALLEL_CHECK(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            std::fprintf(stderr, "姿勢並列評価の検査に失敗: %s:%d %s\n", __FILE__, __LINE__, #expression); \
            std::abort(); \
        } \
    } while (false)

    struct AssetFixture final : A::IClipResolver
    {
        ResourceRegistry Registry;
        C::TSharedPtr<SkeletonResource> Skeleton;
        C::TSharedPtr<SkinnedMeshResource> Mesh;
        C::TSharedPtr<AnimationClipResource> Clip;
        C::TSharedPtr<SkeletalAssetResource> Asset;
        C::TSharedPtr<AnimGraphResource> Graph;

        AssetFixture()
        {
            PARALLEL_CHECK(Registry.Initialize());
            Skeleton = Registry.CreateTransient<SkeletonResource>("ParallelSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("ParallelMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("ParallelClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("ParallelAsset");
            Graph = Registry.CreateTransient<AnimGraphResource>("ParallelGraph");
            PARALLEL_CHECK(Skeleton && Mesh && Clip && Asset && Graph);
            C::VariableArray<S::SkeletalJoint> joints(3);
            for (uint32_t i = 0; i < joints.size(); ++i)
            {
                char name[32];
                std::snprintf(name, sizeof(name), "Joint%u", i);
                joints[i].Name = name;
                joints[i].ParentIndex = int32_t(i) - 1;
                joints[i].InverseBindMatrix.fill(0);
                for (uint32_t axis = 0; axis < 4; ++axis)
                {
                    joints[i].InverseBindMatrix[axis * 5] = 1;
                }
            }
            Skeleton->SetJoints(std::move(joints));
            PARALLEL_CHECK(Skeleton->Load());
            C::VariableArray<S::SkeletalVertex> vertices(3);
            for (uint32_t i = 0; i < vertices.size(); ++i)
            {
                vertices[i].Position = {float(i) - 1, .5f * float(i), -.25f};
                vertices[i].Normal = {0, 1, 0};
                vertices[i].JointIndices[0] = i;
                vertices[i].JointIndices[1] = (i + 1) % 3;
                vertices[i].JointWeights[0] = .25f;
                vertices[i].JointWeights[1] = .75f;
            }
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(C::VariableArray<uint32_t>{0, 1, 2});
            PARALLEL_CHECK(Mesh->Load());
            S::SkeletalAnimationClip clip;
            clip.Name = "motion";
            clip.DurationSeconds = 1;
            for (uint32_t i = 0; i < 3; ++i)
            {
                const float j = float(i);
                auto channel = [&](S::SkeletalAnimationPath path, S::SkeletalValue first, S::SkeletalValue last)
                {
                    S::SkeletalAnimationChannel value;
                    value.JointIndex = i;
                    value.Path = path;
                    value.Interpolation = S::SkeletalAnimationInterpolation::Linear;
                    value.Samples.push_back({0, first});
                    value.Samples.push_back({1, last});
                    clip.Channels.push_back(std::move(value));
                };
                channel(S::SkeletalAnimationPath::Translation, {j, .25f, -.5f * j, 0},
                        {j + 2, -.75f, 1 + j, 0});
                const M::Quaternion first(M::Vector3(0, 1, 0), .125f * (j + 1));
                const M::Quaternion last(M::Vector3(0, 1, 0), .625f + .125f * j);
                channel(S::SkeletalAnimationPath::Rotation, {first.x, first.y, first.z, first.w},
                        {last.x, last.y, last.z, last.w});
                channel(S::SkeletalAnimationPath::Scale, {1, 1 + .125f * j, .875f, 0},
                        {1.25f, .75f, 1.125f + .125f * j, 0});
            }
            Clip->SetClip(std::move(clip));
            PARALLEL_CHECK(Clip->Load());
            A::ClipMetadataReport metadataReport;
            PARALLEL_CHECK(Clip->ApplyMetadataJson(C::String(
                R"({"events":[{"name":"first","t":0.2,"value":1.25,"intValue":7},{"name":"second","t":0.325},{"name":"third","t":0.7}]})"), metadataReport));
            Asset->SetClipResources(Mesh, Skeleton, {Clip});
            PARALLEL_CHECK(Asset->Load());
            A::AnimGraphReport graphReport;
            PARALLEL_CHECK(Graph->Compile(C::String(
                R"({"version":1,"nodes":[{"id":"c","type":"clip","clip":"motion"}],"root":"c"})"),
                *Skeleton, *this, graphReport) && Graph->Load());
        }

        C::TSharedPtr<AnimationClipResource> ResolveClip(C::StringView name) const override
        {
            return name == C::StringView("motion") ? Clip : C::TSharedPtr<AnimationClipResource>{};
        }
    };

    struct RecordedEvent
    {
        uint32_t AnimatorIndex;
        A::AnimEventInfo Info;
    };

    struct WorldFixture
    {
        World Owner;
        Component::SkinnedMeshComponent* Meshes[MeshCount]{};
        Entity* Entities[MeshCount]{};
        C::VariableArray<RecordedEvent> Events;
        uint64_t DestroyedId = 0;
        uint32_t CurrentFrame = 0;
        uint32_t DestroyFrame = FrameCount;
        bool bDestroyRequested = false;

        WorldFixture(AssetFixture& asset, bool bParallel, bool bDestroy)
        {
            Owner.Initialize();
            PARALLEL_CHECK(!Owner.GetPoseEvaluationParallelSettings().bEnabled);
            if (bParallel)
            {
                World::PoseEvaluationParallelSettings settings;
                settings.bEnabled = true;
                settings.MinBatchCount = 2;
                settings.Grain = 3;
                Owner.SetPoseEvaluationParallelSettings(settings);
            }
            for (uint32_t i = 0; i < MeshCount; ++i)
            {
                Entities[i] = Owner.SpawnEntity<Entity>();
                PARALLEL_CHECK(Entities[i]);
                Meshes[i] = Owner.CreateComponent<Component::SkinnedMeshComponent>(Entities[i]);
                PARALLEL_CHECK(Meshes[i]);
                Meshes[i]->SetSkeletalAsset(asset.Asset);
                if (i % 2 == 0)
                {
                    auto* animator = Owner.CreateComponent<Component::AnimatorComponent>(Entities[i]);
                    PARALLEL_CHECK(animator && animator->SetGraph(asset.Graph));
                    PARALLEL_CHECK(animator->Step(float(i) * .0137f));
                    animator->OnEvent.Add([this, i, bDestroy](const A::AnimEventInfo& event)
                    {
                        Events.push_back({i, event});
                        if (bDestroy && i == 0 && !bDestroyRequested)
                        {
                            // 予約の後は PoseFinalize の配送から外れ、取り外しのときに保留分が配られる（直列と並列で同じ）。
                            DestroyedId = Entities[DestroyIndex]->GetObjectId();
                            PARALLEL_CHECK(Owner.RemoveEntity(Entities[DestroyIndex]));
                            PARALLEL_CHECK(Entities[DestroyIndex]->IsPendingDestroy());
                            bDestroyRequested = true;
                            DestroyFrame = CurrentFrame;
                        }
                    });
                }
                else
                {
                    PARALLEL_CHECK(Meshes[i]->SetAnimationClip(asset.Clip));
                    Meshes[i]->SetAnimationTimeSeconds(float(i) * .0173f);
                    Meshes[i]->SetPlaying(true);
                    Meshes[i]->SetLooping(true);
                }
            }
        }

        ~WorldFixture()
        {
            Owner.Finalize();
        }

        void Tick(uint32_t frame)
        {
            CurrentFrame = frame;
            Events.clear();
            Owner.Tick(1.0f / 30.0f);
            if (bDestroyRequested)
            {
                PARALLEL_CHECK(Owner.FindEntityByObjectId(DestroyedId) == nullptr);
                // Tick の終端で解放されたポインタは以後参照しない。
                Entities[DestroyIndex] = nullptr;
                Meshes[DestroyIndex] = nullptr;
            }
        }
    };

    void CheckBits(float left, float right)
    {
        PARALLEL_CHECK(std::bit_cast<uint32_t>(left) == std::bit_cast<uint32_t>(right));
    }

    void CheckVector(const M::Vector3& left, const M::Vector3& right)
    {
        CheckBits(left.x, right.x);
        CheckBits(left.y, right.y);
        CheckBits(left.z, right.z);
    }

    void CheckMeshes(const WorldFixture& serial, const WorldFixture& parallel)
    {
        for (uint32_t i = 0; i < MeshCount; ++i)
        {
            PARALLEL_CHECK(bool(serial.Meshes[i]) == bool(parallel.Meshes[i]));
            if (!serial.Meshes[i])
            {
                continue;
            }
            const auto& left = *serial.Meshes[i];
            const auto& right = *parallel.Meshes[i];
            PARALLEL_CHECK(left.IsPoseCurrent() && right.IsPoseCurrent());
            PARALLEL_CHECK(left.GetPoseSerial() > 0 && left.GetPoseSerial() == right.GetPoseSerial());
            const auto& a = left.GetPoseSnapshot();
            const auto& b = right.GetPoseSnapshot();
            PARALLEL_CHECK(a.BonePalette.size() == 3 && a.BonePalette.size() == b.BonePalette.size());
            for (size_t joint = 0; joint < a.BonePalette.size(); ++joint)
            {
                for (size_t element = 0; element < 16; ++element)
                {
                    CheckBits(a.BonePalette[joint].values[element], b.BonePalette[joint].values[element]);
                }
            }
            PARALLEL_CHECK(a.bHasAnimatedBounds && a.bHasAnimatedBounds == b.bHasAnimatedBounds);
            CheckVector(a.AnimatedBounds.Min, b.AnimatedBounds.Min);
            CheckVector(a.AnimatedBounds.Max, b.AnimatedBounds.Max);
        }
    }

    void CheckEvents(const C::VariableArray<RecordedEvent>& serial, const C::VariableArray<RecordedEvent>& parallel)
    {
        PARALLEL_CHECK(serial.size() == parallel.size());
        for (size_t i = 0; i < serial.size(); ++i)
        {
            PARALLEL_CHECK(serial[i].AnimatorIndex == parallel[i].AnimatorIndex);
            const auto& a = serial[i].Info;
            const auto& b = parallel[i].Info;
            PARALLEL_CHECK(a.Name == b.Name && a.SyncGroup == b.SyncGroup);
            PARALLEL_CHECK(a.Node == b.Node && a.EventIndex == b.EventIndex && a.Kind == b.Kind);
            PARALLEL_CHECK(a.bInterrupted == b.bInterrupted && a.IntValue == b.IntValue);
            CheckBits(a.Value, b.Value);
            PARALLEL_CHECK(std::bit_cast<uint64_t>(a.OffsetSeconds) == std::bit_cast<uint64_t>(b.OffsetSeconds));
            PARALLEL_CHECK(a.Sequence == b.Sequence && a.Batch == b.Batch && a.WindowToken == b.WindowToken);
            PARALLEL_CHECK(a.Occurrence == b.Occurrence);
        }
    }

    struct ScenarioResult
    {
        World::PoseEvaluationParallelStats Stats;
        size_t RemovalEvents;
        uint32_t DestroyFrame;
    };

    ScenarioResult CheckScenario(bool bDestroy)
    {
        AssetFixture asset;
        WorldFixture serial(asset, false, bDestroy);
        WorldFixture parallel(asset, true, bDestroy);
        size_t eventCount = 0;
        size_t eventsAfterDestroy = 0;
        size_t victimEventsOnDestroyFrame = 0;
        size_t removalEvents = 0;
        for (uint32_t frame = 0; frame < FrameCount; ++frame)
        {
            serial.Tick(frame);
            parallel.Tick(frame);
            PARALLEL_CHECK(serial.bDestroyRequested == parallel.bDestroyRequested);
            PARALLEL_CHECK(serial.DestroyFrame == parallel.DestroyFrame);
            CheckMeshes(serial, parallel);
            CheckEvents(serial.Events, parallel.Events);
            eventCount += serial.Events.size();
            if (serial.bDestroyRequested)
            {
                eventsAfterDestroy += serial.Events.size();
                if (frame == serial.DestroyFrame)
                {
                    for (const auto& event : serial.Events)
                    {
                        removalEvents += event.AnimatorIndex == DestroyIndex ? 1 : 0;
                    }
                }
                else
                {
                    // 取り外しの配送は予約したコマに含め、その次のコマ以降には残さない。
                    PARALLEL_CHECK(frame > serial.DestroyFrame);
                    for (const auto& event : serial.Events)
                    {
                        PARALLEL_CHECK(event.AnimatorIndex != DestroyIndex);
                    }
                    for (const auto& event : parallel.Events)
                    {
                        PARALLEL_CHECK(event.AnimatorIndex != DestroyIndex);
                    }
                }
            }
            // 破棄なしのケースでも、破棄を起こすイベントのコマに後方の配送があることを反証する。
            for (const auto& event : serial.Events)
            {
                if (event.AnimatorIndex == 0 && event.Info.Name == Identity("first") && frame < 10)
                {
                    for (const auto& candidate : serial.Events)
                    {
                        victimEventsOnDestroyFrame += candidate.AnimatorIndex == DestroyIndex ? 1 : 0;
                    }
                }
            }
        }
        PARALLEL_CHECK(eventCount > 0);
        if (bDestroy)
        {
            PARALLEL_CHECK(serial.bDestroyRequested && serial.DestroyFrame + 1 < FrameCount);
            PARALLEL_CHECK(eventsAfterDestroy > 0);
            PARALLEL_CHECK(removalEvents > 0);
        }
        else
        {
            PARALLEL_CHECK(victimEventsOnDestroyFrame > 0);
        }
        const auto& zero = serial.Owner.GetPoseEvaluationParallelStats();
        PARALLEL_CHECK(zero.BatchedSegments == 0 && zero.AnimatorJobs == 0 && zero.MeshJobs == 0 &&
                       zero.LastSegmentEntries == 0);
        const auto stats = parallel.Owner.GetPoseEvaluationParallelStats();
        PARALLEL_CHECK(stats.BatchedSegments > 0 && stats.AnimatorJobs > 0 && stats.MeshJobs > 0);
        return {stats, removalEvents, serial.DestroyFrame};
    }
} // namespace

void TestSkeletalAnimationParallel()
{
    auto& jobs = NorvesLib::Thread::JobSystem::Get();
    const bool bOwnJobSystem = jobs.GetWorkerThreadCount() == 0;
    const auto previousMode = jobs.GetExecutionMode();
    if (bOwnJobSystem)
    {
        jobs.Initialize(4);
        PARALLEL_CHECK(jobs.GetWorkerThreadCount() == 4);
    }
    const auto agreement = CheckScenario(false);
    const auto destruction = CheckScenario(true);
    if (bOwnJobSystem)
    {
        jobs.Shutdown();
        jobs.SetExecutionMode(previousMode);
        PARALLEL_CHECK(jobs.GetWorkerThreadCount() == 0);
    }
    std::printf("SkeletalAnimationParallelTest 成功: 直列=0/0/0 一致=%llu/%llu/%llu 破棄=%llu/%llu/%llu 予約コマ=%u 取り外し配送=%zu\n",
                static_cast<unsigned long long>(agreement.Stats.BatchedSegments),
                static_cast<unsigned long long>(agreement.Stats.AnimatorJobs),
                static_cast<unsigned long long>(agreement.Stats.MeshJobs),
                static_cast<unsigned long long>(destruction.Stats.BatchedSegments),
                static_cast<unsigned long long>(destruction.Stats.AnimatorJobs),
                static_cast<unsigned long long>(destruction.Stats.MeshJobs),
                destruction.DestroyFrame, destruction.RemovalEvents);
}
