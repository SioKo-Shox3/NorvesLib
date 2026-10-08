#pragma once
// v1 splitはcurrent restとper-mesh IBMを別々の所有者から読む。
#include "Animation/SkeletalAnimationSampler.h"
namespace NorvesLib::Core::Animation::Detail
{
    [[nodiscard]] bool SampleSplitV1(const SkeletonResource&, const AnimationClipResource&, const SkinnedMeshResource&,
                                     float timeSeconds, const Math::Matrix4x4& meshNodeGlobalRow,
                                     SkeletalPoseSnapshot& out);
}
