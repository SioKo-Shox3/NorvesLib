// JobSystem::ParallelFor の契約を確かめるテスト。
// 全ワーカーが塞がっていても呼び手だけで終わること、各番号がちょうど 1 回走ること、
// 返った後に起きた補助が関数に触れないこと、受理停止後・入れ子・例外の扱いを見る。
#include "Container/VariableArray.h"
#include "Thread/JobSystem.h"
#include "Thread/Task.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <thread>


using namespace NorvesLib::Thread;
using NorvesLib::Core::Container::VariableArray;

namespace NorvesLib::Thread
{
    // JobSystem の提出準備の確認口（フック）へ触れるための friend。
    struct JobParallelForTestAccess
    {
        using Point = JobSystem::SubmitPreparationPoint;
        static void SetSubmitHook(JobSystem &system, void (*hook)(Point, void *), void *context)
        {
            system.m_submitPreparationHook = hook;
            system.m_submitPreparationContext = context;
        }
    };
} // namespace NorvesLib::Thread

namespace
{
    // NDEBUG に依らず止まる検査（assert は RelWithDebInfo で無効になるため使わない）。
    void CheckImpl(bool bCondition, const char *expression, int line)
    {
        if (!bCondition)
        {
            std::cerr << "JobParallelForTest 失敗: " << expression << "（" << line << " 行）" << std::endl;
            std::abort();
        }
    }
#define PF_CHECK(condition) CheckImpl((condition), #condition, __LINE__)

    const char *ModeName(JobSystem::ExecutionMode mode)
    {
        return mode == JobSystem::ExecutionMode::EXECUTION_SIMPLE ? "単純" : "ワークスチーリング";
    }

