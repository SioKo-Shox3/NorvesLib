#pragma once
#include "Animation/SkeletalAnimationSampler.h"
namespace NorvesLib::Core::Animation::LegacyPoseOracle
{
    bool Sample(const SkeletonResource&, const AnimationClipResource&, const SkinnedMeshResource&, float,
                const Math::Matrix4x4&, SkeletalPoseSnapshot&);
    bool SampleSplitV1(const SkeletonResource&, const AnimationClipResource&, const SkinnedMeshResource&, float,
                       const Math::Matrix4x4&, SkeletalPoseSnapshot&);
    SkinnedVertexSample SkinVertex(const Skeletal::SkeletalVertex&, const Container::VariableArray<Math::Matrix4x4>&);
} // namespace NorvesLib::Core::Animation::LegacyPoseOracle
