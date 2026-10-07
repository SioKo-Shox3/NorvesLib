// VRAM の予算をプールへ割り振る計算（VideoMemoryBudgetManager）の契約テスト。
// 上限 = min(ヒープの予算, --vram-budget-mb) からプール以外の使用量を引いた量が VT の目標になり、
// どの組み合わせでも負（符号なしの折り返し）にならないことを、GPU を使わず値を制御して確かめる。
#include "Rendering/VideoMemoryBudgetManager.h"

#include <cstdint>
#include <iostream>

namespace NorvesLib
{
namespace
{

using Core::Rendering::VideoMemoryBudgetInput;
using Core::Rendering::VideoMemoryBudgetManager;
using Core::Rendering::VideoMemoryBudgetResult;
using Core::Rendering::VideoMemoryPool;

constexpr uint64_t Mb = 1024ull * 1024ull;
constexpr uint32_t VtIndex = static_cast<uint32_t>(VideoMemoryPool::VirtualTexture);
constexpr uint32_t GeometryIndex = static_cast<uint32_t>(VideoMemoryPool::Geometry);
constexpr uint32_t ShadowMapIndex = static_cast<uint32_t>(VideoMemoryPool::ShadowMap);

int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "VideoMemoryBudgetManagerTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

VideoMemoryBudgetInput MakeInput(uint64_t heapBudgetMb, uint64_t heapUsageMb, uint64_t capMb, uint64_t vtPoolMb)
{
    VideoMemoryBudgetInput input;
    input.bHeapValid = true;
    input.HeapBudgetBytes = heapBudgetMb * Mb;
    input.HeapUsageBytes = heapUsageMb * Mb;
    input.CapBytes = capMb * Mb;
    input.PoolCapacityBytes[VtIndex] = vtPoolMb * Mb;
    return input;
}

void TestHeapBudgetOnly()
{
    const VideoMemoryBudgetManager manager;
    // 使用量 3000 のうち VT のプールが 1000 なので、プール以外は 2000。目標 = 8000 - 2000
    const VideoMemoryBudgetResult result = manager.Compute(MakeInput(8000, 3000, 0, 1000));
    Expect(result.bLimited, "ヒープの予算だけでも上限がある");
    Expect(result.CeilingBytes == 8000 * Mb, "上限はヒープの予算");
    Expect(result.NonPoolBytes == 2000 * Mb, "プール以外 = ヒープの使用量 - プールの確保分");
    Expect(result.AvailableBytes == 6000 * Mb, "割り振れる量 = 上限 - プール以外");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 6000 * Mb, "VT の目標が残り全部");
    Expect(result.GetTargetBytes(VideoMemoryPool::Geometry) == 0, "ジオメトリは取り分なし");
    Expect(result.GetTargetBytes(VideoMemoryPool::ShadowMap) == 0, "VSM は取り分なし");
}

void TestCapIsSmaller()
{
    const VideoMemoryBudgetManager manager;
    const VideoMemoryBudgetResult result = manager.Compute(MakeInput(8000, 3000, 4096, 1000));
    Expect(result.CeilingBytes == 4096 * Mb, "引数の上限がヒープの予算より小さければ引数が上限");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == (4096 - 2000) * Mb, "目標 = 引数の上限 - プール以外");
}

void TestCapIsLarger()
{
    const VideoMemoryBudgetManager manager;
    const VideoMemoryBudgetResult result = manager.Compute(MakeInput(8000, 3000, 16000, 1000));
    Expect(result.CeilingBytes == 8000 * Mb, "引数の上限がヒープの予算より大きければヒープの予算が上限");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 6000 * Mb, "目標はヒープの予算で決まる");
}

void TestNonPoolExceedsCeiling()
{
    const VideoMemoryBudgetManager manager;
    // プール以外だけで上限を超えている: 目標は 0 で止まり、符号なしの折り返しで巨大にならない
    const VideoMemoryBudgetResult result = manager.Compute(MakeInput(8000, 9000, 0, 0));
    Expect(result.bLimited, "上限はある");
    Expect(result.NonPoolBytes == 9000 * Mb, "プール以外 9000");
    Expect(result.AvailableBytes == 0, "割り振れる量は 0 で止まる");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 0, "目標は負にならず 0");

    const VideoMemoryBudgetResult capped = manager.Compute(MakeInput(8000, 3000, 1500, 1000));
    Expect(capped.NonPoolBytes == 2000 * Mb, "引数の上限 1500 < プール以外 2000");
    Expect(capped.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 0, "引数の上限がプール以外を下回っても 0");
}

