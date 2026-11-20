#pragma once

#include "AI/Navigation/NavTypes.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Modules::AI
{
    /// タイル内のセルの地形と連結成分を属性ごとの配列に保持する。
    struct NavTile
    {
        NavTileCoord Coord;
        uint32_t TileCells = 0;                          // タイルの一辺のセル数
        Core::Container::VariableArray<float> Height;     // セル中心の地面の高さ（m）
        Core::Container::VariableArray<uint8_t> Flags;    // ENavCellFlags のビット
        Core::Container::VariableArray<uint8_t> SlopeQ;   // 0～90 度の傾斜を 0～255 に量子化した値
        Core::Container::VariableArray<uint8_t> ClearanceQ; // セル幅の 1/8 を単位とするクリアランス
        Core::Container::VariableArray<uint8_t> GrassQ;   // 草の密度（0～255）
        // 角を切らない 8 近傍では到達範囲が 4 連結と一致するため、タイル内の成分は 4 連結で数える。
        // 通れないセルの番号は 0、各連結成分の番号は 1～RegionCount。
        Core::Container::VariableArray<uint16_t> Region;
        uint16_t RegionCount = 0;

        /// 座標と一辺のセル数を設定し、全セルの属性と連結成分数を 0 に戻す。
        void Reset(NavTileCoord coord, uint32_t tileCells)
        {
            Coord = coord;
            TileCells = tileCells;
            const size_t cellCount = static_cast<size_t>(tileCells) * tileCells;
            Height.assign(cellCount, 0.0f);
            Flags.assign(cellCount, 0);
            SlopeQ.assign(cellCount, 0);
            ClearanceQ.assign(cellCount, 0);
            GrassQ.assign(cellCount, 0);
            Region.assign(cellCount, 0);
            RegionCount = 0;
        }

        /// タイル内のセル数を返す。
        uint32_t GetCellCount() const
        {
            return TileCells * TileCells;
        }

        /// タイル内の座標を、X が連続する行優先の添字に変換する。
        uint32_t LocalIndex(uint32_t localX, uint32_t localZ) const
        {
            return localZ * TileCells + localX;
        }

        /// セル数が 32 ビットに収まり、全配列の長さがセル数に等しく、成分番号が RegionCount 以下かを調べる。
        bool IsConsistent() const
        {
            const uint64_t cellCount = static_cast<uint64_t>(TileCells) * TileCells;
            if (cellCount > 0xFFFFFFFFu || Height.size() != cellCount || Flags.size() != cellCount ||
                SlopeQ.size() != cellCount || ClearanceQ.size() != cellCount || GrassQ.size() != cellCount ||
                Region.size() != cellCount)
            {
                return false;
            }
            for (uint16_t region : Region)
            {
                if (region > RegionCount)
                {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace NorvesLib::Modules::AI
