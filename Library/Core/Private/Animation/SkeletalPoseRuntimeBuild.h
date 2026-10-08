#pragma once
#include "Animation/SkeletalPoseRuntime.h"
#include "Animation/SkeletalRestPose.h"
#include "Container/Span.h"
namespace NorvesLib::Core::Skeletal
{
    struct SkeletonV1Data;
}
namespace NorvesLib::Core::Animation::Detail
{
    void BuildLegacySkeletonPoseRuntime(Container::Span<const Skeletal::SkeletalJoint> joints,
                                        Container::Span<const Skeletal::SkeletalRestTransform> rest,
                                        SkeletonPoseRuntime& out);
    void BuildSplitSkeletonPoseRuntime(const Skeletal::SkeletonV1Data& skeleton, SkeletonPoseRuntime& out);
    void BuildClipPoseRuntime(const Skeletal::SkeletalAnimationClip& clip, ClipPoseRuntime& out);
} // namespace NorvesLib::Core::Animation::Detail
