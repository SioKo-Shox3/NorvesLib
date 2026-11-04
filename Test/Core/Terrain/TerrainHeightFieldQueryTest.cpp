// 地形の高さ・接触・掃引と地面の照会を解析値や独立した基準と比較する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedTerrainFormat.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Rendering/Terrain/TerrainSystem.h"
#include "Math/GeometryIntersection.h"
#include "Tools/AssetCook/MeshCooker.h"
#include "Tools/AssetCook/TerrainCooker.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace Asset = NorvesLib::Core::Asset;
namespace Container = NorvesLib::Core::Container;
namespace Terrain = NorvesLib::Core::Rendering::Terrain;
namespace Cook = NorvesLib::Tools::AssetCook;
namespace Math = NorvesLib::Math;

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


    constexpr uint32_t GridSize = 65;
    constexpr float FirstX = -10.25f;
    constexpr float FirstZ = 7.5f;
    constexpr float Spacing = 0.5f;
    constexpr float SlopeX = 0.17f;
    constexpr float SlopeZ = -0.11f;
    constexpr float WaveAmplitude = 0.8f;
    constexpr float WaveX = 0.22f;
    constexpr float WaveZ = 0.18f;
    constexpr float Degrees = 57.29577951308232f;

    enum class Surface { Flat, Slope, Wave };

    struct Random
    {
        uint32_t State = 0x812ac73du;

        float Unit()
        {
            State ^= State << 13;
            State ^= State >> 17;
            State ^= State << 5;
            return static_cast<float>((static_cast<double>(State) + 0.5) / 4294967296.0);
        }

        float Range(float minimum, float maximum)
        {
            return minimum + (maximum - minimum) * Unit();
        }
    };

    double HeightAt(Surface surface, double x, double z)
    {
        if (surface == Surface::Flat)
        {
            return 1.75;
        }
        if (surface == Surface::Slope)
        {
            return SlopeX * x + SlopeZ * z + 2.3;
        }
        return WaveAmplitude * std::sin(WaveX * x) * std::cos(WaveZ * z);
    }

    Math::Vector3 Unit(const Math::Vector3& value)
    {
        const double length = std::hypot(static_cast<double>(value.x), static_cast<double>(value.y), static_cast<double>(value.z));
        return Math::Vector3(static_cast<float>(value.x / length), static_cast<float>(value.y / length),
            static_cast<float>(value.z / length));
    }

    Math::Vector3 AnalyticNormal(Surface surface, float x, float z)
    {
        double dx = 0.0;
        double dz = 0.0;
        if (surface == Surface::Slope)
        {
            dx = SlopeX;
            dz = SlopeZ;
        }
        else if (surface == Surface::Wave)
        {
            dx = WaveAmplitude * WaveX * std::cos(WaveX * x) * std::cos(WaveZ * z);
            dz = -WaveAmplitude * WaveZ * std::sin(WaveX * x) * std::sin(WaveZ * z);
        }
        return Unit(Math::Vector3(static_cast<float>(-dx), 1.0f, static_cast<float>(-dz)));
    }

    Math::Vector3 ToVector(const Terrain::TerrainNormal& normal)
    {
        return Math::Vector3(normal.X, normal.Y, normal.Z);
    }

    double Distance(const Math::Vector3& a, const Math::Vector3& b)
    {
        return std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y,
            static_cast<double>(a.z) - b.z);
    }

    double Angle(const Math::Vector3& a, const Math::Vector3& b)
    {
        const double al = std::hypot(static_cast<double>(a.x), static_cast<double>(a.y), static_cast<double>(a.z));
        const double bl = std::hypot(static_cast<double>(b.x), static_cast<double>(b.y), static_cast<double>(b.z));
        if (!(al > 0.0) || !(bl > 0.0))
        {
            return std::numeric_limits<double>::infinity();
        }
        const double dot = (static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y
            + static_cast<double>(a.z) * b.z) / (al * bl);
        return std::acos(std::fmax(-1.0, std::fmin(1.0, dot))) * Degrees;
    }

    Container::TSharedPtr<const Terrain::TerrainHeightField> MakeField(Surface surface,
        Container::VariableArray<uint8_t>* weights = nullptr)
    {
        Terrain::TerrainGridLayout layout;
        layout.Columns = GridSize;
        layout.Rows = GridSize;
        layout.FirstX = FirstX;
        layout.FirstZ = FirstZ;
        layout.Spacing = Spacing;
        Container::VariableArray<float> heights(GridSize * GridSize);
        for (uint32_t row = 0; row < GridSize; ++row)
        {
            for (uint32_t column = 0; column < GridSize; ++column)
            {
                const float x = FirstX + column * Spacing;
                const float z = FirstZ + row * Spacing;
                heights[row * GridSize + column] = static_cast<float>(HeightAt(surface, x, z));
            }
        }
        Terrain::TerrainSplatSamples splat;
        Container::AnsiString names[5] = {"layer0", "layer1", "layer2", "layer3", "layer4"};
        if (weights != nullptr)
        {
            weights->resize(GridSize * GridSize * 5);
            Random random;
            for (uint32_t vertex = 0; vertex < GridSize * GridSize; ++vertex)
            {
                for (uint32_t layer = 0; layer < 5; ++layer)
                {
                    (*weights)[vertex * 5 + layer] = static_cast<uint8_t>(random.State >> 24);
                    random.Unit();
                }
                // 最大重みの同点が生じる頂点も含める。
                if (vertex % 13 == 0)
                {
                    (*weights)[vertex * 5] = 255;
                    (*weights)[vertex * 5 + 1] = 255;
                }
            }
            splat.LayerCount = 5;
            splat.Weights = Container::Span<const uint8_t>(*weights);
            splat.LayerNames = Container::Span<const Container::AnsiString>(names, 5);
        }
        Container::AnsiString error;
        const auto field = Terrain::TerrainHeightField::CreateFromSamples(layout, Container::Span<const float>(heights),
            weights ? &splat : nullptr, 8, error);
        if (!field)
        {
            std::cout << "失敗: 標本から高さ場を作れません: " << error.c_str() << "\n";
            ++g_FailureCount;
        }
        return field;
    }

    void TestHeightsAndNormals(const Terrain::TerrainHeightField& flat, const Terrain::TerrainHeightField& slope,
        const Terrain::TerrainHeightField& wave)
    {
        const Terrain::TerrainHeightField* fields[] = {&flat, &slope, &wave};
        const Surface surfaces[] = {Surface::Flat, Surface::Slope, Surface::Wave};
        MaximumError vertices;
        MaximumError interior;
        MaximumError face;
        MaximumError smooth;
        Random random;
        for (int type = 0; type < 3; ++type)
        {
            for (uint32_t row = 0; row < GridSize; ++row)
            {
                for (uint32_t column = 0; column < GridSize; ++column)
                {
                    const float x = FirstX + column * Spacing;
                    const float z = FirstZ + row * Spacing;
                    vertices.Observe(std::fabs(fields[type]->SampleHeight(x, z) - HeightAt(surfaces[type], x, z)), x, z);
                    if (type == 2)
                    {
                        smooth.Observe(Angle(ToVector(wave.SampleNormal(x, z, Terrain::TerrainNormalKind::Smooth)),
                            AnalyticNormal(Surface::Wave, x, z)), x, z);
                    }
                }
            }
            for (uint32_t i = 0; i < 10000; ++i)
            {
                const float x = FirstX + random.Range(0.0f, 32.0f);
                const float z = FirstZ + random.Range(0.0f, 32.0f);
                if (type < 2)
                {
                    interior.Observe(std::fabs(fields[type]->SampleHeight(x, z) - HeightAt(surfaces[type], x, z)), x, z);
                    face.Observe(Distance(ToVector(fields[type]->SampleNormal(x, z, Terrain::TerrainNormalKind::Face)),
                        AnalyticNormal(surfaces[type], x, z)), x, z);
                }
                else
                {
                    smooth.Observe(Angle(ToVector(wave.SampleNormal(x, z, Terrain::TerrainNormalKind::Smooth)),
                        AnalyticNormal(Surface::Wave, x, z)), x, z);
                }
            }
        }
        vertices.Print("高さ: 3 面の頂点");
        interior.Print("高さ: 平面と斜面の任意点");
        face.Print("法線: 平面と斜面の面法線");
        smooth.Print("法線: 正弦波の角度差[度]");
        std::cout << "頂点数=" << 3 * GridSize * GridSize << " 任意点数=" << 30000 << "\n";
        Expect(vertices.Value <= 1e-4, "頂点の高さは解析値と 1e-4 以内");
        Expect(interior.Value <= 1e-4, "平面と斜面の高さは解析値と 1e-4 以内");
        Expect(face.Value <= 1e-5, "平面と斜面の面法線は解析値と 1e-5 以内");
        // 波数は最大 0.22 rad/m、格子幅は 0.5 m。中心差分と補間の誤差は波長に比べ十分小さく、
        // 外周の片側差分も含めて解析法線との角度差 2 度を上限とする。
        Expect(smooth.Value <= 2.0, "正弦波の滑らかな法線は解析値と 2 度以内");
    }

    Container::VariableArray<Math::Triangle> MakeGridTriangles(const Terrain::TerrainHeightField& field)
    {
        Container::VariableArray<Math::Triangle> triangles;
        for (uint32_t row = 0; row < GridSize - 1; ++row)
        {
            for (uint32_t column = 0; column < GridSize - 1; ++column)
            {
                const float x = FirstX + column * Spacing;
                const float z = FirstZ + row * Spacing;
                const Math::Vector3 v00(x, field.GetGridVertexHeight(column, row), z);
                const Math::Vector3 v10(x + Spacing, field.GetGridVertexHeight(column + 1, row), z);
                const Math::Vector3 v01(x, field.GetGridVertexHeight(column, row + 1), z + Spacing);
                const Math::Vector3 v11(x + Spacing, field.GetGridVertexHeight(column + 1, row + 1), z + Spacing);
                if (Terrain::UsesMainDiagonal(column, row))
                {
                    triangles.push_back(Math::Triangle(v00, v10, v11));
                    triangles.push_back(Math::Triangle(v00, v11, v01));
                }
                else
                {
                    triangles.push_back(Math::Triangle(v00, v10, v01));
                    triangles.push_back(Math::Triangle(v10, v11, v01));
                }
            }
        }
        return triangles;
    }

    void TestRaycast(const Terrain::TerrainHeightField& field)
    {
        const auto triangles = MakeGridTriangles(field);
        Random random;
        uint32_t mismatches = 0;
        uint32_t hits = 0;
        MaximumError maximum;
        for (uint32_t i = 0; i < 10000; ++i)
        {
            const Math::Vector3 origin(FirstX + random.Range(-3.0f, 35.0f), random.Range(3.0f, 9.0f),
                FirstZ + random.Range(-3.0f, 35.0f));
            const Math::Vector3 direction = i % 2 == 0 ? Math::Vector3(0.0f, -1.0f, 0.0f)
                : Unit(Math::Vector3(random.Range(-0.9f, 0.9f), -random.Range(0.2f, 1.0f), random.Range(-0.9f, 0.9f)));
            const Math::Ray ray(origin, direction);
            const float limit = random.Range(2.0f, 25.0f);
            float closest = limit;
            bool expected = false;
            for (const auto& triangle : triangles)
            {
                float t = 0.0f;
                if (Math::RayIntersectsTriangle(ray, triangle.V0, triangle.V1, triangle.V2, t) && t <= closest)
                {
                    closest = t;
                    expected = true;
                }
            }
            Terrain::TerrainRayHit hit;
            const bool actual = field.Raycast(ray, limit, hit);
            if (actual != expected)
            {
                if (mismatches == 0)
                {
                    std::cout << "レイの最初の不一致: 番号=" << i << " 期待=" << expected << " 実際=" << actual << "\n";
                }
                ++mismatches;
            }
            else if (actual)
            {
                ++hits;
                maximum.Observe(Distance(hit.Point, ray.PointAt(closest)), origin.x, origin.z);
            }
        }
        Terrain::TerrainRayHit hit;
        const float x = FirstX + 10.3f;
        const float z = FirstZ + 12.2f;
        const float y = field.SampleHeight(x, z);
        const Math::Ray below(Math::Vector3(x, y - 1.0f, z), Math::Vector3(0.0f, 1.0f, 0.0f));
        Expect(field.Raycast(below, 5.0f, hit) && hit.Distance == 0.0f && Distance(hit.Normal, Math::Vector3(0.0f)) == 0.0,
            "地中からのレイは距離 0・零法線");
        const Math::Ray down(Math::Vector3(x, y + 2.0f, z), Math::Vector3(0.0f, -1.0f, 0.0f));
        Expect(!field.Raycast(down, 1.9f, hit), "最大距離の先にある地表には当たらない");
        const Math::Ray outside(Math::Vector3(FirstX - 1.0f, 5.0f, z), Math::Vector3(0.0f, -1.0f, 0.0f));
        Expect(!field.Raycast(outside, 20.0f, hit), "範囲外を通るレイは外れる");
        const Math::Ray entering(Math::Vector3(FirstX - 1.0f, 2.0f, z), Math::Vector3(1.0f, -0.5f, 0.0f));
        Expect(field.Raycast(entering, 15.0f, hit), "外から入るレイは地表に当たる");
        maximum.Print("レイ: 総当たりとの最近点差[m]");
        std::cout << "レイ: 一致=" << 10000 - mismatches << "/10000 当たり=" << hits << " 三角形数=" << triangles.size() << "\n";
        Expect(mismatches == 0, "レイの当たり・外れは全三角形の総当たりと一致");
        Expect(maximum.Value <= 1e-3, "レイの最近点は総当たりと 1e-3 m 以内");
    }
    const Math::GeometryContact* Deepest(const Container::VariableArray<Math::GeometryContact>& contacts)
    {
        if (contacts.empty())
        {
            return nullptr;
        }
        const Math::GeometryContact* deepest = &contacts[0];
        for (const auto& contact : contacts)
        {
            if (contact.Depth > deepest->Depth)
            {
                deepest = &contact;
            }
        }
        return deepest;
    }

    uint32_t Overlap(const Terrain::TerrainHeightField& field, const Math::Sphere& shape,
        Container::VariableArray<Math::GeometryContact>& contacts)
    {
        return field.OverlapSphere(shape, contacts);
    }

    uint32_t Overlap(const Terrain::TerrainHeightField& field, const Math::Capsule& shape,
        Container::VariableArray<Math::GeometryContact>& contacts)
    {
        return field.OverlapCapsule(shape, contacts);
    }

    uint32_t Overlap(const Terrain::TerrainHeightField& field, const Math::OBB& shape,
        Container::VariableArray<Math::GeometryContact>& contacts)
    {
        return field.OverlapBox(shape, contacts);
    }

    template<typename Shape>
    void CheckSlopeContact(const Terrain::TerrainHeightField& field, const Shape& shape,
        const Math::Vector3& center, float support, const char* label, MaximumError& depthError,
        MaximumError& normalError)
    {
        Container::VariableArray<Math::GeometryContact> contacts;
        Overlap(field, shape, contacts);
        const auto* contact = Deepest(contacts);
        if (contact == nullptr)
        {
            std::cout << "失敗: " << label << " の接触がありません\n";
            ++g_FailureCount;
            return;
        }
        const auto normal = AnalyticNormal(Surface::Slope, center.x, center.z);
        const double signedDistance = (center.y - HeightAt(Surface::Slope, center.x, center.z)) * normal.y;
        const double expectedDepth = support - signedDistance;
        const double depthDifference = std::fabs(contact->Depth - expectedDepth);
        const double normalDifference = Distance(contact->Normal, normal);
        depthError.Observe(depthDifference, center.x, center.z);
        normalError.Observe(normalDifference, center.x, center.z);
        std::cout << label << " 接触深さ=" << contact->Depth << " 解析値=" << expectedDepth
                  << " 法線差=" << normalDifference << " 中心=(" << center.x << "," << center.y << ","
                  << center.z << ") 支持長=" << support << "\n";
    }

    void TestSlopeOverlap(const Terrain::TerrainHeightField& field)
    {
        const float x = FirstX + 15.3f;
        const float z = FirstZ + 14.7f;
        const float height = static_cast<float>(HeightAt(Surface::Slope, x, z));
        const Math::Vector3 center(x, height + 0.20f, z);
        const Math::Vector3 normal = AnalyticNormal(Surface::Slope, x, z);
        const Math::Vector3 tangent = Unit(Math::Vector3(1.0f, SlopeX, 0.0f));
        const Math::Vector3 bitangent = Unit(Math::VectorUtils::Cross(tangent, normal));
        MaximumError depthError;
        MaximumError normalError;
        CheckSlopeContact(field, Math::Sphere(center, 0.45f), center, 0.45f, "斜面上の球", depthError, normalError);
        const float halfLength = 0.55f;
        CheckSlopeContact(field, Math::Capsule(center - tangent * halfLength, center + tangent * halfLength, 0.38f),
            center, 0.38f, "斜面に沿うカプセル", depthError, normalError);
        const Math::Vector3 up(0.0f, 1.0f, 0.0f);
        CheckSlopeContact(field, Math::Capsule(center - up * halfLength, center + up * halfLength, 0.38f),
            center, 0.38f + halfLength * normal.y, "直立カプセル", depthError, normalError);
        const Math::Vector3 extents(0.55f, 0.40f, 0.35f);
        CheckSlopeContact(field, Math::OBB(center, extents, tangent, normal, bitangent), center, extents.y,
            "斜面に沿う箱", depthError, normalError);
        const float axisSupport = std::fabs(normal.x) * extents.x + std::fabs(normal.y) * extents.y
            + std::fabs(normal.z) * extents.z;
        Math::OBB axisBox;
        axisBox.Center = center;
        axisBox.HalfExtents = extents;
        CheckSlopeContact(field, axisBox, center, axisSupport, "軸に沿う箱", depthError, normalError);
        const Math::Vector3 below(x, height - 1.0f, z);
        CheckSlopeContact(field, Math::Sphere(below, 0.45f), below, 0.45f, "地中 1 m の球", depthError, normalError);
        depthError.Print("斜面: 接触深さの最大差[m]");
        normalError.Print("斜面: 接触法線の最大差");
        Expect(depthError.Value <= 1e-4, "斜面の最も深い接触の深さは解析値と 1e-4 m 以内");
        Expect(normalError.Value <= 1e-4, "斜面の最も深い接触の法線は斜面の法線");
    }

    void TestOverlapContinuity(const Terrain::TerrainHeightField& field)
    {
        MaximumError angles[3];
        uint32_t missing[3] = {};
        uint32_t down[3] = {};
        uint32_t steps[3] = {};
        float minimumY[3] = {1.0f, 1.0f, 1.0f};
        for (uint32_t path = 0; path < 3; ++path)
        {
            Math::Vector3 previous[3];
            bool havePrevious[3] = {};
            for (uint32_t step = 0; step <= 40; ++step)
            {
                const float advance = step * 0.1f;
                const float x = FirstX + 10.0f + advance;
                const float z = FirstZ + 10.0f + (path == 1 ? 0.25f : (path == 2 ? advance : 0.0f));
                const Math::Vector3 center(x, field.SampleHeight(x, z) + 0.22f, z);
                const Math::Sphere sphere(center, 0.45f);
                const Math::Vector3 halfSegment(0.3f, 0.0f, 0.0f);
                const Math::Capsule capsule(center - halfSegment, center + halfSegment, 0.45f);
                Math::OBB box;
                box.Center = center;
                box.HalfExtents = Math::Vector3(0.35f, 0.35f, 0.35f);
                for (int shape = 0; shape < 3; ++shape)
                {
                    Container::VariableArray<Math::GeometryContact> contacts;
                    if (shape == 0) Overlap(field, sphere, contacts);
                    if (shape == 1) Overlap(field, capsule, contacts);
                    if (shape == 2) Overlap(field, box, contacts);
                    const auto* contact = Deepest(contacts);
                    ++steps[shape];
                    if (!contact)
                    {
                        ++missing[shape];
                        havePrevious[shape] = false;
                        continue;
                    }
                    minimumY[shape] = std::fmin(minimumY[shape], contact->Normal.y);
                    if (!(contact->Normal.y > 0.0f)) ++down[shape];
                    if (havePrevious[shape]) angles[shape].Observe(Angle(previous[shape], contact->Normal), x, z);
                    previous[shape] = contact->Normal;
                    havePrevious[shape] = true;
                }
            }
        }
        const char* labels[] = {"球", "カプセル", "箱"};
        for (int shape = 0; shape < 3; ++shape)
        {
            std::cout << "境界をまたぐ" << labels[shape] << ": 歩数=" << steps[shape]
                      << " 接触なし=" << missing[shape] << " 下向き=" << down[shape]
                      << " 最小 Y=" << minimumY[shape] << " 最大角度差[度]=" << angles[shape].Value
                      << " (x=" << angles[shape].X << ", z=" << angles[shape].Z << ")\n";
            Expect(missing[shape] == 0 && down[shape] == 0, "境界上でも最も深い接触は上向き");
            Expect(angles[shape].Value <= 10.0, "隣の歩の接触法線の角度差は 10 度以内");
        }
    }

    Math::Sphere Move(const Math::Sphere& sphere, const Math::Vector3& displacement)
    {
        return Math::Sphere(sphere.Center + displacement, sphere.Radius);
    }

    Math::Capsule Move(const Math::Capsule& capsule, const Math::Vector3& displacement)
    {
        return Math::Capsule(capsule.PointA + displacement, capsule.PointB + displacement, capsule.Radius);
    }

    void PrintSweepShape(const Math::Sphere& sphere)
    {
        std::cout << "球: 中心=(" << sphere.Center.x << "," << sphere.Center.y << "," << sphere.Center.z
                  << ") 半径=" << sphere.Radius << "\n";
    }

    void PrintSweepShape(const Math::Capsule& capsule)
    {
        std::cout << "カプセル: 点A=(" << capsule.PointA.x << "," << capsule.PointA.y << "," << capsule.PointA.z
                  << ") 点B=(" << capsule.PointB.x << "," << capsule.PointB.y << "," << capsule.PointB.z
                  << ") 半径=" << capsule.Radius << "\n";
    }

    bool Sweep(const Terrain::TerrainHeightField& field, const Math::Sphere& sphere, const Math::Vector3& direction,
        float limit, Math::GeometrySweepHit& hit)
    {
        return field.SweepSphere(sphere, direction, limit, hit);
    }

    bool Sweep(const Terrain::TerrainHeightField& field, const Math::Capsule& capsule, const Math::Vector3& direction,
        float limit, Math::GeometrySweepHit& hit)
    {
        return field.SweepCapsule(capsule, direction, limit, hit);
    }

    template<typename Shape>
    bool SweepOracle(const Terrain::TerrainHeightField& field, const Shape& shape, const Math::Vector3& direction,
        float limit, float& distance)
    {
        Container::VariableArray<Math::GeometryContact> contacts;
        if (Overlap(field, shape, contacts) != 0)
        {
            distance = 0.0f;
            return true;
        }
        float lower = 0.0f;
        for (uint32_t step = 1; lower < limit; ++step)
        {
            const float upperStep = std::fmin(step * 0.01f, limit);
            if (Overlap(field, Move(shape, direction * upperStep), contacts) != 0)
            {
                float upper = upperStep;
                while (upper - lower > 1e-5f)
                {
                    const float middle = (lower + upper) * 0.5f;
                    if (Overlap(field, Move(shape, direction * middle), contacts) != 0) upper = middle;
                    else lower = middle;
                }
                distance = upper;
                return true;
            }
            lower = upperStep;
        }
        return false;
    }

    template<typename Shape>
    void CompareSweep(const Terrain::TerrainHeightField& field, const Shape& shape, const Math::Vector3& direction,
        float limit, uint32_t index, uint32_t& mismatches, uint32_t& hits, MaximumError& maximum)
    {
        float expectedDistance = 0.0f;
        const bool expected = SweepOracle(field, shape, direction, limit, expectedDistance);
        Math::GeometrySweepHit hit;
        const bool actual = Sweep(field, shape, direction, limit, hit);
        if (actual != expected)
        {
            if (mismatches == 0)
            {
                std::cout << "掃引の最初の不一致: 番号=" << index << " 期待=" << expected << " 実際=" << actual
                          << " 結果=" << static_cast<int>(hit.Result) << " 期待距離=" << expectedDistance
                          << " 未確定距離=" << hit.Distance << " 最大距離=" << limit << " 向き=("
                          << direction.x << "," << direction.y << "," << direction.z << ")\n";
                PrintSweepShape(shape);
            }
            ++mismatches;
        }
        else if (actual)
        {
            ++hits;
            maximum.Observe(std::fabs(hit.Distance - expectedDistance), static_cast<float>(index), 0.0f);
        }
    }

    void TestSweeps(const Terrain::TerrainHeightField& field)
    {
        Random random;
        uint32_t mismatches[2] = {};
        uint32_t hits[2] = {};
        MaximumError errors[2];
        for (uint32_t i = 0; i < 200; ++i)
        {
            const float x = FirstX + random.Range(4.0f, 28.0f);
            const float z = FirstZ + random.Range(4.0f, 28.0f);
            const float offset = i % 12 == 0 ? -0.2f : random.Range(0.6f, 2.4f);
            const Math::Vector3 center(x, field.SampleHeight(x, z) + offset, z);
            const Math::Vector3 direction = Unit(Math::Vector3(random.Range(-0.65f, 0.65f),
                i % 4 == 0 ? random.Range(0.2f, 1.0f) : -random.Range(0.4f, 1.0f), random.Range(-0.65f, 0.65f)));
            const float limit = random.Range(1.0f, 3.0f);
            const float radius = random.Range(0.2f, 0.45f);
            const Math::Vector3 halfSegment = Unit(Math::Vector3(random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f),
                random.Range(-1.0f, 1.0f))) * random.Range(0.1f, 0.5f);
            CompareSweep(field, Math::Sphere(center, radius), direction, limit, i, mismatches[0], hits[0], errors[0]);
            CompareSweep(field, Math::Capsule(center - halfSegment, center + halfSegment, radius), direction,
                limit, i, mismatches[1], hits[1], errors[1]);
        }
        const char* labels[] = {"球", "カプセル"};
        for (int shape = 0; shape < 2; ++shape)
        {
            std::cout << "掃引 " << labels[shape] << ": 一致=" << 200 - mismatches[shape] << "/200 当たり="
                      << hits[shape] << " 最大距離差[m]=" << errors[shape].Value
                      << " (番号=" << errors[shape].X << ")\n";
            Expect(mismatches[shape] == 0, "掃引の当たり・外れは段階的な重なり照会と一致");
            Expect(errors[shape].Value <= 1e-3, "掃引の距離は段階的な重なり照会と 1e-3 m 以内");
        }
    }
    bool EqualBits(float a, float b)
    {
        return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
    }

    bool EqualBits(const Math::Vector3& a, const Math::Vector3& b)
    {
        return EqualBits(a.x, b.x) && EqualBits(a.y, b.y) && EqualBits(a.z, b.z);
    }

    bool EqualSample(const Terrain::GroundSample& a, const Terrain::GroundSample& b)
    {
        return a.bValid == b.bValid && EqualBits(a.Height, b.Height) && EqualBits(a.Normal, b.Normal)
            && EqualBits(a.SlopeRadians, b.SlopeRadians) && a.SurfaceId == b.SurfaceId && a.TileId == b.TileId;
    }

    uint8_t ExpectedSurface(const Container::VariableArray<uint8_t>& weights, float x, float z)
    {
        const uint32_t column = static_cast<uint32_t>(std::floor((x - FirstX) / Spacing + 0.5f));
        const uint32_t row = static_cast<uint32_t>(std::floor((z - FirstZ) / Spacing + 0.5f));
        const size_t offset = (static_cast<size_t>(row) * GridSize + column) * 5;
        uint8_t layer = 0;
        for (uint8_t i = 1; i < 5; ++i)
        {
            if (weights[offset + i] > weights[offset + layer]) layer = i;
        }
        return layer;
    }

    uint32_t ExpectedTile(float x, float z)
    {
        const uint32_t column = static_cast<uint32_t>((x - FirstX) / (Spacing * 8.0f));
        const uint32_t row = static_cast<uint32_t>((z - FirstZ) / (Spacing * 8.0f));
        return (row < 8 ? row : 7) * 8 + (column < 8 ? column : 7);
    }

    void TestGround(const Terrain::TerrainHeightField& field, Terrain::TerrainSystem& system,
        const Terrain::TerrainHandle& handle, const Container::VariableArray<uint8_t>& weights)
    {
        Container::VariableArray<Math::Vector2> positions(1000);
        Container::VariableArray<Terrain::GroundSample> samples(1000);
        Random random;
        for (uint32_t i = 0; i < 1000; ++i)
        {
            float x = FirstX + random.Range(0.0f, 32.0f);
            float z = FirstZ + random.Range(0.0f, 32.0f);
            if (i < 80)
            {
                const uint32_t vertex = i * 13;
                x = FirstX + (vertex % GridSize) * Spacing;
                z = FirstZ + (vertex / GridSize) * Spacing;
            }
            else if (i >= 900)
            {
                x = FirstX - random.Range(0.1f, 2.0f);
            }
            positions[i] = Math::Vector2(x, z);
        }
        system.SampleGroundBatch(Container::Span<const Math::Vector2>(positions), Container::Span<Terrain::GroundSample>(samples));
        uint32_t unequal = 0;
        uint32_t wrongSurface = 0;
        uint32_t wrongTile = 0;
        uint32_t wrongValidity = 0;
        uint32_t valid = 0;
        for (uint32_t i = 0; i < 1000; ++i)
        {
            Terrain::GroundSample individual;
            const bool found = system.SampleGround(positions[i].x, positions[i].y, individual);
            if (!EqualSample(individual, samples[i])) ++unequal;
            if (found != (i < 900) || samples[i].bValid != (i < 900)) ++wrongValidity;
            if (found)
            {
                ++valid;
                if (individual.SurfaceId != ExpectedSurface(weights, positions[i].x, positions[i].y)) ++wrongSurface;
                if (individual.TileId != ExpectedTile(positions[i].x, positions[i].y)) ++wrongTile;
            }
        }
        std::cout << "地面標本: 一括と個別のビット一致=" << 1000 - unequal << "/1000 有効=" << valid
                  << " 範囲判定の誤り=" << wrongValidity << " 層の誤り=" << wrongSurface
                  << " タイルの誤り=" << wrongTile << "\n";
        Expect(unequal == 0, "一括と個別の全フィールドがビット単位で一致");
        Expect(wrongValidity == 0, "範囲外の標本は無効");
        Expect(wrongSurface == 0, "表面の層は最近の頂点の最大重みと一致し同点は小さい番号");
        Expect(wrongTile == 0, "タイル番号は 8 升ごとの行優先の番号と一致");

        uint32_t rayDifferences = 0;
        uint32_t rayHits = 0;
        for (uint32_t i = 0; i < 1000; ++i)
        {
            const Math::Ray ray(Math::Vector3(FirstX + random.Range(-2.0f, 34.0f), random.Range(2.0f, 8.0f),
                FirstZ + random.Range(-2.0f, 34.0f)),
                i % 2 == 0 ? Math::Vector3(0.0f, -1.0f, 0.0f)
                    : Math::Vector3(random.Range(-0.5f, 0.5f), -1.0f, random.Range(-0.5f, 0.5f)));
            const float limit = random.Range(2.0f, 15.0f);
            Terrain::TerrainRayHit expected;
            Terrain::GroundHit actual;
            const bool expectedHit = field.Raycast(ray, limit, expected);
            const bool actualHit = system.RaycastGround(ray, limit, actual);
            bool equal = expectedHit == actualHit && actual.bValid == actualHit;
            if (expectedHit && actualHit)
            {
                ++rayHits;
                equal = equal && EqualBits(expected.Distance, actual.Distance) && EqualBits(expected.Point, actual.Point)
                    && EqualBits(expected.Normal, actual.Normal) && actual.Terrain == handle
                    && actual.SurfaceId == field.SampleSurface(expected.Point.x, expected.Point.z)
                    && actual.TileId == field.GetTileId(expected.Point.x, expected.Point.z);
            }
            if (!equal) ++rayDifferences;
        }
        std::cout << "地面レイ: 一致=" << 1000 - rayDifferences << "/1000 当たり=" << rayHits << "\n";
        Expect(rayDifferences == 0, "地面レイの結果は高さ場のレイと一致");
    }

    void RecordPerformance(const Terrain::TerrainHeightField& field, Terrain::TerrainSystem& system)
    {
        constexpr uint32_t count = 100000;
        const float x = FirstX + 13.37f;
        const float z = FirstZ + 14.26f;
        Terrain::GroundSample sample;
        float checksum = 0.0f;
        const auto sampleStart = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < count; ++i)
        {
            system.SampleGround(x, z, sample);
            checksum += sample.Height;
        }
        const auto sampleEnd = std::chrono::steady_clock::now();
        const Math::Ray ray(Math::Vector3(x, field.SampleHeight(x, z) + 99.0f, z), Math::Vector3(0.0f, -1.0f, 0.0f));
        Terrain::TerrainRayHit hit;
        const auto rayStart = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < count; ++i)
        {
            if (field.Raycast(ray, 100.0f, hit)) checksum += hit.Distance;
        }
        const auto rayEnd = std::chrono::steady_clock::now();
        const double sampleUs = std::chrono::duration<double, std::micro>(sampleEnd - sampleStart).count() / count;
        const double rayUs = std::chrono::duration<double, std::micro>(rayEnd - rayStart).count() / count;
        std::cout << "性能記録(Debug): SampleGround=" << sampleUs << " us/回 Raycast(100 m)=" << rayUs
                  << " us/本 回数=" << count << " 検算=" << checksum << "\n";
    }
} // namespace

