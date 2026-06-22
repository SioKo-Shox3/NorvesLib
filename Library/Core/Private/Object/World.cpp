#include "Object/World.h"
#include "Object/Entity.h"
#include "Object/PrefabAsset.h"
#include "Component/BoardComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/MeshComponent.h"
#include "Component/MegaGeometryComponent.h"
#include "Component/LightComponent.h"
#include "Engine/NorvesEngine.h"
#include "Engine/ComponentDataRegistry.h"
#include "Rendering/IBoardProxySink.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneProxy.h"
#include "Logging/LogMacros.h"
#include "Container/UnorderedSet.h"

namespace NorvesLib::Core
{
    namespace
    {
        constexpr uint32_t MaxPrefabNestedDepth = 16;

        struct PrefabLiveObject
        {
            Object* ObjectPtr = nullptr;
            Entity* EntityPtr = nullptr;
            Component::Component* ComponentPtr = nullptr;
            ObjectHandle Handle;
        };

        struct PrefabBuiltObject
        {
            PrefabTargetPath Target;
            PrefabLiveObject LiveObject;
        };

        struct PrefabBuildState
        {
            Container::VariableArray<PrefabBuiltObject> Objects;
        };

        using PrefabAliasMap = Container::UnorderedMap<SubtreeSnapshotAliasId, PrefabLiveObject>;

        template <typename T>
        class TRawObjectGuard
        {
        public:
            explicit TRawObjectGuard(T* object)
                : m_Object(object)
            {
            }

            ~TRawObjectGuard()
            {
                if (m_Object)
                {
                    if (m_Object->HasFlag(OF_Initialized))
                    {
                        m_Object->Finalize();
                    }
                    delete m_Object;
                }
            }

            T* Get() const
            {
                return m_Object;
            }

            T* Release()
            {
                T* object = m_Object;
                m_Object = nullptr;
                return object;
            }

        private:
            T* m_Object = nullptr;
        };

        StableClassId MakePrefabStableClassId(const IClass& cls)
        {
            return MakeStableSchemaId("NorvesLib", "Class", cls.GetClassName().GetView());
        }

        StablePropertyId MakePrefabStablePropertyId(const IClass& cls, const ClassProperty& property)
        {
            return MakeStableSchemaId(
                "NorvesLib",
                "Property",
                cls.GetClassName().GetView(),
                property.GetName().GetView());
        }

        const IClass* ResolvePrefabClass(StableClassId stableClassId)
        {
            Container::VariableArray<const IClass*> classes = ClassRegistry::Get().GetAllClasses();
            for (const IClass* cls : classes)
            {
                if (cls && MakePrefabStableClassId(*cls) == stableClassId)
                {
                    return cls;
                }
            }
            return nullptr;
        }

        const ClassProperty* FindPrefabProperty(const IClass& cls, StablePropertyId propertyId)
        {
            Container::VariableArray<const ClassProperty*> properties = cls.GetAllProperties();
            for (const ClassProperty* property : properties)
            {
                if (property && MakePrefabStablePropertyId(cls, *property) == propertyId)
                {
                    return property;
                }
            }
            return nullptr;
        }

        bool ContainsPrefabInStack(
            const Container::VariableArray<const PrefabAsset*>& stack,
            const PrefabAsset& prefab)
        {
            for (const PrefabAsset* existing : stack)
            {
                if (existing == &prefab)
                {
                    return true;
                }
            }
            return false;
        }

        bool ContainsEntityAlias(const EntitySubtreeSnapshotNode& node, SubtreeSnapshotAliasId alias)
        {
            if (node.Alias == alias)
            {
                return true;
            }

            for (const EntitySubtreeSnapshotNode& child : node.Children)
            {
                if (ContainsEntityAlias(child, alias))
                {
                    return true;
                }
            }
            return false;
        }

        bool ValidatePrefabGraph(
            const PrefabAsset& prefab,
            Container::VariableArray<const PrefabAsset*>& stack,
            uint32_t depth)
        {
            if (!prefab.HasTree() ||
                depth > MaxPrefabNestedDepth ||
                ContainsPrefabInStack(stack, prefab))
            {
                return false;
            }

            stack.push_back(&prefab);

            for (const PrefabNestedPrefabInstance& nested : prefab.GetNestedPrefabs())
            {
                const PrefabAsset* nestedPrefab = nested.Prefab.Get();
                if (nested.InstanceId == InvalidPrefabNestedInstanceId ||
                    nested.ParentAlias == InvalidSubtreeSnapshotAliasId ||
                    !nestedPrefab ||
                    !nestedPrefab->HasTree() ||
                    !ContainsEntityAlias(prefab.GetTree().Root, nested.ParentAlias) ||
                    !ValidatePrefabGraph(*nestedPrefab, stack, depth + 1))
                {
                    stack.pop_back();
                    return false;
                }
            }

            stack.pop_back();
            return true;
        }

        bool PathsEqual(
            const Container::VariableArray<PrefabNestedInstanceId>& lhs,
            const Container::VariableArray<PrefabNestedInstanceId>& rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (size_t index = 0; index < lhs.size(); ++index)
            {
                if (lhs[index] != rhs[index])
                {
                    return false;
                }
            }
            return true;
        }

