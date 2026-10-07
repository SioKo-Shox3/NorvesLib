#pragma once
// profile2だけの束縛証明。検証済みCPU候補以外から発行せず、setterで失効させる。
#include "Animation/RigSplitBinding.h"
namespace NorvesLib::Core::Skeletal
{
    class RigBoundClipProof
    {
      public:
        RigBoundClipProof() = default;

      private:
        friend class RigBoundClipAccess;
        SkeletonV1 m_Target;
    };
    class RigBoundClipAccess
    {
      public:
        [[nodiscard]] static bool SetValidated(AnimationClipResource&, const CookedRigSplitCpuAsset&, size_t clipIndex);
        [[nodiscard]] static bool Matches(const AnimationClipResource&, const SkeletonV1Data*) noexcept;
        [[nodiscard]] static size_t MemorySize(const AnimationClipResource&) noexcept;
    };
} // namespace NorvesLib::Core::Skeletal
