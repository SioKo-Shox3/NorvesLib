// NVTRN から作る高さ場が、解析面の量子化誤差内に収まり、描画用 LOD0 三角形の高さと一致し、有限の単位法線を返す契約を反証する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedTerrainFormat.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Tools/AssetCook/MeshCooker.h"
#include "Tools/AssetCook/TerrainCooker.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace Asset = NorvesLib::Core::Asset;
namespace Container = NorvesLib::Core::Container;
namespace Terrain = NorvesLib::Core::Rendering::Terrain;
namespace Cook = NorvesLib::Tools::AssetCook;

namespace
{
    constexpr uint32_t TileCells = 8;
    constexpr uint32_t Width = TileCells * 2 + 1;
    constexpr uint32_t Height = TileCells * 3 + 1;
    constexpr float CellSize = 1.75f;
    constexpr uint32_t PointCount = 10000;
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << "\n";
        }
    }

    struct MaximumError
    {
        double Value = 0.0;
        float X = 0.0f;
        float Z = 0.0f;

        void Observe(double error, float x, float z)
        {
            if (!std::isfinite(error))
            {
                error = std::numeric_limits<double>::infinity();
            }
            if (error > Value)
            {
                Value = error;
                X = x;
                Z = z;
            }
        }

        void Print(const char* label) const
        {
            std::cout << label << " 最大差=" << Value << " (x=" << X << ", z=" << Z << ")\n";
        }
    };

    struct Point
    {
        float X;
        float Z;
    };

    struct Triangle
    {
        Asset::CookedMeshFloat3 A;
        Asset::CookedMeshFloat3 B;
        Asset::CookedMeshFloat3 C;
    };

    double AnalyticHeight(double x, double z)
    {
        return 0.17 * x - 0.09 * z + 2.3 * std::sin(0.37 * x) * std::cos(0.37 * z);
    }

    bool BuildTerrain(Cook::TerrainBuildResult& result)
    {
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        for (uint32_t z = 0; z < Height; ++z)
        {
            for (uint32_t x = 0; x < Width; ++x)
            {
                const double height = AnalyticHeight(x * CellSize, z * CellSize);
                minimum = height < minimum ? height : minimum;
                maximum = height > maximum ? height : maximum;
            }
        }

        Container::VariableArray<uint8_t> raw(Width * Height * 2);
        for (uint32_t z = 0; z < Height; ++z)
        {
            for (uint32_t x = 0; x < Width; ++x)
            {
                const auto value = static_cast<uint16_t>(std::round(
                    (AnalyticHeight(x * CellSize, z * CellSize) - minimum) / (maximum - minimum) * 65535.0));
                const size_t offset = (z * Width + x) * 2;
                raw[offset] = static_cast<uint8_t>(value & 255);
                raw[offset + 1] = static_cast<uint8_t>(value >> 8);
            }
        }

        Cook::HeightImportDesc desc;
        desc.Format = Cook::HeightSourceFormat::Raw16;
        desc.RawWidth = Width;
        desc.RawHeight = Height;
        desc.Scale = (maximum - minimum) / 65535.0;
        desc.Offset = minimum;
        desc.TileCells = TileCells;
        desc.CellSizeM = CellSize;
        Cook::HeightImportResult imported;
        Container::AnsiString error;
        if (!Cook::ImportHeightfield(Container::Span<const uint8_t>(raw), desc, imported, error))
        {
            std::cout << "失敗: RAW16 の読み込み: " << error.c_str() << "\n";
            return false;
        }
        Cook::TerrainBuildSettings settings;
        settings.CellSizeM = CellSize;
        settings.TileCells = TileCells;
        if (!Cook::BuildCookedTerrain(imported.Grid, settings, result, error))
        {
            std::cout << "失敗: NVTRN の焼き込み: " << error.c_str() << "\n";
            return false;
        }
        return true;
    }

    void TestGridVertices(const Terrain::TerrainHeightField& field, const Asset::CookedTerrainData& terrain)
    {
        MaximumError maximum;
        for (uint32_t z = 0; z < Height; ++z)
        {
            for (uint32_t x = 0; x < Width; ++x)
            {
                const float px = x * CellSize;
                const float pz = z * CellSize;
                maximum.Observe(std::fabs(static_cast<double>(field.SampleHeight(px, pz)) - AnalyticHeight(px, pz)), px, pz);
            }
        }
        const double tolerance = (static_cast<double>(terrain.HeightMaxM) - terrain.HeightMinM) / 65535.0 + 1e-3;
        maximum.Print("(a) 格子の全頂点と解析面");
        std::cout << "標本数=" << Width * Height << " 許容差=" << tolerance << "\n";
        Expect(maximum.Value <= tolerance, "格子の高さが量子化の幅＋1e-3 以内に収まる（最大差と座標は上記）");
    }

    bool CollectTriangles(const Cook::TerrainBuildResult& terrain, Container::VariableArray<Triangle>& triangles)
    {
        for (uint32_t z = 0; z < terrain.TileCountZ; ++z)
        {
            for (uint32_t x = 0; x < terrain.TileCountX; ++x)
            {
                Cook::MeshCookResult cooked;
                Container::AnsiString error;
                if (!Cook::CookTerrainNvtrnTileToNvmesh(terrain.NvtrnBytes.data(), terrain.NvtrnBytes.size(),
                    "terrain-height-field-test.nvtrn", x, z, "nvmesh.v1.mesh3d.pnt.u32.lodgraph", "terrain-height-field-test.nvmesh", cooked, error))
                {
                    std::cout << "失敗: 区画 (" << x << "," << z << ") のメッシュ焼き込み: " << error.c_str() << "\n";
                    return false;
                }
                const auto parsed = Asset::ParseCookedMesh(Asset::AssetBlob::CopyBytes(
                    Container::Span<const uint8_t>(cooked.NvmeshBytes)));
                if (!parsed.Succeeded() || parsed.Mesh.FormatMajor != 1)
                {
                    std::cout << "失敗: 区画 (" << x << "," << z << ") の NVMESH v1 解析、状態="
                              << static_cast<uint32_t>(parsed.Status) << "\n";
                    return false;
                }
                const auto& mesh = parsed.Mesh;
                const size_t before = triangles.size();
                for (const auto& cluster : mesh.Clusters)
                {
                    // CookedMeshCluster の LODLevel が 0 の段だけを使い、親とフォールバックは混ぜない。
                    if (cluster.LODLevel != 0)
                    {
                        continue;
                    }
                    for (uint32_t index = 0; index < cluster.IndexCount; index += 3)
                    {
                        Triangle triangle;
                        triangle.A = mesh.Vertices[cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + index]].Position;
                        triangle.B = mesh.Vertices[cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + index + 1]].Position;
                        triangle.C = mesh.Vertices[cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + index + 2]].Position;
                        triangles.push_back(triangle);
                    }
                }
                if (triangles.size() == before)
                {
                    std::cout << "失敗: 区画 (" << x << "," << z << ") に最も細かい段の三角形がありません\n";
                    return false;
                }
            }
        }
        return true;
    }

    bool InterpolateTriangle(const Triangle& triangle, const Point& point, double& height)
    {
        const auto& a = triangle.A;
        const auto& b = triangle.B;
        const auto& c = triangle.C;
        const double bx = static_cast<double>(b.X) - a.X;
        const double bz = static_cast<double>(b.Z) - a.Z;
        const double cx = static_cast<double>(c.X) - a.X;
        const double cz = static_cast<double>(c.Z) - a.Z;
        const double determinant = bx * cz - bz * cx;
        if (std::fabs(determinant) < 1e-12)
        {
            return false;
        }
        const double px = static_cast<double>(point.X) - a.X;
        const double pz = static_cast<double>(point.Z) - a.Z;
        const double u = (px * cz - pz * cx) / determinant;
        const double v = (bx * pz - bz * px) / determinant;
        if (u < -1e-10 || v < -1e-10 || u + v > 1.0 + 1e-10)
        {
            return false;
        }
        height = (1.0 - u - v) * a.Y + u * b.Y + v * c.Y;
        return true;
    }

    Container::VariableArray<Point> MakePoints()
    {
        Container::VariableArray<Point> points;
        uint32_t state = 0x13c8a7d5u;
        const auto randomUnit = [&state]()
        {
            // 固定した整数演算で実装差のない乱数を作り、範囲の外周は避ける。
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (static_cast<double>(state) + 0.5) / 4294967296.0;
        };
        for (uint32_t index = 0; index < PointCount; ++index)
        {
            const float x = static_cast<float>(0.001 + randomUnit() * ((Width - 1) * CellSize - 0.002));
            const float z = static_cast<float>(0.001 + randomUnit() * ((Height - 1) * CellSize - 0.002));
            points.push_back({x, z});
        }
        return points;
    }

    void TestMeshHeight(const Terrain::TerrainHeightField& field, const Container::VariableArray<Triangle>& triangles,
                        const Container::VariableArray<Point>& points)
    {
        MaximumError maximum;
        uint32_t holes = 0;
        Point firstHole{};
        for (const auto& point : points)
        {
            bool bFound = false;
            for (const auto& triangle : triangles)
            {
                double expected = 0.0;
                if (InterpolateTriangle(triangle, point, expected))
                {
                    maximum.Observe(std::fabs(static_cast<double>(field.SampleHeight(point.X, point.Z)) - expected), point.X, point.Z);
                    bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                if (holes == 0)
                {
                    firstHole = point;
                }
                ++holes;
            }
        }
        maximum.Print("(b) 焼いた三角形の重心補間と高さ場");
        std::cout << "標本数=" << points.size() << " 三角形数=" << triangles.size() << " 穴=" << holes << "\n";
        if (holes != 0)
        {
            std::cout << "失敗: 含む三角形がない点 (x=" << firstHole.X << ", z=" << firstHole.Z << ")\n";
        }
        Expect(holes == 0, "全標本が焼いた三角形に含まれる（最大差と穴の座標は上記）");
        Expect(maximum.Value <= 1e-4, "メッシュと高さ場の差が 1e-4 以下に収まる（最大差と座標は上記）");
    }

    void TestUnitNormals(const Terrain::TerrainHeightField& field, const Container::VariableArray<Point>& points)
    {
        MaximumError maximum;
        for (const auto& point : points)
        {
            const auto normal = field.SampleNormal(point.X, point.Z);
            const double x = normal.X;
            const double y = normal.Y;
            const double z = normal.Z;
            maximum.Observe(std::fabs(std::sqrt(x * x + y * y + z * z) - 1.0), point.X, point.Z);
        }
        maximum.Print("(c) 有限の単位法線");
        std::cout << "標本数=" << points.size() << "\n";
        Expect(maximum.Value < 1e-4, "法線に NaN・無限大がなく、長さと 1 の差が 1e-4 未満（最大差と座標は上記）");
    }
}

int main()
{
    std::cout.precision(10);
    Cook::TerrainBuildResult cooked;
    if (!BuildTerrain(cooked))
    {
        return 1;
    }
    const auto parsed = Asset::ParseCookedTerrain(Asset::AssetBlob::CopyBytes(
        Container::Span<const uint8_t>(cooked.NvtrnBytes)));
    if (!parsed.Succeeded())
    {
        std::cout << "失敗: NVTRN の解析、状態=" << static_cast<uint32_t>(parsed.Status) << "\n";
        return 1;
    }
    Container::AnsiString error;
    const auto field = Terrain::TerrainHeightField::CreateFromCookedTerrain(parsed.Terrain, error);
    if (!field)
    {
        std::cout << "失敗: 高さ場の生成: " << error.c_str() << "\n";
        return 1;
    }
    TestGridVertices(*field, parsed.Terrain);
    Container::VariableArray<Triangle> triangles;
    if (!CollectTriangles(cooked, triangles))
    {
        return 1;
    }
    const auto points = MakePoints();
    TestMeshHeight(*field, triangles, points);
    TestUnitNormals(*field, points);
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "TerrainHeightFieldTest: 3 件の検査に合格\n";
    return 0;
}
