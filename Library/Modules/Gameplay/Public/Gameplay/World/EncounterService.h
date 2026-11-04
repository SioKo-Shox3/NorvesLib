#pragma once

#include "Container/PointerTypes.h"
#include "Delegate/MulticastDelegate.h"
#include "Gameplay/World/EncounterZoneTypes.h"
#include "Thread/Thread.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::Gameplay
{
    class ProgressionState;
}

namespace NorvesLib::Modules::Gameplay::World
{
    /// 作成した GameThread 専用。参照する進行状態はサービスより長く生存すること。
    class EncounterService
    {
    public:
        explicit EncounterService(ProgressionState& progressionState);
        ~EncounterService();
        EncounterService(const EncounterService&) = delete;
        EncounterService& operator=(const EncounterService&) = delete;

        void RequestRegisterSensor(Core::EntityHandle entity);
        void RequestUnregisterSensor(Core::EntityHandle entity);
        void RequestRegisterZone(Core::EntityHandle entity);
        void RequestUnregisterZone(Core::EntityHandle entity);

        /// 呼び出し一回が一フレーム。通知中の再入は拒否する。
        void Evaluate(Core::World& world, float worldDeltaSeconds);
        void MarkCleared(const Core::Container::String& zoneId);
        EEncounterZoneState GetZoneState(const Core::Container::String& zoneId) const;
        Core::Container::String GetActiveProfileId() const;
        /// 購読を保ったまま実行状態を消し、次の評価で全 Entity を拾い直す。
        void Reset();

        Core::MulticastDelegate<const EncounterZoneEvent&> OnZoneEntered;
        Core::MulticastDelegate<const EncounterZoneEvent&> OnZoneExited;
        Core::MulticastDelegate<const EncounterZoneEvent&> OnZoneReset;
        Core::MulticastDelegate<const EncounterZoneEvent&> OnZoneCleared;
        Core::MulticastDelegate<const Core::Container::String&, const Core::Container::String&, float> OnActiveProfileChanged;

    private:
        struct State;
        void RequestRegistration(Core::EntityHandle entity, bool bZone, bool bRegister);
        bool IsOwnerThread() const;

        ProgressionState& m_ProgressionState;
        Thread::Thread::ThreadId m_OwnerThreadId;
        Core::Container::TUniquePtr<State> m_State;
        bool m_bEvaluating = false;
    };
} // namespace NorvesLib::Modules::Gameplay::World
