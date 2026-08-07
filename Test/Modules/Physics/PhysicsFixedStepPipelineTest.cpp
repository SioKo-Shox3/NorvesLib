// PhysicsFixedStepPipelineTest — M7 fixed-step dispatch と Physics post-step の契約を検証する。

#include "Component/Component.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Engine/ApplicationProcessor.h"
#include "Engine/Engine.h"
#include "Engine/FixedStepScheduler.h"
#include "Module/ModuleRegistry.h"
#include "Object/Resource.h"
#include "Object/World.h"
#include "Physics/ColliderComponent.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneView.h"
#include "Scene/SceneQuery.h"
#include "Thread/Thread.h"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

namespace NorvesLib::Core::Engine
{
    struct ApplicationFixedStepTestAccess
    {
        static void ResetRun(ApplicationProcessor& processor)
        {
            processor.m_FixedStepScheduler->EndRun();
            processor.m_FixedStepScheduler->BeginRun();
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

        static void TickWithDelta(ApplicationProcessor& processor, int64_t rawDeltaNanoseconds)
        {
            const int64_t nowNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            processor.m_LastFrameTimeNanoseconds = nowNanoseconds - rawDeltaNanoseconds;
            processor.Tick();
        }
    };
} // namespace NorvesLib::Core::Engine

namespace NorvesLib::Modules::Physics
{
    class PhysicsModuleTestAccess
    {
    public:
        static bool IsColliderActive(const IPhysicsModule& module, Core::Scene::ColliderHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_ColliderSlots.size()
                && concrete.m_ColliderSlots[handle.Index].bOccupied
                && concrete.m_ColliderSlots[handle.Index].Generation == handle.Generation
                && concrete.m_ColliderSlots[handle.Index].bActive;
        }

        static bool IsColliderAlive(const IPhysicsModule& module, Core::Scene::ColliderHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_ColliderSlots.size()
                && concrete.m_ColliderSlots[handle.Index].bOccupied
                && concrete.m_ColliderSlots[handle.Index].Generation == handle.Generation;
        }

        static bool IsBodyActive(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_BodySlots.size()
                && concrete.m_BodySlots[handle.Index].bOccupied
                && concrete.m_BodySlots[handle.Index].Generation == handle.Generation
                && concrete.m_BodySlots[handle.Index].bActive;
        }

        static Math::Vector3 GetPreStepPosition(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return concrete.m_BodySlots[handle.Index].PreStepPosition;
        }

        static Math::Vector3 GetPendingImpulse(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return concrete.m_BodySlots[handle.Index].PendingImpulse;
        }

        static bool HasPreStepSnapshot(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return concrete.m_BodySlots[handle.Index].bHadPreStepSnapshot;
        }

        static uint32_t GetColliderGeneration(const IPhysicsModule& module, Core::Scene::ColliderHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return concrete.m_ColliderSlots[handle.Index].Generation;
        }

        static uint32_t GetBodyGeneration(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return concrete.m_BodySlots[handle.Index].Generation;
        }

        static uint32_t GetPreviousPairCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_PreviousTriggerPairs.size());
        }

