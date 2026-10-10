#include "Ecs/EcsWorld.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>

namespace NorvesLib::Core::Ecs
{
    namespace
    {
        constexpr uint32_t kChunkPoolBlocksPerSlab = 16;
        constexpr uint32_t kMaxColumnAlign = 64;

        uint32_t AlignUpU32(uint32_t value, uint32_t align)
        {
            return (value + align - 1) & ~(align - 1);
        }

        bool IsValidColumn(const EcsColumnInfo &info)
        {
            return info.Size > 0 && info.Align > 0 && info.Align <= kMaxColumnAlign && (info.Align & (info.Align - 1)) == 0;
        }

        bool SameColumn(const EcsColumnInfo &a, const EcsColumnInfo &b)
        {
            return a.Type == b.Type && a.Size == b.Size && a.Align == b.Align;
        }
    }

    EcsWorld::EcsWorld()
        : m_ChunkPool(kEcsChunkSize, kChunkPoolBlocksPerSlab, 64)
    {
    }

    EcsWorld::~EcsWorld() = default;

    EcsArchetypeId EcsWorld::GetOrCreateArchetype(Container::Span<const EcsColumnInfo> columns, uint64_t tags)
    {
        Container::VariableArray<EcsColumnInfo> sorted(columns.begin(), columns.end());
        std::sort(sorted.begin(), sorted.end(), [](const EcsColumnInfo &a, const EcsColumnInfo &b)
                  { return a.Type < b.Type; });
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            if (!IsValidColumn(sorted[i]) || (i > 0 && sorted[i].Type == sorted[i - 1].Type))
            {
                return kInvalidEcsArchetype;
            }
        }

        for (size_t id = 0; id < m_Archetypes.size(); ++id)
        {
            const Archetype &existing = m_Archetypes[id];
            if (existing.Tags != tags || existing.Columns.size() != sorted.size())
            {
                continue;
            }
            bool bSame = true;
            for (size_t i = 0; i < sorted.size() && bSame; ++i)
            {
                bSame = existing.Columns[i].Type == sorted[i].Type;
                if (bSame && !SameColumn(existing.Columns[i], sorted[i]))
                {
                    // 同じ型の番号で大きさや配置が違う記述は不正
                    return kInvalidEcsArchetype;
                }
            }
            if (bSame)
            {
                return static_cast<EcsArchetypeId>(id);
            }
        }

        // 1 チャンクに入る行数を、見出し・実体の配列・列の配置が 16 KB に収まる最大で決める
        uint64_t rowBytes = sizeof(EcsEntity);
        for (const EcsColumnInfo &column : sorted)
        {
            rowBytes += column.Size;
        }
        uint32_t capacity = static_cast<uint32_t>((kEcsChunkSize - kEcsChunkHeaderSize) / rowBytes);
        Container::VariableArray<uint32_t> offsets(sorted.size());
        while (capacity > 0)
        {
            uint64_t offset = kEcsChunkHeaderSize + static_cast<uint64_t>(sizeof(EcsEntity)) * capacity;
            for (size_t i = 0; i < sorted.size(); ++i)
            {
                offset = AlignUpU32(static_cast<uint32_t>(offset), sorted[i].Align);
                offsets[i] = static_cast<uint32_t>(offset);
                offset += static_cast<uint64_t>(sorted[i].Size) * capacity;
            }
            if (offset <= kEcsChunkSize)
            {
                break;
            }
            --capacity;
        }
        if (capacity == 0)
        {
            return kInvalidEcsArchetype;
        }

