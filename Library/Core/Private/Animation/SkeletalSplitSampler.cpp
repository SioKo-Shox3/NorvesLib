#include "Animation/SkeletalSplitSampler.h"
#include "Animation/SkeletonResource.h"
#include "Resource/SkinnedMeshResource.h"
namespace NorvesLib::Core::Animation::Detail
{
    bool SampleSplitV1(const SkeletonResource& skeleton, const AnimationClipResource& clip,
        const SkinnedMeshResource& mesh, float time, const Math::Matrix4x4& transform, SkeletalPoseSnapshot& out)
    {
        out.Clear();
        return skeleton.IsSplitV1() && mesh.IsSplitV1() &&
            SkeletalAnimationSampler::Sample(skeleton, clip, mesh, time, transform, out);
    }
}
