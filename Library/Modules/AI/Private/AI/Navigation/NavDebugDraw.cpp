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

        bool ClipPathSegment(const Vector3& a, const Vector3& b, const Vector3& center, double radiusSquared,
                             Vector3& start, Vector3& end)
        {
            const double ax = static_cast<double>(a.x) - center.x;
            const double az = static_cast<double>(a.z) - center.z;
            const double dx = static_cast<double>(b.x) - a.x;
            const double dz = static_cast<double>(b.z) - a.z;
            const double lengthSquared = dx * dx + dz * dz;
            if (lengthSquared == 0.0)
            {
                start = a;
                end = b;
                return ax * ax + az * az <= radiusSquared;
            }

            // 円との交点を求める二次方程式の判別式を、XZ 平面の外積を使って計算する。
            const double cross = ax * dz - az * dx;
            const double discriminant = radiusSquared * lengthSquared - cross * cross;
            if (discriminant < 0.0)
            {
                return false;
            }
            const double projection = -(ax * dx + az * dz);
            const double root = std::sqrt(discriminant);
            const double t0 = (std::max)(0.0, (projection - root) / lengthSquared);
            const double t1 = (std::min)(1.0, (projection + root) / lengthSquared);
            if (t0 > t1)
            {
                return false;
            }
            const auto pointAt = [&](double t)
            {
                return Vector3(static_cast<float>(std::lerp(double(a.x), double(b.x), t)),
                               static_cast<float>(std::lerp(double(a.y), double(b.y), t)),
                               static_cast<float>(std::lerp(double(a.z), double(b.z), t)));
            };
            start = pointAt(t0);
            end = pointAt(t1);
            return true;
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

    void SubmitNavPathDebug(Core::Container::Span<const Vector3> points, const Vector3& center,
                            const NavDebugDrawSettings& settings, const Vector4& color, DebugDrawQueue& queue)
    {
        if (!IsFinitePoint(center) || !std::isfinite(settings.Radius) || settings.Radius < 0.0f)
        {
            return;
        }
        const double radiusSquared = static_cast<double>(settings.Radius) * settings.Radius;
        const Vector3* previous = nullptr;
        for (const Vector3& point : points)
        {
            if (!IsFinitePoint(point))
            {
                previous = nullptr;
                continue;
            }
            const double dx = static_cast<double>(point.x) - center.x;
            const double dz = static_cast<double>(point.z) - center.z;
            const bool bDrawMarker = dx * dx + dz * dz <= radiusSquared;
            Vector3 start, end;
            const bool bDrawSegment = previous && ClipPathSegment(*previous, point, center, radiusSquared, start, end);
            const uint32_t required = (bDrawSegment ? 2u : 0u) + (bDrawMarker ? 2u : 0u);
            if (!HasVertexRoom(queue, required))
            {
                return;
            }
            if (bDrawSegment)
            {
                queue.AddLine(start, end, color);
            }
            if (bDrawMarker)
            {
                queue.AddLine(point, Vector3(point.x, point.y + kMarkerHeight, point.z), color);
            }
            previous = &point;
        }
    }
} // namespace NorvesLib::Modules::AI
#endif
