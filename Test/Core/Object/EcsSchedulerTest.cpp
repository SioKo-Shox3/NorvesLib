#include "Debug/DebugConfig.h"
#include "Debug/Stats.h"
#include "Component/TickDispatch.h"
#include "Ecs/EcsScheduler.h"
#include "Thread/JobSystem.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <thread>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Ecs;
namespace NvDebug = NorvesLib::Debug;

namespace
{
    // 列の型の名前は他のテストと重ならないように Sc を付ける（TypeRegistry は型の名前で引く）
    struct ScHeavyA
    {
        uint32_t V[240];
    };

    struct ScHeavyB
    {
        uint32_t V[240];
    };

    struct ScPos
    {
        float X;
        float Y;
    };

    struct ScAux
    {
        uint32_t V;
    };

    template <typename T>
    TypeId TypeOf()
    {
        return EcsColumnInfoOf<T>().Type;
    }

    template <typename T>
    EcsArchetypeId ArchetypeOf(EcsWorld &world)
    {
        const EcsColumnInfo columns[] = {EcsColumnInfoOf<T>()};
        return world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(columns, 1));
    }

    /// チャンクがちょうど chunkCount 個になるまで行を作る
    template <typename T>
    EcsArchetypeId FillChunks(EcsWorld &world, uint32_t chunkCount)
    {
        const EcsArchetypeId archetype = ArchetypeOf<T>(world);
        world.CreateEntities(archetype, world.GetArchetypeChunkCapacity(archetype) * chunkCount);
        assert(world.GetArchetypeChunks(archetype).size() == chunkCount);
        return archetype;
    }

    SystemDesc ChunkSystem(const char *name, EcsStageId stage, TypeId queryColumn, EcsChunkFn fn, void *user)
    {
        SystemDesc desc;
        desc.Name = name;
        desc.Stage = stage;
        desc.Thread = ThreadPolicy::WorkerThread;
        desc.Query.All.push_back(queryColumn);
        desc.RunChunk = fn;
        desc.User = user;
        return desc;
    }

    SystemDesc GameThreadSystem(const char *name, EcsStageId stage, EcsSystemFn fn, void *user)
    {
        SystemDesc desc;
        desc.Name = name;
        desc.Stage = stage;
        desc.Thread = ThreadPolicy::GameThreadOnly;
        desc.Run = fn;
        desc.User = user;
        return desc;
    }

    FILE *OpenFile(const char *path, const char *mode)
    {
        FILE *file = nullptr;
#ifdef _WIN32
        if (fopen_s(&file, path, mode) != 0)
        {
            return nullptr;
        }
#else
        file = std::fopen(path, mode);
#endif
        return file;
    }

    void SleepMs(int ms)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    uint32_t CurrentOsThreadId()
    {
#ifdef _WIN32
        return static_cast<uint32_t>(::GetCurrentThreadId());
#else
        return 0;
#endif
    }

    void NoopChunk(void *, const EcsChunkJob &)
    {
    }

    // ---- 衝突 ----

    struct ConflictProbe
    {
        /// システムごとの、いま走っているチャンクの数（同じシステムのチャンクは同時に走ってよい）
        std::atomic<int> Active[8];
        std::atomic<bool> bViolation{false};
        std::atomic<uint32_t> Calls[8];
        /// 列 A に触るシステムの添字と、書くかどうか
        bool bWriter[8] = {};
        bool bTouchesA[8] = {};

        ConflictProbe()
        {
            for (int i = 0; i < 8; ++i)
            {
                Active[i] = 0;
                Calls[i] = 0;
            }
        }

        /// 別のシステムが、書き込みを含む組み合わせで同時に走っていないか
        void CheckNoOverlap(int self)
        {
            for (int other = 0; other < 8; ++other)
            {
                if (other != self && bTouchesA[other] && Active[other].load() > 0 && (bWriter[self] || bWriter[other]))
                {
                    bViolation = true;
                }
            }
        }
    };

    struct ProbeUser
    {
        ConflictProbe *Probe = nullptr;
        int Index = 0;
    };

    void ProbeChunk(void *user, const EcsChunkJob &)
    {
        auto *probeUser = static_cast<ProbeUser *>(user);
        ConflictProbe &probe = *probeUser->Probe;
        ++probe.Active[probeUser->Index];
        probe.CheckNoOverlap(probeUser->Index);
        ++probe.Calls[probeUser->Index];
        SleepMs(1);
        probe.CheckNoOverlap(probeUser->Index);
        --probe.Active[probeUser->Index];
    }

    void CountChunk(void *user, const EcsChunkJob &)
    {
        auto *probeUser = static_cast<ProbeUser *>(user);
        ++probeUser->Probe->Calls[probeUser->Index];
        SleepMs(1);
    }

    void TestConflictingSystemsNeverOverlap()
    {
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Update");
        const EcsArchetypeId archetypeA = FillChunks<ScHeavyA>(world, 12);
        const EcsArchetypeId archetypeB = FillChunks<ScHeavyB>(world, 12);
        (void)archetypeA;
        (void)archetypeB;

        ConflictProbe probe;
        ProbeUser users[5];
        for (int i = 0; i < 5; ++i)
        {
            users[i].Probe = &probe;
            users[i].Index = i;
        }
        // A: A を書く  B: A を書く  C: A を読む  D: B だけを書く（独立）  E: A を読む
        probe.bWriter[0] = true;
        probe.bWriter[1] = true;
        probe.bTouchesA[0] = probe.bTouchesA[1] = probe.bTouchesA[2] = probe.bTouchesA[4] = true;

        const TypeId typeA = TypeOf<ScHeavyA>();
        const TypeId typeB = TypeOf<ScHeavyB>();
        SystemDesc desc = ChunkSystem("ProbeA", stage, typeA, ProbeChunk, &users[0]);
        desc.Writes.push_back(typeA);
        const EcsSystemId sysA = scheduler.RegisterSystem(desc);
        desc = ChunkSystem("ProbeB", stage, typeA, ProbeChunk, &users[1]);
        desc.Writes.push_back(typeA);
        const EcsSystemId sysB = scheduler.RegisterSystem(desc);
        desc = ChunkSystem("ProbeC", stage, typeA, ProbeChunk, &users[2]);
        desc.Reads.push_back(typeA);
        const EcsSystemId sysC = scheduler.RegisterSystem(desc);
        desc = ChunkSystem("ProbeD", stage, typeB, CountChunk, &users[3]);
        desc.Writes.push_back(typeB);
        const EcsSystemId sysD = scheduler.RegisterSystem(desc);
        desc = ChunkSystem("ProbeE", stage, typeA, ProbeChunk, &users[4]);
        desc.Reads.push_back(typeA);
        const EcsSystemId sysE = scheduler.RegisterSystem(desc);
        assert(sysA != kInvalidEcsSystem && sysB != kInvalidEcsSystem && sysC != kInvalidEcsSystem
               && sysD != kInvalidEcsSystem && sysE != kInvalidEcsSystem);

        // W-W・W-R は登録順に別の波へ、独立なものと R-R は同じ波に入る
        assert(scheduler.GetSystemWave(sysA) == 0);
        assert(scheduler.GetSystemWave(sysB) == 1);
        assert(scheduler.GetSystemWave(sysC) == 2);
        assert(scheduler.GetSystemWave(sysD) == 0);
        assert(scheduler.GetSystemWave(sysE) == 2);
        assert(scheduler.GetWaveCount(stage) == 3);

        for (int round = 0; round < 3; ++round)
        {
            assert(scheduler.RunStage(stage));
        }
        assert(!probe.bViolation.load());
        for (int i = 0; i < 5; ++i)
        {
            assert(probe.Calls[i].load() == 12u * 3u);
        }
    }

    // ---- 波の仕事の列 ----

    struct SpanProbe
    {
        std::atomic<uint32_t> Visits[64];
        std::atomic<uint32_t> Mismatch{0};
        EcsChunk *const *Chunks = nullptr;
        uint64_t Version = 0;
        float Delta = -1.0f;
        std::atomic<uint32_t> VersionMismatch{0};

        SpanProbe()
        {
            for (auto &visit : Visits)
            {
                visit = 0;
            }
        }
    };

    void SpanChunk(void *user, const EcsChunkJob &job)
    {
        auto *probe = static_cast<SpanProbe *>(user);
        ++probe->Visits[job.ChunkIndex];
        if (probe->Chunks[job.ChunkIndex] != job.Chunk || job.Archetype != job.Chunk->Archetype)
        {
            ++probe->Mismatch;
        }
        if (probe->Version == 0)
        {
            return;
        }
        if (job.Version != probe->Version)
        {
            ++probe->VersionMismatch;
        }
        if (job.DeltaTime != probe->Delta)
        {
            ++probe->VersionMismatch;
        }
        SleepMs(1);
    }

    void TestWaveSpansChunksOfAllSystems()
    {
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Update");
        const EcsArchetypeId archetypeA = FillChunks<ScHeavyA>(world, 40);
        const EcsArchetypeId archetypeB = FillChunks<ScHeavyB>(world, 30);

        SpanProbe probeA;
        SpanProbe probeB;
        probeA.Chunks = world.GetArchetypeChunks(archetypeA).data();
        probeB.Chunks = world.GetArchetypeChunks(archetypeB).data();

        SystemDesc desc = ChunkSystem("SpanA", stage, TypeOf<ScHeavyA>(), SpanChunk, &probeA);
        desc.Writes.push_back(TypeOf<ScHeavyA>());
        desc.TimeChannel = 1;
        scheduler.RegisterSystem(desc);
        desc = ChunkSystem("SpanB", stage, TypeOf<ScHeavyB>(), SpanChunk, &probeB);
        desc.Writes.push_back(TypeOf<ScHeavyB>());
        desc.TimeChannel = 3;
        scheduler.RegisterSystem(desc);
        assert(scheduler.GetWaveCount(stage) == 1);

        // システム 1 回ぶんごとに版が進む。登録順に A の版が先
        probeA.Version = world.GetVersion() + 1;
        probeB.Version = world.GetVersion() + 2;
        probeA.Delta = 0.5f;
        probeB.Delta = 0.0f;
        const float deltas[] = {0.25f, 0.5f, 0.75f};
        scheduler.ResetStats();
        assert(scheduler.RunStage(stage, Container::Span<const float>(deltas, 3)));

        // 2 つのシステムの 70 チャンクが、1 回の ParallelFor の仕事の列になる
        assert(scheduler.GetStats().ParallelForCalls == 1);
        assert(scheduler.GetStats().ChunkJobs == 70);
        assert(scheduler.GetStats().LastWaveJobs == 70);
        for (uint32_t i = 0; i < 40; ++i)
        {
            assert(probeA.Visits[i].load() == 1);
        }
        for (uint32_t i = 40; i < 64; ++i)
        {
            assert(probeA.Visits[i].load() == 0);
        }
        for (uint32_t i = 0; i < 30; ++i)
        {
            assert(probeB.Visits[i].load() == 1);
        }
        assert(probeA.Mismatch.load() == 0 && probeB.Mismatch.load() == 0);
        assert(probeA.VersionMismatch.load() == 0 && probeB.VersionMismatch.load() == 0);

        // 対象が無いシステムだけの波は ParallelFor を呼ばない
        EcsWorld emptyWorld;
        SystemScheduler emptyScheduler(emptyWorld);
        const EcsStageId emptyStage = emptyScheduler.AddStage("Empty");
        emptyScheduler.RegisterSystem(ChunkSystem("EmptySys", emptyStage, TypeOf<ScHeavyA>(), NoopChunk, nullptr));
        assert(emptyScheduler.RunStage(emptyStage));
        assert(emptyScheduler.GetStats().ParallelForCalls == 0);
    }

    // ---- GameThreadOnly ----

    struct SoloProbe
    {
        std::atomic<int> ActiveChunks{0};
        std::atomic<uint32_t> ChunkCalls{0};
        std::thread::id CallerThread;
        std::thread::id GameSystemThread;
        int ActiveAtEntry = -1;
        int ActiveAfterSleep = -1;
        uint32_t GameSystemRuns = 0;
    };

    void SoloChunk(void *user, const EcsChunkJob &)
    {
        auto *probe = static_cast<SoloProbe *>(user);
        ++probe->ActiveChunks;
        ++probe->ChunkCalls;
        SleepMs(1);
        --probe->ActiveChunks;
    }

    void SoloGameThread(void *user, const EcsSystemContext &)
    {
        auto *probe = static_cast<SoloProbe *>(user);
        probe->GameSystemThread = std::this_thread::get_id();
        probe->ActiveAtEntry = probe->ActiveChunks.load();
        SleepMs(5);
        probe->ActiveAfterSleep = probe->ActiveChunks.load();
        ++probe->GameSystemRuns;
    }

    void TestGameThreadOnlyRunsAloneOnCallerThread()
    {
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Update");
        FillChunks<ScHeavyA>(world, 16);
        FillChunks<ScHeavyB>(world, 16);

        SoloProbe probe;
        probe.CallerThread = std::this_thread::get_id();
        SystemDesc desc = ChunkSystem("SoloBefore", stage, TypeOf<ScHeavyA>(), SoloChunk, &probe);
        desc.Writes.push_back(TypeOf<ScHeavyA>());
        const EcsSystemId before = scheduler.RegisterSystem(desc);
        const EcsSystemId gate = scheduler.RegisterSystem(GameThreadSystem("SoloGate", stage, SoloGameThread, &probe));
        desc = ChunkSystem("SoloAfter", stage, TypeOf<ScHeavyB>(), SoloChunk, &probe);
        desc.Writes.push_back(TypeOf<ScHeavyB>());
        const EcsSystemId after = scheduler.RegisterSystem(desc);

        // GameThreadOnly はすべてと衝突するので、前後と別の単独の波になる
        assert(scheduler.GetSystemWave(before) == 0);
        assert(scheduler.GetSystemWave(gate) == 1);
        assert(scheduler.GetSystemWave(after) == 2);

        for (int round = 0; round < 3; ++round)
        {
            assert(scheduler.RunStage(stage));
        }
        assert(probe.GameSystemRuns == 3);
        assert(probe.GameSystemThread == probe.CallerThread);
        assert(probe.ActiveAtEntry == 0 && probe.ActiveAfterSleep == 0);
        assert(probe.ChunkCalls.load() == 32u * 3u);
    }

    // ---- 登録の拒否 ----

    void NoopSystem(void *, const EcsSystemContext &)
    {
    }

    void TestRegistrationRejections()
    {
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId early = scheduler.AddStage("Early");
        const EcsStageId late = scheduler.AddStage("Late");
        assert(early == 0 && late == 1);
        assert(scheduler.AddStage("Early") == kInvalidEcsStage);
        assert(scheduler.AddStage("") == kInvalidEcsStage);
        assert(scheduler.AddStage(nullptr) == kInvalidEcsStage);
        assert(scheduler.FindStage("Late") == late);

        // RenderThreadOnly は登録を拒む
        SystemDesc render = GameThreadSystem("RenderOnly", early, NoopSystem, nullptr);
        render.Thread = ThreadPolicy::RenderThreadOnly;
        assert(scheduler.RegisterSystem(render) == kInvalidEcsSystem);
        // 関数が足りない・名前が空・段が無い
        assert(scheduler.RegisterSystem(GameThreadSystem("NoRun", early, nullptr, nullptr)) == kInvalidEcsSystem);
        assert(scheduler.RegisterSystem(ChunkSystem("NoChunkFn", early, TypeOf<ScPos>(), nullptr, nullptr)) == kInvalidEcsSystem);
        assert(scheduler.RegisterSystem(GameThreadSystem("", early, NoopSystem, nullptr)) == kInvalidEcsSystem);
        assert(scheduler.RegisterSystem(GameThreadSystem("NoStage", 9, NoopSystem, nullptr)) == kInvalidEcsSystem);
        assert(scheduler.GetSystemCount() == 0);

        const EcsSystemId a = scheduler.RegisterSystem(GameThreadSystem("A", early, NoopSystem, nullptr));
        assert(a == 0);
        // 同じ名前は拒む
        assert(scheduler.RegisterSystem(GameThreadSystem("A", early, NoopSystem, nullptr)) == kInvalidEcsSystem);
        // After が未登録・後ろの段のシステムは拒む
        SystemDesc dependent = GameThreadSystem("Unknown", early, NoopSystem, nullptr);
        dependent.After.push_back(7);
        assert(scheduler.RegisterSystem(dependent) == kInvalidEcsSystem);
        const EcsSystemId lateSystem = scheduler.RegisterSystem(GameThreadSystem("LateSys", late, NoopSystem, nullptr));
        dependent = GameThreadSystem("ForwardStage", early, NoopSystem, nullptr);
        dependent.After.push_back(lateSystem);
        assert(scheduler.RegisterSystem(dependent) == kInvalidEcsSystem);
        assert(scheduler.GetSystemCount() == 2);

        // 循環する After を拒む
        dependent = GameThreadSystem("B", early, NoopSystem, nullptr);
        dependent.After.push_back(a);
        const EcsSystemId b = scheduler.RegisterSystem(dependent);
        assert(b != kInvalidEcsSystem);
        dependent = GameThreadSystem("C", early, NoopSystem, nullptr);
        dependent.After.push_back(b);
        const EcsSystemId c = scheduler.RegisterSystem(dependent);
        assert(c != kInvalidEcsSystem);
        assert(!scheduler.AddAfter(a, b));
        assert(!scheduler.AddAfter(a, c));
        assert(!scheduler.AddAfter(b, c));
        assert(!scheduler.AddAfter(a, a));
        assert(!scheduler.AddAfter(a, 99));
        // 後ろの段のシステムの後ろには付けられない。前の段なら付けられる
        assert(!scheduler.AddAfter(a, lateSystem));
        assert(scheduler.AddAfter(lateSystem, a));
        // すでにある辺は成功のまま。拒んだ辺は残らない
        assert(scheduler.AddAfter(b, a));
        assert(scheduler.GetSystemWave(a) == 0 && scheduler.GetSystemWave(b) == 1 && scheduler.GetSystemWave(c) == 2);

        // 登録順と逆の After も、循環しなければ受け入れて順序に従う（衝突しない 2 つのチャンクのシステム）
        SystemDesc first = ChunkSystem("ReverseFirst", late, TypeOf<ScPos>(), NoopChunk, nullptr);
        first.Writes.push_back(TypeOf<ScPos>());
        const EcsSystemId e1 = scheduler.RegisterSystem(first);
        SystemDesc second = ChunkSystem("ReverseSecond", late, TypeOf<ScAux>(), NoopChunk, nullptr);
        second.Writes.push_back(TypeOf<ScAux>());
        const EcsSystemId e2 = scheduler.RegisterSystem(second);
        assert(e1 != kInvalidEcsSystem && e2 != kInvalidEcsSystem);
        assert(scheduler.GetSystemWave(e1) == 1 && scheduler.GetSystemWave(e2) == 1);
        assert(scheduler.AddAfter(e1, e2));
        assert(scheduler.GetSystemWave(e2) == 1 && scheduler.GetSystemWave(e1) == 2);
        assert(!scheduler.AddAfter(e2, e1));
        assert(scheduler.GetSystemWave(e2) == 1 && scheduler.GetSystemWave(e1) == 2);
    }

    // ---- 段の中の構造変更 ----

    struct StructuralProbe
    {
        EcsWorld *World = nullptr;
        EcsArchetypeId CreateArchetype = kInvalidEcsArchetype;
        uint32_t EntityCountBefore = 0;
        uint32_t ObservedCount = 0;
        uint32_t ObservedSecondCount = 0;
        EcsEntity Victim;
        bool bVictimAliveInStage = false;
        uint32_t Base = 0;
        std::atomic<bool> bThrow{false};
    };

    void CreateChunk(void *user, const EcsChunkJob &job)
    {
        auto *probe = static_cast<StructuralProbe *>(user);
        const ScAux aux{probe->Base + job.ChunkIndex};
        const EcsColumnValue values[] = {EcsColumnValue{TypeOf<ScAux>(), &aux, static_cast<uint32_t>(sizeof(ScAux))}};
        job.Commands->Create(probe->CreateArchetype, Container::Span<const EcsColumnValue>(values, 1));
        if (probe->bThrow.load() && job.ChunkIndex == 3)
        {
            throw 7;
        }
    }

    void ObserveAndDestroy(void *user, const EcsSystemContext &context)
    {
        auto *probe = static_cast<StructuralProbe *>(user);
        probe->ObservedCount = context.World->GetEntityCount();
        context.Commands->Destroy(probe->Victim);
        probe->bVictimAliveInStage = context.World->IsAlive(probe->Victim);
    }

    void ObserveAgain(void *user, const EcsSystemContext &context)
    {
        auto *probe = static_cast<StructuralProbe *>(user);
        probe->ObservedSecondCount = context.World->GetEntityCount();
        probe->bVictimAliveInStage = probe->bVictimAliveInStage && context.World->IsAlive(probe->Victim);
    }

    struct CountProbe
    {
        std::atomic<uint32_t> Rows{0};
    };

    void CountRowsChunk(void *user, const EcsChunkJob &job)
    {
        static_cast<CountProbe *>(user)->Rows += job.Chunk->Count;
    }

    void TestStructuralChangesWaitForSync()
    {
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Build");
        const EcsStageId nextStage = scheduler.AddStage("Use");
        FillChunks<ScHeavyA>(world, 10);
        FillChunks<ScHeavyB>(world, 6);
        const EcsArchetypeId auxArchetype = ArchetypeOf<ScAux>(world);

        StructuralProbe first;
        first.World = &world;
        first.CreateArchetype = auxArchetype;
        first.Base = 0;
        StructuralProbe second;
        second.World = &world;
        second.CreateArchetype = auxArchetype;
        second.Base = 1000;
        first.Victim = world.GetArchetypeChunks(ArchetypeOf<ScHeavyB>(world))[0]->GetEntities()[0];
        first.EntityCountBefore = world.GetEntityCount();

        // 2 つのシステムは衝突しないので同じ波に入る。再生は波の順ではなく登録順 → チャンクの順
        SystemDesc desc = ChunkSystem("CreateFirst", stage, TypeOf<ScHeavyA>(), CreateChunk, &first);
        desc.Writes.push_back(TypeOf<ScHeavyA>());
        scheduler.RegisterSystem(desc);
        desc = ChunkSystem("CreateSecond", stage, TypeOf<ScHeavyB>(), CreateChunk, &second);
        desc.Writes.push_back(TypeOf<ScHeavyB>());
        scheduler.RegisterSystem(desc);
        assert(scheduler.GetWaveCount(stage) == 1);
        scheduler.RegisterSystem(GameThreadSystem("Observe", stage, ObserveAndDestroy, &first));
        scheduler.RegisterSystem(GameThreadSystem("ObserveAgain", stage, ObserveAgain, &first));

        CountProbe counted;
        scheduler.RegisterSystem(ChunkSystem("CountAux", nextStage, TypeOf<ScAux>(), CountRowsChunk, &counted));

        assert(scheduler.RunStage(stage));
        // 段の中の観測者には、積んだ作成も削除も見えない
        assert(first.ObservedCount == first.EntityCountBefore);
        assert(first.ObservedSecondCount == first.EntityCountBefore);
        assert(first.bVictimAliveInStage);
        // 同期点の後は見える
        assert(scheduler.GetLastReplayResult().Created == 16);
        assert(world.GetEntityCount() == first.EntityCountBefore + 16 - 1);
        assert(!world.IsAlive(first.Victim));

        // 作った行の順は、システムの登録順 → チャンクの順（0..9 のあとに 1000..1005）
        Container::VariableArray<uint32_t> values;
        for (EcsChunk *chunk : world.GetArchetypeChunks(auxArchetype))
        {
            const auto *column = static_cast<const ScAux *>(world.GetChunkColumn(chunk, 0));
            for (uint32_t row = 0; row < chunk->Count; ++row)
            {
                values.push_back(column[row].V);
            }
        }
        assert(values.size() == 16);
        for (uint32_t i = 0; i < 10; ++i)
        {
            assert(values[i] == i);
        }
        for (uint32_t i = 0; i < 6; ++i)
        {
            assert(values[10 + i] == 1000 + i);
        }

        // 次の段には見える
        assert(scheduler.RunStage(nextStage));
        assert(counted.Rows.load() == 16);

        // 例外を投げたら積んだコマンドを捨てて投げ直し、状態は戻る。次の実行は正しく動く
        const uint32_t countBefore = world.GetEntityCount();
        first.bThrow = true;
        first.Victim = EcsEntity{};
        bool bCaught = false;
        try
        {
            scheduler.RunStage(stage);
        }
        catch (int value)
        {
            bCaught = value == 7;
        }
        assert(bCaught);
        assert(!scheduler.IsRunning());
        assert(world.GetEntityCount() == countBefore);
        first.bThrow = false;
        assert(scheduler.RunStage(stage));
        assert(world.GetEntityCount() == countBefore + 16);
    }

    // ---- 更新への参加 ----

    struct JoinProbe
    {
        std::atomic<uint32_t> Joined{0};
        std::atomic<uint32_t> Skipped{0};
        std::atomic<uint32_t> FullChunks{0};

        void Reset()
        {
            Joined = 0;
            Skipped = 0;
            FullChunks = 0;
        }
    };

    /// 参加している行だけを数える（AllRowsJoined のチャンクは行ごとの判定を省く）
    void CountJoinedChunk(void *user, const EcsChunkJob &job)
    {
        auto *probe = static_cast<JoinProbe *>(user);
        if (job.AllRowsJoined())
        {
            for (uint32_t row = 0; row < job.Chunk->Count; ++row)
            {
                assert(job.IsRowJoined(row));
            }
            probe->Joined += job.Chunk->Count;
            ++probe->FullChunks;
            return;
        }
        for (uint32_t row = 0; row < job.Chunk->Count; ++row)
        {
            if (job.IsRowJoined(row))
            {
                ++probe->Joined;
            }
            else
            {
                ++probe->Skipped;
            }
        }
    }

    /// ScAux の列を持つ行のうち、参加の番号が serial 以下の数と、それより大きい数（World の走査で数える）
    void CountAuxRowsBySerial(EcsWorld &world, uint64_t serial, uint32_t &joined, uint32_t &pending)
    {
        EcsQuery query;
        query.All.push_back(TypeOf<ScAux>());
        Container::VariableArray<EcsChunk *> chunks;
        world.CollectChunks(query, chunks);
        joined = 0;
        pending = 0;
        for (const EcsChunk *chunk : chunks)
        {
            for (uint32_t row = 0; row < chunk->Count; ++row)
            {
                (chunk->GetJoinSerials()[row] <= serial ? joined : pending) += 1;
            }
        }
    }

    void TestRowsCreatedAtSyncJoinAtNextCollection()
    {
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId build = scheduler.AddStage("Build");
        const EcsStageId use = scheduler.AddStage("Use");
        FillChunks<ScHeavyA>(world, 4);
        const EcsArchetypeId auxArchetype = ArchetypeOf<ScAux>(world);

        // スケジューラの後に作った行は「次の収集」（1 回目）から参加する
        Container::VariableArray<EcsEntity> oldRows;
        world.CreateEntities(auxArchetype, 5, &oldRows);
        for (const EcsEntity entity : oldRows)
        {
            assert(world.GetJoinSerial(entity) == 1);
        }
        assert(world.GetJoinSerial(EcsEntity{}) == 0);

        StructuralProbe creator;
        creator.CreateArchetype = auxArchetype;
        SystemDesc desc = ChunkSystem("MakeRows", build, TypeOf<ScHeavyA>(), CreateChunk, &creator);
        desc.Writes.push_back(TypeOf<ScHeavyA>());
        scheduler.RegisterSystem(desc);
        JoinProbe joined;
        scheduler.RegisterSystem(ChunkSystem("CountJoined", use, TypeOf<ScAux>(), CountJoinedChunk, &joined));

        // 収集 1: 同じ収集の後ろの段は、同期点で作られた 4 行を呼ばない。存在は同期点から見える
        {
            EcsCollectionScope collection(scheduler);
            assert(scheduler.GetCollectionSerial() == 1);
            assert(scheduler.RunStage(build));
            assert(scheduler.GetLastReplayResult().Created == 4);
            assert(world.GetEntityCount() == 4 * world.GetArchetypeChunkCapacity(ArchetypeOf<ScHeavyA>(world)) + 9);
            assert(scheduler.RunStage(use));
            assert(joined.Joined.load() == 5 && joined.Skipped.load() == 4);
            assert(joined.FullChunks.load() == 0);
        }
        uint32_t joinedRows = 0;
        uint32_t pendingRows = 0;
        CountAuxRowsBySerial(world, 1, joinedRows, pendingRows);
        assert(joinedRows == 5 && pendingRows == 4);

        // 次の収集（2）から 9 行すべてが呼ばれる
        joined.Reset();
        assert(scheduler.RunStage(use));
        assert(scheduler.GetCollectionSerial() == 2);
        assert(joined.Joined.load() == 9 && joined.Skipped.load() == 0 && joined.FullChunks.load() == 1);

        // 同じフレームの複数の固定ステップ（1 つの外側の収集）は番号を共有する。どのステップで作った行も次の収集まで呼ばれない
        joined.Reset();
        {
            EcsCollectionScope collection(scheduler);
            assert(scheduler.GetCollectionSerial() == 3);
            assert(scheduler.RunStage(build));
            assert(scheduler.RunStage(build));
            assert(scheduler.GetCollectionSerial() == 3);
            assert(scheduler.RunStage(use));
            assert(joined.Joined.load() == 9 && joined.Skipped.load() == 8);
        }
        joined.Reset();
        assert(scheduler.RunStage(use));
        assert(joined.Joined.load() == 17 && joined.Skipped.load() == 0);

        // 収集 5 の途中で、入れ替え削除・列の追加・孤児の印があっても、動いた行は自分の参加の番号を保つ
        joined.Reset();
        {
            EcsCollectionScope collection(scheduler);
            assert(scheduler.GetCollectionSerial() == 5);
            assert(scheduler.RunStage(build));

            EcsQuery query;
            query.All.push_back(TypeOf<ScAux>());
            query.bIncludeOrphans = true;
            Container::VariableArray<EcsChunk *> chunks;
            world.CollectChunks(query, chunks);
            Container::VariableArray<EcsEntity> all;
            Container::VariableArray<uint64_t> serials;
            for (const EcsChunk *chunk : chunks)
            {
                for (uint32_t row = 0; row < chunk->Count; ++row)
                {
                    all.push_back(chunk->GetEntities()[row]);
                    serials.push_back(chunk->GetJoinSerials()[row]);
                }
            }
            assert(all.size() == 21);

            // 先頭の参加済みの行を消すと、末尾の（未参加の）行が穴へ入る
            const EcsEntity victim = all[0];
            assert(world.DestroyEntity(victim));
            // 参加済みの行 1 つ、未参加の行 1 つに列を足す（別のアーキタイプへ動く）。未参加の行はもう 1 つ孤児にする
            assert(world.AddColumnOf<ScPos>(all[1]));
            assert(world.AddColumnOf<ScPos>(all[20]));
            assert(world.MarkOrphan(all[19]));
            for (size_t i = 1; i < all.size(); ++i)
            {
                assert(world.IsAlive(all[i]));
                assert(world.GetJoinSerial(all[i]) == serials[i]);
            }
            assert(serials[1] <= 5 && serials[20] == 6 && serials[19] == 6);

            // 呼ばれるのは参加済みの行だけ。孤児と列を足された行も、参加の番号どおり（孤児は既定のクエリに出ない）
            assert(scheduler.RunStage(use));
            uint32_t expectJoined = 0;
            uint32_t expectPending = 0;
            CountAuxRowsBySerial(world, 5, expectJoined, expectPending);
            assert(joined.Joined.load() == expectJoined);
            assert(joined.Skipped.load() == expectPending);
            assert(expectPending == 3 && expectJoined == 16);
        }
    }

    // ---- 変更の記録 ----

    void WritePosChunk(void *user, const EcsChunkJob &job)
    {
        const EcsWorld &world = *job.World;
        const int32_t column = world.FindColumnIndex(job.Archetype, TypeOf<ScPos>());
        auto *positions = static_cast<ScPos *>(world.GetChunkColumn(job.Chunk, static_cast<uint32_t>(column)));
        for (uint32_t row = 0; row < job.Chunk->Count; ++row)
        {
            positions[row].X += 1.0f;
        }
        world.MarkWrittenRange(job.Chunk, static_cast<uint32_t>(column), 0, job.Chunk->Count, job.Version, *job.Changes);
        (void)user;
    }

    void TestSystemWritesAreCommittedToChangeLog()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ScPos>());
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Move");
        const EcsArchetypeId archetype = ArchetypeOf<ScPos>(world);
        const uint32_t rows = world.GetArchetypeChunkCapacity(archetype) * 3 + 5;
        world.CreateEntities(archetype, rows);
        assert(world.GetArchetypeChunks(archetype).size() == 4);

        const EcsConsumerId consumer = world.RegisterChangeConsumer(TypeOf<ScPos>());
        SystemDesc desc = ChunkSystem("MovePos", stage, TypeOf<ScPos>(), WritePosChunk, nullptr);
        desc.Writes.push_back(TypeOf<ScPos>());
        scheduler.RegisterSystem(desc);

        uint64_t previousVersion = 0;
        for (int round = 0; round < 2; ++round)
        {
            assert(scheduler.RunStage(stage));
            Container::VariableArray<EcsChangeEntry> changes;
            assert(world.ReadChanges(consumer, changes) == rows);
            // どの行もその回の版で 1 回ずつ。回ごとに版が進む
            const uint64_t version = changes[0].Version;
            assert(version > previousVersion);
            previousVersion = version;
            for (const EcsChangeEntry &entry : changes)
            {
                assert(entry.Version == version);
            }
        }
    }

    // ---- 順序付きの系と参加の番号 ----

    struct OrderedState;

    void OrderedCall(void *user, const EcsSystemContext &context);

    struct OrderedEntryUser
    {
        OrderedState *State = nullptr;
        int Id = 0;
    };

    struct OrderedState
    {
        SystemScheduler *Scheduler = nullptr;
        Container::VariableArray<int> Calls;
        OrderedEntryUser Users[32];
        EcsOrderedHandle Handles[32];
        EcsSystemId EarlySystem = kInvalidEcsSystem;
        EcsSystemId LateSystem = kInvalidEcsSystem;
        void (*OnCall)(OrderedState &, int) = nullptr;
        bool bRemoveResult = true;

        OrderedState()
        {
            for (int i = 0; i < 32; ++i)
            {
                Users[i].State = this;
                Users[i].Id = i;
            }
        }

        void Add(int id, EcsSystemId system, Component::ETickGroup group, int16_t priority, size_t ordinal)
        {
            OrderedEntryDesc desc;
            desc.Group = group;
            desc.Priority = priority;
            desc.Ordinal = ordinal;
            desc.User = &Users[id];
            desc.Run = &OrderedCall;
            Handles[id] = Scheduler->AddOrderedEntry(system, desc);
        }
    };

    void OrderedCall(void *user, const EcsSystemContext &)
    {
        auto *entry = static_cast<OrderedEntryUser *>(user);
        entry->State->Calls.push_back(entry->Id);
        if (entry->State->OnCall)
        {
            entry->State->OnCall(*entry->State, entry->Id);
        }
    }

    bool SameCalls(const OrderedState &state, std::initializer_list<int> expected)
    {
        if (state.Calls.size() != expected.size())
        {
            return false;
        }
        size_t i = 0;
        for (const int id : expected)
        {
            if (state.Calls[i++] != id)
            {
                return false;
            }
        }
        return true;
    }

    void RunBothStages(SystemScheduler &scheduler, const OrderedState &state, EcsStageId early, EcsStageId late)
    {
        (void)state;
        EcsCollectionScope collection(scheduler);
        assert(scheduler.RunStage(early));
        assert(scheduler.RunStage(late));
    }

    void TestEntriesJoinAtNextCollection()
    {
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId early = scheduler.AddStage("Early");
        const EcsStageId late = scheduler.AddStage("Late");
        OrderedState state;
        state.Scheduler = &scheduler;
        OrderedSystemDesc orderedDesc;
        orderedDesc.Name = "EarlyOrdered";
        orderedDesc.Stage = early;
        state.EarlySystem = scheduler.RegisterOrderedSystem(orderedDesc);
        orderedDesc.Name = "LateOrdered";
        orderedDesc.Stage = late;
        state.LateSystem = scheduler.RegisterOrderedSystem(orderedDesc);
        assert(state.EarlySystem != kInvalidEcsSystem && state.LateSystem != kInvalidEcsSystem);

        // 収集の外で足した呼び先は、次の収集（1 番）から参加する
        assert(scheduler.GetCollectionSerial() == 0 && scheduler.GetNextCollectionSerial() == 1);
        using Group = Component::ETickGroup;
        state.Add(1, state.EarlySystem, Group::Default, 0, 0);
        state.Add(2, state.EarlySystem, Group::Default, 0, 1);
        state.Add(3, state.LateSystem, Group::Default, 0, 2);
        assert(state.Handles[1].IsValid() && state.Handles[3].IsValid());
        assert(scheduler.GetOrderedEntryJoinSerial(state.Handles[1]) == 1);
        assert(scheduler.GetOrderedEntryJoinSerial(EcsOrderedHandle{}) == 0);

        // 1 の中で足した 10（同じ段、順は 2 の後）と 11（後の段）は、この収集では呼ばれない
        state.OnCall = [](OrderedState &s, int id) {
            if (id == 1)
            {
                s.Add(10, s.EarlySystem, Group::Default, 0, 5);
                s.Add(11, s.LateSystem, Group::Default, 0, 6);
                // 走っている収集（1 番）の中で足したので、参加の番号は次の収集（2 番）
                assert(s.Scheduler->GetOrderedEntryJoinSerial(s.Handles[10]) == 2);
                assert(s.Scheduler->GetOrderedEntryJoinSerial(s.Handles[11]) == 2);
            }
        };
        RunBothStages(scheduler, state, early, late);
        assert(SameCalls(state, {1, 2, 3}));
        assert(scheduler.GetCollectionSerial() == 1 && scheduler.GetNextCollectionSerial() == 2);

        // 次の収集では順の中に入る
        state.OnCall = nullptr;
        state.Calls.clear();
        RunBothStages(scheduler, state, early, late);
        assert(SameCalls(state, {1, 2, 10, 3, 11}));
        assert(scheduler.GetCollectionSerial() == 2);
        assert(scheduler.GetOrderedEntryJoinSerial(state.Handles[10]) == 2);
        // 収集の外で足した呼び先は、そのときの「次の収集」（3 番）から参加する
        state.Add(12, state.EarlySystem, Group::Default, 0, 7);
        assert(scheduler.GetOrderedEntryJoinSerial(state.Handles[12]) == 3);
        state.Calls.clear();
        RunBothStages(scheduler, state, early, late);
        assert(SameCalls(state, {1, 2, 10, 12, 3, 11}));
        assert(scheduler.GetCollectionSerial() == 3);

        // 単独の RunStage はそれぞれ 1 つの収集（番号が進む）。外側の収集の中では番号を共有する
        scheduler.RunStage(early);
        assert(scheduler.GetCollectionSerial() == 4);
        scheduler.RunStage(early);
        assert(scheduler.GetCollectionSerial() == 5);
        {
            EcsCollectionScope frame(scheduler);
            assert(frame.GetSerial() == 6);
            scheduler.RunStage(early);
            scheduler.RunStage(early);
            scheduler.RunStage(late);
            assert(scheduler.GetCollectionSerial() == 6 && scheduler.IsCollecting());
        }
        assert(!scheduler.IsCollecting());
        assert(scheduler.GetCollectionSerial() == 6 && scheduler.GetNextCollectionSerial() == 7);
    }

    void TestOrderRebuildsOnlyOnChange()
    {
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId early = scheduler.AddStage("Early");
        const EcsStageId late = scheduler.AddStage("Late");
        OrderedState state;
        state.Scheduler = &scheduler;
        OrderedSystemDesc orderedDesc;
        orderedDesc.Name = "EarlyOrdered";
        orderedDesc.Stage = early;
        state.EarlySystem = scheduler.RegisterOrderedSystem(orderedDesc);
        orderedDesc.Name = "LateOrdered";
        orderedDesc.Stage = late;
        state.LateSystem = scheduler.RegisterOrderedSystem(orderedDesc);
        using Group = Component::ETickGroup;
        state.Add(1, state.EarlySystem, Group::Default, 0, 0);
        state.Add(2, state.EarlySystem, Group::Default, 0, 1);
        state.Add(3, state.LateSystem, Group::Default, 0, 2);

        RunBothStages(scheduler, state, early, late);
        const uint32_t rebuilds = scheduler.GetStats().OrderRebuilds;
        assert(rebuilds == 2);
        for (int i = 0; i < 4; ++i)
        {
            RunBothStages(scheduler, state, early, late);
        }
        assert(scheduler.GetStats().OrderRebuilds == rebuilds);

        // 付け替えは次の収集から。この収集の順は変わらない
        state.Calls.clear();
        state.OnCall = [](OrderedState &s, int id) {
            if (id == 1)
            {
                assert(s.Scheduler->SetOrderedEntryKey(s.Handles[2], Group::Default, -5));
            }
        };
        RunBothStages(scheduler, state, early, late);
        assert(SameCalls(state, {1, 2, 3}));
        state.OnCall = nullptr;
        state.Calls.clear();
        RunBothStages(scheduler, state, early, late);
        assert(SameCalls(state, {2, 1, 3}));
        assert(scheduler.GetStats().OrderRebuilds == rebuilds + 1);

        // 外した呼び先は、走っている収集の中でもすぐに呼ばれなくなる
        state.Calls.clear();
        state.OnCall = [](OrderedState &s, int id) {
            if (id == 2)
            {
                s.bRemoveResult = s.Scheduler->RemoveOrderedEntry(s.Handles[1]);
            }
        };
        RunBothStages(scheduler, state, early, late);
        assert(state.bRemoveResult);
        assert(SameCalls(state, {2, 3}));
        state.OnCall = nullptr;
        // 古い持ち手は何もできない
        assert(!scheduler.RemoveOrderedEntry(state.Handles[1]));
        assert(!scheduler.SetOrderedEntryKey(state.Handles[1], Group::Default, 0));
        state.Calls.clear();
        RunBothStages(scheduler, state, early, late);
        assert(SameCalls(state, {2, 3}));

        // 空いた席を再利用しても古い持ち手は効かない
        const EcsOrderedHandle stale = state.Handles[1];
        state.Add(4, state.EarlySystem, Group::Default, 0, 9);
        assert(!scheduler.RemoveOrderedEntry(stale));
        state.Calls.clear();
        RunBothStages(scheduler, state, early, late);
        assert(SameCalls(state, {2, 4, 3}));
    }

    struct KeyRecord
    {
        Component::ETickGroup Group;
        int16_t Priority;
        size_t Ordinal;
    };

    struct KeyUser
    {
        Container::VariableArray<uint32_t> *Calls = nullptr;
        uint32_t Index = 0;
    };

    void KeyCall(void *user, const EcsSystemContext &)
    {
        auto *entry = static_cast<KeyUser *>(user);
        entry->Calls->push_back(entry->Index);
    }

    uint64_t NextRandom(uint64_t &state)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    void TestOrderedOrderMatchesTickDispatchLess()
    {
        constexpr uint32_t Count = 300;
        uint64_t random = 0x9E3779B97F4A7C15ull;

        // 順番（Ordinal）は重ならない並びを混ぜて作る
        Container::VariableArray<size_t> ordinals;
        for (uint32_t i = 0; i < Count; ++i)
        {
            ordinals.push_back(i);
        }
        for (uint32_t i = Count - 1; i > 0; --i)
        {
            std::swap(ordinals[i], ordinals[NextRandom(random) % (i + 1)]);
        }
        Container::VariableArray<KeyRecord> keys;
        for (uint32_t i = 0; i < Count; ++i)
        {
            keys.push_back(KeyRecord{static_cast<Component::ETickGroup>(NextRandom(random) % Component::TickGroupCount),
                                     static_cast<int16_t>(static_cast<int>(NextRandom(random) % 7) - 3), ordinals[i]});
        }

        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Tick");
        OrderedSystemDesc orderedDesc;
        orderedDesc.Name = "AllGroups";
        orderedDesc.Stage = stage;
        const EcsSystemId all = scheduler.RegisterOrderedSystem(orderedDesc);
        const Component::TickGroupMask subsetMask = Component::TickGroupBit(Component::ETickGroup::Movement)
                                                  | Component::TickGroupBit(Component::ETickGroup::Camera);
        const EcsStageId stage2 = scheduler.AddStage("TickSubset");
        orderedDesc.Name = "Subset";
        orderedDesc.Stage = stage2;
        orderedDesc.Groups = subsetMask;
        const EcsSystemId subset = scheduler.RegisterOrderedSystem(orderedDesc);

        Container::VariableArray<uint32_t> allCalls;
        Container::VariableArray<uint32_t> subsetCalls;
        Container::VariableArray<KeyUser> allUsers(Count);
        Container::VariableArray<KeyUser> subsetUsers(Count);
        // 追加順は混ぜる（足した順は結果に影響しない）
        Container::VariableArray<uint32_t> addOrder;
        for (uint32_t i = 0; i < Count; ++i)
        {
            addOrder.push_back(i);
        }
        for (uint32_t i = Count - 1; i > 0; --i)
        {
            std::swap(addOrder[i], addOrder[NextRandom(random) % (i + 1)]);
        }
        for (const uint32_t index : addOrder)
        {
            allUsers[index] = KeyUser{&allCalls, index};
            subsetUsers[index] = KeyUser{&subsetCalls, index};
            OrderedEntryDesc entry;
            entry.Group = keys[index].Group;
            entry.Priority = keys[index].Priority;
            entry.Ordinal = keys[index].Ordinal;
            entry.Run = KeyCall;
            entry.User = &allUsers[index];
            assert(scheduler.AddOrderedEntry(all, entry).IsValid());
            entry.User = &subsetUsers[index];
            assert(scheduler.AddOrderedEntry(subset, entry).IsValid());
        }

        {
            EcsCollectionScope collection(scheduler);
            assert(scheduler.RunStage(stage));
            assert(scheduler.RunStage(stage2));
        }

        // 期待する順 = TickDispatchLess で並べた順
        Container::VariableArray<TickDispatchEntry> expected;
        for (uint32_t i = 0; i < Count; ++i)
        {
            expected.push_back(TickDispatchEntry{nullptr, nullptr, keys[i].Group, keys[i].Group, keys[i].Priority, keys[i].Ordinal});
        }
        Container::VariableArray<uint32_t> expectedIndex;
        for (uint32_t i = 0; i < Count; ++i)
        {
            expectedIndex.push_back(i);
        }
        std::sort(expectedIndex.begin(), expectedIndex.end(), [&expected](uint32_t a, uint32_t b) {
            return TickDispatchLess(expected[a], expected[b]);
        });
        assert(allCalls.size() == Count);
        for (uint32_t i = 0; i < Count; ++i)
        {
            assert(allCalls[i] == expectedIndex[i]);
        }
        Container::VariableArray<uint32_t> expectedSubset;
        for (const uint32_t index : expectedIndex)
        {
            if ((subsetMask & Component::TickGroupBit(keys[index].Group)) != 0)
            {
                expectedSubset.push_back(index);
            }
        }
        assert(!expectedSubset.empty() && expectedSubset.size() < Count);
        assert(subsetCalls.size() == expectedSubset.size());
        for (size_t i = 0; i < expectedSubset.size(); ++i)
        {
            assert(subsetCalls[i] == expectedSubset[i]);
        }

        // 同じ群・優先度・順番なら足した順
        EcsWorld tieWorld;
        SystemScheduler tieScheduler(tieWorld);
        const EcsStageId tieStage = tieScheduler.AddStage("Tie");
        OrderedSystemDesc tieDesc;
        tieDesc.Name = "Tie";
        tieDesc.Stage = tieStage;
        const EcsSystemId tie = tieScheduler.RegisterOrderedSystem(tieDesc);
        Container::VariableArray<uint32_t> tieCalls;
        KeyUser tieUsers[3] = {{&tieCalls, 0}, {&tieCalls, 1}, {&tieCalls, 2}};
        for (int i = 0; i < 3; ++i)
        {
            OrderedEntryDesc entry;
            entry.Run = KeyCall;
            entry.User = &tieUsers[i];
            tieScheduler.AddOrderedEntry(tie, entry);
        }
        assert(tieScheduler.RunStage(tieStage));
        assert(tieCalls.size() == 3 && tieCalls[0] == 0 && tieCalls[1] == 1 && tieCalls[2] == 2);
    }

    // ---- トレースの区間 ----

    struct TraceProbe
    {
        std::mutex Lock;
        Container::VariableArray<uint32_t> WorkThreads;
        uint32_t GateThread = 0;
        uint32_t SyncThread = 0;
        int SleepMs = 0;
    };

    void TraceWorkChunk(void *user, const EcsChunkJob &)
    {
        auto *probe = static_cast<TraceProbe *>(user);
        {
            std::lock_guard<std::mutex> guard(probe->Lock);
            probe->WorkThreads.push_back(CurrentOsThreadId());
        }
        if (probe->SleepMs > 0)
        {
            SleepMs(probe->SleepMs);
        }
    }

    void TraceGate(void *user, const EcsSystemContext &)
    {
        auto *probe = static_cast<TraceProbe *>(user);
        probe->GateThread = CurrentOsThreadId();
        SleepMs(3);
    }

    void TraceSync(void *user, EcsWorld &)
    {
        auto *probe = static_cast<TraceProbe *>(user);
        probe->SyncThread = CurrentOsThreadId();
        SleepMs(2);
    }