void TestPoolLargerThanUsage()
{
    const VideoMemoryBudgetManager manager;
    // プールの確保分が使用量より大きい（取得の時刻のずれ）: プール以外は 0 で止まる
    const VideoMemoryBudgetResult result = manager.Compute(MakeInput(8000, 500, 0, 1000));
    Expect(result.NonPoolBytes == 0, "プール以外は負にならず 0");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 8000 * Mb, "目標は上限そのもの");
}

void TestCapOnlyWithoutHeap()
{
    const VideoMemoryBudgetManager manager;
    VideoMemoryBudgetInput input;
    input.bHeapValid = false;
    input.CapBytes = 2000 * Mb;
    const VideoMemoryBudgetResult result = manager.Compute(input);
    Expect(result.bLimited, "引数の上限だけでも上限がある");
    Expect(result.CeilingBytes == 2000 * Mb, "上限は引数");
    Expect(result.bNonPoolEstimated, "ヒープの使用量が取れないときは見込み");
    Expect(result.NonPoolBytes == 600 * Mb, "ヒープの使用量が取れないときは上限の30%を見込む");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 1400 * Mb, "目標 = 引数の上限 - 見込み");

    // プールを確保していても見込みは変わらない（プール以外は上限の割合で決まる）
    input.PoolCapacityBytes[VtIndex] = 500 * Mb;
    const VideoMemoryBudgetResult withPool = manager.Compute(input);
    Expect(withPool.NonPoolBytes == 600 * Mb, "見込みはプールの確保分に依らない");

    // ヒープの使用量が取れるときは見込みを使わない
    input.bHeapValid = true;
    input.HeapBudgetBytes = 0;
    input.HeapUsageBytes = 700 * Mb;
    const VideoMemoryBudgetResult heapUsage = manager.Compute(input);
    Expect(heapUsage.NonPoolBytes == 200 * Mb, "ヒープの使用量が取れるときはそこからプールの確保分を引く");
    Expect(!heapUsage.bNonPoolEstimated, "ヒープの使用量が取れるときは見込みではない");
    Expect(heapUsage.CeilingBytes == 2000 * Mb, "予算 0 は異常値として上限に使わない");
}

void TestEstimateNeverNegativeOrOverflow()
{
    const VideoMemoryBudgetManager manager;
    VideoMemoryBudgetInput input;
    input.bHeapValid = false;

    // 端数のある上限でも、見込みは上限を超えず目標は負にならない
    input.CapBytes = 1;
    const VideoMemoryBudgetResult tiny = manager.Compute(input);
    Expect(tiny.NonPoolBytes <= tiny.CeilingBytes, "見込みは上限を超えない");
    Expect(tiny.AvailableBytes == tiny.CeilingBytes - tiny.NonPoolBytes, "割り振れる量 = 上限 - 見込み");

    // 巨大な上限でも掛け算が溢れない
    input.CapBytes = UINT64_MAX;
    const VideoMemoryBudgetResult huge = manager.Compute(input);
    Expect(huge.NonPoolBytes <= UINT64_MAX / 100 * 30 + 30, "巨大な上限でも見込みが溢れない");
    Expect(huge.NonPoolBytes + huge.AvailableBytes == UINT64_MAX, "見込みと割り振れる量の和は上限");
}

