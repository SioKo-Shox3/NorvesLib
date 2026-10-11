#include "Ecs/EcsWorld.h"

#include "Container/UnorderedSet.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>

namespace NorvesLib::Core::Ecs
{
    namespace
    {
        constexpr uint32_t kChunkPoolBlocksPerSlab = 16;
        constexpr uint32_t kMaxColumnAlign = 64;
        /// 変更ログの先頭を詰め直すのは、読み終えた分がこの数以上で、全体の半分以上のとき
        constexpr size_t kLogTrimThreshold = 4096;

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

        /// 記述の大きさと配置が、型の登録（TypeRegistry）と一致するか。タグや列の組に関わらず、毎回照合する。
        bool MatchesRegisteredType(const EcsColumnInfo &info)
        {
            const TypeInfo *registered = TypeRegistry::Get().Find(info.Type);
            return registered != nullptr && registered->Size == info.Size && registered->Alignment == info.Align;
        }

        /// 席の列が生まれた（行の作成・列の追加）: 席を割り当てて列へ書く。作成時から値が入っていたら規則の違反として数える
        void OnSlotColumnCreated(void *user, EcsEntity entity, const void *value, EcsHookReason reason)
        {
            (void)reason;
            EcsWorld *world = static_cast<EcsWorld *>(user);
            EcsGpuSceneSlot *column = world->GetColumnValue<EcsGpuSceneSlot>(entity);
            if (column == nullptr)
            {
                return;
            }
            EcsGpuSceneSlotTable &table = world->GetGpuSceneSlots();
            if (static_cast<const EcsGpuSceneSlot *>(value)->IsAssigned())
            {
                table.NoteRuleViolation();
            }
            *column = table.Allocate();
        }

        /// 席の列が消える: 理由が Destroy の削除のときだけ席を解放する。列の除去では解放せず、規則の違反として数える
        void OnSlotColumnDestroyed(void *user, EcsEntity entity, const void *value, EcsHookReason reason)
        {
            (void)entity;
            EcsGpuSceneSlotTable &table = static_cast<EcsWorld *>(user)->GetGpuSceneSlots();
            if (reason != EcsHookReason::Destroy)
            {
                table.NoteRuleViolation();
                return;
            }
            table.Release(*static_cast<const EcsGpuSceneSlot *>(value));
        }
    }

    EcsWorld::EcsWorld()
        : m_ChunkPool(kEcsChunkSize, kChunkPoolBlocksPerSlab, 64)
    {
        EcsColumnHooks slotHooks;
        slotHooks.OnCreate = &OnSlotColumnCreated;
        slotHooks.OnDestroy = &OnSlotColumnDestroyed;
        slotHooks.User = this;
        SetColumnHooks(EcsColumnInfoOf<EcsGpuSceneSlot>().Type, slotHooks);
    }

    EcsWorld::~EcsWorld() = default;

    EcsArchetypeId EcsWorld::GetOrCreateArchetype(Container::Span<const EcsColumnInfo> columns, uint64_t tags)
    {
        Container::VariableArray<EcsColumnInfo> sorted(columns.begin(), columns.end());
        std::sort(sorted.begin(), sorted.end(), [](const EcsColumnInfo &a, const EcsColumnInfo &b)
                  { return a.Type < b.Type; });
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            if (!IsValidColumn(sorted[i]) || !MatchesRegisteredType(sorted[i]) || (i > 0 && sorted[i].Type == sorted[i - 1].Type))
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

        // 追跡する列を決める（追跡する列は行ごとの版の配列を持つ）
        Container::VariableArray<int32_t> trackedIndex(sorted.size());
        uint32_t trackedCount = 0;
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            trackedIndex[i] = FindLogIndex(sorted[i].Type);
            if (trackedIndex[i] >= 0)
            {
                ++trackedCount;
            }
        }

