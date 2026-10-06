#pragma once
// 作成元rigのアニメ適用前TRS。rotationはglTFの列規約を保持する。
#include "Resource/SkeletalGltfData.h"
namespace NorvesLib::Core::Skeletal
{
    struct SkeletalRestTransform
    {
        SkeletalPosition Translation;
        SkeletalValue Rotation{0, 0, 0, 1};
        SkeletalPosition Scale{1, 1, 1};
    };
    // legacy Samplerのfloat normでIdentity/zeroへ落ちないv1 domain。
    [[nodiscard]] bool IsRepresentableSkeletalRotation(const SkeletalValue& value) noexcept;
    [[nodiscard]] bool IsValidSkeletalRestTransform(const SkeletalRestTransform& value) noexcept;
} // namespace NorvesLib::Core::Skeletal
