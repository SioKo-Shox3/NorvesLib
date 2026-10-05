// ジオメトリの共有プール（GeometryPool・GeometryPoolAllocator）の契約テスト。
// 区画の確保・解放・隣の空きとの結合・整列・断片化の統計、塊の追加と上限・確保できないときの失敗、
// 解放した区画が GpuRetireQueue を通って最後に使った提出の serial の完了まで空きへ戻らないこと（提出順に戻ること）を、
// GPU を使わない偽のバッファで確かめる。
#include "Container/PointerTypes.h"
#include "Rendering/GeometryPool.h"
#include "Rendering/GeometryPoolAllocator.h"
#include "Rendering/GpuRetireQueue.h"
#include "RHI/IBuffer.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace NorvesLib
{
namespace
{

using Core::Container::MakeShared;
using Core::Container::TSharedPtr;
using Core::Rendering::GeometryAllocation;
using Core::Rendering::GeometryPool;
using Core::Rendering::GeometryPoolAllocator;
using Core::Rendering::GeometryPoolStats;
using Core::Rendering::GpuRetireQueue;
using Core::Rendering::IGeometryBlockFactory;

constexpr uint64_t KiB = 1024ull;

int g_failures = 0;
// 生きている偽バッファの数（破棄されたかどうかの観測に使う）
int g_liveBuffers = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "GeometryPoolAllocatorTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

class FakeBlockBuffer final : public RHI::IBuffer
{
public:
    explicit FakeBlockBuffer(uint64_t size)
        : Size(size)
    {
        ++g_liveBuffers;
    }
    ~FakeBlockBuffer() override { --g_liveBuffers; }

    uint64_t GetSize() const override { return Size; }
    void* Map(uint64_t, uint64_t) override { return nullptr; }
    void Unmap() override {}
    void Update(const void*, uint64_t, uint64_t) override {}
    RHI::ResourceUsage GetUsage() const override { return RHI::ResourceUsage::StorageBuffer; }

    uint64_t Size = 0;
};

class FakeBlockFactory final : public IGeometryBlockFactory
{
public:
    RHI::BufferPtr CreateBlock(uint64_t sizeBytes) override
    {
        if (bFail)
        {
            return nullptr;
        }
        ++CreatedCount;
        LastSizeBytes = sizeBytes;
        return MakeShared<FakeBlockBuffer>(sizeBytes);
    }

    bool bFail = false;
    int CreatedCount = 0;
    uint64_t LastSizeBytes = 0;
};

void TestSequentialAllocationAndCoalescing()
{
    GeometryPoolAllocator allocator;
    Expect(allocator.AddBlock(1024) == 0, "最初の塊の番号は 0");

    // 大きさは粒度（16）の倍数へ切り上げる
    const GeometryAllocation a = allocator.Allocate(100, 1);
    const GeometryAllocation b = allocator.Allocate(100, 1);
    const GeometryAllocation c = allocator.Allocate(100, 1);
    Expect(a.IsValid() && b.IsValid() && c.IsValid(), "3 つ確保できる");
    Expect(a.SizeBytes == 112, "100 バイトは粒度の倍数 112 へ切り上がる");
    Expect(a.OffsetBytes == 0 && b.OffsetBytes == 112 && c.OffsetBytes == 224, "先頭から詰めて置く");

    GeometryPoolStats stats = allocator.GetStats();
    Expect(stats.UsedBytes == 336 && stats.FreeBytes == 1024 - 336, "使用量と空きの合計");
    Expect(stats.FreeRangeCount == 1 && stats.AllocationCount == 3, "空きは末尾の 1 つ・確保は 3 つ");

    // 真ん中を返すと空きが 2 つに割れ、両隣を返すと全部が 1 つへ結合する
    Expect(allocator.Free(b), "b を返せる");
    stats = allocator.GetStats();
    Expect(stats.FreeRangeCount == 2, "真ん中を返すと空きが 2 つ");
    Expect(stats.LargestFreeBytes == 1024 - 336, "最大の空きは末尾の側");
    Expect(stats.GetFragmentation() > 0.0, "空きが割れていれば断片化は 0 より大きい");

    Expect(allocator.Free(a), "a を返せる");
    stats = allocator.GetStats();
    Expect(stats.FreeRangeCount == 2, "a を返すと b と結合して先頭の空きは 1 つ（末尾と合わせて 2 つ）");
    Expect(stats.LargestFreeBytes == 1024 - 336, "先頭の空きは 224 バイト");

    Expect(allocator.Free(c), "c を返せる");
    stats = allocator.GetStats();
    Expect(stats.FreeRangeCount == 1 && stats.LargestFreeBytes == 1024 && stats.UsedBytes == 0,
           "全部返すと空きは塊全体の 1 つへ結合する");
    Expect(stats.GetFragmentation() == 0.0, "空きが 1 つなら断片化は 0");

    const GeometryAllocation whole = allocator.Allocate(1024, 1);
    Expect(whole.IsValid() && whole.OffsetBytes == 0 && whole.SizeBytes == 1024, "結合した後は塊全体を 1 区画で確保できる");
}

void TestCoalescingBothNeighbors()
{
    GeometryPoolAllocator allocator;
    allocator.AddBlock(1024);
    GeometryAllocation regions[4];
    for (GeometryAllocation& region : regions)
    {
        region = allocator.Allocate(256, 1);
    }
    Expect(allocator.GetStats().FreeBytes == 0, "塊を使い切った");

    // 0 と 2 を返してから 1 を返すと、前後の空きと同時に結合して 0..768 の 1 つになる
    allocator.Free(regions[0]);
    allocator.Free(regions[2]);
    Expect(allocator.GetStats().FreeRangeCount == 2, "離れた空きが 2 つ");
    allocator.Free(regions[1]);
    const GeometryPoolStats stats = allocator.GetStats();
    Expect(stats.FreeRangeCount == 1 && stats.LargestFreeBytes == 768, "両隣と同時に結合して 768 バイトの空き 1 つ");
    Expect(allocator.Allocate(768, 1).IsValid(), "結合した空きに 768 バイトが収まる");
}

void TestAlignment()
{
    GeometryPoolAllocator allocator;
    allocator.AddBlock(4096);

    const GeometryAllocation head = allocator.Allocate(100, 1);
    const GeometryAllocation aligned = allocator.Allocate(1000, 256);
    Expect(head.IsValid() && aligned.IsValid(), "整列つきの確保ができる");
    Expect(aligned.OffsetBytes % 256 == 0 && aligned.OffsetBytes == 256, "256 に整列した先頭へ置く");

    // 整列の余白（112〜256）は空きとして残り、小さい区画がそこへ入る（収まる最小の空きを選ぶ）
    const GeometryAllocation filler = allocator.Allocate(100, 16);
    Expect(filler.IsValid() && filler.OffsetBytes == 112, "整列の余白へ小さい区画が入る");

    for (uint64_t alignment : {1ull, 2ull, 16ull, 64ull, 256ull, 1024ull})
    {
        const GeometryAllocation region = allocator.Allocate(32, alignment);
        Expect(region.IsValid() && region.OffsetBytes % alignment == 0, "どの整列でも先頭が倍数");
    }

    Expect(!allocator.Allocate(32, 3).IsValid(), "2 の累乗でない整列は失敗");
    Expect(!allocator.Allocate(0, 16).IsValid(), "大きさ 0 は失敗");
    Expect(allocator.Allocate(32, 0).IsValid(), "整列 0 は 1 と同じ");
}

void TestFailureAndInvalidFree()
{
    GeometryPoolAllocator allocator;
    Expect(!allocator.Allocate(16, 1).IsValid(), "塊が無ければ確保できない");
    Expect(allocator.AddBlock(8) == GeometryAllocation::InvalidBlock, "粒度に満たない塊は足せない");
    allocator.AddBlock(1024);

    const GeometryAllocation whole = allocator.Allocate(1024, 1);
    Expect(whole.IsValid(), "塊全体を確保できる");
    Expect(!allocator.Allocate(16, 1).IsValid(), "空きが無ければ失敗する");
    Expect(!allocator.Allocate(2048, 1).IsValid(), "塊より大きい要求は失敗する");

    GeometryAllocation wrongSize = whole;
    wrongSize.SizeBytes = 512;
    Expect(!allocator.Free(wrongSize), "大きさが食い違う解放は拒否する");
    GeometryAllocation wrongBlock = whole;
    wrongBlock.BlockIndex = 5;
    Expect(!allocator.Free(wrongBlock), "存在しない塊の解放は拒否する");
    Expect(!allocator.Free(GeometryAllocation()), "無効な区画の解放は拒否する");
    Expect(allocator.Free(whole), "正しい解放は通る");
    Expect(!allocator.Free(whole), "二重の解放は拒否する");
    const GeometryPoolStats stats = allocator.GetStats();
    Expect(stats.UsedBytes == 0 && stats.FreeBytes == 1024 && stats.FreeRangeCount == 1, "拒否した解放は状態を変えない");
}

void TestFragmentationBlocksLargeRequest()
{
    GeometryPoolAllocator allocator;
    allocator.AddBlock(1024);
    GeometryAllocation regions[8];
    for (GeometryAllocation& region : regions)
    {
        region = allocator.Allocate(128, 1);
    }
    // 1 つおきに返すと 512 バイト空いているが、連続は 128 バイトまで
    for (int i = 0; i < 8; i += 2)
    {
        allocator.Free(regions[i]);
    }
    GeometryPoolStats stats = allocator.GetStats();
    Expect(stats.FreeBytes == 512 && stats.LargestFreeBytes == 128 && stats.FreeRangeCount == 4,
           "空きの合計は 512・最大の連続は 128・4 つに割れている");
    Expect(stats.GetFragmentation() == 0.75, "断片化 = 1 - 128/512");
    Expect(!allocator.Allocate(256, 1).IsValid(), "合計は足りても連続した空きが無ければ失敗する");

    // 隣を返して結合すると収まる
    allocator.Free(regions[1]);
    stats = allocator.GetStats();
    Expect(stats.LargestFreeBytes == 384, "結合して 384 バイトの連続した空き");
    Expect(allocator.Allocate(256, 1).IsValid(), "結合した後は 256 バイトが収まる");
}

void TestMultipleBlocks()
{
    GeometryPoolAllocator allocator;
    allocator.AddBlock(256);
    const GeometryAllocation first = allocator.Allocate(256, 1);
    Expect(first.IsValid() && first.BlockIndex == 0, "最初の塊を使い切る");
    Expect(!allocator.Allocate(16, 1).IsValid(), "塊を足すまでは失敗する");

    Expect(allocator.AddBlock(512) == 1, "2 つ目の塊の番号は 1");
    const GeometryAllocation second = allocator.Allocate(300, 1);
    Expect(second.IsValid() && second.BlockIndex == 1 && second.OffsetBytes == 0, "足した塊へ置く");
    Expect(allocator.GetBlockSizeBytes(1) == 512 && allocator.GetBlockSizeBytes(9) == 0, "塊の大きさの照会");

    // 収まる最小の空きを選ぶ: 塊 0 を返すと 256 の空きができるが、200 の要求（粒度で 208）は、
    // 塊 1 の残り（512 - 304 = 208）のほうが小さいので、そちらへ入る
    allocator.Free(first);
    const GeometryAllocation third = allocator.Allocate(200, 1);
    Expect(third.IsValid() && third.BlockIndex == 1, "収まる最小の空き（塊 1 の残り）を選ぶ");

    const GeometryPoolStats stats = allocator.GetStats();
    Expect(stats.BlockCount == 2 && stats.CapacityBytes == 768, "塊の数と容量の合計");
}

void TestPoolGrowsLazilyAndFails()
{
    TSharedPtr<FakeBlockFactory> factory = MakeShared<FakeBlockFactory>();
    {
        GeometryPool pool(TSharedPtr<IGeometryBlockFactory>(factory), 4096);
        Expect(factory->CreatedCount == 0 && pool.GetStats().BlockCount == 0, "使うまで塊を作らない");
        Expect(!pool.LogLedgerIfChanged(), "塊が無く一度も出していなければ台帳へ出さない");

        GeometryPool::RegionLease a = pool.Allocate(1000, 256);
        Expect(a.IsValid() && a.GetBuffer() != nullptr, "最初の確保で塊を作る");
        Expect(factory->CreatedCount == 1 && factory->LastSizeBytes == 4096, "塊は既定の大きさ（ここでは 4096）");
        Expect(a.GetOffsetBytes() == 0 && a.GetSizeBytes() == 1008, "区画のオフセットと大きさ（1000 → 1008）");
        Expect(a.GetBuffer()->GetSize() == 4096, "区画のバッファは塊全体");

        GeometryPool::RegionLease b = pool.Allocate(1000, 256);
        Expect(b.IsValid() && b.GetOffsetBytes() == 1024 && b.GetBuffer() == a.GetBuffer(), "同じ塊の続きへ置く");
        Expect(factory->CreatedCount == 1, "収まる間は塊を増やさない");

        // 塊に収まらなくなったら足す
        GeometryPool::RegionLease c = pool.Allocate(3000, 256);
        Expect(c.IsValid() && c.GetBlockIndex() == 1 && factory->CreatedCount == 2, "収まらなければ塊を増やす");
        Expect(c.GetBuffer() != a.GetBuffer(), "新しい塊は別のバッファ");

        // 既定より大きい要求は、その大きさの塊を作る
        GeometryPool::RegionLease big = pool.Allocate(10000, 256);
        Expect(big.IsValid() && factory->LastSizeBytes == 10000, "既定より大きい要求はその大きさの塊を作る");
        Expect(big.GetBuffer()->GetSize() == 10000 && big.GetOffsetBytes() == 0, "大きな塊の先頭に置く");

        // 手放すと空きへ戻り、同じ場所を使い回せる
        const uint64_t usedBefore = pool.GetStats().UsedBytes;
        a.Reset();
        Expect(!a.IsValid() && a.GetBuffer() == nullptr, "Reset した区画は無効");
        Expect(pool.GetStats().UsedBytes == usedBefore - 1008, "手放した分だけ使用量が減る");
        GeometryPool::RegionLease reuse = pool.Allocate(500, 256);
        Expect(reuse.IsValid() && reuse.GetOffsetBytes() == 0 && reuse.GetBlockIndex() == 0, "手放した場所を使い回す");

        // 作れない・上限・不正な要求は無効な区画になる
        factory->bFail = true;
        Expect(!pool.Allocate(100000, 256).IsValid(), "塊を作れなければ失敗する");
        factory->bFail = false;
        Expect(!pool.Allocate(0, 256).IsValid(), "大きさ 0 は失敗");
        Expect(!pool.Allocate(64, 3).IsValid(), "2 の累乗でない整列は失敗");
        const int createdBeforeLimit = factory->CreatedCount;
        pool.SetCapacityLimitBytes(pool.GetStats().CapacityBytes);
        Expect(!pool.Allocate(100000, 256).IsValid() && factory->CreatedCount == createdBeforeLimit, "上限に達したら塊を作らず失敗する");
        pool.SetCapacityLimitBytes(0);
        Expect(pool.Allocate(100000, 256).IsValid(), "上限を外すと作れる");

        // 区画はプールより長く生きても、塊のバッファを生かす
        GeometryPool::RegionLease survivor = pool.Allocate(64, 16);
        Expect(survivor.IsValid(), "生き残らせる区画を確保");
        const int liveWithPool = g_liveBuffers;
        Expect(liveWithPool == 4, "3 つの塊と 100000 バイトの塊のバッファが生きている");
    }
    Expect(g_liveBuffers == 0, "プールと区画を破棄したら塊のバッファも破棄される");

    int createdBefore = factory->CreatedCount;
    GeometryPool::RegionLease outlive;
    {
        GeometryPool pool(TSharedPtr<IGeometryBlockFactory>(factory), 4096);
        outlive = pool.Allocate(64, 16);
        Expect(outlive.IsValid() && factory->CreatedCount == createdBefore + 1, "区画を確保");
    }
    Expect(outlive.IsValid() && outlive.GetBuffer() != nullptr && outlive.GetBuffer()->GetSize() == 4096 && g_liveBuffers == 1,
           "プールが先に破棄されても区画は塊のバッファを使える");
    outlive.Reset();
    Expect(g_liveBuffers == 0, "最後の区画を手放すと塊のバッファが破棄される");

    GeometryPool noFactory{TSharedPtr<IGeometryBlockFactory>()};
    Expect(!noFactory.Allocate(64, 16).IsValid(), "窓口が無ければ失敗する");
}

void TestDeferredReleaseOrder()
{
    TSharedPtr<FakeBlockFactory> factory = MakeShared<FakeBlockFactory>();
    GpuRetireQueue queue;
    {
        GeometryPool pool(TSharedPtr<IGeometryBlockFactory>(factory), 4096);

        // 何も提出していない（serial 0）間は、返却はすぐ空きへ戻る
        GeometryPool::RegionLease early = pool.Allocate(1024, 256);
        queue.Retire(std::move(early));
        Expect(pool.GetStats().UsedBytes == 0 && queue.GetPendingCount() == 0, "提出が無ければ返却はすぐ戻る");

        // serial 1 まで提出済みの状態で、a を返す。完了するまで空きへ戻らない
        queue.BeginFrame(0);
        queue.CommitFrame(1);
        GeometryPool::RegionLease a = pool.Allocate(1024, 256);
        GeometryPool::RegionLease b = pool.Allocate(1024, 256);
        const uint64_t aOffset = a.GetOffsetBytes();
        Expect(aOffset == 0 && b.GetOffsetBytes() == 1024, "a・b を並べて確保");
        queue.Retire(std::move(a));
        Expect(queue.GetPendingCount() == 1, "返却は完了待ちに積まれる");
        Expect(pool.GetStats().UsedBytes == 2048, "完了するまで a の区画は空きへ戻らない");

        // 同じ場所は使い回せない（塊の残りへ置かれる）
        GeometryPool::RegionLease c = pool.Allocate(1024, 256);
        Expect(c.IsValid() && c.GetOffsetBytes() == 2048, "返却待ちの区画の場所は避けて置かれる");

        // serial 2 まで提出した後に b を返す。a は serial 1 の完了で戻り、b は serial 2 の完了まで戻らない
        queue.BeginFrame(0);
        queue.CommitFrame(2);
        queue.Retire(std::move(b));
        Expect(queue.GetPendingCount() == 2, "a と b が完了待ち");

        queue.Collect(0);
        Expect(pool.GetStats().UsedBytes == 3072, "何も完了していなければどちらも戻らない");
        queue.Collect(1);
        Expect(pool.GetStats().UsedBytes == 2048 && queue.GetPendingCount() == 1, "serial 1 の完了で a だけが戻る");
        GeometryPool::RegionLease d = pool.Allocate(1024, 256);
        Expect(d.IsValid() && d.GetOffsetBytes() == aOffset, "戻った a の場所を使える");
        queue.Collect(2);
        Expect(pool.GetStats().UsedBytes == 2048 && queue.GetPendingCount() == 0,
               "serial 2 の完了で b が戻る（残るのは c と d）");

        // 記録中のフレームで頼んだ返却は、そのフレームの serial（確定は Commit）まで延びる
        queue.BeginFrame(2);
        GeometryPool::RegionLease e = pool.Allocate(512, 256);
        const uint64_t eOffset = e.GetOffsetBytes();
        queue.Retire(std::move(e));
        queue.Collect(2);
        Expect(queue.GetPendingCount() == 1, "記録中の返却は serial が決まるまで戻らない");
        queue.CommitFrame(3);
        queue.Collect(2);
        Expect(queue.GetPendingCount() == 1, "serial 3 が完了するまで戻らない");
        const uint64_t usedWithE = pool.GetStats().UsedBytes;
        queue.Collect(3);
        Expect(queue.GetPendingCount() == 0 && pool.GetStats().UsedBytes == usedWithE - 512, "serial 3 の完了で戻る");
        GeometryPool::RegionLease f = pool.Allocate(512, 256);
        Expect(f.IsValid() && f.GetOffsetBytes() == eOffset, "戻った場所を使える");

        // 無効な区画の返却は無視する
        queue.Retire(GeometryPool::RegionLease());
        Expect(queue.GetPendingCount() == 0, "無効な区画は積まない");

        // 完了待ちの区画が残ったままキューを片付けても（Shutdown）、区画は空きへ戻って壊れない
        queue.CommitFrame(4);
        queue.Retire(std::move(f));
        Expect(queue.GetPendingCount() == 1, "もう 1 つ完了待ち");
        queue.Clear();
        Expect(queue.GetPendingCount() == 0, "Clear で全部戻す");
    }
    Expect(g_liveBuffers == 0, "全部片付けたら塊のバッファは残らない");
}

void TestLedgerLog()
{
    TSharedPtr<FakeBlockFactory> factory = MakeShared<FakeBlockFactory>();
    GeometryPool pool(TSharedPtr<IGeometryBlockFactory>(factory), 4096);
    GeometryPool::RegionLease a = pool.Allocate(256, 256);
    Expect(a.IsValid(), "確保");
    // 塊の増設で台帳へ出た時点では使用量が 0 なので、確保した分の変化を出す
    Expect(pool.LogLedgerIfChanged(), "増設の後に増えた使用量を出す");
    Expect(!pool.LogLedgerIfChanged(), "変化が無ければ出さない");
    GeometryPool::RegionLease b = pool.Allocate(256, 256);
    Expect(pool.LogLedgerIfChanged(), "使用量が変われば出す");
    Expect(!pool.LogLedgerIfChanged(), "出した直後は出さない");
    pool.SetBudgetTarget(true, 100ull * 1024ull * 1024ull);
    Expect(pool.LogLedgerIfChanged(), "目標が変われば出す");
    pool.SetBudgetTarget(true, 100ull * 1024ull * 1024ull);
    Expect(!pool.LogLedgerIfChanged(), "同じ目標なら出さない");
    b.Reset();
    Expect(pool.LogLedgerIfChanged(), "手放して使用量が減れば出す");
}

void TestConcurrentAllocateAndFree()
{
    TSharedPtr<FakeBlockFactory> factory = MakeShared<FakeBlockFactory>();
    {
        GeometryPool pool(TSharedPtr<IGeometryBlockFactory>(factory), 64 * KiB);
        std::atomic<int> misaligned{0};
        constexpr int ThreadCount = 4;
        constexpr int Iterations = 3000;
        std::vector<std::thread> threads;
        for (int t = 0; t < ThreadCount; ++t)
        {
            threads.emplace_back([&pool, &misaligned, t]() {
                std::vector<GeometryPool::RegionLease> held;
                uint32_t seed = 12345u + static_cast<uint32_t>(t) * 977u;
                for (int i = 0; i < Iterations; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    const uint64_t size = 16 + (seed >> 8) % 4096;
                    const uint64_t alignment = 1ull << ((seed >> 4) % 9);
                    GeometryPool::RegionLease lease = pool.Allocate(size, alignment);
                    if (lease.IsValid())
                    {
                        if (lease.GetOffsetBytes() % alignment != 0)
                        {
                            ++misaligned;
                        }
                        held.push_back(std::move(lease));
                    }
                    if (!held.empty() && (held.size() > 24 || (seed & 3u) == 0u))
                    {
                        const size_t index = (seed >> 12) % held.size();
                        held.erase(held.begin() + static_cast<std::ptrdiff_t>(index));
                    }
                }
            });
        }
        for (std::thread& thread : threads)
        {
            thread.join();
        }

        const GeometryPoolStats stats = pool.GetStats();
        Expect(misaligned == 0, "並行でも区画の先頭は整列を満たす");
        Expect(stats.UsedBytes == 0 && stats.AllocationCount == 0, "全部手放したら使用量は 0");
        Expect(stats.FreeRangeCount == stats.BlockCount, "全部手放したら空きは塊ごとに 1 つへ結合している");
        Expect(stats.FreeBytes == stats.CapacityBytes, "空きの合計は容量と一致する");
    }
    Expect(g_liveBuffers == 0, "並行の確保・解放の後に塊のバッファが残らない");
}

int RunTest()
{
    TestSequentialAllocationAndCoalescing();
    TestCoalescingBothNeighbors();
    TestAlignment();
    TestFailureAndInvalidFree();
    TestFragmentationBlocksLargeRequest();
    TestMultipleBlocks();
    TestPoolGrowsLazilyAndFails();
    TestDeferredReleaseOrder();
    TestLedgerLog();
    TestConcurrentAllocateAndFree();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "GeometryPoolAllocatorTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