        PrefabTargetPath MakeAbsoluteTarget(
            const Container::VariableArray<PrefabNestedInstanceId>& basePath,
            const PrefabTargetPath& localTarget)
        {
            PrefabTargetPath result;
            result.NestedInstances = basePath;
            for (PrefabNestedInstanceId nestedInstance : localTarget.NestedInstances)
            {
                result.NestedInstances.push_back(nestedInstance);
            }
            result.Alias = localTarget.Alias;
            return result;
        }

        PrefabLiveObject* FindBuiltObject(PrefabBuildState& state, const PrefabTargetPath& target)
        {
            for (PrefabBuiltObject& object : state.Objects)
            {
                if (object.Target.Alias == target.Alias &&
                    PathsEqual(object.Target.NestedInstances, target.NestedInstances))
                {
                    return &object.LiveObject;
                }
            }
            return nullptr;
        }

        bool RegisterLiveObject(
            SubtreeSnapshotAliasId alias,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            const PrefabLiveObject& liveObject,
            PrefabAliasMap& aliases,
            PrefabBuildState& state)
        {
            if (alias == InvalidSubtreeSnapshotAliasId ||
                !liveObject.ObjectPtr ||
                aliases.find(alias) != aliases.end())
            {
                return false;
            }

            aliases[alias] = liveObject;

            PrefabBuiltObject builtObject;
            builtObject.Target.NestedInstances = path;
            builtObject.Target.Alias = alias;
            builtObject.LiveObject = liveObject;
            state.Objects.push_back(builtObject);
            return true;
        }

        bool ApplyPrefabValue(Object& object, const ProjectedPropertyValue& projectedValue)
        {
            const IClass* cls = object.GetClass();
            if (!cls)
            {
                return false;
            }

            const ClassProperty* property = FindPrefabProperty(*cls, projectedValue.Property);
            if (!property)
            {
                return false;
            }

            if (ShouldSkipPrefabRestoreProperty(*cls, *property))
            {
                return true;
            }

            const TypeId runtimeType = property->GetRuntimeTypeId();
            const TypeInfo* runtimeTypeInfo = TypeRegistry::Get().Find(runtimeType);
            if (!runtimeTypeInfo || runtimeTypeInfo->StableId != projectedValue.Type)
            {
                return false;
            }

            PropertyValue value;
            if (!value.DeserializeStable(projectedValue.Type, projectedValue.SerializedValue))
            {
                return false;
            }

            if (Entity* entity = CastTo<Entity>(&object))
            {
                const Identity& propertyName = property->GetName();
                if (propertyName == Identity("Position"))
                {
                    const Math::Vector3* position = value.Get<Math::Vector3>();
                    if (!position)
                    {
                        return false;
                    }
                    entity->SetLocalPosition(*position);
                    return true;
                }

                if (propertyName == Identity("Rotation"))
                {
                    const Math::Quaternion* rotation = value.Get<Math::Quaternion>();
                    if (!rotation)
                    {
                        return false;
                    }
                    entity->SetLocalRotation(*rotation);
                    return true;
                }

                if (propertyName == Identity("Scale"))
                {
                    const Math::Vector3* scale = value.Get<Math::Vector3>();
                    if (!scale)
                    {
                        return false;
                    }
                    entity->SetLocalScale(*scale);
                    return true;
                }
            }

            return property->ApplyValue(&object, value);
        }

        bool ApplyPrefabSnapshot(Object& object, const ObjectSnapshot& snapshot)
        {
            for (const ProjectedPropertyValue& value : snapshot.Properties)
            {
                if (!ApplyPrefabValue(object, value))
                {
                    return false;
                }
            }
            return true;
        }

        bool ApplyPrefabOverrideSet(
            const PrefabOverrideSet& overrides,
            const Container::VariableArray<PrefabNestedInstanceId>& basePath,
            PrefabBuildState& state)
        {
            for (const PrefabPropertyOverride& overrideValue : overrides.Properties)
            {
                PrefabTargetPath target = MakeAbsoluteTarget(basePath, overrideValue.Target);
                if (target.Alias == InvalidSubtreeSnapshotAliasId)
                {
                    return false;
                }

                PrefabLiveObject* liveObject = FindBuiltObject(state, target);
                if (!liveObject || !liveObject->ObjectPtr)
                {
                    return false;
                }

                if (!ApplyPrefabValue(*liveObject->ObjectPtr, overrideValue.Value))
                {
                    return false;
                }
            }

            return true;
        }

        Entity* BuildPrefabInstance(
            const PrefabAsset& prefab,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            const PrefabOverrideSet* instanceOverrides,
            PrefabBuildState& state);

        Component::Component* BuildPrefabComponent(
            const ComponentSubtreeSnapshot& snapshot,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            PrefabAliasMap& aliases,
            PrefabBuildState& state)
        {
            const IClass* cls = ResolvePrefabClass(snapshot.Object.Class);
            if (!cls || !cls->IsChildOf(Component::Component::StaticClass()))
            {
                return nullptr;
            }

            IUnknown* instance = cls->NewInstance(nullptr);
            Component::Component* component = CastTo<Component::Component>(instance);
            if (!component)
            {
                delete instance;
                return nullptr;
            }

            TRawObjectGuard<Component::Component> componentGuard(component);
            if (!ApplyPrefabSnapshot(*component, snapshot.Object))
            {
                return nullptr;
            }

            PrefabLiveObject liveObject;
            liveObject.ObjectPtr = component;
            liveObject.ComponentPtr = component;
            if (!RegisterLiveObject(snapshot.Alias, path, liveObject, aliases, state))
            {
                return nullptr;
            }

            return componentGuard.Release();
        }

