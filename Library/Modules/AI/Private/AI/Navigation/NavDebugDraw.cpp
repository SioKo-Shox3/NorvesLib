// カメラ付近のナビ格子と経路を線で描く。
#include "NavDebugDraw.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include "AI/Navigation/NavGrid.h"
#include "Rendering/DebugDrawQueue.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        using Core::Rendering::DebugDrawQueue;
        using Math::Vector3;
        using Math::Vector4;

        struct CellStyle
        {
            ENavCellFlags Flag;
            Vector4 Color;
        };

        // 複数のフラグが立つセルには、表の先頭から最初に合う色を使う。
        const CellStyle kCellStyles[] = {
            {ENavCellFlags::StaticBlocked, Vector4(1.0f, 0.1f, 0.1f, 1.0f)},
            {ENavCellFlags::Cliff,         Vector4(1.0f, 0.5f, 0.0f, 1.0f)},
            {ENavCellFlags::SteepSlope,    Vector4(0.7f, 0.2f, 0.9f, 1.0f)},
            {ENavCellFlags::WaterDeep,     Vector4(0.0f, 0.2f, 0.8f, 1.0f)},
            {ENavCellFlags::WaterShallow,  Vector4(0.2f, 0.85f, 1.0f, 1.0f)}
        };
        constexpr float kMarkerHeight = 0.2f;

        bool IsFinitePoint(const Vector3& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        bool HasVertexRoom(const DebugDrawQueue& queue, uint32_t required)
        {
            return queue.GetVertexCount() <= DebugDrawQueue::kMaxDebugLineVertices - required;
        }

        // タイルは Z、X 順。空の行や描画範囲外の列をまとめて飛ばす。
        uint32_t LowerBoundTile(const NavGrid& grid, uint32_t first, NavTileCoord coord)
        {
            uint32_t last = grid.GetLoadedTileCount();
            while (first < last)
            {
                const uint32_t middle = first + (last - first) / 2;
                const NavTileCoord current = grid.GetLoadedTile(middle)->Coord;
                if (current.Z < coord.Z || (current.Z == coord.Z && current.X < coord.X))
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
    } // namespace

    void SubmitNavGridDebug(const NavGrid& grid, const Vector3& center, const NavDebugDrawSettings& settings,
                            DebugDrawQueue& queue)
    {
        const NavGridDesc& desc = grid.GetDesc();
        if (!IsFinitePoint(center) || !std::isfinite(settings.Radius) || settings.Radius < 0.0f ||
            !std::isfinite(settings.HeightOffset) || settings.MaxCells == 0 || !HasVertexRoom(queue, 2) ||
            !desc.IsValid() || !std::isfinite(desc.OriginX) || !std::isfinite(desc.OriginZ))
        {
            return;
        }

        const NavCellCoord centerCell = grid.WorldToCell(center.x, center.z);
        const int64_t extent = static_cast<int64_t>((std::min)(
            std::ceil(static_cast<double>(settings.Radius) / desc.CellSize), static_cast<double>(UINT32_MAX)));
        const NavCellCoord firstCell = {
            static_cast<int32_t>((std::max<int64_t>)(INT32_MIN, centerCell.X - extent)),
            static_cast<int32_t>((std::max<int64_t>)(INT32_MIN, centerCell.Z - extent))};
        const NavCellCoord lastCell = {
            static_cast<int32_t>((std::min<int64_t>)(INT32_MAX, centerCell.X + extent)),
            static_cast<int32_t>((std::min<int64_t>)(INT32_MAX, centerCell.Z + extent))};
        const NavTileCoord firstTile = grid.CellToTile(firstCell);
        const NavTileCoord lastTile = grid.CellToTile(lastCell);
        const double radiusSquared = static_cast<double>(settings.Radius) * settings.Radius;
        const float halfMark = desc.CellSize * 0.4f;
        uint32_t drawnCells = 0;
        uint32_t tileIndex = LowerBoundTile(grid, 0, firstTile);
        while (const NavTile* tile = grid.GetLoadedTile(tileIndex))
        {
            const NavTileCoord coord = tile->Coord;
            if (coord.Z > lastTile.Z)
            {
                break;
            }
            if (coord.X < firstTile.X)
            {
                tileIndex = LowerBoundTile(grid, tileIndex + 1, {firstTile.X, coord.Z});
                continue;
            }
            if (coord.X > lastTile.X)
            {
                if (coord.Z == lastTile.Z)
                {
                    break;
                }
                tileIndex = LowerBoundTile(grid, tileIndex + 1, {firstTile.X, coord.Z + 1});
                continue;
            }
            ++tileIndex;

            const int64_t tileX = static_cast<int64_t>(coord.X) * desc.TileCells;
            const int64_t tileZ = static_cast<int64_t>(coord.Z) * desc.TileCells;
            const int64_t endX = (std::min<int64_t>)(lastCell.X, tileX + desc.TileCells - 1);
            const int64_t endZ = (std::min<int64_t>)(lastCell.Z, tileZ + desc.TileCells - 1);
            for (int64_t z = (std::max<int64_t>)(firstCell.Z, tileZ); z <= endZ; ++z)
            {
                for (int64_t x = (std::max<int64_t>)(firstCell.X, tileX); x <= endX; ++x)
                {
                    const uint32_t index = tile->LocalIndex(static_cast<uint32_t>(x - tileX),
                                                            static_cast<uint32_t>(z - tileZ));
                    const uint8_t flags = tile->Flags[index];
                    if (flags == 0)
                    {
                        continue;
                    }
                    Vector3 point = grid.GetCellCenter({static_cast<int32_t>(x), static_cast<int32_t>(z)});
                    const double dx = static_cast<double>(point.x) - center.x;
                    const double dz = static_cast<double>(point.z) - center.z;
                    if (dx * dx + dz * dz > radiusSquared)
                    {
                        continue;
                    }
                    point.y += settings.HeightOffset;
                    const Vector3 low(point.x - halfMark, point.y, point.z - halfMark);
                    const Vector3 high(point.x + halfMark, point.y, point.z + halfMark);
                    if (!IsFinitePoint(low) || !IsFinitePoint(high))
                    {
                        continue;
                    }
                    for (const CellStyle& style : kCellStyles)
                    {
                        if (!HasNavFlag(flags, style.Flag))
                        {
                            continue;
                        }
                        const bool bCross = style.Flag != ENavCellFlags::WaterShallow;
                        if (!HasVertexRoom(queue, bCross ? 4u : 2u))
                        {
                            return;
                        }
                        queue.AddLine(low, high, style.Color);
                        if (bCross)
                        {
                            queue.AddLine(Vector3(low.x, point.y, high.z), Vector3(high.x, point.y, low.z), style.Color);
                        }
                        if (++drawnCells == settings.MaxCells || !HasVertexRoom(queue, 2))
                        {
                            return;
                        }
                        break;
                    }
                }
            }
        }
    }

    void SubmitNavPathDebug(Core::Container::Span<const Vector3> points, const Vector4& color, DebugDrawQueue& queue)
    {
        const Vector3* previous = nullptr;
        for (const Vector3& point : points)
        {
            if (!IsFinitePoint(point))
            {
                previous = nullptr;
                continue;
            }
            if (!HasVertexRoom(queue, previous ? 4u : 2u))
            {
                return;
            }
            if (previous)
            {
                queue.AddLine(*previous, point, color);
            }
            queue.AddLine(point, Vector3(point.x, point.y + kMarkerHeight, point.z), color);
            previous = &point;
        }
    }
} // namespace NorvesLib::Modules::AI
#endif
