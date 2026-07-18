#include "Thread/JobSystem.h"
#include "Thread/Task.h"
#include "Thread/Atomic.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <process.h>
#include <thread>
#include <windows.h>

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

namespace NorvesLib::Thread
{
    struct JobSystemTestAccess
    {
        static void SetFiniteDrainWaitHook(JobSystem& jobSystem, void (*hook)(void*), void* context)
        {
            jobSystem.m_finiteDrainWaitHook = hook;
            jobSystem.m_finiteDrainWaitHookContext = context;
        }

        static bool IsShutdownRequested(JobSystem& jobSystem)
        {
            return jobSystem.m_shutdownRequested.Load();
        }
    };
}

class TestLatch
{
public:
    void Signal()
    {
        {
            ScopedLock lock(m_mutex);
            m_bSignaled = true;
        }
        m_condition.NotifyAll();
    }

    bool WaitFor(const char* description)
    {
        ScopedLock lock(m_mutex);
        const bool bCompleted = m_condition.WaitFor(
            m_mutex,
            std::chrono::seconds(5),
            [this]()
            {
                return m_bSignaled.Load();
            });

        if (!bCompleted)
        {
            std::cout << "Timed out waiting for " << description << std::endl;
        }

        return bCompleted;
    }

private:
    Atomic<bool> m_bSignaled = false;
    Mutex m_mutex;
    ConditionVariable m_condition;
};

bool CheckJobSystemCondition(bool bCondition, const char* description)
{
    if (!bCondition)
    {
        std::cout << "JobSystem finite drain check failed: " << description << std::endl;
    }

    return bCondition;
}

void SignalDrainWait(void* context)
{
    static_cast<TestLatch*>(context)->Signal();
}

bool WaitForShutdownRequest(JobSystem& jobSystem)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!JobSystemTestAccess::IsShutdownRequested(jobSystem))
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return CheckJobSystemCondition(false, "shutdown request was published before watchdog timeout");
        }

        std::this_thread::yield();
    }

    return true;
}

bool TestPreInitializeFiniteDrain(JobSystem::ExecutionMode mode)
{
    auto& jobSystem = JobSystem::Get();
    TestLatch taskEntered;
    TestLatch taskGate;
    TestLatch drainWaiting;
    TestLatch drainFinished;
    Atomic<size_t> completedFiniteTasks(0);
    Atomic<size_t> completedFiniteTasksAtDrain(0);
    std::thread drainThread;

    jobSystem.SetExecutionMode(mode);
    TaskPtr task = Task::Create([&taskEntered, &taskGate]()
    {
        taskEntered.Signal();
        taskGate.WaitFor("pre-initialize finite task gate");
    });
    task->OnComplete([&completedFiniteTasks](const TaskPtr&)
    {
        completedFiniteTasks++;
    });

    if (!CheckJobSystemCondition(jobSystem.SubmitTask(task), "pre-initialize finite task was accepted"))
    {
        return false;
    }

    jobSystem.Initialize(1, mode);
    if (!taskEntered.WaitFor("pre-initialize finite task entry"))
    {
        taskGate.Signal();
        jobSystem.Shutdown();
        return false;
    }

    jobSystem.StopAcceptingTasks();
    JobSystemTestAccess::SetFiniteDrainWaitHook(jobSystem, SignalDrainWait, &drainWaiting);
    drainThread = std::thread([&jobSystem, &drainFinished, &completedFiniteTasks, &completedFiniteTasksAtDrain]()
    {
        jobSystem.DrainAcceptedFiniteTasks();
        completedFiniteTasksAtDrain = completedFiniteTasks.Load();
        drainFinished.Signal();
    });

    if (!drainWaiting.WaitFor("pre-initialize finite drain predicate"))
    {
        taskGate.Signal();
        drainThread.join();
        JobSystemTestAccess::SetFiniteDrainWaitHook(jobSystem, nullptr, nullptr);
        jobSystem.Shutdown();
        return false;
    }

    taskGate.Signal();
    if (!drainFinished.WaitFor("pre-initialize finite drain completion"))
    {
        drainThread.join();
        JobSystemTestAccess::SetFiniteDrainWaitHook(jobSystem, nullptr, nullptr);
        jobSystem.Shutdown();
        return false;
    }

    drainThread.join();
    JobSystemTestAccess::SetFiniteDrainWaitHook(jobSystem, nullptr, nullptr);
    const bool bSucceeded = CheckJobSystemCondition(completedFiniteTasksAtDrain.Load() == 1, "pre-initialize finite completion snapshot reached one");
    jobSystem.Shutdown();
    jobSystem.DrainAcceptedFiniteTasks();
    return bSucceeded;
}

