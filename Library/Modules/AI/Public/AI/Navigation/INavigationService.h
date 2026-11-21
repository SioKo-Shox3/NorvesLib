#pragma once

#include "AI/Navigation/NavGridQueries.h"
#include "Math/PolylinePath.h"

namespace NorvesLib::Modules::AI
{
    /// 固定ステップ当たりの既定展開数。未計測の仮の値。
    inline constexpr uint32_t kDefaultNavExpansionBudget = 1500;

    /// ゲームから格子と経路を利用する窓口。スレッド安全ではなく、すべて GameThread からだけ呼ぶ。
    class INavigationService
    {
    public:
        virtual ~INavigationService() = default;

        virtual ENavTileResult LoadTile(NavTile&& tile) = 0;
        virtual bool UnloadTile(NavTileCoord coord) = 0;
        virtual const NavGrid& GetGrid() const = 0;

        /// 始点が通れなければ半径 2 m 内の通行可能なセルに寄せる。候補がなければ Failed の枠を返す。
        /// 非有限の座標・重み・半径、または負の半径には無効なハンドルを返す。
        virtual PathHandle RequestPath(const Math::Vector3& start, const Math::Vector3& goal,
                                       const NavAgentProfile& profile, float epsilon = 1.2f) = 0;
        virtual ENavPathStatus GetPathStatus(PathHandle handle) const = 0;
        /// Succeeded・Partial の平滑化した点列を貸し出す。他の状態では空。解放またはサービス終了まで有効。
        virtual Core::Container::Span<const Math::Vector3> GetPath(PathHandle handle) const = 0;
        /// 点列を折れ線に写す。点がなければ false を返す。
        virtual bool BuildPathPolyline(PathHandle handle, Math::PolylinePath& outPath) const = 0;
        /// Pending の要求を取り消す。取り消した枠は ReleasePath で返す。
        virtual bool CancelPath(PathHandle handle) = 0;
        /// 状態を問わず枠を返す。解放したハンドルの状態は Invalid。
        virtual bool ReleasePath(PathHandle handle) = 0;
        /// 経路のセル列が通るタイルのどれかが置き換えられたか、解放されたときに真を返す。
        virtual bool IsPathStale(PathHandle handle) const = 0;

        virtual bool FindNearestWalkable(const Math::Vector3& position, const NavAgentProfile& profile,
                                         float searchRadius, Math::Vector3& outPosition) const = 0;
        virtual bool IsWalkable(const Math::Vector3& position, const NavAgentProfile& profile) const = 0;
        virtual NavRaycastResult NavRaycast(const Math::Vector3& from, const Math::Vector3& to,
                                            const NavAgentProfile& profile) const = 0;
        virtual uint32_t SampleCost(const Math::Vector3& position, const NavAgentProfile& profile) const = 0;

        virtual const NavAgentProfile* FindProfile(const char* name) const = 0;
        /// 固定ステップ 1 歩で全要求に割り当てる展開数。0 で探索を休止する。
        virtual void SetExpansionBudget(uint32_t expansionsPerStep) = 0;
        virtual uint32_t GetExpansionBudget() const = 0;
    };
} // namespace NorvesLib::Modules::AI
