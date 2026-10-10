#include "Component/BoardComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/SpotLightComponent.h"
#include "Ecs/EcsRenderTypes.h"
#include "Ecs/EcsWorld.h"
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
    void SetTranslationX(NorvesLib::Math::Matrix4x4& matrix, float x)
    {
        NorvesLib::Math::Vector3 translation = matrix.GetTranslationRow();
        translation.x = x;
        matrix.SetTranslationRow(translation);
    }

    float GetTranslationX(const NorvesLib::Math::Matrix4x4& matrix)
    {
        return matrix.GetTranslationRow().x;
    }

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

        SetTranslationX(meshProxy->WorldTransform, 777.0f);
        lightProxy->PositionX = 888.0f;

        world.SyncToSceneView();
        meshProxy = FindMeshProxy(view, object->GetObjectId());
        lightProxy = FindLightProxy(view, light->GetComponentId());
        assert(meshProxy);
        assert(lightProxy);
        // 変わっていない Mesh は現フレームの proxy を毎フレーム公開し直す（速度の履歴のため）ので、書き換えは残らない。
        // 変わっていない Light は公開し直さない
        assert(GetTranslationX(meshProxy->WorldTransform) == 0.0f);
        assert(lightProxy->PositionX == 888.0f);

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(view.GetLightProxies().size() == 1);
        assert(GetTranslationX(FindMeshProxy(view, object->GetObjectId())->WorldTransform) == 0.0f);

        object->SetPosition(3.0f, 4.0f, 5.0f);
        world.SyncToSceneView();
        meshProxy = FindMeshProxy(view, object->GetObjectId());
        lightProxy = FindLightProxy(view, light->GetComponentId());
        assert(meshProxy);
        assert(lightProxy);
        assert(GetTranslationX(meshProxy->WorldTransform) == 3.0f);
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
        SetTranslationX(meshProxy->WorldTransform, 444.0f);

        world.SetSceneView(&view);
        world.SyncToSceneView();
        meshProxy = FindMeshProxy(view, object->GetObjectId());
        assert(meshProxy);
        assert(GetTranslationX(meshProxy->WorldTransform) == 3.0f);

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
        SetTranslationX(megaProxy->WorldTransform, 999.0f);

        world.SyncToSceneView();
        megaProxy = FindMegaGeometryProxy(view, object->GetObjectId());
        assert(megaProxy);
        assert(GetTranslationX(megaProxy->WorldTransform) == 999.0f);
        assert(view.GetMeshProxies().empty());

        object->SetPosition(6.0f, 7.0f, 8.0f);
        world.SyncToSceneView();
        megaProxy = FindMegaGeometryProxy(view, object->GetObjectId());
        assert(megaProxy);
        assert(GetTranslationX(megaProxy->WorldTransform) == 6.0f);
        assert(megaProxy->WorldBounds.CenterX == 6.0f);
        assert(view.GetMeshProxies().empty());

        SetTranslationX(megaProxy->WorldTransform, 555.0f);
        world.SetSceneView(&view);
        world.SyncToSceneView();
        megaProxy = FindMegaGeometryProxy(view, object->GetObjectId());
        assert(megaProxy);
        assert(GetTranslationX(megaProxy->WorldTransform) == 6.0f);

        SceneView replacementView;
        assert(replacementView.Initialize(settings));
        world.SetSceneView(&replacementView);
        world.SyncToSceneView();
        assert(replacementView.GetMeshProxies().empty());
        assert(replacementView.GetMegaGeometryProxies().size() == 1);
        assert(GetTranslationX(FindMegaGeometryProxy(replacementView, object->GetObjectId())->WorldTransform) == 6.0f);

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

