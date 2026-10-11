#include "Component/BillboardComponent.h"
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
#include "Rendering/MegaGeometryMirror.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include <cassert>
#include <cstring>
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

    BoardProxy* FindBoardProxy(SceneView& view, uint64_t componentId)
    {
        auto& proxies = const_cast<Container::VariableArray<BoardProxy>&>(view.GetBoardProxies());
        for (BoardProxy& proxy : proxies)
        {
            if (proxy.ComponentId == componentId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    MeshProxy* FindMeshProxyByComponent(SceneView& view, uint64_t componentId)
    {
        auto& proxies = const_cast<Container::VariableArray<MeshProxy>&>(view.GetMeshProxies());
        for (MeshProxy& proxy : proxies)
        {
            if (proxy.ComponentId == componentId)
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

        // 前フレームの変換を揃える公開し直しがあれば、ここで済ませる
        world.SyncToSceneView();
        world.SyncToSceneView();

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
        // 変わっていない行は公開し直さないので、proxy への書き換えは残る
        assert(GetTranslationX(meshProxy->WorldTransform) == 777.0f);
        assert(lightProxy->PositionX == 888.0f);

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(view.GetLightProxies().size() == 1);
        assert(GetTranslationX(FindMeshProxy(view, object->GetObjectId())->WorldTransform) == 777.0f);

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

    // 描画の無効化の通知（設計 1.10）: 資源の変化・履歴の落ち着き・接続先の交換が、変更の無い行の proxy 更新の理由になる
    // 旧の全走査（--ecs-legacy-sync）の検査: 変更も通知も無い行を作り直して、公開した内容と比べる
    void TestRenderInvalidationNotifications()
    {
        World world;
        world.Initialize();
        world.SetLegacyRenderSync(true);
        world.SetRenderInvalidationCheckForced(true);

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        RenderResources renderResources;
        Ecs::EcsWorld& ecs = world.GetEcsWorld();

        Entity* object = world.SpawnObject<Entity>();
        Entity* other = world.SpawnObject<Entity>();
        assert(object && other);

        const MaterialHandle translucent = MakeMaterialHandle(renderResources, BlendMode::Translucent);
        const MaterialHandle additive = MakeMaterialHandle(renderResources, BlendMode::Additive);

        MeshComponent* mesh = world.CreateComponent<MeshComponent>(object);
        MeshComponent* otherMesh = world.CreateComponent<MeshComponent>(other);
        PointLightComponent* light = world.CreateComponent<PointLightComponent>(object);
        SkinnedMeshComponent* skinned = world.CreateComponent<SkinnedMeshComponent>(object);
        assert(mesh && otherMesh && light && skinned);
        mesh->SetMeshHandle(MakeMeshHandle(500));
        mesh->SetMaterial(0, translucent);
        otherMesh->SetMeshHandle(MakeMeshHandle(501));
        otherMesh->SetMaterial(0, additive);
        skinned->SetVisible(true);

        // 公開の状態の列は描画へ出す行にだけあり、ポーズの版の列は SkinnedMesh の行にだけある
        assert(ecs.GetColumnValue<Ecs::EcsPublishState>(mesh->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsPublishState>(light->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsPublishState>(skinned->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsPoseVersion>(skinned->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsPoseVersion>(mesh->GetEcsEntity()) == nullptr);

        world.SyncToSceneView(&renderResources.Materials());
        const Ecs::EcsPublishState* meshState = ecs.GetColumnValue<Ecs::EcsPublishState>(mesh->GetEcsEntity());
        const Ecs::EcsPublishState* lightState = ecs.GetColumnValue<Ecs::EcsPublishState>(light->GetEcsEntity());
        const Ecs::EcsPublishState* skinnedState = ecs.GetColumnValue<Ecs::EcsPublishState>(skinned->GetEcsEntity());
        assert(meshState && meshState->bPublished && meshState->Version == 1);
        assert(lightState && lightState->bPublished);
        // アセットの無い SkinnedMesh は proxy を作れない。公開されないまま、再試行の一覧に入る
        assert(!skinnedState->bPublished);
        assert(world.GetRenderRetryRows().size() == 1);
        assert(world.GetRenderRetryRows()[0] == skinned->GetEcsEntity());
        assert(world.GetRenderInvalidationStats().Violations == 0);
        assert(world.GetRenderInvalidationStats().RetryMismatches == 0);

        // 資源の handle → 使っている行の逆引き: 通知した資源を使う行だけが作り直しの対象になる
        world.SyncToSceneView(&renderResources.Materials());
        assert(!mesh->IsRenderStateDirty());
        assert(!otherMesh->IsRenderStateDirty());
        world.NotifyRenderResourceChanged(RenderResourceKind::Material, translucent.Id);
        assert(mesh->IsRenderStateDirty());
        assert(!otherMesh->IsRenderStateDirty());
        world.NotifyRenderResourceChanged(RenderResourceKind::Material, 424242);
        world.NotifyRenderResourceChanged(RenderResourceKind::Mesh, 501);
        assert(otherMesh->IsRenderStateDirty());
        world.SyncToSceneView(&renderResources.Materials());
        assert(!mesh->IsRenderStateDirty());
        assert(!otherMesh->IsRenderStateDirty());
        assert(world.GetRenderInvalidationStats().Violations == 0);

        // 材質の更新は資源の記録を通って行に届き、同じ handle のままブレンドが替わった proxy になる
        MaterialCreateData blended;
        blended.Blend = BlendMode::Masked;
        assert(renderResources.Materials().Update(translucent, blended));
        assert(renderResources.ChangeLog().HasPending());
        world.SyncToSceneView(&renderResources.Materials(), nullptr);
        assert(!renderResources.ChangeLog().HasPending());
        assert(FindMeshProxy(view, object->GetObjectId()) != nullptr);
        assert(FindMeshProxy(view, object->GetObjectId())->MaterialBlendModes[0] == BlendMode::Masked);
        assert(world.GetRenderInvalidationStats().Violations == 0);

        // 通知が行に届かなければ、変更も通知も無いのに proxy が変わった行として検査が見つける
        blended.Blend = BlendMode::Additive;
        assert(renderResources.Materials().Update(translucent, blended));
        Container::VariableArray<RenderResourceRef> swallowed;
        (void)renderResources.ChangeLog().Drain(swallowed);
        assert(swallowed.size() == 1);
        world.SyncToSceneView(&renderResources.Materials());
        assert(world.GetRenderInvalidationStats().Violations == 1);
        // 公開し直した内容を覚えるので、次の同期では食い違わない
        world.SyncToSceneView(&renderResources.Materials());
        assert(world.GetRenderInvalidationStats().Violations == 1);

        // 前フレームの変換の履歴が落ち着くまでの公開し直しは通知になる（動かした直後の 2 回目の同期）
        object->SetPosition(9.0f, 0.0f, 0.0f);
        world.SyncToSceneView(&renderResources.Materials());
        world.SyncToSceneView(&renderResources.Materials());
        world.SyncToSceneView(&renderResources.Materials());
        assert(world.GetRenderInvalidationStats().Violations == 1);
        assert(world.GetRenderInvalidationStats().Probed > 0);

        // 接続先の交換は全部の行を公開し直す
        SceneView replacementView;
        assert(replacementView.Initialize(settings));
        world.SetSceneView(&replacementView);
        world.SyncToSceneView(&renderResources.Materials());
        assert(replacementView.GetMeshProxies().size() == 2);
        assert(replacementView.GetLightProxies().size() == 1);
        assert(world.GetRenderInvalidationStats().Violations == 1);

        // 再試行の一覧は、公開を望まなくなった行を外す
        skinned->SetVisible(false);
        world.SyncToSceneView(&renderResources.Materials());
        assert(world.GetRenderRetryRows().empty());
        assert(world.GetRenderInvalidationStats().RetryMismatches == 0);

        // 行が消えた後の通知は何も起こさない
        other->MarkForDestroy();
        world.Tick(0.016f);
        world.NotifyRenderResourceChanged(RenderResourceKind::Mesh, 501);
        world.SyncToSceneView(&renderResources.Materials());

        world.Finalize();
    }

    // 資源を交換した行は、交換前の資源の通知を受けない。非アクティブになった行は公開の状態の列も「公開していない」になる
    void TestRenderInvalidationStaleRefsAndInactiveRows()
    {
        World world;
        world.Initialize();
        world.SetRenderInvalidationCheckForced(true);

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        RenderResources renderResources;
        Ecs::EcsWorld& ecs = world.GetEcsWorld();

        Entity* object = world.SpawnObject<Entity>();
        assert(object);
        const MaterialHandle materialA = MakeMaterialHandle(renderResources, BlendMode::Translucent);
        const MaterialHandle materialB = MakeMaterialHandle(renderResources, BlendMode::Additive);

        MeshComponent* mesh = world.CreateComponent<MeshComponent>(object);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(600));
        mesh->SetMaterial(0, materialA);
        world.SyncToSceneView(&renderResources.Materials());
        assert(!mesh->IsRenderStateDirty());

        // 材質 A → B に交換して同期すると、A の変化は行に届かず、B の変化だけが届く
        mesh->SetMaterial(0, materialB);
        world.SyncToSceneView(&renderResources.Materials());
        assert(!mesh->IsRenderStateDirty());
        world.NotifyRenderResourceChanged(RenderResourceKind::Material, materialA.Id);
        assert(!mesh->IsRenderStateDirty());
        world.NotifyRenderResourceChanged(RenderResourceKind::Material, materialB.Id);
        assert(mesh->IsRenderStateDirty());
        world.SyncToSceneView(&renderResources.Materials());

        // メッシュの handle の交換も同じ
        mesh->SetMeshHandle(MakeMeshHandle(601));
        world.SyncToSceneView(&renderResources.Materials());
        world.NotifyRenderResourceChanged(RenderResourceKind::Mesh, 600);
        assert(!mesh->IsRenderStateDirty());
        world.NotifyRenderResourceChanged(RenderResourceKind::Mesh, 601);
        assert(mesh->IsRenderStateDirty());
        world.SyncToSceneView(&renderResources.Materials());
        assert(world.GetRenderInvalidationStats().Violations == 0);

        // 非アクティブにした行は proxy が消え、公開の状態の列も公開していないになる。戻すと公開し直す
        const Ecs::EcsPublishState* state = ecs.GetColumnValue<Ecs::EcsPublishState>(mesh->GetEcsEntity());
        assert(state && state->bPublished);
        object->SetActive(false);
        world.SyncToSceneView(&renderResources.Materials());
        assert(FindMeshProxy(view, object->GetObjectId()) == nullptr);
        assert(!state->bPublished);
        object->SetActive(true);
        world.SyncToSceneView(&renderResources.Materials());
        assert(FindMeshProxy(view, object->GetObjectId()) != nullptr);
        assert(state->bPublished);
        assert(world.GetRenderInvalidationStats().Violations == 0);

        world.Finalize();
    }

    // 段 RenderSync の一覧（設計 1.10・準備）: 変更ログ・通知・公開の状態から作った「変わった行」と Remove が、全走査と一致する
    void TestRenderSyncListMatchesFullScan()
    {
        using Kind = Ecs::EcsRenderSyncKind;
        World world;
        world.Initialize();
        world.SetLegacyRenderSync(true);
        world.SetRenderSyncCheckForced(true);

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        RenderResources renderResources;
        Entity* parent = world.SpawnObject<Entity>();
        Entity* child = world.SpawnEntity<Entity>(parent);
        Entity* other = world.SpawnObject<Entity>();
        assert(parent && child && other);

        MeshComponent* parentMesh = world.CreateComponent<MeshComponent>(parent);
        MeshComponent* childMesh = world.CreateComponent<MeshComponent>(child);
        PointLightComponent* childLight = world.CreateComponent<PointLightComponent>(child);
        MeshComponent* otherMesh = world.CreateComponent<MeshComponent>(other);
        assert(parentMesh && childMesh && childLight && otherMesh);
        parentMesh->SetMeshHandle(MakeMeshHandle(700));
        childMesh->SetMeshHandle(MakeMeshHandle(701));
        otherMesh->SetMeshHandle(MakeMeshHandle(702));

        const auto keyOf = [](Kind kind, uint64_t id) { return Ecs::EcsRenderSyncKey(kind, id); };
        const auto syncOnce = [&]() { world.SyncToSceneView(&renderResources.Materials()); };

        // 最初の同期: 付けたばかりの行はすべて変わった行
        syncOnce();
        assert(world.GetRenderSyncStats().Checked == 1);
        assert(world.GetRenderSyncStats().Violations == 0);
        const Ecs::EcsRenderSyncList& list = world.GetRenderSyncList();
        assert(list.Changed.count(keyOf(Kind::Mesh, parentMesh->GetComponentId())) == 1);
        assert(list.Changed.count(keyOf(Kind::Mesh, childMesh->GetComponentId())) == 1);
        assert(list.Changed.count(keyOf(Kind::Light, childLight->GetComponentId())) == 1);
        assert(list.Removed.empty());

        // 静止: 履歴の落ち着きの 2 回目の公開を除けば、変わった行は無い
        syncOnce();
        syncOnce();
        syncOnce();
        assert(list.Changed.empty());
        assert(list.Removed.empty());

        // 移動: 動かした Entity と子孫の行が変わった行になり、動かさない行は載らない
        parent->SetPosition(3.0f, 0.0f, 0.0f);
        syncOnce();
        assert(list.Changed.count(keyOf(Kind::Mesh, parentMesh->GetComponentId())) == 1);
        assert(list.Changed.count(keyOf(Kind::Mesh, childMesh->GetComponentId())) == 1);
        assert(list.Changed.count(keyOf(Kind::Light, childLight->GetComponentId())) == 1);
        assert(list.Changed.count(keyOf(Kind::Mesh, otherMesh->GetComponentId())) == 0);

        // 非表示: 値を書いた行だけが変わった行になる。取り下げは作り直した結果で、Remove には載らない
        syncOnce();
        syncOnce();
        otherMesh->SetVisible(false);
        syncOnce();
        assert(list.Changed.count(keyOf(Kind::Mesh, otherMesh->GetComponentId())) == 1);
        assert(list.Changed.size() == 1);
        assert(list.Removed.empty());
        assert(FindMeshProxy(view, other->GetObjectId()) == nullptr);

        // 部分木を非アクティブにする: 公開していた行は Remove になり、次の同期では載らない
        syncOnce();
        parent->SetActive(false);
        syncOnce();
        assert(list.Removed.count(keyOf(Kind::Mesh, parentMesh->GetComponentId())) == 1);
        assert(list.Removed.count(keyOf(Kind::Mesh, childMesh->GetComponentId())) == 1);
        assert(list.Removed.count(keyOf(Kind::Light, childLight->GetComponentId())) == 1);
        syncOnce();
        assert(list.Removed.empty());
        parent->SetActive(true);
        syncOnce();
        assert(list.Changed.count(keyOf(Kind::Mesh, childMesh->GetComponentId())) == 1);

        // 破棄: 行の削除のフックが Remove を積む
        syncOnce();
        syncOnce();
        const uint64_t destroyedMeshId = childMesh->GetComponentId();
        assert(FindMeshProxy(view, child->GetObjectId()) != nullptr);
        child->MarkForDestroy();
        world.Tick(0.016f);
        syncOnce();
        assert(list.Removed.count(keyOf(Kind::Mesh, destroyedMeshId)) == 1);
        assert(FindMeshProxy(view, child->GetObjectId()) == nullptr);
        assert(world.GetRenderSyncStats().Violations == 0);

        // 変更ログに載らない変化（公開した版だけが古くなった行）は、検査が見つける
        const uint32_t violationsBefore = world.GetRenderSyncStats().Violations;
        parentMesh->SetLastSyncedTransformVersion(0);
        syncOnce();
        assert(world.GetRenderSyncStats().Violations == violationsBefore + 1);
        syncOnce();
        assert(world.GetRenderSyncStats().Violations == violationsBefore + 1);

        world.Finalize();
    }

    // 一覧と全走査の照合は、欠け・余り・説明のない取り下げ・Remove なのに生存、をそれぞれ違反にする
    void TestRenderSyncCompareDetectsEachMismatch()
    {
        using Kind = Ecs::EcsRenderSyncKind;
        const auto keyOf = [](Kind kind, uint64_t id) { return Ecs::EcsRenderSyncKey(kind, id); };

        Ecs::EcsRenderSyncList list;
        Container::UnorderedSet<uint64_t> scanChanged;
        Container::UnorderedSet<uint64_t> scanStale;
        Container::UnorderedSet<uint64_t> liveAfter;

        list.Changed.insert(keyOf(Kind::Mesh, 1));
        scanChanged.insert(keyOf(Kind::Mesh, 1));
        // 板は変わった行（Board）が、World・画面空間どちらの取り下げも説明する
        list.Changed.insert(keyOf(Kind::Board, 5));
        scanChanged.insert(keyOf(Kind::Board, 5));
        scanStale.insert(keyOf(Kind::WorldBoard, 5));
        list.Removed.insert(keyOf(Kind::Light, 9));
        scanStale.insert(keyOf(Kind::Light, 9));
        assert(Ecs::EcsRenderSyncCompare(list, scanChanged, scanStale, liveAfter).Violations() == 0);

        scanChanged.insert(keyOf(Kind::Mesh, 2));
        Ecs::EcsRenderSyncReport report = Ecs::EcsRenderSyncCompare(list, scanChanged, scanStale, liveAfter);
        assert(report.ChangedMissing == 1 && report.Violations() == 1);
        scanChanged.erase(keyOf(Kind::Mesh, 2));

        list.Changed.insert(keyOf(Kind::Mesh, 3));
        report = Ecs::EcsRenderSyncCompare(list, scanChanged, scanStale, liveAfter);
        assert(report.ChangedExtra == 1 && report.Violations() == 1);
        list.Changed.erase(keyOf(Kind::Mesh, 3));

        scanStale.insert(keyOf(Kind::Mesh, 4));
        report = Ecs::EcsRenderSyncCompare(list, scanChanged, scanStale, liveAfter);
        assert(report.RemovedUnexplained == 1 && report.Violations() == 1);
        scanStale.erase(keyOf(Kind::Mesh, 4));

        liveAfter.insert(keyOf(Kind::Light, 9));
        report = Ecs::EcsRenderSyncCompare(list, scanChanged, scanStale, liveAfter);
        assert(report.RemovedStillLive == 1 && report.Violations() == 1);
        assert(report.FirstDetail[0] != '\0');
        liveAfter.clear();

        // 種類が違う鍵は別物（Mesh の 1 と Light の 1 を取り違えない）
        scanStale.insert(keyOf(Kind::Light, 1));
        report = Ecs::EcsRenderSyncCompare(list, scanChanged, scanStale, liveAfter);
        assert(report.RemovedUnexplained == 1);
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

    // ライト方向と Billboard の大きさは、setter → 列 → getter・proxy の順に値が通り、行の付け外しで値が保たれる
    void TestLightDirectionAndBillboardSizeLiveInComponentRow()
    {
        World world;
        world.Initialize();

        Entity* object = world.SpawnObject<Entity>();
        assert(object);
        DirectionalLightComponent* light = world.CreateComponent<DirectionalLightComponent>(object);
        BillboardComponent* billboard = world.CreateComponent<BillboardComponent>(object);
        BoardComponent* board = world.CreateComponent<BoardComponent>(object);
        assert(light && billboard && board);

        Ecs::EcsWorld& ecs = world.GetEcsWorld();
        // 大きさの列は Billboard の行だけにある
        assert(ecs.GetColumnValue<Ecs::EcsBillboardSize>(billboard->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsBoardRender>(billboard->GetEcsEntity()) != nullptr);
        assert(ecs.GetColumnValue<Ecs::EcsBillboardSize>(board->GetEcsEntity()) == nullptr);

        // 既定値が列にある（方向は下向き、大きさは 1 x 1）
        const Ecs::EcsLightRender* lightColumn = ecs.GetColumnValue<Ecs::EcsLightRender>(light->GetEcsEntity());
        assert(lightColumn->Direction.x == 0.0f && lightColumn->Direction.y == -1.0f && lightColumn->Direction.z == 0.0f);
        const Ecs::EcsBillboardSize* sizeColumn = ecs.GetColumnValue<Ecs::EcsBillboardSize>(billboard->GetEcsEntity());
        assert(sizeColumn->SizeWorld.x == 1.0f && sizeColumn->SizeWorld.y == 1.0f);

        // setter → 列
        light->SetLightDirection(0.25f, -0.5f, 0.75f);
        assert(lightColumn->Direction.x == 0.25f && lightColumn->Direction.y == -0.5f && lightColumn->Direction.z == 0.75f);
        billboard->SetSizeWorld(NorvesLib::Math::Vector2(2.0f, 3.0f));
        assert(sizeColumn->SizeWorld.x == 2.0f && sizeColumn->SizeWorld.y == 3.0f);

        // 列 → getter・proxy
        Ecs::EcsLightRender* lightWritable = ecs.GetColumnValue<Ecs::EcsLightRender>(light->GetEcsEntity());
        lightWritable->Direction = NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f);
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        light->GetLightDirection(x, y, z);
        assert(x == 1.0f && y == 0.0f && z == 0.0f);
        LightProxy lightProxy;
        assert(light->BuildLightProxy(lightProxy));
        assert(lightProxy.DirectionX == 1.0f && lightProxy.DirectionY == 0.0f && lightProxy.DirectionZ == 0.0f);

        Ecs::EcsBillboardSize* sizeWritable = ecs.GetColumnValue<Ecs::EcsBillboardSize>(billboard->GetEcsEntity());
        sizeWritable->SizeWorld = NorvesLib::Math::Vector2(4.0f, 5.0f);
        assert(billboard->GetSizeWorld().x == 4.0f && billboard->GetSizeWorld().y == 5.0f);
        BoardProxy boardProxy;
        assert(billboard->BuildBoardProxy(boardProxy));
        assert(boardProxy.SizeWorld.x == 4.0f && boardProxy.SizeWorld.y == 5.0f);

        // 大きさは保存の対象のプロパティで列にある。方向は列の一部だが保存の対象ではない
        const ClassProperty* sizeProperty = billboard->GetClass()->GetProperty(Identity("SizeWorld"));
        assert(sizeProperty && sizeProperty->GetStorageKind() == StorageKind::EcsColumn);

        // 外すと予備の値へ戻り、読み口は同じ値を返す。付け直すと列へ戻る
        light->SetLightDirection(0.0f, 0.0f, 1.0f);
        billboard->SetSizeWorld(NorvesLib::Math::Vector2(6.0f, 7.0f));
        assert(object->RemoveInner(light));
        assert(object->RemoveInner(billboard));
        assert(light->GetEcsEntity().IsNull() && billboard->GetEcsEntity().IsNull());
        light->GetLightDirection(x, y, z);
        assert(x == 0.0f && y == 0.0f && z == 1.0f);
        assert(billboard->GetSizeWorld().x == 6.0f && billboard->GetSizeWorld().y == 7.0f);
        // 行が無い間の書き込みも予備の値へ入り、付け直しで列へ写る
        light->SetLightDirection(0.0f, 1.0f, 0.0f);
        billboard->SetSizeWorld(NorvesLib::Math::Vector2(8.0f, 9.0f));
        assert(object->AddInner(light));
        assert(object->AddInner(billboard));
        const Ecs::EcsLightRender* reboundLight = ecs.GetColumnValue<Ecs::EcsLightRender>(light->GetEcsEntity());
        assert(reboundLight && reboundLight->Direction.x == 0.0f && reboundLight->Direction.y == 1.0f &&
               reboundLight->Direction.z == 0.0f);
        const Ecs::EcsBillboardSize* reboundSize = ecs.GetColumnValue<Ecs::EcsBillboardSize>(billboard->GetEcsEntity());
        assert(reboundSize && reboundSize->SizeWorld.x == 8.0f && reboundSize->SizeWorld.y == 9.0f);

        world.Finalize();
    }
}

namespace
{
    // 既定の同期（変わった行だけ）: 変わっていない行は公開し直さず、構造の変化は Remove の一覧で外れ、
    // 全行から求め直した proxy と食い違わない（検査 ECS_RENDER_SYNC の違反が 0）
    void TestDeltaSyncPublishesOnlyChangedRows()
    {
        using Kind = Ecs::EcsRenderSyncKind;
        World world;
        world.Initialize();
        world.SetRenderSyncCheckForced(true);
        assert(!world.IsLegacyRenderSync());

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        Entity* parent = world.SpawnObject<Entity>();
        Entity* child = world.SpawnEntity<Entity>(parent);
        Entity* other = world.SpawnObject<Entity>();
        assert(parent && child && other);
        MeshComponent* parentMesh = world.CreateComponent<MeshComponent>(parent);
        MeshComponent* childMesh = world.CreateComponent<MeshComponent>(child);
        PointLightComponent* childLight = world.CreateComponent<PointLightComponent>(child);
        MeshComponent* otherMesh = world.CreateComponent<MeshComponent>(other);
        BoardComponent* otherBoard = world.CreateComponent<BoardComponent>(other);
        assert(parentMesh && childMesh && childLight && otherMesh && otherBoard);
        parentMesh->SetMeshHandle(MakeMeshHandle(800));
        childMesh->SetMeshHandle(MakeMeshHandle(801));
        otherMesh->SetMeshHandle(MakeMeshHandle(802));
        otherBoard->SetTextureHandle(TextureHandle{42});
        otherBoard->SetBoardSpace(BoardSpace::WorldSpace);

        const auto keyOf = [](Kind kind, uint64_t id) { return Ecs::EcsRenderSyncKey(kind, id); };
        const Ecs::EcsRenderSyncList& list = world.GetRenderSyncList();

        // 最初の同期ですべて公開する。履歴が落ち着く公開し直しを済ませる
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 3);
        assert(view.GetLightProxies().size() == 1);
        assert(view.GetBoardProxies().size() == 1);
        world.SyncToSceneView();
        world.SyncToSceneView();
        assert(list.Changed.empty());
        assert(list.Removed.empty());
        assert(world.GetRenderSyncStats().Checked == 3);
        assert(world.GetRenderSyncStats().Violations == 0);

        // 静止: 変わっていない行は公開し直さないので、proxy への書き換えは残る
        MeshProxy* otherProxy = FindMeshProxy(view, other->GetObjectId());
        assert(otherProxy);
        SetTranslationX(otherProxy->WorldTransform, 555.0f);
        BoardProxy* boardProxy = FindBoardProxy(view, otherBoard->GetComponentId());
        assert(boardProxy);
        boardProxy->SizeWorld.x = 321.0f;
        world.SyncToSceneView();
        world.SyncToSceneView();
        assert(GetTranslationX(FindMeshProxy(view, other->GetObjectId())->WorldTransform) == 555.0f);
        assert(FindBoardProxy(view, otherBoard->GetComponentId())->SizeWorld.x == 321.0f);
        assert(list.Changed.empty());

        // 移動: 動かした Entity と子孫の行だけが公開し直される
        parent->SetPosition(4.0f, 0.0f, 0.0f);
        world.SyncToSceneView();
        assert(list.Changed.size() == 3);
        assert(list.Changed.count(keyOf(Kind::Mesh, parentMesh->GetComponentId())) == 1);
        assert(list.Changed.count(keyOf(Kind::Mesh, childMesh->GetComponentId())) == 1);
        assert(list.Changed.count(keyOf(Kind::Light, childLight->GetComponentId())) == 1);
        assert(GetTranslationX(FindMeshProxy(view, parent->GetObjectId())->WorldTransform) == 4.0f);
        assert(GetTranslationX(FindMeshProxy(view, other->GetObjectId())->WorldTransform) == 555.0f);
        // 前フレームの変換は動く前の位置。次の同期の公開し直しで現在に揃い、その後は何も公開しない
        MeshProxy* movedProxy = FindMeshProxyByComponent(view, parentMesh->GetComponentId());
        assert(movedProxy && GetTranslationX(movedProxy->PreviousWorldTransform) == 0.0f);
        world.SyncToSceneView();
        movedProxy = FindMeshProxyByComponent(view, parentMesh->GetComponentId());
        assert(GetTranslationX(movedProxy->PreviousWorldTransform) == 4.0f);
        assert(list.Changed.empty());
        world.SyncToSceneView();
        assert(list.Changed.empty());

        // 値の書き込み: 書いた行だけ
        childMesh->SetVisible(false);
        world.SyncToSceneView();
        assert(list.Changed.size() == 1 && list.Changed.count(keyOf(Kind::Mesh, childMesh->GetComponentId())) == 1);
        assert(view.GetMeshProxies().size() == 2);
        childMesh->SetVisible(true);
        otherBoard->SetVisible(false);
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 3);
        assert(view.GetBoardProxies().empty());
        otherBoard->SetVisible(true);
        world.SyncToSceneView();
        assert(view.GetBoardProxies().size() == 1);

        // 部分木を非アクティブにする: Remove の一覧で外れ、公開の状態も戻る
        world.SyncToSceneView();
        parent->SetActive(false);
        world.SyncToSceneView();
        assert(list.Removed.count(keyOf(Kind::Mesh, parentMesh->GetComponentId())) == 1);
        assert(list.Removed.count(keyOf(Kind::Mesh, childMesh->GetComponentId())) == 1);
        assert(list.Removed.count(keyOf(Kind::Light, childLight->GetComponentId())) == 1);
        assert(view.GetMeshProxies().size() == 1);
        assert(view.GetLightProxies().empty());
        const Ecs::EcsPublishState* parentState =
            world.GetEcsWorld().GetColumnValue<Ecs::EcsPublishState>(parentMesh->GetEcsEntity());
        assert(parentState && !parentState->bPublished);
        world.SyncToSceneView();
        assert(list.Removed.empty());
        parent->SetActive(true);
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 3);
        assert(view.GetLightProxies().size() == 1);

        // 破棄予約: 回収の前から proxy は外れる。回収後も食い違わない
        world.SyncToSceneView();
        parent->MarkForDestroy();
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(view.GetLightProxies().empty());
        world.Tick(0.016f);
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);

        // 単独の Component の破棄
        otherBoard->MarkForDestroy();
        world.Tick(0.016f);
        world.SyncToSceneView();
        assert(view.GetBoardProxies().empty());

        assert(world.GetRenderSyncStats().Violations == 0);
        assert(world.GetRenderSyncStats().Examined > 0);

        world.Finalize();
    }

    // 通知が行に届かなければ、変わった行だけの同期は古い proxy を残す。全行から求め直す検査がそれを見つける
    void TestDeltaSyncVerifyDetectsMissedNotification()
    {
        World world;
        world.Initialize();
        world.SetRenderSyncCheckForced(true);

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        RenderResources renderResources;
        Entity* object = world.SpawnObject<Entity>();
        assert(object);
        const MaterialHandle material = MakeMaterialHandle(renderResources, BlendMode::Translucent);
        MeshComponent* mesh = world.CreateComponent<MeshComponent>(object);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(810));
        mesh->SetMaterial(0, material);

        world.SyncToSceneView(&renderResources.Materials());
        world.SyncToSceneView(&renderResources.Materials());
        world.SyncToSceneView(&renderResources.Materials());
        assert(world.GetRenderSyncStats().Violations == 0);

        // 届いた通知は公開し直しになり、違反にならない
        MaterialCreateData blended;
        blended.Blend = BlendMode::Masked;
        assert(renderResources.Materials().Update(material, blended));
        world.SyncToSceneView(&renderResources.Materials());
        assert(FindMeshProxy(view, object->GetObjectId())->MaterialBlendModes[0] == BlendMode::Masked);
        assert(world.GetRenderSyncStats().Violations == 0);

        // 届かない通知は、公開した内容と求め直した内容の食い違いになる
        blended.Blend = BlendMode::Additive;
        assert(renderResources.Materials().Update(material, blended));
        Container::VariableArray<RenderResourceRef> swallowed;
        (void)renderResources.ChangeLog().Drain(swallowed);
        world.SyncToSceneView(&renderResources.Materials());
        assert(FindMeshProxy(view, object->GetObjectId())->MaterialBlendModes[0] == BlendMode::Masked);
        assert(world.GetRenderSyncStats().Violations >= 1);

        // 行が無いのに残った proxy も見つける
        const uint32_t before = world.GetRenderSyncStats().Violations;
        MeshProxy orphan = *FindMeshProxy(view, object->GetObjectId());
        orphan.ComponentId = 987654321;
        orphan.ObjectId = 987654321;
        view.AddMeshProxy(orphan);
        world.SyncToSceneView(&renderResources.Materials());
        assert(world.GetRenderSyncStats().Violations > before);

        world.Finalize();
    }

    // --ecs-legacy-sync 相当の切り替え: 旧の全走査は変わっていない Mesh も公開し直し、既定の同期は公開し直さない。実行中に往復できる
    void TestLegacyRenderSyncSwitch()
    {
        World world;
        world.Initialize();
        world.SetRenderSyncCheckForced(true);
        world.SetLegacyRenderSync(true);
        assert(world.IsLegacyRenderSync());

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        Entity* object = world.SpawnObject<Entity>();
        assert(object);
        MeshComponent* mesh = world.CreateComponent<MeshComponent>(object);
        PointLightComponent* light = world.CreateComponent<PointLightComponent>(object);
        assert(mesh && light);
        mesh->SetMeshHandle(MakeMeshHandle(820));

        world.SyncToSceneView();
        world.SyncToSceneView();
        world.SyncToSceneView();
        SetTranslationX(FindMeshProxy(view, object->GetObjectId())->WorldTransform, 777.0f);
        world.SyncToSceneView();
        assert(GetTranslationX(FindMeshProxy(view, object->GetObjectId())->WorldTransform) == 0.0f);

        world.SetLegacyRenderSync(false);
        world.SyncToSceneView();
        world.SyncToSceneView();
        SetTranslationX(FindMeshProxy(view, object->GetObjectId())->WorldTransform, 777.0f);
        world.SyncToSceneView();
        assert(GetTranslationX(FindMeshProxy(view, object->GetObjectId())->WorldTransform) == 777.0f);
        object->SetPosition(2.0f, 0.0f, 0.0f);
        world.SyncToSceneView();
        assert(GetTranslationX(FindMeshProxy(view, object->GetObjectId())->WorldTransform) == 2.0f);
        assert(FindLightProxy(view, light->GetComponentId())->PositionX == 2.0f);
        assert(world.GetRenderSyncStats().Violations == 0);

        world.SetLegacyRenderSync(true);
        world.SyncToSceneView();
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(world.GetRenderSyncStats().Violations == 0);

        world.Finalize();
    }

    // 非表示の SkinnedMesh は、毎フレーム「変わった行」にならない
    void TestInvisibleSkinnedMeshIsNotChangedEveryFrame()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        Entity* object = world.SpawnObject<Entity>();
        assert(object);
        SkinnedMeshComponent* skinned = world.CreateComponent<SkinnedMeshComponent>(object);
        assert(skinned);
        skinned->SetVisible(false);

        world.SyncToSceneView();
        world.SyncToSceneView();
        world.SyncToSceneView();
        assert(world.GetRenderSyncList().Changed.empty());
        assert(view.GetSkinnedMeshProxies().empty());

        world.Finalize();
    }

    // RenderThread の写しの消費者（MegaGeometryPass など）の模型。前に見た版から、変わった席だけで直せるときは直し、
    // 直せないときは写しの全部を取り直す。どちらでも、写しの配列と同じ内容になるはず
    struct MegaMirrorConsumer
    {
        Container::VariableArray<MegaGeometryProxy> Copy;
        uint64_t Revision = 0;
        uint32_t Patched = 0;
        uint32_t Rebuilt = 0;

        void Sync(const MegaGeometryMirror& mirror)
        {
            if (mirror.GetRevision() == Revision)
            {
                return;
            }
            bool bPatched = false;
            if (mirror.CanPatchFrom(Revision) && Copy.size() == mirror.GetProxies().size())
            {
                bPatched = true;
                for (const uint32_t slot : mirror.GetChangedSlots())
                {
                    uint32_t position = 0;
                    if (!mirror.TryGetPositionBySlot(slot, position))
                    {
                        bPatched = false;
                        break;
                    }
                    Copy[position] = mirror.GetProxies()[position];
                }
            }
            if (bPatched)
            {
                ++Patched;
            }
            else
            {
                Copy = mirror.GetProxies();
                ++Rebuilt;
            }
            Revision = mirror.GetRevision();
        }

        bool Matches(const MegaGeometryMirror& mirror) const
        {
            const auto& source = mirror.GetProxies();
            if (Copy.size() != source.size())
            {
                return false;
            }
            for (size_t index = 0; index < source.size(); ++index)
            {
                const MegaGeometryProxy& a = Copy[index];
                const MegaGeometryProxy& b = source[index];
                if (a.ObjectId != b.ObjectId || a.SceneSlot != b.SceneSlot || a.bCastShadow != b.bCastShadow ||
                    std::memcmp(&a.WorldTransform, &b.WorldTransform, sizeof(a.WorldTransform)) != 0 ||
                    std::memcmp(&a.PreviousWorldTransform, &b.PreviousWorldTransform,
                                sizeof(a.PreviousWorldTransform)) != 0)
                {
                    return false;
                }
            }
            return true;
        }
    };

    MegaGeometryProxy MakeMirrorProxy(uint64_t objectId, float x, float previousX, bool bCastShadow)
    {
        MegaGeometryProxy proxy;
        proxy.ObjectId = objectId;
        proxy.SceneSlot = static_cast<uint32_t>(objectId - 99);
        proxy.bCastShadow = bCastShadow;
        SetTranslationX(proxy.WorldTransform, x);
        SetTranslationX(proxy.PreviousWorldTransform, previousX);
        return proxy;
    }

    MegaSceneDelta MakeMirrorDelta(bool bBaseline, uint32_t size)
    {
        MegaSceneDelta delta;
        delta.bActive = true;
        delta.bBaseline = bBaseline;
        delta.Size = size;
        return delta;
    }

    void PushMirrorEntry(MegaSceneDelta& delta, uint32_t index, const MegaGeometryProxy& proxy)
    {
        delta.Indices.push_back(index);
        delta.Proxies.push_back(proxy);
    }

    // 描画を見送った差分を挟んだあとの空の差分でも、変わった席を取り逃がさない（消費者が前に見た版から数える）
    void TestMegaMirrorKeepsChangesAcrossSkippedDraw()
    {
        constexpr uint64_t generation = 3;
        constexpr uint64_t objectA = 100, objectB = 101;
        MegaGeometryMirror mirror;
        MegaMirrorConsumer consumer;

        // 影ありで描く
        MegaSceneDelta delta = MakeMirrorDelta(true, 2);
        PushMirrorEntry(delta, 0, MakeMirrorProxy(objectA, 0.0f, 0.0f, true));
        PushMirrorEntry(delta, 1, MakeMirrorProxy(objectB, 5.0f, 5.0f, true));
        assert(mirror.Apply(delta, generation));
        mirror.NoteDrawn(false);
        consumer.Sync(mirror);
        assert(consumer.Matches(mirror));

        // 影なしの差分を当てて見送り、空の差分で描く
        delta = MakeMirrorDelta(false, 2);
        PushMirrorEntry(delta, 0, MakeMirrorProxy(objectA, 0.0f, 0.0f, false));
        assert(mirror.Apply(delta, generation));
        assert(mirror.Apply(MakeMirrorDelta(false, 2), generation));
        mirror.NoteDrawn(false);
        consumer.Sync(mirror);
        assert(!consumer.Copy[0].bCastShadow);
        assert(consumer.Matches(mirror));
        // 変わった席だけで直せた（全部の取り直しに落ちていない）
        assert(consumer.Patched == 1 && consumer.Rebuilt == 1);

        // 取り込んだあとの空の差分は、何も変えない
        assert(mirror.Apply(MakeMirrorDelta(false, 2), generation));
        mirror.NoteDrawn(false);
        assert(mirror.GetChangedSlots().empty() && !mirror.AreAllChanged());
        consumer.Sync(mirror);
        assert(consumer.Patched == 1 && consumer.Rebuilt == 1 && consumer.Matches(mirror));

        // 基準を当てて見送り、空の差分で描く: 全部が変わったまま残る
        delta = MakeMirrorDelta(true, 2);
        PushMirrorEntry(delta, 0, MakeMirrorProxy(objectA, 1.0f, 1.0f, true));
        PushMirrorEntry(delta, 1, MakeMirrorProxy(objectB, 5.0f, 5.0f, true));
        assert(mirror.Apply(delta, generation + 1));
        assert(mirror.Apply(MakeMirrorDelta(false, 2), generation + 1));
        mirror.NoteDrawn(false);
        assert(mirror.AreAllChanged() && !mirror.CanPatchFrom(consumer.Revision));
        consumer.Sync(mirror);
        assert(consumer.Rebuilt == 2 && consumer.Matches(mirror));

        // 版が離れた消費者（途中の版を見ていない）は、変わった席で直さず取り直す
        delta = MakeMirrorDelta(false, 2);
        PushMirrorEntry(delta, 1, MakeMirrorProxy(objectB, 9.0f, 5.0f, true));
        assert(mirror.Apply(delta, generation + 1));
        mirror.NoteDrawn(false);
        MegaMirrorConsumer late;
        late.Revision = consumer.Revision - 1;
        late.Copy = consumer.Copy;
        late.Sync(mirror);
        assert(late.Rebuilt == 1 && late.Patched == 0 && late.Matches(mirror));
    }

    // 描画を見送ったあとの付け替えは、描いたフレームだけ。次のフレームで履歴が落ち着く席は消費者へ知らせる
    void TestMegaMirrorRebaseLastsOnlyOneFrame()
    {
        constexpr uint64_t generation = 5;
        constexpr uint64_t objectA = 100, objectB = 101;
        MegaGeometryMirror mirror;
        MegaMirrorConsumer consumer;
        const auto previousX = [&](uint32_t index)
        {
            return GetTranslationX(mirror.GetProxies()[index].PreviousWorldTransform);
        };

        // フレーム 10: A=0 を描く
        MegaSceneDelta delta = MakeMirrorDelta(true, 2);
        PushMirrorEntry(delta, 0, MakeMirrorProxy(objectA, 0.0f, 0.0f, true));
        PushMirrorEntry(delta, 1, MakeMirrorProxy(objectB, 50.0f, 50.0f, true));
        assert(mirror.Apply(delta, generation));
        mirror.NoteDrawn(false);
        consumer.Sync(mirror);

        // フレーム 11: A=10 を当てて見送る
        delta = MakeMirrorDelta(false, 2);
        PushMirrorEntry(delta, 0, MakeMirrorProxy(objectA, 10.0f, 0.0f, true));
        assert(mirror.Apply(delta, generation));

        // フレーム 12: 静止した A=10（GameThread の前の変換は 10）を描く。フレームが飛んだので、前の変換は 10 で描いた 0
        delta = MakeMirrorDelta(false, 2);
        PushMirrorEntry(delta, 0, MakeMirrorProxy(objectA, 10.0f, 10.0f, true));
        assert(mirror.Apply(delta, generation));
        mirror.NoteDrawn(true);
        consumer.Sync(mirror);
        assert(previousX(0) == 0.0f && previousX(1) == 50.0f);
        assert(consumer.Matches(mirror));

        // フレーム 13: 空の差分で描く。付け替えは 12 だけなので、前の変換は GameThread の 10 に戻り、消費者へ知らせる
        assert(mirror.Apply(MakeMirrorDelta(false, 2), generation));
        mirror.NoteDrawn(false);
        assert(previousX(0) == 10.0f);
        assert(mirror.CanPatchFrom(consumer.Revision) && !mirror.GetChangedSlots().empty());
        const uint32_t rebuiltBefore = consumer.Rebuilt;
        consumer.Sync(mirror);
        assert(GetTranslationX(consumer.Copy[0].PreviousWorldTransform) == 10.0f);
        assert(consumer.Rebuilt == rebuiltBefore && consumer.Matches(mirror));

        // フレーム 14: また飛んで描く。静止した A は 12 で描いた位置 10 のまま（落ち着いた席は付け替えない）
        assert(mirror.Apply(MakeMirrorDelta(false, 2), generation));
        mirror.NoteDrawn(true);
        consumer.Sync(mirror);
        assert(previousX(0) == 10.0f && previousX(1) == 50.0f);
        assert(consumer.Matches(mirror));

        // 付け替えた席が次の差分で書き換わるときは、差分の値が勝つ（B を見送って動かし、飛んで描く → 次の差分で動かす）
        delta = MakeMirrorDelta(false, 2);
        PushMirrorEntry(delta, 1, MakeMirrorProxy(objectB, 60.0f, 50.0f, true));
        assert(mirror.Apply(delta, generation));
        delta = MakeMirrorDelta(false, 2);
        PushMirrorEntry(delta, 1, MakeMirrorProxy(objectB, 70.0f, 60.0f, true));
        assert(mirror.Apply(delta, generation));
        mirror.NoteDrawn(true);
        consumer.Sync(mirror);
        assert(previousX(1) == 50.0f && consumer.Matches(mirror));
        delta = MakeMirrorDelta(false, 2);
        PushMirrorEntry(delta, 1, MakeMirrorProxy(objectB, 80.0f, 70.0f, true));
        assert(mirror.Apply(delta, generation));
        mirror.NoteDrawn(false);
        consumer.Sync(mirror);
        assert(previousX(1) == 70.0f && consumer.Matches(mirror));
    }
}


int main()
{
    std::cout << "WorldSyncDifferentialTest start\n";

    TestMeshAndLightDifferentialSync();
    TestMegaGeometryDifferentialSync();
    TestMeshMaterialBlendSync();
    TestRenderValuesLiveInComponentRow();
    TestRenderInvalidationNotifications();
    TestRenderInvalidationStaleRefsAndInactiveRows();
    TestRenderSyncListMatchesFullScan();
    TestRenderSyncCompareDetectsEachMismatch();
    TestLightAndBoardValuesLiveInComponentRow();
    TestLightDirectionAndBillboardSizeLiveInComponentRow();
    TestDeltaSyncPublishesOnlyChangedRows();
    TestDeltaSyncVerifyDetectsMissedNotification();
    TestLegacyRenderSyncSwitch();
    TestInvisibleSkinnedMeshIsNotChangedEveryFrame();
    TestMegaMirrorKeepsChangesAcrossSkippedDraw();
    TestMegaMirrorRebaseLastsOnlyOneFrame();

    std::cout << "WorldSyncDifferentialTest passed\n";
    return 0;
}
