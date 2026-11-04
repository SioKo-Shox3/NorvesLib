#pragma once

#include "Component/Component.h"
#include "Delegate/MulticastDelegate.h"
#include "Gameplay/Combat/CombatHitEvent.h"
#include "Gameplay/Combat/CombatJoint.h"
#include "Gameplay/Combat/CombatShape.h"
#include "Math/Transform.h"
#include "Text/IdentityPool.h"
#include "Thread/Thread.h"

namespace NorvesLib::Modules::Gameplay::Combat
{
    class CombatRuntime;
    struct CombatJointBindingState;

    enum class EHurtboxFlags : uint32_t
    {
        None = 0,
        Weak = 1u << 0,
        Anchorable = 1u << 1
    };

    constexpr EHurtboxFlags operator|(EHurtboxFlags left, EHurtboxFlags right)
    {
        return static_cast<EHurtboxFlags>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
    }

    struct HurtboxPart
    {
        uint32_t PartId = 0;
        // AddPart に無効ハンドルを渡すと持ち主へ束縛する。
        Core::EntityHandle Binding;
        CombatShape Shape;
        // 空なら Binding の Entity の姿勢を使い、空でなければ同 Entity の SkinnedMeshComponent の関節に追従する。
        Core::Identity JointName;
        // 当たりのエフェクトの選択に使う表面の名前。
        Core::Identity SurfaceName;
        uint32_t Faction = ~uint32_t{0};
        float DamageScale = 1.0f;
        EHurtboxFlags Flags = EHurtboxFlags::None;
        int32_t Priority = 0;
        // 以下はランタイムが管理する前フレームの姿勢と束縛方式。
        Math::Transform PreviousTransform;
        bool bPreviousValid = false;
        bool bOwnerBinding = true;
    };

    /// 複数の被弾部位。ActorId の既定は持ち主の ObjectId。0 なら明示設定が必要。
    /// 初期化時に登録し、World 参加待ちの場合は登録可能になった時点で登録される。
    class HurtboxSetComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(HurtboxSetComponent, Core::Component::Component)

    public:
        static constexpr uint32_t InvalidPartIndex = ~uint32_t{0};
        HurtboxSetComponent();
        ~HurtboxSetComponent() override;
        void Initialize() override;
        void Finalize() override;
        void BeginPlay() override;
        void EndPlay() override;

        /// 末尾へ追加して部位番号を返す。別 World の束縛先・非所有スレッドなら InvalidPartIndex。
        uint32_t AddPart(const HurtboxPart& part);
        const Core::Container::VariableArray<HurtboxPart>& GetParts() const { return m_Parts; }
        void SetActorId(uint64_t actorId) { m_ActorId = actorId; m_bExplicitActorId = true; }
        uint64_t GetActorId() const;
        uint64_t GetRegistrationIndex() const { return m_RegistrationIndex; }
        void SetWeakJointThreshold(const CombatJointInfluenceThreshold& threshold);
        const CombatJointInfluenceThreshold& GetWeakJointThreshold() const { return m_WeakJointThreshold; }

        Core::MulticastDelegate<const CombatHitEvent&> OnHurt;

    private:
        friend class CombatRuntime;
        bool IsOwnerThread() const;
        bool TryRegister();
        void Unregister();

        Core::Container::VariableArray<HurtboxPart> m_Parts;
        Core::Container::VariableArray<Core::Container::TSharedPtr<CombatJointBindingState>> m_JointBindings;
        CombatJointInfluenceThreshold m_WeakJointThreshold;
        uint64_t m_ActorId = 0;
        bool m_bExplicitActorId = false;
        CombatRuntime* m_Runtime = nullptr;
        uint64_t m_RegistrationIndex = 0;
        Thread::Thread::ThreadId m_OwnerThreadId;
        bool m_bCombatReady = false;
    };
} // namespace NorvesLib::Modules::Gameplay::Combat
