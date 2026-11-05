#include "Rendering/Terrain/TerrainHeightField.h"

#include "Math/GeometryIntersection.h"
#include "TerrainHeightFieldGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

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
            const GridData::LodLevel& level = grid.Lods[GridData::kLodCount - 1u];
            const auto testBlock = [&](uint32_t column, uint32_t row, double intervalBegin, double intervalEnd)
            {
                const size_t index = static_cast<size_t>(row) * level.CellColumns + column;
                const double y0 = ray.Origin.y + static_cast<double>(direction.y) * intervalBegin;
                const double y1 = ray.Origin.y + static_cast<double>(direction.y) * intervalEnd;
                // 交差判定の float の丸めでブロックの端の接触を捨てないよう、除外側だけを広げる。
                const double tolerance = 8.0 * std::numeric_limits<float>::epsilon()
                    * std::max({1.0, std::abs(static_cast<double>(ray.Origin.y)), std::abs(y0), std::abs(y1),
                        std::abs(static_cast<double>(level.MinHeight[index])),
                        std::abs(static_cast<double>(level.MaxHeight[index]))});
                if (std::max(y0, y1) + tolerance < level.MinHeight[index]
                    || std::min(y0, y1) - tolerance > level.MaxHeight[index])
                {
                    return bHit && closest.Distance <= intervalEnd;
                }
                const uint32_t firstColumn = column * level.Stride;
                const uint32_t firstRow = row * level.Stride;
                return WalkDda(originX, originZ, directionX, directionZ, 1.0,
                    firstColumn, std::min(firstColumn + level.Stride - 1u, grid.Columns - 2u),
                    firstRow, std::min(firstRow + level.Stride - 1u, grid.Rows - 2u),
                    intervalBegin, intervalEnd, testCell);
            };
            WalkDda(originX, originZ, directionX, directionZ, level.Stride,
                0u, level.CellColumns - 1u, 0u, level.CellRows - 1u, begin, end, testBlock);
        }
        if (bHit)
        {
            outHit = closest;
        }
        return bHit;
    }
    namespace
    {
        struct QueryBounds
        {
            double MinX;
            double MaxX;
            double MinY;
            double MinZ;
            double MaxZ;
        };

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsValid(const Math::Sphere& sphere)
        {
            return IsFinite(sphere.Center) && std::isfinite(sphere.Radius) && sphere.Radius >= 0.0f;
        }

        bool IsValid(const Math::Capsule& capsule)
        {
            return IsFinite(capsule.PointA) && IsFinite(capsule.PointB)
                && std::isfinite(capsule.Radius) && capsule.Radius >= 0.0f;
        }

        bool IsValid(const Math::OBB& box)
        {
            return IsFinite(box.Center) && IsFinite(box.HalfExtents)
                && box.HalfExtents.x >= 0.0f && box.HalfExtents.y >= 0.0f && box.HalfExtents.z >= 0.0f
                && IsFinite(box.Axes[0]) && IsFinite(box.Axes[1]) && IsFinite(box.Axes[2]);
        }

        QueryBounds GetBounds(const Math::Sphere& sphere)
        {
            return {static_cast<double>(sphere.Center.x) - sphere.Radius,
                static_cast<double>(sphere.Center.x) + sphere.Radius,
                static_cast<double>(sphere.Center.y) - sphere.Radius,
                static_cast<double>(sphere.Center.z) - sphere.Radius,
                static_cast<double>(sphere.Center.z) + sphere.Radius};
        }

        QueryBounds GetBounds(const Math::Capsule& capsule)
        {
            return {static_cast<double>(std::min(capsule.PointA.x, capsule.PointB.x)) - capsule.Radius,
                static_cast<double>(std::max(capsule.PointA.x, capsule.PointB.x)) + capsule.Radius,
                static_cast<double>(std::min(capsule.PointA.y, capsule.PointB.y)) - capsule.Radius,
                static_cast<double>(std::min(capsule.PointA.z, capsule.PointB.z)) - capsule.Radius,
                static_cast<double>(std::max(capsule.PointA.z, capsule.PointB.z)) + capsule.Radius};
        }

        QueryBounds GetBounds(const Math::OBB& box)
        {
            const double half[3] = {box.HalfExtents.x, box.HalfExtents.y, box.HalfExtents.z};
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            for (int axis = 0; axis < 3; ++axis)
            {
                x += std::abs(static_cast<double>(box.Axes[axis].x)) * half[axis];
                y += std::abs(static_cast<double>(box.Axes[axis].y)) * half[axis];
                z += std::abs(static_cast<double>(box.Axes[axis].z)) * half[axis];
            }
            return {box.Center.x - x, box.Center.x + x, box.Center.y - y, box.Center.z - z, box.Center.z + z};
        }

        Math::Vector3 QueryOrigin(const Math::Sphere& sphere)
        {
            return sphere.Center;
        }

        Math::Vector3 QueryOrigin(const Math::Capsule& capsule)
        {
            return capsule.PointA;
        }

        Math::Vector3 QueryOrigin(const Math::OBB& box)
        {
            return box.Center;
        }

        Math::Sphere LocalShape(const Math::Sphere& sphere, const Math::Vector3& origin)
        {
            return Math::Sphere(sphere.Center - origin, sphere.Radius);
        }

        Math::Capsule LocalShape(const Math::Capsule& capsule, const Math::Vector3& origin)
        {
            return Math::Capsule(capsule.PointA - origin, capsule.PointB - origin, capsule.Radius);
        }

        Math::OBB LocalShape(const Math::OBB& box, const Math::Vector3& origin)
        {
            Math::OBB local = box;
            local.Center -= origin;
            return local;
        }

        Math::Vector3 FaceNormal(const Math::Triangle& triangle)
        {
            const Math::Vector3 a = triangle.V1 - triangle.V0;
            const Math::Vector3 b = triangle.V2 - triangle.V0;
            double x = static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y;
            double y = static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z;
            double z = static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x;
            const double length = std::hypot(x, y, z);
            if (!(length > 0.0))
            {
                return Math::Vector3(0.0f, 1.0f, 0.0f);
            }
            const double sign = y < 0.0 ? -1.0 : 1.0;
            return Math::Vector3(static_cast<float>(sign * x / length),
                std::max(static_cast<float>(sign * y / length), std::numeric_limits<float>::denorm_min()),
                static_cast<float>(sign * z / length));
        }

        double SignedDistance(const Math::Vector3& point, const Math::Triangle& triangle, const Math::Vector3& normal)
        {
            return (static_cast<double>(point.x) - triangle.V0.x) * normal.x
                + (static_cast<double>(point.y) - triangle.V0.y) * normal.y
                + (static_cast<double>(point.z) - triangle.V0.z) * normal.z;
        }

        double CrossXZ(const Math::Vector3& a, const Math::Vector3& b, const Math::Vector3& point)
        {
            return (static_cast<double>(b.x) - a.x) * (static_cast<double>(point.z) - a.z)
                - (static_cast<double>(b.z) - a.z) * (static_cast<double>(point.x) - a.x);
        }

        bool ContainsXZ(const Math::Triangle& triangle, const Math::Vector3& point)
        {
            const double a = CrossXZ(triangle.V0, triangle.V1, point);
            const double b = CrossXZ(triangle.V1, triangle.V2, point);
            const double c = CrossXZ(triangle.V2, triangle.V0, point);
            return (a >= 0.0 && b >= 0.0 && c >= 0.0) || (a <= 0.0 && b <= 0.0 && c <= 0.0);
        }

        Math::Vector3 GroundReference(const Math::Sphere& sphere, const Math::Triangle&, const Math::Vector3&,
            float& outExtent)
        {
            outExtent = sphere.Radius;
            return sphere.Center;
        }

        Math::Vector3 GroundReference(const Math::Capsule& capsule, const Math::Triangle& triangle,
            const Math::Vector3& normal, float& outExtent)
        {
            outExtent = capsule.Radius;
            return SignedDistance(capsule.PointB, triangle, normal) < SignedDistance(capsule.PointA, triangle, normal)
                ? capsule.PointB : capsule.PointA;
        }

        template<typename Grid, typename Visitor>
        void VisitTriangles(const Grid& grid, const QueryBounds& bounds, const Math::Vector3& origin, Visitor&& visitor)
        {
            const double minX = (bounds.MinX - grid.FirstX) / grid.Spacing;
            const double maxX = (bounds.MaxX - grid.FirstX) / grid.Spacing;
            const double minZ = (bounds.MinZ - grid.FirstZ) / grid.Spacing;
            const double maxZ = (bounds.MaxZ - grid.FirstZ) / grid.Spacing;
            if (maxX < 0.0 || maxZ < 0.0 || minX > grid.Columns - 1u || minZ > grid.Rows - 1u)
            {
                return;
            }
            // 境界に接する升目を両側とも含め、外周では最後の升目へ収める。
            const uint32_t firstColumn = static_cast<uint32_t>(std::max(0.0, std::ceil(minX) - 1.0));
            const uint32_t lastColumn = static_cast<uint32_t>(std::min(static_cast<double>(grid.Columns - 2u), std::floor(maxX)));
            const uint32_t firstRow = static_cast<uint32_t>(std::max(0.0, std::ceil(minZ) - 1.0));
            const uint32_t lastRow = static_cast<uint32_t>(std::min(static_cast<double>(grid.Rows - 2u), std::floor(maxZ)));

            const auto& level = grid.Lods[Grid::kLodCount - 1u];
            for (uint32_t bz = firstRow / level.Stride; bz <= lastRow / level.Stride; ++bz)
            {
                for (uint32_t bx = firstColumn / level.Stride; bx <= lastColumn / level.Stride; ++bx)
                {
                    const size_t block = static_cast<size_t>(bz) * level.CellColumns + bx;
                    // 地中を除外しないよう、地表より完全に上にあるブロックだけを捨てる。
                    if (bounds.MinY > level.MaxHeight[block])
                    {
                        continue;
                    }
                    const uint32_t beginRow = std::max(firstRow, bz * level.Stride);
                    const uint32_t endRow = std::min(lastRow, bz * level.Stride + level.Stride - 1u);
                    const uint32_t beginColumn = std::max(firstColumn, bx * level.Stride);
                    const uint32_t endColumn = std::min(lastColumn, bx * level.Stride + level.Stride - 1u);
                    for (uint32_t row = beginRow; row <= endRow; ++row)
                    {
                        for (uint32_t column = beginColumn; column <= endColumn; ++column)
                        {
                            // 共有頂点は相対座標を倍精度で作ってから float にし、隣の升目との隙間を避ける。
                            const float x0 = static_cast<float>((grid.FirstX - origin.x) + column * grid.Spacing);
                            const float x1 = static_cast<float>((grid.FirstX - origin.x) + (column + 1u) * grid.Spacing);
                            const float z0 = static_cast<float>((grid.FirstZ - origin.z) + row * grid.Spacing);
                            const float z1 = static_cast<float>((grid.FirstZ - origin.z) + (row + 1u) * grid.Spacing);
                            const Math::Vector3 v00(x0, static_cast<float>(static_cast<double>(grid.At(column, row)) - origin.y), z0);
                            const Math::Vector3 v10(x1, static_cast<float>(static_cast<double>(grid.At(column + 1u, row)) - origin.y), z0);
                            const Math::Vector3 v01(x0, static_cast<float>(static_cast<double>(grid.At(column, row + 1u)) - origin.y), z1);
                            const Math::Vector3 v11(x1, static_cast<float>(static_cast<double>(grid.At(column + 1u, row + 1u)) - origin.y), z1);
                            // 巻き順も上向きにし、最近点が一致した場合の Math の面法線を揃える。
                            if (UsesMainDiagonal(column, row))
                            {
                                visitor(Math::Triangle(v00, v11, v10));
                                visitor(Math::Triangle(v00, v01, v11));
                            }
                            else
                            {
                                visitor(Math::Triangle(v00, v01, v10));
                                visitor(Math::Triangle(v10, v01, v11));
                            }
                        }
                    }
                }
            }
        }

        void StabilizeNormal(const TerrainHeightField&, const Math::Sphere&, const Math::Vector3&,
            Math::GeometryContact&)
        {
        }

        void StabilizeNormal(const TerrainHeightField&, const Math::Capsule&, const Math::Vector3&,
            Math::GeometryContact&)
        {
        }

        struct BoxPrismPoint
        {
            double X;
            double Y;
            double Z;
        };

        double CrossXZ(const BoxPrismPoint& a, const BoxPrismPoint& b, const BoxPrismPoint& point)
        {
            return (b.X - a.X) * (point.Z - a.Z) - (b.Z - a.Z) * (point.X - a.X);
        }

        bool ComputeBoxPrismContact(const TerrainHeightField& field, const Math::Triangle& triangle,
            const Math::OBB& box, const Math::Vector3& origin, Math::GeometryContact& contact)
        {
            const BoxPrismPoint vertices[3] = {
                {triangle.V0.x, triangle.V0.y, triangle.V0.z},
                {triangle.V1.x, triangle.V1.y, triangle.V1.z},
                {triangle.V2.x, triangle.V2.y, triangle.V2.z}};
            const BoxPrismPoint edge1 = {vertices[1].X - vertices[0].X, vertices[1].Y - vertices[0].Y,
                vertices[1].Z - vertices[0].Z};
            const BoxPrismPoint edge2 = {vertices[2].X - vertices[0].X, vertices[2].Y - vertices[0].Y,
                vertices[2].Z - vertices[0].Z};
            BoxPrismPoint normal = {edge1.Y * edge2.Z - edge1.Z * edge2.Y,
                edge1.Z * edge2.X - edge1.X * edge2.Z, edge1.X * edge2.Y - edge1.Y * edge2.X};
            const double normalLength = std::hypot(normal.X, normal.Y, normal.Z);
            const double area = CrossXZ(vertices[0], vertices[1], vertices[2]);
            if (!(normalLength > 0.0) || area == 0.0)
            {
                return false;
            }
            const double divisor = normal.Y < 0.0 ? -normalLength : normalLength;
            normal.X /= divisor;
            normal.Y /= divisor;
            normal.Z /= divisor;

            const double edgeScale = std::max({std::abs(edge1.X), std::abs(edge1.Z),
                std::abs(edge2.X), std::abs(edge2.Z)});
            const auto containsXZ = [&](const BoxPrismPoint& point, bool& bOnBoundary)
            {
                bOnBoundary = false;
                // 共有辺での丸めを吸収し、格子幅に比べた判定の緩みを小さく保つ。
                const double coordinateScale = std::max({1.0, std::abs(point.X), std::abs(point.Z),
                    std::abs(vertices[0].X), std::abs(vertices[0].Z), std::abs(vertices[1].X),
                    std::abs(vertices[1].Z), std::abs(vertices[2].X), std::abs(vertices[2].Z)});
                const double tolerance = std::abs(area) * 1e-10
                    + 32.0 * std::numeric_limits<double>::epsilon() * coordinateScale * edgeScale;
                for (int edge = 0; edge < 3; ++edge)
                {
                    const double side = CrossXZ(vertices[edge], vertices[(edge + 1) % 3], point);
                    if ((area > 0.0 && side < -tolerance) || (area < 0.0 && side > tolerance))
                    {
                        return false;
                    }
                    bOnBoundary = bOnBoundary || std::abs(side) <= tolerance;
                }
                return true;
            };

            bool bHasCandidate = false;
            bool bBoundary = false;
            double maxPenetration = 0.0;
            BoxPrismPoint deepest = {};
            const auto consider = [&](const BoxPrismPoint& point, bool bOnBoundary)
            {
                const double penetration = (vertices[0].X - point.X) * normal.X
                    + (vertices[0].Y - point.Y) * normal.Y + (vertices[0].Z - point.Z) * normal.Z;
                if (!bHasCandidate || penetration > maxPenetration)
                {
                    bHasCandidate = true;
                    bBoundary = bOnBoundary;
                    maxPenetration = penetration;
                    deepest = point;
                }
            };

            const BoxPrismPoint axes[3] = {{box.Axes[0].x, box.Axes[0].y, box.Axes[0].z},
                {box.Axes[1].x, box.Axes[1].y, box.Axes[1].z},
                {box.Axes[2].x, box.Axes[2].y, box.Axes[2].z}};
            const double half[3] = {box.HalfExtents.x, box.HalfExtents.y, box.HalfExtents.z};
            BoxPrismPoint corners[8];
            for (int index = 0; index < 8; ++index)
            {
                BoxPrismPoint point = {box.Center.x, box.Center.y, box.Center.z};
                for (int axis = 0; axis < 3; ++axis)
                {
                    const double extent = (index & (1 << axis)) ? half[axis] : -half[axis];
                    point.X += axes[axis].X * extent;
                    point.Y += axes[axis].Y * extent;
                    point.Z += axes[axis].Z * extent;
                }
                corners[index] = point;
                bool bOnBoundary = false;
                if (containsXZ(point, bOnBoundary))
                {
                    consider(point, bOnBoundary);
                }
            }

            // 箱の各辺と柱の側面の交点を候補に加える。
            for (int index = 0; index < 8; ++index)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    if ((index & (1 << axis)) != 0)
                    {
                        continue;
                    }
                    const BoxPrismPoint& start = corners[index];
                    const BoxPrismPoint& end = corners[index | (1 << axis)];
                    for (int edge = 0; edge < 3; ++edge)
                    {
                        const double startSide = CrossXZ(vertices[edge], vertices[(edge + 1) % 3], start);
                        const double endSide = CrossXZ(vertices[edge], vertices[(edge + 1) % 3], end);
                        const double difference = startSide - endSide;
                        if (difference == 0.0)
                        {
                            continue;
                        }
                        const double t = startSide / difference;
                        if (t < 0.0 || t > 1.0)
                        {
                            continue;
                        }
                        const BoxPrismPoint point = {start.X + (end.X - start.X) * t,
                            start.Y + (end.Y - start.Y) * t, start.Z + (end.Z - start.Z) * t};
                        bool bOnBoundary = false;
                        if (containsXZ(point, bOnBoundary))
                        {
                            consider(point, true);
                        }
                    }
                }
            }

            // 三角形の各頂点を通る鉛直線を、箱の 3 組のスラブで切る。
            for (const BoxPrismPoint& vertex : vertices)
            {
                double begin = -std::numeric_limits<double>::infinity();
                double end = std::numeric_limits<double>::infinity();
                bool bIntersects = true;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const double offset = (vertex.X - box.Center.x) * axes[axis].X
                        - box.Center.y * axes[axis].Y + (vertex.Z - box.Center.z) * axes[axis].Z;
                    if (!ClipAxis(offset, axes[axis].Y, -half[axis], half[axis], begin, end))
                    {
                        bIntersects = false;
                        break;
                    }
                }
                if (bIntersects)
                {
                    consider({vertex.X, begin, vertex.Z}, true);
                    consider({vertex.X, end, vertex.Z}, true);
                }
            }
            if (!bHasCandidate || maxPenetration < 0.0)
            {
                return false;
            }

            contact.Depth = static_cast<float>(maxPenetration);
            contact.Point = Math::Vector3(static_cast<float>(deepest.X + normal.X * maxPenetration + origin.x),
                static_cast<float>(deepest.Y + normal.Y * maxPenetration + origin.y),
                static_cast<float>(deepest.Z + normal.Z * maxPenetration + origin.z));
            const Math::Vector3 faceNormal(static_cast<float>(normal.X),
                std::max(static_cast<float>(normal.Y), std::numeric_limits<float>::denorm_min()),
                static_cast<float>(normal.Z));
            contact.Normal = faceNormal;
            if (bBoundary)
            {
                const TerrainNormal smooth = field.SampleNormal(static_cast<float>(deepest.X + origin.x),
                    static_cast<float>(deepest.Z + origin.z), TerrainNormalKind::Smooth);
                if (std::isfinite(smooth.X) && std::isfinite(smooth.Y) && std::isfinite(smooth.Z) && smooth.Y > 0.0f)
                {
                    contact.Normal = Math::Vector3(smooth.X, smooth.Y, smooth.Z);
                }
            }
            return true;
        }

        template<typename Grid, typename Shape>
        uint32_t OverlapGrid(const Grid& grid, const TerrainHeightField& field, const Shape& shape,
            Container::VariableArray<Math::GeometryContact>& outContacts)
        {
            if (!IsValid(shape))
            {
                return 0;
            }
            const Math::Vector3 origin = QueryOrigin(shape);
            const Shape local = LocalShape(shape, origin);
            VisitTriangles(grid, GetBounds(shape), origin, [&](const Math::Triangle& triangle)
            {
                Math::GeometryContact contact;
                if constexpr (std::is_same_v<Shape, Math::OBB>)
                {
                    if (!ComputeBoxPrismContact(field, triangle, local, origin, contact))
                    {
                        return;
                    }
                }
                else
                {
                    const Math::Vector3 faceNormal = FaceNormal(triangle);
                    float extent = 0.0f;
                    const Math::Vector3 reference = GroundReference(local, triangle, faceNormal, extent);
                    const double distance = SignedDistance(reference, triangle, faceNormal);
                    if (distance < 0.0 && ContainsXZ(triangle, reference))
                    {
                        contact.Normal = faceNormal;
                        contact.Depth = static_cast<float>(-distance + extent);
                        contact.Point = reference - faceNormal * static_cast<float>(distance) + origin;
                    }
                    else
                    {
                        if (!Math::ComputeContact(triangle, local, contact))
                        {
                            return;
                        }
                        contact.Point += origin;
                        StabilizeNormal(field, local, faceNormal, contact);
                        if (!(contact.Normal.y > 0.0f))
                        {
                            contact.Normal = faceNormal;
                        }
                    }
                }
                outContacts.push_back(contact);
            });
            return static_cast<uint32_t>(outContacts.size());
        }

        Math::GeometrySweepHit SweepTriangle(const Math::Sphere& shape, const Math::Triangle& triangle,
            const Math::Vector3& direction, float maxDistance, const Math::GeometrySweepSettings& settings)
        {
            return Math::SweepSphere(shape, triangle, direction, maxDistance, settings);
        }

        Math::GeometrySweepHit SweepTriangle(const Math::Capsule& shape, const Math::Triangle& triangle,
            const Math::Vector3& direction, float maxDistance, const Math::GeometrySweepSettings& settings)
        {
            return Math::SweepCapsule(shape, triangle, direction, maxDistance, settings);
        }

        template<typename Shape>
        bool SweepSolid(const Shape& shape, const Math::Triangle& triangle, const Math::Vector3& normal,
            const Math::Vector3& direction, float maxDistance, Math::GeometrySweepHit& outHit)
        {
            float extent = 0.0f;
            const Math::Vector3 reference = GroundReference(shape, triangle, normal, extent);
            const Math::Vector3 vertices[3] = {triangle.V0, triangle.V1, triangle.V2};
            const double area = CrossXZ(vertices[0], vertices[1], vertices[2]);
            if (area == 0.0)
            {
                return false;
            }
            const double sign = area > 0.0 ? 1.0 : -1.0;
            double begin = 0.0;
            double end = maxDistance;
            // 地中で格子の外から入る場合も、射影が三角形に入る最初の距離で接触する。
            for (int index = 0; index < 3; ++index)
            {
                const Math::Vector3& a = vertices[index];
                const Math::Vector3& b = vertices[(index + 1) % 3];
                const double value = sign * CrossXZ(a, b, reference);
                const double rate = sign * ((static_cast<double>(b.x) - a.x) * direction.z
                    - (static_cast<double>(b.z) - a.z) * direction.x);
                if (rate == 0.0)
                {
                    if (value < 0.0)
                    {
                        return false;
                    }
                }
                else if (rate > 0.0)
                {
                    begin = std::max(begin, -value / rate);
                }
                else
                {
                    end = std::min(end, -value / rate);
                }
            }
            if (begin > end)
            {
                return false;
            }
            const double distance = SignedDistance(reference, triangle, normal);
            const double rate = static_cast<double>(normal.x) * direction.x
                + static_cast<double>(normal.y) * direction.y + static_cast<double>(normal.z) * direction.z;
            if (distance + rate * begin >= 0.0)
            {
                if (rate >= 0.0)
                {
                    return false;
                }
                begin = std::max(begin, -distance / rate);
                if (begin > end)
                {
                    return false;
                }
            }
            const float hitDistance = static_cast<float>(begin);
            const Math::Vector3 hitReference = reference + direction * hitDistance;
            const double hitPlaneDistance = distance + rate * begin;
            outHit.Result = Math::EGeometrySweepResult::Hit;
            outHit.Distance = hitDistance;
            outHit.Normal = normal;
            outHit.Depth = static_cast<float>(std::max(0.0, -hitPlaneDistance + extent));
            outHit.Point = hitReference - normal * static_cast<float>(hitPlaneDistance);
            return true;
        }

        template<typename Grid, typename Shape>
        bool SweepGrid(const Grid& grid, const TerrainHeightField& field, const Shape& shape,
            const Math::Vector3& direction, float maxDistance, Math::GeometrySweepHit& outHit)
        {
            if (!IsValid(shape) || !IsFinite(direction) || !std::isfinite(maxDistance) || maxDistance < 0.0f)
            {
                return false;
            }
            const double length = std::sqrt(static_cast<double>(direction.x) * direction.x
                + static_cast<double>(direction.y) * direction.y + static_cast<double>(direction.z) * direction.z);
            if (maxDistance > 0.0f && !(length > 0.0))
            {
                return false;
            }
            Container::VariableArray<Math::GeometryContact> contacts;
            if (OverlapGrid(grid, field, shape, contacts) > 0)
            {
                const Math::GeometryContact* deepest = &contacts[0];
                for (const Math::GeometryContact& contact : contacts)
                {
                    if (contact.Depth > deepest->Depth)
                    {
                        deepest = &contact;
                    }
                }
                Math::GeometrySweepHit hit;
                hit.Result = Math::EGeometrySweepResult::Hit;
                hit.Point = deepest->Point;
                hit.Normal = deepest->Normal;
                hit.Depth = deepest->Depth;
                hit.bStartPenetrating = true;
                outHit = hit;
                return true;
            }
            if (maxDistance == 0.0f)
            {
                return false;
            }
            const Math::Vector3 unit(static_cast<float>(direction.x / length), static_cast<float>(direction.y / length),
                static_cast<float>(direction.z / length));
            QueryBounds bounds = GetBounds(shape);
            const double dx = static_cast<double>(unit.x) * maxDistance;
            const double dy = static_cast<double>(unit.y) * maxDistance;
            const double dz = static_cast<double>(unit.z) * maxDistance;
            bounds.MinX += std::min(0.0, dx);
            bounds.MaxX += std::max(0.0, dx);
            bounds.MinY += std::min(0.0, dy);
            bounds.MinZ += std::min(0.0, dz);
            bounds.MaxZ += std::max(0.0, dz);

            const Math::Vector3 origin = QueryOrigin(shape);
            const Shape local = LocalShape(shape, origin);
            Math::GeometrySweepSettings settings;
            settings.MaxIterations = 64;
            Math::GeometrySweepHit closest;
            Math::GeometrySweepHit uncertain;
            bool bHit = false;
            bool bUncertain = false;
            bool bInvalid = false;
            VisitTriangles(grid, bounds, origin, [&](const Math::Triangle& triangle)
            {
                const Math::Vector3 faceNormal = FaceNormal(triangle);
                Math::GeometrySweepHit candidate = SweepTriangle(local, triangle, direction, maxDistance, settings);
                if (candidate.Result == Math::EGeometrySweepResult::InvalidArgument)
                {
                    bInvalid = true;
                }
                else if (candidate.Result == Math::EGeometrySweepResult::Hit
                    || candidate.Result == Math::EGeometrySweepResult::IterationLimit)
                {
                    candidate.Point += origin;
                    if (!(candidate.Normal.y > 0.0f))
                    {
                        candidate.Normal = faceNormal;
                    }
                    if (candidate.Result == Math::EGeometrySweepResult::Hit)
                    {
                        if (!bHit || candidate.Distance < closest.Distance)
                        {
                            closest = candidate;
                            bHit = true;
                        }
                    }
                    else if (!bUncertain || candidate.Distance < uncertain.Distance)
                    {
                        uncertain = candidate;
                        bUncertain = true;
                    }
                }

                Math::GeometrySweepHit solid;
                if (SweepSolid(local, triangle, faceNormal, unit, maxDistance, solid)
                    && (!bHit || solid.Distance < closest.Distance))
                {
                    solid.Point += origin;
                    closest = solid;
                    bHit = true;
                }
            });
            if (bInvalid)
            {
                outHit = Math::GeometrySweepHit{};
                outHit.Result = Math::EGeometrySweepResult::InvalidArgument;
                return false;
            }
            if (bUncertain && (!bHit || uncertain.Distance < closest.Distance))
            {
                outHit = uncertain;
                return false;
            }
            if (bHit)
            {
                outHit = closest;
            }
            return bHit;
        }
    } // namespace

    uint32_t TerrainHeightField::OverlapSphere(const Math::Sphere& sphere,
        Container::VariableArray<Math::GeometryContact>& outContacts) const
    {
        outContacts.clear();
        return m_Grid ? OverlapGrid(*m_Grid, *this, sphere, outContacts) : 0u;
    }

    uint32_t TerrainHeightField::OverlapCapsule(const Math::Capsule& capsule,
        Container::VariableArray<Math::GeometryContact>& outContacts) const
    {
        outContacts.clear();
        return m_Grid ? OverlapGrid(*m_Grid, *this, capsule, outContacts) : 0u;
    }

    uint32_t TerrainHeightField::OverlapBox(const Math::OBB& box,
        Container::VariableArray<Math::GeometryContact>& outContacts) const
    {
        outContacts.clear();
        return m_Grid ? OverlapGrid(*m_Grid, *this, box, outContacts) : 0u;
    }

    bool TerrainHeightField::SweepSphere(const Math::Sphere& sphere, const Math::Vector3& direction,
        float maxDistance, Math::GeometrySweepHit& outHit) const
    {
        return m_Grid && SweepGrid(*m_Grid, *this, sphere, direction, maxDistance, outHit);
    }

    bool TerrainHeightField::SweepCapsule(const Math::Capsule& capsule, const Math::Vector3& direction,
        float maxDistance, Math::GeometrySweepHit& outHit) const
    {
        return m_Grid && SweepGrid(*m_Grid, *this, capsule, direction, maxDistance, outHit);
    }
} // namespace NorvesLib::Core::Rendering::Terrain
