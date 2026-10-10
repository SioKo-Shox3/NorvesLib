#include "Debug/DebugConfig.h"
#include "Ecs/EcsCommandBuffer.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Ecs;

namespace
{
    struct CmPos
    {
        float X;
        float Y;
    };

    struct CmVel
    {
        float X;
    };

    struct CmTag
    {
        uint32_t Bits;
    };

    TypeId PosType()
    {
        return EcsColumnInfoOf<CmPos>().Type;
    }

    TypeId VelType()
    {
        return EcsColumnInfoOf<CmVel>().Type;
    }

    EcsArchetypeId PosArchetype(EcsWorld &world)
    {
        const EcsColumnInfo columns[] = {EcsColumnInfoOf<CmPos>()};
        return world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(columns, 1));
    }

    EcsColumnValue ValueOf(const CmPos &pos)
    {
        EcsColumnValue value;
        value.Type = PosType();
        value.Data = &pos;
        value.Size = sizeof(CmPos);
        return value;
    }

    EcsEntity CreatePos(EcsWorld &world, EcsArchetypeId archetype, float x, float y)
    {
        const CmPos pos{x, y};
        const EcsColumnValue values[] = {ValueOf(pos)};
        return world.CreateEntity(archetype, Container::Span<const EcsColumnValue>(values, 1));
    }

    uint32_t BitsOf(float value)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    /// 全アーキタイプのチャンクの行を並び順どおりに写した列（実体の識別子と Pos・Vel の値）
    Container::VariableArray<uint64_t> Snapshot(const EcsWorld &world)
    {
        Container::VariableArray<uint64_t> out;
        for (uint32_t a = 0; a < world.GetArchetypeCount(); ++a)
        {
            out.push_back(0xA0000000ull | a);
            const Container::Span<EcsChunk *const> chunks = world.GetArchetypeChunks(a);
            for (EcsChunk *chunk : chunks)
            {
                const int32_t posColumn = world.FindColumnIndex(a, PosType());
                const int32_t velColumn = world.FindColumnIndex(a, VelType());
                for (uint32_t row = 0; row < chunk->Count; ++row)
                {
                    const EcsEntity entity = chunk->GetEntities()[row];
                    out.push_back((static_cast<uint64_t>(entity.Index) << 32) | entity.Generation);
                    if (posColumn >= 0)
                    {
                        const CmPos pos = static_cast<const CmPos *>(world.GetChunkColumn(chunk, static_cast<uint32_t>(posColumn)))[row];
                        out.push_back((static_cast<uint64_t>(BitsOf(pos.X)) << 32) | BitsOf(pos.Y));
                    }
                    if (velColumn >= 0)
                    {
                        const CmVel vel = static_cast<const CmVel *>(world.GetChunkColumn(chunk, static_cast<uint32_t>(velColumn)))[row];
                        out.push_back(0xB000000000000000ull | BitsOf(vel.X));
                    }
                }
            }
        }
        return out;
    }

    constexpr uint32_t kJobs = 64;
    constexpr uint32_t kPreEntities = 16;

    /// 仕事 job が自分のバッファへ積む（共有の World には触れない）
    void RecordJob(EcsCommandBuffer &buffer, uint32_t job, EcsArchetypeId archetype, const Container::VariableArray<EcsEntity> &pre)
    {
        const float j = static_cast<float>(job);
        const CmPos first{j, 1000.0f + j};
        const EcsColumnValue firstValue[] = {ValueOf(first)};
        buffer.Create(archetype, Container::Span<const EcsColumnValue>(firstValue, 1));

        const CmPos second{j, 2000.0f + j};
        const EcsColumnValue secondValue[] = {ValueOf(second)};
        const EcsCreateTicket ticket = buffer.Create(archetype, Container::Span<const EcsColumnValue>(secondValue, 1));
        if ((job & 1) != 0)
        {
            // 奇数の仕事は 2 つ目の作成を取り消す（その札を対象にした書き込みも一緒に捨てられる）
            assert(buffer.CancelCreate(ticket));
            buffer.WriteOf<CmPos>(ticket, CmPos{-1.0f, -1.0f});
        }

        buffer.WriteOf<CmPos>(pre[job % kPreEntities], CmPos{j, -1.0f});
        if (job % 7 == 0)
        {
            buffer.Destroy(pre[(job / 7) % kPreEntities]);
        }
        if (job % 5 == 0)
        {
            buffer.AddColumnOf<CmVel>(pre[(job + 3) % kPreEntities], CmVel{j});
        }
    }

    struct RunOutput
    {
        Container::VariableArray<uint64_t> State;
        EcsReplayResult Result;
    };

    /// threadCount 本のスレッドで、仕事を取る順を reverse で変えて積み、番号順に再生した結果
    RunOutput RunWithThreads(uint32_t threadCount, bool reverse)
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PosArchetype(world);
        Container::VariableArray<EcsEntity> pre;
        for (uint32_t i = 0; i < kPreEntities; ++i)
        {
            pre.push_back(CreatePos(world, archetype, static_cast<float>(i), 0.0f));
            assert(!pre.back().IsNull());
        }
        // 並列の中で型を引かないように、先に登録しておく
        (void)EcsRegisterColumn<CmVel>();

        Container::VariableArray<EcsCommandBuffer> buffers;
        for (uint32_t job = 0; job < kJobs; ++job)
        {
            buffers.push_back(EcsCommandBuffer(job));
        }

        const uint32_t unregisteredBefore = EcsGetUnregisteredColumnReportCount();
        {
            EcsParallelScope scope;
            std::atomic<uint32_t> cursor{0};
            Container::VariableArray<std::thread> threads;
            for (uint32_t t = 0; t < threadCount; ++t)
            {
                threads.emplace_back(
                    [&]
                    {
                        for (;;)
                        {
                            const uint32_t claimed = cursor.fetch_add(1);
                            if (claimed >= kJobs)
                            {
                                break;
                            }
                            const uint32_t job = reverse ? kJobs - 1 - claimed : claimed;
                            RecordJob(buffers[job], job, archetype, pre);
                        }
                    });
            }
            for (std::thread &thread : threads)
            {
                thread.join();
            }
        }
        assert(EcsGetUnregisteredColumnReportCount() == unregisteredBefore);

        Container::VariableArray<EcsCommandBuffer *> inJobOrder;
        for (uint32_t job = 0; job < kJobs; ++job)
        {
            inJobOrder.push_back(&buffers[job]);
        }
        RunOutput output;
        output.Result = EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(inJobOrder.data(), inJobOrder.size()));
        output.State = Snapshot(world);
        return output;
    }

    void TestReplayOrderIsIndependentOfThreadCount()
    {
        const RunOutput baseline = RunWithThreads(1, false);
        // 偶数の仕事は 2 つ、奇数の仕事は 1 つ作る（2 つ目は取り消し）
        assert(baseline.Result.Created == kJobs + kJobs / 2);
        assert(baseline.Result.CancelledCreates == kJobs / 2);
        // 奇数の仕事の取り消した作成を対象にした書き込みは、対象なしとして捨てる
        assert(baseline.Result.DiscardedStale >= kJobs / 2);
        assert(baseline.State.size() > 100);

        const uint32_t counts[] = {1, 2, 4, 8};
        for (uint32_t count : counts)
        {
            for (int reverse = 0; reverse < 2; ++reverse)
            {
                const RunOutput run = RunWithThreads(count, reverse != 0);
                assert(run.State == baseline.State);
                assert(run.Result.Applied == baseline.Result.Applied);
                assert(run.Result.Created == baseline.Result.Created);
                assert(run.Result.CancelledCreates == baseline.Result.CancelledCreates);
                assert(run.Result.DiscardedStale == baseline.Result.DiscardedStale);
                assert(run.Result.Rejected == baseline.Result.Rejected);
            }
        }
    }

    void TestOrderFollowsBufferSequence()
    {
        // 同じ実体への書き込みは、バッファの順の最後が残る（渡す順を変えれば結果も変わる）
        EcsWorld world;
        const EcsArchetypeId archetype = PosArchetype(world);
        const EcsEntity target = CreatePos(world, archetype, 0.0f, 0.0f);

        EcsCommandBuffer first(1);
        EcsCommandBuffer second(2);
        first.WriteOf<CmPos>(target, CmPos{1.0f, 1.0f});
        first.WriteOf<CmPos>(target, CmPos{3.0f, 3.0f});
        second.WriteOf<CmPos>(target, CmPos{2.0f, 2.0f});

        EcsCommandBuffer *order[] = {&second, &first};
        EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 2));
        assert(world.GetColumnValue<CmPos>(target)->X == 3.0f);

        first.Clear();
        second.Clear();
        first.WriteOf<CmPos>(target, CmPos{1.0f, 1.0f});
        second.WriteOf<CmPos>(target, CmPos{2.0f, 2.0f});
        EcsCommandBuffer *reversed[] = {&first, &second};
        EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(reversed, 2));
        assert(world.GetColumnValue<CmPos>(target)->X == 2.0f);
    }

    void TestCancelledReservationIsNotReplayed()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PosArchetype(world);

        EcsCommandBuffer buffer(7);
        const CmPos a{1.0f, 0.0f};
        const CmPos b{2.0f, 0.0f};
        const CmPos c{3.0f, 0.0f};
        const EcsColumnValue av[] = {ValueOf(a)};
        const EcsColumnValue bv[] = {ValueOf(b)};
        const EcsColumnValue cv[] = {ValueOf(c)};
        const EcsCreateTicket ticketA = buffer.Create(archetype, Container::Span<const EcsColumnValue>(av, 1));
        const EcsCreateTicket ticketB = buffer.Create(archetype, Container::Span<const EcsColumnValue>(bv, 1));
        const EcsCreateTicket ticketC = buffer.Create(archetype, Container::Span<const EcsColumnValue>(cv, 1));
        buffer.AddColumnOf<CmVel>(ticketA, CmVel{9.0f});
        buffer.AddColumnOf<CmVel>(ticketB, CmVel{9.0f});

        // 再生の前は実体を引けない。別のバッファの札・無効な札・二重の取り消しは false
        assert(buffer.ResolveCreate(ticketA).IsNull());
        EcsCreateTicket foreign = ticketB;
        foreign.Buffer = 8;
        assert(!buffer.CancelCreate(foreign));
        assert(!buffer.CancelCreate(EcsCreateTicket{}));
        assert(buffer.CancelCreate(ticketB));
        assert(!buffer.CancelCreate(ticketB));

        EcsCommandBuffer *order[] = {&buffer};
        const EcsReplayResult result = EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 1));
        assert(result.Created == 2);
        assert(result.CancelledCreates == 1);
        // 取り消した札を対象にした AddColumn は捨てる
        assert(result.DiscardedStale == 1);
        assert(world.GetEntityCount() == 2);
        assert(buffer.ResolveCreate(ticketB).IsNull());

        const EcsEntity entityA = buffer.ResolveCreate(ticketA);
        const EcsEntity entityC = buffer.ResolveCreate(ticketC);
        assert(world.IsAlive(entityA) && world.IsAlive(entityC));
        assert(world.GetColumnValue<CmPos>(entityA)->X == 1.0f);
        assert(world.GetColumnValue<CmPos>(entityC)->X == 3.0f);
        assert(world.GetColumnValue<CmVel>(entityA)->X == 9.0f);
        assert(world.GetColumnValue<CmVel>(entityC) == nullptr);

        // 再生の後の取り消しは false。二重には再生しない
        assert(!buffer.CancelCreate(ticketC));
        assert(buffer.IsReplayed());
        const EcsReplayResult again = EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 1));
        assert(again.Applied == 0 && again.Created == 0);
        assert(world.GetEntityCount() == 2);

        // Clear の後は札が無効になり、積み直せる
        buffer.Clear();
        assert(!buffer.IsReplayed() && buffer.Size() == 0);
        assert(buffer.ResolveCreate(ticketA).IsNull());
        buffer.Create(archetype);
        EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 1));
        assert(world.GetEntityCount() == 3);
    }

    void TestTicketFromBeforeClearDoesNotAffectNewReservation()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PosArchetype(world);

        EcsCommandBuffer buffer(7);
        const EcsCreateTicket oldTicket = buffer.Create(archetype);
        assert(buffer.CancelCreate(oldTicket));

        // Clear の後の新しい予約は、旧札と同じ Buffer・Index になる
        buffer.Clear();
        const EcsCreateTicket freshTicket = buffer.Create(archetype);
        assert(freshTicket.Buffer == oldTicket.Buffer && freshTicket.Index == oldTicket.Index);

        // 旧札では新しい予約を取り消せず、旧札宛てのコマンドは積んでも捨てられる
        assert(!buffer.CancelCreate(oldTicket));
        buffer.AddColumnOf<CmVel>(oldTicket, CmVel{1.0f});
        buffer.Destroy(oldTicket);

        EcsCommandBuffer *order[] = {&buffer};
        const EcsReplayResult result = EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 1));
        assert(result.Created == 1);
        assert(result.CancelledCreates == 0);
        assert(result.DiscardedStale == 2);
        assert(world.GetEntityCount() == 1);

        // 新予約の再生後も、旧札では解決できない（新札は解決できる）
        assert(buffer.ResolveCreate(oldTicket).IsNull());
        const EcsEntity created = buffer.ResolveCreate(freshTicket);
        assert(world.IsAlive(created));
        assert(world.GetColumnValue<CmVel>(created) == nullptr);
    }

    void TestStaleCommandsAreDiscarded()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PosArchetype(world);
        const EcsEntity old = CreatePos(world, archetype, 1.0f, 0.0f);

        // 積んだ後に実体が消え、同じ番号が世代を進めて再利用される
        EcsCommandBuffer buffer(1);
        buffer.Destroy(old);
        buffer.WriteOf<CmPos>(old, CmPos{99.0f, 99.0f});
        buffer.AddColumnOf<CmVel>(old, CmVel{1.0f});
        buffer.RemoveColumnOf<CmPos>(old);

        assert(world.DestroyEntity(old));
        const EcsEntity reused = CreatePos(world, archetype, 5.0f, 5.0f);
        assert(reused.Index == old.Index && reused.Generation != old.Generation);

        EcsCommandBuffer *order[] = {&buffer};
        const EcsReplayResult result = EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 1));
        assert(result.Applied == 0);
        assert(result.DiscardedStale == 4);
        assert(world.IsAlive(reused));
        assert(world.GetColumnValue<CmPos>(reused)->X == 5.0f);
        assert(world.GetColumnValue<CmVel>(reused) == nullptr);

        // 2 つのバッファが同じ実体を消すと、2 つ目は捨てる
        EcsCommandBuffer one(1);
        EcsCommandBuffer two(2);
        one.Destroy(reused);
        two.Destroy(reused);
        EcsCommandBuffer *both[] = {&one, &two};
        const EcsReplayResult twice = EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(both, 2));
        assert(twice.Applied == 1 && twice.DiscardedStale == 1);
        assert(world.GetEntityCount() == 0);
    }

    void TestRejectedCommandsChangeNothing()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PosArchetype(world);
        const EcsEntity entity = CreatePos(world, archetype, 1.0f, 2.0f);
        const uint32_t sizeBefore = world.GetEntityCount();

        EcsCommandBuffer buffer(1);
        // すでにある列の追加・無い列の除去・大きさの違う書き込みは、生きた対象でも拒まれる
        buffer.AddColumnOf<CmPos>(entity, CmPos{9.0f, 9.0f});
        buffer.RemoveColumnOf<CmVel>(entity);
        const CmTag wrongSize{1};
        buffer.Write(entity, PosType(), &wrongSize, sizeof(CmTag));
        // 列が無い型の初期値を持つ作成は、実体を作らない
        const CmVel vel{3.0f};
        EcsColumnValue badValue;
        badValue.Type = VelType();
        badValue.Data = &vel;
        badValue.Size = sizeof(CmVel);
        const EcsColumnValue badValues[] = {badValue};
        buffer.Create(archetype, Container::Span<const EcsColumnValue>(badValues, 1));

        EcsCommandBuffer *order[] = {&buffer};
        const EcsReplayResult result = EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 1));
        assert(result.Applied == 0 && result.Rejected == 4 && result.Created == 0);
        assert(world.GetEntityCount() == sizeBefore);
        assert(world.GetColumnValue<CmPos>(entity)->X == 1.0f && world.GetColumnValue<CmPos>(entity)->Y == 2.0f);
    }

    void TestReplayAdvancesVersionOnlyWithCommands()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PosArchetype(world);
        EcsCommandBuffer empty(1);
        EcsCommandBuffer *order[] = {&empty, nullptr};
        const uint64_t before = world.GetVersion();
        EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 2));
        assert(world.GetVersion() == before);

        EcsCommandBuffer filled(2);
        filled.Create(archetype);
        EcsCommandBuffer *withCommands[] = {&empty, &filled};
        EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(withCommands, 2));
        assert(world.GetVersion() == before + 1);
    }

    struct HookProbe
    {
        int Created = 0;
        int ColumnAdded = 0;
        int Destroyed = 0;
        int ColumnRemoved = 0;
        float LastValue = -1.0f;
        EcsEntity LastEntity;
        EcsWorld *World = nullptr;
        bool bTryStructural = false;
        int StructuralRefused = 0;
    };

    void ProbeHook(void *user, EcsEntity entity, const void *value, EcsHookReason reason)
    {
        HookProbe &probe = *static_cast<HookProbe *>(user);
        switch (reason)
        {
        case EcsHookReason::Create:
            ++probe.Created;
            break;
        case EcsHookReason::ColumnAdded:
            ++probe.ColumnAdded;
            break;
        case EcsHookReason::Destroy:
            ++probe.Destroyed;
            break;
        case EcsHookReason::ColumnRemoved:
            ++probe.ColumnRemoved;
            break;
        }
        probe.LastEntity = entity;
        std::memcpy(&probe.LastValue, value, sizeof(float));
        if (probe.bTryStructural && probe.World != nullptr)
        {
            // フックの中の構造変更は拒まれる
            const bool destroyed = probe.World->DestroyEntity(entity);
            const bool added = probe.World->AddColumnOf<CmTag>(entity, CmTag{1});
            const bool made = !probe.World->CreateEntity(PosArchetype(*probe.World)).IsNull();
            if (!destroyed && !added && !made)
            {
                ++probe.StructuralRefused;
            }
        }
    }

    void TestHooksDistinguishDestroyFromMove()
    {
        EcsWorld world;
        HookProbe pos;
        HookProbe vel;
        EcsColumnHooks posHooks;
        posHooks.OnCreate = &ProbeHook;
        posHooks.OnDestroy = &ProbeHook;
        posHooks.User = &pos;
        EcsColumnHooks velHooks = posHooks;
        velHooks.User = &vel;
        assert(world.SetColumnHooks(PosType(), posHooks));
        assert(world.SetColumnHooks(VelType(), velHooks));
        assert(!world.SetColumnHooks(InvalidSchemaId, posHooks));

        const EcsArchetypeId archetype = PosArchetype(world);
        // 作成: 初期値を入れた後に OnCreate が値を見る
        const EcsEntity entity = CreatePos(world, archetype, 4.0f, 0.0f);
        assert(pos.Created == 1 && pos.LastValue == 4.0f && pos.LastEntity == entity);
        const EcsEntity other = CreatePos(world, archetype, 6.0f, 0.0f);
        assert(pos.Created == 2);

        // 列の追加は Move: 足した列だけ OnCreate（ColumnAdded）、残る Pos には何も呼ばない
        assert(world.AddColumnOf<CmVel>(entity, CmVel{7.0f}));
        assert(vel.ColumnAdded == 1 && vel.Created == 0 && vel.LastValue == 7.0f);
        assert(pos.Created == 2 && pos.ColumnAdded == 0 && pos.Destroyed == 0 && pos.ColumnRemoved == 0);

        // 孤児の印も Move
        assert(world.MarkOrphan(entity));
        assert(pos.Destroyed == 0 && pos.Created == 2 && vel.Destroyed == 0 && vel.ColumnAdded == 1);

        // 列の除去は Move: 外す列だけ OnDestroy（ColumnRemoved）。値は消える前に見える
        assert(world.RemoveColumnOf<CmVel>(entity));
        assert(vel.ColumnRemoved == 1 && vel.Destroyed == 0 && vel.LastValue == 7.0f);
        assert(pos.Destroyed == 0 && pos.ColumnRemoved == 0);

        // 入れ替え削除で動いた other のフックは呼ばれない。消えた entity の Pos だけ Destroy
        assert(world.DestroyEntity(entity));
        assert(pos.Destroyed == 1 && pos.LastValue == 4.0f && pos.LastEntity == entity);
        assert(pos.Created == 2);
        assert(world.IsAlive(other));

        // 死んだ実体・フックなしの列は呼ばない
        assert(!world.DestroyEntity(entity));
        assert(pos.Destroyed == 1);

        // バッチの作成は行ごとに 1 回
        Container::VariableArray<EcsEntity> batch;
        assert(world.CreateEntities(archetype, 100, &batch) == 100);
        assert(pos.Created == 102 && batch.size() == 100);

        // フックの中の構造変更は拒まれる
        pos.bTryStructural = true;
        pos.World = &world;
        const uint32_t beforeCount = world.GetEntityCount();
        const EcsEntity probeEntity = CreatePos(world, archetype, 8.0f, 0.0f);
        assert(pos.StructuralRefused == 1);
        assert(world.GetEntityCount() == beforeCount + 1);
        assert(world.DestroyEntity(probeEntity));
        assert(pos.StructuralRefused == 2);
        pos.bTryStructural = false;

        // 外したフックは呼ばない
        assert(world.SetColumnHooks(PosType(), EcsColumnHooks{}));
        const int createdBefore = pos.Created;
        CreatePos(world, archetype, 1.0f, 1.0f);
        assert(pos.Created == createdBefore);
    }

    void TestCommandMoveDoesNotCallDestroy()
    {
        EcsWorld world;
        HookProbe pos;
        HookProbe vel;
        EcsColumnHooks hooks;
        hooks.OnCreate = &ProbeHook;
        hooks.OnDestroy = &ProbeHook;
        hooks.User = &pos;
        world.SetColumnHooks(PosType(), hooks);
        hooks.User = &vel;
        world.SetColumnHooks(VelType(), hooks);

        const EcsArchetypeId archetype = PosArchetype(world);
        const EcsEntity entity = CreatePos(world, archetype, 2.0f, 0.0f);
        assert(pos.Created == 1);

        // コマンドの再生でも同じ: 列の追加と除去（Move）で Pos の Destroy は呼ばれない
        EcsCommandBuffer buffer(1);
        buffer.AddColumnOf<CmVel>(entity, CmVel{5.0f});
        buffer.RemoveColumnOf<CmVel>(entity);
        buffer.AddColumnOf<CmVel>(entity, CmVel{6.0f});
        EcsCommandBuffer *order[] = {&buffer};
        const EcsReplayResult result = EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(order, 1));
        assert(result.Applied == 3);
        assert(pos.Destroyed == 0 && pos.ColumnRemoved == 0 && pos.Created == 1);
        assert(vel.ColumnAdded == 2 && vel.ColumnRemoved == 1 && vel.Destroyed == 0);

        // 再生で消すときは、全列に Destroy が 1 回ずつ
        EcsCommandBuffer destroy(2);
        destroy.Destroy(entity);
        EcsCommandBuffer *destroyOrder[] = {&destroy};
        EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(destroyOrder, 1));
        assert(pos.Destroyed == 1 && vel.Destroyed == 1 && vel.LastValue == 6.0f);
        assert(world.GetEntityCount() == 0);

        // 再生の作成は、初期値を入れた後に OnCreate が呼ばれる
        EcsCommandBuffer create(3);
        const CmPos initial{11.0f, 0.0f};
        const EcsColumnValue values[] = {ValueOf(initial)};
        create.Create(archetype, Container::Span<const EcsColumnValue>(values, 1));
        EcsCommandBuffer *createOrder[] = {&create};
        EcsCommandBuffer::Replay(world, Container::Span<EcsCommandBuffer *const>(createOrder, 1));
        assert(pos.Created == 2 && pos.LastValue == 11.0f);
    }
}

int main()
{
    TestReplayOrderIsIndependentOfThreadCount();
    TestOrderFollowsBufferSequence();
    TestCancelledReservationIsNotReplayed();
    TestTicketFromBeforeClearDoesNotAffectNewReservation();
    TestStaleCommandsAreDiscarded();
    TestRejectedCommandsChangeNothing();
    TestReplayAdvancesVersionOnlyWithCommands();
    TestHooksDistinguishDestroyFromMove();
    TestCommandMoveDoesNotCallDestroy();
    std::cout << "EcsCommandTest 合格\n";
    return 0;
}