        Archetype archetype;
        archetype.Columns = std::move(sorted);
        archetype.Offsets = std::move(offsets);
        archetype.Tags = tags;
        archetype.Capacity = capacity;
        m_Archetypes.push_back(std::move(archetype));
        return static_cast<EcsArchetypeId>(m_Archetypes.size() - 1);
    }

    EcsChunk *EcsWorld::AcquireChunk(EcsArchetypeId archetypeId)
    {
        Archetype &archetype = m_Archetypes[archetypeId];
        void *memory = m_ChunkPool.Allocate(kEcsChunkSize);
        assert(memory != nullptr);
        EcsChunk *chunk = new (memory) EcsChunk{};
        chunk->Archetype = archetypeId;
        chunk->Count = 0;
        chunk->Capacity = archetype.Capacity;
        chunk->IndexInArchetype = static_cast<uint32_t>(archetype.Chunks.size());
        archetype.Chunks.push_back(chunk);
        ++m_ChunkCount;
        return chunk;
    }

    void EcsWorld::ReleaseLastChunk(Archetype &archetype)
    {
        assert(!archetype.Chunks.empty() && archetype.Chunks.back()->Count == 0);
        m_ChunkPool.Deallocate(archetype.Chunks.back());
        archetype.Chunks.pop_back();
        --m_ChunkCount;
    }

    EcsEntity EcsWorld::AcquireSlot()
    {
        if (m_FreeSlotHead != kNoSlot)
        {
            const uint32_t index = m_FreeSlotHead;
            Slot &slot = m_Slots[index];
            m_FreeSlotHead = slot.Row;
            return EcsEntity{index, slot.Generation};
        }
        m_Slots.push_back(Slot{});
        return EcsEntity{static_cast<uint32_t>(m_Slots.size() - 1), m_Slots.back().Generation};
    }

    void EcsWorld::ZeroRows(const Archetype &archetype, EcsChunk *chunk, uint32_t firstRow, uint32_t rowCount) const
    {
        std::byte *base = reinterpret_cast<std::byte *>(chunk);
        for (size_t i = 0; i < archetype.Columns.size(); ++i)
        {
            const uint32_t size = archetype.Columns[i].Size;
            std::memset(base + archetype.Offsets[i] + static_cast<size_t>(size) * firstRow, 0, static_cast<size_t>(size) * rowCount);
        }
    }

    EcsEntity EcsWorld::CreateEntity(EcsArchetypeId archetypeId)
    {
        Container::VariableArray<EcsEntity> created;
        if (CreateEntities(archetypeId, 1, &created) == 0)
        {
            return kNullEcsEntity;
        }
        return created[0];
    }

    uint32_t EcsWorld::CreateEntities(EcsArchetypeId archetypeId, uint32_t count, Container::VariableArray<EcsEntity> *outEntities)
    {
        if (archetypeId >= m_Archetypes.size())
        {
            return 0;
        }
        Archetype &archetype = m_Archetypes[archetypeId];
        if (outEntities != nullptr)
        {
            outEntities->reserve(outEntities->size() + count);
        }

        uint32_t remaining = count;
        while (remaining > 0)
        {
            EcsChunk *chunk = archetype.Chunks.empty() || archetype.Chunks.back()->Count == archetype.Capacity
                                  ? AcquireChunk(archetypeId)
                                  : archetype.Chunks.back();
            const uint32_t firstRow = chunk->Count;
            const uint32_t batch = (std::min)(remaining, archetype.Capacity - firstRow);
            ZeroRows(archetype, chunk, firstRow, batch);
            EcsEntity *entities = chunk->GetEntities();
            for (uint32_t i = 0; i < batch; ++i)
            {
                const EcsEntity entity = AcquireSlot();
                Slot &slot = m_Slots[entity.Index];
                slot.Chunk = chunk;
                slot.Row = firstRow + i;
                entities[firstRow + i] = entity;
                if (outEntities != nullptr)
                {
                    outEntities->push_back(entity);
                }
            }
            chunk->Count += batch;
            archetype.RowCount += batch;
            m_EntityCount += batch;
            remaining -= batch;
        }
        return count;
    }

    const EcsWorld::Slot *EcsWorld::FindSlot(EcsEntity entity) const
    {
        if (entity.IsNull() || entity.Index >= m_Slots.size())
        {
            return nullptr;
        }
        const Slot &slot = m_Slots[entity.Index];
        return slot.Chunk != nullptr && slot.Generation == entity.Generation ? &slot : nullptr;
    }

    bool EcsWorld::IsAlive(EcsEntity entity) const
    {
        return FindSlot(entity) != nullptr;
    }

    EcsArchetypeId EcsWorld::GetArchetypeOf(EcsEntity entity) const
    {
        const Slot *slot = FindSlot(entity);
        return slot != nullptr ? slot->Chunk->Archetype : kInvalidEcsArchetype;
    }

    bool EcsWorld::DestroyEntity(EcsEntity entity)
    {
        const Slot *found = FindSlot(entity);
        if (found == nullptr)
        {
            return false;
        }
        EcsChunk *chunk = found->Chunk;
        const uint32_t row = found->Row;
        Archetype &archetype = m_Archetypes[chunk->Archetype];

        // 末尾の行を削除する行へ移して、行を詰める
        EcsChunk *lastChunk = archetype.Chunks.back();
        const uint32_t lastRow = lastChunk->Count - 1;
        if (lastChunk != chunk || lastRow != row)
        {
            std::byte *dstBase = reinterpret_cast<std::byte *>(chunk);
            const std::byte *srcBase = reinterpret_cast<const std::byte *>(lastChunk);
            for (size_t i = 0; i < archetype.Columns.size(); ++i)
            {
                const size_t size = archetype.Columns[i].Size;
                std::memcpy(dstBase + archetype.Offsets[i] + size * row, srcBase + archetype.Offsets[i] + size * lastRow, size);
            }
            const EcsEntity moved = lastChunk->GetEntities()[lastRow];
            chunk->GetEntities()[row] = moved;
            Slot &movedSlot = m_Slots[moved.Index];
            movedSlot.Chunk = chunk;
            movedSlot.Row = row;
        }
        --lastChunk->Count;
        --archetype.RowCount;
        --m_EntityCount;
        if (lastChunk->Count == 0)
        {
            ReleaseLastChunk(archetype);
        }

        // 世代を進めて空き一覧へ戻す（0 は null なので飛ばす）
        Slot &slot = m_Slots[entity.Index];
        slot.Chunk = nullptr;
        slot.Generation = slot.Generation + 1 == 0 ? 1 : slot.Generation + 1;
        slot.Row = m_FreeSlotHead;
        m_FreeSlotHead = entity.Index;
        return true;
    }

    const void *EcsWorld::GetColumnValue(EcsEntity entity, TypeId type) const
    {
        const Slot *slot = FindSlot(entity);
        if (slot == nullptr)
        {
            return nullptr;
        }
        const int32_t column = FindColumnIndex(slot->Chunk->Archetype, type);
        if (column < 0)
        {
            return nullptr;
        }
        const Archetype &archetype = m_Archetypes[slot->Chunk->Archetype];
        return reinterpret_cast<const std::byte *>(slot->Chunk) + archetype.Offsets[column] +
               static_cast<size_t>(archetype.Columns[column].Size) * slot->Row;
    }

    void *EcsWorld::GetColumnValue(EcsEntity entity, TypeId type)
    {
        return const_cast<void *>(static_cast<const EcsWorld *>(this)->GetColumnValue(entity, type));
    }

    uint32_t EcsWorld::GetArchetypeCount() const
    {
        return static_cast<uint32_t>(m_Archetypes.size());
    }

    Container::Span<const EcsColumnInfo> EcsWorld::GetArchetypeColumns(EcsArchetypeId archetype) const
    {
        if (archetype >= m_Archetypes.size())
        {
            return {};
        }
        const auto &columns = m_Archetypes[archetype].Columns;
        return Container::Span<const EcsColumnInfo>(columns.data(), columns.size());
    }

    uint64_t EcsWorld::GetArchetypeTags(EcsArchetypeId archetype) const
    {
        return archetype < m_Archetypes.size() ? m_Archetypes[archetype].Tags : 0;
    }

    uint32_t EcsWorld::GetArchetypeChunkCapacity(EcsArchetypeId archetype) const
    {
        return archetype < m_Archetypes.size() ? m_Archetypes[archetype].Capacity : 0;
    }

    uint32_t EcsWorld::GetArchetypeRowCount(EcsArchetypeId archetype) const
    {
        return archetype < m_Archetypes.size() ? m_Archetypes[archetype].RowCount : 0;
    }

    Container::Span<EcsChunk *const> EcsWorld::GetArchetypeChunks(EcsArchetypeId archetype) const
    {
        if (archetype >= m_Archetypes.size())
        {
            return {};
        }
        const auto &chunks = m_Archetypes[archetype].Chunks;
        return Container::Span<EcsChunk *const>(chunks.data(), chunks.size());
    }

    int32_t EcsWorld::FindColumnIndex(EcsArchetypeId archetype, TypeId type) const
    {
        if (archetype >= m_Archetypes.size())
        {
            return -1;
        }
        const auto &columns = m_Archetypes[archetype].Columns;
        const auto it = std::lower_bound(columns.begin(), columns.end(), type, [](const EcsColumnInfo &column, TypeId value)
                                         { return column.Type < value; });
        return it != columns.end() && it->Type == type ? static_cast<int32_t>(it - columns.begin()) : -1;
    }

    const void *EcsWorld::GetChunkColumn(const EcsChunk *chunk, uint32_t columnIndex) const
    {
        const Archetype &archetype = m_Archetypes[chunk->Archetype];
        assert(columnIndex < archetype.Offsets.size());
        return reinterpret_cast<const std::byte *>(chunk) + archetype.Offsets[columnIndex];
    }

    void *EcsWorld::GetChunkColumn(EcsChunk *chunk, uint32_t columnIndex) const
    {
        return const_cast<void *>(GetChunkColumn(static_cast<const EcsChunk *>(chunk), columnIndex));
    }

}
