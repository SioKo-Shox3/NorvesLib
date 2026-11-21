#include "AI/Navigation/NavGridBaker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        using Core::Container::VariableArray;

        // 量子化の上限に半セルを足し、分母を 2 倍にそろえて整数セルに切り上げる。
        constexpr uint32_t kMinClearanceApronCells = []
        {
            constexpr uint32_t numerator = 2u * 255u + kNavClearanceStepsPerCell;
            constexpr uint32_t denominator = 2u * kNavClearanceStepsPerCell;
            return (numerator + denominator - 1u) / denominator;
        }();

        struct TerrainCell
        {
            float Height;
            float WaterDepth;
            uint8_t Grass;
            uint8_t Flags;
            uint8_t Slope;
        };

        bool IsNonnegativeFinite(float value)
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        // ワールドのセル座標は、タイル座標と一辺の積を取る前に 64 ビットに広げる。
        bool BuildAxisCenters(VariableArray<double>& centers, float origin, float cellSize,
                              int32_t tileCoord, uint32_t tileCells, uint32_t apron)
        {
            const int64_t first = static_cast<int64_t>(tileCoord) * tileCells - apron;
            for (size_t i = 0; i < centers.size(); ++i)
            {
                centers[i] = origin + (static_cast<double>(first) + static_cast<double>(i) + 0.5) * cellSize;
            }
            const double limit = (std::numeric_limits<float>::max)();
            return centers.front() >= -limit && centers.back() <= limit;
        }

        void ClassifyTerrain(VariableArray<TerrainCell>& cells, uint32_t width,
                             double cellSize, const NavBakeSettings& settings)
        {
            for (uint32_t z = 0; z < width; ++z)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const uint32_t i = z * width + x;
                    TerrainCell& cell = cells[i];
                    if (std::isfinite(cell.Height))
                    {
                        // 中心差分を取り、領域の端や非有限の隣では利用できる側の高さを使う。
                        const auto neighbor = [&](bool bInside, uint32_t adjacent)
                        {
                            return bInside && std::isfinite(cells[adjacent].Height) ? adjacent : i;
                        };
                        const uint32_t left = neighbor(x > 0, i - 1);
                        const uint32_t right = neighbor(x + 1 < width, i + 1);
                        const uint32_t top = neighbor(z > 0, i - width);
                        const uint32_t bottom = neighbor(z + 1 < width, i + width);
                        const auto gradient = [&](uint32_t low, uint32_t high)
                        {
                            const int steps = (low != i) + (high != i);
                            const double rise = static_cast<double>(cells[high].Height) - cells[low].Height;
                            return steps == 0 ? 0.0 : rise / (steps * cellSize);
                        };
                        const double gx = gradient(left, right);
                        const double gz = gradient(top, bottom);
                        const double degrees = std::atan(std::sqrt(gx * gx + gz * gz)) * (180.0 / std::numbers::pi);
                        cell.Slope = QuantizeNavSlopeDegrees(static_cast<float>(degrees));
                        if (degrees > settings.MaxWalkableSlopeDegrees)
                        {
                            cell.Flags |= static_cast<uint8_t>(ENavCellFlags::SteepSlope);
                        }

                        // 4 近傍のうち一つでも段差の上限を超えれば崖にする。
                        for (uint32_t adjacent : {left, right, top, bottom})
                        {
                            if (std::abs(static_cast<double>(cell.Height) - cells[adjacent].Height) > settings.CliffStepHeight)
                            {
                                cell.Flags |= static_cast<uint8_t>(ENavCellFlags::Cliff);
                                break;
                            }
                        }
                    }

                    // 深水を先に判定し、浅瀬と深水のフラグを排他的に付ける。
                    if (cell.WaterDepth >= settings.DeepWaterDepth)
                    {
                        cell.Flags |= static_cast<uint8_t>(ENavCellFlags::WaterDeep);
                    }
                    else if (cell.WaterDepth >= settings.ShallowWaterDepth)
                    {
                        cell.Flags |= static_cast<uint8_t>(ENavCellFlags::WaterShallow);
                    }
                }
            }
        }

        // 足跡の外接矩形を半セル広げて候補を絞り、セルの正方形と箱が 4 軸すべてで重なるかを調べる。
        void MarkObstacle(const NavObstacleFootprint& box, const VariableArray<double>& centersX,
                          const VariableArray<double>& centersZ, double halfCell, VariableArray<TerrainCell>& cells)
        {
            if (!std::isfinite(box.CenterX) || !std::isfinite(box.CenterZ) || !std::isfinite(box.YawRadians) ||
                !IsNonnegativeFinite(box.HalfExtentX) || !IsNonnegativeFinite(box.HalfExtentZ))
            {
                return;
            }
            const double cosine = std::cos(static_cast<double>(box.YawRadians));
            const double sine = std::sin(static_cast<double>(box.YawRadians));
            const double absCos = std::abs(cosine);
            const double absSin = std::abs(sine);
            const double extentX = absCos * box.HalfExtentX + absSin * box.HalfExtentZ + halfCell;
            const double extentZ = absSin * box.HalfExtentX + absCos * box.HalfExtentZ + halfCell;
            const double cellProjection = halfCell * (absCos + absSin);
            const auto firstX = std::lower_bound(centersX.begin(), centersX.end(), box.CenterX - extentX);
            const auto endX = std::upper_bound(firstX, centersX.end(), box.CenterX + extentX);
            const auto firstZ = std::lower_bound(centersZ.begin(), centersZ.end(), box.CenterZ - extentZ);
            const auto endZ = std::upper_bound(firstZ, centersZ.end(), box.CenterZ + extentZ);
            for (auto z = firstZ; z != endZ; ++z)
            {
                const size_t row = (z - centersZ.begin()) * centersX.size();
                const double dz = *z - box.CenterZ;
                for (auto x = firstX; x != endX; ++x)
                {
                    const double dx = *x - box.CenterX;
                    const double localX = dx * cosine - dz * sine;
                    const double localZ = dx * sine + dz * cosine;
                    if (std::abs(dx) <= extentX && std::abs(dz) <= extentZ &&
                        std::abs(localX) <= box.HalfExtentX + cellProjection &&
                        std::abs(localZ) <= box.HalfExtentZ + cellProjection)
                    {
                        cells[row + (x - centersX.begin())].Flags |= static_cast<uint8_t>(ENavCellFlags::StaticBlocked);
                    }
                }
            }
        }

        struct Parabola
        {
            int Site;  // 放物線の中心
            int Base;  // 中心での二乗距離
            int Start; // この放物線が最小になる最初の整数座標
        };

        // 有限の種から放物線の下包絡を作り、各整数座標での最小値を元の行または列に戻す。
        void TransformLine(int* line, int width, int stride, VariableArray<Parabola>& envelope)
        {
            envelope.clear();
            for (int site = 0; site < width; ++site)
            {
                const int base = line[site * stride];
                if (base < 0)
                {
                    continue;
                }
                int start = 0;
                while (!envelope.empty())
                {
                    const Parabola& last = envelope.back();
                    const int numerator = base + site * site - last.Base - last.Site * last.Site;
                    const int denominator = 2 * (site - last.Site);
                    // 交点を整数へ切り上げる。分母は正で、負の商は整数除算で既に切り上がる。
                    start = numerator / denominator + (numerator % denominator > 0);
                    if (start > last.Start)
                    {
                        break;
                    }
                    envelope.pop_back();
                }
                envelope.push_back({site, base, envelope.empty() ? 0 : start});
            }
            if (envelope.empty())
            {
                return;
            }
            size_t active = 0;
            for (int x = 0; x < width; ++x)
            {
                if (active + 1 < envelope.size() && envelope[active + 1].Start <= x)
                {
                    ++active;
                }
                const int offset = x - envelope[active].Site;
                line[x * stride] = envelope[active].Base + offset * offset;
            }
        }

        bool LabelRegions(NavTile& tile)
        {
            VariableArray<uint32_t> stack;
            const auto enqueue = [&](uint32_t i)
            {
                if (tile.Region[i] == 0 && (tile.Flags[i] & kNavBlockingFlags) == 0)
                {
                    tile.Region[i] = tile.RegionCount;
                    stack.push_back(i);
                }
            };
            const uint32_t width = tile.TileCells;
            const uint32_t count = tile.GetCellCount();
            for (uint32_t seed = 0; seed < count; ++seed)
            {
                if (tile.Region[seed] != 0 || (tile.Flags[seed] & kNavBlockingFlags) != 0)
                {
                    continue;
                }
                if (tile.RegionCount == (std::numeric_limits<uint16_t>::max)())
                {
                    return false;
                }
                ++tile.RegionCount;
                enqueue(seed);
                while (!stack.empty())
                {
                    const uint32_t i = stack.back();
                    stack.pop_back();
                    const uint32_t x = i % width;
                    if (x > 0)
                    {
                        enqueue(i - 1);
                    }
                    if (x + 1 < width)
                    {
                        enqueue(i + 1);
                    }
                    if (i >= width)
                    {
                        enqueue(i - width);
                    }
                    if (i + width < count)
                    {
                        enqueue(i + width);
                    }
                }
            }
            return true;
        }
    } // namespace

    bool NavGridBaker::Bake(const NavBakeInput& input, NavTile& outTile)
    {
        const NavGridDesc& grid = input.Grid;
        const NavBakeSettings& settings = input.Settings;
        if (!grid.IsValid() || !IsNonnegativeFinite(settings.MaxWalkableSlopeDegrees) ||
            !IsNonnegativeFinite(settings.CliffStepHeight) || !IsNonnegativeFinite(settings.ShallowWaterDepth) ||
            !IsNonnegativeFinite(settings.DeepWaterDepth))
        {
            return false;
        }

        // タイルの四方に余白を足し、各軸のセル中心をワールド座標で求める。
        const uint32_t apron = std::clamp(settings.ClearanceApronCells, kMinClearanceApronCells, 256u);
        const uint32_t width = grid.TileCells + 2 * apron;
        VariableArray<double> centersX(width), centersZ(width);
        if (!BuildAxisCenters(centersX, grid.OriginX, grid.CellSize, input.Tile.X, grid.TileCells, apron) ||
            !BuildAxisCenters(centersZ, grid.OriginZ, grid.CellSize, input.Tile.Z, grid.TileCells, apron))
        {
            return false;
        }

        // 高さ、水深、草を各中心で一度だけ取得し、非有限の高さは通行不能にする。
        const FlatNavTerrainSource flat;
        const INavTerrainSource& terrain = input.Terrain ? *input.Terrain : flat;
        VariableArray<TerrainCell> cells(width * width);
        for (uint32_t z = 0; z < width; ++z)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const float wx = static_cast<float>(centersX[x]);
                const float wz = static_cast<float>(centersZ[z]);
                TerrainCell& cell = cells[z * width + x];
                cell.Height = terrain.SampleHeight(wx, wz);
                cell.WaterDepth = terrain.SampleWaterDepth(wx, wz);
                cell.Grass = terrain.SampleGrassDensity(wx, wz);
                if (!std::isfinite(cell.Height))
                {
                    cell.Flags |= static_cast<uint8_t>(ENavCellFlags::StaticBlocked);
                }
            }
        }
        ClassifyTerrain(cells, width, grid.CellSize, settings);

        const double halfCell = grid.CellSize * 0.5;
        for (const NavObstacleFootprint& box : input.StaticObstacles)
        {
            MarkObstacle(box, centersX, centersZ, halfCell, cells);
        }

        // 通行不能セルからの二乗距離を、行と列の順に厳密な距離変換で求める。負値は無限。
        VariableArray<int> squaredDistances(cells.size());
        for (size_t i = 0; i < cells.size(); ++i)
        {
            squaredDistances[i] = (cells[i].Flags & kNavBlockingFlags) != 0 ? 0 : -1;
        }
        const int lineSize = static_cast<int>(width);
        VariableArray<Parabola> envelope;
        envelope.reserve(width);
        for (int z = 0; z < lineSize; ++z)
        {
            TransformLine(squaredDistances.data() + z * lineSize, lineSize, 1, envelope);
        }
        for (int x = 0; x < lineSize; ++x)
        {
            TransformLine(squaredDistances.data() + x, lineSize, lineSize, envelope);
        }

        // 余白を除くセルの属性を写し、中心間距離から半セル引いてクリアランスにする。
        // 量子化の上限までの遮蔽は余白に含まれるため、計算範囲に種がなければ 255 になる。
        NavTile tile;
        tile.Reset(input.Tile, grid);
        for (uint32_t z = 0; z < grid.TileCells; ++z)
        {
            for (uint32_t x = 0; x < grid.TileCells; ++x)
            {
                const uint32_t source = (z + apron) * width + x + apron;
                const uint32_t dest = tile.LocalIndex(x, z);
                const TerrainCell& cell = cells[source];
                tile.Height[dest] = cell.Height;
                tile.Flags[dest] = cell.Flags;
                tile.SlopeQ[dest] = cell.Slope;
                tile.GrassQ[dest] = cell.Grass;
                const int distance = squaredDistances[source];
                tile.ClearanceQ[dest] = distance < 0 ? 255 :
                    QuantizeNavClearance(static_cast<float>(std::sqrt(static_cast<double>(distance)) - 0.5));
            }
        }

        // タイル内の通行可能セルを行優先でたどり、4 連結成分ごとに番号を振る。
        if (!LabelRegions(tile))
        {
            return false;
        }
        outTile = std::move(tile);
        return true;
    }
} // namespace NorvesLib::Modules::AI
