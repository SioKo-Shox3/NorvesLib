#pragma once

#include "Container/VariableArray.h"
#include "Container/Span.h"
#include "Math/GeometryTypes.h"
#include "Scene/PhysicsQueryTypes.h"

#include <cassert>
#include <cstdint>

namespace NorvesLib::Modules::Physics
{
    // 葉は項目の範囲を指し、内部節は隣接する二つの子を指す。
    struct StaticAabbTreeNode
    {
        Math::AABB Bounds;
        uint32_t First = 0;
        uint32_t Count = 0;
    };

    class StaticAabbTree
    {
    public:
        static constexpr uint32_t MaxLeafItems = 4;

        // 同じ長さの境界とハンドルから木を作り直す。空なら木も空になる。
        void Build(Core::Container::Span<const Math::AABB> bounds,
            Core::Container::Span<const Core::Scene::ColliderHandle> handles);
        void Clear();
        bool IsEmpty() const;

        const Core::Container::VariableArray<StaticAabbTreeNode>& GetNodes() const;
        const Core::Container::VariableArray<uint32_t>& GetItems() const;
        const Math::AABB& GetItemBounds(uint32_t item) const;

        // 接する場合を含め、境界と重なる各項目を訪れる。
        template<typename Visitor>
        uint64_t QueryAabb(const Math::AABB& bounds, Visitor&& visitor) const;

        // 節を入口の近い順に訪れ、各項目の処理後に距離の上限を更新する。
        template<typename Visitor>
        uint64_t QueryRay(const Math::Vector3& origin, const Math::Vector3& unitDirection,
            float limit, Visitor&& visitor) const;

    private:
        static bool Overlaps(const Math::AABB& first, const Math::AABB& second);
        static bool IntersectsRay(const Math::AABB& bounds, const Math::Vector3& origin,
            const Math::Vector3& unitDirection, float limit, double& outEntry);

        Core::Container::VariableArray<StaticAabbTreeNode> m_Nodes;
        Core::Container::VariableArray<uint32_t> m_Items;
        Core::Container::VariableArray<Math::AABB> m_ItemBounds;
    };

    template<typename Visitor>
    uint64_t StaticAabbTree::QueryAabb(const Math::AABB& bounds, Visitor&& visitor) const
    {
        if (m_Nodes.empty())
        {
            return 0;
        }

        constexpr uint32_t StackCapacity = 64;
        uint32_t stack[StackCapacity];
        uint32_t stackSize = 0;
        stack[stackSize++] = 0;
        uint64_t visitedCount = 0;

        while (stackSize != 0)
        {
            const StaticAabbTreeNode& node = m_Nodes[stack[--stackSize]];
            ++visitedCount;
            if (!Overlaps(node.Bounds, bounds))
            {
                continue;
            }

            if (node.Count != 0)
            {
                for (uint32_t offset = 0; offset < node.Count; ++offset)
                {
                    const uint32_t item = m_Items[node.First + offset];
                    if (Overlaps(m_ItemBounds[item], bounds))
                    {
                        visitor(item);
                    }
                }
                continue;
            }

            assert(stackSize + 2 <= StackCapacity);
            stack[stackSize++] = node.First + 1;
            stack[stackSize++] = node.First;
        }

        return visitedCount;
    }

    template<typename Visitor>
    uint64_t StaticAabbTree::QueryRay(const Math::Vector3& origin, const Math::Vector3& unitDirection,
        float limit, Visitor&& visitor) const
    {
        if (m_Nodes.empty())
        {
            return 0;
        }

        struct PendingNode
        {
            uint32_t Index;
            double Entry;
        };

        constexpr uint32_t StackCapacity = 64;
        PendingNode stack[StackCapacity];
        uint32_t stackSize = 0;
        uint64_t visitedCount = 1;
        double rootEntry = 0.0;
        if (!IntersectsRay(m_Nodes[0].Bounds, origin, unitDirection, limit, rootEntry))
        {
            return visitedCount;
        }
        stack[stackSize++] = {0, rootEntry};

        while (stackSize != 0)
        {
            const PendingNode pending = stack[--stackSize];
            if (pending.Entry > static_cast<double>(limit))
            {
                continue;
            }

            const StaticAabbTreeNode& node = m_Nodes[pending.Index];
            if (node.Count != 0)
            {
                for (uint32_t offset = 0; offset < node.Count; ++offset)
                {
                    if (pending.Entry > static_cast<double>(limit))
                    {
                        break;
                    }
                    const float newLimit = visitor(m_Items[node.First + offset]);
                    assert(newLimit <= limit);
                    limit = newLimit;
                }
                continue;
            }

            const uint32_t left = node.First;
            const uint32_t right = left + 1;
            double leftEntry = 0.0;
            double rightEntry = 0.0;
            const bool bHitLeft = IntersectsRay(m_Nodes[left].Bounds, origin, unitDirection, limit, leftEntry);
            const bool bHitRight = IntersectsRay(m_Nodes[right].Bounds, origin, unitDirection, limit, rightEntry);
            visitedCount += 2;

            if (bHitLeft && bHitRight)
            {
                assert(stackSize + 2 <= StackCapacity);
                if (leftEntry <= rightEntry)
                {
                    stack[stackSize++] = {right, rightEntry};
                    stack[stackSize++] = {left, leftEntry};
                }
                else
                {
                    stack[stackSize++] = {left, leftEntry};
                    stack[stackSize++] = {right, rightEntry};
                }
            }
            else if (bHitLeft || bHitRight)
            {
                assert(stackSize + 1 <= StackCapacity);
                stack[stackSize++] = bHitLeft ? PendingNode{left, leftEntry} : PendingNode{right, rightEntry};
            }
        }

        return visitedCount;
    }
} // namespace NorvesLib::Modules::Physics
