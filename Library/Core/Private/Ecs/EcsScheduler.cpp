#include "Ecs/EcsScheduler.h"

#include "Component/TickDispatch.h"
#include "Debug/Stats.h"
#include "Thread/JobSystem.h"

#include <algorithm>
#include <cstring>

namespace NorvesLib::Core::Ecs
{
    namespace
    {
        constexpr const char *kStatCategory = "Ecs";

        void NormalizeTypes(Container::VariableArray<TypeId> &types)
        {
            std::sort(types.begin(), types.end());
            types.erase(std::unique(types.begin(), types.end()), types.end());
        }

        bool HasInvalidType(const Container::VariableArray<TypeId> &types)
        {
            for (const TypeId type : types)
            {
                if (type == InvalidSchemaId)
                {
                    return true;
                }
            }
            return false;
        }

        /// 昇順の 2 つの配列に共通の要素があるか
        bool Intersects(const Container::VariableArray<TypeId> &a, const Container::VariableArray<TypeId> &b)
        {
            size_t i = 0;
            size_t j = 0;
            while (i < a.size() && j < b.size())
            {
                if (a[i] == b[j])
                {
                    return true;
                }
                if (a[i] < b[j])
                {
                    ++i;
                }
                else
                {
                    ++j;
                }
            }
            return false;
        }

        bool ContainsSystem(const Container::VariableArray<EcsSystemId> &ids, EcsSystemId id)
        {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        float DeltaOf(Container::Span<const float> deltas, uint8_t channel)
        {
            return channel < deltas.size() ? deltas[channel] : 0.0f;
        }

        /// 順序付きの呼び先の比較。TickDispatchLess と同じ（群 → 優先度 → 順番）で、同じなら足した順
        TickDispatchEntry MakeDispatchKey(Component::ETickGroup group, int16_t priority, size_t ordinal)
        {
            return TickDispatchEntry{nullptr, nullptr, group, group, priority, ordinal};
        }
    }

    SystemScheduler::SystemScheduler(EcsWorld &world)
        : m_World(world)
    {
        // 次の収集から参加する番号を World に渡す（以後に作る行は次の収集まで更新に参加しない）
        m_World.SetCreateJoinSerial(GetNextCollectionSerial());
    }

    SystemScheduler::~SystemScheduler() = default;

    // ---- 段 ----

    EcsStageId SystemScheduler::AddStage(const char *name)
    {
        if (m_bRunning || !name || name[0] == '\0' || FindStage(name) != kInvalidEcsStage)
        {
            return kInvalidEcsStage;
        }
        Stage stage;
        stage.Name = Container::String(name);
        stage.StageScopeName = Container::String("Ecs.Stage.") + name;
        stage.SyncScopeName = Container::String("Ecs.Sync.") + name;
        m_Stages.push_back(std::move(stage));
        return static_cast<EcsStageId>(m_Stages.size() - 1);
    }

    uint32_t SystemScheduler::GetStageCount() const
    {
        return static_cast<uint32_t>(m_Stages.size());
    }

    EcsStageId SystemScheduler::FindStage(const char *name) const
    {
        if (!name)
        {
            return kInvalidEcsStage;
        }
        for (size_t i = 0; i < m_Stages.size(); ++i)
        {
            if (std::strcmp(m_Stages[i].Name.c_str(), name) == 0)
            {
                return static_cast<EcsStageId>(i);
            }
        }
        return kInvalidEcsStage;
    }

    bool SystemScheduler::SetSyncHook(EcsStageId stage, EcsSyncFn fn, void *user)
    {
        if (stage >= m_Stages.size())
        {
            return false;
        }
        m_Stages[stage].SyncFn = fn;
        m_Stages[stage].SyncUser = user;
        return true;
    }

    // ---- システム ----

