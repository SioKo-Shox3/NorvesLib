#include "Physics/PhysicsModule.h"

#include "CoreTypes.h"
#include "Engine/Engine.h"

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        constexpr const char* kPhysicsModuleName = "NorvesPhysicsModule";
    } // namespace

    PhysicsModule::PhysicsModule()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId())
    {
    }

    Core::Identity PhysicsModule::GetModuleId() const
    {
        return Core::Identity(kPhysicsModuleName);
    }

    const char* PhysicsModule::GetName() const
    {
        return kPhysicsModuleName;
    }

    bool PhysicsModule::Install(Core::Engine::Engine& engine)
    {
        if (!IsOwnerThread())
        {
            return false;
        }

        if (m_bBound || m_Engine != nullptr)
        {
            return false;
        }

        if (engine.GetSceneQuery().BindPhysicsProvider(*this) != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return false;
        }

        m_Engine = &engine;
        m_bBound = true;
        return true;
    }

    bool PhysicsModule::Initialize()
    {
        if (!IsOwnerThread() || !m_bBound || m_Engine == nullptr)
        {
            return false;
        }

        m_bInitialized = true;
        m_bHasPublishedSnapshot = false;
        return true;
    }

    void PhysicsModule::Tick(float /*deltaTime*/)
    {
    }

    void PhysicsModule::PreFixedTick(float /*fixedDeltaTime*/)
    {
    }

    void PhysicsModule::FixedTick(float /*fixedDeltaTime*/)
    {
        if (!IsOwnerThread() || !m_bInitialized)
        {
            return;
        }

        m_bHasPublishedSnapshot = true;
    }

    void PhysicsModule::Shutdown()
    {
        if (!IsOwnerThread())
        {
            return;
        }

        m_bInitialized = false;
        m_bHasPublishedSnapshot = false;
    }

    void PhysicsModule::Uninstall(Core::Engine::Engine& engine)
    {
        if (!IsOwnerThread())
        {
            return;
        }

        if (!m_bBound)
        {
            m_Engine = nullptr;
            m_bInitialized = false;
            m_bHasPublishedSnapshot = false;
            return;
        }

        if (m_Engine != &engine)
        {
            return;
        }

        const Core::Scene::EPhysicsSceneQueryResult result =
            engine.GetSceneQuery().UnbindPhysicsProvider(*this);
        if (result != Core::Scene::EPhysicsSceneQueryResult::Success
            && result != Core::Scene::EPhysicsSceneQueryResult::Unavailable)
        {
            return;
        }

        // Unavailable は provider が既に存在しないため dangling binding はない。
        m_Engine = nullptr;
        m_bBound = false;
        m_bInitialized = false;
        m_bHasPublishedSnapshot = false;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::Raycast(
        const Math::Ray& /*ray*/,
        float /*maxDistance*/,
        Core::Scene::PhysicsRaycastHit& outHit) const
    {
        outHit = Core::Scene::PhysicsRaycastHit{};
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        return readiness == Core::Scene::EPhysicsSceneQueryResult::Success
            ? Core::Scene::EPhysicsSceneQueryResult::NoHit
            : readiness;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapSphere(
        const Math::Sphere& /*sphere*/,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        return readiness == Core::Scene::EPhysicsSceneQueryResult::Success
            ? Core::Scene::EPhysicsSceneQueryResult::NoHit
            : readiness;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapBox(
        const Math::OBB& /*box*/,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        return readiness == Core::Scene::EPhysicsSceneQueryResult::Success
            ? Core::Scene::EPhysicsSceneQueryResult::NoHit
            : readiness;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapCapsule(
        const Math::Capsule& /*capsule*/,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        outHits.clear();
        const Core::Scene::EPhysicsSceneQueryResult readiness = GetReadinessResult();
        return readiness == Core::Scene::EPhysicsSceneQueryResult::Success
            ? Core::Scene::EPhysicsSceneQueryResult::NoHit
            : readiness;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::IsAlive(
        Core::Scene::ColliderHandle /*collider*/,
        bool& outAlive) const
    {
        outAlive = false;
        return GetReadinessResult();
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::IsAlive(
        Core::Scene::BodyHandle /*body*/,
        bool& outAlive) const
    {
        outAlive = false;
        return GetReadinessResult();
    }

    bool PhysicsModule::IsOwnerThread() const
    {
        return m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsModule::GetReadinessResult() const
    {
        if (!IsOwnerThread())
        {
            return Core::Scene::EPhysicsSceneQueryResult::WrongThread;
        }

        if (!m_bInitialized || !m_bHasPublishedSnapshot)
        {
            return Core::Scene::EPhysicsSceneQueryResult::NotReady;
        }

        return Core::Scene::EPhysicsSceneQueryResult::Success;
    }

    IPhysicsModule* RegisterPhysicsModule(Core::Module::ModuleRegistry& registry)
    {
        if (IPhysicsModule* existing = FindPhysicsModule(registry))
        {
            return existing;
        }

        return dynamic_cast<IPhysicsModule*>(
            registry.Register(Core::Container::MakeUnique<PhysicsModule>()));
    }

    IPhysicsModule* FindPhysicsModule(Core::Module::ModuleRegistry& registry)
    {
        return dynamic_cast<IPhysicsModule*>(registry.FindModule(Core::Identity(kPhysicsModuleName)));
    }
} // namespace NorvesLib::Modules::Physics
