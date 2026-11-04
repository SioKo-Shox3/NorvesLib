#include "Gameplay/World/EncounterService.h"

#include "Container/VariableArray.h"
#include "Gameplay/Progression/ProgressionState.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/EncounterZoneMath.h"
#include "Gameplay/World/ZoneProfileStack.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Object/World.h"

namespace NorvesLib::Modules::Gameplay::World
{
    namespace
    {
        enum class EEventKind : uint8_t
        {
            Entered,
            Exited,
            Reset,
            Cleared
        };

        struct PendingEvent
        {
            EEventKind Kind;
            EncounterZoneEvent Event;
        };

        struct SensorSample
        {
            Core::EntityHandle Entity;
            Math::Vector3 Position;
            uint32_t KindBits = 0;
            bool bTeleported = false;
        };

        template <typename T>
        T* ResolveComponent(Core::World& world, Core::EntityHandle handle)
        {
            Core::Entity* entity = world.ResolveEntityHandle(handle);
            T* component = entity && !entity->IsPendingDestroy() ? entity->GetComponent<T>() : nullptr;
            return component && !component->IsPendingDestroy() ? component : nullptr;
        }

        void AddProfileChange(const ZoneProfileChange& change,
                              Core::Container::VariableArray<ZoneProfileChange>& changes)
        {
            if (change.bChanged)
            {
                changes.push_back(change);
            }
        }
    } // namespace

    struct EncounterService::State
    {
        struct Registration
        {
            Core::EntityHandle Entity;
            uint64_t Sequence = 0;
        };

        struct ZoneRegistration : Registration
        {
            Core::Container::String ZoneId;
            EncounterZoneStateMachine Machine;
            bool bClearedThisEvaluation = false;
        };

        struct Request
        {
            Core::EntityHandle Entity;
            bool bZone = false;
            bool bRegister = false;
        };

        void Register(Core::EntityHandle handle, bool bZone)
        {
            if (!handle.IsValid())
            {
                return;
            }
            if (bZone)
            {
                for (const auto& zone : Zones)
                {
                    if (zone.Entity == handle)
                    {
                        return;
                    }
                }
                ZoneRegistration zone;
                zone.Entity = handle;
                zone.Sequence = NextRegistrationSequence++;
                Zones.push_back(zone);
            }
            else
            {
                for (const auto& sensor : Sensors)
                {
                    if (sensor.Entity == handle)
                    {
                        return;
                    }
                }
                Sensors.push_back({handle, NextRegistrationSequence++});
            }
        }

        // EntityHandle の全 96 bit を照合する。Stack の 64 bit キーには、
        // そのハンドルの登録に一意な番号を使い、別 World・別世代との衝突を防ぐ。
        ZoneProfileStack Profiles;
        Core::Container::VariableArray<Registration> Sensors;
        Core::Container::VariableArray<ZoneRegistration> Zones;
        Core::Container::VariableArray<Request> Requests;
        Core::Container::VariableArray<Core::Container::String> PendingClearedIds;
        Core::Container::VariableArray<Core::Container::String> ClearedIds;
        uint64_t NextRegistrationSequence = 1;
        uint64_t EvaluationSequence = 0;
        bool bNeedsInitialScan = true;
    };

    EncounterService::EncounterService(ProgressionState& progressionState)
        : m_ProgressionState(progressionState),
          m_OwnerThreadId(Thread::Thread::GetCurrentThreadId()),
          m_State(Core::Container::MakeUnique<State>())
    {
    }

    EncounterService::~EncounterService() = default;