void TestDeviceLocalHeapSizeCeiling()
{
    const VideoMemoryBudgetManager manager;

    // ヒープの予算も引数も無いときは、DeviceLocal のヒープの大きさを上限にして30%を見込む
    VideoMemoryBudgetInput input;
    input.bHeapValid = false;
    input.DeviceLocalHeapBytes = 2000 * Mb;
    const VideoMemoryBudgetResult result = manager.Compute(input);
    Expect(result.bLimited, "ヒープの大きさだけでも上限がある");
    Expect(result.CeilingBytes == 2000 * Mb, "上限はヒープの大きさ");
    Expect(result.bNonPoolEstimated, "ヒープの使用量が取れないので見込み");
    Expect(result.NonPoolBytes == 600 * Mb, "ヒープの大きさの30%を見込む");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 1400 * Mb, "目標 = ヒープの大きさ - 見込み");

    // 引数の上限があるときは、ヒープの大きさより引数を優先する
    input.CapBytes = 1000 * Mb;
    const VideoMemoryBudgetResult capped = manager.Compute(input);
    Expect(capped.CeilingBytes == 1000 * Mb, "引数があればヒープの大きさより引数");
    Expect(capped.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 700 * Mb, "引数の上限から30%を引く");

    // ヒープの予算があるときも、ヒープの大きさは使わない（大きさが予算より大きくても予算が上限）
    VideoMemoryBudgetInput withBudget = MakeInput(1500, 300, 0, 0);
    withBudget.DeviceLocalHeapBytes = 8000 * Mb;
    const VideoMemoryBudgetResult budgeted = manager.Compute(withBudget);
    Expect(budgeted.CeilingBytes == 1500 * Mb, "ヒープの予算があれば予算が上限");
    Expect(!budgeted.bNonPoolEstimated && budgeted.NonPoolBytes == 300 * Mb, "使用量が取れるときは使用量から出す");

    // 拡張があって予算が 0（異常値）のときは、ヒープの大きさを上限にし、プール以外は使用量から出す
    VideoMemoryBudgetInput zeroBudget = MakeInput(0, 500, 0, 100);
    zeroBudget.DeviceLocalHeapBytes = 2000 * Mb;
    const VideoMemoryBudgetResult zero = manager.Compute(zeroBudget);
    Expect(zero.CeilingBytes == 2000 * Mb, "予算 0 のときはヒープの大きさを上限にする");
    Expect(!zero.bNonPoolEstimated && zero.NonPoolBytes == 400 * Mb, "使用量が取れるので見込まない");

    // 巨大なヒープでも溢れず、負にならない
    VideoMemoryBudgetInput huge;
    huge.DeviceLocalHeapBytes = UINT64_MAX;
    const VideoMemoryBudgetResult hugeResult = manager.Compute(huge);
    Expect(hugeResult.NonPoolBytes + hugeResult.AvailableBytes == UINT64_MAX, "巨大なヒープでも見込みと割り振れる量の和は上限");
}

void TestUnlimited()
{
    const VideoMemoryBudgetManager manager;
    VideoMemoryBudgetInput input;
    input.bHeapValid = false;
    const VideoMemoryBudgetResult result = manager.Compute(input);
    Expect(!result.bLimited, "取得手段も引数も無ければ上限なし");
    Expect(result.CeilingBytes == 0 && result.AvailableBytes == 0, "上限なしでは上限も割り振りも 0");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 0, "上限なしでは目標を持たない");
    Expect(result.NonPoolBytes == 0 && !result.bNonPoolEstimated, "上限が無ければ見込む元が無く、プール以外は 0");
}

void TestShares()
{
    VideoMemoryBudgetManager manager;
    manager.SetPoolShare(VideoMemoryPool::VirtualTexture, 3);
    manager.SetPoolShare(VideoMemoryPool::Geometry, 1);
    const VideoMemoryBudgetResult result = manager.Compute(MakeInput(8000, 2000, 0, 0));
    Expect(result.AvailableBytes == 6000 * Mb, "割り振れる量 6000");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 4500 * Mb, "VT は 3/4");
    Expect(result.GetTargetBytes(VideoMemoryPool::Geometry) == 1500 * Mb, "ジオメトリは 1/4");
    Expect(result.GetTargetBytes(VideoMemoryPool::ShadowMap) == 0, "VSM は 0");

    uint64_t sum = 0;
    for (uint32_t i = 0; i < Core::Rendering::VideoMemoryPoolCount; ++i)
    {
        sum += result.PoolTargetBytes[i];
    }
    Expect(sum <= result.AvailableBytes, "取り分の合計は割り振れる量を超えない");

    // 重みの上限と、全部 0 の場合
    manager.SetPoolShare(VideoMemoryPool::VirtualTexture, 0xFFFFFFFFu);
    Expect(manager.GetPoolShare(VideoMemoryPool::VirtualTexture) == VideoMemoryBudgetManager::MaxShareWeight, "重みは上限へ丸める");
    manager.SetPoolShare(VideoMemoryPool::VirtualTexture, 0);
    manager.SetPoolShare(VideoMemoryPool::Geometry, 0);
    const VideoMemoryBudgetResult none = manager.Compute(MakeInput(8000, 2000, 0, 0));
    Expect(none.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 0, "重みが全部 0 なら誰も受け取らない");

    // 巨大な値でも溢れない
    VideoMemoryBudgetManager big;
    big.SetPoolShare(VideoMemoryPool::VirtualTexture, VideoMemoryBudgetManager::MaxShareWeight);
    big.SetPoolShare(VideoMemoryPool::Geometry, VideoMemoryBudgetManager::MaxShareWeight);
    VideoMemoryBudgetInput huge;
    huge.bHeapValid = true;
    huge.HeapBudgetBytes = UINT64_MAX / 2;
    const VideoMemoryBudgetResult bigResult = big.Compute(huge);
    Expect(bigResult.GetTargetBytes(VideoMemoryPool::VirtualTexture) <= UINT64_MAX / 4 + 2, "巨大な予算でも掛け算が溢れない");
}

