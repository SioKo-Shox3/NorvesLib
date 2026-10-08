#include "Animation/RootMotionTimeQueue.h"
#include "Library/Core/Private/Engine/FixedStepScheduler.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
using namespace NorvesLib::Core::Animation;
namespace
{
    void Near(const RootMotionDelta& a, const RootMotionDelta& b, double tolerance = 1e-9)
    {
        assert(std::fabs(a.X - b.X) < tolerance);
        assert(std::fabs(a.Z - b.Z) < tolerance);
        assert(std::fabs(a.Yaw - b.Yaw) < tolerance);
    }
    void TestFixedCadence()
    {
        using namespace NorvesLib::Core::Engine;
        for (uint32_t rate : {60u, 120u})
        {
            FixedStepScheduler scheduler;
            assert(scheduler.SetRate(rate));
            scheduler.BeginRun();
            RootMotionTimeQueue queue;
            RootMotionDelta part;
            unsigned zeroFrames = 0;
            bool sawDrop = false;
            for (int64_t frame = 0; frame < 288; ++frame)
            {
                // 世代切替では以前の端数をidentityとして埋め、新しい区間と時刻を揃える。
                if (frame == 150)
                {
                    queue.Clear();
                    const auto remainder = scheduler.GetRemainderScaledUnits();
                    if (remainder)
                        assert(queue.Push({}, remainder));
                }
                const int64_t raw = frame == 50   ? 500000000
                                    : frame == 80 ? 0
                                                  : (frame + 1) * 1000000000 / 144 - frame * 1000000000 / 144;
                if (raw)
                    assert(queue.Push({double(raw) * 1e-9, 0, 0}, uint64_t(raw) * rate));
                const auto result = scheduler.Advance(raw, true);
                assert(result.Status == EFixedStepAdvanceStatus::Advanced);
                if (!result.ExecutedSteps)
                    ++zeroFrames;
                for (uint64_t step = 0; step < result.ExecutedSteps; ++step)
                {
                    // 依存bodyが未準備の区間でも時間を消費し、復帰後の突進を防ぐ。
                    if (frame == 100)
                        queue.Discard(1000000000);
                    else
                    {
                        assert(queue.Consume(1000000000, part));
                        assert(part.X >= 0 && part.X <= 1. / rate + 1e-10);
                        if (frame != 150)
                            assert(std::fabs(part.X - 1. / rate) < 1e-10);
                    }
                }
                sawDrop = sawDrop || result.DroppedSteps != 0;
                queue.Discard(result.DroppedSteps * 1000000000);
                assert(queue.GetRemainingTime() == result.RemainderScaledUnits);
            }
            assert(zeroFrames > 20 && sawDrop);
            scheduler.EndRun();
        }
    }
    uint64_t randomState = 19;
    uint64_t Next()
    {
        randomState = randomState * 6364136223846793005ull + 1;
        return randomState;
    }
} // namespace
int main()
{
    static_assert(!std::is_copy_constructible_v<RootMotionTimeQueue> &&
                  !std::is_move_constructible_v<RootMotionTimeQueue>);
    RootMotionTimeQueue queue;
    RootMotionDelta out{42, 43, 44};
    assert(queue.Peek(10, out));
    Near(out, {});
    assert(!queue.Push({1, 0, 0}, 0));
    assert(!queue.Push({std::numeric_limits<double>::infinity(), 0, 0}, 1));
    assert(!queue.Push({3, -2, 1e16}, 100));
    assert(!queue.Push(
        {3, -2, std::nextafter(RootMotionTimeQueue::MaximumAbsoluteYaw, std::numeric_limits<double>::infinity())},
        100));
    for (double yaw : {0., 1.5707963267948966, 6.283185307179586, -7.2, RootMotionTimeQueue::MaximumAbsoluteYaw,
                       -RootMotionTimeQueue::MaximumAbsoluteYaw})
    {
        const RootMotionDelta full{3, -2, yaw};
        assert(queue.Push(full, 100));
        assert(queue.Peek(100, out));
        Near(out, full);
        assert(queue.GetRemainingTime() == 100);
        RootMotionDelta composed;
        for (uint64_t duration : {1u, 27u, 31u, 41u})
        {
            assert(queue.Consume(duration, out));
            composed = ComposeRootMotion(composed, out);
        }
        Near(composed, full);
        assert(queue.GetRemainingTime() == 0 && queue.GetPendingSegmentCount() == 0);
    }
    // 区間を跨ぐ不規則な分割でも、旋回を含む元の差分列の終端を保つ。
    for (unsigned trial = 0; trial < 1000; ++trial)
    {
        RootMotionDelta expected, actual;
        uint64_t remaining = 0;
        for (unsigned i = 0; i < 8; ++i)
        {
            RootMotionDelta delta{double(int(Next() % 2001) - 1000) / 91, double(int(Next() % 2001) - 1000) / 83,
                                  double(int(Next() % 2001) - 1000) / 107};
            const uint64_t duration = Next() % 10000 + 1;
            assert(queue.Push(delta, duration));
            remaining += duration;
            expected = ComposeRootMotion(expected, delta);
        }
        while (remaining)
        {
            const uint64_t take = std::min(remaining, Next() % 1000 + 1);
            assert(queue.Consume(take, out));
            actual = ComposeRootMotion(actual, out);
            remaining -= take;
            assert(queue.GetRemainingTime() == remaining);
        }
        Near(actual, expected);
    }
    // schedulerのdropや衝突で阻止された区間は、次のframeへ変位を再請求しない。
    const RootMotionDelta motion{4, 2, 1};
    assert(queue.Push(motion, 100));
    queue.Discard(60);
    assert(queue.Consume(100, out));
    Near(out, RootMotionBetween(ScaleRootMotion(motion, .6), motion));
    assert(queue.Consume(100, out));
    Near(out, {});
    // epoch開始時の既存端数とAnimation停止区間をidentityとして供給できる。
    assert(queue.Push({}, 5) && queue.Push({10, 0, 0}, 10));
    assert(queue.Consume(10, out));
    Near(out, {5, 0, 0});
    assert(queue.Consume(10, out));
    Near(out, {5, 0, 0});
    assert(queue.GetRemainingTime() == 0);
    // 終端近傍でも、fraction二点の差の桁落ちで微小区間を失わない。
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    assert(queue.Push({1e19, 0, 0}, maximum));
    assert(!queue.Push({}, 1));
    queue.Discard(maximum - 1);
    assert(queue.Consume(1, out));
    assert(std::fabs(out.X - 1e19 / double(maximum)) < 1e-12);
    // 合成結果が非有限になると出力・消費cursorとも不変。
    const double huge = std::numeric_limits<double>::max();
    assert(queue.Push({huge, 0, 0}, 1) && queue.Push({huge, 0, 0}, 1));
    out = {42, 43, 44};
    assert(!queue.Consume(2, out));
    Near(out, {42, 43, 44});
    assert(queue.GetRemainingTime() == 2 && queue.GetPendingSegmentCount() == 2);
    queue.Clear();
    for (size_t i = 0; i < RootMotionTimeQueue::MaximumSegments; ++i)
        assert(queue.Push({}, 1));
    assert(!queue.Push({}, 1));
    queue.Discard(1);
    assert(queue.Push({1, 0, 0}, 1));
    assert(queue.GetPendingSegmentCount() == RootMotionTimeQueue::MaximumSegments);
    assert(queue.Consume(maximum, out));
    Near(out, {1, 0, 0});
    assert(queue.GetRemainingTime() == 0);
    TestFixedCadence();
    std::cout << "RootMotionTimeQueueTest passed\n";
    return 0;
}