    void SleepMilliseconds(int milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    // 指定の数のワーカーを、解放されるまで返らないタスクで塞ぐ。
    class WorkerBlocker
    {
    public:
        WorkerBlocker(JobSystem &jobSystem, int blockCount) : m_BlockCount(blockCount)
        {
            for (int index = 0; index < blockCount; ++index)
            {
                const bool bAccepted = jobSystem.SubmitTask(Task::Create([this]()
                                                                         {
                    m_Started++;
                    while (!m_bRelease.load())
                    {
                        SleepMilliseconds(1);
                    } }));
                PF_CHECK(bAccepted);
            }
            while (m_Started.load() < blockCount)
            {
                SleepMilliseconds(1);
            }
        }

        ~WorkerBlocker() { Release(); }

        void Release() { m_bRelease.store(true); }

        int GetBlockCount() const { return m_BlockCount; }

    private:
        int m_BlockCount = 0;
        std::atomic<int> m_Started{0};
        std::atomic<bool> m_bRelease{false};
    };

    void CheckEachIndexOnce(const VariableArray<std::atomic<int>> &hits)
    {
        for (const auto &hit : hits)
        {
            PF_CHECK(hit.load() == 1);
        }
    }

    // 各番号がちょうど 1 回走る（個数と区間の幅の組を変えて確かめる）。
    void TestEachIndexRunsExactlyOnce(JobSystem::ExecutionMode mode)
    {
        JobSystem &jobSystem = JobSystem::Get();
        jobSystem.Initialize(4, mode);

        struct Case
        {
            size_t Count;
            size_t Grain;
        };
        const Case cases[] = {
            {0, 1}, {1, 1}, {2, 1}, {1000, 1}, {1000, 7}, {1000, 0},
            {1000, 5000}, {100003, 64}, {100000, 100000}, {4097, 4096}};
        for (const Case &testCase : cases)
        {
            VariableArray<std::atomic<int>> hits(testCase.Count);
            std::atomic<size_t> calls{0};
            jobSystem.ParallelFor(testCase.Count, testCase.Grain, [&](size_t index)
                                  {
                PF_CHECK(index < hits.size());
                hits[index]++;
                calls++; });
            PF_CHECK(calls.load() == testCase.Count);
            CheckEachIndexOnce(hits);
        }

        jobSystem.Shutdown();
    }

    struct SubmitFaultMarker
    {
    };

    // 提出準備の 2 回目の BeforeTicket で例外を投げる。
    struct SubmitFault
    {
        std::atomic<int> TicketCalls{0};
    };

    void ThrowOnSecondTicket(JobParallelForTestAccess::Point point, void *context)
    {
        if (point == JobParallelForTestAccess::Point::BeforeTicket)
        {
            auto *fault = static_cast<SubmitFault *>(context);
            if (fault->TicketCalls.fetch_add(1) + 1 == 2)
            {
                throw SubmitFaultMarker{};
            }
        }
    }

    // 全ワーカーが長いタスクで塞がっていても呼び手だけで終わる。返った後に起きた補助は関数を呼ばない。
    void TestCallerAloneWhenWorkersBlocked(JobSystem::ExecutionMode mode)
    {
        constexpr int WorkerCount = 2;
        constexpr size_t Count = 2000;

        JobSystem &jobSystem = JobSystem::Get();
        jobSystem.Initialize(WorkerCount, mode);

        std::atomic<size_t> totalCalls{0};
        std::atomic<int> foreignCalls{0};
        VariableArray<std::atomic<int>> hits(Count);
        {
            WorkerBlocker blocker(jobSystem, WorkerCount);
            const std::thread::id callerId = std::this_thread::get_id();

            {
                // 関数オブジェクトは返った後に破棄される。起きた補助が呼べば不正な呼び出しになる。
                JobSystem::ParallelForFunction fn = [&](size_t index)
                {
                    hits[index]++;
                    totalCalls++;
                    if (std::this_thread::get_id() != callerId)
                    {
                        foreignCalls++;
                    }
                };
                jobSystem.ParallelFor(Count, 3, fn);
            }

            PF_CHECK(totalCalls.load() == Count);
            PF_CHECK(foreignCalls.load() == 0);
            CheckEachIndexOnce(hits);
            // 補助はワーカーの数まで（塞いだ分と合わせて 2 倍を超えない）。
            PF_CHECK(jobSystem.GetQueuedTaskCount() <= static_cast<size_t>(WorkerCount) * 2);

            // 塞ぎを解くと、積まれていた補助が起きる。カーソルは尽きているので関数は呼ばれない。
            blocker.Release();
            jobSystem.WaitForAll();
        }
        PF_CHECK(jobSystem.GetQueuedTaskCount() == 0);
        PF_CHECK(totalCalls.load() == Count);
        CheckEachIndexOnce(hits);

        jobSystem.Shutdown();
    }

    // 受理停止の後は補助を出せないが、呼び手だけで終わる。
    void TestAfterStopAcceptingTasks(JobSystem::ExecutionMode mode)
    {
        constexpr size_t Count = 500;

        JobSystem &jobSystem = JobSystem::Get();
        jobSystem.Initialize(3, mode);
        jobSystem.StopAcceptingTasks();

        const std::thread::id callerId = std::this_thread::get_id();
        std::atomic<int> foreignCalls{0};
        VariableArray<std::atomic<int>> hits(Count);
        jobSystem.ParallelFor(Count, 2, [&](size_t index)
                              {
            hits[index]++;
            if (std::this_thread::get_id() != callerId)
            {
                foreignCalls++;
            } });
        CheckEachIndexOnce(hits);
        PF_CHECK(foreignCalls.load() == 0);
        PF_CHECK(jobSystem.GetQueuedTaskCount() == 0);

        jobSystem.Shutdown();
    }

    // 入れ子はその場（同じスレッド）で番号の順に直列で実行する。
    void TestNestedRunsInPlace(JobSystem::ExecutionMode mode)
    {
        constexpr size_t OuterCount = 16;
        constexpr size_t InnerCount = 50;

        JobSystem &jobSystem = JobSystem::Get();
        jobSystem.Initialize(4, mode);

        VariableArray<std::atomic<int>> hits(OuterCount * InnerCount);
        std::atomic<int> wrongThread{0};
        std::atomic<int> outOfOrder{0};
        jobSystem.ParallelFor(OuterCount, 1, [&](size_t outer)
                              {
            const std::thread::id selfId = std::this_thread::get_id();
            long long previous = -1;
            jobSystem.ParallelFor(InnerCount, 1, [&](size_t inner)
                                  {
                if (std::this_thread::get_id() != selfId)
                {
                    wrongThread++;
                }
                if (static_cast<long long>(inner) != previous + 1)
                {
                    outOfOrder++;
                }
                previous = static_cast<long long>(inner);
                hits[outer * InnerCount + inner]++; });
            PF_CHECK(previous == static_cast<long long>(InnerCount) - 1); });
        CheckEachIndexOnce(hits);
        PF_CHECK(wrongThread.load() == 0);
        PF_CHECK(outOfOrder.load() == 0);

        // 3 重でも各番号が 1 回ずつ。
        std::atomic<int> leafCalls{0};
        jobSystem.ParallelFor(4, 1, [&](size_t)
                              { jobSystem.ParallelFor(4, 1, [&](size_t)
                                                      { jobSystem.ParallelFor(4, 1, [&](size_t)
                                                                              { leafCalls++; }); }); });
        PF_CHECK(leafCalls.load() == 64);

        // 入れ子の外側が終わった後の呼び出しは、また並列に使える（入れ子の目印が残らない）。
        VariableArray<std::atomic<int>> hitsAfter(1000);
        jobSystem.ParallelFor(hitsAfter.size(), 1, [&](size_t index)
                              { hitsAfter[index]++; });
        CheckEachIndexOnce(hitsAfter);

        jobSystem.Shutdown();
    }

    // ワーカーのタスクの中から呼んでも終わる（入れ子ではない通常の呼び出し）。
    void TestCalledFromWorkerTask(JobSystem::ExecutionMode mode)
    {
        constexpr size_t Count = 1000;

        JobSystem &jobSystem = JobSystem::Get();
        jobSystem.Initialize(2, mode);

        VariableArray<std::atomic<int>> hits(Count);
        TaskPtr task = Task::Create([&]()
                                    { JobSystem::Get().ParallelFor(Count, 1, [&](size_t index)
                                                                   { hits[index]++; }); });
        PF_CHECK(jobSystem.SubmitTask(task));
        task->Wait();
        CheckEachIndexOnce(hits);

        jobSystem.WaitForAll();
        jobSystem.Shutdown();
    }

    // 例外は走っている分の終わりを待ってから呼び手で投げ直す。返った後は何も走らない。
    void TestExceptionIsRethrownAfterAllFinish(JobSystem::ExecutionMode mode)
    {
        constexpr size_t Count = 1000;

        JobSystem &jobSystem = JobSystem::Get();
        jobSystem.Initialize(4, mode);

        // 1 件だけ投げる。ほかの番号は少し眠って、投げた時点で走っている分を作る。
        {
            VariableArray<std::atomic<int>> hits(Count);
            std::atomic<int> inFlight{0};
            std::atomic<size_t> calls{0};
            bool bCaught = false;
            try
            {
                jobSystem.ParallelFor(Count, 1, [&](size_t index)
                                      {
                    inFlight++;
                    hits[index]++;
                    calls++;
                    if (index == 10)
                    {
                        inFlight--;
                        throw std::runtime_error("boom");
                    }
                    SleepMilliseconds(1);
                    inFlight--; });
            }
            catch (const std::runtime_error &error)
            {
                bCaught = true;
                PF_CHECK(std::strcmp(error.what(), "boom") == 0);
            }
            PF_CHECK(bCaught);
            PF_CHECK(inFlight.load() == 0);
            const size_t callsAtReturn = calls.load();
            jobSystem.WaitForAll();
            SleepMilliseconds(30);
            PF_CHECK(calls.load() == callsAtReturn);
            for (const auto &hit : hits)
            {
                PF_CHECK(hit.load() <= 1);
            }
        }

        // 全部の番号が投げても、投げ直されるのは 1 件だけで止まらない。
        {
            int caughtCount = 0;
            try
            {
                jobSystem.ParallelFor(Count, 4, [](size_t index)
                                      { throw index; });
            }
            catch (size_t)
            {
                caughtCount++;
            }
            PF_CHECK(caughtCount == 1);
        }

        // 標準の例外でない型も同じ道で伝わる。
        {
            bool bCaught = false;
            try
            {
                jobSystem.ParallelFor(Count, 1, [](size_t index)
                                      {
                    if (index == 500)
                    {
                        throw 42;
                    } });
            }
            catch (int value)
            {
                bCaught = true;
                PF_CHECK(value == 42);
            }
            PF_CHECK(bCaught);
        }

        // 直列の経路（区間が 1 つ・入れ子）でも伝わる。
        {
            bool bCaught = false;
            try
            {
                jobSystem.ParallelFor(1, 1, [](size_t)
                                      { throw std::runtime_error("serial"); });
            }
            catch (const std::runtime_error &)
            {
                bCaught = true;
            }
            PF_CHECK(bCaught);

            bCaught = false;
            try
            {
                jobSystem.ParallelFor(8, 1, [&](size_t outer)
                                      { jobSystem.ParallelFor(8, 1, [&](size_t inner)
                                                              {
                                          if (outer == 3 && inner == 4)
                                          {
                                              throw std::runtime_error("nested");
                                          } }); });
            }
            catch (const std::runtime_error &error)
            {
                bCaught = true;
                PF_CHECK(std::strcmp(error.what(), "nested") == 0);
            }
            PF_CHECK(bCaught);
        }

        // 例外の後も使える（入れ子の目印やカーソルが次の呼び出しに残らない）。
        {
            VariableArray<std::atomic<int>> hits(Count);
            jobSystem.ParallelFor(Count, 1, [&](size_t index)
                                  { hits[index]++; });
            CheckEachIndexOnce(hits);
        }

        // ワーカーが塞がっていて呼び手だけが走る場合も、投げ直されて補助は後始末だけで終わる。
        {
            std::atomic<size_t> calls{0};
            {
                WorkerBlocker blocker(jobSystem, 4);
                bool bCaught = false;
                try
                {
                    jobSystem.ParallelFor(Count, 1, [&](size_t index)
                                          {
                        calls++;
                        if (index == 3)
                        {
                            throw std::runtime_error("caller");
                        } });
                }
                catch (const std::runtime_error &)
                {
                    bCaught = true;
                }
                PF_CHECK(bCaught);
                const size_t callsAtReturn = calls.load();
                blocker.Release();
                jobSystem.WaitForAll();
                PF_CHECK(calls.load() == callsAtReturn);
            }
        }

        jobSystem.Shutdown();
    }

    // 補助の提出の途中で例外が出ても、受理済みの補助が返った後に関数を呼ばず、例外は投げ直される。
    void TestSubmitFaultDuringHelperSubmission(JobSystem::ExecutionMode mode)
    {
        constexpr int WorkerCount = 2;
        constexpr size_t Count = 400;

        JobSystem &jobSystem = JobSystem::Get();
        jobSystem.Initialize(WorkerCount, mode);

        std::atomic<size_t> totalCalls{0};
        std::atomic<bool> bReturned{false};
        std::atomic<size_t> callsAfterReturn{0};
        {
            WorkerBlocker blocker(jobSystem, WorkerCount);

            // 2 個目の補助の提出で例外を出す（1 個目は受理されて塞がれたキューに残る）。
            SubmitFault fault;
            JobParallelForTestAccess::SetSubmitHook(jobSystem, ThrowOnSecondTicket, &fault);
            bool bCaught = false;
            // 関数は塞ぎを解くまで生かしておき、呼び出しが返った後に呼ばれたかを数える。
            JobSystem::ParallelForFunction fn = [&](size_t)
            {
                totalCalls++;
                if (bReturned.load())
                {
                    callsAfterReturn++;
                }
            };
            try
            {
                jobSystem.ParallelFor(Count, 1, fn);
            }
            catch (const SubmitFaultMarker &)
            {
                bCaught = true;
            }
            bReturned.store(true);
            JobParallelForTestAccess::SetSubmitHook(jobSystem, nullptr, nullptr);
            PF_CHECK(bCaught);
            PF_CHECK(fault.TicketCalls.load() == 2);

            // 例外の後は未着手の区間を呼ばずに返る。受理済みの補助は塞ぎを解いても関数を呼ばない。
            PF_CHECK(totalCalls.load() == 0);
            blocker.Release();
            jobSystem.WaitForAll();
            SleepMilliseconds(30);
            PF_CHECK(callsAfterReturn.load() == 0);
            PF_CHECK(totalCalls.load() == 0);
        }
        PF_CHECK(jobSystem.GetQueuedTaskCount() == 0);

        // 例外の後も使える。
        VariableArray<std::atomic<int>> hits(1000);
        jobSystem.ParallelFor(hits.size(), 1, [&](size_t index)
                              { hits[index]++; });
        CheckEachIndexOnce(hits);

        jobSystem.Shutdown();
    }

    void RunAllForMode(JobSystem::ExecutionMode mode)
    {
        std::cout << "JobParallelForTest: 実行モード " << ModeName(mode) << std::endl;
        TestEachIndexRunsExactlyOnce(mode);
        TestCallerAloneWhenWorkersBlocked(mode);
        TestAfterStopAcceptingTasks(mode);
        TestNestedRunsInPlace(mode);
        TestCalledFromWorkerTask(mode);
        TestExceptionIsRethrownAfterAllFinish(mode);
        TestSubmitFaultDuringHelperSubmission(mode);
    }
} // namespace

int main()
{
    std::cout << "JobParallelForTest 開始" << std::endl;
    RunAllForMode(JobSystem::ExecutionMode::EXECUTION_WORK_STEALING);
    RunAllForMode(JobSystem::ExecutionMode::EXECUTION_SIMPLE);
    std::cout << "JobParallelForTest 合格" << std::endl;
    return 0;
}