#if NORVES_ENABLE_STATS

    struct ScopeRow
    {
        Container::String Name;
        Container::String Category;
        uint64_t Frame = 0;
        uint64_t EndUs = 0;
        uint32_t Thread = 0;
        double DurationMs = 0.0;

        int64_t StartUs() const
        {
            return static_cast<int64_t>(EndUs) - static_cast<int64_t>(DurationMs * 1000.0);
        }
    };

    void SplitCsvLine(const char *line, Container::VariableArray<Container::String> &out)
    {
        out.clear();
        Container::String current;
        bool bQuoted = false;
        for (const char *p = line; *p && *p != '\n' && *p != '\r'; ++p)
        {
            if (bQuoted)
            {
                if (*p == '"')
                {
                    if (p[1] == '"')
                    {
                        current += '"';
                        ++p;
                    }
                    else
                    {
                        bQuoted = false;
                    }
                }
                else
                {
                    current += *p;
                }
            }
            else if (*p == '"')
            {
                bQuoted = true;
            }
            else if (*p == ',')
            {
                out.push_back(current);
                current = Container::String();
            }
            else
            {
                current += *p;
            }
        }
        out.push_back(current);
    }

    Container::VariableArray<ScopeRow> ReadScopeRows(const char *path)
    {
        Container::VariableArray<ScopeRow> rows;
        FILE *file = OpenFile(path, "rb");
        assert(file != nullptr);
        char buffer[4096];
        Container::VariableArray<Container::String> fields;
        while (std::fgets(buffer, sizeof(buffer), file))
        {
            SplitCsvLine(buffer, fields);
            if (fields.size() < 7 || std::strcmp(fields[0].c_str(), "Scope") != 0)
            {
                continue;
            }
            ScopeRow row;
            row.Frame = std::strtoull(fields[1].c_str(), nullptr, 10);
            row.EndUs = std::strtoull(fields[2].c_str(), nullptr, 10);
            row.Thread = static_cast<uint32_t>(std::strtoul(fields[3].c_str(), nullptr, 10));
            row.Category = fields[4];
            row.Name = fields[5];
            row.DurationMs = std::atof(fields[6].c_str());
            rows.push_back(row);
        }
        std::fclose(file);
        return rows;
    }

    Container::VariableArray<const ScopeRow *> RowsNamed(const Container::VariableArray<ScopeRow> &rows, const char *name)
    {
        Container::VariableArray<const ScopeRow *> found;
        for (const ScopeRow &row : rows)
        {
            if (std::strcmp(row.Name.c_str(), name) == 0)
            {
                found.push_back(&row);
            }
        }
        return found;
    }

    bool Contains(const Container::VariableArray<uint32_t> &values, uint32_t value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    void TestSchedulerRecordsScopesInTrace()
    {
        const char *tracePath = "EcsSchedulerTest.trace.csv";
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Trace");
        FillChunks<ScHeavyA>(world, 24);

        TraceProbe probe;
        probe.SleepMs = 2;
        SystemDesc desc = ChunkSystem("WorkSys", stage, TypeOf<ScHeavyA>(), TraceWorkChunk, &probe);
        desc.Writes.push_back(TypeOf<ScHeavyA>());
        scheduler.RegisterSystem(desc);
        scheduler.RegisterSystem(GameThreadSystem("GateSys", stage, TraceGate, &probe));
        scheduler.SetSyncHook(stage, TraceSync, &probe);

        // 名前は登録のときに決めた文字列で、フレームをまたいで同じ
        const EcsSystemId work = scheduler.FindSystem("WorkSys");
        const char *workName = scheduler.GetSystemScopeName(work);
        assert(std::strcmp(workName, "Ecs.Sys.WorkSys") == 0);
        assert(std::strcmp(scheduler.GetStageScopeName(stage), "Ecs.Stage.Trace") == 0);
        assert(std::strcmp(scheduler.GetSyncScopeName(stage), "Ecs.Sync.Trace") == 0);

        const uint32_t caller = CurrentOsThreadId();
        bool bWorkerSeen = false;
        for (int attempt = 0; attempt < 5 && !bWorkerSeen; ++attempt)
        {
            probe.WorkThreads.clear();
            assert(NvDebug::StatsManager::Get().StartTrace(Container::String(tracePath)));
            NvDebug::StatsManager::Get().BeginFrame(7, 0.016f);
            assert(scheduler.RunStage(stage));
            NvDebug::StatsManager::Get().EndFrame();
            NvDebug::StatsManager::Get().StopTrace();
            assert(scheduler.GetSystemScopeName(work) == workName);

            const Container::VariableArray<ScopeRow> rows = ReadScopeRows(tracePath);
            const auto stageRows = RowsNamed(rows, "Ecs.Stage.Trace");
            const auto syncRows = RowsNamed(rows, "Ecs.Sync.Trace");
            const auto gateRows = RowsNamed(rows, "Ecs.Sys.GateSys");
            const auto workRows = RowsNamed(rows, "Ecs.Sys.WorkSys");
            assert(stageRows.size() == 1 && syncRows.size() == 1 && gateRows.size() == 1);
            assert(workRows.size() == 24);
            for (const ScopeRow &row : rows)
            {
                assert(std::strcmp(row.Category.c_str(), "Ecs") == 0);
                assert(row.Frame == 7);
            }

            // 入れ子: 系と同期点の区間は段の区間の中にあり、同期点は系の後（計測の揺れを許す）
            constexpr int64_t Tolerance = 2000;
            const ScopeRow &stageRow = *stageRows[0];
            const ScopeRow &syncRow = *syncRows[0];
            int64_t lastSystemEnd = 0;
            auto expectInside = [&](const ScopeRow &inner) {
                assert(inner.StartUs() >= stageRow.StartUs() - Tolerance);
                assert(static_cast<int64_t>(inner.EndUs) <= static_cast<int64_t>(stageRow.EndUs) + Tolerance);
                assert(inner.DurationMs <= stageRow.DurationMs + 2.0);
            };
            expectInside(syncRow);
            expectInside(*gateRows[0]);
            lastSystemEnd = std::max(lastSystemEnd, static_cast<int64_t>(gateRows[0]->EndUs));
            for (const ScopeRow *row : workRows)
            {
                expectInside(*row);
                lastSystemEnd = std::max(lastSystemEnd, static_cast<int64_t>(row->EndUs));
            }
            assert(syncRow.StartUs() >= lastSystemEnd - Tolerance);
            // 系の仕事は合わせて、段の中の他の区間より長い（スリープ分）
            assert(gateRows[0]->DurationMs >= 2.5);
            assert(syncRow.DurationMs >= 1.5);

            // スレッドの番号: GameThreadOnly と同期点は呼び手、チャンクの仕事は走ったスレッド
            assert(gateRows[0]->Thread == caller && probe.GateThread == caller);
            assert(syncRow.Thread == caller && probe.SyncThread == caller);
            assert(probe.WorkThreads.size() == 24);
            for (const ScopeRow *row : workRows)
            {
                assert(Contains(probe.WorkThreads, row->Thread));
            }
            for (const uint32_t thread : probe.WorkThreads)
            {
                bool bFound = false;
                for (const ScopeRow *row : workRows)
                {
                    bFound = bFound || row->Thread == thread;
                }
                assert(bFound);
                bWorkerSeen = bWorkerSeen || thread != caller;
            }
        }
        // ワーカーが 1 つも仕事を取らなかったら、ワーカーのスレッドの番号の確認にならない
        assert(bWorkerSeen);
        std::remove(tracePath);
    }

    void TestTraceKeepsEveryChunkScopeBeyondFrameEventLimit()
    {
        const char *tracePath = "EcsSchedulerTest.burst.trace.csv";
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Burst");
        FillChunks<ScHeavyA>(world, 300);
        TraceProbe probe;
        scheduler.RegisterSystem(ChunkSystem("BurstSys", stage, TypeOf<ScHeavyA>(), TraceWorkChunk, &probe));

        assert(NvDebug::StatsManager::Get().StartTrace(Container::String(tracePath)));
        NvDebug::StatsManager::Get().BeginFrame(1, 0.016f);
        assert(scheduler.RunStage(stage));
        // フレームの記録に残るのは先頭の 256 件までだが、トレースには全件が書かれる
        assert(NvDebug::StatsManager::Get().GetFrameProfileSnapshot().Events.size() == NvDebug::Detail::MaxFrameProfileEvents);
        NvDebug::StatsManager::Get().EndFrame();
        NvDebug::StatsManager::Get().StopTrace();

        const Container::VariableArray<ScopeRow> rows = ReadScopeRows(tracePath);
        assert(RowsNamed(rows, "Ecs.Sys.BurstSys").size() == 300);
        assert(RowsNamed(rows, "Ecs.Stage.Burst").size() == 1);
        assert(RowsNamed(rows, "Ecs.Sync.Burst").size() == 1);
        std::remove(tracePath);
    }

#else

    void TestSchedulerRecordsNothingWithoutStats()
    {
        const char *tracePath = "EcsSchedulerTest.trace.csv";
        EcsWorld world;
        SystemScheduler scheduler(world);
        const EcsStageId stage = scheduler.AddStage("Trace");
        FillChunks<ScHeavyA>(world, 8);
        TraceProbe probe;
        scheduler.RegisterSystem(ChunkSystem("WorkSys", stage, TypeOf<ScHeavyA>(), TraceWorkChunk, &probe));
        scheduler.RegisterSystem(GameThreadSystem("GateSys", stage, TraceGate, &probe));

        // 統計が無効な構成では、トレースを始められず何も記録されない
        assert(!NvDebug::StatsManager::IsEnabled());
        assert(!NvDebug::StatsManager::Get().StartTrace(Container::String(tracePath)));
        assert(scheduler.RunStage(stage));
        assert(NvDebug::StatsManager::Get().GetFrameProfileSnapshot().Events.empty());
        FILE *file = OpenFile(tracePath, "rb");
        assert(file == nullptr);
    }

#endif
}

int main()
{
    NorvesLib::Thread::JobSystem::Get().Initialize(4);

    TestConflictingSystemsNeverOverlap();
    TestWaveSpansChunksOfAllSystems();
    TestGameThreadOnlyRunsAloneOnCallerThread();
    TestRegistrationRejections();
    TestStructuralChangesWaitForSync();
    TestRowsCreatedAtSyncJoinAtNextCollection();
    TestSystemWritesAreCommittedToChangeLog();
    TestEntriesJoinAtNextCollection();
    TestOrderRebuildsOnlyOnChange();
    TestOrderedOrderMatchesTickDispatchLess();
#if NORVES_ENABLE_STATS
    TestSchedulerRecordsScopesInTrace();
    TestTraceKeepsEveryChunkScopeBeyondFrameEventLimit();
#else
    TestSchedulerRecordsNothingWithoutStats();
#endif

    NorvesLib::Thread::JobSystem::Get().Shutdown();
    std::cout << "EcsSchedulerTest 合格\n";
    return 0;
}
