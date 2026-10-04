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
    input.CapBytes = 2048 * Mb;
    input.LedgerNonPoolBytes = 600 * Mb;
    const VideoMemoryBudgetResult result = manager.Compute(input);
    Expect(result.bLimited, "引数の上限だけでも上限がある");
    Expect(result.CeilingBytes == 2048 * Mb, "上限は引数");
    Expect(result.NonPoolBytes == 600 * Mb, "ヒープの使用量が取れないときは台帳の値");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == (2048 - 600) * Mb, "目標 = 引数の上限 - 台帳");

    // ヒープの使用量が取れるときは台帳の値を使わない
    input.bHeapValid = true;
    input.HeapBudgetBytes = 0;
    input.HeapUsageBytes = 700 * Mb;
    const VideoMemoryBudgetResult heapUsage = manager.Compute(input);
    Expect(heapUsage.NonPoolBytes == 700 * Mb, "ヒープの使用量が取れるときはそれを使う");
    Expect(heapUsage.CeilingBytes == 2048 * Mb, "予算 0 は異常値として上限に使わない");
}

void TestUnlimited()
{
    const VideoMemoryBudgetManager manager;
    VideoMemoryBudgetInput input;
    input.bHeapValid = false;
    input.LedgerNonPoolBytes = 100 * Mb;
    const VideoMemoryBudgetResult result = manager.Compute(input);
    Expect(!result.bLimited, "取得手段も引数も無ければ上限なし");
    Expect(result.CeilingBytes == 0 && result.AvailableBytes == 0, "上限なしでは上限も割り振りも 0");
    Expect(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) == 0, "上限なしでは目標を持たない");
    Expect(result.NonPoolBytes == 100 * Mb, "上限なしでもプール以外は数える");
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
    TestUnlimited();
    TestShares();
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
