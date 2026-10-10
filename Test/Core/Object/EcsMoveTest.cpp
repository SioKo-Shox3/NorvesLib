#include "Ecs/EcsWorld.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Ecs;

namespace
{
    struct Position
    {
        float X;
        float Y;
        float Z;
    };

    struct Health
    {
        int32_t Value;
    };

    struct Tag64
    {
        uint64_t Bits;
    };

    /// 並列の外では登録しないまま、並列の中で使って検出を確かめる型
    struct NeverRegistered
    {
        int32_t Value;
    };

    /// 並列の中で未登録の型が使われた回数を、assert の代わりに数える
    void CountOnly()
    {
    }

    Container::Span<const EcsColumnInfo> Span(const EcsColumnInfo *columns, size_t count)
    {
        return Container::Span<const EcsColumnInfo>(columns, count);
    }

    EcsArchetypeId PositionArchetype(EcsWorld &world, uint64_t tags = 0)
    {
        const EcsColumnInfo columns[] = {EcsColumnInfoOf<Position>()};
        return world.GetOrCreateArchetype(Span(columns, 1), tags);
    }

    /// 実体の表・チャンクの実体配列・アーキタイプの行数が互いに食い違っていないか
    void CheckConsistent(const EcsWorld &world, EcsArchetypeId archetype, const Container::VariableArray<EcsEntity> &alive)
    {
        uint32_t rows = 0;
        for (EcsChunk *chunk : world.GetArchetypeChunks(archetype))
        {
            assert(chunk->Count > 0 && chunk->Count <= chunk->Capacity);
            for (uint32_t row = 0; row < chunk->Count; ++row)
            {
                const EcsEntity entity = chunk->GetEntities()[row];
                assert(world.IsAlive(entity));
                assert(world.GetArchetypeOf(entity) == archetype);
            }
            rows += chunk->Count;
        }
        assert(rows == world.GetArchetypeRowCount(archetype));
        for (const EcsEntity entity : alive)
        {
            assert(world.IsAlive(entity));
        }
    }

    void TestRegisterFixesTheTypeNumber()
    {
        const EcsColumnInfo registered = EcsRegisterColumn<Position>();
        assert(registered.Type == TypeRegistry::Get().GetTypeId<Position>());
        assert(registered.Size == sizeof(Position) && registered.Align == alignof(Position));
        // 何度呼んでも同じ番号（EcsColumnInfoOf も同じ）
        assert(EcsRegisterColumn<Position>().Type == registered.Type);
        assert(EcsColumnInfoOf<Position>().Type == registered.Type);
        assert(EcsRegisterColumn<Health>().Type != registered.Type);
    }

    void TestAddColumnKeepsValuesAndEntity()
    {
        EcsWorld world;
        const EcsArchetypeId positionOnly = PositionArchetype(world);
        const EcsEntity entity = world.CreateEntity(positionOnly);
        *world.GetColumnValue<Position>(entity) = Position{1.0f, 2.0f, 3.0f};

        assert(world.AddColumnOf<Health>(entity, Health{42}));
        // 実体の識別子は変わらず、別のアーキタイプへ移る
        assert(world.IsAlive(entity));
        const EcsArchetypeId moved = world.GetArchetypeOf(entity);
        assert(moved != kInvalidEcsArchetype && moved != positionOnly);
        assert(world.FindColumnIndex(moved, EcsColumnInfoOf<Health>().Type) >= 0);
        const Position *position = world.GetColumnValue<Position>(entity);
        assert(position != nullptr && position->X == 1.0f && position->Y == 2.0f && position->Z == 3.0f);
        assert(world.GetColumnValue<Health>(entity)->Value == 42);
        assert(world.GetArchetypeRowCount(positionOnly) == 0);
        assert(world.GetArchetypeRowCount(moved) == 1);
        assert(world.GetEntityCount() == 1);
        assert(world.GetChunkCount() == 1);

        // 初期値を渡さない追加は 0
        const EcsEntity other = world.CreateEntity(positionOnly);
        assert(world.AddColumn(other, EcsColumnInfoOf<Health>()));
        assert(world.GetColumnValue<Health>(other)->Value == 0);
        assert(world.GetArchetypeOf(other) == moved);

        // 列を外すと、列の組が元に戻り、同じアーキタイプの番号に戻る
        assert(world.RemoveColumnOf<Health>(entity));
        assert(world.GetArchetypeOf(entity) == positionOnly);
        assert(world.GetColumnValue<Health>(entity) == nullptr);
        position = world.GetColumnValue<Position>(entity);
        assert(position != nullptr && position->X == 1.0f && position->Y == 2.0f && position->Z == 3.0f);
        assert(world.GetArchetypeCount() == 2);
    }

    void TestRejectedChangesDoNothing()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PositionArchetype(world);
        const EcsEntity entity = world.CreateEntity(archetype);
        *world.GetColumnValue<Position>(entity) = Position{4.0f, 5.0f, 6.0f};

