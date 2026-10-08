#pragma once
#include "Component/Component.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/PoseModifier.h"
#include "Animation/SkeletalAssetResource.h"

namespace NorvesLib::Core::Component
{
    class SkinnedMeshComponent;
    struct AnimatorDebugParam
    {
        Identity Name;
        Animation::AnimParamType Type = Animation::AnimParamType::Float;
        Animation::AnimParamValue Value;
    };
    struct AnimatorDebugNode
    {
        Identity Name;
        float Weight = 0;
        Animation::AnimStateStatus State;
        bool bStateMachine = false;
    };
    struct AnimatorDebugSync
    {
        Identity Name;
        float Phase = 0;
    };
    struct AnimatorDebugSnapshot
    {
        Container::VariableArray<AnimatorDebugParam> Parameters;
        Container::VariableArray<AnimatorDebugNode> Nodes;
        Container::VariableArray<AnimatorDebugSync> SyncGroups;
        bool bFrozen = false;
        bool bReady = false;
    };
    class AnimatorComponent : public Component
    {
        REFLECTION_CLASS(AnimatorComponent, Component)
      public:
        Engine::TimeChannel GetTimeChannel() const noexcept override
        {
            return Engine::TimeChannel::Animation;
        }
        AnimatorComponent();
        explicit AnimatorComponent(const FieldInitializer*);
        explicit AnimatorComponent(const IUnknown*);
        ~AnimatorComponent() override;
        void Initialize() override;
        void Finalize() override;
        void EndPlay() override;
        void OnTickGroup(ETickGroup, float) override;
        // 同じEntityのSkinnedMeshComponentと有効資産が先に必要。失敗時は前のgraphを維持する。
        [[nodiscard]] bool SetGraph(const Container::TSharedPtr<AnimGraphResource>&);
        MulticastDelegate<const Animation::AnimEventInfo&> OnEvent;
        const Container::TSharedPtr<AnimGraphResource>& GetGraph() const
        {
            return m_Graph;
        }
        Animation::AnimParamHandle FindParam(Identity) const;
        [[nodiscard]] bool SetFloat(Animation::AnimParamHandle, float);
        [[nodiscard]] bool SetInt(Animation::AnimParamHandle, int32_t);
        [[nodiscard]] bool GetInt(Animation::AnimParamHandle, int32_t&) const;
        [[nodiscard]] bool SetBool(Animation::AnimParamHandle, bool);
        [[nodiscard]] bool SetTrigger(Animation::AnimParamHandle);
        [[nodiscard]] bool SetDriveSignals(const Animation::AnimDriveSignals&);
        [[nodiscard]] bool RequestState(Identity machine, Identity state, float seconds);
        [[nodiscard]] bool GetState(Identity machine, Animation::AnimStateStatus&) const;
        [[nodiscard]] bool AddModifier(const Container::TSharedPtr<Animation::IPoseModifier>&);
        void ClearModifiers();
        void SetFrozen(bool frozen)
        {
            m_bFrozen = frozen;
        }
        [[nodiscard]] bool Step(float seconds);
        Animation::RootMotionDelta ConsumeRootMotion()
        {
            return m_Instance.ConsumeRootMotion();
        }
        void SetEvaluationEnabled(bool enabled)
        {
            m_bEvaluate = enabled;
        }
        [[nodiscard]] bool UpdateAnimation(float dt);
        [[nodiscard]] bool EvaluateAnimation();
        void BuildDebugSnapshot(AnimatorDebugSnapshot&) const;

      private:
        SkinnedMeshComponent* Mesh() const;
        void Detach();
        void ForwardEvent(const Animation::AnimEventInfo& info)
        {
            // 配送先が別のリスナーを解除しても反復を失効させない。
            const auto listeners = OnEvent;
            listeners.Broadcast(info);
        }
        Container::TSharedPtr<AnimGraphResource> m_Graph;
        Container::TSharedPtr<SkeletalAssetResource> m_BoundAsset;
        Container::TSharedPtr<SkeletonResource> m_BoundSkeleton;
        Container::TSharedPtr<SkinnedMeshResource> m_BoundMesh;
        Animation::AnimGraphInstance m_Instance;
        Animation::LocalPose m_FinalPose;
        Animation::PoseScratch m_ModifierScratch;
        Container::VariableArray<Math::Matrix4x4> m_ModifierModels;
        Container::VariableArray<Container::TSharedPtr<Animation::IPoseModifier>> m_Modifiers;
        float m_LastDelta = 0, m_Step = 0;
        bool m_bFrozen = false, m_bEvaluate = true;
        bool m_bApplyingModifiers = false;
    };
} // namespace NorvesLib::Core::Component