bool TestFiniteDrainLifecycle(JobSystem::ExecutionMode mode)
{
    auto& jobSystem = JobSystem::Get();
    TestLatch persistentEntered;
    TestLatch persistentGate;
    TestLatch persistentFinished;
    TestLatch finiteEntered;
    TestLatch finiteGate;
    TestLatch queuedFiniteFinished;
    TestLatch childHandlerFinished;
    TestLatch grandchildHandlerFinished;
    TestLatch drainWaiting;
    TestLatch drainFinished;
    Atomic<bool> bUnexpectedGrandchildAcceptance(false);
    Atomic<size_t> completedFiniteTasks(0);
    Atomic<size_t> completedFiniteTasksAtDrain(0);
    std::thread::id finiteExecutionThread;
    std::thread::id queuedFiniteExecutionThread;
    std::thread::id drainExecutionThread;
    std::thread drainThread;

    auto cleanup = [&]()
    {
        finiteGate.Signal();
        persistentGate.Signal();
        if (drainThread.joinable())
        {
            drainThread.join();
        }
        JobSystemTestAccess::SetFiniteDrainWaitHook(jobSystem, nullptr, nullptr);
        jobSystem.Shutdown();
    };

    jobSystem.Initialize(2, mode);

    TaskPtr persistentTask = Task::Create([&persistentEntered, &persistentGate, &persistentFinished]()
    {
        persistentEntered.Signal();
        persistentGate.WaitFor("persistent task gate");
        persistentFinished.Signal();
    });

    if (!CheckJobSystemCondition(jobSystem.SubmitPersistentTask(persistentTask), "persistent task was accepted") ||
        !persistentEntered.WaitFor("persistent task entry"))
    {
        cleanup();
        return false;
    }

    TaskPtr finiteTask = Task::Create([&finiteEntered, &finiteGate, &finiteExecutionThread]()
    {
        finiteExecutionThread = std::this_thread::get_id();
        finiteEntered.Signal();
        finiteGate.WaitFor("finite task gate");
    });
    TaskPtr queuedFiniteTask = Task::Create([&queuedFiniteFinished, &queuedFiniteExecutionThread]()
    {
        queuedFiniteExecutionThread = std::this_thread::get_id();
        queuedFiniteFinished.Signal();
    });
    finiteTask->OnComplete([&completedFiniteTasks](const TaskPtr&)
    {
        completedFiniteTasks++;
    });
    queuedFiniteTask->OnComplete([&completedFiniteTasks](const TaskPtr&)
    {
        completedFiniteTasks++;
    });

    if (!CheckJobSystemCondition(jobSystem.SubmitTask(finiteTask), "gated finite task was accepted") ||
        !finiteEntered.WaitFor("gated finite task entry") ||
        !CheckJobSystemCondition(jobSystem.SubmitTask(queuedFiniteTask), "queued finite task was accepted"))
    {
        cleanup();
        return false;
    }

    jobSystem.Initialize(3, mode);
    if (!CheckJobSystemCondition(jobSystem.GetWorkerThreadCount() == 2, "running initialize preserved workers and generation"))
    {
        cleanup();
        return false;
    }

    jobSystem.StopAcceptingTasks();

    TaskPtr grandchildTask = Task::Create([]()
    {
    });
    TaskPtr childTask = Task::Create([]()
    {
    });
    grandchildTask->OnComplete([&grandchildHandlerFinished](const TaskPtr&)
    {
        grandchildHandlerFinished.Signal();
    });
    childTask->OnComplete([&jobSystem, &grandchildTask, &bUnexpectedGrandchildAcceptance, &childHandlerFinished](const TaskPtr&)
    {
        if (jobSystem.SubmitTask(grandchildTask))
        {
            bUnexpectedGrandchildAcceptance = true;
        }
        childHandlerFinished.Signal();
    });

    if (!CheckJobSystemCondition(!jobSystem.SubmitTask(childTask), "child submission was rejected after admission close") ||
        !childHandlerFinished.WaitFor("rejected child completion handler") ||
        !grandchildHandlerFinished.WaitFor("rejected grandchild completion handler") ||
        !CheckJobSystemCondition(childTask->GetState() == Task::State::CANCELED, "rejected child was canceled") ||
        !CheckJobSystemCondition(grandchildTask->GetState() == Task::State::CANCELED, "rejected grandchild was canceled") ||
        !CheckJobSystemCondition(!bUnexpectedGrandchildAcceptance.Load(), "rejected child handler could not reopen admission"))
    {
        cleanup();
        return false;
    }

    JobSystemTestAccess::SetFiniteDrainWaitHook(jobSystem, SignalDrainWait, &drainWaiting);
    drainThread = std::thread([&jobSystem, &drainFinished, &drainExecutionThread, &completedFiniteTasks, &completedFiniteTasksAtDrain]()
    {
        drainExecutionThread = std::this_thread::get_id();
        jobSystem.DrainAcceptedFiniteTasks();
        completedFiniteTasksAtDrain = completedFiniteTasks.Load();
        drainFinished.Signal();
    });

    if (!drainWaiting.WaitFor("finite drain predicate"))
    {
        cleanup();
        return false;
    }

    finiteGate.Signal();
    if (!drainFinished.WaitFor("finite drain completion") ||
        !queuedFiniteFinished.WaitFor("queued finite task completion"))
    {
        cleanup();
        return false;
    }

    drainThread.join();
    JobSystemTestAccess::SetFiniteDrainWaitHook(jobSystem, nullptr, nullptr);
    const bool bDrainChecksPassed =
        CheckJobSystemCondition(persistentTask->GetState() == Task::State::RUNNING, "persistent task remained running during finite drain") &&
        CheckJobSystemCondition(finiteExecutionThread != drainExecutionThread, "gated finite work was not executed by the drain caller") &&
        CheckJobSystemCondition(queuedFiniteExecutionThread != drainExecutionThread, "queued finite work was not executed by the drain caller") &&
        CheckJobSystemCondition(completedFiniteTasksAtDrain.Load() == 2, "both accepted finite tasks completed before finite drain returned") &&
        CheckJobSystemCondition(jobSystem.GetWorkerThreadCount() == 2, "workers remained live after finite drain");

    persistentGate.Signal();
    const bool bPersistentFinished = persistentFinished.WaitFor("persistent task completion");
    jobSystem.Shutdown();

    if (!bDrainChecksPassed || !bPersistentFinished)
    {
        return false;
    }

    TestLatch freshTaskFinished;
    jobSystem.Initialize(1, mode);
    TaskPtr freshTask = Task::Create([&freshTaskFinished]()
    {
        freshTaskFinished.Signal();
    });
    const bool bFreshTaskAccepted = jobSystem.SubmitTask(freshTask);
    const bool bFreshTaskFinished = freshTaskFinished.WaitFor("fresh generation finite task completion");
    jobSystem.Shutdown();

    return CheckJobSystemCondition(bFreshTaskAccepted, "post-shutdown finite task was accepted by a fresh generation") &&
           bFreshTaskFinished;
}

