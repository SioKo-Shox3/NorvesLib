#include "Gameplay/GameplayModule.h"

#include "Container/Containers.h"
#include "Engine/Engine.h"
#include "Gameplay/Combat/HealthComponent.h"

namespace NorvesLib::Modules::Gameplay
{
    namespace
    {
        constexpr const char* kGameplayModuleName = "NorvesGameplayModule";
    }

    GameplayModule::GameplayModule()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId())
    {
    }

    Core::Identity GameplayModule::GetModuleId() const
    {
        return Core::Identity(kGameplayModuleName);
    }

    const char* GameplayModule::GetName() const
    {
        return kGameplayModuleName;
    }

    bool GameplayModule::Install(Core::Engine::Engine& engine)
    {
        if (m_OwnerThreadId != Thread::Thread::GetCurrentThreadId() || m_Engine != nullptr)
        {
            return false;
        }

        m_Engine = &engine;
        RegisterWorldSystems(engine.GetWorld());
        return true;
    }

    void GameplayModule::RegisterWorldSystems(Core::World&)
    {
        // Component を追加したら、ここでシステムを登録する。
    }

    void GameplayModule::RegisterReflectedTypes()
    {
        Core::ClassRegistry::Get().RegisterClass(Combat::HealthComponent::StaticClass());
    }

    bool GameplayModule::Initialize()
    {
        m_bInitialized = m_Engine != nullptr;
        return m_bInitialized;
    }

    void GameplayModule::Tick(float)
    {
    }

    void GameplayModule::Shutdown()
    {
        m_bInitialized = false;
    }

    void GameplayModule::Uninstall(Core::Engine::Engine&)
    {
        m_bInitialized = false;
        m_Engine = nullptr;
    }

    IGameplayModule* RegisterGameplayModule(Core::Module::ModuleRegistry& registry)
    {
        if (IGameplayModule* existing = FindGameplayModule(registry))
        {
            return existing;
        }

        return dynamic_cast<IGameplayModule*>(
            registry.Register(Core::Container::MakeUnique<GameplayModule>()));
    }

    IGameplayModule* FindGameplayModule(Core::Module::ModuleRegistry& registry)
    {
        return dynamic_cast<IGameplayModule*>(registry.FindModule(Core::Identity(kGameplayModuleName)));
    }
}
