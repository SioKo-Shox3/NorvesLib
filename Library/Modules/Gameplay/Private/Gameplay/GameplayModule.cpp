#include "Gameplay/GameplayModule.h"

#include "Asset/AssetFileReader.h"
#include "Container/Containers.h"
#include "Engine/Engine.h"
#include "Gameplay/Combat/HealthComponent.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Modules::Gameplay
{
    namespace
    {
        constexpr const char* kGameplayModuleName = "NorvesGameplayModule";

        void RegisterCombatEntity(Core::Entity& entity, Combat::CombatRuntime& runtime)
        {
            if (entity.IsPendingDestroy())
            {
                return;
            }
            for (auto* component : entity.GetComponents())
            {
                if (auto* hitbox = Core::CastTo<Combat::HitboxComponent>(component))
                {
                    runtime.Register(*hitbox);
                }
                else if (auto* hurtbox = Core::CastTo<Combat::HurtboxSetComponent>(component))
                {
                    runtime.Register(*hurtbox);
                }
            }
            for (auto* child : entity.GetChildEntities())
            {
                RegisterCombatEntity(*child, runtime);
            }
        }
    }

    GameplayModule::GameplayModule()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId()),
          m_CombatRuntime(*this),
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

    uint64_t GameplayModule::GetRunSeed() const
    {
        return m_RunSeed;
    }

    void GameplayModule::SetRunSeed(uint64_t seed)
    {
        m_RunSeed = seed;
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
        Core::ClassRegistry::Get().RegisterClass(Combat::HitboxComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(Combat::HurtboxSetComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(World::ZoneSensorComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(World::EncounterZoneComponent::StaticClass());
    }

    bool GameplayModule::Initialize()
    {
        if (!IsOwnerThread())
        {
            return false;
        }
        m_CombatRuntime.ResetTransientState();
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
        RegisterCombatComponents(m_Engine->GetWorld());
        return true;
    }

    void GameplayModule::Tick(float)
    {
        if (m_bInitialized && m_Engine != nullptr)
        {
            m_DefeatGate.Update(m_IsDefeatHoldOwnerAlive);
        }
    }

    bool GameplayModule::IsDefeatHoldOwnerAlive(Core::EntityHandle owner) const
    {
        return m_Engine != nullptr && m_Engine->GetWorld().ResolveEntityHandle(owner) != nullptr;
    }

    bool GameplayModule::IsOwnerThread() const
    {
        return m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    GameplayModule* GameplayModule::FindBoundModule(const Core::World& world)
    {
        auto* module = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
        if (module == nullptr || !module->m_bInitialized || module->m_Engine == nullptr ||
            &module->m_Engine->GetWorld() != &world)
        {
            return nullptr;
        }
        return module;
    }

    void GameplayModule::RegisterCombatComponents(Core::World& world)
    {
        // 初期化前に作られた Component を登録し、登録待ちはランタイムで再試行する。
        for (auto* entity : world.GetRootEntities())
        {
            RegisterCombatEntity(*entity, m_CombatRuntime);
        }
    }

    void GameplayModule::LateTick(float)
    {
        if (!IsOwnerThread() || !m_bInitialized || m_Engine == nullptr)
        {
            return;
        }
        // 攻撃判定を先に評価し(先頭で Transform をそろえる)、遭遇領域はそろった位置を読む。
        auto& world = m_Engine->GetWorld();
        const auto& times = m_Engine->GetTimeSystem().GetFrameTimes();
        m_CombatRuntime.Evaluate(world, times.World);
        m_EncounterService.Evaluate(world, times.GetDelta(Core::Engine::TimeChannel::World));
    }

    void GameplayModule::Shutdown()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        m_CombatRuntime.ResetTransientState();
        m_bInitialized = false;
        m_EncounterService.Reset();
    }

    void GameplayModule::Uninstall(Core::Engine::Engine&)
    {
        if (!IsOwnerThread())
        {
            return;
        }
        m_CombatRuntime.ResetTransientState();
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
