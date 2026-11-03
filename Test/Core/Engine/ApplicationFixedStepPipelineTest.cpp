#include "Application/ApplicationHandlerBase.h"
#include "Component/Component.h"
#include "Component/PointLightComponent.h"
#include "Ecs/EcsScheduler.h"
#include "Engine/ApplicationProcessor.h"
#include "Engine/Engine.h"
#include "Engine/FixedStepScheduler.h"
#include "Engine/NorvesEngine.h"
#include "Input/InputButtonState.h"
#include "Module/IModule.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Rendering/SceneView.h"

#include <Windows.h>
#include <shellapi.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <thread>

#pragma execution_character_set("utf-8")
#pragma comment(lib, "Shell32.lib")

namespace NorvesLib::Core::Engine
{
    struct ApplicationFixedStepTestAccess
    {
        static bool ResetRun(ApplicationProcessor& processor, uint32_t rate = 60)
        {
            processor.m_FixedStepScheduler->EndRun();
            if (!processor.m_FixedStepScheduler->SetRate(rate))
                return false;
            processor.m_FixedStepScheduler->BeginRun();
            return true;
        }

        static void EndRun(ApplicationProcessor& processor)
        {
            processor.m_FixedStepScheduler->EndRun();
        }

        static FixedStepAdvanceResult Advance(
            ApplicationProcessor& processor,
            int64_t rawDeltaNanoseconds,
            bool bAdvanceSimulation)
        {
            return processor.AdvanceFixedSimulation(rawDeltaNanoseconds, bAdvanceSimulation);
        }

        static FixedStepAdvanceResult Simulate(
            ApplicationProcessor& processor, int64_t rawDeltaNanoseconds,
            bool bAdvanceSimulation, Application::IApplicationHandler* handler)
        {
            return processor.TickSimulation(rawDeltaNanoseconds,
                processor.ClampVariableDeltaTime(rawDeltaNanoseconds), bAdvanceSimulation, handler);
        }

        static bool SetFixedFrameClock(ApplicationProcessor& processor, const char* step)
        {
            int64_t numeratorNanoseconds = 0;
            int64_t denominator = 0;
            if (!TryParseFixedFrameDelta(step, std::strlen(step), numeratorNanoseconds, denominator) ||
                !processor.m_FixedFrameClock.SetStep(numeratorNanoseconds, denominator))
            {
                return false;
            }
            processor.m_FixedFrameClock.Reset();
            return true;
        }

        static int64_t AcquireFrameDeltaNanoseconds(ApplicationProcessor& processor)
        {
            return processor.AcquireFrameDeltaNanoseconds();
        }

        static const FixedFrameClock& GetFixedFrameClock(const ApplicationProcessor& processor)
        {
            return processor.m_FixedFrameClock;
        }

        static float ClampVariableDeltaTime(ApplicationProcessor& processor, int64_t rawDeltaNanoseconds)
        {
            return processor.ClampVariableDeltaTime(rawDeltaNanoseconds);
        }
    };
} // namespace NorvesLib::Core::Engine

namespace
{
    using namespace NorvesLib::Core;

    constexpr uint32_t kCaseCount = 23;
    constexpr uint32_t kFixedFrame24Case = 21;
    constexpr uint32_t kFixedFrame24Count = 48;

    struct FixedFrameRecord
    {
        uint64_t ExecutedSteps;
        uint64_t RemainderScaledUnits;
        int64_t ElapsedNanoseconds;
        int64_t PhysicsDeltaNanoseconds;
        float DeltaSeconds;
        double TotalSeconds;
    };

    constexpr DWORD kFixedFrameRecordSize = sizeof(uint64_t) * 2 + sizeof(int64_t) * 2 +
        sizeof(float) + sizeof(double);
    constexpr DWORD kFixedFrameRecordsSize = kFixedFrameRecordSize * kFixedFrame24Count;

    struct DynamicFixture
    {
        Container::VariableArray<Container::String> Events;
        Container::VariableArray<float> ModuleAPreObservedChildWorldX;
        Container::VariableArray<float> ModuleAPostObservedChildWorldX;
        Entity* DestroyEntity = nullptr;
        Entity* ObservedChild = nullptr;
        EntityHandle DestroyHandle;
        bool bPostObservedPending = false;
        float LastModuleFixedDelta = 0;
        uint32_t ParentFixedCount = 0;
        uint32_t ChildObservedLatestCount = 0;
    };

    DynamicFixture* GFixture = nullptr;

    // クラスの中では基底の注入名 Component が名前空間の別名を隠すので、外で別名を付ける
    using ProbeBase = Component::Component;

    // 固定ステップの段 FixedTick で呼ぶ試験の本体。段のシステム（RunFixedProbes）が Entity の深さ優先の順に呼ぶ
    class IFixedProbe
    {
    public:
        virtual ~IFixedProbe() = default;
        virtual void OnFixed(float fixedDeltaTime) = 0;
    };

