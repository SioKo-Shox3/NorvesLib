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

    struct Velocity
    {
        float X;
        float Y;
        float Z;
    };

    struct Health
    {
        int32_t Value;
    };

    struct alignas(32) Aligned32
    {
        float Lanes[8];
    };

    struct TooBig
    {
        unsigned char Bytes[20000];
    };

    EcsArchetypeId MakeMovingArchetype(EcsWorld &world)
    {
        const EcsColumnInfo columns[] = {EcsColumnInfoOf<Position>(), EcsColumnInfoOf<Velocity>(), EcsColumnInfoOf<Health>()};
        return world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(columns, 3));
    }

    Health *HealthOf(EcsWorld &world, EcsEntity entity)
    {
        return world.GetColumnValue<Health>(entity);
    }

    void TestArchetypeIdentity()
    {
        EcsWorld world;
        const EcsColumnInfo ab[] = {EcsColumnInfoOf<Position>(), EcsColumnInfoOf<Health>()};
        const EcsColumnInfo ba[] = {EcsColumnInfoOf<Health>(), EcsColumnInfoOf<Position>()};
        const EcsArchetypeId first = world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(ab, 2));
        assert(first != kInvalidEcsArchetype);
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(ba, 2)) == first);
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(ab, 2), 1) != first);
        assert(world.GetArchetypeCount() == 2);

        // 同じ型の重複・大きさ 0・16 KB に収まらない行は作れない
        const EcsColumnInfo duplicated[] = {EcsColumnInfoOf<Health>(), EcsColumnInfoOf<Health>()};
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(duplicated, 2)) == kInvalidEcsArchetype);
        EcsColumnInfo zeroSized = EcsColumnInfoOf<Velocity>();
        zeroSized.Size = 0;
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(&zeroSized, 1)) == kInvalidEcsArchetype);
        const EcsColumnInfo tooBig[] = {EcsColumnInfoOf<TooBig>()};
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(tooBig, 1)) == kInvalidEcsArchetype);
        // 同じ型の番号で大きさの違う記述は不正
        EcsColumnInfo wrongSize = EcsColumnInfoOf<Health>();
        wrongSize.Size = 8;
        const EcsColumnInfo mismatch[] = {EcsColumnInfoOf<Position>(), wrongSize};
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(mismatch, 2)) == kInvalidEcsArchetype);
        assert(world.CreateEntity(kInvalidEcsArchetype).IsNull());
        assert(world.GetArchetypeCount() == 2);

        // 既存のアーキタイプと列の数・タグが違っても、記述は型の登録と照合される
        EcsColumnInfo wrongAlign = EcsColumnInfoOf<Health>();
        wrongAlign.Align = 16;
        const EcsColumnInfo wrongAlignColumns[] = {wrongAlign};
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(wrongAlignColumns, 1)) == kInvalidEcsArchetype);
        const EcsColumnInfo healthOnly[] = {EcsColumnInfoOf<Health>()};
        const EcsArchetypeId healthTag0 = world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(healthOnly, 1), 0);
        assert(healthTag0 != kInvalidEcsArchetype);
        EcsColumnInfo shrunk = EcsColumnInfoOf<Health>();
        shrunk.Size = 1;
        const EcsColumnInfo shrunkColumns[] = {shrunk};
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(shrunkColumns, 1), 1) == kInvalidEcsArchetype);
        const EcsColumnInfo shrunkWithOther[] = {EcsColumnInfoOf<Position>(), shrunk};
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(shrunkWithOther, 2), 0) == kInvalidEcsArchetype);
        // 登録されていない型の番号は拒む
        const EcsColumnInfo unregistered[] = {EcsColumnInfo{0x7FFFFFF0u, 4, 4}};
        assert(world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(unregistered, 1)) == kInvalidEcsArchetype);
        assert(world.GetArchetypeCount() == 3);
    }

    void TestChunkLayout()
    {
        EcsWorld world;
        const EcsColumnInfo columns[] = {EcsColumnInfoOf<Health>(), EcsColumnInfoOf<Aligned32>(), EcsColumnInfoOf<Position>()};
        const EcsArchetypeId archetype = world.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(columns, 3));
        assert(archetype != kInvalidEcsArchetype);
        const uint32_t capacity = world.GetArchetypeChunkCapacity(archetype);
        assert(capacity > 0 && capacity <= (kEcsChunkSize - kEcsChunkHeaderSize) / (sizeof(EcsEntity) + 4 + 32 + 12));

        world.CreateEntities(archetype, capacity + 1);
        const auto chunks = world.GetArchetypeChunks(archetype);
        assert(chunks.size() == 2);
        assert(chunks[0]->Count == capacity && chunks[1]->Count == 1);
        for (EcsChunk *chunk : chunks)
        {
            const std::byte *begin = reinterpret_cast<const std::byte *>(chunk);
            const std::byte *end = begin + kEcsChunkSize;
            // 列は互いに重ならず、チャンクの 16 KB に収まり、型の配置を満たす
            const std::byte *previousEnd = begin + kEcsChunkHeaderSize + sizeof(EcsEntity) * chunk->Capacity;
            for (uint32_t i = 0; i < world.GetArchetypeColumns(archetype).size(); ++i)
            {
                const EcsColumnInfo &column = world.GetArchetypeColumns(archetype)[i];
                const std::byte *columnBegin = static_cast<const std::byte *>(world.GetChunkColumn(static_cast<const EcsChunk *>(chunk), i));
                assert(columnBegin >= previousEnd);
                assert(reinterpret_cast<uintptr_t>(columnBegin) % column.Align == 0);
                previousEnd = columnBegin + static_cast<size_t>(column.Size) * chunk->Capacity;
                assert(previousEnd <= end);
            }
        }
    }

    void TestGenerationRejectsStaleIdentifiers()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = MakeMovingArchetype(world);
        assert(!world.IsAlive(kNullEcsEntity) && !world.DestroyEntity(kNullEcsEntity));

        const EcsEntity first = world.CreateEntity(archetype);
        assert(!first.IsNull() && world.IsAlive(first) && first.Generation == 1);
        assert(HealthOf(world, first) != nullptr && HealthOf(world, first)->Value == 0);
        HealthOf(world, first)->Value = 77;

        assert(world.DestroyEntity(first));
        assert(!world.IsAlive(first));
        assert(!world.DestroyEntity(first));
        assert(HealthOf(world, first) == nullptr);
        assert(world.GetArchetypeOf(first) == kInvalidEcsArchetype);

        // 同じ番号が世代を進めて再利用され、古い識別子は拒まれたまま
        const EcsEntity second = world.CreateEntity(archetype);
        assert(second.Index == first.Index && second.Generation == first.Generation + 1);
        assert(world.IsAlive(second) && !world.IsAlive(first));
        assert(HealthOf(world, second)->Value == 0);
        assert(HealthOf(world, first) == nullptr);
        assert(!world.DestroyEntity(first) && world.IsAlive(second));

        // 範囲外の番号と、世代の違う識別子
        assert(!world.IsAlive(EcsEntity{second.Index + 100, 1}));
        assert(!world.IsAlive(EcsEntity{second.Index, second.Generation + 1}));
        assert(world.GetEntityCount() == 1);
    }

    void TestSwapRemovalKeepsValues()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = MakeMovingArchetype(world);
        const uint32_t capacity = world.GetArchetypeChunkCapacity(archetype);
        const uint32_t total = capacity * 2 + 3;

        Container::VariableArray<EcsEntity> entities;
        assert(world.CreateEntities(archetype, total, &entities) == total && entities.size() == total);
        for (uint32_t i = 0; i < total; ++i)
        {
            HealthOf(world, entities[i])->Value = static_cast<int32_t>(i) + 1000;
            world.GetColumnValue<Position>(entities[i])->X = static_cast<float>(i);
        }

        // 先頭・中ほどの別チャンク・末尾を消しても、残った実体の値は変わらない
        const uint32_t removed[] = {0, capacity + 5, total - 1, capacity - 1};
        for (const uint32_t index : removed)
        {
            assert(world.DestroyEntity(entities[index]));
        }
        assert(world.GetEntityCount() == total - 4);
        assert(world.GetArchetypeRowCount(archetype) == total - 4);
        for (uint32_t i = 0; i < total; ++i)
        {
            bool bRemoved = false;
            for (const uint32_t index : removed)
            {
                bRemoved = bRemoved || index == i;
            }
            assert(world.IsAlive(entities[i]) == !bRemoved);
            if (!bRemoved)
            {
                assert(HealthOf(world, entities[i])->Value == static_cast<int32_t>(i) + 1000);
                assert(world.GetColumnValue<Position>(entities[i])->X == static_cast<float>(i));
            }
        }

        // チャンクの実体の配列と実体の表が対応している。チャンクは先頭から詰まっている
        const auto chunks = world.GetArchetypeChunks(archetype);
        uint32_t counted = 0;
        for (size_t c = 0; c < chunks.size(); ++c)
        {
            assert(chunks[c]->Count > 0 && chunks[c]->IndexInArchetype == c);
            assert(c + 1 == chunks.size() || chunks[c]->Count == capacity);
            const int32_t healthColumn = world.FindColumnIndex(archetype, EcsColumnInfoOf<Health>().Type);
            assert(healthColumn >= 0);
            const Health *healths = static_cast<const Health *>(world.GetChunkColumn(static_cast<const EcsChunk *>(chunks[c]), static_cast<uint32_t>(healthColumn)));
            for (uint32_t row = 0; row < chunks[c]->Count; ++row)
            {
                const EcsEntity entity = chunks[c]->GetEntities()[row];
                assert(world.IsAlive(entity));
                assert(&healths[row] == world.GetColumnValue<Health>(entity));
                ++counted;
            }
        }
        assert(counted == total - 4);
        assert(world.FindColumnIndex(archetype, EcsColumnInfoOf<Aligned32>().Type) == -1);
    }

    void TestChunkReleaseAndReuse()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = MakeMovingArchetype(world);
        const uint32_t capacity = world.GetArchetypeChunkCapacity(archetype);
        const uint32_t total = capacity * 3 + 5;
        assert(world.GetChunkCount() == 0);

        Container::VariableArray<EcsEntity> entities;
        world.CreateEntities(archetype, total, &entities);
        assert(world.GetChunkCount() == 4);
        const size_t poolBytes = world.GetChunkPoolBytes();

        // 末尾のチャンクの行を全部消すと、そのチャンクだけが解放される
        for (uint32_t i = capacity * 3; i < total; ++i)
        {
            assert(world.DestroyEntity(entities[i]));
        }
        assert(world.GetChunkCount() == 3 && world.GetArchetypeChunks(archetype).size() == 3);

        // 解放したブロックを再利用し、プールは増えない
        world.CreateEntities(archetype, capacity * 2);
        assert(world.GetChunkCount() == 5);
        assert(world.GetChunkPoolBytes() == poolBytes);

        for (uint32_t i = 0; i < capacity * 3; ++i)
        {
            assert(world.DestroyEntity(entities[i]));
        }
        assert(world.GetChunkCount() == 2);
        assert(world.GetEntityCount() == capacity * 2);
        // 全部消すとチャンクは 0
        while (world.GetArchetypeRowCount(archetype) > 0)
        {
            assert(world.DestroyEntity(world.GetArchetypeChunks(archetype)[0]->GetEntities()[0]));
        }
        assert(world.GetChunkCount() == 0 && world.GetEntityCount() == 0 && world.GetArchetypeRowCount(archetype) == 0);

        world.CreateEntities(archetype, total);
        assert(world.GetChunkCount() == 4);
        assert(world.GetChunkPoolBytes() == poolBytes);
    }

    void TestReleasedBlocksAreReusedAfterSlabExhausted()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = MakeMovingArchetype(world);
        const uint32_t capacity = world.GetArchetypeChunkCapacity(archetype);
        const size_t initialPoolBytes = world.GetChunkPoolBytes();

        // 最初のスラブ（16 ブロック）を使い切って、プールを広げさせる
        constexpr uint32_t kChunks = 40;
        world.CreateEntities(archetype, capacity * kChunks);
        assert(world.GetChunkCount() == kChunks);
        const size_t grownPoolBytes = world.GetChunkPoolBytes();
        assert(grownPoolBytes > initialPoolBytes);

        Container::VariableArray<EcsChunk *> before;
        for (EcsChunk *chunk : world.GetArchetypeChunks(archetype))
        {
            before.push_back(chunk);
        }

        // 末尾の 10 チャンクを 1 つずつ空にして解放し、その後の作成が解放したブロックだけを使うことを確かめる
        constexpr uint32_t kReleased = 10;
        for (uint32_t i = 0; i < kReleased * capacity; ++i)
        {
            const auto chunks = world.GetArchetypeChunks(archetype);
            EcsChunk *last = chunks[chunks.size() - 1];
            assert(world.DestroyEntity(last->GetEntities()[last->Count - 1]));
        }
        assert(world.GetChunkCount() == kChunks - kReleased);

        world.CreateEntities(archetype, capacity * kReleased);
        assert(world.GetChunkCount() == kChunks);
        assert(world.GetChunkPoolBytes() == grownPoolBytes);
        const auto after = world.GetArchetypeChunks(archetype);
        for (size_t i = 0; i < after.size(); ++i)
        {
            bool bFound = false;
            for (EcsChunk *chunk : before)
            {
                bFound = bFound || chunk == after[i];
            }
            assert(bFound);
        }
        // 同じブロックを二重に渡していない
        for (size_t i = 0; i < after.size(); ++i)
        {
            for (size_t j = i + 1; j < after.size(); ++j)
            {
                assert(after[i] != after[j]);
            }
        }

        // 全部消してから作り直しても、プールは増えない
        while (world.GetArchetypeRowCount(archetype) > 0)
        {
            const auto chunks = world.GetArchetypeChunks(archetype);
            EcsChunk *last = chunks[chunks.size() - 1];
            assert(world.DestroyEntity(last->GetEntities()[last->Count - 1]));
        }
        assert(world.GetChunkCount() == 0);
        world.CreateEntities(archetype, capacity * kChunks);
        assert(world.GetChunkCount() == kChunks && world.GetChunkPoolBytes() == grownPoolBytes);
    }

    void TestMillionRowsReturnToZero()
    {
        EcsWorld world;
        const EcsArchetypeId archetype = MakeMovingArchetype(world);
        constexpr uint32_t kRows = 1000000;

        Container::VariableArray<EcsEntity> entities;
        assert(world.CreateEntities(archetype, kRows, &entities) == kRows);
        assert(world.GetEntityCount() == kRows && world.GetArchetypeRowCount(archetype) == kRows);
        const uint32_t capacity = world.GetArchetypeChunkCapacity(archetype);
        assert(world.GetChunkCount() == (kRows + capacity - 1) / capacity);

        for (uint32_t i = 0; i < kRows; i += 997)
        {
            HealthOf(world, entities[i])->Value = static_cast<int32_t>(i);
        }
        // 先頭から消すと毎回末尾の行が入ってくる。残りの値は保たれる
        for (uint32_t i = 0; i < kRows / 2; ++i)
        {
            assert(world.DestroyEntity(entities[i]));
        }
        assert(world.GetEntityCount() == kRows - kRows / 2);
        for (uint32_t i = kRows / 2; i < kRows; ++i)
        {
            const int32_t expected = i % 997 == 0 ? static_cast<int32_t>(i) : 0;
            assert(HealthOf(world, entities[i])->Value == expected);
        }
        for (uint32_t i = kRows / 2; i < kRows; ++i)
        {
            assert(world.DestroyEntity(entities[i]));
        }
        assert(world.GetEntityCount() == 0);
        assert(world.GetChunkCount() == 0);
        assert(world.GetArchetypeRowCount(archetype) == 0 && world.GetArchetypeChunks(archetype).empty());
        for (uint32_t i = 0; i < kRows; i += 4999)
        {
            assert(!world.IsAlive(entities[i]));
        }
    }
}

int main()
{
    TestArchetypeIdentity();
    TestChunkLayout();
    TestGenerationRejectsStaleIdentifiers();
    TestSwapRemovalKeepsValues();
    TestChunkReleaseAndReuse();
    TestReleasedBlocksAreReusedAfterSlabExhausted();
    TestMillionRowsReturnToZero();
    std::cout << "EcsStorageTest 合格\n";
    return 0;
}
