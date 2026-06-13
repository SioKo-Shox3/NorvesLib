#include "Object/World.h"
#include "Object/WorldObject.h"
#include "Component/MeshComponent.h"
#include "Component/MegaGeometryComponent.h"
#include "Component/LightComponent.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneProxy.h"
#include "Logging/LogMacros.h"
#include "Container/UnorderedSet.h"

namespace NorvesLib::Core
{
    IMPLEMENT_CLASS(World, Object)

    namespace
    {
        bool DestroyContextOwnedInner(IUnknown &owner, IUnknown *inner)
        {
            if (!inner)
            {
                return false;
            }

            IUnknown *outer = inner->GetOuter();
            if (outer == &owner)
            {
                if (!owner.RemoveInner(inner))
                {
                    return false;
                }
            }
            else if (outer)
            {
                return false;
            }
            else
            {
                owner.RemoveInner(inner);
            }

            if (inner->HasFlag(OF_HeapOwned))
            {
                inner->SetFlag(OF_PendingDestroy, true);
                return true;
            }

            inner->Finalize();
            delete inner;
            return true;
        }
    }

    World::World()
        : Object()
    {
        NextObjectId = 1;
    }

    World::World(const FieldInitializer *initializer)
        : Object(initializer)
    {
        NextObjectId = 1;
    }

    World::World(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        NextObjectId = 1;
    }

    World::~World()
    {
        Finalize();
    }

    void World::Initialize()
    {
        if (HasFlag(OF_Initialized))
        {
            return;
        }

        LOG_INFO("World::Initialize()");

        NextObjectId = 1;
        m_SceneView = nullptr;
        m_bForceFullProxySync = false;

        Object::Initialize();
    }

    void World::Finalize()
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        LOG_INFO("World::Finalize() - Destroying %llu objects",
                 static_cast<uint64_t>(GetObjectCount()));

        while (!m_Inners.empty())
        {
            IUnknown *inner = m_Inners.back();
            if (auto *obj = CastTo<WorldObject>(inner))
            {
                obj->OnRemovedFromWorld();
            }
            if (!DestroyContextOwnedInner(*this, inner))
            {
                RemoveInner(inner);
            }
        }

        m_SceneView = nullptr;
        m_bForceFullProxySync = false;
        NextObjectId = 1;

        Object::Finalize();

