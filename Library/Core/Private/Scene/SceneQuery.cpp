#include "Scene/SceneQuery.h"
#include "Object/World.h"
#include "Object/Entity.h"
#include "Math/GeometryIntersection.h"
#include <cfloat>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        constexpr uint32_t BVHLeafEntryThreshold = 4;
    } // namespace

    void SceneQuery::Rebuild(const World& world)
    {
        m_Entries.clear();

        auto rootEntities = world.GetRootEntities();
        for (Entity* entity : rootEntities)
        {
            CollectRecursive(entity, m_Entries);
        }

        BuildBVH();
    }

    void SceneQuery::Rebuild(Container::Span<Entity* const> entities)
    {
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

        BuildBVH();
    }

    void SceneQuery::Clear()
    {
        m_Entries.clear();
        m_Nodes.clear();
    }

    bool SceneQuery::Raycast(const Math::Ray& ray, RaycastHit& outHit) const
    {
        outHit = RaycastHit{};
        if (m_Nodes.empty())
        {
            return false;
        }

        float bestT = FLT_MAX;
        uint32_t bestOriginalIndex = static_cast<uint32_t>(-1);
        RaycastNode(0, ray, outHit, bestT, bestOriginalIndex);
        return outHit.bHit;
    }

    void SceneQuery::OverlapSphere(
        const Math::Sphere& sphere,
        Container::VariableArray<Entity*>& outEntities) const
    {
        outEntities.clear();
        if (m_Nodes.empty())
        {
            return;
        }

        CollectSphereNode(0, sphere, outEntities);
    }

    void SceneQuery::OverlapBox(
        const Math::AABB& box,
        Container::VariableArray<Entity*>& outEntities) const
    {
        outEntities.clear();
        if (m_Nodes.empty())
        {
            return;
        }

        CollectBoxNode(0, box, outEntities);
    }

    void SceneQuery::QueryFrustum(
        const Math::Frustum& frustum,
        Container::VariableArray<Entity*>& outEntities) const
    {
        outEntities.clear();
        if (m_Nodes.empty())
        {
            return;
        }

        CollectFrustumNode(0, frustum, outEntities);
    }

    size_t SceneQuery::GetEntryCount() const
    {
        return m_Entries.size();
    }

    void SceneQuery::BuildBVH()
    {
        m_Nodes.clear();
        if (m_Entries.empty())
        {
            return;
        }

        BuildNode(0, static_cast<uint32_t>(m_Entries.size()));
    }

    uint32_t SceneQuery::BuildNode(uint32_t start, uint32_t count)
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

        const uint32_t nodeIndex = static_cast<uint32_t>(m_Nodes.size());
        if (count <= BVHLeafEntryThreshold)
        {
            node.bLeaf = true;
            m_Nodes.push_back(node);
            return nodeIndex;
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
            node.bLeaf = true;
            m_Nodes.push_back(node);
            return nodeIndex;
        }

        for (uint32_t entryIndex = start + 1; entryIndex < end; ++entryIndex)
        {
            Entry current = m_Entries[entryIndex];
            const float currentCenter = GetCenterAxis(current.Bounds, axis);
            uint32_t sortedIndex = entryIndex;
            while (sortedIndex > start && GetCenterAxis(m_Entries[sortedIndex - 1].Bounds, axis) > currentCenter)
            {
                m_Entries[sortedIndex] = m_Entries[sortedIndex - 1];
                --sortedIndex;
            }

            m_Entries[sortedIndex] = current;
        }

        if (GetCenterAxis(m_Entries[start].Bounds, axis) == GetCenterAxis(m_Entries[end - 1].Bounds, axis))
        {
            node.bLeaf = true;
            m_Nodes.push_back(node);
            return nodeIndex;
        }

        const uint32_t split = start + count / 2;
        const uint32_t leftCount = split - start;
        const uint32_t rightCount = end - split;
        if (leftCount == 0 || rightCount == 0)
        {
            node.bLeaf = true;
            m_Nodes.push_back(node);
            return nodeIndex;
        }

        m_Nodes.push_back(node);
        const uint32_t left = BuildNode(start, leftCount);
        const uint32_t right = BuildNode(split, rightCount);

        m_Nodes[nodeIndex].Start = 0;
        m_Nodes[nodeIndex].Count = 0;
        m_Nodes[nodeIndex].Left = left;
        m_Nodes[nodeIndex].Right = right;
        m_Nodes[nodeIndex].bLeaf = false;
        return nodeIndex;
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
                const Entry& entry = m_Entries[entryIndex];
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
                if (Math::SphereIntersectsAABB(sphere, entry.Bounds))
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
                if (Math::AABBIntersectsAABB(box, entry.Bounds))
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
                if (Math::FrustumIntersectsAABB(frustum, entry.Bounds))
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

} // namespace NorvesLib::Core::Scene