int main()
{
    std::cout.precision(10);
    const auto flat = MakeField(Surface::Flat);
    const auto slope = MakeField(Surface::Slope);
    Container::VariableArray<uint8_t> weights;
    const auto wave = MakeField(Surface::Wave, &weights);
    if (!flat || !slope || !wave) return 1;
    TestHeightsAndNormals(*flat, *slope, *wave);

    Cook::TerrainBuildResult cooked;
    if (!BuildTerrain(cooked)) return 1;
    const auto parsed = Asset::ParseCookedTerrain(Asset::AssetBlob::CopyBytes(Container::Span<const uint8_t>(cooked.NvtrnBytes)));
    if (!parsed.Succeeded())
    {
        std::cout << "失敗: NVTRN を解析できません: 状態=" << static_cast<uint32_t>(parsed.Status) << "\n";
        return 1;
    }
    Container::AnsiString error;
    const auto cookedField = Terrain::TerrainHeightField::CreateFromCookedTerrain(parsed.Terrain, error);
    if (!cookedField)
    {
        std::cout << "失敗: 焼いた高さ場を作れません: " << error.c_str() << "\n";
        return 1;
    }
    Container::VariableArray<Triangle> triangles;
    if (!CollectTriangles(cooked, triangles)) return 1;
    TestMeshHeight(*cookedField, triangles, MakePoints());
    TestRaycast(*wave);
    TestSlopeOverlap(*slope);
    TestOverlapContinuity(*wave);
    TestSweeps(*wave);

    Terrain::TerrainSystem system;
    const Terrain::TerrainHandle handle = system.RegisterTerrain(wave);
    Expect(handle.IsValid(), "高さ場を地面へ登録できる");
    TestGround(*wave, system, handle, weights);
    RecordPerformance(*wave, system);
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "TerrainHeightFieldQueryTest: 全検査に合格\n";
    return 0;
}