        Entity* BuildPrefabEntityNode(
            const EntitySubtreeSnapshotNode& node,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            PrefabAliasMap& aliases,
            PrefabBuildState& state)
        {
            const IClass* cls = ResolvePrefabClass(node.Object.Class);
            if (!cls || !cls->IsChildOf(Entity::StaticClass()))
            {
                return nullptr;
            }

            IUnknown* instance = cls->NewInstance(nullptr);
            Entity* entity = CastTo<Entity>(instance);
            if (!entity)
            {
                delete instance;
                return nullptr;
            }

            TRawObjectGuard<Entity> entityGuard(entity);
            if (!ApplyPrefabSnapshot(*entity, node.Object))
            {
                return nullptr;
            }

            PrefabLiveObject liveObject;
            liveObject.ObjectPtr = entity;
            liveObject.EntityPtr = entity;
            if (!RegisterLiveObject(node.Alias, path, liveObject, aliases, state))
            {
                return nullptr;
            }

            for (const ComponentSubtreeSnapshot& componentSnapshot : node.Components)
            {
                if (componentSnapshot.OwnerAlias != node.Alias)
                {
                    return nullptr;
                }

                Component::Component* component = BuildPrefabComponent(
                    componentSnapshot,
                    path,
                    aliases,
                    state);
                TRawObjectGuard<Component::Component> componentGuard(component);
                if (!component || !entity->AddInner(component))
                {
                    return nullptr;
                }
                componentGuard.Release();
            }

            for (const EntitySubtreeSnapshotNode& childNode : node.Children)
            {
                if (childNode.ParentAlias != node.Alias)
                {
                    return nullptr;
                }

                Entity* child = BuildPrefabEntityNode(childNode, path, aliases, state);
                TRawObjectGuard<Entity> childGuard(child);
                if (!child || !entity->AddInner(child))
                {
                    return nullptr;
                }
                childGuard.Release();
            }

            return entityGuard.Release();
        }

        Entity* BuildPrefabInstance(
            const PrefabAsset& prefab,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            const PrefabOverrideSet* instanceOverrides,
            PrefabBuildState& state)
        {
            if (!prefab.HasTree() ||
                prefab.GetTree().RootAlias == InvalidSubtreeSnapshotAliasId ||
                prefab.GetTree().Root.Alias != prefab.GetTree().RootAlias ||
                prefab.GetTree().Root.ParentAlias != InvalidSubtreeSnapshotAliasId)
            {
                return nullptr;
            }

            PrefabAliasMap localAliases;
            Entity* root = BuildPrefabEntityNode(prefab.GetTree().Root, path, localAliases, state);
            TRawObjectGuard<Entity> rootGuard(root);
            if (!root)
            {
                return nullptr;
            }

            for (const PrefabNestedPrefabInstance& nested : prefab.GetNestedPrefabs())
            {
                auto parentIt = localAliases.find(nested.ParentAlias);
                const PrefabAsset* nestedPrefab = nested.Prefab.Get();
                if (parentIt == localAliases.end() ||
                    !parentIt->second.EntityPtr ||
                    !nestedPrefab ||
                    nested.InstanceId == InvalidPrefabNestedInstanceId)
                {
                    return nullptr;
                }

                Container::VariableArray<PrefabNestedInstanceId> nestedPath = path;
                nestedPath.push_back(nested.InstanceId);

                Entity* nestedRoot = BuildPrefabInstance(
                    *nestedPrefab,
                    nestedPath,
                    &nested.Overrides,
                    state);
                TRawObjectGuard<Entity> nestedRootGuard(nestedRoot);
                if (!nestedRoot || !parentIt->second.EntityPtr->AddInner(nestedRoot))
                {
                    return nullptr;
                }
                nestedRootGuard.Release();
            }

            if (instanceOverrides && !ApplyPrefabOverrideSet(*instanceOverrides, path, state))
            {
                return nullptr;
            }

            return rootGuard.Release();
        }

        void RegisterEntitySubtreeForComponentData(World& world, Entity& entity)
        {
            GEngine.GetComponentDataRegistry().RegisterEntity(world, entity);

            auto children = entity.GetChildEntities();
            for (auto* child : children)
            {
                RegisterEntitySubtreeForComponentData(world, *child);
            }
        }

        void UnregisterEntitySubtreeForComponentData(Entity& entity)
        {
            auto children = entity.GetChildEntities();
            for (auto* child : children)
            {
                UnregisterEntitySubtreeForComponentData(*child);
            }

            GEngine.GetComponentDataRegistry().UnregisterEntity(entity);
        }
    } // namespace

    IMPLEMENT_CLASS(World, Object)

    World::World()
        : Object()
    {
        NextObjectId = 1;
    }