    bool EncounterService::IsOwnerThread() const
    {
        return m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    void EncounterService::RequestRegistration(Core::EntityHandle entity, bool bZone, bool bRegister)
    {
        if (!IsOwnerThread())
        {
            return;
        }
        if (entity.IsValid())
        {
            m_State->Requests.push_back({entity, bZone, bRegister});
        }
        else if (bRegister)
        {
            // World の段の最中は Entity の行が同期点まで予約されることがある。
            // 生のポインタを保存せず、次の評価で接続済みの Entity を拾い直す。
            m_State->bNeedsInitialScan = true;
        }
    }

    void EncounterService::RequestRegisterSensor(Core::EntityHandle entity)
    {
        RequestRegistration(entity, false, true);
    }

    void EncounterService::RequestUnregisterSensor(Core::EntityHandle entity)
    {
        RequestRegistration(entity, false, false);
    }

    void EncounterService::RequestRegisterZone(Core::EntityHandle entity)
    {
        RequestRegistration(entity, true, true);
    }

    void EncounterService::RequestUnregisterZone(Core::EntityHandle entity)
    {
        RequestRegistration(entity, true, false);
    }

    void EncounterService::MarkCleared(const Core::Container::String& zoneId)
    {
        if (IsOwnerThread())
        {
            m_State->PendingClearedIds.push_back(zoneId);
        }
    }

    EEncounterZoneState EncounterService::GetZoneState(const Core::Container::String& zoneId) const
    {
        if (!IsOwnerThread())
        {
            return EEncounterZoneState::Outside;
        }
        for (const auto& id : m_State->ClearedIds)
        {
            if (id == zoneId)
            {
                return EEncounterZoneState::Cleared;
            }
        }
        for (const auto& zone : m_State->Zones)
        {
            if (zone.ZoneId == zoneId)
            {
                return zone.Machine.GetState();
            }
        }
        return EEncounterZoneState::Outside;
    }

    Core::Container::String EncounterService::GetActiveProfileId() const
    {
        return IsOwnerThread() ? m_State->Profiles.GetActiveProfileId() : Core::Container::String{};
    }

    void EncounterService::Reset()
    {
        if (IsOwnerThread())
        {
            *m_State = State{};
        }
    }

    void EncounterService::Evaluate(Core::World& world, float worldDeltaSeconds)
    {
        if (!IsOwnerThread() || m_bEvaluating)
        {
            return;
        }
        m_bEvaluating = true;
        struct EvaluationGuard
        {
            bool& bEvaluating;
            ~EvaluationGuard() { bEvaluating = false; }
        } guard{m_bEvaluating};

        State& state = *m_State;
        const uint64_t evaluationSequence = ++state.EvaluationSequence;
        Core::Container::VariableArray<PendingEvent> events;
        Core::Container::VariableArray<ZoneProfileChange> profileChanges;

        // 登録は評価の境界だけで変更する。通知中の依頼は次回までこの列に残る。
        for (const auto& request : state.Requests)
        {
            if (request.bRegister)
            {
                state.Register(request.Entity, request.bZone);
            }
            else if (request.bZone)
            {
                for (auto it = state.Zones.begin(); it != state.Zones.end(); ++it)
                {
                    if (it->Entity == request.Entity)
                    {
                        AddProfileChange(state.Profiles.Remove(it->Sequence), profileChanges);
                        state.Zones.erase(it);
                        break;
                    }
                }
            }
            else
            {
                for (auto it = state.Sensors.begin(); it != state.Sensors.end(); ++it)
                {
                    if (it->Entity == request.Entity)
                    {
                        state.Sensors.erase(it);
                        break;
                    }
                }
            }
        }
        state.Requests.clear();

        if (state.bNeedsInitialScan)
        {
            // 初期化前に復元された Component も拾う。ポインタはこの評価だけで使う。
            auto entities = world.GetRootEntities();
            for (size_t index = 0; index < entities.size(); ++index)
            {
                Core::Entity* entity = entities[index];
                if (!entity || entity->IsPendingDestroy())
                {
                    continue;
                }
                const auto handle = world.GetEntityHandle(*entity);
                if (ResolveComponent<ZoneSensorComponent>(world, handle))
                {
                    state.Register(handle, false);
                }
                if (ResolveComponent<EncounterZoneComponent>(world, handle))
                {
                    state.Register(handle, true);
                }
                const auto children = entity->GetChildEntities();
                entities.insert(entities.end(), children.begin(), children.end());
            }
            state.bNeedsInitialScan = false;
        }

        for (const auto& id : state.PendingClearedIds)
        {
            bool bKnown = false;
            for (const auto& known : state.ClearedIds)
            {
                bKnown = bKnown || known == id;
            }
            if (!bKnown)
            {
                state.ClearedIds.push_back(id);
            }
        }
        state.PendingClearedIds.clear();

        for (auto it = state.Zones.begin(); it != state.Zones.end();)
        {
            auto* zone = ResolveComponent<EncounterZoneComponent>(world, it->Entity);
            if (!zone)
            {
                AddProfileChange(state.Profiles.Remove(it->Sequence), profileChanges);
                it = state.Zones.erase(it);
                continue;
            }
            it->ZoneId = zone->ZoneId.Get();
            it->bClearedThisEvaluation = false;
            for (const auto& id : state.ClearedIds)
            {
                if (id == it->ZoneId)
                {
                    it->bClearedThisEvaluation = it->Machine.MarkCleared().bCleared;
                    break;
                }
            }
            ++it;
        }

        world.UpdateWorldTransforms();
        Core::Container::VariableArray<SensorSample> samples;
        for (auto it = state.Sensors.begin(); it != state.Sensors.end();)
        {
            auto* sensor = ResolveComponent<ZoneSensorComponent>(world, it->Entity);
            if (!sensor)
            {
                it = state.Sensors.erase(it);
                continue;
            }
            samples.push_back({it->Entity,
                sensor->GetOwner()->GetWorldTransform().TransformPoint(sensor->Offset.Get()),
                sensor->KindBits.Get(), sensor->m_bTeleported});
            ++it;
        }

        for (auto& registration : state.Zones)
        {
            auto* zone = ResolveComponent<EncounterZoneComponent>(world, registration.Entity);
            if (!zone)
            {
                continue;
            }
            EncounterZoneShapeDesc shape;
            shape.Shape = zone->Shape.Get();
            shape.Center = zone->GetOwner()->GetWorldTransform().position;
            shape.HalfExtents = zone->HalfExtents.Get();
            shape.Radius = zone->Radius.Get();
            shape.HalfHeight = zone->HalfHeight.Get();
            const EncounterZoneTuning tuning{zone->EnterMargin.Get(), zone->ExitMargin.Get(),
                                            zone->ResetDelaySeconds.Get()};
            bool bInside = false;
            bool bAllOutside = true;
            bool bTeleported = false;
            for (const auto& sample : samples)
            {
                if ((sample.KindBits & zone->SensorMask.Get()) == 0)
                {
                    continue;
                }
                bInside = bInside || IsInsideInflated(shape, sample.Position, tuning.EnterMargin);
                bAllOutside = bAllOutside && !IsInsideInflated(shape, sample.Position, tuning.GetEffectiveExitMargin());
                bTeleported = bTeleported || sample.bTeleported;
            }
            if (zone->bRequiresProgression.Get() && !m_ProgressionState.IsFinalUnlocked())
            {
                bInside = false;
            }
            auto transitions = registration.Machine.Step(bInside, bAllOutside, bTeleported, worldDeltaSeconds, tuning);
            transitions.bCleared = registration.bClearedThisEvaluation;
            zone->m_State = registration.Machine.GetState();
            const EncounterZoneEvent event{registration.Entity, registration.ZoneId, zone->EnemyId.Get(),
                zone->ProfileId.Get(), zone->ResetPolicy.Get(), evaluationSequence};

            if (transitions.bEntered)
            {
                events.push_back({EEventKind::Entered, event});
                AddProfileChange(state.Profiles.Push(registration.Sequence, event.ProfileId,
                    zone->Priority.Get(), zone->ProfileBlendSeconds.Get()), profileChanges);
            }
            if (transitions.bExited)
            {
                events.push_back({EEventKind::Exited, event});
                AddProfileChange(state.Profiles.Remove(registration.Sequence), profileChanges);
            }
            if (transitions.bReset)
            {
                events.push_back({EEventKind::Reset, event});
            }
            if (transitions.bCleared)
            {
                events.push_back({EEventKind::Cleared, event});
                AddProfileChange(state.Profiles.Remove(registration.Sequence), profileChanges);
            }
        }

        // 通知前に消すことで、購読者が付けた新しい印は次の評価に残す。
        for (const auto& sample : samples)
        {
            if (auto* sensor = ResolveComponent<ZoneSensorComponent>(world, sample.Entity))
            {
                sensor->m_bTeleported = false;
            }
        }

        for (const auto& pending : events)
        {
            if (!ResolveComponent<EncounterZoneComponent>(world, pending.Event.ZoneEntity))
            {
                continue;
            }
            switch (pending.Kind)
            {
            case EEventKind::Entered:
                OnZoneEntered.Broadcast(pending.Event);
                break;
            case EEventKind::Exited:
                OnZoneExited.Broadcast(pending.Event);
                break;
            case EEventKind::Reset:
                OnZoneReset.Broadcast(pending.Event);
                break;
            case EEventKind::Cleared:
                OnZoneCleared.Broadcast(pending.Event);
                break;
            }
        }
        for (const auto& change : profileChanges)
        {
            OnActiveProfileChanged.Broadcast(change.OldProfileId, change.NewProfileId, change.BlendSeconds);
        }
    }
} // namespace NorvesLib::Modules::Gameplay::World
