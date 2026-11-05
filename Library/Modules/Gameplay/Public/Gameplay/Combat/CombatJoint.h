// 戦闘判定で関節への束縛を警告する閾値を定義する。
#pragma once

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Combat
{
    /// 関節への束縛を警告する閾値。どちらかの値以下の関節を「影響の弱い関節」とみなす。両方 0 なら検査しない。
    struct CombatJointInfluenceThreshold
    {
        uint32_t MinVertexCount = 0;   // 影響する頂点の数がこの値以下なら弱い
        float MinWeightSum = 0.0f;     // 重みの総和がこの値以下なら弱い
    };
} // namespace NorvesLib::Modules::Gameplay::Combat