    World::World(const FieldInitializer* initializer)
        : Object(initializer)
    {
        NextObjectId = 1;
    }

    World::World(const IUnknown* sourceObject)
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
            IUnknown* inner = m_Inners.back();
            if (auto* entity = CastTo<Entity>(inner))
            {
                DestroyEntitySubtree(*entity);
                continue;
            }

            EraseInnerReference(*this, *inner);
            SetOuterReference(*inner, nullptr);
            if (inner->HasFlag(OF_HeapOwned))
            {
                inner->SetFlag(OF_PendingDestroy, true);
            }
            else
            {
                inner->Finalize();
                delete inner;
            }
        }

        GEngine.GetComponentDataRegistry().UnregisterWorld(*this);

        m_SceneView = nullptr;
        m_ScreenSpaceBoardSink = nullptr;
        NextObjectId = 1;

        Object::Finalize();

        LOG_INFO("World::Finalize() - Complete");
    }

    bool World::AddInner(IUnknown* inner)
    {
        if (CastTo<Entity>(inner))
        {
            return false;
        }

        return UnknownImpl::AddInner(inner);
    }

    bool World::AddObject(Entity* object)
    {
        if (!AttachRootEntity(object))
        {
            return false;
        }

        NORVES_LOG_DEBUG("World", "Object added (ID=%llu), total: %llu",
                         object->GetObjectId(),
                         static_cast<uint64_t>(GetObjectCount()));
        return true;
    }

    Entity* World::SpawnPrefab(const PrefabAsset& prefab, Entity* parent, const PrefabOverrideSet* overrides)
    {
        if (!HasFlag(OF_Initialized))
        {
            return nullptr;
        }

        if (parent && parent->GetWorld() != this)
        {
            return nullptr;
        }

        Container::VariableArray<const PrefabAsset*> prefabStack;
        if (!ValidatePrefabGraph(prefab, prefabStack, 1))
        {
            return nullptr;
        }

        PrefabBuildState buildState;
        Container::VariableArray<PrefabNestedInstanceId> rootPath;
        Entity* root = BuildPrefabInstance(prefab, rootPath, nullptr, buildState);
        TRawObjectGuard<Entity> rootGuard(root);
        if (!root)
        {
            return nullptr;
        }

        if (overrides && !ApplyPrefabOverrideSet(*overrides, rootPath, buildState))
        {
            return nullptr;
        }

        const bool bAttached = parent
            ? AttachChildEntity(parent, root)
            : AttachRootEntity(root);
        if (!bAttached)
        {
            return nullptr;
        }

        return rootGuard.Release();
    }

    void World::RemoveObject(Entity* object)
    {
        if (!object || !IsEntityRoot(*object))
        {
            return;
        }

        DestroyEntitySubtree(*object);

        NORVES_LOG_DEBUG("World", "Object removed, remaining: %llu",
                         static_cast<uint64_t>(GetObjectCount()));
    }

    bool World::RemoveEntity(Entity* entity)
    {
        if (!entity || entity->GetWorld() != this || entity->GetOuter() == nullptr)
        {
            return false;
        }

        return DestroyEntitySubtree(*entity);
    }

    bool World::ReparentEntity(Entity* entity, Entity* newParent)
    {
        if (!entity || entity->GetWorld() != this || entity->GetOuter() == nullptr)
        {
            return false;
        }

        if (entity->IsPendingDestroy())
        {
            return false;
        }

        if (HasPendingEntityAncestor(*entity))
        {
            return false;
        }

        if (newParent)
        {
            if (newParent->GetWorld() != this ||
                newParent->IsPendingDestroy() ||
                HasPendingEntityAncestor(*newParent) ||
                newParent == entity ||
                IsEntityInSubtree(*entity, *newParent))
            {
                return false;
            }
        }

        IUnknown* newOwner = newParent ? static_cast<IUnknown*>(newParent) : static_cast<IUnknown*>(this);
        if (entity->GetOuter() == newOwner)
        {
            return true;
        }

        UpdateWorldTransforms();
        const Math::Transform savedWorldTransform = entity->GetWorldTransform();

        if (!MoveEntityInnerAtomic(*entity, *newOwner))
        {
            return false;
        }

        entity->SetWorldTransform(savedWorldTransform);
        MarkEntitySubtreeTransformDirty(*entity);
        MarkEntitySubtreeRenderStateDirty(*entity);
        return true;
    }

    Container::VariableArray<Entity*> World::GetRootEntities() const
    {
        Container::VariableArray<Entity*> result;
        for (auto* inner : m_Inners)
        {
            if (auto* obj = CastTo<Entity>(inner))
            {
                result.push_back(obj);
            }
        }
        return result;
    }

    size_t World::GetObjectCount() const
    {
        size_t count = 0;
        for (auto* inner : m_Inners)
        {
            if (CastTo<Entity>(inner))
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

        auto roots = GetRootEntities();
        for (auto* entity : roots)
        {
            TickEntityRecursive(*entity, deltaTime);
        }

        CleanupDestroyedObjects();
    }

    void World::SetSceneView(Rendering::SceneView* sceneView)
    {
        m_SceneView = sceneView;

        auto roots = GetRootEntities();
        for (auto* entity : roots)
        {
            MarkEntitySubtreeRenderStateDirty(*entity);
        }
    }

    void World::SetScreenSpaceBoardSink(Rendering::IBoardProxySink* sink)
    {
        if (m_ScreenSpaceBoardSink == sink)
        {
            return;
        }

        m_ScreenSpaceBoardSink = sink;

        auto roots = GetRootEntities();
        for (auto* entity : roots)
        {
            MarkEntitySubtreeRenderStateDirty(*entity);
        }
    }

    void World::SyncToSceneView(const Rendering::MaterialResources* materials)
    {
        if (!HasFlag(OF_Initialized) ||
            (!m_SceneView && !m_ScreenSpaceBoardSink))
        {
            return;
        }

        ComponentDataRegistry& componentDataRegistry = GEngine.GetComponentDataRegistry();
        ComponentDataRegistry* enabledComponentDataRegistry = componentDataRegistry.IsEnabled()
            ? &componentDataRegistry
            : nullptr;
        if (enabledComponentDataRegistry)
        {
            enabledComponentDataRegistry->BeginFrameCapture();
        }

        UpdateWorldTransforms();

        Container::UnorderedSet<uint64_t> liveMeshComponentIds;
        Container::UnorderedSet<uint64_t> liveMegaGeometryObjectIds;
        Container::UnorderedSet<uint64_t> liveLightIds;
        Container::UnorderedSet<uint64_t> liveScreenBoardComponentIds;
        Container::UnorderedSet<uint64_t> liveWorldBoardComponentIds;
        liveMeshComponentIds.reserve(m_Inners.size());
        liveMegaGeometryObjectIds.reserve(m_Inners.size());
        liveLightIds.reserve(m_Inners.size());
        liveScreenBoardComponentIds.reserve(m_Inners.size());
        liveWorldBoardComponentIds.reserve(m_Inners.size());

        auto roots = GetRootEntities();
        for (auto* entity : roots)
        {
            SyncEntityRecursive(
                *entity,
                materials,
                liveMeshComponentIds,
                liveMegaGeometryObjectIds,
                liveLightIds,
                liveScreenBoardComponentIds,
                liveWorldBoardComponentIds,
                enabledComponentDataRegistry);
        }

        if (m_SceneView)
        {
            m_SceneView->RemoveStaleMeshProxies(liveMeshComponentIds);
            m_SceneView->RemoveStaleMegaGeometryProxies(liveMegaGeometryObjectIds);
            m_SceneView->RemoveStaleLightProxies(liveLightIds);
            m_SceneView->RemoveStaleBoardProxies(liveWorldBoardComponentIds);
        }

        if (m_ScreenSpaceBoardSink)
        {
            m_ScreenSpaceBoardSink->RemoveStaleBoardProxies(liveScreenBoardComponentIds);
        }
    }

    void World::UpdateWorldTransforms()
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        auto roots = GetRootEntities();
        for (auto* entity : roots)
        {
            UpdateEntityTransformRecursive(*entity, Math::Transform::Identity);
        }
    }

    void World::UpdateEntityTransformRecursive(Entity& entity, const Math::Transform& parentWorld)
    {
        entity.RecomputeWorldTransform(parentWorld);
        const Math::Transform& worldTransform = entity.GetWorldTransform();

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            UpdateEntityTransformRecursive(*child, worldTransform);
        }
    }

    void World::TickEntityRecursive(Entity& entity, float deltaTime)
    {
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            return;
        }

        if (entity.IsTickEnabled())
        {
            entity.Tick(deltaTime);
            entity.TickComponents(deltaTime);
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            TickEntityRecursive(*child, deltaTime);
        }
    }

    void World::SyncEntityRecursive(
        Entity& entity,
        const Rendering::MaterialResources* materials,
        Container::UnorderedSet<uint64_t>& liveMeshComponentIds,
        Container::UnorderedSet<uint64_t>& liveMegaGeometryObjectIds,
        Container::UnorderedSet<uint64_t>& liveLightIds,
        Container::UnorderedSet<uint64_t>& liveScreenBoardComponentIds,
        Container::UnorderedSet<uint64_t>& liveWorldBoardComponentIds,
        ComponentDataRegistry* componentDataRegistry)
    {
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            return;
        }

        if (componentDataRegistry)
        {
            componentDataRegistry->PublishTransform(entity);
        }

        const uint64_t ownerVersion = entity.GetTransformVersion();
        auto components = entity.GetComponents();
        for (auto* comp : components)
        {
            auto* megaComp = CastTo<Component::MegaGeometryComponent>(comp);

            auto* meshComp = CastTo<Component::MeshComponent>(comp);
            if (m_SceneView && meshComp && !megaComp)
            {
                const bool bNeedsSync = meshComp->IsRenderStateDirty() ||
                                        meshComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (!bNeedsSync)
                {
                    liveMeshComponentIds.insert(meshComp->GetComponentId());
                    if (componentDataRegistry)
                    {
                        Rendering::MeshProxy meshProxy;
                        if (meshComp->BuildMeshProxy(meshProxy, materials))
                        {
                            meshProxy.ObjectId = entity.GetObjectId();
                            meshProxy.ComponentId = meshComp->GetComponentId();
                            componentDataRegistry->PublishMeshProxy(entity, meshProxy);
                        }
                    }
                }
                else
                {
                    meshComp->RefreshRenderTransformCache();

                    Rendering::MeshProxy meshProxy;
                    if (meshComp->BuildMeshProxy(meshProxy, materials))
                    {
                        meshProxy.ObjectId = entity.GetObjectId();
                        meshProxy.ComponentId = meshComp->GetComponentId();
                        m_SceneView->UpdateMeshProxy(meshProxy);
                        liveMeshComponentIds.insert(meshProxy.ComponentId);
                        if (componentDataRegistry)
                        {
                            componentDataRegistry->PublishMeshProxy(entity, meshProxy);
                        }
                    }

                    meshComp->ClearRenderStateDirty();
                    meshComp->SetLastSyncedTransformVersion(ownerVersion);
                }
            }

            if (m_SceneView && megaComp)
            {
                const bool bNeedsSync = megaComp->IsRenderStateDirty() ||
                                        megaComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (!bNeedsSync)
                {
                    liveMegaGeometryObjectIds.insert(entity.GetObjectId());
                    if (componentDataRegistry)
                    {
                        Rendering::MegaGeometryProxy megaProxy;
                        if (megaComp->BuildMegaGeometryProxy(megaProxy))
                        {
                            megaProxy.ObjectId = entity.GetObjectId();
                            megaProxy.ComponentId = megaComp->GetComponentId();
                            componentDataRegistry->PublishMegaGeometryProxy(entity, megaProxy);
                        }
                    }
                }
                else
                {
                    megaComp->RefreshRenderTransformCache();

                    Rendering::MegaGeometryProxy megaProxy;
                    if (megaComp->BuildMegaGeometryProxy(megaProxy))
                    {
                        megaProxy.ObjectId = entity.GetObjectId();
                        megaProxy.ComponentId = megaComp->GetComponentId();
                        m_SceneView->UpdateMegaGeometryProxy(megaProxy);
                        liveMegaGeometryObjectIds.insert(megaProxy.ObjectId);
                        if (componentDataRegistry)
                        {
                            componentDataRegistry->PublishMegaGeometryProxy(entity, megaProxy);
                        }
                    }

                    megaComp->ClearRenderStateDirty();
                    megaComp->SetLastSyncedTransformVersion(ownerVersion);
                }
            }

            auto* lightComp = CastTo<Component::LightComponent>(comp);
            if (m_SceneView && lightComp)
            {
                const bool bNeedsSync = lightComp->IsRenderStateDirty() ||
                                        lightComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (!bNeedsSync)
                {
                    liveLightIds.insert(lightComp->GetComponentId());
                }
                else
                {
                    Rendering::LightProxy lightProxy;
                    if (lightComp->BuildLightProxy(lightProxy))
                    {
                        lightProxy.LightId = lightComp->GetComponentId();
                        m_SceneView->UpdateLightProxy(lightProxy);
                        liveLightIds.insert(lightProxy.LightId);
                    }

                    lightComp->ClearRenderStateDirty();
                    lightComp->SetLastSyncedTransformVersion(ownerVersion);
                }
            }

            auto* boardComp = CastTo<Component::BoardComponent>(comp);
            if (boardComp)
            {
                boardComp->PrepareFlipbookForRenderSync();
                const bool bForceWorldSpace = CastTo<Component::BillboardComponent>(boardComp) != nullptr;
                const Rendering::BoardSpace effectiveSpace = bForceWorldSpace
                                                                  ? Rendering::BoardSpace::WorldSpace
                                                                  : boardComp->GetBoardSpace();

                const bool bNeedsSync = boardComp->IsRenderStateDirty() ||
                                        boardComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (!bNeedsSync)
                {
                    if (effectiveSpace == Rendering::BoardSpace::WorldSpace)
                    {
                        liveWorldBoardComponentIds.insert(boardComp->GetComponentId());
                    }
                    else
                    {
                        liveScreenBoardComponentIds.insert(boardComp->GetComponentId());
                    }
                }
                else
                {
                    boardComp->RefreshRenderTransformCache();

                    Rendering::BoardProxy boardProxy;
                    if (boardComp->BuildBoardProxy(boardProxy))
                    {
                        boardProxy.ObjectId = entity.GetObjectId();
                        boardProxy.ComponentId = boardComp->GetComponentId();
                        if (boardProxy.Space == Rendering::BoardSpace::WorldSpace)
                        {
                            liveWorldBoardComponentIds.insert(boardProxy.ComponentId);
                            if (m_SceneView)
                            {
                                m_SceneView->UpdateBoardProxy(boardProxy);
                            }
                            if (m_ScreenSpaceBoardSink)
                            {
                                m_ScreenSpaceBoardSink->RemoveBoardProxy(boardProxy.ComponentId);
                            }
                        }
                        else
                        {
                            liveScreenBoardComponentIds.insert(boardProxy.ComponentId);
                            if (m_ScreenSpaceBoardSink)
                            {
                                m_ScreenSpaceBoardSink->UpdateBoardProxy(boardProxy.ComponentId, boardProxy);
                            }
                            if (m_SceneView)
                            {
                                m_SceneView->RemoveBoardProxy(boardProxy.ComponentId);
                            }
                        }
                    }
                    else
                    {
                        if (m_ScreenSpaceBoardSink)
                        {
                            m_ScreenSpaceBoardSink->RemoveBoardProxy(boardComp->GetComponentId());
                        }
                        if (m_SceneView)
                        {
                            m_SceneView->RemoveBoardProxy(boardComp->GetComponentId());
                        }
                    }

                    boardComp->ClearRenderStateDirty();
                    boardComp->SetLastSyncedTransformVersion(ownerVersion);
                }
            }
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            SyncEntityRecursive(
                *child,
                materials,
                liveMeshComponentIds,
                liveMegaGeometryObjectIds,
                liveLightIds,
                liveScreenBoardComponentIds,
                liveWorldBoardComponentIds,
                componentDataRegistry);
        }
    }

    void World::CleanupDestroyedObjects()
    {
        Container::VariableArray<Entity*> toRemove;
        auto roots = GetRootEntities();
        for (auto* entity : roots)
        {
            CollectPendingDestroyRecursive(*entity, toRemove);
        }

        for (auto* entity : toRemove)
        {
            if (entity && entity->GetWorld() == this && entity->IsPendingDestroy())
            {
                DestroyEntitySubtree(*entity);
            }
        }

        if (!toRemove.empty())
        {
            NORVES_LOG_DEBUG("World", "Cleaned up %llu destroyed objects",
                             static_cast<uint64_t>(toRemove.size()));
        }
    }

    void World::CollectPendingDestroyRecursive(Entity& entity, Container::VariableArray<Entity*>& toRemove)
    {
        if (entity.IsPendingDestroy())
        {
            toRemove.push_back(&entity);
            return;
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            CollectPendingDestroyRecursive(*child, toRemove);
        }
    }

    bool World::AttachRootEntity(Entity* entity)
    {
        if (!entity ||
            !HasFlag(OF_Initialized) ||
            entity->GetOuter() != nullptr ||
            entity->IsPendingDestroy() ||
            ContainsHeapOwnedEntity(*entity))
        {
            return false;
        }

        if (ContainsInner(*this, *entity))
        {
            return false;
        }

        AppendInnerReference(*this, *entity);
        SetOuterReference(*entity, this);

        InitializeEntitySubtreeForWorld(*entity);
        AssignFreshObjectIdsRecursive(*entity);
        RegisterEntitySubtreeForComponentData(*this, *entity);
        MarkEntitySubtreeTransformDirty(*entity);
        MarkEntitySubtreeRenderStateDirty(*entity);
        NotifyEntitySubtreeAdded(*entity);
        return true;
    }

    bool World::AttachChildEntity(Entity* parent, Entity* child)
    {
        if (!parent || !child || !HasFlag(OF_Initialized))
        {
            return false;
        }

        if (parent->GetWorld() != this ||
            parent->IsPendingDestroy() ||
            child->GetOuter() != nullptr ||
            child->IsPendingDestroy() ||
            ContainsHeapOwnedEntity(*child) ||
            parent == child ||
            IsEntityInSubtree(*child, *parent))
        {
            return false;
        }

        if (ContainsInner(*parent, *child))
        {
            return false;
        }

        AppendInnerReference(*parent, *child);
        SetOuterReference(*child, parent);

        InitializeEntitySubtreeForWorld(*child);
        AssignFreshObjectIdsRecursive(*child);
        RegisterEntitySubtreeForComponentData(*this, *child);
        MarkEntitySubtreeTransformDirty(*child);
        MarkEntitySubtreeRenderStateDirty(*child);
        NotifyEntitySubtreeAdded(*child);
        return true;
    }

    bool World::MoveEntityInnerAtomic(Entity& entity, IUnknown& newOwner)
    {
        IUnknown* oldOwner = entity.GetOuter();
        if (!oldOwner || oldOwner == &newOwner)
        {
            return oldOwner == &newOwner;
        }

        if (ContainsInner(newOwner, entity))
        {
            return false;
        }

        AppendInnerReference(newOwner, entity);
        SetOuterReference(entity, &newOwner);

        if (!EraseInnerReference(*oldOwner, entity))
        {
            EraseInnerReference(newOwner, entity);
            SetOuterReference(entity, oldOwner);
            return false;
        }

        return true;
    }

    bool World::DestroyEntitySubtree(Entity& entity)
    {
        if (entity.GetWorld() != this)
        {
            return false;
        }

        IUnknown* owner = entity.GetOuter();
        if (!owner)
        {
            return false;
        }

        UnregisterEntitySubtreeForComponentData(entity);

        RemoveEntitySubtreeProxies(entity);
        NotifyEntitySubtreeRemoved(entity);

        EraseInnerReference(*owner, entity);
        SetOuterReference(entity, nullptr);

        if (entity.HasFlag(OF_HeapOwned))
        {
            entity.SetFlag(OF_PendingDestroy, true);
            return true;
        }

        entity.Finalize();
        delete &entity;
        return true;
    }

    void World::InitializeEntitySubtreeForWorld(Entity& entity)
    {
        if (!entity.HasFlag(OF_Initialized))
        {
            entity.Initialize();
        }

        for (auto* inner : entity.GetInners())
        {
            if (auto* child = CastTo<Entity>(inner))
            {
                InitializeEntitySubtreeForWorld(*child);
                continue;
            }

            if (auto* component = CastTo<Component::Component>(inner))
            {
                if (!component->HasFlag(OF_Initialized))
                {
                    component->Initialize();
                }
                component->BeginPlay();
            }
        }
    }

    void World::AssignFreshObjectIdsRecursive(Entity& entity)
    {
        entity.SetObjectId(NextObjectId++);

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            AssignFreshObjectIdsRecursive(*child);
        }
    }

    void World::MarkEntitySubtreeRenderStateDirty(Entity& entity)
    {
        auto components = entity.GetComponents();
        for (auto* component : components)
        {
            component->MarkRenderStateDirty();
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            MarkEntitySubtreeRenderStateDirty(*child);
        }
    }

    void World::MarkEntitySubtreeTransformDirty(Entity& entity)
    {
        entity.MarkWorldTransformDirty();

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            MarkEntitySubtreeTransformDirty(*child);
        }
    }

    void World::NotifyEntitySubtreeAdded(Entity& entity)
    {
        entity.OnAddedToWorld();

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            NotifyEntitySubtreeAdded(*child);
        }
    }

    void World::NotifyEntitySubtreeRemoved(Entity& entity)
    {
        entity.OnRemovedFromWorld();

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            NotifyEntitySubtreeRemoved(*child);
        }
    }

    void World::RemoveEntitySubtreeProxies(Entity& entity)
    {
        auto components = entity.GetComponents();

        if (m_SceneView)
        {
            m_SceneView->RemoveMegaGeometryProxy(entity.GetObjectId());

            for (auto* comp : components)
            {
                if (auto* meshComp = CastTo<Component::MeshComponent>(comp))
                {
                    if (CastTo<Component::MegaGeometryComponent>(meshComp) == nullptr)
                    {
                        m_SceneView->RemoveMeshProxy(meshComp->GetComponentId());
                    }
                }
                if (auto* lightComp = CastTo<Component::LightComponent>(comp))
                {
                    m_SceneView->RemoveLightProxy(lightComp->GetComponentId());
                }
                if (auto* boardComp = CastTo<Component::BoardComponent>(comp))
                {
                    m_SceneView->RemoveBoardProxy(boardComp->GetComponentId());
                }
            }
        }

        if (m_ScreenSpaceBoardSink)
        {
            for (auto* comp : components)
            {
                if (auto* boardComp = CastTo<Component::BoardComponent>(comp))
                {
                    m_ScreenSpaceBoardSink->RemoveBoardProxy(boardComp->GetComponentId());
                }
            }
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            RemoveEntitySubtreeProxies(*child);
        }
    }

    bool World::IsEntityRoot(const Entity& entity) const
    {
        return entity.GetOuter() == this && entity.GetWorld() == this;
    }

    bool World::IsEntityInSubtree(const Entity& root, const Entity& candidate) const
    {
        if (&root == &candidate)
        {
            return true;
        }

        auto children = root.GetChildEntities();
        for (auto* child : children)
        {
            if (IsEntityInSubtree(*child, candidate))
            {
                return true;
            }
        }
        return false;
    }

    bool World::ContainsHeapOwnedEntity(const Entity& entity) const
    {
        if (entity.HasFlag(OF_HeapOwned))
        {
            return true;
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            if (ContainsHeapOwnedEntity(*child))
            {
                return true;
            }
        }
        return false;
    }

    bool World::HasPendingEntityAncestor(const Entity& entity) const
    {
        for (Entity* parent = entity.GetParentEntity(); parent != nullptr; parent = parent->GetParentEntity())
        {
            if (parent->IsPendingDestroy())
            {
                return true;
            }
        }
        return false;
    }

    bool World::ContainsInner(const IUnknown& owner, const IUnknown& inner) const
    {
        for (auto* existing : owner.GetInners())
        {
            if (existing == &inner)
            {
                return true;
            }
        }
        return false;
    }

    bool World::EraseInnerReference(IUnknown& owner, IUnknown& inner)
    {
        auto* ownerImpl = dynamic_cast<UnknownImpl*>(&owner);
        if (!ownerImpl)
        {
            return false;
        }

        for (auto it = ownerImpl->m_Inners.begin(); it != ownerImpl->m_Inners.end(); ++it)
        {
            if (*it == &inner)
            {
                ownerImpl->m_Inners.erase(it);
                return true;
            }
        }
        return false;
    }

    void World::AppendInnerReference(IUnknown& owner, IUnknown& inner)
    {
        auto* ownerImpl = dynamic_cast<UnknownImpl*>(&owner);
        if (!ownerImpl)
        {
            return;
        }

        ownerImpl->m_Inners.push_back(&inner);
    }

    void World::SetOuterReference(IUnknown& inner, IUnknown* outer)
    {
        auto* innerImpl = dynamic_cast<UnknownImpl*>(&inner);
        if (!innerImpl)
        {
            return;
        }

        innerImpl->m_Outer = outer;
    }

} // namespace NorvesLib::Core
