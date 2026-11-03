// グラフ経由のイベント配送で、刻み・境界・閾値・溢れの契約を確かめる。
#include "Animation/AnimGraphInstance.h"
#include "Animation/MotionAnalysis.h"
#include "Animation/SkeletonResource.h"
#include "Logging/Logger.h"
#include "Object/ResourceRegistry.h"
#include "Resource/SkinnedMeshResource.h"
#include "Text/JsonDocument.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

namespace
{
    using namespace NorvesLib::Core;
    namespace A = NorvesLib::Core::Animation;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Skeletal;
    namespace M = NorvesLib::Math;
    namespace L = NorvesLib::Core::Logging;

#define EVENT_CHECK(x) \
    do \
    { \
        if (!(x)) \
        { \
            std::fprintf(stderr, "イベント配送の検証失敗 %s:%d %s\n", __FILE__, __LINE__, #x); \
            std::abort(); \
        } \
    } while (false)

    struct EventFixture : A::IClipResolver
    {
        ResourceRegistry Registry;
        C::TSharedPtr<SkeletonResource> Skeleton;
        C::TSharedPtr<SkinnedMeshResource> Mesh;
        C::TSharedPtr<AnimationClipResource> AClip, BClip;

        EventFixture()
        {
            EVENT_CHECK(Registry.Initialize());
            Skeleton = Registry.CreateTransient<SkeletonResource>("EventSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("EventMesh");
            AClip = Registry.CreateTransient<AnimationClipResource>("EventA");
            BClip = Registry.CreateTransient<AnimationClipResource>("EventB");
            EVENT_CHECK(Skeleton && Mesh && AClip && BClip);
            C::VariableArray<S::SkeletalJoint> joints(1);
            joints[0].Name = "Root";
            joints[0].InverseBindMatrix.fill(0);
            for (unsigned i = 0; i < 4; ++i)
            {
                joints[0].InverseBindMatrix[i * 5] = 1;
            }
            Skeleton->SetJoints(std::move(joints));
            EVENT_CHECK(Skeleton->Load());
            C::VariableArray<S::SkeletalVertex> vertices(1);
            vertices[0].Normal = {0, 1, 0};
            vertices[0].JointWeights[0] = 1;
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(C::VariableArray<uint32_t>{0, 0, 0});
            EVENT_CHECK(Mesh->Load());
            for (auto* resource : {AClip.get(), BClip.get()})
            {
                S::SkeletalAnimationClip clip;
                clip.Name = resource == AClip.get() ? "a" : "b";
                clip.DurationSeconds = 1;
                S::SkeletalAnimationChannel channel;
                channel.JointIndex = 0;
                channel.Path = S::SkeletalAnimationPath::Translation;
                channel.Samples = {{0, {0, 0, 0, 0}}, {1, {1, 0, 0, 0}}};
                clip.Channels.push_back(std::move(channel));
                resource->SetClip(std::move(clip));
                EVENT_CHECK(resource->Load());
            }
        }
        C::TSharedPtr<AnimationClipResource> ResolveClip(C::StringView name) const override
        {
            return name == C::StringView("a") ? AClip
                   : name == C::StringView("b") ? BClip : C::TSharedPtr<AnimationClipResource>{};
        }
        C::TSharedPtr<AnimGraphResource> Graph(const char* json)
        {
            auto graph = Registry.CreateTransient<AnimGraphResource>("EventGraph");
            A::AnimGraphReport report;
            EVENT_CHECK(graph && graph->Compile(C::String(json), *Skeleton, *this, report) && graph->Load());
            return graph;
        }
        void Metadata(AnimationClipResource& clip, const C::String& json)
        {
            A::ClipMetadataReport report;
            EVENT_CHECK(clip.ApplyMetadataJson(json, report));
        }
    };

    using EventList = C::VariableArray<A::AnimEventInfo>;
    constexpr const char* SingleGraph =
        R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})";

