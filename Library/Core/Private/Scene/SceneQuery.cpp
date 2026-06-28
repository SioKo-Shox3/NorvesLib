#include "Scene/SceneQuery.h"
#include "Object/World.h"
#include "Object/Entity.h"
#include "Math/GeometryIntersection.h"
#include <cfloat>

namespace NorvesLib::Core::Scene
{
    void SceneQuery::Rebuild(const World& world)
    {
        m_Entries.clear();

        auto rootEntities = world.GetRootEntities();
        for (Entity* entity : rootEntities)
        {
            CollectRecursive(entity, m_Entries);
        }
    }

    void SceneQuery::Rebuild(Container::Span<Entity* const> entities)
    {
        m_Entries.clear();

        for (Entity* entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            Math::AABB bounds;
            if (entity->GetWorldAABB(bounds))
            {
                m_Entries.push_back({entity, bounds});
            }
        }
    }

    void SceneQuery::Clear()
    {
        m_Entries.clear();
    }

    bool SceneQuery::Raycast(const Math::Ray& ray, RaycastHit& outHit) const
    {
        outHit = RaycastHit{};

        float bestT = FLT_MAX;
        for (const Entry& entry : m_Entries)
        {
            float t = 0.0f;
            if (Math::RayIntersectsAABB(ray, entry.Bounds, t) && t < bestT)
            {
                bestT = t;
                outHit.HitEntity = entry.EntityPtr;
                outHit.Distance = t;
                outHit.bHit = true;
            }
        }

        return outHit.bHit;
    }

    size_t SceneQuery::GetEntryCount() const
    {
        return m_Entries.size();
    }

    void SceneQuery::CollectRecursive(Entity* entity, Container::VariableArray<Entry>& entries)
    {
        if (!entity)
        {
            return;
        }

        if (!entity->IsActive() || entity->IsPendingDestroy())
        {
            return;
        }

        Math::AABB bounds;
        if (entity->GetWorldAABB(bounds))
        {
            entries.push_back({entity, bounds});
        }

        auto childEntities = entity->GetChildEntities();
        for (Entity* child : childEntities)
        {
            CollectRecursive(child, entries);
        }
    }

} // namespace NorvesLib::Core::Scene
