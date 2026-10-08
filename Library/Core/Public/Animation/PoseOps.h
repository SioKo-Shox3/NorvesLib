#pragma once
#include "Animation/PoseTypes.h"
#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Core::Animation
{
    struct BoneMask
    {
        Container::VariableArray<float> Weights;
        // root以下の枝だけを選ぶ。fadeDepth=0は全て1、正なら根から深さに沿って1へ立ち上げる。
        // 名前からroot添字への解決は、骨格に束縛するグラフの読込時に行う。
        [[nodiscard]] bool Build(Container::Span<const int32_t> parents, uint32_t root, uint32_t fadeDepth = 0);
    };

    class PoseOps final
    {
      public:
        // 入力とmaskの要素数・有限値を確認する。失敗時はdstを変更しない。
        // weightは[0,1]。maskが空なら全関節に適用。同じサイズのdstを再確保しない。
        [[nodiscard]] static bool BlendInto(LocalPose& dst, const LocalPose& source, float weight,
                                            Container::Span<const float> mask = {});
        // target * inverse(reference)の回転差を現在姿勢へ左から加える。
        // 平行移動・scaleは差分を加えるため、referenceのscaleが0でも定義できる。
        [[nodiscard]] static bool AddInto(LocalPose& dst, const LocalPose& target, const LocalPose& reference,
                                          float weight, Container::Span<const float> mask = {});
        [[nodiscard]] static bool IsFinite(const JointTransform& transform) noexcept;
        [[nodiscard]] static Math::Quaternion Nlerp(const Math::Quaternion&, const Math::Quaternion&, float weight);
    };
} // namespace NorvesLib::Core::Animation