namespace
{
    // 描画の値は World の木にいる間、Component の行の列が正になる（予備の値は使われない）
    void TestRenderValuesLiveInComponentRow()
    {
        World world;
        world.Initialize();

        Entity* object = world.SpawnObject<Entity>();
        assert(object);

        MeshComponent* mesh = world.CreateComponent<MeshComponent>(object);
        assert(mesh);
        MegaGeometryComponent* mega = world.CreateComponent<MegaGeometryComponent>(object);
        assert(mega);
        SkinnedMeshComponent* skinned = world.CreateComponent<SkinnedMeshComponent>(object);
        assert(skinned);

        Ecs::EcsWorld& ecs = world.GetEcsWorld();

        mesh->SetMeshHandle(MakeMeshHandle(321));
        mesh->SetVisible(false);
        mesh->SetCastShadow(false);
        mesh->SetRenderLayer(RenderLayer::UI);
        const Ecs::EcsMeshRender* meshColumn = ecs.GetColumnValue<Ecs::EcsMeshRender>(mesh->GetEcsEntity());
        assert(meshColumn);
        assert(meshColumn->MeshHandle.Id == 321);
        assert(!meshColumn->bVisible);
        assert(!meshColumn->bCastShadow);
        assert(meshColumn->bReceiveShadow);
        assert(meshColumn->Layer == RenderLayer::UI);
        assert(mesh->GetMeshHandle().Id == 321);
        assert(!mesh->IsVisible());
        assert(ecs.GetColumnValue<Ecs::EcsMegaMesh>(mesh->GetEcsEntity()) == nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsSkinnedRender>(mesh->GetEcsEntity()) == nullptr);

        mega->SetMegaMeshHandle(MakeMegaMeshHandle(654));
        const Ecs::EcsMegaMesh* megaColumn = ecs.GetColumnValue<Ecs::EcsMegaMesh>(mega->GetEcsEntity());
        assert(megaColumn);
        assert(megaColumn->Handle.Id == 654);
        assert(ecs.GetColumnValue<Ecs::EcsMeshRender>(mega->GetEcsEntity()) != nullptr);
        assert(mega->GetMegaMeshHandle().Id == 654);

        MaterialHandle material;
        material.Id = 987;
        skinned->SetMaterial(material);
        skinned->SetVisible(false);
        skinned->SetCastShadow(false);
        const Ecs::EcsSkinnedRender* skinnedColumn = ecs.GetColumnValue<Ecs::EcsSkinnedRender>(skinned->GetEcsEntity());
        assert(skinnedColumn);
        assert(skinnedColumn->Material.Id == 987);
        assert(!skinnedColumn->bVisible);
        assert(!skinnedColumn->bCastShadow);
        assert(skinned->GetMaterial().Id == 987);
        assert(!skinned->IsVisible());
        assert(!skinned->CastsShadow());
        assert(ecs.GetColumnValue<Ecs::EcsMeshRender>(skinned->GetEcsEntity()) == nullptr);

        // 可視・影・メッシュの handle は列に置くプロパティで、値は列にだけある
        const IClass* skinnedClass = skinned->GetClass();
        assert(skinnedClass);
        for (const char* name : {"MeshHandle", "bVisible", "bCastShadow"})
        {
            const ClassProperty* property = skinnedClass->GetProperty(Identity(name));
            assert(property);
            assert(property->GetStorageKind() == StorageKind::EcsColumn);
        }
        // 列の値を直接書き換えると、Component の読み口と proxy の元がその値を返す
        Ecs::EcsSkinnedRender* skinnedWritable = ecs.GetColumnValue<Ecs::EcsSkinnedRender>(skinned->GetEcsEntity());
        assert(skinnedWritable);
        skinnedWritable->bVisible = true;
        skinnedWritable->bCastShadow = true;
        assert(skinned->IsVisible());
        assert(skinned->CastsShadow());

        world.Finalize();
    }

