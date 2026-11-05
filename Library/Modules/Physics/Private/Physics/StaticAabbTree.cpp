#include "Physics/StaticAabbTree.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        double Coordinate(const Math::Vector3& vector, uint32_t axis)
        {
            switch (axis)
            {
            case 0:
                return vector.x;
            case 1:
                return vector.y;
            default:
                return vector.z;
            }
        }

        double Centroid(const Math::AABB& bounds, uint32_t axis)
        {
            // 加算時の float のオーバーフローを避ける。
            return (Coordinate(bounds.Min, axis) + Coordinate(bounds.Max, axis)) * 0.5;
        }
    }

    void StaticAabbTree::Build(Core::Container::Span<const Math::AABB> bounds,
        Core::Container::Span<const Core::Scene::ColliderHandle> handles)
    {
        assert(bounds.size() == handles.size());
        assert(bounds.size() <= (std::numeric_limits<uint32_t>::max)());

        Clear();
        if (bounds.empty())
        {
            return;
        }

        m_ItemBounds.assign(bounds.begin(), bounds.end());
        m_Items.resize(bounds.size());
        for (size_t index = 0; index < m_Items.size(); ++index)
        {
            m_Items[index] = static_cast<uint32_t>(index);
        }
        m_Nodes.emplace_back();

        struct BuildRange
        {
            uint32_t Node;
            uint32_t First;
            uint32_t Count;
        };

        constexpr uint32_t StackCapacity = 64;
        BuildRange stack[StackCapacity];
        uint32_t stackSize = 0;
        stack[stackSize++] = {0, 0, static_cast<uint32_t>(bounds.size())};

        while (stackSize != 0)
        {
            const BuildRange range = stack[--stackSize];
            const Math::AABB& firstBounds = m_ItemBounds[m_Items[range.First]];
            double centroidMin[3] = {Centroid(firstBounds, 0), Centroid(firstBounds, 1), Centroid(firstBounds, 2)};
            double centroidMax[3] = {centroidMin[0], centroidMin[1], centroidMin[2]};
            for (uint32_t offset = 1; offset < range.Count; ++offset)
            {
                const Math::AABB& itemBounds = m_ItemBounds[m_Items[range.First + offset]];
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    const double center = Centroid(itemBounds, axis);
                    centroidMin[axis] = (std::min)(centroidMin[axis], center);
                    centroidMax[axis] = (std::max)(centroidMax[axis], center);
                }
            }

            uint32_t splitAxis = 0;
            for (uint32_t axis = 1; axis < 3; ++axis)
            {
                if (centroidMax[axis] - centroidMin[axis] > centroidMax[splitAxis] - centroidMin[splitAxis])
                {
                    splitAxis = axis;
                }
            }

            // 葉も整列し、入力順が葉の中のハンドル順に影響しないようにする。
            std::sort(m_Items.begin() + range.First, m_Items.begin() + range.First + range.Count,
                [&](uint32_t left, uint32_t right)
                {
                    const double leftCenter = Centroid(m_ItemBounds[left], splitAxis);
                    const double rightCenter = Centroid(m_ItemBounds[right], splitAxis);
                    if (leftCenter != rightCenter)
                    {
                        return leftCenter < rightCenter;
                    }
                    return handles[left] < handles[right];
                });

            StaticAabbTreeNode& node = m_Nodes[range.Node];
            node.Bounds = m_ItemBounds[m_Items[range.First]];
            for (uint32_t offset = 1; offset < range.Count; ++offset)
            {
                node.Bounds.Merge(m_ItemBounds[m_Items[range.First + offset]]);
            }

            if (range.Count <= MaxLeafItems)
            {
                node.First = range.First;
                node.Count = range.Count;
                continue;
            }

            const uint32_t leftCount = range.Count / 2;
            const uint32_t leftNode = static_cast<uint32_t>(m_Nodes.size());
            node.First = leftNode;
            node.Count = 0;
            m_Nodes.emplace_back();
            m_Nodes.emplace_back();

            assert(stackSize + 2 <= StackCapacity);
            stack[stackSize++] = {leftNode + 1, range.First + leftCount, range.Count - leftCount};
            stack[stackSize++] = {leftNode, range.First, leftCount};
        }
    }

    void StaticAabbTree::Clear()
    {
        m_Nodes.clear();
        m_Items.clear();
        m_ItemBounds.clear();
    }

    bool StaticAabbTree::IsEmpty() const
    {
        return m_Nodes.empty();
    }

    const Core::Container::VariableArray<StaticAabbTreeNode>& StaticAabbTree::GetNodes() const
    {
        return m_Nodes;
    }

    const Core::Container::VariableArray<uint32_t>& StaticAabbTree::GetItems() const
    {
        return m_Items;
    }

    const Math::AABB& StaticAabbTree::GetItemBounds(uint32_t item) const
    {
        assert(item < m_ItemBounds.size());
        return m_ItemBounds[item];
    }

    bool StaticAabbTree::Overlaps(const Math::AABB& first, const Math::AABB& second)
    {
        return first.Min.x <= second.Max.x && second.Min.x <= first.Max.x
            && first.Min.y <= second.Max.y && second.Min.y <= first.Max.y
            && first.Min.z <= second.Max.z && second.Min.z <= first.Max.z;
    }

    bool StaticAabbTree::IntersectsRay(const Math::AABB& bounds, const Math::Vector3& origin,
        const Math::Vector3& unitDirection, float limit, double& outEntry)
    {
        double entry = 0.0;
        double exit = static_cast<double>(limit);
        if (exit < entry)
        {
            return false;
        }

        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const double lower = Coordinate(bounds.Min, axis);
            const double upper = Coordinate(bounds.Max, axis);
            const double padding = 1e-4 * (1.0 + (std::max)(std::fabs(lower), std::fabs(upper)));
            const double expandedLower = lower - padding;
            const double expandedUpper = upper + padding;
            const double position = Coordinate(origin, axis);
            const double direction = Coordinate(unitDirection, axis);
            if (direction == 0.0)
            {
                if (position < expandedLower || position > expandedUpper)
                {
                    return false;
                }
                continue;
            }

            double axisEntry = (expandedLower - position) / direction;
            double axisExit = (expandedUpper - position) / direction;
            if (axisEntry > axisExit)
            {
                std::swap(axisEntry, axisExit);
            }
            entry = (std::max)(entry, axisEntry);
            exit = (std::min)(exit, axisExit);
            if (entry > exit)
            {
                return false;
            }
        }

        outEntry = entry;
        return true;
    }
} // namespace NorvesLib::Modules::Physics
