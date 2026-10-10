#include "Debug/DebugConfig.h"
#include "Ecs/EcsWorld.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Ecs;

namespace
{
    struct ChPos
    {
        float X;
        float Y;
        float Z;
    };

    struct ChVel
    {
        float X;
    };

    struct ChFlag
    {
        uint32_t Bits;
    };

    Container::Span<const EcsColumnInfo> Span(const EcsColumnInfo *columns, size_t count)
    {
        return Container::Span<const EcsColumnInfo>(columns, count);
    }

    TypeId PosType()
    {
        return EcsColumnInfoOf<ChPos>().Type;
    }

    TypeId VelType()
    {
        return EcsColumnInfoOf<ChVel>().Type;
    }

    TypeId FlagType()
    {
        return EcsColumnInfoOf<ChFlag>().Type;
    }

    uint32_t ColumnOf(const EcsWorld &world, EcsArchetypeId archetype, TypeId type)
    {
        const int32_t column = world.FindColumnIndex(archetype, type);
        assert(column >= 0);
        return static_cast<uint32_t>(column);
    }

    EcsArchetypeId PosVelArchetype(EcsWorld &world, uint64_t tags = 0)
    {
        const EcsColumnInfo columns[] = {EcsColumnInfoOf<ChPos>(), EcsColumnInfoOf<ChVel>()};
        return world.GetOrCreateArchetype(Span(columns, 2), tags);
    }

    EcsArchetypeId PosArchetype(EcsWorld &world, uint64_t tags = 0)
    {
        const EcsColumnInfo columns[] = {EcsColumnInfoOf<ChPos>()};
        return world.GetOrCreateArchetype(Span(columns, 1), tags);
    }

    /// 実体の（チャンク, 行）を、アーキタイプのチャンクを走査して求める
    bool Locate(const EcsWorld &world, EcsEntity entity, EcsChunk *&outChunk, uint32_t &outRow)
    {
        const EcsArchetypeId archetype = world.GetArchetypeOf(entity);
        if (archetype == kInvalidEcsArchetype)
        {
            return false;
        }
        for (EcsChunk *chunk : world.GetArchetypeChunks(archetype))
        {
            for (uint32_t row = 0; row < chunk->Count; ++row)
            {
                if (chunk->GetEntities()[row] == entity)
                {
                    outChunk = chunk;
                    outRow = row;
                    return true;
                }
            }
        }
        return false;
    }

    /// 位置の列を書いたことにして、束へ積む（値も書く）
    void WritePos(EcsWorld &world, EcsEntity entity, uint64_t version, EcsChangeBundle &bundle, float value)
    {
        EcsChunk *chunk = nullptr;
        uint32_t row = 0;
        const bool bFound = Locate(world, entity, chunk, row);
        assert(bFound);
        const int32_t column = world.FindColumnIndex(chunk->Archetype, PosType());
        assert(column >= 0);
        static_cast<ChPos *>(world.GetChunkColumn(chunk, static_cast<uint32_t>(column)))[row].X = value;
        world.MarkWritten(chunk, static_cast<uint32_t>(column), row, version, bundle);
    }

    void Commit(EcsWorld &world, EcsChangeBundle &bundle)
    {
        EcsChangeBundle *bundles[] = {&bundle};
        world.CommitChanges(Container::Span<EcsChangeBundle *const>(bundles, 1));
    }

    uint32_t CountOf(const Container::VariableArray<EcsChangeEntry> &entries, EcsEntity entity)
    {
        uint32_t count = 0;
        for (const EcsChangeEntry &entry : entries)
        {
            count += entry.Entity == entity ? 1 : 0;
        }
        return count;
    }

    Container::VariableArray<EcsChangeEntry> Drain(EcsWorld &world, EcsConsumerId consumer)
    {
        Container::VariableArray<EcsChangeEntry> entries;
        const uint32_t added = world.ReadChanges(consumer, entries);
        assert(added == entries.size());
        return entries;
    }

