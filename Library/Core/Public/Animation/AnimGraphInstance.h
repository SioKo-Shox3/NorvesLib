#pragma once
#include "Animation/AnimGraphResource.h"
#include "Animation/SkeletalPoseBuilder.h"

namespace NorvesLib::Core::Animation
{
    // 折返し前の秒を渡す。GR11はUpdate-onlyでもこの列からイベントとroot deltaを作れる。
    struct AnimClipTraversal
    {
        uint32_t Node = InvalidAnimNode;
        uint32_t Clip = InvalidAnimNode;
        double Previous = 0;
        double Current = 0;
        float Weight = 0;
        bool bLoop = true;
        Identity SyncGroup;
    };
    struct AnimStateStatus
    {
        Identity Current;
        Identity Next;
        double NormalizedTime = 0;
        float Transition = 0;
        bool bTransitioning = false;
        bool bReachedEnd = false;
    };
    // Initializeに渡す骨格・meshは使用中、呼出側が生存を保証する。Componentは強参照で保持する。
    class AnimGraphInstance
    {
      public:
        [[nodiscard]] bool Initialize(const AnimGraphResource&, const SkeletonResource&, const SkinnedMeshResource&,
                                      const Math::Matrix4x4&);
        // Mだけが変わったときに再準備し、再生時刻・パラメータを保持する。
        [[nodiscard]] bool SetMeshTransform(const Math::Matrix4x4&);
        void Reset();
        AnimParamSet& Parameters()
        {
            return m_Parameters;
        }
        const AnimParamSet& Parameters() const
        {
            return m_Parameters;
        }
        [[nodiscard]] bool Update(float dt);
        [[nodiscard]] bool Evaluate();
        [[nodiscard]] bool RequestState(Identity machine, Identity state, float blendSeconds);
        [[nodiscard]] bool GetState(Identity machine, AnimStateStatus&) const;
        const LocalPose& GetLocalPose() const;
        const Container::VariableArray<AnimClipTraversal>& GetTraversals() const
        {
            return m_Traversals;
        }
        const Container::VariableArray<float>& GetNodeWeights() const
        {
            return m_Weights;
        }
        const Container::TSharedPtr<const AnimGraphData>& GetGraph() const
        {
            return m_Graph;
        }
        // modifier後の姿勢も同じ準備済みcontextでpaletteへ変換する。
        [[nodiscard]] bool BuildJointModelMatrices(const LocalPose&, PoseScratch&,
                                                   Container::VariableArray<Math::Matrix4x4>&) const;
        [[nodiscard]] bool BuildPose(const LocalPose&, PoseScratch&, SkeletalPoseSnapshot&) const;

      private:
        struct NodeRuntime
        {
            double Time = 0;
            double StateTime = 0, NextStateTime = 0;
            uint32_t Current = 0, Next = InvalidAnimNode;
            float Elapsed = 0, Duration = 0;
            AnimTransitionCurve Curve = AnimTransitionCurve::Linear;
            AnimInterrupt Interrupt = AnimInterrupt::None;
            bool bSnapshot = false;
            Container::VariableArray<float> Edges;
            LocalPose Saved, SavedReference;
        };
        [[nodiscard]] bool ResourcesCurrent() const;
        void Edges(uint32_t);
        [[nodiscard]] bool EvaluateNodes(bool reference, uint32_t requiredRoot = InvalidAnimNode);
        [[nodiscard]] bool StartTransition(uint32_t, uint32_t target, float duration, AnimTransitionCurve,
                                           AnimInterrupt);
        bool Matches(const AnimTransition&, uint32_t node, uint32_t state) const;
        void ResetSubgraph(uint32_t);
        Container::TSharedPtr<const AnimGraphData> m_Graph;
        AnimParamSet m_Parameters;
        Container::VariableArray<SkeletalPoseContext> m_Contexts;
        Container::VariableArray<NodeRuntime> m_Runtime;
        Container::VariableArray<LocalPose> m_Poses, m_Reference;
        Container::VariableArray<float> m_Weights, m_EvaluationWeights;
        Container::VariableArray<double> m_Durations;
        Container::VariableArray<AnimClipTraversal> m_Traversals;
        Container::VariableArray<uint8_t> m_ResetVisited;
        LocalPose m_Output;
        const SkeletonResource* m_Skeleton = nullptr;
        const SkinnedMeshResource* m_Mesh = nullptr;
        Math::Matrix4x4 m_Transform = Math::Matrix4x4::Identity;
        bool m_bValid = false;
    };
} // namespace NorvesLib::Core::Animation
