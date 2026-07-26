#include "Component/ScriptComponent.h"
#include "Engine/NorvesEngine.h"
#include "Object/Entity.h"
#include "Object/ObjectHeap.h"
#include "Object/World.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    constexpr const char* ScriptPath = "Scripts/Test/ScriptComponentMover.as";
    constexpr const char* ScriptClassName = "ScriptComponentMover";
    constexpr const char* BeginPlayThrowPath = "Scripts/Test/ScriptComponentBeginPlayThrower.as";
    constexpr const char* BeginPlayThrowClassName = "ScriptComponentBeginPlayThrower";
    constexpr const char* MissingTickPath = "Scripts/Test/ScriptComponentMissingTick.as";
    constexpr const char* MissingTickClassName = "ScriptComponentMissingTick";
    constexpr const char* MissingClassPath = "Scripts/Test/ScriptComponentMissingClass.as";
    constexpr const char* CompileFailurePath = "Scripts/Test/ScriptComponentCompileFailure.as";
    constexpr const char* ThrowingTickPath = "Scripts/Test/ScriptComponentThrowingTick.as";
    constexpr const char* ThrowingTickClassName = "ScriptComponentThrowingTick";
    constexpr const char* PrivateConstructorPath = "Scripts/Test/ScriptComponentPrivateConstructor.as";
    constexpr const char* PrivateConstructorClassName = "ScriptComponentPrivateConstructor";
    constexpr const char* RetainedReferencePath = "Scripts/Test/ScriptComponentRetainedReference.as";
    constexpr const char* RetainedReferenceClassName = "ScriptComponentRetainedReference";

    Component::ScriptComponent* AddConfiguredComponent(Entity& owner)
    {
        auto* component = new Component::ScriptComponent();
        component->getScriptPath() = Container::String(ScriptPath);
        component->getScriptClassName() = Container::String(ScriptClassName);
        assert(owner.AddComponent(component));
        return component;
    }

    Component::ScriptComponent* AddConfiguredComponent(Entity& owner, const char* path, const char* className)
    {
        auto* component = new Component::ScriptComponent();
        component->getScriptPath() = Container::String(path);
        component->getScriptClassName() = Container::String(className);
        assert(owner.AddComponent(component));
        return component;
    }

    void ShutdownRuntimeAfterWorld(World& world)
    {
        world.Finalize();
        assert(GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 0);
        assert(GEngine.GetScriptRuntime().Shutdown() == EScriptRuntimeResult::Success);
    }

    void TestMovementAndDisabledStates()
    {
        World world;
        world.Initialize();
        assert(GEngine.GetScriptRuntime().Initialize(world) == EScriptRuntimeResult::Success);

        Entity* owner = world.SpawnEntity<Entity>();
        assert(owner != nullptr);
        Component::ScriptComponent* component = AddConfiguredComponent(*owner);
        assert(GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 1);

        world.Tick(1.0f);
        assert(owner->GetPosition().x == 1.0f);

        component->Disable();
        world.Tick(1.0f);
        assert(owner->GetPosition().x == 1.0f);

        component->Enable();
        owner->SetActive(false);
        world.Tick(1.0f);
        assert(owner->GetPosition().x == 1.0f);

        owner->SetActive(true);
        world.Tick(1.0f);
        assert(owner->GetPosition().x == 2.0f);

        ShutdownRuntimeAfterWorld(world);
    }

    void TestRemovalFinalizeAndSlotAba()
    {
        World world;
        world.Initialize();
        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        assert(runtime.Initialize(world) == EScriptRuntimeResult::Success);

        Entity* firstOwner = world.SpawnEntity<Entity>();
        assert(firstOwner != nullptr);
        Component::ScriptComponent* first = AddConfiguredComponent(*firstOwner);
        ScriptBindingHandle oldHandle;
        assert(runtime.BindComponent(*first, oldHandle) == EScriptRuntimeResult::InvalidArgument);
        assert(!oldHandle.IsValid());

        firstOwner->RemoveComponent(first);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 0);

        Entity* reboundOwner = world.SpawnEntity<Entity>();
        assert(reboundOwner != nullptr);
        Component::ScriptComponent* rebound = AddConfiguredComponent(*reboundOwner);
        ScriptBindingHandle staleHandle;
        assert(runtime.BindComponent(*rebound, staleHandle) == EScriptRuntimeResult::InvalidArgument);
        assert(!staleHandle.IsValid());

        reboundOwner->RemoveComponent(rebound);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 0);

        ScriptBindingHandle invalidHandle;
        invalidHandle.SlotIndex = 0;
        invalidHandle.Generation = 1;
        const ScriptBindingHandle staleCopy = invalidHandle;
        assert(runtime.UnbindComponent(invalidHandle) == EScriptRuntimeResult::Success);
        assert(!invalidHandle.IsValid());
        assert(runtime.TickComponent(staleCopy, 1.0f) == EScriptRuntimeResult::InvalidHandle);

        Entity* pendingOwner = world.SpawnEntity<Entity>();
        assert(pendingOwner != nullptr);
        AddConfiguredComponent(*pendingOwner);
        pendingOwner->MarkForDestroy();
        world.Tick(0.0f);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 0);

        Entity* finalizedOwner = world.SpawnEntity<Entity>();
        assert(finalizedOwner != nullptr);
        AddConfiguredComponent(*finalizedOwner);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 1);
        ShutdownRuntimeAfterWorld(world);
    }

    void TestHeapDeferredRemovalAndFaultIsolation()
    {
        World world;
        world.Initialize();
        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        assert(runtime.Initialize(world) == EScriptRuntimeResult::Success);

        Entity* owner = world.SpawnEntity<Entity>();
        assert(owner != nullptr);
        ObjectHeap heap;
        const ObjectHandle heapHandle = heap.Create<Component::ScriptComponent>();
        Component::ScriptComponent* heapComponent = heap.Resolve<Component::ScriptComponent>(heapHandle);
        assert(heapComponent != nullptr);
        heapComponent->getScriptPath() = Container::String(ScriptPath);
        heapComponent->getScriptClassName() = Container::String(ScriptClassName);
        assert(owner->AddComponent(heapComponent));
        owner->RemoveComponent(heapComponent);
        assert(heapComponent->HasFlag(OF_PendingDestroy));
        assert(heap.EnqueueDestroy(heapHandle));
        assert(heap.ProcessDestroyQueue() == 1);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 0);

        Entity* goodOwner = world.SpawnEntity<Entity>();
        Entity* badOwner = world.SpawnEntity<Entity>();
        assert(goodOwner != nullptr && badOwner != nullptr);
        AddConfiguredComponent(*goodOwner);
        AddConfiguredComponent(*badOwner, "Scripts/Test/NoSuchScript.as", "MissingClass");
        Entity* missingClassOwner = world.SpawnEntity<Entity>();
        Entity* missingTickOwner = world.SpawnEntity<Entity>();
        Entity* compileFailureOwner = world.SpawnEntity<Entity>();
        Entity* privateConstructorOwner = world.SpawnEntity<Entity>();
        Entity* throwingOwner = world.SpawnEntity<Entity>();
        assert(missingClassOwner != nullptr && missingTickOwner != nullptr && compileFailureOwner != nullptr);
        assert(privateConstructorOwner != nullptr && throwingOwner != nullptr);
        AddConfiguredComponent(*missingClassOwner, MissingClassPath, "MissingClass");
        AddConfiguredComponent(*missingTickOwner, MissingTickPath, MissingTickClassName);
        AddConfiguredComponent(*compileFailureOwner, CompileFailurePath, "ScriptComponentCompileFailure");
        AddConfiguredComponent(*privateConstructorOwner, PrivateConstructorPath, PrivateConstructorClassName);
        AddConfiguredComponent(*throwingOwner, ThrowingTickPath, ThrowingTickClassName);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 2);
        world.Tick(1.0f);
        assert(goodOwner->GetPosition().x == 1.0f);
        assert(throwingOwner->GetPosition().x == 10.0f);
        assert(runtime.GetDiagnostics().LastResult == EScriptRuntimeResult::ExecutionFailed);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 2);

        world.Tick(1.0f);
        assert(goodOwner->GetPosition().x == 2.0f);
        assert(throwingOwner->GetPosition().x == 10.0f);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 2);

        ShutdownRuntimeAfterWorld(world);
    }

    void TestBeginPlayRollbackAndShutdownBeforeFinalize()
    {
        World world;
        world.Initialize();
        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        assert(runtime.Initialize(world) == EScriptRuntimeResult::Success);

        Entity* failedOwner = world.SpawnEntity<Entity>();
        assert(failedOwner != nullptr);
        const NorvesLib::Math::Vector3 originalPosition = failedOwner->GetPosition();
        AddConfiguredComponent(*failedOwner, BeginPlayThrowPath, BeginPlayThrowClassName);
        assert(runtime.GetDiagnostics().LastResult == EScriptRuntimeResult::ExecutionFailed);
        assert(failedOwner->GetPosition() == originalPosition);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 0);

        Entity* liveOwner = world.SpawnEntity<Entity>();
        assert(liveOwner != nullptr);
        AddConfiguredComponent(*liveOwner);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 1);
        world.Tick(1.0f);
        assert(liveOwner->GetPosition().x == 1.0f);
        assert(runtime.Shutdown() == EScriptRuntimeResult::Success);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 0);

        world.Finalize();
    }

    void TestScriptRetainedStaleEntityRefFaultIsolation()
    {
        World world;
        world.Initialize();
        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        assert(runtime.Initialize(world) == EScriptRuntimeResult::Success);

        Entity* staleOwner = world.SpawnEntity<Entity>();
        Entity* healthyOwner = world.SpawnEntity<Entity>();
        assert(staleOwner != nullptr && healthyOwner != nullptr);
        Component::ScriptComponent* staleComponent = AddConfiguredComponent(
            *staleOwner,
            RetainedReferencePath,
            RetainedReferenceClassName);
        AddConfiguredComponent(*healthyOwner);
        staleOwner->MarkForDestroy();
        staleComponent->Tick(1.0f);
        world.Tick(1.0f);
        assert(healthyOwner->GetPosition().x == 1.0f);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 1);

        ShutdownRuntimeAfterWorld(world);
    }
}

int main()
{
    std::cout << "ScriptComponentTickTest start\n";
    TestMovementAndDisabledStates();
    TestRemovalFinalizeAndSlotAba();
    TestHeapDeferredRemovalAndFaultIsolation();
    TestBeginPlayRollbackAndShutdownBeforeFinalize();
    TestScriptRetainedStaleEntityRefFaultIsolation();
    std::cout << "ScriptComponentTickTest passed\n";
    return 0;
}
