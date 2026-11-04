#pragma once

#include "Container/VariableArray.h"
#include "Delegate/MulticastDelegate.h"
#include "Gameplay/Combat/CombatHitEvent.h"
#include "Object/EntityHandle.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::Gameplay
{
    class GameplayModule;
}

namespace NorvesLib::Modules::Gameplay::Combat
{
    class HitboxComponent;
    class HurtboxSetComponent;

    /// GameplayModule が所有する登録簿。登録・解除・採番は所有スレッド専用。
    class CombatRuntime
    {
    public:
        explicit CombatRuntime(GameplayModule& module);
        ~CombatRuntime();
        CombatRuntime(const CombatRuntime&) = delete;
        CombatRuntime& operator=(const CombatRuntime&) = delete;

        bool IsOwnerThread() const;
        bool Register(HitboxComponent& component);
        bool Register(HurtboxSetComponent& component);
        void Unregister(HitboxComponent& component);
        void Unregister(HurtboxSetComponent& component);
        uint64_t AllocateAttackInstanceId();
        double GetWorldTime() const { return m_WorldTime; }
        uint64_t GetFrameNumber() const { return m_FrameNumber; }
        void ResetTransientState();
        void Evaluate(Core::World& world, float worldDt);

        Core::MulticastDelegate<const CombatHitEvent&> OnAnyHit;

    private:
        template<typename T>
        struct Registration
        {
            T* Component = nullptr;
            Core::EntityHandle Owner;
            uint64_t RegistrationIndex = 0;
        };

        void RetryPendingRegistrations();
        void CompactRegistrations();

        GameplayModule& m_Module;
        Core::Container::VariableArray<Registration<HitboxComponent>> m_Hitboxes;
        Core::Container::VariableArray<Registration<HurtboxSetComponent>> m_HurtboxSets;
        Core::Container::VariableArray<HitboxComponent*> m_PendingHitboxes;
        Core::Container::VariableArray<HurtboxSetComponent*> m_PendingHurtboxSets;
        uint64_t m_NextRegistrationIndex = 1;
        uint64_t m_NextAttackInstanceId = 1;
        double m_WorldTime = 0.0;
        uint64_t m_FrameNumber = 0;
        uint64_t m_StateRevision = 0;
        bool m_bEvaluating = false;
    };
} // namespace NorvesLib::Modules::Gameplay::Combat
