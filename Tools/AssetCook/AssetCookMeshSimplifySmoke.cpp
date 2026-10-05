// CookMeshOptimizer(meshoptimizer の簡略化の境界)と CookMeshDag(LOD の階層の焼き込み)のスモーク。
// 緯度経度の球(約 2 万三角形)の極側を切り落として縁を作り、縁の頂点を固定して三角形を半分に簡略化したとき、
// 三角形の数・誤差・固定した頂点が消えないことを確かめる。あわせて、固定しなければ縁の頂点が消えること(対照)と、
// 不正な入力の扱いを確かめる。
// 閉じた球から LOD の階層を焼き、NVMESH v1 として読み直したうえで、誤差の単調性・境界球の包含・
// 誤差のしきい値で切った各メッシュに穴や割れ目が無いこと・フォールバックの段の三角形数を確かめる。

#include "Asset/AssetBlob.h"
#include "Asset/CookedMeshFormat.h"
#include "CookMeshDag.h"
#include "CookMeshOptimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    using namespace NorvesLib::Tools::AssetCook;
    using NorvesLib::Core::Container::AnsiString;
    using NorvesLib::Core::Container::VariableArray;

    constexpr uint32_t Segments = 144;
    constexpr uint32_t Rings = 72;
    // 北極側のこの本数の帯を切り落とす。切り口(緯度の輪)が縁になる。
    constexpr uint32_t CutRings = 3;
    constexpr double Pi = 3.14159265358979323846;

    // クックが --fallback-min-triangles で上げたフォールバックの段の下限（--check-package で指定。0 は指定なし）と、
    // その上限（Tools/AssetCook/CookMeshDag.cpp の FallbackMinOverrideMaxTriangles と同じ値）。
    uint32_t g_fallbackMinTriangles = 0;
    constexpr size_t FallbackMinOverrideMaxTriangles = 131072;

    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::printf("MESH_SIMPLIFY_SMOKE_FAIL %s\n", message);
            ++g_failures;
        }
    }

    struct Mesh
    {
        VariableArray<float> Positions; // float3 の連なり
        VariableArray<uint32_t> Indices;
        size_t VertexCount() const { return Positions.size() / 3; }
    };

    // 半径 1 の緯度経度の球。経度の継ぎ目と極は頂点を複製する(UV 付きの球と同じ作り)。
    // 北極側の CutRings 本の帯は三角形を作らない(頂点は残る)。
    Mesh MakeCutSphere()
    {
        Mesh mesh;
        for (uint32_t r = 0; r <= Rings; ++r)
        {
            const double theta = Pi * static_cast<double>(r) / Rings;
            // 極は x・z を厳密に 0 にし、継ぎ目の列は最初の列と同じ座標にする(位置が同じ頂点として溶接されるように)。
            const double sinTheta = (r == 0 || r == Rings) ? 0.0 : std::sin(theta);
            for (uint32_t s = 0; s <= Segments; ++s)
            {
                const double phi = 2.0 * Pi * static_cast<double>(s % Segments) / Segments;
                mesh.Positions.push_back(static_cast<float>(sinTheta * std::cos(phi)));
                mesh.Positions.push_back(static_cast<float>(std::cos(theta)));
                mesh.Positions.push_back(static_cast<float>(sinTheta * std::sin(phi)));
            }
        }
        for (uint32_t r = CutRings; r < Rings; ++r)
        {
            for (uint32_t s = 0; s < Segments; ++s)
            {
                const uint32_t a = r * (Segments + 1) + s;
                const uint32_t b = a + 1;
                const uint32_t c = a + (Segments + 1);
                const uint32_t d = c + 1;
                if (r != 0)
                {
                    mesh.Indices.push_back(a);
                    mesh.Indices.push_back(c);
                    mesh.Indices.push_back(b);
                }
                if (r != Rings - 1)
                {
                    mesh.Indices.push_back(b);
                    mesh.Indices.push_back(c);
                    mesh.Indices.push_back(d);
                }
            }
        }
        return mesh;
    }

    VariableArray<uint8_t> MarkReferenced(const VariableArray<uint32_t>& indices, size_t vertexCount)
    {
        VariableArray<uint8_t> used(vertexCount, 0);
        for (const uint32_t index : indices)
        {
            used[index] = 1;
        }
        return used;
    }

    // バウンディングボックスの最大の辺(meshoptimizer の誤差の尺度と同じ定義を独立に求める)。
    float MaxExtent(const Mesh& mesh)
    {
        float lo[3] = {1e30f, 1e30f, 1e30f};
        float hi[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t v = 0; v < mesh.VertexCount(); ++v)
        {
            for (size_t k = 0; k < 3; ++k)
            {
                lo[k] = std::min(lo[k], mesh.Positions[v * 3 + k]);
                hi[k] = std::max(hi[k], mesh.Positions[v * 3 + k]);
            }
        }
        return std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    }

    void RunSimplify()
    {
        const Mesh mesh = MakeCutSphere();
        const size_t vertexCount = mesh.VertexCount();
        const size_t inTriangles = mesh.Indices.size() / 3;
        std::printf("MESH_SIMPLIFY_INPUT vertices=%zu triangles=%zu\n", vertexCount, inTriangles);
        Check(inTriangles >= 19000 && inTriangles <= 21000, "入力の三角形が約 2 万ではない");

        const VariableArray<uint8_t> boundary =
            FindBoundaryVertices(mesh.Positions.data(), vertexCount, sizeof(float) * 3, mesh.Indices.data(), mesh.Indices.size());
        Check(boundary.size() == vertexCount, "縁のフラグの大きさが頂点数と合わない");

        // 縁は切り口の輪だけ。継ぎ目で複製された頂点も含め Segments + 1 個で、その行にだけある。
        size_t boundaryCount = 0;
        bool onlyCutRow = true;
        for (size_t v = 0; v < boundary.size(); ++v)
        {
            if (boundary[v] != 0)
            {
                ++boundaryCount;
                onlyCutRow = onlyCutRow && (v / (Segments + 1) == CutRings);
            }
        }
        Check(boundaryCount == Segments + 1, "縁の頂点の数が切り口の輪と合わない");
        Check(onlyCutRow, "縁の頂点が切り口の輪の外にある");

        CookSimplifyParams params;
        params.TargetIndexCount = (mesh.Indices.size() / 2 / 3) * 3;
        params.TargetErrorRelative = 0.05f;
        params.VertexLock = boundary;
        CookSimplifyResult locked;
        AnsiString error;
        Check(SimplifyMeshTriangles(mesh.Positions.data(), vertexCount, sizeof(float) * 3, mesh.Indices.data(),
                                    mesh.Indices.size(), params, locked, error),
              "固定ありの簡略化が失敗した");

        const size_t outTriangles = locked.Indices.size() / 3;
        std::printf("MESH_SIMPLIFY_LOCKED triangles=%zu error_relative=%.6f error_absolute=%.6f\n", outTriangles,
                    static_cast<double>(locked.ErrorRelative), static_cast<double>(locked.ErrorAbsolute));
        Check(locked.Indices.size() % 3 == 0, "出力の索引数が 3 の倍数ではない");
        Check(locked.Indices.size() <= params.TargetIndexCount, "目標の索引数を超えている");
        Check(outTriangles * 10 >= (params.TargetIndexCount / 3) * 9, "半分近くまで減っていない");

        bool inRange = true;
        bool nonDegenerate = true;
        for (size_t i = 0; i < locked.Indices.size(); i += 3)
        {
            const uint32_t a = locked.Indices[i];
            const uint32_t b = locked.Indices[i + 1];
            const uint32_t c = locked.Indices[i + 2];
            inRange = inRange && a < vertexCount && b < vertexCount && c < vertexCount;
            nonDegenerate = nonDegenerate && a != b && b != c && a != c;
        }
        Check(inRange, "出力の索引が頂点数の範囲外");
        Check(nonDegenerate, "出力に縮退した三角形がある");

        // 誤差: 正で、小さく、絶対値は相対値 × 外形の大きさに合う。
        Check(locked.ErrorRelative > 0.0f, "誤差が 0(何も簡略化していない)");
        Check(locked.ErrorRelative < 0.02f, "誤差(相対)が大きすぎる");
        const float expectedAbsolute = locked.ErrorRelative * MaxExtent(mesh);
        Check(std::fabs(locked.ErrorAbsolute - expectedAbsolute) <= expectedAbsolute * 1e-4f + 1e-9f,
              "誤差(絶対)が相対 × 外形の大きさと合わない");

        // 簡略化した三角形の重心が球面から大きく離れない(球の弦のへこみ以上に痩せていない)。
        float worstSag = 0.0f;
        for (size_t i = 0; i < locked.Indices.size(); i += 3)
        {
            float centroid[3] = {0.0f, 0.0f, 0.0f};
            for (size_t k = 0; k < 3; ++k)
            {
                for (size_t e = 0; e < 3; ++e)
                {
                    centroid[k] += mesh.Positions[locked.Indices[i + e] * 3 + k] / 3.0f;
                }
            }
            const float radius = std::sqrt(centroid[0] * centroid[0] + centroid[1] * centroid[1] + centroid[2] * centroid[2]);
            worstSag = std::max(worstSag, 1.0f - radius);
        }
        std::printf("MESH_SIMPLIFY_SAG worst=%.6f\n", static_cast<double>(worstSag));
        Check(worstSag < 0.01f, "簡略化した面が球面から離れすぎている");

        // 固定した頂点は、入力で使われていたものが出力にも残る(位置は元の頂点バッファのまま)。
        const VariableArray<uint8_t> usedIn = MarkReferenced(mesh.Indices, vertexCount);
        const VariableArray<uint8_t> usedLocked = MarkReferenced(locked.Indices, vertexCount);
        size_t lockedKept = 0;
        size_t lockedLost = 0;
        for (size_t v = 0; v < vertexCount; ++v)
        {
            if (boundary[v] != 0 && usedIn[v] != 0)
            {
                (usedLocked[v] != 0 ? lockedKept : lockedLost) += 1;
            }
        }
        std::printf("MESH_SIMPLIFY_LOCK kept=%zu lost=%zu\n", lockedKept, lockedLost);
        Check(lockedKept == Segments + 1 && lockedLost == 0, "固定した頂点が出力から消えた");

        // 対照: 固定しなければ縁の頂点が消える(固定の検査が何も区別しないものになっていない)。
        CookSimplifyParams freeParams = params;
        freeParams.VertexLock.clear();
        CookSimplifyResult unlocked;
        Check(SimplifyMeshTriangles(mesh.Positions.data(), vertexCount, sizeof(float) * 3, mesh.Indices.data(),
                                    mesh.Indices.size(), freeParams, unlocked, error),
              "固定なしの簡略化が失敗した");
        const VariableArray<uint8_t> usedFree = MarkReferenced(unlocked.Indices, vertexCount);
        size_t freeLost = 0;
        for (size_t v = 0; v < vertexCount; ++v)
        {
            if (boundary[v] != 0 && usedIn[v] != 0 && usedFree[v] == 0)
            {
                ++freeLost;
            }
        }
        std::printf("MESH_SIMPLIFY_CONTROL unlocked_lost=%zu\n", freeLost);
        Check(freeLost > 0, "固定しない対照でも縁の頂点が消えなかった");

        // 固定フラグは 0 でなければ固定(1 以外の値でも固定される)。上流は最下位ビットだけを見るので、2 と 255 で確かめる。
        for (const uint8_t flagValue : {static_cast<uint8_t>(2), static_cast<uint8_t>(255)})
        {
            CookSimplifyParams valueParams = params;
            for (size_t v = 0; v < valueParams.VertexLock.size(); ++v)
            {
                valueParams.VertexLock[v] = boundary[v] != 0 ? flagValue : static_cast<uint8_t>(0);
            }
            CookSimplifyResult valueResult;
            Check(SimplifyMeshTriangles(mesh.Positions.data(), vertexCount, sizeof(float) * 3, mesh.Indices.data(),
                                        mesh.Indices.size(), valueParams, valueResult, error),
                  "固定値が 1 以外の簡略化が失敗した");
            const VariableArray<uint8_t> usedValue = MarkReferenced(valueResult.Indices, vertexCount);
            size_t valueLost = 0;
            for (size_t v = 0; v < vertexCount; ++v)
            {
                if (boundary[v] != 0 && usedIn[v] != 0 && usedValue[v] == 0)
                {
                    ++valueLost;
                }
            }
            std::printf("MESH_SIMPLIFY_LOCK_VALUE flag=%u lost=%zu\n", static_cast<unsigned>(flagValue), valueLost);
            Check(valueLost == 0, "固定値が 1 以外の頂点が出力から消えた");
            Check(valueResult.Indices.size() == locked.Indices.size(), "固定値が 1 の結果と 1 以外の結果が違う");
        }
    }

    using NorvesLib::Core::Asset::CookedMeshCluster;
    using NorvesLib::Core::Asset::CookedMeshData;
    using NorvesLib::Core::Asset::CookedMeshVertex;

    struct VertexMesh
    {
        VariableArray<CookedMeshVertex> Vertices;
        VariableArray<uint32_t> Indices;
    };

    // 切り口のない閉じた緯度経度の球。法線 = 位置、UV = 経度・緯度の格子。経度の継ぎ目と極は UV が違う頂点を複製する
    // (位置は同じ。溶接は位置と属性が全部同じものだけ)。
    VertexMesh MakeClosedSphere(uint32_t segments, uint32_t rings)
    {
        VertexMesh mesh;
        for (uint32_t r = 0; r <= rings; ++r)
        {
            const double theta = Pi * static_cast<double>(r) / rings;
            const double sinTheta = (r == 0 || r == rings) ? 0.0 : std::sin(theta);
            for (uint32_t s = 0; s <= segments; ++s)
            {
                const double phi = 2.0 * Pi * static_cast<double>(s % segments) / segments;
                CookedMeshVertex vertex;
                vertex.Position.X = static_cast<float>(sinTheta * std::cos(phi)) + 0.0f;
                vertex.Position.Y = static_cast<float>(std::cos(theta)) + 0.0f;
                vertex.Position.Z = static_cast<float>(sinTheta * std::sin(phi)) + 0.0f;
                vertex.Normal = vertex.Position;
                vertex.TexCoord.U = static_cast<float>(s) / static_cast<float>(segments);
                vertex.TexCoord.V = static_cast<float>(r) / static_cast<float>(rings);
                mesh.Vertices.push_back(vertex);
            }
        }
        for (uint32_t r = 0; r < rings; ++r)
        {
            for (uint32_t s = 0; s < segments; ++s)
            {
                const uint32_t a = r * (segments + 1) + s;
                const uint32_t b = a + 1;
                const uint32_t c = a + (segments + 1);
                const uint32_t d = c + 1;
                if (r != 0)
                {
                    mesh.Indices.push_back(a);
                    mesh.Indices.push_back(c);
                    mesh.Indices.push_back(b);
                }
                if (r != rings - 1)
                {
                    mesh.Indices.push_back(b);
                    mesh.Indices.push_back(c);
                    mesh.Indices.push_back(d);
                }
            }
        }
        return mesh;
    }

    struct PositionKey
    {
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Z = 0;

        bool operator<(const PositionKey& other) const
        {
            if (X != other.X)
            {
                return X < other.X;
            }
            return Y != other.Y ? Y < other.Y : Z < other.Z;
        }
        bool operator==(const PositionKey& other) const
        {
            return X == other.X && Y == other.Y && Z == other.Z;
        }
    };

    PositionKey MakePositionKey(const CookedMeshVertex& vertex)
    {
        PositionKey key;
        const float x = vertex.Position.X + 0.0f;
        const float y = vertex.Position.Y + 0.0f;
        const float z = vertex.Position.Z + 0.0f;
        std::memcpy(&key.X, &x, sizeof(float));
        std::memcpy(&key.Y, &y, sizeof(float));
        std::memcpy(&key.Z, &z, sizeof(float));
        return key;
    }

    struct EdgeReport
    {
        size_t Edges = 0;
        // 向きのある辺と逆向きの辺の数が合わない辺(穴・割れ目、向きの食い違い)
        size_t Holes = 0;
        // 向きは釣り合っているが、3 つ以上の三角形が共有する辺(2 枚の面が辺で接する「つまみ」)
        size_t Pinched = 0;
    };

    // 三角形リスト(頂点番号は vertices への絶対の番号)の辺を調べる。位置が同じ頂点は同じ点として数える。
    // 閉じた向き付き多様体なら、すべての辺がちょうど 2 つの三角形に共有され、向きのある辺は 1 度ずつ現れる。
    EdgeReport AnalyzeEdges(const VariableArray<CookedMeshVertex>& vertices, const VariableArray<uint32_t>& triangles)
    {
        VariableArray<PositionKey> keys;
        keys.reserve(vertices.size());
        for (const CookedMeshVertex& vertex : vertices)
        {
            keys.push_back(MakePositionKey(vertex));
        }
        VariableArray<PositionKey> sortedKeys = keys;
        std::sort(sortedKeys.begin(), sortedKeys.end());
        sortedKeys.erase(std::unique(sortedKeys.begin(), sortedKeys.end()), sortedKeys.end());
        const auto pointOf = [&](uint32_t vertex) {
            return static_cast<uint32_t>(std::lower_bound(sortedKeys.begin(), sortedKeys.end(), keys[vertex]) -
                                         sortedKeys.begin());
        };

        VariableArray<uint64_t> directed;
        directed.reserve(triangles.size());
        for (size_t i = 0; i < triangles.size(); i += 3)
        {
            const uint32_t p[3] = {pointOf(triangles[i]), pointOf(triangles[i + 1]), pointOf(triangles[i + 2])};
            for (size_t e = 0; e < 3; ++e)
            {
                const uint32_t from = p[e];
                const uint32_t to = p[(e + 1) % 3];
                if (from != to)
                {
                    directed.push_back((static_cast<uint64_t>(from) << 32) | to);
                }
            }
        }
        std::sort(directed.begin(), directed.end());

        EdgeReport report;
        for (size_t i = 0; i < directed.size();)
        {
            size_t j = i + 1;
            while (j < directed.size() && directed[j] == directed[i])
            {
                ++j;
            }
            const uint32_t from = static_cast<uint32_t>(directed[i] >> 32);
            const uint32_t to = static_cast<uint32_t>(directed[i] & 0xffffffffull);
            const uint64_t reverse = (static_cast<uint64_t>(to) << 32) | from;
            const auto reverseRange = std::equal_range(directed.begin(), directed.end(), reverse);
            const size_t forwardCount = j - i;
            const size_t reverseCount = static_cast<size_t>(reverseRange.second - reverseRange.first);
            if (forwardCount != reverseCount)
            {
                ++report.Holes;
            }
            else if (forwardCount > 1)
            {
                ++report.Pinched;
            }
            if (from < to)
            {
                ++report.Edges;
            }
            i = j;
        }
        return report;
    }

    // 閉じた向き付き多様体か(穴も、つまみも無い)。outBadEdges は穴とつまみの辺の数。
    bool IsWatertight(const VariableArray<CookedMeshVertex>& vertices, const VariableArray<uint32_t>& triangles,
                      size_t& outUndirectedEdges, size_t& outBadEdges)
    {
        const EdgeReport report = AnalyzeEdges(vertices, triangles);
        outUndirectedEdges = report.Edges;
        outBadEdges = report.Holes + report.Pinched;
        return outBadEdges == 0 && report.Edges != 0;
    }

    // 誤差のしきい値で切る: 自分の誤差 <= しきい値 < 親の誤差 のクラスタの三角形(頂点は絶対の番号)
    VariableArray<uint32_t> CutTriangles(const CookedMeshData& mesh, float threshold)
    {
        VariableArray<uint32_t> triangles;
        for (const CookedMeshCluster& cluster : mesh.Clusters)
        {
            if (cluster.LODError <= threshold && threshold < cluster.ParentError)
            {
                for (uint32_t k = 0; k < cluster.IndexCount; ++k)
                {
                    triangles.push_back(cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + k]);
                }
            }
        }
        return triangles;
    }

    double SphereDistance(const NorvesLib::Core::Asset::CookedMeshFloat3& a, const NorvesLib::Core::Asset::CookedMeshFloat3& b)
    {
        const double dx = static_cast<double>(a.X) - static_cast<double>(b.X);
        const double dy = static_cast<double>(a.Y) - static_cast<double>(b.Y);
        const double dz = static_cast<double>(a.Z) - static_cast<double>(b.Z);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // 球の四角形(2 三角形)ごとに頂点を複製し、UV を四角形ごとに別の島へ置く。位置は変えない。
    // 継ぎ目だらけのスキャン資産(UV の島が細かい)を模す。継ぎ目を保ったままでは頂点がほとんど動かせない。
    VertexMesh MakeIslandSphere(const VertexMesh& base)
    {
        VertexMesh mesh;
        for (size_t i = 0; i < base.Indices.size(); i += 6)
        {
            // MakeClosedSphere は四角形ごとに 2 三角形(両方ある行)を並べる。端の行は 1 三角形だけなので、
            // 三角形の組ごとに「前の三角形と頂点を共有するか」で四角形をまとめず、単純に 2 三角形ずつ島にする。
            const size_t triangleCount = std::min<size_t>(6, base.Indices.size() - i) / 3;
            const float islandU = static_cast<float>((i / 6) % 61) / 61.0f;
            const float islandV = static_cast<float>((i / 6) % 59) / 59.0f;
            VariableArray<uint32_t> remap;
            VariableArray<uint32_t> source;
            for (size_t t = 0; t < triangleCount * 3; ++t)
            {
                const uint32_t original = base.Indices[i + t];
                size_t slot = 0;
                while (slot < source.size() && source[slot] != original)
                {
                    ++slot;
                }
                if (slot == source.size())
                {
                    source.push_back(original);
                    CookedMeshVertex vertex = base.Vertices[original];
                    vertex.TexCoord.U = islandU + vertex.TexCoord.U * 0.01f;
                    vertex.TexCoord.V = islandV + vertex.TexCoord.V * 0.01f;
                    remap.push_back(static_cast<uint32_t>(mesh.Vertices.size()));
                    mesh.Vertices.push_back(vertex);
                }
                mesh.Indices.push_back(remap[slot]);
            }
        }
        return mesh;
    }

    void CheckDagMesh(const CookedMeshData& mesh, const VertexMesh* source, size_t sourceTriangles);

    void RunDagCase(const char* caseName, const VertexMesh& source, bool bExpectPermissive)
    {
        std::printf("MESH_DAG_CASE %s\n", caseName);
        const size_t sourceTriangles = source.Indices.size() / 3;
        Check(sourceTriangles >= 19000 && sourceTriangles <= 21000, "階層の入力の三角形が約 2 万ではない");

        // 入力そのものが閉じている(対照: 以降の切り口の検査が、閉じていない入力を通さない)
        size_t edges = 0;
        size_t badEdges = 0;
        Check(IsWatertight(source.Vertices, source.Indices, edges, badEdges), "入力の球が閉じていない");

        CookMeshDagResult baked;
        AnsiString error;
        Check(BakeMeshLodDag(source.Vertices.data(), source.Vertices.size(), source.Indices.data(), source.Indices.size(),
                             baked, error),
              "LOD の階層の焼き込みが失敗した");
        const CookMeshDagStats& stats = baked.Stats;
        std::printf("MESH_DAG_BAKE source_triangles=%u welded_vertices=%u levels=%u clusters=%u groups=%u roots=%u "
                    "permissive_groups=%u rejected_groups=%u fallback_triangles=%u fallback_target=%u\n",
                    stats.SourceTriangles, stats.WeldedVertices, stats.LODLevelCount, stats.ClusterCount, stats.GroupCount,
                    stats.RootClusterCount, stats.PermissiveGroupCount, stats.RejectedGroupCount, stats.FallbackTriangles,
                    stats.FallbackTargetTriangles);
        std::printf("MESH_DAG_LEVELS");
        for (const uint32_t levelTriangles : stats.LevelTriangles)
        {
            std::printf(" %u", levelTriangles);
        }
        std::printf("\n");
        Check(stats.SourceTriangles == sourceTriangles, "溶接のあとの三角形数が入力と違う");
        Check(stats.LODLevelCount >= 5, "階層の段数が少なすぎる");
        Check(stats.bReachedSingleRoot && stats.RootClusterCount == 1, "閉じた球の階層が 1 つの根まで縮まらなかった");
        // 継ぎ目だらけの入力は、継ぎ目を保ったままでは半分に届かず、許容モードの簡略化が使われる
        Check(!bExpectPermissive || stats.PermissiveGroupCount > 0, "継ぎ目だらけの入力で許容モードが使われなかった");
        // 非多様体の辺を作らない簡略化が見つからず、簡略化を諦めたグループが無い
        Check(stats.RejectedGroupCount == 0, "簡略化を諦めたグループがある");

        // NVMESH v1 として書き、読み直す。読み込みの検査(根の条件・グループの整合・境界球の包含など)も通る。
        VariableArray<uint8_t> bytes;
        Check(NorvesLib::Core::Asset::SerializeCookedMeshV1(baked.Output, bytes), "NVMESH v1 の書き出しに失敗した");
        const NorvesLib::Core::Container::Span<const uint8_t> span(bytes.data(), bytes.size());
        const auto parsed =
            NorvesLib::Core::Asset::ParseCookedMesh(NorvesLib::Core::Asset::AssetBlob::CopyBytes(span, "AssetCook DAG smoke"));
        Check(parsed.Succeeded(), "焼いた NVMESH v1 を読み込めなかった");
        if (!parsed.Succeeded())
        {
            std::printf("MESH_DAG_PARSE_STATUS %d\n", static_cast<int>(parsed.Status));
            return;
        }
        const CookedMeshData& mesh = parsed.Mesh;
        Check(mesh.FormatMajor == 1 && mesh.LODLevelCount == stats.LODLevelCount, "読み直した段数が焼いた値と違う");
        CheckDagMesh(mesh, &source, sourceTriangles);

        // 不正な入力は拒否する
        CookMeshDagResult rejected;
        Check(!BakeMeshLodDag(source.Vertices.data(), source.Vertices.size(), source.Indices.data(),
                              source.Indices.size() - 1, rejected, error),
              "3 の倍数でない索引数を受け付けた");
        Check(!BakeMeshLodDag(nullptr, 0, source.Indices.data(), source.Indices.size(), rejected, error),
              "空の頂点を受け付けた");
    }

    // 焼いて読み直したメッシュの性質を確かめる。source は入力(属性が保たれたかの確認に使う。null なら省く)で、
    // sourceTriangles は段 0 の三角形数。切り口の辺は、穴(割れ目)も「つまみ」(3 つ以上の三角形が 1 本の辺を共有する)も
    // 無く、すべてちょうど 2 つの三角形に共有されていなければ失敗とする。
    void CheckDagMesh(const CookedMeshData& mesh, const VertexMesh* source, size_t sourceTriangles)
    {

        // (0) 作ったグループの番号: 最も細かい段以外の全クラスタが、1つ細かい段のグループの番号を持つ
        //     （読み込みが、そのグループの境界球・誤差と自分の値の一致を検査済み）。最も細かい段は持たない
        bool bSourceGroups = true;
        for (const CookedMeshCluster& cluster : mesh.Clusters)
        {
            const bool bHasSource = cluster.SourceGroupId != NorvesLib::Core::Asset::CookedMeshFormatV1::InvalidGroupId;
            bSourceGroups = bSourceGroups && bHasSource == (cluster.LODLevel != 0) &&
                            (!bHasSource || (cluster.SourceGroupId < mesh.Groups.size() &&
                                             mesh.Groups[cluster.SourceGroupId].LODLevel + 1 == cluster.LODLevel));
        }
        Check(bSourceGroups, "クラスタが作ったグループの番号を持たない、または指す先が1つ細かい段のグループではない");

        // (1) 誤差が子から親へ単調: 根でないクラスタの親の誤差は自分の誤差以上、グループの誤差はメンバの誤差以上。
        //     段が上がると誤差が増える(最も細かい段は 0、根は最大)ことも確かめる。
        bool bMonotone = true;
        bool bGroupsAbove = true;
        float maxGroupError = 0.0f;
        for (const CookedMeshCluster& cluster : mesh.Clusters)
        {
            if (!cluster.bIsRoot && cluster.ParentError < cluster.LODError)
            {
                bMonotone = false;
            }
            if (cluster.LODLevel == 0 && cluster.LODError != 0.0f)
            {
                bMonotone = false;
            }
        }
        for (const auto& group : mesh.Groups)
        {
            maxGroupError = std::max(maxGroupError, group.Error);
            for (uint32_t m = group.ClusterOffset; m < group.ClusterOffset + group.ClusterCount; ++m)
            {
                if (mesh.Clusters[m].LODError > group.Error)
                {
                    bMonotone = false;
                }
            }
            // グループの親にあたる次の段のクラスタは、グループの誤差と境界球を自分の値として持つ
            size_t parentCount = 0;
            for (const CookedMeshCluster& cluster : mesh.Clusters)
            {
                if (cluster.LODLevel == group.LODLevel + 1 && cluster.LODError == group.Error &&
                    cluster.BoundsRadius == group.BoundsRadius && cluster.BoundsCenter.X == group.BoundsCenter.X &&
                    cluster.BoundsCenter.Y == group.BoundsCenter.Y && cluster.BoundsCenter.Z == group.BoundsCenter.Z)
                {
                    ++parentCount;
                }
            }
            if (parentCount == 0)
            {
                bGroupsAbove = false;
            }
        }
        std::printf("MESH_DAG_ERROR groups=%zu max_group_error=%.6f\n", mesh.Groups.size(),
                    static_cast<double>(maxGroupError));
        Check(bMonotone, "誤差が子から親へ単調ではない");
        Check(bGroupsAbove, "グループの親にあたる次の段のクラスタが見つからない");
        Check(maxGroupError > 0.0f, "階層の誤差がすべて 0(簡略化していない)");

        // (2) 親の境界球が子の境界球を包む。あわせて、クラスタの三角形の頂点が自分の境界球に収まる(段 0 は頂点の球、
        //     それより上は親を作ったグループの球)。
        bool bSpheresContain = true;
        bool bVerticesInside = true;
        for (const auto& group : mesh.Groups)
        {
            for (uint32_t m = group.ClusterOffset; m < group.ClusterOffset + group.ClusterCount; ++m)
            {
                const CookedMeshCluster& member = mesh.Clusters[m];
                const double needed = SphereDistance(member.BoundsCenter, group.BoundsCenter) + member.BoundsRadius;
                if (needed > static_cast<double>(group.BoundsRadius) * (1.0 + 1.0e-6))
                {
                    bSpheresContain = false;
                }
            }
        }
        for (const CookedMeshCluster& cluster : mesh.Clusters)
        {
            for (uint32_t k = 0; k < cluster.IndexCount; ++k)
            {
                const CookedMeshVertex& vertex = mesh.Vertices[cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + k]];
                if (SphereDistance(vertex.Position, cluster.BoundsCenter) >
                    static_cast<double>(cluster.BoundsRadius) * (1.0 + 1.0e-6) + 1.0e-9)
                {
                    bVerticesInside = false;
                }
            }
        }
        Check(bSpheresContain, "親の境界球が子の境界球を包んでいない");
        Check(bVerticesInside, "クラスタの頂点が自分の境界球から出ている");

        // 属性を保つ: 出力の頂点はすべて入力の頂点(位置・法線・UV が同じ)のどれかと一致する。
        struct AttributeKey
        {
            uint32_t Bits[8];
            bool operator<(const AttributeKey& other) const
            {
                return std::memcmp(Bits, other.Bits, sizeof(Bits)) < 0;
            }
        };
        const auto makeAttributeKey = [](const CookedMeshVertex& vertex) {
            AttributeKey key;
            const float values[8] = {vertex.Position.X + 0.0f, vertex.Position.Y + 0.0f, vertex.Position.Z + 0.0f,
                                     vertex.Normal.X + 0.0f,   vertex.Normal.Y + 0.0f,   vertex.Normal.Z + 0.0f,
                                     vertex.TexCoord.U + 0.0f, vertex.TexCoord.V + 0.0f};
            std::memcpy(key.Bits, values, sizeof(values));
            return key;
        };
        if (source != nullptr)
        {
            VariableArray<AttributeKey> sourceKeys;
            for (const CookedMeshVertex& vertex : source->Vertices)
            {
                sourceKeys.push_back(makeAttributeKey(vertex));
            }
            std::sort(sourceKeys.begin(), sourceKeys.end());
            bool bAttributesKept = true;
            for (const CookedMeshVertex& vertex : mesh.Vertices)
            {
                if (!std::binary_search(sourceKeys.begin(), sourceKeys.end(), makeAttributeKey(vertex)))
                {
                    bAttributesKept = false;
                    break;
                }
            }
            Check(bAttributesKept, "出力の頂点が入力の頂点の属性と一致しない");
        }

        // (3) 誤差のしきい値を 5 通りに変えて切り、どの切り口も閉じている(すべての辺がちょうど 2 つの三角形に共有される)。
        //     しきい値は、グループの誤差の昇順の 1/6 〜 5/6 の位置の値。両端(段 0 だけ・根だけ)も確かめる。
        VariableArray<float> groupErrors;
        for (const auto& group : mesh.Groups)
        {
            groupErrors.push_back(group.Error);
        }
        std::sort(groupErrors.begin(), groupErrors.end());
        groupErrors.erase(std::unique(groupErrors.begin(), groupErrors.end()), groupErrors.end());
        Check(groupErrors.size() >= 6, "異なる誤差の値が少なすぎてしきい値を 5 通り取れない");

        VariableArray<float> thresholds;
        for (size_t k = 1; k <= 5; ++k)
        {
            thresholds.push_back(groupErrors[(k * (groupErrors.size() - 1)) / 6]);
        }
        thresholds.push_back(0.0f);
        thresholds.push_back(groupErrors.back());
        size_t previousTriangles = std::numeric_limits<size_t>::max();
        size_t distinctCounts = 0;
        for (size_t t = 0; t < thresholds.size(); ++t)
        {
            const VariableArray<uint32_t> cut = CutTriangles(mesh, thresholds[t]);
            const EdgeReport report = AnalyzeEdges(mesh.Vertices, cut);
            const bool bClosed = report.Edges != 0 && report.Holes == 0 && report.Pinched == 0;
            std::printf("MESH_DAG_CUT threshold=%.6f triangles=%zu edges=%zu holes=%zu pinched=%zu\n",
                        static_cast<double>(thresholds[t]), cut.size() / 3, report.Edges, report.Holes, report.Pinched);
            Check(bClosed, "誤差のしきい値で切ったメッシュに穴か割れ目がある");
            if (t < 5)
            {
                // 5 通りは昇順なので、しきい値が上がるほど三角形は増えない
                Check(cut.size() / 3 <= previousTriangles, "しきい値を上げたのに三角形が増えた");
                if (cut.size() / 3 != previousTriangles)
                {
                    ++distinctCounts;
                }
                previousTriangles = cut.size() / 3;
            }
        }
        Check(distinctCounts >= 3, "しきい値を変えても切り口がほとんど変わらない");
        Check(CutTriangles(mesh, 0.0f).size() / 3 == sourceTriangles, "しきい値 0 の切り口が段 0 の三角形数と違う");

        // (4) フォールバックの段: 三角形数が 1 以上で、目標(全体の 1/16。クックが fallback_min_triangles で下限を上げた
        //     ときは、その下限の方)以下。粗すぎない(目標の 1/4 以上)。閉じている。
        const size_t fallbackTriangles = mesh.FallbackIndexCount / 3;
        const size_t fallbackTarget = std::max<size_t>(sourceTriangles / 16,
                                                       std::min<size_t>(g_fallbackMinTriangles, FallbackMinOverrideMaxTriangles));
        Check(fallbackTriangles >= 1, "フォールバックの段が空");
        Check(fallbackTriangles <= fallbackTarget, "フォールバックの段の三角形が目標(全体の 1/16 か指定した下限)を超える");
        Check(fallbackTriangles >= sourceTriangles / 64, "フォールバックの段が粗すぎる");
        VariableArray<uint32_t> fallback;
        for (uint32_t k = 0; k < mesh.FallbackIndexCount; ++k)
        {
            fallback.push_back(mesh.Indices[mesh.FallbackIndexOffset + k]);
        }
        const EdgeReport fallbackReport = AnalyzeEdges(mesh.Vertices, fallback);
        std::printf("MESH_DAG_FALLBACK_EDGES edges=%zu holes=%zu pinched=%zu\n", fallbackReport.Edges, fallbackReport.Holes, fallbackReport.Pinched);
        Check(fallbackReport.Edges != 0 && fallbackReport.Holes == 0 && fallbackReport.Pinched == 0,
              "フォールバックの段に穴か割れ目がある");
        std::printf("MESH_DAG_FALLBACK triangles=%zu error=%.6f\n", fallbackTriangles,
                    static_cast<double>(mesh.FallbackError));
    }

    // 座標の単位(1024 倍)を変えても、属性の重みの効き方が変わらず、同じ階層になる(2 のべき乗倍は浮動小数で厳密)
    void RunDagScaleInvariance(const VertexMesh& source)
    {
        VertexMesh scaled = source;
        for (CookedMeshVertex& vertex : scaled.Vertices)
        {
            vertex.Position.X *= 1024.0f;
            vertex.Position.Y *= 1024.0f;
            vertex.Position.Z *= 1024.0f;
        }
        CookMeshDagResult baseline;
        CookMeshDagResult enlarged;
        AnsiString error;
        Check(BakeMeshLodDag(source.Vertices.data(), source.Vertices.size(), source.Indices.data(),
                             source.Indices.size(), baseline, error),
              "LOD の階層の焼き込みが失敗した(基準)");
        Check(BakeMeshLodDag(scaled.Vertices.data(), scaled.Vertices.size(), scaled.Indices.data(),
                             scaled.Indices.size(), enlarged, error),
              "LOD の階層の焼き込みが失敗した(1024 倍)");
        std::printf("MESH_DAG_SCALE levels=%u/%u clusters=%u/%u permissive=%u/%u\n", baseline.Stats.LODLevelCount,
                    enlarged.Stats.LODLevelCount, baseline.Stats.ClusterCount, enlarged.Stats.ClusterCount,
                    baseline.Stats.PermissiveGroupCount, enlarged.Stats.PermissiveGroupCount);
        Check(baseline.Stats.LevelTriangles == enlarged.Stats.LevelTriangles,
              "座標を拡大すると、段ごとの三角形数が変わった(属性の重みが単位に左右される)");
        Check(baseline.Stats.PermissiveGroupCount == enlarged.Stats.PermissiveGroupCount,
              "座標を拡大すると、許容モードのグループ数が変わった");
    }

    void RunDag()
    {
        const VertexMesh sphere = MakeClosedSphere(144, 72);
        RunDagCase("smooth_sphere", sphere, false);
        const VertexMesh islandSphere = MakeIslandSphere(sphere);
        RunDagCase("island_sphere", islandSphere, true);
        RunDagScaleInvariance(islandSphere);
    }

    void RunInvalidInputs()
    {
        const Mesh mesh = MakeCutSphere();
        const size_t vertexCount = mesh.VertexCount();
        CookSimplifyParams params;
        params.TargetIndexCount = 300;
        CookSimplifyResult result;
        AnsiString error;

        Check(!SimplifyMeshTriangles(nullptr, vertexCount, 12, mesh.Indices.data(), mesh.Indices.size(), params, result, error),
              "null の頂点位置を受け付けた");
        Check(!SimplifyMeshTriangles(mesh.Positions.data(), vertexCount, 8, mesh.Indices.data(), mesh.Indices.size(), params,
                                     result, error),
              "12 バイト未満の stride を受け付けた");
        Check(!SimplifyMeshTriangles(mesh.Positions.data(), vertexCount, 12, mesh.Indices.data(), mesh.Indices.size() - 1,
                                     params, result, error),
              "3 の倍数でない索引数を受け付けた");

        params.TargetIndexCount = 301;
        Check(!SimplifyMeshTriangles(mesh.Positions.data(), vertexCount, 12, mesh.Indices.data(), mesh.Indices.size(), params,
                                     result, error),
              "3 の倍数でない目標を受け付けた");

        params.TargetIndexCount = 300;
        params.VertexLock.assign(vertexCount - 1, 0);
        Check(!SimplifyMeshTriangles(mesh.Positions.data(), vertexCount, 12, mesh.Indices.data(), mesh.Indices.size(), params,
                                     result, error),
              "大きさの合わない固定フラグを受け付けた");
        params.VertexLock.clear();

        VariableArray<uint32_t> outOfRange = mesh.Indices;
        outOfRange[0] = static_cast<uint32_t>(vertexCount);
        Check(!SimplifyMeshTriangles(mesh.Positions.data(), vertexCount, 12, outOfRange.data(), outOfRange.size(), params,
                                     result, error),
              "範囲外の索引を受け付けた");
    }
}