    void CollectFixedProbes(Entity& entity, Container::VariableArray<IFixedProbe*>& out)
    {
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            return;
        }
        if (entity.IsTickEnabled())
        {
            for (ProbeBase* component : entity.GetComponents())
            {
                if (component->IsActive() && component->IsTickEnabled() && !component->IsPendingDestroy())
                {
                    if (auto* probe = dynamic_cast<IFixedProbe*>(component))
                    {
                        out.push_back(probe);
                    }
                }
            }
        }
        for (Entity* child : entity.GetChildEntities())
        {
            CollectFixedProbes(*child, out);
        }
    }

    void RunFixedProbes(void* user, const Ecs::EcsSystemContext& context)
    {
        auto& world = *static_cast<World*>(user);
        Container::VariableArray<IFixedProbe*> probes;
        for (Entity* root : world.GetRootEntities())
        {
            CollectFixedProbes(*root, probes);
        }
        for (IFixedProbe* probe : probes)
        {
            probe->OnFixed(context.DeltaTime);
        }
    }

    // 型に Tick の本体を登録する（World の順序付きの系が呼ぶ）
    template <typename T>
    void RegisterProbeTick(World& world, const char* name, Engine::TimeChannel channel = Engine::TimeChannel::World)
    {
        Component::ComponentTickRegistration registration;
        registration.Name = name;
        registration.Run = [](ProbeBase& component, float deltaTime) { static_cast<T&>(component).OnTick(deltaTime); };
        registration.TimeChannel = channel;
        world.RegisterComponentTick(T::StaticClass(), registration);
    }

    class FixedProbeComponent final : public ProbeBase, public IFixedProbe
    {
        REFLECTION_CLASS(FixedProbeComponent, ProbeBase)

    public:
        void OnTick(float deltaTime)
        {
            ++VariableTickCount;
            LastVariableDeltaTime = deltaTime;
        }

        void OnFixed(float fixedDeltaTime) override
        {
            ++FixedTickCount;
            LastFixedDeltaTime = fixedDeltaTime;
            if (!GFixture)
            {
                return;
            }

            GFixture->Events.push_back(Container::String(Name));
            if (bMoveOwner)
            {
                GetOwner()->SetLocalPosition(static_cast<float>(++GFixture->ParentFixedCount), 0.0f, 0.0f);
            }
            if (bReadParent &&
                GetOwner()->GetParentEntity()->GetPosition().x == static_cast<float>(GFixture->ParentFixedCount))
            {
                ++GFixture->ChildObservedLatestCount;
            }
        }

        uint32_t VariableTickCount = 0;
        uint32_t FixedTickCount = 0;
        float LastVariableDeltaTime = 0.0f;
        float LastFixedDeltaTime = 0.0f;
        const char* Name = "";
        bool bMoveOwner = false;
        bool bReadParent = false;
    };
    IMPLEMENT_CLASS(FixedProbeComponent, ProbeBase)

    class FixedProbeModule final : public Module::IModule
    {
    public:
        Identity GetModuleId() const override
        {
            return Identity(Name);
        }

        const char* GetName() const override
        {
            return "FixedProbeModule";
        }

        bool Install(Engine::Engine&) override
        {
            return true;
        }

        bool Initialize() override
        {
            return true;
        }

        void Shutdown() override
        {
        }

        void PreFixedTick(float fixedDeltaTime) override
        {
            ++PreFixedTickCount;
            LastFixedDeltaTime = fixedDeltaTime;
            if (GFixture)
            {
                GFixture->Events.push_back(Container::String(PreName));
                if (bObserveChildWorldTransform && GFixture->ObservedChild)
                {
                    GFixture->ModuleAPreObservedChildWorldX.push_back(
                        GFixture->ObservedChild->GetPosition().x);
                }
            }
        }

        void FixedTick(float fixedDeltaTime) override
        {
            ++FixedTickCount;
            LastFixedDeltaTime = fixedDeltaTime;
            if (!GFixture)
            {
                return;
            }

            GFixture->LastModuleFixedDelta = fixedDeltaTime;
            GFixture->Events.push_back(Container::String(FixedName));
            if (GFixture->DestroyEntity)
            {
                GFixture->bPostObservedPending = GFixture->DestroyEntity->IsPendingDestroy();
            }
            if (bObserveChildWorldTransform && GFixture->ObservedChild)
            {
                GFixture->ModuleAPostObservedChildWorldX.push_back(
                    GFixture->ObservedChild->GetPosition().x);
            }
        }

        void LateTick(float) override
        {
            if (GFixture)
            {
                GFixture->Events.push_back(Container::String(LateName));
            }
        }

        const char* LateName = "";
        uint32_t PreFixedTickCount = 0;
        uint32_t FixedTickCount = 0;
        float LastFixedDeltaTime = 0.0f;
        const char* Name = "";
        const char* PreName = "";
        const char* FixedName = "";
        bool bObserveChildWorldTransform = false;
    };

    bool RegisterModules()
    {
        FixedProbeModule* moduleA = new FixedProbeModule();
        moduleA->Name = "FixedProbeModuleA";
        moduleA->PreName = "ModuleA Pre";
        moduleA->FixedName = "ModuleA Fixed";
        moduleA->LateName = "ModuleA Late";
        moduleA->bObserveChildWorldTransform = true;
        FixedProbeModule* moduleB = new FixedProbeModule();
        moduleB->Name = "FixedProbeModuleB";
        moduleB->PreName = "ModuleB Pre";
        moduleB->FixedName = "ModuleB Fixed";
        moduleB->LateName = "ModuleB Late";

        Module::ModuleRegistry& registry = Module::GetModuleRegistry();
        return registry.Register(Container::TUniquePtr<Module::IModule>(moduleA)) == moduleA &&
            registry.Register(Container::TUniquePtr<Module::IModule>(moduleB)) == moduleB &&
            registry.InstallAll(*Engine::GEngine);
    }

    bool RegisterWorldProbes(World& world);

    bool SetupDynamicFixture(DynamicFixture& fixture, Engine::ApplicationProcessor& processor, bool bRegisterModules = true)
    {
        GFixture = &fixture;
        Engine::GEngine = new Engine::Engine();
        Engine::GEngine->GetWorld().Initialize();
        if (!RegisterWorldProbes(Engine::GEngine->GetWorld()))
        {
            return false;
        }
        if (bRegisterModules && !RegisterModules())
        {
            return false;
        }

        Engine::ApplicationFixedStepTestAccess::ResetRun(processor);
        return true;
    }

    void TeardownDynamicFixture(Engine::ApplicationProcessor& processor)
    {
        Engine::ApplicationFixedStepTestAccess::EndRun(processor);
        if (Engine::GEngine)
        {
            Module::GetModuleRegistry().ShutdownAll(*Engine::GEngine);
            Engine::GEngine->GetWorld().Finalize();
            delete Engine::GEngine;
            Engine::GEngine = nullptr;
        }
        GFixture = nullptr;
    }

    bool CreateOrderFixture(
        World& world,
        FixedProbeComponent*& outA1,
        FixedProbeComponent*& outA2,
        FixedProbeComponent*& outB1)
    {
        Entity* root = world.SpawnEntity<Entity>();
        if (!root)
        {
            return false;
        }

        Entity* child = world.SpawnEntity<Entity>(root);
        if (!child)
        {
            return false;
        }

        outA1 = world.CreateComponent<FixedProbeComponent>(root);
        outA2 = world.CreateComponent<FixedProbeComponent>(root);
        outB1 = world.CreateComponent<FixedProbeComponent>(child);
        if (!outA1 || !outA2 || !outB1)
        {
            return false;
        }

        outA1->Name = "A1";
        outA2->Name = "A2";
        outB1->Name = "B1";
        return true;
    }

    bool HasExpectedOrder(const DynamicFixture& fixture, uint32_t stepCount)
    {
        const Container::String expectedEvents[] =
        {
            Container::String("ModuleA Pre"),
            Container::String("ModuleB Pre"),
            Container::String("A1"),
            Container::String("A2"),
            Container::String("B1"),
            Container::String("ModuleA Fixed"),
            Container::String("ModuleB Fixed")
        };
        if (fixture.Events.size() != stepCount * 7)
        {
            return false;
        }

        for (uint32_t step = 0; step < stepCount; ++step)
        {
            for (uint32_t eventIndex = 0; eventIndex < 7; ++eventIndex)
            {
                if (fixture.Events[step * 7 + eventIndex] != expectedEvents[eventIndex])
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool TestVariableTickRunsOnceAndFixedTickRunsZeroOrMoreTimes(Engine::ApplicationProcessor& processor)
    {
        FixedProbeComponent* a1 = nullptr;
        FixedProbeComponent* a2 = nullptr;
        FixedProbeComponent* b1 = nullptr;
        if (!CreateOrderFixture(Engine::GEngine->GetWorld(), a1, a2, b1))
        {
            return false;
        }

        Engine::GEngine->GetWorld().Tick(0.1f);
        const Engine::FixedStepAdvanceResult result =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 1'000'000'000, true);
        return result.ExecutedSteps == 8 &&
            a1->VariableTickCount == 1 && a2->VariableTickCount == 1 && b1->VariableTickCount == 1 &&
            a1->FixedTickCount == 8 && a2->FixedTickCount == 8 && b1->FixedTickCount == 8 &&
            HasExpectedOrder(*GFixture, 8);
    }

    bool TestModuleWorldComponentOrderIsStable(Engine::ApplicationProcessor& processor)
    {
        FixedProbeComponent* a1 = nullptr;
        FixedProbeComponent* a2 = nullptr;
        FixedProbeComponent* b1 = nullptr;
        if (!CreateOrderFixture(Engine::GEngine->GetWorld(), a1, a2, b1))
        {
            return false;
        }

        const Engine::FixedStepAdvanceResult result =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 17'000'000, true);
        return result.ExecutedSteps == 1 && HasExpectedOrder(*GFixture, 1);
    }

    bool TestWorldUsesComponentRegistrationAndEntityDepthFirstOrder(Engine::ApplicationProcessor& processor)
    {
        return TestModuleWorldComponentOrderIsStable(processor);
    }

    bool TestTickDisabledParentStillTraversesChild(Engine::ApplicationProcessor& processor)
    {
        (void)processor;
        World& world = Engine::GEngine->GetWorld();
        Entity* parent = world.SpawnEntity<Entity>();
        Entity* child = parent ? world.SpawnEntity<Entity>(parent) : nullptr;
        FixedProbeComponent* parentComponent = parent ? world.CreateComponent<FixedProbeComponent>(parent) : nullptr;
        FixedProbeComponent* childComponent = child ? world.CreateComponent<FixedProbeComponent>(child) : nullptr;
        if (!parent || !child || !parentComponent || !childComponent)
        {
            return false;
        }

        parent->SetTickEnabled(false);
        world.Tick(0.01f);
        world.LateTick(0.01f);
        return parentComponent->VariableTickCount == 0 && childComponent->VariableTickCount == 1;
    }

    bool TestShouldAdvanceSimulationPausesAccumulatorAndHooks(Engine::ApplicationProcessor& processor)
    {
        const Engine::FixedStepAdvanceResult seeded =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 10'000'000, true);
        const Engine::FixedStepAdvanceResult paused =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 1'000'000'000, false);
        const Engine::FixedStepAdvanceResult resumed =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 7'000'000, true);
        return seeded.RemainderScaledUnits != 0 &&
            paused.Status == Engine::EFixedStepAdvanceStatus::Paused && paused.ExecutedSteps == 0 &&
            paused.RemainderScaledUnits == seeded.RemainderScaledUnits &&
            resumed.ExecutedSteps == 1;
    }

    bool TestResumeDoesNotCatchUpPausedWallTime(Engine::ApplicationProcessor& processor)
    {
        const Engine::FixedStepAdvanceResult paused =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 1'000'000'000, false);
        const Engine::FixedStepAdvanceResult resumed =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 10'000'000, true);
        return paused.RemainderScaledUnits == 0 && resumed.ExecutedSteps == 0;
    }

    bool TestFixedStepTransformSynchronizationOrder(Engine::ApplicationProcessor& processor)
    {
        World& world = Engine::GEngine->GetWorld();
        FixedProbeComponent* mover = nullptr;
        FixedProbeComponent* unused = nullptr;
        FixedProbeComponent* childComponent = nullptr;
        if (!CreateOrderFixture(world, mover, unused, childComponent))
        {
            return false;
        }

        Entity* parent = mover->GetOwner();
        Entity* child = childComponent->GetOwner();
        child->SetLocalPosition(1.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        parent->SetLocalPosition(10.0f, 0.0f, 0.0f);
        mover->bMoveOwner = true;
        GFixture->ObservedChild = child;
        const Engine::FixedStepAdvanceResult result =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 34'000'000, true);
        return result.ExecutedSteps == 2 &&
            HasExpectedOrder(*GFixture, 2) &&
            GFixture->ModuleAPreObservedChildWorldX.size() == 2 &&
            GFixture->ModuleAPreObservedChildWorldX[0] == 11.0f &&
            GFixture->ModuleAPreObservedChildWorldX[1] == 2.0f &&
            GFixture->ModuleAPostObservedChildWorldX.size() == 2 &&
            GFixture->ModuleAPostObservedChildWorldX[0] == 11.0f &&
            GFixture->ModuleAPostObservedChildWorldX[1] == 2.0f &&
            child->GetPosition().x == 3.0f;
    }

    bool TestSyncToSceneViewPublishesFinalFixedTransform(Engine::ApplicationProcessor& processor)
    {
        World& world = Engine::GEngine->GetWorld();
        Rendering::SceneView view;
        Rendering::SceneViewSettings settings;
        if (!view.Initialize(settings))
        {
            return false;
        }

        world.SetSceneView(&view);
        Entity* entity = world.SpawnEntity<Entity>();
        Component::PointLightComponent* light = entity ? world.CreateComponent<Component::PointLightComponent>(entity) : nullptr;
        FixedProbeComponent* mover = entity ? world.CreateComponent<FixedProbeComponent>(entity) : nullptr;
        if (!entity || !light || !mover)
        {
            world.SetSceneView(nullptr);
            view.Shutdown();
            return false;
        }

        mover->bMoveOwner = true;
        const Engine::FixedStepAdvanceResult result =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 51'000'000, true);
        world.SyncToSceneView();
        const bool bPublished = view.GetLightProxies().size() == 1 &&
            view.GetLightProxies()[0].PositionX == 3.0f;
        world.SetSceneView(nullptr);
        view.Shutdown();
        return result.ExecutedSteps == 3 && bPublished;
    }

    bool TestEntityDestroyIsVisibleToPostHookThenRemovedBeforeNextStep(Engine::ApplicationProcessor& processor)
    {
        World& world = Engine::GEngine->GetWorld();
        Entity* entity = world.SpawnEntity<Entity>();
        if (!entity)
        {
            return false;
        }

        GFixture->DestroyEntity = entity;
        GFixture->DestroyHandle = world.GetEntityHandle(*entity);
        if (!GFixture->DestroyHandle.IsValid() || world.ResolveEntityHandle(GFixture->DestroyHandle) != entity)
        {
            return false;
        }
        entity->MarkForDestroy();
        const Engine::FixedStepAdvanceResult first =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 17'000'000, true);
        const bool bRemoved = world.ResolveEntityHandle(GFixture->DestroyHandle) == nullptr;
        GFixture->DestroyEntity = nullptr;
        const Engine::FixedStepAdvanceResult second =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 17'000'000, true);
        return first.ExecutedSteps == 1 && GFixture->bPostObservedPending && bRemoved && second.ExecutedSteps == 1;
    }

    bool TestWorkerThreadCannotEnterApplicationFixedStepPath(Engine::ApplicationProcessor& processor)
    {
        const Engine::FixedStepAdvanceResult seeded =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 10'000'000, true);
        Engine::FixedStepAdvanceResult worker{};
        std::thread thread([&processor, &worker]()
        {
            worker = Engine::ApplicationFixedStepTestAccess::Advance(processor, 20'000'000, true);
        });
        thread.join();
        const Engine::FixedStepAdvanceResult boundary =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 7'000'000, true);
        return seeded.RemainderScaledUnits != 0 &&
            worker.Status == Engine::EFixedStepAdvanceStatus::WrongThread && worker.ExecutedSteps == 0 &&
            worker.RemainderScaledUnits == seeded.RemainderScaledUnits && boundary.ExecutedSteps == 1;
    }

    bool TestVariableDeltaRemainsClampedWhileFixedUsesRawDelta(Engine::ApplicationProcessor& processor)
    {
        const float variableDelta = Engine::ApplicationFixedStepTestAccess::ClampVariableDeltaTime(processor, 1'000'000'000);
        const Engine::FixedStepAdvanceResult fixed =
            Engine::ApplicationFixedStepTestAccess::Advance(processor, 1'000'000'000, true);
        return variableDelta == 0.1f && fixed.ExecutedSteps == 8;
    }

    class StageProbeComponent final : public ProbeBase
    {
        REFLECTION_CLASS(StageProbeComponent, ProbeBase)

    public:
        void OnTick(float)
        {
            GFixture->Events.push_back(Container::String(Name));
            ObservedWorldX = GetOwner()->GetPosition().x;
        }
        const char* Name = "";
        float ObservedWorldX = -1.0f;
    };
    IMPLEMENT_CLASS(StageProbeComponent, ProbeBase)

    class LateProbeHandler final : public Application::ApplicationHandlerBase
    {
    public:
        void OnLateUpdate(float) override
        {
            ++CallCount;
            GFixture->Events.push_back(Container::String("Handler Late"));
        }
        uint32_t CallCount = 0;
    };

    bool TestSimulationLateStages(Engine::ApplicationProcessor& processor,
        uint32_t stepCount, bool bModulesRunning)
    {
        World& world = Engine::GEngine->GetWorld();
        Entity* root = world.SpawnEntity<Entity>();
        FixedProbeComponent* mover = root ? world.CreateComponent<FixedProbeComponent>(root) : nullptr;
        if (!mover) return false;
        root->SetLocalPosition(3.0f, 0.0f, 0.0f);
        mover->Name = "Mover Fixed";
        mover->bMoveOwner = true;
        const Component::ETickGroup groups[] = { Component::ETickGroup::Input,
            Component::ETickGroup::Movement, Component::ETickGroup::Default,
            Component::ETickGroup::Animation, Component::ETickGroup::PoseFinalize,
            Component::ETickGroup::PostPhysics, Component::ETickGroup::Camera, Component::ETickGroup::PreRender };
        const char* names[] = { "World Input", "World Movement", "World Default", "World Animation",
            "World PoseFinalize", "World PostPhysics", "World Camera", "World PreRender" };
        StageProbeComponent* camera = nullptr;
        StageProbeComponent* early = nullptr;
        // 登録順の逆順でも群順で実行されることを確認する。
        for (uint32_t remaining = 8; remaining > 0; --remaining)
        {
            const uint32_t index = remaining - 1;
            StageProbeComponent* probe = world.CreateComponent<StageProbeComponent>(root);
            if (!probe || !probe->SetTickGroup(groups[index])) return false;
            probe->Name = names[index];
            if (index == 6) camera = probe;
            if (index == 2) early = probe;
        }
        world.UpdateWorldTransforms();
        LateProbeHandler handler;
        const int64_t rawDelta = stepCount == 0 ? 1'000'000 : (stepCount == 1 ? 17'000'000 : 34'000'000);
        const auto result = Engine::ApplicationFixedStepTestAccess::Simulate(processor, rawDelta, true, &handler);
        Container::VariableArray<Container::String> expected;
        expected.push_back(Container::String("World Input"));
        expected.push_back(Container::String("World Movement"));
        expected.push_back(Container::String("World Default"));
        expected.push_back(Container::String("World Animation"));
        expected.push_back(Container::String("World PoseFinalize"));
        for (uint32_t step = 0; step < stepCount; ++step)
        {
            if (bModulesRunning)
            {
                expected.push_back(Container::String("ModuleA Pre"));
                expected.push_back(Container::String("ModuleB Pre"));
            }
            expected.push_back(Container::String("Mover Fixed"));
            if (bModulesRunning)
            {
                expected.push_back(Container::String("ModuleA Fixed"));
                expected.push_back(Container::String("ModuleB Fixed"));
            }
        }
        expected.push_back(Container::String("World PostPhysics"));
        expected.push_back(Container::String("World Camera"));
        expected.push_back(Container::String("World PreRender"));
        if (bModulesRunning)
        {
            expected.push_back(Container::String("ModuleA Late"));
            expected.push_back(Container::String("ModuleB Late"));
        }
        expected.push_back(Container::String("Handler Late"));
        if (result.ExecutedSteps != stepCount || handler.CallCount != 1 ||
            camera->ObservedWorldX != (stepCount == 0 ? 3.0f : static_cast<float>(stepCount)) ||
            early->ObservedWorldX != 3.0f ||
            GFixture->Events.size() != expected.size()) return false;
        for (size_t index = 0; index < expected.size(); ++index)
        {
            if (GFixture->Events[index] != expected[index]) return false;
        }
        return true;
    }

    bool TestSimulationPauseSuppressesLateStages(Engine::ApplicationProcessor& processor)
    {
        const auto seeded = Engine::ApplicationFixedStepTestAccess::Advance(processor, 10'000'000, true);
        World& world = Engine::GEngine->GetWorld();
        Entity* entity = world.SpawnEntity<Entity>();
        StageProbeComponent* early = entity ? world.CreateComponent<StageProbeComponent>(entity) : nullptr;
        StageProbeComponent* late = entity ? world.CreateComponent<StageProbeComponent>(entity) : nullptr;
        if (!early || !late || !early->SetTickGroup(Component::ETickGroup::Input) ||
            !late->SetTickGroup(Component::ETickGroup::Camera)) return false;
        early->Name = "Paused Input";
        late->Name = "Paused Camera";
        LateProbeHandler handler;
        GFixture->Events.clear();
        const auto paused = Engine::ApplicationFixedStepTestAccess::Simulate(processor, 1'000'000'000, false, &handler);
        if (paused.Status != Engine::EFixedStepAdvanceStatus::Paused || paused.ExecutedSteps != 0 ||
            paused.RemainderScaledUnits != seeded.RemainderScaledUnits ||
            !GFixture->Events.empty() || handler.CallCount != 0) return false;
        const auto resumed = Engine::ApplicationFixedStepTestAccess::Simulate(processor, 7'000'000, true, &handler);
        return resumed.ExecutedSteps == 1 && handler.CallCount == 1 && GFixture->Events.size() == 9 &&
            GFixture->Events[0] == Container::String("Paused Input") &&
            GFixture->Events[5] == Container::String("Paused Camera") &&
            GFixture->Events[6] == Container::String("ModuleA Late") &&
            GFixture->Events[7] == Container::String("ModuleB Late") &&
            GFixture->Events[8] == Container::String("Handler Late");
    }

    class FixedInputEventProbe final : public ProbeBase, public IFixedProbe
    {
        REFLECTION_CLASS(FixedInputEventProbe, ProbeBase)

      public:
        Input::InputButtonState Button;
        Container::VariableArray<Input::EInputButtonEventType> Consumed;
        bool bQueuePair = true;
        uint32_t FixedCalls = 0;
        void OnTick(float)
        {
            Button.BeginFrame();
            Button.SetFixedEventCapture(true);
            if (bQueuePair)
            {
                Button.SetDown(true);
                Button.SetDown(false);
                bQueuePair = false;
            }
        }
        void OnFixed(float) override
        {
            ++FixedCalls;
            Input::InputButtonEvent event;
            while (Button.ConsumeFixedEvent(event))
                Consumed.push_back(event.Type);
        }
    };
    IMPLEMENT_CLASS(FixedInputEventProbe, ProbeBase)
    bool TestFixedInputEventFrameOrder(Engine::ApplicationProcessor& processor)
    {
        auto& world = Engine::GEngine->GetWorld();
        auto* entity = world.SpawnEntity<Entity>();
        auto* input = entity ? world.CreateComponent<FixedInputEventProbe>(entity) : nullptr;
        if (!input || !input->SetTickGroup(Component::ETickGroup::Input))
            return false;
        auto result = Engine::ApplicationFixedStepTestAccess::Simulate(processor, 17'000'000, true, nullptr);
        if (result.ExecutedSteps != 1 || input->Consumed.size() != 2)
            return false;
        input->bQueuePair = true;
        result = Engine::ApplicationFixedStepTestAccess::Simulate(processor, 1'000'000, true, nullptr);
        if (result.ExecutedSteps != 0 || input->Consumed.size() != 2)
            return false;
        result = Engine::ApplicationFixedStepTestAccess::Simulate(processor, 16'000'000, true, nullptr);
        if (result.ExecutedSteps != 1 || input->Consumed.size() != 4)
            return false;
        input->bQueuePair = true;
        result = Engine::ApplicationFixedStepTestAccess::Simulate(processor, 34'000'000, true, nullptr);
        if (result.ExecutedSteps != 2 || input->FixedCalls != 4 || input->Consumed.size() != 6)
            return false;
        for (size_t i = 0; i < 6; ++i)
            if (input->Consumed[i] !=
                (i % 2 == 0 ? Input::EInputButtonEventType::Pressed : Input::EInputButtonEventType::Released))
                return false;
        return true;
    }
    bool Test120HzPipeline(Engine::ApplicationProcessor& processor)
    {
        if (!Engine::ApplicationFixedStepTestAccess::ResetRun(processor, 120) ||
            processor.GetFixedUpdateRateHz() != 120)
            return false;
        auto& world = Engine::GEngine->GetWorld();
        auto* entity = world.SpawnEntity<Entity>();
        auto* component = entity ? world.CreateComponent<FixedProbeComponent>(entity) : nullptr;
        if (!component)
            return false;
        const auto result = Engine::ApplicationFixedStepTestAccess::Advance(processor, 16'666'667, true);
        return result.ExecutedSteps == 2 && component->FixedTickCount == 2 &&
               component->LastFixedDeltaTime == 1.f / 120 && GFixture->LastModuleFixedDelta == 1.f / 120;
    }

    // channel は型ごとの登録に持つので、channel ごとに型を分ける
    class TimeChannelProbe : public ProbeBase, public IFixedProbe
    {
        REFLECTION_CLASS(TimeChannelProbe, ProbeBase)

      public:
        float Delta = -1, FixedDelta = -1;
        unsigned Calls = 0, FixedCalls = 0;
        void OnTick(float value)
        {
            Delta = value;
            ++Calls;
        }
        void OnFixed(float value) override
        {
            FixedDelta = value;
            ++FixedCalls;
        }
    };
    IMPLEMENT_CLASS(TimeChannelProbe, ProbeBase)
    class TimeChannelWorldProbe final : public TimeChannelProbe
    {
        REFLECTION_CLASS(TimeChannelWorldProbe, TimeChannelProbe)
    };
    IMPLEMENT_CLASS(TimeChannelWorldProbe, TimeChannelProbe)
    class TimeChannelAnimationProbe final : public TimeChannelProbe
    {
        REFLECTION_CLASS(TimeChannelAnimationProbe, TimeChannelProbe)
    };
    IMPLEMENT_CLASS(TimeChannelAnimationProbe, TimeChannelProbe)
    class TimeChannelUnscaledProbe final : public TimeChannelProbe
    {
        REFLECTION_CLASS(TimeChannelUnscaledProbe, TimeChannelProbe)
    };
    IMPLEMENT_CLASS(TimeChannelUnscaledProbe, TimeChannelProbe)

    TimeChannelProbe* CreateTimeChannelProbe(World& world, Entity* owner, Engine::TimeChannel channel)
    {
        switch (channel)
        {
        case Engine::TimeChannel::Animation:
            return world.CreateComponent<TimeChannelAnimationProbe>(owner);
        case Engine::TimeChannel::Unscaled:
            return world.CreateComponent<TimeChannelUnscaledProbe>(owner);
        default:
            return world.CreateComponent<TimeChannelWorldProbe>(owner);
        }
    }
    bool TestHitStopTimeChannels(Engine::ApplicationProcessor& processor, uint32_t rate)
    {
        using namespace Engine;
        if (!ApplicationFixedStepTestAccess::ResetRun(processor, rate))
            return false;
        auto& engine = *NorvesLib::Core::Engine::GEngine;
        auto& world = engine.GetWorld();
        auto& clock = engine.GetTimeSystem();
        auto* owner = world.SpawnEntity<Entity>();
        if (!owner || !owner->SetCustomTimeDilation(.25f))
            return false;
        TimeChannelProbe* probes[4]{};
        const TimeChannel channels[] = {TimeChannel::World, TimeChannel::Animation, TimeChannel::Unscaled,
                                        TimeChannel::Unscaled};
        const Component::ETickGroup groups[] = {Component::ETickGroup::Default, Component::ETickGroup::Animation,
                                                Component::ETickGroup::Input, Component::ETickGroup::Camera};
        for (unsigned i = 0; i < 4; ++i)
        {
            probes[i] = CreateTimeChannelProbe(world, owner, channels[i]);
            if (!probes[i])
                return false;
            probes[i]->SetTickGroup(groups[i]);
        }
        TimeScaleRequest half;
        half.Scale = .5;
        half.DurationSeconds = 10;
        half.Channels = TimeChannelBit(TimeChannel::Physics);
        TimeScaleHandle handle;
        if (clock.PushScale(half, handle) != TimeSystemResult::Success)
            return false;
        auto result = ApplicationFixedStepTestAccess::Simulate(processor, 1, true, nullptr);
        if (result.ExecutedSteps != 0 || clock.GetPhysicsRemainder() != 32768)
            return false;
        const auto seed = ApplicationFixedStepTestAccess::Advance(processor, rate == 60 ? 8333333 : 4166666, true);
        TimeScaleRequest stop;
        stop.Scale = 0;
        stop.DurationSeconds = .1;
        stop.Channels = TimeChannelBit(TimeChannel::World) | TimeChannelBit(TimeChannel::Animation) |
                        TimeChannelBit(TimeChannel::Physics);
        if (clock.PushScale(stop, handle) != TimeSystemResult::Success)
            return false;
        result = ApplicationFixedStepTestAccess::Simulate(processor, 50000000, true, nullptr);
        if (result.Status != EFixedStepAdvanceStatus::Advanced || result.ExecutedSteps != 0 ||
            result.RemainderScaledUnits != seed.RemainderScaledUnits || clock.GetPhysicsRemainder() != 32768 ||
            probes[0]->Delta != 0 || probes[1]->Delta != 0 || probes[2]->Delta != .05f || probes[3]->Delta != .05f)
            return false;
        const auto calls = probes[3]->Calls;
        result = ApplicationFixedStepTestAccess::Simulate(processor, 100000000, false, nullptr);
        if (result.Status != EFixedStepAdvanceStatus::Paused ||
            result.RemainderScaledUnits != seed.RemainderScaledUnits || clock.GetPhysicsRemainder() != 32768 ||
            probes[3]->Calls != calls)
            return false;
        result = ApplicationFixedStepTestAccess::Simulate(processor, rate == 60 ? 17000000 : 8500000, true, nullptr);
        return result.ExecutedSteps == 1 && probes[0]->FixedCalls == 1 && probes[0]->FixedDelta == 1.f / rate &&
               probes[0]->Delta > 0 && probes[1]->Delta > 0 && probes[3]->Calls == calls + 1;
    }

    // 構造体の未初期化パディングを含めず、浮動小数点もそのままのビット列で保存する。
    bool WriteFixedFrameRecord(HANDLE file, const FixedFrameRecord& record)
    {
        unsigned char bytes[kFixedFrameRecordSize]{};
        size_t offset = 0;
        const auto append = [&bytes, &offset](const auto& value)
        {
            std::memcpy(bytes + offset, &value, sizeof(value));
            offset += sizeof(value);
        };
        append(record.ExecutedSteps);
        append(record.RemainderScaledUnits);
        append(record.ElapsedNanoseconds);
        append(record.PhysicsDeltaNanoseconds);
        append(record.DeltaSeconds);
        append(record.TotalSeconds);
        DWORD written = 0;
        return WriteFile(file, bytes, sizeof(bytes), &written, nullptr) != FALSE && written == sizeof(bytes);
    }

    bool TestFixedFrame24Pipeline(Engine::ApplicationProcessor& processor, HANDLE recordFile)
    {
        using namespace Engine;
        if (!ApplicationFixedStepTestAccess::ResetRun(processor, 60) ||
            !ApplicationFixedStepTestAccess::SetFixedFrameClock(processor, "1/24"))
        {
            return false;
        }

        const auto& fixedClock = ApplicationFixedStepTestAccess::GetFixedFrameClock(processor);
        auto& timeSystem = NorvesLib::Core::Engine::GEngine->GetTimeSystem();
        constexpr int64_t nanosecondsPerSecond = 1'000'000'000;
        int64_t previousExpectedTime = 0;
        uint64_t previousExpectedSteps = 0;
        uint64_t totalSteps = 0;
        bool bPassed = true;
        bool bReportedFailure = false;
        std::cout << "ケース21 ステップ数:";
        for (uint32_t frame = 1; frame <= kFixedFrame24Count; ++frame)
        {
            const int64_t raw = ApplicationFixedStepTestAccess::AcquireFrameDeltaNanoseconds(processor);
            const auto result = ApplicationFixedStepTestAccess::Simulate(processor, raw, true, nullptr);
            const FixedFrameRecord record{result.ExecutedSteps, result.RemainderScaledUnits,
                timeSystem.GetElapsedNanoseconds(), timeSystem.GetFrameTimes().PhysicsDeltaNanoseconds,
                fixedClock.GetDeltaSeconds(), fixedClock.GetTotalSeconds()};
            std::cout << ' ' << record.ExecutedSteps;
            totalSteps += record.ExecutedSteps;

            // 時刻とステップの有理数を、フレーム番号から直接求める。
            const int64_t expectedTime = (static_cast<int64_t>(frame) * nanosecondsPerSecond + 23) / 24;
            const uint64_t scaledTime = static_cast<uint64_t>(expectedTime) * 60;
            const uint64_t expectedSteps = scaledTime / nanosecondsPerSecond;
            const uint64_t expectedFrameSteps = expectedSteps - previousExpectedSteps;
            const bool bFramePassed = result.Status == EFixedStepAdvanceStatus::Advanced &&
                result.DroppedSteps == 0 && (record.ExecutedSteps == 2 || record.ExecutedSteps == 3) &&
                record.ExecutedSteps == expectedFrameSteps &&
                record.RemainderScaledUnits == scaledTime % nanosecondsPerSecond &&
                raw == expectedTime - previousExpectedTime &&
                record.ElapsedNanoseconds == fixedClock.GetTotalNanoseconds() &&
                record.ElapsedNanoseconds == expectedTime && record.PhysicsDeltaNanoseconds == raw &&
                std::fabs(static_cast<double>(record.DeltaSeconds) -
                    static_cast<double>(raw) / nanosecondsPerSecond) < 1e-8 &&
                std::fabs(record.TotalSeconds - static_cast<double>(expectedTime) / nanosecondsPerSecond) < 1e-15;
            if (!bFramePassed && !bReportedFailure)
            {
                std::cerr << "ケース21 不一致: フレーム=" << frame << " 実行=" << record.ExecutedSteps
                    << " 期待=" << expectedFrameSteps << " 経過ns=" << record.ElapsedNanoseconds
                    << " 期待ns=" << expectedTime << '\n';
                bReportedFailure = true;
            }
            bPassed &= bFramePassed;
            if (recordFile != INVALID_HANDLE_VALUE)
            {
                bPassed &= WriteFixedFrameRecord(recordFile, record);
            }
            previousExpectedTime = expectedTime;
            previousExpectedSteps = expectedSteps;
        }
        std::cout << " 合計=" << totalSteps << '\n';
        return bPassed && totalSteps == 120 &&
            std::llabs(fixedClock.GetTotalNanoseconds() - 2'000'000'000LL) <= 1 &&
            timeSystem.GetElapsedNanoseconds() == fixedClock.GetTotalNanoseconds();
    }

    bool TestFixedFrame30Pipeline(Engine::ApplicationProcessor& processor)
    {
        using namespace Engine;
        if (!ApplicationFixedStepTestAccess::ResetRun(processor, 60) ||
            !ApplicationFixedStepTestAccess::SetFixedFrameClock(processor, "1/30"))
        {
            return false;
        }

        uint64_t totalSteps = 0;
        bool bPassed = true;
        std::cout << "ケース22 ステップ数:";
        for (uint32_t frame = 0; frame < 30; ++frame)
        {
            const int64_t raw = ApplicationFixedStepTestAccess::AcquireFrameDeltaNanoseconds(processor);
            const auto result = ApplicationFixedStepTestAccess::Simulate(processor, raw, true, nullptr);
            std::cout << ' ' << result.ExecutedSteps;
            totalSteps += result.ExecutedSteps;
            bPassed &= result.Status == EFixedStepAdvanceStatus::Advanced &&
                result.DroppedSteps == 0 && result.ExecutedSteps == 2;
        }
        std::cout << " 合計=" << totalSteps << '\n';
        return bPassed && totalSteps == 60;
    }

    // 試験の型の本体と、固定ステップの段のシステムを World へ登録する
    bool RegisterWorldProbes(World& world)
    {
        RegisterProbeTick<FixedProbeComponent>(world, "Test.FixedProbe");
        RegisterProbeTick<StageProbeComponent>(world, "Test.StageProbe");
        RegisterProbeTick<FixedInputEventProbe>(world, "Test.FixedInputEventProbe");
        RegisterProbeTick<TimeChannelWorldProbe>(world, "Test.TimeChannelWorld", Engine::TimeChannel::World);
        RegisterProbeTick<TimeChannelAnimationProbe>(world, "Test.TimeChannelAnimation", Engine::TimeChannel::Animation);
        RegisterProbeTick<TimeChannelUnscaledProbe>(world, "Test.TimeChannelUnscaled", Engine::TimeChannel::Unscaled);
        Ecs::SystemScheduler& scheduler = world.GetScheduler();
        Ecs::SystemDesc desc;
        desc.Name = "Test.FixedProbes";
        desc.Stage = scheduler.FindStage("FixedTick");
        desc.Thread = ThreadPolicy::GameThreadOnly;
        desc.Run = &RunFixedProbes;
        desc.User = &world;
        return scheduler.RegisterSystem(desc) != Ecs::kInvalidEcsSystem;
    }

    bool RunCase(uint32_t caseIndex, Engine::ApplicationProcessor& processor, HANDLE recordFile)
    {
        switch (caseIndex)
        {
        case 0:
            return TestVariableTickRunsOnceAndFixedTickRunsZeroOrMoreTimes(processor);
        case 1:
            return TestModuleWorldComponentOrderIsStable(processor);
        case 2:
            return TestWorldUsesComponentRegistrationAndEntityDepthFirstOrder(processor);
        case 3:
            return TestTickDisabledParentStillTraversesChild(processor);
        case 4:
            return TestShouldAdvanceSimulationPausesAccumulatorAndHooks(processor);
        case 5:
            return TestResumeDoesNotCatchUpPausedWallTime(processor);
        case 6:
            return TestFixedStepTransformSynchronizationOrder(processor);
        case 7:
            return TestSyncToSceneViewPublishesFinalFixedTransform(processor);
        case 8:
            return TestEntityDestroyIsVisibleToPostHookThenRemovedBeforeNextStep(processor);
        case 9:
            return TestWorkerThreadCannotEnterApplicationFixedStepPath(processor);
        case 10:
            return TestVariableDeltaRemainsClampedWhileFixedUsesRawDelta(processor);
        case 11:
            return TestSimulationLateStages(processor, 0, true);
        case 12:
            return TestSimulationLateStages(processor, 1, true);
        case 13:
            return TestSimulationLateStages(processor, 2, true);
        case 14:
            return TestSimulationPauseSuppressesLateStages(processor);
        case 15:
            Module::GetModuleRegistry().ShutdownAll(*Engine::GEngine);
            return TestSimulationLateStages(processor, 1, false);
        case 16:
            return TestSimulationLateStages(processor, 1, false);
        case 17:
            return TestFixedInputEventFrameOrder(processor);
        case 18:
            return Test120HzPipeline(processor);
        case 19:
            return TestHitStopTimeChannels(processor, 60);
        case 20:
            return TestHitStopTimeChannels(processor, 120);
        case 21:
            return TestFixedFrame24Pipeline(processor, recordFile);
        case 22:
            return TestFixedFrame30Pipeline(processor);
        default:
            return false;
        }
    }

    bool RunChild(uint32_t caseIndex, HANDLE recordFile = INVALID_HANDLE_VALUE)
    {
        DynamicFixture fixture;
        Engine::ApplicationProcessor processor;
        if (!SetupDynamicFixture(fixture, processor, caseIndex != 16))
        {
            TeardownDynamicFixture(processor);
            return false;
        }

        const bool bPassed = RunCase(caseIndex, processor, recordFile);
        TeardownDynamicFixture(processor);
        return bPassed;
    }

    bool RunChildProcess(uint32_t caseIndex, const wchar_t* recordPath = nullptr)
    {
        wchar_t executablePath[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        if (executableLength == 0 || executableLength >= MAX_PATH)
        {
            return false;
        }

        wchar_t commandLine[MAX_PATH * 2 + 64]{};
        const int commandLength = recordPath
            ? swprintf_s(commandLine, L"\"%s\" --child=%u \"--record=%s\"", executablePath, caseIndex, recordPath)
            : swprintf_s(commandLine, L"\"%s\" --child=%u", executablePath, caseIndex);
        if (commandLength <= 0)
        {
            return false;
        }

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        if (CreateProcessW(nullptr, commandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &startupInfo, &processInfo) == FALSE)
        {
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode = EXIT_FAILURE;
        const bool bReadExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return waitResult == WAIT_OBJECT_0 && bReadExitCode && exitCode == EXIT_SUCCESS;
    }

    bool CompareFixedFrameRecords(const wchar_t* firstPath, const wchar_t* secondPath)
    {
        HANDLE first = CreateFileW(firstPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE second = CreateFileW(secondPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        bool bPassed = false;
        if (first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER firstSize{};
            LARGE_INTEGER secondSize{};
            unsigned char firstBytes[kFixedFrameRecordsSize]{};
            unsigned char secondBytes[kFixedFrameRecordsSize]{};
            DWORD firstRead = 0;
            DWORD secondRead = 0;
            bPassed = GetFileSizeEx(first, &firstSize) != FALSE &&
                GetFileSizeEx(second, &secondSize) != FALSE &&
                firstSize.QuadPart == kFixedFrameRecordsSize && secondSize.QuadPart == kFixedFrameRecordsSize &&
                ReadFile(first, firstBytes, sizeof(firstBytes), &firstRead, nullptr) != FALSE &&
                ReadFile(second, secondBytes, sizeof(secondBytes), &secondRead, nullptr) != FALSE &&
                firstRead == sizeof(firstBytes) && secondRead == sizeof(secondBytes) &&
                std::memcmp(firstBytes, secondBytes, sizeof(firstBytes)) == 0;
        }
        if (first != INVALID_HANDLE_VALUE)
        {
            bPassed &= CloseHandle(first) != FALSE;
        }
        if (second != INVALID_HANDLE_VALUE)
        {
            bPassed &= CloseHandle(second) != FALSE;
        }
        return bPassed;
    }

    bool RunFixedFrame24Twice()
    {
        wchar_t directory[MAX_PATH]{};
        const DWORD length = GetTempPathW(MAX_PATH, directory);
        if (length == 0 || length >= MAX_PATH)
        {
            return false;
        }
        wchar_t firstPath[MAX_PATH]{};
        wchar_t secondPath[MAX_PATH]{};
        const bool bFirstCreated = GetTempFileNameW(directory, L"NFS", 0, firstPath) != 0;
        const bool bSecondCreated = GetTempFileNameW(directory, L"NFS", 0, secondPath) != 0;
        bool bPassed = false;
        if (bFirstCreated && bSecondCreated)
        {
            const bool bFirstPassed = RunChildProcess(kFixedFrame24Case, firstPath);
            const bool bSecondPassed = RunChildProcess(kFixedFrame24Case, secondPath);
            const bool bRecordsMatch = CompareFixedFrameRecords(firstPath, secondPath);
            std::cout << "ケース21 ２実行の記録: " << (bRecordsMatch ? "一致" : "不一致") << '\n';
            bPassed = bFirstPassed && bSecondPassed && bRecordsMatch;
        }
        if (bFirstCreated)
        {
            bPassed &= DeleteFileW(firstPath) != FALSE;
        }
        if (bSecondCreated)
        {
            bPassed &= DeleteFileW(secondPath) != FALSE;
        }
        return bPassed;
    }

    bool TryParseChildCase(const char* argument, uint32_t& outCaseIndex)
    {
        constexpr const char* prefix = "--child=";
        if (std::strncmp(argument, prefix, std::strlen(prefix)) != 0)
        {
            return false;
        }

        const long parsed = std::strtol(argument + std::strlen(prefix), nullptr, 10);
        if (parsed < 0 || parsed >= kCaseCount)
        {
            return false;
        }

        outCaseIndex = static_cast<uint32_t>(parsed);
        return true;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount == 2)
    {
        uint32_t caseIndex = 0;
        return TryParseChildCase(arguments[1], caseIndex) && RunChild(caseIndex)
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    if (argumentCount == 3)
    {
        uint32_t caseIndex = 0;
        constexpr const char* recordPrefix = "--record=";
        if (!TryParseChildCase(arguments[1], caseIndex) || caseIndex != kFixedFrame24Case ||
            std::strncmp(arguments[2], recordPrefix, std::strlen(recordPrefix)) != 0)
        {
            return EXIT_FAILURE;
        }

        // 狭い文字列への変換で失われる一時パスを避け、元の Unicode 引数を使う。
        int wideCount = 0;
        wchar_t** wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideCount);
        if (!wideArguments)
        {
            return EXIT_FAILURE;
        }
        constexpr const wchar_t* widePrefix = L"--record=";
        const wchar_t* recordPath = nullptr;
        bool bDuplicate = false;
        for (int index = 1; index < wideCount; ++index)
        {
            if (std::wcsncmp(wideArguments[index], widePrefix, std::wcslen(widePrefix)) == 0)
            {
                bDuplicate |= recordPath != nullptr;
                recordPath = wideArguments[index] + std::wcslen(widePrefix);
            }
        }
        HANDLE recordFile = INVALID_HANDLE_VALUE;
        if (recordPath && *recordPath && !bDuplicate)
        {
            recordFile = CreateFileW(recordPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        LocalFree(wideArguments);
        if (recordFile == INVALID_HANDLE_VALUE)
        {
            return EXIT_FAILURE;
        }
        const bool bPassed = RunChild(caseIndex, recordFile);
        const bool bClosed = CloseHandle(recordFile) != FALSE;
        return bPassed && bClosed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argumentCount != 1)
    {
        return EXIT_FAILURE;
    }

    bool bPassed = true;
    for (uint32_t caseIndex = 0; caseIndex < kCaseCount; ++caseIndex)
    {
        bPassed &= caseIndex == kFixedFrame24Case
            ? RunFixedFrame24Twice()
            : RunChildProcess(caseIndex);
    }
    std::cout << (bPassed ? "ApplicationFixedStepPipelineTest passed\n" : "ApplicationFixedStepPipelineTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
