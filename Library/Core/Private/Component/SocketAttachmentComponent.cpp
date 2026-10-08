#include "Component/SocketAttachmentComponent.h"
#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/HoldSlotComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/QuaternionUtils.h"
#include "Object/World.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Component
{
    namespace
    {
        bool Active(const Entity* entity)
        {
            for (auto* current = entity; current; current = current->GetParentEntity())
                if (!current->IsActive() || current->IsPendingDestroy())
                    return false;
            return entity != nullptr;
        }
        Math::Transform Mix(const Math::Transform& a, const Math::Transform& b, float t)
        {
            return {a.position + (b.position - a.position) * t, Math::QuaternionUtils::Slerp(a.rotation, b.rotation, t),
                    a.scale + (b.scale - a.scale) * t};
        }
        bool Rigid(const Math::Transform& transform)
        {
            Animation::SocketDefinition socket;
            socket.Name = Identity("offset");
            socket.ParentJoint = 0;
            socket.Offset = transform;
            Animation::SocketReport report;
            return Animation::ValidateSockets({&socket, 1}, 1, report);
        }
    } // namespace
    IMPLEMENT_CLASS(SocketAttachmentComponent, Component)
    SocketAttachmentComponent::SocketAttachmentComponent()
    {
        SetTickGroup(ETickGroup::PostPhysics);
    }
    SocketAttachmentComponent::SocketAttachmentComponent(const FieldInitializer* f) : Component(f)
    {
        SetTickGroup(ETickGroup::PostPhysics);
    }
    SocketAttachmentComponent::SocketAttachmentComponent(const IUnknown* s) : Component(s)
    {
        SetTickGroup(ETickGroup::PostPhysics);
    }
    SocketAttachmentComponent::~SocketAttachmentComponent() = default;
    Entity* SocketAttachmentComponent::ResolveTarget() const
    {
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        if (!world || world != m_BoundWorld || !m_Target)
            return nullptr;
        auto* target = world->FindEntityByObjectId(m_Target);
        return Active(target) && target->GetWorld() == world ? target : nullptr;
    }
    void SocketAttachmentComponent::UnbindEvents()
    {
        auto* world = m_BoundWorld;
        auto* target = world ? world->FindEntityByObjectId(m_Target) : nullptr;
        if (target && m_BoundAnimator)
            for (auto* inner : target->GetInners())
                if (auto* animator = CastTo<AnimatorComponent>(inner);
                    animator && animator->GetComponentId() == m_BoundAnimator)
                    animator->OnEvent.Remove(this, &SocketAttachmentComponent::OnAnimationEvent);
        m_BoundAnimator = 0;
    }
    void SocketAttachmentComponent::BindEvents()
    {
        auto* target = ResolveTarget();
        auto* animator = target ? target->GetComponent<AnimatorComponent>() : nullptr;
        if (!animator || animator->IsPendingDestroy() || !animator->IsActive())
        {
            UnbindEvents();
            return;
        }
        if (m_BoundAnimator == animator->GetComponentId())
            return;
        UnbindEvents();
        animator->OnEvent.Add(this, &SocketAttachmentComponent::OnAnimationEvent);
        m_BoundAnimator = animator->GetComponentId();
    }
    void SocketAttachmentComponent::OnAnimationEvent(const Animation::AnimEventInfo& event)
    {
        if (!IsPendingDestroy() && event.Name == Identity("Hold.Profile") &&
            event.Kind != Animation::AnimEventKind::End && event.IntValue >= 0)
            (void)SetProfileByIndex(uint32_t(event.IntValue));
    }
    void SocketAttachmentComponent::MarkSlotTransition(bool value)
    {
        auto* target = ResolveTarget();
        auto* slots = target ? target->GetComponent<HoldSlotComponent>() : nullptr;
        if (slots && m_Slot.IsValid() && GetOwner())
            (void)slots->SetTransitioning(m_Slot, GetOwner()->GetObjectId(), value);
    }
    bool SocketAttachmentComponent::Attach(uint64_t targetId, Identity socket, Identity slot, float seconds,
                                           Container::Span<const Identity> tags, float mass)
    {
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        if (!world || !Active(owner) || IsPendingDestroy() || !IsActive() || m_State != AttachState::Detached ||
            !std::isfinite(seconds) || seconds < 0 || !targetId || targetId == owner->GetObjectId())
            return false;
        auto* target = world->FindEntityByObjectId(targetId);
        if (!Active(target))
            return false;
        auto* mesh = target->GetComponent<SkinnedMeshComponent>();
        if (!mesh || mesh->IsPendingDestroy() || !mesh->IsActive() || !mesh->GetSkeletalAsset() ||
            !mesh->GetSkeletalAsset()->GetSkeleton() || !mesh->GetSkeletalAsset()->GetSkeleton()->FindSocket(socket))
            return false;
        // Entity親と追従先の両方を辿り、合成された依存の循環も拒否する。
        uint64_t path[64]{};
        auto validDependency = [&](auto&& self, Entity* entity, unsigned depth) -> bool {
            if (!entity)
                return true;
            if (depth >= 64 || entity == owner)
                return false;
            for (unsigned i = 0; i < depth; ++i)
                if (path[i] == entity->GetObjectId())
                    return false;
            path[depth] = entity->GetObjectId();
            if (!self(self, entity->GetParentEntity(), depth + 1))
                return false;
            auto* attachment = entity->GetComponent<SocketAttachmentComponent>();
            return !attachment || attachment->m_State == AttachState::Detached ||
                   self(self, world->FindEntityByObjectId(attachment->m_Target), depth + 1);
        };
        if (!validDependency(validDependency, target, 0))
            return false;
        if (slot.IsValid())
        {
            auto* slots = target->GetComponent<HoldSlotComponent>();
            if (!slots || slots->IsPendingDestroy() || !slots->IsActive())
                return false;
            const auto result = slots->TryAcquire(slot, owner->GetObjectId(), tags, mass, seconds > 0);
            if (result != HoldAcquireResult::Granted && result != HoldAcquireResult::AlreadyHeld)
                return false;
            if (!slots->SetTransitioning(slot, owner->GetObjectId(), seconds > 0))
                return false;
        }
        m_BoundWorld = world;
        m_BoundOwner = owner->GetObjectId();
        m_Target = targetId;
        m_Socket = socket;
        m_Slot = slot;
        m_AttachFrom = owner->GetWorldTransform();
        m_AttachElapsed = 0;
        m_AttachDuration = seconds;
        m_State = seconds > 0 ? AttachState::Blending : AttachState::Attached;
        m_bHaveSample = false;
        m_Velocity = {};
        m_LastFrame = UINT64_MAX;
        BindEvents();
        return true;
    }
    void SocketAttachmentComponent::Detach()
    {
        if (m_State == AttachState::Detached)
        {
            UnbindEvents();
            return;
        }
        m_ReleaseVelocity = m_Velocity;
        UnbindEvents();
        auto* world = m_BoundWorld;
        auto* target = world ? world->FindEntityByObjectId(m_Target) : nullptr;
        if (target && m_Slot.IsValid())
            if (auto* slots = target->GetComponent<HoldSlotComponent>())
                (void)slots->ReleaseInternal(m_Slot, m_BoundOwner, true);
        m_State = AttachState::Detached;
        m_Target = 0;
        m_BoundWorld = nullptr;
        m_BoundOwner = 0;
        m_Slot = {};
        m_Socket = {};
        m_bHaveSample = false;
        m_bLastSuccess = false;
    }
    bool SocketAttachmentComponent::SetProfiles(Container::Span<const Animation::AttachProfile> profiles)
    {
        if (profiles.size() > 64)
            return false;
        for (size_t i = 0; i < profiles.size(); ++i)
        {
            Animation::SocketDefinition check;
            check.Name = profiles[i].Name;
            check.ParentJoint = 0;
            check.Offset = profiles[i].Offset;
            Animation::SocketReport report;
            if (!Animation::ValidateSockets({&check, 1}, 1, report))
                return false;
            for (size_t j = 0; j < i; ++j)
                if (profiles[j].Name == profiles[i].Name)
                    return false;
        }
        Container::VariableArray<Animation::AttachProfile> candidate(profiles.begin(), profiles.end());
        m_Profiles = std::move(candidate);
        m_ProfileIndex = UINT32_MAX;
        if (m_Profiles.empty())
        {
            m_ProfileCurrent = {};
            m_ProfileFrom = {};
            m_ProfileTarget = {};
            m_ProfileDuration = 0;
            m_ProfileElapsed = 0;
            return true;
        }
        return SetProfileByIndex(0);
    }
    bool SocketAttachmentComponent::ApplySettingsFile(const Container::String& path, Animation::SocketReport& report)
    {
        Container::String json;
        return Animation::ReadSocketSettingsFile(path, json, report) && ApplySettingsJson(json, report);
    }
    bool SocketAttachmentComponent::ApplySettingsJson(const Container::String& json, Animation::SocketReport& report)
    {
        Container::VariableArray<Animation::AttachProfile> profiles;
        Math::Transform grip;
        if (!Animation::ParseAttachProfiles(json, profiles, grip, report) || !SetProfiles(profiles))
            return false;
        m_Grip = grip;
        return true;
    }
    bool SocketAttachmentComponent::SetProfileOffset(uint32_t index, const Math::Transform& offset, float seconds)
    {
        if (index >= m_Profiles.size() || !Rigid(offset) || !std::isfinite(seconds) || seconds < 0)
            return false;
        m_Profiles[index].Offset = offset;
        if (index == m_ProfileIndex)
        {
            m_ProfileIndex = UINT32_MAX;
            return SetProfileByIndex(index, seconds);
        }
        return true;
    }
    bool SocketAttachmentComponent::SetGrip(const Math::Transform& grip)
    {
        if (!Rigid(grip))
            return false;
        m_Grip = grip;
        return true;
    }
    bool SocketAttachmentComponent::SetProfile(Identity name, float seconds)
    {
        for (uint32_t i = 0; i < m_Profiles.size(); ++i)
            if (m_Profiles[i].Name == name)
                return SetProfileByIndex(i, seconds);
        return false;
    }
    bool SocketAttachmentComponent::SetProfileByIndex(uint32_t index, float seconds)
    {
        if (index >= m_Profiles.size() || !std::isfinite(seconds) || seconds < 0)
            return false;
        if (m_ProfileParameter.IsValid())
        {
            auto* target = ResolveTarget();
            auto* animator = target ? target->GetComponent<AnimatorComponent>() : nullptr;
            if (animator && !animator->IsPendingDestroy())
                (void)animator->SetInt(animator->FindParam(m_ProfileParameter), int32_t(index));
        }
        if (index == m_ProfileIndex)
            return true;
        m_ProfileIndex = index;
        m_ProfileFrom = m_ProfileCurrent;
        m_ProfileTarget = m_Profiles[index].Offset;
        m_ProfileElapsed = 0;
        m_ProfileDuration = m_State == AttachState::Detached ? 0 : seconds;
        if (m_ProfileDuration == 0)
            m_ProfileCurrent = m_ProfileTarget;
        else
        {
            m_State = AttachState::Blending;
            MarkSlotTransition(true);
        }
        return true;
    }
    bool SocketAttachmentComponent::SetVelocitySampling(float maximumDt, float smoothing)
    {
        if (!std::isfinite(maximumDt) || maximumDt <= 0 || !std::isfinite(smoothing) || smoothing < 0)
            return false;
        m_MaximumSampleDt = maximumDt;
        m_SmoothingSeconds = smoothing;
        return true;
    }
    bool SocketAttachmentComponent::UpdateAttachment(float dt, uint64_t frame, uint32_t depth)
    {
        if (m_State == AttachState::Detached)
            return true;
        if (m_bVisiting || depth >= 64)
            return false;
        if (m_LastFrame == frame)
            return m_bLastSuccess;
        if (!std::isfinite(dt) || dt < 0)
            return false;
        m_LastFrame = frame;
        m_bLastSuccess = false;
        struct Guard
        {
            bool& Flag;
            explicit Guard(bool& b) : Flag(b)
            {
                Flag = true;
            }
            ~Guard()
            {
                Flag = false;
            }
        } guard(m_bVisiting);
        auto* owner = GetOwner();
        auto* target = ResolveTarget();
        if (!Active(owner) || !target)
            return false;
        // SetWorldTransformもowner親の確定値を使う。両方の親鎖を祖先から公開する。
        auto ensure = [&](auto&& self, Entity* entity, uint32_t level) -> bool {
            if (!entity)
                return true;
            if (level >= 64)
                return false;
            if (!self(self, entity->GetParentEntity(), level + 1))
                return false;
            auto* attachment = entity->GetComponent<SocketAttachmentComponent>();
            if (!attachment || attachment->m_State == AttachState::Detached)
                return true;
            if (!attachment->IsTickEnabled() || !entity->IsTickEnabled())
                return true;
            if (attachment == this || attachment->IsPendingDestroy() || !attachment->IsActive() ||
                !attachment->UpdateAttachment(dt, frame, level + 1))
                return false;
            owner->GetWorld()->UpdateWorldTransforms();
            return true;
        };
        if (!ensure(ensure, owner->GetParentEntity(), depth + 1) || !ensure(ensure, target, depth + 1))
            return false;
        BindEvents();
        if (m_ProfileParameter.IsValid())
            if (auto* animator = target->GetComponent<AnimatorComponent>())
            {
                int32_t value = 0;
                if (animator->GetInt(animator->FindParam(m_ProfileParameter), value) && value >= 0)
                    (void)SetProfileByIndex(uint32_t(value));
            }
        m_ProfileElapsed = std::min(m_ProfileDuration, m_ProfileElapsed + dt);
        m_ProfileCurrent =
            Mix(m_ProfileFrom, m_ProfileTarget, m_ProfileDuration <= 0 ? 1 : m_ProfileElapsed / m_ProfileDuration);
        auto* mesh = target->GetComponent<SkinnedMeshComponent>();
        Math::Transform socket, wanted;
        if (!mesh || mesh->IsPendingDestroy() || !mesh->IsActive() ||
            !mesh->GetSocketWorldTransform(m_Socket, socket) ||
            !Animation::ComposeSocketAttachment(socket, m_ProfileCurrent, m_Grip, wanted))
            return false;
        m_AttachElapsed = std::min(m_AttachDuration, m_AttachElapsed + dt);
        const auto result = m_AttachDuration > 0 && m_AttachElapsed < m_AttachDuration
                                ? Mix(m_AttachFrom, wanted, m_AttachElapsed / m_AttachDuration)
                                : wanted;
        if (m_bHaveSample && dt > 1e-6f && dt <= m_MaximumSampleDt)
        {
            const auto linear = (result.position - m_LastWorld.position) / dt;
            auto delta = result.rotation * Math::QuaternionUtils::Conjugate(m_LastWorld.rotation);
            if (delta.w < 0)
                delta = {-delta.x, -delta.y, -delta.z, -delta.w};
            const double sine = std::hypot(double(delta.x), delta.y, delta.z);
            const double angle = 2 * std::atan2(sine, std::clamp(double(delta.w), -1.0, 1.0));
            const auto angular = sine > 1e-9 ? Math::Vector3(delta.x, delta.y, delta.z) * float(angle / (sine * dt))
                                             : Math::Vector3::Zero;
            const float alpha = m_SmoothingSeconds <= 0 ? 1 : float(1 - std::exp(-double(dt) / m_SmoothingSeconds));
            m_Velocity.Linear = m_Velocity.Linear + (linear - m_Velocity.Linear) * alpha;
            m_Velocity.Angular = m_Velocity.Angular + (angular - m_Velocity.Angular) * alpha;
        }
        else if (dt > m_MaximumSampleDt)
            m_Velocity = {};
        m_LastWorld = result;
        m_bHaveSample = true;
        owner->SetWorldTransform(result);
        if (m_AttachElapsed >= m_AttachDuration && m_ProfileElapsed >= m_ProfileDuration)
        {
            m_State = AttachState::Attached;
            MarkSlotTransition(false);
        }
        m_bLastSuccess = true;
        return true;
    }
    void SocketAttachmentComponent::OnTickGroup(ETickGroup group, float dt)
    {
        if (group != ETickGroup::PostPhysics || m_State == AttachState::Detached)
            return;
        auto* world = GetOwner() ? GetOwner()->GetWorld() : nullptr;
        if (!world || !UpdateAttachment(dt, world->GetTickSerial(), 0))
            Detach();
    }
    void SocketAttachmentComponent::EndPlay()
    {
        Detach();
        Component::EndPlay();
    }
    void SocketAttachmentComponent::Finalize()
    {
        Detach();
        m_Profiles.clear();
        Component::Finalize();
    }
} // namespace NorvesLib::Core::Component