        // すでに持つ列の追加・持たない列の削除・死んだ実体・null
        assert(!world.AddColumnOf<Position>(entity, Position{}));
        assert(!world.RemoveColumnOf<Health>(entity));
        assert(!world.AddColumnOf<Health>(kNullEcsEntity, Health{1}));
        assert(!world.RemoveColumnOf<Position>(kNullEcsEntity));

        // 登録と食い違う記述・未登録の型の番号
        EcsColumnInfo wrongSize = EcsColumnInfoOf<Health>();
        wrongSize.Size = 8;
        assert(!world.AddColumn(entity, wrongSize));
        EcsColumnInfo unknown = EcsColumnInfoOf<Health>();
        unknown.Type = 0x7FFFFFFFFFFFFFFFull;
        assert(!world.AddColumn(entity, unknown));

        assert(world.GetArchetypeOf(entity) == archetype);
        assert(world.GetColumnValue<Position>(entity)->Y == 5.0f);
        assert(world.GetArchetypeCount() == 1);

        // 解放後の古い識別子は拒む
        assert(world.DestroyEntity(entity));
        assert(!world.AddColumnOf<Health>(entity, Health{1}));
        assert(!world.RemoveColumnOf<Position>(entity));
        assert(world.GetEntityCount() == 0);
    }

    void TestSourceRowIsFilledBySwap()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PositionArchetype(world);
        Container::VariableArray<EcsEntity> entities;
        for (int32_t i = 0; i < 5; ++i)
        {
            const EcsEntity entity = world.CreateEntity(archetype);
            *world.GetColumnValue<Position>(entity) = Position{static_cast<float>(i), 0.0f, 0.0f};
            entities.push_back(entity);
        }

        // 中ほどの行を移すと、末尾の行が詰める。ほかの実体の値は保たれる
        assert(world.AddColumnOf<Health>(entities[1], Health{100}));
        assert(world.GetArchetypeRowCount(archetype) == 4);
        for (int32_t i = 0; i < 5; ++i)
        {
            assert(world.GetColumnValue<Position>(entities[i])->X == static_cast<float>(i));
        }
        Container::VariableArray<EcsEntity> stay = {entities[0], entities[2], entities[3], entities[4]};
        CheckConsistent(world, archetype, stay);
        // 末尾だった実体（4）が、移した行（1）に入っている
        assert(world.GetArchetypeChunks(archetype)[0]->GetEntities()[1] == entities[4]);

        // 末尾の行を移しても、残りは動かない
        assert(world.AddColumnOf<Health>(entities[3], Health{200}));
        assert(world.GetArchetypeRowCount(archetype) == 3);
        for (int32_t i : {0, 2, 4})
        {
            assert(world.GetColumnValue<Position>(entities[i])->X == static_cast<float>(i));
        }
        Container::VariableArray<EcsEntity> remaining = {entities[0], entities[2], entities[4]};
        CheckConsistent(world, archetype, remaining);

        // 全部移すと、移動元のチャンクは解放される
        for (int32_t i : {0, 2, 4})
        {
            assert(world.AddColumnOf<Health>(entities[i], Health{i}));
        }
        assert(world.GetArchetypeRowCount(archetype) == 0 && world.GetArchetypeChunks(archetype).empty());
        assert(world.GetEntityCount() == 5);
        assert(world.GetChunkCount() == 1);
        assert(world.GetColumnValue<Health>(entities[1])->Value == 100);
        assert(world.GetColumnValue<Health>(entities[3])->Value == 200);
        assert(world.GetColumnValue<Health>(entities[4])->Value == 4);
    }

    void TestMoveAcrossManyChunks()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = PositionArchetype(world);
        const uint32_t capacity = world.GetArchetypeChunkCapacity(archetype);
        const uint32_t count = capacity * 3 + 7;
        Container::VariableArray<EcsEntity> entities;
        assert(world.CreateEntities(archetype, count, &entities) == count);
        for (uint32_t i = 0; i < count; ++i)
        {
            *world.GetColumnValue<Position>(entities[i]) = Position{static_cast<float>(i), 1.0f, 2.0f};
        }
        assert(world.GetArchetypeChunks(archetype).size() == 4);

        // 偶数番を移す（複数のチャンクにまたがり、移動先も複数のチャンクになる）
        for (uint32_t i = 0; i < count; i += 2)
        {
            assert(world.AddColumnOf<Health>(entities[i], Health{static_cast<int32_t>(i)}));
        }
        const EcsArchetypeId withHealth = world.GetArchetypeOf(entities[0]);
        assert(world.GetArchetypeRowCount(archetype) + world.GetArchetypeRowCount(withHealth) == count);
        assert(world.GetEntityCount() == count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const Position *position = world.GetColumnValue<Position>(entities[i]);
            assert(position != nullptr && position->X == static_cast<float>(i) && position->Y == 1.0f && position->Z == 2.0f);
            const Health *health = world.GetColumnValue<Health>(entities[i]);
            if (i % 2 == 0)
            {
                assert(health != nullptr && health->Value == static_cast<int32_t>(i));
            }
            else
            {
                assert(health == nullptr);
            }
        }
        Container::VariableArray<EcsEntity> evens;
        Container::VariableArray<EcsEntity> odds;
        for (uint32_t i = 0; i < count; ++i)
        {
            (i % 2 == 0 ? evens : odds).push_back(entities[i]);
        }
        CheckConsistent(world, withHealth, evens);
        CheckConsistent(world, archetype, odds);

        // 全部を列を外して戻すと、元のアーキタイプに全部が戻り、移動先は空になる
        for (uint32_t i = 0; i < count; i += 2)
        {
            assert(world.RemoveColumnOf<Health>(entities[i]));
        }
        assert(world.GetArchetypeRowCount(withHealth) == 0 && world.GetArchetypeChunks(withHealth).empty());
        assert(world.GetArchetypeRowCount(archetype) == count);
        assert(world.GetChunkCount() == world.GetArchetypeChunks(archetype).size());
        for (uint32_t i = 0; i < count; ++i)
        {
            assert(world.GetColumnValue<Position>(entities[i])->X == static_cast<float>(i));
        }
        CheckConsistent(world, archetype, entities);
    }

    void TestTagsAreKept()
    {
        EcsWorld world;
        const EcsArchetypeId tagged = PositionArchetype(world, 0x5);
        const EcsEntity entity = world.CreateEntity(tagged);
        assert(world.AddColumnOf<Tag64>(entity, Tag64{9}));
        const EcsArchetypeId moved = world.GetArchetypeOf(entity);
        assert(world.GetArchetypeTags(moved) == 0x5);
        assert(world.RemoveColumnOf<Tag64>(entity));
        assert(world.GetArchetypeOf(entity) == tagged);
        // 最後の列を外した実体は、列の無いアーキタイプへ移る（実体は生きている）
        assert(world.RemoveColumnOf<Position>(entity));
        assert(world.IsAlive(entity));
        const EcsArchetypeId empty = world.GetArchetypeOf(entity);
        assert(empty != kInvalidEcsArchetype && world.GetArchetypeColumns(empty).empty());
        assert(world.GetArchetypeTags(empty) == 0x5);
        assert(world.AddColumnOf<Health>(entity, Health{3}));
        assert(world.GetColumnValue<Health>(entity)->Value == 3);
    }

    void TestUnregisteredColumnIsDetectedInParallel()
    {
        // 登録済みの型は並列の中でも使え、報告されない
        (void)EcsRegisterColumn<Health>();
        const EcsUnregisteredColumnHandler previous = EcsSetUnregisteredColumnHandler(&CountOnly);
        const uint32_t before = EcsGetUnregisteredColumnReportCount();
        {
            EcsParallelScope scope;
            assert(EcsColumnInfoOf<Health>().Type == TypeRegistry::Get().GetTypeId<Health>());
            assert(EcsGetUnregisteredColumnReportCount() == before);

            // 未登録の型は報告され、アーキタイプには使えない記述が返る
            const EcsColumnInfo unregistered = EcsColumnInfoOf<NeverRegistered>();
            assert(EcsGetUnregisteredColumnReportCount() == before + 1);
            assert(unregistered.Type == InvalidSchemaId);
            assert(unregistered.Size == sizeof(NeverRegistered));
            EcsWorld world;
            assert(world.GetOrCreateArchetype(Span(&unregistered, 1)) == kInvalidEcsArchetype);

            // 入れ子の区間でも同じ
            {
                EcsParallelScope nested;
                (void)EcsColumnInfoOf<NeverRegistered>();
                assert(EcsGetUnregisteredColumnReportCount() == before + 2);
            }
            (void)EcsColumnInfoOf<NeverRegistered>();
            assert(EcsGetUnregisteredColumnReportCount() == before + 3);
        }
        // 並列の外では、使った時点で登録されて使える。報告は増えない
        const EcsColumnInfo late = EcsColumnInfoOf<NeverRegistered>();
        assert(late.Type != InvalidSchemaId && late.Type == TypeRegistry::Get().GetTypeId<NeverRegistered>());
        assert(EcsGetUnregisteredColumnReportCount() == before + 3);
        {
            // 登録した後は並列の中でも使える
            EcsParallelScope scope;
            assert(EcsColumnInfoOf<NeverRegistered>().Type == late.Type);
            assert(EcsGetUnregisteredColumnReportCount() == before + 3);
        }
        EcsSetUnregisteredColumnHandler(previous);
    }
}

int main()
{
    TestRegisterFixesTheTypeNumber();
    TestAddColumnKeepsValuesAndEntity();
    TestRejectedChangesDoNothing();
    TestSourceRowIsFilledBySwap();
    TestMoveAcrossManyChunks();
    TestTagsAreKept();
    TestUnregisteredColumnIsDetectedInParallel();
    std::cout << "EcsMoveTest 合格\n";
    return 0;
}
