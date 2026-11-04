// PhysicsFixedStepPipelineTest — M7 fixed-step dispatch と Physics post-step の契約を検証する。

#include "Asset/AssetFileReader.h"
#include "Animation/SkeletalAssetResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/Component.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Ecs/EcsScheduler.h"
#include "Engine/ApplicationProcessor.h"
#include "Engine/Engine.h"
#include "Engine/FixedStepScheduler.h"
#include "Game/Gameplay/Camera/FollowCameraComponent.h"
#include "Game/Gameplay/Locomotion/QuadrupedLocomotionComponent.h"
#include "Input/InputRouter.h"
#include "Input/InputSystem.h"
#include "Locomotion/QuadrupedLocomotionJson.h"
#include "Logging/Logger.h"
#include "Module/ModuleRegistry.h"
#include "Object/ObjectHeap.h"
#include "Object/Resource.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "Physics/CharacterBodyComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include "PhysicsModuleTestAccess.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneView.h"
#include "Scene/SceneQuery.h"
#include "Thread/Thread.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

        static bool SetRate(ApplicationProcessor& processor, uint32_t rate)
        {
            processor.m_FixedStepScheduler->EndRun();
            if (!processor.m_FixedStepScheduler->SetRate(rate))
                return false;
            processor.m_FixedStepScheduler->BeginRun();
            return true;
        }
        static FixedStepAdvanceResult Simulate(ApplicationProcessor& processor, int64_t raw)
        {
            return processor.TickSimulation(raw, processor.ClampVariableDeltaTime(raw), true, nullptr);
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
    static_assert(!std::is_standard_layout_v<FramePacketType>);
    static_assert(!std::is_standard_layout_v<SceneProxyType>);

    constexpr uint32_t kCaseCount = 32;
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

    // 固定ステップの段 FixedTick で呼ぶ試験の本体。段のシステム（RunFixedProbes）が Entity の深さ優先の順に呼ぶ。
    // システムは物理モジュールより先に登録するので、段の中では物理の要求・一歩より前に走る
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
            for (Component::Component* component : entity.GetComponents())
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

    bool RegisterFixedProbeSystem(World& world)
    {
        Ecs::SystemScheduler& scheduler = world.GetScheduler();
        Ecs::SystemDesc desc;
        desc.Name = "Test.FixedProbes";
        desc.Stage = scheduler.FindStage("FixedTick");
        desc.Thread = ThreadPolicy::GameThreadOnly;
        desc.Run = &RunFixedProbes;
        desc.User = &world;
        return scheduler.RegisterSystem(desc) != Ecs::kInvalidEcsSystem;
    }

    class MoveKinematicComponent final : public Component::Component, public IFixedProbe
    {
    public:
        void OnFixed(float /*fixedDeltaTime*/) override
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

    class MoveSceneQueryComponent final : public Component::Component, public IFixedProbe
    {
    public:
        void OnFixed(float /*fixedDeltaTime*/) override
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
        if (!RegisterFixedProbeSystem(NorvesLib::Core::Engine::GEngine->GetWorld()))
        {
            return false;
        }
        // Game の起動と同じく、Game の Component の本体を順序付きの系へ登録する
        Game::Gameplay::FollowCameraComponent::RegisterTick(NorvesLib::Core::Engine::GEngine->GetWorld());
        Game::Gameplay::QuadrupedLocomotionComponent::RegisterTick(NorvesLib::Core::Engine::GEngine->GetWorld());
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
        fixture.Physics->StepFixed(kFixedDeltaTime);
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

        fixture.Physics->StepFixed(kFixedDeltaTime);
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

        fixture.Physics->StepFixed(kFixedDeltaTime);
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
        fixture.Physics->StepFixed(kFixedDeltaTime);

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
            fixture.Physics->StepPreFixed(kFixedDeltaTime);
            fixture.Physics->StepFixed(kFixedDeltaTime);
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
        return !std::is_standard_layout_v<FramePacketType> && !std::is_standard_layout_v<SceneProxyType>;
    }

    struct CharacterFixture
    {
        Entity* Owner = nullptr;
        CharacterBodyComponent* Character = nullptr;
        ColliderComponent* Collider = nullptr;
        RigidBodyComponent* Body = nullptr;
    };
    bool CreateCharacter(CharacterFixture& value, const Math::Vector3& position)
    {
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        value.Owner = world.SpawnEntity<Entity>();
        if (!value.Owner)
            return false;
        value.Owner->SetPosition(position);
        // 依存先より先に作っても、最初の固定stepで束縛できる。
        value.Character = world.CreateComponent<CharacterBodyComponent>(value.Owner);
        value.Collider = world.CreateComponent<ColliderComponent>(value.Owner);
        value.Body = world.CreateComponent<RigidBodyComponent>(value.Owner);
        return value.Character && value.Collider && value.Body &&
               value.Collider->SetCapsule(.3f, .1f) == EPhysicsResult::Success &&
               value.Collider->SetLocalPose(Math::Transform(Math::Vector3(0, .4f, 0))) == EPhysicsResult::Success &&
               value.Body->SetBodyType(EPhysicsBodyType::Kinematic) == EPhysicsResult::Success;
    }
    Entity* CreateCharacterFloor(float extent = 100)
    {
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        auto* floor = world.SpawnEntity<Entity>();
        if (!floor)
            return nullptr;
        floor->SetPosition(Math::Vector3(0, -.5f, 0));
        auto* collider = world.CreateComponent<ColliderComponent>(floor);
        if (!collider || collider->SetBox(Math::Vector3(extent, .5f, extent)) != EPhysicsResult::Success)
            return nullptr;
        return floor;
    }
    bool StepCharacter(ApplicationProcessor& processor)
    {
        return ApplicationFixedStepTestAccess::Advance(processor, 16'666'667, true).ExecutedSteps == 1;
    }
    class CharacterPlatformTick final : public Component::Component, public IFixedProbe
    {
      public:
        Math::Transform Target;
        void OnFixed(float) override
        {
            GetOwner()->SetWorldTransform(Target);
        }
    };
    class CharacterTeleportTick final : public Component::Component, public IFixedProbe
    {
      public:
        CharacterBodyComponent* Target = nullptr;
        bool bDone = false;
        void OnFixed(float) override
        {
            if (!bDone && Target)
            {
                bDone = Target->Teleport(Math::Vector3(20, 0, 0)) == EPhysicsResult::Success;
            }
        }
    };
    bool TestCharacterQueuedPlatform(ApplicationProcessor& processor)
    {
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        CharacterFixture value;
        if (!CreateCharacter(value, Math::Vector3(1, 0, 0)))
            return false;
        // 足場のFixedTickはcharacterより後でも、そのstepの運搬へ反映する。
        Entity* floor = CreateCharacterFloor(4);
        auto* body = floor ? world.CreateComponent<RigidBodyComponent>(floor) : nullptr;
        auto* mover = floor ? world.CreateComponent<CharacterPlatformTick>(floor) : nullptr;
        if (!body || !mover || body->SetBodyType(EPhysicsBodyType::Kinematic) != EPhysicsResult::Success)
            return false;
        mover->Target = Math::Transform(Math::Vector3(0, -.5f, 0));
        if (!StepCharacter(processor) || !value.Character->GetState().bGrounded)
            return false;
        for (unsigned frame = 1; frame <= 120; ++frame)
        {
            const float angle = frame * .01f, shift = std::sin(frame * .02f);
            mover->Target =
                Math::Transform(Math::Vector3(shift, -.5f, 0), Math::Quaternion(Math::Vector3::UnitY, angle));
            const auto previous = value.Owner->GetPosition();
            if (!StepCharacter(processor))
                return false;
            const auto actual = value.Owner->GetPosition();
            const auto velocity = (actual - previous) / kFixedDeltaTime;
            const auto& state = value.Character->GetState();
            if (!state.bReady || !state.bGrounded || std::fabs(actual.x - (shift + std::cos(angle))) > .02f ||
                std::fabs(actual.z + std::sin(angle)) > .02f ||
                (value.Body->GetLinearVelocity() - velocity).Length() > .005f)
                return false;
        }
        return true;
    }
    bool TestCharacterTeleportAndLifecycle(ApplicationProcessor& processor)
    {
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero))
            return false;
        unsigned landed = 0;
        bool published = true;
        value.Character->OnLanded.Add([&](const CharacterBodyState&) {
            ++landed;
            PhysicsShapeProxy proxy;
            published = published &&
                        PhysicsModuleTestAccess::CopyPublishedProxy(*GFixture->Physics,
                                                                    value.Collider->GetColliderHandle(), proxy) &&
                        std::fabs(proxy.Capsule.PointA.x - value.Owner->GetPosition().x) < 1e-4f;
        });
        if (!StepCharacter(processor) || landed != 1 || !published)
            return false;
        auto* teleporter = world.CreateComponent<CharacterTeleportTick>(value.Owner);
        if (!teleporter)
            return false;
        teleporter->Target = value.Character;
        if (!StepCharacter(processor) || !teleporter->bDone || std::fabs(value.Owner->GetPosition().x - 20) > 1e-4f ||
            value.Body->GetLinearVelocity().Length() > .001f)
            return false;
        const auto oldBody = value.Body->GetBodyHandle();
        value.Owner->RemoveComponent(value.Body);
        value.Body = nullptr;
        if (value.Character->GetState().bReady || value.Character->GetState().bGrounded ||
            value.Character->Jump(5) != EPhysicsResult::InvalidState)
            return false;
        if (value.Character->Teleport(Math::Vector3(3, 0, 0)) != EPhysicsResult::Success)
            return false;
        value.Body = world.CreateComponent<RigidBodyComponent>(value.Owner);
        if (!value.Body || value.Body->SetBodyType(EPhysicsBodyType::Kinematic) != EPhysicsResult::Success ||
            value.Body->GetBodyHandle() == oldBody || !StepCharacter(processor) ||
            std::fabs(value.Owner->GetPosition().x - 3) > 1e-4f)
            return false;
        value.Collider->Disable();
        if (value.Character->Teleport(Math::Vector3(6, 0, 0)) != EPhysicsResult::Success || !StepCharacter(processor) ||
            std::fabs(value.Owner->GetPosition().x - 3) > 1e-4f)
            return false;
        value.Collider->Enable();
        if (!StepCharacter(processor) || std::fabs(value.Owner->GetPosition().x - 6) > 1e-4f)
            return false;
        value.Collider->Disable();
        if (value.Character->MoveDelta(Math::Vector3(1, 0, 0)) != EPhysicsResult::Success || !StepCharacter(processor))
            return false;
        value.Collider->Enable();
        if (!StepCharacter(processor) || std::fabs(value.Owner->GetPosition().x - 7) > 1e-4f ||
            std::fabs(value.Body->GetLinearVelocity().x - 60) > .01f)
            return false;
        return published && value.Character->GetState().bReady;
    }
    bool TestCharacterLandedRemoval(ApplicationProcessor& processor)
    {
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero))
            return false;
        unsigned landed = 0;
        value.Character->OnLanded.Add([&](const CharacterBodyState&) {
            ++landed;
            value.Owner->RemoveComponent(value.Character);
            value.Character = nullptr;
        });
        if (!StepCharacter(processor) || landed != 1 || value.Owner->GetComponent<CharacterBodyComponent>())
            return false;
        return StepCharacter(processor) && landed == 1;
    }

    bool TestCharacterDriveEquality(ApplicationProcessor& processor)
    {
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        auto* floor = CreateCharacterFloor();
        CharacterFixture fixed, variable;
        if (!floor || !CreateCharacter(fixed, Math::Vector3::Zero) || !CreateCharacter(variable, Math::Vector3::Zero))
            return false;
        if (floor->GetComponent<ColliderComponent>()->SetCollisionLayer(4) != EPhysicsResult::Success ||
            fixed.Collider->SetCollisionLayer(1) != EPhysicsResult::Success ||
            variable.Collider->SetCollisionLayer(2) != EPhysicsResult::Success ||
            fixed.Collider->SetCollisionMask(4) != EPhysicsResult::Success ||
            variable.Collider->SetCollisionMask(4) != EPhysicsResult::Success ||
            variable.Character->SetDriveMode(CharacterDriveMode::Variable) != EPhysicsResult::Success)
            return false;
        auto* wall = world.SpawnEntity<Entity>();
        auto* wallCollider = wall ? world.CreateComponent<ColliderComponent>(wall) : nullptr;
        if (!wallCollider || wallCollider->SetBox(Math::Vector3(.05f, .8f, 1)) != EPhysicsResult::Success ||
            wallCollider->SetCollisionLayer(4) != EPhysicsResult::Success)
            return false;
        wall->SetPosition(Math::Vector3(3, .8f, 0));
        unsigned frame = 0, fixedCalls = 0, variableCalls = 0;
        bool commands = true;
        const auto drive = [&](CharacterBodyComponent* character, float dt) {
            commands = commands && character->SetDesiredVelocity(Math::Vector3(2, 0, 0)) == EPhysicsResult::Success;
            commands = commands && character->MoveDelta(Math::Vector3::Zero, .05f * dt) == EPhysicsResult::Success;
            if (frame == 30)
                commands = commands && character->MoveDelta(Math::Vector3(.2f, 0, 0), .1f) == EPhysicsResult::Success;
            if (frame == 60 || frame == 61)
                commands = commands && character->LaunchVertical(frame == 60 ? 4.f : 2.f) == EPhysicsResult::Success;
            if (frame == 120)
            {
                commands = commands && character->Teleport(Math::Vector3(-5, 0, 0)) == EPhysicsResult::Success;
                commands = commands && character->MoveDelta(Math::Vector3(.5f, 0, 0)) == EPhysicsResult::Success;
            }
        };
        fixed.Character->BeforeSimulation.Add([&](float dt) {
            ++fixedCalls;
            drive(fixed.Character, dt);
        });
        variable.Character->BeforeSimulation.Add([&](float dt) {
            ++variableCalls;
            drive(variable.Character, dt);
        });
        for (frame = 0; frame < 240; ++frame)
        {
            world.Tick(kFixedDeltaTime);
            if (!StepCharacter(processor) || !commands)
                return false;
            if ((fixed.Owner->GetPosition() - variable.Owner->GetPosition()).Length() > 1e-5f ||
                fixed.Character->GetState().bGrounded != variable.Character->GetState().bGrounded ||
                fixed.Character->GetState().StepSerial != variable.Character->GetState().StepSerial)
                return false;
        }
        if (fixedCalls != 240 || variableCalls != 240)
            return false;
        const auto before = variable.Owner->GetPosition();
        const auto serial = variable.Character->GetState().StepSerial;
        const auto observed = variable.Character->GetState().Velocity;
        if (ApplicationFixedStepTestAccess::Advance(processor, 1'000'000, true).ExecutedSteps != 0 ||
            ApplicationFixedStepTestAccess::Advance(processor, 34'000'000, true).ExecutedSteps != 2)
            return false;
        return variable.Owner->GetPosition() == before && variable.Character->GetState().StepSerial == serial &&
               variable.Character->GetState().Velocity == observed && variableCalls == 240;
    }
    bool TestCharacterBeforeSimulationRemoval(ApplicationProcessor& processor)
    {
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero) ||
            value.Character->SetDriveMode(CharacterDriveMode::Variable) != EPhysicsResult::Success)
            return false;
        unsigned calls = 0;
        auto* character = value.Character;
        character->BeforeSimulation.Add([&](float) {
            ++calls;
            value.Owner->RemoveComponent(character);
            value.Character = nullptr;
        });
        world.Tick(kFixedDeltaTime);
        if (calls != 1 || value.Character || value.Owner->GetComponent<CharacterBodyComponent>())
            return false;
        world.Tick(kFixedDeltaTime);
        // 収集を開いたまま作った Component は次のフレームから参加する。後から作る replacement を固定ステップに参加させるため、ここでフレームを閉じる
        world.LateTick(kFixedDeltaTime);
        auto* replacement = world.CreateComponent<CharacterBodyComponent>(value.Owner);
        if (!replacement)
            return false;
        bool guarded = true;
        replacement->BeforeSimulation.Add([&](float dt) {
            const auto serial = replacement->GetState().StepSerial;
            GFixture->Physics->StepPreFixed(dt);
            GFixture->Physics->StepFixed(dt);
            guarded = guarded && replacement->GetState().StepSerial == serial;
        });
        return StepCharacter(processor) && calls == 1 && guarded && replacement->GetState().bReady &&
               replacement->GetState().StepSerial == 1;
    }

    // フレームの Tick の後に足した CharacterBody は、同じフレームの固定ステップでは要求を出さず、次の収集から加わる
    bool TestCharacterAddedAfterTickJoinsNextCollection(ApplicationProcessor& processor)
    {
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        if (!CreateCharacterFloor())
            return false;
        world.Tick(kFixedDeltaTime);
        CharacterFixture value;
        if (!CreateCharacter(value, Math::Vector3::Zero))
            return false;
        unsigned calls = 0;
        value.Character->BeforeSimulation.Add([&](float) { ++calls; });
        if (!StepCharacter(processor) || !StepCharacter(processor) || calls != 0)
            return false;
        world.LateTick(kFixedDeltaTime);
        world.Tick(kFixedDeltaTime);
        if (!StepCharacter(processor) || calls != 1)
            return false;
        world.LateTick(kFixedDeltaTime);
        // 収集の外で足したものは、次に走る固定ステップ（自分で収集を開く）から加わる
        CharacterFixture outside;
        if (!CreateCharacter(outside, Math::Vector3(3, 0, 0)))
            return false;
        unsigned outsideCalls = 0;
        outside.Character->BeforeSimulation.Add([&](float) { ++outsideCalls; });
        return StepCharacter(processor) && outsideCalls == 1 && calls == 2;
    }

    bool TestGameLocomotionInput(ApplicationProcessor& processor)
    {
        using namespace Input;
        using namespace NorvesLib::Core::literals;
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero))
            return false;
        InputSystem source;
        InputRouter routes;
        source.SetRouter(&routes);
        InputMapper mapper(source.GetState());
        InputBindingSet bindings;
        InputActionDefinition jump;
        jump.Id = "Jump"_id;
        InputBinding key;
        key.Source = {EInputBindingSource::Key, static_cast<uint16_t>(KeyCode::Space), 0};
        jump.Bindings = {key};
        InputActionDefinition move;
        move.Id = "Move"_id;
        move.Settings.Type = EInputMappingValueType::Axis2D;
        key.Source.Code = static_cast<uint16_t>(KeyCode::W);
        key.Component = EInputAxisComponent::Y;
        move.Bindings = {key};
        if (!bindings.AddContext("Gameplay"_id, ECursorMode::Normal) || !bindings.AddAction("Gameplay"_id, jump) ||
            !bindings.AddAction("Gameplay"_id, move) || !mapper.ConfigureWithContext(bindings, "Gameplay"_id))
            return false;
        mapper.Attach(routes);
        auto* driver = world.CreateComponent<Game::Gameplay::QuadrupedLocomotionComponent>(value.Owner);
        auto* visual = world.SpawnEntity<Entity>(value.Owner);
        auto* pose = visual ? world.SpawnEntity<Entity>(visual) : nullptr;
        if (!driver || !visual || !pose)
            return false;
        // 途中で失敗してもMapperより先に非所有接続を解除する。
        struct Unbind
        {
            Game::Gameplay::QuadrupedLocomotionComponent* Driver;
            ~Unbind()
            {
                Driver->BindInput(nullptr);
            }
        } unbind{driver};
        if (!driver->SetVisualRoots(visual, pose))
            return false;
        driver->BindInput(&mapper);
        mapper.BeginFrame(0);
        mapper.Update(0, 0);
        world.Tick(kFixedDeltaTime);
        if (!StepCharacter(processor) || !value.Character->GetState().bReady)
            return false;
        const auto pressRelease = [&] {
            source.InjectKeyEvent(KeyCode::Space, InputAction::Pressed);
            source.InjectKeyEvent(KeyCode::Space, InputAction::Released);
        };
        pressRelease();
        world.Tick(kFixedDeltaTime);
        if (driver->GetConsumedJumpEventCount() != 0 ||
            ApplicationFixedStepTestAccess::Advance(processor, 1'000'000, true).ExecutedSteps != 0)
            return false;
        world.Tick(kFixedDeltaTime);
        if (!StepCharacter(processor) || driver->GetConsumedJumpEventCount() != 2 ||
            value.Character->GetState().Velocity.y <= 0)
            return false;
        if (ApplicationFixedStepTestAccess::Advance(processor, 34'000'000, true).ExecutedSteps != 2 ||
            driver->GetConsumedJumpEventCount() != 2)
            return false;
        // 一旦取り込んだ入力も、0固定frame中のfocus喪失→復帰で取消す。
        pressRelease();
        world.Tick(kFixedDeltaTime);
        mapper.SetFocused(false);
        mapper.SetFocused(true);
        mapper.Update(0, 0);
        if (!StepCharacter(processor) || driver->GetConsumedJumpEventCount() != 2)
            return false;
        driver->Disable();
        pressRelease();
        driver->Enable();
        world.Tick(kFixedDeltaTime);
        if (!StepCharacter(processor) || driver->GetConsumedJumpEventCount() != 2)
            return false;
        // 手動の重複FixedTickでもBeforeを二重実行しない。
        unsigned callbacks = 0;
        value.Character->BeforeSimulation.Add([&](float) { ++callbacks; });
        value.Character->TickFixedStep(kFixedDeltaTime);
        value.Character->TickFixedStep(kFixedDeltaTime);
        const bool once = callbacks == 1 && StepCharacter(processor) && callbacks == 1;
        value.Character->BeforeSimulation.Clear();
        return once;
    }

    bool TestGameLocomotionHeapAfterWorld()
    {
        ObjectHeap heap;
        const auto handle = heap.Create<Game::Gameplay::QuadrupedLocomotionComponent>();
        auto* driver = heap.Resolve<Game::Gameplay::QuadrupedLocomotionComponent>(handle);
        if (!driver)
            return false;
        {
            World temporary;
            temporary.Initialize();
            auto* owner = temporary.SpawnEntity<Entity>();
            if (!owner || !temporary.CreateComponent<CharacterBodyComponent>(owner) || !owner->AddComponent(driver))
                return false;
            driver->Initialize();
            driver->TickInputGroup(kFixedDeltaTime);
            // Worldはheap所有componentのOuterを解除してpendingにするが、この場では解放しない。
        }
        return driver->GetOwner() == nullptr && heap.DestroyNow(handle);
    }

    bool TestFollowCameraRenderBinding(ApplicationProcessor& processor)
    {
        auto& engine = *NorvesLib::Core::Engine::GEngine;
        auto& world = engine.GetWorld();
        CharacterFixture subject;
        if (!CreateCharacterFloor() || !CreateCharacter(subject, Math::Vector3::Zero))
            return false;
        auto* visual = world.SpawnEntity<Entity>(subject.Owner);
        auto* cameraOwner = world.SpawnEntity<Entity>();
        auto* viewSource = world.SpawnEntity<Entity>();
        auto* wall = world.SpawnEntity<Entity>();
        auto* trigger = world.SpawnEntity<Entity>();
        if (!visual || !cameraOwner || !viewSource || !wall || !trigger)
            return false;
        wall->SetPosition({0, 2, 2});
        trigger->SetPosition({0, 2, 1});
        auto* wallCollider = world.CreateComponent<ColliderComponent>(wall);
        auto* triggerCollider = world.CreateComponent<ColliderComponent>(trigger);
        auto* follow = world.CreateComponent<Game::Gameplay::FollowCameraComponent>(cameraOwner);
        if (!wallCollider || !triggerCollider || !follow ||
            wallCollider->SetBox({20, 2, .1f}) != EPhysicsResult::Success ||
            triggerCollider->SetBox({20, 2, .1f}) != EPhysicsResult::Success ||
            triggerCollider->SetTrigger(true) != EPhysicsResult::Success ||
            !follow->SetSubject(visual, subject.Owner) || !follow->SetViewSource(viewSource))
            return false;
        follow->BindSceneQuery(&engine.GetSceneQuery());
        follow->SetPitch(0);
        follow->SetYaw(0);
        follow->SetArmLength(5);
        follow->SetTargetOffset({0, .4f, 0});
        visual->SetRenderInterpolationEnabled(true);
        if (!StepCharacter(processor))
            return false;
        world.Tick(kFixedDeltaTime);
        world.LateTick(kFixedDeltaTime);
        if (follow->GetLastCollisionResult() != Camera::CameraCollisionResult::Success ||
            std::fabs(follow->GetCollisionOutput().EffectiveLength - 1.65f) > .01f)
            return false;
        if (!follow->SetFollowSmoothing(0, 0)) return false;
        // queryの物理rootはx=0のままでも、Cameraは補間描画位置x=2を読む。
        world.PrepareRenderInterpolationStep();
        subject.Owner->SetPosition({4, subject.Owner->GetPosition().y, 0});
        world.UpdateWorldTransforms();
        world.CaptureRenderInterpolationStep();
        world.SetRenderInterpolationAlpha(.5f);
        world.Tick(kFixedDeltaTime);
        world.LateTick(kFixedDeltaTime);
        if (std::fabs(cameraOwner->GetPosition().x - 2) > 1e-4f || subject.Owner->GetPosition().x != 4)
            return false;
        wall->SetPosition({0, 2, 20});
        if (PhysicsModuleTestAccess::RefreshDynamicSnapshot(*GFixture->Physics) != EPhysicsSceneQueryResult::Success)
            return false;
        world.Tick(kFixedDeltaTime);
        world.LateTick(kFixedDeltaTime);
        if (follow->GetCollisionOutput().EffectiveLength <= 1.65f || follow->GetCollisionOutput().EffectiveLength >= 5)
            return false;
        const auto pose = cameraOwner->GetWorldTransform();
        const auto length = follow->GetCollisionOutput().EffectiveLength;
        const auto cameraTicks = follow->GetCameraTickCount();
        follow->RefreshOwnerTransform();
        follow->RefreshOwnerTransform();
        if (cameraOwner->GetWorldTransform() != pose || follow->GetCollisionOutput().EffectiveLength != length ||
            follow->GetCameraTickCount() != cameraTicks)
            return false;
        follow->BindSceneQuery(nullptr);
        follow->SetYaw(90);
        world.Tick(kFixedDeltaTime);
        world.LateTick(kFixedDeltaTime);
        if (cameraOwner->GetWorldTransform() != pose)
            return false;
        follow->BindSceneQuery(&engine.GetSceneQuery());
        follow->SetPivotObjectId(cameraOwner->GetObjectId());
        world.Tick(kFixedDeltaTime);
        world.LateTick(kFixedDeltaTime);
        if (follow->GetLastCollisionResult() != Camera::CameraCollisionResult::ProbeFailed ||
            cameraOwner->GetWorldTransform() != pose)
            return false;
        return !follow->SetSubject(subject.Owner, subject.Owner);
    }

    bool TestDynamicRenderInterpolationDefault(ApplicationProcessor& processor)
    {
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        auto* owner = world.SpawnEntity<Entity>();
        auto* collider = owner ? world.CreateComponent<ColliderComponent>(owner) : nullptr;
        auto* body = owner ? world.CreateComponent<RigidBodyComponent>(owner) : nullptr;
        if (!body || !collider || collider->SetSphere(.3f) != EPhysicsResult::Success ||
            body->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success ||
            body->SetGravityScale(0) != EPhysicsResult::Success ||
            body->SetLinearVelocity({60, 0, 0}) != EPhysicsResult::Success || !owner->IsRenderInterpolationEnabled())
            return false;
        world.Tick(kFixedDeltaTime);
        if (!StepCharacter(processor))
            return false;
        world.SetRenderInterpolationAlpha(.5f);
        world.LateTick(kFixedDeltaTime);
        if (std::fabs(owner->GetPosition().x - 1) > 1e-4f ||
            std::fabs(owner->GetRenderWorldTransform().position.x - .5f) > 1e-4f)
            return false;
        owner->SetRenderInterpolationEnabled(false);
        world.Tick(kFixedDeltaTime);
        if (!StepCharacter(processor))
            return false;
        world.SetRenderInterpolationAlpha(.5f);
        world.LateTick(kFixedDeltaTime);
        if (owner->GetRenderWorldTransform() != owner->GetWorldTransform())
            return false;
        owner->ClearRenderInterpolationOverride();
        if (!owner->IsRenderInterpolationEnabled())
            return false;
        auto* other = world.SpawnEntity<Entity>();
        if (!other)
            return false;
        // false既定と同じ値でも、明示OFFをDynamic既定で上書きしない。
        other->SetRenderInterpolationEnabled(false);
        auto* second = world.CreateComponent<RigidBodyComponent>(other);
        if (!second || second->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success ||
            other->IsRenderInterpolationEnabled())
            return false;
        other->ClearRenderInterpolationOverride();
        if (!other->IsRenderInterpolationEnabled() ||
            second->SetBodyType(EPhysicsBodyType::Kinematic) != EPhysicsResult::Success ||
            other->IsRenderInterpolationEnabled())
            return false;
        other->SetRenderInterpolationEnabled(true);
        if (second->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success)
            return false;
        second->Finalize();
        if (!other->IsRenderInterpolationEnabled())
            return false;
        other->ClearRenderInterpolationOverride();
        if (other->IsRenderInterpolationEnabled())
            return false;
        second->Initialize();
        if (!other->IsRenderInterpolationEnabled())
            return false;
        EPhysicsResult fromWorker = EPhysicsResult::Success;
        Thread::Thread worker([&] { fromWorker = second->SetBodyType(EPhysicsBodyType::Kinematic); });
        worker.Join();
        if (fromWorker != EPhysicsResult::WrongThread || !other->IsRenderInterpolationEnabled())
            return false;
        auto* parent = world.SpawnEntity<Entity>();
        if (!parent || !world.ReparentEntity(other, parent) || !StepCharacter(processor) ||
            other->IsRenderInterpolationEnabled())
            return false;
        if (second->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::InvalidState ||
            other->IsRenderInterpolationEnabled())
            return false;
        if (!world.ReparentEntity(other, nullptr) || !StepCharacter(processor) ||
            !other->IsRenderInterpolationEnabled())
            return false;
        second->Disable();
        if (!StepCharacter(processor) || other->IsRenderInterpolationEnabled())
            return false;
        second->Enable();
        if (!StepCharacter(processor) || !other->IsRenderInterpolationEnabled())
            return false;
        GFixture->Physics->Shutdown();
        if (owner->IsRenderInterpolationEnabled() || other->IsRenderInterpolationEnabled() ||
            !GFixture->Physics->Initialize() || !owner->IsRenderInterpolationEnabled() ||
            !other->IsRenderInterpolationEnabled())
            return false;
        world.SetRenderInterpolationAllowed(false);
        world.Tick(kFixedDeltaTime);
        if (!StepCharacter(processor))
            return false;
        world.SetRenderInterpolationAlpha(.5f);
        world.LateTick(kFixedDeltaTime);
        return owner->GetRenderWorldTransform() == owner->GetWorldTransform();
    }

    bool TestDynamicInterpolationAt144Hz(ApplicationProcessor& processor, uint32_t rate)
    {
        if (!ApplicationFixedStepTestAccess::SetRate(processor, rate))
            return false;
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        Entity* entities[2]{};
        for (unsigned i = 0; i < 2; ++i)
        {
            entities[i] = world.SpawnEntity<Entity>();
            if (!entities[i])
                return false;
            entities[i]->SetPosition({0, 0, float(i) * 10});
            auto* collider = world.CreateComponent<ColliderComponent>(entities[i]);
            auto* body = world.CreateComponent<RigidBodyComponent>(entities[i]);
            if (!collider || !body || collider->SetSphere(.2f) != EPhysicsResult::Success ||
                body->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success ||
                body->SetGravityScale(0) != EPhysicsResult::Success ||
                body->SetLinearVelocity({1, 0, 0}) != EPhysicsResult::Success)
                return false;
        }
        entities[1]->SetRenderInterpolationEnabled(false);
        float previousOn = 0, previousOff = 0;
        unsigned unsmoothedZeroFrames = 0;
        for (int64_t frame = 0; frame < 144; ++frame)
        {
            const int64_t raw = (frame + 1) * 1000000000 / 144 - frame * 1000000000 / 144;
            const auto result = ApplicationFixedStepTestAccess::Simulate(processor, raw);
            if (result.Status != EFixedStepAdvanceStatus::Advanced)
                return false;
            const float on = entities[0]->GetRenderWorldTransform().position.x;
            const float off = entities[1]->GetRenderWorldTransform().position.x;
            if (frame > 5)
            {
                if (std::fabs((on - previousOn) - 1.f / 144) > 1e-5f)
                    return false;
                if (std::fabs(off - previousOff) < 1e-7f)
                    ++unsmoothedZeroFrames;
            }
            if (entities[0]->GetPosition().x != entities[1]->GetPosition().x)
                return false;
            previousOn = on;
            previousOff = off;
        }
        return unsmoothedZeroFrames >= (rate == 60 ? 65u : 15u);
    }

    bool TestCharacterGravityScale(ApplicationProcessor& processor)
    {
        CharacterFixture value;
        if (!CreateCharacter(value, Math::Vector3(0, 100, 0)))
            return false;
        // 初回の準備を無重力で行い、計測開始時の鉛直速度をゼロにする。
        CharacterBodySettings settings;
        settings.GravityScale = 0;
        if (value.Character->SetSettings(settings) != EPhysicsResult::Success)
            return false;
        for (unsigned step = 0; step < 120 && !value.Character->GetState().bReady; ++step)
            if (!StepCharacter(processor))
                return false;
        if (!value.Character->GetState().bReady)
            return false;
        const float initialHeight = value.Owner->GetPosition().y;
        settings.GravityScale = .5f;
        if (value.Character->SetSettings(settings) != EPhysicsResult::Success)
            return false;
        for (unsigned step = 0; step < 60; ++step)
            if (!StepCharacter(processor) || !value.Character->GetState().bReady)
                return false;
        constexpr float expected = 9.81f * .5f * .5f;
        const float descent = initialHeight - value.Owner->GetPosition().y;
        if (!std::isfinite(descent) || std::fabs(descent - expected) > expected * .02f)
        {
            std::cout << "GRAVITY_SCALE descent=" << descent << " expected=" << expected << '\n';
            return false;
        }
        return true;
    }

    bool TestCharacterSteepSlopeSlide(ApplicationProcessor& processor)
    {
        Entity* slope = CreateCharacterFloor(20);
        if (!slope)
            return false;
        const float angle = 60.0f * Math::Constants::PI / 180.0f;
        const Math::Quaternion rotation(Math::Vector3::UnitZ, angle);
        const Math::Vector3 normal = rotation * Math::Vector3::UnitY;
        slope->SetPosition(normal * -.5f);
        slope->SetRotation(rotation);
        CharacterFixture value;
        // 半径 .3 の球端が斜面にほぼ接する位置から始める。
        if (!CreateCharacter(value, Math::Vector3(0, .301f, 0)) ||
            value.Character->SetDesiredVelocity(Math::Vector3::Zero) != EPhysicsResult::Success)
            return false;
        const auto initial = value.Owner->GetPosition();
        for (unsigned step = 0; step < 60; ++step)
            if (!StepCharacter(processor) || !value.Character->GetState().bReady)
                return false;
        const auto final = value.Owner->GetPosition();
        if (!std::isfinite(final.x) || !std::isfinite(final.y) || final.x > initial.x - .05f ||
            final.y >= initial.y || value.Character->GetState().bGrounded)
        {
            std::cout << "STEEP_SLOPE dx=" << final.x - initial.x << " dy=" << final.y - initial.y
                      << " grounded=" << value.Character->GetState().bGrounded << '\n';
            return false;
        }
        return true;
    }

    class CharacterElevatorTick final : public Component::Component, public IFixedProbe
    {
      public:
        bool bMoving = false;
        bool bRefresh = false;
        bool bRefreshSucceeded = true;
        unsigned Step = 0;
        void OnFixed(float fixedDeltaTime) override
        {
            if (!bMoving)
                return;
            auto target = GetOwner()->GetWorldTransform();
            const float direction = (Step / 10) % 2 == 0 ? 1.0f : -1.0f;
            target.position.y += direction * 3.0f * fixedDeltaTime;
            GetOwner()->SetWorldTransform(target);
            ++Step;
            if (bRefresh && GEngine->GetSceneQuery().RefreshDynamicSnapshot() != EPhysicsSceneQueryResult::Success)
                bRefreshSucceeded = false;
        }
    };

    bool TestCharacterElevatorJitter(ApplicationProcessor& processor)
    {
        auto& world = GEngine->GetWorld();
        Entity* floors[2] = {CreateCharacterFloor(4), CreateCharacterFloor(4)};
        CharacterElevatorTick* movers[2] = {};
        CharacterFixture characters[2];
        if (!floors[0] || !floors[1])
            return false;
        floors[1]->SetPosition(Math::Vector3(20, -.5f, 0));
        for (unsigned i = 0; i < 2; ++i)
        {
            auto* body = world.CreateComponent<RigidBodyComponent>(floors[i]);
            movers[i] = world.CreateComponent<CharacterElevatorTick>(floors[i]);
            if (!body || !movers[i] || body->SetBodyType(EPhysicsBodyType::Kinematic) != EPhysicsResult::Success ||
                !CreateCharacter(characters[i], Math::Vector3(static_cast<float>(i) * 20, 0, 0)))
                return false;
        }
        // 更新ありの足場を先に処理し、更新なしの足場はその後に動かす。
        movers[0]->bRefresh = true;
        for (unsigned step = 0; step < 10; ++step)
            if (!StepCharacter(processor))
                return false;
        for (unsigned i = 0; i < 2; ++i)
        {
            if (!characters[i].Character->GetState().bReady || !characters[i].Character->GetState().bGrounded)
                return false;
            movers[i]->bMoving = true;
        }
        float minimum[2] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        float maximum[2] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        float floorMinimum[2] = {floors[0]->GetPosition().y, floors[1]->GetPosition().y};
        float floorMaximum[2] = {floorMinimum[0], floorMinimum[1]};
        float previousHeight = characters[0].Owner->GetPosition().y;
        float maximumVelocityError = 0.0f;
        for (unsigned step = 0; step < 120; ++step)
        {
            if (!StepCharacter(processor))
                return false;
            for (unsigned i = 0; i < 2; ++i)
            {
                if (i == 0 && !characters[i].Character->GetState().bReady)
                    return false;
                if (i == 0)
                {
                    const float height = characters[0].Owner->GetPosition().y;
                    const float displacementVelocity = (height - previousHeight) / kFixedDeltaTime;
                    const float velocityError = std::fabs(characters[0].Body->GetLinearVelocity().y - displacementVelocity);
                    maximumVelocityError = std::max(velocityError, maximumVelocityError);
                    previousHeight = height;
                }
                const float floorHeight = floors[i]->GetPosition().y;
                const float gap = characters[i].Owner->GetPosition().y - (floorHeight + .5f);
                if (!std::isfinite(gap))
                {
                    if (i == 0)
                        return false;
                    minimum[i] = maximum[i] = std::numeric_limits<float>::quiet_NaN();
                }
                else
                {
                    minimum[i] = std::min(minimum[i], gap);
                    maximum[i] = std::max(maximum[i], gap);
                }
                floorMinimum[i] = std::min(floorMinimum[i], floorHeight);
                floorMaximum[i] = std::max(floorMaximum[i], floorHeight);
            }
        }
        const float refresh = maximum[0] - minimum[0];
        const float noRefresh = maximum[1] - minimum[1];
        std::cout << "ELEVATOR_JITTER refresh=" << refresh << " norefresh=" << noRefresh
                  << " velocity_error=" << maximumVelocityError << '\n';
        const bool platformsMoved = std::fabs(floorMaximum[0] - floorMinimum[0] - .5f) < .001f &&
                                    std::fabs(floorMaximum[1] - floorMinimum[1] - .5f) < .001f;
        return movers[0]->bRefreshSucceeded && movers[0]->Step == 120 && movers[1]->Step == 120 &&
               platformsMoved && refresh < .01f && maximumVelocityError <= .005f;
    }

    bool TestCharacterPlatformDepartureVelocity(ApplicationProcessor& processor)
    {
        auto& world = GEngine->GetWorld();
        Entity* floor = CreateCharacterFloor(4);
        auto* body = floor ? world.CreateComponent<RigidBodyComponent>(floor) : nullptr;
        auto* mover = floor ? world.CreateComponent<CharacterPlatformTick>(floor) : nullptr;
        CharacterFixture value;
        if (!body || !mover || body->SetBodyType(EPhysicsBodyType::Kinematic) != EPhysicsResult::Success ||
            !CreateCharacter(value, Math::Vector3(1, 0, 0)) ||
            value.Character->SetDesiredVelocity(Math::Vector3::Zero) != EPhysicsResult::Success)
        {
            return false;
        }

        const auto platformTransform = [](unsigned frame)
        {
            const unsigned phase = frame % 120;
            const float distance = static_cast<float>(phase <= 60 ? phase : 120 - phase) * 2.0f * kFixedDeltaTime;
            const float angle = static_cast<float>(frame) * kFixedDeltaTime * Math::Constants::PI / 6.0f;
            return Math::Transform(Math::Vector3(distance, -.5f, 0), Math::Quaternion(Math::Vector3::UnitY, angle));
        };
        const auto horizontalError = [](const Math::Vector3& a, const Math::Vector3& b)
        {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return std::sqrt(dx * dx + dz * dz);
        };

        mover->Target = platformTransform(0);
        if (!StepCharacter(processor) || !value.Character->GetState().bReady ||
            !value.Character->GetState().bGrounded)
        {
            return false;
        }
        const auto initialLocal = value.Owner->GetPosition() - floor->GetPosition();

        // 120 ステップの追従後、折り返しから 15 ステップ離れた位置で離脱する。
        constexpr unsigned departureFrame = 135;
        for (unsigned frame = 1; frame <= departureFrame; ++frame)
        {
            mover->Target = platformTransform(frame);
            if (!StepCharacter(processor) || !value.Character->GetState().bReady ||
                !value.Character->GetState().bGrounded)
            {
                std::cout << "PLATFORM_GROUNDED frame=" << frame << '\n';
                return false;
            }
            const float angle = static_cast<float>(frame) * kFixedDeltaTime * Math::Constants::PI / 6.0f;
            const auto local = Math::Quaternion(Math::Vector3::UnitY, -angle) *
                               (value.Owner->GetPosition() - floor->GetPosition());
            const float drift = (local - initialLocal).Length();
            if (!std::isfinite(drift) || drift >= .02f)
            {
                std::cout << "PLATFORM_LOCAL_DRIFT frame=" << frame << " distance=" << drift << '\n';
                return false;
            }
        }

        const auto expected = value.Character->GetState().PlatformVelocity;
        const auto analytical = (platformTransform(departureFrame).TransformPoint(initialLocal) -
                                 platformTransform(departureFrame - 1).TransformPoint(initialLocal)) /
                                kFixedDeltaTime;
        const float analyticalError = horizontalError(expected, analytical);
        if (!std::isfinite(analyticalError) || analyticalError > .05f ||
            horizontalError(analytical, Math::Vector3(2, 0, 0)) < .45f)
        {
            std::cout << "PLATFORM_VELOCITY expected_x=" << expected.x << " expected_z=" << expected.z
                      << " analytical_x=" << analytical.x << " analytical_z=" << analytical.z << '\n';
            return false;
        }
        if (value.Character->Jump(5) != EPhysicsResult::Success)
        {
            return false;
        }
        for (unsigned frame = departureFrame + 1; frame <= departureFrame + 2; ++frame)
        {
            const auto before = value.Owner->GetPosition();
            mover->Target = platformTransform(frame);
            if (!StepCharacter(processor) || !value.Character->GetState().bReady ||
                value.Character->GetState().bGrounded)
            {
                return false;
            }
            const auto observed = value.Body->GetLinearVelocity();
            const auto displacementVelocity = (value.Owner->GetPosition() - before) / kFixedDeltaTime;
            const float bodyError = horizontalError(observed, expected);
            const float displacementError = horizontalError(displacementVelocity, expected);
            if (!std::isfinite(bodyError) || !std::isfinite(displacementError) ||
                bodyError > .05f || displacementError > .05f)
            {
                std::cout << "PLATFORM_DEPARTURE frame=" << frame << " body_error=" << bodyError
                          << " displacement_error=" << displacementError << " expected_x=" << expected.x
                          << " expected_z=" << expected.z << " observed_x=" << observed.x
                          << " observed_z=" << observed.z << '\n';
                return false;
            }
        }
        return true;
    }

    bool TestCharacterExternalMovement(ApplicationProcessor& processor)
    {
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero) ||
            !StepCharacter(processor) || !value.Character->GetState().bGrounded)
        {
            return false;
        }
        // モード切り替えは保留中の Teleport を破棄するため、先に固定ステップで反映する。
        if (value.Character->Teleport(Math::Vector3(0, 2, 0)) != EPhysicsResult::Success ||
            !StepCharacter(processor))
        {
            return false;
        }
        const auto start = value.Owner->GetPosition();
        const auto isNear = [](float actual, float expected, float tolerance)
        {
            return std::isfinite(actual) && std::fabs(actual - expected) <= tolerance;
        };
        const auto nearVector = [&](const Math::Vector3& actual, const Math::Vector3& expected, float tolerance)
        {
            return isNear(actual.x, expected.x, tolerance) && isNear(actual.y, expected.y, tolerance) &&
                   isNear(actual.z, expected.z, tolerance);
        };
        if (!nearVector(start, Math::Vector3(0, 2, 0), .005f) ||
            value.Character->SetMovementMode(CharacterMovementMode::External) != EPhysicsResult::Success ||
            value.Character->SetDesiredVelocity(Math::Vector3(5, 0, 0)) != EPhysicsResult::Success ||
            value.Character->Jump(5) != EPhysicsResult::InvalidState ||
            value.Character->LaunchVertical(5) != EPhysicsResult::InvalidState)
        {
            return false;
        }
        const auto serial = value.Character->GetState().StepSerial;
        const auto generation = value.Character->GetMotionGeneration();
        for (unsigned step = 0; step < 30; ++step)
        {
            if (!StepCharacter(processor))
            {
                return false;
            }
            const auto& state = value.Character->GetState();
            if (!nearVector(value.Owner->GetPosition(), start, 1e-4f) || !state.bReady || state.bGrounded ||
                state.Result != EPhysicsResult::Success || state.StepSerial != serial + step + 1 ||
                value.Character->GetMotionGeneration() != generation)
            {
                std::cout << "EXTERNAL_REST step=" << step << '\n';
                return false;
            }
        }
        if (value.Character->MoveDelta(Math::Vector3::Zero, .1f) != EPhysicsResult::Success ||
            value.Character->MoveDelta(Math::Vector3(1, 0, 0), .15f) != EPhysicsResult::Success ||
            !StepCharacter(processor))
        {
            return false;
        }
        const auto moved = start + Math::Vector3(1, 0, 0);
        const auto expectedForward = Math::Quaternion(Math::Vector3::UnitY, .25f) * Math::Vector3::UnitZ;
        const auto actualForward = value.Owner->GetWorldTransform().rotation * Math::Vector3::UnitZ;
        const auto& state = value.Character->GetState();
        if (!nearVector(value.Owner->GetPosition(), moved, 1e-4f) ||
            !nearVector(value.Body->GetLinearVelocity(), Math::Vector3(60, 0, 0), .01f) ||
            !nearVector(state.Velocity, Math::Vector3(60, 0, 0), .01f) ||
            !nearVector(actualForward, expectedForward, 1e-4f) || !state.bReady || state.bGrounded ||
            state.Result != EPhysicsResult::Success || state.StepSerial != serial + 31)
        {
            std::cout << "EXTERNAL_MOVE x=" << value.Owner->GetPosition().x
                      << " velocity_x=" << value.Body->GetLinearVelocity().x << '\n';
            return false;
        }
        // 移動と回転は一度だけ消費し、床を横切る要求も衝突で制限しない。
        if (!StepCharacter(processor) || !nearVector(value.Owner->GetPosition(), moved, 1e-4f) ||
            !nearVector(value.Owner->GetWorldTransform().rotation * Math::Vector3::UnitZ, expectedForward, 1e-4f) ||
            !nearVector(value.Body->GetLinearVelocity(), Math::Vector3::Zero, .01f) ||
            value.Character->MoveDelta(Math::Vector3(0, -3, 0)) != EPhysicsResult::Success ||
            !StepCharacter(processor) ||
            !nearVector(value.Owner->GetPosition(), moved + Math::Vector3(0, -3, 0), 1e-4f))
        {
            return false;
        }
        // External 中の Teleport は瞬間移動分を速度に含めず、切り替え時には保留入力を破棄する。
        if (value.Character->Teleport(moved) != EPhysicsResult::Success || !StepCharacter(processor) ||
            !nearVector(value.Owner->GetPosition(), moved, 1e-4f) ||
            !nearVector(value.Body->GetLinearVelocity(), Math::Vector3::Zero, .01f) ||
            value.Character->MoveDelta(Math::Vector3(2, 0, 0)) != EPhysicsResult::Success ||
            value.Character->SetMovementMode(CharacterMovementMode::Walking) != EPhysicsResult::Success ||
            value.Character->SetDesiredVelocity(Math::Vector3::Zero) != EPhysicsResult::Success)
        {
            return false;
        }
        for (unsigned step = 0; step < 3; ++step)
        {
            if (!StepCharacter(processor))
            {
                return false;
            }
        }
        const auto falling = value.Owner->GetPosition();
        return isNear(falling.x, moved.x, 1e-4f) && isNear(falling.z, moved.z, 1e-4f) &&
               std::isfinite(falling.y) && falling.y < moved.y - 1e-4f;
    }

    bool TestCharacterPushDynamicSphere(ApplicationProcessor& processor)
    {
        auto& world = GEngine->GetWorld();
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero))
        {
            return false;
        }
        Entity* sphere = world.SpawnEntity<Entity>();
        auto* collider = sphere ? world.CreateComponent<ColliderComponent>(sphere) : nullptr;
        auto* body = sphere ? world.CreateComponent<RigidBodyComponent>(sphere) : nullptr;
        if (!collider || !body || collider->SetSphere(.25f) != EPhysicsResult::Success ||
            body->SetBodyType(EPhysicsBodyType::Dynamic) != EPhysicsResult::Success)
        {
            return false;
        }
        sphere->SetPosition(Math::Vector3(1.5f, .3f, 0));
        for (unsigned step = 0; step < 12; ++step)
        {
            if (!StepCharacter(processor))
            {
                return false;
            }
        }
        const float sphereStart = sphere->GetPosition().x;
        const float characterStart = value.Owner->GetPosition().x;
        if (!value.Character->GetState().bReady || !value.Character->GetState().bGrounded ||
            value.Character->SetDesiredVelocity(Math::Vector3(2, 0, 0)) != EPhysicsResult::Success)
        {
            return false;
        }
        Container::VariableArray<PhysicsOverlapHit> hits;
        for (unsigned step = 0; step < 90; ++step)
        {
            if (!StepCharacter(processor) || !value.Character->GetState().bReady)
            {
                return false;
            }
            PhysicsShapeProxy proxy;
            if (!PhysicsModuleTestAccess::CopyPublishedProxy(*GFixture->Physics,
                    value.Collider->GetColliderHandle(), proxy) || proxy.Shape != EPhysicsProxyShape::Capsule ||
                GEngine->GetSceneQuery().OverlapCapsule(proxy.Capsule, hits) != EPhysicsSceneQueryResult::Success)
            {
                return false;
            }
            for (const auto& hit : hits)
            {
                if (hit.Collider == collider->GetColliderHandle() &&
                    (!std::isfinite(hit.Contact.Depth) || hit.Contact.Depth > 1e-3f))
                {
                    std::cout << "DYNAMIC_PUSH_CONTACT step=" << step << " depth=" << hit.Contact.Depth << '\n';
                    return false;
                }
            }
        }
        const float sphereDistance = sphere->GetPosition().x - sphereStart;
        const float characterDistance = value.Owner->GetPosition().x - characterStart;
        std::cout << "DYNAMIC_PUSH sphere_distance=" << sphereDistance
                  << " character_distance=" << characterDistance << '\n';
        return std::isfinite(sphereDistance) && std::isfinite(characterDistance) &&
               sphereDistance >= 1.0f && characterDistance >= 1.0f;
    }

    bool TestCharacterVelocityAndDepenetration(ApplicationProcessor& processor)
    {
        auto& world = GEngine->GetWorld();
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero))
        {
            return false;
        }

        unsigned step = 0;
        const auto checkStep = [&]()
        {
            const auto before = value.Owner->GetPosition();
            if (!StepCharacter(processor) || !value.Character->GetState().bReady)
            {
                return false;
            }
            ++step;
            const auto displacementVelocity = (value.Owner->GetPosition() - before) / kFixedDeltaTime;
            const auto bodyVelocity = value.Body->GetLinearVelocity();
            const float error = (bodyVelocity - displacementVelocity).Length();
            if (!std::isfinite(error) || error > .005f)
            {
                std::cout << "CHARACTER_VELOCITY step=" << step << " error=" << error << '\n';
                return false;
            }
            return true;
        };

        if (!checkStep() || !value.Character->GetState().bGrounded ||
            value.Character->SetDesiredVelocity(Math::Vector3(3, 0, 0)) != EPhysicsResult::Success)
        {
            return false;
        }
        const auto straightStart = value.Owner->GetPosition();
        for (unsigned i = 0; i < 30; ++i)
        {
            if (!checkStep() || !value.Character->GetState().bGrounded)
            {
                return false;
            }
        }
        const auto straightEnd = value.Owner->GetPosition();
        if (std::fabs(straightEnd.x - straightStart.x - 1.5f) > .02f ||
            value.Character->SetDesiredVelocity(Math::Vector3(-1.5f, 0, 2)) != EPhysicsResult::Success)
        {
            return false;
        }
        for (unsigned i = 0; i < 30; ++i)
        {
            if (!checkStep() || !value.Character->GetState().bGrounded)
            {
                return false;
            }
        }
        const auto diagonalEnd = value.Owner->GetPosition();
        if (std::fabs(diagonalEnd.x - straightEnd.x + .75f) > .02f ||
            std::fabs(diagonalEnd.z - straightEnd.z - 1.0f) > .02f ||
            value.Character->SetDesiredVelocity(Math::Vector3::Zero) != EPhysicsResult::Success)
        {
            return false;
        }
        for (unsigned i = 0; i < 10; ++i)
        {
            if (!checkStep() || !value.Character->GetState().bGrounded)
            {
                return false;
            }
        }
        if (value.Character->Jump(5) != EPhysicsResult::Success)
        {
            return false;
        }
        bool bAirborne = false;
        bool bLanded = false;
        for (unsigned i = 0; i < 120; ++i)
        {
            if (!checkStep())
            {
                return false;
            }
            if (!value.Character->GetState().bGrounded)
            {
                bAirborne = true;
            }
            else if (bAirborne)
            {
                bLanded = true;
                break;
            }
        }
        if (!bAirborne || !bLanded ||
            value.Character->Teleport(Math::Vector3(10, 0, 0)) != EPhysicsResult::Success ||
            !StepCharacter(processor) || !value.Character->GetState().bReady ||
            !value.Character->GetState().bGrounded)
        {
            return false;
        }
        const float teleportError = (value.Owner->GetPosition() - Math::Vector3(10, 0, 0)).Length();
        const float teleportSpeed = value.Body->GetLinearVelocity().Length();
        if (!std::isfinite(teleportError) || !std::isfinite(teleportSpeed) ||
            teleportError > .001f || teleportSpeed > .001f)
        {
            std::cout << "CHARACTER_TELEPORT error=" << teleportError << " speed=" << teleportSpeed << '\n';
            return false;
        }

        // 左側の面をカプセルへ 0.05 m 食い込ませる。
        Entity* wall = world.SpawnEntity<Entity>();
        auto* wallCollider = wall ? world.CreateComponent<ColliderComponent>(wall) : nullptr;
        if (!wallCollider || wallCollider->SetBox(Math::Vector3(.5f, 1, 1)) != EPhysicsResult::Success)
        {
            return false;
        }
        wall->SetPosition(Math::Vector3(10.75f, 1, 0));
        const auto beforePush = value.Owner->GetPosition();
        if (!StepCharacter(processor) || !value.Character->GetState().bReady ||
            !value.Character->GetState().bGrounded)
        {
            return false;
        }
        const auto displacement = value.Owner->GetPosition() - beforePush;
        const float speed = value.Body->GetLinearVelocity().Length();
        if (!std::isfinite(displacement.Length()) || !std::isfinite(speed) || displacement.x > -.05f || speed > .001f)
        {
            std::cout << "CHARACTER_DEPENETRATION displacement_x=" << displacement.x << " speed=" << speed << '\n';
            return false;
        }
        return true;
    }

    bool TestCharacterPlatformPushedAgainstWall(ApplicationProcessor& processor)
    {
        auto& world = GEngine->GetWorld();
        Entity* floor = CreateCharacterFloor(4);
        auto* floorCollider = floor ? floor->GetComponent<ColliderComponent>() : nullptr;
        auto* body = floor ? world.CreateComponent<RigidBodyComponent>(floor) : nullptr;
        auto* mover = floor ? world.CreateComponent<CharacterPlatformTick>(floor) : nullptr;
        Entity* wall = world.SpawnEntity<Entity>();
        auto* wallCollider = wall ? world.CreateComponent<ColliderComponent>(wall) : nullptr;
        CharacterFixture value;
        if (!floorCollider || !body || !mover || !wallCollider ||
            body->SetBodyType(EPhysicsBodyType::Kinematic) != EPhysicsResult::Success ||
            wallCollider->SetBox(Math::Vector3(.5f, 1, 1)) != EPhysicsResult::Success ||
            !CreateCharacter(value, Math::Vector3::Zero) ||
            value.Character->SetDesiredVelocity(Math::Vector3::Zero) != EPhysicsResult::Success)
        {
            return false;
        }
        // 壁の下端を足場の上面より高くし、足場が壁の下を通れるようにする。
        wall->SetPosition(Math::Vector3(1, 1.01f, 0));
        mover->Target = Math::Transform(Math::Vector3(0, -.5f, 0));

        unsigned step = 0;
        bool bVelocityValid = true;
        const auto checkStep = [&]()
        {
            const auto before = value.Owner->GetPosition();
            if (!StepCharacter(processor) || !value.Character->GetState().bReady)
                return false;
            const auto displacementVelocity = (value.Owner->GetPosition() - before) / kFixedDeltaTime;
            const float error = (value.Body->GetLinearVelocity() - displacementVelocity).Length();
            if (!std::isfinite(error) || error > .005f)
            {
                if (bVelocityValid)
                    std::cout << "CHARACTER_WALL_VELOCITY step=" << step << " error=" << error << '\n';
                bVelocityValid = false;
            }
            return true;
        };

        if (!checkStep() || !value.Character->GetState().bGrounded ||
            !(value.Character->GetState().GroundCollider == floorCollider->GetColliderHandle()))
        {
            return false;
        }
        unsigned pinnedSteps = 0;
        for (step = 1; step <= 90; ++step)
        {
            const auto beforeCharacter = value.Owner->GetPosition();
            const float beforeFloor = floor->GetPosition().x;
            const float distance = static_cast<float>(step) * 2.0f * kFixedDeltaTime;
            mover->Target = Math::Transform(Math::Vector3(distance, -.5f, 0));
            if (!checkStep() || !value.Character->GetState().bGrounded ||
                !(value.Character->GetState().GroundCollider == floorCollider->GetColliderHandle()))
            {
                std::cout << "CHARACTER_WALL_GROUND step=" << step << '\n';
                return false;
            }
            const float floorDelta = floor->GetPosition().x - beforeFloor;
            if (!std::isfinite(floorDelta) || std::fabs(floorDelta - 2.0f * kFixedDeltaTime) > .0001f)
            {
                std::cout << "CHARACTER_WALL_PLATFORM step=" << step << " delta=" << floorDelta << '\n';
                return false;
            }
            if (std::fabs(value.Owner->GetPosition().x - beforeCharacter.x) < .001f)
                ++pinnedSteps;
        }
        std::cout << "CHARACTER_WALL_PINNED steps=" << pinnedSteps << '\n';
        return bVelocityValid && pinnedSteps >= 10;
    }

    bool TestGameLocomotionProfileReload(ApplicationProcessor& processor)
    {
        using namespace Input;
        using namespace Logging;
        using namespace Locomotion;
        using namespace NorvesLib::Core::literals;
        const auto check = [](bool bPassed, const char* contract) {
            if (!bPassed)
            {
                std::cout << "設定の再読込: " << contract << '\n';
            }
            return bPassed;
        };
        const auto source = Asset::AssetFileReader().Read("Gameplay/DogLocomotion.json");
        if (!check(source.Succeeded(), "元の設定を読める"))
        {
            return false;
        }
        Container::AnsiString reloaded;
        reloaded.append(reinterpret_cast<const char*>(source.Blob.GetData()), source.Blob.GetSize());
        // Wだけでは走行指定にならず、歩様1の最高速が目標になる。6→4なら隣の歩様との整合も保つ。
        const Container::AnsiString speedKey = "\"speedMax\": 4";
        const auto position = reloaded.find(speedKey);
        if (!check(position != Container::AnsiString::npos, "歩行の最高速を特定できる"))
        {
            return false;
        }
        Container::AnsiString initial;
        initial.append(reloaded.data(), position);
        initial.append("\"speedMax\": 6");
        initial.append(reloaded.data() + position + speedKey.size(), reloaded.size() - position - speedKey.size());
        QuadrupedLocomotionParams initialParams, reloadedParams;
        Container::String error;
        const auto parse = [&](const Container::AnsiString& text, QuadrupedLocomotionParams& params) {
            return ParseQuadrupedLocomotionJson({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, params,
                                                error);
        };
        if (!check(parse(initial, initialParams) && parse(reloaded, reloadedParams) &&
                       initialParams.Gaits[1].SpeedMax == 6 && reloadedParams.Gaits[1].SpeedMax == 4,
                   "両方の設定が有効である"))
        {
            return false;
        }
        struct ProfileFixture
        {
            std::filesystem::path Root;
            ~ProfileFixture()
            {
                std::error_code ignored;
                std::filesystem::remove_all(Root, ignored);
            }
            bool Write(const Container::AnsiString& text) const
            {
                std::ofstream file(Root / "profile.json", std::ios::binary | std::ios::trunc);
                file.write(text.data(), static_cast<std::streamsize>(text.size()));
                file.close();
                return !file.fail();
            }
        } profile;
        char name[96];
        std::snprintf(name, sizeof(name), "NorvesComponentProfile_%lu_%lld", GetCurrentProcessId(),
                      static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        profile.Root = std::filesystem::temp_directory_path() / name;
        if (!check(std::filesystem::create_directory(profile.Root) && profile.Write(initial), "一時設定を書ける"))
        {
            return false;
        }
        struct WarningSink final : ILogSink
        {
            unsigned Count = 0;
            void OnLog(const LogEntry& entry) override
            {
                if (entry.level == LogLevel::Warning && entry.category == "Locomotion")
                {
                    ++Count;
                }
            }
            ~WarningSink() override
            {
                Logger::GetInstance().RemoveSink(this);
                Logger::GetInstance().Shutdown();
            }
        } warnings;
        auto& logger = Logger::GetInstance();
        logger.Shutdown();
        LogConfig logConfig;
        logConfig.minLevel = LogLevel::Trace;
        logConfig.outputType = LogOutput::None;
        logConfig.bAsyncLogging = false;
        if (!check(logger.Initialize(logConfig), "同期ログを初期化できる"))
        {
            return false;
        }
        logger.AddSink(&warnings);
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero))
        {
            return false;
        }
        // Componentの最初のInput群から出力を観測できるよう、物理本体だけ先に準備する。
        world.Tick(kFixedDeltaTime);
        if (!StepCharacter(processor) || !value.Character->GetState().bReady)
        {
            return false;
        }
        world.LateTick(kFixedDeltaTime);
        InputSystem input;
        InputRouter routes;
        input.SetRouter(&routes);
        InputMapper mapper(input.GetState());
        InputBindingSet bindings;
        InputActionDefinition move;
        move.Id = "Move"_id;
        move.Settings.Type = EInputMappingValueType::Axis2D;
        InputBinding key;
        key.Source = {EInputBindingSource::Key, static_cast<uint16_t>(KeyCode::W), 0};
        key.Component = EInputAxisComponent::Y;
        move.Bindings = {key};
        if (!bindings.AddContext("Gameplay"_id, ECursorMode::Normal) || !bindings.AddAction("Gameplay"_id, move) ||
            !mapper.ConfigureWithContext(bindings, "Gameplay"_id))
        {
            return false;
        }
        mapper.Attach(routes);
        auto* driver = world.CreateComponent<Game::Gameplay::QuadrupedLocomotionComponent>(value.Owner);
        if (!driver)
        {
            return false;
        }
        struct Unbind
        {
            Game::Gameplay::QuadrupedLocomotionComponent* Driver;
            ~Unbind()
            {
                Driver->BindInput(nullptr);
            }
        } unbind{driver};
        if (!driver->SetProfileSource(profile.Root.generic_string().c_str(), "profile.json"))
        {
            return false;
        }
        driver->BindInput(&mapper);
        input.InjectKeyEvent(KeyCode::W, InputAction::Pressed);
        double inputTime = 0;
        const auto frame = [&](float dt) {
            inputTime += dt;
            if (!mapper.BeginFrame(inputTime) || !mapper.Update(inputTime, dt))
            {
                return false;
            }
            FrameTimes times;
            times.Unscaled = dt;
            times.World = times.Animation = dt * .5f;
            world.Tick(times);
            const bool bStepped = StepCharacter(processor);
            world.LateTick(times);
            return bStepped && value.Character->GetState().bReady;
        };
        const auto speed = [&] {
            const auto velocity = driver->GetOutput().PlanarVelocity;
            return std::hypot(velocity.x, velocity.z);
        };
        const auto nearSpeed = [&](float expected) {
            return std::isfinite(speed()) && std::fabs(speed() - expected) < .01f;
        };
        // 公開モデルを基準に、既定値4と異なる設定6がdt=0の最初のフレームで使われることを確かめる。
        const auto observed = value.Character->GetState();
        LocomotionGroundInfo ground;
        ground.bReady = observed.bReady;
        ground.bGrounded = observed.bGrounded;
        ground.Normal = observed.GroundNormal;
        ground.Velocity = observed.Velocity;
        ground.PlatformVelocity = observed.PlatformVelocity;
        ground.Gravity = 9.81f * value.Character->GetSettings().GravityScale;
        LocomotionIntent intent;
        intent.Move = Math::Vector3::UnitZ;
        LocomotionState initialState, defaultState;
        LocomotionOutput expected, defaultOutput;
        if (!check(QuadrupedLocomotionModel::Step(initialState, initialParams, intent, ground, kFixedDeltaTime,
                                                 expected) == LocomotionStepStatus::Success &&
                       QuadrupedLocomotionModel::Step(defaultState, reloadedParams, intent, ground, kFixedDeltaTime,
                                                     defaultOutput) == LocomotionStepStatus::Success &&
                       (expected.PlanarVelocity - defaultOutput.PlanarVelocity).Length() > .001f && frame(0) &&
                       (driver->GetOutput().PlanarVelocity - expected.PlanarVelocity).Length() < .0001f,
                   "最初のフレームは待たずに設定を適用する"))
        {
            return false;
        }
        // 1/64秒は二進で厳密。16フレームで0.25秒になり、World時間とは別にInputのUnscaled時間を数える。
        constexpr float inputDelta = 1.0f / 64;
        for (unsigned i = 0; i < 192; ++i)
        {
            if (!frame(inputDelta))
            {
                return false;
            }
        }
        if (!check(nearSpeed(6) && driver->GetProfileError().empty() && warnings.Count == 0,
                   "初期設定の最高速に落ち着く") || !profile.Write(reloaded))
        {
            return false;
        }
        for (unsigned i = 0; i < 15; ++i)
        {
            if (!check(frame(inputDelta) && nearSpeed(6), "0.25秒に満たない間は以前の最高速を保つ"))
            {
                return false;
            }
        }
        const float beforeReload = speed();
        if (!check(frame(inputDelta) && std::isfinite(speed()) && speed() < beforeReload - .001f && speed() > 4 &&
                       driver->GetProfileError().empty() && warnings.Count == 0,
                   "0.25秒を満たす最初のフレームから新しい最高速へ減速する"))
        {
            return false;
        }
        for (unsigned i = 0; i < 192; ++i)
        {
            if (!frame(inputDelta))
            {
                return false;
            }
        }
        if (!check(nearSpeed(4), "再読込した最高速に落ち着く") || !profile.Write("{"))
        {
            return false;
        }
        for (unsigned i = 0; i < 15; ++i)
        {
            if (!check(frame(inputDelta) && nearSpeed(4) && driver->GetProfileError().empty() && warnings.Count == 0,
                       "壊れた設定も次の読込間隔までは観測しない"))
            {
                return false;
            }
        }
        if (!check(frame(inputDelta) && nearSpeed(4) && !driver->GetProfileError().empty() && warnings.Count == 1,
                   "壊れたJSONでは最後の有効値を保ち、警告を1回出す"))
        {
            return false;
        }
        for (unsigned i = 0; i < 64; ++i)
        {
            if (!check(frame(inputDelta) && nearSpeed(4) && !driver->GetProfileError().empty() && warnings.Count == 1,
                       "同じ壊れたJSONを再観測しても値と警告回数を保つ"))
            {
                return false;
            }
        }
        std::cout << "設定の再読込: 初回即時、0.25秒で6→4、壊れたJSONの警告1回を確認\n";
        return true;
    }

    bool TestGameLocomotionAnimatorValues(ApplicationProcessor& processor)
    {
        using namespace Input;
        using namespace Animation;
        using namespace NorvesLib::Core::literals;
        const auto check = [](bool bPassed, const char* contract) {
            if (!bPassed)
            {
                std::cout << "Animatorへの値: " << contract << '\n';
            }
            return bPassed;
        };
        ResourceRegistry registry;
        if (!check(registry.Initialize(), "資産の管理を初期化できる"))
        {
            return false;
        }
        auto skeleton = registry.CreateTransient<SkeletonResource>("LocomotionSkeleton");
        auto meshResource = registry.CreateTransient<SkinnedMeshResource>("LocomotionMesh");
        auto clip = registry.CreateTransient<AnimationClipResource>("LocomotionClip");
        auto asset = registry.CreateTransient<SkeletalAssetResource>("LocomotionAsset");
        auto graph = registry.CreateTransient<AnimGraphResource>("LocomotionGraph");
        auto emptyGraph = registry.CreateTransient<AnimGraphResource>("LocomotionEmptyGraph");
        if (!check(skeleton && meshResource && clip && asset && graph && emptyGraph, "試験用の資産を作れる"))
        {
            return false;
        }
        Container::VariableArray<Skeletal::SkeletalJoint> joints(1);
        joints[0].Name = "Root";
        for (size_t i = 0; i < 16; ++i)
        {
            joints[0].InverseBindMatrix[i] = Math::Matrix4x4::Identity.values[i];
        }
        skeleton->SetJoints(std::move(joints));
        Container::VariableArray<Skeletal::SkeletalVertex> vertices(3);
        for (unsigned i = 0; i < vertices.size(); ++i)
        {
            vertices[i].Position = {float(i == 1), float(i == 2), 0};
            vertices[i].Normal = {0, 0, 1};
            vertices[i].JointWeights[0] = 1;
        }
        meshResource->SetVertices(std::move(vertices));
        meshResource->SetIndices(Container::VariableArray<uint32_t>{0, 1, 2});
        Skeletal::SkeletalAnimationClip animation;
        animation.Name = "idle";
        animation.DurationSeconds = 1;
        Skeletal::SkeletalAnimationChannel channel;
        channel.JointIndex = 0;
        channel.Path = Skeletal::SkeletalAnimationPath::Translation;
        channel.Samples = {{0, {0, 0, 0, 0}}, {1, {0, 0, 0, 0}}};
        animation.Channels.push_back(std::move(channel));
        clip->SetClip(std::move(animation));
        if (!check(skeleton->Load() && meshResource->Load() && clip->Load(), "骨格・メッシュ・クリップが有効である"))
        {
            return false;
        }
        asset->SetResources(meshResource, skeleton, clip);
        struct ClipResolver final : IClipResolver
        {
            Container::TSharedPtr<AnimationClipResource> Clip;
            Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const override
            {
                return name == Container::StringView("idle") ? Clip : Container::TSharedPtr<AnimationClipResource>{};
            }
        } resolver;
        resolver.Clip = clip;
        AnimGraphReport report;
        const Container::String graphJson = R"({"version":1,"params":[
            {"name":"GaitId","type":"int","value":-1},
            {"name":"GaitWeight0","type":"float","value":-1},
            {"name":"GaitWeight1","type":"float","value":-1},
            {"name":"GaitWeight2","type":"float","value":-1},
            {"name":"GaitWeight3","type":"float","value":-1},
            {"name":"PitchAngle","type":"float","value":-1},
            {"name":"BankAngle","type":"float","value":-1},
            {"name":"ObservedSpeed","type":"float","bind":"speed"},
            {"name":"ObservedTurn","type":"float","bind":"turnRate"},
            {"name":"ObservedGrounded","type":"bool","bind":"grounded"}
            ],"nodes":[{"id":"idle","type":"clip","clip":"idle"}],"root":"idle"})";
        const Container::String emptyJson =
            R"({"version":1,"nodes":[{"id":"idle","type":"clip","clip":"idle"}],"root":"idle"})";
        if (!check(asset->Load() && graph->Compile(graphJson, *skeleton, resolver, report) && graph->Load() &&
                       emptyGraph->Compile(emptyJson, *skeleton, resolver, report) && emptyGraph->Load(),
                   "名前付きのグラフとパラメータのないグラフを組める"))
        {
            return false;
        }
        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        CharacterFixture value;
        if (!CreateCharacterFloor() || !CreateCharacter(value, Math::Vector3::Zero))
        {
            return false;
        }
        world.Tick(kFixedDeltaTime);
        const bool bReady = StepCharacter(processor) && value.Character->GetState().bReady;
        world.LateTick(kFixedDeltaTime);
        if (!check(bReady, "入力を与える前に物理本体を準備できる"))
        {
            return false;
        }
        InputSystem input;
        InputRouter routes;
        input.SetRouter(&routes);
        InputMapper mapper(input.GetState());
        InputBindingSet bindings;
        InputActionDefinition move, jump;
        move.Id = "Move"_id;
        move.Settings.Type = EInputMappingValueType::Axis2D;
        InputBinding key;
        key.Source = {EInputBindingSource::Key, static_cast<uint16_t>(KeyCode::W), 0};
        key.Component = EInputAxisComponent::Y;
        move.Bindings.push_back(key);
        key.Source.Code = static_cast<uint16_t>(KeyCode::D);
        key.Component = EInputAxisComponent::X;
        move.Bindings.push_back(key);
        jump.Id = "Jump"_id;
        key.Source.Code = static_cast<uint16_t>(KeyCode::Space);
        key.Component = EInputAxisComponent::X;
        jump.Bindings.push_back(key);
        if (!bindings.AddContext("Gameplay"_id, ECursorMode::Normal) || !bindings.AddAction("Gameplay"_id, move) ||
            !bindings.AddAction("Gameplay"_id, jump) || !mapper.ConfigureWithContext(bindings, "Gameplay"_id))
        {
            return false;
        }
        mapper.Attach(routes);
        auto* driver = world.CreateComponent<Game::Gameplay::QuadrupedLocomotionComponent>(value.Owner);
        auto* visual = world.SpawnEntity<Entity>(value.Owner);
        auto* pose = visual ? world.SpawnEntity<Entity>(visual) : nullptr;
        if (!driver || !pose)
        {
            return false;
        }
        struct Unbind
        {
            Game::Gameplay::QuadrupedLocomotionComponent* Driver;
            ~Unbind()
            {
                Driver->BindInput(nullptr);
            }
        } unbind{driver};
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(pose);
        if (!mesh)
        {
            return false;
        }
        mesh->SetSkeletalAsset(asset);
        auto* animator = world.CreateComponent<Component::AnimatorComponent>(pose);
        if (!check(animator && animator->SetGraph(graph) && driver->SetVisualRoots(visual, pose),
                   "見た目の子へAnimatorとグラフを接続できる"))
        {
            return false;
        }
        driver->BindInput(&mapper);
        input.InjectKeyEvent(KeyCode::W, InputAction::Pressed);
        double inputTime = 0;
        const auto frame = [&] {
            inputTime += kFixedDeltaTime;
            if (!mapper.BeginFrame(inputTime) || !mapper.Update(inputTime, kFixedDeltaTime))
            {
                return false;
            }
            world.Tick(kFixedDeltaTime);
            const bool bStepped = StepCharacter(processor);
            world.LateTick(kFixedDeltaTime);
            return bStepped && value.Character->GetState().bReady;
        };
        Component::AnimatorDebugSnapshot snapshot;
        const auto find = [&](Identity name, AnimParamType type) -> const AnimParamValue* {
            for (const auto& param : snapshot.Parameters)
            {
                if (param.Name == name && param.Type == type)
                {
                    return &param.Value;
                }
            }
            return nullptr;
        };
        unsigned frameIndex = 0, mismatches = 0;
        const auto compareFloat = [&](const char* name, float expected) {
            const auto* actual = find(Identity(name), AnimParamType::Float);
            const bool bMatches = actual && std::isfinite(actual->Float) &&
                                  std::fabs(actual->Float - expected) <= 1e-4f;
            if (!bMatches && mismatches++ < 4)
            {
                std::cout << "Animatorへの値: フレーム=" << frameIndex << " 名前=" << name
                          << " 実際=" << (actual ? actual->Float : -999) << " 期待=" << expected << '\n';
            }
            return bMatches;
        };
        bool bMatches = true, bBlended = false, bTurned = false, bGrounded = false, bAirborne = false;
        for (; frameIndex < 96; ++frameIndex)
        {
            if (frameIndex == 20)
            {
                input.InjectKeyEvent(KeyCode::D, InputAction::Pressed);
            }
            if (frameIndex == 48)
            {
                input.InjectKeyEvent(KeyCode::Space, InputAction::Pressed);
            }
            if (frameIndex == 49)
            {
                input.InjectKeyEvent(KeyCode::Space, InputAction::Released);
            }
            if (!frame())
            {
                return false;
            }
            animator->BuildDebugSnapshot(snapshot);
            if (!check(snapshot.bReady && snapshot.Parameters.size() == 10, "全パラメータを型付きで観測できる"))
            {
                return false;
            }
            const auto& output = driver->GetOutput();
            bMatches &= compareFloat("ObservedSpeed", output.Speed);
            bMatches &= compareFloat("ObservedTurn", output.TurnRate);
            bMatches &= compareFloat("PitchAngle", output.Pitch);
            bMatches &= compareFloat("BankAngle", output.Bank);
            const auto* gait = find("GaitId"_id, AnimParamType::Int);
            const auto* grounded = find("ObservedGrounded"_id, AnimParamType::Bool);
            bMatches &= check(gait && gait->Int == output.Gait, "歩様の識別子がモデルと一致する");
            bMatches &= check(grounded && grounded->Bool == output.bGrounded, "接地の信号がモデルと一致する");
            float sum = 0;
            unsigned activeWeights = 0;
            const char* names[] = {"GaitWeight0", "GaitWeight1", "GaitWeight2", "GaitWeight3"};
            for (unsigned i = 0; i < 4; ++i)
            {
                bMatches &= compareFloat(names[i], output.GaitWeights[i]);
                const auto* weight = find(Identity(names[i]), AnimParamType::Float);
                if (weight)
                {
                    sum += weight->Float;
                    activeWeights += weight->Float > 1e-4f && weight->Float < 1 - 1e-4f;
                }
            }
            bMatches &= check(std::isfinite(sum) && std::fabs(sum - 1) <= 1e-4f, "歩様の重みの合計が1である");
            bBlended |= activeWeights >= 2;
            bTurned |= std::fabs(output.TurnRate) > 1e-4f;
            bGrounded |= output.bGrounded;
            bAirborne |= !output.bGrounded;
        }
        if (!check(bBlended && bTurned && bGrounded && bAirborne, "歩様の遷移・旋回・接地と空中の両方を含む") ||
            !check(animator->SetGraph(emptyGraph), "名前のパラメータのないグラフへ差し替えられる"))
        {
            return false;
        }
        for (unsigned i = 0; i < 8; ++i)
        {
            const auto before = value.Owner->GetPosition();
            if (!frame())
            {
                return false;
            }
            animator->BuildDebugSnapshot(snapshot);
            const auto delta = value.Owner->GetPosition() - before;
            if (!check(snapshot.bReady && snapshot.Parameters.empty() && std::hypot(delta.x, delta.z) > 1e-4f,
                       "名前のパラメータがなくても毎フレーム本体が移動し続ける"))
            {
                return false;
            }
        }
        if (bMatches)
        {
            std::cout << "Animatorへの値: 96フレームでモデルとの一致、遷移と旋回、空グラフでの移動継続を確認\n";
        }
        return bMatches;
    }

    bool TestGameLocomotionRootMotion(ApplicationProcessor& processor)
    {
        using namespace Input;
        using namespace Animation;
        using namespace NorvesLib::Core::literals;
        const auto check = [](bool passed, const char* contract) {
            if (!passed)
                std::cout << "ルート移動: " << contract << '\n';
            return passed;
        };
        ResourceRegistry registry;
        if (!check(registry.Initialize(), "資産の管理を初期化できる"))
            return false;
        auto skeleton = registry.CreateTransient<SkeletonResource>("RootMotionSkeleton");
        auto meshResource = registry.CreateTransient<SkinnedMeshResource>("RootMotionMesh");
        auto clip = registry.CreateTransient<AnimationClipResource>("RootMotionClip");
        auto asset = registry.CreateTransient<SkeletalAssetResource>("RootMotionAsset");
        auto graph = registry.CreateTransient<AnimGraphResource>("RootMotionGraph");
        if (!check(skeleton && meshResource && clip && asset && graph, "試験用の資産を作れる"))
            return false;
        Container::VariableArray<Skeletal::SkeletalJoint> joints(1);
        joints[0].Name = "Root";
        for (size_t i = 0; i < 16; ++i)
            joints[0].InverseBindMatrix[i] = Math::Matrix4x4::Identity.values[i];
        skeleton->SetJoints(std::move(joints));
        Container::VariableArray<Skeletal::SkeletalVertex> vertices(3);
        for (unsigned i = 0; i < vertices.size(); ++i)
        {
            vertices[i].Position = {float(i == 1), float(i == 2), 0};
            vertices[i].Normal = {0, 0, 1};
            vertices[i].JointWeights[0] = 1;
        }
        meshResource->SetVertices(std::move(vertices));
        meshResource->SetIndices(Container::VariableArray<uint32_t>{0, 1, 2});
        Skeletal::SkeletalAnimationClip animation;
        animation.Name = "forward";
        animation.DurationSeconds = 1;
        animation.Metadata.Root.Mode = RootMotionMode::Extract;
        Skeletal::SkeletalAnimationChannel channel;
        channel.JointIndex = 0;
        channel.Path = Skeletal::SkeletalAnimationPath::Translation;
        channel.Samples = {{0, {0, 0, 0, 0}}, {1, {1, 0, 0, 0}}};
        animation.Channels.push_back(std::move(channel));
        clip->SetClip(std::move(animation));
        if (!check(skeleton->Load() && meshResource->Load() && clip->Load(),
                   "骨格・メッシュ・クリップが有効である"))
            return false;
        asset->SetResources(meshResource, skeleton, clip);
        struct ClipResolver final : IClipResolver
        {
            Container::TSharedPtr<AnimationClipResource> Clip;
            Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const override
            {
                return name == Container::StringView("forward") ? Clip : Container::TSharedPtr<AnimationClipResource>{};
            }
        } resolver;
        resolver.Clip = clip;
        AnimGraphReport report;
        const Container::String graphJson =
            R"({"version":1,"nodes":[{"id":"forward","type":"clip","clip":"forward","loop":true}],"root":"forward"})";
        if (!check(asset->Load() && graph->Compile(graphJson, *skeleton, resolver, report) && graph->Load(),
                   "直進するループのグラフを組める"))
            return false;

        auto& world = NorvesLib::Core::Engine::GEngine->GetWorld();
        CharacterFixture value;
        if (!check(CreateCharacterFloor() && CreateCharacter(value, Math::Vector3::Zero), "床と本体を作れる"))
            return false;
        world.Tick(kFixedDeltaTime);
        const bool ready = StepCharacter(processor) && value.Character->GetState().bReady;
        world.LateTick(kFixedDeltaTime);
        if (!check(ready, "移動の前に物理本体を準備できる"))
            return false;
        ApplicationFixedStepTestAccess::ResetRun(processor);

        InputSystem input;
        InputRouter routes;
        input.SetRouter(&routes);
        InputMapper mapper(input.GetState());
        InputBindingSet bindings;
        InputActionDefinition move, jump;
        move.Id = "Move"_id;
        move.Settings.Type = EInputMappingValueType::Axis2D;
        InputBinding key;
        key.Source = {EInputBindingSource::Key, static_cast<uint16_t>(KeyCode::W), 0};
        key.Component = EInputAxisComponent::Y;
        move.Bindings.push_back(key);
        jump.Id = "Jump"_id;
        key.Source.Code = static_cast<uint16_t>(KeyCode::Space);
        key.Component = EInputAxisComponent::X;
        jump.Bindings.push_back(key);
        if (!check(bindings.AddContext("Gameplay"_id, ECursorMode::Normal) &&
                       bindings.AddAction("Gameplay"_id, move) && bindings.AddAction("Gameplay"_id, jump) &&
                       mapper.ConfigureWithContext(bindings, "Gameplay"_id), "入力を設定できる"))
            return false;
        mapper.Attach(routes);
        auto* driver = world.CreateComponent<Game::Gameplay::QuadrupedLocomotionComponent>(value.Owner);
        auto* visual = world.SpawnEntity<Entity>(value.Owner);
        auto* pose = visual ? world.SpawnEntity<Entity>(visual) : nullptr;
        if (!check(driver && pose, "移動と見た目の子を作れる"))
            return false;
        struct Unbind
        {
            Game::Gameplay::QuadrupedLocomotionComponent* Driver;
            ~Unbind() { Driver->BindInput(nullptr); }
        } unbind{driver};
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(pose);
        if (!check(mesh != nullptr, "見た目にメッシュを付けられる"))
            return false;
        mesh->SetSkeletalAsset(asset);
        auto* animator = world.CreateComponent<Component::AnimatorComponent>(pose);
        if (!check(animator && animator->SetGraph(graph) && driver->SetVisualRoots(visual, pose) &&
                       driver->SetMotionSource(Game::Gameplay::LocomotionMotionSource::Animation),
                   "見た目のAnimatorを移動元にできる"))
            return false;
        driver->BindInput(&mapper);
        double inputTime = 0;
        const auto frame = [&](int64_t raw, FixedStepAdvanceResult& result) {
            const double seconds = double(raw) / 1'000'000'000.0;
            inputTime += seconds;
            if (!mapper.BeginFrame(inputTime) || !mapper.Update(inputTime, float(seconds)))
                return false;
            result = ApplicationFixedStepTestAccess::Simulate(processor, raw);
            return result.Status == EFixedStepAdvanceStatus::Advanced && result.DroppedSteps == 0 &&
                   value.Character->GetState().bReady;
        };
        const int64_t durations[] = {8'333'333, 16'666'667, 25'000'000};
        uint32_t stepCounts[3]{};
        uint64_t lastSerial = animator->GetRootMotionFrame().UpdateSerial;
        const auto start = value.Owner->GetPosition();
        double rootX = 0, rootZ = 0, maxError = 0;
        bool matches = true;
        for (unsigned i = 0; i < 72; ++i)
        {
            const int64_t raw = durations[i % 3];
            FixedStepAdvanceResult result;
            if (!check(frame(raw, result) && result.ExecutedSteps <= 2, "固定ステップを実行できる"))
                return false;
            ++stepCounts[result.ExecutedSteps];
            const auto observed = animator->GetRootMotionFrame();
            if (!check(observed.bValid && observed.bConsumed && observed.UpdateSerial != lastSerial &&
                           observed.Delta.IsFinite() && observed.Delta.X > 0 &&
                           std::fabs(observed.Delta.X - double(raw) / 1'000'000'000.0) < 1e-5 &&
                           std::fabs(observed.Delta.Z) < 1e-6 && std::fabs(observed.Delta.Yaw) < 1e-6,
                       "毎フレーム直進の差分が生成され消費される"))
                return false;
            lastSerial = observed.UpdateSerial;
            rootX += observed.Delta.X;
            rootZ += observed.Delta.Z;
            // この列では端数が常に当フレームの長さ以下。一定速度の差分から未消費分を引く。
            const uint64_t duration = uint64_t(raw) * 60;
            if (!check(result.RemainderScaledUnits <= duration, "端数は当フレームに収まる"))
                return false;
            const double fraction = double(result.RemainderScaledUnits) / double(duration);
            const auto position = value.Owner->GetPosition();
            const double expectedX = rootX - observed.Delta.X * fraction;
            const double expectedZ = rootZ - observed.Delta.Z * fraction;
            const double error = std::hypot(double(position.x - start.x) - expectedX,
                                            double(position.z - start.z) - expectedZ);
            maxError = std::max(maxError, error);
            if (!std::isfinite(error) || error > 1e-4)
            {
                if (matches)
                    std::printf("ルート移動: フレーム=%u 段=%llu 期待=(%.9f, %.9f) 本体=(%.9f, %.9f) 差=%.9f\n",
                                i, static_cast<unsigned long long>(result.ExecutedSteps), expectedX, expectedZ,
                                double(position.x - start.x), double(position.z - start.z), error);
                matches = false;
            }
            if (i % 3 == 2 && !check(result.RemainderScaledUnits == 0, "周期の終わりで端数がゼロになる"))
                return false;
        }
        const auto end = value.Owner->GetPosition();
        const double finalError = std::hypot(double(end.x - start.x) - rootX, double(end.z - start.z) - rootZ);
        std::printf("ルート移動: 段数 0=%u 1=%u 2=%u、差分=(%.9f, %.9f) m、本体=(%.9f, %.9f) m、差=%.9f m、最大差=%.9f m\n",
                    stepCounts[0], stepCounts[1], stepCounts[2], rootX, rootZ,
                    double(end.x - start.x), double(end.z - start.z), finalError, maxError);
        if (!check(stepCounts[0] && stepCounts[1] && stepCounts[2] && rootX > 1 &&
                       std::hypot(double(end.x - start.x), double(end.z - start.z)) > 1 && matches && finalError <= 1e-4,
                   "0・1・2段と周回を含む全フレームで消費すべき差分と本体が一致する"))
            return false;

        // 半段ぶんを未消費のまま残し、直交するW入力へ切り替える。
        FixedStepAdvanceResult pending;
        if (!check(frame(8'333'333, pending) && pending.ExecutedSteps == 0 &&
                       pending.RemainderScaledUnits > 0 && animator->GetRootMotionFrame().bValid &&
                       animator->GetRootMotionFrame().bConsumed && animator->GetRootMotionFrame().UpdateSerial != lastSerial &&
                       animator->GetRootMotionFrame().Delta.X > .008 &&
                       std::hypot(double(value.Owner->GetPosition().x - end.x),
                                  double(value.Owner->GetPosition().z - end.z)) < 1e-6,
                   "切り替え前に未消費の差分を残せる"))
            return false;
        lastSerial = animator->GetRootMotionFrame().UpdateSerial;
        if (!check(driver->SetMotionSource(Game::Gameplay::LocomotionMotionSource::Procedural),
                   "手続きの移動へ戻せる"))
            return false;
        input.InjectKeyEvent(KeyCode::W, InputAction::Pressed);
        const auto switchPosition = value.Owner->GetPosition();
        for (unsigned i = 0; i < 8; ++i)
        {
            FixedStepAdvanceResult result;
            if (!check(frame(16'666'667, result) && result.ExecutedSteps == 1, "切り替え後の固定ステップを実行できる"))
                return false;
            const auto observed = animator->GetRootMotionFrame();
            const auto position = value.Owner->GetPosition();
            if (!check(observed.bValid && observed.bConsumed && observed.UpdateSerial != lastSerial &&
                           observed.Delta.X > .01 && std::fabs(double(position.x - switchPosition.x)) < 1e-5 &&
                           position.z - switchPosition.z > 1e-6f,
                       "残りと新しいルート差分を捨て、次のフレームから入力方向へ進む"))
                return false;
            lastSerial = observed.UpdateSerial;
        }
        const auto procedural = value.Owner->GetPosition() - switchPosition;
        std::printf("ルート移動: 切り替え後の本体移動=(%.9f, %.9f) m\n", double(procedural.x), double(procedural.z));

        // 再びAnimationにしても、切り替え前の半段は再利用されない。
        if (!check(driver->SetMotionSource(Game::Gameplay::LocomotionMotionSource::Animation),
                   "再びAnimatorを移動元にできる"))
            return false;
        const auto resumePosition = value.Owner->GetPosition();
        constexpr int64_t resumeDuration = 16'666'667;
        FixedStepAdvanceResult resumed;
        if (!check(frame(resumeDuration, resumed) && resumed.ExecutedSteps == 1 &&
                       resumed.RemainderScaledUnits <= uint64_t(resumeDuration) * 60,
                   "再開したフレームで固定ステップを実行できる"))
            return false;
        const auto observed = animator->GetRootMotionFrame();
        const auto resumedPosition = value.Owner->GetPosition() - resumePosition;
        const double expected = observed.Delta.X *
                                (1 - double(resumed.RemainderScaledUnits) / double(uint64_t(resumeDuration) * 60));
        const double resumeError = std::hypot(double(resumedPosition.x) - expected, double(resumedPosition.z));
        std::printf("ルート移動: 再開後の期待=%.9f m、本体=(%.9f, %.9f) m、差=%.9f m\n",
                    expected, double(resumedPosition.x), double(resumedPosition.z), resumeError);
        return check(observed.bValid && observed.bConsumed && observed.UpdateSerial != lastSerial &&
                         observed.Delta.X > .01 && resumeError <= 1e-4,
                     "再開後は新しい差分だけを消費する");
    }

    bool TestGameLocomotionCameraSameFrame(ApplicationProcessor& processor)
    {
        using namespace Input;
        using namespace NorvesLib::Core::literals;
        const auto check = [](bool passed, const char* contract) {
            if (!passed)
                std::cout << "同じフレームの追従: " << contract << '\n';
            return passed;
        };
        auto& engine = *NorvesLib::Core::Engine::GEngine;
        auto& world = engine.GetWorld();
        CharacterFixture value;
        if (!check(CreateCharacterFloor() && CreateCharacter(value, Math::Vector3::Zero), "床と本体を作れる"))
            return false;
        world.Tick(kFixedDeltaTime);
        const bool ready = StepCharacter(processor) && value.Character->GetState().bReady;
        world.LateTick(kFixedDeltaTime);
        if (!check(ready, "移動前に物理本体を準備できる"))
            return false;
        ApplicationFixedStepTestAccess::ResetRun(processor);

        InputSystem input;
        InputRouter routes;
        input.SetRouter(&routes);
        InputMapper mapper(input.GetState());
        InputBindingSet bindings;
        InputActionDefinition move, jump;
        move.Id = "Move"_id;
        move.Settings.Type = EInputMappingValueType::Axis2D;
        InputBinding key;
        key.Source = {EInputBindingSource::Key, static_cast<uint16_t>(KeyCode::W), 0};
        key.Component = EInputAxisComponent::Y;
        move.Bindings.push_back(key);
        jump.Id = "Jump"_id;
        key.Source.Code = static_cast<uint16_t>(KeyCode::Space);
        key.Component = EInputAxisComponent::X;
        jump.Bindings.push_back(key);
        if (!check(bindings.AddContext("Gameplay"_id, ECursorMode::Normal) &&
                       bindings.AddAction("Gameplay"_id, move) && bindings.AddAction("Gameplay"_id, jump) &&
                       mapper.ConfigureWithContext(bindings, "Gameplay"_id), "入力を設定できる"))
            return false;
        mapper.Attach(routes);

        // 各固定段の物理処理前を記録し、複数段のフレームでも最後の区間を照合する。
        struct FixedPositionProbe final : Component::Component, IFixedProbe
        {
            Math::Vector3 LastPosition;
            uint64_t Count = 0;
            void OnFixed(float) override
            {
                LastPosition = GetOwner()->GetPosition();
                ++Count;
            }
        };
        auto* probe = world.CreateComponent<FixedPositionProbe>(value.Owner);
        auto* driver = world.CreateComponent<Game::Gameplay::QuadrupedLocomotionComponent>(value.Owner);
        auto* visual = world.SpawnEntity<Entity>(value.Owner);
        auto* pose = visual ? world.SpawnEntity<Entity>(visual) : nullptr;
        auto* cameraOwner = world.SpawnEntity<Entity>();
        auto* viewSource = world.SpawnEntity<Entity>();
        auto* follow = cameraOwner ? world.CreateComponent<Game::Gameplay::FollowCameraComponent>(cameraOwner) : nullptr;
        if (!check(probe && driver && visual && pose && cameraOwner && viewSource && follow,
                   "移動、見た目、独立したカメラと視線を作れる"))
            return false;
        struct Unbind
        {
            Game::Gameplay::QuadrupedLocomotionComponent* Driver;
            Game::Gameplay::FollowCameraComponent* Follow;
            ~Unbind()
            {
                Driver->BindInput(nullptr);
                Follow->BindInput(nullptr);
                Follow->BindSceneQuery(nullptr);
            }
        } unbind{driver, follow};
        if (!check(driver->SetVisualRoots(visual, pose) && driver->SetViewSource(viewSource) &&
                       follow->SetSubject(visual, value.Owner) && follow->SetViewSource(viewSource) &&
                       visual->IsRenderInterpolationEnabled(), "移動とカメラを同じ見た目と視線に接続できる"))
            return false;
        driver->BindInput(&mapper);
        follow->BindInput(&mapper);
        follow->BindSceneQuery(&engine.GetSceneQuery());
        follow->SetPitch(0);
        follow->SetYaw(0);
        follow->SetArmLength(5);
        const Math::Vector3 offset(0, .4f, 0);
        follow->SetTargetOffset(offset);
        // 最初のInput群で設定ファイルを読み終えてから、試験用の平滑0を指定する。
        world.Tick(kFixedDeltaTime);
        world.LateTick(kFixedDeltaTime);
        if (!check(follow->GetLastCollisionResult() == Camera::CameraCollisionResult::Success &&
                       follow->SetFollowSmoothing(0, 0), "カメラを初期化して平滑を無効にできる"))
            return false;

        input.InjectKeyEvent(KeyCode::W, InputAction::Pressed);
        const int64_t durations[] = {8'333'333, 16'666'667, 25'000'000};
        uint32_t stepCounts[3]{}, alphaCounts[3]{};
        uint32_t movingFrames = 0, interpolatedFrames = 0, zeroStepMovingFrames = 0, motionChecks = 0;
        float maxCameraError = 0, maxInterpolationError = 0, maxMotionError = 0, maxRenderChange = 0;
        float minPreviousError = std::numeric_limits<float>::infinity();
        float minMiddleAlpha = 1, maxMiddleAlpha = 0;
        auto previousRender = visual->GetRenderWorldTransform().position;
        auto previousBody = value.Owner->GetPosition();
        const auto startBody = previousBody;
        probe->LastPosition = previousBody;
        double inputTime = 0;
        bool matches = true;
        unsigned failures = 0;
        for (unsigned frameIndex = 0; frameIndex < 72; ++frameIndex)
        {
            const int64_t raw = durations[frameIndex % 3];
            const double seconds = double(raw) / 1'000'000'000.0;
            inputTime += seconds;
            if (!check(mapper.BeginFrame(inputTime) && mapper.Update(inputTime, float(seconds)), "入力を更新できる"))
                return false;
            const auto previousProbeCount = probe->Count;
            const auto previousCameraTicks = follow->GetCameraTickCount();
            const auto result = ApplicationFixedStepTestAccess::Simulate(processor, raw);
            if (!check(result.Status == EFixedStepAdvanceStatus::Advanced && result.DroppedSteps == 0 &&
                           result.ExecutedSteps <= 2 && value.Character->GetState().bReady &&
                           probe->Count - previousProbeCount == result.ExecutedSteps &&
                           follow->GetCameraTickCount() == previousCameraTicks + 1,
                       "固定段とカメラ群を本番の順に実行できる"))
                return false;
            ++stepCounts[result.ExecutedSteps];
            const float alpha = float(result.RemainderScaledUnits) / 1'000'000'000.0f;
            if (alpha <= 1e-6f)
                ++alphaCounts[0];
            else if (alpha >= 1 - 1e-6f)
                ++alphaCounts[2];
            else
            {
                ++alphaCounts[1];
                minMiddleAlpha = std::min(minMiddleAlpha, alpha);
                maxMiddleAlpha = std::max(maxMiddleAlpha, alpha);
            }
            const auto body = value.Owner->GetPosition();
            const auto render = visual->GetRenderWorldTransform().position;
            const auto expectedRender = probe->LastPosition + (body - probe->LastPosition) * alpha;
            const float interpolationError = (render - expectedRender).Length();
            const auto& collision = follow->GetCollisionOutput();
            const auto camera = cameraOwner->GetWorldTransform();
            const auto aim = camera.position + (camera.rotation * Math::Vector3::UnitZ) * collision.EffectiveLength;
            const float cameraError = (aim - (render + offset)).Length();
            const float renderChange = (render - previousRender).Length();
            const float previousError = (aim - (previousRender + offset)).Length();
            maxCameraError = std::max(maxCameraError, cameraError);
            maxInterpolationError = std::max(maxInterpolationError, interpolationError);
            maxRenderChange = std::max(maxRenderChange, renderChange);
            bool frameMatches = std::isfinite(cameraError) && cameraError <= 1e-4f &&
                                std::isfinite(interpolationError) && interpolationError <= 1e-4f &&
                                follow->GetLastCollisionResult() == Camera::CameraCollisionResult::Success &&
                                !collision.bObstructed && collision.EffectiveLength > 0 &&
                                (camera.position - collision.Position).Length() <= 1e-4f;
            if (result.ExecutedSteps > 0)
            {
                const auto actual = body - probe->LastPosition;
                const auto commanded = driver->GetOutput().PlanarDisplacement;
                const float motionError = std::hypot(actual.x - commanded.x, actual.z - commanded.z);
                maxMotionError = std::max(maxMotionError, motionError);
                frameMatches &= std::isfinite(motionError) && motionError <= 1e-4f;
                ++motionChecks;
            }
            else
                frameMatches &= (body - previousBody).Length() <= 1e-4f;
            if (alpha > 1e-6f && alpha < 1 - 1e-6f && (body - probe->LastPosition).Length() > .001f &&
                (render - probe->LastPosition).Length() > 1e-4f && (body - render).Length() > 1e-4f)
                ++interpolatedFrames;
            if (renderChange > .001f)
            {
                ++movingFrames;
                if (result.ExecutedSteps == 0)
                    ++zeroStepMovingFrames;
                minPreviousError = std::min(minPreviousError, previousError);
                frameMatches &= std::isfinite(previousError) && previousError > 1e-4f;
            }
            if (!frameMatches && failures++ < 4)
                std::printf("同じフレームの追従: フレーム=%u 段=%llu α=%.9f カメラ差=%.9f 補間差=%.9f 本体=(%.9f, %.9f, %.9f) 描画=(%.9f, %.9f, %.9f) 注視点=(%.9f, %.9f, %.9f)\n",
                            frameIndex, static_cast<unsigned long long>(result.ExecutedSteps), double(alpha),
                            double(cameraError), double(interpolationError), double(body.x), double(body.y),
                            double(body.z), double(render.x), double(render.y), double(render.z),
                            double(aim.x), double(aim.y), double(aim.z));
            matches &= frameMatches;
            previousRender = render;
            previousBody = body;
        }
        const float bodyDistance = (previousBody - startBody).Length();
        std::printf("同じフレームの追従: 固定段 0=%u 1=%u 2=%u、α 0=%u 中間=%u 1=%u、中間の範囲=[%.9f, %.9f]\n",
                    stepCounts[0], stepCounts[1], stepCounts[2], alphaCounts[0], alphaCounts[1], alphaCounts[2],
                    double(minMiddleAlpha), double(maxMiddleAlpha));
        std::printf("同じフレームの追従: カメラ最大差=%.9f m、補間最大差=%.9f m、本体移動最大差=%.9f m、本体移動距離=%.9f m\n",
                    double(maxCameraError), double(maxInterpolationError), double(maxMotionError), double(bodyDistance));
        std::printf("同じフレームの追従: 描画の最大移動=%.9f m、前フレームとの差の最小=%.9f m、移動=%u 中間補間=%u 0段で描画移動=%u 本体照合=%u\n",
                    double(maxRenderChange), double(minPreviousError), movingFrames, interpolatedFrames,
                    zeroStepMovingFrames, motionChecks);
        return check(matches && stepCounts[0] && stepCounts[1] && stepCounts[2] && alphaCounts[1] &&
                         movingFrames >= 10 && interpolatedFrames >= 10 && zeroStepMovingFrames >= 1 &&
                         motionChecks >= 10 && bodyDistance > .1f && maxRenderChange > .001f,
                     "移動と補間を伴う全フレームで、カメラは同じフレームの見た目を追う");
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
        case 8:
            return TestCharacterQueuedPlatform(processor);
        case 9:
            return TestCharacterTeleportAndLifecycle(processor);
        case 10:
            return TestCharacterLandedRemoval(processor);
        case 11:
            return TestCharacterDriveEquality(processor);
        case 12:
            return TestCharacterBeforeSimulationRemoval(processor);
        case 13:
            return TestGameLocomotionInput(processor);
        case 14:
            return TestGameLocomotionHeapAfterWorld();
        case 15:
            return TestFollowCameraRenderBinding(processor);
        case 16:
            return TestDynamicRenderInterpolationDefault(processor);
        case 17:
            return TestDynamicInterpolationAt144Hz(processor, 60);
        case 18:
            return TestDynamicInterpolationAt144Hz(processor, 120);
        case 19:
            return TestCharacterAddedAfterTickJoinsNextCollection(processor);
        case 20:
            return TestCharacterGravityScale(processor);
        case 21:
            return TestCharacterSteepSlopeSlide(processor);
        case 22:
            return TestCharacterElevatorJitter(processor);
        case 23:
            return TestCharacterPlatformDepartureVelocity(processor);
        case 24:
            return TestCharacterExternalMovement(processor);
        case 25:
            return TestCharacterPushDynamicSphere(processor);
        case 26:
            return TestCharacterVelocityAndDepenetration(processor);
        case 27:
            return TestCharacterPlatformPushedAgainstWall(processor);
        case 28:
            return TestGameLocomotionProfileReload(processor);
        case 29:
            return TestGameLocomotionAnimatorValues(processor);
        case 30:
            return TestGameLocomotionRootMotion(processor);
        case 31:
            return TestGameLocomotionCameraSameFrame(processor);
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
        bPassed &= RunChildProcess(caseIndex);
    }
    std::cout << (bPassed ? "PhysicsFixedStepPipelineTest passed\n" : "PhysicsFixedStepPipelineTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
