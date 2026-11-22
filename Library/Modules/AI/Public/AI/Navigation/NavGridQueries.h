#pragma once

#include "AI/Navigation/NavGrid.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Modules::AI
{
    class NavDynamicOverlay;

    /// 視線が止まったセルと、線分上の位置。
    struct NavRaycastResult
    {
        bool bHit = false;
        float Fraction = 1.0f; // 始点を 0、終点を 1 とした進行割合
        NavCellCoord HitCell;
        Math::Vector3 HitPosition;
    };

    namespace NavGridQueries
    {
        /// 読み込み済みで、地形とクリアランス、動的な押印が通行を許すセルかを返す。
        bool IsCellPassable(const NavGrid& grid, NavCellCoord cell, uint16_t thresholdQ,
                            const NavDynamicOverlay* overlay = nullptr);

        /// 指定した位置のセルを、半径 radius のエージェントが通れるかを返す。
        bool IsWalkable(const NavGrid& grid, const Math::Vector3& position, float radius,
                        const NavDynamicOverlay* overlay = nullptr);

        /// searchRadius 内で XZ 距離が最小の通行可能なセル中心を地面の高さで返す。
        /// 同距離なら Z、X の昇順で選ぶ。候補がなければ false を返す。
        bool FindNearestWalkable(const NavGrid& grid, const Math::Vector3& position, float radius,
                                 float searchRadius, Math::Vector3& outPosition, const NavDynamicOverlay* overlay = nullptr);

        /// XZ 線分が触れる全セルをたどる。角や格子線では両側を調べ、最初の当たりで止まる。
        /// 始点セルでは動的な塞ぎだけを無視し、押印の中からの移動を許す。
        /// 非有限入力や表現できない半径は始点で、走査上限に達した場合は最初の未確認セルで当たりとする。
        NavRaycastResult Raycast(const NavGrid& grid, const Math::Vector3& from,
                                 const Math::Vector3& to, float radius, const NavDynamicOverlay* overlay = nullptr);

        /// Raycast と同じ走査をし、通行を調べたセル（当たりのセルを含む）が属するタイルの座標を outTestedTiles に足す。
        /// 既に outTestedTiles にある座標は足さない。
        NavRaycastResult Raycast(const NavGrid& grid, const Math::Vector3& from, const Math::Vector3& to, float radius,
                                 Core::Container::VariableArray<NavTileCoord>& outTestedTiles, const NavDynamicOverlay* overlay = nullptr);

        /// 始点セルを除き、当たりまでに通れた各セルの動的な割増を一度ずつ足して outCostExtraPer128 に返す。
        NavRaycastResult Raycast(const NavGrid& grid, const Math::Vector3& from, const Math::Vector3& to, float radius,
                                 const NavDynamicOverlay& overlay, uint64_t& outCostExtraPer128);

        /// この位置のセルに直進で入るコストを返す。通れなければ kNavCostImpassable。
        uint32_t SampleCost(const NavGrid& grid, const Math::Vector3& position, const NavAgentProfile& profile,
                            const NavDynamicOverlay* overlay = nullptr);
    } // namespace NavGridQueries
} // namespace NorvesLib::Modules::AI
