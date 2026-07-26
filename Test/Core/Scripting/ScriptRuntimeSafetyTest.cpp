#include "Object/World.h"
#include "Component/ScriptComponent.h"
#include "Object/Entity.h"
#include "Scripting/ScriptRuntime.h"

#include "Scripting/AngelScriptEngineOwner.h"

#include <cstring>
#include <iostream>
#include <thread>

#include <angelscript.h>

using namespace NorvesLib::Core;

namespace
{
    bool Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::cout << "ScriptRuntimeSafetyTest failure: " << message << "\n";
        }
        return bCondition;
    }

    struct DiagnosticsSnapshot
    {
        uint64_t AllocationCount = 0;
        uint64_t FreeCount = 0;
        uint64_t LiveAllocationCount = 0;
        uint64_t GcStepCount = 0;
        uint64_t ReloadGeneration = 0;
        uint32_t ActiveBindingCount = 0;
    };

    DiagnosticsSnapshot CaptureDiagnostics(const ScriptRuntimeDiagnostics& diagnostics)
    {
        return {
            diagnostics.AllocationCount,
            diagnostics.FreeCount,
            diagnostics.LiveAllocationCount,
            diagnostics.GcStepCount,
            diagnostics.ReloadGeneration,
            diagnostics.ActiveBindingCount
        };
    }

    bool MatchesDiagnosticsExceptLastResult(
        const ScriptRuntimeDiagnostics& diagnostics,
        const DiagnosticsSnapshot& snapshot)
    {
        return diagnostics.AllocationCount == snapshot.AllocationCount &&
            diagnostics.FreeCount == snapshot.FreeCount &&
            diagnostics.LiveAllocationCount == snapshot.LiveAllocationCount &&
            diagnostics.GcStepCount == snapshot.GcStepCount &&
            diagnostics.ReloadGeneration == snapshot.ReloadGeneration &&
            diagnostics.ActiveBindingCount == snapshot.ActiveBindingCount;
    }

    bool HasReleasedAllocations(const ScriptRuntimeDiagnostics& diagnostics)
    {
        return diagnostics.AllocationCount == diagnostics.FreeCount && diagnostics.LiveAllocationCount == 0;
    }

    bool VerifyFreshRuntimeLifecycle(World& world, const char* context)
    {
        ScriptRuntime runtime;
        bool bPassed = Check(
            runtime.Initialize(world) == EScriptRuntimeResult::Success,
            context) && Check(
            Scripting::GetActiveAngelScriptEngine() != nullptr,
            "fresh runtime did not own the active AngelScript engine");
        bPassed = Check(
            runtime.Shutdown() == EScriptRuntimeResult::Success,
            "fresh runtime shutdown failed") && bPassed;
        bPassed = Check(
            HasReleasedAllocations(runtime.GetDiagnostics()),
            "fresh runtime shutdown did not balance allocations") && bPassed;
        bPassed = Check(
            Scripting::GetActiveAngelScriptEngine() == nullptr,
            "fresh runtime shutdown left an active AngelScript engine") && bPassed;
        return bPassed;
    }

    bool VerifyFailedInitializeReleasesOwnerForOtherThread()
    {
        World world;
        world.Initialize();

        Scripting::AngelScriptEngineOwner blocker;
        ScriptRuntime runtime;
        bool bPassed = Check(blocker.Initialize(), "failed-initialize blocker setup failed");
        bPassed = Check(
            runtime.Initialize(world) == EScriptRuntimeResult::LoadFailed,
            "runtime initialization did not fail while the blocker owned the global hook") && bPassed;
        bPassed = Check(!runtime.IsInitialized(), "failed initialization left runtime initialized") && bPassed;
        bPassed = Check(
            blocker.Shutdown(),
            "failed-initialize blocker shutdown failed") && bPassed;

        EScriptRuntimeResult workerInitializeResult = EScriptRuntimeResult::ExecutionFailed;
        EScriptRuntimeResult workerShutdownResult = EScriptRuntimeResult::ExecutionFailed;
        std::thread worker([&runtime, &world, &workerInitializeResult, &workerShutdownResult]()
        {
            workerInitializeResult = runtime.Initialize(world);
            workerShutdownResult = runtime.Shutdown();
        });
        worker.join();

        bPassed = Check(
            workerInitializeResult == EScriptRuntimeResult::Success,
            "clean failed initialization retained the owner thread") && bPassed;
        bPassed = Check(
            workerShutdownResult == EScriptRuntimeResult::Success,
            "other-thread retry shutdown failed") && bPassed;
        bPassed = Check(!runtime.IsInitialized(), "other-thread retry shutdown left runtime initialized") && bPassed;
        bPassed = Check(
            HasReleasedAllocations(runtime.GetDiagnostics()),
            "other-thread retry shutdown did not balance allocations") && bPassed;
        bPassed = Check(
            Scripting::GetActiveAngelScriptEngine() == nullptr,
            "other-thread retry shutdown left an active AngelScript engine") && bPassed;

        world.Finalize();
        return bPassed;
    }

    bool VerifyOffThreadMutatingApiMatrix()
    {
        World world;
        world.Initialize();

        ScriptRuntime runtime;
        bool bPassed = Check(
            runtime.Initialize(world) == EScriptRuntimeResult::Success,
            "matrix runtime initialization failed");

        Entity* owner = bPassed ? world.SpawnEntity<Entity>() : nullptr;
        bPassed = Check(owner != nullptr, "matrix entity creation failed") && bPassed;

        Component::ScriptComponent* component = nullptr;
        ScriptBindingHandle activeHandle;
        if (bPassed)
        {
            component = new Component::ScriptComponent();
            component->getScriptPath() = Container::String("Scripts/Test/ScriptComponentMover.as");
            component->getScriptClassName() = Container::String("ScriptComponentMover");
            bPassed = Check(owner->AddComponent(component), "matrix component attachment failed") && bPassed;
            bPassed = Check(
                runtime.BindComponent(*component, activeHandle) == EScriptRuntimeResult::Success,
                "matrix healthy binding creation failed") && bPassed;
        }

        Component::ScriptComponent rejectedComponent;
        ScriptBindingHandle rejectedOutput;
        rejectedOutput.SlotIndex = 17;
        rejectedOutput.Generation = 29;
        ScriptBindingHandle unbindInput = activeHandle;
        const ScriptBindingHandle tickInput = activeHandle;
        const ScriptBindingHandle rejectedOutputBefore = rejectedOutput;
        const ScriptBindingHandle unbindInputBefore = unbindInput;
        const ScriptBindingHandle tickInputBefore = tickInput;
        const NorvesLib::Math::Vector3 positionBefore = owner != nullptr
            ? owner->GetPosition()
            : NorvesLib::Math::Vector3{};
        const DiagnosticsSnapshot diagnosticsBefore = CaptureDiagnostics(runtime.GetDiagnostics());
        const bool bInitializedBefore = runtime.IsInitialized();

        EScriptRuntimeResult initializeResult = EScriptRuntimeResult::ExecutionFailed;
        EScriptRuntimeResult beginResult = EScriptRuntimeResult::ExecutionFailed;
        EScriptRuntimeResult endResult = EScriptRuntimeResult::ExecutionFailed;
        EScriptRuntimeResult bindResult = EScriptRuntimeResult::ExecutionFailed;
        EScriptRuntimeResult unbindResult = EScriptRuntimeResult::ExecutionFailed;
        EScriptRuntimeResult tickResult = EScriptRuntimeResult::ExecutionFailed;
        EScriptRuntimeResult shutdownResult = EScriptRuntimeResult::ExecutionFailed;
        std::thread worker([&]()
        {
            initializeResult = runtime.Initialize(world);
            beginResult = runtime.BeginFrameMaintenance(0.016f);
            endResult = runtime.EndFrameMaintenance();
            bindResult = runtime.BindComponent(rejectedComponent, rejectedOutput);
            unbindResult = runtime.UnbindComponent(unbindInput);
            tickResult = runtime.TickComponent(tickInput, 1.0f);
            shutdownResult = runtime.Shutdown();
        });
        worker.join();

        bPassed = Check(initializeResult == EScriptRuntimeResult::WrongThread, "worker Initialize did not return WrongThread") && bPassed;
        bPassed = Check(beginResult == EScriptRuntimeResult::WrongThread, "worker BeginFrameMaintenance did not return WrongThread") && bPassed;
        bPassed = Check(endResult == EScriptRuntimeResult::WrongThread, "worker EndFrameMaintenance did not return WrongThread") && bPassed;
        bPassed = Check(bindResult == EScriptRuntimeResult::WrongThread, "worker BindComponent did not return WrongThread") && bPassed;
        bPassed = Check(unbindResult == EScriptRuntimeResult::WrongThread, "worker UnbindComponent did not return WrongThread") && bPassed;
        bPassed = Check(tickResult == EScriptRuntimeResult::WrongThread, "worker TickComponent did not return WrongThread") && bPassed;
        bPassed = Check(shutdownResult == EScriptRuntimeResult::WrongThread, "worker Shutdown did not return WrongThread") && bPassed;
        bPassed = Check(runtime.IsInitialized() == bInitializedBefore, "worker matrix changed initialized state") && bPassed;
        bPassed = Check(
            std::memcmp(&rejectedOutput, &rejectedOutputBefore, sizeof(rejectedOutput)) == 0,
            "worker BindComponent changed output handle bytes") && bPassed;
        bPassed = Check(
            std::memcmp(&unbindInput, &unbindInputBefore, sizeof(unbindInput)) == 0,
            "worker UnbindComponent changed input handle bytes") && bPassed;
        bPassed = Check(
            std::memcmp(&tickInput, &tickInputBefore, sizeof(tickInput)) == 0,
            "worker TickComponent changed input handle bytes") && bPassed;
        bPassed = Check(
            owner != nullptr && owner->GetPosition() == positionBefore,
            "worker matrix changed entity position") && bPassed;
        bPassed = Check(
            MatchesDiagnosticsExceptLastResult(runtime.GetDiagnostics(), diagnosticsBefore),
            "worker matrix changed diagnostics other than LastResult") && bPassed;
        bPassed = Check(
            runtime.GetDiagnostics().LastResult == EScriptRuntimeResult::WrongThread,
            "worker matrix did not record WrongThread as the last result") && bPassed;

        if (bPassed)
        {
            bPassed = Check(
                runtime.TickComponent(activeHandle, 1.0f) == EScriptRuntimeResult::Success,
                "owner TickComponent failed after worker matrix") && bPassed;
            bPassed = Check(
                runtime.EndFrameMaintenance() == EScriptRuntimeResult::Success,
                "owner EndFrameMaintenance failed after worker matrix") && bPassed;
            bPassed = Check(
                runtime.Shutdown() == EScriptRuntimeResult::Success,
                "owner Shutdown failed after worker matrix") && bPassed;
            bPassed = Check(
                HasReleasedAllocations(runtime.GetDiagnostics()),
                "owner Shutdown after worker matrix did not balance allocations") && bPassed;
        }
        else if (runtime.IsInitialized())
        {
            runtime.Shutdown();
        }

        world.Finalize();
        return bPassed;
    }

    bool VerifyDestructorPrivateCleanup()
    {
        bool bPassed = true;

        {
            ScriptRuntime runtime;
        }
        bPassed = Check(
            Scripting::GetActiveAngelScriptEngine() == nullptr,
            "owner-uncaptured runtime destructor left an active AngelScript engine") && bPassed;

        {
            World world;
            world.Initialize();
            {
                ScriptRuntime runtime;
                bPassed = Check(
                    runtime.Initialize(world) == EScriptRuntimeResult::Success,
                    "scope-exit runtime initialization failed") && bPassed;
                bPassed = Check(
                    Scripting::GetActiveAngelScriptEngine() != nullptr,
                    "scope-exit runtime did not own the active AngelScript engine") && bPassed;
            }
            bPassed = Check(
                Scripting::GetActiveAngelScriptEngine() == nullptr,
                "owner-thread destructor left an active AngelScript engine") && bPassed;
            bPassed = VerifyFreshRuntimeLifecycle(world, "fresh runtime initialization after owner-thread destructor failed") && bPassed;
            world.Finalize();
        }

        {
            World world;
            world.Initialize();
            ScriptRuntime* runtime = new ScriptRuntime();
            bPassed = Check(
                runtime->Initialize(world) == EScriptRuntimeResult::Success,
                "cross-thread destructor runtime initialization failed") && bPassed;
            std::thread worker([runtime]()
            {
                delete runtime;
            });
            worker.join();
            bPassed = Check(
                Scripting::GetActiveAngelScriptEngine() == nullptr,
                "cross-thread destructor was rejected by the public owner gate") && bPassed;
            bPassed = VerifyFreshRuntimeLifecycle(world, "fresh runtime initialization after cross-thread destructor failed") && bPassed;
            world.Finalize();
        }

        return bPassed;
    }

    bool VerifyEndFrameCollectsUnreachableSelfCycle()
    {
        constexpr const char* moduleName = "RuntimeGcCycleProbe";
        constexpr const char* source =
            "class SelfCycle\n"
            "{\n"
            "    SelfCycle@ Self;\n"
            "}\n"
            "void CreateCycle()\n"
            "{\n"
            "    SelfCycle@ cycle = SelfCycle();\n"
            "    @cycle.Self = cycle;\n"
            "}\n";

        World world;
        world.Initialize();

        ScriptRuntime runtime;
        bool bPassed = Check(
            runtime.Initialize(world) == EScriptRuntimeResult::Success,
            "runtime initialization failed");
        asIScriptEngine* engine = bPassed ? Scripting::GetActiveAngelScriptEngine() : nullptr;
        bPassed = Check(engine != nullptr, "active AngelScript engine is unavailable") && bPassed;

        asIScriptModule* module = nullptr;
        if (bPassed)
        {
            module = engine->GetModule(moduleName, asGM_ALWAYS_CREATE);
            bPassed = Check(module != nullptr, "self-cycle module creation failed") && bPassed;
        }
        if (bPassed)
        {
            bPassed = Check(
                module->AddScriptSection(moduleName, source) >= 0,
                "self-cycle script section compilation setup failed") && bPassed;
            bPassed = Check(module->Build() >= 0, "self-cycle script module build failed") && bPassed;
        }

        asIScriptContext* context = nullptr;
        if (bPassed)
        {
            asIScriptFunction* createCycle = module->GetFunctionByDecl("void CreateCycle()");
            context = engine->CreateContext();
            bPassed = Check(createCycle != nullptr, "self-cycle factory function is unavailable") && bPassed;
            bPassed = Check(context != nullptr, "self-cycle execution context creation failed") && bPassed;
            if (bPassed)
            {
                bPassed = Check(context->Prepare(createCycle) >= 0, "self-cycle factory preparation failed") && bPassed;
                bPassed = Check(
                    context->Execute() == asEXECUTION_FINISHED,
                    "self-cycle factory execution failed") && bPassed;
            }
        }
        if (context != nullptr)
        {
            bPassed = Check(context->Release() == 0, "self-cycle execution context release failed") && bPassed;
            context = nullptr;
        }

        asUINT initialGcSize = 0;
        asUINT initialDestroyed = 0;
        if (bPassed)
        {
            engine->GetGCStatistics(&initialGcSize, &initialDestroyed);
            bPassed = Check(initialGcSize > 0, "self-cycle was not registered with AngelScript GC") && bPassed;
        }

        bool bCycleReclaimed = false;
        if (bPassed)
        {
            uint64_t expectedGcStepCount = runtime.GetDiagnostics().GcStepCount;
            for (uint32_t index = 0; index < 1024; ++index)
            {
                bPassed = Check(
                    runtime.EndFrameMaintenance() == EScriptRuntimeResult::Success,
                    "EndFrameMaintenance failed while collecting self-cycle") && bPassed;
                ++expectedGcStepCount;
                bPassed = Check(
                    runtime.GetDiagnostics().GcStepCount == expectedGcStepCount,
                    "EndFrameMaintenance did not record exactly one GC step") && bPassed;

                asUINT currentGcSize = 0;
                asUINT totalDestroyed = 0;
                engine->GetGCStatistics(&currentGcSize, &totalDestroyed);
                bCycleReclaimed = totalDestroyed > initialDestroyed || currentGcSize < initialGcSize;
                if (bCycleReclaimed || !bPassed)
                {
                    break;
                }
            }
            if (!bCycleReclaimed)
            {
                std::cout << "ScriptRuntimeSafetyTest behavior mismatch: self-cycle was not reclaimed within 1024 GC steps\n";
                bPassed = false;
            }
        }

        if (module != nullptr)
        {
            bPassed = Check(engine->DiscardModule(moduleName) >= 0, "self-cycle module discard failed") && bPassed;
        }
        bPassed = Check(runtime.Shutdown() == EScriptRuntimeResult::Success, "runtime shutdown failed") && bPassed;
        world.Finalize();
        return bPassed;
    }

}

int main()
{
    std::cout << "ScriptRuntimeSafetyTest start\n";
    const bool bPassed = VerifyEndFrameCollectsUnreachableSelfCycle() &&
        VerifyFailedInitializeReleasesOwnerForOtherThread() &&
        VerifyOffThreadMutatingApiMatrix() &&
        VerifyDestructorPrivateCleanup();
    std::cout << (bPassed ? "ScriptRuntimeSafetyTest passed\n" : "ScriptRuntimeSafetyTest failed\n");
    return bPassed ? 0 : 1;
}