bool TestShutdownDropsQueuedFiniteTasks(JobSystem::ExecutionMode mode)
{
    auto& jobSystem = JobSystem::Get();
    TestLatch persistentEntered;
    TestLatch persistentGate;
    TestLatch canceledHandlerFinished;
    Atomic<bool> bFiniteTaskExecuted(false);
    std::thread shutdownThread;

    jobSystem.Initialize(1, mode);
    TaskPtr persistentTask = Task::Create([&persistentEntered, &persistentGate]()
    {
        persistentEntered.Signal();
        persistentGate.WaitFor("shutdown persistent task gate");
    });
    TaskPtr finiteTask = Task::Create([&bFiniteTaskExecuted]()
    {
        bFiniteTaskExecuted = true;
    });
    finiteTask->OnComplete([&canceledHandlerFinished](const TaskPtr&)
    {
        canceledHandlerFinished.Signal();
    });

    if (!CheckJobSystemCondition(jobSystem.SubmitPersistentTask(persistentTask), "shutdown persistent task was accepted") ||
        !persistentEntered.WaitFor("shutdown persistent task entry") ||
        !CheckJobSystemCondition(jobSystem.SubmitTask(finiteTask), "shutdown queued finite task was accepted"))
    {
        persistentGate.Signal();
        jobSystem.Shutdown();
        return false;
    }

    shutdownThread = std::thread([&jobSystem]()
    {
        jobSystem.Shutdown();
    });
    if (!WaitForShutdownRequest(jobSystem))
    {
        persistentGate.Signal();
        shutdownThread.join();
        return false;
    }

    persistentGate.Signal();
    shutdownThread.join();

    const bool bHandlerFinished = canceledHandlerFinished.WaitFor("shutdown canceled finite handler");
    jobSystem.DrainAcceptedFiniteTasks();
    return bHandlerFinished &&
           CheckJobSystemCondition(finiteTask->GetState() == Task::State::CANCELED, "shutdown dropped finite task was canceled") &&
           CheckJobSystemCondition(!bFiniteTaskExecuted.Load(), "shutdown dropped finite task was not executed");
}

