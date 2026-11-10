// タスク 1 本（500 ms）の CPU 時間の 2 倍である 1000 ms を上限にする。
// ほかのワーカーが空転すれば、ワーカー数 × 500 ms の桁の CPU 時間になる。
// 短いタスクの束の提出でワーカーを起こし、長いタスクだけが残る状況を作る。
// 短いタスクを終えたワーカーが、長いタスクの実行中に待機できるかを確かめる。
// 時刻割り込みの標本化と機の負荷で ms の計上が揺れるため、サイクル数の比も使う。
// 比なら長いタスクと空転するワーカーを同じ条件で数えられ、同じ根拠で上限を 2.0 とする。
#include "Thread/JobSystem.h"
#include "Thread/Task.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using namespace NorvesLib::Thread;

namespace
{
    bool ReadProcessCpuTime(std::uint64_t& ticks)
    {
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        {
            std::cerr << "プロセス CPU 時間の取得に失敗: エラー番号=" << GetLastError() << std::endl;
            return false;
        }
        ULARGE_INTEGER kernelTicks{}, userTicks{};
        kernelTicks.LowPart = kernel.dwLowDateTime;
        kernelTicks.HighPart = kernel.dwHighDateTime;
        userTicks.LowPart = user.dwLowDateTime;
        userTicks.HighPart = user.dwHighDateTime;
        ticks = kernelTicks.QuadPart + userTicks.QuadPart;
        return true;
    }

    bool ReadThreadCpuTime(std::uint64_t& ticks)
    {
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user))
        {
            std::cerr << "長いタスク自身の CPU 時間の取得に失敗: エラー番号=" << GetLastError() << std::endl;
            return false;
        }
        ULARGE_INTEGER kernelTicks{}, userTicks{};
        kernelTicks.LowPart = kernel.dwLowDateTime;
        kernelTicks.HighPart = kernel.dwHighDateTime;
        userTicks.LowPart = user.dwLowDateTime;
        userTicks.HighPart = user.dwHighDateTime;
        ticks = kernelTicks.QuadPart + userTicks.QuadPart;
        return true;
    }

    bool ReadProcessCycles(ULONG64& cycles)
    {
        if (!QueryProcessCycleTime(GetCurrentProcess(), &cycles))
        {
            std::cerr << "プロセスのサイクル数の取得に失敗: エラー番号=" << GetLastError() << std::endl;
            return false;
        }
        return true;
    }

    bool ReadThreadCycles(ULONG64& cycles)
    {
        if (!QueryThreadCycleTime(GetCurrentThread(), &cycles))
        {
            std::cerr << "長いタスク自身のサイクル数の取得に失敗: エラー番号=" << GetLastError() << std::endl;
            return false;
        }
        return true;
    }
} // namespace

int main()
{
    JobSystem& jobSystem = JobSystem::Get();
    jobSystem.Initialize();
    const size_t workerCount = jobSystem.GetWorkerThreadCount();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::atomic<bool> bCompleted{false};
    double longTaskCpuMilliseconds = -1.0;
    ULONG64 longTaskCycles = 0;
    std::uint64_t cpuStart = 0;
    ULONG64 processCycleStart = 0;
    if (!ReadProcessCpuTime(cpuStart) || !ReadProcessCycles(processCycleStart))
    {
        jobSystem.Shutdown();
        return 1;
    }
    const auto wallStart = std::chrono::steady_clock::now();
    const bool bAccepted = jobSystem.SubmitTask(Task::Create([&]()
    {
        std::uint64_t threadCpuStart = 0;
        const bool bThreadStartMeasured = ReadThreadCpuTime(threadCpuStart);
        ULONG64 threadCycleStart = 0;
        const bool bThreadCycleStartMeasured = ReadThreadCycles(threadCycleStart);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline)
        {
            // 長いタスクは sleep を挟まず CPU を使い続ける。
        }
        ULONG64 threadCycleEnd = 0;
        const bool bThreadCycleEndMeasured = ReadThreadCycles(threadCycleEnd);
        if (bThreadCycleStartMeasured && bThreadCycleEndMeasured)
        {
            longTaskCycles = threadCycleEnd - threadCycleStart;
        }
        std::uint64_t threadCpuEnd = 0;
        const bool bThreadEndMeasured = ReadThreadCpuTime(threadCpuEnd);
        if (bThreadStartMeasured && bThreadEndMeasured)
        {
            longTaskCpuMilliseconds = static_cast<double>(threadCpuEnd - threadCpuStart) / 10000.0;
        }
        // 完了通知で CPU 時間とサイクル数の測定結果も呼び手へ公開する。
        bCompleted.store(true);
    }));
    if (!bAccepted)
    {
        std::cerr << "JobSystemIdleCpuTest 失敗: タスクが受理されませんでした" << std::endl;
        jobSystem.Shutdown();
        return 1;
    }
    for (size_t index = 0; index < workerCount * 4; ++index)
    {
        if (!jobSystem.SubmitTask(Task::Create([]()
        {
        })))
        {
            std::cerr << "JobSystemIdleCpuTest 失敗: 短いタスクが受理されませんでした" << std::endl;
            jobSystem.Shutdown();
            return 1;
        }
    }
    while (!bCompleted.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // 完了直後の CPU 時間の計上の遅れを避ける。
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::uint64_t cpuEnd = 0;
    const bool bMeasured = ReadProcessCpuTime(cpuEnd);
    ULONG64 processCycleEnd = 0;
    const bool bCyclesMeasured = ReadProcessCycles(processCycleEnd);
    const double wallMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - wallStart).count();
    if (!bMeasured || !bCyclesMeasured)
    {
        jobSystem.Shutdown();
        return 1;
    }
    // FILETIME の 1 単位は 100 ns。
    const double cpuMilliseconds = static_cast<double>(cpuEnd - cpuStart) / 10000.0;
    const ULONG64 processCycles = processCycleEnd - processCycleStart;
    const double cycleRatio = longTaskCycles > 0
        ? static_cast<double>(processCycles) / static_cast<double>(longTaskCycles)
        : std::numeric_limits<double>::infinity();
    std::cout << "JobSystemIdleCpuTest: CPU 時間=" << cpuMilliseconds
              << " ms, 長いタスク自身の CPU 時間=" << longTaskCpuMilliseconds
              << " ms, プロセスのサイクル数=" << processCycles
              << ", 長いタスク自身のサイクル数=" << longTaskCycles << ", 比=" << cycleRatio
              << ", 実時間=" << wallMilliseconds << " ms, ワーカー数=" << workerCount << std::endl;
    const bool bTimeWithinLimit = cpuMilliseconds <= 1000.0;
    const bool bCyclesWithinLimit = longTaskCycles > 0 && cycleRatio <= 2.0;
    if (!bTimeWithinLimit)
    {
        std::cerr << "JobSystemIdleCpuTest 失敗: CPU 時間=" << cpuMilliseconds
                  << " ms が上限 1000 ms を超えました" << std::endl;
    }
    if (longTaskCycles == 0)
    {
        std::cerr << "JobSystemIdleCpuTest 失敗: 長いタスク自身のサイクル数が 0 のため比を測れませんでした"
                  << std::endl;
    }
    else if (!bCyclesWithinLimit)
    {
        std::cerr << "JobSystemIdleCpuTest 失敗: サイクル数の比=" << cycleRatio
                  << " が上限 2.0 を超えました" << std::endl;
    }
    jobSystem.Shutdown();
    return bTimeWithinLimit && bCyclesWithinLimit ? 0 : 1;
}
