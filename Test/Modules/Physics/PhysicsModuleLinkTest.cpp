// PhysicsModuleLinkTest — Physics module の公開境界、registry lifecycle と
// SceneQuery binding の実際の観測結果を検証する。

#include "Physics/IPhysicsModule.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Scene/SceneQuery.h"
#include "Thread/Thread.h"

#include <cassert>
#include <iostream>
#include <type_traits>
#include <Windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;
using namespace NorvesLib::Core::Scene;

namespace
{
    void ConfigureFailureReporting()
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

    template <typename T>
    concept HasPublicPhysicsQuery = requires(
        T& module,
        const Math::Ray& ray,
        const Math::Sphere& sphere,
        PhysicsRaycastHit& raycastHit,
        Container::VariableArray<PhysicsOverlapHit>& overlapHits,
        ColliderHandle collider,
        bool& bAlive)
    {
        module.Raycast(ray, 1.0f, raycastHit);
        module.OverlapSphere(sphere, overlapHits);
        module.IsAlive(collider, bAlive);
    };

    static_assert(!std::is_base_of_v<
        IPhysicsSceneQueryProvider,
        NorvesLib::Modules::Physics::IPhysicsModule>);
    static_assert(!HasPublicPhysicsQuery<NorvesLib::Modules::Physics::IPhysicsModule>);

    Engine::Engine& LeakedEngineRef()
    {
        static Engine::Engine* s_Engine = new Engine::Engine();
        return *s_Engine;
    }

    class ForeignProvider final : public IPhysicsSceneQueryProvider
    {
    public:
        EPhysicsSceneQueryResult Raycast(
            const Math::Ray& /*ray*/,
            float /*maxDistance*/,
            PhysicsRaycastHit& /*outHit*/) const override
        {
            ++RaycastCallCount;
            return EPhysicsSceneQueryResult::NoHit;
        }

        EPhysicsSceneQueryResult OverlapSphere(
            const Math::Sphere& /*sphere*/,
            Container::VariableArray<PhysicsOverlapHit>& /*outHits*/) const override
        {
            return EPhysicsSceneQueryResult::NoHit;
        }

        EPhysicsSceneQueryResult OverlapBox(
            const Math::OBB& /*box*/,
            Container::VariableArray<PhysicsOverlapHit>& /*outHits*/) const override
        {
            return EPhysicsSceneQueryResult::NoHit;
        }

        EPhysicsSceneQueryResult OverlapCapsule(
            const Math::Capsule& /*capsule*/,
            Container::VariableArray<PhysicsOverlapHit>& /*outHits*/) const override
        {
            return EPhysicsSceneQueryResult::NoHit;
        }

        EPhysicsSceneQueryResult IsAlive(ColliderHandle /*collider*/, bool& outAlive) const override
        {
            outAlive = false;
            return EPhysicsSceneQueryResult::Success;
        }

        EPhysicsSceneQueryResult IsAlive(BodyHandle /*body*/, bool& outAlive) const override
        {
            outAlive = false;
            return EPhysicsSceneQueryResult::Success;
        }

        mutable uint32_t RaycastCallCount = 0;
    };

    class FailingModule final : public IModule
    {
    public:
        FailingModule(const char* name, bool bInstallSucceeds, bool bInitializeSucceeds)
            : m_Name(name)
            , m_Id(name)
            , m_bInstallSucceeds(bInstallSucceeds)
            , m_bInitializeSucceeds(bInitializeSucceeds)
        {
        }

        Identity GetModuleId() const override
        {
            return m_Id;
        }

        const char* GetName() const override
        {
            return m_Name;
        }

        bool Install(Engine::Engine& /*engine*/) override
        {
            return m_bInstallSucceeds;
        }

        bool Initialize() override
        {
            return m_bInitializeSucceeds;
        }

        void Shutdown() override
        {
        }

    private:
        const char* m_Name;
        Identity m_Id;
        bool m_bInstallSucceeds;
        bool m_bInitializeSucceeds;
    };

    EPhysicsSceneQueryResult QueryRaycast(SceneQuery& sceneQuery)
    {
        const Math::Ray ray(Math::Vector3(), Math::Vector3(1.0f, 0.0f, 0.0f));
        PhysicsRaycastHit hit;
        return sceneQuery.Raycast(ray, 1.0f, hit);
    }

    void AssertEmptySnapshotReady(SceneQuery& sceneQuery)
    {
        const Math::Sphere sphere(Math::Vector3(), 1.0f);
        Container::VariableArray<PhysicsOverlapHit> hits;
        bool bColliderAlive = true;
        bool bBodyAlive = true;

        assert(QueryRaycast(sceneQuery) == EPhysicsSceneQueryResult::NoHit);
        assert(sceneQuery.OverlapSphere(sphere, hits) == EPhysicsSceneQueryResult::NoHit);
        assert(hits.empty());
        assert(sceneQuery.IsAlive(ColliderHandle{}, bColliderAlive) == EPhysicsSceneQueryResult::Success);
        assert(!bColliderAlive);
        assert(sceneQuery.IsAlive(BodyHandle{}, bBodyAlive) == EPhysicsSceneQueryResult::Success);
        assert(!bBodyAlive);
    }

    void TestRegisterFindDuplicateAndPublicBoundary()
    {
        std::cout << "[Test] physics register/find/duplicate and public query boundary\n";
        ModuleRegistry registry;
        const Identity physicsId("NorvesPhysicsModule");

        NorvesLib::Modules::Physics::IPhysicsModule* physics =
            NorvesLib::Modules::Physics::RegisterPhysicsModule(registry);
        assert(physics != nullptr);
        assert(NorvesLib::Modules::Physics::FindPhysicsModule(registry) == physics);
        assert(registry.FindModule(physicsId) == physics);
        assert(NorvesLib::Modules::Physics::RegisterPhysicsModule(registry) == physics);
        assert(NorvesLib::Modules::Physics::FindPhysicsModule(registry) == physics);
    }

