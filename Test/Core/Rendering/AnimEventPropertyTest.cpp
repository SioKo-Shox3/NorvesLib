// グラフ経由のイベント配送で、刻み・境界・閾値・溢れの契約を確かめる。
#include "Animation/AnimGraphInstance.h"
#include "Animation/SkeletonResource.h"
#include "Logging/Logger.h"
#include "Object/ResourceRegistry.h"
#include "Resource/SkinnedMeshResource.h"
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
} // 無名名前空間

void TestAnimEventProperties()
{
    TestPartitionInvariant();
    TestTraversalBoundaries();
    TestWeightAndInterruption();
    TestDeliveryOverflow();
    std::puts("AnimEventPropertyTest PASS");
}
