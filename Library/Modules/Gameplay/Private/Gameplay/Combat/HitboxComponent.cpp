#include "Gameplay/Combat/HitboxComponent.h"

#include "Component/AnimatorComponent.h"
#include "Gameplay/Combat/CombatRuntime.h"
#include "Gameplay/GameplayModule.h"
#include "Object/World.h"

namespace NorvesLib::Modules::Gameplay::Combat
{
    IMPLEMENT_CLASS(HitboxComponent, Core::Component::Component)

    HitboxComponent::HitboxComponent()
        : m_OwnerThreadId(Thread::Thread::GetCurrentThreadId())
    {
    }

    HitboxComponent::~HitboxComponent()
    {
        DetachAnimator();
        Unregister();
    }

    bool HitboxComponent::IsOwnerThread() const
    {
        if (m_Runtime != nullptr)
        {
            return m_Runtime->IsOwnerThread();
        }
        auto* module = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
        return module != nullptr ? module->IsOwnerThread()
                                 : m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    void HitboxComponent::Initialize()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        Super::Initialize();
        m_bCombatReady = true;
        TryRegister();
        if (auto* owner = GetOwner())
        {
            BindAnimator(owner->GetComponent<Core::Component::AnimatorComponent>());
        }
    }

    void HitboxComponent::BeginPlay()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        Super::BeginPlay();
        m_bCombatReady = true;
        TryRegister();
        if (auto* owner = GetOwner())
        {
            BindAnimator(owner->GetComponent<Core::Component::AnimatorComponent>());
        }
    }

    void HitboxComponent::EndPlay()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        DetachAnimator();
        Unregister();
        ResetWindow();
        m_bCombatReady = false;
        Super::EndPlay();
    }

    void HitboxComponent::Finalize()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        EndPlay();
        Super::Finalize();
    }

    void HitboxComponent::Disable()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        EndAttack();
        Super::Disable();
    }

    bool HitboxComponent::TryRegister()
    {
        if (!IsOwnerThread() || !m_bCombatReady)
        {
            return false;
        }
        auto* owner = GetOwner();
        auto* world = owner != nullptr ? owner->GetWorld() : nullptr;
        auto* module = world != nullptr ? GameplayModule::FindBoundModule(*world) : nullptr;
        return module != nullptr && module->GetCombatRuntime().Register(*this);
    }

    void HitboxComponent::Unregister()
    {
        if (m_Runtime != nullptr)
        {
            m_Runtime->Unregister(*this);
        }
    }

    bool HitboxComponent::SetShape(const CombatShape& shape)
    {
        if (!IsOwnerThread() || (shape.Type != ECombatShapeType::Sphere && shape.Type != ECombatShapeType::Capsule))
        {
            return false;
        }
        m_Shape = shape;
        m_bPreviousValid = false;
        m_bFirstWindowFrame = m_bWindowOpen;
        return true;
    }

    bool HitboxComponent::SetBinding(Core::Entity* entity)
    {
        if (!IsOwnerThread())
        {
            return false;
        }
        if (entity != nullptr)
        {
            auto* owner = GetOwner();
            if (owner == nullptr || owner->GetWorld() == nullptr || entity->GetWorld() != owner->GetWorld() ||
                !entity->GetEntityHandle().IsValid())
            {
                return false;
            }
        }
        m_bOwnerBinding = entity == nullptr;
        m_Binding = entity != nullptr ? entity->GetEntityHandle() : Core::EntityHandle{};
        m_bPreviousValid = false;
        m_bFirstWindowFrame = m_bWindowOpen;
        return true;
    }

    Core::EntityHandle HitboxComponent::GetBinding() const
    {
        const auto* owner = GetOwner();
        return m_bOwnerBinding && owner != nullptr ? owner->GetEntityHandle() : m_Binding;
    }

    void HitboxComponent::SetProfile(const Core::Container::TSharedPtr<const AttackProfile>& profile)
    {
        if (!IsOwnerThread())
        {
            return;
        }
        EndAttack();
        m_Profile = profile;
    }

    uint64_t HitboxComponent::GetActorId() const
    {
        const auto* owner = GetOwner();
        return m_bExplicitActorId || owner == nullptr ? m_ActorId : owner->GetObjectId();
    }

    bool HitboxComponent::BeginAttack()
    {
        if (!IsOwnerThread() || !m_bCombatReady || !IsEnabled() || IsPendingDestroy() ||
            !Core::Container::IsValid(m_Profile) || GetActorId() == 0 || !TryRegister())
        {
            return false;
        }
        auto* owner = GetOwner();
        auto* world = owner != nullptr ? owner->GetWorld() : nullptr;
        auto* binding = world != nullptr ? world->ResolveEntityHandle(GetBinding()) : nullptr;
        if (owner == nullptr || owner->IsPendingDestroy() || !owner->IsActive() ||
            binding == nullptr || binding->IsPendingDestroy() || !binding->IsActive())
        {
            return false;
        }
        const uint64_t instanceId = m_Runtime->AllocateAttackInstanceId();
        if (instanceId == 0)
        {
            return false;
        }
        ResetWindow();
        m_AttackInstanceId = instanceId;
        m_WindowOpenedAt = m_Runtime->GetWorldTime();
        m_bWindowOpen = true;
        m_bFirstWindowFrame = true;
        return true;
    }

    void HitboxComponent::EndAttack()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        ResetWindow();
    }

    void HitboxComponent::ResetWindow()
    {
        m_bWindowOpen = false;
        m_AttackInstanceId = 0;
        m_WindowOpenedAt = 0.0;
        m_bFirstWindowFrame = false;
        m_PreviousTransform = {};
        m_bPreviousValid = false;
        m_HitRecords.clear();
        m_WindowToken = 0;
        m_bAnimationWindow = false;
    }

    bool HitboxComponent::BindAnimator(Core::Component::AnimatorComponent* animator)
    {
        if (!IsOwnerThread() || (animator != nullptr &&
            (!m_bCombatReady || GetOwner() == nullptr || animator->GetOwner() != GetOwner() || animator->IsPendingDestroy())))
        {
            return false;
        }
        DetachAnimator();
        if (animator == nullptr)
        {
            return true;
        }
        m_Animator = animator;
        m_AnimatorComponentId = animator->GetComponentId();
        m_AnimatorWorld = GetOwner()->GetWorld();
        m_AnimatorOwner = GetOwner()->GetEntityHandle();
        m_AnimatorSubscription = Core::Container::MakeShared<AnimatorSubscription>();
        m_AnimatorSubscription->Target = this;
        const auto subscription = m_AnimatorSubscription;
        m_AnimatorCallback = Core::Delegate<void, const Core::Animation::AnimEventInfo&>(
            [subscription](const Core::Animation::AnimEventInfo& event)
            {
                if (subscription->Target != nullptr)
                {
                    subscription->Target->HandleAnimationEvent(event);
                }
            });
        animator->OnEvent.Add(m_AnimatorCallback);
        return true;
    }

    void HitboxComponent::DetachAnimator()
    {
        if (!IsOwnerThread())
        {
            return;
        }
        // 配送前に複製された購読も無効化し、配送中の破棄で this を参照させない。
        if (Core::Container::IsValid(m_AnimatorSubscription))
        {
            m_AnimatorSubscription->Target = nullptr;
        }
        // Animator が先に破棄された場合は既に購読も破棄済み。生きているものだけを参照する。
        auto* owner = GetOwner();
        if (owner == nullptr && m_AnimatorWorld != nullptr)
        {
            owner = m_AnimatorWorld->ResolveEntityHandle(m_AnimatorOwner);
        }
        if (m_Animator != nullptr && owner != nullptr)
        {
            for (auto* component : owner->GetComponents())
            {
                if (component == m_Animator && component->GetComponentId() == m_AnimatorComponentId)
                {
                    m_Animator->OnEvent.Remove(m_AnimatorCallback);
                    break;
                }
            }
        }
        m_Animator = nullptr;
        m_AnimatorComponentId = 0;
        m_AnimatorWorld = nullptr;
        m_AnimatorOwner = {};
        m_AnimatorSubscription = {};
        m_AnimatorCallback.Clear();
        if (m_bAnimationWindow)
        {
            ResetWindow();
        }
    }

    void HitboxComponent::HandleAnimationEvent(const Core::Animation::AnimEventInfo& event)
    {
        if (!IsOwnerThread() || !m_bCombatReady || !Core::Container::IsValid(m_Profile))
        {
            return;
        }
        if (event.Kind == Core::Animation::AnimEventKind::Begin &&
            m_Profile->WindowEventName.IsValid() && event.Name == m_Profile->WindowEventName)
        {
            if (BeginAttack())
            {
                m_WindowToken = event.WindowToken;
                m_bAnimationWindow = true;
            }
        }
        else if (event.Kind == Core::Animation::AnimEventKind::End &&
                 m_bAnimationWindow && event.WindowToken == m_WindowToken)
        {
            EndAttack();
        }
    }
} // namespace NorvesLib::Modules::Gameplay::Combat