    EcsSystemId SystemScheduler::RegisterSystem(const SystemDesc &desc)
    {
        if (m_bRunning || !desc.Name || desc.Name[0] == '\0' || desc.Stage >= m_Stages.size()
            || FindSystem(desc.Name) != kInvalidEcsSystem)
        {
            return kInvalidEcsSystem;
        }
        switch (desc.Thread)
        {
        case ThreadPolicy::RenderThreadOnly:
            return kInvalidEcsSystem;
        case ThreadPolicy::GameThreadOnly:
            if (!desc.Run)
            {
                return kInvalidEcsSystem;
            }
            break;
        case ThreadPolicy::WorkerThread:
        case ThreadPolicy::AnyThread:
            if (!desc.RunChunk)
            {
                return kInvalidEcsSystem;
            }
            break;
        }
        if (HasInvalidType(desc.Reads) || HasInvalidType(desc.Writes))
        {
            return kInvalidEcsSystem;
        }
        for (const EcsSystemId after : desc.After)
        {
            if (after >= m_Systems.size() || m_Systems[after].Stage > desc.Stage)
            {
                return kInvalidEcsSystem;
            }
        }

        System system;
        system.Name = Container::String(desc.Name);
        system.ScopeName = Container::String("Ecs.Sys.") + desc.Name;
        system.Stage = desc.Stage;
        system.Reads = desc.Reads;
        system.Writes = desc.Writes;
        NormalizeTypes(system.Reads);
        NormalizeTypes(system.Writes);
        system.Thread = desc.Thread;
        for (const EcsSystemId after : desc.After)
        {
            if (!ContainsSystem(system.After, after))
            {
                system.After.push_back(after);
            }
        }
        system.TimeChannel = desc.TimeChannel;
        system.Query = desc.Query;
        system.Run = desc.Run;
        system.RunChunk = desc.RunChunk;
        system.User = desc.User;

        const EcsSystemId id = static_cast<EcsSystemId>(m_Systems.size());
        m_Systems.push_back(std::move(system));
        m_Stages[desc.Stage].Systems.push_back(id);
        m_Stages[desc.Stage].bPlanDirty = true;
        return id;
    }

    EcsSystemId SystemScheduler::RegisterOrderedSystem(const OrderedSystemDesc &desc)
    {
        if (m_bRunning || !desc.Name || desc.Name[0] == '\0' || desc.Stage >= m_Stages.size()
            || FindSystem(desc.Name) != kInvalidEcsSystem)
        {
            return kInvalidEcsSystem;
        }
        for (const EcsSystemId after : desc.After)
        {
            if (after >= m_Systems.size() || m_Systems[after].Stage > desc.Stage)
            {
                return kInvalidEcsSystem;
            }
        }

        System system;
        system.Name = Container::String(desc.Name);
        system.ScopeName = Container::String("Ecs.Sys.") + desc.Name;
        system.Stage = desc.Stage;
        system.Thread = ThreadPolicy::GameThreadOnly;
        for (const EcsSystemId after : desc.After)
        {
            if (!ContainsSystem(system.After, after))
            {
                system.After.push_back(after);
            }
        }
        system.bOrdered = true;
        system.Groups = desc.Groups;

        const EcsSystemId id = static_cast<EcsSystemId>(m_Systems.size());
        m_Systems.push_back(std::move(system));
        m_Stages[desc.Stage].Systems.push_back(id);
        m_Stages[desc.Stage].bPlanDirty = true;
        return id;
    }

    bool SystemScheduler::AddAfter(EcsSystemId system, EcsSystemId after)
    {
        if (m_bRunning || system >= m_Systems.size() || after >= m_Systems.size() || system == after)
        {
            return false;
        }
        System &target = m_Systems[system];
        const System &predecessor = m_Systems[after];
        if (predecessor.Stage > target.Stage)
        {
            return false;
        }
        if (ContainsSystem(target.After, after))
        {
            return true;
        }
        target.After.push_back(after);
        if (predecessor.Stage < target.Stage)
        {
            // 前の段のシステムは、段の並びがすでに順序を保証する
            return true;
        }

        Stage &stage = m_Stages[target.Stage];
        Container::VariableArray<int32_t> waves;
        int32_t waveCount = 0;
        if (!ComputeWaves(stage, waves, waveCount))
        {
            target.After.pop_back();
            return false;
        }
        stage.bPlanDirty = true;
        return true;
    }

    uint32_t SystemScheduler::GetSystemCount() const
    {
        return static_cast<uint32_t>(m_Systems.size());
    }

    EcsSystemId SystemScheduler::FindSystem(const char *name) const
    {
        if (!name)
        {
            return kInvalidEcsSystem;
        }
        for (size_t i = 0; i < m_Systems.size(); ++i)
        {
            if (std::strcmp(m_Systems[i].Name.c_str(), name) == 0)
            {
                return static_cast<EcsSystemId>(i);
            }
        }
        return kInvalidEcsSystem;
    }

    uint32_t SystemScheduler::GetWaveCount(EcsStageId stage)
    {
        if (stage >= m_Stages.size())
        {
            return 0;
        }
        EnsurePlan(stage);
        return static_cast<uint32_t>(m_Stages[stage].Waves.size());
    }

