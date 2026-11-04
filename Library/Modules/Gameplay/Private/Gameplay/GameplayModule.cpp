#include "Gameplay/GameplayModule.h"

#include "Asset/AssetFileReader.h"
#include "Container/Containers.h"
#include "Engine/Engine.h"
#include "Gameplay/Combat/HealthComponent.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Modules::Gameplay
{
    namespace
    {
        constexpr const char* kGameplayModuleName = "NorvesGameplayModule";
    }

    GameplayModule::GameplayModule()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId()),
          m_EncounterService(m_ProgressionState),
          m_IsDefeatHoldOwnerAlive(this, &GameplayModule::IsDefeatHoldOwnerAlive)
    {
        m_DefeatGate.OnDefeatCompleted.Add(&m_ProgressionState, &ProgressionState::MarkDefeated);
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
        Core::ClassRegistry::Get().RegisterClass(World::ZoneSensorComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(World::EncounterZoneComponent::StaticClass());
    }

    bool GameplayModule::Initialize()
    {
        m_bInitialized = m_Engine != nullptr;
        if (!m_bInitialized)
        {
            return false;
        }

        const auto definition = Core::Asset::AssetFileReader().Read("Gameplay/progression.json");
        if (!definition.Succeeded())
        {
            NORVES_LOG_WARNING("Gameplay", "進行条件の資産を読み込めませんでした。");
        }
        else
        {
            Core::Container::String text;
            if (definition.Blob.GetSize() != 0)
            {
                text.append(reinterpret_cast<const char*>(definition.Blob.GetData()), definition.Blob.GetSize());
            }
            if (!m_ProgressionState.LoadDefinitionFromJson(text))
            {
                NORVES_LOG_WARNING("Gameplay", "進行条件を適用できませんでした。");
            }
        }
        return true;
    }

    void GameplayModule::Tick(float)
    {
        if (m_bInitialized && m_Engine != nullptr)
        {
            m_DefeatGate.Update(m_IsDefeatHoldOwnerAlive);
        }
    }

    void GameplayModule::LateTick(float)
    {
        if (m_bInitialized && m_Engine != nullptr)
        {
            const auto& times = m_Engine->GetTimeSystem().GetFrameTimes();
            m_EncounterService.Evaluate(m_Engine->GetWorld(), times.GetDelta(Core::Engine::TimeChannel::World));
        }
    }

    bool GameplayModule::IsDefeatHoldOwnerAlive(Core::EntityHandle owner) const
    {
        return m_Engine != nullptr && m_Engine->GetWorld().ResolveEntityHandle(owner) != nullptr;
    }

    void GameplayModule::Shutdown()
    {
        m_bInitialized = false;
        m_EncounterService.Reset();
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
