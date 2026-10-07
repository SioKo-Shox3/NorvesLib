#pragma once
#include "Animation/PoseTypes.h"
#include "Animation/SkeletalPoseBounds.h"
#include "Animation/SkeletalAnimationSampler.h"
#include <cstdint>

namespace NorvesLib::Core::Animation
{
    class SkeletalPoseBuilder;
    // 準備した資産はこのcontextの使用期間中、呼び出し側が生存させる。
    // 値配列はcontextが所有し、Resourceのsetter後はPrepareし直す。
    class SkeletalPoseContext
    {
        friend class SkeletalPoseBuilder;
        const SkeletonResource* Skeleton = nullptr;
        const AnimationClipResource* Clip = nullptr;
        const SkinnedMeshResource* Mesh = nullptr;
        uint64_t SkeletonRevision = 0, ClipRevision = 0, MeshRevision = 0;
        Math::Matrix4x4 MeshTransform = Math::Matrix4x4::Identity;
        Math::Matrix4x4 InverseMesh = Math::Matrix4x4::Identity;
        Math::Matrix4x4 RootFrame = Math::Matrix4x4::Identity;
        Container::VariableArray<int32_t> Parents;
        Container::VariableArray<uint32_t> Order;
        Container::VariableArray<Math::Matrix4x4> InverseBind;
        LocalPose DefaultLocal;
        Container::VariableArray<JointBindBounds> JointBounds;
        Math::AABB FallbackBounds = Math::AABB::CreateInvalid();
        PoseBoundsSettings BoundsSettings;
        float MaximumWeightSum = 1;
        bool bHasVertices = false, bNeedsExactBounds = false;
        bool bSplit = false;
        bool bValid = false;
    };

    class SkeletalPoseBuilder final
    {
      public:
        [[nodiscard]] static bool Prepare(const SkeletonResource&, const AnimationClipResource&,
                                          const SkinnedMeshResource&, const Math::Matrix4x4& meshTransform,
                                          SkeletalPoseContext&, const PoseBoundsSettings& = {});
        [[nodiscard]] static bool IsPreparedFor(const SkeletalPoseContext&, const SkeletonResource&,
                                                const AnimationClipResource&, const SkinnedMeshResource&,
                                                const Math::Matrix4x4& meshTransform);
        [[nodiscard]] static bool SampleClipToLocalPose(const SkeletalPoseContext&, const AnimationClipResource&,
                                                        float timeSeconds, LocalPose& out);
        // 後段modifier用の遅延FK。palette・頂点境界を計算しない。
        [[nodiscard]] static bool BuildJointModelMatrices(const SkeletalPoseContext&,const LocalPose&,PoseScratch&,
            Container::VariableArray<Math::Matrix4x4>&);
        [[nodiscard]] static bool BuildPose(const SkeletalPoseContext&, const LocalPose&, PoseScratch&,
                                            SkeletalPoseSnapshot& out);
        [[nodiscard]] static bool Sample(const SkeletalPoseContext&, const AnimationClipResource&, float timeSeconds,
                                         PoseScratch&, SkeletalPoseSnapshot& out);
      private:
        static bool BuildGlobalPose(const SkeletalPoseContext&,const LocalPose&,PoseScratch&);
    };
} // namespace NorvesLib::Core::Animation