        static uint32_t GetCurrentPairCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_CurrentTriggerPairs.size());
        }

        static uint32_t GetDispatchedEventCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_DispatchedEventCount;
        }

        static uint32_t GetPendingEventCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_PendingEventCount;
        }

        static bool HasPublishedSnapshot(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_bHasPublishedSnapshot;
        }

    private:
        static const PhysicsModule& GetConcrete(const IPhysicsModule& module)
        {
            const PhysicsModule* concrete = dynamic_cast<const PhysicsModule*>(&module);
            return *concrete;
        }
    };
} // namespace NorvesLib::Modules::Physics

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Engine;
    using namespace NorvesLib::Core::Module;
    using namespace NorvesLib::Core::Scene;
    using namespace NorvesLib::Modules::Physics;

    using FramePacketType = Rendering::FramePacket;

    template <typename T>
    using TPacketElement = typename std::remove_cvref_t<T>::value_type;

    template <typename T>
    constexpr bool bPhysicsFramePacketType = std::is_same_v<std::remove_cvref_t<T>, ColliderComponent>
        || std::is_same_v<std::remove_cvref_t<T>, RigidBodyComponent>
        || std::is_same_v<std::remove_cvref_t<T>, PhysicsModule>
        || std::is_same_v<std::remove_cvref_t<T>, IPhysicsModule>
        || std::is_same_v<std::remove_cvref_t<T>, PhysicsOverlapHit>
        || std::is_same_v<std::remove_cvref_t<T>, PhysicsRaycastHit>
        || std::is_same_v<std::remove_cvref_t<T>, ColliderHandle>
        || std::is_same_v<std::remove_cvref_t<T>, BodyHandle>
        || std::is_same_v<std::remove_cvref_t<T>, Entity>;

    static_assert(!std::is_pointer_v<decltype(FramePacketType::Scene)>);
    static_assert(!std::is_pointer_v<decltype(FramePacketType::DrawCommands)>);
    static_assert(!std::is_pointer_v<decltype(FramePacketType::InstanceData)>);
    static_assert(!std::is_pointer_v<decltype(FramePacketType::DebugLineVertices)>);
    static_assert(!std::is_pointer_v<decltype(FramePacketType::Stats)>);
    static_assert(!std::is_pointer_v<decltype(FramePacketType::Views)>);
    using SkinnedMeshFrameLeaseElement = TPacketElement<decltype(FramePacketType::SkinnedMeshFrameLeases)>;
    static_assert(std::is_same_v<
        SkinnedMeshFrameLeaseElement,
        Container::TSharedPtr<const Rendering::SkinnedMeshFrameLease>>);
    static_assert(!std::is_pointer_v<SkinnedMeshFrameLeaseElement>);
    static_assert(!std::is_same_v<SkinnedMeshFrameLeaseElement, Object*>);
    static_assert(!std::is_same_v<SkinnedMeshFrameLeaseElement, Resource*>);
    static_assert(!bPhysicsFramePacketType<decltype(FramePacketType::Scene)>);
    static_assert(!bPhysicsFramePacketType<TPacketElement<decltype(FramePacketType::DrawCommands)>>);
    static_assert(!bPhysicsFramePacketType<TPacketElement<decltype(FramePacketType::InstanceData)>>);
    static_assert(!bPhysicsFramePacketType<TPacketElement<decltype(FramePacketType::DebugLineVertices)>>);
    static_assert(!bPhysicsFramePacketType<TPacketElement<decltype(FramePacketType::Views)>>);
    static_assert(std::is_same_v<TPacketElement<decltype(FramePacketType::OverlayPasses)>, Rendering::IViewPass*>);
    static_assert(std::is_same_v<decltype(FramePacketType::FrameNumber), uint64_t>);
    static_assert(std::is_same_v<decltype(FramePacketType::DeltaTime), float>);
    static_assert(std::is_same_v<decltype(FramePacketType::TotalTime), double>);
    static_assert(std::is_same_v<decltype(FramePacketType::bHasMainCamera), bool>);
    static_assert(std::is_same_v<decltype(FramePacketType::Scene), Rendering::SceneProxy>);
    static_assert(std::is_same_v<decltype(FramePacketType::DrawCommands), Container::VariableArray<Rendering::DrawCommand>>);
    static_assert(std::is_same_v<decltype(FramePacketType::DrawCommandRange), Rendering::CommandRange>);
    static_assert(std::is_same_v<decltype(FramePacketType::OpaqueCommandRange), Rendering::CommandRange>);
    static_assert(std::is_same_v<decltype(FramePacketType::TransparentCommandRange), Rendering::CommandRange>);
    static_assert(std::is_same_v<decltype(FramePacketType::InstanceData), Container::VariableArray<Rendering::GPUSceneInstanceData>>);
    static_assert(std::is_same_v<decltype(FramePacketType::DebugLineVertices), Container::VariableArray<Rendering::DebugLineVertex>>);
    static_assert(std::is_same_v<decltype(FramePacketType::Stats), Rendering::FrameStatsSnapshot>);
    static_assert(std::is_same_v<decltype(FramePacketType::GeneratedDrawCommandCount), uint32_t>);
    static_assert(std::is_same_v<decltype(FramePacketType::Views), Container::VariableArray<Rendering::ViewRenderPlan>>);
    static_assert(std::is_same_v<decltype(FramePacketType::OverlayPasses), Container::VariableArray<Rendering::IViewPass*>>);
    static_assert(std::is_same_v<decltype(FramePacketType::State), Thread::Atomic<uint8_t>>);

    using SceneProxyType = Rendering::SceneProxy;

    static_assert(!std::is_pointer_v<decltype(SceneProxyType::MainCamera)>);
    static_assert(!std::is_pointer_v<decltype(SceneProxyType::AdditionalCameras)>);
    static_assert(!std::is_pointer_v<decltype(SceneProxyType::MeshProxies)>);
    static_assert(!std::is_pointer_v<decltype(SceneProxyType::SkinnedMeshProxies)>);
    static_assert(!std::is_pointer_v<decltype(SceneProxyType::MegaGeometryProxies)>);
    static_assert(!std::is_pointer_v<decltype(SceneProxyType::LightProxies)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::SkinnedMeshProxy::BonePalette)>);
    static_assert(!bPhysicsFramePacketType<decltype(SceneProxyType::MainCamera)>);
    static_assert(!bPhysicsFramePacketType<TPacketElement<decltype(SceneProxyType::AdditionalCameras)>>);
    static_assert(!bPhysicsFramePacketType<TPacketElement<decltype(SceneProxyType::MeshProxies)>>);
    static_assert(!bPhysicsFramePacketType<TPacketElement<decltype(SceneProxyType::MegaGeometryProxies)>>);
    static_assert(!bPhysicsFramePacketType<TPacketElement<decltype(SceneProxyType::LightProxies)>>);
    static_assert(std::is_same_v<decltype(SceneProxyType::MainCamera), Rendering::CameraProxy>);
    static_assert(std::is_same_v<decltype(SceneProxyType::AdditionalCameras), Container::VariableArray<Rendering::CameraProxy>>);
    static_assert(std::is_same_v<decltype(SceneProxyType::MeshProxies), Container::VariableArray<Rendering::MeshProxy>>);
    static_assert(std::is_same_v<decltype(SceneProxyType::SkinnedMeshProxies), Container::VariableArray<Rendering::SkinnedMeshProxy>>);
    static_assert(std::is_same_v<decltype(SceneProxyType::MegaGeometryProxies), Container::VariableArray<Rendering::MegaGeometryProxy>>);
    static_assert(std::is_same_v<decltype(SceneProxyType::LightProxies), Container::VariableArray<Rendering::LightProxy>>);
    static_assert(std::is_same_v<TPacketElement<decltype(SceneProxyType::SkinnedMeshProxies)>, Rendering::SkinnedMeshProxy>);
    static_assert(std::is_same_v<TPacketElement<decltype(Rendering::SkinnedMeshProxy::BonePalette)>, Math::Matrix4x4>);
    static_assert(std::is_same_v<decltype(SceneProxyType::AmbientColorR), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::AmbientColorG), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::AmbientColorB), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::AmbientIntensity), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::bFogEnabled), bool>);
    static_assert(std::is_same_v<decltype(SceneProxyType::FogColorR), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::FogColorG), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::FogColorB), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::FogDensity), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::FogStart), float>);
    static_assert(std::is_same_v<decltype(SceneProxyType::FogEnd), float>);

    // VariableArray の継承実装により両型は standard-layout ではないため、offsetof は使えない。
    // sizeof/alignof は ABI 変更を検出するが、末尾パディングだけの変更は検出できない。
    static_assert(!std::is_standard_layout_v<FramePacketType>);
    static_assert(!std::is_standard_layout_v<SceneProxyType>);
    static_assert(sizeof(FramePacketType) == 720);
    static_assert(alignof(FramePacketType) == 8);
    static_assert(sizeof(SceneProxyType) == 320);
    static_assert(alignof(SceneProxyType) == 8);

    constexpr uint32_t kCaseCount = 8;
    constexpr uint32_t kRepetitionsPerCase = 8;
    constexpr float kFixedDeltaTime = 1.0f / 60.0f;

    struct Fixture
    {
        Container::VariableArray<Container::String> Events;
        IPhysicsModule* Physics = nullptr;
        uint32_t PreCount = 0;
        uint32_t PostCount = 0;
        bool bPostSawQuery = false;
        Rendering::SceneView SceneView;
    };

    Fixture* GFixture = nullptr;

    class MoveKinematicComponent final : public Component::Component
    {
    public:
        void FixedTick(float /*fixedDeltaTime*/) override
        {
            ++FixedTickCount;
            if (GFixture)
            {
                GFixture->Events.push_back(Container::String("component"));
            }
            GetOwner()->SetLocalPosition(TargetPosition);
        }

        Math::Vector3 TargetPosition;
        uint32_t FixedTickCount = 0;
    };

    class MoveSceneQueryComponent final : public Component::Component
    {
    public:
        void FixedTick(float /*fixedDeltaTime*/) override
        {
            ++FixedTickCount;
            GetOwner()->SetLocalPosition(GetOwner()->GetLocalTransform().position + StepDisplacement);
        }

        Math::Vector3 StepDisplacement = Math::Vector3(3.5f, 0.0f, 0.0f);
        uint32_t FixedTickCount = 0;
    };

    class RecorderModule final : public IModule
    {
    public:
        Identity GetModuleId() const override
        {
            return Identity("PhysicsFixedStepRecorder");
        }

        const char* GetName() const override
        {
            return "PhysicsFixedStepRecorder";
        }

        bool Install(NorvesLib::Core::Engine::Engine&) override
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

        void PreFixedTick(float /*fixedDeltaTime*/) override
        {
            if (!GFixture)
            {
                return;
            }
            ++GFixture->PreCount;
            GFixture->Events.push_back(Container::String("pre"));
        }

        void FixedTick(float /*fixedDeltaTime*/) override
        {
            if (!GFixture)
            {
                return;
            }
            ++GFixture->PostCount;
            GFixture->Events.push_back(Container::String("post"));
            Container::VariableArray<PhysicsOverlapHit> hits;
            GFixture->bPostSawQuery = NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
                Math::Sphere(Math::Vector3(5.0f, 0.0f, 0.0f), 0.1f), hits)
                == EPhysicsSceneQueryResult::Success && hits.size() == 1;
        }
    };

    bool SetupFixture(Fixture& fixture, ApplicationProcessor& processor)
    {
        GFixture = &fixture;
        NorvesLib::Core::Engine::GEngine = new NorvesLib::Core::Engine::Engine();
        NorvesLib::Core::Engine::GEngine->GetWorld().Initialize();
        Rendering::SceneViewSettings sceneViewSettings;
        if (!fixture.SceneView.Initialize(sceneViewSettings))
        {
            return false;
        }
        NorvesLib::Core::Engine::GEngine->GetWorld().SetSceneView(&fixture.SceneView);
        ModuleRegistry& registry = GetModuleRegistry();
        fixture.Physics = RegisterPhysicsModule(registry);
        RecorderModule* recorder = new RecorderModule();
        if (!fixture.Physics || registry.Register(Container::TUniquePtr<IModule>(recorder)) != recorder
            || !registry.InstallAll(*NorvesLib::Core::Engine::GEngine))
        {
            return false;
        }
        ApplicationFixedStepTestAccess::ResetRun(processor);
        return true;
    }

    void TeardownFixture(ApplicationProcessor& processor)
    {
        ApplicationFixedStepTestAccess::EndRun(processor);
        if (NorvesLib::Core::Engine::GEngine)
        {
            GetModuleRegistry().ShutdownAll(*NorvesLib::Core::Engine::GEngine);
            NorvesLib::Core::Engine::GEngine->GetWorld().SetSceneView(nullptr);
            NorvesLib::Core::Engine::GEngine->GetWorld().Finalize();
            delete NorvesLib::Core::Engine::GEngine;
            NorvesLib::Core::Engine::GEngine = nullptr;
        }
        if (GFixture)
        {
            GFixture->SceneView.Shutdown();
        }
        GFixture = nullptr;
    }

    bool HasStepOrder(const Fixture& fixture, uint32_t stepCount)
    {
        if (fixture.Events.size() != stepCount * 3)
        {
            return false;
        }
        for (uint32_t step = 0; step < stepCount; ++step)
        {
            const uint32_t base = step * 3;
            if (fixture.Events[base] != Container::String("pre")
                || fixture.Events[base + 1] != Container::String("component")
                || fixture.Events[base + 2] != Container::String("post"))
            {
                return false;
            }
        }
        return true;
    }

    bool CreateKinematicFixture(
        Fixture& fixture,
        Entity*& outParent,
        Entity*& outChild,
        ColliderComponent*& outCollider,
        MoveKinematicComponent*& outMover,
        RigidBodyComponent*& outBody)
    {
        World& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        outParent = world.SpawnEntity<Entity>();
        outChild = outParent ? world.SpawnEntity<Entity>(outParent) : nullptr;
        if (!outParent || !outChild)
        {
            return false;
        }
        outParent->SetLocalPosition(1.0f, 0.0f, 0.0f);
        outParent->SetLocalScale(2.0f, 2.0f, 2.0f);
        outChild->SetLocalPosition(1.0f, 0.0f, 0.0f);
        outCollider = world.CreateComponent<ColliderComponent>(outChild);
        outBody = world.CreateComponent<RigidBodyComponent>(outChild);
        outMover = world.CreateComponent<MoveKinematicComponent>(outChild);
        Component::PointLightComponent* light = world.CreateComponent<Component::PointLightComponent>(outChild);
        if (!outCollider || !outBody || !outMover || !light || outCollider->SetSphere(0.25f) != EPhysicsResult::Success
            || outBody->SetBodyType(EPhysicsBodyType::Kinematic) != EPhysicsResult::Success)
        {
            return false;
        }
        world.UpdateWorldTransforms();
        fixture.Physics->FixedTick(kFixedDeltaTime);
        fixture.Events.clear();
        return true;
    }

    bool TestPreComponentPostAndFinalTransform(ApplicationProcessor& processor)
    {
        Fixture& fixture = *GFixture;
        Entity* parent = nullptr;
        Entity* child = nullptr;
        ColliderComponent* collider = nullptr;
        MoveKinematicComponent* mover = nullptr;
        RigidBodyComponent* body = nullptr;
        if (!CreateKinematicFixture(fixture, parent, child, collider, mover, body))
        {
            return false;
        }
        mover->TargetPosition = Math::Vector3(2.0f, 0.0f, 0.0f);
        ApplicationFixedStepTestAccess::TickWithDelta(processor, 17'000'000);
        const bool bSteps = fixture.PreCount == 1 && fixture.PostCount == 1;
        const bool bOrder = HasStepOrder(fixture, 1);
        const bool bPostQuery = fixture.bPostSawQuery;
        const bool bComponent = mover->FixedTickCount == 1;
        const Math::Vector3 velocity = body->GetLinearVelocity();
        const bool bVelocity = std::fabs(velocity.x - 120.0f) <= 0.01f
            && std::fabs(velocity.y) <= 0.01f && std::fabs(velocity.z) <= 0.01f;
        const bool bChildTransform = child->GetWorldTransform().position == Math::Vector3(5.0f, 0.0f, 0.0f);
        const bool bParentTransform = parent->GetWorldTransform().position == Math::Vector3(1.0f, 0.0f, 0.0f);
        const bool bSceneViewPublished = fixture.SceneView.GetLightProxies().size() == 1
            && fixture.SceneView.GetLightProxies()[0].PositionX == 5.0f;
        return bSteps && bOrder && bPostQuery && bComponent && bVelocity
            && bChildTransform && bParentTransform && bSceneViewPublished;
    }

    bool TestPauseAndCatchUp(ApplicationProcessor& processor)
    {
        Fixture& fixture = *GFixture;
        Entity* parent = nullptr;
        Entity* child = nullptr;
        ColliderComponent* collider = nullptr;
        MoveKinematicComponent* mover = nullptr;
        RigidBodyComponent* body = nullptr;
        if (!CreateKinematicFixture(fixture, parent, child, collider, mover, body))
        {
            return false;
        }
        mover->TargetPosition = Math::Vector3(2.0f, 0.0f, 0.0f);
        const FixedStepAdvanceResult paused = ApplicationFixedStepTestAccess::Advance(processor, 1'000'000'000, false);
        const FixedStepAdvanceResult catchUp = ApplicationFixedStepTestAccess::Advance(processor, 51'000'000, true);
        const bool bCatchUpOrder = fixture.Events.size() == 9 && HasStepOrder(fixture, 3);
        fixture.Events.clear();
        const FixedStepAdvanceResult saturated = ApplicationFixedStepTestAccess::Advance(processor, 1'000'000'000, true);
        return paused.Status == EFixedStepAdvanceStatus::Paused && paused.ExecutedSteps == 0
            && paused.DroppedSteps == 0 && paused.RemainderScaledUnits == 0
            && bCatchUpOrder
            && catchUp.Status == EFixedStepAdvanceStatus::Advanced && catchUp.ExecutedSteps == 3
            && catchUp.DroppedSteps == 0 && catchUp.RemainderScaledUnits == 60'000'000
            && saturated.Status == EFixedStepAdvanceStatus::Advanced && saturated.ExecutedSteps == 8
            && saturated.DroppedSteps == 52 && saturated.RemainderScaledUnits == 60'000'000
            && HasStepOrder(fixture, 8) && mover->FixedTickCount == 11
            && body->GetLinearVelocity() == Math::Vector3();
    }

    bool TestDynamicWritebackAndNextStepSnapshot(ApplicationProcessor& processor)
    {
        Fixture& fixture = *GFixture;
        World& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        Entity* entity = world.SpawnEntity<Entity>();
        ColliderComponent* collider = entity ? world.CreateComponent<ColliderComponent>(entity) : nullptr;
        RigidBodyComponent* body = entity ? world.CreateComponent<RigidBodyComponent>(entity) : nullptr;
        if (!entity || !collider || !body || collider->SetSphere(0.25f) != EPhysicsResult::Success
            || body->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success
            || body->SetGravityScale(0.0f) != EPhysicsResult::Success)
        {
            return false;
        }

        fixture.Physics->FixedTick(kFixedDeltaTime);
        if (body->SetLinearVelocity(Math::Vector3(60.0f, 0.0f, 0.0f)) != EPhysicsResult::Success)
        {
            return false;
        }

        ApplicationFixedStepTestAccess::TickWithDelta(processor, 17'000'000);
        Container::VariableArray<PhysicsOverlapHit> firstHits;
        const bool bFirstQuery = NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
            Math::Sphere(Math::Vector3(1.0f, 0.0f, 0.0f), 0.1f), firstHits)
            == EPhysicsSceneQueryResult::Success && firstHits.size() == 1;

        ApplicationFixedStepTestAccess::TickWithDelta(processor, 17'000'000);
        Container::VariableArray<PhysicsOverlapHit> secondHits;
        const bool bSecondQuery = NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
            Math::Sphere(Math::Vector3(2.0f, 0.0f, 0.0f), 0.1f), secondHits)
            == EPhysicsSceneQueryResult::Success && secondHits.size() == 1;
        return fixture.PreCount == 2 && fixture.PostCount == 2
            && PhysicsModuleTestAccess::GetPreStepPosition(*fixture.Physics, body->GetBodyHandle())
                == Math::Vector3(1.0f, 0.0f, 0.0f)
            && entity->GetWorldTransform().position == Math::Vector3(2.0f, 0.0f, 0.0f)
            && body->GetLinearVelocity() == Math::Vector3(60.0f, 0.0f, 0.0f)
            && bFirstQuery && bSecondQuery;
    }

    bool TestTickDisabledActiveRemainsQueryable(ApplicationProcessor& processor)
    {
        Fixture& fixture = *GFixture;
        Entity* parent = nullptr;
        Entity* child = nullptr;
        ColliderComponent* collider = nullptr;
        MoveKinematicComponent* mover = nullptr;
        RigidBodyComponent* body = nullptr;
        if (!CreateKinematicFixture(fixture, parent, child, collider, mover, body))
        {
            return false;
        }
        child->SetTickEnabled(false);
        mover->TargetPosition = Math::Vector3(2.0f, 0.0f, 0.0f);
        ApplicationFixedStepTestAccess::TickWithDelta(processor, 17'000'000);
        Container::VariableArray<PhysicsOverlapHit> hits;
        return fixture.PreCount == 1 && fixture.PostCount == 1 && mover->FixedTickCount == 0
            && PhysicsModuleTestAccess::IsColliderActive(*fixture.Physics, collider->GetColliderHandle())
            && PhysicsModuleTestAccess::IsBodyActive(*fixture.Physics, body->GetBodyHandle())
            && NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
                Math::Sphere(Math::Vector3(3.0f, 0.0f, 0.0f), 0.1f), hits)
                == EPhysicsSceneQueryResult::Success && hits.size() == 1 && body->GetBodyHandle().IsValid();
    }

    bool TestInactiveDisabledPendingAndInvalidTransformAreExcluded(ApplicationProcessor& processor)
    {
        Fixture& fixture = *GFixture;
        World& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        auto createCollider = [&world](float positionX) -> ColliderComponent*
        {
            Entity* entity = world.SpawnEntity<Entity>();
            ColliderComponent* collider = entity ? world.CreateComponent<ColliderComponent>(entity) : nullptr;
            if (!entity || !collider || collider->SetSphere(0.25f) != EPhysicsResult::Success)
            {
                return nullptr;
            }
            entity->SetLocalPosition(positionX, 0.0f, 0.0f);
            return collider;
        };

        ColliderComponent* inactiveCollider = createCollider(2.0f);
        ColliderComponent* disabledCollider = createCollider(4.0f);
        ColliderComponent* pendingCollider = createCollider(6.0f);
        ColliderComponent* invalidCollider = createCollider(8.0f);
        if (!inactiveCollider || !disabledCollider || !pendingCollider || !invalidCollider)
        {
            return false;
        }

        fixture.Physics->FixedTick(kFixedDeltaTime);
        const ColliderHandle inactiveHandle = inactiveCollider->GetColliderHandle();
        const ColliderHandle disabledHandle = disabledCollider->GetColliderHandle();
        const ColliderHandle pendingHandle = pendingCollider->GetColliderHandle();
        const ColliderHandle invalidHandle = invalidCollider->GetColliderHandle();
        inactiveCollider->GetOwner()->SetActive(false);
        disabledCollider->Disable();
        pendingCollider->GetOwner()->MarkForDestroy();
        invalidCollider->GetOwner()->SetLocalScale(std::numeric_limits<float>::infinity(), 1.0f, 1.0f);

        ApplicationFixedStepTestAccess::TickWithDelta(processor, 17'000'000);
        const float excludedPositions[] = {2.0f, 4.0f, 6.0f, 8.0f};
        for (float positionX : excludedPositions)
        {
            Container::VariableArray<PhysicsOverlapHit> hits;
            const EPhysicsSceneQueryResult queryResult = NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
                Math::Sphere(Math::Vector3(positionX, 0.0f, 0.0f), 0.1f), hits);
            if (queryResult != EPhysicsSceneQueryResult::NoHit || !hits.empty())
            {
                return false;
            }
        }
        return fixture.PreCount == 1 && fixture.PostCount == 1
            && PhysicsModuleTestAccess::IsColliderAlive(*fixture.Physics, inactiveHandle)
            && !PhysicsModuleTestAccess::IsColliderActive(*fixture.Physics, inactiveHandle)
            && PhysicsModuleTestAccess::IsColliderAlive(*fixture.Physics, disabledHandle)
            && !PhysicsModuleTestAccess::IsColliderActive(*fixture.Physics, disabledHandle)
            && PhysicsModuleTestAccess::IsColliderAlive(*fixture.Physics, invalidHandle)
            && !PhysicsModuleTestAccess::IsColliderActive(*fixture.Physics, invalidHandle)
            && !PhysicsModuleTestAccess::IsColliderAlive(*fixture.Physics, pendingHandle);
    }

    bool TestSceneQueryRebuildUsesFinalFixedStepTransform(ApplicationProcessor& processor)
    {
        Fixture& fixture = *GFixture;
        World& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        Entity* entity = world.SpawnEntity<Entity>();
        Component::MeshComponent* mesh = entity ? world.CreateComponent<Component::MeshComponent>(entity) : nullptr;
        MoveSceneQueryComponent* mover = entity ? world.CreateComponent<MoveSceneQueryComponent>(entity) : nullptr;
        if (!entity || !mesh || !mover)
        {
            return false;
        }

        SceneQuery& sceneQuery = NorvesLib::Core::Engine::GEngine->GetSceneQuery();
        sceneQuery.Clear();
        if (sceneQuery.GetEntryCount() != 0)
        {
            return false;
        }

        ApplicationFixedStepTestAccess::TickWithDelta(processor, 51'000'000);
        Container::VariableArray<Entity*> stepOneEntities;
        Container::VariableArray<Entity*> finalStepEntities;
        sceneQuery.OverlapSphere(Math::Sphere(Math::Vector3(3.5f, 0.0f, 0.0f), 0.1f), stepOneEntities);
        sceneQuery.OverlapSphere(Math::Sphere(Math::Vector3(10.5f, 0.0f, 0.0f), 0.1f), finalStepEntities);
        return fixture.PreCount == 3 && fixture.PostCount == 3 && mover->FixedTickCount == 3
            && entity->GetWorldTransform().position == Math::Vector3(10.5f, 0.0f, 0.0f)
            && sceneQuery.GetEntryCount() == 1 && stepOneEntities.empty()
            && finalStepEntities.size() == 1 && finalStepEntities[0] == entity;
    }

    bool AreEqual(const Math::GeometryContact& left, const Math::GeometryContact& right)
    {
        return left.Normal == right.Normal && left.Depth == right.Depth && left.Point == right.Point;
    }

    bool AreEqual(const PhysicsOverlapHit& left, const PhysicsOverlapHit& right)
    {
        return left.Collider == right.Collider && left.Body == right.Body && left.Entity == right.Entity
            && left.bHasEntity == right.bHasEntity && AreEqual(left.Contact, right.Contact);
    }

    bool AreEqual(
        const Container::VariableArray<PhysicsOverlapHit>& left,
        const Container::VariableArray<PhysicsOverlapHit>& right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (size_t index = 0; index < left.size(); ++index)
        {
            if (!AreEqual(left[index], right[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool TestWrongThreadDoesNotAdvancePhysics(ApplicationProcessor& processor)
    {
        Fixture& fixture = *GFixture;
        Entity* parent = nullptr;
        Entity* child = nullptr;
        ColliderComponent* collider = nullptr;
        MoveKinematicComponent* mover = nullptr;
        RigidBodyComponent* body = nullptr;
        if (!CreateKinematicFixture(fixture, parent, child, collider, mover, body))
        {
            return false;
        }

        World& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        Entity* triggerEntity = world.SpawnEntity<Entity>();
        ColliderComponent* triggerCollider = triggerEntity ? world.CreateComponent<ColliderComponent>(triggerEntity) : nullptr;
        if (!triggerEntity || !triggerCollider || triggerCollider->SetSphere(0.25f) != EPhysicsResult::Success
            || triggerCollider->SetTrigger(true) != EPhysicsResult::Success)
        {
            return false;
        }
        triggerEntity->SetLocalPosition(3.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        fixture.Physics->FixedTick(kFixedDeltaTime);

        const Math::Transform localTransformBefore = child->GetLocalTransform();
        const Math::Transform worldTransformBefore = child->GetWorldTransform();
        const Math::Vector3 velocityBefore = body->GetLinearVelocity();
        const Math::Vector3 preStepPositionBefore = PhysicsModuleTestAccess::GetPreStepPosition(
            *fixture.Physics, body->GetBodyHandle());
        const Math::Vector3 pendingImpulseBefore = PhysicsModuleTestAccess::GetPendingImpulse(
            *fixture.Physics, body->GetBodyHandle());
        const bool bHadPreStepSnapshotBefore = PhysicsModuleTestAccess::HasPreStepSnapshot(
            *fixture.Physics, body->GetBodyHandle());
        const bool bColliderActiveBefore = PhysicsModuleTestAccess::IsColliderActive(
            *fixture.Physics, collider->GetColliderHandle());
        const bool bBodyActiveBefore = PhysicsModuleTestAccess::IsBodyActive(
            *fixture.Physics, body->GetBodyHandle());
        const uint32_t colliderGenerationBefore = PhysicsModuleTestAccess::GetColliderGeneration(
            *fixture.Physics, collider->GetColliderHandle());
        const uint32_t bodyGenerationBefore = PhysicsModuleTestAccess::GetBodyGeneration(
            *fixture.Physics, body->GetBodyHandle());
        const uint32_t previousPairsBefore = PhysicsModuleTestAccess::GetPreviousPairCount(*fixture.Physics);
        const uint32_t currentPairsBefore = PhysicsModuleTestAccess::GetCurrentPairCount(*fixture.Physics);
        const uint32_t dispatchedBefore = PhysicsModuleTestAccess::GetDispatchedEventCount(*fixture.Physics);
        const uint32_t pendingEventsBefore = PhysicsModuleTestAccess::GetPendingEventCount(*fixture.Physics);
        const bool bPublishedBefore = PhysicsModuleTestAccess::HasPublishedSnapshot(*fixture.Physics);
        Container::VariableArray<PhysicsOverlapHit> hitsBefore;
        const EPhysicsSceneQueryResult queryBefore = NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
            Math::Sphere(Math::Vector3(3.0f, 0.0f, 0.0f), 1.0f), hitsBefore);
        Container::VariableArray<PhysicsOverlapHit> radiusBefore;
        const EPhysicsSceneQueryResult radiusQueryBefore = NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
            Math::Sphere(Math::Vector3(3.6f, 0.0f, 0.0f), 0.05f), radiusBefore);
        EPhysicsSceneQueryResult workerQuery = EPhysicsSceneQueryResult::Success;
        EPhysicsResult workerImpulse = EPhysicsResult::Success;
        EPhysicsResult workerColliderMutation = EPhysicsResult::Success;
        Thread::Thread worker([&fixture, &workerQuery, &workerImpulse, &workerColliderMutation, body, collider]()
        {
            fixture.Physics->PreFixedTick(kFixedDeltaTime);
            fixture.Physics->FixedTick(kFixedDeltaTime);
            workerImpulse = body->SetLinearVelocity(Math::Vector3(7.0f, 0.0f, 0.0f));
            workerColliderMutation = collider->SetSphere(1.0f);
            PhysicsRaycastHit hit;
            workerQuery = NorvesLib::Core::Engine::GEngine->GetSceneQuery().Raycast(
                Math::Ray(Math::Vector3(-3.0f, 0.0f, 0.0f), Math::Vector3::UnitX), 10.0f, hit);
        });
        worker.Join();
        Container::VariableArray<PhysicsOverlapHit> hitsAfter;
        const EPhysicsSceneQueryResult queryAfter = NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
            Math::Sphere(Math::Vector3(3.0f, 0.0f, 0.0f), 1.0f), hitsAfter);
        Container::VariableArray<PhysicsOverlapHit> radiusAfter;
        const EPhysicsSceneQueryResult radiusQueryAfter = NorvesLib::Core::Engine::GEngine->GetSceneQuery().OverlapSphere(
            Math::Sphere(Math::Vector3(3.6f, 0.0f, 0.0f), 0.05f), radiusAfter);
        const bool bPairOrEventObserved = previousPairsBefore != 0 || currentPairsBefore != 0 || dispatchedBefore != 0;
        const bool bUnchanged = workerQuery == EPhysicsSceneQueryResult::WrongThread
            && workerImpulse == EPhysicsResult::WrongThread
            && workerColliderMutation == EPhysicsResult::WrongThread
            && child->GetLocalTransform() == localTransformBefore
            && child->GetWorldTransform() == worldTransformBefore
            && body->GetLinearVelocity() == velocityBefore
            && PhysicsModuleTestAccess::GetPreStepPosition(*fixture.Physics, body->GetBodyHandle()) == preStepPositionBefore
            && PhysicsModuleTestAccess::GetPendingImpulse(*fixture.Physics, body->GetBodyHandle()) == pendingImpulseBefore
            && PhysicsModuleTestAccess::HasPreStepSnapshot(*fixture.Physics, body->GetBodyHandle()) == bHadPreStepSnapshotBefore
            && PhysicsModuleTestAccess::IsColliderActive(*fixture.Physics, collider->GetColliderHandle()) == bColliderActiveBefore
            && PhysicsModuleTestAccess::IsBodyActive(*fixture.Physics, body->GetBodyHandle()) == bBodyActiveBefore
            && PhysicsModuleTestAccess::GetColliderGeneration(*fixture.Physics, collider->GetColliderHandle()) == colliderGenerationBefore
            && PhysicsModuleTestAccess::GetBodyGeneration(*fixture.Physics, body->GetBodyHandle()) == bodyGenerationBefore
            && PhysicsModuleTestAccess::GetPreviousPairCount(*fixture.Physics) == previousPairsBefore
            && PhysicsModuleTestAccess::GetCurrentPairCount(*fixture.Physics) == currentPairsBefore
            && PhysicsModuleTestAccess::GetDispatchedEventCount(*fixture.Physics) == dispatchedBefore
            && PhysicsModuleTestAccess::GetPendingEventCount(*fixture.Physics) == pendingEventsBefore
            && PhysicsModuleTestAccess::HasPublishedSnapshot(*fixture.Physics) == bPublishedBefore
            && queryBefore == EPhysicsSceneQueryResult::Success && queryAfter == queryBefore
            && AreEqual(hitsAfter, hitsBefore)
            && radiusQueryBefore == EPhysicsSceneQueryResult::NoHit && radiusBefore.empty()
            && radiusQueryAfter == EPhysicsSceneQueryResult::NoHit && radiusAfter.empty()
            && bPairOrEventObserved && mover->FixedTickCount == 0;
        return bUnchanged;
    }

    bool TestFramePacketBoundaryHasNoLivePhysicsPointers()
    {
        return !std::is_standard_layout_v<FramePacketType> && !std::is_standard_layout_v<SceneProxyType>
            && sizeof(FramePacketType) == 720 && alignof(FramePacketType) == 8
            && sizeof(SceneProxyType) == 320 && alignof(SceneProxyType) == 8;
    }

    bool RunCase(uint32_t caseIndex, ApplicationProcessor& processor)
    {
        switch (caseIndex)
        {
        case 0:
            return TestPreComponentPostAndFinalTransform(processor);
        case 1:
            return TestDynamicWritebackAndNextStepSnapshot(processor);
        case 2:
            return TestPauseAndCatchUp(processor);
        case 3:
            return TestTickDisabledActiveRemainsQueryable(processor);
        case 4:
            return TestInactiveDisabledPendingAndInvalidTransformAreExcluded(processor);
        case 5:
            return TestWrongThreadDoesNotAdvancePhysics(processor);
        case 6:
            return TestFramePacketBoundaryHasNoLivePhysicsPointers();
        case 7:
            return TestSceneQueryRebuildUsesFinalFixedStepTransform(processor);
        default:
            return false;
        }
    }

    bool RunChild(uint32_t caseIndex)
    {
        Fixture fixture;
        ApplicationProcessor processor;
        if (!SetupFixture(fixture, processor))
        {
            TeardownFixture(processor);
            return false;
        }
        const bool bPassed = RunCase(caseIndex, processor);
        TeardownFixture(processor);
        return bPassed;
    }

    bool RunChildProcess(uint32_t caseIndex)
    {
        wchar_t executablePath[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, executablePath, MAX_PATH) == 0)
        {
            return false;
        }
        wchar_t commandLine[MAX_PATH + 64]{};
        if (swprintf_s(commandLine, L"\"%s\" --child=%u", executablePath, caseIndex) <= 0)
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
        constexpr DWORD ChildProcessTimeoutMilliseconds = 60'000;
        const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, ChildProcessTimeoutMilliseconds);
        if (waitResult == WAIT_TIMEOUT)
        {
            TerminateProcess(processInfo.hProcess, EXIT_FAILURE);
            WaitForSingleObject(processInfo.hProcess, ChildProcessTimeoutMilliseconds);
        }
        DWORD exitCode = EXIT_FAILURE;
        const bool bReadExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return waitResult == WAIT_OBJECT_0 && bReadExitCode && exitCode == EXIT_SUCCESS;
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
} // namespace

int main(int argumentCount, char** arguments)
{
    if (argumentCount == 2)
    {
        uint32_t caseIndex = 0;
        return TryParseChildCase(arguments[1], caseIndex) && RunChild(caseIndex) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argumentCount != 1)
    {
        return EXIT_FAILURE;
    }
    bool bPassed = true;
    for (uint32_t caseIndex = 0; caseIndex < kCaseCount; ++caseIndex)
    {
        for (uint32_t repetition = 0; repetition < kRepetitionsPerCase; ++repetition)
        {
            bPassed &= RunChildProcess(caseIndex);
        }
    }
    std::cout << (bPassed ? "PhysicsFixedStepPipelineTest passed\n" : "PhysicsFixedStepPipelineTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
