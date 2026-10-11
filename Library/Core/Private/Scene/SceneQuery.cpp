#include "Scene/SceneQuery.h"
#include "Object/World.h"
#include "Object/Entity.h"
#include "Object/ObjectCast.h"
#include "Component/MeshComponent.h"
#include "Debug/EngineChecks.h"
#include "Ecs/EcsRenderSync.h"
#include "Ecs/EcsWorld.h"
#include "Logging/LogMacros.h"
#include "Math/GeometryIntersection.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <functional>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        constexpr uint32_t BVHLeafEntryThreshold = 4;
        /// 保留・墓石が増えても作り直さない下限
        constexpr uint32_t kRebuildSlack = 64;
        /// 累積した refit の回数がこれ以上（Entry 数に対する倍率、下限つき）になったら作り直す
        constexpr uint64_t kRefitRebuildFloor = 256;
        constexpr uint64_t kRefitRebuildFactor = 2;
        /// 開発ログ SCENEQUERY を出す間隔（Update の回数）
        constexpr uint64_t kReportInterval = 120;

        /// 行を 1 つの値に詰める（World の逆引きと同じ形）
        constexpr uint64_t PackRow(Ecs::EcsEntity entity)
        {
            return (static_cast<uint64_t>(entity.Generation) << 32) | entity.Index;
        }

        bool SameFloat(float lhs, float rhs)
        {
            return lhs == rhs || std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
        }

        bool SameBounds(const Math::AABB& lhs, const Math::AABB& rhs)
        {
            return SameFloat(lhs.Min.x, rhs.Min.x) && SameFloat(lhs.Min.y, rhs.Min.y) && SameFloat(lhs.Min.z, rhs.Min.z)
                && SameFloat(lhs.Max.x, rhs.Max.x) && SameFloat(lhs.Max.y, rhs.Max.y) && SameFloat(lhs.Max.z, rhs.Max.z);
        }

        /// 行から Entity を引く（Entity の行でない・Object を切られた行は null）
        Entity* ResolveEntityRow(Ecs::EcsWorld& ecs, Ecs::EcsEntity row)
        {
            if (row.IsNull() || !ecs.IsAlive(row))
            {
                return nullptr;
            }
            const Ecs::EcsObjectRef* ref = ecs.GetColumnValue<Ecs::EcsObjectRef>(row);
            const Ecs::EcsHierarchy* hierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(row);
            if (ref == nullptr || ref->Object == nullptr || ref->Generation != row.Generation
                || hierarchy == nullptr || hierarchy->Kind != Ecs::kEcsHierarchyKindEntity)
            {
                return nullptr;
            }
            return static_cast<Entity*>(ref->Object);
        }

        /// 自分と祖先がすべて Active で、破棄待ちでない（Rebuild の再帰が子を辿る条件と同じ）
        bool IsEffectivelyActive(const Entity& entity)
        {
            for (const Entity* cursor = &entity; cursor != nullptr; cursor = cursor->GetParentEntity())
            {
                if (!cursor->IsActive() || cursor->IsPendingDestroy())
                {
                    return false;
                }
            }
            return true;
        }

        /// 行の削除のフックの呼び出し先。追っている World の行だけを、削除の一覧へ積む
        void OnRowDestroyed(void* user, Ecs::EcsEntity entity, const void* /*value*/, Ecs::EcsHookReason /*reason*/)
        {
            auto* hookUser = static_cast<SceneQuery::HookUser*>(user);
            if (hookUser == nullptr || hookUser->Destroyed == nullptr || hookUser->Current == nullptr
                || *hookUser->Current != hookUser->Tag)
            {
                return;
            }
            hookUser->Destroyed->push_back(PackRow(entity));
        }
    } // namespace

    SceneQuery::SceneQuery()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId())
    {
    }

    void SceneQuery::Rebuild(const World& world)
    {
        DetachTracking();
        m_Entries.clear();

        auto rootEntities = world.GetRootEntities();
        for (Entity* entity : rootEntities)
        {
            CollectRecursive(entity, m_Entries);
        }

        m_NextOriginalIndex = static_cast<uint32_t>(m_Entries.size());
        BuildBVH();
    }

    void SceneQuery::Rebuild(Container::Span<Entity* const> entities)
    {
        DetachTracking();
        m_Entries.clear();

        for (Entity* entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            Math::AABB bounds;
            if (entity->GetWorldAABB(bounds))
            {
                const uint32_t originalIndex = static_cast<uint32_t>(m_Entries.size());
                m_Entries.push_back({entity, bounds, originalIndex});
            }
        }

        m_NextOriginalIndex = static_cast<uint32_t>(m_Entries.size());
        BuildBVH();
    }

    void SceneQuery::Clear()
    {
        DetachTracking();
        m_Entries.clear();
        m_Nodes.clear();
    }

    bool SceneQuery::Raycast(const Math::Ray& ray, RaycastHit& outHit) const
    {
        outHit = RaycastHit{};
        float bestT = FLT_MAX;
        uint32_t bestOriginalIndex = static_cast<uint32_t>(-1);
        bool bFoundHit = false;
        if (!m_Nodes.empty())
        {
            RaycastNode(0, ray, outHit, bestT, bestOriginalIndex);
        }
        for (const Entry& entry : m_Pending)
        {
            RaycastEntry(entry, ray, outHit, bestT, bestOriginalIndex, bFoundHit);
        }
        return outHit.bHit;
    }

    void SceneQuery::OverlapSphere(
        const Math::Sphere& sphere,
        Container::VariableArray<Entity*>& outEntities) const
    {
        outEntities.clear();
        if (!m_Nodes.empty())
        {
            CollectSphereNode(0, sphere, outEntities);
        }
        for (const Entry& entry : m_Pending)
        {
            if (Math::SphereIntersectsAABB(sphere, entry.Bounds))
            {
                outEntities.push_back(entry.EntityPtr);
            }
        }
    }

    void SceneQuery::OverlapBox(
        const Math::AABB& box,
        Container::VariableArray<Entity*>& outEntities) const
    {
        outEntities.clear();
        if (!m_Nodes.empty())
        {
            CollectBoxNode(0, box, outEntities);
        }
        for (const Entry& entry : m_Pending)
        {
            if (Math::AABBIntersectsAABB(box, entry.Bounds))
            {
                outEntities.push_back(entry.EntityPtr);
            }
        }
    }

    void SceneQuery::QueryFrustum(
        const Math::Frustum& frustum,
        Container::VariableArray<Entity*>& outEntities) const
    {
        outEntities.clear();
        if (!m_Nodes.empty())
        {
            CollectFrustumNode(0, frustum, outEntities);
        }
        for (const Entry& entry : m_Pending)
        {
            if (Math::FrustumIntersectsAABB(frustum, entry.Bounds))
            {
                outEntities.push_back(entry.EntityPtr);
            }
        }
    }

    size_t SceneQuery::GetEntryCount() const
    {
        return m_Entries.size() - m_TombstoneCount + m_Pending.size();
    }

    EPhysicsSceneQueryResult SceneQuery::BindPhysicsProvider(IPhysicsSceneQueryProvider& provider)
    {
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }

        if (m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::AlreadyBound;
        }

        m_PhysicsProvider = &provider;
        return EPhysicsSceneQueryResult::Success;
    }

    EPhysicsSceneQueryResult SceneQuery::UnbindPhysicsProvider(IPhysicsSceneQueryProvider& provider)
    {
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }

        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        if (m_PhysicsProvider != &provider)
        {
            return EPhysicsSceneQueryResult::ProviderMismatch;
        }

        m_PhysicsProvider = nullptr;
        return EPhysicsSceneQueryResult::Success;
    }

    EPhysicsSceneQueryResult SceneQuery::RefreshDynamicSnapshot()
    {
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }
        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }
        return m_PhysicsProvider->RefreshDynamicSnapshot();
    }

    EPhysicsSceneQueryResult SceneQuery::ExecuteBatch(Container::Span<const PhysicsQueryDesc> queries,
        Container::VariableArray<PhysicsQueryHit>& outHits,
        Container::VariableArray<PhysicsQueryBatchResult>& outResults) const
    {
        outHits.clear();
        outResults.clear();
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }
        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }
        if (queries.size() != 0 && queries.data() == nullptr)
        {
            return EPhysicsSceneQueryResult::InvalidArgument;
        }
        try
        {
            const auto result = m_PhysicsProvider->ExecuteBatch(queries, outHits, outResults);
            if (result != EPhysicsSceneQueryResult::Success)
            {
                outHits.clear();
                outResults.clear();
            }
            return result;
        }
        catch (...)
        {
            outHits.clear();
            outResults.clear();
            throw;
        }
    }

    EPhysicsSceneQueryResult SceneQuery::ExecuteQuery(const PhysicsQueryDesc& query,
        Container::VariableArray<PhysicsQueryHit>& outHits) const
    {
        outHits.clear();
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }
        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }
        try
        {
            const auto result = m_PhysicsProvider->ExecuteQuery(query, outHits);
            if (result != EPhysicsSceneQueryResult::Success)
            {
                outHits.clear();
            }
            return result;
        }
        catch (...)
        {
            outHits.clear();
            throw;
        }
    }

    EPhysicsSceneQueryResult SceneQuery::Raycast(
        const Math::Ray& ray,
        float maxDistance,
        PhysicsRaycastHit& outHit) const
    {
        if (!IsOwnerThread())
        {
            outHit = PhysicsRaycastHit{};
            return EPhysicsSceneQueryResult::WrongThread;
        }

        outHit = PhysicsRaycastHit{};

        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        const EPhysicsSceneQueryResult result = m_PhysicsProvider->Raycast(ray, maxDistance, outHit);
        if (result != EPhysicsSceneQueryResult::Success)
        {
            outHit = PhysicsRaycastHit{};
        }

        return result;
    }

    EPhysicsSceneQueryResult SceneQuery::OverlapSphere(
        const Math::Sphere& sphere,
        Container::VariableArray<PhysicsOverlapHit>& outHits) const
    {
        if (!IsOwnerThread())
        {
            outHits.clear();
            return EPhysicsSceneQueryResult::WrongThread;
        }

        outHits.clear();

        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        const EPhysicsSceneQueryResult result = m_PhysicsProvider->OverlapSphere(sphere, outHits);
        if (result != EPhysicsSceneQueryResult::Success)
        {
            outHits.clear();
        }

        return result;
    }

    EPhysicsSceneQueryResult SceneQuery::OverlapBox(
        const Math::OBB& box,
        Container::VariableArray<PhysicsOverlapHit>& outHits) const
    {
        if (!IsOwnerThread())
        {
            outHits.clear();
            return EPhysicsSceneQueryResult::WrongThread;
        }

        outHits.clear();

        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        const EPhysicsSceneQueryResult result = m_PhysicsProvider->OverlapBox(box, outHits);
        if (result != EPhysicsSceneQueryResult::Success)
        {
            outHits.clear();
        }

        return result;
    }

    EPhysicsSceneQueryResult SceneQuery::OverlapCapsule(
        const Math::Capsule& capsule,
        Container::VariableArray<PhysicsOverlapHit>& outHits) const
    {
        if (!IsOwnerThread())
        {
            outHits.clear();
            return EPhysicsSceneQueryResult::WrongThread;
        }

        outHits.clear();

        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        const EPhysicsSceneQueryResult result = m_PhysicsProvider->OverlapCapsule(capsule, outHits);
        if (result != EPhysicsSceneQueryResult::Success)
        {
            outHits.clear();
        }

        return result;
    }

    EPhysicsSceneQueryResult SceneQuery::IsAlive(ColliderHandle collider, bool& outAlive) const
    {
        if (!IsOwnerThread())
        {
            outAlive = false;
            return EPhysicsSceneQueryResult::WrongThread;
        }

        outAlive = false;

        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        const EPhysicsSceneQueryResult result = m_PhysicsProvider->IsAlive(collider, outAlive);
        if (result != EPhysicsSceneQueryResult::Success)
        {
            outAlive = false;
        }

        return result;
    }

    EPhysicsSceneQueryResult SceneQuery::IsAlive(BodyHandle body, bool& outAlive) const
    {
        if (!IsOwnerThread())
        {
            outAlive = false;
            return EPhysicsSceneQueryResult::WrongThread;
        }

        outAlive = false;

        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        const EPhysicsSceneQueryResult result = m_PhysicsProvider->IsAlive(body, outAlive);
        if (result != EPhysicsSceneQueryResult::Success)
        {
            outAlive = false;
        }

        return result;
    }

    EPhysicsSceneQueryResult SceneQuery::GetPublishedSnapshotSequence(uint64_t& outSequence) const
    {
        outSequence = 0;
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }

        if (!m_PhysicsProvider)
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        const EPhysicsSceneQueryResult result = m_PhysicsProvider->GetPublishedSnapshotSequence(outSequence);
        if (result != EPhysicsSceneQueryResult::Success)
        {
            outSequence = 0;
        }
        return result;
    }

    bool SceneQuery::IsOwnerThread() const
    {
        return m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    void SceneQuery::BuildBVH()
    {
        m_Nodes.clear();
        m_Pending.clear();
        m_TombstoneCount = 0;
        m_UpdatesSinceBuild = 0;
        if (m_Entries.empty())
        {
            return;
        }

        BuildNode(0, static_cast<uint32_t>(m_Entries.size()), kNoNode);
    }

    uint32_t SceneQuery::BuildNode(uint32_t start, uint32_t count, uint32_t parent)
    {
        Math::AABB bounds = Math::AABB::CreateInvalid();
        const uint32_t end = start + count;
        for (uint32_t entryIndex = start; entryIndex < end; ++entryIndex)
        {
            bounds.Merge(m_Entries[entryIndex].Bounds);
        }

        BVHNode node;
        node.Bounds = bounds;
        node.Start = start;
        node.Count = count;
        node.Parent = parent;

        const uint32_t nodeIndex = static_cast<uint32_t>(m_Nodes.size());
        // 葉にする。含む Entry に葉の節を覚えさせる（refit が Entry から葉へ辿る）
        auto finishLeaf = [&]() -> uint32_t
        {
            node.bLeaf = true;
            m_Nodes.push_back(node);
            for (uint32_t entryIndex = start; entryIndex < end; ++entryIndex)
            {
                m_Entries[entryIndex].Leaf = nodeIndex;
            }
            return nodeIndex;
        };

        if (count <= BVHLeafEntryThreshold)
        {
            return finishLeaf();
        }

        const Math::Vector3 extents = bounds.Extents();
        uint32_t axis = 0;
        float longestExtent = extents.x;
        if (extents.y > longestExtent)
        {
            axis = 1;
            longestExtent = extents.y;
        }
        if (extents.z > longestExtent)
        {
            axis = 2;
            longestExtent = extents.z;
        }

        if (longestExtent <= 0.0f)
        {
            return finishLeaf();
        }

        // 分割軸の中心がすべて同じなら分けられない
        float minCenter = FLT_MAX;
        float maxCenter = -FLT_MAX;
        for (uint32_t entryIndex = start; entryIndex < end; ++entryIndex)
        {
            const float center = GetCenterAxis(m_Entries[entryIndex].Bounds, axis);
            minCenter = center < minCenter ? center : minCenter;
            maxCenter = center > maxCenter ? center : maxCenter;
        }
        if (minCenter == maxCenter)
        {
            return finishLeaf();
        }

        // 中央値で分ける。全体を整列せず、中央値の位置だけを求める（O(count)）ので、作り直しは全体で O(N log N)
        const uint32_t split = start + count / 2;
        std::nth_element(
            m_Entries.begin() + start,
            m_Entries.begin() + split,
            m_Entries.begin() + end,
            [axis](const Entry& lhs, const Entry& rhs)
            {
                return GetCenterAxis(lhs.Bounds, axis) < GetCenterAxis(rhs.Bounds, axis);
            });

        const uint32_t leftCount = split - start;
        const uint32_t rightCount = end - split;
        if (leftCount == 0 || rightCount == 0)
        {
            return finishLeaf();
        }

        m_Nodes.push_back(node);
        const uint32_t left = BuildNode(start, leftCount, nodeIndex);
        const uint32_t right = BuildNode(split, rightCount, nodeIndex);

        m_Nodes[nodeIndex].Start = 0;
        m_Nodes[nodeIndex].Count = 0;
        m_Nodes[nodeIndex].Left = left;
        m_Nodes[nodeIndex].Right = right;
        m_Nodes[nodeIndex].bLeaf = false;
        return nodeIndex;
    }

    void SceneQuery::RaycastEntry(
        const Entry& entry,
        const Math::Ray& ray,
        RaycastHit& outHit,
        float& bestT,
        uint32_t& bestOriginalIndex,
        bool& bFoundHit) const
    {
        if (entry.EntityPtr == nullptr)
        {
            return;
        }

        float t = 0.0f;
        if (Math::RayIntersectsAABB(ray, entry.Bounds, t)
            && (t < bestT || (t == bestT && entry.OriginalIndex < bestOriginalIndex)))
        {
            bestT = t;
            bestOriginalIndex = entry.OriginalIndex;
            outHit.HitEntity = entry.EntityPtr;
            outHit.Distance = t;
            outHit.bHit = true;
            bFoundHit = true;
        }
    }

    bool SceneQuery::RaycastNode(
        uint32_t nodeIndex,
        const Math::Ray& ray,
        RaycastHit& outHit,
        float& bestT,
        uint32_t& bestOriginalIndex) const
    {
        const BVHNode& node = m_Nodes[nodeIndex];

        float nodeT = 0.0f;
        if (!Math::RayIntersectsAABB(ray, node.Bounds, nodeT) || nodeT > bestT)
        {
            return false;
        }

        bool bFoundHit = false;
        if (node.bLeaf)
        {
            const uint32_t end = node.Start + node.Count;
            for (uint32_t entryIndex = node.Start; entryIndex < end; ++entryIndex)
            {
                RaycastEntry(m_Entries[entryIndex], ray, outHit, bestT, bestOriginalIndex, bFoundHit);
            }

            return bFoundHit;
        }

        if (RaycastNode(node.Left, ray, outHit, bestT, bestOriginalIndex))
        {
            bFoundHit = true;
        }
        if (RaycastNode(node.Right, ray, outHit, bestT, bestOriginalIndex))
        {
            bFoundHit = true;
        }

        return bFoundHit;
    }

    void SceneQuery::CollectSphereNode(
        uint32_t nodeIndex,
        const Math::Sphere& sphere,
        Container::VariableArray<Entity*>& outEntities) const
    {
        const BVHNode& node = m_Nodes[nodeIndex];
        if (!Math::SphereIntersectsAABB(sphere, node.Bounds))
        {
            return;
        }

        if (node.bLeaf)
        {
            const uint32_t end = node.Start + node.Count;
            for (uint32_t entryIndex = node.Start; entryIndex < end; ++entryIndex)
            {
                const Entry& entry = m_Entries[entryIndex];
                if (entry.EntityPtr != nullptr && Math::SphereIntersectsAABB(sphere, entry.Bounds))
                {
                    outEntities.push_back(entry.EntityPtr);
                }
            }

            return;
        }

        CollectSphereNode(node.Left, sphere, outEntities);
        CollectSphereNode(node.Right, sphere, outEntities);
    }

    void SceneQuery::CollectBoxNode(
        uint32_t nodeIndex,
        const Math::AABB& box,
        Container::VariableArray<Entity*>& outEntities) const
    {
        const BVHNode& node = m_Nodes[nodeIndex];
        if (!Math::AABBIntersectsAABB(box, node.Bounds))
        {
            return;
        }

        if (node.bLeaf)
        {
            const uint32_t end = node.Start + node.Count;
            for (uint32_t entryIndex = node.Start; entryIndex < end; ++entryIndex)
            {
                const Entry& entry = m_Entries[entryIndex];
                if (entry.EntityPtr != nullptr && Math::AABBIntersectsAABB(box, entry.Bounds))
                {
                    outEntities.push_back(entry.EntityPtr);
                }
            }

            return;
        }

        CollectBoxNode(node.Left, box, outEntities);
        CollectBoxNode(node.Right, box, outEntities);
    }

    void SceneQuery::CollectFrustumNode(
        uint32_t nodeIndex,
        const Math::Frustum& frustum,
        Container::VariableArray<Entity*>& outEntities) const
    {
        const BVHNode& node = m_Nodes[nodeIndex];
        if (!Math::FrustumIntersectsAABB(frustum, node.Bounds))
        {
            return;
        }

        if (node.bLeaf)
        {
            const uint32_t end = node.Start + node.Count;
            for (uint32_t entryIndex = node.Start; entryIndex < end; ++entryIndex)
            {
                const Entry& entry = m_Entries[entryIndex];
                if (entry.EntityPtr != nullptr && Math::FrustumIntersectsAABB(frustum, entry.Bounds))
                {
                    outEntities.push_back(entry.EntityPtr);
                }
            }

            return;
        }

        CollectFrustumNode(node.Left, frustum, outEntities);
        CollectFrustumNode(node.Right, frustum, outEntities);
    }

    float SceneQuery::GetCenterAxis(const Math::AABB& bounds, uint32_t axis)
    {
        const Math::Vector3 center = bounds.Center();
        if (axis == 1)
        {
            return center.y;
        }
        if (axis == 2)
        {
            return center.z;
        }

        return center.x;
    }

    void SceneQuery::CollectRecursive(Entity* entity, Container::VariableArray<Entry>& entries)
    {
        if (!entity)
        {
            return;
        }

        if (!entity->IsActive() || entity->IsPendingDestroy())
        {
            return;
        }

        Math::AABB bounds;
        if (entity->GetWorldAABB(bounds))
        {
            const uint32_t originalIndex = static_cast<uint32_t>(entries.size());
            entries.push_back({entity, bounds, originalIndex});
        }

        auto childEntities = entity->GetChildEntities();
        for (Entity* child : childEntities)
        {
            CollectRecursive(child, entries);
        }
    }

    // ========================================
    // 増分の更新（World の変更から、変わった Entity の境界だけを反映する）
    // ========================================

    bool SceneQuery::AttachTracking(World& world)
    {
        Ecs::EcsWorld& ecs = world.GetEcsWorld();
        const TypeId types[3] = {
            Ecs::EcsColumnInfoOf<Ecs::EcsLocalTransform>().Type,
            Ecs::EcsColumnInfoOf<Ecs::EcsHierarchy>().Type,
            Ecs::EcsColumnInfoOf<Ecs::EcsEntityFlags>().Type,
        };

        bool bOk = true;
        for (int i = 0; i < 3; ++i)
        {
            m_Consumers[i] = ecs.RegisterChangeConsumer(types[i]);
            if (m_Consumers[i] == Ecs::kInvalidEcsConsumer)
            {
                bOk = false;
            }
        }

        // 行の削除は変更ログに載らない（死んだ行は読めない）ので、Entity の行が必ず持つ列のフックで拾う
        m_TrackedEcs = &ecs;
        m_HookUser.Destroyed = &m_DestroyedRows;
        m_HookUser.Current = &m_TrackedEcs;
        m_HookUser.Tag = &ecs;
        if (bOk)
        {
            Ecs::EcsColumnHooks hooks;
            hooks.OnDestroy = &OnRowDestroyed;
            hooks.User = &m_HookUser;
            bOk = ecs.SetColumnHooks(types[2], hooks);
        }

        if (!bOk)
        {
            for (uint32_t& consumer : m_Consumers)
            {
                if (consumer != Ecs::kInvalidEcsConsumer)
                {
                    ecs.UnregisterChangeConsumer(consumer);
                }
                consumer = Ecs::kInvalidEcsConsumer;
            }
            m_TrackedEcs = nullptr;
            m_HookUser = HookUser{};
            return false;
        }

        m_TrackedWorld = &world;
        return true;
    }

    void SceneQuery::DetachTracking()
    {
        if (m_TrackedWorld != nullptr)
        {
            Ecs::EcsWorld& ecs = m_TrackedWorld->GetEcsWorld();
            for (const uint32_t consumer : m_Consumers)
            {
                if (consumer != Ecs::kInvalidEcsConsumer)
                {
                    ecs.UnregisterChangeConsumer(consumer);
                }
            }
            ecs.SetColumnHooks(Ecs::EcsColumnInfoOf<Ecs::EcsEntityFlags>().Type, Ecs::EcsColumnHooks{});
        }
        for (uint32_t& consumer : m_Consumers)
        {
            consumer = Ecs::kInvalidEcsConsumer;
        }
        m_TrackedWorld = nullptr;
        m_TrackedEcs = nullptr;
        m_HookUser = HookUser{};
        ForgetTrackedData();
    }

    void SceneQuery::ForgetTrackedData()
    {
        m_Tracked.clear();
        m_MeshOwner.clear();
        m_DestroyedRows.clear();
        m_RefreshedRows.clear();
        m_DescendedRows.clear();
        m_Pending.clear();
        m_TombstoneCount = 0;
        m_UpdatesSinceBuild = 0;
    }

    void SceneQuery::Update(World& world)
    {
        m_LastStats = UpdateStats{};
        ++m_UpdateCalls;

        // 描画の同期が旧の全走査のとき・SceneView が無いとき（同期の一覧が作られない）は、毎回全量で作る
        const bool bIncremental = !world.IsLegacyRenderSync() && world.GetSceneView() != nullptr;
        if (bIncremental && m_TrackedWorld != nullptr && m_TrackedWorld != &world)
        {
            // 別の World に替わった。古い World は消えているかもしれないので触らない
            for (uint32_t& consumer : m_Consumers)
            {
                consumer = Ecs::kInvalidEcsConsumer;
            }
            m_TrackedWorld = nullptr;
            m_TrackedEcs = nullptr;
            m_HookUser = HookUser{};
            ForgetTrackedData();
        }

        // 全量で作る（毎回の切り戻し・追跡を始められないとき）
        auto rebuildAll = [&]()
        {
            Rebuild(world);
            m_LastStats.Rebuilt = 1;
            ++m_TotalRebuilt;
            m_LastStats.Entries = static_cast<uint32_t>(GetEntryCount());
            ReportUpdate();
        };

        if (!bIncremental)
        {
            rebuildAll();
            return;
        }

        if (m_TrackedWorld == nullptr)
        {
            if (!AttachTracking(world))
            {
                rebuildAll();
                return;
            }
            // 登録より前にあった Entity を全部拾う。以後は変更ログから追う
            BuildTrackedFromWorld(world);
            m_LastStats = UpdateStats{};
            m_LastStats.Rebuilt = 1;
            ++m_TotalRebuilt;
            m_LastStats.Entries = static_cast<uint32_t>(GetEntryCount());
            ReportUpdate();
            return;
        }

        Ecs::EcsWorld& ecs = world.GetEcsWorld();
        m_RefreshedRows.clear();
        m_DescendedRows.clear();

        // 削除: 行の削除のフックが積んだ行（外すだけなので Entity は触らない）
        for (const uint64_t rowKey : m_DestroyedRows)
        {
            RemoveTrackedEntity(rowKey);
        }
        m_DestroyedRows.clear();

        // 変更ログ: 局所の変換・階層・フラグ。作成された行は全列が「変わった」になるので、追加もここで拾う
        Container::VariableArray<Ecs::EcsChangeEntry> changes;
        for (int i = 0; i < 3; ++i)
        {
            changes.clear();
            ecs.ReadChanges(m_Consumers[i], changes);
            for (const Ecs::EcsChangeEntry& change : changes)
            {
                Entity* entity = ResolveEntityRow(ecs, change.Entity);
                if (entity == nullptr && i == 1)
                {
                    // Component の行の階層が変わった（付け外し）。持ち主の Entity を更新する
                    const Ecs::EcsHierarchy* hierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(change.Entity);
                    if (hierarchy != nullptr && hierarchy->Kind == Ecs::kEcsHierarchyKindComponent)
                    {
                        entity = ResolveEntityRow(ecs, hierarchy->Parent);
                    }
                }
                if (entity == nullptr)
                {
                    continue;
                }
                // Active・破棄待ちの変化は子孫が走査に入るかを変えるので、部分木ごと見直す。
                // 局所の変換・階層は、子孫の境界の変化を描画同期の一覧（下）が教えるので、その Entity だけ見る
                if (i == 2)
                {
                    RefreshSubtree(world, *entity);
                }
                else
                {
                    RefreshEntity(world, *entity);
                }
            }
        }

        // 描画同期の一覧: ワールドの変換の版・資源の境界・Mesh の付け外しで proxy が変わる MeshComponent
        const Ecs::EcsRenderSyncList& syncList = world.GetRenderSyncList();
        for (const uint64_t packed : syncList.Changed)
        {
            const Ecs::EcsRenderSyncKind kind = Ecs::EcsRenderSyncKindOf(packed);
            if (kind != Ecs::EcsRenderSyncKind::Mesh && kind != Ecs::EcsRenderSyncKind::SkinnedMesh)
            {
                continue;
            }
            const auto ownerIt = m_MeshOwner.find(Ecs::EcsRenderSyncKeyOf(packed));
            if (ownerIt == m_MeshOwner.end())
            {
                continue;
            }
            const auto trackedIt = m_Tracked.find(ownerIt->second);
            if (trackedIt == m_Tracked.end() || trackedIt->second.EntityPtr == nullptr)
            {
                continue;
            }
            RefreshEntity(world, *trackedIt->second.EntityPtr);
        }

        if (NeedsRebuild())
        {
            RebuildTree();
            m_LastStats.Rebuilt = 1;
            ++m_TotalRebuilt;
        }

        m_LastStats.Entries = static_cast<uint32_t>(GetEntryCount());
        m_TotalUpdated += m_LastStats.Updated;
        ReportUpdate();
    }

    void SceneQuery::BuildTrackedFromWorld(World& world)
    {
        ForgetTrackedData();
        m_Entries.clear();
        m_Nodes.clear();
        m_NextOriginalIndex = 0;

        for (Entity* root : world.GetRootEntities())
        {
            if (root != nullptr)
            {
                RefreshSubtree(world, *root);
            }
        }
        // 追加分（保留）を BVH に入れる
        RebuildTree();
    }

    void SceneQuery::RefreshSubtree(const World& world, Entity& entity)
    {
        const Ecs::EcsEntity row = entity.GetEcsEntity();
        if (!row.IsNull() && !m_DescendedRows.insert(PackRow(row)).second)
        {
            return;
        }

        RefreshEntity(world, entity);
        for (Entity* child : entity.GetChildEntities())
        {
            if (child != nullptr)
            {
                RefreshSubtree(world, *child);
            }
        }
    }

    void SceneQuery::RefreshEntity(const World& world, Entity& entity)
    {
        const Ecs::EcsEntity row = entity.GetEcsEntity();
        if (row.IsNull())
        {
            return;
        }
        const uint64_t rowKey = PackRow(row);
        if (!m_RefreshedRows.insert(rowKey).second)
        {
            return;
        }

        // MeshComponent の集合。境界は MeshComponent からだけ求まる
        Container::VariableArray<uint64_t> meshIds;
        for (Component::Component* component : entity.GetComponents())
        {
            if (component != nullptr && CastTo<Component::MeshComponent>(component) != nullptr)
            {
                meshIds.push_back(component->GetComponentId());
            }
        }

        auto trackedIt = m_Tracked.find(rowKey);
        if (meshIds.empty())
        {
            if (trackedIt != m_Tracked.end())
            {
                RemoveTrackedEntity(rowKey);
            }
            return;
        }

        bool bMember = false;
        Math::AABB bounds;
        if (entity.GetWorld() == &world && IsEffectivelyActive(entity))
        {
            bMember = entity.GetWorldAABB(bounds);
        }

        if (trackedIt == m_Tracked.end())
        {
            TrackedEntity created;
            created.Row = row;
            trackedIt = m_Tracked.emplace(rowKey, created).first;
        }
        TrackedEntity& tracked = trackedIt->second;
        tracked.EntityPtr = &entity;
        for (const uint64_t oldId : tracked.MeshIds)
        {
            m_MeshOwner.erase(oldId);
        }
        tracked.MeshIds = meshIds;
        for (const uint64_t meshId : tracked.MeshIds)
        {
            m_MeshOwner[meshId] = rowKey;
        }

        ApplyBounds(tracked, rowKey, bMember, bounds);
    }

    void SceneQuery::ApplyBounds(TrackedEntity& tracked, uint64_t rowKey, bool bMember, const Math::AABB& bounds)
    {
        if (!bMember)
        {
            if (tracked.Index >= 0)
            {
                RemoveEntry(tracked);
                ++m_LastStats.Removed;
            }
            return;
        }

        if (tracked.Index < 0)
        {
            Entry entry;
            entry.EntityPtr = tracked.EntityPtr;
            entry.Bounds = bounds;
            entry.OriginalIndex = m_NextOriginalIndex++;
            entry.RowKey = rowKey;
            tracked.Index = static_cast<int32_t>(m_Pending.size());
            tracked.bPending = true;
            m_Pending.push_back(entry);
            ++m_LastStats.Added;
            return;
        }

        Entry& entry = tracked.bPending ? m_Pending[tracked.Index] : m_Entries[tracked.Index];
        entry.EntityPtr = tracked.EntityPtr;
        if (SameBounds(entry.Bounds, bounds))
        {
            return;
        }
        entry.Bounds = bounds;
        ++m_LastStats.Updated;
        if (!tracked.bPending)
        {
            ++m_UpdatesSinceBuild;
            RefitFrom(entry.Leaf);
        }
    }

    void SceneQuery::RemoveTrackedEntity(uint64_t rowKey)
    {
        const auto it = m_Tracked.find(rowKey);
        if (it == m_Tracked.end())
        {
            return;
        }
        if (it->second.Index >= 0)
        {
            RemoveEntry(it->second);
            ++m_LastStats.Removed;
        }
        for (const uint64_t meshId : it->second.MeshIds)
        {
            m_MeshOwner.erase(meshId);
        }
        m_Tracked.erase(it);
    }

    void SceneQuery::RemoveEntry(TrackedEntity& tracked)
    {
        if (tracked.bPending)
        {
            const size_t index = static_cast<size_t>(tracked.Index);
            const size_t last = m_Pending.size() - 1;
            if (index != last)
            {
                m_Pending[index] = m_Pending[last];
                const auto movedIt = m_Tracked.find(m_Pending[index].RowKey);
                if (movedIt != m_Tracked.end())
                {
                    movedIt->second.Index = static_cast<int32_t>(index);
                }
            }
            m_Pending.pop_back();
        }
        else
        {
            // 墓石にする。葉の範囲は動かさず、照会が飛ばす。葉と祖先の境界は縮める
            Entry& entry = m_Entries[tracked.Index];
            entry.EntityPtr = nullptr;
            entry.Bounds = Math::AABB::CreateInvalid();
            ++m_TombstoneCount;
            RefitFrom(entry.Leaf);
        }
        tracked.Index = -1;
        tracked.bPending = false;
    }

    void SceneQuery::RefitFrom(uint32_t leafNode)
    {
        // 葉は含む Entry から、内側の節は子から境界を求め直し、変わらなくなったところで止める
        uint32_t current = leafNode;
        while (current != kNoNode && current < m_Nodes.size())
        {
            BVHNode& node = m_Nodes[current];
            Math::AABB bounds = Math::AABB::CreateInvalid();
            if (node.bLeaf)
            {
                const uint32_t end = node.Start + node.Count;
                for (uint32_t entryIndex = node.Start; entryIndex < end; ++entryIndex)
                {
                    if (m_Entries[entryIndex].EntityPtr != nullptr)
                    {
                        bounds.Merge(m_Entries[entryIndex].Bounds);
                    }
                }
            }
            else
            {
                bounds = m_Nodes[node.Left].Bounds;
                bounds.Merge(m_Nodes[node.Right].Bounds);
            }

            if (SameBounds(bounds, node.Bounds))
            {
                break;
            }
            node.Bounds = bounds;
            current = node.Parent;
        }
    }

    bool SceneQuery::NeedsRebuild() const
    {
        const size_t live = GetEntryCount();
        const size_t pendingLimit = live / 8 > kRebuildSlack ? live / 8 : kRebuildSlack;
        const size_t tombstoneLimit = live / 4 > kRebuildSlack ? live / 4 : kRebuildSlack;
        const uint64_t refitLimit = static_cast<uint64_t>(live) * kRefitRebuildFactor > kRefitRebuildFloor
            ? static_cast<uint64_t>(live) * kRefitRebuildFactor
            : kRefitRebuildFloor;
        return m_Pending.size() > pendingLimit || m_TombstoneCount > tombstoneLimit || m_UpdatesSinceBuild > refitLimit;
    }

    void SceneQuery::RebuildTree()
    {
        // 墓石を捨て、保留を足して、BVH を作り直す
        Container::VariableArray<Entry> live;
        live.reserve(GetEntryCount());
        for (const Entry& entry : m_Entries)
        {
            if (entry.EntityPtr != nullptr)
            {
                live.push_back(entry);
            }
        }
        for (const Entry& entry : m_Pending)
        {
            live.push_back(entry);
        }
        m_Entries = std::move(live);
        BuildBVH();

        for (uint32_t index = 0; index < m_Entries.size(); ++index)
        {
            const auto it = m_Tracked.find(m_Entries[index].RowKey);
            if (it != m_Tracked.end())
            {
                it->second.Index = static_cast<int32_t>(index);
                it->second.bPending = false;
            }
        }
    }

    void SceneQuery::ReportUpdate()
    {
        if (m_UpdateCalls % kReportInterval != 1)
        {
            return;
        }
        LOG_INFO("SCENEQUERY updated=%u added=%u removed=%u rebuilt=%u entries=%u calls=%llu total_updated=%llu total_rebuilt=%llu",
                 m_LastStats.Updated, m_LastStats.Added, m_LastStats.Removed, m_LastStats.Rebuilt, m_LastStats.Entries,
                 static_cast<unsigned long long>(m_UpdateCalls), static_cast<unsigned long long>(m_TotalUpdated),
                 static_cast<unsigned long long>(m_TotalRebuilt));
    }

    // ========================================
    // 検査 ECS_SCENEQUERY
    // ========================================

    void SceneQuery::RunCheck(const World& world, Debug::EngineChecks& checks)
    {
        if (!checks.IsEnabled("ECS_SCENEQUERY"))
        {
            return;
        }

        uint32_t examined = 0;
        char detail[200] = {};
        const bool bPass = VerifyAgainstRebuild(world, examined, detail, sizeof(detail));
        ++m_CheckCalls;
        if (!bPass && checks.GetFailCount() <= 8)
        {
            LOG_ERROR("ECS_SCENEQUERY: 増分で保った結果が、全量で作った結果と食い違う: %s", detail);
        }

        // 呼び出しが多いので、fail は毎回、pass は最初に照合できた回と 64 回ごとに出す
        const bool bReportPass = bPass && examined > 0 && (m_LastCheckReport == 0 || m_CheckCalls % 64 == 0);
        if (!bPass || bReportPass)
        {
            checks.Report("ECS_SCENEQUERY", bPass, examined);
            m_LastCheckReport = m_CheckCalls;
        }
    }

    bool SceneQuery::VerifyAgainstRebuild(const World& world, uint32_t& outExamined, char* outDetail, size_t detailSize) const
    {
        outExamined = 0;
        if (outDetail != nullptr && detailSize > 0)
        {
            outDetail[0] = '\0';
        }
        auto describe = [&](const char* text, const void* subject)
        {
            if (outDetail != nullptr && detailSize > 0 && outDetail[0] == '\0')
            {
                std::snprintf(outDetail, detailSize, "%s (%p)", text, subject);
            }
        };

        SceneQuery fresh;
        fresh.Rebuild(world);

        struct Item
        {
            Entity* Ptr;
            Math::AABB Bounds;
        };
        auto lessPtr = [](const Item& lhs, const Item& rhs)
        {
            return std::less<Entity*>{}(lhs.Ptr, rhs.Ptr);
        };

        Container::VariableArray<Item> mine;
        Container::VariableArray<Item> theirs;
        for (const Entry& entry : m_Entries)
        {
            if (entry.EntityPtr != nullptr)
            {
                mine.push_back({entry.EntityPtr, entry.Bounds});
            }
        }
        for (const Entry& entry : m_Pending)
        {
            mine.push_back({entry.EntityPtr, entry.Bounds});
        }
        for (const Entry& entry : fresh.m_Entries)
        {
            theirs.push_back({entry.EntityPtr, entry.Bounds});
        }
        std::sort(mine.begin(), mine.end(), lessPtr);
        std::sort(theirs.begin(), theirs.end(), lessPtr);

        outExamined = static_cast<uint32_t>(theirs.size());
        bool bPass = true;

        // Entry の集合と境界が、全量で作った結果と同じ
        size_t i = 0;
        size_t j = 0;
        while (i < mine.size() || j < theirs.size())
        {
            if (j >= theirs.size() || (i < mine.size() && lessPtr(mine[i], theirs[j])))
            {
                describe("増分にだけある Entry", mine[i].Ptr);
                bPass = false;
                ++i;
            }
            else if (i >= mine.size() || lessPtr(theirs[j], mine[i]))
            {
                describe("増分に無い Entry", theirs[j].Ptr);
                bPass = false;
                ++j;
            }
            else
            {
                if (!SameBounds(mine[i].Bounds, theirs[j].Bounds))
                {
                    describe("Entry の境界が違う", theirs[j].Ptr);
                    bPass = false;
                }
                ++i;
                ++j;
            }
        }

        // 照会の結果が同じ（Entry を等間隔に選び、球・箱・下向きの光線で比べる）
        const size_t stride = theirs.size() / 32 > 1 ? theirs.size() / 32 : 1;
        for (size_t index = 0; index < theirs.size(); index += stride)
        {
            const Math::AABB& bounds = theirs[index].Bounds;
            const Math::Vector3 center = bounds.Center();
            const Math::Vector3 half = bounds.HalfExtents();

            Container::VariableArray<Entity*> lhs;
            Container::VariableArray<Entity*> rhs;
            auto sameSet = [&]()
            {
                std::sort(lhs.begin(), lhs.end(), std::less<Entity*>{});
                std::sort(rhs.begin(), rhs.end(), std::less<Entity*>{});
                return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
            };

            const Math::Sphere sphere(center, half.Length() * 1.25f + 0.01f);
            OverlapSphere(sphere, lhs);
            fresh.OverlapSphere(sphere, rhs);
            if (!sameSet())
            {
                describe("OverlapSphere の結果が違う", theirs[index].Ptr);
                bPass = false;
            }

            OverlapBox(bounds, lhs);
            fresh.OverlapBox(bounds, rhs);
            if (!sameSet())
            {
                describe("OverlapBox の結果が違う", theirs[index].Ptr);
                bPass = false;
            }

            const Math::Ray ray(center + Math::Vector3(0.0f, half.y + 100.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f));
            RaycastHit hitMine;
            RaycastHit hitFresh;
            Raycast(ray, hitMine);
            fresh.Raycast(ray, hitFresh);
            // 同じ距離の Entry が複数あるときの順は問わず、当たりの有無と距離を比べる
            if (hitMine.bHit != hitFresh.bHit || !SameFloat(hitMine.Distance, hitFresh.Distance))
            {
                describe("Raycast の結果が違う", theirs[index].Ptr);
                bPass = false;
            }
            outExamined += 3;
        }

        return bPass;
    }

} // namespace NorvesLib::Core::Scene
