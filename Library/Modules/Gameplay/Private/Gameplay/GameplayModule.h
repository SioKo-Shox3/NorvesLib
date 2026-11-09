#pragma once

#include "Gameplay/IGameplayModule.h"
#include "Gameplay/Combat/CombatRuntime.h"
#include "Gameplay/Combat/CombatResponse.h"
#include "Gameplay/Progression/DefeatGate.h"
#include "Gameplay/Progression/ProgressionState.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/InteractionService.h"
#include "Thread/Thread.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::Gameplay
{
    class GameplayModule final : public IGameplayModule
    {
    public:
        GameplayModule();
        ~GameplayModule() override = default;

        Core::Identity GetModuleId() const override;
        const char* GetName() const override;

        bool Install(Core::Engine::Engine& engine) override;
        void RegisterReflectedTypes() override;
        bool Initialize() override;
        void Tick(float deltaTime) override;
        void LateTick(float deltaTime) override;
        void Shutdown() override;
        void Uninstall(Core::Engine::Engine& engine) override;

        uint64_t GetRunSeed() const override;
        void SetRunSeed(uint64_t seed) override;
        void SetCombatResponseSettings(const Combat::CombatResponseSettings& settings) override;
        const Combat::CombatResponseSettings& GetCombatResponseSettings() const override;

        ProgressionState& GetProgressionState() override { return m_ProgressionState; }
        DefeatGate& GetDefeatGate() override { return m_DefeatGate; }
        Core::MulticastDelegate<const Combat::CombatHitEvent&>& GetOnAnyHit() override { return m_CombatRuntime.OnAnyHit; }
        Combat::CombatRuntime& GetCombatRuntime() { return m_CombatRuntime; }
        static GameplayModule* FindBoundModule(const Core::World& world);
        bool IsOwnerThread() const;
        World::EncounterService& GetEncounterService() override { return m_EncounterService; }
        World::InteractionService& GetInteractionService() override { return m_InteractionService; }

    private:
        void RegisterWorldSystems(Core::World& world);
        void RegisterCombatComponents(Core::World& world);
        bool IsDefeatHoldOwnerAlive(Core::EntityHandle owner) const;

        Core::Engine::Engine* m_Engine = nullptr;
        Thread::Thread::ThreadId m_OwnerThreadId;
        bool m_bInitialized = false;
        uint64_t m_RunSeed = 0;
        Combat::CombatRuntime m_CombatRuntime;
        Combat::CombatResponse m_CombatResponse;
        ProgressionState m_ProgressionState;
        DefeatGate m_DefeatGate;
        World::EncounterService m_EncounterService;
        World::InteractionService m_InteractionService;
        Core::Delegate<bool, Core::EntityHandle> m_IsDefeatHoldOwnerAlive;
    };
}
