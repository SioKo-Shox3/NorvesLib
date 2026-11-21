#include "AI/Navigation/NavGrid.h"

#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        bool GridDescriptionsMatch(const NavGridDesc& left, const NavGridDesc& right)
        {
            return left.OriginX == right.OriginX && left.OriginZ == right.OriginZ &&
                   left.CellSize == right.CellSize && left.TileCells == right.TileCells;
        }

        /// タイル座標を Z、X の順に比較する。
        bool TileCoordLess(NavTileCoord left, NavTileCoord right)
        {
            return left.Z < right.Z || (left.Z == right.Z && left.X < right.X);
        }

        /// 正の除数で割った商を負の無限大側に切り下げる。
        int32_t FloorDivide(int32_t value, int32_t divisor)
        {
            const int32_t quotient = value / divisor;
            return value % divisor < 0 ? quotient - 1 : quotient;
        }

        /// 正の除数で割った余りを 0 以上かつ除数未満にそろえる。
        uint32_t PositiveRemainder(int32_t value, int32_t divisor)
        {
            const int32_t remainder = value % divisor;
            return static_cast<uint32_t>(remainder < 0 ? remainder + divisor : remainder);
        }

        /// 一軸の座標をセル単位で切り下げ、整数の範囲に収める。NaN は 0。
        int32_t WorldAxisToCell(float position, float origin, float cellSize)
        {
            const double cell = std::floor((static_cast<double>(position) - origin) / cellSize);
            if (std::isnan(cell))
            {
                return 0;
            }
            if (cell <= static_cast<double>((std::numeric_limits<int32_t>::min)()))
            {
                return (std::numeric_limits<int32_t>::min)();
            }
            if (cell >= static_cast<double>((std::numeric_limits<int32_t>::max)()))
            {
                return (std::numeric_limits<int32_t>::max)();
            }
            return static_cast<int32_t>(cell);
        }

        /// 連結成分の根を探し、たどった節点を根に直接つなぐ。
        size_t FindRoot(Core::Container::VariableArray<size_t>& parents, size_t node)
        {
            size_t root = node;
            while (parents[root] != root)
            {
                root = parents[root];
            }
            while (parents[node] != node)
            {
                const size_t next = parents[node];
                parents[node] = root;
                node = next;
            }
            return root;
        }

        /// 二つの連結成分を根のランクに従って併合する。
        void JoinRegions(Core::Container::VariableArray<size_t>& parents,
                         Core::Container::VariableArray<uint8_t>& ranks, size_t left, size_t right)
        {
            size_t leftRoot = FindRoot(parents, left);
            size_t rightRoot = FindRoot(parents, right);
            if (leftRoot == rightRoot)
            {
                return;
            }
            if (ranks[leftRoot] < ranks[rightRoot])
            {
                const size_t temporary = leftRoot;
                leftRoot = rightRoot;
                rightRoot = temporary;
            }
            parents[rightRoot] = leftRoot;
            if (ranks[leftRoot] == ranks[rightRoot])
            {
                ++ranks[leftRoot];
            }
        }
    } // namespace

    NavGrid::NavGrid(const NavGridDesc& desc)
        : m_Desc(desc)
    {
    }

    const NavGridDesc& NavGrid::GetDesc() const
    {
        return m_Desc;
    }

    bool NavGrid::SetDesc(const NavGridDesc& desc)
    {
        if (!m_Tiles.empty() || !desc.IsValid())
        {
            return false;
        }
        m_Desc = desc;
        m_CachedTileIndex = static_cast<size_t>(-1);
        return true;
    }

    ENavTileResult NavGrid::LoadTile(NavTile&& tile)
    {
        if (!tile.IsConsistent() || m_LastTileGeneration == (std::numeric_limits<uint32_t>::max)())
        {
            return ENavTileResult::InvalidTile;
        }
        if (m_Tiles.empty())
        {
            m_Desc = tile.Grid;
            m_CachedTileIndex = static_cast<size_t>(-1);
        }
        else if (!GridDescriptionsMatch(tile.Grid, m_Desc))
        {
            return ENavTileResult::Mismatch;
        }

        const size_t index = FindTileIndex(tile.Coord);
        const bool bReplace = index < m_Tiles.size() && m_Tiles[index].Tile->Coord == tile.Coord;
        TileEntry entry;
        entry.Tile = Core::Container::MakeUnique<NavTile>(std::move(tile));
        entry.Generation = ++m_LastTileGeneration;
        m_CachedTileIndex = static_cast<size_t>(-1);
        if (bReplace)
        {
            m_Tiles[index] = std::move(entry);
        }
        else
        {
            m_Tiles.insert(m_Tiles.begin() + static_cast<ptrdiff_t>(index), std::move(entry));
        }
        RebuildConnectivity();
        ++m_Version;
        return ENavTileResult::Ok;
    }

    bool NavGrid::UnloadTile(NavTileCoord coord)
    {
        const size_t index = FindTileIndex(coord);
        if (index >= m_Tiles.size() || !(m_Tiles[index].Tile->Coord == coord))
        {
            return false;
        }
        m_CachedTileIndex = static_cast<size_t>(-1);
        m_Tiles.erase(m_Tiles.begin() + static_cast<ptrdiff_t>(index));
        RebuildConnectivity();
        ++m_Version;
        return true;
    }

    void NavGrid::Clear()
    {
        m_CachedTileIndex = static_cast<size_t>(-1);
        m_Tiles.clear();
        ++m_Version;
    }

    const NavTile* NavGrid::FindTile(NavTileCoord coord) const
    {
        if (m_CachedTileIndex < m_Tiles.size() && m_Tiles[m_CachedTileIndex].Tile->Coord == coord)
        {
            return m_Tiles[m_CachedTileIndex].Tile.get();
        }
        const size_t index = FindTileIndex(coord);
        if (index >= m_Tiles.size() || !(m_Tiles[index].Tile->Coord == coord))
        {
            return nullptr;
        }
        m_CachedTileIndex = index;
        return m_Tiles[index].Tile.get();
    }

    uint32_t NavGrid::GetTileGeneration(NavTileCoord coord) const
    {
        const size_t index = FindTileIndex(coord);
        return index < m_Tiles.size() && m_Tiles[index].Tile->Coord == coord ? m_Tiles[index].Generation : 0;
    }

    uint64_t NavGrid::GetVersion() const
    {
        return m_Version;
    }

    uint32_t NavGrid::GetLoadedTileCount() const
    {
        return static_cast<uint32_t>(m_Tiles.size());
    }

    const NavTile* NavGrid::GetLoadedTile(uint32_t index) const
    {
        return index < m_Tiles.size() ? m_Tiles[index].Tile.get() : nullptr;
    }

    NavCellCoord NavGrid::WorldToCell(float x, float z) const
    {
        if (!m_Desc.IsValid())
        {
            return {};
        }
        return {WorldAxisToCell(x, m_Desc.OriginX, m_Desc.CellSize),
                WorldAxisToCell(z, m_Desc.OriginZ, m_Desc.CellSize)};
    }

    Math::Vector3 NavGrid::GetCellCenter(NavCellCoord cell) const
    {
        const NavCellRef ref = FindCell(cell);
        const float height = ref ? ref.Tile->Height[ref.Index] : 0.0f;
        const float x = static_cast<float>(static_cast<double>(m_Desc.OriginX) +
            (static_cast<double>(cell.X) + 0.5) * m_Desc.CellSize);
        const float z = static_cast<float>(static_cast<double>(m_Desc.OriginZ) +
            (static_cast<double>(cell.Z) + 0.5) * m_Desc.CellSize);
        return Math::Vector3(x, height, z);
    }

    NavTileCoord NavGrid::CellToTile(NavCellCoord cell) const
    {
        if (!m_Desc.IsValid())
        {
            return {};
        }
        const int32_t divisor = static_cast<int32_t>(m_Desc.TileCells);
        return {FloorDivide(cell.X, divisor), FloorDivide(cell.Z, divisor)};
    }

    uint32_t NavGrid::CellToLocalIndex(NavCellCoord cell) const
    {
        if (!m_Desc.IsValid())
        {
            return 0;
        }
        const int32_t divisor = static_cast<int32_t>(m_Desc.TileCells);
        const uint32_t localX = PositiveRemainder(cell.X, divisor);
        const uint32_t localZ = PositiveRemainder(cell.Z, divisor);
        return localZ * m_Desc.TileCells + localX;
    }

    NavCellRef NavGrid::FindCell(NavCellCoord cell) const
    {
        if (!m_Desc.IsValid())
        {
            return {};
        }
        const NavTile* tile = FindTile(CellToTile(cell));
        return tile ? NavCellRef{tile, CellToLocalIndex(cell)} : NavCellRef{};
    }

    uint32_t NavGrid::GetConnectivityRegion(NavCellCoord cell) const
    {
        const NavCellRef ref = FindCell(cell);
        if (!ref || (ref.Tile->Flags[ref.Index] & kNavBlockingFlags) != 0)
        {
            return 0;
        }
        const uint16_t region = ref.Tile->Region[ref.Index];
        return m_Tiles[m_CachedTileIndex].Connectivity[region];
    }

    size_t NavGrid::FindTileIndex(NavTileCoord coord) const
    {
        size_t first = 0;
        size_t last = m_Tiles.size();
        while (first < last)
        {
            const size_t middle = first + (last - first) / 2;
            if (TileCoordLess(m_Tiles[middle].Tile->Coord, coord))
            {
                first = middle + 1;
            }
            else
            {
                last = middle;
            }
        }
        return first;
    }

    void NavGrid::RebuildConnectivity()
    {
        Core::Container::VariableArray<size_t> offsets(m_Tiles.size());
        size_t totalRegions = 0;
        for (size_t i = 0; i < m_Tiles.size(); ++i)
        {
            offsets[i] = totalRegions;
            const uint32_t regionCount = m_Tiles[i].Tile->RegionCount;
            totalRegions += regionCount;
            m_Tiles[i].Connectivity.assign(static_cast<size_t>(regionCount) + 1, 0);
        }

        Core::Container::VariableArray<size_t> parents(totalRegions);
        Core::Container::VariableArray<uint8_t> ranks(totalRegions, 0);
        for (size_t i = 0; i < totalRegions; ++i)
        {
            parents[i] = i;
        }

        for (size_t i = 0; i < m_Tiles.size(); ++i)
        {
            const NavTile& tile = *m_Tiles[i].Tile;
            for (uint32_t axis = 0; axis < 2; ++axis)
            {
                NavTileCoord neighborCoord = tile.Coord;
                if (axis == 0)
                {
                    if (neighborCoord.X == (std::numeric_limits<int32_t>::max)())
                    {
                        continue;
                    }
                    ++neighborCoord.X;
                }
                else
                {
                    if (neighborCoord.Z == (std::numeric_limits<int32_t>::max)())
                    {
                        continue;
                    }
                    ++neighborCoord.Z;
                }
                const size_t neighborIndex = FindTileIndex(neighborCoord);
                if (neighborIndex >= m_Tiles.size() || !(m_Tiles[neighborIndex].Tile->Coord == neighborCoord))
                {
                    continue;
                }
                const NavTile& neighbor = *m_Tiles[neighborIndex].Tile;
                for (uint32_t edge = 0; edge < tile.TileCells; ++edge)
                {
                    const uint32_t local = axis == 0 ? tile.LocalIndex(tile.TileCells - 1, edge)
                                                     : tile.LocalIndex(edge, tile.TileCells - 1);
                    const uint32_t adjacent = axis == 0 ? neighbor.LocalIndex(0, edge)
                                                        : neighbor.LocalIndex(edge, 0);
                    const uint16_t leftRegion = tile.Region[local];
                    const uint16_t rightRegion = neighbor.Region[adjacent];
                    if (leftRegion != 0 && rightRegion != 0)
                    {
                        JoinRegions(parents, ranks, offsets[i] + leftRegion - 1,
                                    offsets[neighborIndex] + rightRegion - 1);
                    }
                }
            }
        }

        Core::Container::VariableArray<uint32_t> rootNumbers(totalRegions, 0);
        uint32_t nextRegion = 0;
        for (size_t i = 0; i < m_Tiles.size(); ++i)
        {
            const uint32_t regionCount = m_Tiles[i].Tile->RegionCount;
            for (uint32_t region = 1; region <= regionCount; ++region)
            {
                const size_t root = FindRoot(parents, offsets[i] + region - 1);
                if (rootNumbers[root] == 0)
                {
                    rootNumbers[root] = ++nextRegion;
                }
                m_Tiles[i].Connectivity[region] = rootNumbers[root];
            }
        }
    }
} // namespace NorvesLib::Modules::AI
