#include "Component/MegaGeometryScatterComponent.h"

#include "Ecs/EcsGpuSceneSlots.h"
#include "Ecs/EcsTransformSystem.h"
#include "Ecs/EcsWorld.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/SceneView.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(MegaGeometryScatterComponent, Component)

    namespace
    {
        /// 行の値の flags: 影を落とす
        constexpr uint32_t kScatterFlagCastShadow = 1u << 0;

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
        if (!m_Rows.empty())
        {
            // 行は World の木から外れるとき（OnEcsRowDetaching）に消える。ここで残っているのは World が先に消えた場合で、
            // 行は EcsWorld ごと消えているので触らない
            LOG_WARNING_F("MegaGeometryScatterComponent: 行 %u 本を持ったまま終了します（World が先に消えた）",
                          static_cast<uint32_t>(m_Rows.size()));
        }
        m_Rows.clear();
        m_RowToInput.clear();
        m_InputToRow.clear();
        m_Ranges.clear();
        m_Instances.clear();
        m_pRowWorld = nullptr;
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
        MegaMeshHandleValue = handle;
        ReflectAllRanges(true);
    }

    void MegaGeometryScatterComponent::SetCastShadow(bool bCastShadow)
    {
        this->bCastShadow = bCastShadow;
        if (m_pRowWorld == nullptr || m_Rows.empty())
        {
            return;
        }
        Ecs::EcsWorld &ecs = m_pRowWorld->GetEcsWorld();
        for (const Ecs::EcsEntity row : m_Rows)
        {
            if (Ecs::EcsMegaInstance *instance = ecs.GetColumnValue<Ecs::EcsMegaInstance>(row))
            {
                Ecs::EcsMegaInstance next = *instance;
                next.Flags = bCastShadow ? (next.Flags | kScatterFlagCastShadow) : (next.Flags & ~kScatterFlagCastShadow);
                ecs.WriteColumnValueOf(row, next);
            }
        }
        ReflectAllRanges(true);
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
        return BuildRows(*world);
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
        if (m_pRowWorld == nullptr || m_Rows.empty())
        {
            return true;
        }
        Ecs::EcsWorld &ecs = m_pRowWorld->GetEcsWorld();
        const Entity *owner = GetOwner();
        const Math::Transform ownerWorld = owner != nullptr ? owner->GetWorldTransform() : Math::Transform::Identity;
        // 範囲の全行を書き終えてから、書いた行を含む範囲だけを公開し直す
        Container::VariableArray<uint8_t> touched(m_Ranges.size(), 0);
        for (size_t index = 0; index < values.size(); ++index)
        {
            const uint32_t row = m_InputToRow[first + index];
            WriteRow(ecs, row, ownerWorld);
            touched[row / kRangeRows] = 1;
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
        if (inputIndex >= m_InputToRow.size())
        {
            return kInvalidIndex;
        }
        return m_InputToRow[inputIndex] / kRangeRows;
    }

    void MegaGeometryScatterComponent::OnEcsRowAttached()
    {
        if (m_Instances.empty() || !m_Rows.empty())
        {
            return;
        }
        if (World *world = ResolveWorld())
        {
            BuildRows(*world);
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

    bool MegaGeometryScatterComponent::BuildRows(World &world)
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
        const Ecs::EcsArchetypeId archetype = ecs.GetOrCreateArchetype(Container::Span<const Ecs::EcsColumnInfo>(columns, 4));
        if (archetype == Ecs::kInvalidEcsArchetype)
        {
            LOG_ERROR("MegaGeometryScatterComponent: 軽い行のアーキタイプを作れませんでした");
            return false;
        }

        // 席は連続した範囲の組から先頭の順に取る。行は Morton 順に作るので、範囲が空間的にまとまる
        uint32_t firstRange = 0;
        uint32_t rangeCount = 0;
        const uint32_t block = table.ReserveBlock(count, kRangeRows, firstRange, rangeCount);
        if (block == Ecs::EcsGpuSceneSlotTable::kInvalidBlock)
        {
            LOG_ERROR("MegaGeometryScatterComponent: 席の範囲を予約できませんでした");
            return false;
        }
        Container::VariableArray<uint32_t> order;
        SortByMorton(m_Instances, order);

        table.BeginBlockAllocation(block);
        Container::VariableArray<Ecs::EcsEntity> rows;
        const uint32_t made = ecs.CreateEntities(archetype, count, &rows);
        table.EndBlockAllocation();
        if (made != count || rows.size() != count)
        {
            LOG_ERROR_F("MegaGeometryScatterComponent: 軽い行を作れませんでした（要求 %u・実際 %u）", count, made);
            for (const Ecs::EcsEntity row : rows)
            {
                ecs.DestroyEntity(row);
            }
            table.ReleaseBlock(block);
            return false;
        }

        m_pRowWorld = &world;
        m_Block = block;
        m_BatchId = static_cast<uint32_t>(GetComponentId() & 0x3FFFFFFFull);
        m_Rows = std::move(rows);
        m_RowToInput = std::move(order);
        m_InputToRow.assign(count, kInvalidIndex);
        for (uint32_t row = 0; row < count; ++row)
        {
            m_InputToRow[m_RowToInput[row]] = row;
        }

        const Entity *owner = GetOwner();
        const Math::Transform ownerWorld = owner != nullptr ? owner->GetWorldTransform() : Math::Transform::Identity;
        for (uint32_t row = 0; row < count; ++row)
        {
            WriteRow(ecs, row, ownerWorld);
        }

        m_Ranges.resize(rangeCount);
        uint32_t rangeMismatches = 0;
        for (uint32_t range = 0; range < rangeCount; ++range)
        {
            ScatterRange &target = m_Ranges[range];
            target = ScatterRange{};
            target.TableRange = firstRange + range;
            target.FirstRow = range * kRangeRows;
            target.RowCount = std::min(kRangeRows, count - target.FirstRow);
            for (uint32_t row = target.FirstRow; row < target.FirstRow + target.RowCount; ++row)
            {
                const Ecs::EcsGpuSceneSlot *slot = ecs.GetColumnValue<Ecs::EcsGpuSceneSlot>(m_Rows[row]);
                if (slot == nullptr || !slot->IsAssigned() || slot->Range != target.TableRange)
                {
                    ++rangeMismatches;
                }
            }
        }
        if (rangeMismatches != 0)
        {
            LOG_WARNING_F("MegaGeometryScatterComponent: 予約した範囲の外の席を持つ行が %u 本あります", rangeMismatches);
        }

        // 範囲ごとに公開する（全行を書き終えた後）
        ReflectAllRanges(false);

        const Ecs::EcsGpuSceneSlot *firstSlot = ecs.GetColumnValue<Ecs::EcsGpuSceneSlot>(m_Rows[0]);
        LOG_INFO_F("MEGA_SCATTER_ROWS batch=%u rows=%u ranges=%u first_slot=%u published_ranges=%u ecs_rows=%u slots=%u "
                   "slots_live=%u table_ranges=%u blocks=%u",
                   m_BatchId, count, rangeCount, firstSlot != nullptr ? firstSlot->GetIndex() : 0u, GetPublishedRangeCount(),
                   ecs.GetEntityCount(), table.GetSlotCount(), table.GetLiveCount(), table.GetRangeCount(), table.GetBlockCount());
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
        Ecs::EcsGpuSceneSlotTable &table = ecs.GetGpuSceneSlots();

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

        // 行の削除（理由が Destroy）で席が解放される。後ろから消すと入れ替えが起きにくい
        const uint32_t rowCount = static_cast<uint32_t>(m_Rows.size());
        for (uint32_t row = rowCount; row-- > 0;)
        {
            if (ecs.IsAlive(m_Rows[row]))
            {
                ecs.DestroyEntity(m_Rows[row]);
            }
        }
        if (m_Block != Ecs::EcsGpuSceneSlotTable::kInvalidBlock)
        {
            table.ReleaseBlock(m_Block);
        }
        LOG_INFO_F("MEGA_SCATTER_RELEASED batch=%u rows=%u ranges=%u ecs_rows=%u slots=%u slots_live=%u table_ranges=%u "
                   "blocks_in_use=%u",
                   m_BatchId, rowCount, static_cast<uint32_t>(m_Ranges.size()), ecs.GetEntityCount(), table.GetSlotCount(),
                   table.GetLiveCount(), table.GetRangeCount(), table.GetBlocksInUse());

        m_Rows.clear();
        m_RowToInput.clear();
        m_InputToRow.clear();
        m_Ranges.clear();
        m_pRowWorld = nullptr;
        m_Block = Ecs::EcsGpuSceneSlotTable::kInvalidBlock;
    }

    void MegaGeometryScatterComponent::WriteRow(Ecs::EcsWorld &ecs, uint32_t row, const Math::Transform &ownerWorld)
    {
        const MegaScatterInstance &input = m_Instances[m_RowToInput[row]];
        // 行のワールドの変換 = 所有者のワールドの変換 × 入力の局所の変換（階層のシステムと同じ式）
        const Math::Transform world =
            Ecs::EcsComposeTransform(ownerWorld, Math::Transform(input.Position, input.Rotation, input.Scale));
        const Ecs::EcsEntity entity = m_Rows[row];
        ecs.WriteColumnValueOf(entity, Ecs::EcsWorldTransform{world.position, world.rotation, world.scale});
        Ecs::EcsMegaInstance instance{};
        instance.MeshIndex = 0;
        instance.Flags = bCastShadow.Get() ? kScatterFlagCastShadow : 0u;
        instance.Scale = std::max(world.scale.x, std::max(world.scale.y, world.scale.z));
        ecs.WriteColumnValueOf(entity, instance);
        ecs.WriteColumnValueOf(entity, Ecs::EcsScatterOwner{m_BatchId});
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
        // 変換と境界は、1 本ずつの MegaGeometryComponent（MeshComponent の既定の局所の箱 ±1）と同じ式で求める
        const Math::Matrix4x4 matrix = Math::MatrixUtils::CreateWorldRowVector(world->position, world->rotation, world->scale);
        const float localRadius = std::sqrt(1.0f * 1.0f + 1.0f * 1.0f + 1.0f * 1.0f);
        const Math::Vector3 translation = matrix.GetTranslationRow();
        const Math::Vector3 scale = Math::MatrixUtils::ExtractScale(matrix);
        const float maxScale = scale.x > scale.y ? (scale.x > scale.z ? scale.x : scale.z) : (scale.y > scale.z ? scale.y : scale.z);

        outProxy = Rendering::MegaGeometryProxy{};
        outProxy.ObjectId = Rendering::MakeMegaScatterProxyKey(m_BatchId, m_RowToInput[row]);
        outProxy.ComponentId = GetComponentId();
        outProxy.MegaMeshHandle = MegaMeshHandleValue.Get();
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
        return IsEnabled() && bVisible.Get() && MegaMeshHandleValue.Get().IsValid() && range.bActive && range.bVisible;
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

} // namespace NorvesLib::Core::Component
