// 非同期の列の優先度、実行枠、取り消し、世代と受け渡しの一意性を確かめる。
#include "Thread/FrameAsyncQueue.h"
#include "Thread/JobSystem.h"
#include "Thread/Mutex.h"
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <utility>

namespace
{
    using namespace NorvesLib::Thread;
    using namespace NorvesLib::Core::Container;
    using Clock = std::chrono::steady_clock;

    int Failures = 0;

    void Check(bool bPassed, const char* description)
    {
        if (!bPassed)
        {
            ++Failures;
            std::cerr << "FrameAsyncQueueTest 失敗: " << description << std::endl;
        }
    }

    template<typename Predicate>
    bool Await(Predicate predicate, const char* description)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!predicate())
        {
            if (Clock::now() >= deadline)
            {
                Check(false, description);
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    void Drain(FrameAsyncQueue& queue)
    {
        Await([&]()
        {
            queue.DeliverCompleted();
            const auto stats = queue.GetStats();
            return stats.Pending == 0 && stats.Running == 0 && stats.Completed == 0;
        }, "列が制限時間内に空になりませんでした");
    }

    struct Receipt
    {
        int Delivered = 0;
        int Discarded = 0;
        int Destroyed = 0;
        FrameAsyncOutcome Outcome = FrameAsyncOutcome::Delivered;
        std::thread::id GameThread = std::this_thread::get_id();
        bool bWrongThread = false;
    };

    template<typename ExecuteFunction, typename DeliverFunction>
    class ReceiptJob final : public IFrameAsyncJob
    {
    public:
        ReceiptJob(Receipt& receipt, ExecuteFunction execute, DeliverFunction deliver)
            : m_receipt(receipt), m_execute(std::move(execute)), m_deliver(std::move(deliver))
        {
        }

        ~ReceiptJob() override
        {
            ++m_receipt.Destroyed;
            CheckThread();
        }

        void Execute(const FrameAsyncExecuteContext& context) override
        {
            m_execute(context);
        }

        void Deliver() override
        {
            CheckThread();
            ++m_receipt.Delivered;
            m_deliver();
        }

        void Discard(FrameAsyncOutcome outcome) override
        {
            CheckThread();
            ++m_receipt.Discarded;
            m_receipt.Outcome = outcome;
        }

    private:
        void CheckThread()
        {
            m_receipt.bWrongThread |= m_receipt.GameThread != std::this_thread::get_id();
        }

        Receipt& m_receipt;
        ExecuteFunction m_execute;
        DeliverFunction m_deliver;
    };

    template<typename ExecuteFunction, typename DeliverFunction>
    TUniquePtr<IFrameAsyncJob> NewJob(Receipt& receipt, ExecuteFunction execute, DeliverFunction deliver)
    {
        return MakeUnique<ReceiptJob<ExecuteFunction, DeliverFunction>>(receipt, std::move(execute), std::move(deliver));
    }

    template<typename ExecuteFunction>
    TUniquePtr<IFrameAsyncJob> NewJob(Receipt& receipt, ExecuteFunction execute)
    {
        return NewJob(receipt, std::move(execute), []() {});
    }

    auto EmptyExecute = [](const FrameAsyncExecuteContext&) {};

    struct Gate
    {
        Atomic<bool> bStarted{false};
        Atomic<bool> bRelease{false};
        Atomic<bool> bSawCancellation{false};
        Atomic<bool> bEnded{false};

        void Execute(const FrameAsyncExecuteContext& context, bool bDelayAfterCancellation = false)
        {
            bStarted.Store(true);
            const auto deadline = Clock::now() + std::chrono::seconds(10);
            while (!bRelease.Load() && !context.IsCancelRequested() && Clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            bSawCancellation.Store(context.IsCancelRequested());
            if (bDelayAfterCancellation)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            bEnded.Store(true);
        }

        void AwaitStart()
        {
            Await([&]() { return bStarted.Load(); }, "塞ぐ要求が始まりませんでした");
        }
    };

    void CheckDiscard(const Receipt& receipt, FrameAsyncOutcome outcome)
    {
        Check(receipt.Delivered == 0 && receipt.Discarded == 1 && receipt.Outcome == outcome &&
              receipt.Destroyed == 1 && !receipt.bWrongThread,
              "要求がゲームスレッドで指定の理由により一度だけ破棄されませんでした");
    }

    void PriorityCase()
    {
        std::cout << "ケース1: 優先度と同順位の投入順" << std::endl;
        Receipt blocker;
        Receipt receipts[5];
        Gate gate;
        Mutex orderMutex;
        VariableArray<int> order;
        FrameAsyncQueue queue(nullptr, {1});
        queue.Submit(NewJob(blocker, [&](const auto& context) { gate.Execute(context); }), 0, {});
        gate.AwaitStart();
        const int priorities[] = {1, 9, 9, -2, 4};
        for (int index = 0; index < 5; ++index)
        {
            queue.Submit(NewJob(receipts[index], [&, index](const auto&)
            {
                ScopedLock lock(orderMutex);
                order.push_back(index);
            }), priorities[index], {});
        }
        gate.bRelease.Store(true);
        Drain(queue);
        const int expected[] = {1, 2, 4, 0, 3};
        {
            ScopedLock lock(orderMutex);
            Check(order.size() == 5, "開始した要求の数が違います");
            for (size_t index = 0; index < order.size(); ++index)
            {
                Check(index < 5 && order[index] == expected[index], "優先度または同順位の投入順が違います");
            }
        }
        Check(queue.GetStats().PeakRunning == 1, "同時実行数1の枠を超えました");
    }

    void ConcurrentCase()
    {
        std::cout << "ケース2: 同時実行数の上限" << std::endl;
        Receipt receipts[48];
        Atomic<int> running{0};
        Atomic<int> peak{0};
        FrameAsyncQueue queue(nullptr, {4});
        for (auto& receipt : receipts)
        {
            queue.Submit(NewJob(receipt, [&](const auto&)
            {
                const int count = ++running;
                int observed = peak.Load();
                while (count > observed && !peak.CompareExchangeWeak(observed, count))
                {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                --running;
            }), 0, {});
        }
        Drain(queue);
        Check(peak.Load() > 0 && peak.Load() <= 4 && queue.GetStats().PeakRunning <= 4,
              "同時実行数の最大が4を超えました");
        for (const auto& receipt : receipts)
        {
            Check(receipt.Delivered == 1 && receipt.Discarded == 0 && receipt.Destroyed == 1 &&
                  !receipt.bWrongThread, "短い要求が一度だけ配送されませんでした");
        }
    }

    void CancellationCase()
    {
        std::cout << "ケース3: 待機中・実行中・配送直前の取り消し" << std::endl;
        Receipt running, pending, first, second;
        Gate gate;
        Atomic<bool> bPendingExecuted{false};
        FrameAsyncQueue queue(nullptr, {1});
        const auto runningId = queue.Submit(NewJob(running, [&](const auto& context) { gate.Execute(context); }), 0, {});
        gate.AwaitStart();
        const auto pendingId = queue.Submit(NewJob(pending, [&](const auto&) { bPendingExecuted.Store(true); }), 0, {});
        Check(queue.Cancel(pendingId) && queue.Cancel(runningId), "待機中または実行中の取り消しに失敗しました");
        Drain(queue);
        CheckDiscard(running, FrameAsyncOutcome::Cancelled);
        CheckDiscard(pending, FrameAsyncOutcome::Cancelled);
        Check(gate.bSawCancellation.Load() && !bPendingExecuted.Load(), "取り消しの印または未着手の保証が壊れました");
        Check(!queue.Cancel(runningId), "通知済みの識別子を取り消せてしまいました");

        FrameAsyncRequestId secondId;
        queue.Submit(NewJob(first, EmptyExecute, [&]()
        {
            Check(queue.Cancel(secondId), "同じ配送一覧の後続要求を取り消せませんでした");
        }), 0, {});
        secondId = queue.Submit(NewJob(second, EmptyExecute), 0, {});
        Await([&]() { return queue.GetStats().Completed == 2; }, "配送待ちの2要求が完了しませんでした");
        Check(queue.DeliverCompleted() == 1, "配送コールバックで取り消した要求も配送されました");
        CheckDiscard(second, FrameAsyncOutcome::Cancelled);
    }

    void GenerationCase()
    {
        std::cout << "ケース4: 持ち主の世代の変更" << std::endl;
        Receipt running, pending, fresh, alreadyStale;
        Gate gate;
        Atomic<int> staleExecutions{0};
        FrameAsyncQueue queue(nullptr, {1});
        queue.Submit(NewJob(running, [&](const auto& context) { gate.Execute(context); }), 0, {17, 1});
        gate.AwaitStart();
        queue.Submit(NewJob(pending, [&](const auto&) { ++staleExecutions; }), 0, {17, 1});
        queue.SetOwnerGeneration(17, 2);
        queue.Submit(NewJob(fresh, EmptyExecute), 0, {17, 2});
        queue.Submit(NewJob(alreadyStale, [&](const auto&) { ++staleExecutions; }), 0, {17, 1});
        Drain(queue);
        CheckDiscard(running, FrameAsyncOutcome::Stale);
        CheckDiscard(pending, FrameAsyncOutcome::Stale);
        CheckDiscard(alreadyStale, FrameAsyncOutcome::Stale);
        Check(gate.bSawCancellation.Load() && staleExecutions.Load() == 0,
              "古い世代の未着手要求を実行したか、実行中の印が立ちませんでした");
        Check(fresh.Delivered == 1 && fresh.Discarded == 0, "新しい世代の要求を配送できませんでした");
    }

    void OwnerJoinCase()
    {
        std::cout << "ケース5: 持ち主単位の取り消しと結合" << std::endl;
        Receipt running, pending, other;
        Gate gate;
        FrameAsyncQueue queue(nullptr, {1});
        const uint64_t owner = queue.AllocateOwnerKey();
        const uint64_t otherOwner = queue.AllocateOwnerKey();
        Check((owner & (uint64_t{1} << 63)) != 0 && owner != otherOwner, "持ち主の鍵の範囲か一意性が違います");
        queue.Submit(NewJob(running, [&](const auto& context) { gate.Execute(context, true); }), 0, {owner, 0});
        gate.AwaitStart();
        queue.Submit(NewJob(pending, EmptyExecute), 0, {owner, 0});
        queue.Submit(NewJob(other, EmptyExecute), 0, {otherOwner, 0});
        queue.CancelOwnerAndJoin(owner);
        Check(gate.bEnded.Load() && gate.bSawCancellation.Load(), "持ち主の実行の終了を待たずに返りました");
        CheckDiscard(running, FrameAsyncOutcome::Cancelled);
        CheckDiscard(pending, FrameAsyncOutcome::Cancelled);
        Drain(queue);
        Check(other.Delivered == 1 && other.Discarded == 0, "別の持ち主の要求も取り消されました");
    }

    void ExactlyOnceCase()
    {
        std::cout << "ケース6: 1万件の一意な受け渡し" << std::endl;
        constexpr size_t Count = 10000;
        VariableArray<Receipt> receipts(Count);
        VariableArray<FrameAsyncRequestId> ids;
        ids.reserve(Count);
        std::mt19937 random(0x51A7u);
        uint32_t generations[8] = {};
        Atomic<bool> bRelease{false};
        Atomic<uint32_t> parked{0};
        uint32_t priorityChanges = 0;
        FrameAsyncQueue queue;
        for (size_t index = 0; index < Count; ++index)
        {
            const uint64_t owner = index % 8;
            ids.push_back(queue.Submit(NewJob(receipts[index], [&, index](const auto&)
            {
                if (index < 4)
                {
                    // 最初の4件で枠を塞ぎ、後続の待機要求へ確実に変更を加える。
                    ++parked;
                    const auto deadline = Clock::now() + std::chrono::seconds(10);
                    while (!bRelease.Load() && Clock::now() < deadline)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            }), static_cast<int32_t>(random() % 101) - 50, {owner, generations[owner]}));
            if (index == 3)
            {
                Await([&]() { return parked.Load() == 4; }, "負荷試験の4枠を塞げませんでした");
            }
            Check(ids.back().IsValid(), "受付中の要求が拒まれました");
            if (index % 3 == 0)
            {
                queue.Cancel(ids[random() % ids.size()]);
            }
            if (index % 5 == 0)
            {
                priorityChanges += queue.SetPriority(ids[random() % ids.size()], static_cast<int32_t>(random() % 101)) ? 1u : 0u;
            }
            if (index % 211 == 0)
            {
                queue.SetOwnerGeneration(owner, ++generations[owner]);
            }
            if (index % 257 == 0)
            {
                queue.CancelOwner((owner + 1) % 8);
            }
            if (index % 193 == 0)
            {
                queue.SetMaxConcurrent(4);
                queue.DeliverCompleted();
            }
        }
        bRelease.Store(true);
        Drain(queue);
        Check(priorityChanges > 0, "待機中の優先度の付け直しが一度も成立しませんでした");
        for (const auto& receipt : receipts)
        {
            Check(receipt.Delivered + receipt.Discarded == 1 && receipt.Destroyed == 1 && !receipt.bWrongThread,
                  "要求の通知・破棄の回数か実行スレッドが違います");
        }
        const auto stats = queue.GetStats();
        Check(stats.Pending == 0 && stats.Running == 0 && stats.Completed == 0 && stats.Submitted == Count &&
              stats.Delivered + stats.Discarded == Count, "全件を流した後の統計が一致しません");
    }

    void AdmissionStopCase()
    {
        std::cout << "ケース7: JobSystem の受付停止" << std::endl;
        Receipt running, pending, rejected;
        Gate gate;
        FrameAsyncQueue queue(nullptr, {1});
        queue.Submit(NewJob(running, [&](const auto& context) { gate.Execute(context, true); }), 0, {});
        gate.AwaitStart();
        queue.Submit(NewJob(pending, EmptyExecute), 0, {});
        JobSystem::Get().StopAcceptingTasks();
        const auto id = queue.Submit(NewJob(rejected, EmptyExecute), 0, {});
        Check(!id.IsValid(), "受付停止後の要求が有効な識別子を返しました");
        CheckDiscard(rejected, FrameAsyncOutcome::Rejected);
        Check(queue.DeliverCompleted() == 0 && gate.bEnded.Load(), "受付停止後に配送したか実行の終了を待ちませんでした");
        CheckDiscard(running, FrameAsyncOutcome::Shutdown);
        CheckDiscard(pending, FrameAsyncOutcome::Rejected);
        const auto stats = queue.GetStats();
        Check(!queue.IsClosed() && stats.Pending == 0 && stats.Running == 0 && stats.Completed == 0,
              "受付停止後の列が閉じたか未処理の要求が残りました");
    }
} // namespace

int main()
{
    NorvesLib::Thread::JobSystem::Get().Initialize(4);
    PriorityCase();
    ConcurrentCase();
    CancellationCase();
    GenerationCase();
    OwnerJoinCase();
    ExactlyOnceCase();
    AdmissionStopCase();
    NorvesLib::Thread::JobSystem::Get().Shutdown();
    std::cout << "FrameAsyncQueueTest: 7ケース、失敗数=" << Failures << std::endl;
    return Failures == 0 ? 0 : 1;
}
