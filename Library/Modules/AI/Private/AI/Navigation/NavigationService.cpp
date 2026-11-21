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

    NavigationService::NavigationService()
    {
        m_Overlay.Reset(m_Grid.GetDesc());
    }

    ENavTileResult NavigationService::LoadTile(NavTile&& tile)
    {
        const NavGridDesc previous = m_Grid.GetDesc();
        const ENavTileResult result = m_Grid.LoadTile(std::move(tile));
        if (result == ENavTileResult::Ok)
        {
            // 空の格子は最初のタイルから設定を取り込む。
            SyncOverlayGridDesc(previous);
        }
        return result;
    }

    bool NavigationService::UnloadTile(NavTileCoord coord)
    {
        return m_Grid.UnloadTile(coord);
    }

    const NavGrid& NavigationService::GetGrid() const
    {
        return m_Grid;
    }

    NavDynamicOverlay& NavigationService::GetDynamicOverlay()
    {
        return m_Overlay;
    }

    const NavDynamicOverlay& NavigationService::GetDynamicOverlay() const
    {
        return m_Overlay;
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
        if (!NavGridQueries::IsWalkable(m_Grid, start, profile.Radius)
            && !NavGridQueries::FindNearestWalkable(m_Grid, start, profile.Radius, 2.0f, slot.Start))
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
        const PathSlot& slot = m_Slots[handle.Index];
        if (slot.bDynamicStaleAtFinish)
        {
            return true;
        }
        for (const TileStamp& tile : slot.Tiles)
        {
            if (m_Grid.GetTileGeneration(tile.Coord) != tile.Generation
                || m_Overlay.GetTileVersion(tile.Coord) != tile.DynamicVersion)
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
                m_Pathfinder.Begin(m_Grid, slot.Query, &m_Overlay);
            }

            const uint32_t before = m_Pathfinder.GetExpansionCount();
            // 累計が飽和した後は 1 展開ずつ数え、後続の要求にも正確な残りを渡す。
            const uint32_t limit = before == UINT32_MAX ? 1u : (std::min)(remaining, UINT32_MAX - before);
            const ENavSearchState state = m_Pathfinder.Step(limit);
            remaining -= before == UINT32_MAX ? 1u : m_Pathfinder.GetExpansionCount() - before;
            if (state != ENavSearchState::InProgress)
            {
                const bool finished = TryFinishPath(slot, state);
                m_Pathfinder.Reset();
                if (finished)
                {
                    m_Pending.pop_front();
                }
            }
        }
    }

    bool NavigationService::TryFinishPath(PathSlot& slot, ENavSearchState state)
    {
        const uint32_t serialAtBegin = m_Pathfinder.GetDynamicSerialAtBegin();
        const auto cells = m_Pathfinder.GetPathCells();
        const float radius = slot.Query.Profile.Radius;
        if ((state != ENavSearchState::Succeeded && state != ENavSearchState::Partial)
            || !SmoothNavPath(m_Grid, cells, radius, slot.Points, &m_Overlay))
        {
            if (m_Overlay.GetSerial() != serialAtBegin)
            {
                slot.Points.clear();
                return false;
            }
            slot.Status = ENavPathStatus::Failed;
            return true;
        }
        slot.Cells.assign(cells.begin(), cells.end());
        if (slot.Points.size() > 1)
        {
            if (!NavGridQueries::Raycast(m_Grid, slot.Start, slot.Points[1], radius, &m_Overlay).bHit)
            {
                slot.Points.front() = slot.Start;
            }
            const auto& previous = slot.Points[slot.Points.size() - 2];
            if (state == ENavSearchState::Succeeded
                && !NavGridQueries::Raycast(m_Grid, previous, slot.Goal, radius, &m_Overlay).bHit)
            {
                slot.Points.back() = slot.Goal;
            }
        }

        slot.Tiles.clear();
        Core::Container::UnorderedSet<uint64_t> seen;
        const auto recordTile = [&](NavTileCoord coord)
        {
            if (seen.insert(coord.ToKey()).second)
            {
                const uint32_t version = m_Overlay.GetTileVersion(coord);
                slot.Tiles.push_back({coord, m_Grid.GetTileGeneration(coord), version});
                if (version > serialAtBegin)
                {
                    slot.bDynamicStaleAtFinish = true;
                }
            }
        };
        for (NavCellCoord cell : slot.Cells)
        {
            recordTile(m_Grid.CellToTile(cell));
        }

        Core::Container::VariableArray<NavTileCoord> testedTiles;
        const auto recordSegment = [&](const Math::Vector3& from, const Math::Vector3& to)
        {
            testedTiles.clear();
            NavGridQueries::Raycast(m_Grid, from, to, radius, testedTiles, &m_Overlay);
            for (NavTileCoord coord : testedTiles)
            {
                recordTile(coord);
            }
        };
        if (slot.Points.size() == 1)
        {
            recordSegment(slot.Points.front(), slot.Points.front());
        }
        for (size_t i = 1; i < slot.Points.size(); ++i)
        {
            recordSegment(slot.Points[i - 1], slot.Points[i]);
        }
        slot.Status = state == ENavSearchState::Succeeded ? ENavPathStatus::Succeeded : ENavPathStatus::Partial;
        return true;
    }

    bool NavigationService::FindNearestWalkable(const Math::Vector3& position, const NavAgentProfile& profile,
                                                float searchRadius, Math::Vector3& outPosition) const
    {
        return NavGridQueries::FindNearestWalkable(m_Grid, position, profile.Radius, searchRadius, outPosition, &m_Overlay);
    }

    bool NavigationService::IsWalkable(const Math::Vector3& position, const NavAgentProfile& profile) const
    {
        return NavGridQueries::IsWalkable(m_Grid, position, profile.Radius, &m_Overlay);
    }

    NavRaycastResult NavigationService::NavRaycast(const Math::Vector3& from, const Math::Vector3& to,
                                                   const NavAgentProfile& profile) const
    {
        return NavGridQueries::Raycast(m_Grid, from, to, profile.Radius, &m_Overlay);
    }

    uint32_t NavigationService::SampleCost(const Math::Vector3& position, const NavAgentProfile& profile) const
    {
        return NavGridQueries::SampleCost(m_Grid, position, profile, &m_Overlay);
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
        const NavGridDesc previous = m_Grid.GetDesc();
        if (!m_Grid.SetDesc(desc))
        {
            return false;
        }
        SyncOverlayGridDesc(previous);
        return true;
    }

    void NavigationService::SyncOverlayGridDesc(const NavGridDesc& previous)
    {
        const NavGridDesc& current = m_Grid.GetDesc();
        if (current.OriginX != previous.OriginX || current.OriginZ != previous.OriginZ ||
            current.CellSize != previous.CellSize || current.TileCells != previous.TileCells)
        {
            m_Overlay.Reset(current);
        }
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
        m_Overlay.Clear();
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