bool RunChildScenario(const char* executablePath, const char* childMode)
{
    const char* childArguments[] =
    {
        executablePath,
        childMode,
        nullptr
    };
    const intptr_t childProcess = _spawnv(_P_NOWAIT, executablePath, childArguments);
    if (childProcess == -1)
    {
        return CheckJobSystemCondition(false, "child process was created");
    }

    HANDLE childHandle = reinterpret_cast<HANDLE>(childProcess);
    const DWORD waitResult = WaitForSingleObject(childHandle, 10000);
    if (waitResult != WAIT_OBJECT_0)
    {
        TerminateProcess(childHandle, 1);
        WaitForSingleObject(childHandle, INFINITE);
        CloseHandle(childHandle);
        return CheckJobSystemCondition(false, "child process wait returned WAIT_OBJECT_0");
    }

    DWORD exitCode = 1;
    const bool bExitCodeRead = GetExitCodeProcess(childHandle, &exitCode) != 0;
    CloseHandle(childHandle);
    return CheckJobSystemCondition(waitResult == WAIT_OBJECT_0 && bExitCodeRead && exitCode == 0, "child process scenario succeeded");
}

bool TestFiniteDrainAdmissionAndGeneration(const char* executablePath)
{
    std::cout << "Running JobSystem finite drain admission and generation tests..." << std::endl;

    return RunChildScenario(executablePath, "--pre-initialize-simple") &&
           RunChildScenario(executablePath, "--pre-initialize-work-stealing") &&
           RunChildScenario(executablePath, "--lifecycle-simple") &&
           RunChildScenario(executablePath, "--lifecycle-work-stealing") &&
           RunChildScenario(executablePath, "--shutdown-drop-simple") &&
           RunChildScenario(executablePath, "--shutdown-drop-work-stealing");
}

int main(int argc, char** argv)
{
    if (argc == 2)
    {
        if (std::strcmp(argv[1], "--pre-initialize-simple") == 0)
        {
            return TestPreInitializeFiniteDrain(JobSystem::ExecutionMode::EXECUTION_SIMPLE) ? 0 : 1;
        }

        if (std::strcmp(argv[1], "--pre-initialize-work-stealing") == 0)
        {
            return TestPreInitializeFiniteDrain(JobSystem::ExecutionMode::EXECUTION_WORK_STEALING) ? 0 : 1;
        }

        if (std::strcmp(argv[1], "--lifecycle-simple") == 0)
        {
            return TestFiniteDrainLifecycle(JobSystem::ExecutionMode::EXECUTION_SIMPLE) ? 0 : 1;
        }

        if (std::strcmp(argv[1], "--lifecycle-work-stealing") == 0)
        {
            return TestFiniteDrainLifecycle(JobSystem::ExecutionMode::EXECUTION_WORK_STEALING) ? 0 : 1;
        }

        if (std::strcmp(argv[1], "--shutdown-drop-simple") == 0)
        {
            return TestShutdownDropsQueuedFiniteTasks(JobSystem::ExecutionMode::EXECUTION_SIMPLE) ? 0 : 1;
        }

        if (std::strcmp(argv[1], "--shutdown-drop-work-stealing") == 0)
        {
            return TestShutdownDropsQueuedFiniteTasks(JobSystem::ExecutionMode::EXECUTION_WORK_STEALING) ? 0 : 1;
        }
    }

    std::cout << "Running JobSystem shutdown tests..." << std::endl;

    if (!TestFiniteDrainAdmissionAndGeneration(argv[0]))
    {
        return 1;
    }

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
