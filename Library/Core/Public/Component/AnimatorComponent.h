#pragma once
#include "Component/Component.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/PoseModifier.h"
#include "Animation/Procedural/PoseModifierConfig.h"
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
    // 最新のUpdate一回分。累積Consumeと消費権を共有し、二重適用しない。
    struct AnimatorRootMotionFrame
    {
        Animation::RootMotionDelta Delta;
        uint64_t SourceGeneration = 1, UpdateSerial = 0;
        float UpdateSeconds = 0;
        bool bValid = false, bConsumed = true;
    };
    class AnimatorComponent : public Component
    {
        REFLECTION_CLASS(AnimatorComponent, Component)
      public:
        AnimatorComponent();
        explicit AnimatorComponent(const FieldInitializer*);
        explicit AnimatorComponent(const IUnknown*);
        ~AnimatorComponent() override;
        void Initialize() override;
        void Finalize() override;
        void EndPlay() override;
        /// Animation の群の本体（World の順序付きの系が呼ぶ）。グラフを dt だけ進める
        void TickAnimationGroup(float dt);
        /// PoseFinalize の群の本体。姿勢を評価してから、溜まったイベントを GameThread で配る
        void TickPoseFinalizeGroup();
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
        bool TryGetLastDriveSignals(Animation::AnimDriveSignals& out) const;
        [[nodiscard]] bool RequestState(Identity machine, Identity state, float seconds);
        [[nodiscard]] bool GetState(Identity machine, Animation::AnimStateStatus&) const;
        [[nodiscard]] bool AddModifier(const Container::TSharedPtr<Animation::IPoseModifier>&);
        void ClearModifiers();
        // 成功時は AddModifier 分も含めて全件を置き換え、同じ order は設定の記載順を保つ。
        // 失敗時は現在の modifier 群を保持する。適用中・骨格未設定時は拒否する。
        [[nodiscard]] bool ApplyPoseModifiersJson(
            const Container::String& json, const Animation::Procedural::PoseModifierTypeTable& types,
            Animation::Procedural::PoseModifierConfigReport* report = nullptr);
        [[nodiscard]] bool ApplyPoseModifiersJson(
            const Container::String& json, Animation::Procedural::PoseModifierConfigReport* report = nullptr);
        // 指定パスの実行時サイドカーを読み、JSON と同じ全件置き換えを行う。
        [[nodiscard]] bool ApplyPoseModifiersFile(
            const Container::String& path, const Animation::Procedural::PoseModifierTypeTable& types,
            Animation::Procedural::PoseModifierConfigReport* report = nullptr);
        [[nodiscard]] bool ApplyPoseModifiersFile(
            const Container::String& path, Animation::Procedural::PoseModifierConfigReport* report = nullptr);
        void SetFrozen(bool frozen)
        {
            if (m_bFrozen != frozen)
                InvalidateRootMotionFrame(true);
            m_bFrozen = frozen;
        }
        [[nodiscard]] bool Step(float seconds);
        Animation::RootMotionDelta ConsumeRootMotion()
        {
            m_RootMotionFrame.bConsumed = true;
            return m_Instance.ConsumeRootMotion();
        }
        const AnimatorRootMotionFrame& GetRootMotionFrame() const
        {
            return m_RootMotionFrame;
        }
        // グラフ評価後（modifier 適用前）の姿勢を返し、作業配列の再確保を試験・診断する読み取り専用の口。
        const Animation::LocalPose& GetLocalPose() const { return m_Instance.GetLocalPose(); }
        // modifier 適用後に渡す姿勢を返し、作業配列の再確保を試験・診断する読み取り専用の口。
        const Animation::LocalPose& GetFinalPose() const { return m_FinalPose; }
        // modifier 用の作業領域を返し、作業配列の再確保を試験・診断する読み取り専用の口。
        const Animation::PoseScratch& GetModifierScratch() const { return m_ModifierScratch; }
        // modifier 用のモデル行列を返し、作業配列の再確保を試験・診断する読み取り専用の口。
        const Container::VariableArray<Math::Matrix4x4>& GetModifierModelMatrices() const { return m_ModifierModels; }
        // 取り逃した古い累積分は破棄し、指定した最新Updateだけを取得する。
        bool TryConsumeRootMotionFrame(uint64_t serial, Animation::RootMotionDelta& out);
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
        void InvalidateRootMotionFrame(bool sourceChanged);
        bool HasValidRootMotionSource() const;
        AnimatorRootMotionFrame m_RootMotionFrame;
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
        Animation::AnimDriveSignals m_LastDriveSignals;
        bool m_bHasDriveSignals = false;
        Animation::LocalPose m_FinalPose;
        Animation::PoseScratch m_ModifierScratch;
        Container::VariableArray<Math::Matrix4x4> m_ModifierModels;
        Container::VariableArray<Container::TSharedPtr<Animation::IPoseModifier>> m_Modifiers;
        float m_LastDelta = 0, m_Step = 0;
        bool m_bFrozen = false, m_bEvaluate = true;
        bool m_bApplyingModifiers = false;
    };
} // namespace NorvesLib::Core::Component
