#include "Rendering/Terrain/TerrainHeightField.h"

#include "Math/GeometryIntersection.h"
#include "TerrainHeightFieldGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Rendering::Terrain
{
    namespace
    {
        /// 片軸の閉区間でレイを切る。平行な場合も外周を含める
        bool ClipAxis(double origin, double direction, double minimum, double maximum, double& begin, double& end)
        {
            if (direction == 0.0)
            {
                return origin >= minimum && origin <= maximum;
            }
            double nearT = (minimum - origin) / direction;
            double farT = (maximum - origin) / direction;
            if (nearT > farT)
            {
                std::swap(nearT, farT);
            }
            begin = std::max(begin, nearT);
            end = std::min(end, farT);
            return begin <= end;
        }

        struct DdaAxis
        {
            double Origin;
            double Direction;
            double Size;
            int64_t Index;
            int Step;

            double Next() const
            {
                if (Step == 0)
                {
                    return std::numeric_limits<double>::infinity();
                }
                const double boundary = static_cast<double>(Index + (Step > 0 ? 1 : 0)) * Size;
                return (boundary - Origin) / Direction;
            }
        };

        /// 閉じた区間に接する全区画を訪れる 2D-DDA。visitor はその区間までに最近点が確定すると true
        template <typename Visitor>
        bool WalkDda(double originX, double originZ, double directionX, double directionZ, double size,
            uint32_t firstColumn, uint32_t lastColumn, uint32_t firstRow, uint32_t lastRow,
            double begin, double end, const Visitor& visitor)
        {
            const auto makeAxis = [size, begin](double origin, double direction, uint32_t first, uint32_t last)
            {
                const double position = (origin + direction * begin) / size;
                const int64_t index = static_cast<int64_t>(std::clamp(std::floor(position),
                    static_cast<double>(first), static_cast<double>(last)));
                return DdaAxis{origin, direction, size, index, direction > 0.0 ? 1 : (direction < 0.0 ? -1 : 0)};
            };
            DdaAxis x = makeAxis(originX, directionX, firstColumn, lastColumn);
            DdaAxis z = makeAxis(originZ, directionZ, firstRow, lastRow);

            // 区間端では両側、角では最大 4 区画を試す。平行に境を進む場合も両側になる。
            // 許容幅は区画番号の double の丸めだけを吸収し、三角形の交差条件は緩めない。
            const auto visitAt = [&](double positionT, double intervalBegin, double intervalEnd)
            {
                const auto indices = [size, positionT](double origin, double direction, int64_t (&values)[2])
                {
                    const double position = (origin + direction * positionT) / size;
                    const double rounded = std::round(position);
                    const double tolerance = 32.0 * std::numeric_limits<double>::epsilon()
                        * std::max(1.0, std::abs(position));
                    if (std::abs(position - rounded) <= tolerance)
                    {
                        values[0] = static_cast<int64_t>(rounded) - 1;
                        values[1] = static_cast<int64_t>(rounded);
                        return 2;
                    }
                    values[0] = static_cast<int64_t>(std::floor(position));
                    return 1;
                };
                int64_t columns[2];
                int64_t rows[2];
                const int columnCount = indices(originX, directionX, columns);
                const int rowCount = indices(originZ, directionZ, rows);
                bool bStop = false;
                for (int r = 0; r < rowCount; ++r)
                {
                    for (int c = 0; c < columnCount; ++c)
                    {
                        if (columns[c] >= firstColumn && columns[c] <= lastColumn
                            && rows[r] >= firstRow && rows[r] <= lastRow)
                        {
                            // 短絡評価せず、同じ境へ接する全区画の最小を取る。
                            const bool bFound = visitor(static_cast<uint32_t>(columns[c]),
                                static_cast<uint32_t>(rows[r]), intervalBegin, intervalEnd);
                            bStop = bStop || bFound;
                        }
                    }
                }
                return bStop;
            };
            if (visitAt(begin, begin, begin))
            {
                return true;
            }
            double current = begin;
            while (current < end)
            {
                const double nextX = x.Next();
                const double nextZ = z.Next();
                const double next = std::max(current, std::min({nextX, nextZ, end}));
                const bool bIntervalHit = visitAt(current + (next - current) * 0.5, current, next);
                const bool bBoundaryHit = visitAt(next, next, next);
                if (bIntervalHit || bBoundaryHit)
                {
                    return true;
                }
                if (next == end)
                {
                    break;
                }
                if (nextX <= next)
                {
                    x.Index += x.Step;
                }
                if (nextZ <= next)
                {
                    z.Index += z.Step;
                }
                current = next;
            }
            return false;
        }
    } // namespace

    bool TerrainHeightField::Raycast(const Math::Ray& ray, float maxDistance, TerrainRayHit& outHit) const
    {
        if (!m_Grid || !(maxDistance >= 0.0f)
            || !std::isfinite(ray.Origin.x) || !std::isfinite(ray.Origin.y) || !std::isfinite(ray.Origin.z))
        {
            return false;
        }
        const double length = std::hypot(static_cast<double>(ray.Direction.x),
            static_cast<double>(ray.Direction.y), static_cast<double>(ray.Direction.z));
        if (!(length > 0.0) || !std::isfinite(length))
        {
            return false;
        }
        const Math::Vector3 direction(static_cast<float>(ray.Direction.x / length),
            static_cast<float>(ray.Direction.y / length), static_cast<float>(ray.Direction.z / length));
        const Math::Ray localRay(Math::Vector3(), direction);
        const GridData& grid = *m_Grid;
        const double originX = (static_cast<double>(ray.Origin.x) - grid.FirstX) / grid.Spacing;
        const double originZ = (static_cast<double>(ray.Origin.z) - grid.FirstZ) / grid.Spacing;
        const double directionX = direction.x / grid.Spacing;
        const double directionZ = direction.z / grid.Spacing;
        if (Contains(ray.Origin.x, ray.Origin.z) && ray.Origin.y < SampleHeight(ray.Origin.x, ray.Origin.z))
        {
            TerrainRayHit hit;
            hit.Point = ray.Origin;
            hit.Column = std::min(static_cast<uint32_t>(originX), grid.Columns - 2u);
            hit.Row = std::min(static_cast<uint32_t>(originZ), grid.Rows - 2u);
            outHit = hit;
            return true;
        }

        // 鉛直に近く、最大距離までの横移動もセル幅に対して微小なら 1 セルで照会する。
        // 外周から外向きの微小成分で t の上限が 0 になる場合も、三角形判定と同じ丸めで接触を拾う。
        const bool bSingleCell = Contains(ray.Origin.x, ray.Origin.z)
            && ((direction.x == 0.0f && direction.z == 0.0f)
                || (std::abs(direction.x) <= 1e-8f && std::abs(direction.z) <= 1e-8f
                    && std::isfinite(maxDistance)
                    && std::max(std::abs(directionX), std::abs(directionZ)) * maxDistance <= 1e-7));
        double begin = 0.0;
        double end = maxDistance;
        if (!bSingleCell && (!ClipAxis(originX, directionX, 0.0, grid.Columns - 1u, begin, end)
            || !ClipAxis(originZ, directionZ, 0.0, grid.Rows - 1u, begin, end)))
        {
            return false;
        }
        TerrainRayHit closest;
        closest.Distance = maxDistance;
        bool bHit = false;
        const auto testCell = [&](uint32_t column, uint32_t row, double, double intervalEnd)
        {
            // ワールド座標を float に丸めてから引かず、原点との差を double で作ってから渡す。
            const double relativeFirstX = grid.FirstX - ray.Origin.x;
            const double relativeFirstZ = grid.FirstZ - ray.Origin.z;
            // 共有頂点は隣のセルからも同じ式で作り、足し算の順による隙間を作らない。
            const double x0 = relativeFirstX + static_cast<double>(column) * grid.Spacing;
            const double x1 = relativeFirstX + static_cast<double>(column + 1u) * grid.Spacing;
            const double z0 = relativeFirstZ + static_cast<double>(row) * grid.Spacing;
            const double z1 = relativeFirstZ + static_cast<double>(row + 1u) * grid.Spacing;
            const Math::Vector3 v00(static_cast<float>(x0), grid.At(column, row) - ray.Origin.y, static_cast<float>(z0));
            const Math::Vector3 v10(static_cast<float>(x1), grid.At(column + 1u, row) - ray.Origin.y,
                static_cast<float>(z0));
            const Math::Vector3 v01(static_cast<float>(x0), grid.At(column, row + 1u) - ray.Origin.y,
                static_cast<float>(z1));
            const Math::Vector3 v11(static_cast<float>(x1), grid.At(column + 1u, row + 1u) - ray.Origin.y,
                static_cast<float>(z1));
            const auto testTriangle = [&](const Math::Vector3& a, const Math::Vector3& b, const Math::Vector3& c)
            {
                float distance = 0.0f;
                if (!Math::RayIntersectsTriangle(localRay, a, b, c, distance) || !std::isfinite(distance)
                    || distance > maxDistance || (bHit && distance >= closest.Distance))
                {
                    return;
                }
                const Math::Vector3 edge1 = b - a;
                const Math::Vector3 edge2 = c - a;
                double nx = static_cast<double>(edge1.y) * edge2.z - static_cast<double>(edge1.z) * edge2.y;
                double ny = static_cast<double>(edge1.z) * edge2.x - static_cast<double>(edge1.x) * edge2.z;
                double nz = static_cast<double>(edge1.x) * edge2.y - static_cast<double>(edge1.y) * edge2.x;
                const double normalLength = std::hypot(nx, ny, nz);
                const double divisor = ny < 0.0 ? -normalLength : normalLength;
                nx /= divisor;
                ny /= divisor;
                nz /= divisor;
                closest.Distance = distance;
                closest.Point = ray.Origin + direction * distance;
                closest.Normal = Math::Vector3(static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz));
                closest.Column = column;
                closest.Row = row;
                bHit = true;
            };
            if (UsesMainDiagonal(column, row))
            {
                testTriangle(v00, v10, v11);
                testTriangle(v00, v11, v01);
            }
            else
            {
                testTriangle(v00, v10, v01);
                testTriangle(v10, v11, v01);
            }
            return bHit && closest.Distance <= intervalEnd;
        };

        // 微小方向でも横移動が無視できない長さなら、通常の DDA で境をたどる。
        if (bSingleCell)
        {
            testCell(std::min(static_cast<uint32_t>(originX), grid.Columns - 2u),
                std::min(static_cast<uint32_t>(originZ), grid.Rows - 2u), begin, end);
        }
        else
        {
            const auto testBlock = [&](uint32_t column, uint32_t row, double intervalBegin, double intervalEnd)
            {
                const size_t index = static_cast<size_t>(row) * grid.BlockColumns + column;
                const double y0 = ray.Origin.y + static_cast<double>(direction.y) * intervalBegin;
                const double y1 = ray.Origin.y + static_cast<double>(direction.y) * intervalEnd;
                // 交差判定の float の丸めでブロックの端の接触を捨てないよう、除外側だけを広げる。
                const double tolerance = 8.0 * std::numeric_limits<float>::epsilon()
                    * std::max({1.0, std::abs(static_cast<double>(ray.Origin.y)), std::abs(y0), std::abs(y1),
                        std::abs(static_cast<double>(grid.BlockMinHeight[index])),
                        std::abs(static_cast<double>(grid.BlockMaxHeight[index]))});
                if (std::max(y0, y1) + tolerance < grid.BlockMinHeight[index]
                    || std::min(y0, y1) - tolerance > grid.BlockMaxHeight[index])
                {
                    return bHit && closest.Distance <= intervalEnd;
                }
                const uint32_t firstColumn = column * GridData::kBlockCells;
                const uint32_t firstRow = row * GridData::kBlockCells;
                return WalkDda(originX, originZ, directionX, directionZ, 1.0,
                    firstColumn, std::min(firstColumn + GridData::kBlockCells - 1u, grid.Columns - 2u),
                    firstRow, std::min(firstRow + GridData::kBlockCells - 1u, grid.Rows - 2u),
                    intervalBegin, intervalEnd, testCell);
            };
            WalkDda(originX, originZ, directionX, directionZ, GridData::kBlockCells,
                0u, grid.BlockColumns - 1u, 0u, grid.BlockRows - 1u, begin, end, testBlock);
        }
        if (bHit)
        {
            outHit = closest;
        }
        return bHit;
    }
} // namespace NorvesLib::Core::Rendering::Terrain