        // 1 チャンクに入る行数を、見出し・実体の配列・列・行ごとの版・チャンクの版が 16 KB に収まる最大で決める
        const uint64_t fixedBytes = kEcsChunkHeaderSize + sizeof(uint64_t) * sorted.size();
        if (fixedBytes >= kEcsChunkSize)
        {
            return kInvalidEcsArchetype;
        }
        uint64_t rowBytes = sizeof(EcsEntity) + sizeof(uint64_t) + sizeof(uint64_t) * trackedCount;
        for (const EcsColumnInfo &column : sorted)
        {
            rowBytes += column.Size;
        }
        uint32_t capacity = static_cast<uint32_t>((kEcsChunkSize - fixedBytes) / rowBytes);
        Container::VariableArray<uint32_t> offsets(sorted.size());
        Container::VariableArray<uint32_t> rowVersionOffsets(sorted.size());
        uint32_t chunkVersionOffset = 0;
        while (capacity > 0)
        {
            // 実体の配列、続けて参加の番号の配列
            uint64_t offset = kEcsChunkHeaderSize + static_cast<uint64_t>(sizeof(EcsEntity) + sizeof(uint64_t)) * capacity;
            for (size_t i = 0; i < sorted.size(); ++i)
            {
                offset = AlignUpU32(static_cast<uint32_t>(offset), sorted[i].Align);
                offsets[i] = static_cast<uint32_t>(offset);
                offset += static_cast<uint64_t>(sorted[i].Size) * capacity;
            }
            for (size_t i = 0; i < sorted.size(); ++i)
            {
                rowVersionOffsets[i] = 0;
                if (trackedIndex[i] >= 0)
                {
                    offset = AlignUpU32(static_cast<uint32_t>(offset), alignof(uint64_t));
                    rowVersionOffsets[i] = static_cast<uint32_t>(offset);
                    offset += sizeof(uint64_t) * static_cast<uint64_t>(capacity);
                }
            }
            offset = AlignUpU32(static_cast<uint32_t>(offset), alignof(uint64_t));
            chunkVersionOffset = static_cast<uint32_t>(offset);
            offset += sizeof(uint64_t) * static_cast<uint64_t>(sorted.size());
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
        archetype.TrackedIndex = std::move(trackedIndex);
        archetype.RowVersionOffsets = std::move(rowVersionOffsets);
        archetype.ChunkVersionOffset = chunkVersionOffset;
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
        chunk->MaxJoinSerial = 0;
        chunk->IndexInArchetype = static_cast<uint32_t>(archetype.Chunks.size());
        chunk->MaxJoinSerial = 0;
        std::memset(reinterpret_cast<std::byte *>(chunk) + archetype.ChunkVersionOffset, 0, sizeof(uint64_t) * archetype.Columns.size());
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
            if (archetype.TrackedIndex[i] >= 0)
            {
                // 古い持ち主の版が残らないように 0 に戻す（0 の行は必ず次の TouchRow でログに積まれる）
                std::memset(base + archetype.RowVersionOffsets[i] + sizeof(uint64_t) * firstRow, 0, sizeof(uint64_t) * rowCount);
            }
        }
    }

    EcsEntity EcsWorld::CreateEntity(EcsArchetypeId archetypeId, Container::Span<const EcsColumnValue> initialValues)
    {
        if (m_HookDepth > 0 || archetypeId >= m_Archetypes.size())
        {
            return kNullEcsEntity;
        }
        for (const EcsColumnValue &value : initialValues)
        {
            const int32_t column = FindColumnIndex(archetypeId, value.Type);
            if (value.Data == nullptr || column < 0 || m_Archetypes[archetypeId].Columns[column].Size != value.Size)
            {
                return kNullEcsEntity;
            }
        }
        Container::VariableArray<EcsEntity> created;
        if (CreateRows(archetypeId, 1, &created) == 0)
        {
            return kNullEcsEntity;
        }
        const EcsEntity entity = created[0];
        // 初期値は OnCreate より前に入れる（作成の直後の行は全列が「変わった」なので、版は付け直さない）
        for (const EcsColumnValue &value : initialValues)
        {
            const int32_t column = FindColumnIndex(archetypeId, value.Type);
            std::memcpy(GetColumnValue(entity, value.Type), value.Data, m_Archetypes[archetypeId].Columns[column].Size);
        }
        FireRowHooks(entity, true, EcsHookReason::Create);
        return entity;
    }

    uint32_t EcsWorld::CreateEntities(EcsArchetypeId archetypeId, uint32_t count, Container::VariableArray<EcsEntity> *outEntities)
    {
        if (m_HookDepth > 0)
        {
            return 0;
        }
        if (m_Hooks.empty())
        {
            return CreateRows(archetypeId, count, outEntities);
        }
        Container::VariableArray<EcsEntity> created;
        const uint32_t made = CreateRows(archetypeId, count, &created);
        for (const EcsEntity &entity : created)
        {
            FireRowHooks(entity, true, EcsHookReason::Create);
            if (outEntities != nullptr)
            {
                outEntities->push_back(entity);
            }
        }
        return made;
    }

    uint32_t EcsWorld::CreateRows(EcsArchetypeId archetypeId, uint32_t count, Container::VariableArray<EcsEntity> *outEntities)
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
        const uint64_t version = AcquireWriteVersion();

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
            uint64_t *joinSerials = chunk->GetJoinSerials();
            chunk->MaxJoinSerial = (std::max)(chunk->MaxJoinSerial, m_CreateJoinSerial);
            for (uint32_t i = 0; i < batch; ++i)
            {
                const EcsEntity entity = AcquireSlot();
                Slot &slot = m_Slots[entity.Index];
                slot.Chunk = chunk;
                slot.Row = firstRow + i;
                entities[firstRow + i] = entity;
                joinSerials[firstRow + i] = m_CreateJoinSerial;
                TouchRow(archetype, chunk, firstRow + i, version);
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

    uint64_t EcsWorld::GetJoinSerial(EcsEntity entity) const
    {
        const Slot *slot = FindSlot(entity);
        return slot != nullptr ? slot->Chunk->GetJoinSerials()[slot->Row] : 0;
    }

    EcsArchetypeId EcsWorld::GetArchetypeOf(EcsEntity entity) const
    {
        const Slot *slot = FindSlot(entity);
        return slot != nullptr ? slot->Chunk->Archetype : kInvalidEcsArchetype;
    }

    void EcsWorld::RemoveRow(Archetype &archetype, EcsChunk *chunk, uint32_t row)
    {
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
            for (size_t i = 0; i < archetype.Columns.size(); ++i)
            {
                // 動いた行は自分の版を持って移る（移動先の古い持ち主の版を残さない）
                if (archetype.TrackedIndex[i] >= 0)
                {
                    reinterpret_cast<uint64_t *>(dstBase + archetype.RowVersionOffsets[i])[row] =
                        reinterpret_cast<const uint64_t *>(srcBase + archetype.RowVersionOffsets[i])[lastRow];
                }
            }
            const EcsEntity moved = lastChunk->GetEntities()[lastRow];
            chunk->GetEntities()[row] = moved;
            // 参加の番号も行と一緒に動く（入れ替えで未参加の行が参加済みになったり、その逆になったりしない）
            chunk->GetJoinSerials()[row] = lastChunk->GetJoinSerials()[lastRow];
            chunk->MaxJoinSerial = (std::max)(chunk->MaxJoinSerial, chunk->GetJoinSerials()[row]);
            Slot &movedSlot = m_Slots[moved.Index];
            movedSlot.Chunk = chunk;
            movedSlot.Row = row;
            // 入れ替えで動いた行は「変わった」になる
            TouchRow(archetype, chunk, row, AcquireWriteVersion());
        }
        --lastChunk->Count;
        --archetype.RowCount;
        if (lastChunk->Count == 0)
        {
            ReleaseLastChunk(archetype);
        }
    }

    bool EcsWorld::DestroyEntity(EcsEntity entity)
    {
        const Slot *found = FindSlot(entity);
        if (found == nullptr || m_HookDepth > 0)
        {
            return false;
        }
        // 行が消える前に、全列の OnDestroy を呼ぶ
        FireRowHooks(entity, false, EcsHookReason::Destroy);
        found = FindSlot(entity);
        EcsChunk *chunk = found->Chunk;
        const uint32_t row = found->Row;
        Archetype &archetype = m_Archetypes[chunk->Archetype];

        RemoveRow(archetype, chunk, row);
        --m_EntityCount;

        // 世代を進めて空き一覧へ戻す（0 は null なので飛ばす）
        Slot &slot = m_Slots[entity.Index];
        slot.Chunk = nullptr;
        slot.Generation = slot.Generation + 1 == 0 ? 1 : slot.Generation + 1;
        slot.Row = m_FreeSlotHead;
        m_FreeSlotHead = entity.Index;
        return true;
    }

    void EcsWorld::MoveRow(EcsEntity entity, EcsArchetypeId targetId, const EcsColumnInfo *added, const void *addedValue)
    {
        Slot &slot = m_Slots[entity.Index];
        EcsChunk *srcChunk = slot.Chunk;
        const uint32_t srcRow = slot.Row;
        assert(srcChunk->Archetype != targetId);

        // 移動先の末尾に 1 行を取る（チャンクの確保は m_Archetypes の配列を変えない）
        Archetype &target = m_Archetypes[targetId];
        EcsChunk *dstChunk = target.Chunks.empty() || target.Chunks.back()->Count == target.Capacity
                                 ? AcquireChunk(targetId)
                                 : target.Chunks.back();
        const uint32_t dstRow = dstChunk->Count;
        ZeroRows(target, dstChunk, dstRow, 1);
        const uint64_t version = AcquireWriteVersion();

        // 共通の列（どちらも型の番号の昇順）の値を移す
        Archetype &source = m_Archetypes[srcChunk->Archetype];
        const std::byte *srcBase = reinterpret_cast<const std::byte *>(srcChunk);
        std::byte *dstBase = reinterpret_cast<std::byte *>(dstChunk);
        size_t j = 0;
        for (size_t i = 0; i < source.Columns.size(); ++i)
        {
            while (j < target.Columns.size() && target.Columns[j].Type < source.Columns[i].Type)
            {
                ++j;
            }
            if (j < target.Columns.size() && target.Columns[j].Type == source.Columns[i].Type)
            {
                const size_t size = source.Columns[i].Size;
                std::memcpy(dstBase + target.Offsets[j] + size * dstRow, srcBase + source.Offsets[i] + size * srcRow, size);
                // 追跡列の行の版も引き継ぐ（同じ版で書いた直後の移動が、同じ実体・版をログへ二重に積まない）
                if (source.TrackedIndex[i] >= 0 && target.TrackedIndex[j] >= 0)
                {
                    reinterpret_cast<uint64_t *>(dstBase + target.RowVersionOffsets[j])[dstRow] =
                        reinterpret_cast<const uint64_t *>(srcBase + source.RowVersionOffsets[i])[srcRow];
                }
            }
        }
        if (added != nullptr && addedValue != nullptr)
        {
            const int32_t column = FindColumnIndex(targetId, added->Type);
            assert(column >= 0);
            std::memcpy(dstBase + target.Offsets[column] + static_cast<size_t>(added->Size) * dstRow, addedValue, added->Size);
        }

        dstChunk->GetEntities()[dstRow] = entity;
        // 列の組が変わるだけの移動は参加の番号を保つ（同期点で作った行が、列を足されても早く参加しない）
        const uint64_t joinSerial = srcChunk->GetJoinSerials()[srcRow];
        dstChunk->GetJoinSerials()[dstRow] = joinSerial;
        dstChunk->MaxJoinSerial = (std::max)(dstChunk->MaxJoinSerial, joinSerial);
        // 追跡列を足して版を引き継げなかった行は、外す前に同じ版で積んだ項目と重なりうる（読むときに除く）
        for (size_t k = 0; k < target.Columns.size(); ++k)
        {
            if (target.TrackedIndex[k] >= 0 &&
                reinterpret_cast<const uint64_t *>(dstBase + target.RowVersionOffsets[k])[dstRow] == 0)
            {
                m_Logs[target.TrackedIndex[k]].bMayDuplicate = true;
            }
        }
        // 移動した行は全列が「変わった」になる
        TouchRow(target, dstChunk, dstRow, version);
        ++dstChunk->Count;
        ++target.RowCount;
        slot.Chunk = dstChunk;
        slot.Row = dstRow;

        // 移動元を詰める（実体の数は変わらない）
        RemoveRow(source, srcChunk, srcRow);
    }

    bool EcsWorld::AddColumn(EcsEntity entity, const EcsColumnInfo &column, const void *initialValue)
    {
        const Slot *slot = FindSlot(entity);
        if (slot == nullptr || m_HookDepth > 0)
        {
            return false;
        }
        const EcsArchetypeId sourceId = slot->Chunk->Archetype;
        if (FindColumnIndex(sourceId, column.Type) >= 0)
        {
            return false;
        }
        // GetOrCreateArchetype が m_Archetypes を伸ばすので、元の列とタグは先にコピーする
        Container::VariableArray<EcsColumnInfo> columns = m_Archetypes[sourceId].Columns;
        const uint64_t tags = m_Archetypes[sourceId].Tags;
        columns.push_back(column);
        const EcsArchetypeId targetId = GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(columns.data(), columns.size()), tags);
        if (targetId == kInvalidEcsArchetype)
        {
            return false;
        }
        MoveRow(entity, targetId, &column, initialValue);
        // 足した列だけが生まれる（共通の列は Move なのでフックを呼ばない）
        FireColumnHook(entity, column.Type, true, EcsHookReason::ColumnAdded);
        return true;
    }

    bool EcsWorld::RemoveColumn(EcsEntity entity, TypeId type)
    {
        const Slot *slot = FindSlot(entity);
        if (slot == nullptr || m_HookDepth > 0)
        {
            return false;
        }
        const EcsArchetypeId sourceId = slot->Chunk->Archetype;
        const int32_t index = FindColumnIndex(sourceId, type);
        if (index < 0)
        {
            return false;
        }
        Container::VariableArray<EcsColumnInfo> columns = m_Archetypes[sourceId].Columns;
        const uint64_t tags = m_Archetypes[sourceId].Tags;
        columns.erase(columns.begin() + index);
        const EcsArchetypeId targetId = GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(columns.data(), columns.size()), tags);
        if (targetId == kInvalidEcsArchetype)
        {
            return false;
        }
        // 外す列だけが消える（共通の列は Move なのでフックを呼ばない）。値が残っているうちに呼ぶ
        FireColumnHook(entity, type, false, EcsHookReason::ColumnRemoved);
        MoveRow(entity, targetId, nullptr, nullptr);
        return true;
    }

    bool EcsWorld::SetColumnHooks(TypeId type, const EcsColumnHooks &hooks)
    {
        if (type == InvalidSchemaId || m_HookDepth > 0)
        {
            return false;
        }
        for (size_t i = 0; i < m_Hooks.size(); ++i)
        {
            if (m_Hooks[i].Type == type)
            {
                if (hooks.OnCreate == nullptr && hooks.OnDestroy == nullptr)
                {
                    m_Hooks.erase(m_Hooks.begin() + i);
                }
                else
                {
                    m_Hooks[i].Hooks = hooks;
                }
                return true;
            }
        }
        if (hooks.OnCreate != nullptr || hooks.OnDestroy != nullptr)
        {
            HookEntry entry;
            entry.Type = type;
            entry.Hooks = hooks;
            m_Hooks.push_back(entry);
        }
        return true;
    }

    const EcsColumnHooks *EcsWorld::FindHooks(TypeId type) const
    {
        for (const HookEntry &entry : m_Hooks)
        {
            if (entry.Type == type)
            {
                return &entry.Hooks;
            }
        }
        return nullptr;
    }

    void EcsWorld::FireRowHooks(EcsEntity entity, bool create, EcsHookReason reason)
    {
        if (m_Hooks.empty())
        {
            return;
        }
        const Slot *slot = FindSlot(entity);
        if (slot == nullptr)
        {
            return;
        }
        const Archetype &archetype = m_Archetypes[slot->Chunk->Archetype];
        const std::byte *base = reinterpret_cast<const std::byte *>(slot->Chunk);
        const uint32_t row = slot->Row;
        ++m_HookDepth;
        for (size_t i = 0; i < archetype.Columns.size(); ++i)
        {
            const EcsColumnHooks *hooks = FindHooks(archetype.Columns[i].Type);
            const EcsHookFn fn = hooks == nullptr ? nullptr : (create ? hooks->OnCreate : hooks->OnDestroy);
            if (fn != nullptr)
            {
                fn(hooks->User, entity, base + archetype.Offsets[i] + static_cast<size_t>(archetype.Columns[i].Size) * row, reason);
            }
        }
        --m_HookDepth;
    }

    void EcsWorld::FireColumnHook(EcsEntity entity, TypeId type, bool create, EcsHookReason reason)
    {
        const EcsColumnHooks *hooks = FindHooks(type);
        const EcsHookFn fn = hooks == nullptr ? nullptr : (create ? hooks->OnCreate : hooks->OnDestroy);
        if (fn == nullptr)
        {
            return;
        }
        const void *value = static_cast<const EcsWorld *>(this)->GetColumnValue(entity, type);
        if (value == nullptr)
        {
            return;
        }
        ++m_HookDepth;
        fn(hooks->User, entity, value, reason);
        --m_HookDepth;
    }

    EcsGpuSceneSlotAudit EcsWorld::AuditGpuSceneSlots() const
    {
        EcsGpuSceneSlotAudit report;
        const TypeId slotType = EcsColumnInfoOf<EcsGpuSceneSlot>().Type;
        const EcsGpuSceneSlotTable &table = m_GpuSceneSlots;
        Container::VariableArray<uint8_t> seen(table.GetSlotCount(), 0);
        uint32_t distinctSlots = 0;
        char text[160];
        for (EcsArchetypeId archetypeId = 0; archetypeId < m_Archetypes.size(); ++archetypeId)
        {
            const int32_t column = FindColumnIndex(archetypeId, slotType);
            if (column < 0)
            {
                continue;
            }
            for (const EcsChunk *chunk : m_Archetypes[archetypeId].Chunks)
            {
                const EcsGpuSceneSlot *slots = static_cast<const EcsGpuSceneSlot *>(GetChunkColumn(chunk, static_cast<uint32_t>(column)));
                for (uint32_t row = 0; row < chunk->Count; ++row)
                {
                    ++report.Rows;
                    const EcsGpuSceneSlot &slot = slots[row];
                    if (!slot.IsAssigned())
                    {
                        ++report.UnassignedRows;
                        report.Note("席が未割り当ての行がある");
                        continue;
                    }
                    const uint32_t index = slot.GetIndex();
                    if (!table.IsLive(index) || table.GetRangeOf(index) != slot.Range)
                    {
                        ++report.UnknownSlots;
                        std::snprintf(text, sizeof(text), "行の席 %u が表で使用中でない、または範囲が食い違う", index);
                        report.Note(text);
                    }
                    else if (seen[index] != 0)
                    {
                        ++report.DuplicateSlots;
                        std::snprintf(text, sizeof(text), "席 %u を 2 つの行が持つ", index);
                        report.Note(text);
                    }
                    else
                    {
                        seen[index] = 1;
                        ++distinctSlots;
                    }
                }
            }
        }
        if (table.GetLiveCount() > distinctSlots)
        {
            report.OrphanLiveSlots = table.GetLiveCount() - distinctSlots;
            std::snprintf(text, sizeof(text), "表で使用中の席が行より %u 多い（解放漏れ）", report.OrphanLiveSlots);
            report.Note(text);
        }
        report.ContainmentMisses = table.AuditContainment(report);
        report.PrematureReuse = table.GetPrematureReuseCount();
        report.InvalidReleases = table.GetInvalidReleaseCount();
        report.RuleViolations = table.GetRuleViolationCount();
        if (report.PrematureReuse != 0)
        {
            report.Note("解放の条件がそろう前に席を再利用した");
        }
        if (report.InvalidReleases != 0)
        {
            report.Note("使用中でない席・未割り当ての席を解放しようとした");
        }
        if (report.RuleViolations != 0)
        {
            report.Note("Destroy 以外の理由で席の列を外した、または作成時から値が入っていた");
        }
        return report;
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

    uint64_t EcsWorld::AcquireWriteVersion()
    {
        if (m_bVersionSealed)
        {
            ++m_Version;
            m_bVersionSealed = false;
        }
        return m_Version;
    }

    uint64_t EcsWorld::AdvanceVersion()
    {
        m_bVersionSealed = false;
        return ++m_Version;
    }

    int32_t EcsWorld::FindLogIndex(TypeId type) const
    {
        for (size_t i = 0; i < m_Logs.size(); ++i)
        {
            if (m_Logs[i].Type == type)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    bool EcsWorld::SetColumnTracked(TypeId type)
    {
        if (type == InvalidSchemaId)
        {
            return false;
        }
        if (FindLogIndex(type) >= 0)
        {
            return true;
        }
        for (size_t id = 0; id < m_Archetypes.size(); ++id)
        {
            if (FindColumnIndex(static_cast<EcsArchetypeId>(id), type) >= 0)
            {
                return false;
            }
        }
        ChangeLog log;
        log.Type = type;
        m_Logs.push_back(std::move(log));
        return true;
    }

    bool EcsWorld::IsColumnTracked(TypeId type) const
    {
        return FindLogIndex(type) >= 0;
    }

    void EcsWorld::AppendLog(int32_t logIndex, const EcsChangeEntry &entry)
    {
        ChangeLog &log = m_Logs[logIndex];
        if (log.ConsumerCount > 0)
        {
            log.Entries.push_back(entry);
        }
    }

    void EcsWorld::TouchRow(const Archetype &archetype, EcsChunk *chunk, uint32_t row, uint64_t version)
    {
        std::byte *base = reinterpret_cast<std::byte *>(chunk);
        uint64_t *chunkVersions = reinterpret_cast<uint64_t *>(base + archetype.ChunkVersionOffset);
        const EcsEntity entity = chunk->GetEntities()[row];
        for (size_t i = 0; i < archetype.Columns.size(); ++i)
        {
            chunkVersions[i] = (std::max)(chunkVersions[i], version);
            if (archetype.TrackedIndex[i] < 0)
            {
                continue;
            }
            uint64_t &rowVersion = reinterpret_cast<uint64_t *>(base + archetype.RowVersionOffsets[i])[row];
            if (rowVersion == version)
            {
                continue;
            }
            rowVersion = version;
            AppendLog(archetype.TrackedIndex[i], EcsChangeEntry{entity, version});
        }
    }

    void EcsWorld::TrimLog(ChangeLog &log)
    {
        const size_t size = log.Entries.size();
        uint64_t minCursor = log.BaseSeq + size;
        for (const Consumer &consumer : m_Consumers)
        {
            if (consumer.bActive && &m_Logs[consumer.Log] == &log)
            {
                minCursor = (std::min)(minCursor, consumer.Cursor);
            }
        }
        const size_t drop = static_cast<size_t>(minCursor - log.BaseSeq);
        if (drop == size)
        {
            log.Entries.clear();
            log.BaseSeq += drop;
            log.bMayDuplicate = false;
        }
        else if (drop >= kLogTrimThreshold && drop * 2 >= size)
        {
            log.Entries.erase(log.Entries.begin(), log.Entries.begin() + drop);
            log.BaseSeq += drop;
        }
    }

    EcsConsumerId EcsWorld::RegisterChangeConsumer(TypeId trackedType)
    {
        const int32_t logIndex = FindLogIndex(trackedType);
        if (logIndex < 0)
        {
            return kInvalidEcsConsumer;
        }
        ChangeLog &log = m_Logs[logIndex];
        Consumer consumer;
        consumer.Log = static_cast<uint32_t>(logIndex);
        consumer.Cursor = log.BaseSeq + log.Entries.size();
        consumer.ReadVersion = m_Version;
        consumer.bActive = true;
        ++log.ConsumerCount;
        // 登録した時点の版を読んだことにする（以後の段の外の書き込みは新しい版になる）
        m_bVersionSealed = true;
        for (size_t i = 0; i < m_Consumers.size(); ++i)
        {
            if (!m_Consumers[i].bActive)
            {
                m_Consumers[i] = consumer;
                return static_cast<EcsConsumerId>(i);
            }
        }
        m_Consumers.push_back(consumer);
        return static_cast<EcsConsumerId>(m_Consumers.size() - 1);
    }

    bool EcsWorld::UnregisterChangeConsumer(EcsConsumerId consumerId)
    {
        if (consumerId >= m_Consumers.size() || !m_Consumers[consumerId].bActive)
        {
            return false;
        }
        Consumer &consumer = m_Consumers[consumerId];
        consumer.bActive = false;
        ChangeLog &log = m_Logs[consumer.Log];
        --log.ConsumerCount;
        TrimLog(log);
        return true;
    }

    uint32_t EcsWorld::ReadChanges(EcsConsumerId consumerId, Container::VariableArray<EcsChangeEntry> &out)
    {
        if (consumerId >= m_Consumers.size() || !m_Consumers[consumerId].bActive)
        {
            return 0;
        }
        Consumer &consumer = m_Consumers[consumerId];
        ChangeLog &log = m_Logs[consumer.Log];
        uint32_t added = 0;
        // 同じ実体・版の項目は 1 回にまとめる（版は単調なので、版が変わるたびに見た実体を捨てる）
        Container::UnorderedSet<uint64_t> seen;
        uint64_t seenVersion = 0;
        for (size_t i = static_cast<size_t>(consumer.Cursor - log.BaseSeq); i < log.Entries.size(); ++i)
        {
            const EcsChangeEntry &entry = log.Entries[i];
            if (!IsAlive(entry.Entity))
            {
                continue;
            }
            if (log.bMayDuplicate)
            {
                if (entry.Version != seenVersion)
                {
                    seen.clear();
                    seenVersion = entry.Version;
                }
                const uint64_t key = (static_cast<uint64_t>(entry.Entity.Index) << 32) | entry.Entity.Generation;
                if (!seen.insert(key).second)
                {
                    continue;
                }
            }
            out.push_back(entry);
            ++added;
        }
        consumer.Cursor = log.BaseSeq + log.Entries.size();
        consumer.ReadVersion = m_Version;
        m_bVersionSealed = true;
        TrimLog(log);
        return added;
    }

    uint64_t EcsWorld::GetConsumerReadVersion(EcsConsumerId consumerId) const
    {
        return consumerId < m_Consumers.size() && m_Consumers[consumerId].bActive ? m_Consumers[consumerId].ReadVersion : 0;
    }

    size_t EcsWorld::GetChangeLogLength(TypeId trackedType) const
    {
        const int32_t logIndex = FindLogIndex(trackedType);
        return logIndex >= 0 ? m_Logs[logIndex].Entries.size() : 0;
    }

    void EcsWorld::MarkWritten(EcsChunk *chunk, uint32_t columnIndex, uint32_t row, uint64_t version, EcsChangeBundle &bundle) const
    {
        MarkWrittenRange(chunk, columnIndex, row, 1, version, bundle);
    }

    void EcsWorld::MarkWrittenRange(EcsChunk *chunk, uint32_t columnIndex, uint32_t firstRow, uint32_t count, uint64_t version,
                                    EcsChangeBundle &bundle) const
    {
        const Archetype &archetype = m_Archetypes[chunk->Archetype];
        assert(columnIndex < archetype.Columns.size() && firstRow + count <= chunk->Count);
        std::byte *base = reinterpret_cast<std::byte *>(chunk);
        uint64_t &chunkVersion = reinterpret_cast<uint64_t *>(base + archetype.ChunkVersionOffset)[columnIndex];
        chunkVersion = (std::max)(chunkVersion, version);
        const int32_t tracked = archetype.TrackedIndex[columnIndex];
        if (tracked < 0)
        {
            return;
        }
        uint64_t *rowVersions = reinterpret_cast<uint64_t *>(base + archetype.RowVersionOffsets[columnIndex]);
        const EcsEntity *entities = chunk->GetEntities();
        for (uint32_t row = firstRow; row < firstRow + count; ++row)
        {
            if (rowVersions[row] == version)
            {
                continue;
            }
            rowVersions[row] = version;
            bundle.m_Records.push_back(EcsChangeBundle::Record{static_cast<uint32_t>(tracked), EcsChangeEntry{entities[row], version}});
        }
    }

    void EcsWorld::CommitChanges(Container::Span<EcsChangeBundle *const> bundlesInJobOrder)
    {
        for (size_t b = 0; b < bundlesInJobOrder.size(); ++b)
        {
            EcsChangeBundle *bundle = bundlesInJobOrder[b];
            if (bundle == nullptr)
            {
                continue;
            }
            for (const EcsChangeBundle::Record &record : bundle->m_Records)
            {
                AppendLog(static_cast<int32_t>(record.Log), record.Entry);
            }
            bundle->Clear();
        }
    }

    bool EcsWorld::WriteColumnValue(EcsEntity entity, TypeId type, const void *value)
    {
        const Slot *slot = FindSlot(entity);
        if (slot == nullptr || value == nullptr)
        {
            return false;
        }
        const int32_t column = FindColumnIndex(slot->Chunk->Archetype, type);
        if (column < 0)
        {
            return false;
        }
        const uint32_t size = m_Archetypes[slot->Chunk->Archetype].Columns[column].Size;
        return WriteColumnPart(entity, type, 0, size, value);
    }

    bool EcsWorld::WriteColumnPart(EcsEntity entity, TypeId type, uint32_t offset, uint32_t partSize, const void *value)
    {
        const Slot *slot = FindSlot(entity);
        if (slot == nullptr || value == nullptr)
        {
            return false;
        }
        EcsChunk *chunk = slot->Chunk;
        const uint32_t row = slot->Row;
        const int32_t column = FindColumnIndex(chunk->Archetype, type);
        if (column < 0)
        {
            return false;
        }
        const Archetype &archetype = m_Archetypes[chunk->Archetype];
        const size_t size = archetype.Columns[column].Size;
        if (static_cast<size_t>(offset) + partSize > size)
        {
            return false;
        }
        std::memcpy(reinterpret_cast<std::byte *>(chunk) + archetype.Offsets[column] + size * row + offset, value, partSize);

        EcsChangeBundle bundle;
        MarkWritten(chunk, static_cast<uint32_t>(column), row, AcquireWriteVersion(), bundle);
        EcsChangeBundle *bundles[] = {&bundle};
        CommitChanges(Container::Span<EcsChangeBundle *const>(bundles, 1));
        return true;
    }

    uint64_t EcsWorld::GetRowVersion(EcsEntity entity, TypeId type) const
    {
        const Slot *slot = FindSlot(entity);
        if (slot == nullptr)
        {
            return 0;
        }
        const int32_t column = FindColumnIndex(slot->Chunk->Archetype, type);
        if (column < 0)
        {
            return 0;
        }
        const uint64_t *rowVersions = GetChunkRowVersions(slot->Chunk, static_cast<uint32_t>(column));
        return rowVersions != nullptr ? rowVersions[slot->Row] : 0;
    }

    uint64_t EcsWorld::GetChunkColumnVersion(const EcsChunk *chunk, uint32_t columnIndex) const
    {
        const Archetype &archetype = m_Archetypes[chunk->Archetype];
        assert(columnIndex < archetype.Columns.size());
        return reinterpret_cast<const uint64_t *>(reinterpret_cast<const std::byte *>(chunk) + archetype.ChunkVersionOffset)[columnIndex];
    }

    const uint64_t *EcsWorld::GetChunkRowVersions(const EcsChunk *chunk, uint32_t columnIndex) const
    {
        const Archetype &archetype = m_Archetypes[chunk->Archetype];
        assert(columnIndex < archetype.Columns.size());
        if (archetype.TrackedIndex[columnIndex] < 0)
        {
            return nullptr;
        }
        return reinterpret_cast<const uint64_t *>(reinterpret_cast<const std::byte *>(chunk) + archetype.RowVersionOffsets[columnIndex]);
    }

    void EcsWorld::MatchArchetypes(const EcsQuery &query, Container::VariableArray<EcsArchetypeId> &out) const
    {
        for (size_t id = 0; id < m_Archetypes.size(); ++id)
        {
            const Archetype &archetype = m_Archetypes[id];
            const uint64_t tags = archetype.Tags;
            if ((!query.bIncludeOrphans && (tags & kEcsTagOrphan) != 0) || (tags & query.AllTags) != query.AllTags ||
                (query.AnyTags != 0 && (tags & query.AnyTags) == 0) || (tags & query.NoneTags) != 0)
            {
                continue;
            }
            const EcsArchetypeId archetypeId = static_cast<EcsArchetypeId>(id);
            bool bMatch = true;
            for (size_t i = 0; i < query.All.size() && bMatch; ++i)
            {
                bMatch = FindColumnIndex(archetypeId, query.All[i]) >= 0;
            }
            for (size_t i = 0; i < query.None.size() && bMatch; ++i)
            {
                bMatch = FindColumnIndex(archetypeId, query.None[i]) < 0;
            }
            if (bMatch && !query.Any.empty())
            {
                bool bHasAny = false;
                for (size_t i = 0; i < query.Any.size() && !bHasAny; ++i)
                {
                    bHasAny = FindColumnIndex(archetypeId, query.Any[i]) >= 0;
                }
                bMatch = bHasAny;
            }
            if (bMatch && !query.Changed.empty())
            {
                bool bHasChanged = false;
                for (size_t i = 0; i < query.Changed.size() && !bHasChanged; ++i)
                {
                    bHasChanged = FindColumnIndex(archetypeId, query.Changed[i]) >= 0;
                }
                bMatch = bHasChanged;
            }
            if (bMatch)
            {
                out.push_back(archetypeId);
            }
        }
    }

    uint32_t EcsWorld::CollectChunks(const EcsQuery &query, Container::VariableArray<EcsChunk *> &out) const
    {
        Container::VariableArray<EcsArchetypeId> matched;
        MatchArchetypes(query, matched);
        uint32_t added = 0;
        Container::VariableArray<int32_t> changedColumns;
        for (const EcsArchetypeId archetypeId : matched)
        {
            const Archetype &archetype = m_Archetypes[archetypeId];
            changedColumns.clear();
            for (const TypeId type : query.Changed)
            {
                const int32_t column = FindColumnIndex(archetypeId, type);
                if (column >= 0)
                {
                    changedColumns.push_back(column);
                }
            }
            for (EcsChunk *chunk : archetype.Chunks)
            {
                if (!query.Changed.empty())
                {
                    bool bChanged = false;
                    for (size_t i = 0; i < changedColumns.size() && !bChanged; ++i)
                    {
                        bChanged = GetChunkColumnVersion(chunk, static_cast<uint32_t>(changedColumns[i])) > query.ChangedSince;
                    }
                    if (!bChanged)
                    {
                        continue;
                    }
                }
                out.push_back(chunk);
                ++added;
            }
        }
        return added;
    }

    bool EcsWorld::MarkOrphan(EcsEntity entity)
    {
        const Slot *slot = FindSlot(entity);
        if (slot == nullptr || m_HookDepth > 0)
        {
            return false;
        }
        const EcsArchetypeId sourceId = slot->Chunk->Archetype;
        if ((m_Archetypes[sourceId].Tags & kEcsTagOrphan) != 0)
        {
            return false;
        }
        Container::VariableArray<EcsColumnInfo> columns = m_Archetypes[sourceId].Columns;
        const uint64_t tags = m_Archetypes[sourceId].Tags | kEcsTagOrphan;
        const EcsArchetypeId targetId = GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(columns.data(), columns.size()), tags);
        if (targetId == kInvalidEcsArchetype)
        {
            return false;
        }
        MoveRow(entity, targetId, nullptr, nullptr);
        return true;
    }

    bool EcsWorld::IsOrphan(EcsEntity entity) const
    {
        const Slot *slot = FindSlot(entity);
        return slot != nullptr && (m_Archetypes[slot->Chunk->Archetype].Tags & kEcsTagOrphan) != 0;
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
