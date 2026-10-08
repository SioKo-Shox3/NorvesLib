#pragma once
#include "Animation/AnimGraphResource.h"
#include "Animation/AnimationEvents.h"
#include "Animation/ClipPlayback.h"
#include "Animation/RootMotion.h"
#include "Animation/SkeletalPoseBuilder.h"

namespace NorvesLib::Core::Animation
{
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
        AnimGraphInstance() = default;
        ~AnimGraphInstance();
        AnimGraphInstance(const AnimGraphInstance&) = delete;
        AnimGraphInstance& operator=(const AnimGraphInstance&) = delete;
        AnimGraphInstance(AnimGraphInstance&&);
        AnimGraphInstance& operator=(AnimGraphInstance&&);
        [[nodiscard]] bool Initialize(const AnimGraphResource&, const SkeletonResource&, const SkinnedMeshResource&,
                                      const Math::Matrix4x4&);
        // Mだけが変わったときに再準備し、再生時刻・パラメータを保持する。
        [[nodiscard]] bool SetMeshTransform(const Math::Matrix4x4&);
        AnimationEventQueue& Events()
        {
            return m_Events;
        }
        const AnimationEventQueue& Events() const
        {
            return m_Events;
        }
        const RootMotionDelta& GetLastRootMotionDelta() const { return m_LastRootMotionDelta; }
        RootMotionDelta ConsumeRootMotion()
        {
            auto value = m_PendingRootMotion;
            m_PendingRootMotion = {};
            return value;
        }
        [[nodiscard]] float GetNominalSpeed(uint32_t clip) const;
        void Reset();
        Container::Span<const float> GetSyncPhases() const
        {
            return m_SyncPhases;
        }
        uint32_t FindSyncGroup(Identity) const;

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
        void Swap(AnimGraphInstance&);
        struct SyncClipRuntime
        {
            AnimSyncMap Map;
            uint64_t Revision = UINT64_MAX;
        };
        struct SyncGroupRuntime
        {
            double Phase = 0;
            uint32_t Leader = InvalidAnimNode;
            float StrideRate = 1;
            bool bInitialized = false;
        };
        Container::VariableArray<SyncClipRuntime> m_SyncClips;
        Container::VariableArray<SyncGroupRuntime> m_SyncGroups;
        Container::VariableArray<float> m_SyncPhases;
        Container::VariableArray<double> m_SyncOffsets;
        bool RefreshSyncMaps();
        bool AdvanceSyncGroups(float dt);
        void SeedSyncNode(uint32_t);

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
                                           AnimInterrupt, Identity sourceMarker = {}, Identity targetMarker = {},
                                           uint32_t sourceState = InvalidAnimNode);
        bool Matches(const AnimTransition&, uint32_t node, uint32_t state) const;
        void ResetSubgraph(uint32_t);
        struct RootYawKey
        {
            float Time = 0;
            double Raw = 0, Unwrapped = 0;
        };
        struct RootClipRuntime
        {
            uint32_t Joint = UINT32_MAX;
            uint64_t MetadataRevision = UINT64_MAX;
            JointTransform Reference;
            RootMotionDelta ReferenceModel;
            bool bCurve = false;
            float NominalSpeed = 0;
            Container::VariableArray<RootYawKey> YawKeys;
        };
        struct NodeMotion
        {
            RootMotionDelta Extracted, Available;
        };
        bool RefreshRootMetadata(bool force = false);
        bool RawRootAt(uint32_t clip, double time, RootMotionDelta&);
        bool RootAt(uint32_t clip, double time, RootMotionDelta&);
        bool RootAtUnwrapped(uint32_t clip, double time, bool loop, RootMotionDelta&);
        bool AdvanceRootMotion();
        bool RemoveRootMotion(uint32_t clip, LocalPose&) const;
        Container::VariableArray<RootClipRuntime> m_RootClips;
        Container::VariableArray<NodeMotion> m_NodeMotion;
        PoseScratch m_MotionScratch;
        Container::VariableArray<Math::Matrix4x4> m_MotionModels;
        RootMotionDelta m_PendingRootMotion, m_LastRootMotionDelta;
        Container::TSharedPtr<const AnimGraphData> m_Graph;
        AnimParamSet m_Parameters;
        AnimationEventQueue m_Events;
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