namespace
{
    // クックしたパッケージ(NVPKG。圧縮なしの 1 エントリ)の中の NVMESH v1 を取り出して読み、焼いた階層の性質を確かめる。
    // 実資産(岩など)を AssetCook で焼いたあとに、同じ検査を掛けるための入口。
    void RunCheckPackage(const char* path)
    {
        std::printf("MESH_DAG_CASE package %s\n", path);
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0)
        {
            file = nullptr;
        }
        Check(file != nullptr, "パッケージを開けなかった");
        if (file == nullptr)
        {
            return;
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        VariableArray<uint8_t> bytes(static_cast<size_t>(std::max(fileSize, 0L)), 0);
        const size_t readSize = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
        std::fclose(file);
        Check(!bytes.empty() && readSize == bytes.size(), "パッケージを読み切れなかった");
        if (bytes.empty() || readSize != bytes.size())
        {
            return;
        }

        static constexpr uint8_t magic[8] = {'N', 'V', 'M', 'E', 'S', 'H', 'v', '1'};
        size_t payloadOffset = bytes.size();
        for (size_t offset = 0; offset + 256 <= bytes.size(); offset += 8)
        {
            if (std::memcmp(bytes.data() + offset, magic, sizeof(magic)) == 0)
            {
                payloadOffset = offset;
                break;
            }
        }
        Check(payloadOffset != bytes.size(), "パッケージに NVMESH v1 が見つからない");
        if (payloadOffset == bytes.size())
        {
            return;
        }
        uint64_t payloadSize = 0;
        std::memcpy(&payloadSize, bytes.data() + payloadOffset + 40, sizeof(payloadSize));
        Check(payloadSize >= 256 && payloadOffset + payloadSize <= bytes.size(), "NVMESH v1 の大きさが不正");
        if (payloadSize < 256 || payloadOffset + payloadSize > bytes.size())
        {
            return;
        }

        const NorvesLib::Core::Container::Span<const uint8_t> span(bytes.data() + payloadOffset,
                                                                   static_cast<size_t>(payloadSize));
        const auto parsed = NorvesLib::Core::Asset::ParseCookedMesh(
            NorvesLib::Core::Asset::AssetBlob::CopyBytes(span, "AssetCook DAG package check"));
        Check(parsed.Succeeded(), "パッケージの NVMESH v1 を読み込めなかった");
        if (!parsed.Succeeded())
        {
            std::printf("MESH_DAG_PARSE_STATUS %d\n", static_cast<int>(parsed.Status));
            return;
        }

        const CookedMeshData& mesh = parsed.Mesh;
        size_t level0Triangles = 0;
        size_t rootCount = 0;
        for (const CookedMeshCluster& cluster : mesh.Clusters)
        {
            level0Triangles += cluster.LODLevel == 0 ? cluster.IndexCount / 3 : 0;
            rootCount += cluster.bIsRoot ? 1 : 0;
        }
        std::printf("MESH_DAG_PACKAGE levels=%u clusters=%zu groups=%zu roots=%zu level0_triangles=%zu vertices=%zu\n",
                    mesh.LODLevelCount, mesh.Clusters.size(), mesh.Groups.size(), rootCount, level0Triangles,
                    mesh.Vertices.size());
        Check(mesh.LODLevelCount >= 5, "実資産の階層の段数が少なすぎる");
        Check(rootCount <= 4, "実資産の階層が数個の根まで縮まらなかった");
        CheckDagMesh(mesh, nullptr, level0Triangles);
    }
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "--check-package") == 0)
    {
        if (argc >= 5 && std::strcmp(argv[3], "--fallback-min-triangles") == 0)
        {
            g_fallbackMinTriangles = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
        }
        RunCheckPackage(argv[2]);
        if (g_failures != 0)
        {
            std::printf("MESH_SIMPLIFY_SMOKE_RESULT failed=%d\n", g_failures);
            return 1;
        }
        std::printf("MESH_SIMPLIFY_SMOKE_RESULT passed\n");
        return 0;
    }

    RunSimplify();
    RunInvalidInputs();
    RunDag();

    if (g_failures != 0)
    {
        std::printf("MESH_SIMPLIFY_SMOKE_RESULT failed=%d\n", g_failures);
        return 1;
    }
    std::printf("MESH_SIMPLIFY_SMOKE_RESULT passed\n");
    return 0;
}