    int32_t SystemScheduler::GetSystemWave(EcsSystemId system)
    {
        if (system >= m_Systems.size())
        {
            return -1;
        }
        EnsurePlan(m_Systems[system].Stage);
        return m_Systems[system].Wave;
    }

    const char *SystemScheduler::GetStageScopeName(EcsStageId stage) const
    {
        return stage < m_Stages.size() ? m_Stages[stage].StageScopeName.c_str() : nullptr;
    }

    const char *SystemScheduler::GetSyncScopeName(EcsStageId stage) const
    {
        return stage < m_Stages.size() ? m_Stages[stage].SyncScopeName.c_str() : nullptr;
    }

    const char *SystemScheduler::GetSystemScopeName(EcsSystemId system) const
    {
        return system < m_Systems.size() ? m_Systems[system].ScopeName.c_str() : nullptr;
    }

    bool SystemScheduler::ConflictsWith(const System &a, const System &b) const
    {
        // GameThreadOnly はすべてと衝突する（Object に触るので単独で走る）
        if (a.Thread == ThreadPolicy::GameThreadOnly || b.Thread == ThreadPolicy::GameThreadOnly)
        {
            return true;
        }
        return Intersects(a.Writes, b.Writes) || Intersects(a.Writes, b.Reads) || Intersects(a.Reads, b.Writes);
    }

    bool SystemScheduler::ComputeWaves(const Stage &stage, Container::VariableArray<int32_t> &outWaves,
                                       int32_t &outWaveCount) const
    {
        const size_t count = stage.Systems.size();
        // preds[j] = j より前に走る添字（段の中の登録順）
        Container::VariableArray<Container::VariableArray<uint32_t>> preds(count);
        for (size_t i = 0; i < count; ++i)
        {
            for (size_t j = i + 1; j < count; ++j)
            {
                const System &a = m_Systems[stage.Systems[i]];
                const System &b = m_Systems[stage.Systems[j]];
                const bool bBAfterA = ContainsSystem(b.After, stage.Systems[i]);
                const bool bAAfterB = ContainsSystem(a.After, stage.Systems[j]);
                if (bBAfterA && bAAfterB)
                {
                    return false;
                }
                if (bBAfterA)
                {
                    preds[j].push_back(static_cast<uint32_t>(i));
                }
                else if (bAAfterB)
                {
                    preds[i].push_back(static_cast<uint32_t>(j));
                }
                else if (ConflictsWith(a, b))
                {
                    // 直接の After が無い衝突は登録順
                    preds[j].push_back(static_cast<uint32_t>(i));
                }
            }
        }

        outWaves.assign(count, -1);
        outWaveCount = 0;
        size_t assigned = 0;
        while (assigned < count)
        {
            bool bProgress = false;
            for (size_t v = 0; v < count; ++v)
            {
                if (outWaves[v] >= 0)
                {
                    continue;
                }
                bool bReady = true;
                int32_t level = 0;
                for (const uint32_t pred : preds[v])
                {
                    if (outWaves[pred] < 0)
                    {
                        bReady = false;
                        break;
                    }
                    level = std::max(level, outWaves[pred] + 1);
                }
                if (bReady)
                {
                    outWaves[v] = level;
                    outWaveCount = std::max(outWaveCount, level + 1);
                    ++assigned;
                    bProgress = true;
                }
            }
            if (!bProgress)
            {
                return false;
            }
        }
        return true;
    }

    void SystemScheduler::EnsurePlan(EcsStageId stageId)
    {
        Stage &stage = m_Stages[stageId];
        if (!stage.bPlanDirty)
        {
            return;
        }
        Container::VariableArray<int32_t> waves;
        int32_t waveCount = 0;
        const bool bAcyclic = ComputeWaves(stage, waves, waveCount);
        // 登録と AddAfter が循環を拒むので、ここで失敗することはない
        (void)bAcyclic;

        stage.Waves.clear();
        stage.Waves.resize(static_cast<size_t>(waveCount));
        for (size_t i = 0; i < stage.Systems.size(); ++i)
        {
            const EcsSystemId id = stage.Systems[i];
            m_Systems[id].Wave = waves[i];
            stage.Waves[static_cast<size_t>(waves[i])].push_back(id);
        }
        stage.bPlanDirty = false;
    }

    // ---- 順序付きの系の呼び先 ----