    void TestLifecyclePublishesEmptyReadySnapshotAndUnbinds()
    {
        std::cout << "[Test] physics lifecycle publishes empty ready snapshot and unbinds\n";
        ModuleRegistry registry;
        Engine::Engine& engine = LeakedEngineRef();
        NorvesLib::Modules::Physics::IPhysicsModule* physics =
            NorvesLib::Modules::Physics::RegisterPhysicsModule(registry);

        assert(registry.InstallAll(engine));
        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::NotReady);

        physics->FixedTick(1.0f / 60.0f);
        AssertEmptySnapshotReady(engine.GetSceneQuery());

        registry.ShutdownAll(engine);
        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::Unavailable);
        registry.ShutdownAll(engine);
        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::Unavailable);
    }

    void TestForeignProviderIsNotOverwritten()
    {
        std::cout << "[Test] physics install leaves foreign provider bound\n";
        ModuleRegistry registry;
        Engine::Engine& engine = LeakedEngineRef();
        ForeignProvider foreignProvider;

        assert(engine.GetSceneQuery().BindPhysicsProvider(foreignProvider) == EPhysicsSceneQueryResult::Success);
        NorvesLib::Modules::Physics::RegisterPhysicsModule(registry);
        assert(!registry.InstallAll(engine));
        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::NoHit);
        assert(foreignProvider.RaycastCallCount == 1);
        assert(engine.GetSceneQuery().UnbindPhysicsProvider(foreignProvider) == EPhysicsSceneQueryResult::Success);
    }

    void TestRollbackAfterLaterInstallFailureUnbinds()
    {
        std::cout << "[Test] rollback after later install failure unbinds physics\n";
        ModuleRegistry registry;
        Engine::Engine& engine = LeakedEngineRef();

        NorvesLib::Modules::Physics::RegisterPhysicsModule(registry);
        registry.Register(MakeUnique<FailingModule>("PhysicsInstallFailure", false, true));
        assert(!registry.InstallAll(engine));
        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::Unavailable);
    }

    void TestRollbackAfterLaterInitializeFailureUnbinds()
    {
        std::cout << "[Test] rollback after later initialize failure unbinds physics\n";
        ModuleRegistry registry;
        Engine::Engine& engine = LeakedEngineRef();

        NorvesLib::Modules::Physics::RegisterPhysicsModule(registry);
        registry.Register(MakeUnique<FailingModule>("PhysicsInitializeFailure", true, false));
        assert(!registry.InstallAll(engine));
        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::Unavailable);
    }

    void TestDestroyedModuleLeavesSceneQueryUnbound()
    {
        std::cout << "[Test] destroyed physics module leaves scene query unbound\n";
        Engine::Engine& engine = LeakedEngineRef();
        {
            ModuleRegistry registry;
            NorvesLib::Modules::Physics::IPhysicsModule* physics =
                NorvesLib::Modules::Physics::RegisterPhysicsModule(registry);
            assert(registry.InstallAll(engine));
            physics->FixedTick(1.0f / 60.0f);
            AssertEmptySnapshotReady(engine.GetSceneQuery());
            registry.ShutdownAll(engine);
        }

        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::Unavailable);
    }

    void TestInstallRejectsDifferentRegistrationThread()
    {
        std::cout << "[Test] physics install rejects different registration thread\n";
        ModuleRegistry registry;
        Engine::Engine& engine = LeakedEngineRef();

        Thread::Thread worker([&registry]()
        {
            assert(NorvesLib::Modules::Physics::RegisterPhysicsModule(registry) != nullptr);
        });
        worker.Join();

        assert(!registry.InstallAll(engine));
        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::Unavailable);
    }

    void TestWrongThreadTeardownPreservesReadyBinding()
    {
        std::cout << "[Test] wrong-thread teardown preserves ready physics binding\n";
        ModuleRegistry registry;
        Engine::Engine& engine = LeakedEngineRef();
        NorvesLib::Modules::Physics::IPhysicsModule* physics =
            NorvesLib::Modules::Physics::RegisterPhysicsModule(registry);

        assert(registry.InstallAll(engine));
        physics->FixedTick(1.0f / 60.0f);
        AssertEmptySnapshotReady(engine.GetSceneQuery());

        Thread::Thread worker([physics, &engine]()
        {
            physics->Shutdown();
            physics->Uninstall(engine);
        });
        worker.Join();

        AssertEmptySnapshotReady(engine.GetSceneQuery());
        registry.ShutdownAll(engine);
        assert(QueryRaycast(engine.GetSceneQuery()) == EPhysicsSceneQueryResult::Unavailable);
    }
} // namespace

int main()
{
    ConfigureFailureReporting();

    std::cout << "PhysicsModuleLinkTest start\n";
    TestRegisterFindDuplicateAndPublicBoundary();
    TestLifecyclePublishesEmptyReadySnapshotAndUnbinds();
    TestForeignProviderIsNotOverwritten();
    TestRollbackAfterLaterInstallFailureUnbinds();
    TestRollbackAfterLaterInitializeFailureUnbinds();
    TestDestroyedModuleLeavesSceneQueryUnbound();
    TestInstallRejectsDifferentRegistrationThread();
    TestWrongThreadTeardownPreservesReadyBinding();
    std::cout << "PhysicsModuleLinkTest passed\n";
    return 0;
}
