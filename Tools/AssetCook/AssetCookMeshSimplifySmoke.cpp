// CookMeshOptimizer(meshoptimizer の簡略化の境界)のスモーク。
// 緯度経度の球(約 2 万三角形)の極側を切り落として縁を作り、縁の頂点を固定して三角形を半分に簡略化したとき、
// 三角形の数・誤差・固定した頂点が消えないことを確かめる。あわせて、固定しなければ縁の頂点が消えること(対照)と、
// 不正な入力の扱いを確かめる。

#include "CookMeshOptimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

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

int main()
{
    RunSimplify();
    RunInvalidInputs();

    if (g_failures != 0)
    {
        std::printf("MESH_SIMPLIFY_SMOKE_RESULT failed=%d\n", g_failures);
        return 1;
    }
    std::printf("MESH_SIMPLIFY_SMOKE_RESULT passed\n");
    return 0;
}
