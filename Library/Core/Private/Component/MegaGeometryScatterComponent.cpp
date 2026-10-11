#include "Component/MegaGeometryScatterComponent.h"

#include "Debug/EngineChecks.h"
#include "Ecs/EcsGpuSceneSlots.h"
#include "Ecs/EcsScheduler.h"
#include "Ecs/EcsTransformSystem.h"
#include "Ecs/EcsWorld.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/SceneView.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(MegaGeometryScatterComponent, Component)

    namespace
    {
        /// 行の値の flags: 影を落とす
        constexpr uint32_t kScatterFlagCastShadow = 1u << 0;
        /// 検査 MASS_RANGES の名前と、仕事の無いフレームに突き合わせる間隔（列の Run の回数）
        constexpr const char *kMassRangesCheckName = "MASS_RANGES";
        constexpr uint64_t kMassRangesIdleInterval = 60;

        /// 10 bit の値の bit を 3 つおきに広げる（Morton の符号用）
        uint32_t SpreadBits10(uint32_t value)
        {
            value &= 0x3FFu;
            value = (value | (value << 16)) & 0x030000FFu;
            value = (value | (value << 8)) & 0x0300F00Fu;
            value = (value | (value << 4)) & 0x030C30C3u;
            value = (value | (value << 2)) & 0x09249249u;
            return value;
        }

        /// 位置を入力全体の箱で 10 bit ずつに量子化する
        uint32_t QuantizeAxis(float value, float minimum, float extent)
        {
            if (!(extent > 0.0f))
            {
                return 0;
            }
            const float normalized = (value - minimum) / extent;
            const float clamped = std::min(std::max(normalized, 0.0f), 1.0f);
            return static_cast<uint32_t>(clamped * 1023.0f + 0.5f);
        }

        /// 入力の番号を、局所の位置の Morton 順（同じ符号は入力の番号の順）に並べる
        void SortByMorton(const Container::VariableArray<MegaScatterInstance> &instances, Container::VariableArray<uint32_t> &outOrder)
        {
            const size_t count = instances.size();
            outOrder.resize(count);
            if (count == 0)
            {
                return;
            }
            Math::Vector3 minimum = instances[0].Position;
            Math::Vector3 maximum = instances[0].Position;
            for (const MegaScatterInstance &instance : instances)
            {
                minimum = Math::Vector3(std::min(minimum.x, instance.Position.x), std::min(minimum.y, instance.Position.y),
                                        std::min(minimum.z, instance.Position.z));
                maximum = Math::Vector3(std::max(maximum.x, instance.Position.x), std::max(maximum.y, instance.Position.y),
                                        std::max(maximum.z, instance.Position.z));
            }
            Container::VariableArray<uint32_t> codes(count);
            for (size_t index = 0; index < count; ++index)
            {
                const Math::Vector3 &position = instances[index].Position;
                const uint32_t x = QuantizeAxis(position.x, minimum.x, maximum.x - minimum.x);
                const uint32_t y = QuantizeAxis(position.y, minimum.y, maximum.y - minimum.y);
                const uint32_t z = QuantizeAxis(position.z, minimum.z, maximum.z - minimum.z);
                codes[index] = SpreadBits10(x) | (SpreadBits10(y) << 1) | (SpreadBits10(z) << 2);
                outOrder[index] = static_cast<uint32_t>(index);
            }
            std::stable_sort(outOrder.begin(), outOrder.end(),
                             [&codes](uint32_t lhs, uint32_t rhs) { return codes[lhs] < codes[rhs]; });
        }

        /// 球の境界を包む AABB
        void SphereToBox(const Rendering::BoundingSphere &sphere, Math::Vector3 &outMin, Math::Vector3 &outMax)
        {
            outMin = Math::Vector3(sphere.CenterX - sphere.Radius, sphere.CenterY - sphere.Radius, sphere.CenterZ - sphere.Radius);
            outMax = Math::Vector3(sphere.CenterX + sphere.Radius, sphere.CenterY + sphere.Radius, sphere.CenterZ + sphere.Radius);
        }

        /// 2 つの変換が値まで同じか（所有者の変化の検出。誤差で見逃さない）
        bool SameTransformValues(const Math::Transform &lhs, const Math::Transform &rhs)
        {
            return lhs.position.x == rhs.position.x && lhs.position.y == rhs.position.y && lhs.position.z == rhs.position.z &&
                   lhs.rotation.x == rhs.rotation.x && lhs.rotation.y == rhs.rotation.y && lhs.rotation.z == rhs.rotation.z &&
                   lhs.rotation.w == rhs.rotation.w && lhs.scale.x == rhs.scale.x && lhs.scale.y == rhs.scale.y &&
                   lhs.scale.z == rhs.scale.z;
        }

        /// 突き合わせの破れを 1 件数え、最初の 1 件だけ説明を残す
        void NoteAuditFailure(Ecs::EcsMassAudit &audit, const char *format, uint32_t batch, uint32_t range, uint32_t detail)
        {
            if (audit.Failures == 0)
            {
                std::snprintf(audit.FirstFailure, sizeof(audit.FirstFailure), format, batch, range, detail);
            }
            ++audit.Failures;
        }

        Debug::EngineChecks *GetEngineChecks()
        {
            return Engine::GEngine != nullptr ? &Engine::GEngine->GetEngineChecks() : nullptr;
        }

        /// 検査 MASS_RANGES: 列の全項目を突き合わせて結果を出す（仕事のあったフレームと、無いフレームの 60 回に 1 回）
        void RunMassRangesCheck(const Ecs::EcsMassWorkQueue &queue)
        {
            Debug::EngineChecks *checks = GetEngineChecks();
            if (checks == nullptr || !checks->IsEnabled(kMassRangesCheckName) || queue.GetItemCount() == 0)
            {
                return;
            }
            const Ecs::EcsMassWorkStats &stats = queue.GetStats();
            if (stats.Steps == 0 && stats.Runs % kMassRangesIdleInterval != 0)
            {
                return;
            }
            Ecs::EcsMassAudit audit;
            queue.Audit(audit);
            const bool bPass = audit.Failures == 0;
            if (!bPass)
            {
                LOG_ERROR_F("MASS_RANGES: 範囲の公開の規則が破れた (破れた範囲=%u / %u): %s", audit.Failures, audit.Ranges,
                            audit.FirstFailure);
            }
            else if (stats.Steps != 0)
            {
                LOG_INFO_F("MASS_RANGES items=%u pending=%u ranges=%u published=%u rows=%u steps=%u ms=%.3f",
                           stats.Items, stats.PendingItems, audit.Ranges, audit.PublishedRanges, stats.Rows, stats.Steps,
                           stats.Milliseconds);
            }
            checks->Report(kMassRangesCheckName, bPass, audit.Ranges);
        }

        /// World の LateTick の段の系: 仕事の列を予算で 1 フレーム分進め、検査 MASS_RANGES を突き合わせる
        void RunMassWorkSystem(void *user, const Ecs::EcsSystemContext &)
        {
            World &world = *static_cast<World *>(user);
            Ecs::EcsMassWorkQueue &queue = world.GetEcsWorld().GetMassWork();
            queue.Run();
            const Ecs::EcsMassWorkStats &stats = queue.GetStats();
            if (stats.bBurstEnded)
            {
                // ひと続きの仕事（作成・反映・解放）を終えた: かかったフレームの数と、1 フレームの最大の時間
                LOG_INFO_F("MASS_WORK_SETTLED frames=%u rows=%llu max_ms=%.3f budget_rows=%u budget_ms=%.3f items=%u",
                           stats.BurstRuns, static_cast<unsigned long long>(stats.BurstRows), stats.BurstMaxMilliseconds,
                           queue.GetBudget().RowsPerFrame, static_cast<double>(queue.GetBudget().MillisecondsPerFrame),
                           stats.Items);
            }
            RunMassRangesCheck(queue);
        }

        /// 仕事の列を回す系を World に登録する（登録済みなら何もしない）。段の実行中は登録できないので、次の呼び出しで試し直す
        void EnsureMassWorkSystem(World &world)
        {
            Ecs::SystemScheduler &scheduler = world.GetScheduler();
            if (scheduler.FindSystem(MegaGeometryScatterComponent::kMassWorkSystemName) != Ecs::kInvalidEcsSystem)
            {
                return;
            }
            Ecs::SystemDesc desc;
            desc.Name = MegaGeometryScatterComponent::kMassWorkSystemName;
            desc.Stage = scheduler.FindStage("LateTick");
            desc.Thread = ThreadPolicy::GameThreadOnly;
            desc.Run = &RunMassWorkSystem;
            desc.User = &world;
            if (desc.Stage == Ecs::kInvalidEcsStage || scheduler.RegisterSystem(desc) == Ecs::kInvalidEcsSystem)
            {
                LOG_WARNING("MegaGeometryScatterComponent: 散布の仕事の列を回す系を登録できませんでした（次の操作で試し直します）");
            }
        }

        /// 散布の行の削除の後始末（Component が消えた後も、列が持って予算で進める）
        class MegaScatterReleaseJob final : public Ecs::IEcsMassWork
        {
        public:
            MegaScatterReleaseJob(World &world, uint32_t batchId, Ecs::EcsArchetypeId archetype, uint32_t chunkGroup,
                                  uint32_t block, Container::VariableArray<Ecs::EcsEntity> &&rows, uint32_t instanceCount,
                                  uint32_t rangeCount, uint64_t startRun)
                : m_World(world),
                  m_BatchId(batchId),
                  m_Archetype(archetype),
                  m_ChunkGroup(chunkGroup),
                  m_Block(block),
                  m_Rows(std::move(rows)),
                  m_InstanceCount(instanceCount),
                  m_RangeCount(rangeCount),
                  m_StartRun(startRun)
            {
            }

            bool HasMassWork() const override
            {
                return !m_bFinished;
            }

            void StepMassWork(Ecs::EcsMassBudgetCursor &cursor) override
            {
                if (m_bFinished)
                {
                    return;
                }
                Ecs::EcsWorld &ecs = m_World.GetEcsWorld();
                // 散布のアーキタイプにはこの散布の行だけが入っているので、後ろのチャンクからチャンクごと返す
                // （行ごとの理由 Destroy のフックで席も解放される）
                if (m_Archetype != Ecs::kInvalidEcsArchetype && ecs.GetArchetypeRowCount(m_Archetype) > 0)
                {
                    const uint32_t chunksBefore = ecs.GetChunkCount();
                    const uint32_t destroyed = ecs.DestroyArchetypeTailChunks(m_Archetype, cursor.GetRemainingRows());
                    m_ChunksReturned += chunksBefore - ecs.GetChunkCount();
                    m_Destroyed += destroyed;
                    cursor.Consume(destroyed);
                    return;
                }
                Finish(ecs);
                cursor.Consume(0);
            }

            void AuditMassWork(Ecs::EcsMassAudit &audit) const override
            {
                // 解放した範囲は描かない: この散布の鍵の proxy が 1 つも残っていない
                const Rendering::SceneView *view = m_World.GetSceneView();
                audit.Ranges += m_RangeCount;
                if (view == nullptr)
                {
                    return;
                }
                for (uint32_t input = 0; input < m_InstanceCount; ++input)
                {
                    if (view->FindMegaGeometryProxy(Rendering::MakeMegaScatterProxyKey(m_BatchId, input)) != nullptr)
                    {
                        NoteAuditFailure(audit, "解放した散布 %u の範囲 %u に proxy が残っている（入力 %u）", m_BatchId,
                                         input / MegaGeometryScatterComponent::kRangeRows, input);
                        return;
                    }
                }
            }

        private:
            void Finish(Ecs::EcsWorld &ecs)
            {
                Ecs::EcsGpuSceneSlotTable &table = ecs.GetGpuSceneSlots();
                // 列の追加などで別のアーキタイプへ移った行が残っていれば、1 行ずつ消す
                for (uint32_t row = static_cast<uint32_t>(m_Rows.size()); row-- > 0;)
                {
                    if (!m_Rows[row].IsNull() && ecs.IsAlive(m_Rows[row]))
                    {
                        ecs.DestroyEntity(m_Rows[row]);
                        ++m_Destroyed;
                    }
                }
                if (m_Block != Ecs::EcsGpuSceneSlotTable::kInvalidBlock)
                {
                    table.ReleaseBlock(m_Block);
                }
                if (m_ChunkGroup != 0 && !ecs.ReleaseChunkGroup(m_ChunkGroup))
                {
                    LOG_WARNING_F("MegaGeometryScatterComponent: チャンクの組 %u に行が残っているので返せませんでした", m_ChunkGroup);
                }
                const uint64_t frames = ecs.GetMassWork().GetStats().Runs + 1 - m_StartRun;
                LOG_INFO_F("MEGA_SCATTER_RELEASED batch=%u rows=%u destroyed=%u chunks_returned=%u ranges=%u frames=%llu "
                           "ecs_rows=%u slots=%u slots_live=%u table_ranges=%u blocks_in_use=%u",
                           m_BatchId, static_cast<uint32_t>(m_Rows.size()), m_Destroyed, m_ChunksReturned, m_RangeCount,
                           static_cast<unsigned long long>(frames), ecs.GetEntityCount(), table.GetSlotCount(),
                           table.GetLiveCount(), table.GetRangeCount(), table.GetBlocksInUse());
                m_Rows.clear();
                m_bFinished = true;
            }

            World &m_World;
            uint32_t m_BatchId = 0;
            Ecs::EcsArchetypeId m_Archetype = Ecs::kInvalidEcsArchetype;
            uint32_t m_ChunkGroup = 0;
            uint32_t m_Block = Ecs::EcsGpuSceneSlotTable::kInvalidBlock;
            Container::VariableArray<Ecs::EcsEntity> m_Rows;
            uint32_t m_InstanceCount = 0;
            uint32_t m_RangeCount = 0;
            uint64_t m_StartRun = 0;
            uint32_t m_Destroyed = 0;
            uint32_t m_ChunksReturned = 0;
            bool m_bFinished = false;
        };
    } // namespace

    MegaGeometryScatterComponent::MegaGeometryScatterComponent()
        : Component()
    {
        MegaMeshHandleValue = Rendering::MegaGeometry::MegaMeshHandle::Invalid();
        bCastShadow = true;
        bVisible = true;
        Seed = 0;
        InstanceCount = 0;
    }

    MegaGeometryScatterComponent::MegaGeometryScatterComponent(const FieldInitializer *initializer)
        : Component(initializer)
    {
        MegaMeshHandleValue = Rendering::MegaGeometry::MegaMeshHandle::Invalid();
        bCastShadow = true;
        bVisible = true;
        Seed = 0;
        InstanceCount = 0;
    }

    MegaGeometryScatterComponent::MegaGeometryScatterComponent(const IUnknown *sourceObject)
        : Component(sourceObject)
    {
    }

    MegaGeometryScatterComponent::~MegaGeometryScatterComponent()
    {
    }

    void MegaGeometryScatterComponent::Finalize()
    {
        if (m_pRowWorld != nullptr)
        {
            // 行と列の項目は World の木から外れるとき（OnEcsRowDetaching）に片付く。ここで残っているのは World が先に消えた場合で、
            // 行と列は EcsWorld ごと消えているので触らない
            LOG_WARNING_F("MegaGeometryScatterComponent: 行 %u 本を持ったまま終了します（World が先に消えた）", m_CreatedRows);
        }
        m_Rows.clear();
        m_RowToInput.clear();
        m_InputToRow.clear();
        m_Ranges.clear();
        m_Instances.clear();
        m_MeshHandles.clear();
        m_pRowWorld = nullptr;
        m_bMassWorkRegistered = false;
        m_Block = Ecs::EcsGpuSceneSlotTable::kInvalidBlock;
        Component::Finalize();
    }

    void MegaGeometryScatterComponent::Enable()
    {
        Component::Enable();
        ReflectAllRanges(false);
    }

    void MegaGeometryScatterComponent::Disable()
    {
        Component::Disable();
        ReflectAllRanges(false);
    }

    void MegaGeometryScatterComponent::SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle)
    {
        SetMegaMeshHandles(Container::Span<const Rendering::MegaGeometry::MegaMeshHandle>(&handle, 1));
    }

    void MegaGeometryScatterComponent::SetMegaMeshHandles(Container::Span<const Rendering::MegaGeometry::MegaMeshHandle> handles)
    {
        m_MeshHandles.assign(handles.begin(), handles.end());
        MegaMeshHandleValue = m_MeshHandles.empty() ? Rendering::MegaGeometry::MegaMeshHandle::Invalid() : m_MeshHandles[0];
        // 公開中の範囲は前のメッシュのまま描き、範囲ごとに公開し直す
        BumpContentVersion();
        if (m_MeshHandles.empty())
        {
            // 描くメッシュが無くなった: 公開中の範囲はすぐ取り下げる（返されたメッシュを描画が読まない）
            ReflectAllRanges(false);
        }
    }

    void MegaGeometryScatterComponent::SetCastShadow(bool bCastShadow)
    {
        this->bCastShadow = bCastShadow;
        BumpContentVersion();
    }

    void MegaGeometryScatterComponent::SetVisible(bool bVisible)
    {
        this->bVisible = bVisible;
        ReflectAllRanges(false);
    }

    bool MegaGeometryScatterComponent::SetInstances(Container::Span<const MegaScatterInstance> instances)
    {
        assert(!Ecs::Detail::EcsIsParallelActive());
        ReleaseRows();
        m_Instances.assign(instances.begin(), instances.end());
        InstanceCount = static_cast<uint32_t>(m_Instances.size());
        World *world = ResolveWorld();
        if (world == nullptr || GetEcsEntity().IsNull())
        {
            // World の木に付いたとき（OnEcsRowAttached）に作る
            return true;
        }
        return BeginRows(*world);
    }

    bool MegaGeometryScatterComponent::UpdateInstances(uint32_t first, Container::Span<const MegaScatterInstance> values)
    {
        assert(!Ecs::Detail::EcsIsParallelActive());
        const uint64_t end = static_cast<uint64_t>(first) + values.size();
        if (end > m_Instances.size())
        {
            return false;
        }
        for (size_t index = 0; index < values.size(); ++index)
        {
            m_Instances[first + index] = values[index];
        }
        if (m_pRowWorld == nullptr || m_CreatedRanges == 0)
        {
            return true;
        }
        Ecs::EcsWorld &ecs = m_pRowWorld->GetEcsWorld();
        // 範囲の全行を書き終えてから、書いた行を含む範囲だけを公開し直す。書き直しを待っている範囲は、範囲の全行を今の版で書く
        // （前の版の行と混ぜて公開しない）
        Container::VariableArray<uint8_t> touched(m_Ranges.size(), 0);
        for (size_t index = 0; index < values.size(); ++index)
        {
            const uint32_t row = m_InputToRow[first + index];
            const uint32_t range = row / kRangeRows;
            if (!m_Ranges[range].bCreated)
            {
                continue;
            }
            if (m_Ranges[range].WrittenVersion != m_ContentVersion)
            {
                if (touched[range] == 0)
                {
                    RewriteRange(range);
                }
            }
            else
            {
                WriteRow(ecs, row);
            }
            touched[range] = 1;
        }
        for (uint32_t range = 0; range < m_Ranges.size(); ++range)
        {
            if (touched[range] != 0)
            {
                ReflectRange(range, true);
            }
        }
        return true;
    }

    bool MegaGeometryScatterComponent::SetRangeActive(uint32_t range, bool bActive)
    {
        if (range >= m_Ranges.size())
        {
            return false;
        }
        m_Ranges[range].bActive = bActive;
        ReflectRange(range, false);
        return true;
    }

    bool MegaGeometryScatterComponent::SetRangeVisible(uint32_t range, bool bVisible)
    {
        if (range >= m_Ranges.size())
        {
            return false;
        }
        m_Ranges[range].bVisible = bVisible;
        ReflectRange(range, false);
        return true;
    }

    uint32_t MegaGeometryScatterComponent::GetPublishedRangeCount() const
    {
        uint32_t count = 0;
        for (const ScatterRange &range : m_Ranges)
        {
            count += range.bPublished ? 1u : 0u;
        }
        return count;
    }

    bool MegaGeometryScatterComponent::HasPendingWork() const
    {
        if (m_pRowWorld == nullptr)
        {
            return false;
        }
        const bool bCreating = !m_bCreateFailed && m_CreatedRanges < m_Ranges.size();
        return bCreating || m_StaleRanges > 0;
    }

    Ecs::EcsEntity MegaGeometryScatterComponent::GetInstanceRow(uint32_t inputIndex) const
    {
        if (inputIndex >= m_InputToRow.size())
        {
            return Ecs::kNullEcsEntity;
        }
        return m_Rows[m_InputToRow[inputIndex]];
    }

    uint32_t MegaGeometryScatterComponent::GetInstanceRange(uint32_t inputIndex) const
    {
        if (inputIndex >= m_InputToRow.size() || m_Rows[m_InputToRow[inputIndex]].IsNull())
        {
            return kInvalidIndex;
        }
        return m_InputToRow[inputIndex] / kRangeRows;
    }

    void MegaGeometryScatterComponent::OnEcsRowAttached()
    {
        if (m_Instances.empty() || m_pRowWorld != nullptr)
        {
            return;
        }
        if (World *world = ResolveWorld())
        {
            BeginRows(*world);
        }
    }

    void MegaGeometryScatterComponent::OnEcsRowDetaching()
    {
        ReleaseRows();
    }

    World *MegaGeometryScatterComponent::ResolveWorld() const
    {
        const Entity *owner = GetOwner();
        return owner != nullptr ? owner->GetWorld() : nullptr;
    }

    bool MegaGeometryScatterComponent::BeginRows(World &world)
    {
        assert(!Ecs::Detail::EcsIsParallelActive());
        const uint32_t count = static_cast<uint32_t>(m_Instances.size());
        if (count == 0)
        {
            return true;
        }

        Ecs::EcsWorld &ecs = world.GetEcsWorld();
        Ecs::EcsGpuSceneSlotTable &table = ecs.GetGpuSceneSlots();
        Ecs::EcsColumnInfo columns[4];
        Ecs::EcsMegaInstanceColumns(columns);
        // 散布ごとのチャンクの組で作る（ほかの散布の行とチャンクを共有せず、破棄でチャンクごと返せる）
        const uint32_t chunkGroup = ecs.AcquireChunkGroup();
        const Ecs::EcsArchetypeId archetype =
            ecs.GetOrCreateArchetype(Container::Span<const Ecs::EcsColumnInfo>(columns, 4), 0, chunkGroup);
        if (archetype == Ecs::kInvalidEcsArchetype)
        {
            LOG_ERROR("MegaGeometryScatterComponent: 軽い行のアーキタイプを作れませんでした");
            ecs.ReleaseChunkGroup(chunkGroup);
            return false;
        }

        // 席は連続した範囲の組から先頭の順に取る。行は Morton 順に作るので、範囲が空間的にまとまる
        uint32_t firstRange = 0;
        uint32_t rangeCount = 0;
        const uint32_t block = table.ReserveBlock(count, kRangeRows, firstRange, rangeCount);
        if (block == Ecs::EcsGpuSceneSlotTable::kInvalidBlock)
        {
            LOG_ERROR("MegaGeometryScatterComponent: 席の範囲を予約できませんでした");
            ecs.ReleaseChunkGroup(chunkGroup);
            return false;
        }
        Container::VariableArray<uint32_t> order;
        SortByMorton(m_Instances, order);

        m_pRowWorld = &world;
        m_Block = block;
        m_ChunkGroup = chunkGroup;
        m_Archetype = archetype;
        m_BatchId = static_cast<uint32_t>(GetComponentId() & 0x3FFFFFFFull);
        m_Rows.assign(count, Ecs::kNullEcsEntity);
        m_RowToInput = std::move(order);
        m_InputToRow.assign(count, kInvalidIndex);
        for (uint32_t row = 0; row < count; ++row)
        {
            m_InputToRow[m_RowToInput[row]] = row;
        }
        m_Ranges.resize(rangeCount);
        for (uint32_t range = 0; range < rangeCount; ++range)
        {
            ScatterRange &target = m_Ranges[range];
            target = ScatterRange{};
            target.TableRange = firstRange + range;
            target.FirstRow = range * kRangeRows;
            target.RowCount = std::min(kRangeRows, count - target.FirstRow);
        }
        m_CreatedRanges = 0;
        m_CreatedRows = 0;
        m_bCreateFailed = false;
        m_RangeMismatches = 0;
        m_StaleRanges = 0;
        m_RewriteCursor = 0;
        const Entity *owner = GetOwner();
        m_OwnerWorld = owner != nullptr ? owner->GetWorldTransform() : Math::Transform::Identity;

        // 行は仕事の列が範囲ごとに作る（LateTick の段の系が予算で進める）
        Ecs::EcsMassWorkQueue &queue = ecs.GetMassWork();
        m_CreateStartRun = queue.GetStats().Runs;
        queue.Register(&m_MassWork);
        m_bMassWorkRegistered = true;
        EnsureMassWorkSystem(world);
        return true;
    }

    void MegaGeometryScatterComponent::ReleaseRows()
    {
        if (m_pRowWorld == nullptr)
        {
            return;
        }
        assert(!Ecs::Detail::EcsIsParallelActive());
        World &world = *m_pRowWorld;
        Ecs::EcsWorld &ecs = world.GetEcsWorld();
        Ecs::EcsMassWorkQueue &queue = ecs.GetMassWork();
        if (m_bMassWorkRegistered)
        {
            queue.Unregister(&m_MassWork);
            m_bMassWorkRegistered = false;
        }

        // 公開中の proxy はすぐ全部取り下げる（持ち主がこの直後にメッシュを返しても、描画が古いメッシュを読まない）。
        // 取り下げは後ろの範囲・後ろの行から（公開の順の逆なので、SceneView の配列の末尾から外れる）
        if (Rendering::SceneView *view = world.GetSceneView())
        {
            for (uint32_t range = static_cast<uint32_t>(m_Ranges.size()); range-- > 0;)
            {
                ScatterRange &target = m_Ranges[range];
                if (!target.bPublished)
                {
                    continue;
                }
                for (uint32_t row = target.FirstRow + target.RowCount; row-- > target.FirstRow;)
                {
                    view->RemoveMegaGeometryProxy(Rendering::MakeMegaScatterProxyKey(m_BatchId, m_RowToInput[row]));
                }
                target.bPublished = false;
            }
        }

        // 行の削除はチャンクごとに予算で分ける。後始末は列が持つ（この Component が先に消えても続く）
        queue.Adopt(Container::MakeUnique<MegaScatterReleaseJob>(
            world, m_BatchId, m_Archetype, m_ChunkGroup, m_Block, std::move(m_Rows), static_cast<uint32_t>(m_RowToInput.size()),
            static_cast<uint32_t>(m_Ranges.size()), queue.GetStats().Runs));
        EnsureMassWorkSystem(world);

        m_Rows.clear();
        m_RowToInput.clear();
        m_InputToRow.clear();
        m_Ranges.clear();
        m_pRowWorld = nullptr;
        m_Block = Ecs::EcsGpuSceneSlotTable::kInvalidBlock;
        m_ChunkGroup = 0;
        m_Archetype = Ecs::kInvalidEcsArchetype;
        m_CreatedRanges = 0;
        m_CreatedRows = 0;
        m_StaleRanges = 0;
    }

    void MegaGeometryScatterComponent::CreateNextRange(Ecs::EcsMassBudgetCursor &cursor)
    {
        World &world = *m_pRowWorld;
        Ecs::EcsWorld &ecs = world.GetEcsWorld();
        Ecs::EcsGpuSceneSlotTable &table = ecs.GetGpuSceneSlots();
        const uint32_t rangeIndex = m_CreatedRanges;
        ScatterRange &range = m_Ranges[rangeIndex];

        Container::VariableArray<Ecs::EcsEntity> made;
        table.BeginBlockAllocation(m_Block);
        const uint32_t createdCount = ecs.CreateEntities(m_Archetype, range.RowCount, &made);
        table.EndBlockAllocation();
        if (createdCount != range.RowCount || made.size() != range.RowCount)
        {
            LOG_ERROR_F("MegaGeometryScatterComponent: 軽い行を作れませんでした（範囲 %u・要求 %u・実際 %u）", rangeIndex,
                        range.RowCount, createdCount);
            // 作れた分は散布のアーキタイプに入ったまま、解放でチャンクごと返る。残りの範囲は作らない
            m_bCreateFailed = true;
            cursor.Consume(createdCount);
            return;
        }
        for (uint32_t offset = 0; offset < range.RowCount; ++offset)
        {
            const uint32_t row = range.FirstRow + offset;
            m_Rows[row] = made[offset];
            WriteRow(ecs, row);
            const Ecs::EcsGpuSceneSlot *slot = ecs.GetColumnValue<Ecs::EcsGpuSceneSlot>(m_Rows[row]);
            if (slot == nullptr || !slot->IsAssigned() || slot->Range != range.TableRange)
            {
                ++m_RangeMismatches;
            }
        }
        range.bCreated = true;
        range.WrittenVersion = m_ContentVersion;
        ++m_CreatedRanges;
        m_CreatedRows += range.RowCount;
        // 範囲の全行を作って書いた後に、範囲を公開する
        ReflectRange(rangeIndex, false);
        cursor.Consume(range.RowCount);

        if (m_CreatedRanges == m_Ranges.size())
        {
            if (m_RangeMismatches != 0)
            {
                LOG_WARNING_F("MegaGeometryScatterComponent: 予約した範囲の外の席を持つ行が %u 本あります", m_RangeMismatches);
            }
            const Ecs::EcsGpuSceneSlot *firstSlot = ecs.GetColumnValue<Ecs::EcsGpuSceneSlot>(m_Rows[0]);
            const uint64_t frames = ecs.GetMassWork().GetStats().Runs + 1 - m_CreateStartRun;
            LOG_INFO_F("MEGA_SCATTER_ROWS batch=%u rows=%u ranges=%u frames=%llu chunk_group=%u chunks=%u archetype_rows=%u "
                       "first_slot=%u published_ranges=%u ecs_rows=%u slots=%u slots_live=%u table_ranges=%u blocks=%u",
                       m_BatchId, m_CreatedRows, m_CreatedRanges, static_cast<unsigned long long>(frames), m_ChunkGroup,
                       static_cast<uint32_t>(ecs.GetArchetypeChunks(m_Archetype).size()), ecs.GetArchetypeRowCount(m_Archetype),
                       firstSlot != nullptr ? firstSlot->GetIndex() : 0u, GetPublishedRangeCount(), ecs.GetEntityCount(),
                       table.GetSlotCount(), table.GetLiveCount(), table.GetRangeCount(), table.GetBlockCount());
        }
    }

    void MegaGeometryScatterComponent::RewriteNextStaleRange(Ecs::EcsMassBudgetCursor &cursor)
    {
        const uint32_t count = static_cast<uint32_t>(m_Ranges.size());
        for (uint32_t step = 0; step < count; ++step)
        {
            const uint32_t range = (m_RewriteCursor + step) % count;
            const ScatterRange &target = m_Ranges[range];
            if (!target.bCreated || target.WrittenVersion == m_ContentVersion)
            {
                continue;
            }
            RewriteRange(range);
            // 範囲の全行を書き直してから公開し直す（それまでは前の値で描く）
            ReflectRange(range, true);
            m_RewriteCursor = (range + 1) % count;
            cursor.Consume(target.RowCount);
            return;
        }
        // 数え違い（書き直しを待つ範囲が見つからない）。止まったままにしない
        m_StaleRanges = 0;
    }

    void MegaGeometryScatterComponent::RewriteRange(uint32_t range)
    {
        ScatterRange &target = m_Ranges[range];
        Ecs::EcsWorld &ecs = m_pRowWorld->GetEcsWorld();
        for (uint32_t row = target.FirstRow; row < target.FirstRow + target.RowCount; ++row)
        {
            WriteRow(ecs, row);
        }
        if (target.WrittenVersion != m_ContentVersion && m_StaleRanges > 0)
        {
            --m_StaleRanges;
        }
        target.WrittenVersion = m_ContentVersion;
    }

    void MegaGeometryScatterComponent::BumpContentVersion()
    {
        ++m_ContentVersion;
        // 作った範囲は全部、新しい版での書き直しを待つ。まだ作っていない範囲は作るときに新しい版で書く
        m_StaleRanges = m_CreatedRanges;
    }

    void MegaGeometryScatterComponent::WriteRow(Ecs::EcsWorld &ecs, uint32_t row)
    {
        const MegaScatterInstance &input = m_Instances[m_RowToInput[row]];
        // 行のワールドの変換 = 所有者のワールドの変換 × 入力の局所の変換（階層のシステムと同じ式）
        const Math::Transform world =
            Ecs::EcsComposeTransform(m_OwnerWorld, Math::Transform(input.Position, input.Rotation, input.Scale));
        const Ecs::EcsEntity entity = m_Rows[row];
        ecs.WriteColumnValueOf(entity, Ecs::EcsWorldTransform{world.position, world.rotation, world.scale});
        Ecs::EcsMegaInstance instance{};
        instance.MeshIndex = input.MeshIndex;
        instance.Flags = bCastShadow.Get() ? kScatterFlagCastShadow : 0u;
        instance.Scale = std::max(world.scale.x, std::max(world.scale.y, world.scale.z));
        ecs.WriteColumnValueOf(entity, instance);
        ecs.WriteColumnValueOf(entity, Ecs::EcsScatterOwner{m_BatchId});
    }

    Rendering::MegaGeometry::MegaMeshHandle MegaGeometryScatterComponent::ResolveMesh(uint32_t meshIndex) const
    {
        return meshIndex < m_MeshHandles.size() ? m_MeshHandles[meshIndex] : Rendering::MegaGeometry::MegaMeshHandle::Invalid();
    }

    bool MegaGeometryScatterComponent::BuildProxy(const Ecs::EcsWorld &ecs, uint32_t row, Rendering::MegaGeometryProxy &outProxy) const
    {
        const Ecs::EcsEntity entity = m_Rows[row];
        const Ecs::EcsWorldTransform *world = ecs.GetColumnValue<Ecs::EcsWorldTransform>(entity);
        const Ecs::EcsMegaInstance *instance = ecs.GetColumnValue<Ecs::EcsMegaInstance>(entity);
        const Ecs::EcsGpuSceneSlot *slot = ecs.GetColumnValue<Ecs::EcsGpuSceneSlot>(entity);
        if (world == nullptr || instance == nullptr || slot == nullptr)
        {
            return false;
        }
        const Rendering::MegaGeometry::MegaMeshHandle mesh = ResolveMesh(instance->MeshIndex);
        if (!mesh.IsValid())
        {
            return false;
        }
        // 変換と境界は、1 本ずつの MegaGeometryComponent（MeshComponent の既定の局所の箱 ±1）と同じ式で求める
        const Math::Matrix4x4 matrix = Math::MatrixUtils::CreateWorldRowVector(world->position, world->rotation, world->scale);
        const float localRadius = std::sqrt(1.0f * 1.0f + 1.0f * 1.0f + 1.0f * 1.0f);
        const Math::Vector3 translation = matrix.GetTranslationRow();
        const Math::Vector3 scale = Math::MatrixUtils::ExtractScale(matrix);
        const float maxScale = scale.x > scale.y ? (scale.x > scale.z ? scale.x : scale.z) : (scale.y > scale.z ? scale.y : scale.z);

        outProxy = Rendering::MegaGeometryProxy{};
        outProxy.ObjectId = Rendering::MakeMegaScatterProxyKey(m_BatchId, m_RowToInput[row]);
        outProxy.ComponentId = GetComponentId();
        outProxy.MegaMeshHandle = mesh;
        outProxy.WorldTransform = matrix;
        outProxy.PreviousWorldTransform = matrix;
        outProxy.WorldBounds.CenterX = translation.x + 0.0f;
        outProxy.WorldBounds.CenterY = translation.y + 0.0f;
        outProxy.WorldBounds.CenterZ = translation.z + 0.0f;
        outProxy.WorldBounds.Radius = localRadius * maxScale;
        outProxy.bVisible = true;
        outProxy.bCastShadow = (instance->Flags & kScatterFlagCastShadow) != 0;
        outProxy.LayerMask = Rendering::RenderLayer::Default;
        outProxy.SceneSlot = slot->Value;
        return true;
    }

    bool MegaGeometryScatterComponent::ShouldPublish(const ScatterRange &range) const
    {
        return range.bCreated && IsEnabled() && bVisible.Get() && !m_MeshHandles.empty() && range.bActive && range.bVisible;
    }

    void MegaGeometryScatterComponent::ReflectRange(uint32_t range, bool bRepublish)
    {
        if (m_pRowWorld == nullptr || range >= m_Ranges.size())
        {
            return;
        }
        Rendering::SceneView *view = m_pRowWorld->GetSceneView();
        if (view == nullptr)
        {
            return;
        }
        ScatterRange &target = m_Ranges[range];
        if (!target.bCreated)
        {
            return;
        }
        const bool bWant = ShouldPublish(target);
        if (!bWant)
        {
            if (target.bPublished)
            {
                for (uint32_t row = target.FirstRow + target.RowCount; row-- > target.FirstRow;)
                {
                    view->RemoveMegaGeometryProxy(Rendering::MakeMegaScatterProxyKey(m_BatchId, m_RowToInput[row]));
                }
                target.bPublished = false;
            }
            return;
        }
        if (target.bPublished && !bRepublish)
        {
            return;
        }
        Ecs::EcsWorld &ecs = m_pRowWorld->GetEcsWorld();
        Ecs::EcsGpuSceneSlotTable &table = ecs.GetGpuSceneSlots();
        const bool bFullSync = view->IsMegaFullSync();
        Rendering::MegaGeometryProxy proxy;
        for (uint32_t row = target.FirstRow; row < target.FirstRow + target.RowCount; ++row)
        {
            if (!BuildProxy(ecs, row, proxy))
            {
                // 描けない行（メッシュが表の外・無効）は、前に公開した proxy も外す
                view->RemoveMegaGeometryProxy(Rendering::MakeMegaScatterProxyKey(m_BatchId, m_RowToInput[row]));
                continue;
            }
            // 差分の同期は前の変換の履歴つきで公開し、全量の同期は前の変換をパケットが付け直す（World の公開と同じ）
            if (bFullSync)
            {
                view->UpdateMegaGeometryProxy(proxy);
            }
            else
            {
                view->PublishMegaGeometryProxy(proxy);
            }
            // 席の範囲の AABB を、公開した proxy の境界を包むところまで広げる
            if (const Ecs::EcsGpuSceneSlot *slot = ecs.GetColumnValue<Ecs::EcsGpuSceneSlot>(m_Rows[row]))
            {
                Math::Vector3 boundsMin;
                Math::Vector3 boundsMax;
                SphereToBox(proxy.WorldBounds, boundsMin, boundsMax);
                table.NoteBounds(*slot, boundsMin, boundsMax);
            }
        }
        target.bPublished = true;
    }

    void MegaGeometryScatterComponent::ReflectAllRanges(bool bRepublish)
    {
        for (uint32_t range = 0; range < m_Ranges.size(); ++range)
        {
            ReflectRange(range, bRepublish);
        }
    }

    void MegaGeometryScatterComponent::Audit(Ecs::EcsMassAudit &audit) const
    {
        if (m_pRowWorld == nullptr)
        {
            return;
        }
        const Rendering::SceneView *view = m_pRowWorld->GetSceneView();
        const Ecs::EcsWorld &ecs = m_pRowWorld->GetEcsWorld();
        for (uint32_t rangeIndex = 0; rangeIndex < m_Ranges.size(); ++rangeIndex)
        {
            const ScatterRange &range = m_Ranges[rangeIndex];
            ++audit.Ranges;
            if (view == nullptr)
            {
                continue;
            }
            if (range.bPublished)
            {
                ++audit.PublishedRanges;
                if (!ShouldPublish(range))
                {
                    NoteAuditFailure(audit, "散布 %u の範囲 %u が描かない印のまま公開されている（作成 %u）", m_BatchId, rangeIndex,
                                     range.bCreated ? 1u : 0u);
                    continue;
                }
                // 公開中の範囲は全行がそろっている: 行が生きていて、描ける全行の proxy が行の値と一致する
                Rendering::MegaGeometryProxy expected;
                for (uint32_t row = range.FirstRow; row < range.FirstRow + range.RowCount; ++row)
                {
                    const uint64_t key = Rendering::MakeMegaScatterProxyKey(m_BatchId, m_RowToInput[row]);
                    const Rendering::MegaGeometryProxy *actual = view->FindMegaGeometryProxy(key);
                    if (m_Rows[row].IsNull() || !ecs.IsAlive(m_Rows[row]))
                    {
                        NoteAuditFailure(audit, "散布 %u の公開中の範囲 %u の行 %u が無い", m_BatchId, rangeIndex, row);
                        break;
                    }
                    if (!BuildProxy(ecs, row, expected))
                    {
                        if (actual != nullptr)
                        {
                            NoteAuditFailure(audit, "散布 %u の範囲 %u の描けない行 %u の proxy が残っている", m_BatchId,
                                             rangeIndex, row);
                            break;
                        }
                        continue;
                    }
                    if (actual == nullptr)
                    {
                        NoteAuditFailure(audit, "散布 %u の公開中の範囲 %u の行 %u の proxy が無い", m_BatchId, rangeIndex, row);
                        break;
                    }
                    if (actual->SceneSlot != expected.SceneSlot || !(actual->MegaMeshHandle == expected.MegaMeshHandle) ||
                        std::memcmp(&actual->WorldTransform, &expected.WorldTransform, sizeof(expected.WorldTransform)) != 0 ||
                        actual->bCastShadow != expected.bCastShadow)
                    {
                        NoteAuditFailure(audit, "散布 %u の公開中の範囲 %u の行 %u の proxy が行の値と違う", m_BatchId, rangeIndex,
                                         row);
                        break;
                    }
                }
            }
            else
            {
                // 公開していない範囲（作成待ち・印で外した範囲）は描かない
                for (uint32_t row = range.FirstRow; row < range.FirstRow + range.RowCount; ++row)
                {
                    if (view->FindMegaGeometryProxy(Rendering::MakeMegaScatterProxyKey(m_BatchId, m_RowToInput[row])) != nullptr)
                    {
                        NoteAuditFailure(audit, "散布 %u の公開していない範囲 %u の行 %u の proxy がある", m_BatchId, rangeIndex,
                                         row);
                        break;
                    }
                }
            }
        }
    }

    // ---- 仕事の列の項目 ----

    void MegaGeometryScatterComponent::MassWork::PollMassWork()
    {
        MegaGeometryScatterComponent &owner = m_Owner;
        if (owner.m_pRowWorld == nullptr)
        {
            return;
        }
        const Entity *entity = owner.GetOwner();
        const Math::Transform current = entity != nullptr ? entity->GetWorldTransform() : Math::Transform::Identity;
        if (!SameTransformValues(current, owner.m_OwnerWorld))
        {
            // 所有者が動いた: 作った範囲を範囲ごとに書き直す（それまでは前の値で描く）
            owner.m_OwnerWorld = current;
            owner.BumpContentVersion();
        }
    }

    bool MegaGeometryScatterComponent::MassWork::HasMassWork() const
    {
        return m_Owner.HasPendingWork();
    }

    void MegaGeometryScatterComponent::MassWork::StepMassWork(Ecs::EcsMassBudgetCursor &cursor)
    {
        MegaGeometryScatterComponent &owner = m_Owner;
        if (owner.m_pRowWorld == nullptr)
        {
            return;
        }
        // 作成を先に進める（作った範囲は今の版で書くので、書き直しの数を増やさない）
        if (!owner.m_bCreateFailed && owner.m_CreatedRanges < owner.m_Ranges.size())
        {
            owner.CreateNextRange(cursor);
            return;
        }
        if (owner.m_StaleRanges > 0)
        {
            owner.RewriteNextStaleRange(cursor);
        }
    }

    void MegaGeometryScatterComponent::MassWork::AuditMassWork(Ecs::EcsMassAudit &audit) const
    {
        m_Owner.Audit(audit);
    }

} // namespace NorvesLib::Core::Component
