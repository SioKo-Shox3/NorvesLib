#pragma once

#include "AI/Navigation/NavTypes.h"
#include "Container/PointerTypes.h"
#include "Container/Span.h"

namespace NorvesLib::Modules::AI
{
    class NavGrid;

    /// 格子上の経路とその探索条件。
    struct NavPathQuery
    {
        NavCellCoord Start;
        NavCellCoord Goal;
        NavAgentProfile Profile;
        float Epsilon = 1.2f;        // 1～4 を Q8 に丸めた重み。非有限値は 307/256。
        uint32_t MaxExpansions = 0;  // 探索全体の展開上限。0 は無制限。
    };

    enum class ENavSearchState : uint8_t
    {
        Idle = 0,
        InProgress,
        Succeeded,
        Partial,
        Failed
    };

    /// 整数コストの重み付き A*。乱数・時刻・ポインタ値を順序に使わず、同じ入力から
    /// バイト単位で同じ経路を返す。展開を複数回に分けても一括実行と結果がビット単位で一致する。
    class NavPathfinder
    {
    public:
        NavPathfinder();
        ~NavPathfinder();
        NavPathfinder(NavPathfinder&&) noexcept;
        NavPathfinder& operator=(NavPathfinder&&) noexcept;

        /// 展開せずに探索を初期化する。grid は最後の Step 呼び出しまで有効であること。
        void Begin(const NavGrid& grid, const NavPathQuery& query);

        /// 最大 maxExpansions 個を展開する。格子の版が変わったら展開せず Failed を返すため、Begin し直すこと。
        ENavSearchState Step(uint32_t maxExpansions);
        ENavSearchState GetState() const;

        /// Begin からの累計展開数。連結成分の不一致による即決も、展開数 0 として確認できる。
        uint32_t GetExpansionCount() const;
        uint64_t GetGridVersionAtBegin() const;

        /// Succeeded または Partial の経路を始点順に返す。それ以外では空。
        Core::Container::Span<const NavCellCoord> GetPathCells() const;

        /// 経路の累積コスト。経路がなければ 0、32 ビットで表せない場合は UINT32_MAX。
        uint32_t GetPathCost() const;

        /// 探索結果を破棄して Idle に戻し、割り当てたページは次の探索に残す。
        void Reset();

    private:
        struct Search;
        Core::Container::TUniquePtr<Search> m_Search;
    };
} // namespace NorvesLib::Modules::AI
