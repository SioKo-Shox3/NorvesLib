#pragma once

#include "Gameplay/Enemy/IEnemyAnimator.h"
#include "Container/PointerTypes.h"
#include "Object/EntityHandle.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Core::Component
{
    class AnimatorComponent;
}

namespace NorvesLib::Modules::Gameplay::Enemy
{
    /// AnimatorComponent を借用する窓口。構築・破棄を含め GameThread 専用。
    /// World 登録済みの Animator に接続し、World の破棄前に Unbind すること。
    class AnimatorEnemyAnimator : public IEnemyAnimator
    {
    public:
        AnimatorEnemyAnimator() = default;
        AnimatorEnemyAnimator(Core::Component::AnimatorComponent* animator, Core::Identity machine);
        ~AnimatorEnemyAnimator() override;
        AnimatorEnemyAnimator(const AnimatorEnemyAnimator&) = delete;
        AnimatorEnemyAnimator& operator=(const AnimatorEnemyAnimator&) = delete;
        AnimatorEnemyAnimator(AnimatorEnemyAnimator&&) = delete;
        AnimatorEnemyAnimator& operator=(AnimatorEnemyAnimator&&) = delete;

        /// 既存の接続を外して接続する。World 未登録・破棄予約済みなら false。
        /// nullptr は解除のみ。通知先は接続変更後も保持する。
        bool Bind(Core::Component::AnimatorComponent* animator, Core::Identity machine);
        /// 接続と要求状態を解除する。通知先は SetEventListener で別途変更できる。
        void Unbind();

        bool RequestState(Core::Identity state, float blendSeconds) override;
        double NormalizedTime() const override;
        bool IsFinished() const override;
        void SetEventListener(Core::Delegate<void, const Core::Animation::AnimEventInfo&> listener) override;

    private:
        struct AnimatorSubscription
        {
            AnimatorEnemyAnimator* Target = nullptr;
        };

        Core::Component::AnimatorComponent* ResolveAnimator() const;
        void ForwardEvent(const Core::Animation::AnimEventInfo& event);

        Core::Component::AnimatorComponent* m_Animator = nullptr;
        uint64_t m_AnimatorComponentId = 0;
        Core::World* m_AnimatorWorld = nullptr;
        Core::EntityHandle m_AnimatorOwner;
        Core::Identity m_Machine;
        Core::Identity m_RequestedState;
        bool m_bHasRequestedState = false;
        Core::Container::TSharedPtr<AnimatorSubscription> m_AnimatorSubscription;
        Core::Delegate<void, const Core::Animation::AnimEventInfo&> m_AnimatorCallback;
        Core::Delegate<void, const Core::Animation::AnimEventInfo&> m_Listener;
    };
} // namespace NorvesLib::Modules::Gameplay::Enemy
