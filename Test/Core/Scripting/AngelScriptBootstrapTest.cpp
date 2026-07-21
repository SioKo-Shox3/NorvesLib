#include "Scripting/ScriptRuntime.h"

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

namespace NorvesLib::Core::Component
{
    class ScriptComponent
    {
    };
} // namespace NorvesLib::Core::Component

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

    void ProbeFree(void* memory)
    {
        NorvesLib::Memory::Free(memory);
    }

    void* ProbeAllocate(size_t size)
    {
        return NorvesLib::Memory::Malloc(size);
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
        CHECK_EXPRESSION(runtime.GetDiagnostics().GcStepCount == 0);

        CHECK_EXPRESSION(runtime.BindComponent(component, handle) ==
            NorvesLib::Core::EScriptRuntimeResult::BindFailed);
        CHECK_EXPRESSION(handle.SlotIndex == 7);
        CHECK_EXPRESSION(handle.Generation == 11);
        CHECK_EXPRESSION(runtime.UnbindComponent(handle) == NorvesLib::Core::EScriptRuntimeResult::InvalidHandle);
        CHECK_EXPRESSION(handle.SlotIndex == 7);
        CHECK_EXPRESSION(handle.Generation == 11);
        CHECK_EXPRESSION(runtime.TickComponent(handle, 0.016f) == NorvesLib::Core::EScriptRuntimeResult::BindFailed);
        CHECK_EXPRESSION(handle.SlotIndex == 7);
        CHECK_EXPRESSION(handle.Generation == 11);
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
        CHECK_EXPRESSION(RunCase("VerifyCycle", &VerifyCycle));
        CHECK_EXPRESSION(RunCase("VerifyCycle", &VerifyCycle));
        CHECK_EXPRESSION(RunCase("VerifyRetainedResourcesDelayShutdown", &VerifyRetainedResourcesDelayShutdown));
        CHECK_EXPRESSION(RunCase("VerifyRuntimeShutdownFailurePropagation", &VerifyRuntimeShutdownFailurePropagation));
        CHECK_EXPRESSION(RunCase("VerifyBootstrapStubsPreserveState", &VerifyBootstrapStubsPreserveState));
        CHECK_EXPRESSION(RunCase("VerifyPimplAllocatorCrossesMemorySystemPhase", &VerifyPimplAllocatorCrossesMemorySystemPhase));
        CHECK_EXPRESSION(RunCase("VerifyHandleBasics", &VerifyHandleBasics));
        CHECK_EXPRESSION(RunCase("VerifyResultOrdering", &VerifyResultOrdering));
        CHECK_EXPRESSION(RunCase("VerifyDestructorFailsFastWithoutPoisoningParent", &VerifyDestructorFailsFastWithoutPoisoningParent));
        return true;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount == 2 && std::strcmp(arguments[1], "--pimpl-phase-probe") == 0)
    {
        {
            NorvesLib::Core::ScriptRuntime runtime;
            NorvesLib::Memory::Initialize();
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
