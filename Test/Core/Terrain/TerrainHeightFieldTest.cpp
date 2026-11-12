// NVTRN から作る高さ場が、解析面の量子化誤差内に収まり、描画用 LOD0 三角形の高さと一致し、有限の単位法線を返す契約を反証する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedTerrainFormat.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Tools/AssetCook/MeshCooker.h"
#include "Tools/AssetCook/TerrainCooker.h"

#include <algorithm>
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
    uint16_t OuterFixtureNearSample(uint32_t x, uint32_t z)
    {
        // 縁は平面のままにし、内側だけに起伏を付ける。
        return static_cast<uint16_t>(12000 + 19 * x + 11 * z + x * (16 - x) * z * (16 - z) / 32);
    }

    uint16_t OuterFixtureSample(uint32_t column, uint32_t row)
    {
        const int32_t x = (static_cast<int32_t>(column) - 2) * 4;
        const int32_t z = (static_cast<int32_t>(row) - 2) * 4;
        if (x >= 0 && x <= 16 && z >= 0 && z <= 16)
        {
            return OuterFixtureNearSample(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
        }
        return static_cast<uint16_t>(12000 + 19 * x + 11 * z + 500 +
            (column * 97 + row * 211 + column * row * 53) % 700);
    }

    void WriteOuterFixtureSample(Container::VariableArray<uint8_t>& bytes, size_t index, uint16_t value)
    {
        bytes[index * 2] = static_cast<uint8_t>(value & 0xffu);
        bytes[index * 2 + 1] = static_cast<uint8_t>(value >> 8);
    }

    void TestOuterRing()
    {
        Asset::CookedTerrainWriteDesc input;
        input.CellCountX = 16;
        input.CellCountZ = 16;
        input.TileCells = 8;
        input.LodCount = 1;
        input.CellSizeM = 1.0f;
        input.OriginX = 40.0;
        input.OriginY = 12.25;
        input.OriginZ = -30.0;
        input.HeightMinM = -20.0f;
        input.HeightMaxM = 120.0f;
        input.Layers.resize(1);
        input.Layers[0].FourCC = Asset::CookedTerrainFormatV0::HeightLayerFourCC;
        input.Layers[0].Encoding = Asset::CookedTerrainFormatV0::LayerEncodingRawHeight16;
        input.Layers[0].Semantic = "height";
        input.bHasOuterRing = true;
        auto& ring = input.OuterRing;
        ring.CellCountX = 8;
        ring.CellCountZ = 8;
        ring.TileCells = 2;
        ring.TileCountX = 4;
        ring.TileCountZ = 4;
        ring.HoleTileMinX = 1;
        ring.HoleTileMinZ = 1;
        ring.HoleTileCountX = 2;
        ring.HoleTileCountZ = 2;
        ring.CellSizeM = 4.0f;
        ring.OriginX = input.OriginX - 8.0;
        ring.OriginZ = input.OriginZ - 8.0;

        Container::VariableArray<uint8_t> nearBytes[4];
        input.Tiles.resize(4);
        for (uint32_t index = 0; index < 4; ++index)
        {
            auto& tile = input.Tiles[index];
            tile.TileX = index % 2;
            tile.TileZ = index / 2;
            tile.HeightMinM = input.HeightMinM;
            tile.HeightMaxM = input.HeightMaxM;
            tile.AabbMin[0] = static_cast<float>(tile.TileX * 8);
            tile.AabbMin[1] = input.HeightMinM;
            tile.AabbMin[2] = static_cast<float>(tile.TileZ * 8);
            tile.AabbMax[0] = tile.AabbMin[0] + 8.0f;
            tile.AabbMax[1] = input.HeightMaxM;
            tile.AabbMax[2] = tile.AabbMin[2] + 8.0f;
            nearBytes[index].resize(9 * 9 * 2);
            for (uint32_t z = 0; z <= 8; ++z)
            {
                for (uint32_t x = 0; x <= 8; ++x)
                {
                    WriteOuterFixtureSample(nearBytes[index], z * 9 + x,
                        OuterFixtureNearSample(tile.TileX * 8 + x, tile.TileZ * 8 + z));
                }
            }
            tile.LayerBytes.push_back(Container::Span<const uint8_t>(nearBytes[index]));
        }
        Container::VariableArray<uint8_t> outerBytes[16];
        input.OuterTiles.resize(12);
        uint32_t outerIndex = 0;
        for (uint32_t tz = 0; tz < 4; ++tz)
        {
            for (uint32_t tx = 0; tx < 4; ++tx)
            {
                if (tx >= 1 && tx < 3 && tz >= 1 && tz < 3)
                {
                    continue;
                }
                auto& tile = input.OuterTiles[outerIndex++];
                tile.TileX = tx;
                tile.TileZ = tz;
                tile.HeightMinM = input.HeightMinM;
                tile.HeightMaxM = input.HeightMaxM;
                tile.AabbMin[0] = static_cast<float>(static_cast<int32_t>(tx) * 8 - 8);
                tile.AabbMin[1] = input.HeightMinM;
                tile.AabbMin[2] = static_cast<float>(static_cast<int32_t>(tz) * 8 - 8);
                tile.AabbMax[0] = tile.AabbMin[0] + 8.0f;
                tile.AabbMax[1] = input.HeightMaxM;
                tile.AabbMax[2] = tile.AabbMin[2] + 8.0f;
                auto& bytes = outerBytes[tz * 4 + tx];
                bytes.resize(3 * 3 * 2);
                for (uint32_t z = 0; z <= 2; ++z)
                {
                    for (uint32_t x = 0; x <= 2; ++x)
                    {
                        WriteOuterFixtureSample(bytes, z * 3 + x, OuterFixtureSample(tx * 2 + x, tz * 2 + z));
                    }
                }
                tile.HeightBytes = Container::Span<const uint8_t>(bytes);
            }
        }
        Container::VariableArray<uint8_t> encoded;
        const auto status = Asset::WriteCookedTerrain(input, encoded);
        if (status != Asset::CookedTerrainWriteStatus::Success)
        {
            Expect(false, "手組みの外周付き NVTRN を書き出せる");
            std::cout << "書き出し状態=" << static_cast<uint32_t>(status) << "\n";
            return;
        }
        const auto parsed = Asset::ParseCookedTerrain(Asset::AssetBlob::CopyBytes(Container::Span<const uint8_t>(encoded)));
        if (!parsed.Succeeded())
        {
            Expect(false, "手組みの外周付き NVTRN を解析できる");
            std::cout << "解析状態=" << static_cast<uint32_t>(parsed.Status) << "\n";
            return;
        }
        Container::AnsiString error = "前回のエラー";
        const auto nearField = Terrain::TerrainHeightField::CreateFromCookedTerrain(parsed.Terrain, error);
        if (!nearField)
        {
            Expect(false, "外周の試験の近景の高さ場を作れる");
            std::cout << error.c_str() << "\n";
            return;
        }
        const auto full = Terrain::TerrainHeightField::CreateOuterRingFromCookedTerrain(parsed.Terrain, error);
        if (!full || !error.empty())
        {
            Expect(false, "外周の全格子の高さ場を作れる");
            std::cout << error.c_str() << "\n";
            return;
        }
        Container::VariableArray<Container::TSharedPtr<const Terrain::TerrainHeightField>> strips;
        strips.push_back(full);
        if (!Terrain::TerrainHeightField::CreateOuterRingStripsFromCookedTerrain(parsed.Terrain, strips, error) ||
            !error.empty() || strips.size() != 4)
        {
            Expect(false, "外周を北・南・西・東の 4 本の帯にできる");
            std::cout << error.c_str() << "\n";
            return;
        }

        const auto restored = [&](uint32_t x, uint32_t z)
        {
            return static_cast<float>(parsed.Terrain.OriginY + parsed.Terrain.DecodeHeightM(OuterFixtureSample(x, z)));
        };
        const auto expectedHeight = [&](uint32_t x, uint32_t z, double u, double v)
        {
            const double h00 = restored(x, z);
            const double h10 = restored(x + 1, z);
            const double h01 = restored(x, z + 1);
            const double h11 = restored(x + 1, z + 1);
            if (Terrain::UsesMainDiagonal(x, z))
            {
                return u >= v ? h00 + u * (h10 - h00) + v * (h11 - h10)
                              : h00 + v * (h01 - h00) + u * (h11 - h01);
            }
            return u + v <= 1.0 ? h00 + u * (h10 - h00) + v * (h01 - h00)
                                : h11 + (1.0 - v) * (h10 - h11) + (1.0 - u) * (h01 - h11);
        };
        struct Rectangle
        {
            uint32_t MinX;
            uint32_t MinZ;
            uint32_t MaxX;
            uint32_t MaxZ;
        };
        const Rectangle rectangles[] = {{0, 0, 8, 2}, {0, 6, 8, 8}, {0, 2, 2, 6}, {6, 2, 8, 6}};
        const double offsets[][2] = {{0.0, 0.0}, {0.2, 0.65}, {0.75, 0.749},
                                     {0.75, 0.751}, {0.2, 0.799}, {0.2, 0.801}};
        MaximumError interpolation;
        MaximumError fullComparison;
        for (uint32_t stripIndex = 0; stripIndex < 4; ++stripIndex)
        {
            const auto& rectangle = rectangles[stripIndex];
            for (uint32_t z = rectangle.MinZ; z < rectangle.MaxZ; ++z)
            {
                for (uint32_t x = rectangle.MinX; x < rectangle.MaxX; ++x)
                {
                    for (const auto& offset : offsets)
                    {
                        const float px = static_cast<float>(ring.OriginX + (x + offset[0]) * ring.CellSizeM);
                        const float pz = static_cast<float>(ring.OriginZ + (z + offset[1]) * ring.CellSizeM);
                        // 実際に float に丸められた座標で期待値を求める。
                        const double u = (static_cast<double>(px) - ring.OriginX) / ring.CellSizeM - x;
                        const double v = (static_cast<double>(pz) - ring.OriginZ) / ring.CellSizeM - z;
                        const float actual = strips[stripIndex]->SampleHeight(px, pz);
                        interpolation.Observe(std::fabs(static_cast<double>(actual) - expectedHeight(x, z, u, v)), px, pz);
                        fullComparison.Observe(std::fabs(static_cast<double>(actual) - full->SampleHeight(px, pz)), px, pz);
                    }
                }
            }
        }
        interpolation.Print("(d) 外周の各帯と三角形の補間");
        fullComparison.Print("(d) 外周の各帯と全格子");
        Expect(interpolation.Value <= 1e-4 && fullComparison.Value <= 1e-4,
            "4 本の帯の頂点・升目・対角線付近が全格子の補間と一致する");

        MaximumError interior;
        for (uint32_t z = 2; z <= 6; ++z)
        {
            for (uint32_t x = 2; x <= 6; ++x)
            {
                interior.Observe(std::fabs(static_cast<double>(full->GetGridVertexHeight(x, z)) - restored(x, z)),
                    static_cast<float>(x), static_cast<float>(z));
            }
        }
        Expect(interior.Value == 0.0, "全格子の穴の内側は近景の標本から復元する");

        double maxSlope = 0.0;
        const auto observeSlopes = [&](const Terrain::TerrainHeightField& field)
        {
            const auto layout = field.GetGridLayout();
            for (uint32_t z = 0; z < layout.Rows; ++z)
            {
                for (uint32_t x = 0; x < layout.Columns; ++x)
                {
                    const double h = field.GetGridVertexHeight(x, z);
                    if (x + 1 < layout.Columns)
                    {
                        maxSlope = std::max(maxSlope, std::fabs(field.GetGridVertexHeight(x + 1, z) - h) / layout.Spacing);
                    }
                    if (z + 1 < layout.Rows)
                    {
                        maxSlope = std::max(maxSlope, std::fabs(field.GetGridVertexHeight(x, z + 1) - h) / layout.Spacing);
                    }
                }
            }
        };
        observeSlopes(*nearField);
        observeSlopes(*full);
        constexpr float epsilon = 1e-3f;
        MaximumError seam;
        for (uint32_t point = 0; point <= 8; ++point)
        {
            const float alongX = static_cast<float>(input.OriginX + point * 2);
            const float alongZ = static_cast<float>(input.OriginZ + point * 2);
            const float x0 = static_cast<float>(input.OriginX);
            const float x1 = x0 + 16.0f;
            const float z0 = static_cast<float>(input.OriginZ);
            const float z1 = z0 + 16.0f;
            seam.Observe(std::fabs(static_cast<double>(nearField->SampleHeight(alongX, z0 + epsilon)) -
                strips[0]->SampleHeight(alongX, z0 - epsilon)), alongX, z0);
            seam.Observe(std::fabs(static_cast<double>(nearField->SampleHeight(alongX, z1 - epsilon)) -
                strips[1]->SampleHeight(alongX, z1 + epsilon)), alongX, z1);
            seam.Observe(std::fabs(static_cast<double>(nearField->SampleHeight(x0 + epsilon, alongZ)) -
                strips[2]->SampleHeight(x0 - epsilon, alongZ)), x0, alongZ);
            seam.Observe(std::fabs(static_cast<double>(nearField->SampleHeight(x1 - epsilon, alongZ)) -
                strips[3]->SampleHeight(x1 + epsilon, alongZ)), x1, alongZ);
        }
        const double tolerance = (input.HeightMaxM - input.HeightMinM) / 65535.0 + maxSlope * 2.0 * epsilon;
        seam.Print("(d) 外周と近景の境界の両側");
        std::cout << "境界の許容差=" << tolerance << "\n";
        Expect(seam.Value <= tolerance, "近景の 4 辺の両側は量子化と傾きの許容差以内でつながる");

        const Point outside[] = {{31.0f, -22.0f}, {65.0f, -22.0f}, {48.0f, -39.0f}, {48.0f, -5.0f}};
        for (const auto& point : outside)
        {
            bool bOutOfRange = std::isnan(nearField->SampleHeight(point.X, point.Z));
            for (const auto& strip : strips)
            {
                bOutOfRange = bOutOfRange && std::isnan(strip->SampleHeight(point.X, point.Z));
            }
            Expect(bOutOfRange, "外周の矩形の外では近景と全帯が範囲外を返す");
        }
        for (const auto& strip : strips)
        {
            Expect(std::isnan(strip->SampleHeight(48.0f, -22.0f)), "帯は近景の穴の内部を覆わない");
        }
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
    TestOuterRing();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "TerrainHeightFieldTest: 4 件の検査に合格\n";
    return 0;
}
