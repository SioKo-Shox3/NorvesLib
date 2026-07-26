#include "Scripting/ScriptRuntime.h"
#include "Component/ScriptComponent.h"

#include "Scripting/AngelScriptEngineOwner.h"
#include "EngineGlobals/MemoryOverrides.h"
#include "Object/World.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <Windows.h>
#include <angelscript.h>

namespace
{
    bool Check(bool bCondition, const char* expression, int line)
    {
        if (!bCondition)
        {
            std::cout << "CHECK failed at line " << line << ": " << expression << "\n";
        }
        return bCondition;
    }

    bool RunCase(const char* name, bool (*test)())
    {
        std::cout << name << " start" << std::endl;
        const bool bPassed = test();
        std::cout << name << (bPassed ? " passed" : " failed") << std::endl;
        return bPassed;
    }

#define CHECK_EXPRESSION(expression) \
    do \
    { \
        if (!Check((expression), #expression, __LINE__)) \
        { \
            return false; \
        } \
    } while (false)

    asIScriptEngine* CreateNullEngine()
    {
        return nullptr;
    }

    bool RejectEngine(asIScriptEngine* engine)
    {
        return engine == nullptr;
    }

    bool ThrowingValidator(asIScriptEngine*)
    {
        throw 1;
    }

    asIScriptEngine* GRetainedExceptionEngine = nullptr;

    bool RetainAndThrowValidator(asIScriptEngine* engine)
    {
        GRetainedExceptionEngine = engine;
        engine->AddRef();
        throw 1;
    }

    void ProbeFree(void* memory)
    {
        NorvesLib::Memory::Free(memory);
    }

    void* ProbeAllocate(size_t size)
    {
        return NorvesLib::Memory::Malloc(size);
    }

    void RequireChildPredicate(bool bCondition, UINT exitCode)
    {
        if (!bCondition)
        {
            ExitProcess(exitCode);
        }
    }

    struct RetainedResources
    {
        asIScriptObject* m_Object = nullptr;
        asIScriptContext* m_Context = nullptr;
    };

    struct ReleasedResourceState
    {
        int Context = -1;
        int Object = -1;
        int Module = -1;
        asUINT ModuleCount = ~asUINT{0};
    };

    bool CreateRetainedResources(
        NorvesLib::Core::Scripting::AngelScriptEngineOwner& owner,
        RetainedResources& outResources)
    {
        asIScriptEngine* engine = owner.GetEngine();
        CHECK_EXPRESSION(engine != nullptr);

        asIScriptModule* module = engine->GetModule("BootstrapProbe", asGM_ALWAYS_CREATE);
        CHECK_EXPRESSION(module != nullptr);
        CHECK_EXPRESSION(module->AddScriptSection("BootstrapProbe", "class BootstrapProbe {}") >= 0);
        CHECK_EXPRESSION(module->Build() >= 0);

        asITypeInfo* type = module->GetTypeInfoByName("BootstrapProbe");
        CHECK_EXPRESSION(type != nullptr);
        outResources.m_Object = static_cast<asIScriptObject*>(engine->CreateScriptObject(type));
        CHECK_EXPRESSION(outResources.m_Object != nullptr);
        outResources.m_Context = engine->CreateContext();
        CHECK_EXPRESSION(outResources.m_Context != nullptr);
        return true;
    }

    bool ExerciseEngine(
        NorvesLib::Core::Scripting::AngelScriptEngineOwner& owner,
        ReleasedResourceState& outState)
    {
        RetainedResources resources;
        CHECK_EXPRESSION(CreateRetainedResources(owner, resources));
        outState.Context = resources.m_Context->Release();
        resources.m_Context = nullptr;
        CHECK_EXPRESSION(outState.Context == 0);
        outState.Object = resources.m_Object->Release();
        resources.m_Object = nullptr;
        CHECK_EXPRESSION(outState.Object == 0);
        outState.Module = owner.GetEngine()->DiscardModule("BootstrapProbe");
        CHECK_EXPRESSION(outState.Module == 0);
        outState.ModuleCount = owner.GetEngine()->GetModuleCount();
        CHECK_EXPRESSION(outState.ModuleCount == 0);
        CHECK_EXPRESSION(asGetActiveContext() == nullptr);
        return true;
    }

    bool VerifyCycle()
    {
        NorvesLib::Core::Scripting::AngelScriptEngineOwner owner;
        CHECK_EXPRESSION(owner.Initialize());
        CHECK_EXPRESSION(owner.OwnsGlobalAllocator());
        {
            NorvesLib::Core::Scripting::AngelScriptEngineOwner contender;
            CHECK_EXPRESSION(!contender.Initialize());
        }
        CHECK_EXPRESSION(owner.OwnsGlobalAllocator());
        ReleasedResourceState releasedState;
        CHECK_EXPRESSION(ExerciseEngine(owner, releasedState));
        CHECK_EXPRESSION(owner.Shutdown());
        CHECK_EXPRESSION(!owner.IsInitialized());
        CHECK_EXPRESSION(NorvesLib::Core::Scripting::GetActiveAngelScriptEngine() == nullptr);

        std::cout << "cycle_refcounts context=" << releasedState.Context
                  << " object=" << releasedState.Object
                  << " module=" << releasedState.ModuleCount
                  << " engine=" << (owner.IsInitialized() ? 1 : 0) << std::endl;

        const NorvesLib::Core::ScriptRuntimeDiagnostics& diagnostics = owner.GetDiagnostics();
        CHECK_EXPRESSION(diagnostics.AllocationCount > 0);
        CHECK_EXPRESSION(diagnostics.AllocationCount == diagnostics.FreeCount);
        CHECK_EXPRESSION(diagnostics.LiveAllocationCount == 0);
        CHECK_EXPRESSION(diagnostics.ActiveBindingCount == 0);
        CHECK_EXPRESSION(diagnostics.GcStepCount == 0);
        return true;
    }

    bool VerifyRetainedResourcesDelayShutdown()
    {
        NorvesLib::Core::Scripting::AngelScriptEngineOwner owner;
        CHECK_EXPRESSION(owner.Initialize());
        asIScriptEngine* engine = owner.GetEngine();
        CHECK_EXPRESSION(engine->AddRef() > 0);

        RetainedResources resources;
        CHECK_EXPRESSION(CreateRetainedResources(owner, resources));
        CHECK_EXPRESSION(!owner.Shutdown());
        CHECK_EXPRESSION(owner.IsInitialized());
        CHECK_EXPRESSION(owner.OwnsGlobalAllocator());
        CHECK_EXPRESSION(owner.GetDiagnostics().LastResult ==
            NorvesLib::Core::EScriptRuntimeResult::ExecutionFailed);

        {
            NorvesLib::Core::Scripting::AngelScriptEngineOwner contender;
            CHECK_EXPRESSION(!contender.Initialize());
        }

        CHECK_EXPRESSION(resources.m_Context->Release() == 0);
        resources.m_Context = nullptr;
        CHECK_EXPRESSION(resources.m_Object->Release() == 0);
        resources.m_Object = nullptr;
        CHECK_EXPRESSION(engine->Release() > 0);
        CHECK_EXPRESSION(owner.Shutdown());
        CHECK_EXPRESSION(owner.GetDiagnostics().AllocationCount == owner.GetDiagnostics().FreeCount);
        CHECK_EXPRESSION(owner.GetDiagnostics().LiveAllocationCount == 0);
        CHECK_EXPRESSION(asSetGlobalMemoryFunctions(&ProbeAllocate, &ProbeFree) == 0);
        CHECK_EXPRESSION(asResetGlobalMemoryFunctions() == 0);
        CHECK_EXPRESSION(VerifyCycle());
        return true;
    }

    bool VerifyRuntimeShutdownFailurePropagation()
    {
        NorvesLib::Core::World world;
        NorvesLib::Core::ScriptRuntime runtime;
        CHECK_EXPRESSION(runtime.Initialize(world) == NorvesLib::Core::EScriptRuntimeResult::Success);
        asIScriptEngine* engine = NorvesLib::Core::Scripting::GetActiveAngelScriptEngine();
        CHECK_EXPRESSION(engine != nullptr);
        CHECK_EXPRESSION(engine->AddRef() > 0);
        CHECK_EXPRESSION(runtime.Shutdown() == NorvesLib::Core::EScriptRuntimeResult::ExecutionFailed);
        CHECK_EXPRESSION(runtime.IsInitialized());
        CHECK_EXPRESSION(engine->Release() > 0);
        CHECK_EXPRESSION(runtime.Shutdown() == NorvesLib::Core::EScriptRuntimeResult::Success);
        return true;
    }

    bool VerifyBootstrapStubsPreserveState()
    {
        NorvesLib::Core::World world;
        NorvesLib::Core::ScriptRuntime runtime;
        NorvesLib::Core::Component::ScriptComponent component;
        NorvesLib::Core::ScriptBindingHandle handle;
        handle.SlotIndex = 7;
        handle.Generation = 11;
        CHECK_EXPRESSION(runtime.BindComponent(component, handle) ==
            NorvesLib::Core::EScriptRuntimeResult::NotInitialized);
        CHECK_EXPRESSION(handle.SlotIndex == 7);
        CHECK_EXPRESSION(handle.Generation == 11);

        CHECK_EXPRESSION(runtime.Initialize(world) == NorvesLib::Core::EScriptRuntimeResult::Success);
        CHECK_EXPRESSION(runtime.BeginFrameMaintenance(0.016f) == NorvesLib::Core::EScriptRuntimeResult::Success);
        CHECK_EXPRESSION(runtime.EndFrameMaintenance() == NorvesLib::Core::EScriptRuntimeResult::Success);
        CHECK_EXPRESSION(runtime.GetDiagnostics().GcStepCount == 1);

        CHECK_EXPRESSION(runtime.BindComponent(component, handle) ==
            NorvesLib::Core::EScriptRuntimeResult::InvalidArgument);
        CHECK_EXPRESSION(handle.SlotIndex == 7);
        CHECK_EXPRESSION(handle.Generation == 11);
        CHECK_EXPRESSION(runtime.UnbindComponent(handle) == NorvesLib::Core::EScriptRuntimeResult::Success);
        CHECK_EXPRESSION(!handle.IsValid());
        CHECK_EXPRESSION(runtime.TickComponent(handle, 0.016f) == NorvesLib::Core::EScriptRuntimeResult::InvalidHandle);
        CHECK_EXPRESSION(runtime.Shutdown() == NorvesLib::Core::EScriptRuntimeResult::Success);
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

    bool VerifyPimplAllocatorCrossesMemorySystemPhase()
    {
        DWORD exitCode = EXIT_FAILURE;
        CHECK_EXPRESSION(RunChildProbe(L"--pimpl-phase-probe", exitCode));
        CHECK_EXPRESSION(exitCode == EXIT_SUCCESS);
        return true;
    }

    bool VerifyBindingStorageCrossesMemorySystemPhase()
    {
        DWORD exitCode = EXIT_FAILURE;
        CHECK_EXPRESSION(RunChildProbe(L"--binding-storage-probe", exitCode));
        CHECK_EXPRESSION(exitCode == EXIT_SUCCESS);
        return true;
    }

    bool VerifyBindingStorageShutdownRetry()
    {
        DWORD exitCode = EXIT_FAILURE;
        CHECK_EXPRESSION(RunChildProbe(L"--binding-storage-shutdown-retry-probe", exitCode));
        CHECK_EXPRESSION(exitCode == EXIT_SUCCESS);
        return true;
    }

    bool VerifyDestructorFailsFastWithoutPoisoningParent()
    {
        DWORD exitCode = EXIT_SUCCESS;
        CHECK_EXPRESSION(RunChildProbe(L"--destructor-fail-fast-probe", exitCode));
        CHECK_EXPRESSION(exitCode != EXIT_SUCCESS);
        CHECK_EXPRESSION(VerifyCycle());
        CHECK_EXPRESSION(asSetGlobalMemoryFunctions(&ProbeAllocate, &ProbeFree) == 0);
        CHECK_EXPRESSION(asResetGlobalMemoryFunctions() == 0);
        return true;
    }

    bool VerifyPartialInitializeRollback()
    {
        NorvesLib::Core::Scripting::AngelScriptEngineOwner owner;
        CHECK_EXPRESSION(!owner.Initialize(&CreateNullEngine));
        CHECK_EXPRESSION(owner.GetDiagnostics().AllocationCount == owner.GetDiagnostics().FreeCount);
        CHECK_EXPRESSION(owner.GetDiagnostics().LiveAllocationCount == 0);
        CHECK_EXPRESSION(asSetGlobalMemoryFunctions(&ProbeAllocate, &ProbeFree) == 0);
        CHECK_EXPRESSION(asResetGlobalMemoryFunctions() == 0);
        return true;
    }

    bool VerifyRejectedRealEngineRollback()
    {
        NorvesLib::Core::Scripting::AngelScriptEngineOwner owner;
        CHECK_EXPRESSION(!owner.Initialize(
            &NorvesLib::Core::Scripting::CreateDefaultScriptEngine,
            &RejectEngine));
        CHECK_EXPRESSION(!owner.IsInitialized());
        CHECK_EXPRESSION(!owner.OwnsGlobalAllocator());
        CHECK_EXPRESSION(owner.GetDiagnostics().AllocationCount > 0);
        CHECK_EXPRESSION(owner.GetDiagnostics().AllocationCount == owner.GetDiagnostics().FreeCount);
        CHECK_EXPRESSION(owner.GetDiagnostics().LiveAllocationCount == 0);
        CHECK_EXPRESSION(NorvesLib::Core::Scripting::GetActiveAngelScriptEngine() == nullptr);
        CHECK_EXPRESSION(asSetGlobalMemoryFunctions(&ProbeAllocate, &ProbeFree) == 0);
        CHECK_EXPRESSION(asResetGlobalMemoryFunctions() == 0);
        CHECK_EXPRESSION(VerifyCycle());
        return true;
    }

    bool VerifyValidatorExceptionCleanRollback()
    {
        NorvesLib::Core::Scripting::AngelScriptEngineOwner owner;
        CHECK_EXPRESSION(!owner.Initialize(
            &NorvesLib::Core::Scripting::CreateDefaultScriptEngine,
            &ThrowingValidator));
        CHECK_EXPRESSION(owner.GetDiagnostics().LastResult ==
            NorvesLib::Core::EScriptRuntimeResult::ExecutionFailed);
        CHECK_EXPRESSION(!owner.IsInitialized());
        CHECK_EXPRESSION(!owner.OwnsGlobalAllocator());
        CHECK_EXPRESSION(NorvesLib::Core::Scripting::GetActiveAngelScriptEngine() == nullptr);
        CHECK_EXPRESSION(owner.GetDiagnostics().AllocationCount == owner.GetDiagnostics().FreeCount);
        CHECK_EXPRESSION(owner.GetDiagnostics().LiveAllocationCount == 0);
        CHECK_EXPRESSION(asSetGlobalMemoryFunctions(&ProbeAllocate, &ProbeFree) == 0);
        CHECK_EXPRESSION(asResetGlobalMemoryFunctions() == 0);
        CHECK_EXPRESSION(owner.Initialize());
        CHECK_EXPRESSION(owner.Shutdown());
        return true;
    }

    bool VerifyValidatorExceptionRetainedPendingShutdown()
    {
        GRetainedExceptionEngine = nullptr;
        NorvesLib::Core::Scripting::AngelScriptEngineOwner owner;
        CHECK_EXPRESSION(!owner.Initialize(
            &NorvesLib::Core::Scripting::CreateDefaultScriptEngine,
            &RetainAndThrowValidator));
        CHECK_EXPRESSION(owner.GetDiagnostics().LastResult ==
            NorvesLib::Core::EScriptRuntimeResult::ExecutionFailed);
        CHECK_EXPRESSION(GRetainedExceptionEngine != nullptr);
        CHECK_EXPRESSION(owner.IsInitialized());
        CHECK_EXPRESSION(owner.OwnsGlobalAllocator());
        CHECK_EXPRESSION(NorvesLib::Core::Scripting::GetActiveAngelScriptEngine() == GRetainedExceptionEngine);
        {
            NorvesLib::Core::Scripting::AngelScriptEngineOwner contender;
            CHECK_EXPRESSION(!contender.Initialize());
        }
        CHECK_EXPRESSION(owner.GetDiagnostics().LiveAllocationCount > 0);
        CHECK_EXPRESSION(GRetainedExceptionEngine->Release() > 0);
        GRetainedExceptionEngine = nullptr;
        CHECK_EXPRESSION(owner.Shutdown());
        CHECK_EXPRESSION(owner.GetDiagnostics().AllocationCount == owner.GetDiagnostics().FreeCount);
        CHECK_EXPRESSION(owner.GetDiagnostics().LiveAllocationCount == 0);
        CHECK_EXPRESSION(NorvesLib::Core::Scripting::GetActiveAngelScriptEngine() == nullptr);
        CHECK_EXPRESSION(asSetGlobalMemoryFunctions(&ProbeAllocate, &ProbeFree) == 0);
        CHECK_EXPRESSION(asResetGlobalMemoryFunctions() == 0);
        CHECK_EXPRESSION(VerifyCycle());
        return true;
    }

    bool VerifyHandleBasics()
    {
        NorvesLib::Core::ScriptBindingHandle handle;
        CHECK_EXPRESSION(!handle.IsValid());
        handle.SlotIndex = 0;
        CHECK_EXPRESSION(handle.IsValid());
        handle.Reset();
        CHECK_EXPRESSION(!handle.IsValid());
        return true;
    }

    bool VerifyResultOrdering()
    {
        const uint8_t notInitialized = static_cast<uint8_t>(
            NorvesLib::Core::EScriptRuntimeResult::NotInitialized);
        const uint8_t wrongThread = static_cast<uint8_t>(
            NorvesLib::Core::EScriptRuntimeResult::WrongThread);
        const uint8_t invalidArgument = static_cast<uint8_t>(
            NorvesLib::Core::EScriptRuntimeResult::InvalidArgument);
        CHECK_EXPRESSION(wrongThread == notInitialized + 1);
        CHECK_EXPRESSION(invalidArgument == wrongThread + 1);
        return true;
    }

    bool RunTest()
    {
        CHECK_EXPRESSION(RunCase("VerifyPartialInitializeRollback", &VerifyPartialInitializeRollback));
        CHECK_EXPRESSION(RunCase("VerifyRejectedRealEngineRollback", &VerifyRejectedRealEngineRollback));
        CHECK_EXPRESSION(RunCase("VerifyValidatorExceptionCleanRollback", &VerifyValidatorExceptionCleanRollback));
        CHECK_EXPRESSION(RunCase("VerifyValidatorExceptionRetainedPendingShutdown", &VerifyValidatorExceptionRetainedPendingShutdown));
        CHECK_EXPRESSION(RunCase("VerifyCycle", &VerifyCycle));
        CHECK_EXPRESSION(RunCase("VerifyCycle", &VerifyCycle));
        CHECK_EXPRESSION(RunCase("VerifyRetainedResourcesDelayShutdown", &VerifyRetainedResourcesDelayShutdown));
        CHECK_EXPRESSION(RunCase("VerifyRuntimeShutdownFailurePropagation", &VerifyRuntimeShutdownFailurePropagation));
        CHECK_EXPRESSION(RunCase("VerifyBootstrapStubsPreserveState", &VerifyBootstrapStubsPreserveState));
        CHECK_EXPRESSION(RunCase("VerifyPimplAllocatorCrossesMemorySystemPhase", &VerifyPimplAllocatorCrossesMemorySystemPhase));
        CHECK_EXPRESSION(RunCase("VerifyBindingStorageCrossesMemorySystemPhase", &VerifyBindingStorageCrossesMemorySystemPhase));
        CHECK_EXPRESSION(RunCase("VerifyBindingStorageShutdownRetry", &VerifyBindingStorageShutdownRetry));
        CHECK_EXPRESSION(RunCase("VerifyHandleBasics", &VerifyHandleBasics));
        CHECK_EXPRESSION(RunCase("VerifyResultOrdering", &VerifyResultOrdering));
        CHECK_EXPRESSION(RunCase("VerifyDestructorFailsFastWithoutPoisoningParent", &VerifyDestructorFailsFastWithoutPoisoningParent));
        return true;
    }
}

int main(int argumentCount, char** arguments)
{
#ifdef _MSC_VER
    if (argumentCount == 2)
    {
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    }
#endif
    if (argumentCount == 2 && std::strcmp(arguments[1], "--pimpl-phase-probe") == 0)
    {
        {
            NorvesLib::Core::ScriptRuntime runtime;
            NorvesLib::Memory::Initialize();
            NorvesLib::Memory::Shutdown();
        }
        ExitProcess(EXIT_SUCCESS);
    }
    if (argumentCount == 2 && std::strcmp(arguments[1], "--binding-storage-probe") == 0)
    {
        NorvesLib::Core::ScriptBindingHandle firstGenerationHandle;
        NorvesLib::Core::ScriptBindingHandle staleFirstGenerationHandle;
        NorvesLib::Core::ScriptBindingHandle secondGenerationHandle;
        {
            NorvesLib::Core::ScriptRuntime runtime;
            NorvesLib::Memory::Initialize();
            {
                NorvesLib::Core::World world;
                world.Initialize();
                RequireChildPredicate(runtime.Initialize(world) == NorvesLib::Core::EScriptRuntimeResult::Success, 10);
                NorvesLib::Core::Entity* owner = world.SpawnEntity<NorvesLib::Core::Entity>();
                RequireChildPredicate(owner != nullptr, 11);
                auto* component = new NorvesLib::Core::Component::ScriptComponent();
                RequireChildPredicate(component != nullptr, 12);
                component->getScriptPath() = NorvesLib::Core::Container::String("Scripts/Test/ScriptComponentMover.as");
                component->getScriptClassName() = NorvesLib::Core::Container::String("ScriptComponentMover");
                RequireChildPredicate(owner->AddComponent(component), 13);
                RequireChildPredicate(runtime.BindComponent(*component, firstGenerationHandle) ==
                    NorvesLib::Core::EScriptRuntimeResult::Success, 14);
                RequireChildPredicate(runtime.TickComponent(firstGenerationHandle, 1.0f) ==
                    NorvesLib::Core::EScriptRuntimeResult::Success, 15);
                staleFirstGenerationHandle = firstGenerationHandle;
                RequireChildPredicate(runtime.UnbindComponent(firstGenerationHandle) ==
                    NorvesLib::Core::EScriptRuntimeResult::Success, 16);
                RequireChildPredicate(runtime.Shutdown() == NorvesLib::Core::EScriptRuntimeResult::Success, 17);
                world.Finalize();
            }
            {
                NorvesLib::Core::World world;
                world.Initialize();
                RequireChildPredicate(runtime.Initialize(world) == NorvesLib::Core::EScriptRuntimeResult::Success, 18);
                NorvesLib::Core::Entity* owner = world.SpawnEntity<NorvesLib::Core::Entity>();
                RequireChildPredicate(owner != nullptr, 19);
                auto* component = new NorvesLib::Core::Component::ScriptComponent();
                RequireChildPredicate(component != nullptr, 20);
                component->getScriptPath() = NorvesLib::Core::Container::String("Scripts/Test/ScriptComponentMover.as");
                component->getScriptClassName() = NorvesLib::Core::Container::String("ScriptComponentMover");
                RequireChildPredicate(owner->AddComponent(component), 21);
                RequireChildPredicate(runtime.BindComponent(*component, secondGenerationHandle) ==
                    NorvesLib::Core::EScriptRuntimeResult::Success, 22);
                RequireChildPredicate(secondGenerationHandle.SlotIndex == staleFirstGenerationHandle.SlotIndex, 23);
                RequireChildPredicate(secondGenerationHandle.Generation != staleFirstGenerationHandle.Generation, 24);
                RequireChildPredicate(runtime.TickComponent(staleFirstGenerationHandle, 1.0f) ==
                    NorvesLib::Core::EScriptRuntimeResult::InvalidHandle, 25);
                const NorvesLib::Math::Vector3 positionBeforeTick = owner->GetPosition();
                RequireChildPredicate(runtime.TickComponent(secondGenerationHandle, 1.0f) ==
                    NorvesLib::Core::EScriptRuntimeResult::Success, 26);
                RequireChildPredicate(owner->GetPosition().x == positionBeforeTick.x + 1.0f, 27);
                RequireChildPredicate(runtime.UnbindComponent(secondGenerationHandle) ==
                    NorvesLib::Core::EScriptRuntimeResult::Success, 28);
                RequireChildPredicate(runtime.Shutdown() == NorvesLib::Core::EScriptRuntimeResult::Success, 29);
                world.Finalize();
            }
            NorvesLib::Memory::Shutdown();
        }
        ExitProcess(EXIT_SUCCESS);
    }
    if (argumentCount == 2 && std::strcmp(arguments[1], "--binding-storage-shutdown-retry-probe") == 0)
    {
        {
            NorvesLib::Core::ScriptRuntime runtime;
            NorvesLib::Memory::Initialize();
            {
                NorvesLib::Core::World world;
                world.Initialize();
                RequireChildPredicate(runtime.Initialize(world) == NorvesLib::Core::EScriptRuntimeResult::Success, 30);
                NorvesLib::Core::Entity* owner = world.SpawnEntity<NorvesLib::Core::Entity>();
                RequireChildPredicate(owner != nullptr, 31);
                auto* component = new NorvesLib::Core::Component::ScriptComponent();
                RequireChildPredicate(component != nullptr, 32);
                component->getScriptPath() = NorvesLib::Core::Container::String("Scripts/Test/ScriptComponentMover.as");
                component->getScriptClassName() = NorvesLib::Core::Container::String("ScriptComponentMover");
                RequireChildPredicate(owner->AddComponent(component), 33);
                NorvesLib::Core::ScriptBindingHandle handle;
                RequireChildPredicate(runtime.BindComponent(*component, handle) ==
                    NorvesLib::Core::EScriptRuntimeResult::Success, 34);
                asIScriptEngine* engine = NorvesLib::Core::Scripting::GetActiveAngelScriptEngine();
                RequireChildPredicate(engine != nullptr, 35);
                RequireChildPredicate(engine->AddRef() > 0, 36);
                RequireChildPredicate(runtime.Shutdown() == NorvesLib::Core::EScriptRuntimeResult::ExecutionFailed, 37);
                RequireChildPredicate(runtime.GetDiagnostics().ActiveBindingCount == 0, 38);
                RequireChildPredicate(runtime.TickComponent(handle, 1.0f) ==
                    NorvesLib::Core::EScriptRuntimeResult::InvalidHandle, 39);
                RequireChildPredicate(engine->Release() > 0, 40);
                RequireChildPredicate(runtime.Shutdown() == NorvesLib::Core::EScriptRuntimeResult::Success, 41);
                world.Finalize();
            }
            NorvesLib::Memory::Shutdown();
        }
        ExitProcess(EXIT_SUCCESS);
    }
    if (argumentCount == 2 && std::strcmp(arguments[1], "--destructor-fail-fast-probe") == 0)
    {
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        {
            NorvesLib::Core::Scripting::AngelScriptEngineOwner owner;
            if (!owner.Initialize() || owner.GetEngine()->AddRef() <= 0)
            {
                ExitProcess(EXIT_FAILURE);
            }
        }
        ExitProcess(EXIT_SUCCESS);
    }

    std::cout << "AngelScriptBootstrapTest start" << std::endl;
    const bool bPassed = RunTest();
    std::cout << (bPassed ? "AngelScriptBootstrapTest passed\n" : "AngelScriptBootstrapTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
