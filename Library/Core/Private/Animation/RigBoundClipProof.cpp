#include "Animation/RigBoundClipProof.h"
namespace NorvesLib::Core::Skeletal
{
    bool RigBoundClipAccess::SetValidated(AnimationClipResource& resource, const CookedRigSplitCpuAsset& cpu,
                                          size_t index)
    {
        const auto* d = cpu.GetData();
        if (resource.IsLoaded() || !d || index >= d->Clips.size() || !d->Skeleton.GetData() ||
            !IsStaticRootFrameProfile(d->Skeleton.GetData()->Profile) ||
            d->BindingReport.Status != RigV1Status::Success || d->BindingReport.Banks.empty())
        {
            return false;
        }
        for (const auto& bank : d->BindingReport.Banks)
        {
            if (bank.Status != RigV1Status::Success || !bank.bFrameComparisonComplete || !bank.bComparisonComplete)
            {
                return false;
            }
        }
        auto proof = Container::MakeShared<RigBoundClipProof>();
        proof->m_Target = d->Skeleton;
        resource.SetClip(SkeletalAnimationClip(d->Clips[index]));
        resource.m_BoundRigProof = std::move(proof);
        return true;
    }
    bool RigBoundClipAccess::Matches(const AnimationClipResource& resource, const SkeletonV1Data* target) noexcept
    {
        return target && IsStaticRootFrameProfile(target->Profile) && resource.m_BoundRigProof &&
               resource.m_BoundRigProof->m_Target.GetData() == target;
    }
    size_t RigBoundClipAccess::MemorySize(const AnimationClipResource& resource) noexcept
    {
        // immutable target本文はSkeletonResource側の会計と二重に加算しない。
        return resource.m_BoundRigProof ? sizeof(RigBoundClipProof) : 0;
    }
} // namespace NorvesLib::Core::Skeletal
