#pragma once

#include "Component/Component.h"
#include "Delegate/MulticastDelegate.h"
#include "Gameplay/Combat/AttackProfile.h"
#include "Gameplay/Combat/CombatHitEvent.h"
#include "Gameplay/Combat/CombatShape.h"
#include "Math/Transform.h"
#include "Thread/Thread.h"

namespace NorvesLib::Core::Animation
{
    struct AnimEventInfo;
}

namespace NorvesLib::Core::Component
{
    class AnimatorComponent;
}

namespace NorvesLib::Modules::Gameplay::Combat
{
    class CombatRuntime;

    /// 判定窓と武器ローカルの形状を保持する。操作と通知は GameThread 専用。
    /// ActorId の既定は持ち主の ObjectId。0 の場合と武器の子 Entity には SetActorId が必要。
    /// モジュール初期化前・World 参加前の生成は、初期化時と LateTick の走査で登録される。
    class HitboxComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(HitboxComponent, Core::Component::Component)

    public:
        HitboxComponent();
        ~HitboxComponent() override;
        void Initialize() override;
        void Finalize() override;
        void BeginPlay() override;
        void EndPlay() override;
        void Disable() override;

        /// Box は受理せず false。形状変更で前フレームの姿勢を無効にする。
        bool SetShape(const CombatShape& shape);
        const CombatShape& GetShape() const { return m_Shape; }
        /// nullptr は持ち主へ戻す。同じ World に参加済みの Entity だけを明示指定できる。
        bool SetBinding(Core::Entity* entity);
        Core::EntityHandle GetBinding() const;
        void SetProfile(const Core::Container::TSharedPtr<const AttackProfile>& profile);
        const Core::Container::TSharedPtr<const AttackProfile>& GetProfile() const { return m_Profile; }
        void SetActorId(uint64_t actorId) { m_ActorId = actorId; m_bExplicitActorId = true; }
        uint64_t GetActorId() const;

        bool BeginAttack();
        void EndAttack();
        bool IsWindowOpen() const { return m_bWindowOpen; }
        uint64_t GetAttackInstanceId() const { return m_AttackInstanceId; }
        double GetWindowOpenedAt() const { return m_WindowOpenedAt; }
        bool IsFirstWindowFrame() const { return m_bFirstWindowFrame; }
        uint64_t GetRegistrationIndex() const { return m_RegistrationIndex; }

        /// Initialize / BeginPlay で同じ Entity の Animator を探す。
        /// 後から Animator を足した場合は明示的に呼ぶ。nullptr は購読解除。
        bool BindAnimator(Core::Component::AnimatorComponent* animator);

        Core::MulticastDelegate<const CombatHitEvent&> OnHit;

    private:
        friend class CombatRuntime;
        struct HitRecord
        {
            uint64_t VictimActorId = 0;
            uint32_t PartId = 0;
            double LastHitTime = 0.0;
        };
        struct AnimatorSubscription
        {
            HitboxComponent* Target = nullptr;
        };

        bool IsOwnerThread() const;
        bool TryRegister();
        void Unregister();
        void DetachAnimator();
        void HandleAnimationEvent(const Core::Animation::AnimEventInfo& event);
        void ResetWindow();

        CombatShape m_Shape;
        Core::EntityHandle m_Binding;
        bool m_bOwnerBinding = true;
        Core::Container::TSharedPtr<const AttackProfile> m_Profile;
        uint64_t m_ActorId = 0;
        bool m_bExplicitActorId = false;
        CombatRuntime* m_Runtime = nullptr;
        uint64_t m_RegistrationIndex = 0;
        Thread::Thread::ThreadId m_OwnerThreadId;
        bool m_bCombatReady = false;
        bool m_bWindowOpen = false;
        uint64_t m_AttackInstanceId = 0;
        double m_WindowOpenedAt = 0.0;
        bool m_bFirstWindowFrame = false;
        Math::Transform m_PreviousTransform;
        bool m_bPreviousValid = false;
        Core::Container::VariableArray<HitRecord> m_HitRecords;
        Core::Component::AnimatorComponent* m_Animator = nullptr;
        uint64_t m_AnimatorComponentId = 0;
        Core::World* m_AnimatorWorld = nullptr;
        Core::EntityHandle m_AnimatorOwner;
        Core::Container::TSharedPtr<AnimatorSubscription> m_AnimatorSubscription;
        Core::Delegate<void, const Core::Animation::AnimEventInfo&> m_AnimatorCallback;
        uint64_t m_WindowToken = 0;
        bool m_bAnimationWindow = false;
    };
} // namespace NorvesLib::Modules::Gameplay::Combat