    uint32_t RowsOf(const Container::VariableArray<EcsChunk *> &chunks)
    {
        uint32_t rows = 0;
        for (const EcsChunk *chunk : chunks)
        {
            rows += chunk->Count;
        }
        return rows;
    }

    /// 追跡は最初のアーキタイプより前に宣言する。宣言後は同じ型の呼びが何度でも true。
    void TestTrackingMustPrecedeArchetypes()
    {
        EcsWorld world;
        assert(!world.IsColumnTracked(PosType()));
        assert(world.SetColumnTrackedOf<ChPos>());
        assert(world.SetColumnTrackedOf<ChPos>());
        assert(world.IsColumnTracked(PosType()));
        assert(!world.IsColumnTracked(VelType()));

        const EcsArchetypeId archetype = PosVelArchetype(world);
        assert(archetype != kInvalidEcsArchetype);
        // その型を持つアーキタイプができた後は、新しく追跡にできない
        assert(!world.SetColumnTrackedOf<ChVel>());
        assert(!world.IsColumnTracked(VelType()));
        // 持たない型なら、アーキタイプができた後でも追跡にできる
        assert(world.SetColumnTrackedOf<ChFlag>());
        assert(!world.SetColumnTracked(InvalidSchemaId));

        // 版と変更ログを持つチャンクも、16 KB に収まり、列と重ならない
        world.CreateEntities(archetype, world.GetArchetypeChunkCapacity(archetype) + 1);
        for (const EcsChunk *chunk : world.GetArchetypeChunks(archetype))
        {
            const std::byte *begin = reinterpret_cast<const std::byte *>(chunk);
            const std::byte *end = begin + kEcsChunkSize;
            const uint32_t posCol = ColumnOf(world, archetype, PosType());
            const uint32_t velCol = ColumnOf(world, archetype, VelType());
            const std::byte *columnsEnd = (std::max)(
                static_cast<const std::byte *>(world.GetChunkColumn(chunk, posCol)) + sizeof(ChPos) * chunk->Capacity,
                static_cast<const std::byte *>(world.GetChunkColumn(chunk, velCol)) + sizeof(ChVel) * chunk->Capacity);
            const uint64_t *rowVersions = world.GetChunkRowVersions(chunk, posCol);
            assert(rowVersions != nullptr && world.GetChunkRowVersions(chunk, velCol) == nullptr);
            assert(reinterpret_cast<uintptr_t>(rowVersions) % alignof(uint64_t) == 0);
            assert(reinterpret_cast<const std::byte *>(rowVersions) >= columnsEnd);
            assert(reinterpret_cast<const std::byte *>(rowVersions + chunk->Capacity) <= end);
        }
    }

