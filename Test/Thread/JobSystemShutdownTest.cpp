#include "Thread/JobSystem.h"
#include "Thread/Task.h"
#include "Thread/Atomic.h"

#include <cassert>
#include <iostream>
#include <thread>

using namespace NorvesLib::Thread;

void TestRepeatedShutdown()
{
    std::cout << "Running repeated JobSystem shutdown test..." << std::endl;

    constexpr int IterationCount = 100;
    constexpr int TaskCount = 16;

    for (int iteration = 0; iteration < IterationCount; ++iteration)
    {
        Atomic<int> completedTasks(0);
        auto &jobSystem = JobSystem::Get();

        jobSystem.Initialize(2, JobSystem::ExecutionMode::EXECUTION_WORK_STEALING);
        assert(jobSystem.GetWorkerThreadCount() == 2);
        assert(jobSystem.GetWorkerThreadsStats().size() == 2);

        for (int taskIndex = 0; taskIndex < TaskCount; ++taskIndex)
        {
            jobSystem.SubmitTask(Task::Create([&completedTasks]()
            {
                completedTasks++;
                std::this_thread::yield();
            }));
        }

        jobSystem.WaitForAll();
        assert(completedTasks.Load() == TaskCount);
        assert(jobSystem.GetQueuedTaskCount() == 0);
        assert(jobSystem.GetWorkerThreadsStats().size() == 2);

        jobSystem.Shutdown();
        assert(jobSystem.GetWorkerThreadCount() == 0);
    }

    std::cout << "Repeated JobSystem shutdown test passed!" << std::endl;
}

void TestDynamicResize()
{
    std::cout << "Running JobSystem dynamic resize test..." << std::endl;

    auto &jobSystem = JobSystem::Get();
    jobSystem.Initialize(1, JobSystem::ExecutionMode::EXECUTION_WORK_STEALING);
    jobSystem.EnableDynamicSizing(true, 1, 3);
    assert(jobSystem.GetWorkerThreadCount() == 1);
    assert(jobSystem.GetWorkerThreadsStats().size() == 1);

    assert(jobSystem.AdjustWorkerThreadCount(3) == 3);
    assert(jobSystem.GetWorkerThreadCount() == 3);
    assert(jobSystem.GetWorkerThreadsStats().size() == 3);

    Atomic<int> completedTasks(0);
    for (int taskIndex = 0; taskIndex < 24; ++taskIndex)
    {
        jobSystem.SubmitTask(Task::Create([&completedTasks]()
        {
            completedTasks++;
            std::this_thread::yield();
        }));
    }

    jobSystem.WaitForAll();
    assert(completedTasks.Load() == 24);
    assert(jobSystem.GetQueuedTaskCount() == 0);

    assert(jobSystem.AdjustWorkerThreadCount(1) == 1);
    assert(jobSystem.GetWorkerThreadCount() == 1);
    assert(jobSystem.GetWorkerThreadsStats().size() == 1);

    jobSystem.EnableDynamicSizing(false);
    jobSystem.Shutdown();
    assert(jobSystem.GetWorkerThreadCount() == 0);

    std::cout << "JobSystem dynamic resize test passed!" << std::endl;
}

void TestShutdownWithDynamicSizingEnabled()
{
    std::cout << "Running dynamic sizing shutdown test..." << std::endl;

    auto &jobSystem = JobSystem::Get();
    Atomic<bool> startFlag(false);

    jobSystem.Initialize(2, JobSystem::ExecutionMode::EXECUTION_WORK_STEALING);
    jobSystem.EnableDynamicSizing(true, 1, 3);

    std::thread resizeThread([&jobSystem, &startFlag]()
    {
        while (!startFlag.Load())
        {
            std::this_thread::yield();
        }

        for (int iteration = 0; iteration < 32; ++iteration)
        {
            jobSystem.AdjustWorkerThreadCount(3);
            jobSystem.AdjustWorkerThreadCount(1);
            std::this_thread::yield();
        }
    });

    for (int taskIndex = 0; taskIndex < 64; ++taskIndex)
    {
        jobSystem.SubmitTask(Task::Create([]()
        {
            std::this_thread::yield();
        }));
    }

    startFlag = true;
    std::this_thread::yield();
    jobSystem.Shutdown();
    resizeThread.join();

    assert(jobSystem.GetWorkerThreadCount() == 0);
    assert(jobSystem.GetQueuedTaskCount() == 0);

    jobSystem.EnableDynamicSizing(false);

    std::cout << "Dynamic sizing shutdown test passed!" << std::endl;
}

void TestShutdownWithQueuedWork()
{
    std::cout << "Running JobSystem queued shutdown test..." << std::endl;

    auto &jobSystem = JobSystem::Get();
    jobSystem.Initialize(2, JobSystem::ExecutionMode::EXECUTION_WORK_STEALING);

    for (int taskIndex = 0; taskIndex < 64; ++taskIndex)
    {
        jobSystem.SubmitTask(Task::Create([]()
        {
            std::this_thread::yield();
        }));
    }

    jobSystem.Shutdown();
    assert(jobSystem.GetWorkerThreadCount() == 0);
    assert(jobSystem.GetQueuedTaskCount() == 0);

    std::cout << "JobSystem queued shutdown test passed!" << std::endl;
}

