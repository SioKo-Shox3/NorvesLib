// 雨の遮蔽マップの正射影、格子、描き直しと深度の読み取りを確かめる。
#include "Rendering/OverheadHeightMapMath.h"

#include <cmath>
#include <cstdio>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;

namespace
{
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::printf("失敗: %s\n", message);
        }
    }

    bool NearlyEqual(float lhs, float rhs, float tolerance = 1.0e-4f)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    void ExpectClip(const OverheadHeightMapGpuParams& params,
        float x, float y, float z, float expectedX, float expectedY, float expectedZ)
    {
        const float point[4] = {x, y, z, 1.0f};
        float clip[4] = {};
        // GPU に渡す配列を GLSL の列優先 mat4 として掛け、転置の取り違えも検出する。
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                clip[row] += params.ViewProjection[column * 4u + row] * point[column];
            }
        }
        Expect(NearlyEqual(clip[0], expectedX) && NearlyEqual(clip[1], expectedY) &&
            NearlyEqual(clip[2], expectedZ) && NearlyEqual(clip[3], 1.0f),
            "GPU の列優先行列は X を列、Z を行、上面からの距離を深度へ写す");
    }

    void TestOrthographic()
    {
        const auto window = ComputeOverheadHeightMapWindow({}, 17.0f, 23.0f, -31.0f);
        const auto params = MakeOverheadHeightMapGpuParams(window, 3u, 5u);
        const float x = window.GetOriginX();
        const float z = window.GetOriginZ();
        const float coverage = window.CoverageMeters;
        const float middleY = window.TopY - window.RangeMeters * 0.5f;
        ExpectClip(params, x, middleY, z, -1.0f, -1.0f, 0.5f);
        ExpectClip(params, x + coverage, middleY, z + coverage, 1.0f, 1.0f, 0.5f);
        ExpectClip(params, x + coverage, middleY, z, 1.0f, -1.0f, 0.5f);
        ExpectClip(params, x, middleY, z + coverage, -1.0f, 1.0f, 0.5f);
        ExpectClip(params, x, window.TopY, z, -1.0f, -1.0f, 0.0f);
        ExpectClip(params, x, window.TopY - window.RangeMeters, z, -1.0f, -1.0f, 1.0f);
        Expect(params.Window[0] == x && params.Window[1] == z &&
            params.Window[2] == coverage && params.Window[3] == window.TexelMeters &&
            params.Depth[0] == window.TopY && params.Depth[1] == window.RangeMeters &&
            params.Depth[2] == 0.0f && params.Depth[3] == 0.0f &&
            params.Counts[0] == 3u && params.Counts[1] == 5u &&
            params.Counts[2] == 0u && params.Counts[3] == 0u,
            "UBO の窓、深度、箱と楕円体の数と予約欄が一致");
    }

    void TestTexelRoundingAndRedraw()
    {
        const OverheadHeightMapSettings settings;
        const float texel = settings.CoverageMeters / settings.Resolution;
        const float cameraX = 20.5f * texel;
        const float cameraZ = -11.5f * texel;
        const auto window = ComputeOverheadHeightMapWindow(settings, cameraX, 1.0f, cameraZ);
        Expect(window.TexelMeters == 0.09375f && window.OriginTexelX == 20 - 512 &&
            window.OriginTexelZ == -12 - 512 && window.TopY == 144.0f,
            "負の座標を含む原点は切り下げ、上面は 16 m ごとに切り上げる");
        for (const float offset : {-0.4f, 0.4f})
        {
            const auto shifted = ComputeOverheadHeightMapWindow(settings,
                cameraX + offset * texel, 1.0f, cameraZ + offset * texel);
            Expect(shifted.OriginTexelX == window.OriginTexelX &&
                shifted.OriginTexelZ == window.OriginTexelZ,
                "テクセル中心から正負 0.4 テクセルの移動では原点を変えない");
        }
        OverheadHeightMapRedrawState state;
        state.bHasContent = true;
        state.Window = window;
        state.LastRedrawSeconds = 10.0;
        auto candidate = window;
        candidate.OriginTexelX += 7;
        auto decision = DecideOverheadHeightMapRedraw(settings, state, candidate, 10.25, false);
        Expect(!decision.bRedraw && decision.Window.OriginTexelX == window.OriginTexelX,
            "7 テクセルの移動では既存の窓を保つ");
        candidate.OriginTexelX += 1;
        decision = DecideOverheadHeightMapRedraw(settings, state, candidate, 10.25, false);
        Expect(decision.bRedraw && decision.Window.OriginTexelX == candidate.OriginTexelX,
            "8 テクセルの移動と 0.25 秒の経過で候補の窓を描く");
        decision = DecideOverheadHeightMapRedraw(settings, state, candidate, 10.1, false);
        Expect(!decision.bRedraw && decision.Window.OriginTexelX == window.OriginTexelX,
            "0.1 秒では描き直しを保留し、既存の窓を保つ");
        candidate = window;
        candidate.OriginTexelZ -= 8;
        Expect(DecideOverheadHeightMapRedraw(settings, state, candidate, 10.25, false).bRedraw,
            "Z の負方向も 8 テクセルで描き直す");
        candidate = window;
        candidate.TopY += settings.TopSnapMeters;
        Expect(DecideOverheadHeightMapRedraw(settings, state, candidate, 10.25, false).bRedraw,
            "上面の高さの変更でも描き直す");
        candidate = window;
        candidate.OriginTexelX += 7;
        decision = DecideOverheadHeightMapRedraw(settings, state, candidate, 10.25, true);
        Expect(decision.bRedraw && decision.Window.OriginTexelX == window.OriginTexelX,
            "遮蔽物だけの変更は既存の窓を描き直す");
        Expect(!DecideOverheadHeightMapRedraw(settings, state, candidate, 10.1, true).bRedraw,
            "遮蔽物の変更も最小間隔までは保留する");
        Expect(DecideOverheadHeightMapRedraw(settings, state, candidate, 9.0, true).bRedraw,
            "時間が巻き戻ったら待たずに描き直す");
        state.bHasContent = false;
        decision = DecideOverheadHeightMapRedraw(settings, state, candidate, 10.1, false);
        Expect(decision.bRedraw && decision.Window.OriginTexelX == candidate.OriginTexelX,
            "中身がなければ間隔を待たず候補の窓を描く");
    }

    void TestRoundTrips()
    {
        const auto window = ComputeOverheadHeightMapWindow({}, -12.0f, 32.0f, 21.0f);
        const uint32_t indices[] = {0u, 1u, 511u, 512u, 1023u};
        for (const uint32_t i : indices)
        {
            for (const uint32_t j : indices)
            {
                float x = 0.0f;
                float z = 0.0f;
                OverheadTexelCenterToWorld(window, i, j, x, z);
                uint32_t resultI = window.Resolution;
                uint32_t resultJ = window.Resolution;
                Expect(WorldToOverheadTexel(window, x, z, resultI, resultJ) &&
                    resultI == i && resultJ == j, "テクセル中心とワールドの往復で番号を保つ");
            }
        }
        const float fractions[] = {0.0f, 0.12345f, 0.5f, 0.99999f, 1.0f};
        for (const float fraction : fractions)
        {
            const float x = window.GetOriginX() + fraction * window.CoverageMeters;
            const float z = window.GetOriginZ() + (1.0f - fraction) * window.CoverageMeters;
            const float u = (x - window.GetOriginX()) / window.CoverageMeters;
            const float v = (z - window.GetOriginZ()) / window.CoverageMeters;
            Expect(NearlyEqual(window.GetOriginX() + u * window.CoverageMeters, x) &&
                NearlyEqual(window.GetOriginZ() + v * window.CoverageMeters, z),
                "ワールドと UV の往復の誤差は 1e-4 m 以内");
        }
        for (uint32_t step = 0u; step <= 100u; ++step)
        {
            const float y = window.TopY - window.RangeMeters * (static_cast<float>(step) / 100.0f);
            Expect(NearlyEqual(OverheadRoofYFromDepth(window, OverheadDepthFromHeight(window, y)), y, 1.0e-3f),
                "全深度範囲で高さの往復の誤差は 1e-3 m 以内");
        }
    }

    void TestOpenSamples()
    {
        const auto window = ComputeOverheadHeightMapWindow({}, 0.0f, 0.0f, 0.0f);
        Container::VariableArray<float> depths(static_cast<size_t>(window.Resolution) * window.Resolution, 1.0f);
        float roofY = -999.0f;
        float x = 0.0f;
        float z = 0.0f;
        OverheadTexelCenterToWorld(window, 17u, 29u, x, z);
        Expect(!SampleOverheadRoofY(window, depths, x, z, roofY) && roofY == -999.0f,
            "深度 1.0 は開けており、出力を変えない");
        depths[29u * window.Resolution + 17u] = 0.5f;
        Expect(SampleOverheadRoofY(window, depths, x, z, roofY) &&
            NearlyEqual(roofY, window.TopY - 256.0f),
            "行優先の深度 0.5 は上面から 256 m 下の屋根");
        const float outside[][2] = {
            {window.GetOriginX() - window.TexelMeters, z},
            {window.GetOriginX() + window.CoverageMeters, z},
            {x, window.GetOriginZ() - window.TexelMeters},
            {x, window.GetOriginZ() + window.CoverageMeters}};
        for (const auto& point : outside)
        {
            Expect(!SampleOverheadRoofY(window, depths, point[0], point[1], roofY),
                "窓の外側と上限の境界は開けている");
        }
        Expect(!SampleOverheadRoofY(window, Container::Span<const float>(depths.data(), depths.size() - 1u), x, z, roofY),
            "深度配列の大きさが窓に足りなければ開けている");
    }
} // namespace

int main()
{
    TestOrthographic();
    TestTexelRoundingAndRedraw();
    TestRoundTrips();
    TestOpenSamples();
    if (g_FailureCount != 0)
    {
        std::printf("OverheadHeightMapMathTest: %d 件失敗\n", g_FailureCount);
        return 1;
    }
    std::printf("OverheadHeightMapMathTest: 成功\n");
    return 0;
}