    SystemScheduler::OrderedEntry *SystemScheduler::ResolveEntry(EcsOrderedHandle handle)
    {
        if (handle.System >= m_Systems.size())
        {
            return nullptr;
        }
        System &system = m_Systems[handle.System];
        if (!system.bOrdered || handle.Index >= system.Entries.size())
        {
            return nullptr;
        }
        OrderedEntry &entry = system.Entries[handle.Index];
        return entry.bAlive && entry.Generation == handle.Generation ? &entry : nullptr;
    }

    EcsOrderedHandle SystemScheduler::AddOrderedEntry(EcsSystemId systemId, const OrderedEntryDesc &desc)
    {
        if (systemId >= m_Systems.size() || !m_Systems[systemId].bOrdered || !desc.Run
            || !Component::IsValidTickGroup(desc.Group))
        {
            return EcsOrderedHandle{};
        }
        System &system = m_Systems[systemId];
        uint32_t index = 0;
        if (!system.FreeEntries.empty())
        {
            index = system.FreeEntries.back();
            system.FreeEntries.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(system.Entries.size());
            system.Entries.emplace_back();
        }
        OrderedEntry &entry = system.Entries[index];
        const uint32_t generation = entry.Generation;
        entry = OrderedEntry{};
        entry.Generation = generation;
        entry.Run = desc.Run;
        entry.User = desc.User;
        entry.TimeChannel = desc.TimeChannel;
        entry.Group = desc.Group;
        entry.Priority = desc.Priority;
        entry.Ordinal = desc.Ordinal;
        entry.bAlive = true;
        entry.JoinSerial = GetNextCollectionSerial();
        entry.Sequence = system.NextSequence++;
        system.bOrderDirty = true;
        return EcsOrderedHandle{systemId, index, generation};
    }

    uint64_t SystemScheduler::GetOrderedEntryJoinSerial(EcsOrderedHandle handle)
    {
        const OrderedEntry *entry = ResolveEntry(handle);
        return entry ? entry->JoinSerial : 0;
    }

    bool SystemScheduler::RemoveOrderedEntry(EcsOrderedHandle handle)
    {
        OrderedEntry *entry = ResolveEntry(handle);
        if (!entry)
        {
            return false;
        }
        entry->bAlive = false;
        entry->Run = nullptr;
        entry->User = nullptr;
        ++entry->Generation;
        m_Systems[handle.System].bOrderDirty = true;
        return true;
    }

    bool SystemScheduler::SetOrderedEntryKey(EcsOrderedHandle handle, Component::ETickGroup group, int16_t priority)
    {
        OrderedEntry *entry = ResolveEntry(handle);
        if (!entry || !Component::IsValidTickGroup(group))
        {
            return false;
        }
        entry->NextGroup = group;
        entry->NextPriority = priority;
        entry->bHasNextKey = true;
        m_Systems[handle.System].bOrderDirty = true;
        return true;
    }

    void SystemScheduler::RebuildDirtyOrders()
    {
        for (System &system : m_Systems)
        {
            if (!system.bOrdered || !system.bOrderDirty)
            {
                continue;
            }
            system.Order.clear();
            bool bPending = false;
            for (size_t i = 0; i < system.Entries.size(); ++i)
            {
                OrderedEntry &entry = system.Entries[i];
                if (!entry.bAlive)
                {
                    if (!entry.bFree)
                    {
                        entry.bFree = true;
                        system.FreeEntries.push_back(static_cast<uint32_t>(i));
                    }
                    continue;
                }
                if (entry.JoinSerial > m_CollectionSerial)
                {
                    // 参加の番号がまだ来ていない呼び先は、その収集の先頭の作り直しで入れる
                    bPending = true;
                    continue;
                }
                if (entry.bHasNextKey)
                {
                    entry.Group = entry.NextGroup;
                    entry.Priority = entry.NextPriority;
                    entry.bHasNextKey = false;
                }
                if ((system.Groups & Component::TickGroupBit(entry.Group)) == 0)
                {
                    continue;
                }
                system.Order.push_back(static_cast<uint32_t>(i));
            }
            const Container::VariableArray<OrderedEntry> &entries = system.Entries;
            std::sort(system.Order.begin(), system.Order.end(), [&entries](uint32_t lhs, uint32_t rhs) {
                const OrderedEntry &a = entries[lhs];
                const OrderedEntry &b = entries[rhs];
                const TickDispatchEntry keyA = MakeDispatchKey(a.Group, a.Priority, a.Ordinal);
                const TickDispatchEntry keyB = MakeDispatchKey(b.Group, b.Priority, b.Ordinal);
                if (TickDispatchLess(keyA, keyB))
                {
                    return true;
                }
                if (TickDispatchLess(keyB, keyA))
                {
                    return false;
                }
                return a.Sequence < b.Sequence;
            });
            system.bOrderDirty = bPending;
            ++m_Stats.OrderRebuilds;
        }
    }

