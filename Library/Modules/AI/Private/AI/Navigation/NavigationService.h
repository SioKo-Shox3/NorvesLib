#pragma once

#include "AI/Navigation/INavigationService.h"
#include "AI/Navigation/NavAgentProfiles.h"
#include "AI/Navigation/NavPathfinder.h"
#include "Container/Deque.h"

namespace NorvesLib::Modules::AI
{
    /// GameThread 上で格子と経路要求を所有する。探索中の格子のアドレスは固定する。
    class NavigationService final : public INavigationService
    {
    public:
        NavigationService();
        NavigationService(const NavigationService&) = delete;
        NavigationService& operator=(const NavigationService&) = delete;

        ENavTileResult LoadTile(NavTile&& tile) override;
        bool UnloadTile(NavTileCoord coord) override;
        const NavGrid& GetGrid() const override;
        NavDynamicOverlay& GetDynamicOverlay();
        const NavDynamicOverlay& GetDynamicOverlay() const override;
        PathHandle RequestPath(const Math::Vector3& start, const Math::Vector3& goal,
                               const NavAgentProfile& profile, float epsilon = 1.2f) override;
        ENavPathStatus GetPathStatus(PathHandle handle) const override;
        Core::Container::Span<const Math::Vector3> GetPath(PathHandle handle) const override;
        bool BuildPathPolyline(PathHandle handle, Math::PolylinePath& outPath) const override;
        bool CancelPath(PathHandle handle) override;
        bool ReleasePath(PathHandle handle) override;
        bool IsPathStale(PathHandle handle) const override;
        bool FindNearestWalkable(const Math::Vector3& position, const NavAgentProfile& profile,
                                 float searchRadius, Math::Vector3& outPosition) const override;
        bool IsWalkable(const Math::Vector3& position, const NavAgentProfile& profile) const override;
        NavRaycastResult NavRaycast(const Math::Vector3& from, const Math::Vector3& to,
                                    const NavAgentProfile& profile) const override;
        uint32_t SampleCost(const Math::Vector3& position, const NavAgentProfile& profile) const override;
        const NavAgentProfile* FindProfile(const char* name) const override;
        void SetExpansionBudget(uint32_t expansionsPerStep) override;
        uint32_t GetExpansionBudget() const override;

        /// PreFixedTick で 1 回呼ぶ。到着順に同じ探索を 1 本ずつ続けるため、格子と押印が変わらなければ
        /// 固定ステップに展開を分けても一括で探索したときと同じ経路になる。
        void RunBudget();
        void SetProfiles(NavAgentProfileSet&& profiles);
        const NavAgentProfileSet& GetProfiles() const;
        bool SetGridDesc(const NavGridDesc& desc);
        void Reset();

        uint32_t GetPathSlotCount() const;
        ENavPathStatus GetSlotStatus(uint32_t i) const;
        Core::Container::Span<const Math::Vector3> GetSlotPath(uint32_t i) const;

    private:
        struct TileStamp
        {
            NavTileCoord Coord;
            uint32_t Generation;
            uint32_t DynamicVersion;
        };

        struct PathSlot
        {
            // 枠配列の拡張で、貸し出し中の点列をコピーして移動元を破棄することを防ぐ。
            PathSlot() = default;
            PathSlot(PathSlot&&) = default;
            PathSlot& operator=(PathSlot&&) = default;

            ENavPathStatus Status = ENavPathStatus::Invalid;
            bool bDynamicStaleAtFinish = false;
            Math::Vector3 Start;
            Math::Vector3 Goal;
            NavPathQuery Query;
            Core::Container::VariableArray<NavCellCoord> Cells;
            Core::Container::VariableArray<Math::Vector3> Points;
            Core::Container::VariableArray<TileStamp> Tiles;
        };

        bool HasSlot(PathHandle handle) const;
        void FinishPath(PathSlot& slot, ENavSearchState state);

        void SyncOverlayGridDesc(const NavGridDesc& previous);

        NavGrid m_Grid;
        NavDynamicOverlay m_Overlay;
        NavAgentProfileSet m_Profiles;
        NavPathfinder m_Pathfinder;
        Core::Container::VariableArray<PathSlot> m_Slots;
        // 世代は枠と同じ添字で保持し、Reset 後に枠を作り直しても古いハンドルを失効させる。
        Core::Container::VariableArray<uint32_t> m_Generations;
        Core::Container::VariableArray<uint32_t> m_FreeSlots;
        Core::Container::Deque<uint32_t> m_Pending;
        uint32_t m_ExpansionBudget = kDefaultNavExpansionBudget;
    };
} // namespace NorvesLib::Modules::AI
