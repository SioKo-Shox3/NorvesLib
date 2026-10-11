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
        staleComponent->TickScript(1.0f);
        world.Tick(1.0f);
        assert(healthyOwner->GetPosition().x == 1.0f);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 1);

        ShutdownRuntimeAfterWorld(world);
    }

    // Tick の最中に破棄を呼ぶ・確かめる Component（型の登録が無いので旧の Tick で呼ばれる）
    struct DestroyDuringTickState
    {
        World* TargetWorld = nullptr;
        Entity* DestroyedOwner = nullptr;
        Entity* ComponentOwner = nullptr;
        Component::ScriptComponent* Scripts[3] = {};
        Component::ScriptComponent* RemovedScript = nullptr;
        Component::ScriptComponent* SurvivorScript = nullptr;
        Entity* Spawned = nullptr;
        bool bDestroyed = false;
        bool bChecked = false;
    };

    // 登録した ScriptComponent の Tick の呼び先を包み、対象ごとの呼び出し回数を数える（本体はそのまま呼ぶ）
    struct ScriptTickCounter
    {
        static constexpr size_t MaxTargets = 8;
        Component::ComponentTickFn OriginalRun = nullptr;
        const Component::Component* Targets[MaxTargets] = {};
        uint32_t Counts[MaxTargets] = {};
        size_t TargetCount = 0;

        uint32_t CountOf(const Component::Component* target) const
        {
            for (size_t i = 0; i < TargetCount; ++i)
            {
                if (Targets[i] == target)
                {
                    return Counts[i];
                }
            }
            return 0;
        }

        void Note(const Component::Component* target)
        {
            for (size_t i = 0; i < TargetCount; ++i)
            {
                if (Targets[i] == target)
                {
                    ++Counts[i];
                    return;
                }
            }
            assert(TargetCount < MaxTargets);
            Targets[TargetCount] = target;
            Counts[TargetCount] = 1;
            ++TargetCount;
        }
    };

    ScriptTickCounter* GScriptTickCounter = nullptr;

    void CountingScriptTick(Component::Component& component, float deltaTime)
    {
        GScriptTickCounter->Note(&component);
        GScriptTickCounter->OriginalRun(component, deltaTime);
    }

    // ScriptRuntime が World に登録した呼び先を、計数の包みに置き換える（反映は次の Tick の収集から）
    void InstallScriptTickCounter(World& world, ScriptTickCounter& counter)
    {
        const Component::ComponentTickRegistration* registered =
            world.FindComponentTick(Component::ScriptComponent::StaticClass());
        assert(registered != nullptr && registered->Run != nullptr);
        Component::ComponentTickRegistration counting = *registered;
        counter.OriginalRun = registered->Run;
        counting.Run = &CountingScriptTick;
        GScriptTickCounter = &counter;
        world.RegisterComponentTick(Component::ScriptComponent::StaticClass(), counting);
    }

    // クラスの中では基底の注入名 Component が名前空間の別名を隠すので、外で別名を付ける
    using ProbeBase = Component::Component;

    class DestroyDuringTickProbe final : public ProbeBase
    {
        REFLECTION_CLASS(DestroyDuringTickProbe, ProbeBase)

    public:
        DestroyDuringTickState* State = nullptr;
        bool bDestroyPhase = false;

        // 型に登録する本体（World の順序付きの系が呼ぶ）
        static void RunTick(ProbeBase& component, float deltaTime)
        {
            static_cast<DestroyDuringTickProbe&>(component).OnTick(deltaTime);
        }

        void OnTick(float deltaTime)
        {
            if (bDestroyPhase)
            {
                RunDestroy();
            }
            else
            {
                RunCheck(deltaTime);
            }
        }

    private:
        void RunDestroy()
        {
            if (State->bDestroyed)
            {
                return;
            }
            State->bDestroyed = true;
            assert(State->TargetWorld->RemoveEntity(State->DestroyedOwner));
            State->ComponentOwner->RemoveComponent(State->RemovedScript);
            // 破棄予約は呼んだ時点で立ち、Object の解放は同期点まで待つ
            assert(State->DestroyedOwner->IsPendingDestroy());
            assert(State->RemovedScript->IsPendingDestroy());
            assert(State->DestroyedOwner->GetWorld() == State->TargetWorld);
            assert(State->RemovedScript->GetOwner() == State->ComponentOwner);
            // 生成は Object が即時、行は同期点で作られる
            State->Spawned = State->TargetWorld->SpawnEntity<Entity>();
            assert(State->Spawned != nullptr);
            assert(State->Spawned->GetEcsEntity().IsNull());
        }

        void RunCheck(float deltaTime)
        {
            if (!State->bDestroyed || State->bChecked)
            {
                return;
            }
            State->bChecked = true;
            ScriptRuntime& runtime = GEngine.GetScriptRuntime();
            // 計数が効いていること: 破棄していない対象は、このフレームで登録した呼び先がちょうど 1 回呼ばれた
            assert(GScriptTickCounter->CountOf(State->SurvivorScript) == 1);
            // 破棄の後に並ぶ Tick は呼ばれていない。直接の呼び出しより前に、対象ごとの回数で確かめる
            for (auto* script : State->Scripts)
            {
                assert(GScriptTickCounter->CountOf(script) == 0);
            }
            for (auto* script : State->Scripts)
            {
                assert(script->GetOwner()->GetPosition().x == 0.0f);
                // スクリプトからの取得は失敗する（Mover の GetPosition が例外になり、位置は動かない）
                script->TickScript(deltaTime);
                assert(runtime.GetDiagnostics().LastResult == EScriptRuntimeResult::ExecutionFailed);
                assert(script->GetOwner()->GetPosition().x == 0.0f);
            }
        }
    };
    IMPLEMENT_CLASS(DestroyDuringTickProbe, ProbeBase)

    void TestDestroyDuringTickReservesUntilSyncPoint()
    {
        World world;
        world.Initialize();
        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        assert(runtime.Initialize(world) == EScriptRuntimeResult::Success);

        Entity* probeOwner = world.SpawnEntity<Entity>();
        Entity* destroyedOwner = world.SpawnEntity<Entity>();
        Entity* destroyedChild = world.SpawnEntity<Entity>(destroyedOwner);
        Entity* componentOwner = world.SpawnEntity<Entity>();
        Entity* survivor = world.SpawnEntity<Entity>();
        assert(probeOwner != nullptr && destroyedOwner != nullptr && destroyedChild != nullptr);
        assert(componentOwner != nullptr && survivor != nullptr);

        DestroyDuringTickState state;
        state.TargetWorld = &world;
        state.DestroyedOwner = destroyedOwner;
        state.ComponentOwner = componentOwner;
        state.Scripts[0] = AddConfiguredComponent(*destroyedOwner);
        state.Scripts[1] = AddConfiguredComponent(*destroyedChild);
        state.Scripts[2] = AddConfiguredComponent(*componentOwner);
        state.RemovedScript = state.Scripts[2];
        state.SurvivorScript = AddConfiguredComponent(*survivor);
        assert(runtime.GetDiagnostics().ActiveBindingCount == 4);
        ScriptTickCounter counter;
        InstallScriptTickCounter(world, counter);
        Component::ComponentTickRegistration probeTick;
        probeTick.Name = "Test.DestroyDuringTickProbe";
        probeTick.Run = &DestroyDuringTickProbe::RunTick;
        world.RegisterComponentTick(DestroyDuringTickProbe::StaticClass(), probeTick);

        // スクリプトの Tick（優先度 0）より前で破棄し、後で確かめる
        auto* destroyProbe = new DestroyDuringTickProbe();
        destroyProbe->State = &state;
        destroyProbe->bDestroyPhase = true;
        destroyProbe->SetTickPriority(-100);
        assert(probeOwner->AddComponent(destroyProbe));
        auto* checkProbe = new DestroyDuringTickProbe();
        checkProbe->State = &state;
        checkProbe->SetTickPriority(100);
        assert(probeOwner->AddComponent(checkProbe));

        const Ecs::EcsEntity destroyedRow = destroyedOwner->GetEcsEntity();
        const Ecs::EcsEntity childRow = destroyedChild->GetEcsEntity();
        assert(world.GetEcsWorld().IsAlive(destroyedRow) && world.GetEcsWorld().IsAlive(childRow));

        world.Tick(1.0f);
        assert(state.bDestroyed && state.bChecked);
        assert(survivor->GetPosition().x == 1.0f);
        // 解放と行の除去・行の作成は、Tick の後の同期点で行われた
        assert(runtime.GetDiagnostics().ActiveBindingCount == 1);
        assert(componentOwner->GetComponent<Component::ScriptComponent>() == nullptr);
        assert(!world.GetEcsWorld().IsAlive(destroyedRow) && !world.GetEcsWorld().IsAlive(childRow));
        assert(!state.Spawned->GetEcsEntity().IsNull() && world.GetEcsWorld().IsAlive(state.Spawned->GetEcsEntity()));

        world.Tick(1.0f);
        assert(survivor->GetPosition().x == 2.0f);
        assert(componentOwner->GetPosition().x == 0.0f);
        assert(counter.CountOf(state.SurvivorScript) == 2);

        ShutdownRuntimeAfterWorld(world);
        GScriptTickCounter = nullptr;
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
    TestDestroyDuringTickReservesUntilSyncPoint();
    std::cout << "ScriptComponentTickTest passed\n";
    return 0;
}
