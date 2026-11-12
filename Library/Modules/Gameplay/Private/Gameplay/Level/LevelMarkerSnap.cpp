// 全階層の接地対象を集め、親の移動後のキャッシュを更新して子の吸着を行う。
#include "Gameplay/Level/LevelMarkerSnap.h"

#include "Gameplay/Level/LevelMarkerComponent.h"
#include "Gameplay/Level/ScentTrailComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    namespace
    {
        using Core::Scene::LevelSnap::LevelSnapTarget;
        using Core::Scene::LevelSnap::LevelSnapStats;
        using Targets = Core::Container::VariableArray<LevelSnapTarget>;

        void CollectEntity(Core::Entity& entity, Targets& markerTargets, Targets& trailPointTargets)
        {
            const auto* marker = entity.GetComponent<LevelMarkerComponent>();
            if (marker != nullptr && marker->getbSnapToGround())
            {
                markerTargets.push_back({&entity, marker->getGroundOffset()});
            }
            if (entity.GetComponent<ScentTrailPointComponent>() != nullptr)
            {
                const Core::Entity* parent = entity.GetParentEntity();
                const auto* parentMarker = parent != nullptr ? parent->GetComponent<LevelMarkerComponent>() : nullptr;
                if (parentMarker != nullptr && parentMarker->getbSnapToGround()
                    && parent->GetComponent<ScentTrailComponent>() != nullptr)
                {
                    trailPointTargets.push_back({&entity, parentMarker->getGroundOffset()});
                }
            }
            for (Core::Entity* child : entity.GetChildEntities())
            {
                CollectEntity(*child, markerTargets, trailPointTargets);
            }
        }

        LevelSnapStats SnapTargets(Core::World& world, const Core::Scene::SceneQuery& query,
            const Targets& targets, float maxDrop)
        {
            LevelSnapStats total;
            for (const LevelSnapTarget& target : targets)
            {
                const auto stats = Core::Scene::LevelSnap::ReSnapAll(query,
                    Core::Container::Span<const LevelSnapTarget>(&target, 1), maxDrop);
                total.Snapped += stats.Snapped;
                total.Missed += stats.Missed;
                // 同じ列にも親子のマーカーが存在するため、親を動かしたら子を読む前に更新する。
                if (stats.Snapped != 0 && !target.Target->GetChildEntities().empty())
                {
                    world.UpdateWorldTransforms();
                }
            }
            return total;
        }
    } // namespace

    void CollectLevelSnapTargets(const Core::World& world, Targets& outMarkerTargets, Targets& outTrailPointTargets)
    {
        outMarkerTargets.clear();
        outTrailPointTargets.clear();
        for (Core::Entity* root : world.GetRootEntities())
        {
            CollectEntity(*root, outMarkerTargets, outTrailPointTargets);
        }
    }

    Core::Scene::LevelSnap::LevelSnapStats ReSnapAllLevelMarkers(Core::World& world,
        const Core::Scene::SceneQuery& query, float maxDrop)
    {
        world.UpdateWorldTransforms();
        Targets markerTargets;
        Targets trailPointTargets;
        CollectLevelSnapTargets(world, markerTargets, trailPointTargets);
        auto total = SnapTargets(world, query, markerTargets, maxDrop);
        world.UpdateWorldTransforms();
        const auto points = SnapTargets(world, query, trailPointTargets, maxDrop);
        total.Snapped += points.Snapped;
        total.Missed += points.Missed;
        world.UpdateWorldTransforms();
        return total;
    }

    bool FindLevelMarkerSnapDeviations(Core::World& world, const Core::Scene::SceneQuery& query,
        float maxDrop, float tolerance,
        Core::Container::VariableArray<Core::Scene::LevelSnap::LevelSnapDeviation>& outDeviations)
    {
        world.UpdateWorldTransforms();
        Targets markerTargets;
        Targets trailPointTargets;
        CollectLevelSnapTargets(world, markerTargets, trailPointTargets);
        for (const auto& point : trailPointTargets)
        {
            markerTargets.push_back(point);
        }
        return Core::Scene::LevelSnap::FindSnapDeviations(query,
            Core::Container::Span<const LevelSnapTarget>(markerTargets), maxDrop, tolerance, outDeviations);
    }
} // namespace NorvesLib::Modules::Gameplay::Level