    // ---- 収集 ----

    uint64_t SystemScheduler::BeginCollection()
    {
        if (m_CollectionDepth++ == 0)
        {
            ++m_CollectionSerial;
            m_World.SetCreateJoinSerial(GetNextCollectionSerial());
            RebuildDirtyOrders();
        }
        return m_CollectionSerial;
    }

    void SystemScheduler::EndCollection()
    {
        if (m_CollectionDepth > 0)
        {
            --m_CollectionDepth;
        }
    }

    // ---- 実行 ----

    void SystemScheduler::ResetStats()
    {
        m_Stats = SchedulerStats{};
    }

    void SystemScheduler::EnsureSlots(uint32_t count)
    {
        while (m_Slots.size() < count)
        {
            m_Slots.push_back(Container::MakeUnique<Slot>(static_cast<uint32_t>(m_Slots.size())));
        }
    }

    bool SystemScheduler::RunStage(EcsStageId stageId, Container::Span<const float> channelDeltas)
    {
        if (stageId >= m_Stages.size() || m_bRunning)
        {
            return false;
        }

        struct RunningGuard
        {
            bool &Flag;

            explicit RunningGuard(bool &flag)
                : Flag(flag)
            {
                Flag = true;
            }

            ~RunningGuard()
            {
                Flag = false;
            }
        };

        EcsCollectionScope collection(*this);
        RunningGuard running(m_bRunning);
        Stage &stage = m_Stages[stageId];
        EnsurePlan(stageId);

        m_SlotCursor = 0;
        for (const EcsSystemId id : stage.Systems)
        {
            System &system = m_Systems[id];
            system.FirstSlot = 0;
            system.SlotCount = 0;
        }

        Debug::ScopedStat stageScope(stage.StageScopeName.c_str(), kStatCategory);
        try
        {
            for (const Container::VariableArray<EcsSystemId> &wave : stage.Waves)
            {
                if (wave.size() == 1 && m_Systems[wave[0]].Thread == ThreadPolicy::GameThreadOnly)
                {
                    RunGameThreadSystem(m_Systems[wave[0]], channelDeltas);
                }
                else
                {
                    RunChunkWave(wave, channelDeltas);
                }
            }
            SyncPoint(stage);
        }
        catch (...)
        {
            DiscardStageBuffers(stage);
            throw;
        }
        return true;
    }

    void SystemScheduler::RunGameThreadSystem(System &system, Container::Span<const float> channelDeltas)
    {
        const uint32_t slotIndex = m_SlotCursor++;
        EnsureSlots(m_SlotCursor);
        system.FirstSlot = slotIndex;
        system.SlotCount = 1;

        EcsSystemContext context;
        context.World = &m_World;
        context.Commands = &m_Slots[slotIndex]->Commands;
        context.Version = m_World.AdvanceVersion();
        context.DeltaTime = DeltaOf(channelDeltas, system.TimeChannel);
        context.CollectionSerial = m_CollectionSerial;

        Debug::ScopedStat scope(system.ScopeName.c_str(), kStatCategory);
        if (system.bOrdered)
        {
            RunOrderedEntries(system, context, channelDeltas);
        }
        else
        {
            system.Run(system.User, context);
        }
    }

    void SystemScheduler::RunOrderedEntries(System &system, EcsSystemContext &context,
                                            Container::Span<const float> channelDeltas)
    {
        // Order は収集の先頭でだけ作り直すので、呼び先の中で項目を足し引きしても添字は変わらない。
        // 呼び先が項目を足すと Entries が伸びるので、関数と引数は呼ぶ前に写す。
        for (size_t i = 0; i < system.Order.size(); ++i)
        {
            const OrderedEntry &entry = system.Entries[system.Order[i]];
            if (!entry.bAlive)
            {
                continue;
            }
            const EcsSystemFn fn = entry.Run;
            void *const user = entry.User;
            context.DeltaTime = DeltaOf(channelDeltas, entry.TimeChannel);
            fn(user, context);
        }
    }

