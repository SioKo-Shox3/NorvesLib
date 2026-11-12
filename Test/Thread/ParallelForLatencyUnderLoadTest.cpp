// ParallelFor は、ワーカーが長いタスクで塞がっていても呼び手だけで最後まで進む契約を持つ。
// 最悪でも直列とほぼ同じ時間になるため、揺れを許容して直列の 1.5 倍を上限にする。
#include "Thread/JobSystem.h"
#include "Thread/Task.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace NorvesLib::Thread;

namespace
{
    constexpr size_t Count = 256;
    constexpr int Repetitions = 3;
    constexpr size_t LoadTaskCount = 64;

    void BusyWork()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            // 直列と ParallelFor で同じ時間だけ CPU を使う。
        }
    }

    // 参照されるフラグは Shutdown が全ワーカーを回収するまで生かしておく。
    class WorkerBlocker
    {
    public:
        bool SubmitAndWait(JobSystem& jobSystem, size_t workerCount)
        {
            for (size_t index = 0; index < LoadTaskCount; ++index)
            {
                if (!jobSystem.SubmitTask(Task::Create([this]()
                {
                    m_Started.fetch_add(1);
                    while (!m_bRelease.load())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                })))
                {
                    std::cerr << "負荷タスクが受理されませんでした: 番号=" << index << std::endl;
                    return false;
                }
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (m_Started.load() < workerCount)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    std::cerr << "負荷タスクの開始待ちが時間切れ: 開始数=" << m_Started.load()
                              << ", ワーカー数=" << workerCount << std::endl;
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return m_Started.load() == workerCount;
        }

        void Release() { m_bRelease.store(true); }

    private:
        std::atomic<size_t> m_Started{0};
        std::atomic<bool> m_bRelease{false};
    };
} // namespace

int main()
{
    JobSystem& jobSystem = JobSystem::Get();
    jobSystem.Initialize();
    const size_t workerCount = jobSystem.GetWorkerThreadCount();
    if (workerCount == 0 || workerCount > LoadTaskCount)
    {
        std::cerr << "ParallelForLatencyUnderLoadTest 失敗: ワーカー数=" << workerCount
                  << " は 1～64 の範囲外です" << std::endl;
        jobSystem.Shutdown();
        return 1;
    }

    double serialMilliseconds = 0.0;
    for (int repetition = 0; repetition < Repetitions; ++repetition)
    {
        const auto start = std::chrono::steady_clock::now();
        for (size_t index = 0; index < Count; ++index)
        {
            BusyWork();
        }
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (repetition == 0 || elapsed < serialMilliseconds)
        {
            serialMilliseconds = elapsed;
        }
    }

    WorkerBlocker blocker;
    if (!blocker.SubmitAndWait(jobSystem, workerCount))
    {
        std::cerr << "ParallelForLatencyUnderLoadTest 失敗: 全ワーカーを塞げませんでした" << std::endl;
        blocker.Release();
        jobSystem.Shutdown();
        return 1;
    }

    // 各回に別の計数領域を使い、遅れて起きる補助タスクの回収まで保持する。
    std::atomic<int> hits[Repetitions][Count]{};
    bool bEachIndexOnce = true;
    double loadedMilliseconds = 0.0;
    for (int repetition = 0; repetition < Repetitions; ++repetition)
    {
        const auto start = std::chrono::steady_clock::now();
        jobSystem.ParallelFor(Count, 1, [&, repetition](size_t index)
        {
            hits[repetition][index].fetch_add(1);
            BusyWork();
        });
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (repetition == 0 || elapsed < loadedMilliseconds)
        {
            loadedMilliseconds = elapsed;
        }
        for (size_t index = 0; index < Count; ++index)
        {
            const int calls = hits[repetition][index].load();
            if (calls != 1)
            {
                std::cerr << "ParallelForLatencyUnderLoadTest 失敗: 測定回=" << repetition + 1
                          << ", 番号=" << index << ", 呼出回数=" << calls << std::endl;
                bEachIndexOnce = false;
            }
        }
    }

    const double ratio = loadedMilliseconds / serialMilliseconds;
    std::cout << "ParallelForLatencyUnderLoadTest: 直列=" << serialMilliseconds
              << " ms, 負荷下=" << loadedMilliseconds << " ms, 比=" << ratio << std::endl;
    const bool bWithinLimit = loadedMilliseconds <= serialMilliseconds * 1.5;
    if (!bWithinLimit)
    {
        std::cerr << "ParallelForLatencyUnderLoadTest 失敗: 負荷下の時間が直列の 1.5 倍を超えました"
                  << std::endl;
    }
    blocker.Release();
    jobSystem.Shutdown();
    return bEachIndexOnce && bWithinLimit ? 0 : 1;
}