    void Initialize(EventFixture& f, const AnimGraphResource& graph, A::AnimGraphInstance& instance, EventList& events)
    {
        EVENT_CHECK(instance.Initialize(graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        instance.Events().OnEvent.Add([&events](const A::AnimEventInfo& event) { events.push_back(event); });
    }
    void Advance(A::AnimGraphInstance& instance, float dt)
    {
        EVENT_CHECK(instance.Update(dt));
        instance.Events().Dispatch();
    }
    bool SameEvent(const A::AnimEventInfo& a, const A::AnimEventInfo& b)
    {
        return a.Name == b.Name && a.Kind == b.Kind && a.Occurrence == b.Occurrence &&
               a.bInterrupted == b.bInterrupted;
    }
    void CheckPairs(const EventList& events)
    {
        // トークンは各実行の内部でだけ照合し、対の名前と周回が一致することを確かめる。
        for (size_t i = 0; i < events.size(); ++i)
        {
            const auto& event = events[i];
            if (event.Kind == A::AnimEventKind::Point)
            {
                continue;
            }
            EVENT_CHECK(event.WindowToken != 0);
            size_t begins = 0, ends = 0, beginIndex = 0, endIndex = 0;
            for (size_t j = 0; j < events.size(); ++j)
            {
                if (events[j].WindowToken != event.WindowToken)
                {
                    continue;
                }
                EVENT_CHECK(events[j].Name == event.Name && events[j].Occurrence == event.Occurrence);
                if (events[j].Kind == A::AnimEventKind::Begin)
                {
                    ++begins;
                    beginIndex = j;
                }
                if (events[j].Kind == A::AnimEventKind::End)
                {
                    ++ends;
                    endIndex = j;
                }
            }
            EVENT_CHECK(begins == 1 && ends == 1 && beginIndex < endIndex);
        }
    }
    void CheckSequence(const EventList& expected, const EventList& actual)
    {
        CheckPairs(expected);
        CheckPairs(actual);
        EVENT_CHECK(expected.size() == actual.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            EVENT_CHECK(SameEvent(expected[i], actual[i]));
        }
    }

    void TestPartitionInvariant()
    {
        EventFixture f;
        auto clip = f.BClip->GetClip();
        clip.DurationSeconds = 2;
        clip.Channels[0].Samples.back().TimeSeconds = 2;
        f.BClip->SetClip(std::move(clip));
        f.Metadata(*f.AClip, C::String(
            R"({"markers":[{"name":"left","t":0},{"name":"right","t":0.5}],"events":[{"name":"attack","t":0.125,"end":0.375},{"name":"step","t":0.25},{"name":"aOnly","t":0.625}]})"));
        f.Metadata(*f.BClip, C::String(
            R"({"markers":[{"name":"left","t":0.25},{"name":"right","t":1.5}],"events":[{"name":"attack","t":0.5625,"end":1.1875},{"name":"step","t":0.875},{"name":"bOnly","t":1.34375}]})"));
        auto graph = f.Graph(
            R"({"version":1,"params":[{"name":"blend","type":"float","value":0.5}],"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"gait"},{"id":"b","type":"clip","clip":"b","syncGroup":"gait"},{"id":"mix","type":"blend2","children":["a","b"],"weight":"blend"}],"root":"mix"})");
        EventList streams[3];
        const double total = double(1.f / 60) * 240;
        for (unsigned variant = 0; variant < 3; ++variant)
        {
            A::AnimGraphInstance instance;
            Initialize(f, *graph, instance, streams[variant]);
            EVENT_CHECK(instance.FindSyncGroup(Identity("gait")) == 0);
            std::mt19937 random(0x4156454e);
            double elapsed = 0;
            unsigned steps = 0;
            while (elapsed < total)
            {
                float dt = variant == 0 ? 1.f / 60 : variant == 1 ? 1.f / 30
                    : float(17 + random() % 240) / 4096.f;
                if (double(dt) > total - elapsed)
                {
                    dt = float(total - elapsed);
                }
                Advance(instance, dt);
                elapsed += dt;
                ++steps;
            }
            EVENT_CHECK(elapsed == total && (variant == 2 || steps == (variant == 0 ? 240u : 120u)));
            EVENT_CHECK(instance.Events().DroppedCount() == 0 && instance.Events().ActiveWindowCount() == 0);
            // 同名イベントの同期による統合も、空列同士の一致ではなく独立した期待列で押さえる。
            EVENT_CHECK(streams[variant].size() == 20);
            const char* names[] = {"attack", "step", "attack", "bOnly", "aOnly"};
            const A::AnimEventKind kinds[] = {A::AnimEventKind::Begin, A::AnimEventKind::Point,
                A::AnimEventKind::End, A::AnimEventKind::Point, A::AnimEventKind::Point};
            for (size_t i = 0; i < streams[variant].size(); ++i)
            {
                const auto& e = streams[variant][i];
                EVENT_CHECK(e.Name == Identity(names[i % 5]) && e.Kind == kinds[i % 5] &&
                            e.Occurrence == int64_t(i / 5) && !e.bInterrupted);
            }
        }
        CheckSequence(streams[0], streams[1]);
        CheckSequence(streams[0], streams[2]);
    }

    void TestTraversalBoundaries()
    {
        EventFixture f;
        f.Metadata(*f.AClip, C::String(
            R"({"events":[{"name":"window","t":0.125,"end":0.875},{"name":"quarter","t":0.25},{"name":"half","t":0.5},{"name":"threeQuarter","t":0.75}]})"));
        EventList directions[2];
        for (unsigned reverse = 0; reverse < 2; ++reverse)
        {
            auto graph = f.Graph(reverse == 0
                ? R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a","loop":false}],"root":"a"})"
                : R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a","loop":false,"rate":-1}],"root":"a"})");
            A::AnimGraphInstance instance;
            auto& events = directions[reverse];
            Initialize(f, *graph, instance, events);
            Advance(instance, .25f);
            EVENT_CHECK(events.size() == 2 && events[0].Kind == A::AnimEventKind::Begin &&
                        events[1].Name == Identity(reverse ? "threeQuarter" : "quarter"));
            Advance(instance, 0);
            EVENT_CHECK(events.size() == 2);
            Advance(instance, .25f);
            EVENT_CHECK(events.size() == 3 && events.back().Name == Identity("half"));
            Advance(instance, .25f);
            EVENT_CHECK(events.size() == 4 && events.back().Name == Identity(reverse ? "quarter" : "threeQuarter"));
            Advance(instance, .125f);
            EVENT_CHECK(events.size() == 5 && events.back().Kind == A::AnimEventKind::End);
            Advance(instance, .125f);
            Advance(instance, .25f);
            EVENT_CHECK(events.size() == 5 && instance.Events().DroppedCount() == 0);
            CheckPairs(events);
        }
        // 逆再生は順序を反転するが、点と窓の集合・周回・中断の有無を保つ。
        for (const auto& event : directions[0])
        {
            size_t matches = 0;
            for (const auto& other : directions[1])
            {
                matches += SameEvent(event, other) ? 1 : 0;
            }
            EVENT_CHECK(matches == 1);
        }
        auto graph = f.Graph(SingleGraph);
        EventList coarse, fine;
        for (unsigned variant = 0; variant < 2; ++variant)
        {
            A::AnimGraphInstance instance;
            auto& events = variant == 0 ? coarse : fine;
            Initialize(f, *graph, instance, events);
            for (unsigned i = 0; i < (variant == 0 ? 1u : 16u); ++i)
            {
                Advance(instance, variant == 0 ? 2.f : .125f);
            }
            EVENT_CHECK(events.size() == 10 && instance.Events().DroppedCount() == 0);
            for (size_t i = 0; i < events.size(); ++i)
            {
                EVENT_CHECK(events[i].Occurrence == int64_t(i / 5) && !events[i].bInterrupted);
            }
        }
        CheckSequence(fine, coarse);
    }

    void TestWeightAndInterruption()
    {
        EventFixture f;
        f.Metadata(*f.BClip, C::String(R"({"events":[{"name":"threshold","t":0.25,"minWeight":0.5}]})"));
        auto graph = f.Graph(
            R"({"version":1,"params":[{"name":"blend","type":"float","value":0.5}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"mix","type":"blend2","children":["a","b"],"weight":"blend"}],"root":"mix"})");
        const float weights[] = {std::nextafter(.5f, 0.f), .5f, std::nextafter(.5f, 1.f)};
        for (unsigned i = 0; i < 3; ++i)
        {
            EventList events;
            A::AnimGraphInstance instance;
            Initialize(f, *graph, instance, events);
            EVENT_CHECK(instance.Parameters().SetFloat(0, weights[i]));
            Advance(instance, .25f);
            EVENT_CHECK(events.size() == (i == 0 ? 0u : 1u));
            if (i != 0)
            {
                EVENT_CHECK(events[0].Name == Identity("threshold") && events[0].Kind == A::AnimEventKind::Point);
            }
        }
        f.Metadata(*f.AClip, C::String(R"({"events":[{"name":"attack","t":0.125,"end":0.875}]})"));
        f.Metadata(*f.BClip, C::String(R"({"events":[]})"));
        auto machine = f.Graph(
            R"({"version":1,"params":[{"name":"Go","type":"trigger"}],"nodes":[{"id":"a","type":"clip","clip":"a"},{"id":"b","type":"clip","clip":"b"},{"id":"m","type":"stateMachine","states":[{"name":"Attack","node":"a"},{"name":"Idle","node":"b"}],"transitions":[{"from":"Attack","to":"Idle","duration":0.25,"conditions":[{"param":"Go","op":"eq","value":true}]}]}],"root":"m"})");
        EventList events;
        A::AnimGraphInstance instance;
        Initialize(f, *machine, instance, events);
        Advance(instance, .25f);
        EVENT_CHECK(events.size() == 1 && events[0].Kind == A::AnimEventKind::Begin);
        EVENT_CHECK(instance.Parameters().SetTrigger(0));
        Advance(instance, .125f);
        EVENT_CHECK(events.size() == 1);
        Advance(instance, .125f);
        EVENT_CHECK(events.size() == 2 && events[1].Kind == A::AnimEventKind::End && events[1].bInterrupted);
        for (unsigned i = 0; i < 8; ++i)
        {
            Advance(instance, .25f);
        }
        EVENT_CHECK(events.size() == 2 && instance.Events().ActiveWindowCount() == 0);
        CheckPairs(events);
    }

    struct OverflowSink final : L::ILogSink
    {
        unsigned Warnings = 0;
        void OnLog(const L::LogEntry& entry) override
        {
            if (entry.level == L::LogLevel::Warning && entry.category == C::String("Animation") &&
                std::strstr(entry.filename.c_str(), "AnimationEvents.cpp") &&
                std::strstr(entry.message.c_str(), "dropped="))
            {
                ++Warnings;
            }
        }
    };
    void TestDeliveryOverflow()
    {
        EventFixture f;
        C::String metadata = R"({"events":[{"name":"openFirst","t":0.0625,"end":0.5},{"name":"openSecond","t":0.125,"end":0.75},{"name":"beforeFlood","t":0.25,"end":0.625})";
        for (unsigned i = 0; i < 96; ++i)
        {
            char event[96];
            std::snprintf(event, sizeof(event), R"(,{"name":"flood%u","t":0.375})", i);
            metadata += event;
        }
        metadata += R"(,{"name":"afterFlood","t":0.4375,"end":0.8125}]})";
        f.Metadata(*f.AClip, metadata);
        auto graph = f.Graph(SingleGraph);
        EventList events;
        A::AnimGraphInstance instance;
        Initialize(f, *graph, instance, events);
        Advance(instance, .125f);
        EVENT_CHECK(events.size() == 2 && instance.Events().ActiveWindowCount() == 2);
        const uint64_t droppedBefore = instance.Events().DroppedCount();
        auto& logger = L::Logger::GetInstance();
        const auto savedConfig = logger.GetConfig();
        auto config = savedConfig;
        config.bAsyncLogging = false;
        config.minLevel = L::LogLevel::Warning;
        config.outputType = L::LogOutput::None;
        logger.SetFormatter(nullptr);
        logger.UpdateConfig(config);
        OverflowSink sink;
        logger.AddSink(&sink);
        Advance(instance, .75f);
        logger.RemoveSink(&sink);
        logger.UpdateConfig(savedConfig);
        EVENT_CHECK(instance.Events().DroppedCount() > droppedBefore);
#if NORVES_ENABLE_LOGGING
        EVENT_CHECK(sink.Warnings > 0);
#endif
        EVENT_CHECK(events.size() > 2 && events.size() <= 2 + A::AnimationEventQueue::Capacity);
        // 飽和前から開いていた窓のEndと、同じ配送内で採用されたBeginのEndを予約する。
        unsigned normalEnds = 0, lateBegins = 0;
        for (const auto& event : events)
        {
            if (event.Kind == A::AnimEventKind::End)
            {
                EVENT_CHECK(!event.bInterrupted);
                ++normalEnds;
            }
            lateBegins += event.Kind == A::AnimEventKind::Begin && event.Name == Identity("afterFlood") ? 1 : 0;
        }
        EVENT_CHECK(normalEnds == 3 && lateBegins == 0);
        EVENT_CHECK(instance.Events().ActiveWindowCount() == 0);
        Advance(instance, .125f);
        CheckPairs(events);
    }
    void TestMetadataLoadingProperties()
    {
        const A::ClipMetadata empty;
        A::ClipMetadata parsed, overridden;
        A::ClipMetadataReport report;
        const C::String overflow = R"({"events":[{"name":"overflow","t":1e400}]})";
        JsonDocument document;
        const bool bJsonAccepted = JsonDocument::TryParse(overflow, document);
        if (bJsonAccepted)
        {
            EVENT_CHECK(!std::isfinite(document.GetRoot().FindMember("events").GetArrayElement(0)
                                           .FindMember("t").AsNumber()));
        }
        std::puts(bJsonAccepted ? "1e400: JSON解析後のmetadata数値検査で拒否"
                                : "1e400: JsonDocumentの数値解析で拒否");
        const char* rejected[] = {
            R"({"events":[{"name":"late","t":0.75},{"name":"early","t":0.25}]})",
            R"({"events":[{"name":"","t":0.25}]})",
            overflow.c_str()
        };
        const A::ClipMetadataError errors[] = {
            A::ClipMetadataError::InvalidTime, A::ClipMetadataError::InvalidName,
            bJsonAccepted ? A::ClipMetadataError::InvalidTime : A::ClipMetadataError::InvalidJson
        };
        for (unsigned i = 0; i < 3; ++i)
        {
            // extrasと実行時sidecarの入口で同じ不正データを拒否する。
            EVENT_CHECK(!A::ParseClipMetadata(C::String(rejected[i]), 1, false, parsed, report));
            EVENT_CHECK(report.Error == errors[i]);
            EVENT_CHECK(!A::ApplyClipMetadataOverride(empty, C::String(rejected[i]), 1, overridden, report));
            EVENT_CHECK(report.Error == errors[i]);
        }

        const C::String unknown = R"({"futureTop":true,"events":[{"name":"step","t":0.25,"futureEvent":1}],"markers":[{"name":"plant","t":0.125,"futureMarker":1}],"rootMotion":{"mode":"extract","futureRoot":1}})";
        struct MetadataWarningSink final : L::ILogSink
        {
            unsigned Warnings = 0;
            void OnLog(const L::LogEntry& entry) override
            {
                if (entry.level == L::LogLevel::Warning && entry.category == C::String("Animation"))
                {
                    ++Warnings;
                }
            }
        };
        auto& logger = L::Logger::GetInstance();
        const auto savedConfig = logger.GetConfig();
        auto config = savedConfig;
        config.bAsyncLogging = false;
        config.minLevel = L::LogLevel::Warning;
        config.outputType = L::LogOutput::None;
        logger.UpdateConfig(config);
        MetadataWarningSink sink;
        logger.AddSink(&sink);
        EVENT_CHECK(A::ParseClipMetadata(unknown, 1, false, parsed, report));
        EVENT_CHECK(sink.Warnings == 0);
        EVENT_CHECK(report.Error == A::ClipMetadataError::None && report.UnknownKeys == 0);
        EVENT_CHECK(A::ApplyClipMetadataOverride(empty, unknown, 1, overridden, report));
        logger.RemoveSink(&sink);
        logger.UpdateConfig(savedConfig);
#if NORVES_ENABLE_LOGGING
        EVENT_CHECK(sink.Warnings > 0);
#endif
        EVENT_CHECK(report.Error == A::ClipMetadataError::None && report.UnknownKeys == 4);
        EVENT_CHECK(A::SameClipMetadata(parsed, overridden));

        const C::String complete = R"({"version":1,"events":[{"name":"step","t":0.125,"value":0.5,"intValue":2},{"name":"plant","t":0.25,"end":0.625,"minWeight":0.5}],"markers":[{"name":"left","t":0},{"name":"right","t":0.5}],"rootMotion":{"mode":"extract","joint":0,"x":true,"z":false,"yaw":true,"nominalSpeed":2},"loop":{"start":0.125,"end":0.875},"groundOffset":0.125})";
        EVENT_CHECK(A::ParseClipMetadata(complete, 1, false, parsed, report));
        EVENT_CHECK(report.Error == A::ClipMetadataError::None && report.UnknownKeys == 0);
        EVENT_CHECK(A::ApplyClipMetadataOverride(empty, complete, 1, overridden, report));
        EVENT_CHECK(report.Error == A::ClipMetadataError::None && report.UnknownKeys == 0);
        EVENT_CHECK(parsed.Events.size() == 2 && parsed.Events[0].EndTime < 0 &&
                    parsed.Events[1].EndTime == .625f && parsed.Markers.size() == 2 &&
                    parsed.Root.Mode == A::RootMotionMode::Extract && parsed.Root.Joint == 0 &&
                    !parsed.Root.bZ && parsed.Root.NominalSpeed == 2);
        EVENT_CHECK(A::SameClipMetadata(parsed, overridden));
    }

    void TestRootMotionProperties()
    {
        EventFixture f;
        auto clip = f.AClip->GetClip();
        clip.Channels[0].Samples = {{0, {2, 1, -3, 0}}, {1, {6, 3, 3, 0}}};
        S::SkeletalAnimationChannel rotation;
        rotation.JointIndex = 0;
        rotation.Path = S::SkeletalAnimationPath::Rotation;
        rotation.Samples = {{0, {0, 0, 0, 1}},
                            {1, {0, float(std::sin(.4)), 0, float(std::cos(.4))}}};
        clip.Channels.push_back(std::move(rotation));
        f.AClip->SetClip(std::move(clip));
        f.Metadata(*f.AClip, C::String(R"({"rootMotion":{"mode":"inPlace"}})"));
        auto graph = f.Graph(SingleGraph);
        A::AnimGraphInstance inPlace;
        EVENT_CHECK(inPlace.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
        for (unsigned frame = 1; frame <= 3; ++frame)
        {
            EVENT_CHECK(inPlace.Update(.25f) && inPlace.Evaluate());
            const auto& root = inPlace.GetLocalPose()[0].Translation;
            EVENT_CHECK(std::fabs(root.x - 2) < 1e-5f && std::fabs(root.z + 3) < 1e-5f);
            EVENT_CHECK(std::fabs(root.y - (1 + .5f * frame)) < 1e-5f);
            const auto delta = inPlace.ConsumeRootMotion();
            EVENT_CHECK(delta.X == 0 && delta.Z == 0 && delta.Yaw == 0);
        }

        // 各曲線はX=vx*t、Z=vz*t、Yaw=turn*t。速度と向きの両方を違える。
        const double vx[] = {2, -1}, vz[] = {1, 3}, turn[] = {.8, -.6};
        for (unsigned i = 0; i < 2; ++i)
        {
            auto& resource = i == 0 ? *f.AClip : *f.BClip;
            auto curve = resource.GetClip();
            curve.Channels.clear();
            S::SkeletalAnimationChannel stationary;
            stationary.JointIndex = 0;
            stationary.Path = S::SkeletalAnimationPath::Translation;
            stationary.Samples = {{0, {0, 0, 0, 0}}, {1, {0, 0, 0, 0}}};
            curve.Channels.push_back(std::move(stationary));
            curve.RootMotionJoint = 0;
            curve.RootMotion = {{0, 0, 0, 0}, {1, vx[i], vz[i], turn[i]}};
            resource.SetClip(std::move(curve));
            f.Metadata(resource, C::String(R"({"rootMotion":{"mode":"extract"}})"));
        }
        graph = f.Graph(
            R"({"version":1,"params":[{"name":"blend","type":"float","value":0.5}],"nodes":[{"id":"a","type":"clip","clip":"a","loop":false},{"id":"b","type":"clip","clip":"b","loop":false},{"id":"mix","type":"blend2","children":["a","b"],"weight":"blend"}],"root":"mix"})");
        for (float weight : {.25f, .7f})
        {
            A::AnimGraphInstance blended;
            EVENT_CHECK(blended.Initialize(*graph, *f.Skeleton, *f.Mesh, M::Matrix4x4::Identity));
            EVENT_CHECK(blended.Parameters().SetFloat(0, weight));
            double previous = 0;
            for (float dt : {.25f, .375f, .25f})
            {
                double expectedX = 0, expectedZ = 0, expectedYaw = 0;
                for (unsigned i = 0; i < 2; ++i)
                {
                    // 始点の向きで逆回転した曲線差分を、成分ごとに線形加重する。
                    // 製品の差分・合成・ブレンド関数は期待値の計算に使わない。
                    const double c = std::cos(turn[i] * previous), s = std::sin(turn[i] * previous);
                    const double dx = vx[i] * dt, dz = vz[i] * dt;
                    const double w = i == 0 ? 1 - double(weight) : double(weight);
                    expectedX += w * (c * dx - s * dz);
                    expectedZ += w * (s * dx + c * dz);
                    expectedYaw += w * turn[i] * dt;
                }
                EVENT_CHECK(blended.Update(dt));
                const auto last = blended.GetLastRootMotionDelta();
                const auto consumed = blended.ConsumeRootMotion();
                for (const auto& actual : {last, consumed})
                {
                    EVENT_CHECK(std::fabs(actual.X - expectedX) < 1e-6 &&
                                std::fabs(actual.Z - expectedZ) < 1e-6 &&
                                std::fabs(actual.Yaw - expectedYaw) < 1e-6);
                }
                previous += dt;
            }
        }
    }

    void TestNoisyFootContactProperties()
    {
        EventFixture f;
        auto joints = f.Skeleton->GetJoints();
        joints.push_back(joints[0]);
        joints[1].Name = "Foot";
        joints[1].ParentIndex = 0;
        f.Skeleton->SetJoints(std::move(joints));
        auto clip = f.AClip->GetClip();
        clip.Channels.clear();
        S::SkeletalAnimationChannel foot;
        foot.JointIndex = 1;
        foot.Path = S::SkeletalAnimationPath::Translation;
        A::FootContactOptions options;
        options.SampleRate = 60;
        const double noiseAmplitude = options.VerticalSpeedThreshold / (4 * options.SampleRate);
        EVENT_CHECK(2 * noiseAmplitude * options.SampleRate <= options.VerticalSpeedThreshold / 2 &&
                    noiseAmplitude < options.HeightThreshold / 10);
        std::mt19937 random(0x464f4f54);
        for (unsigned frame = 0; frame <= 60; ++frame)
        {
            // 真の接地は[12,24]と[36,48]コマ。間は明確な速度で上げ下げする。
            float height = .1f;
            if (frame < 12)
            {
                height += .2f * float(12 - frame) / 12;
            }
            else if (frame > 24 && frame < 36)
            {
                height += .2f * float(frame <= 30 ? frame - 24 : 36 - frame) / 6;
            }
            else if (frame > 48)
            {
                height += .2f * float(frame - 48) / 12;
            }
            const float noise = float(noiseAmplitude * float(int(random() % 2001) - 1000) / 1000);
            foot.Samples.push_back({float(frame) / 60, {0, height + noise, 0, 0}});
        }
        clip.Channels.push_back(std::move(foot));
        f.AClip->SetClip(std::move(clip));
        A::SkeletalPoseContext context;
        EVENT_CHECK(A::SkeletalPoseBuilder::Prepare(*f.Skeleton, *f.AClip, *f.Mesh,
                                                   M::Matrix4x4::Identity, context));
        const A::FootContactSpec spec{1, Identity("Foot.Contact")};
        A::FootContactReport contact;
        EVENT_CHECK(A::AnalyzeFootContacts(context, *f.AClip, {&spec, 1}, options, contact));
        EVENT_CHECK(contact.Samples == 61 && contact.Windows.size() == 2);
        const unsigned starts[] = {12, 36}, ends[] = {24, 48};
        for (unsigned i = 0; i < 2; ++i)
        {
            // 時刻のfloat丸めだけを足し、許容差は解析の1コマに固定する。
            const double tolerance = 1 / options.SampleRate + 1e-7;
            EVENT_CHECK(contact.Windows[i].Name == spec.Name &&
                        std::fabs(contact.Windows[i].Start - starts[i] / options.SampleRate) <= tolerance &&
                        std::fabs(contact.Windows[i].End - ends[i] / options.SampleRate) <= tolerance);
        }
    }
} // 無名名前空間

void TestAnimEventProperties()
{
    TestPartitionInvariant();
    TestTraversalBoundaries();
    TestWeightAndInterruption();
    TestDeliveryOverflow();
    TestMetadataLoadingProperties();
    TestRootMotionProperties();
    TestNoisyFootContactProperties();
    std::puts("AnimEventPropertyTest PASS");
}
