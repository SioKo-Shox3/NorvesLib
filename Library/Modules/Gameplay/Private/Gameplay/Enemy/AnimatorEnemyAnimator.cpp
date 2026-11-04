#include "Gameplay/Enemy/AnimatorEnemyAnimator.h"

#include "Component/AnimatorComponent.h"
#include "Object/World.h"

namespace NorvesLib::Modules::Gameplay::Enemy
{
    AnimatorEnemyAnimator::AnimatorEnemyAnimator(Core::Component::AnimatorComponent* animator, Core::Identity machine)
    {
        Bind(animator, machine);
    }

    AnimatorEnemyAnimator::~AnimatorEnemyAnimator()
    {
        Unbind();
    }

    bool AnimatorEnemyAnimator::Bind(Core::Component::AnimatorComponent* animator, Core::Identity machine)
    {
        Unbind();
        if (animator == nullptr)
        {
            return true;
        }
        auto* owner = animator->GetOwner();
        if (animator->IsPendingDestroy() || owner == nullptr || owner->IsPendingDestroy())
        {
            return false;
        }
        auto* world = owner->GetWorld();
        const auto handle = owner->GetEntityHandle();
        if (world == nullptr || !handle.IsValid() || world->ResolveEntityHandle(handle) != owner)
        {
            return false;
        }

        m_Animator = animator;
        m_AnimatorComponentId = animator->GetComponentId();
        m_AnimatorWorld = world;
        m_AnimatorOwner = handle;
        m_Machine = machine;
        m_AnimatorSubscription = Core::Container::MakeShared<AnimatorSubscription>();
        m_AnimatorSubscription->Target = this;
        const auto subscription = m_AnimatorSubscription;
        m_AnimatorCallback = Core::Delegate<void, const Core::Animation::AnimEventInfo&>(
            [subscription](const Core::Animation::AnimEventInfo& event)
            {
                if (subscription->Target != nullptr)
                {
                    subscription->Target->ForwardEvent(event);
                }
            });
        animator->OnEvent.Add(m_AnimatorCallback);
        return true;
    }

    Core::Component::AnimatorComponent* AnimatorEnemyAnimator::ResolveAnimator() const
    {
        if (m_Animator == nullptr || m_AnimatorWorld == nullptr)
        {
            return nullptr;
        }
        auto* owner = m_AnimatorWorld->ResolveEntityHandle(m_AnimatorOwner);
        if (owner == nullptr)
        {
            return nullptr;
        }
        // 保持したポインタは照合だけに使い、生きている一覧の要素から ID を読む。
        for (auto* component : owner->GetComponents())
        {
            if (component == m_Animator && component->GetComponentId() == m_AnimatorComponentId)
            {
                return m_Animator;
            }
        }
        return nullptr;
    }

    void AnimatorEnemyAnimator::Unbind()
    {
        // Animator が配送前に複製したコールバックも、解除後は呼出先に触れない。
        if (Core::Container::IsValid(m_AnimatorSubscription))
        {
            m_AnimatorSubscription->Target = nullptr;
        }
        if (auto* animator = ResolveAnimator())
        {
            animator->OnEvent.Remove(m_AnimatorCallback);
        }
        m_Animator = nullptr;
        m_AnimatorComponentId = 0;
        m_AnimatorWorld = nullptr;
        m_AnimatorOwner = {};
        m_Machine = {};
        m_RequestedState = {};
        m_bHasRequestedState = false;
        m_AnimatorSubscription = {};
        m_AnimatorCallback.Clear();
    }

    bool AnimatorEnemyAnimator::RequestState(Core::Identity state, float blendSeconds)
    {
        auto* animator = ResolveAnimator();
        if (animator == nullptr || animator->IsPendingDestroy() ||
            !animator->RequestState(m_Machine, state, blendSeconds))
        {
            return false;
        }
        m_RequestedState = state;
        m_bHasRequestedState = true;
        return true;
    }

    double AnimatorEnemyAnimator::NormalizedTime() const
    {
        auto* animator = ResolveAnimator();
        Core::Animation::AnimStateStatus status;
        if (!m_bHasRequestedState || animator == nullptr || animator->IsPendingDestroy() ||
            !animator->GetState(m_Machine, status) || status.Current != m_RequestedState)
        {
            return 0.0;
        }
        return status.NormalizedTime;
    }

    bool AnimatorEnemyAnimator::IsFinished() const
    {
        auto* animator = ResolveAnimator();
        Core::Animation::AnimStateStatus status;
        return m_bHasRequestedState && animator != nullptr && !animator->IsPendingDestroy() &&
               animator->GetState(m_Machine, status) && status.Current == m_RequestedState && status.bReachedEnd;
    }

    void AnimatorEnemyAnimator::SetEventListener(Core::Delegate<void, const Core::Animation::AnimEventInfo&> listener)
    {
        m_Listener = listener;
    }

    void AnimatorEnemyAnimator::ForwardEvent(const Core::Animation::AnimEventInfo& event)
    {
        // 通知先が自身を差し替える、またはこの窓口を破棄しても呼出中の Delegate を保つ。
        const auto listener = m_Listener;
        listener.InvokeIfBound(event);
    }
} // namespace NorvesLib::Modules::Gameplay::Enemy
