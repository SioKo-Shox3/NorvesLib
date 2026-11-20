#pragma once

#include "AI/Navigation/NavTile.h"
#include "Container/PointerTypes.h"
#include "Math/Vector3.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Modules::AI
{
    /// セルを格納するタイルと、そのタイル内での添字。
    struct NavCellRef
    {
        const NavTile* Tile = nullptr;
        uint32_t Index = 0;

        /// 参照先のタイルがあれば真を返す。
        explicit operator bool() const
        {
            return Tile != nullptr;
        }
    };

    /// タイルとその連結成分を管理する。スレッド安全ではないため GameThread で使う。
    /// タイルの置き換えや解放でそのタイルへの参照が無効になり、Clear で全タイルへの参照が無効になる。
    class NavGrid
    {
    public:
        /// 指定した格子設定で空の格子を作る。
        explicit NavGrid(const NavGridDesc& desc = {});

        /// 現在の格子設定を返す。
        const NavGridDesc& GetDesc() const;

        /// タイルが 0 枚で設定が有効なときだけ更新する。
        bool SetDesc(const NavGridDesc& desc);

        /// タイルを検証して読み込み、同じ座標のタイルがあれば置き換える。
        /// 読み込みごとに新しい世代番号を振り、番号を使い切ったら InvalidTile を返す。
        ENavTileResult LoadTile(NavTile&& tile);

        /// 指定したタイルを解放し、該当するタイルがなければ false を返す。
        bool UnloadTile(NavTileCoord coord);

        /// 全タイルを解放して格子の版を進める。
        void Clear();

        /// 指定した座標のタイルを返す。未読み込みなら nullptr。
        const NavTile* FindTile(NavTileCoord coord) const;

        /// 指定したタイルの世代番号を返す。未読み込みなら 0。
        uint32_t GetTileGeneration(NavTileCoord coord) const;

        /// 読み込みや解放の成功時と Clear の呼び出しごとに増える格子の版を返す。
        uint64_t GetVersion() const;

        /// 読み込み済みのタイル数を返す。
        uint32_t GetLoadedTileCount() const;

        /// Z、X の昇順で並ぶタイルを添字で返す。範囲外なら nullptr。
        const NavTile* GetLoadedTile(uint32_t index) const;

        /// ワールド座標をセル単位で切り下げる。各軸の NaN は 0、範囲外の値は整数の上下限に収める。
        NavCellCoord WorldToCell(float x, float z) const;

        /// セル中心の XZ 座標と地面の高さを返す。未読み込みなら高さは 0。
        Math::Vector3 GetCellCenter(NavCellCoord cell) const;

        /// 負の座標も負の無限大側に切り下げて、セルが属するタイルの座標を求める。
        NavTileCoord CellToTile(NavCellCoord cell) const;

        /// 格子上のセル座標を、タイル内で X が連続する添字に変換する。
        uint32_t CellToLocalIndex(NavCellCoord cell) const;

        /// 指定したセルの参照を返す。タイルが未読み込みなら空の参照を返す。
        NavCellRef FindCell(NavCellCoord cell) const;

        /// タイルをまたぐ連結成分の番号を返す。未読み込みのセルや通れないセルは 0。
        uint32_t GetConnectivityRegion(NavCellCoord cell) const;

    private:
        /// 所有するタイルと世代番号、タイル内の成分番号から格子全体の成分番号への対応表。
        struct TileEntry
        {
            Core::Container::TUniquePtr<NavTile> Tile;
            uint32_t Generation = 0;
            Core::Container::VariableArray<uint32_t> Connectivity;
        };

        /// 二分探索で指定座標のタイルの挿入位置を求める。
        size_t FindTileIndex(NavTileCoord coord) const;

        /// タイル境界で隣り合う成分を結び、格子全体の連結成分に番号を振り直す。
        void RebuildConnectivity();

        NavGridDesc m_Desc;
        Core::Container::VariableArray<TileEntry> m_Tiles;
        uint32_t m_LastTileGeneration = 0;
        uint64_t m_Version = 0;
        mutable size_t m_CachedTileIndex = static_cast<size_t>(-1);
    };
} // namespace NorvesLib::Modules::AI
