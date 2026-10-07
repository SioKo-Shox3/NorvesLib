// 太陽の仮想シャドウマップ（VSM）のクリップマップの CPU の計算を確かめる。
//
// 契約: (1) 段の中心はページの格子へスナップされ、カメラが 1 ページ未満動いても texel の格子のワールドの位置は変わらず、
// 範囲は 1 ページ単位でしかずれない。(2) ページの表の番地はトーラス（絶対のページの座標 mod 128）で、
// 1 ページ動いても範囲に残ったページの番地は変わらない。(3) 受け手の段は、texel の一辺が画素の大きさ × 2^bias 以下の
// 最も粗い段で、距離について単調で、いつも受け手を含む。
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Math/VectorUtils.h"

#include <cmath>
#include <initializer_list>
#include <cstdint>
#include <cstdio>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;
namespace CoreContainer = NorvesLib::Core::Container;

namespace
{
    int GFailureCount = 0;

    void Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::printf("失敗: %s\n", message);
            ++GFailureCount;
        }
    }

    // 再現できる擬似乱数（0 以上 1 未満）
    class DeterministicRandom
    {
    public:
        float Next()
        {
            m_State = m_State * 1664525u + 1013904223u;
            return static_cast<float>(m_State >> 8) / static_cast<float>(1u << 24);
        }

        float Range(float minimum, float maximum) { return minimum + (maximum - minimum) * Next(); }

    private:
        uint32_t m_State = 12345u;
    };

    Math::Vector3 RandomUnitVector(DeterministicRandom& random)
    {
        for (;;)
        {
            const Math::Vector3 candidate(random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f));
            const float length = Math::VectorUtils::Length(candidate);
            if (length > 0.1f && length <= 1.0f)
            {
                return candidate * (1.0f / length);
            }
        }
    }

    // 影を落とす太陽の斜めの向き（真下に近いと基底の選び方が変わるので、ここでは避ける）
    const Math::Vector3 SunDirection(-0.4f, -0.8f, 0.3f);

    VirtualShadowMapClipmap Build(const Math::Vector3& cameraPosition)
    {
        return BuildVirtualShadowMapClipmap(SunDirection, 7u, cameraPosition, VirtualShadowMapClipmapSettings{});
    }

    void TestDefaultsAndLevelGeometry()
    {
        const VirtualShadowMapClipmapSettings settings;
        Check(settings.LevelCount == 10u, "既定の段の数が 10 でない");
        Check(settings.FirstWidthMeters == 4.0f, "既定の段 0 の幅が 4 m でない");
        Check(settings.VirtualResolution == 16384u && settings.PageResolution == 128u, "既定の解像度・ページの大きさが違う");
        Check(settings.BiasLevels == -0.5f, "既定の bias が -0.5 でない");
        Check(settings.DepthRangeMeters == 1000.0f, "既定の深度の範囲が 1000 m でない");
        Check(settings.MaxShadowDistance == 80.0f, "既定の影の距離が 80 m でない");
        Check(IsValidVirtualShadowMapClipmapSettings(settings), "既定の設定が無効");

        const VirtualShadowMapClipmap clipmap = Build(Math::Vector3(3.0f, 2.0f, 1.0f));
        Check(clipmap.bEnabled, "クリップマップが有効にならない");
        Check(clipmap.LevelCount == 10u && clipmap.PagesPerAxis == 128u, "段の数・ページ数が違う");
        Check(std::fabs(clipmap.Levels[0].WidthMeters - 4.0f) < 1.0e-6f, "段 0 の幅が 4 m でない");
        Check(std::fabs(clipmap.Levels[9].WidthMeters - 2048.0f) < 1.0e-3f, "段 9 の幅が 2048 m でない");
        for (uint32_t level = 0u; level < clipmap.LevelCount; ++level)
        {
            const VirtualShadowMapClipmapLevel& data = clipmap.Levels[level];
            Check(std::fabs(data.TexelMeters * 16384.0f - data.WidthMeters) < 1.0e-4f * data.WidthMeters, "texel × 16384 が段の幅と違う");
            Check(std::fabs(data.PageMeters * 128.0f - data.WidthMeters) < 1.0e-4f * data.WidthMeters, "ページ × 128 が段の幅と違う");
            if (level > 0u)
            {
                Check(std::fabs(data.WidthMeters - 2.0f * clipmap.Levels[level - 1u].WidthMeters) < 1.0e-4f * data.WidthMeters,
                      "段の幅が段ごとに 2 倍になっていない");
            }
        }

        VirtualShadowMapClipmapSettings invalid;
        invalid.LevelCount = 0u;
        Check(!BuildVirtualShadowMapClipmap(SunDirection, 1u, Math::Vector3::Zero, invalid).bEnabled, "段が 0 の設定を許した");
        invalid = VirtualShadowMapClipmapSettings{};
        invalid.VirtualResolution = 16385u;
        Check(!BuildVirtualShadowMapClipmap(SunDirection, 1u, Math::Vector3::Zero, invalid).bEnabled, "ページの倍数でない解像度を許した");
        Check(!BuildVirtualShadowMapClipmap(Math::Vector3::Zero, 1u, Math::Vector3::Zero, VirtualShadowMapClipmapSettings{}).bEnabled,
              "向きが 0 のクリップマップが有効になった");
    }

    // 範囲の最小の角は、絶対のページの格子（= texel の格子）に乗る。カメラが 1 ページ未満動くと、範囲は動かないか 1 ページだけずれる。
    void TestTexelLatticeIsFixedInTheWorld()
    {
        DeterministicRandom random;
        for (int trial = 0; trial < 200; ++trial)
        {
            const Math::Vector3 base(random.Range(-300.0f, 300.0f), random.Range(-20.0f, 60.0f), random.Range(-300.0f, 300.0f));
            const VirtualShadowMapClipmap before = Build(base);
            for (uint32_t level = 0u; level < before.LevelCount; ++level)
            {
                const VirtualShadowMapClipmapLevel& a = before.Levels[level];
                // カメラを 1 ページ未満、ライト空間の XY の任意の向きへ動かす
                const float move = random.Range(0.01f, 0.99f) * a.PageMeters;
                const float angle = random.Range(0.0f, 6.2831853f);
                const Math::Vector3 moved = base + before.LightRight * (move * std::cos(angle)) + before.LightUp * (move * std::sin(angle));
                const VirtualShadowMapClipmap after = Build(moved);
                const VirtualShadowMapClipmapLevel& b = after.Levels[level];

                const int64_t shiftX = b.OriginPageX - a.OriginPageX;
                const int64_t shiftY = b.OriginPageY - a.OriginPageY;
                Check(shiftX >= -1 && shiftX <= 1 && shiftY >= -1 && shiftY <= 1, "カメラが 1 ページ未満動いたのに範囲が 2 ページ以上ずれた");

                for (const VirtualShadowMapClipmapLevel* data : {&a, &b})
                {
                    // 範囲の最小の角は texel の格子の上（texel の整数倍）で、ページの番号と一致する
                    const double texel = static_cast<double>(data->TexelMeters);
                    const double latticeX = data->OriginLightX / texel;
                    const double latticeY = data->OriginLightY / texel;
                    Check(std::fabs(latticeX - std::round(latticeX)) < 1.0e-6 && std::fabs(latticeY - std::round(latticeY)) < 1.0e-6,
                          "範囲の最小の角が texel の格子の上にない（スナップされていない）");
                    Check(std::fabs(data->OriginLightX - static_cast<double>(data->OriginPageX) * data->PageMeters) < 1.0e-9 &&
                              std::fabs(data->OriginLightY - static_cast<double>(data->OriginPageY) * data->PageMeters) < 1.0e-9,
                          "範囲の最小の角がページの番号と一致しない");
                }
                // 範囲が動かなかった段は、最小の角のライト空間の位置がビット一致する（格子のワールドの位置が変わらない）
                if (shiftX == 0 && shiftY == 0)
                {
                    Check(a.OriginLightX == b.OriginLightX && a.OriginLightY == b.OriginLightY, "範囲が動かない段の格子の位置が変わった");
                }
                // スナップ済みの中心（ワールド）は、ライト空間でページの格子の上にある
                double centerX = 0.0;
                double centerY = 0.0;
                double centerDepth = 0.0;
                VirtualShadowMapWorldToLightSpace(after, b.Center, centerX, centerY, centerDepth);
                const double tolerance = 2.0e-4 + 1.0e-6 * static_cast<double>(b.WidthMeters);
                Check(std::fabs(centerX - static_cast<double>(b.CenterPageX) * b.PageMeters) < tolerance &&
                          std::fabs(centerY - static_cast<double>(b.CenterPageY) * b.PageMeters) < tolerance,
                      "段の中心がページの格子へスナップされていない");
            }
        }
    }

    // ページの番地はトーラス。1 ページ動くと範囲が 1 ページだけずれ、残ったページの番地は変わらず、出ていくページの番地が入ってくるページに回る。
    void TestTorusAddressesSurviveOnePageMove()
    {
        Check(VirtualShadowMapPageTorusAddress(0, 128u) == 0u, "番地 0");
        Check(VirtualShadowMapPageTorusAddress(127, 128u) == 127u, "番地 127");
        Check(VirtualShadowMapPageTorusAddress(128, 128u) == 0u, "番地 128 は 0 へ回る");
        Check(VirtualShadowMapPageTorusAddress(-1, 128u) == 127u, "負の座標 -1 は 127");
        Check(VirtualShadowMapPageTorusAddress(-128, 128u) == 0u, "負の座標 -128 は 0");
        Check(VirtualShadowMapPageTorusAddress(-129, 128u) == 127u, "負の座標 -129 は 127");

        DeterministicRandom random;
        for (int trial = 0; trial < 50; ++trial)
        {
            const Math::Vector3 base(random.Range(-300.0f, 300.0f), random.Range(-20.0f, 60.0f), random.Range(-300.0f, 300.0f));
            const VirtualShadowMapClipmap before = Build(base);
            for (uint32_t level = 0u; level < before.LevelCount; ++level)
            {
                const VirtualShadowMapClipmapLevel& a = before.Levels[level];
                // ライト空間の x だけを、ちょうど 1 ページ動かす（動かす前の位置が格子の境界の真ん中に近いと丸めが不安定なので、
                // 丸めの境界から離れた位置に置き直す）
                double lightX = 0.0;
                double lightY = 0.0;
                double lightDepth = 0.0;
                VirtualShadowMapWorldToLightSpace(before, base, lightX, lightY, lightDepth);
                const double pageMeters = static_cast<double>(a.PageMeters);
                const double fraction = lightX / pageMeters - std::floor(lightX / pageMeters);
                const double nudge = (fraction > 0.35 && fraction < 0.65) ? (0.9 - fraction) * pageMeters : 0.0;
                const Math::Vector3 start = base + before.LightRight * static_cast<float>(nudge);
                const VirtualShadowMapClipmap origin = Build(start);
                const VirtualShadowMapClipmap stepped = Build(start + before.LightRight * a.PageMeters);
                const VirtualShadowMapClipmapLevel& o = origin.Levels[level];
                const VirtualShadowMapClipmapLevel& s = stepped.Levels[level];

                Check(s.CenterPageX == o.CenterPageX + 1 && s.CenterPageY == o.CenterPageY, "1 ページ動いたのに中心が 1 ページ分だけ動かなかった");
                Check(s.OriginPageX == o.OriginPageX + 1 && s.OriginPageY == o.OriginPageY, "1 ページ動いたのに範囲が 1 ページ分だけずれなかった");

                const int64_t count = static_cast<int64_t>(origin.PagesPerAxis);
                // 残ったページ（新しい範囲と古い範囲の両方）の番地は変わらない
                for (int64_t page = s.OriginPageX; page < o.OriginPageX + count; ++page)
                {
                    Check(VirtualShadowMapPageTorusAddress(page, origin.PagesPerAxis) == VirtualShadowMapPageTorusAddress(page, stepped.PagesPerAxis),
                          "残ったページの番地が変わった");
                }
                // 出ていく最も古いページの番地が、入ってくる最も新しいページの番地になる（番地が衝突も欠けもせずに回る）
                Check(VirtualShadowMapPageTorusAddress(o.OriginPageX, origin.PagesPerAxis) ==
                          VirtualShadowMapPageTorusAddress(s.OriginPageX + count - 1, origin.PagesPerAxis),
                      "出ていくページの番地が入ってくるページに回らない");
                // 範囲の 128 ページの番地はすべて違う
                bool bSeen[128] = {};
                bool bAllDistinct = true;
                for (int64_t page = s.OriginPageX; page < s.OriginPageX + count; ++page)
                {
                    const uint32_t address = VirtualShadowMapPageTorusAddress(page, stepped.PagesPerAxis);
                    bAllDistinct = bAllDistinct && !bSeen[address];
                    bSeen[address] = true;
                }
                Check(bAllDistinct, "範囲の中のページの番地が衝突した");
            }
        }
    }

    // 受け手の段の選び方: 距離について単調、texel は画素の大きさ × 2^bias 以下の最も粗い段、0 段より細かくは選ばない、距離の上限の外は -1。
    void TestLevelSelection()
    {
        const VirtualShadowMapClipmapSettings settings;
        const float fovYValues[] = {35.0f, 60.0f, 90.0f};
        const float heightValues[] = {360.0f, 720.0f, 1440.0f};

        // 通常の画面（1280×720・fovY 60 度）では、範囲の被覆で段が粗くなることはない
        for (int step = 0; step <= 8000; ++step)
        {
            const float distance = static_cast<float>(step) * 0.01f;
            const int32_t level = SelectVirtualShadowMapLevel(settings, distance, 60.0f, 720.0f);
            const float target = VirtualShadowMapScreenPixelMeters(distance, 60.0f, 720.0f) * std::exp2(settings.BiasLevels);
            Check(target < VirtualShadowMapLevelTexelMeters(settings, 0u) || VirtualShadowMapLevelTexelMeters(settings, static_cast<uint32_t>(level)) <= target,
                  "通常の画面で texel が画素の大きさ × 2^bias を超えた");
            Check(distance <= VirtualShadowMapLevelCoverageMeters(settings, static_cast<uint32_t>(level)), "通常の画面で選んだ段が受け手を覆わない");
        }
        for (const float fovY : fovYValues)
        {
            for (const float height : heightValues)
            {
                int32_t previous = -1;
                for (int step = 0; step <= 8000; ++step)
                {
                    const float distance = static_cast<float>(step) * 0.01f;
                    const int32_t level = SelectVirtualShadowMapLevel(settings, distance, fovY, height);
                    Check(level >= 0 && level < static_cast<int32_t>(settings.LevelCount), "影の距離の中で段が選ばれない");
                    Check(level >= previous, "選ぶ段が距離について単調でない");
                    previous = level;

                    const float target = VirtualShadowMapScreenPixelMeters(distance, fovY, height) * std::exp2(settings.BiasLevels);
                    const float texel = VirtualShadowMapLevelTexelMeters(settings, static_cast<uint32_t>(level));
                    // この格子の画面では被覆による粗くする補正は入らず、texel の上限と受け手の被覆が両立する（免除なし）
                    if (target >= VirtualShadowMapLevelTexelMeters(settings, 0u))
                    {
                        Check(texel <= target, "選んだ段の texel が画素の大きさ × 2^bias を超えた");
                        if (level + 1 < static_cast<int32_t>(settings.LevelCount))
                        {
                            Check(VirtualShadowMapLevelTexelMeters(settings, static_cast<uint32_t>(level) + 1u) > target, "より粗い段でも足りるのに細かい段を選んだ");
                        }
                    }
                    else
                    {
                        Check(level == 0, "目標が段 0 の texel より小さいのに 0 段以外を選んだ");
                    }
                    Check(distance <= VirtualShadowMapLevelCoverageMeters(settings, static_cast<uint32_t>(level)), "選んだ段が受け手を覆わない");
                }
            }
        }

        Check(SelectVirtualShadowMapLevel(settings, 80.01f, 60.0f, 720.0f) == -1, "影の距離の外で段を選んだ");
        Check(SelectVirtualShadowMapLevel(settings, -1.0f, 60.0f, 720.0f) == -1, "負の距離で段を選んだ");
        Check(SelectVirtualShadowMapLevel(settings, 10.0f, 60.0f, 0.0f) == -1, "画面の高さが 0 で段を選んだ");
        Check(SelectVirtualShadowMapLevel(settings, 0.0f, 60.0f, 720.0f) == 0, "距離 0 の受け手が段 0 でない");

        // 1 m・fovY 60 度・720 画素: 1 画素 = 1.6 mm、目標 1.6·2^-0.5 = 1.13 mm、texel 0.244 mm·2^L ≤ 1.13 mm の最大は L = 2
        Check(SelectVirtualShadowMapLevel(settings, 1.0f, 60.0f, 720.0f) == 2, "距離 1 m の段が 2 でない");
        // 80 m: 1 画素 = 128 mm、目標 90.5 mm、0.244 mm·2^L ≤ 90.5 mm の最大は L = 8
        Check(SelectVirtualShadowMapLevel(settings, 80.0f, 60.0f, 720.0f) == 8, "距離 80 m の段が 8 でない");

        // bias を 1 上げる（目標が 2 倍）と段は 1 つ粗くなる
        VirtualShadowMapClipmapSettings coarser = settings;
        coarser.BiasLevels = settings.BiasLevels + 1.0f;
        Check(SelectVirtualShadowMapLevel(coarser, 10.0f, 60.0f, 720.0f) == SelectVirtualShadowMapLevel(settings, 10.0f, 60.0f, 720.0f) + 1,
              "bias を 1 上げても段が 1 つ粗くならない");

        Check(VirtualShadowMapShadowFadeWeight(settings, 0.0f) == 0.0f, "手前で薄めが入った");
        Check(VirtualShadowMapShadowFadeWeight(settings, 71.9f) == 0.0f, "奥の 10% の手前で薄めが入った");
        Check(std::fabs(VirtualShadowMapShadowFadeWeight(settings, 76.0f) - 0.5f) < 1.0e-5f, "奥の 10% の真ん中の重みが 0.5 でない");
        Check(VirtualShadowMapShadowFadeWeight(settings, 80.0f) == 1.0f, "影の距離の端で影が消えていない");
    }

    // texel の上限と被覆が両立しない画面（BiasLevels の説明の式を満たさない高精細・狭画角）では、被覆を優先して粗い段を選ぶ。
    // 選んだ段は受け手を覆い、より細かい段は覆わず、距離について単調なまま。
    void TestCoveragePriorityOutsideSupportedScreens()
    {
        const VirtualShadowMapClipmapSettings settings;
        int32_t previous = -1;
        bool bClampedOnce = false;
        for (int step = 0; step <= 8000; ++step)
        {
            const float distance = static_cast<float>(step) * 0.01f;
            const int32_t level = SelectVirtualShadowMapLevel(settings, distance, 20.0f, 2160.0f);
            Check(level >= previous, "対応外の画面で選ぶ段が距離について単調でない");
            previous = level;
            Check(level >= 0 && distance <= VirtualShadowMapLevelCoverageMeters(settings, static_cast<uint32_t>(level)), "対応外の画面で選んだ段が受け手を覆わない");
            if (level > 0)
            {
                Check(distance > VirtualShadowMapLevelCoverageMeters(settings, static_cast<uint32_t>(level) - 1u), "対応外の画面で受け手を覆う最も細かい段を選んでいない");
                const float target = VirtualShadowMapScreenPixelMeters(distance, 20.0f, 2160.0f) * std::exp2(settings.BiasLevels);
                bClampedOnce = bClampedOnce || VirtualShadowMapLevelTexelMeters(settings, static_cast<uint32_t>(level)) > target;
            }
        }
        Check(bClampedOnce, "対応外の画面の例で被覆による補正が入らなかった（例の画面が対応内になっている）");
    }

    // 選んだ段の範囲は、いつも受け手（カメラからの距離 d の点）を含む。
    void TestSelectedLevelContainsReceiver()
    {
        DeterministicRandom random;
        const VirtualShadowMapClipmapSettings settings;
        for (int trial = 0; trial < 300; ++trial)
        {
            const Math::Vector3 cameraPosition(random.Range(-500.0f, 500.0f), random.Range(-50.0f, 100.0f), random.Range(-500.0f, 500.0f));
            const Math::Vector3 lightDirection = RandomUnitVector(random);
            const VirtualShadowMapClipmap clipmap =
                BuildVirtualShadowMapClipmap(lightDirection, 3u, cameraPosition, settings);
            Check(clipmap.bEnabled, "任意の向きの太陽でクリップマップが無効になった");
            if (!clipmap.bEnabled)
            {
                continue;
            }
            const float fovY = random.Range(30.0f, 100.0f);
            const float height = random.Range(240.0f, 2160.0f);
            for (int sample = 0; sample < 40; ++sample)
            {
                const float distance = random.Range(0.0f, settings.MaxShadowDistance);
                const Math::Vector3 receiver = cameraPosition + RandomUnitVector(random) * distance;
                const int32_t level = SelectVirtualShadowMapLevel(settings, distance, fovY, height);
                Check(level >= 0, "影の距離の中で段が選ばれない");
                if (level < 0)
                {
                    continue;
                }
                double lightX = 0.0;
                double lightY = 0.0;
                double lightDepth = 0.0;
                VirtualShadowMapWorldToLightSpace(clipmap, receiver, lightX, lightY, lightDepth);
                int64_t pageX = 0;
                int64_t pageY = 0;
                Check(VirtualShadowMapLevelFindPage(clipmap, static_cast<uint32_t>(level), lightX, lightY, pageX, pageY),
                      "選んだ段の範囲が受け手を含まない");
                Check(std::fabs(lightDepth - clipmap.DepthCenter) < static_cast<double>(settings.DepthRangeMeters),
                      "受け手の深度がクリップマップの深度の範囲の外");
            }
        }
    }

    // 深度の原点は範囲の 1/4 の刻みでスナップされ、小さな動きでは変わらない。
    void TestDepthOriginSnapsToCoarseSteps()
    {
        const VirtualShadowMapClipmapSettings settings;
        const double step = static_cast<double>(settings.DepthRangeMeters) * 0.25;
        DeterministicRandom random;
        for (int trial = 0; trial < 100; ++trial)
        {
            const Math::Vector3 position(random.Range(-300.0f, 300.0f), random.Range(-50.0f, 150.0f), random.Range(-300.0f, 300.0f));
            const VirtualShadowMapClipmap clipmap = Build(position);
            Check(std::fabs(clipmap.DepthCenter / step - std::round(clipmap.DepthCenter / step)) < 1.0e-9, "深度の原点が範囲の 1/4 の刻みの上にない");

            double lightX = 0.0;
            double lightY = 0.0;
            double depth = 0.0;
            VirtualShadowMapWorldToLightSpace(clipmap, position, lightX, lightY, depth);
            Check(std::fabs(depth - clipmap.DepthCenter) <= step * 0.5 + 1.0e-3, "深度の原点がカメラの深度の半刻みより離れた");

            // 刻みの境界から離れていれば、数 m の動きでは深度の原点は変わらない
            const double fraction = depth / step - std::floor(depth / step);
            if (fraction > 0.2 && fraction < 0.3)
            {
                const VirtualShadowMapClipmap moved = Build(position + clipmap.Direction * 5.0f);
                Check(moved.DepthCenter == clipmap.DepthCenter, "小さな動きで深度の原点が変わった");
            }
        }
    }

    // CSM と同じ太陽（SelectShadowedDirectionalLight）から作る。
    void TestBuildFromLightProxies()
    {
        CameraProxy camera;
        camera.PositionX = 10.0f;
        camera.PositionY = 2.0f;
        camera.PositionZ = -5.0f;

        CoreContainer::VariableArray<LightProxy> lights;
        Check(!BuildVirtualShadowMapClipmap(&lights, camera, VirtualShadowMapClipmapSettings{}).bEnabled, "太陽が無いのに有効になった");
        Check(!BuildVirtualShadowMapClipmap(nullptr, camera, VirtualShadowMapClipmapSettings{}).bEnabled, "光源の一覧が無いのに有効になった");

        LightProxy sun;
        sun.LightId = 42u;
        sun.Type = LightType::Directional;
        sun.DirectionX = SunDirection.x;
        sun.DirectionY = SunDirection.y;
        sun.DirectionZ = SunDirection.z;
        sun.bCastShadows = true;
        sun.bVisible = true;
        sun.Intensity = 1.0f;
        lights.push_back(sun);

        const VirtualShadowMapClipmap fromLights = BuildVirtualShadowMapClipmap(&lights, camera, VirtualShadowMapClipmapSettings{});
        const VirtualShadowMapClipmap direct = BuildVirtualShadowMapClipmap(SunDirection, 42u, Math::Vector3(10.0f, 2.0f, -5.0f), VirtualShadowMapClipmapSettings{});
        Check(fromLights.bEnabled && fromLights.LightId == 42u, "太陽から作ったクリップマップが有効でない");
        Check(fromLights.DepthCenter == direct.DepthCenter && fromLights.Levels[3].OriginPageX == direct.Levels[3].OriginPageX &&
                  fromLights.Levels[3].OriginPageY == direct.Levels[3].OriginPageY,
              "光源の一覧から作った結果と、向きとカメラから作った結果が違う");

        lights[0].bCastShadows = false;
        Check(!BuildVirtualShadowMapClipmap(&lights, camera, VirtualShadowMapClipmapSettings{}).bEnabled, "影を落とさない太陽で有効になった");
    }
} // namespace

int main()
{
    std::printf("VirtualShadowMapClipmapTest 開始\n");

    TestDefaultsAndLevelGeometry();
    TestTexelLatticeIsFixedInTheWorld();
    TestTorusAddressesSurviveOnePageMove();
    TestLevelSelection();
    TestCoveragePriorityOutsideSupportedScreens();
    TestSelectedLevelContainsReceiver();
    TestDepthOriginSnapsToCoarseSteps();
    TestBuildFromLightProxies();

    if (GFailureCount != 0)
    {
        std::printf("VirtualShadowMapClipmapTest 失敗: %d\n", GFailureCount);
        return 1;
    }
    std::printf("VirtualShadowMapClipmapTest 合格\n");
    return 0;
}
