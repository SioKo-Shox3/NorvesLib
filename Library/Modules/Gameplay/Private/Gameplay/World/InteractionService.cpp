#include "Gameplay/World/InteractionService.h"

#include "Container/VariableArray.h"
#include "Gameplay/World/InteractableComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Object/World.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Modules::Gameplay::World
{
    namespace
    {
        template <typename T>
        T* ResolveComponent(Core::World& world, Core::EntityHandle handle)
        {
            Core::Entity* entity = world.ResolveEntityHandle(handle);
            T* component = entity && !entity->IsPendingDestroy() ? entity->GetComponent<T>() : nullptr;
            return component && !component->IsPendingDestroy() ? component : nullptr;
        }

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        struct FlagGuard
        {
            explicit FlagGuard(bool& flag) : Flag(flag) { Flag = true; }
            ~FlagGuard() { Flag = false; }
            bool& Flag;
        };
    } // namespace

    struct InteractionService::State
    {
        struct Request
        {
            Core::EntityHandle Entity;
            bool bInteractable = false;
            bool bRegister = false;
            uint64_t ComponentId = 0;
        };

        struct SensorRegistration
        {
            Core::EntityHandle Entity;
            uint64_t ComponentId = 0;
        };

        void Register(Core::World& world, Core::EntityHandle entity, bool bInteractable, uint64_t expectedComponentId = 0)
        {
            if (!entity.IsValid())
            {
                return;
            }
            if (bInteractable)
            {
                for (const auto& registered : Interactables)
                {
                    if (registered == entity)
                    {
                        return;
                    }
                }
                Interactables.push_back(entity);
                return;
            }

            auto* sensor = ResolveComponent<ZoneSensorComponent>(world, entity);
            if (!sensor || (expectedComponentId != 0 && sensor->GetComponentId() != expectedComponentId))
            {
                return;
            }
            const uint64_t componentId = sensor->GetComponentId();
            for (auto it = Sensors.begin(); it != Sensors.end(); ++it)
            {
                if (it->Entity == entity)
                {
                    if (it->ComponentId == componentId)
                    {
                        return;
                    }
                    // 同じ Entity に新しいセンサーが付いた場合は新しい登録として扱う。
                    Sensors.erase(it);
                    break;
                }
            }
            Sensors.push_back({entity, componentId});
        }

        void Unregister(Core::EntityHandle entity, bool bInteractable, uint64_t componentId = 0)
        {
            if (bInteractable)
            {
                for (auto it = Interactables.begin(); it != Interactables.end(); ++it)
                {
                    if (*it == entity)
                    {
                        Interactables.erase(it);
                        break;
                    }
                }
                return;
            }
            for (auto it = Sensors.begin(); it != Sensors.end(); ++it)
            {
                if (it->Entity == entity && (componentId == 0 || it->ComponentId == componentId))
                {
                    Sensors.erase(it);
                    break;
                }
            }
        }

        Core::Container::VariableArray<Core::EntityHandle> Interactables;
        Core::Container::VariableArray<SensorRegistration> Sensors;
        Core::Container::VariableArray<Request> Requests;
        Core::Container::VariableArray<InteractionPrompt> PendingUnavailable;
        InteractionPrompt CurrentPrompt;
        bool bNeedsInitialScan = true;
    };

    InteractionService::InteractionService()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId()),
          m_State(Core::Container::MakeUnique<State>())
    {
    }

    InteractionService::~InteractionService() = default;

    bool InteractionService::IsOwnerThread() const
    {
        return m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    void InteractionService::RequestRegistration(Core::EntityHandle entity, bool bInteractable, bool bRegister)
    {
        if (!IsOwnerThread())
        {
            return;
        }
        if (entity.IsValid())
        {
            m_State->Requests.push_back({entity, bInteractable, bRegister});
        }
        else if (bRegister)
        {
            // World の行がまだ予約中なら、次の評価で接続済みの Entity を拾い直す。
            m_State->bNeedsInitialScan = true;
        }
    }

    void InteractionService::RequestRegisterInteractable(Core::EntityHandle entity)
    {
        RequestRegistration(entity, true, true);
    }

    void InteractionService::RequestUnregisterInteractable(Core::EntityHandle entity)
    {
        RequestRegistration(entity, true, false);
    }

    void InteractionService::RequestRegisterSensor(Core::EntityHandle entity)
    {
        RequestRegistration(entity, false, true);
    }

    void InteractionService::RequestUnregisterSensor(Core::EntityHandle entity)
    {
        RequestRegistration(entity, false, false);
    }

    void InteractionService::RequestRegisterSensorComponent(Core::EntityHandle entity, uint64_t componentId)
    {
        if (!IsOwnerThread())
        {
            return;
        }
        if (entity.IsValid())
        {
            m_State->Requests.push_back({entity, false, true, componentId});
        }
        else
        {
            m_State->bNeedsInitialScan = true;
        }
    }

    void InteractionService::RequestUnregisterSensorComponent(Core::EntityHandle entity, uint64_t componentId)
    {
        if (!IsOwnerThread())
        {
            return;
        }
        if (!entity.IsValid())
        {
            for (const auto& sensor : m_State->Sensors)
            {
                if (sensor.ComponentId == componentId)
                {
                    entity = sensor.Entity;
                    break;
                }
            }
        }
        if (entity.IsValid())
        {
            m_State->Requests.push_back({entity, false, false, componentId});
        }
    }

    void InteractionService::NotifyInteractableUnavailable(Core::EntityHandle entity)
    {
        if (!IsOwnerThread() || !entity.IsValid() || m_State->CurrentPrompt.Interactable != entity)
        {
            return;
        }

        const InteractionPrompt previous = m_State->CurrentPrompt;
        m_State->CurrentPrompt = {};
        if (m_bNotifying)
        {
            m_State->PendingUnavailable.push_back(previous);
            return;
        }

        const InteractionPrompt empty;
        const FlagGuard guard(m_bNotifying);
        OnPromptChanged.Broadcast(previous, empty);
    }

    void InteractionService::SetInteractPressedSource(Core::Delegate<bool> source)
    {
        if (IsOwnerThread())
        {
            m_InteractPressedSource = std::move(source);
        }
    }

    void InteractionService::SetEngagedQuery(Core::Delegate<bool, Core::EntityHandle> query)
    {
        if (IsOwnerThread())
        {
            m_EngagedQuery = std::move(query);
        }
    }

    void InteractionService::SetMouthFreeQuery(Core::Delegate<bool, Core::EntityHandle> query)
    {
        if (IsOwnerThread())
        {
            m_MouthFreeQuery = std::move(query);
        }
    }

    const InteractionPrompt& InteractionService::GetCurrentPrompt() const
    {
        return IsOwnerThread() ? m_State->CurrentPrompt : m_EmptyPrompt;
    }

    void InteractionService::Reset()
    {
        if (IsOwnerThread())
        {
            *m_State = State{};
            ++m_ResetGeneration;
        }
    }

    void InteractionService::Evaluate(Core::World& world)
    {
        if (!IsOwnerThread() || m_bEvaluating || m_bNotifying)
        {
            return;
        }
        const FlagGuard evaluationGuard(m_bEvaluating);
        const uint64_t resetGeneration = m_ResetGeneration;
        State& state = *m_State;

        // 境界より後の依頼は、冒頭の保留通知からの依頼も含めて次回に残す。
        const auto requests = state.Requests;
        state.Requests.clear();
        const bool bNeedsInitialScan = state.bNeedsInitialScan;
        state.bNeedsInitialScan = false;
        const auto pendingUnavailable = state.PendingUnavailable;
        state.PendingUnavailable.clear();
        // 前回の通知中に外された候補は、値の写しで解除を先に伝える。
        for (const auto& previous : pendingUnavailable)
        {
            const InteractionPrompt empty;
            {
                const FlagGuard notificationGuard(m_bNotifying);
                OnPromptChanged.Broadcast(previous, empty);
            }
            if (m_ResetGeneration != resetGeneration)
            {
                return;
            }
        }

        // 登録は評価の境界だけで変更し、通知中の依頼は次回に残す。
        for (const auto& request : requests)
        {
            if (request.bRegister)
            {
                state.Register(world, request.Entity, request.bInteractable, request.ComponentId);
            }
            else
            {
                state.Unregister(request.Entity, request.bInteractable, request.ComponentId);
            }
        }

        if (bNeedsInitialScan)
        {
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
                    state.Register(world, handle, false);
                }
                if (ResolveComponent<InteractableComponent>(world, handle))
                {
                    state.Register(world, handle, true);
                }
                const auto children = entity->GetChildEntities();
                entities.insert(entities.end(), children.begin(), children.end());
            }
        }

        // 解除前に同じ Entity へ付け直されたセンサーも、別の登録として後ろへ移す。
        const auto registeredSensors = state.Sensors;
        for (const auto& registration : registeredSensors)
        {
            auto* sensor = ResolveComponent<ZoneSensorComponent>(world, registration.Entity);
            if (!sensor)
            {
                state.Unregister(registration.Entity, false);
            }
            else if (sensor->GetComponentId() != registration.ComponentId)
            {
                state.Register(world, registration.Entity, false);
            }
        }
        for (auto it = state.Interactables.begin(); it != state.Interactables.end();)
        {
            it = ResolveComponent<InteractableComponent>(world, *it) ? it + 1 : state.Interactables.erase(it);
        }

        // 呼び先が自身を差し替えても、実行中のデリゲートを保持する。
        const auto pressedSource = m_InteractPressedSource;
        const bool bPressed = pressedSource.IsBound() && pressedSource.Invoke();
        if (m_ResetGeneration != resetGeneration)
        {
            return;
        }

        Core::EntityHandle sensorHandle;
        uint64_t sensorComponentId = 0;
        bool bEngaged = false;
        bool bMouthFree = true;
        const auto sensors = state.Sensors;
        const auto resolveRegisteredSensor = [&world](const State::SensorRegistration& registration)
        {
            auto* sensor = ResolveComponent<ZoneSensorComponent>(world, registration.Entity);
            return sensor && sensor->GetComponentId() == registration.ComponentId ? sensor : nullptr;
        };
        for (const auto& registration : sensors)
        {
            if (!resolveRegisteredSensor(registration))
            {
                continue;
            }
            const auto engagedQuery = m_EngagedQuery;
            const bool bSensorEngaged = engagedQuery.IsBound() && engagedQuery.Invoke(registration.Entity);
            if (m_ResetGeneration != resetGeneration)
            {
                return;
            }
            if (!resolveRegisteredSensor(registration))
            {
                continue;
            }
            const auto mouthFreeQuery = m_MouthFreeQuery;
            const bool bSensorMouthFree = !mouthFreeQuery.IsBound() || mouthFreeQuery.Invoke(registration.Entity);
            if (m_ResetGeneration != resetGeneration)
            {
                return;
            }
            if (resolveRegisteredSensor(registration))
            {
                sensorHandle = registration.Entity;
                sensorComponentId = registration.ComponentId;
                bEngaged = bSensorEngaged;
                bMouthFree = bSensorMouthFree;
                break;
            }
        }

        world.UpdateWorldTransforms();
        auto* sensor = resolveRegisteredSensor({sensorHandle, sensorComponentId});
        InteractionPrompt selected;
        int32_t selectedPriority = 0;
        double selectedDistance = 0.0;
        double selectedAngle = 0.0;
        if (sensor)
        {
            const auto transform = sensor->GetOwner()->GetWorldTransform();
            const Math::Vector3 sensorOffset = sensor->getOffset();
            const Math::Vector3 sensorPosition = transform.TransformPoint(sensorOffset);
            const Math::Vector3 forward = transform.TransformDirection(Math::Vector3::Forward);
            const double forwardLength = std::hypot(double(forward.x), double(forward.y), double(forward.z));
            if (IsFinite(sensorPosition) && IsFinite(forward) && std::isfinite(forwardLength) && forwardLength > 0.0)
            {
                for (const auto& handle : state.Interactables)
                {
                    auto* component = ResolveComponent<InteractableComponent>(world, handle);
                    if (!component)
                    {
                        continue;
                    }
                    component->m_LastEvaluatedOwner = handle;
                    if (!component->IsEnabled())
                    {
                        continue;
                    }
                    const float enterRadius = component->EnterRadius.Get();
                    const float exitRadius = component->ExitRadius.Get();
                    const float cone = component->FacingConeDegrees.Get();
                    if (!std::isfinite(enterRadius) || !std::isfinite(exitRadius) ||
                        !std::isfinite(cone) || cone <= 0.0f ||
                        (component->bDisabledWhileEngaged.Get() && bEngaged) ||
                        (component->bRequiresFreeMouth.Get() && !bMouthFree))
                    {
                        continue;
                    }

                    const Math::Vector3 position = component->GetOwner()->GetWorldTransform().TransformPoint(
                        component->PromptOffset.Get());
                    if (!IsFinite(position))
                    {
                        continue;
                    }
                    const double dx = double(position.x) - double(sensorPosition.x);
                    const double dy = double(position.y) - double(sensorPosition.y);
                    const double dz = double(position.z) - double(sensorPosition.z);
                    const double distance = std::hypot(dx, dy, dz);
                    const bool bCurrent = state.CurrentPrompt.Interactable == handle;
                    const double radius = bCurrent ? std::max(enterRadius, exitRadius) : enterRadius;
                    if (!std::isfinite(distance) || distance > radius)
                    {
                        continue;
                    }

                    double angle = 0.0;
                    if (distance > 1.0e-6)
                    {
                        const double dot = (dx * double(forward.x) + dy * double(forward.y) +
                                            dz * double(forward.z)) / (distance * forwardLength);
                        if (!std::isfinite(dot))
                        {
                            continue;
                        }
                        constexpr double radiansToDegrees = 180.0 / 3.14159265358979323846;
                        angle = std::acos(std::clamp(dot, -1.0, 1.0)) * radiansToDegrees;
                    }
                    if (!std::isfinite(angle) || (cone < 360.0f && angle > double(cone) * 0.5))
                    {
                        continue;
                    }

                    const int32_t priority = component->Priority.Get();
                    const bool bSelectedIsCurrent = selected.Interactable == state.CurrentPrompt.Interactable;
                    const bool bBetter = !selected.IsValid() || priority > selectedPriority ||
                        (priority == selectedPriority && (distance < selectedDistance ||
                        (distance == selectedDistance && (angle < selectedAngle ||
                        (angle == selectedAngle && bCurrent && !bSelectedIsCurrent)))));
                    if (bBetter)
                    {
                        selected.Interactable = handle;
                        selected.ObjectId = component->GetOwner()->GetObjectId();
                        selected.ActionId = component->ActionId.Get();
                        selected.PromptTextKey = component->PromptTextKey.Get();
                        selected.WorldPosition = position;
                        selectedPriority = priority;
                        selectedDistance = distance;
                        selectedAngle = angle;
                    }
                }
            }
        }

        const InteractionPrompt previous = state.CurrentPrompt;
        state.CurrentPrompt = selected;
        if (previous.Interactable != selected.Interactable)
        {
            // 新しい候補は配信直前にも破棄と無効化を確認する。
            auto* component = selected.IsValid() ? ResolveComponent<InteractableComponent>(world, selected.Interactable) : nullptr;
            if (selected.IsValid() && (!component || !component->IsEnabled()))
            {
                selected = {};
                state.CurrentPrompt = {};
            }
            if (previous.Interactable != selected.Interactable)
            {
                const FlagGuard notificationGuard(m_bNotifying);
                OnPromptChanged.Broadcast(previous, selected);
            }
            if (m_ResetGeneration != resetGeneration)
            {
                return;
            }
        }

        auto* selectedComponent = selected.IsValid() ? ResolveComponent<InteractableComponent>(world, selected.Interactable) : nullptr;
        if (bPressed && selectedComponent && selectedComponent->IsEnabled() &&
            state.CurrentPrompt.Interactable == selected.Interactable &&
            resolveRegisteredSensor({sensorHandle, sensorComponentId}))
        {
            const FlagGuard notificationGuard(m_bNotifying);
            OnInteractionTriggered.Broadcast(selected.ActionId, selected.ObjectId);
        }
    }
} // namespace NorvesLib::Modules::Gameplay::World
