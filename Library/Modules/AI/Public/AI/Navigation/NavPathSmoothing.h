#pragma once

#include "AI/Navigation/NavGrid.h"
#include "Container/Span.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Modules::AI
{
    class NavDynamicOverlay;

    /// A* の隣接セル列を地面の高さのセル中心に間引き、始点と終点を残す。
    /// 成功時の各線分は半径 radius の Raycast を通る。1 セルなら中心を 1 点返す。
    /// 入力が空、または隣接セルにも進めない場合は出力を空にして false を返す。
    bool SmoothNavPath(const NavGrid& grid, Core::Container::Span<const NavCellCoord> cells, float radius,
                       Core::Container::VariableArray<Math::Vector3>& outPoints, const NavDynamicOverlay* overlay = nullptr);
} // namespace NorvesLib::Modules::AI
