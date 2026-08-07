// PhysicsModuleLinkTest — Physics module の公開境界、registry lifecycle と
// SceneQuery binding の実際の観測結果を検証する。

#include "Physics/IPhysicsModule.h"
#include "Physics/ColliderComponent.h"
#include "Physics/PhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Scene/SceneQuery.h"
#include "Thread/Thread.h"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <type_traits>
#include <Windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;
using namespace NorvesLib::Core::Scene;
using PhysicsModule = NorvesLib::Modules::Physics::PhysicsModule;

namespace NorvesLib::Modules::Physics
{
    class PhysicsModuleTestAccess
    {
    public:
        static uint32_t GetColliderSlotCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_ColliderSlots.size());
        }

        static uint32_t GetBodySlotCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_BodySlots.size());
        }

        static uint32_t GetActiveColliderCount(const IPhysicsModule& module)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            uint32_t count = 0;
            for (const PhysicsModule::ColliderSlot& slot : concrete.m_ColliderSlots)
            {
                if (slot.bOccupied && slot.bActive)
                {
                    ++count;
                }
            }
            return count;
        }

        static uint32_t GetRegisteredColliderCount(const IPhysicsModule& module)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            uint32_t count = 0;
            for (const PhysicsModule::ColliderSlot& slot : concrete.m_ColliderSlots)
            {
                count += slot.bOccupied ? 1u : 0u;
            }
            return count;
        }

        static uint32_t GetRegisteredBodyCount(const IPhysicsModule& module)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            uint32_t count = 0;
            for (const PhysicsModule::BodySlot& slot : concrete.m_BodySlots)
            {
                count += slot.bOccupied ? 1u : 0u;
            }
            return count;
        }

        static bool IsBodyActive(const IPhysicsModule& module, BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_BodySlots.size()
                && concrete.m_BodySlots[handle.Index].bOccupied
                && concrete.m_BodySlots[handle.Index].Generation == handle.Generation
                && concrete.m_BodySlots[handle.Index].bActive;
        }

        static bool IsColliderAlive(const IPhysicsModule& module, ColliderHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_ColliderSlots.size()
                && concrete.m_ColliderSlots[handle.Index].bOccupied
                && concrete.m_ColliderSlots[handle.Index].Generation == handle.Generation;
        }

        static bool IsBodyAlive(const IPhysicsModule& module, BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_BodySlots.size()
                && concrete.m_BodySlots[handle.Index].bOccupied
                && concrete.m_BodySlots[handle.Index].Generation == handle.Generation;
        }

        static uint32_t GetDuplicateDiagnosticCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_DuplicateDiagnosticCount;
        }

        static EPhysicsDiagnostic GetLastDiagnostic(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_LastDiagnostic;
        }

        static uint32_t GetCallbackCount(const IPhysicsModule& module, const ColliderComponent& component)
        {
            return GetConcrete(module).GetCallbackCount(component);
        }

        static EPhysicsResult GetLastRegistrationResult(
            const IPhysicsModule& module,
            const ColliderComponent& component)
        {
            return GetConcrete(module).GetColliderRegistrationResult(component);
        }

        static void PrepareColliderGenerationWrap(IPhysicsModule& module, ColliderComponent& component)
        {
            PhysicsModule& concrete = GetConcrete(module);
            concrete.PrepareColliderGenerationWrap(component);
        }

        static void PrepareBodyGenerationWrap(IPhysicsModule& module, RigidBodyComponent& component)
        {
            GetConcrete(module).PrepareBodyGenerationWrap(component);
        }

        static void PrepareOverlapBeginGenerationWrap(
            IPhysicsModule& module,
            ColliderComponent& component,
            PhysicsCallbackHandle& handle)
        {
            GetConcrete(module).PrepareOverlapBeginGenerationWrap(component, handle);
        }

        static float GetColliderRadius(const IPhysicsModule& module, const ColliderComponent& component)
        {
            return GetConcrete(module).GetColliderRadius(component);
        }

        static uint32_t GetDispatchedEventCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_DispatchedEventCount;
        }

        static uint32_t GetPendingEventCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_PendingEventCount;
        }

        static uint32_t GetLifecycleStateCount(const IPhysicsModule& module)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return (concrete.m_bBound ? 1u : 0u)
                + (concrete.m_bInitialized ? 1u : 0u)
                + (concrete.m_bHasPublishedSnapshot ? 1u : 0u);
        }

    private:
        static const PhysicsModule& GetConcrete(const IPhysicsModule& module)
        {
            const auto* concrete = dynamic_cast<const PhysicsModule*>(&module);
            assert(concrete != nullptr);
            return *concrete;
        }

        static PhysicsModule& GetConcrete(IPhysicsModule& module)
        {
            auto* concrete = dynamic_cast<PhysicsModule*>(&module);
            assert(concrete != nullptr);
            return *concrete;
        }
    };
} // namespace NorvesLib::Modules::Physics

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

    void TestModuleLifecycleRegistersPhysicsReflectedTypes()
    {
        std::cout << "[Test] module lifecycle registers Physics reflected types\n";
        ClassRegistry& classRegistry = ClassRegistry::Get();
        const Identity colliderClassName("ColliderComponent");
        const Identity bodyClassName("RigidBodyComponent");
        assert(classRegistry.FindClass(colliderClassName) == nullptr);
        assert(classRegistry.FindClass(bodyClassName) == nullptr);

        ModuleRegistry registry;
        Engine::Engine& engine = LeakedEngineRef();
        assert(NorvesLib::Modules::Physics::RegisterPhysicsModule(registry) != nullptr);
        assert(registry.InstallAll(engine));

        const IClass* colliderClass = classRegistry.FindClass(colliderClassName);
        const IClass* bodyClass = classRegistry.FindClass(bodyClassName);
        assert(colliderClass != nullptr);
        assert(bodyClass != nullptr);
        assert(colliderClass->GetParentClass() != nullptr);
        assert(bodyClass->GetParentClass() != nullptr);
        assert(colliderClass->GetParentClass()->GetClassName() == Identity("Component"));
        assert(bodyClass->GetParentClass()->GetClassName() == Identity("Component"));

        registry.ShutdownAll(engine);
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

    bool HasRequiredPhysicsRegistrationPrefix(const Container::String& source);

    void TestStrictGamePhysicsContractRejectsMutations()
    {
        std::cout << "[Test] strict Game Physics contract rejects representative mutations\n";
        const Container::String validSource =
            "#include \"Physics/IPhysicsModule.h\"\n"
            "bool GameApplicationHandler::OnPreInitialize(const VariableArray<String>& args)\n"
            "{\n"
            "    LOG_INFO(\"GameApplicationHandler::OnPreInitialize()\");\n"
            "    if (NorvesLib::Modules::Physics::RegisterPhysicsModule(\n"
            "            NorvesLib::Core::Module::GetModuleRegistry()) == nullptr)\n"
            "    {\n"
            "        LOG_ERROR(\"Physics module registration failed\");\n"
            "        return false;\n"
            "    }\n"
            "    m_M6ScriptSmokeController.Configure(args);\n"
            "}\n";
        const Container::String nullHandlingRemoved =
            "#include \"Physics/IPhysicsModule.h\"\n"
            "bool GameApplicationHandler::OnPreInitialize(const VariableArray<String>& args)\n"
            "{\n"
            "    LOG_INFO(\"GameApplicationHandler::OnPreInitialize()\");\n"
            "    NorvesLib::Modules::Physics::RegisterPhysicsModule(\n"
            "        NorvesLib::Core::Module::GetModuleRegistry());\n"
            "    m_M6ScriptSmokeController.Configure(args);\n"
            "}\n";
        const Container::String conditionalRegistration =
            "#include \"Physics/IPhysicsModule.h\"\n"
            "bool GameApplicationHandler::OnPreInitialize(const VariableArray<String>& args)\n"
            "{\n"
            "    LOG_INFO(\"GameApplicationHandler::OnPreInitialize()\");\n"
            "    if (bPhysics && NorvesLib::Modules::Physics::RegisterPhysicsModule(\n"
            "            NorvesLib::Core::Module::GetModuleRegistry()) == nullptr)\n"
            "    {\n"
            "        LOG_ERROR(\"Physics module registration failed\");\n"
            "        return false;\n"
            "    }\n"
            "    m_M6ScriptSmokeController.Configure(args);\n"
            "}\n";
        const Container::String configureBeforeRegistration =
            "#include \"Physics/IPhysicsModule.h\"\n"
            "bool GameApplicationHandler::OnPreInitialize(const VariableArray<String>& args)\n"
            "{\n"
            "    LOG_INFO(\"GameApplicationHandler::OnPreInitialize()\");\n"
            "    m_M6ScriptSmokeController.Configure(args);\n"
            "    if (NorvesLib::Modules::Physics::RegisterPhysicsModule(\n"
            "            NorvesLib::Core::Module::GetModuleRegistry()) == nullptr)\n"
            "    {\n"
            "        LOG_ERROR(\"Physics module registration failed\");\n"
            "        return false;\n"
            "    }\n"
            "}\n";
        const Container::String commentedFalsePositive =
            "/*\n"
            "bool GameApplicationHandler::OnPreInitialize(const VariableArray<String>& args)\n"
            "{\n"
            "    LOG_INFO(\"GameApplicationHandler::OnPreInitialize()\");\n"
            "    if (NorvesLib::Modules::Physics::RegisterPhysicsModule(\n"
            "            NorvesLib::Core::Module::GetModuleRegistry()) == nullptr)\n"
            "    {\n"
            "        LOG_ERROR(\"Physics module registration failed\");\n"
            "        return false;\n"
            "    }\n"
            "    m_M6ScriptSmokeController.Configure(args);\n"
            "}\n"
            "*/\n"
            "bool GameApplicationHandler::OnPreInitialize(const VariableArray<String>& args)\n"
            "{\n"
            "    LOG_INFO(\"GameApplicationHandler::OnPreInitialize()\");\n"
            "    m_M6ScriptSmokeController.Configure(args);\n"
            "}\n";

        assert(HasRequiredPhysicsRegistrationPrefix(validSource));
        assert(!HasRequiredPhysicsRegistrationPrefix(nullHandlingRemoved));
        assert(!HasRequiredPhysicsRegistrationPrefix(conditionalRegistration));
        assert(!HasRequiredPhysicsRegistrationPrefix(configureBeforeRegistration));
        assert(!HasRequiredPhysicsRegistrationPrefix(commentedFalsePositive));
    }

    Container::String ReadSourceFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return {};
        }

        Container::String result;
        char buffer[4096]{};
        while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0)
        {
            result.append(buffer, static_cast<size_t>(input.gcount()));
        }
        return result;
    }

    Container::String RemoveAsciiWhitespace(const Container::String& source)
    {
        Container::String result;
        result.reserve(source.size());
        for (char character : source)
        {
            if (!std::isspace(static_cast<unsigned char>(character)))
            {
                result.push_back(character);
            }
        }
        return result;
    }

    bool HasRequiredPhysicsRegistrationPrefix(const Container::String& source)
    {
        constexpr const char* kAnchor = "boolGameApplicationHandler::OnPreInitialize(";
        constexpr const char* kRequiredPrefix =
            "{LOG_INFO(\"GameApplicationHandler::OnPreInitialize()\");"
            "if(NorvesLib::Modules::Physics::RegisterPhysicsModule("
            "NorvesLib::Core::Module::GetModuleRegistry())==nullptr){"
            "LOG_ERROR(\"Physicsmoduleregistrationfailed\");returnfalse;}"
            "m_M6ScriptSmokeController.Configure(args);";

        const Container::String compact = RemoveAsciiWhitespace(source);
        const size_t anchorOffset = compact.find(kAnchor);
        if (anchorOffset == Container::String::npos ||
            compact.find(kAnchor, anchorOffset + Container::String(kAnchor).size()) != Container::String::npos)
        {
            return false;
        }

        const size_t openingBrace = compact.find('{', anchorOffset);
        if (openingBrace == Container::String::npos)
        {
            return false;
        }

        return compact.find(kRequiredPrefix, openingBrace) == openingBrace;
    }

    void TestGamePhysicsRegistrationSourceContract()
    {
        std::cout << "[Test] Game unconditionally registers Physics before command-line parsing\n";
        const std::filesystem::path sourceRoot(NORVES_SOURCE_ROOT);
        const Container::String handlerSource = ReadSourceFile(
            sourceRoot / "Game/GameApplicationHandler.cpp");

        assert(HasRequiredPhysicsRegistrationPrefix(handlerSource));
    }

    void TestPhysicsModuleCMakeContract()
    {
        std::cout << "[Test] Physics module keeps private Core dependencies and C++23 requirements private\n";
        const std::filesystem::path sourceRoot(NORVES_SOURCE_ROOT);
        const Container::String cmakeSource = RemoveAsciiWhitespace(ReadSourceFile(
            sourceRoot / "Library/Modules/Physics/CMakeLists.txt"));

        assert(cmakeSource.find(
            "add_library(${MODULE_NAME}STATIC${PHYSICS_PUBLIC_HEADERS}${PHYSICS_PRIVATE_SOURCES})")
            != Container::String::npos);
        assert(cmakeSource.find(
            "target_include_directories(${MODULE_NAME}PUBLIC${CMAKE_CURRENT_SOURCE_DIR}/PublicPRIVATE"
            "${CMAKE_CURRENT_SOURCE_DIR}/Private${CMAKE_SOURCE_DIR})") != Container::String::npos);
        assert(cmakeSource.find("target_compile_features(${MODULE_NAME}PRIVATEcxx_std_23)")
            != Container::String::npos);
        assert(cmakeSource.find("target_compile_features(${MODULE_NAME}PUBLICcxx_std_23)")
            == Container::String::npos);
        assert(cmakeSource.find("target_compile_features(${MODULE_NAME}INTERFACEcxx_std_23)")
            == Container::String::npos);
        assert(cmakeSource.find(
            "source_group(TREE${CMAKE_CURRENT_SOURCE_DIR}FILES${PHYSICS_PUBLIC_HEADERS}${PHYSICS_PRIVATE_SOURCES})")
            != Container::String::npos);
    }

    void TestPhaseThreeUnregisteredComponentsRemainInvalid()
    {
        std::cout << "[Test] phase 3 unregistered Components remain invalid\n";
        NorvesLib::Modules::Physics::ColliderComponent collider;
        NorvesLib::Modules::Physics::RigidBodyComponent body;
        collider.Initialize();
        body.Initialize();

        assert(!collider.GetColliderHandle().IsValid());
        assert(!body.GetBodyHandle().IsValid());
        assert(collider.SetSphere(1.0f) == NorvesLib::Modules::Physics::EPhysicsResult::NotRegistered);
        assert(body.SetMass(1.0f) == NorvesLib::Modules::Physics::EPhysicsResult::NotRegistered);
        collider.Finalize();
        body.Finalize();
        std::cout << "[PASS] Phase 3 unregistered Component handles stay invalid\n";
    }

    void TestPhaseThreeComponentOwnershipAndSlots()
    {
        std::cout << "[Test] phase 3 Component ownership, slots, validation, callback tokens and reflection\n";
        ModuleRegistry& registry = GetModuleRegistry();
        Engine::Engine& engine = LeakedEngineRef();

        World unregisteredWorld;
        unregisteredWorld.Initialize();
        Entity* unregisteredOwner = unregisteredWorld.SpawnEntity<Entity>();
        assert(unregisteredOwner != nullptr);
        auto* unregisteredCollider =
            unregisteredWorld.CreateComponent<NorvesLib::Modules::Physics::ColliderComponent>(unregisteredOwner);
        assert(unregisteredCollider != nullptr);
        assert(!unregisteredCollider->GetColliderHandle().IsValid());

        NorvesLib::Modules::Physics::IPhysicsModule* physics =
            NorvesLib::Modules::Physics::RegisterPhysicsModule(registry);
        assert(physics != nullptr);
        assert(!unregisteredCollider->GetColliderHandle().IsValid());
        unregisteredWorld.Finalize();
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetColliderSlotCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetBodySlotCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetActiveColliderCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetRegisteredColliderCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetRegisteredBodyCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetLifecycleStateCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetDispatchedEventCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetPendingEventCount(*physics) == 0);

        const IClass* colliderClass = NorvesLib::Modules::Physics::ColliderComponent::StaticClass();
        const IClass* bodyClass = NorvesLib::Modules::Physics::RigidBodyComponent::StaticClass();
        assert(colliderClass != nullptr && bodyClass != nullptr);
        assert(colliderClass->GetParentClass() == Core::Component::Component::StaticClass());
        assert(bodyClass->GetParentClass() == Core::Component::Component::StaticClass());
        assert(colliderClass->GetClassId() != 0 && bodyClass->GetClassId() != 0);
        assert(colliderClass->GetClassId() != bodyClass->GetClassId());
        assert(ClassRegistry::Get().FindClass(colliderClass->GetClassId()) == colliderClass);
        assert(ClassRegistry::Get().FindClass(bodyClass->GetClassId()) == bodyClass);

        World world;
        world.Initialize();
        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);
        auto* collider = world.CreateComponent<NorvesLib::Modules::Physics::ColliderComponent>(root);
        assert(collider != nullptr && collider->GetOuter() == root);
        assert(collider->GetClass() == colliderClass);
        const ColliderHandle firstHandle = collider->GetColliderHandle();
        assert(firstHandle.IsValid());
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetRegisteredColliderCount(*physics) == 1);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetActiveColliderCount(*physics) == 0);
        assert(registry.InstallAll(engine));

        auto* duplicateCollider = world.CreateComponent<NorvesLib::Modules::Physics::ColliderComponent>(root);
        assert(duplicateCollider != nullptr && duplicateCollider->GetOuter() == root);
        assert(!duplicateCollider->GetColliderHandle().IsValid());
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetLastRegistrationResult(*physics, *duplicateCollider)
            == NorvesLib::Modules::Physics::EPhysicsResult::Duplicate);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetLastDiagnostic(*physics)
            == NorvesLib::Modules::Physics::EPhysicsDiagnostic::Duplicate);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetDuplicateDiagnosticCount(*physics) == 1);

        assert(collider->SetSphere(0.0f) == NorvesLib::Modules::Physics::EPhysicsResult::InvalidArgument);
        assert(collider->SetSphere(std::numeric_limits<float>::infinity())
            == NorvesLib::Modules::Physics::EPhysicsResult::InvalidArgument);
        assert(collider->SetBox(Math::Vector3(1.0f, 0.0f, 1.0f)) == NorvesLib::Modules::Physics::EPhysicsResult::InvalidArgument);
        assert(collider->SetCapsule(1.0f, 0.0f) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(collider->SetTrigger(true) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        physics->FixedTick(1.0f / 60.0f);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetActiveColliderCount(*physics) == 1);

        auto* body = world.CreateComponent<NorvesLib::Modules::Physics::RigidBodyComponent>(root);
        assert(body != nullptr && body->GetBodyHandle().IsValid());
        assert(body->GetClass() == bodyClass);
        const BodyHandle bodyHandle = body->GetBodyHandle();
        assert(body->SetMass(0.0f) == NorvesLib::Modules::Physics::EPhysicsResult::InvalidArgument);
        assert(body->SetMass(std::numeric_limits<float>::quiet_NaN())
            == NorvesLib::Modules::Physics::EPhysicsResult::InvalidArgument);
        assert(body->SetMass(2.0f) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(body->SetGravityScale(-1.0f) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(body->SetGravityScale(0.0f) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(body->SetLinearVelocity(Math::Vector3(1.0f, 2.0f, 3.0f)) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(body->SetLinearVelocity(Math::Vector3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f))
            == NorvesLib::Modules::Physics::EPhysicsResult::InvalidArgument);
        assert(body->AddImpulse(Math::Vector3(1.0f, 0.0f, 0.0f)) == NorvesLib::Modules::Physics::EPhysicsResult::InvalidState);
        assert(body->GetLinearVelocity() == Math::Vector3(1.0f, 2.0f, 3.0f));
        assert(body->SetBodyType(NorvesLib::Modules::Physics::EPhysicsBodyType::Dynamic)
            == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(body->SetBodyType(static_cast<NorvesLib::Modules::Physics::EPhysicsBodyType>(255))
            == NorvesLib::Modules::Physics::EPhysicsResult::InvalidArgument);
        assert(body->GetBodyType() == NorvesLib::Modules::Physics::EPhysicsBodyType::Dynamic);
        assert(body->AddImpulse(Math::Vector3(1.0f, 0.0f, 0.0f)) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(body->GetLinearVelocity() == Math::Vector3(1.0f, 2.0f, 3.0f));
        physics->FixedTick(1.0f / 60.0f);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyActive(*physics, bodyHandle));

        auto* duplicateBody = world.CreateComponent<NorvesLib::Modules::Physics::RigidBodyComponent>(root);
        assert(duplicateBody != nullptr && duplicateBody->GetOuter() == root);
        assert(!duplicateBody->GetBodyHandle().IsValid());
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetDuplicateDiagnosticCount(*physics) == 2);

        Core::Delegate<void, const NorvesLib::Modules::Physics::PhysicsContactEvent&> callback(
            [](const NorvesLib::Modules::Physics::PhysicsContactEvent&) {});
        NorvesLib::Modules::Physics::PhysicsCallbackHandle firstCallback;
        NorvesLib::Modules::Physics::PhysicsCallbackHandle secondCallback;
        NorvesLib::Modules::Physics::PhysicsCallbackHandle thirdCallback;
        NorvesLib::Modules::Physics::PhysicsCallbackHandle endCallback;
        NorvesLib::Modules::Physics::PhysicsCallbackHandle hitCallback;
        Core::Delegate<void, const NorvesLib::Modules::Physics::PhysicsContactEvent&> unboundCallback;
        assert(collider->AddOnOverlapBegin(unboundCallback, firstCallback)
            == NorvesLib::Modules::Physics::EPhysicsResult::InvalidArgument);
        assert(collider->AddOnOverlapBegin(callback, firstCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(collider->AddOnOverlapBegin(callback, secondCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(collider->AddOnOverlapBegin(callback, thirdCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(collider->AddOnOverlapEnd(callback, endCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(collider->AddOnHit(callback, hitCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(!(firstCallback == secondCallback));
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetCallbackCount(*physics, *collider) == 5);
        assert(collider->RemoveOnOverlapBegin(firstCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(collider->RemoveOnOverlapBegin(thirdCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(collider->RemoveOnOverlapBegin(firstCallback) == NorvesLib::Modules::Physics::EPhysicsResult::NotRegistered);
        assert(collider->RemoveOnOverlapEnd(secondCallback) == NorvesLib::Modules::Physics::EPhysicsResult::NotRegistered);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetCallbackCount(*physics, *collider) == 3);
        NorvesLib::Modules::Physics::PhysicsCallbackHandle replacementCallback;
        assert(collider->AddOnOverlapBegin(callback, replacementCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(replacementCallback.Index == thirdCallback.Index);
        const NorvesLib::Modules::Physics::PhysicsCallbackHandle initialSecondCallback = secondCallback;
        assert(initialSecondCallback.Generation == 1);
        NorvesLib::Modules::Physics::PhysicsModuleTestAccess::PrepareOverlapBeginGenerationWrap(
            *physics,
            *collider,
            secondCallback);
        assert(collider->RemoveOnOverlapBegin(secondCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        NorvesLib::Modules::Physics::PhysicsCallbackHandle wrappedCallback;
        assert(collider->AddOnOverlapBegin(callback, wrappedCallback) == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        assert(wrappedCallback.Index != initialSecondCallback.Index);
        assert(collider->RemoveOnOverlapBegin(initialSecondCallback)
            == NorvesLib::Modules::Physics::EPhysicsResult::NotRegistered);
        assert(collider->RemoveOnOverlapBegin(secondCallback) == NorvesLib::Modules::Physics::EPhysicsResult::NotRegistered);

        const Math::Vector3 velocityBeforeWrongThread = body->GetLinearVelocity();
        const float colliderRadiusBeforeWrongThread =
            NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetColliderRadius(*physics, *collider);
        const uint32_t callbackCountBeforeWrongThread =
            NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetCallbackCount(*physics, *collider);
        NorvesLib::Modules::Physics::EPhysicsResult workerResult = NorvesLib::Modules::Physics::EPhysicsResult::Success;
        NorvesLib::Modules::Physics::EPhysicsResult workerColliderResult = NorvesLib::Modules::Physics::EPhysicsResult::Success;
        NorvesLib::Modules::Physics::EPhysicsResult workerCallbackResult = NorvesLib::Modules::Physics::EPhysicsResult::Success;
        NorvesLib::Modules::Physics::PhysicsCallbackHandle workerCallbackHandle = hitCallback;
        const ColliderHandle colliderHandleBeforeWrongThreadFinalize = collider->GetColliderHandle();
        const BodyHandle bodyHandleBeforeWrongThreadFinalize = body->GetBodyHandle();
        Thread::Thread worker([body, collider, &workerResult, &workerColliderResult, &workerCallbackResult, &workerCallbackHandle, callback]()
        {
            workerResult = body->AddImpulse(Math::Vector3(5.0f, 0.0f, 0.0f));
            workerColliderResult = collider->SetSphere(2.0f);
            workerCallbackResult = collider->AddOnHit(callback, workerCallbackHandle);
            collider->Finalize();
            body->Finalize();
        });
        worker.Join();
        assert(workerResult == NorvesLib::Modules::Physics::EPhysicsResult::WrongThread);
        assert(workerColliderResult == NorvesLib::Modules::Physics::EPhysicsResult::WrongThread);
        assert(workerCallbackResult == NorvesLib::Modules::Physics::EPhysicsResult::WrongThread);
        assert(workerCallbackHandle == hitCallback);
        assert(collider->GetColliderHandle() == colliderHandleBeforeWrongThreadFinalize);
        assert(body->GetBodyHandle() == bodyHandleBeforeWrongThreadFinalize);
        assert(body->GetLinearVelocity() == velocityBeforeWrongThread);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetColliderRadius(*physics, *collider)
            == colliderRadiusBeforeWrongThread);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetCallbackCount(*physics, *collider)
            == callbackCountBeforeWrongThread);

        Entity* reparentParent = world.SpawnEntity<Entity>();
        assert(reparentParent != nullptr);
        assert(world.ReparentEntity(root, reparentParent));
        physics->FixedTick(1.0f / 60.0f);
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyActive(*physics, bodyHandle));

        Entity* child = world.SpawnEntity<Entity>(root);
        assert(child != nullptr);
        auto* childBody = world.CreateComponent<NorvesLib::Modules::Physics::RigidBodyComponent>(child);
        assert(childBody != nullptr);
        assert(childBody->SetBodyType(NorvesLib::Modules::Physics::EPhysicsBodyType::Dynamic)
            == NorvesLib::Modules::Physics::EPhysicsResult::InvalidState);
        assert(childBody->SetBodyType(NorvesLib::Modules::Physics::EPhysicsBodyType::Kinematic)
            == NorvesLib::Modules::Physics::EPhysicsResult::Success);

        Entity* bodyFirstOwner = world.SpawnEntity<Entity>();
        assert(bodyFirstOwner != nullptr);
        auto* bodyFirst = world.CreateComponent<NorvesLib::Modules::Physics::RigidBodyComponent>(bodyFirstOwner);
        assert(bodyFirst != nullptr && bodyFirst->GetBodyHandle().IsValid());
        const BodyHandle bodyFirstHandle = bodyFirst->GetBodyHandle();
        physics->FixedTick(1.0f / 60.0f);
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyActive(*physics, bodyFirstHandle));
        auto* lateCollider = world.CreateComponent<NorvesLib::Modules::Physics::ColliderComponent>(bodyFirstOwner);
        assert(lateCollider != nullptr && lateCollider->SetSphere(1.0f)
            == NorvesLib::Modules::Physics::EPhysicsResult::Success);
        physics->FixedTick(1.0f / 60.0f);
        assert(lateCollider->GetColliderHandle().IsValid());
        assert(bodyFirst->GetBodyHandle() == bodyFirstHandle);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyActive(*physics, bodyFirstHandle));
        bodyFirstOwner->RemoveComponent(lateCollider);
        physics->FixedTick(1.0f / 60.0f);
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyActive(*physics, bodyFirstHandle));

        assert(firstHandle.Generation == 1);
        NorvesLib::Modules::Physics::PhysicsModuleTestAccess::PrepareColliderGenerationWrap(*physics, *collider);
        const ColliderHandle maximumGenerationColliderHandle = collider->GetColliderHandle();
        root->RemoveComponent(collider);
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsColliderAlive(*physics, firstHandle));
        physics->FixedTick(1.0f / 60.0f);
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyActive(*physics, bodyHandle));

        Entity* reuseOwner = world.SpawnEntity<Entity>();
        assert(reuseOwner != nullptr);
        auto* reusedCollider = world.CreateComponent<NorvesLib::Modules::Physics::ColliderComponent>(reuseOwner);
        assert(reusedCollider != nullptr);
        assert(reusedCollider->GetColliderHandle().Index != firstHandle.Index);
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsColliderAlive(
            *physics,
            maximumGenerationColliderHandle));

        Entity* bodyLifoOwnerA = world.SpawnEntity<Entity>();
        Entity* bodyLifoOwnerB = world.SpawnEntity<Entity>();
        Entity* bodyLifoOwnerC = world.SpawnEntity<Entity>();
        assert(bodyLifoOwnerA != nullptr && bodyLifoOwnerB != nullptr && bodyLifoOwnerC != nullptr);
        auto* bodyLifoA = world.CreateComponent<NorvesLib::Modules::Physics::RigidBodyComponent>(bodyLifoOwnerA);
        auto* bodyLifoB = world.CreateComponent<NorvesLib::Modules::Physics::RigidBodyComponent>(bodyLifoOwnerB);
        assert(bodyLifoA != nullptr && bodyLifoB != nullptr);
        const BodyHandle bodyLifoAHandle = bodyLifoA->GetBodyHandle();
        const BodyHandle bodyLifoBHandle = bodyLifoB->GetBodyHandle();
        assert(bodyLifoAHandle.Generation == 1);
        assert(bodyLifoBHandle.Generation == 1);
        bodyLifoOwnerA->RemoveComponent(bodyLifoA);
        NorvesLib::Modules::Physics::PhysicsModuleTestAccess::PrepareBodyGenerationWrap(*physics, *bodyLifoB);
        const BodyHandle maximumGenerationBodyHandle = bodyLifoB->GetBodyHandle();
        bodyLifoOwnerB->RemoveComponent(bodyLifoB);
        auto* bodyLifoC = world.CreateComponent<NorvesLib::Modules::Physics::RigidBodyComponent>(bodyLifoOwnerC);
        assert(bodyLifoC != nullptr);
        assert(bodyLifoC->GetBodyHandle().Index == bodyLifoAHandle.Index);
        assert(bodyLifoC->GetBodyHandle().Index != bodyLifoBHandle.Index);
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyAlive(*physics, bodyLifoAHandle));
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyAlive(*physics, bodyLifoBHandle));
        assert(!NorvesLib::Modules::Physics::PhysicsModuleTestAccess::IsBodyAlive(
            *physics,
            maximumGenerationBodyHandle));

        world.Finalize();
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetActiveColliderCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetRegisteredColliderCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetRegisteredBodyCount(*physics) == 0);
        registry.ShutdownAll(engine);
        assert(NorvesLib::Modules::Physics::PhysicsModuleTestAccess::GetLifecycleStateCount(*physics) == 0);
        assert(NorvesLib::Modules::Physics::ColliderComponent::StaticClass() == colliderClass);
        assert(NorvesLib::Modules::Physics::RigidBodyComponent::StaticClass() == bodyClass);
        assert(ClassRegistry::Get().FindClass(colliderClass->GetClassId()) == colliderClass);
        assert(ClassRegistry::Get().FindClass(bodyClass->GetClassId()) == bodyClass);
        std::cout << "[PASS] Phase 3 Entity Inner ownership, slots, callbacks, thread guard and reflection\n";
    }
} // namespace

int main()
{
    ConfigureFailureReporting();

    std::cout << "PhysicsModuleLinkTest start\n";
    TestModuleLifecycleRegistersPhysicsReflectedTypes();
    TestRegisterFindDuplicateAndPublicBoundary();
    TestLifecyclePublishesEmptyReadySnapshotAndUnbinds();
    TestForeignProviderIsNotOverwritten();
    TestRollbackAfterLaterInstallFailureUnbinds();
    TestRollbackAfterLaterInitializeFailureUnbinds();
    TestDestroyedModuleLeavesSceneQueryUnbound();
    TestInstallRejectsDifferentRegistrationThread();
    TestWrongThreadTeardownPreservesReadyBinding();
    TestStrictGamePhysicsContractRejectsMutations();
    TestGamePhysicsRegistrationSourceContract();
    TestPhysicsModuleCMakeContract();
    TestPhaseThreeUnregisteredComponentsRemainInvalid();
    TestPhaseThreeComponentOwnershipAndSlots();
    std::cout << "PhysicsModuleLinkTest passed\n";
    return 0;
}
