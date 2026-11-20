#include "AI/Navigation/NavGridQueries.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Modules::AI::NavGridQueries
{
    namespace
    {
        constexpr int64_t kMinCell = INT32_MIN;
        constexpr int64_t kMaxCell = INT32_MAX;
        constexpr double kInfinity = std::numeric_limits<double>::infinity();

        bool IsPassable(NavCellRef ref, uint8_t thresholdQ)
        {
            return ref && IsNavCellPassable(ref.Tile->Flags[ref.Index], ref.Tile->ClearanceQ[ref.Index], thresholdQ);
        }

        // 次の輪も含めた外側のセル中心までの、一軸方向の距離の下限。中心は GetCellCenter と同じ精度に丸める。
        double OuterAxisDistance(int64_t center, int64_t ring, double position, double origin, double cellSize)
        {
            double distance = kInfinity;
            if (center - ring >= kMinCell)
            {
                const float left = static_cast<float>(origin + (static_cast<double>(center - ring) + 0.5) * cellSize);
                distance = position - left;
            }
            if (center + ring <= kMaxCell)
            {
                const float right = static_cast<float>(origin + (static_cast<double>(center + ring) + 0.5) * cellSize);
                distance = (std::min)(distance, right - position);
            }
            return (std::max)(distance, 0.0);
        }

        // 指定した矩形と輪の四辺が重なるセルだけを列挙する。角は上下の辺に含める。
        template <typename Visitor>
        void VisitRing(NavCellCoord center, int64_t ring, int64_t minX, int64_t maxX,
                       int64_t minZ, int64_t maxZ, const Visitor& visit)
        {
            const int64_t left = center.X - ring;
            const int64_t right = center.X + ring;
            const int64_t top = center.Z - ring;
            const int64_t bottom = center.Z + ring;
            for (int64_t x = (std::max)(left, minX); x <= (std::min)(right, maxX); ++x)
            {
                if (top >= minZ && top <= maxZ)
                {
                    visit(x, top);
                }
                if (bottom >= minZ && bottom <= maxZ)
                {
                    visit(x, bottom);
                }
            }
            for (int64_t z = (std::max)(top + 1, minZ); z <= (std::min)(bottom - 1, maxZ); ++z)
            {
                if (left >= minX && left <= maxX)
                {
                    visit(left, z);
                }
                if (right >= minX && right <= maxX)
                {
                    visit(right, z);
                }
            }
        }

        bool IsFinite(const Math::Vector3& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        // 境界までの割合を毎回整数座標から計算し、加算による角の判定誤差を避ける。
        double BoundaryFraction(int32_t cell, int step, double startOffset, double endOffset,
                                double delta, double cellSize)
        {
            if (step == 0 || (step > 0 && cell == kMaxCell) || (step < 0 && cell == kMinCell))
            {
                return kInfinity;
            }
            const double edge = static_cast<double>(cell) + (step > 0 ? 1.0 : 0.0);
            const double boundary = edge * cellSize;
            // 長い線分では終点の手前と奥で割合が同じ 1 に丸まるため、境界座標で終端を判定する。
            if ((step > 0 && boundary > endOffset) || (step < 0 && boundary < endOffset))
            {
                return kInfinity;
            }
            return std::clamp((boundary - startOffset) / delta, 0.0, 1.0);
        }
    } // namespace

    bool IsCellPassable(const NavGrid& grid, NavCellCoord cell, uint8_t thresholdQ)
    {
        return IsPassable(grid.FindCell(cell), thresholdQ);
    }

    bool IsWalkable(const NavGrid& grid, const Math::Vector3& position, float radius)
    {
        return IsCellPassable(grid, grid.WorldToCell(position.x, position.z),
                              NavClearanceThresholdQ(radius, grid.GetDesc().CellSize));
    }

    bool FindNearestWalkable(const NavGrid& grid, const Math::Vector3& position, float radius,
                             float searchRadius, Math::Vector3& outPosition)
    {
        if (grid.GetLoadedTileCount() == 0 || !std::isfinite(position.x) || !std::isfinite(position.z))
        {
            return false;
        }
        const NavGridDesc& desc = grid.GetDesc();
        const uint8_t threshold = NavClearanceThresholdQ(radius, desc.CellSize);
        const double limit = std::isfinite(searchRadius) && searchRadius > 0.0f ? searchRadius : 0.0;
        double bestSquared = limit * limit;
        const NavCellCoord center = grid.WorldToCell(position.x, position.z);
        NavCellCoord bestCell;
        bool bFound = false;
        const auto visit = [&](int64_t x, int64_t z)
        {
            const NavCellCoord cell{static_cast<int32_t>(x), static_cast<int32_t>(z)};
            if (!IsCellPassable(grid, cell, threshold))
            {
                return;
            }
            const Math::Vector3 point = grid.GetCellCenter(cell);
            const double dx = static_cast<double>(point.x) - position.x;
            const double dz = static_cast<double>(point.z) - position.z;
            const double squared = dx * dx + dz * dz;
            if (squared < bestSquared || (squared == bestSquared &&
                (!bFound || cell.Z < bestCell.Z || (cell.Z == bestCell.Z && cell.X < bestCell.X))))
            {
                bFound = true;
                bestSquared = squared;
                bestCell = cell;
            }
        };

        visit(center.X, center.Z);
        for (int64_t ring = 1; ring <= kMaxCell - kMinCell;)
        {
            const double bound = (std::min)(OuterAxisDistance(center.X, ring, position.x, desc.OriginX, desc.CellSize),
                                            OuterAxisDistance(center.Z, ring, position.z, desc.OriginZ, desc.CellSize));
            if (bound * bound > bestSquared)
            {
                break;
            }
            // 近傍ではタイル一覧を走査するより、四辺を直接引く方が安い。
            constexpr int64_t kDirectRings = 8;
            if (ring <= kDirectRings)
            {
                VisitRing(center, ring, kMinCell, kMaxCell, kMinCell, kMaxCell, visit);
                ++ring;
                continue;
            }

            // 広域検索では未読み込みの区間を列挙せず、タイルに届く次の輪まで飛ばす。
            int64_t nextRing = kMaxCell - kMinCell + 1;
            const int64_t width = desc.TileCells;
            for (uint32_t i = 0; i < grid.GetLoadedTileCount(); ++i)
            {
                const NavTileCoord coord = grid.GetLoadedTile(i)->Coord;
                const int64_t x = coord.X * width;
                const int64_t z = coord.Z * width;
                const int64_t minX = (std::max)(x, kMinCell);
                const int64_t maxX = (std::min)(x + width - 1, kMaxCell);
                const int64_t minZ = (std::max)(z, kMinCell);
                const int64_t maxZ = (std::min)(z + width - 1, kMaxCell);
                if (minX > maxX || minZ > maxZ)
                {
                    continue;
                }
                const int64_t first = (std::max)({minX - center.X, center.X - maxX, minZ - center.Z, center.Z - maxZ});
                const int64_t last = (std::max)({center.X - minX, maxX - center.X, center.Z - minZ, maxZ - center.Z});
                if (ring >= first && ring <= last)
                {
                    VisitRing(center, ring, minX, maxX, minZ, maxZ, visit);
                }
                if (ring < last)
                {
                    nextRing = (std::min)(nextRing, (std::max)(first, ring + 1));
                }
            }
            ring = nextRing;
        }
        if (bFound)
        {
            outPosition = grid.GetCellCenter(bestCell);
        }
        return bFound;
    }

    NavRaycastResult Raycast(const NavGrid& grid, const Math::Vector3& from, const Math::Vector3& to, float radius)
    {
        NavCellCoord cell = grid.WorldToCell(from.x, from.z);
        const NavGridDesc& desc = grid.GetDesc();
        if (!IsFinite(from) || !IsFinite(to) || !std::isfinite(radius) ||
            !std::isfinite(desc.OriginX) || !std::isfinite(desc.OriginZ))
        {
            return {true, 0.0f, cell, from};
        }

        const uint8_t threshold = NavClearanceThresholdQ(radius, desc.CellSize);
        NavRaycastResult result{false, 1.0f, grid.WorldToCell(to.x, to.z), to};
        uint32_t visited = 0;
        const auto hit = [&](NavCellCoord candidate, double t)
        {
            constexpr uint32_t kMaxVisited = 1u << 20;
            if (visited++ < kMaxVisited && IsCellPassable(grid, candidate, threshold))
            {
                return false;
            }
            result = {true, static_cast<float>(t), candidate,
                      {static_cast<float>(std::lerp(static_cast<double>(from.x), static_cast<double>(to.x), t)),
                       static_cast<float>(std::lerp(static_cast<double>(from.y), static_cast<double>(to.y), t)),
                       static_cast<float>(std::lerp(static_cast<double>(from.z), static_cast<double>(to.z), t))}};
            return true;
        };

        const double dx = static_cast<double>(to.x) - from.x;
        const double dz = static_cast<double>(to.z) - from.z;
        if (hit(cell, 0.0) || (dx == 0.0 && dz == 0.0))
        {
            return result;
        }
        const double startX = static_cast<double>(from.x) - desc.OriginX;
        const double startZ = static_cast<double>(from.z) - desc.OriginZ;
        const double endX = static_cast<double>(to.x) - desc.OriginX;
        const double endZ = static_cast<double>(to.z) - desc.OriginZ;
        const bool bOnX = cell.X > kMinCell && startX == static_cast<double>(cell.X) * desc.CellSize;
        const bool bOnZ = cell.Z > kMinCell && startZ == static_cast<double>(cell.Z) * desc.CellSize;
        if ((bOnX && hit({cell.X - 1, cell.Z}, 0.0)) ||
            (bOnZ && hit({cell.X, cell.Z - 1}, 0.0)) ||
            (bOnX && bOnZ && hit({cell.X - 1, cell.Z - 1}, 0.0)))
        {
            return result;
        }

        const int stepX = (dx > 0.0) - (dx < 0.0);
        const int stepZ = (dz > 0.0) - (dz < 0.0);
        for (;;)
        {
            const double tMaxX = BoundaryFraction(cell.X, stepX, startX, endX, dx, desc.CellSize);
            const double tMaxZ = BoundaryFraction(cell.Z, stepZ, startZ, endZ, dz, desc.CellSize);
            const double t = (std::min)(tMaxX, tMaxZ);
            if (t > 1.0)
            {
                return result;
            }
            if (tMaxX == tMaxZ)
            {
                if (hit({cell.X + stepX, cell.Z}, t) || hit({cell.X, cell.Z + stepZ}, t))
                {
                    return result;
                }
            }
            if (tMaxX == t)
            {
                cell.X += stepX;
            }
            if (tMaxZ == t)
            {
                cell.Z += stepZ;
            }
            // 格子線に沿う線分は、進むたびに反対側の列または行にも触れる。
            if (hit(cell, t) || (stepX == 0 && bOnX && hit({cell.X - 1, cell.Z}, t)) ||
                (stepZ == 0 && bOnZ && hit({cell.X, cell.Z - 1}, t)))
            {
                return result;
            }
        }
    }

    uint32_t SampleCost(const NavGrid& grid, const Math::Vector3& position, const NavAgentProfile& profile)
    {
        const NavCellRef ref = grid.FindCell(grid.WorldToCell(position.x, position.z));
        if (!IsPassable(ref, NavClearanceThresholdQ(profile.Radius, grid.GetDesc().CellSize)))
        {
            return kNavCostImpassable;
        }
        return NavStepCost(false, NavCellExtraPer128(profile, ref.Tile->Flags[ref.Index], ref.Tile->GrassQ[ref.Index]));
    }
} // namespace NorvesLib::Modules::AI::NavGridQueries
