// 枯れ度の空間評価・時間遷移・GPU 向け選別を解析値で検証する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Vegetation/DrynessMap.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Vegetation;
namespace Container = NorvesLib::Core::Container;

namespace
{
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << "\n";
        }
    }

    void ExpectSample(const DrynessMap& map, float x, float z, float expected, const char* message)
    {
        const float actual = map.Sample(x, z);
        if (!(std::fabs(actual - expected) <= 1e-5f))
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << " x=" << x << " z=" << z
                      << " 期待=" << expected << " 実測=" << actual << "\n";
        }
    }

    void TestCircle()
    {
        DrynessMap map;
        Expect(map.AddCircle(3.0f, -2.0f, 2.0f, 4.0f, 0.8f) != InvalidDrynessSourceId,
               "円を追加できる");
        ExpectSample(map, 3.0f, -2.0f, 0.8f, "円の中心");
        ExpectSample(map, 4.0f, -2.0f, 0.8f, "円の内側");
        ExpectSample(map, 5.0f, -2.0f, 0.8f, "円の外縁");
        ExpectSample(map, 7.0f, -2.0f, 0.4f, "外縁から Falloff/2");
        constexpr float t = 0.25f;
        ExpectSample(map, 6.0f, -2.0f, 0.8f * (1.0f - t * t * (3.0f - 2.0f * t)),
                     "外縁から Falloff/4");
        ExpectSample(map, 9.5f, -2.0f, 0.0f, "減衰帯の外側");

        DrynessMap hardEdge;
        Expect(hardEdge.AddCircle(0.0f, 0.0f, 2.0f, 0.0f, 0.7f) != InvalidDrynessSourceId,
               "幅 0 の円を追加できる");
        ExpectSample(hardEdge, 2.0f, 0.0f, 0.7f, "幅 0 の外縁は内側");
        ExpectSample(hardEdge, 2.01f, 0.0f, 0.0f, "幅 0 の外縁直後は寄与なし");
        ExpectSample(map, std::numeric_limits<float>::infinity(), -2.0f, 0.0f, "非有限の X 座標");
        ExpectSample(map, 3.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, "非有限の Z 座標");
        std::cout << "円: 内側・外縁・減衰帯・幅 0・非有限座標を検査\n";
    }

    void TestEllipse(float yaw)
    {
        DrynessMap map;
        constexpr float centerX = -2.0f;
        constexpr float centerZ = 3.0f;
        constexpr float falloff = 2.0f;
        Expect(map.AddEllipse(centerX, centerZ, 6.0f, 1.0f, yaw, falloff, 1.0f) !=
               InvalidDrynessSourceId, "楕円を追加できる");
        const float cosine = std::cos(yaw);
        const float sine = std::sin(yaw);
        for (int axis = 0; axis < 2; ++axis)
        {
            const float radius = axis == 0 ? 6.0f : 1.0f;
            const float directionX = axis == 0 ? cosine : -sine;
            const float directionZ = axis == 0 ? sine : cosine;
            // 主軸上では外縁からの距離が半径を超えた分に等しい。
            const float distances[] = {radius * 0.5f, radius + falloff * 0.25f, radius + falloff + 0.5f};
            constexpr float t = 0.25f;
            const float expected[] = {1.0f, 1.0f - t * t * (3.0f - 2.0f * t), 0.0f};
            for (int point = 0; point < 3; ++point)
            {
                ExpectSample(map, centerX + distances[point] * directionX,
                             centerZ + distances[point] * directionZ, expected[point],
                             axis == 0 ? "楕円の局所 X 軸" : "楕円の局所 Z 軸");
            }
        }
        if (yaw != 0.0f)
        {
            DrynessMap narrowFalloff;
            Expect(narrowFalloff.AddEllipse(0.0f, 0.0f, 6.0f, 1.0f, yaw, 0.25f, 1.0f) !=
                   InvalidDrynessSourceId, "向きの検査用楕円を追加できる");
            ExpectSample(narrowFalloff, 5.0f * cosine, 5.0f * sine, 1.0f, "回した長軸上は内側");
            ExpectSample(narrowFalloff, 5.0f, 0.0f, 0.0f, "ワールド X 軸上の同じ距離は外側");
        }
        std::cout << "楕円: yaw=" << yaw << "、両局所軸の内側・減衰帯・外側を検査\n";
    }

    void TestOverlapAndRemove()
    {
        DrynessMap map;
        const auto low = map.AddCircle(0.0f, 0.0f, 2.0f, 1.0f, 0.3f);
        const auto high = map.AddCircle(0.5f, 0.0f, 2.0f, 1.0f, 0.7f);
        Expect(low != InvalidDrynessSourceId && high != InvalidDrynessSourceId, "重なる源を追加できる");
        ExpectSample(map, 0.0f, 0.0f, 0.7f, "重なる源の寄与は最大値");
        Expect(map.Remove(high), "高い源を除去できる");
        Expect(map.Find(high) == nullptr, "除去した源は検索できない");
        ExpectSample(map, 0.0f, 0.0f, 0.3f, "除去後は低い源の寄与だけ残る");
        Expect(map.Remove(low), "残りの源を除去できる");
        ExpectSample(map, 0.0f, 0.0f, 0.0f, "全源の除去後は寄与なし");
        std::cout << "重なりと除去: 最大値の合成と寄与の消失を検査\n";
    }

    void TestTransition(float start, float target, float dt, int steps)
    {
        DrynessMap map;
        const auto id = map.AddCircle(0.0f, 0.0f, 2.0f, 1.0f, start);
        Expect(id != InvalidDrynessSourceId, "遷移する源を追加できる");
        Expect(map.SetTarget(id, target, 1.0f), "1 秒の目標を設定できる");
        float previous = start;
        for (int step = 1; step <= steps; ++step)
        {
            map.Tick(dt);
            const DrynessSource* source = map.Find(id);
            Expect(source != nullptr, "遷移中の源が見つかる");
            if (source == nullptr)
            {
                return;
            }
            const float elapsed = static_cast<float>(step) * dt;
            const float fraction = elapsed >= 1.0f ? 1.0f : elapsed;
            const float expected = start + (target - start) * fraction;
            if (!(std::fabs(source->CurrentLevel - expected) <= 1e-5f))
            {
                ++g_FailureCount;
                std::cout << "失敗: 一定速度の現在値 開始=" << start << " 目標=" << target
                          << " 秒数=1 dt=" << dt << " 刻み=" << step
                          << " 期待=" << expected << " 実測=" << source->CurrentLevel << "\n";
            }
            ExpectSample(map, 0.0f, 0.0f, expected, "遷移中の中心サンプル");
            const float sample = map.Sample(0.0f, 0.0f);
            const bool bIncreasing = target > start;
            Expect(bIncreasing ? (source->CurrentLevel >= previous && source->CurrentLevel <= target &&
                                  sample >= previous && sample <= target) :
                                 (source->CurrentLevel <= previous && source->CurrentLevel >= target &&
                                  sample <= previous && sample >= target),
                   "現在値とサンプルは単調に目標へ近づく");
            if (elapsed < 1.0f)
            {
                Expect(source->CurrentLevel != target && source->RemainingSeconds > 0.0f,
                       "指定秒数より前に目標へ到達しない");
            }
            else if (source->CurrentLevel != target || sample != target || source->RemainingSeconds != 0.0f)
            {
                ++g_FailureCount;
                std::cout << "失敗: 到達時刻 dt=" << dt << " 刻み=" << step
                          << " 期待=" << target << " 現在値=" << source->CurrentLevel
                          << " サンプル=" << sample << " 残秒=" << source->RemainingSeconds << "\n";
            }
            previous = source->CurrentLevel;
        }
        Expect(map.SetTarget(id, start, 0.0f), "0 秒の目標を設定できる");
        const DrynessSource* source = map.Find(id);
        Expect(source != nullptr && source->CurrentLevel == start && source->TargetLevel == start &&
               source->TransitionSeconds == 0.0f && source->RemainingSeconds == 0.0f &&
               map.Sample(0.0f, 0.0f) == start, "0 秒なら Tick 前に目標に一致する");
        std::cout << "遷移: 開始=" << start << " 目標=" << target << " 秒数=1 dt=" << dt << "\n";
    }

    void TestGpuSelection()
    {
        DrynessMap map;
        Expect(map.GetGpuSourceLimit() == 64, "GPU 上限の既定は 64");
        for (uint32_t index = 0; index < 70; ++index)
        {
            Expect(map.AddCircle(10.0f + static_cast<float>(70 - index), 20.0f, 1.0f, 0.0f, 0.5f) ==
                   index + 1, "選別用の源を追加できる");
        }
        Container::VariableArray<DrynessSource> selected;
        map.CollectGpuSources(10.0f, 20.0f, selected);
        Expect(selected.size() == 64, "70 源から既定上限の 64 源を選ぶ");
        for (size_t index = 0; index < selected.size(); ++index)
        {
            if (selected[index].Id != 70 - index)
            {
                ++g_FailureCount;
                std::cout << "失敗: 焦点=(10,20) 選別位置=" << index
                          << " 期待 ID=" << 70 - index << " 実測 ID=" << selected[index].Id << "\n";
            }
        }
        map.SetGpuSourceLimit(3);
        Expect(map.GetGpuSourceLimit() == 3, "GPU 上限を変更できる");
        map.CollectGpuSources(10.0f, 20.0f, selected);
        Expect(selected.size() == 3, "変更後の上限の数だけ返す");
        for (size_t index = 0; index < selected.size(); ++index)
        {
            Expect(selected[index].Id == 70 - index, "変更後も近い順で前回の出力を置き換える");
        }
        map.SetGpuSourceLimit(0);
        map.CollectGpuSources(10.0f, 20.0f, selected);
        Expect(selected.empty(), "上限 0 で前回の出力も空にする");
        std::cout << "GPU 向け選別: 70 源、上限 64・3・0 を検査（CPU のみ）\n";
    }
}

int main()
{
    std::cout.precision(9);
    TestCircle();
    TestEllipse(0.0f);
    TestEllipse(0.6f);
    TestOverlapAndRemove();
    TestTransition(0.2f, 0.9f, 0.25f, 4);
    TestTransition(0.9f, 0.2f, 0.3f, 4);
    TestGpuSelection();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "DrynessMapQueryTest: すべての検査に合格\n";
    return 0;
}