// VSM の物理ページのプール（ShadowMap の枠）の確保量は、ヒープの使用量に全部入っているので、プール以外の使用量から引かれる。
// 取り分（重み）を持たないので固定の取り置きとして割り振れる量からも引かれ、VT・ジオメトリの目標は渡す前と変わらない
// （引かないと、ヒープの中の VSM の分を VT・ジオメトリへ二重に割り振り、計画の合計が上限を超える）。
// ヒープの使用量を下回る分では負にならず 0 で止まり、ヒープの使用量が取れないときも取り置きは引く
void TestShadowMapPoolIsSubtractedFromNonPool()
{
    VideoMemoryBudgetManager manager;
    manager.SetPoolShare(VideoMemoryPool::VirtualTexture, 1);
    manager.SetPoolShare(VideoMemoryPool::Geometry, 1);

    // VSM のプールを渡さない: 使用量 3000 のうち VT のプールが 1000 で、プール以外は 2000
    VideoMemoryBudgetInput input = MakeInput(8000, 3000, 0, 1000);
    const VideoMemoryBudgetResult without = manager.Compute(input);
    Expect(without.NonPoolBytes == 2000 * Mb, "VSM のプールを渡さなければプール以外は 2000");
    Expect(without.AvailableBytes == 6000 * Mb, "割り振れる量は 6000");
    Expect(without.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 3000 * Mb, "VT の目標は半分");
    Expect(without.GetTargetBytes(VideoMemoryPool::Geometry) == 3000 * Mb, "ジオメトリの目標は半分");

    // 256 MB の VSM のプールを渡す: プール以外が 256 減り、同じ 256 を固定の取り置きとして引くので、VT・ジオメトリの目標は変わらない
    input.PoolCapacityBytes[ShadowMapIndex] = 256 * Mb;
    const VideoMemoryBudgetResult with = manager.Compute(input);
    Expect(with.NonPoolBytes == 1744 * Mb, "プール以外 = 使用量 - VT - VSM のプール");
    Expect(with.FixedPoolBytes == 256 * Mb, "取り分の無い VSM のプールは固定の取り置き");
    Expect(with.AvailableBytes == 6000 * Mb, "割り振れる量 = 上限 - プール以外 - 取り置き");
    Expect(with.GetTargetBytes(VideoMemoryPool::VirtualTexture) == without.GetTargetBytes(VideoMemoryPool::VirtualTexture), "VSM の確保量を渡しても VT の目標は増えない");
    Expect(with.GetTargetBytes(VideoMemoryPool::Geometry) == without.GetTargetBytes(VideoMemoryPool::Geometry), "VSM の確保量を渡してもジオメトリの目標は増えない");
    Expect(with.GetTargetBytes(VideoMemoryPool::ShadowMap) == 0, "VSM は取り分を持たない（確保量を渡しても目標は 0 のまま）");
    Expect(with.NonPoolBytes + with.FixedPoolBytes + with.GetTargetBytes(VideoMemoryPool::VirtualTexture) +
                   with.GetTargetBytes(VideoMemoryPool::Geometry) <=
               with.CeilingBytes,
           "プール以外・取り置き・目標の合計が上限を超えない");

    // 3 つのプールの確保量はそれぞれ別に引かれ、合計がヒープの使用量を超えるときは 0 で止まる
    input.PoolCapacityBytes[GeometryIndex] = 500 * Mb;
    const VideoMemoryBudgetResult three = manager.Compute(input);
    Expect(three.NonPoolBytes == 1244 * Mb, "VT・ジオメトリ・VSM の確保量をすべて引く");
    Expect(three.AvailableBytes == 6500 * Mb, "取り分のある VT・ジオメトリの確保量は割り振れる量に戻り、VSM の取り置きだけを引く");
    input.PoolCapacityBytes[ShadowMapIndex] = 4000 * Mb;
    const VideoMemoryBudgetResult over = manager.Compute(input);
    Expect(over.NonPoolBytes == 0, "確保量の合計が使用量より大きければプール以外は 0 で止まる");
    Expect(over.AvailableBytes == 4000 * Mb, "割り振れる量 = 上限 - VSM の取り置き");
    input.PoolCapacityBytes[ShadowMapIndex] = 9000 * Mb;
    const VideoMemoryBudgetResult beyond = manager.Compute(input);
    Expect(beyond.AvailableBytes == 0, "取り置きが上限を超えれば割り振れる量は 0 で止まる");

    // ヒープの使用量が取れないときは、プール以外は見込み（上限の 30%）で VSM のプールに依らず、取り置きは引く
    VideoMemoryBudgetInput estimated;
    estimated.bHeapValid = false;
    estimated.CapBytes = 2000 * Mb;
    const VideoMemoryBudgetResult estimatedWithout = manager.Compute(estimated);
    estimated.PoolCapacityBytes[ShadowMapIndex] = 256 * Mb;
    const VideoMemoryBudgetResult estimatedWith = manager.Compute(estimated);
    Expect(estimatedWithout.NonPoolBytes == 600 * Mb && estimatedWith.NonPoolBytes == 600 * Mb, "見込みは VSM のプールに依らない");
    Expect(estimatedWithout.AvailableBytes == 1400 * Mb && estimatedWith.AvailableBytes == 1144 * Mb, "見込みのときも VSM の取り置きを引く");

    // 取り分（重み）を与えれば、VSM も割り振りを受け取る
    manager.SetPoolShare(VideoMemoryPool::ShadowMap, 2);
    VideoMemoryBudgetInput shared = MakeInput(8000, 3000, 0, 1000);
    shared.PoolCapacityBytes[ShadowMapIndex] = 256 * Mb;
    const VideoMemoryBudgetResult shares = manager.Compute(shared);
    Expect(shares.AvailableBytes == 6256 * Mb, "割り振れる量は同じ");
    Expect(shares.GetTargetBytes(VideoMemoryPool::ShadowMap) == 3128 * Mb, "VSM は 2/4");
    Expect(shares.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 1564 * Mb, "VT は 1/4");
}

