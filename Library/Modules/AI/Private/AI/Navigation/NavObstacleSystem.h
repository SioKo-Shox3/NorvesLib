#pragma once

#include "AI/Navigation/NavTypes.h"
#include "Container/VariableArray.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::AI
{
    class NavigationService;
    class NavDynamicOverlay;
    class NavObstacleComponent;
    struct NavObstacleSettings;

    class NavObstacleSystem
    {
    public:
        void Register(NavObstacleComponent& obstacle);
        void Unregister(NavObstacleComponent& obstacle, NavigationService& nav);
        void Reset(NavigationService& nav);
        void MarkAllForReevaluate();
        void Step(float dt, NavigationService& nav, const Core::World& world, const NavObstacleSettings& settings);

        bool HasRegisteredObstacles() const { return !m_Obstacles.empty(); }
        uint32_t GetObstacleCount() const;
        size_t GetStampedCellCount() const;

    private:
        static void Detach(NavObstacleComponent& obstacle, NavDynamicOverlay& overlay);
        void Update(NavObstacleComponent& obstacle, NavigationService& nav, const Core::World& world,
                    const NavObstacleSettings& settings);

        Core::Container::VariableArray<NavObstacleComponent*> m_Obstacles;
        Core::Container::VariableArray<NavCellCoord> m_RasterCells;
        uint64_t m_GridVersion = 0;
        uint64_t m_OverlayGeneration = 0;
        double m_Time = 0.0;
    };
}
