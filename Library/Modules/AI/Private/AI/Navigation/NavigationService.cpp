#include "AI/Navigation/NavigationService.h"

#include "AI/Navigation/NavPathSmoothing.h"
#include "Container/UnorderedSet.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        bool IsFinite(const Math::Vector3& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        uint32_t NextGeneration(uint32_t generation)
        {
            return generation == UINT32_MAX ? 1u : generation + 1;
        }
    }

    ENavTileResult NavigationService::LoadTile(NavTile&& tile)
    {
        return m_Grid.LoadTile(std::move(tile));
    }

    bool NavigationService::UnloadTile(NavTileCoord coord)
    {
        return m_Grid.UnloadTile(coord);
    }

    const NavGrid& NavigationService::GetGrid() const
    {
        return m_Grid;
    }

    PathHandle NavigationService::RequestPath(const Math::Vector3& start, const Math::Vector3& goal,
                                              const NavAgentProfile& profile, float epsilon)
    {
        if (!IsFinite(start) || !IsFinite(goal) || !std::isfinite(epsilon)
            || !std::isfinite(profile.Radius) || profile.Radius < 0.0f)
        {
            return {};
        }

        uint32_t index;
        if (m_FreeSlots.empty())
        {
            if (m_Slots.size() == UINT32_MAX)
            {
                return {};
            }
            index = GetPathSlotCount();
            if (index == m_Generations.size())
            {
                m_Generations.push_back(1);
            }
            m_Slots.emplace_back();
        }
        else
        {
            index = m_FreeSlots.back();
            m_FreeSlots.pop_back();
        }

        const PathHandle handle{index, m_Generations[index]};
        PathSlot& slot = m_Slots[index];
        slot.Start = start;
        slot.Goal = goal;
        slot.Query.Profile = profile;
        slot.Query.Epsilon = epsilon;
        if (!IsWalkable(start, profile) && !FindNearestWalkable(start, profile, 2.0f, slot.Start))
        {
            slot.Status = ENavPathStatus::Failed;
            return handle;
        }
        m_Pending.push_back(index);
        slot.Status = ENavPathStatus::Pending;
        return handle;
    }

    bool NavigationService::HasSlot(PathHandle handle) const
    {
        return handle.Index < m_Slots.size() && handle.Generation == m_Generations[handle.Index]
            && m_Slots[handle.Index].Status != ENavPathStatus::Invalid;
    }

    ENavPathStatus NavigationService::GetPathStatus(PathHandle handle) const
    {
        return HasSlot(handle) ? m_Slots[handle.Index].Status : ENavPathStatus::Invalid;
    }

    Core::Container::Span<const Math::Vector3> NavigationService::GetPath(PathHandle handle) const
    {
        if (!HasSlot(handle))
        {
            return {};
        }
        return GetSlotPath(handle.Index);
    }

    bool NavigationService::BuildPathPolyline(PathHandle handle, Math::PolylinePath& outPath) const
    {
        const auto points = GetPath(handle);
        if (points.empty())
        {
            return false;
        }
        outPath.Assign(points);
        return true;
    }

    bool NavigationService::CancelPath(PathHandle handle)
    {
        if (GetPathStatus(handle) != ENavPathStatus::Pending)
        {
            return false;
        }
        if (m_Pending.front() == handle.Index)
        {
            m_Pathfinder.Reset();
            m_Pending.pop_front();
        }
        else
        {
            m_Pending.erase(std::find(m_Pending.begin(), m_Pending.end(), handle.Index));
        }
        m_Slots[handle.Index].Status = ENavPathStatus::Cancelled;
        return true;
    }

    bool NavigationService::ReleasePath(PathHandle handle)
    {
        if (!HasSlot(handle))
        {
            return false;
        }
        m_FreeSlots.push_back(handle.Index);
        CancelPath(handle);
        m_Slots[handle.Index] = PathSlot{};
        m_Generations[handle.Index] = NextGeneration(handle.Generation);
        return true;
    }

    bool NavigationService::IsPathStale(PathHandle handle) const
    {
        if (!HasSlot(handle))
        {
            return false;
        }
        for (const TileStamp& tile : m_Slots[handle.Index].Tiles)
        {
            if (m_Grid.GetTileGeneration(tile.Coord) != tile.Generation)
            {
                return true;
            }
        }
        return false;
    }

    void NavigationService::RunBudget()
    {
        uint32_t remaining = m_ExpansionBudget;
        while (remaining > 0 && !m_Pending.empty())
        {
            PathSlot& slot = m_Slots[m_Pending.front()];
            if (m_Pathfinder.GetState() == ENavSearchState::Idle || m_Pathfinder.GetGridVersionAtBegin() != m_Grid.GetVersion())
            {
                slot.Query.Start = m_Grid.WorldToCell(slot.Start.x, slot.Start.z);
                slot.Query.Goal = m_Grid.WorldToCell(slot.Goal.x, slot.Goal.z);
                m_Pathfinder.Begin(m_Grid, slot.Query);
            }

            const uint32_t before = m_Pathfinder.GetExpansionCount();
            // 累計が飽和した後は 1 展開ずつ数え、後続の要求にも正確な残りを渡す。
            const uint32_t limit = before == UINT32_MAX ? 1u : (std::min)(remaining, UINT32_MAX - before);
            const ENavSearchState state = m_Pathfinder.Step(limit);
            remaining -= before == UINT32_MAX ? 1u : m_Pathfinder.GetExpansionCount() - before;
            if (state != ENavSearchState::InProgress)
            {
                FinishPath(slot, state);
                m_Pathfinder.Reset();
                m_Pending.pop_front();
            }
        }
    }

    void NavigationService::FinishPath(PathSlot& slot, ENavSearchState state)
    {
        const auto cells = m_Pathfinder.GetPathCells();
        const float radius = slot.Query.Profile.Radius;
        if ((state != ENavSearchState::Succeeded && state != ENavSearchState::Partial)
            || !SmoothNavPath(m_Grid, cells, radius, slot.Points))
        {
            slot.Status = ENavPathStatus::Failed;
            return;
        }
        slot.Cells.assign(cells.begin(), cells.end());
        if (slot.Points.size() > 1)
        {
            if (!NavGridQueries::Raycast(m_Grid, slot.Start, slot.Points[1], radius).bHit)
            {
                slot.Points.front() = slot.Start;
            }
            const auto& previous = slot.Points[slot.Points.size() - 2];
            if (state == ENavSearchState::Succeeded && !NavGridQueries::Raycast(m_Grid, previous, slot.Goal, radius).bHit)
            {
                slot.Points.back() = slot.Goal;
            }
        }

        Core::Container::UnorderedSet<uint64_t> seen;
        for (NavCellCoord cell : slot.Cells)
        {
            const NavTileCoord coord = m_Grid.CellToTile(cell);
            if (seen.insert(coord.ToKey()).second)
            {
                slot.Tiles.push_back({coord, m_Grid.GetTileGeneration(coord)});
            }
        }
        slot.Status = state == ENavSearchState::Succeeded ? ENavPathStatus::Succeeded : ENavPathStatus::Partial;
    }

    bool NavigationService::FindNearestWalkable(const Math::Vector3& position, const NavAgentProfile& profile,
                                                float searchRadius, Math::Vector3& outPosition) const
    {
        return NavGridQueries::FindNearestWalkable(m_Grid, position, profile.Radius, searchRadius, outPosition);
    }

    bool NavigationService::IsWalkable(const Math::Vector3& position, const NavAgentProfile& profile) const
    {
        return NavGridQueries::IsWalkable(m_Grid, position, profile.Radius);
    }

    NavRaycastResult NavigationService::NavRaycast(const Math::Vector3& from, const Math::Vector3& to,
                                                   const NavAgentProfile& profile) const
    {
        return NavGridQueries::Raycast(m_Grid, from, to, profile.Radius);
    }

    uint32_t NavigationService::SampleCost(const Math::Vector3& position, const NavAgentProfile& profile) const
    {
        return NavGridQueries::SampleCost(m_Grid, position, profile);
    }

    const NavAgentProfile* NavigationService::FindProfile(const char* name) const
    {
        return m_Profiles.Find(name);
    }

    void NavigationService::SetExpansionBudget(uint32_t expansionsPerStep)
    {
        m_ExpansionBudget = expansionsPerStep;
    }

    uint32_t NavigationService::GetExpansionBudget() const
    {
        return m_ExpansionBudget;
    }

    void NavigationService::SetProfiles(NavAgentProfileSet&& profiles)
    {
        m_Profiles = std::move(profiles);
    }

    const NavAgentProfileSet& NavigationService::GetProfiles() const
    {
        return m_Profiles;
    }

    bool NavigationService::SetGridDesc(const NavGridDesc& desc)
    {
        return m_Grid.SetDesc(desc);
    }

    void NavigationService::Reset()
    {
        m_Pathfinder = NavPathfinder{};
        m_Pending.clear();
        m_FreeSlots.clear();
        for (size_t i = 0; i < m_Slots.size(); ++i)
        {
            if (m_Slots[i].Status != ENavPathStatus::Invalid)
            {
                m_Generations[i] = NextGeneration(m_Generations[i]);
            }
        }
        m_Slots.clear();
        m_Grid.Clear();
    }

    uint32_t NavigationService::GetPathSlotCount() const
    {
        return static_cast<uint32_t>(m_Slots.size());
    }

    ENavPathStatus NavigationService::GetSlotStatus(uint32_t i) const
    {
        return i < m_Slots.size() ? m_Slots[i].Status : ENavPathStatus::Invalid;
    }

    Core::Container::Span<const Math::Vector3> NavigationService::GetSlotPath(uint32_t i) const
    {
        const ENavPathStatus status = GetSlotStatus(i);
        if (status == ENavPathStatus::Succeeded || status == ENavPathStatus::Partial)
        {
            return {m_Slots[i].Points.data(), m_Slots[i].Points.size()};
        }
        return {};
    }
} // namespace NorvesLib::Modules::AI
