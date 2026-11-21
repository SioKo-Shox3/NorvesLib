#include "AI/Navigation/NavDynamicOverlay.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        constexpr int64_t kMaxRasterCells = 65536;

        bool IsDimensionValid(float value)
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        // 円の中心、カプセルの線分、OBB をいずれも局所座標の矩形として扱う。
        // セル中心から矩形までの距離と、形状の半径に半セルを足した値を比べる。
        struct FootprintBox
        {
            double AxisX = 1.0;
            double AxisZ = 0.0;
            double HalfX = 0.0;
            double HalfZ = 0.0;
            double Radius = 0.0;

            bool Covers(double dx, double dz, double reachSquared) const
            {
                const double localX = dx * AxisX + dz * AxisZ;
                const double localZ = dz * AxisX - dx * AxisZ;
                const double gapX = (std::max)(std::abs(localX) - HalfX, 0.0);
                const double gapZ = (std::max)(std::abs(localZ) - HalfZ, 0.0);
                return gapX * gapX + gapZ * gapZ <= reachSquared;
            }
        };

        bool MakeFootprintBox(const NavDynamicFootprint& footprint, FootprintBox& box)
        {
            if (!std::isfinite(footprint.Center.x) || !std::isfinite(footprint.Center.y) ||
                !std::isfinite(footprint.Center.z) || !std::isfinite(footprint.AxisX) ||
                !std::isfinite(footprint.AxisZ) || !std::isfinite(footprint.MinY) ||
                !std::isfinite(footprint.MaxY) || footprint.MinY > footprint.MaxY ||
                !IsDimensionValid(footprint.Radius) || !IsDimensionValid(footprint.HalfLength) ||
                !IsDimensionValid(footprint.HalfExtentX) || !IsDimensionValid(footprint.HalfExtentZ))
            {
                return false;
            }
            switch (footprint.Shape)
            {
            case ENavObstacleShape::Circle:
                box.Radius = footprint.Radius;
                return true;
            case ENavObstacleShape::Capsule:
                box.HalfX = footprint.HalfLength;
                box.Radius = footprint.Radius;
                break;
            case ENavObstacleShape::Obb:
                box.HalfX = footprint.HalfExtentX;
                box.HalfZ = footprint.HalfExtentZ;
                break;
            default:
                return false;
            }
            const double length = std::hypot(static_cast<double>(footprint.AxisX), static_cast<double>(footprint.AxisZ));
            if (length > 0.0)
            {
                box.AxisX = footprint.AxisX / length;
                box.AxisZ = footprint.AxisZ / length;
            }
            return true;
        }

        // セル中心 (i + 0.5) が半セル広げた外接矩形に入る添字の両端。
        struct CellRange
        {
            double First;
            double Last;
        };

        CellRange CandidateRange(double center, double extent, double cellSize)
        {
            return {std::ceil((center - extent) / cellSize - 1.0),
                    std::floor((center + extent) / cellSize)};
        }
    }

    uint32_t NavCostMultiplierToExtraPer128(float multiplier)
    {
        if (!std::isfinite(multiplier) || multiplier <= 1.0f)
        {
            return 0;
        }
        const double extra = (std::min)(static_cast<double>(multiplier) - 1.0, 255.0);
        return static_cast<uint32_t>(std::round(extra * kNavCostOrthogonal));
    }

    void RasterizeNavDynamicFootprint(const NavGrid& grid, const NavDynamicFootprint& footprint,
                                      float heightBand, Core::Container::VariableArray<NavCellCoord>& outCells)
    {
        outCells.clear();
        const NavGridDesc& desc = grid.GetDesc();
        FootprintBox box;
        if (!desc.IsValid() || !std::isfinite(desc.OriginX) || !std::isfinite(desc.OriginZ) ||
            !IsDimensionValid(heightBand) || !MakeFootprintBox(footprint, box))
        {
            return;
        }
        const double cellSize = desc.CellSize;
        const double centerX = static_cast<double>(footprint.Center.x) - desc.OriginX;
        const double centerZ = static_cast<double>(footprint.Center.z) - desc.OriginZ;
        const double extentX = std::abs(box.AxisX) * box.HalfX + std::abs(box.AxisZ) * box.HalfZ + box.Radius;
        const double extentZ = std::abs(box.AxisZ) * box.HalfX + std::abs(box.AxisX) * box.HalfZ + box.Radius;
        const CellRange xs = CandidateRange(centerX, extentX, cellSize);
        const CellRange zs = CandidateRange(centerZ, extentZ, cellSize);
        if ((xs.Last - xs.First + 1.0) * (zs.Last - zs.First + 1.0) > kMaxRasterCells)
        {
            NORVES_LOG_WARNING("AI", "動的障害物の走査範囲が 65536 セルを超えたため、押印を省きます。");
            return;
        }
        constexpr double minCell = (std::numeric_limits<int32_t>::min)();
        constexpr double maxCell = (std::numeric_limits<int32_t>::max)();
        if (xs.Last < minCell || xs.First > maxCell || zs.Last < minCell || zs.First > maxCell)
        {
            return;
        }
        const auto firstX = static_cast<int64_t>((std::max)(xs.First, minCell));
        const auto lastX = static_cast<int64_t>((std::min)(xs.Last, maxCell));
        const auto firstZ = static_cast<int64_t>((std::max)(zs.First, minCell));
        const auto lastZ = static_cast<int64_t>((std::min)(zs.Last, maxCell));
        const double reach = box.Radius + cellSize * 0.5;
        const double reachSquared = reach * reach;
        for (int64_t z = firstZ; z <= lastZ; ++z)
        {
            const double dz = (static_cast<double>(z) + 0.5) * cellSize - centerZ;
            for (int64_t x = firstX; x <= lastX; ++x)
            {
                const double dx = (static_cast<double>(x) + 0.5) * cellSize - centerX;
                if (!box.Covers(dx, dz, reachSquared))
                {
                    continue;
                }
                const NavCellCoord cell{static_cast<int32_t>(x), static_cast<int32_t>(z)};
                const NavCellRef ref = grid.FindCell(cell);
                if (ref)
                {
                    const float ground = ref.Tile->Height[ref.Index];
                    if (!(footprint.MaxY >= ground - 0.05f && footprint.MinY <= ground + heightBand))
                    {
                        continue;
                    }
                }
                outCells.push_back(cell);
            }
        }
    }

    void NavDynamicOverlay::Reset(const NavGridDesc& desc)
    {
        Clear();
        m_Coordinates = NavGrid(desc);
    }

    void NavDynamicOverlay::Clear()
    {
        m_Tiles.clear();
        m_NonZeroCells = 0;
        ++m_Generation;
    }

    size_t NavDynamicOverlay::FindTileIndex(NavTileCoord coord) const
    {
        const auto position = std::lower_bound(m_Tiles.begin(), m_Tiles.end(), coord,
            [](const TileEntry& tile, NavTileCoord key)
            {
                return tile.Coord.Z < key.Z || (tile.Coord.Z == key.Z && tile.Coord.X < key.X);
            });
        return static_cast<size_t>(position - m_Tiles.begin());
    }

    const NavDynamicOverlay::CellCounters* NavDynamicOverlay::FindCounters(NavCellCoord cell) const
    {
        const NavTileCoord coord = m_Coordinates.CellToTile(cell);
        const size_t index = FindTileIndex(coord);
        return index < m_Tiles.size() && m_Tiles[index].Coord == coord
            ? &m_Tiles[index].Cells[m_Coordinates.CellToLocalIndex(cell)] : nullptr;
    }

    uint32_t NavDynamicOverlay::AdvanceSerial()
    {
        // 0 は未押印のタイル用に空けておく。
        if (++m_Serial == 0)
        {
            ++m_Serial;
        }
        return m_Serial;
    }

    void NavDynamicOverlay::Apply(NavDynamicStamp& stamp)
    {
        if (stamp.bApplied || !m_Coordinates.GetDesc().IsValid())
        {
            return;
        }
        const auto cellLess = [](NavCellCoord a, NavCellCoord b)
        {
            return a.Z < b.Z || (a.Z == b.Z && a.X < b.X);
        };
        if (!std::is_sorted(stamp.Cells.begin(), stamp.Cells.end(), cellLess))
        {
            std::sort(stamp.Cells.begin(), stamp.Cells.end(), cellLess);
        }
        stamp.Cells.erase(std::unique(stamp.Cells.begin(), stamp.Cells.end()), stamp.Cells.end());

        const bool block = stamp.Mode == ENavObstacleMode::Block;
        const uint32_t amount = block ? 1u : stamp.CostExtraPer128;
        const uint32_t side = m_Coordinates.GetDesc().TileCells;
        // 全セルの格納先と加算可能なことを先に確かめ、押印の途中では失敗させない。
        for (NavCellCoord cell : stamp.Cells)
        {
            const NavTileCoord coord = m_Coordinates.CellToTile(cell);
            const size_t index = FindTileIndex(coord);
            if (index == m_Tiles.size() || m_Tiles[index].Coord != coord)
            {
                TileEntry tile;
                tile.Coord = coord;
                tile.Cells.resize(side * side);
                m_Tiles.insert(m_Tiles.begin() + static_cast<ptrdiff_t>(index), std::move(tile));
            }
            const CellCounters& counters = m_Tiles[index].Cells[m_Coordinates.CellToLocalIndex(cell)];
            const uint32_t value = block ? counters.Blocks : counters.Cost;
            if (value > (std::numeric_limits<uint32_t>::max)() - amount)
            {
                NORVES_LOG_ERROR("AI", "動的障害物のカウンタが上限を超えるため、押印できません。");
                return;
            }
        }
        if (!stamp.Cells.empty())
        {
            AdvanceSerial();
        }
        for (NavCellCoord cell : stamp.Cells)
        {
            TileEntry& tile = m_Tiles[FindTileIndex(m_Coordinates.CellToTile(cell))];
            CellCounters& counters = tile.Cells[m_Coordinates.CellToLocalIndex(cell)];
            if (counters.IsZero() && amount != 0)
            {
                ++m_NonZeroCells;
            }
            (block ? counters.Blocks : counters.Cost) += amount;
            tile.Version = m_Serial;
        }
        stamp.OverlayGeneration = m_Generation;
        stamp.bApplied = true;
    }

    void NavDynamicOverlay::Revoke(NavDynamicStamp& stamp)
    {
        if (!stamp.bApplied)
        {
            return;
        }
        stamp.bApplied = false;
        if (stamp.OverlayGeneration != m_Generation)
        {
            return;
        }
        const bool block = stamp.Mode == ENavObstacleMode::Block;
        const uint32_t amount = block ? 1u : stamp.CostExtraPer128;
        uint32_t version = 0;
        bool underflow = false;
        for (NavCellCoord cell : stamp.Cells)
        {
            const NavTileCoord coord = m_Coordinates.CellToTile(cell);
            const size_t index = FindTileIndex(coord);
            if (index == m_Tiles.size() || m_Tiles[index].Coord != coord)
            {
                underflow |= amount != 0;
                continue;
            }
            TileEntry& tile = m_Tiles[index];
            if (version == 0)
            {
                version = AdvanceSerial();
            }
            tile.Version = version;
            CellCounters& counters = tile.Cells[m_Coordinates.CellToLocalIndex(cell)];
            const bool wasZero = counters.IsZero();
            uint32_t& value = block ? counters.Blocks : counters.Cost;
            underflow |= value < amount;
            value -= (std::min)(value, amount);
            if (!wasZero && counters.IsZero())
            {
                --m_NonZeroCells;
            }
        }
        if (underflow)
        {
            NORVES_LOG_ERROR("AI", "動的障害物の解除量が残量を超えたため、該当カウンタを 0 にしました。");
        }
    }

    bool NavDynamicOverlay::IsBlocked(NavCellCoord cell) const
    {
        const CellCounters* counters = FindCounters(cell);
        return counters && counters->Blocks != 0;
    }

    uint32_t NavDynamicOverlay::GetCostExtraPer128(NavCellCoord cell) const
    {
        const CellCounters* counters = FindCounters(cell);
        return counters ? counters->Cost : 0;
    }

    uint32_t NavDynamicOverlay::GetTileVersion(NavTileCoord coord) const
    {
        const size_t index = FindTileIndex(coord);
        return index < m_Tiles.size() && m_Tiles[index].Coord == coord ? m_Tiles[index].Version : 0;
    }
}
