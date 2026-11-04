#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector3.h"

namespace NorvesLib::Core::Animation
{
    struct TwoBoneIKInput
    {
        Math::Vector3 Root;          // 根の関節のモデル空間位置
        Math::Vector3 Mid;           // 中間の関節のモデル空間位置
        Math::Vector3 End;           // 端の関節のモデル空間位置
        Math::Vector3 Target;        // 端の目標のモデル空間位置
        Math::Vector3 Pole;          // 曲げたい方向。位置ではなく方向を指定する
        Math::Vector3 FallbackPole;  // Pole が軸と平行な場合に代わりに使う方向
        float MaxStretch = 1.0f;     // 区間に許す伸びの上限。1 未満は 1 として扱う
        float SoftLimitWidth = 0.0f; // 完全伸展の手前で和らげる幅。単位はメートル
    };

    struct TwoBoneIKResult
    {
        Math::Vector3 Mid;          // 新しい中間の関節のモデル空間位置
        Math::Vector3 End;          // 新しい端の関節のモデル空間位置
        Math::Quaternion RootDelta; // 根の区間のグローバル回転の差分
        Math::Quaternion MidDelta;  // 中間の区間のグローバル回転の差分
        Math::Vector3 PoleUsed;     // 実際に使った曲げの単位方向
        float Stretch = 1.0f;       // 各区間に掛けた伸びの倍率
        bool Degenerate = false;    // 退化に対する代替を使った場合は true
    };

    /**
     * @brief モデル空間の位置から二関節 IK の解析解を求める。
     *
     * 差分はグローバル回転の左から掛ける。退化していない区間について、
     * RootDelta * normalize(input.Mid - input.Root) = normalize(result.Mid - input.Root)、
     * MidDelta * normalize(input.End - input.Mid) = normalize(result.End - result.Mid) を満たす。
     * PoleUsed は次回の FallbackPole に渡せる。入力または出力が非有限のとき、
     * 関節位置の非有限成分を 0 に置き換え、単位回転と退化状態を返す。
     */
    [[nodiscard]] TwoBoneIKResult SolveTwoBoneIK(const TwoBoneIKInput& input) noexcept;
} // namespace NorvesLib::Core::Animation
