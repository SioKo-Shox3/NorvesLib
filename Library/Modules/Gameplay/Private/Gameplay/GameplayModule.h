#pragma once

#include "Gameplay/IGameplayModule.h"
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

        ProgressionState& GetProgressionState() override { return m_ProgressionState; }
        DefeatGate& GetDefeatGate() override { return m_DefeatGate; }
        World::EncounterService& GetEncounterService() override { return m_EncounterService; }
        World::InteractionService& GetInteractionService() override { return m_InteractionService; }

    private:
        void RegisterWorldSystems(Core::World& world);
        bool IsDefeatHoldOwnerAlive(Core::EntityHandle owner) const;

        Core::Engine::Engine* m_Engine = nullptr;
        Thread::Thread::ThreadId m_OwnerThreadId;
        bool m_bInitialized = false;
        ProgressionState m_ProgressionState;
        DefeatGate m_DefeatGate;
        World::EncounterService m_EncounterService;
        World::InteractionService m_InteractionService;
        Core::Delegate<bool, Core::EntityHandle> m_IsDefeatHoldOwnerAlive;
    };
}