    void SystemScheduler::RunChunkWave(const Container::VariableArray<EcsSystemId> &wave,
                                       Container::Span<const float> channelDeltas)
    {
        m_WaveChunks.clear();
        m_Jobs.clear();
        for (const EcsSystemId id : wave)
        {
            System &system = m_Systems[id];
            system.FirstChunk = static_cast<uint32_t>(m_WaveChunks.size());
            m_World.CollectChunks(system.Query, m_WaveChunks);
            system.ChunkCount = static_cast<uint32_t>(m_WaveChunks.size()) - system.FirstChunk;
            if (system.ChunkCount == 0)
            {
                continue;
            }
            system.RunVersion = m_World.AdvanceVersion();
            system.RunDelta = DeltaOf(channelDeltas, system.TimeChannel);
            system.FirstSlot = m_SlotCursor;
            system.SlotCount = system.ChunkCount;
            for (uint32_t k = 0; k < system.ChunkCount; ++k)
            {
                m_Jobs.push_back(ChunkJobRecord{id, m_WaveChunks[system.FirstChunk + k], k, m_SlotCursor++});
            }
        }
        if (m_Jobs.empty())
        {
            return;
        }
        EnsureSlots(m_SlotCursor);

        m_Stats.LastWaveJobs = static_cast<uint32_t>(m_Jobs.size());
        m_Stats.ChunkJobs += m_Stats.LastWaveJobs;
        ++m_Stats.ParallelForCalls;
        {
            // 仕事の中で未登録の列の型を引くと、開発ビルドで止まる
            EcsParallelScope parallel;
            NorvesLib::Thread::JobSystem::Get().ParallelFor(m_Jobs.size(), 1, [this](size_t index) {
                RunChunkJob(static_cast<uint32_t>(index));
            });
        }

        // 変更の束は仕事の順（システムの登録順 → チャンクの順）にログへ移す
        m_WaveBundles.clear();
        for (const ChunkJobRecord &job : m_Jobs)
        {
            m_WaveBundles.push_back(&m_Slots[job.Slot]->Changes);
        }
        m_World.CommitChanges(Container::Span<EcsChangeBundle *const>(m_WaveBundles.data(), m_WaveBundles.size()));
    }

    void SystemScheduler::RunChunkJob(uint32_t jobIndex)
    {
        const ChunkJobRecord &record = m_Jobs[jobIndex];
        const System &system = m_Systems[record.System];
        Slot &slot = *m_Slots[record.Slot];

        EcsChunkJob job;
        job.World = &m_World;
        job.Chunk = record.Chunk;
        job.Archetype = record.Chunk->Archetype;
        job.ChunkIndex = record.ChunkIndex;
        job.Version = system.RunVersion;
        job.DeltaTime = system.RunDelta;
        job.Changes = &slot.Changes;
        job.Commands = &slot.Commands;
        job.CollectionSerial = m_CollectionSerial;

        // ワーカーで走った仕事は、そのワーカーのスレッドの番号で系の名前の区間に入る
        Debug::ScopedStat scope(system.ScopeName.c_str(), kStatCategory);
        system.RunChunk(system.User, job);
    }

    void SystemScheduler::SyncPoint(Stage &stage)
    {
        Debug::ScopedStat scope(stage.SyncScopeName.c_str(), kStatCategory);

        // 再生はシステムの登録順 → チャンクの順（どのスレッドが走らせたかに依らず決定的）
        m_ReplayList.clear();
        for (const EcsSystemId id : stage.Systems)
        {
            const System &system = m_Systems[id];
            for (uint32_t k = 0; k < system.SlotCount; ++k)
            {
                m_ReplayList.push_back(&m_Slots[system.FirstSlot + k]->Commands);
            }
        }
        m_LastReplay = EcsCommandBuffer::Replay(
            m_World, Container::Span<EcsCommandBuffer *const>(m_ReplayList.data(), m_ReplayList.size()));
        for (uint32_t i = 0; i < m_SlotCursor; ++i)
        {
            m_Slots[i]->Commands.Clear();
        }

        if (stage.SyncFn)
        {
            stage.SyncFn(stage.SyncUser, m_World);
        }
    }

    void SystemScheduler::DiscardStageBuffers(const Stage &stage)
    {
        (void)stage;
        for (uint32_t i = 0; i < m_SlotCursor && i < m_Slots.size(); ++i)
        {
            m_Slots[i]->Commands.Clear();
            m_Slots[i]->Changes.Clear();
        }
    }

}