        LOG_INFO("World::Finalize() - Complete");
    }

    bool World::AddObject(WorldObject *object)
    {
        if (!object || !HasFlag(OF_Initialized))
        {
            return false;
        }

        // 重複チェック（既にInnerに存在するか）
        for (auto *inner : m_Inners)
        {
            if (inner == object)
            {
                NORVES_LOG_WARNING("World", "Object already in World");
                return false;
            }
        }

        if (object->GetOuter() != nullptr)
        {
            NORVES_LOG_WARNING("World", "Object already has an outer");
            return false;
        }

        if (!object->HasFlag(OF_Initialized))
        {
            object->Initialize();
        }

        // Innerとして追加（Outerも自動設定される）
        if (!AddInner(object))
        {
            return false;
        }

        // オブジェクトIDを付与
        object->SetObjectId(NextObjectId++);

        // ライフサイクル通知
        object->OnAddedToWorld();

        NORVES_LOG_DEBUG("World", "Object added (ID=%llu), total: %llu",
                         object->GetObjectId(),
                         static_cast<uint64_t>(GetObjectCount()));
        return true;
    }

    void World::RemoveObject(WorldObject *object)
    {
        if (!object)
        {
            return;
        }

        // Innersに存在するか確認
        for (auto *inner : m_Inners)
        {
            if (inner == object)
            {
                // SceneViewからProxy削除
                if (m_SceneView)
                {
                    m_SceneView->RemoveMeshProxy(object->GetObjectId());
                    m_SceneView->RemoveMegaGeometryProxy(object->GetObjectId());

                    // LightComponentのLightProxyも削除
                    auto components = object->GetComponents();
                    for (auto* comp : components)
                    {
                        auto* lightComp = CastTo<Component::LightComponent>(comp);
                        if (lightComp)
                        {
                            m_SceneView->RemoveLightProxy(lightComp->GetComponentId());
                        }
                    }
                }

                // ライフサイクル通知
                object->OnRemovedFromWorld();

                // Innerから除去して破棄する
                if (!DestroyContextOwnedInner(*this, object))
                {
                    RemoveInner(object);
                }

                NORVES_LOG_DEBUG("World", "Object removed, remaining: %llu",
                                 static_cast<uint64_t>(GetObjectCount()));
                return;
            }
        }
    }

    Container::VariableArray<WorldObject *> World::GetWorldObjects() const
    {
        Container::VariableArray<WorldObject *> result;
        for (auto *inner : m_Inners)
        {
            if (auto *obj = CastTo<WorldObject>(inner))
            {
                result.push_back(obj);
            }
        }
        return result;
    }

    size_t World::GetObjectCount() const
    {
        size_t count = 0;
        for (auto *inner : m_Inners)
        {
            if (CastTo<WorldObject>(inner))
            {
                ++count;
            }
        }
        return count;
    }

    void World::Tick(float deltaTime)
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        // 全InnerのWorldObjectをTick
        for (auto *inner : m_Inners)
        {
            auto *obj = CastTo<WorldObject>(inner);
            if (obj && obj->IsActive() && obj->IsTickEnabled() && !obj->IsPendingDestroy())
            {
                obj->Tick(deltaTime);
                obj->TickComponents(deltaTime);
            }
        }

        // 破棄予約されたオブジェクトのクリーンアップ
        CleanupDestroyedObjects();
    }

    void World::SetSceneView(Rendering::SceneView *sceneView)
    {
        m_SceneView = sceneView;
        m_bForceFullProxySync = true;
    }

    void World::SyncToSceneView(const Rendering::MaterialResources *materials)
    {
        if (!m_SceneView || !HasFlag(OF_Initialized))
        {
            return;
        }

        Container::UnorderedSet<uint64_t> liveMeshObjectIds;
        Container::UnorderedSet<uint64_t> liveMegaGeometryObjectIds;
        Container::UnorderedSet<uint64_t> liveLightIds;
        liveMeshObjectIds.reserve(m_Inners.size());
        liveMegaGeometryObjectIds.reserve(m_Inners.size());

        const bool bForceFullProxySync = m_bForceFullProxySync;

        // 全WorldObjectのMeshComponent/LightComponentからProxyを構築してSceneViewへ送信
        for (auto *inner : m_Inners)
        {
            auto *obj = CastTo<WorldObject>(inner);
            if (!obj || !obj->IsActive() || obj->IsPendingDestroy())
            {
                continue;
            }

            const uint64_t ownerVersion = obj->GetTransformVersion();

            // オブジェクトのInner（Component）をチェック
            auto components = obj->GetComponents();
            for (auto *comp : components)
            {
                auto *megaComp = CastTo<Component::MegaGeometryComponent>(comp);

                // MeshComponentの同期
                auto *meshComp = CastTo<Component::MeshComponent>(comp);
                if (meshComp && !megaComp)
                {
                    const bool bNeedsSync = bForceFullProxySync ||
                                            meshComp->IsRenderStateDirty() ||
                                            meshComp->GetLastSyncedTransformVersion() != ownerVersion;
                    if (!bNeedsSync)
                    {
                        liveMeshObjectIds.insert(obj->GetObjectId());
                    }
                    else
                    {
                        meshComp->RefreshRenderTransformCache();

                        // MeshProxyを構築
                        Rendering::MeshProxy meshProxy;
                        if (meshComp->BuildMeshProxy(meshProxy, materials))
                        {
                            // ObjectIdとComponentIdを設定
                            meshProxy.ObjectId = obj->GetObjectId();
                            meshProxy.ComponentId = meshComp->GetComponentId();

                            // SceneViewに追加/更新
                            m_SceneView->UpdateMeshProxy(meshProxy);
                            liveMeshObjectIds.insert(meshProxy.ObjectId);
                        }

                        meshComp->ClearRenderStateDirty();
                        meshComp->SetLastSyncedTransformVersion(ownerVersion);
                    }
                }

                // MegaGeometryComponentの同期
                if (megaComp)
                {
                    const bool bNeedsSync = bForceFullProxySync ||
                                            megaComp->IsRenderStateDirty() ||
                                            megaComp->GetLastSyncedTransformVersion() != ownerVersion;
                    if (!bNeedsSync)
                    {
                        liveMegaGeometryObjectIds.insert(obj->GetObjectId());
                    }
                    else
                    {
                        megaComp->RefreshRenderTransformCache();

                        Rendering::MegaGeometryProxy megaProxy;
                        if (megaComp->BuildMegaGeometryProxy(megaProxy))
                        {
                            megaProxy.ObjectId = obj->GetObjectId();
                            megaProxy.ComponentId = megaComp->GetComponentId();
                            m_SceneView->UpdateMegaGeometryProxy(megaProxy);
                            liveMegaGeometryObjectIds.insert(megaProxy.ObjectId);
                        }

                        megaComp->ClearRenderStateDirty();
                        megaComp->SetLastSyncedTransformVersion(ownerVersion);
                    }
                }

                // LightComponentの同期
                auto *lightComp = CastTo<Component::LightComponent>(comp);
                if (lightComp)
                {
                    const bool bNeedsSync = bForceFullProxySync ||
                                            lightComp->IsRenderStateDirty() ||
                                            lightComp->GetLastSyncedTransformVersion() != ownerVersion;
                    if (!bNeedsSync)
                    {
                        liveLightIds.insert(lightComp->GetComponentId());
                    }
                    else
                    {
                        // LightProxyを構築
                        Rendering::LightProxy lightProxy;
                        if (lightComp->BuildLightProxy(lightProxy))
                        {
                            lightProxy.LightId = lightComp->GetComponentId();

                            // SceneViewに追加/更新
                            m_SceneView->UpdateLightProxy(lightProxy);
                            liveLightIds.insert(lightProxy.LightId);
                        }

                        lightComp->ClearRenderStateDirty();
                        lightComp->SetLastSyncedTransformVersion(ownerVersion);
                    }
                }
            }
        }

        m_SceneView->RemoveStaleMeshProxies(liveMeshObjectIds);
        m_SceneView->RemoveStaleMegaGeometryProxies(liveMegaGeometryObjectIds);
        m_SceneView->RemoveStaleLightProxies(liveLightIds);
        m_bForceFullProxySync = false;
    }

    void World::CleanupDestroyedObjects()
    {
        // 破棄予約されたWorldObjectを収集
        Container::VariableArray<WorldObject *> toRemove;
        for (auto *inner : m_Inners)
        {
            auto *obj = CastTo<WorldObject>(inner);
            if (obj && obj->IsPendingDestroy())
            {
                toRemove.push_back(obj);
            }
        }

        // 収集したオブジェクトを除去・Finalize
        for (auto *obj : toRemove)
        {
            if (m_SceneView)
            {
                m_SceneView->RemoveMeshProxy(obj->GetObjectId());
                m_SceneView->RemoveMegaGeometryProxy(obj->GetObjectId());

                // LightComponentのLightProxyも削除
                auto components = obj->GetComponents();
                for (auto* comp : components)
                {
                    auto* lightComp = CastTo<Component::LightComponent>(comp);
                    if (lightComp)
                    {
                        m_SceneView->RemoveLightProxy(lightComp->GetComponentId());
                    }
                }
            }

            obj->OnRemovedFromWorld();
            if (!DestroyContextOwnedInner(*this, obj))
            {
                RemoveInner(obj);
            }
        }

        if (!toRemove.empty())
        {
            NORVES_LOG_DEBUG("World", "Cleaned up %llu destroyed objects",
                             static_cast<uint64_t>(toRemove.size()));
        }
    }

} // namespace NorvesLib::Core
