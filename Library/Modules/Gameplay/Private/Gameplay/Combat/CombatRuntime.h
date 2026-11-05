#pragma once

#include "Container/VariableArray.h"
#include "Delegate/MulticastDelegate.h"
#include "Gameplay/Combat/CombatHitEvent.h"
#include "Gameplay/Combat/CombatGeometry.h"
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

    struct CombatRuntimeDiagnostics
    {
        // 関節の姿勢を引けず、判定から外した形の延べ数。
        uint64_t JointUnavailableCount = 0;
        // 影響の弱い関節への束縛を警告した数。
        uint64_t WeakJointWarningCount = 0;
    };

    struct CombatVolumeSnapshot
    {
        bool bHitbox = false;
        uint64_t RegistrationIndex = 0;
        uint32_t PartIndex = 0;
        uint32_t PartId = 0;
        bool bWindowOpen = false;
        CombatSweepVolume Volume;
    };

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
        const HitboxComponent* FindHitbox(uint64_t registrationIndex) const;
        const HurtboxSetComponent* FindHurtboxSet(uint64_t registrationIndex) const;
        uint64_t AllocateAttackInstanceId();
        double GetWorldTime() const { return m_WorldTime; }
        uint64_t GetFrameNumber() const { return m_FrameNumber; }
        void ResetTransientState();
        void Evaluate(Core::World& world, float worldDt);
        const CombatRuntimeDiagnostics& GetDiagnostics() const { return m_Diagnostics; }
        const Core::Container::VariableArray<CombatVolumeSnapshot>& GetLastEvaluatedVolumes() const
        {
            return m_LastEvaluatedVolumes;
        }

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
        CombatRuntimeDiagnostics m_Diagnostics;
        Core::Container::VariableArray<CombatVolumeSnapshot> m_LastEvaluatedVolumes;
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