void TestLogGate()
{
    VideoMemoryBudgetManager manager;
    const VideoMemoryBudgetResult base = manager.Compute(MakeInput(8000, 3000, 0, 1000));
    Expect(manager.CommitLogIfChanged(base), "初回は必ず出す");
    Expect(!manager.CommitLogIfChanged(base), "同じ値では出さない");

    // 1% 未満の変化は出さない（プール以外 2000 → 2010 = 0.5%、目標 6000 → 5990 = 0.17%）
    const VideoMemoryBudgetResult small = manager.Compute(MakeInput(8000, 3010, 0, 1000));
    Expect(!manager.CommitLogIfChanged(small), "1% 未満の変化では出さない");

    const VideoMemoryBudgetResult changed = manager.Compute(MakeInput(8000, 3500, 0, 1000));
    Expect(manager.CommitLogIfChanged(changed), "プール以外が 1% 以上変われば出す");
    Expect(!manager.CommitLogIfChanged(changed), "出した後は同じ値で出さない");

    const VideoMemoryBudgetResult capped = manager.Compute(MakeInput(8000, 3500, 4096, 1000));
    Expect(manager.CommitLogIfChanged(capped), "上限が変われば出す");

    // 同じ上限・同じ大きさでも、ヒープの値から見込みへ変わればログへ出す（source が変わる）
    VideoMemoryBudgetInput estimateInput;
    estimateInput.CapBytes = 4096 * Mb;
    estimateInput.bHeapValid = false;
    const VideoMemoryBudgetResult estimated = manager.Compute(estimateInput);
    Expect(manager.CommitLogIfChanged(estimated), "見込みへ変われば出す");
    Expect(!manager.CommitLogIfChanged(estimated), "見込みのままなら出さない");

    VideoMemoryBudgetInput unlimitedInput;
    const VideoMemoryBudgetResult unlimited = manager.Compute(unlimitedInput);
    Expect(manager.CommitLogIfChanged(unlimited), "上限の有無が変われば出す");
}

int RunTest()
{
    TestHeapBudgetOnly();
    TestCapIsSmaller();
    TestCapIsLarger();
    TestNonPoolExceedsCeiling();
    TestPoolLargerThanUsage();
    TestCapOnlyWithoutHeap();
    TestEstimateNeverNegativeOrOverflow();
    TestDeviceLocalHeapSizeCeiling();
    TestUnlimited();
    TestShares();
    TestShadowMapPoolIsSubtractedFromNonPool();
    TestLogGate();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "VideoMemoryBudgetManagerTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
