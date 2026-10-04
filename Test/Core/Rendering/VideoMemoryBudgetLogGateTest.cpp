// VRAM_BUDGET ログの間引き（VideoMemoryBudgetLogGate）の契約テスト。
// 初回は必ず出す・1秒未満は取得しない・1%未満の変化は出さない・1%以上の変化は出す、の境界を
// 制御した予算値と時刻で確かめる。GPU は使わない。
#include "Rendering/VideoMemoryBudgetLogGate.h"

#include <chrono>
#include <cstdint>
#include <iostream>

namespace NorvesLib
{
namespace
{

using Core::Rendering::VideoMemoryBudgetLogGate;
using Clock = std::chrono::steady_clock;

int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "VideoMemoryBudgetLogGateTest failed: " << message << std::endl;
        ++g_failures;
    }
}

// 予算 10000 MB・使用量 2000 MB を基準にする（1% は 100 MB と 20 MB）。
constexpr uint64_t kMb = 1024ull * 1024ull;
constexpr uint64_t kBudget = 10000 * kMb;
constexpr uint64_t kUsage = 2000 * kMb;

int RunTest()
{
    const Clock::time_point t0 = Clock::time_point{} + std::chrono::seconds(100);

    VideoMemoryBudgetLogGate gate;

    // 初回は時刻に依らず取得でき、値も必ず出す。
    Expect(gate.IsPollDue(t0), "first poll must be due");
    gate.MarkPolled(t0);
    Expect(gate.CommitIfChanged(kBudget, kUsage), "first value must be logged");

    // 間隔: 1秒未満は取得しない、ちょうど1秒と以後は取得する。
    Expect(!gate.IsPollDue(t0 + std::chrono::milliseconds(999)), "poll within 999ms must not be due");
    Expect(gate.IsPollDue(t0 + std::chrono::milliseconds(1000)), "poll at exactly 1s must be due");
    Expect(gate.IsPollDue(t0 + std::chrono::seconds(5)), "poll at 5s must be due");

    // 変化率: 同値と 1% 未満の変化は出さない。
    Expect(!gate.CommitIfChanged(kBudget, kUsage), "unchanged value must not be logged");
    Expect(!gate.CommitIfChanged(kBudget, kUsage + 19 * kMb), "usage +0.95% must not be logged");
    Expect(!gate.CommitIfChanged(kBudget - 99 * kMb, kUsage), "budget -0.99% must not be logged");

    // 1% ちょうど以上の変化は出し、基準が新しい値に移る。
    Expect(gate.CommitIfChanged(kBudget, kUsage + 20 * kMb), "usage +1% must be logged");
    const uint64_t usage2 = kUsage + 20 * kMb;
    Expect(!gate.CommitIfChanged(kBudget, usage2 + 10 * kMb), "usage +0.5% from new baseline must not be logged");
    Expect(gate.CommitIfChanged(kBudget - 100 * kMb, usage2), "budget -1% must be logged");

    // 値の増加が 1% 未満ずつでも、前回ログした値からの累計で 1% に届けば出す。
    VideoMemoryBudgetLogGate drift;
    drift.MarkPolled(t0);
    Expect(drift.CommitIfChanged(kBudget, kUsage), "drift: first value must be logged");
    Expect(!drift.CommitIfChanged(kBudget, kUsage + 12 * kMb), "drift: +0.6% must not be logged");
    Expect(drift.CommitIfChanged(kBudget, kUsage + 24 * kMb), "drift: +1.2% cumulative must be logged");

    // 拡張が無いときの値（0, 0）は初回に1度だけ出て、その後は変化が無いので出さない。
    VideoMemoryBudgetLogGate none;
    Expect(none.CommitIfChanged(0, 0), "none: first (0,0) must be logged");
    Expect(!none.CommitIfChanged(0, 0), "none: repeated (0,0) must not be logged");

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "VideoMemoryBudgetLogGateTest passed" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
