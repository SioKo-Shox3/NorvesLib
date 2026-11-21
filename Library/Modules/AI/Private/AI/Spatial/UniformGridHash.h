#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"

#include <compare>
#include <cstdint>

namespace NorvesLib::Modules::AI::Spatial
{
    /// エージェントの XZ 座標をセルごとにまとめ、円内の近傍を元の添字で返す。
    class UniformGridHash
    {
    public:
        static constexpr float kDefaultCellSize = 8.0f;
        explicit UniformGridHash(float cellSize = kDefaultCellSize);

        /// 有限かつ正の値を次の Build 用に設定する。不正な値なら設定を保ち、false を返す。
        bool SetCellSize(float cellSize);
        float GetCellSize() const;
        /// 確保した容量を保って、点とセルを空にする。
        void Clear();
        /// 両配列に共通する範囲の有限な点から格子を作り直す。共通部分の長さの上限は UINT32_MAX。
        void Build(Core::Container::Span<const float> xs, Core::Container::Span<const float> zs);
        uint32_t GetPointCount() const;

        /// out を空にし、excludeIndex を除く円内の添字を昇順で格納する。既定では全点を対象とする。
        /// 判定式は double dx = double(px) - double(x), dz = double(pz) - double(z);
        /// dx*dx + dz*dz <= double(radius)*double(radius)。中心や半径が非有限、または半径が負なら空。
        void QueryRadius(float x, float z, float radius, Core::Container::VariableArray<uint32_t>& out,
                         uint32_t excludeIndex = UINT32_MAX) const;

    private:
        struct CellKey
        {
            int32_t X;
            int32_t Z;
            auto operator<=>(const CellKey&) const = default;
        };

        struct Entry
        {
            CellKey Cell;
            uint32_t Index;
        };

        struct CellRange
        {
            CellKey Cell;
            uint32_t Begin;
            uint32_t End;
        };

        float m_CellSize = kDefaultCellSize;
        float m_BuiltCellSize = kDefaultCellSize;
        Core::Container::VariableArray<float> m_Xs;
        Core::Container::VariableArray<float> m_Zs;
        Core::Container::VariableArray<Entry> m_Entries;
        Core::Container::VariableArray<CellRange> m_Cells;
    };
}