void TestConcurrentSubmitDuringShutdown()
{
    std::cout << "Running concurrent submit/shutdown test..." << std::endl;

    constexpr int IterationCount = 25;
    constexpr int SubmitterCount = 4;
    constexpr int TaskCountPerSubmitter = 32;

    for (int iteration = 0; iteration < IterationCount; ++iteration)
    {
        auto &jobSystem = JobSystem::Get();
        Atomic<bool> startFlag(false);
        Atomic<int> executedTasks(0);

        jobSystem.Initialize(2, JobSystem::ExecutionMode::EXECUTION_WORK_STEALING);

        std::thread submitters[SubmitterCount];
        for (int submitterIndex = 0; submitterIndex < SubmitterCount; ++submitterIndex)
        {
            submitters[submitterIndex] = std::thread([&jobSystem, &startFlag, &executedTasks]()
            {
                while (!startFlag.Load())
                {
                    std::this_thread::yield();
                }

                for (int taskIndex = 0; taskIndex < TaskCountPerSubmitter; ++taskIndex)
                {
                    jobSystem.SubmitTask(Task::Create([&executedTasks]()
                    {
                        executedTasks++;
                        std::this_thread::yield();
                    }));
                }
            });
        }

        std::thread shutdownThread([&jobSystem, &startFlag]()
        {
            while (!startFlag.Load())
            {
                std::this_thread::yield();
            }

            std::this_thread::yield();
            jobSystem.Shutdown();
        });

        startFlag = true;

        for (int submitterIndex = 0; submitterIndex < SubmitterCount; ++submitterIndex)
        {
            submitters[submitterIndex].join();
        }
        shutdownThread.join();

        assert(jobSystem.GetWorkerThreadCount() == 0);
        assert(jobSystem.GetQueuedTaskCount() == 0);
    }

    std::cout << "Concurrent submit/shutdown test passed!" << std::endl;
}

void TestConcurrentSubmitDuringResize()
{
    std::cout << "Running concurrent submit/resize test..." << std::endl;

    constexpr int SubmitterCount = 4;
    constexpr int TaskCountPerSubmitter = 32;
    constexpr int ResizeIterationCount = 16;

    auto &jobSystem = JobSystem::Get();
    Atomic<bool> startFlag(false);
    Atomic<int> submittedTasks(0);
    Atomic<int> executedTasks(0);

    jobSystem.Initialize(3, JobSystem::ExecutionMode::EXECUTION_WORK_STEALING);
    jobSystem.EnableDynamicSizing(true, 1, 3);

    std::thread submitters[SubmitterCount];
    for (int submitterIndex = 0; submitterIndex < SubmitterCount; ++submitterIndex)
    {
        submitters[submitterIndex] = std::thread([&jobSystem, &startFlag, &submittedTasks, &executedTasks]()
        {
            while (!startFlag.Load())
            {
                std::this_thread::yield();
            }

            for (int taskIndex = 0; taskIndex < TaskCountPerSubmitter; ++taskIndex)
            {
                jobSystem.SubmitTask(Task::Create([&executedTasks]()
                {
                    executedTasks++;
                    std::this_thread::yield();
                }));
                submittedTasks++;
            }
        });
    }

    std::thread resizeThread([&jobSystem, &startFlag]()
    {
        while (!startFlag.Load())
        {
            std::this_thread::yield();
        }

        for (int iteration = 0; iteration < ResizeIterationCount; ++iteration)
        {
            jobSystem.AdjustWorkerThreadCount(1);
            jobSystem.AdjustWorkerThreadCount(3);
            std::this_thread::yield();
        }
    });

    startFlag = true;

    for (int submitterIndex = 0; submitterIndex < SubmitterCount; ++submitterIndex)
    {
        submitters[submitterIndex].join();
    }
    resizeThread.join();

    jobSystem.WaitForAll();
    assert(executedTasks.Load() == submittedTasks.Load());
    assert(jobSystem.GetQueuedTaskCount() == 0);

    jobSystem.EnableDynamicSizing(false);
    jobSystem.Shutdown();
    assert(jobSystem.GetWorkerThreadCount() == 0);

    std::cout << "Concurrent submit/resize test passed!" << std::endl;
}

void TestConcurrentShutdown()
{
    std::cout << "Running concurrent shutdown test..." << std::endl;

    auto &jobSystem = JobSystem::Get();
    Atomic<bool> startFlag(false);

    jobSystem.Initialize(2, JobSystem::ExecutionMode::EXECUTION_WORK_STEALING);
    for (int taskIndex = 0; taskIndex < 32; ++taskIndex)
    {
        jobSystem.SubmitTask(Task::Create([]()
        {
            std::this_thread::yield();
        }));
    }

    std::thread shutdownThreadA([&jobSystem, &startFlag]()
    {
        while (!startFlag.Load())
        {
            std::this_thread::yield();
        }

        jobSystem.Shutdown();
    });
    std::thread shutdownThreadB([&jobSystem, &startFlag]()
    {
        while (!startFlag.Load())
        {
            std::this_thread::yield();
        }

        jobSystem.Shutdown();
    });

    startFlag = true;
    shutdownThreadA.join();
    shutdownThreadB.join();

    assert(jobSystem.GetWorkerThreadCount() == 0);
    assert(jobSystem.GetQueuedTaskCount() == 0);

    std::cout << "Concurrent shutdown test passed!" << std::endl;
}

int main()
{
    std::cout << "Running JobSystem shutdown tests..." << std::endl;

    TestRepeatedShutdown();
    TestDynamicResize();
    TestShutdownWithDynamicSizingEnabled();
    TestShutdownWithQueuedWork();
    TestConcurrentSubmitDuringShutdown();
    TestConcurrentSubmitDuringResize();
    TestConcurrentShutdown();

    std::cout << "All JobSystem shutdown tests passed!" << std::endl;
    return 0;
}
