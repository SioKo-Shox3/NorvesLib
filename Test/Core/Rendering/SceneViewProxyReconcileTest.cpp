#include "Rendering/SceneView.h"
#include "Container/UnorderedSet.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;

namespace
{
    MeshProxy MakeMeshProxy(uint64_t objectId, uint64_t componentId)
    {
        MeshProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.MeshHandle.Id = objectId * 10;
        proxy.MaterialCount = 1;
        proxy.Materials[0].Id = componentId * 10;
        proxy.WorldBounds.Radius = 1.0f;
        return proxy;
    }

    MegaGeometryProxy MakeMegaGeometryProxy(uint64_t objectId, uint64_t componentId)
    {
        MegaGeometryProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.MegaMeshHandle.Id = objectId * 100;
        proxy.WorldBounds.Radius = 1.0f;
        return proxy;
    }

    LightProxy MakeLightProxy(uint64_t lightId)
    {
        LightProxy proxy;
        proxy.LightId = lightId;
        proxy.Intensity = 1.0f;
        proxy.bVisible = true;
        return proxy;
    }
}

int main()
{
    std::cout << "SceneViewProxyReconcileTest start\n";

    SceneView view;
    SceneViewSettings settings;
    assert(view.Initialize(settings));

    view.UpdateMeshProxy(MakeMeshProxy(1, 11));
    view.UpdateMeshProxy(MakeMeshProxy(2, 12));
    view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(1, 21));
    view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(3, 23));
    view.UpdateLightProxy(MakeLightProxy(101));
    view.UpdateLightProxy(MakeLightProxy(102));

    Container::UnorderedSet<uint64_t> liveMeshComponentIds;
    liveMeshComponentIds.insert(12);
    view.RemoveStaleMeshProxies(liveMeshComponentIds);
    assert(view.GetMeshProxies().size() == 1);
    assert(view.GetMeshProxies()[0].ComponentId == 12);

    Container::UnorderedSet<uint64_t> liveMegaGeometryObjectIds;
    liveMegaGeometryObjectIds.insert(3);
    view.RemoveStaleMegaGeometryProxies(liveMegaGeometryObjectIds);
    assert(view.GetMegaGeometryProxies().size() == 1);
    assert(view.GetMegaGeometryProxies()[0].ObjectId == 3);

    Container::UnorderedSet<uint64_t> liveLightIds;
    liveLightIds.insert(101);
    view.RemoveStaleLightProxies(liveLightIds);
    assert(view.GetLightProxies().size() == 1);
    assert(view.GetLightProxies()[0].LightId == 101);

    view.Shutdown();

    std::cout << "SceneViewProxyReconcileTest passed\n";
    return 0;
}
