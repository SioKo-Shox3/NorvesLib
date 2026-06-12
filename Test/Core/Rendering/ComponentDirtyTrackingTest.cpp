#include "Component/Component.h"
#include "Component/LightComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Object/ObjectCast.h"
#include "Object/WorldObject.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;

namespace
{
    Rendering::MeshDataHandle MakeMeshHandle(uint64_t id)
    {
        Rendering::MeshDataHandle handle;
        handle.Id = id;
        return handle;
    }

    Rendering::MaterialHandle MakeMaterialHandle(uint64_t id)
    {
        Rendering::MaterialHandle handle;
        handle.Id = id;
        return handle;
    }

    void ExpectDirtyAfter(
        void (*operation)(NorvesLib::Core::Component::Component&),
        NorvesLib::Core::Component::Component& component)
    {
        component.ClearRenderStateDirty();
        assert(!component.IsRenderStateDirty());
        operation(component);
        assert(component.IsRenderStateDirty());
    }

    void TestTransformVersion()
    {
        WorldObject object;
        assert(object.GetTransformVersion() == 1);

        object.SetPosition(1.0f, 2.0f, 3.0f);
        assert(object.GetTransformVersion() == 2);

        object.SetPosition(NorvesLib::Math::Vector3(4.0f, 5.0f, 6.0f));
        assert(object.GetTransformVersion() == 3);

        object.SetRotation(0.0f, 0.0f, 0.0f, 1.0f);
        assert(object.GetTransformVersion() == 4);

        object.SetRotation(NorvesLib::Math::Quaternion(0.0f, 0.0f, 0.0f, 1.0f));
        assert(object.GetTransformVersion() == 5);

        object.SetScale(2.0f, 2.0f, 2.0f);
        assert(object.GetTransformVersion() == 6);

        object.SetScale(NorvesLib::Math::Vector3(1.0f, 1.0f, 1.0f));
        assert(object.GetTransformVersion() == 7);
    }

    void TestComponentDirty()
    {
        NorvesLib::Core::Component::Component component;
        assert(component.IsRenderStateDirty());
        assert(component.GetLastSyncedTransformVersion() == 0);

        component.ClearRenderStateDirty();
        assert(!component.IsRenderStateDirty());

        component.Disable();
        assert(component.IsRenderStateDirty());

        component.ClearRenderStateDirty();
        component.Disable();
        assert(!component.IsRenderStateDirty());

        component.Enable();
        assert(component.IsRenderStateDirty());

        component.SetLastSyncedTransformVersion(42);
        assert(component.GetLastSyncedTransformVersion() == 42);
    }

    void TestMeshDirty()
    {
        MeshComponent mesh;
        assert(mesh.IsRenderStateDirty());

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->SetMeshHandle(MakeMeshHandle(1));
        }, mesh);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->SetMaterial(0, MakeMaterialHandle(2));
        }, mesh);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->ClearMaterials();
        }, mesh);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->SetVisible(false);
        }, mesh);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->SetCastShadow(false);
        }, mesh);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->SetReceiveShadow(false);
        }, mesh);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->SetRenderLayer(Rendering::RenderLayer::All);
        }, mesh);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->SetCustomData(0, 1.0f);
        }, mesh);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<MeshComponent>(&component)->SetForcedLODLevel(1);
        }, mesh);
    }

    void TestLightDirty()
    {
        LightComponent light;
        assert(light.IsRenderStateDirty());

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<LightComponent>(&component)->SetLightColor(0.5f, 0.6f, 0.7f);
        }, light);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<LightComponent>(&component)->SetIntensity(2.0f);
        }, light);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<LightComponent>(&component)->SetLightDirection(1.0f, 0.0f, 0.0f);
        }, light);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<LightComponent>(&component)->SetCastShadows(true);
        }, light);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<LightComponent>(&component)->SetLightVisible(false);
        }, light);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<LightComponent>(&component)->SetShadowBias(0.01f);
        }, light);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<LightComponent>(&component)->SetShadowMapResolution(2048);
        }, light);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<LightComponent>(&component)->SetAffectedLayers(Rendering::RenderLayer::Default);
        }, light);
    }

    void TestPointLightDirty()
    {
        PointLightComponent pointLight;
        assert(pointLight.IsRenderStateDirty());

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<PointLightComponent>(&component)->SetRange(12.0f);
        }, pointLight);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<PointLightComponent>(&component)->SetAttenuationConstant(1.2f);
        }, pointLight);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<PointLightComponent>(&component)->SetAttenuationLinear(0.2f);
        }, pointLight);

        ExpectDirtyAfter([](NorvesLib::Core::Component::Component& component)
        {
            CastTo<PointLightComponent>(&component)->SetAttenuationQuadratic(0.05f);
        }, pointLight);
    }

    void TestOwnerActiveDirty()
    {
        WorldObject owner;
        owner.Initialize();

        auto* mesh = new MeshComponent();
        auto* light = new PointLightComponent();
        assert(owner.AddComponent(mesh));
        assert(owner.AddComponent(light));

        mesh->ClearRenderStateDirty();
        light->ClearRenderStateDirty();

        owner.SetActive(false);
        assert(mesh->IsRenderStateDirty());
        assert(light->IsRenderStateDirty());

        mesh->ClearRenderStateDirty();
        light->ClearRenderStateDirty();

        owner.SetActive(true);
        assert(mesh->IsRenderStateDirty());
        assert(light->IsRenderStateDirty());

        owner.Finalize();
    }
}

int main()
{
    std::cout << "ComponentDirtyTrackingTest start\n";

    TestTransformVersion();
    TestComponentDirty();
    TestMeshDirty();
    TestLightDirty();
    TestPointLightDirty();
    TestOwnerActiveDirty();

    std::cout << "ComponentDirtyTrackingTest passed\n";
    return 0;
}
