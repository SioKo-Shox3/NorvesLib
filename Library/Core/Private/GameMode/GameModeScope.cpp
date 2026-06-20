#include "GameMode/GameModeScope.h"
#include "Object/World.h"
#include "Object/Entity.h"
#include "Rendering/RenderResources.h"

namespace NorvesLib::Core::GameMode
{
    namespace
    {
        bool ContainsEntity(
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Entity*>& entities,
            const NorvesLib::Core::Entity* entity)
        {
            for (const NorvesLib::Core::Entity* existing : entities)
            {
                if (existing == entity)
                {
                    return true;
                }
            }
            return false;
        }

        bool HasTrackedAncestor(
            NorvesLib::Core::Entity* entity,
            NorvesLib::Core::World* world,
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Entity*>& trackedObjects)
        {
            for (NorvesLib::Core::Entity* parent = entity->GetParentEntity(); parent != nullptr; parent = parent->GetParentEntity())
            {
                if (parent->GetWorld() != world)
                {
                    continue;
                }

                if (ContainsEntity(trackedObjects, parent))
                {
                    return true;
                }
            }
            return false;
        }
    }

    GameModeScope::GameModeScope(
        NorvesLib::Core::World* world,
        NorvesLib::Core::Rendering::RenderResources* renderResources)
        : m_pWorld(world)
        , m_pRenderResources(renderResources)
    {
    }

    void GameModeScope::TrackObject(NorvesLib::Core::Entity* object)
    {
        if (object == nullptr || ContainsEntity(m_TrackedObjects, object))
        {
            return;
        }

        m_TrackedObjects.push_back(object);
    }

    void GameModeScope::TrackMesh(NorvesLib::Core::Rendering::MeshDataHandle handle)
    {
        m_TrackedMeshes.push_back(handle);
    }

    void GameModeScope::TrackModel(NorvesLib::Core::Rendering::ModelHandle handle)
    {
        m_TrackedModels.push_back(handle);
    }

    void GameModeScope::Untrack(NorvesLib::Core::Entity* object)
    {
        if (object == nullptr)
        {
            return;
        }

        for (auto it = m_TrackedObjects.begin(); it != m_TrackedObjects.end(); ++it)
        {
            if (*it == object)
            {
                m_TrackedObjects.erase(it);
                return;
            }
        }
    }

    void GameModeScope::Cleanup()
    {
        // 1) Entity を tree-aware に World から除去
        if (m_pWorld)
        {
            NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Entity*> toDestroy;
            for (NorvesLib::Core::Entity* obj : m_TrackedObjects)
            {
                if (obj == nullptr || obj->GetWorld() != m_pWorld)
                {
                    continue;
                }

                if (HasTrackedAncestor(obj, m_pWorld, m_TrackedObjects))
                {
                    continue;
                }

                if (!ContainsEntity(toDestroy, obj))
                {
                    toDestroy.push_back(obj);
                }
            }

            for (NorvesLib::Core::Entity* obj : toDestroy)
            {
                if (obj == nullptr || obj->GetWorld() != m_pWorld)
                {
                    continue;
                }

                if (obj->GetParentEntity() == nullptr)
                {
                    m_pWorld->RemoveObject(obj);
                }
                else
                {
                    m_pWorld->RemoveEntity(obj);
                }
            }
        }
        m_TrackedObjects.clear();

        // 2) MeshDataHandle を MeshResources::Unregister で解放
        // 3) ModelHandle を MegaGeometryResources::ReleaseModel で解放
        if (m_pRenderResources)
        {
            for (NorvesLib::Core::Rendering::MeshDataHandle handle : m_TrackedMeshes)
            {
                m_pRenderResources->Meshes().Unregister(handle);
            }
            m_TrackedMeshes.clear();

            for (NorvesLib::Core::Rendering::ModelHandle handle : m_TrackedModels)
            {
                m_pRenderResources->MegaGeometry().ReleaseModel(handle);
            }
        }
        m_TrackedMeshes.clear();
        m_TrackedModels.clear();
    }

    bool GameModeScope::IsEmpty() const
    {
        return m_TrackedObjects.empty()
            && m_TrackedMeshes.empty()
            && m_TrackedModels.empty();
    }

} // namespace NorvesLib::Core::GameMode