    /// 書いた行だけが 1 回ずつログに出る。同じ版の 2 回目の書き込みは積まれず、追跡しない列はログに出ない。
    void TestWrittenRowsAppearOncePerVersion()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ChPos>());
        const EcsArchetypeId archetype = PosVelArchetype(world);
        const EcsConsumerId consumer = world.RegisterChangeConsumer(PosType());
        assert(consumer != kInvalidEcsConsumer);
        assert(world.RegisterChangeConsumer(VelType()) == kInvalidEcsConsumer);

        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, 8, &entities);
        // 作成された行は全部ログに出る（1 回ずつ）
        Container::VariableArray<EcsChangeEntry> created = Drain(world, consumer);
        assert(created.size() == 8);
        for (const EcsEntity entity : entities)
        {
            assert(CountOf(created, entity) == 1);
        }
        assert(Drain(world, consumer).empty());

        const uint64_t version = world.AdvanceVersion();
        EcsChangeBundle bundle;
        WritePos(world, entities[1], version, bundle, 1.0f);
        WritePos(world, entities[3], version, bundle, 3.0f);
        WritePos(world, entities[3], version, bundle, 33.0f);
        WritePos(world, entities[5], version, bundle, 5.0f);
        // 追跡しない列を書いても、ログには出ない（チャンクの版だけ進む）
        {
            EcsChunk *chunk = nullptr;
            uint32_t row = 0;
            const bool bFound = Locate(world, entities[2], chunk, row);
            assert(bFound);
            const uint32_t velCol = ColumnOf(world, archetype, VelType());
            world.MarkWritten(chunk, velCol, row, version, bundle);
            assert(world.GetChunkColumnVersion(chunk, velCol) == version);
        }
        assert(bundle.Size() == 3);
        // 束をコミットするまでは、ログにも読みにも出ない
        assert(world.GetChangeLogLength(PosType()) == 0);
        Commit(world, bundle);
        assert(bundle.Size() == 0);
        assert(world.GetChangeLogLength(PosType()) == 3);

        Container::VariableArray<EcsChangeEntry> changes = Drain(world, consumer);
        assert(changes.size() == 3);
        assert(CountOf(changes, entities[1]) == 1 && CountOf(changes, entities[3]) == 1 && CountOf(changes, entities[5]) == 1);
        for (const EcsChangeEntry &entry : changes)
        {
            assert(entry.Version == version);
        }
        assert(world.GetRowVersion(entities[1], PosType()) == version);
        assert(world.GetRowVersion(entities[0], PosType()) < version && world.GetRowVersion(entities[0], PosType()) > 0);
        assert(world.GetRowVersion(entities[1], VelType()) == 0);
        assert(world.GetChangeLogLength(PosType()) == 0);
        assert(world.GetChunkColumnVersion(world.GetArchetypeChunks(archetype)[0], ColumnOf(world, archetype, PosType())) == version);

        // 次の版で同じ行をもう一度書けば、また 1 回出る
        const uint64_t next = world.AdvanceVersion();
        WritePos(world, entities[3], next, bundle, 4.0f);
        Commit(world, bundle);
        changes = Drain(world, consumer);
        assert(changes.size() == 1 && changes[0].Entity == entities[3] && changes[0].Version == next);
        assert(static_cast<ChPos *>(world.GetColumnValue(entities[3], PosType()))->X == 4.0f);
    }

    /// 束の連結は渡した仕事の順。ログの順が仕事の順になる。
    void TestCommitFollowsJobOrder()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ChPos>());
        const EcsArchetypeId archetype = PosArchetype(world);
        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, 6, &entities);
        const EcsConsumerId consumer = world.RegisterChangeConsumer(PosType());

        const uint64_t version = world.AdvanceVersion();
        EcsChangeBundle jobA;
        EcsChangeBundle jobB;
        WritePos(world, entities[4], version, jobA, 1.0f);
        WritePos(world, entities[0], version, jobA, 1.0f);
        WritePos(world, entities[2], version, jobB, 1.0f);
        EcsChangeBundle *order[] = {&jobB, &jobA};
        world.CommitChanges(Container::Span<EcsChangeBundle *const>(order, 2));
        const Container::VariableArray<EcsChangeEntry> changes = Drain(world, consumer);
        assert(changes.size() == 3);
        assert(changes[0].Entity == entities[2] && changes[1].Entity == entities[4] && changes[2].Entity == entities[0]);
    }

    /// 段の外の書き込みは、読んだ後なら新しい版になって、読み落としが起きない。読む前なら同じ版にまとまる。
    void TestExternalWritesGetNewVersionAfterRead()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ChPos>());
        const EcsArchetypeId archetype = PosArchetype(world);
        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, 4, &entities);
        const EcsConsumerId consumer = world.RegisterChangeConsumer(PosType());
        const uint64_t registered = world.GetConsumerReadVersion(consumer);
        assert(registered == world.GetVersion());

        // 登録の直後の書き込みも、登録した版より後になる
        const ChPos one{1, 0, 0};
        assert(world.WriteColumnValueOf(entities[0], one));
        const uint64_t v1 = world.GetVersion();
        assert(v1 > registered && world.GetRowVersion(entities[0], PosType()) == v1);
        // 読む前の書き込みは同じ版にまとまる
        assert(world.WriteColumnValueOf(entities[1], one));
        assert(world.GetVersion() == v1 && world.GetRowVersion(entities[1], PosType()) == v1);

        EcsQuery query;
        query.Changed.push_back(PosType());
        query.ChangedSince = registered;
        Container::VariableArray<EcsChunk *> chunks;
        assert(world.CollectChunks(query, chunks) == 1);

        Container::VariableArray<EcsChangeEntry> changes = Drain(world, consumer);
        assert(changes.size() == 2 && CountOf(changes, entities[0]) == 1 && CountOf(changes, entities[1]) == 1);
        const uint64_t readVersion = world.GetConsumerReadVersion(consumer);
        assert(readVersion == v1);
        query.ChangedSince = readVersion;
        chunks.clear();
        assert(world.CollectChunks(query, chunks) == 0);

        // 読んだ後に同じ行を書き直すと、新しい版で、もう一度出る
        assert(world.WriteColumnValueOf(entities[0], ChPos{2, 0, 0}));
        const uint64_t v2 = world.GetVersion();
        assert(v2 > readVersion && world.GetRowVersion(entities[0], PosType()) == v2);
        chunks.clear();
        assert(world.CollectChunks(query, chunks) == 1);
        changes = Drain(world, consumer);
        assert(changes.size() == 1 && changes[0].Entity == entities[0] && changes[0].Version == v2);

        // 書けないものは false で何も変えない
        const uint64_t before = world.GetVersion();
        assert(!world.WriteColumnValueOf(entities[0], ChVel{1}));
        assert(!world.WriteColumnValueOf(kNullEcsEntity, one));
        assert(world.GetVersion() == before);
        assert(Drain(world, consumer).empty());
    }

    /// 作成・移動・入れ替えで動いた行は、全追跡列で「変わった」になる。
    void TestStructuralChangesMarkRowsChanged()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ChPos>());
        const EcsArchetypeId archetype = PosArchetype(world);
        const EcsConsumerId consumer = world.RegisterChangeConsumer(PosType());
        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, 4, &entities);
        assert(Drain(world, consumer).size() == 4);

        // 先頭を消すと、末尾の行が先頭へ入れ替わって「変わった」になる。消した実体は出ない。
        assert(world.DestroyEntity(entities[0]));
        Container::VariableArray<EcsChangeEntry> changes = Drain(world, consumer);
        assert(changes.size() == 1 && changes[0].Entity == entities[3]);
        assert(world.GetRowVersion(entities[3], PosType()) == changes[0].Version);

        // 列を足すと、移った行と、移動元で入れ替わった行が「変わった」になる（[e3, e1, e2] の e1 が移り、e2 が e1 の跡へ入る）
        assert(world.AddColumnOf<ChVel>(entities[1], ChVel{7}));
        changes = Drain(world, consumer);
        assert(changes.size() == 2 && CountOf(changes, entities[1]) == 1 && CountOf(changes, entities[2]) == 1);
        // 移動先のチャンクの追跡する列の版が行に付いている
        assert(world.GetRowVersion(entities[1], PosType()) == changes[0].Version || world.GetRowVersion(entities[1], PosType()) == changes[1].Version);
        assert(static_cast<ChVel *>(world.GetColumnValue(entities[1], VelType()))->X == 7.0f);

        // 列を外しても、孤児にしても、移った行が「変わった」になる
        assert(world.RemoveColumnOf<ChVel>(entities[1]));
        changes = Drain(world, consumer);
        assert(CountOf(changes, entities[1]) == 1);
        assert(world.MarkOrphan(entities[2]));
        changes = Drain(world, consumer);
        assert(CountOf(changes, entities[2]) == 1);

        // 追跡しない列だけを持つ行の移動は、追跡する列のログに出ない
        const EcsColumnInfo velOnly[] = {EcsColumnInfoOf<ChVel>()};
        const EcsArchetypeId velArchetype = world.GetOrCreateArchetype(Span(velOnly, 1));
        const EcsEntity velEntity = world.CreateEntity(velArchetype);
        assert(Drain(world, consumer).empty());
        assert(world.AddColumnOf<ChFlag>(velEntity, ChFlag{1}));
        assert(Drain(world, consumer).empty());
    }

    /// 動いた行が、同じ版の前の持ち主の版を引き継がない。作った行・入れ替わった行は、必ず自分の版でログに出る。
    void TestMovedRowsDoNotInheritStaleVersions()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ChPos>());
        const EcsArchetypeId archetype = PosArchetype(world);
        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, 3, &entities);
        const EcsConsumerId consumer = world.RegisterChangeConsumer(PosType());

        // a を書き、読まないうちに a を消す。末尾の c が a の行へ入れ替わり、同じ版でも c が出る。
        assert(world.WriteColumnValueOf(entities[0], ChPos{1, 0, 0}));
        const uint64_t version = world.GetVersion();
        assert(world.DestroyEntity(entities[0]));
        assert(world.GetVersion() == version);
        Container::VariableArray<EcsChangeEntry> changes = Drain(world, consumer);
        assert(CountOf(changes, entities[2]) == 1);
        assert(CountOf(changes, entities[0]) == 0);

        // 読んだ後に末尾の b を書いて消し、空いた行に新しい実体を作る。行に残った同じ版が、新しい行のログを隠さない。
        assert(world.WriteColumnValueOf(entities[1], ChPos{2, 0, 0}));
        const uint64_t writeVersion = world.GetVersion();
        assert(world.DestroyEntity(entities[1]));
        const EcsEntity created = world.CreateEntity(archetype);
        assert(world.GetVersion() == writeVersion);
        changes = Drain(world, consumer);
        assert(CountOf(changes, created) == 1 && CountOf(changes, entities[1]) == 0);
    }

    /// 2 つの消費者の既読は独立している。全員が読み終えた分だけが捨てられ、後から登録した消費者は過去を見ない。
    void TestConsumersAreIndependent()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ChPos>());
        const EcsArchetypeId archetype = PosArchetype(world);
        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, 4, &entities);
        assert(world.GetChangeLogLength(PosType()) == 0);

        const EcsConsumerId a = world.RegisterChangeConsumer(PosType());
        const EcsConsumerId b = world.RegisterChangeConsumer(PosType());
        assert(a != kInvalidEcsConsumer && b != kInvalidEcsConsumer && a != b);

        assert(world.WriteColumnValueOf(entities[1], ChPos{1, 0, 0}));
        Container::VariableArray<EcsChangeEntry> seenByA = Drain(world, a);
        assert(seenByA.size() == 1 && seenByA[0].Entity == entities[1]);
        const uint64_t aRead = world.GetConsumerReadVersion(a);

        assert(world.WriteColumnValueOf(entities[2], ChPos{2, 0, 0}));
        seenByA = Drain(world, a);
        assert(seenByA.size() == 1 && seenByA[0].Entity == entities[2]);
        // b は a の読みと無関係に、まだ両方を持っている
        assert(world.GetConsumerReadVersion(b) < aRead);
        assert(world.GetChangeLogLength(PosType()) == 2);
        Container::VariableArray<EcsChangeEntry> seenByB = Drain(world, b);
        assert(seenByB.size() == 2 && CountOf(seenByB, entities[1]) == 1 && CountOf(seenByB, entities[2]) == 1);
        // 全員が読み終えたので、ログは空になる
        assert(world.GetChangeLogLength(PosType()) == 0);

        // 後から登録した消費者は、登録前の変更を見ない
        const EcsConsumerId late = world.RegisterChangeConsumer(PosType());
        assert(Drain(world, late).empty());
        assert(world.WriteColumnValueOf(entities[3], ChPos{3, 0, 0}));
        assert(Drain(world, late).size() == 1);
        assert(Drain(world, a).size() == 1 && Drain(world, b).size() == 1);

        // 解除した消費者は読めず、読み終えていない消費者のためにログは残る
        assert(world.WriteColumnValueOf(entities[0], ChPos{4, 0, 0}));
        assert(Drain(world, a).size() == 1);
        assert(world.UnregisterChangeConsumer(b));
        assert(!world.UnregisterChangeConsumer(b));
        assert(Drain(world, b).empty());
        assert(world.GetConsumerReadVersion(b) == 0);
        assert(world.GetChangeLogLength(PosType()) == 1);
        assert(Drain(world, late).size() == 1);
        assert(world.GetChangeLogLength(PosType()) == 0);
        // 全員が解除されたら、以後はログを積まない
        assert(world.UnregisterChangeConsumer(a) && world.UnregisterChangeConsumer(late));
        assert(world.WriteColumnValueOf(entities[0], ChPos{5, 0, 0}));
        assert(world.GetChangeLogLength(PosType()) == 0);
        // 解除した番号は再利用される
        assert(world.RegisterChangeConsumer(PosType()) != kInvalidEcsConsumer);
    }

    /// クエリ: All/Any/None・タグ・孤児の除外
    void TestQueryMatching()
    {
        EcsWorld world;
        const EcsColumnInfo posCol = EcsColumnInfoOf<ChPos>();
        const EcsColumnInfo velCol = EcsColumnInfoOf<ChVel>();
        const EcsColumnInfo flagCol = EcsColumnInfoOf<ChFlag>();
        const EcsColumnInfo p[] = {posCol};
        const EcsColumnInfo pv[] = {posCol, velCol};
        const EcsColumnInfo v[] = {velCol};
        const EcsColumnInfo pvf[] = {posCol, velCol, flagCol};
        const EcsArchetypeId aP = world.GetOrCreateArchetype(Span(p, 1));
        const EcsArchetypeId aPV = world.GetOrCreateArchetype(Span(pv, 2));
        const EcsArchetypeId aPVTag = world.GetOrCreateArchetype(Span(pv, 2), 0x2);
        const EcsArchetypeId aV = world.GetOrCreateArchetype(Span(v, 1));
        const EcsArchetypeId aPVF = world.GetOrCreateArchetype(Span(pvf, 3), 0x4);
        assert(aPV != aPVTag);

        auto Match = [&](const EcsQuery &query)
        {
            Container::VariableArray<EcsArchetypeId> out;
            world.MatchArchetypes(query, out);
            return out;
        };
        auto Has = [](const Container::VariableArray<EcsArchetypeId> &set, EcsArchetypeId id)
        {
            for (const EcsArchetypeId item : set)
            {
                if (item == id)
                {
                    return true;
                }
            }
            return false;
        };

        EcsQuery query;
        query.All.push_back(PosType());
        auto set = Match(query);
        assert(set.size() == 4 && Has(set, aP) && Has(set, aPV) && Has(set, aPVTag) && Has(set, aPVF) && !Has(set, aV));

        query.None.push_back(VelType());
        set = Match(query);
        assert(set.size() == 1 && Has(set, aP));

        EcsQuery any;
        any.Any.push_back(PosType());
        any.Any.push_back(FlagType());
        set = Match(any);
        assert(set.size() == 4 && !Has(set, aV));
        any.Any.clear();
        any.Any.push_back(FlagType());
        set = Match(any);
        assert(set.size() == 1 && Has(set, aPVF));

        // 何も指定しなければ全部
        assert(Match(EcsQuery{}).size() == 5);

        EcsQuery tags;
        tags.AllTags = 0x2;
        set = Match(tags);
        assert(set.size() == 1 && Has(set, aPVTag));
        tags = EcsQuery{};
        tags.NoneTags = 0x6;
        set = Match(tags);
        assert(set.size() == 3 && Has(set, aP) && Has(set, aPV) && Has(set, aV));
        tags = EcsQuery{};
        tags.AnyTags = 0x6;
        set = Match(tags);
        assert(set.size() == 2 && Has(set, aPVTag) && Has(set, aPVF));
        tags.All.push_back(FlagType());
        set = Match(tags);
        assert(set.size() == 1 && Has(set, aPVF));

        // Changed の列を 1 つも持たないアーキタイプは出ない
        EcsQuery changed;
        changed.Changed.push_back(FlagType());
        set = Match(changed);
        assert(set.size() == 1 && Has(set, aPVF));
    }

    /// 孤児の行はどのクエリにも出ない。含めると指定したときだけ出る。値と実体は保たれる。
    void TestOrphanIsExcludedFromQueries()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PosVelArchetype(world);
        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, 5, &entities);
        for (uint32_t i = 0; i < entities.size(); ++i)
        {
            static_cast<ChPos *>(world.GetColumnValue(entities[i], PosType()))->X = static_cast<float>(i) + 0.5f;
        }

        EcsQuery query;
        query.All.push_back(PosType());
        Container::VariableArray<EcsChunk *> chunks;
        assert(world.CollectChunks(query, chunks) >= 1 && RowsOf(chunks) == 5);

        assert(!world.IsOrphan(entities[1]));
        assert(world.MarkOrphan(entities[1]));
        assert(world.IsOrphan(entities[1]));
        assert(!world.MarkOrphan(entities[1]));
        assert(world.MarkOrphan(entities[3]));
        assert(!world.MarkOrphan(kNullEcsEntity));
        assert(world.IsAlive(entities[1]) && world.GetArchetypeOf(entities[1]) != archetype);
        assert(static_cast<ChPos *>(world.GetColumnValue(entities[1], PosType()))->X == 1.5f);
        assert(static_cast<ChPos *>(world.GetColumnValue(entities[3], PosType()))->X == 3.5f);
        assert(world.GetArchetypeTags(world.GetArchetypeOf(entities[1])) == kEcsTagOrphan);

        chunks.clear();
        world.CollectChunks(query, chunks);
        assert(RowsOf(chunks) == 3);
        // 何も条件を付けない走査でも出ない
        chunks.clear();
        world.CollectChunks(EcsQuery{}, chunks);
        assert(RowsOf(chunks) == 3);

        query.bIncludeOrphans = true;
        chunks.clear();
        world.CollectChunks(query, chunks);
        assert(RowsOf(chunks) == 5);

        // 孤児のタグだけを条件にしても、含めると指定しなければ出ない
        EcsQuery orphanOnly;
        orphanOnly.AllTags = kEcsTagOrphan;
        chunks.clear();
        assert(world.CollectChunks(orphanOnly, chunks) == 0);
        orphanOnly.bIncludeOrphans = true;
        world.CollectChunks(orphanOnly, chunks);
        assert(RowsOf(chunks) == 2);
    }

    /// チャンクの版による絞り込み: 書いたチャンクだけが通る（追跡しない列も）
    void TestChunkVersionFilter()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ChPos>());
        const EcsArchetypeId archetype = PosVelArchetype(world);
        const uint32_t capacity = world.GetArchetypeChunkCapacity(archetype);
        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, capacity * 3, &entities);
        assert(world.GetArchetypeChunks(archetype).size() == 3);
        const EcsConsumerId consumer = world.RegisterChangeConsumer(PosType());
        assert(Drain(world, consumer).empty());
        const uint64_t since = world.GetConsumerReadVersion(consumer);

        EcsQuery posChanged;
        posChanged.Changed.push_back(PosType());
        posChanged.ChangedSince = since;
        EcsQuery velChanged;
        velChanged.Changed.push_back(VelType());
        velChanged.ChangedSince = since;
        Container::VariableArray<EcsChunk *> chunks;
        assert(world.CollectChunks(posChanged, chunks) == 0 && world.CollectChunks(velChanged, chunks) == 0);

        // 2 番目のチャンクの位置と、3 番目のチャンクの速度だけを書く
        const uint64_t version = world.AdvanceVersion();
        EcsChangeBundle bundle;
        WritePos(world, entities[capacity + 2], version, bundle, 1.0f);
        {
            EcsChunk *chunk = world.GetArchetypeChunks(archetype)[2];
            world.MarkWritten(chunk, ColumnOf(world, archetype, VelType()), 0, version, bundle);
        }
        Commit(world, bundle);

        chunks.clear();
        assert(world.CollectChunks(posChanged, chunks) == 1);
        assert(chunks[0] == world.GetArchetypeChunks(archetype)[1]);
        chunks.clear();
        assert(world.CollectChunks(velChanged, chunks) == 1);
        assert(chunks[0] == world.GetArchetypeChunks(archetype)[2]);
        // 全体の列の和集合
        EcsQuery either;
        either.Changed.push_back(PosType());
        either.Changed.push_back(VelType());
        either.ChangedSince = since;
        chunks.clear();
        assert(world.CollectChunks(either, chunks) == 2);
        // All と組み合わせても、チャンクの絞り込みは効く
        either.All.push_back(PosType());
        chunks.clear();
        assert(world.CollectChunks(either, chunks) == 2);
        // 書かれた版より後を基準にすれば何も通らない
        either.ChangedSince = version;
        chunks.clear();
        assert(world.CollectChunks(either, chunks) == 0);
        // 絞り込みを使わない走査は全部のチャンクを返す
        chunks.clear();
        assert(world.CollectChunks(EcsQuery{}, chunks) == 3);
    }

    /// 大量の行でも、書いた行の数だけがログに出る
    void TestManyRows()
    {
        EcsWorld world;
        assert(world.SetColumnTrackedOf<ChPos>());
        const EcsArchetypeId archetype = PosVelArchetype(world);
        constexpr uint32_t kRows = 100000;
        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, kRows, &entities);
        const EcsConsumerId consumer = world.RegisterChangeConsumer(PosType());
        assert(Drain(world, consumer).empty());

        const uint64_t version = world.AdvanceVersion();
        Container::VariableArray<EcsChangeBundle> bundles;
        const auto chunks = world.GetArchetypeChunks(archetype);
        bundles.resize(chunks.size());
        const uint32_t posCol = ColumnOf(world, archetype, PosType());
        for (size_t c = 0; c < chunks.size(); ++c)
        {
            // 仕事 1 つ = チャンク 1 つ。偶数行だけ書き、全行の範囲書きと重ねても 1 回ずつ
            for (uint32_t row = 0; row < chunks[c]->Count; row += 2)
            {
                world.MarkWritten(chunks[c], posCol, row, version, bundles[c]);
            }
            world.MarkWrittenRange(chunks[c], posCol, 0, chunks[c]->Count, version, bundles[c]);
        }
        Container::VariableArray<EcsChangeBundle *> pointers;
        uint32_t expected = 0;
        for (size_t c = 0; c < bundles.size(); ++c)
        {
            pointers.push_back(&bundles[c]);
            expected += chunks[c]->Count;
        }
        assert(expected == kRows);
        world.CommitChanges(Container::Span<EcsChangeBundle *const>(pointers.data(), pointers.size()));
        assert(world.GetChangeLogLength(PosType()) == kRows);
        const Container::VariableArray<EcsChangeEntry> changes = Drain(world, consumer);
        assert(changes.size() == kRows);
        assert(world.GetChangeLogLength(PosType()) == 0);
    }
}

int main()
{
    TestTrackingMustPrecedeArchetypes();
    TestWrittenRowsAppearOncePerVersion();
    TestCommitFollowsJobOrder();
    TestExternalWritesGetNewVersionAfterRead();
    TestStructuralChangesMarkRowsChanged();
    TestMovedRowsDoNotInheritStaleVersions();
    TestConsumersAreIndependent();
    TestQueryMatching();
    TestOrphanIsExcludedFromQueries();
    TestChunkVersionFilter();
    TestManyRows();
    std::cout << "EcsChangeTest 合格\n";
    return 0;
}
