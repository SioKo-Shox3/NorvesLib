#pragma once

#include "Animation/SkeletalPoseRuntime.h"
#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Core::Animation::Procedural
{
    struct JointRotationShare
    {
        int32_t Joint = -1;
        float Weight = 0;
    };

    // 回転のみを親から合成する。split skeleton の根の親には RootFrame の回転を使い、
    // それ以外は単位回転を使う。メッシュ変換の逆行列は引数にないため、回転を持つ
    // メッシュ変換がある場合は、その変換前の空間とモデル空間の間で呼び出し側が変換する。
    // 親の非一様スケールによる回転への影響は含まず、その場合は行列での結果と厳密には一致しない。
    // 姿勢と親配列の長さの不一致、不正な関節・親添字、親の循環、経路上の非有限または
    // ゼロ長付近の回転、不正な RootFrame は false とし、outGlobal を変更しない。
    [[nodiscard]] bool TryComputeJointGlobalRotation(const LocalPose& pose, const SkeletonPoseRuntime& skeleton,
                                                     int32_t joint, Math::Quaternion& outGlobal);

    // 関節のグローバル回転を newGlobal にする。子孫のローカル回転は変更しない。
    // 関節経路に上記と同じ不正がある場合、または newGlobal が非有限・ゼロ長付近なら、
    // false を返し、姿勢を変更しない。
    [[nodiscard]] bool TrySetJointGlobalRotation(LocalPose& pose, const SkeletonPoseRuntime& skeleton, int32_t joint,
                                                 const Math::Quaternion& newGlobal);

    // モデル空間の回転を根側から先端側の関節へ分配する。重みの和は 1 ± 1e-4 とする。
    // 空のチェーン、不正・重複した添字、先行関節の子孫でない並び、非有限・負の重み、
    // 範囲外の重みの和、非有限・ゼロ長付近の modelRotation、関節経路の不正は拒否する。
    // 拒否すると false を返し、姿勢を一切変更しない。ヒープ確保は行わない。
    [[nodiscard]] bool TryDistributeModelRotation(LocalPose& pose, const SkeletonPoseRuntime& skeleton,
                                                  Container::Span<const JointRotationShare> chain,
                                                  const Math::Quaternion& modelRotation);
} // namespace NorvesLib::Core::Animation::Procedural
