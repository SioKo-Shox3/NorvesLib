#pragma once
// 現在の借用骨格を使う一時的な参照。作者時restや永続cacheの契約ではない。
#include "SkeletalRotationRetarget.h"
#include "Math/Matrix4x4.h"
#include "Resource/SkeletalGltfData.h"
namespace NorvesLib::Core::Animation
{
    struct SkeletalRetargetNativeInput
    {
        Container::Span<const SkeletalRetargetSourceRotation> Source;
        Container::Span<const SkeletalJointMappingPair> Mappings;
        Container::Span<const Bvh::Matrix3d> Corrections;
        SkeletalJointMappingRoot Root;
        SkeletalSourceReusePolicy SourceReuse = SkeletalSourceReusePolicy::Unspecified;
        SkeletalRetargetRotationPolicy Policy = SkeletalRetargetRotationPolicy::Unspecified;
    };
    // 正uniform scaleのtarget専用。確保例外/失敗ではout不変。未写像targetの値は出力しない。
    // outと借用入力の重なりは拒否する。入力は呼出し中不変。
    // 成功時のAngularErrorRadiansは全targetの実float world回転と目標との差の最大値。
    [[nodiscard]] SkeletalRetargetResult RetargetSkeletalRotationFrame(
        const SkeletalRetargetNativeInput& input, Container::Span<const Skeletal::SkeletalJoint> joints,
        const Math::Matrix4x4& meshGlobal, Container::Span<SkeletalRetargetRotationValue> out);
} // namespace NorvesLib::Core::Animation
