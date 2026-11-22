// 動く障害物の足跡を格子に重ね、静的な地形を保ったままセル単位で押印・解除する。
#pragma once

#include "AI/Navigation/NavGrid.h"

namespace NorvesLib::Modules::AI
{
    enum class ENavObstacleShape : uint8_t
    {
        Circle,
        Capsule,
        Obb
    };

    enum class ENavObstacleMode : uint8_t
    {
        Block,
        Cost
    };

    /// ワールド座標の足跡。カプセルの線分は XZ 平面の局所 X 軸に沿う。
    struct NavDynamicFootprint
    {
        ENavObstacleShape Shape = ENavObstacleShape::Circle;
        Math::Vector3 Center;
        float AxisX = 1.0f;
        float AxisZ = 0.0f;
        float Radius = 0.0f;
        float HalfLength = 0.0f;
        float HalfExtentX = 0.0f;
        float HalfExtentZ = 0.0f;
        float MinY = 0.0f;
        float MaxY = 0.0f;
    };

    /// 押印中は記録の複製・書き換えを避け、同じオーバーレイで解除してから次の足跡に使う。
    struct NavDynamicStamp
    {
        Core::Container::VariableArray<NavCellCoord> Cells;
        ENavObstacleMode Mode = ENavObstacleMode::Block;
        uint32_t CostExtraPer128 = 0;
        bool bApplied = false;
        uint64_t OverlayGeneration = 0; // 全消去より前の記録を解除するときの照合用。
    };

    uint32_t NavCostMultiplierToExtraPer128(float multiplier);
    void RasterizeNavDynamicFootprint(const NavGrid& grid, const NavDynamicFootprint& footprint,
                                      float heightBand, Core::Container::VariableArray<NavCellCoord>& outCells);

    /// GameThread が所有する動的障害物の集計。解除後もタイルの版を保持する。
    class NavDynamicOverlay
    {
    public:
        /// 通し番号を保ったまま格子設定を取り替え、既存の押印を消去する。
        void Reset(const NavGridDesc& desc);
        /// 通し番号を保ったまま全タイルを捨て、既存の押印記録を無効にする。
        void Clear();

        /// セルの重複をまとめて押印する。カウンタの上限を超える場合は押印全体を取り消す。
        void Apply(NavDynamicStamp& stamp);
        void Revoke(NavDynamicStamp& stamp);

        bool IsBlocked(NavCellCoord cell) const;
        uint32_t GetCostExtraPer128(NavCellCoord cell) const;
        uint32_t GetTileVersion(NavTileCoord coord) const;
        uint32_t GetSerial() const { return m_Serial; }
        uint64_t GetGeneration() const { return m_Generation; } // 全消去ごとに進む世代。押印の記録の OverlayGeneration と照合する。
        size_t CountNonZeroCells() const { return m_NonZeroCells; }
        bool IsEmpty() const { return m_NonZeroCells == 0; }

    private:
        struct CellCounters
        {
            uint32_t Blocks = 0;
            uint32_t Cost = 0;
            bool IsZero() const { return Blocks == 0 && Cost == 0; }
        };

        struct TileEntry
        {
            NavTileCoord Coord;
            Core::Container::VariableArray<CellCounters> Cells;
            uint32_t Version = 0;
        };

        size_t FindTileIndex(NavTileCoord coord) const;
        const CellCounters* FindCounters(NavCellCoord cell) const;
        uint32_t AdvanceSerial();

        NavGrid m_Coordinates; // 格子設定と座標変換だけを使い、静的タイルは所有しない。
        Core::Container::VariableArray<TileEntry> m_Tiles;
        size_t m_NonZeroCells = 0;
        uint32_t m_Serial = 0;
        uint64_t m_Generation = 1;
    };
}
