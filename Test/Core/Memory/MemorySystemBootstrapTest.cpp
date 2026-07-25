#include "EngineGlobals/MemoryOverrides.h"
#include "Memory/GlobalAllocator.h"
#include "Memory/MemorySystem.h"
#include "Memory/ThreadLocalCache.h"

#include <Windows.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace
{
    constexpr size_t LargeAllocationSize = 1024 * 1024;
    constexpr size_t SmallAllocationSize = NorvesLib::Memory::Config::SizeClasses[0];
    constexpr size_t PoolGrowthBlockCount = NorvesLib::Memory::Config::DefaultBlocksPerChunk + 1;

    struct WorkerProbeState
    {
        HANDLE FirstGenerationComplete = nullptr;
        HANDLE BeginSecondGeneration = nullptr;
        HANDLE SecondGenerationCacheReady = nullptr;
        HANDLE VerifyFlushedCache = nullptr;
        HANDLE SecondGenerationComplete = nullptr;
        NorvesLib::Memory::ThreadLocalCache* FirstGenerationCache = nullptr;
        NorvesLib::Memory::ThreadLocalCache* SecondGenerationCache = nullptr;
        NorvesLib::Memory::ThreadLocalCache* ParentSecondGenerationCache = nullptr;
        bool bFirstGenerationCacheObserved = false;
        bool bFlushBeforeSecondCache = false;
        bool bSecondGenerationCacheObserved = false;
        bool bSecondGenerationCachePopulated = false;
        bool bWorkerCacheFlushedByParent = false;
        bool bPassed = false;
    };

    bool Check(bool bCondition, const char* expression, int line)
    {
        if (!bCondition)
        {
            std::cout << "CHECK failed at line " << line << ": " << expression << "\n";
        }
        return bCondition;
    }

#define CHECK_EXPRESSION(expression) \
    do \
    { \
        if (!Check((expression), #expression, __LINE__)) \
        { \
            return false; \
        } \
    } while (false)

    DWORD WINAPI RunStaleCacheWorker(void* parameter)
    {
        WorkerProbeState* state = static_cast<WorkerProbeState*>(parameter);
        bool bPassed = true;
        void* firstAllocation = NorvesLib::Memory::Malloc(SmallAllocationSize);
        if (firstAllocation == nullptr)
        {
            bPassed = false;
        }
        else
        {
            std::memset(firstAllocation, 0x2A, SmallAllocationSize);
            bPassed = static_cast<unsigned char*>(firstAllocation)[SmallAllocationSize - 1] == 0x2A;
            state->FirstGenerationCache = NorvesLib::Memory::MemorySystem::GetThreadLocalCache();
            state->bFirstGenerationCacheObserved = state->FirstGenerationCache != nullptr;
            bPassed = bPassed && state->bFirstGenerationCacheObserved;
            NorvesLib::Memory::Free(firstAllocation);
        }

        if (SetEvent(state->FirstGenerationComplete) == FALSE)
        {
            SetEvent(state->SecondGenerationCacheReady);
            SetEvent(state->SecondGenerationComplete);
            return EXIT_FAILURE;
        }
        if (WaitForSingleObject(state->BeginSecondGeneration, INFINITE) != WAIT_OBJECT_0)
        {
            SetEvent(state->SecondGenerationCacheReady);
            SetEvent(state->SecondGenerationComplete);
            return EXIT_FAILURE;
        }

        NorvesLib::Memory::MemorySystem::FlushThreadCache();
        state->bFlushBeforeSecondCache = true;
        void* secondAllocation = NorvesLib::Memory::Malloc(SmallAllocationSize);
        if (secondAllocation == nullptr)
        {
            bPassed = false;
        }
        else
        {
            std::memset(secondAllocation, 0x5C, SmallAllocationSize);
            bPassed = bPassed && static_cast<unsigned char*>(secondAllocation)[SmallAllocationSize - 1] == 0x5C;
            state->SecondGenerationCache = NorvesLib::Memory::MemorySystem::GetThreadLocalCache();
            state->bSecondGenerationCacheObserved = state->SecondGenerationCache != nullptr;
            bPassed = bPassed && state->bSecondGenerationCacheObserved;
            NorvesLib::Memory::Free(secondAllocation);
            state->bSecondGenerationCachePopulated = state->SecondGenerationCache != nullptr &&
                state->SecondGenerationCache->GetTotalCachedCount() > 0;
            bPassed = bPassed && state->bSecondGenerationCachePopulated;
        }

        if (SetEvent(state->SecondGenerationCacheReady) == FALSE)
        {
            SetEvent(state->SecondGenerationComplete);
            return EXIT_FAILURE;
        }
        if (WaitForSingleObject(state->VerifyFlushedCache, INFINITE) != WAIT_OBJECT_0)
        {
            SetEvent(state->SecondGenerationComplete);
            return EXIT_FAILURE;
        }

        state->bWorkerCacheFlushedByParent = state->SecondGenerationCache != nullptr &&
            state->SecondGenerationCache->GetTotalCachedCount() == 0;
        state->bPassed = bPassed && state->bFlushBeforeSecondCache && state->bWorkerCacheFlushedByParent;
        const bool bSignaledCompletion = SetEvent(state->SecondGenerationComplete) != FALSE;
        return bSignaledCompletion && state->bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    bool RunWrapperAllocatedBlockProbe()
    {
        NorvesLib::Memory::Initialize();
        NorvesLib::Memory::GlobalAllocator* allocator = NorvesLib::Memory::MemorySystem::GetGlobalAllocator();
        bool bPassed = allocator != nullptr;
        uint64_t deallocationCount = bPassed ? allocator->GetTotalDeallocationCount() : 0;
        void* allocation = bPassed ? NorvesLib::Memory::Malloc(LargeAllocationSize) : nullptr;
        if (allocation == nullptr)
        {
            bPassed = false;
        }
        else
        {
            std::memset(allocation, 0xA5, LargeAllocationSize);
            bPassed = static_cast<unsigned char*>(allocation)[LargeAllocationSize - 1] == 0xA5;
            NorvesLib::Memory::Free(allocation);
            bPassed = bPassed && allocator->GetTotalDeallocationCount() == deallocationCount + 1;
        }
        NorvesLib::Memory::Shutdown();
        return bPassed;
    }

    bool RunDirectGlobalFreeProbe()
    {
        NorvesLib::Memory::Initialize();
        NorvesLib::Memory::GlobalAllocator* allocator = NorvesLib::Memory::MemorySystem::GetGlobalAllocator();
        bool bPassed = allocator != nullptr;
        uint64_t deallocationCount = bPassed ? allocator->GetTotalDeallocationCount() : 0;
        void* allocation = bPassed ? allocator->Allocate(LargeAllocationSize) : nullptr;
        if (allocation == nullptr)
        {
            bPassed = false;
        }
        else
        {
            NorvesLib::Memory::Free(allocation);
            bPassed = allocator->GetTotalDeallocationCount() == deallocationCount + 1;
        }
        NorvesLib::Memory::Shutdown();
        return bPassed;
    }

    bool RunStaleThreadCacheProbe()
    {
        WorkerProbeState state;
        state.FirstGenerationComplete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        state.BeginSecondGeneration = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        state.SecondGenerationCacheReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        state.VerifyFlushedCache = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        state.SecondGenerationComplete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (state.FirstGenerationComplete == nullptr || state.BeginSecondGeneration == nullptr ||
            state.SecondGenerationCacheReady == nullptr || state.VerifyFlushedCache == nullptr ||
            state.SecondGenerationComplete == nullptr)
        {
            if (state.FirstGenerationComplete != nullptr)
            {
                CloseHandle(state.FirstGenerationComplete);
            }
            if (state.BeginSecondGeneration != nullptr)
            {
                CloseHandle(state.BeginSecondGeneration);
            }
            if (state.SecondGenerationCacheReady != nullptr)
            {
                CloseHandle(state.SecondGenerationCacheReady);
            }
            if (state.VerifyFlushedCache != nullptr)
            {
                CloseHandle(state.VerifyFlushedCache);
            }
            if (state.SecondGenerationComplete != nullptr)
            {
                CloseHandle(state.SecondGenerationComplete);
            }
            return false;
        }

        bool bMemoryInitialized = false;
        bool bPassed = true;
        HANDLE worker = nullptr;
        NorvesLib::Memory::Initialize();
        bMemoryInitialized = true;
        worker = CreateThread(nullptr, 0, &RunStaleCacheWorker, &state, 0, nullptr);
        if (worker == nullptr)
        {
            bPassed = false;
        }
        else
        {
            HANDLE firstPhaseWaitHandles[2]{state.FirstGenerationComplete, worker};
            const DWORD firstPhaseWait = WaitForMultipleObjects(2, firstPhaseWaitHandles, FALSE, INFINITE);
            if (firstPhaseWait != WAIT_OBJECT_0)
            {
                bPassed = false;
            }
            else
            {
                NorvesLib::Memory::Shutdown();
                bMemoryInitialized = false;
                NorvesLib::Memory::Initialize();
                bMemoryInitialized = true;
                void* parentAllocation = NorvesLib::Memory::Malloc(SmallAllocationSize);
                if (parentAllocation == nullptr)
                {
                    bPassed = false;
                }
                else
                {
                    std::memset(parentAllocation, 0x7E, SmallAllocationSize);
                    bPassed = bPassed && static_cast<unsigned char*>(parentAllocation)[SmallAllocationSize - 1] == 0x7E;
                    state.ParentSecondGenerationCache = NorvesLib::Memory::MemorySystem::GetThreadLocalCache();
                    bPassed = bPassed && state.ParentSecondGenerationCache != nullptr;
                    NorvesLib::Memory::Free(parentAllocation);
                }
                if (SetEvent(state.BeginSecondGeneration) == FALSE)
                {
                    bPassed = false;
                }
                else
                {
                    HANDLE workerReadyWaitHandles[2]{state.SecondGenerationCacheReady, worker};
                    const DWORD workerReadyWait = WaitForMultipleObjects(2, workerReadyWaitHandles, FALSE, INFINITE);
                    if (workerReadyWait != WAIT_OBJECT_0)
                    {
                        bPassed = false;
                    }
                    else
                    {
                        bPassed = bPassed && state.bSecondGenerationCacheObserved &&
                            state.bSecondGenerationCachePopulated &&
                            state.SecondGenerationCache != state.ParentSecondGenerationCache;
                        NorvesLib::Memory::MemorySystem::FlushAllThreadCaches();
                        if (SetEvent(state.VerifyFlushedCache) == FALSE)
                        {
                            bPassed = false;
                        }
                        else
                        {
                            HANDLE secondPhaseWaitHandles[2]{state.SecondGenerationComplete, worker};
                            const DWORD secondPhaseWait = WaitForMultipleObjects(2, secondPhaseWaitHandles, FALSE, INFINITE);
                            if (secondPhaseWait != WAIT_OBJECT_0 || WaitForSingleObject(worker, INFINITE) != WAIT_OBJECT_0)
                            {
                                bPassed = false;
                            }
                        }
                    }
                }
            }
        }
        DWORD exitCode = EXIT_FAILURE;
        bool bGotExitCode = false;
        if (worker != nullptr)
        {
            if (SetEvent(state.BeginSecondGeneration) == FALSE)
            {
                bPassed = false;
            }
            if (SetEvent(state.VerifyFlushedCache) == FALSE)
            {
                bPassed = false;
            }
            if (WaitForSingleObject(worker, INFINITE) != WAIT_OBJECT_0)
            {
                bPassed = false;
            }
            bGotExitCode = GetExitCodeThread(worker, &exitCode) != FALSE;
            CloseHandle(worker);
        }
        CloseHandle(state.FirstGenerationComplete);
        CloseHandle(state.BeginSecondGeneration);
        CloseHandle(state.SecondGenerationCacheReady);
        CloseHandle(state.VerifyFlushedCache);
        CloseHandle(state.SecondGenerationComplete);
        if (bMemoryInitialized)
        {
            NorvesLib::Memory::Shutdown();
        }
        return bPassed && bGotExitCode && exitCode == EXIT_SUCCESS && state.bPassed &&
            state.bFirstGenerationCacheObserved && state.bFlushBeforeSecondCache && state.bSecondGenerationCacheObserved &&
            state.bSecondGenerationCachePopulated && state.bWorkerCacheFlushedByParent;
    }

    bool RunLifecycleProbe()
    {
        NorvesLib::Memory::Initialize();
        bool bPassed = NorvesLib::Memory::MemorySystem::IsInitialized();
        NorvesLib::Memory::Initialize();
        NorvesLib::Memory::Shutdown();
        NorvesLib::Memory::Shutdown();
        NorvesLib::Memory::Initialize();
        void* allocation = bPassed ? NorvesLib::Memory::Malloc(LargeAllocationSize) : nullptr;
        if (allocation == nullptr)
        {
            bPassed = false;
        }
        else
        {
            NorvesLib::Memory::Free(allocation);
        }
        NorvesLib::Memory::Shutdown();
        return bPassed;
    }

    void ConfigurePoolGrowthChildFailureReporting()
    {
#ifdef _MSC_VER
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    }

    bool RunPoolChunkGrowthProbe()
    {
        for (size_t generation = 0; generation < 2; ++generation)
        {
            void* allocations[PoolGrowthBlockCount]{};
            bool bPassed = true;
            NorvesLib::Memory::Initialize();
            NorvesLib::Memory::GlobalAllocator* allocator = NorvesLib::Memory::MemorySystem::GetGlobalAllocator();
            const size_t totalSizeBefore = allocator != nullptr ? allocator->GetTotalSize() : 0;
            if (allocator == nullptr)
            {
                bPassed = false;
            }

            for (size_t index = 0; bPassed && index < PoolGrowthBlockCount; ++index)
            {
                allocations[index] = NorvesLib::Memory::Malloc(NorvesLib::Memory::Config::SizeClasses[0]);
                if (allocations[index] == nullptr)
                {
                    bPassed = false;
                    break;
                }
                std::memset(allocations[index], static_cast<int>(generation + index),
                    NorvesLib::Memory::Config::SizeClasses[0]);
                const unsigned char expected = static_cast<unsigned char>(generation + index);
                if (static_cast<unsigned char*>(allocations[index])[0] != expected ||
                    static_cast<unsigned char*>(allocations[index])[NorvesLib::Memory::Config::SizeClasses[0] - 1] != expected ||
                    !allocator->OwnsMemory(allocations[index]) ||
                    NorvesLib::Memory::MemorySystem::GetBlockSize(allocations[index]) !=
                        NorvesLib::Memory::Config::SizeClasses[0])
                {
                    bPassed = false;
                }
            }

            if (allocator != nullptr && allocator->GetTotalSize() <= totalSizeBefore)
            {
                bPassed = false;
            }

            for (void* allocation : allocations)
            {
                NorvesLib::Memory::Free(allocation);
            }
            NorvesLib::Memory::MemorySystem::FlushThreadCache();
            NorvesLib::Memory::Shutdown();
            if (NorvesLib::Memory::IsInitialized() || NorvesLib::Memory::MemorySystem::IsInitialized())
            {
                bPassed = false;
            }
            if (!bPassed)
            {
                return false;
            }
        }
        return true;
    }

    bool RunChildProbe(const wchar_t* argument, DWORD& outExitCode)
    {
        wchar_t executablePath[MAX_PATH]{};
        CHECK_EXPRESSION(GetModuleFileNameW(nullptr, executablePath, MAX_PATH) > 0);
        wchar_t commandLine[MAX_PATH + 64]{};
        CHECK_EXPRESSION(swprintf_s(commandLine, L"\"%s\" %s", executablePath, argument) > 0);
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        CHECK_EXPRESSION(CreateProcessW(nullptr, commandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &startupInfo, &processInfo) != FALSE);
        CHECK_EXPRESSION(WaitForSingleObject(processInfo.hProcess, INFINITE) == WAIT_OBJECT_0);
        const bool bReadExitCode = GetExitCodeProcess(processInfo.hProcess, &outExitCode) != FALSE;
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        CHECK_EXPRESSION(bReadExitCode);
        return true;
    }

    bool VerifyChildProbe(const wchar_t* argument)
    {
        DWORD exitCode = EXIT_FAILURE;
        CHECK_EXPRESSION(RunChildProbe(argument, exitCode));
        CHECK_EXPRESSION(exitCode == EXIT_SUCCESS);
        return true;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount == 2)
    {
        const char* argument = arguments[1];
        bool bPassed = false;
        if (std::strcmp(argument, "--wrapper-allocation-probe") == 0)
        {
            bPassed = RunWrapperAllocatedBlockProbe();
        }
        else if (std::strcmp(argument, "--direct-global-free-probe") == 0)
        {
            bPassed = RunDirectGlobalFreeProbe();
        }
        else if (std::strcmp(argument, "--stale-thread-cache-probe") == 0)
        {
            bPassed = RunStaleThreadCacheProbe();
        }
        else if (std::strcmp(argument, "--lifecycle-probe") == 0)
        {
            bPassed = RunLifecycleProbe();
        }
        else if (std::strcmp(argument, "--pool-chunk-growth-probe") == 0)
        {
            ConfigurePoolGrowthChildFailureReporting();
            bPassed = RunPoolChunkGrowthProbe();
        }
        ExitProcess(bPassed ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    std::cout << "MemorySystemBootstrapTest start\n";
    const bool bPassed = VerifyChildProbe(L"--wrapper-allocation-probe") &&
        VerifyChildProbe(L"--direct-global-free-probe") &&
        VerifyChildProbe(L"--stale-thread-cache-probe") &&
        VerifyChildProbe(L"--lifecycle-probe") &&
        VerifyChildProbe(L"--pool-chunk-growth-probe");
    std::cout << (bPassed ? "MemorySystemBootstrapTest passed\n" : "MemorySystemBootstrapTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
