#include "Gameplay/Combat/CombatRuntime.h"

#include "Gameplay/Combat/CombatGeometry.h"
#include "Gameplay/Combat/CombatJointBinding.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Gameplay/GameplayModule.h"
#include "Object/World.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace NorvesLib::Modules::Gameplay::Combat
{
    namespace
    {
        // float のフレーム時間を合算した誤差で、間隔ちょうどの再ヒットを落とさない。
        constexpr double kRehitTimeToleranceSeconds = 1.0e-6;

        bool IsUsableEntity(const Core::Entity* entity)
        {
            return entity != nullptr && !entity->IsPendingDestroy() && entity->IsActive();
        }

        bool IsUsableComponent(const Core::Component::Component* component)
        {
            return component != nullptr && !component->IsPendingDestroy() &&
                   component->IsEnabled() && component->IsActive();
        }

        CombatPose MakePose(const Math::Transform& transform)
        {
            return {transform.position, transform.rotation};
        }

        template<typename T>
        void RemovePending(Core::Container::VariableArray<T*>& pending, T* component)
        {
            pending.erase(std::remove(pending.begin(), pending.end(), component), pending.end());
        }

        struct HitboxFrame
        {
            size_t Slot = 0;
            uint64_t RegistrationIndex = 0;
            uint64_t PoseRevision = 0;
            Core::EntityHandle Owner;
            Core::EntityHandle Binding;
            Math::Transform CurrentTransform;
            CombatSweepVolume Volume;
            bool bPoseAvailable = false;
            bool bUsable = false;
        };

        struct PartFrame
        {
            size_t Slot = 0;
            uint64_t RegistrationIndex = 0;
            uint32_t PartIndex = 0;
            uint32_t PartId = 0;
            uint32_t Faction = 0;
            int32_t Priority = 0;
            uint64_t ActorId = 0;
            Core::EntityHandle Owner;
            Core::EntityHandle Binding;
            Math::Transform CurrentTransform;
            CombatSweepVolume Volume;
            bool bPoseAvailable = false;
            bool bUsable = false;
        };

        // コールバックの前に確定し、ポインタを保持せずに配送時に引き直す。
        struct HitCandidate
        {
            size_t HitboxSlot = 0;
            size_t HurtboxSlot = 0;
            uint32_t PartIndex = 0;
            int32_t Priority = 0;
            Core::EntityHandle HitboxOwner;
            Core::EntityHandle HitboxBinding;
            Core::EntityHandle HurtboxOwner;
            Core::EntityHandle HurtboxBinding;
            CombatHitEvent Event;
            EPerVictimPolicy Policy = EPerVictimPolicy::FirstPart;
            bool bSelected = true;
        };

        static_assert(std::is_trivially_copyable_v<HitCandidate>);

        bool IsEarlierPart(const HitCandidate& left, const HitCandidate& right)
        {
            if (left.Event.S != right.Event.S)
            {
                return left.Event.S < right.Event.S;
            }
            if (left.Event.HurtboxIndex != right.Event.HurtboxIndex)
            {
                return left.Event.HurtboxIndex < right.Event.HurtboxIndex;
            }
            return left.PartIndex < right.PartIndex;
        }
    }

    CombatRuntime::CombatRuntime(GameplayModule& module)
        : m_Module(module)
    {
    }

    CombatRuntime::~CombatRuntime()
    {
        ResetTransientState();
        for (auto& entry : m_Hitboxes)
        {
            if (entry.Component != nullptr)
            {
                entry.Component->m_Runtime = nullptr;
                entry.Component->m_RegistrationIndex = 0;
            }
        }
        for (auto& entry : m_HurtboxSets)
        {
            if (entry.Component != nullptr)
            {
                entry.Component->m_Runtime = nullptr;
                entry.Component->m_RegistrationIndex = 0;
            }
        }
        for (auto* component : m_PendingHitboxes)
        {
            component->m_Runtime = nullptr;
        }
        for (auto* component : m_PendingHurtboxSets)
        {
            component->m_Runtime = nullptr;
        }
    }

    bool CombatRuntime::IsOwnerThread() const
    {
        return m_Module.IsOwnerThread();
    }

    bool CombatRuntime::Register(HitboxComponent& component)
    {
        if (!IsOwnerThread())
        {
            return false;
        }
        auto* owner = component.GetOwner();
        auto* world = owner != nullptr ? owner->GetWorld() : nullptr;
        if (!component.m_bCombatReady || component.IsPendingDestroy() || owner == nullptr || owner->IsPendingDestroy() ||
            world == nullptr || GameplayModule::FindBoundModule(*world) != &m_Module ||
            (component.m_Runtime != nullptr && component.m_Runtime != this))
        {
            return false;
        }
        const Core::EntityHandle ownerHandle = world->GetEntityHandle(*owner);
        if (!ownerHandle.IsValid())
        {
            if (component.m_Runtime == this && component.m_RegistrationIndex != 0)
            {
                Unregister(component);
            }
            if (component.m_Runtime == nullptr)
            {
                m_PendingHitboxes.push_back(&component);
                component.m_Runtime = this;
            }
            return false;
        }
        if (component.m_Runtime == this && component.m_RegistrationIndex != 0)
        {
            for (const auto& entry : m_Hitboxes)
            {
                if (entry.Component == &component && entry.Owner == ownerHandle)
                {
                    return true;
                }
            }
            Unregister(component);
        }
        if (m_NextRegistrationIndex == 0)
        {
            return false;
        }
        RemovePending(m_PendingHitboxes, &component);
        const uint64_t index = m_NextRegistrationIndex++;
        m_Hitboxes.push_back({&component, ownerHandle, index});
        component.m_Runtime = this;
        component.m_RegistrationIndex = index;
        if (component.m_bOwnerBinding)
        {
            component.m_Binding = ownerHandle;
        }
        return true;
    }

    bool CombatRuntime::Register(HurtboxSetComponent& component)
    {
        if (!IsOwnerThread())
        {
            return false;
        }
        auto* owner = component.GetOwner();
        auto* world = owner != nullptr ? owner->GetWorld() : nullptr;
        if (!component.m_bCombatReady || component.IsPendingDestroy() || owner == nullptr || owner->IsPendingDestroy() ||
            world == nullptr || GameplayModule::FindBoundModule(*world) != &m_Module ||
            (component.m_Runtime != nullptr && component.m_Runtime != this))
        {
            return false;
        }
        const Core::EntityHandle ownerHandle = world->GetEntityHandle(*owner);
        if (!ownerHandle.IsValid())
        {
            if (component.m_Runtime == this && component.m_RegistrationIndex != 0)
            {
                Unregister(component);
            }
            if (component.m_Runtime == nullptr)
            {
                m_PendingHurtboxSets.push_back(&component);
                component.m_Runtime = this;
            }
            return false;
        }
        if (component.m_Runtime == this && component.m_RegistrationIndex != 0)
        {
            for (const auto& entry : m_HurtboxSets)
            {
                if (entry.Component == &component && entry.Owner == ownerHandle)
                {
                    return true;
                }
            }
            Unregister(component);
        }
        if (m_NextRegistrationIndex == 0)
        {
            return false;
        }
        RemovePending(m_PendingHurtboxSets, &component);
        const uint64_t index = m_NextRegistrationIndex++;
        m_HurtboxSets.push_back({&component, ownerHandle, index});
        component.m_Runtime = this;
        component.m_RegistrationIndex = index;
        for (auto& part : component.m_Parts)
        {
            if (part.bOwnerBinding)
            {
                part.Binding = ownerHandle;
            }
        }
        return true;
    }

    void CombatRuntime::Unregister(HitboxComponent& component)
    {
        if (!IsOwnerThread() || component.m_Runtime != this)
        {
            return;
        }
        RemovePending(m_PendingHitboxes, &component);
        for (auto it = m_Hitboxes.begin(); it != m_Hitboxes.end(); ++it)
        {
            if (it->Component == &component)
            {
                if (m_bEvaluating)
                {
                    it->Component = nullptr;
                }
                else
                {
                    m_Hitboxes.erase(it);
                }
                break;
            }
        }
        component.ResetWindow();
        component.m_Runtime = nullptr;
        component.m_RegistrationIndex = 0;
    }

    void CombatRuntime::Unregister(HurtboxSetComponent& component)
    {
        if (!IsOwnerThread() || component.m_Runtime != this)
        {
            return;
        }
        RemovePending(m_PendingHurtboxSets, &component);
        for (auto it = m_HurtboxSets.begin(); it != m_HurtboxSets.end(); ++it)
        {
            if (it->Component == &component)
            {
                if (m_bEvaluating)
                {
                    it->Component = nullptr;
                }
                else
                {
                    m_HurtboxSets.erase(it);
                }
                break;
            }
        }
        for (auto& part : component.m_Parts)
        {
            part.PreviousTransform = {};
            part.bPreviousValid = false;
        }
        component.m_Runtime = nullptr;
        component.m_RegistrationIndex = 0;
    }

    const HitboxComponent* CombatRuntime::FindHitbox(uint64_t registrationIndex) const
    {
        for (const auto& entry : m_Hitboxes)
        {
            if (entry.Component != nullptr && entry.RegistrationIndex == registrationIndex)
            {
                return entry.Component;
            }
        }
        return nullptr;
    }

    const HurtboxSetComponent* CombatRuntime::FindHurtboxSet(uint64_t registrationIndex) const
    {
        for (const auto& entry : m_HurtboxSets)
        {
            if (entry.Component != nullptr && entry.RegistrationIndex == registrationIndex)
            {
                return entry.Component;
            }
        }
        return nullptr;
    }

    void CombatRuntime::RetryPendingRegistrations()
    {
        for (size_t index = 0; index < m_PendingHitboxes.size();)
        {
            if (!Register(*m_PendingHitboxes[index]))
            {
                ++index;
            }
        }
        for (size_t index = 0; index < m_PendingHurtboxSets.size();)
        {
            if (!Register(*m_PendingHurtboxSets[index]))
            {
                ++index;
            }
        }
    }

    void CombatRuntime::CompactRegistrations()
    {
        m_Hitboxes.erase(std::remove_if(m_Hitboxes.begin(), m_Hitboxes.end(),
            [](const auto& entry) { return entry.Component == nullptr; }), m_Hitboxes.end());
        m_HurtboxSets.erase(std::remove_if(m_HurtboxSets.begin(), m_HurtboxSets.end(),
            [](const auto& entry) { return entry.Component == nullptr; }), m_HurtboxSets.end());
    }

    uint64_t CombatRuntime::AllocateAttackInstanceId()
    {
        if (!IsOwnerThread() || m_NextAttackInstanceId == 0)
        {
            return 0;
        }
        return m_NextAttackInstanceId++;
    }

    void CombatRuntime::ResetTransientState()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        ++m_StateRevision;
        m_LastEvaluatedVolumes.clear();
        for (auto& entry : m_Hitboxes)
        {
            if (entry.Component != nullptr)
            {
                entry.Component->ResetWindow();
            }
        }
        for (auto& entry : m_HurtboxSets)
        {
            if (entry.Component == nullptr)
            {
                continue;
            }
            for (auto& part : entry.Component->m_Parts)
            {
                part.PreviousTransform = {};
                part.bPreviousValid = false;
            }
        }
        m_WorldTime = 0.0;
        m_FrameNumber = 0;
        // インスタンスと登録の通し番号は再利用しない。
    }

    void CombatRuntime::Evaluate(Core::World& world, float worldDt)
    {
        if (!IsOwnerThread() || m_bEvaluating)
        {
            return;
        }
        m_bEvaluating = true;
        m_LastEvaluatedVolumes.clear();
        RetryPendingRegistrations();
        world.UpdateWorldTransforms();
        ++m_FrameNumber;
        const float elapsed = std::isfinite(worldDt) && worldDt >= 0.0f ? worldDt : 0.0f;
        m_WorldTime += static_cast<double>(elapsed);
        const uint64_t stateRevision = m_StateRevision;

        Core::Container::VariableArray<HitboxFrame> hitboxFrames;
        Core::Container::VariableArray<PartFrame> partFrames;
        hitboxFrames.reserve(m_Hitboxes.size());

        // 判定窓の寿命を検査してから、スケールを使わない姿勢を保存する。
        for (size_t slot = 0; slot < m_Hitboxes.size(); ++slot)
        {
            const auto& entry = m_Hitboxes[slot];
            auto* hitbox = entry.Component;
            if (hitbox == nullptr)
            {
                continue;
            }
            auto* owner = world.ResolveEntityHandle(entry.Owner);
            const Core::EntityHandle bindingHandle = hitbox->GetBinding();
            auto* binding = world.ResolveEntityHandle(bindingHandle);
            const bool bPoseAvailable = owner != nullptr && hitbox->GetOwner() == owner && binding != nullptr;
            const bool bUsable = bPoseAvailable && IsUsableComponent(hitbox) &&
                                 IsUsableEntity(owner) && IsUsableEntity(binding);
            if (hitbox->m_bWindowOpen)
            {
                const bool bHasProfile = Core::Container::IsValid(hitbox->m_Profile);
                const bool bExpired = bHasProfile && hitbox->m_Profile->MaxDurationSeconds > 0.0f &&
                    m_WorldTime - hitbox->m_WindowOpenedAt > hitbox->m_Profile->MaxDurationSeconds;
                if (!bUsable || !bHasProfile || bExpired)
                {
                    hitbox->ResetWindow();
                }
            }

            HitboxFrame frame;
            frame.Slot = slot;
            frame.RegistrationIndex = entry.RegistrationIndex;
            frame.PoseRevision = hitbox->m_PoseRevision;
            frame.Owner = entry.Owner;
            frame.Binding = bindingHandle;
            frame.bPoseAvailable = bPoseAvailable;
            frame.bUsable = bUsable;
            if (hitbox->m_JointName.IsValid())
            {
                frame.bPoseAvailable = bPoseAvailable && ResolveJointPose(*binding, hitbox->m_JointName,
                    hitbox->m_JointBinding, hitbox->m_WeakJointThreshold, m_Diagnostics, frame.CurrentTransform);
                if (!frame.bPoseAvailable)
                {
                    ++m_Diagnostics.JointUnavailableCount;
                }
            }
            else if (bPoseAvailable)
            {
                frame.CurrentTransform = binding->GetWorldTransform();
            }
            frame.bUsable = bUsable && frame.bPoseAvailable;
            if (frame.bPoseAvailable)
            {
                frame.Volume.Shape = hitbox->m_Shape;
                frame.Volume.Current = MakePose(frame.CurrentTransform);
                frame.Volume.Previous = hitbox->m_bPreviousValid && !hitbox->m_bFirstWindowFrame
                    ? MakePose(hitbox->m_PreviousTransform) : frame.Volume.Current;
                CombatVolumeSnapshot snapshot;
                snapshot.bHitbox = true;
                snapshot.RegistrationIndex = entry.RegistrationIndex;
                snapshot.bWindowOpen = hitbox->m_bWindowOpen;
                snapshot.Volume = frame.Volume;
                m_LastEvaluatedVolumes.push_back(snapshot);
            }
            hitboxFrames.push_back(frame);
        }

        for (size_t slot = 0; slot < m_HurtboxSets.size(); ++slot)
        {
            const auto& entry = m_HurtboxSets[slot];
            auto* hurtbox = entry.Component;
            if (hurtbox == nullptr)
            {
                continue;
            }
            auto* owner = world.ResolveEntityHandle(entry.Owner);
            const bool bUsableSet = IsUsableComponent(hurtbox) && IsUsableEntity(owner) && hurtbox->GetOwner() == owner;
            for (size_t partIndex = 0; partIndex < hurtbox->m_Parts.size(); ++partIndex)
            {
                const auto& part = hurtbox->m_Parts[partIndex];
                auto* binding = world.ResolveEntityHandle(part.Binding);
                PartFrame frame;
                frame.Slot = slot;
                frame.RegistrationIndex = entry.RegistrationIndex;
                frame.PartIndex = static_cast<uint32_t>(partIndex);
                frame.PartId = part.PartId;
                frame.Faction = part.Faction;
                frame.Priority = part.Priority;
                frame.ActorId = hurtbox->GetActorId();
                frame.Owner = entry.Owner;
                frame.Binding = part.Binding;
                frame.bPoseAvailable = owner != nullptr && hurtbox->GetOwner() == owner && binding != nullptr;
                frame.bUsable = bUsableSet && IsUsableEntity(binding);
                if (part.JointName.IsValid())
                {
                    frame.bPoseAvailable = frame.bPoseAvailable && ResolveJointPose(*binding, part.JointName,
                        hurtbox->m_JointBindings[partIndex], hurtbox->m_WeakJointThreshold,
                        m_Diagnostics, frame.CurrentTransform);
                    if (!frame.bPoseAvailable)
                    {
                        ++m_Diagnostics.JointUnavailableCount;
                    }
                }
                else if (frame.bPoseAvailable)
                {
                    frame.CurrentTransform = binding->GetWorldTransform();
                }
                frame.bUsable = frame.bUsable && frame.bPoseAvailable;
                if (frame.bPoseAvailable)
                {
                    frame.Volume.Shape = part.Shape;
                    frame.Volume.Current = MakePose(frame.CurrentTransform);
                    frame.Volume.Previous = part.bPreviousValid ? MakePose(part.PreviousTransform) : frame.Volume.Current;
                    CombatVolumeSnapshot snapshot;
                    snapshot.RegistrationIndex = entry.RegistrationIndex;
                    snapshot.PartIndex = frame.PartIndex;
                    snapshot.PartId = frame.PartId;
                    snapshot.Volume = frame.Volume;
                    m_LastEvaluatedVolumes.push_back(snapshot);
                }
                partFrames.push_back(frame);
            }
        }

        // 全候補を集めてから再ヒットと部位選択を適用し、配送より前に記録を確定する。
        Core::Container::VariableArray<HitCandidate> candidates;
        const CombatSweepSettings settings;
        for (const auto& hitboxFrame : hitboxFrames)
        {
            auto* hitbox = m_Hitboxes[hitboxFrame.Slot].Component;
            if (!hitboxFrame.bUsable || !hitbox->m_bWindowOpen || !Core::Container::IsValid(hitbox->m_Profile))
            {
                continue;
            }
            const auto& profile = *hitbox->m_Profile;
            const uint64_t attackerId = hitbox->GetActorId();
            for (const auto& partFrame : partFrames)
            {
                if (!partFrame.bUsable || partFrame.ActorId == 0 || attackerId == partFrame.ActorId ||
                    (profile.HitMask & partFrame.Faction) == 0)
                {
                    continue;
                }
                CombatSweepContact contact;
                if (SweepCombatVolumes(hitboxFrame.Volume, partFrame.Volume, settings, contact) != ECombatSweepResult::Hit)
                {
                    continue;
                }
                bool bAlreadyHit = false;
                for (const auto& record : hitbox->m_HitRecords)
                {
                    if (record.VictimActorId != partFrame.ActorId)
                    {
                        continue;
                    }
                    if (profile.RehitPolicy == ERehitPolicy::OncePerVictim ||
                        (record.PartId == partFrame.PartId &&
                         (profile.RehitPolicy == ERehitPolicy::OncePerPart ||
                          (profile.RehitPolicy == ERehitPolicy::Interval &&
                           m_WorldTime - record.LastHitTime + kRehitTimeToleranceSeconds <
                               static_cast<double>(profile.RehitIntervalSeconds)))))
                    {
                        bAlreadyHit = true;
                        break;
                    }
                }
                if (bAlreadyHit)
                {
                    continue;
                }
                HitCandidate candidate;
                candidate.HitboxSlot = hitboxFrame.Slot;
                candidate.HurtboxSlot = partFrame.Slot;
                candidate.PartIndex = partFrame.PartIndex;
                candidate.Priority = partFrame.Priority;
                candidate.HitboxOwner = hitboxFrame.Owner;
                candidate.HitboxBinding = hitboxFrame.Binding;
                candidate.HurtboxOwner = partFrame.Owner;
                candidate.HurtboxBinding = partFrame.Binding;
                candidate.Policy = profile.PerVictimPolicy;
                candidate.Event.AttackInstanceId = hitbox->m_AttackInstanceId;
                candidate.Event.ProfileId = profile.ProfileId.GetHash();
                candidate.Event.AttackerActorId = attackerId;
                candidate.Event.VictimActorId = partFrame.ActorId;
                candidate.Event.HitboxIndex = hitboxFrame.RegistrationIndex;
                candidate.Event.HurtboxIndex = partFrame.RegistrationIndex;
                candidate.Event.PartId = partFrame.PartId;
                candidate.Event.Point = contact.Point;
                candidate.Event.Normal = contact.Normal;
                candidate.Event.Depth = contact.Depth;
                candidate.Event.S = contact.S;
                candidate.Event.FrameNumber = m_FrameNumber;
                if (elapsed > 0.0f)
                {
                    candidate.Event.RelativeVelocity =
                        ((hitboxFrame.Volume.Current.Position - hitboxFrame.Volume.Previous.Position) -
                         (partFrame.Volume.Current.Position - partFrame.Volume.Previous.Position)) / elapsed;
                }
                candidates.push_back(candidate);
            }
        }

        for (size_t index = 0; index < candidates.size(); ++index)
        {
            auto& candidate = candidates[index];
            if (!candidate.bSelected || candidate.Policy == EPerVictimPolicy::AllParts)
            {
                continue;
            }
            size_t best = index;
            for (size_t other = index + 1; other < candidates.size(); ++other)
            {
                auto& next = candidates[other];
                if (!next.bSelected || next.Event.HitboxIndex != candidate.Event.HitboxIndex ||
                    next.Event.VictimActorId != candidate.Event.VictimActorId)
                {
                    continue;
                }
                const auto& current = candidates[best];
                const bool bBetterPriority = candidate.Policy == EPerVictimPolicy::HighestPriority &&
                                             next.Priority > current.Priority;
                const bool bSamePriority = candidate.Policy != EPerVictimPolicy::HighestPriority ||
                                           next.Priority == current.Priority;
                if (bBetterPriority || (bSamePriority && IsEarlierPart(next, current)))
                {
                    candidates[best].bSelected = false;
                    best = other;
                }
                else
                {
                    next.bSelected = false;
                }
            }
        }

        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
            [](const HitCandidate& candidate) { return !candidate.bSelected; }), candidates.end());
        Core::Container::VariableArray<size_t> deliveryOrder;
        deliveryOrder.reserve(candidates.size());
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            deliveryOrder.push_back(index);
        }
        std::stable_sort(deliveryOrder.begin(), deliveryOrder.end(), [&candidates](size_t leftIndex, size_t rightIndex)
        {
            const auto& left = candidates[leftIndex];
            const auto& right = candidates[rightIndex];
            if (left.Event.HitboxIndex != right.Event.HitboxIndex)
            {
                return left.Event.HitboxIndex < right.Event.HitboxIndex;
            }
            if (left.Event.HurtboxIndex != right.Event.HurtboxIndex)
            {
                return left.Event.HurtboxIndex < right.Event.HurtboxIndex;
            }
            return left.PartIndex < right.PartIndex;
        });
        for (size_t candidateIndex : deliveryOrder)
        {
            const auto& candidate = candidates[candidateIndex];
            auto* hitbox = m_Hitboxes[candidate.HitboxSlot].Component;
            HitboxComponent::HitRecord* existing = nullptr;
            for (auto& record : hitbox->m_HitRecords)
            {
                if (record.VictimActorId == candidate.Event.VictimActorId && record.PartId == candidate.Event.PartId)
                {
                    existing = &record;
                    break;
                }
            }
            if (existing != nullptr)
            {
                existing->LastHitTime = m_WorldTime;
            }
            else
            {
                hitbox->m_HitRecords.push_back({candidate.Event.VictimActorId, candidate.Event.PartId, m_WorldTime});
            }
        }

        for (size_t candidateIndex : deliveryOrder)
        {
            const auto& candidate = candidates[candidateIndex];
            if (m_StateRevision != stateRevision)
            {
                break;
            }
            // 配送中は登録の添字と通し番号を照合し、列挙中の登録簿を保持しない。
            const auto& hitEntry = m_Hitboxes[candidate.HitboxSlot];
            const auto& hurtEntry = m_HurtboxSets[candidate.HurtboxSlot];
            auto* hitbox = hitEntry.Component;
            auto* hurtbox = hurtEntry.Component;
            auto* hitOwner = world.ResolveEntityHandle(candidate.HitboxOwner);
            auto* hitBinding = world.ResolveEntityHandle(candidate.HitboxBinding);
            auto* hurtOwner = world.ResolveEntityHandle(candidate.HurtboxOwner);
            auto* hurtBinding = world.ResolveEntityHandle(candidate.HurtboxBinding);
            if (hitEntry.RegistrationIndex != candidate.Event.HitboxIndex || hitEntry.Owner != candidate.HitboxOwner ||
                hurtEntry.RegistrationIndex != candidate.Event.HurtboxIndex || hurtEntry.Owner != candidate.HurtboxOwner ||
                !IsUsableComponent(hitbox) || !IsUsableComponent(hurtbox) ||
                !IsUsableEntity(hitOwner) || !IsUsableEntity(hitBinding) ||
                !IsUsableEntity(hurtOwner) || !IsUsableEntity(hurtBinding) ||
                hitbox->GetOwner() != hitOwner || hurtbox->GetOwner() != hurtOwner ||
                hitbox->GetBinding() != candidate.HitboxBinding || candidate.PartIndex >= hurtbox->m_Parts.size() ||
                hurtbox->m_Parts[candidate.PartIndex].Binding != candidate.HurtboxBinding)
            {
                continue;
            }
            const auto onHit = hitbox->OnHit;
            const auto onHurt = hurtbox->OnHurt;
            const auto onAnyHit = OnAnyHit;
            onHit.Broadcast(candidate.Event);
            onHurt.Broadcast(candidate.Event);
            onAnyHit.Broadcast(candidate.Event);
        }

        if (m_StateRevision == stateRevision)
        {
            for (const auto& frame : hitboxFrames)
            {
                const auto& entry = m_Hitboxes[frame.Slot];
                auto* hitbox = entry.Component;
                if (hitbox == nullptr || entry.RegistrationIndex != frame.RegistrationIndex ||
                    hitbox->m_PoseRevision != frame.PoseRevision)
                {
                    continue;
                }
                auto* owner = world.ResolveEntityHandle(frame.Owner);
                const bool bStillAvailable = frame.bPoseAvailable && owner != nullptr && hitbox->GetOwner() == owner &&
                    world.ResolveEntityHandle(frame.Binding) != nullptr && hitbox->GetBinding() == frame.Binding;
                hitbox->m_bPreviousValid = bStillAvailable;
                if (bStillAvailable)
                {
                    hitbox->m_PreviousTransform = frame.CurrentTransform;
                    hitbox->m_bFirstWindowFrame = false;
                }
            }
            for (const auto& frame : partFrames)
            {
                const auto& entry = m_HurtboxSets[frame.Slot];
                auto* hurtbox = entry.Component;
                if (hurtbox == nullptr || entry.RegistrationIndex != frame.RegistrationIndex ||
                    frame.PartIndex >= hurtbox->m_Parts.size())
                {
                    continue;
                }
                auto& part = hurtbox->m_Parts[frame.PartIndex];
                auto* owner = world.ResolveEntityHandle(frame.Owner);
                const bool bStillAvailable = frame.bPoseAvailable && owner != nullptr && hurtbox->GetOwner() == owner &&
                    world.ResolveEntityHandle(frame.Binding) != nullptr && part.Binding == frame.Binding;
                part.bPreviousValid = bStillAvailable;
                if (bStillAvailable)
                {
                    part.PreviousTransform = frame.CurrentTransform;
                }
            }
        }
        CompactRegistrations();
        m_bEvaluating = false;
    }
} // namespace NorvesLib::Modules::Gameplay::Combat
