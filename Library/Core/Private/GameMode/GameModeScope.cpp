#include "GameMode/GameModeScope.h"
#include "Object/World.h"
#include "Object/WorldObject.h"
#include "Rendering/RenderResources.h"

namespace NorvesLib::Core::GameMode
{

    GameModeScope::GameModeScope(
        NorvesLib::Core::World* world,
        NorvesLib::Core::Rendering::RenderResources* renderResources)
        : m_pWorld(world)
        , m_pRenderResources(renderResources)
    {
    }

    void GameModeScope::TrackObject(NorvesLib::Core::WorldObject* object)
    {
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

    void GameModeScope::Cleanup()
    {
        // 1) WorldObject を挿入順に World::RemoveObject で除去
        if (m_pWorld)
        {
            for (NorvesLib::Core::WorldObject* obj : m_TrackedObjects)
            {
                m_pWorld->RemoveObject(obj);
            }
            m_TrackedObjects.clear();
        }

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
            m_TrackedModels.clear();
        }
    }

    bool GameModeScope::IsEmpty() const
    {
        return m_TrackedObjects.empty()
            && m_TrackedMeshes.empty()
            && m_TrackedModels.empty();
    }

} // namespace NorvesLib::Core::GameMode
