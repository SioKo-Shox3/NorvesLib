#include "AI/Navigation/NavPathfinder.h"

#include "AI/Navigation/NavGrid.h"
#include "NavBinaryHeap.h"
#include "Container/UnorderedMap.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        constexpr uint8_t kNoParent = 8;

        struct Direction
        {
            int32_t X;
            int32_t Z;
            uint8_t Reverse;
        };

        constexpr Direction kDirections[] = {
            {1, 0, 1}, {-1, 0, 0}, {0, 1, 3}, {0, -1, 2},
            {1, 1, 7}, {-1, 1, 6}, {1, -1, 5}, {-1, -1, 4}
        };

        uint32_t QuantizeEpsilon(float epsilon)
        {
            if (!std::isfinite(epsilon))
            {
                return 307;
            }
            return static_cast<uint32_t>(std::round(std::clamp(epsilon, 1.0f, 4.0f) * 256.0f));
        }

        uint64_t Octile(NavCellCoord from, NavCellCoord to)
        {
            const uint64_t dx = std::abs(static_cast<int64_t>(from.X) - to.X);
            const uint64_t dz = std::abs(static_cast<int64_t>(from.Z) - to.Z);
            const uint64_t diagonal = std::min(dx, dz);
            return (std::max(dx, dz) - diagonal) * kNavCostOrthogonal + diagonal * kNavCostDiagonal;
        }

        /// 座標域の端から反対側へ折り返さないよう、加算してから範囲を確かめる。
        bool TryNeighbor(NavCellCoord from, Direction direction, NavCellCoord& neighbor)
        {
            const int64_t x = static_cast<int64_t>(from.X) + direction.X;
            const int64_t z = static_cast<int64_t>(from.Z) + direction.Z;
            if (x < INT32_MIN || x > INT32_MAX || z < INT32_MIN || z > INT32_MAX)
            {
                return false;
            }
            neighbor = {static_cast<int32_t>(x), static_cast<int32_t>(z)};
            return true;
        }
    } // namespace

    struct NavPathfinder::Search
    {
        struct Page
        {
            uint32_t Generation = 0;
            Core::Container::VariableArray<uint32_t> Stamps;
            Core::Container::VariableArray<uint64_t> Costs;
            Core::Container::VariableArray<uint8_t> Parents;
            // 印が一致し、位置が NoPosition の節点は閉じている。
            Core::Container::VariableArray<size_t> Positions;
        };

        const NavGrid* Grid = nullptr;
        NavPathQuery Query;
        uint64_t GridVersion = 0;
        uint32_t EpsilonQ8 = 256;
        uint16_t ThresholdQ = 0;
        ENavSearchState State = ENavSearchState::Idle;
        uint32_t ExpansionCount = 0;
        uint32_t Stamp = 0;
        Core::Container::UnorderedMap<uint64_t, Page> Pages;
        NavTileCoord CachedCoord;
        Page* CachedPage = nullptr;
        NavBinaryHeap Open;
        NavCellCoord Closest;
        uint64_t ClosestDistance = UINT64_MAX;
        uint64_t ClosestCost = 0;
        Core::Container::VariableArray<NavCellCoord> Path;
        uint32_t PathCost = 0;

        void AdvanceStamp()
        {
            if (++Stamp == 0)
            {
                for (auto& [key, page] : Pages)
                {
                    std::fill(page.Stamps.begin(), page.Stamps.end(), 0u);
                }
                Stamp = 1;
            }
        }

        Page& GetPage(const NavTile& tile)
        {
            if (CachedPage && CachedCoord == tile.Coord)
            {
                return *CachedPage;
            }
            Page& page = Pages[tile.Coord.ToKey()];
            const uint32_t generation = Grid->GetTileGeneration(tile.Coord);
            const uint32_t count = tile.GetCellCount();
            if (page.Generation != generation || page.Stamps.size() != count)
            {
                page = {};
                page.Generation = generation;
                page.Stamps.resize(count);
                page.Costs.resize(count);
                page.Parents.resize(count);
                page.Positions.resize(count);
            }
            CachedCoord = tile.Coord;
            CachedPage = &page;
            return page;
        }

        NavCellRef FindPassable(NavCellCoord cell) const
        {
            const NavCellRef ref = Grid->FindCell(cell);
            if (ref && IsNavCellPassable(ref.Tile->Flags[ref.Index], ref.Tile->ClearanceQ[ref.Index], ThresholdQ))
            {
                return ref;
            }
            return {};
        }

        void Relax(NavCellCoord cell, NavCellRef ref, uint64_t cost, uint8_t parent)
        {
            Page& page = GetPage(*ref.Tile);
            const uint32_t i = ref.Index;
            const bool bVisited = page.Stamps[i] == Stamp;
            if (bVisited && (page.Positions[i] == NavBinaryHeap::NoPosition || cost >= page.Costs[i]))
            {
                return;
            }
            page.Parents[i] = parent;
            page.Costs[i] = cost;
            const uint64_t h = (Octile(cell, Query.Goal) * EpsilonQ8) >> 8;
            if (bVisited)
            {
                Open.DecreaseKey(page.Positions[i], cost + h);
            }
            else
            {
                page.Stamps[i] = Stamp;
                Open.Push({cell, cost + h, h, &page.Positions[i]});
            }
        }

        void ExpandNeighbors(NavCellCoord cell, uint64_t cost)
        {
            bool bPassable[4] = {};
            for (uint8_t i = 0; i < 8; ++i)
            {
                const Direction direction = kDirections[i];
                const bool bDiagonal = i >= 4;
                if (bDiagonal && !(bPassable[direction.X > 0 ? 0 : 1] && bPassable[direction.Z > 0 ? 2 : 3]))
                {
                    continue;
                }
                NavCellCoord neighbor;
                if (!TryNeighbor(cell, direction, neighbor))
                {
                    continue;
                }
                const NavCellRef ref = FindPassable(neighbor);
                if (!ref)
                {
                    continue;
                }
                if (!bDiagonal)
                {
                    bPassable[i] = true;
                }
                const uint32_t extra = NavCellExtraPer128(Query.Profile, ref.Tile->Flags[ref.Index],
                                                          ref.Tile->GrassQ[ref.Index]);
                Relax(neighbor, ref, cost + NavStepCost(bDiagonal, extra), direction.Reverse);
            }
        }

        void Finish(ENavSearchState result, NavCellCoord cell, uint64_t cost)
        {
            PathCost = static_cast<uint32_t>(std::min<uint64_t>(cost, UINT32_MAX));
            for (;;)
            {
                Path.push_back(cell);
                const NavCellRef ref = Grid->FindCell(cell);
                const uint8_t parent = GetPage(*ref.Tile).Parents[ref.Index];
                if (parent == kNoParent)
                {
                    break;
                }
                cell.X += kDirections[parent].X;
                cell.Z += kDirections[parent].Z;
            }
            std::reverse(Path.begin(), Path.end());
            State = result;
        }

        void ExpandOne()
        {
            const NavBinaryHeap::Entry entry = Open.Pop();
            const NavCellCoord cell = entry.Cell;
            const uint64_t cost = entry.F - entry.H;
            if (ExpansionCount < UINT32_MAX)
            {
                ++ExpansionCount;
            }
            if (cell == Query.Goal)
            {
                Finish(ENavSearchState::Succeeded, cell, cost);
                return;
            }
            const uint64_t distance = Octile(cell, Query.Goal);
            if (distance < ClosestDistance || (distance == ClosestDistance &&
                (cost < ClosestCost || (cost == ClosestCost && cell.ToKey() < Closest.ToKey()))))
            {
                Closest = cell;
                ClosestDistance = distance;
                ClosestCost = cost;
            }
            if (Query.MaxExpansions == 0 || ExpansionCount < Query.MaxExpansions)
            {
                ExpandNeighbors(cell, cost);
                if (!Open.IsEmpty())
                {
                    return;
                }
            }
            Finish(ENavSearchState::Partial, Closest, ClosestCost);
        }
    };

    NavPathfinder::NavPathfinder() = default;
    NavPathfinder::~NavPathfinder() = default;
    NavPathfinder::NavPathfinder(NavPathfinder&&) noexcept = default;
    NavPathfinder& NavPathfinder::operator=(NavPathfinder&&) noexcept = default;

    void NavPathfinder::Begin(const NavGrid& grid, const NavPathQuery& query)
    {
        Reset();
        if (!m_Search)
        {
            m_Search = Core::Container::MakeUnique<Search>();
        }
        Search& search = *m_Search;
        search.Grid = &grid;
        search.GridVersion = grid.GetVersion();
        search.Query = query;
        search.EpsilonQ8 = QuantizeEpsilon(query.Epsilon);
        search.ThresholdQ = NavClearanceThresholdQ(query.Profile.Radius, grid.GetDesc().CellSize);
        search.AdvanceStamp();

        const NavCellRef start = search.FindPassable(query.Start);
        if (!start || (search.FindPassable(query.Goal) &&
            grid.GetConnectivityRegion(query.Start) != grid.GetConnectivityRegion(query.Goal)))
        {
            search.State = ENavSearchState::Failed;
            return;
        }
        if (query.Start == query.Goal)
        {
            search.Path.push_back(query.Start);
            search.State = ENavSearchState::Succeeded;
            return;
        }
        search.Relax(query.Start, start, 0, kNoParent);
        search.State = ENavSearchState::InProgress;
    }

    ENavSearchState NavPathfinder::Step(uint32_t maxExpansions)
    {
        if (!m_Search)
        {
            return ENavSearchState::Idle;
        }
        Search& search = *m_Search;
        if (search.Grid && search.Grid->GetVersion() != search.GridVersion)
        {
            search.Path.clear();
            search.PathCost = 0;
            search.State = ENavSearchState::Failed;
        }
        while (search.State == ENavSearchState::InProgress && maxExpansions > 0)
        {
            search.ExpandOne();
            --maxExpansions;
        }
        return search.State;
    }

    ENavSearchState NavPathfinder::GetState() const
    {
        return m_Search ? m_Search->State : ENavSearchState::Idle;
    }

    uint32_t NavPathfinder::GetExpansionCount() const
    {
        return m_Search ? m_Search->ExpansionCount : 0;
    }

    uint64_t NavPathfinder::GetGridVersionAtBegin() const
    {
        return m_Search ? m_Search->GridVersion : 0;
    }

    Core::Container::Span<const NavCellCoord> NavPathfinder::GetPathCells() const
    {
        if (!m_Search || m_Search->Path.empty())
        {
            return {};
        }
        return {m_Search->Path.data(), m_Search->Path.size()};
    }

    uint32_t NavPathfinder::GetPathCost() const
    {
        return m_Search ? m_Search->PathCost : 0;
    }

    void NavPathfinder::Reset()
    {
        if (m_Search)
        {
            Search& search = *m_Search;
            search.Grid = nullptr;
            search.GridVersion = 0;
            search.CachedPage = nullptr;
            search.ClosestDistance = UINT64_MAX;
            search.Path.clear();
            search.PathCost = 0;
            search.ExpansionCount = 0;
            search.Open.Clear();
            search.State = ENavSearchState::Idle;
        }
    }
} // namespace NorvesLib::Modules::AI