    void TestLightAndBoardValuesLiveInComponentRow()
    {
        World world;
        world.Initialize();

        Entity* object = world.SpawnObject<Entity>();
        assert(object);

        DirectionalLightComponent* directional = world.CreateComponent<DirectionalLightComponent>(object);
        PointLightComponent* point = world.CreateComponent<PointLightComponent>(object);
        SpotLightComponent* spot = world.CreateComponent<SpotLightComponent>(object);
        BoardComponent* board = world.CreateComponent<BoardComponent>(object);
        assert(directional && point && spot && board);

        Ecs::EcsWorld& ecs = world.GetEcsWorld();

        // 種類ごとに付く列が違う（Directional は共通の列だけ、Point は減衰を足し、Spot はコーン角まで足す）
        assert(ecs.GetColumnValue<Ecs::EcsLightRender>(directional->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsLightFalloff>(directional->GetEcsEntity()) == nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsLightFalloff>(point->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsSpotCone>(point->GetEcsEntity()) == nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsSpotCone>(spot->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsBoardRender>(board->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsLightRender>(board->GetEcsEntity()) == nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsMeshRender>(point->GetEcsEntity()) == nullptr);

        // 生成時の既定値が列にある
        const Ecs::EcsLightRender* directionalColumn = ecs.GetColumnValue<Ecs::EcsLightRender>(directional->GetEcsEntity());
        assert(directionalColumn->Type == LightType::Directional);
        assert(directionalColumn->Unit == LightIntensityUnit::Lux);
        assert(directionalColumn->bVisible);

        point->SetIntensity(7.0f);
        point->SetLightColor(0.5f, 0.25f, 0.125f);
        point->SetCastShadows(true);
        point->SetShadowMapResolution(256);
        point->SetRange(3.5f);
        point->SetAttenuationLinear(0.5f);
        const Ecs::EcsLightRender* pointColumn = ecs.GetColumnValue<Ecs::EcsLightRender>(point->GetEcsEntity());
        assert(pointColumn->Type == LightType::Point);
        assert(pointColumn->Intensity == 7.0f);
        assert(pointColumn->Color.x == 0.5f && pointColumn->Color.y == 0.25f && pointColumn->Color.z == 0.125f);
        assert(pointColumn->bCastShadows);
        assert(pointColumn->ShadowMapResolution == 256);
        const Ecs::EcsLightFalloff* pointFalloff = ecs.GetColumnValue<Ecs::EcsLightFalloff>(point->GetEcsEntity());
        assert(pointFalloff->Range == 3.5f);
        assert(pointFalloff->AttenuationLinear == 0.5f);
        assert(point->GetIntensity() == 7.0f && point->GetRange() == 3.5f);

        spot->SetInnerConeAngle(20.0f);
        spot->SetOuterConeAngle(30.0f);
        const Ecs::EcsSpotCone* spotCone = ecs.GetColumnValue<Ecs::EcsSpotCone>(spot->GetEcsEntity());
        assert(spotCone->InnerConeAngle == 20.0f && spotCone->OuterConeAngle == 30.0f);

        board->SetTextureHandle(TextureHandle{42});
        board->SetVisible(false);
        board->SetTint(NorvesLib::Math::Vector4(0.1f, 0.2f, 0.3f, 0.4f));
        board->SetPivot(NorvesLib::Math::Vector2(0.5f, 0.25f));
        board->SetBoardSpace(BoardSpace::WorldSpace);
        board->SetLayerPriority(3);
        board->SetOrderInLayer(9);
        board->SetFlipX(true);
        const Ecs::EcsBoardRender* boardColumn = ecs.GetColumnValue<Ecs::EcsBoardRender>(board->GetEcsEntity());
        assert(boardColumn->Texture.Id == 42);
        assert(!boardColumn->bVisible);
        assert(boardColumn->Tint.w == 0.4f);
        assert(boardColumn->Pivot.x == 0.5f && boardColumn->Pivot.y == 0.25f);
        assert(boardColumn->Space == BoardSpace::WorldSpace);
        assert(boardColumn->LayerPriority == 3 && boardColumn->OrderInLayer == 9);
        assert(boardColumn->bFlipX && !boardColumn->bFlipY);
        assert(board->GetTextureHandle().Id == 42);
        assert(!board->IsVisible());

        // 光と Board の値は列に置くプロパティで、値は列にだけある
        const char* lightNames[] = {"LightTypeProp", "Intensity", "IntensityUnit", "LightColor", "bCastShadows",
                                    "bLightVisible", "ShadowBias", "ShadowMapResolution", "AffectedLayers"};
        for (const char* name : lightNames)
        {
            const ClassProperty* property = point->GetClass()->GetProperty(Identity(name));
            assert(property);
            assert(property->GetStorageKind() == StorageKind::EcsColumn);
        }
        const char* boardNames[] = {"TextureHandle", "bVisible", "RenderLayerProp", "Space", "BlendModeProp", "Tint",
                                    "bFlipX", "bFlipY", "Pivot", "SizePx", "UVRectProp", "LayerPriority", "OrderInLayer"};
        for (const char* name : boardNames)
        {
            const ClassProperty* property = board->GetClass()->GetProperty(Identity(name));
            assert(property);
            assert(property->GetStorageKind() == StorageKind::EcsColumn);
        }
        // フリップブックの設定は描画の値ではないので列に置かない
        const ClassProperty* frameCount = board->GetClass()->GetProperty(Identity("FrameCount"));
        assert(frameCount && frameCount->GetStorageKind() != StorageKind::EcsColumn);

        // 列の値を直接書き換えると、Component の読み口がその値を返す
        Ecs::EcsLightRender* pointWritable = ecs.GetColumnValue<Ecs::EcsLightRender>(point->GetEcsEntity());
        pointWritable->bVisible = false;
        assert(!point->IsLightVisible());

        world.Finalize();
    }
}

int main()
{
    std::cout << "WorldSyncDifferentialTest start\n";

    TestMeshAndLightDifferentialSync();
    TestMegaGeometryDifferentialSync();
    TestMeshMaterialBlendSync();
    TestRenderValuesLiveInComponentRow();
    TestLightAndBoardValuesLiveInComponentRow();

    std::cout << "WorldSyncDifferentialTest passed\n";
    return 0;
}
