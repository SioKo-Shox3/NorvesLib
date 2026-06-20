#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Object/World.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;

namespace
{
    MeshDataHandle MakeMeshHandle(uint64_t id)
    {
        MeshDataHandle handle;
        handle.Id = id;
        return handle;
    }

    MegaGeometry::MegaMeshHandle MakeMegaMeshHandle(uint64_t id)
    {
        MegaGeometry::MegaMeshHandle handle;
        handle.Id = id;
        return handle;
    }

    MaterialHandle MakeMaterialHandle(RenderResources& renderResources, BlendMode blendMode)
    {
        MaterialCreateData createInfo;
        createInfo.Blend = blendMode;
        return renderResources.Materials().Create(createInfo);
    }

    MeshProxy* FindMeshProxy(SceneView& view, uint64_t objectId)
    {
        auto& proxies = const_cast<Container::VariableArray<MeshProxy>&>(view.GetMeshProxies());
        for (MeshProxy& proxy : proxies)
        {
            if (proxy.ObjectId == objectId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    LightProxy* FindLightProxy(SceneView& view, uint64_t lightId)
    {
        auto& proxies = const_cast<Container::VariableArray<LightProxy>&>(view.GetLightProxies());
        for (LightProxy& proxy : proxies)
        {
            if (proxy.LightId == lightId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    MegaGeometryProxy* FindMegaGeometryProxy(SceneView& view, uint64_t objectId)
    {
        auto& proxies = const_cast<Container::VariableArray<MegaGeometryProxy>&>(view.GetMegaGeometryProxies());
        for (MegaGeometryProxy& proxy : proxies)
        {
            if (proxy.ObjectId == objectId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    void TestMeshAndLightDifferentialSync()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        Entity* object = world.SpawnObject<Entity>();
        assert(object);

        MeshComponent* mesh = world.CreateComponent<MeshComponent>(object);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(100));

        PointLightComponent* light = world.CreateComponent<PointLightComponent>(object);
        assert(light);

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(view.GetLightProxies().size() == 1);
        assert(!mesh->IsRenderStateDirty());
        assert(!light->IsRenderStateDirty());
        assert(mesh->GetLastSyncedTransformVersion() == object->GetTransformVersion());
        assert(light->GetLastSyncedTransformVersion() == object->GetTransformVersion());

        MeshProxy* meshProxy = FindMeshProxy(view, object->GetObjectId());
        LightProxy* lightProxy = FindLightProxy(view, light->GetComponentId());
        assert(meshProxy);
        assert(lightProxy);

        meshProxy->WorldTransform.m30 = 777.0f;
        lightProxy->PositionX = 888.0f;

        world.SyncToSceneView();
        meshProxy = FindMeshProxy(view, object->GetObjectId());
        lightProxy = FindLightProxy(view, light->GetComponentId());
        assert(meshProxy);
        assert(lightProxy);
        assert(meshProxy->WorldTransform.m30 == 777.0f);
        assert(lightProxy->PositionX == 888.0f);

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(view.GetLightProxies().size() == 1);
        assert(FindMeshProxy(view, object->GetObjectId())->WorldTransform.m30 == 777.0f);

        object->SetPosition(3.0f, 4.0f, 5.0f);
        world.SyncToSceneView();
        meshProxy = FindMeshProxy(view, object->GetObjectId());
        lightProxy = FindLightProxy(view, light->GetComponentId());
        assert(meshProxy);
        assert(lightProxy);
        assert(meshProxy->WorldTransform.m30 == 3.0f);
        assert(meshProxy->WorldBounds.CenterX == 3.0f);
        assert(lightProxy->PositionX == 3.0f);
        assert(lightProxy->PositionY == 4.0f);
        assert(lightProxy->PositionZ == 5.0f);

        mesh->SetVisible(false);
        world.SyncToSceneView();
        assert(view.GetMeshProxies().empty());
        assert(!mesh->IsRenderStateDirty());

        mesh->SetVisible(true);
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(!mesh->IsRenderStateDirty());

        mesh->SetMeshHandle(MeshDataHandle::Invalid());
        world.SyncToSceneView();
        assert(view.GetMeshProxies().empty());
        assert(!mesh->IsRenderStateDirty());

        mesh->SetMeshHandle(MakeMeshHandle(101));
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);

        meshProxy = FindMeshProxy(view, object->GetObjectId());
        assert(meshProxy);
        meshProxy->WorldTransform.m30 = 444.0f;

        world.SetSceneView(&view);
        world.SyncToSceneView();
        meshProxy = FindMeshProxy(view, object->GetObjectId());
        assert(meshProxy);
        assert(meshProxy->WorldTransform.m30 == 3.0f);

        world.Finalize();
    }

    void TestMegaGeometryDifferentialSync()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        Entity* object = world.SpawnObject<Entity>();
        assert(object);

        MegaGeometryComponent* mega = world.CreateComponent<MegaGeometryComponent>(object);
        assert(mega);
        mega->SetMegaMeshHandle(MakeMegaMeshHandle(200));

        world.SyncToSceneView();
        assert(view.GetMeshProxies().empty());
        assert(view.GetMegaGeometryProxies().size() == 1);
        assert(!mega->IsRenderStateDirty());

        MegaGeometryProxy* megaProxy = FindMegaGeometryProxy(view, object->GetObjectId());
        assert(megaProxy);
        megaProxy->WorldTransform.m30 = 999.0f;

        world.SyncToSceneView();
        megaProxy = FindMegaGeometryProxy(view, object->GetObjectId());
        assert(megaProxy);
        assert(megaProxy->WorldTransform.m30 == 999.0f);
        assert(view.GetMeshProxies().empty());

        object->SetPosition(6.0f, 7.0f, 8.0f);
        world.SyncToSceneView();
        megaProxy = FindMegaGeometryProxy(view, object->GetObjectId());
        assert(megaProxy);
        assert(megaProxy->WorldTransform.m30 == 6.0f);
        assert(megaProxy->WorldBounds.CenterX == 6.0f);
        assert(view.GetMeshProxies().empty());

        megaProxy->WorldTransform.m30 = 555.0f;
        world.SetSceneView(&view);
        world.SyncToSceneView();
        megaProxy = FindMegaGeometryProxy(view, object->GetObjectId());
        assert(megaProxy);
        assert(megaProxy->WorldTransform.m30 == 6.0f);

        SceneView replacementView;
        assert(replacementView.Initialize(settings));
        world.SetSceneView(&replacementView);
        world.SyncToSceneView();
        assert(replacementView.GetMeshProxies().empty());
        assert(replacementView.GetMegaGeometryProxies().size() == 1);
        assert(FindMegaGeometryProxy(replacementView, object->GetObjectId())->WorldTransform.m30 == 6.0f);

        world.Finalize();
    }

    void TestMeshMaterialBlendSync()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        RenderResources renderResources;

        Entity* object = world.SpawnObject<Entity>();
        assert(object);

        MeshComponent* mesh = world.CreateComponent<MeshComponent>(object);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(300));

        const MaterialHandle translucent = MakeMaterialHandle(renderResources, BlendMode::Translucent);
        const MaterialHandle additive = MakeMaterialHandle(renderResources, BlendMode::Additive);

        mesh->SetMaterial(0, translucent);
        world.SyncToSceneView(&renderResources.Materials());

        MeshProxy* meshProxy = FindMeshProxy(view, object->GetObjectId());
        assert(meshProxy);
        assert(meshProxy->MaterialBlendModes[0] == BlendMode::Translucent);

        mesh->SetMaterial(0, additive);
        world.SyncToSceneView(&renderResources.Materials());

        meshProxy = FindMeshProxy(view, object->GetObjectId());
        assert(meshProxy);
        assert(meshProxy->MaterialBlendModes[0] == BlendMode::Additive);

        MaterialHandle missingHandle;
        missingHandle.Id = 99999;
        mesh->SetMaterial(0, missingHandle);
        world.SyncToSceneView(&renderResources.Materials());

        meshProxy = FindMeshProxy(view, object->GetObjectId());
        assert(meshProxy);
        assert(meshProxy->MaterialBlendModes[0] == BlendMode::Opaque);

        world.Finalize();
    }
}

int main()
{
    std::cout << "WorldSyncDifferentialTest start\n";

    TestMeshAndLightDifferentialSync();
    TestMegaGeometryDifferentialSync();
    TestMeshMaterialBlendSync();

    std::cout << "WorldSyncDifferentialTest passed\n";
    return 0;
}
