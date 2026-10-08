#pragma once
// 作者restの距離式・閾値・全joint診断をB1とsplit bindingで共有する。
#include "Asset/CookedClipBankV1.h"
namespace NorvesLib::Core::Skeletal::Detail
{
    [[nodiscard]] bool CompareClipBankRest(const ClipBankV1Data& source,
                                           Container::Span<const SkeletalRestTransform> targetCanonical,
                                           Container::Span<const uint32_t> targetIndices, double targetScale,
                                           const RigBindingPolicy&, RigV1Report&);
}
