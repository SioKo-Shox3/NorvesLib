#include "Object/World.h"
#include "Component/BillboardComponent.h"
#include "Component/BoardComponent.h"
#include "Component/LightComponent.h"
#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/TextComponent.h"
#include "Container/UnorderedSet.h"
#include "Ecs/EcsRenderTypes.h"
#include "Ecs/EcsScheduler.h"
#include "Ecs/EcsTransformSystem.h"
#include "Ecs/EcsWorld.h"
#include "Engine/ComponentDataRegistry.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/PrefabAsset.h"
#include "Rendering/CanvasView.h"
#include "Rendering/IBoardProxySink.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SceneView.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace NorvesLib::Core
{
    namespace
    {
        constexpr uint32_t MaxPrefabNestedDepth = 16;
        /// Entity::m_EcsCreateSlot の「予約なし」の値
        constexpr uint32_t kNoEntityCreateSlot = 0xFFFFFFFFu;

        class ScopedWorldFlag
        {
        public:
            explicit ScopedWorldFlag(bool& flag) : m_Flag(flag), m_Previous(flag)
            {
                flag = true;
            }
            ~ScopedWorldFlag()
            {
                m_Flag = m_Previous;
            }
        private:
            bool& m_Flag;
            bool m_Previous;
        };

        /// 行の階層を読んで fn で書き換え、書き戻す（変更として記録される）。生きていない行は何もしない
        template <typename Fn>
        void EditHierarchy(Ecs::EcsWorld& ecs, Ecs::EcsEntity row, Fn&& fn)
        {
            if (const Ecs::EcsHierarchy* current = ecs.GetColumnValue<Ecs::EcsHierarchy>(row))
            {
                Ecs::EcsHierarchy next = *current;
                fn(next);
                ecs.WriteColumnValueOf(row, next);
            }
        }

        /// 検査 ECS_TRANSFORM の再帰: 階層のシステムが求めた EcsWorldTransform が、根から局所の変換をたどって求め直した値
        /// （親の列の値 × 局所の変換）と、祖先をたどる従来の Active 判定に一致するか。
        /// 行の無い Entity（段の最中に予約中）は ECS_BIND の担当なので飛ばす
        bool VerifyEntityTransforms(Ecs::EcsWorld& ecs, Entity& entity, const Ecs::EcsWorldTransform& parentWorld,
                                    bool parentActive, uint32_t& count, const char*& error)
        {
            const Ecs::EcsEntity row = entity.GetEcsEntity();
            if (row.IsNull() || !ecs.IsAlive(row))
            {
                return true;
            }
            const Ecs::EcsPropagatedTransform* propagated = ecs.GetColumnValue<Ecs::EcsPropagatedTransform>(row);
            const Ecs::EcsWorldTransform* stored = ecs.GetColumnValue<Ecs::EcsWorldTransform>(row);
            if (!propagated || !stored)
            {
                error = "行に EcsPropagatedTransform か EcsWorldTransform が無い";
                return false;
            }
            ++count;
            if (propagated->bValid == 0)
            {
                error = "階層のシステムが求めていない行がある";
                return false;
            }
            const Math::Transform expected =
                Ecs::EcsComposeTransform(Math::Transform(parentWorld.position, parentWorld.rotation, parentWorld.scale),
                                         entity.GetLocalTransform());
            if (!Ecs::EcsEqualWorldTransform(*stored,
                                             Ecs::EcsWorldTransform{expected.position, expected.rotation, expected.scale}))
            {
                error = "階層のシステムのワールドの変換が根から求め直した値と一致しない";
                return false;
            }
            const bool bActive = parentActive && entity.IsActive() && !entity.IsPendingDestroy();
            if ((propagated->bEffectiveActive != 0) != bActive)
            {
                error = "階層のシステムの EffectiveActive が祖先をたどる判定と一致しない";
                return false;
            }
            for (Entity* child : entity.GetChildEntities())
            {
                if (!VerifyEntityTransforms(ecs, *child, *stored, bActive, count, error))
                {
                    return false;
                }
            }
            return true;
        }

        /// 検査 ECS_BIND の再帰: entity の行と、その部分木が Outer/Inner と一致するか。食い違いは error に理由を入れて false
        bool VerifyEntityRows(Ecs::EcsWorld& ecs, Entity& entity, Ecs::EcsEntity parentRow, uint32_t depth,
                              uint32_t& count, uint32_t& componentCount, const char*& error)
        {
            ++count;
            const Ecs::EcsEntity row = entity.GetEcsEntity();
            if (row.IsNull() || !ecs.IsAlive(row))
            {
                error = "Entity の行が無い（または死んでいる）";
                return false;
            }
            if (ecs.IsOrphan(row))
            {
                error = "生きた Entity の行が孤児になっている";
                return false;
            }
            const Ecs::EcsObjectRef* ref = ecs.GetColumnValue<Ecs::EcsObjectRef>(row);
            if (!ref || ref->Object != static_cast<void*>(&entity) || ref->Generation != row.Generation)
            {
                error = "行の EcsObjectRef が Entity を指していない";
                return false;
            }
            const Ecs::EcsHierarchy* hierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(row);
            if (!hierarchy)
            {
                error = "行に EcsHierarchy が無い";
                return false;
            }
            if (hierarchy->Parent != parentRow || hierarchy->Depth != depth ||
                hierarchy->Kind != Ecs::kEcsHierarchyKindEntity)
            {
                error = "EcsHierarchy の Parent・Depth・Kind が Outer と一致しない";
                return false;
            }
            if (parentRow.IsNull() && (!hierarchy->PrevSibling.IsNull() || !hierarchy->NextSibling.IsNull()))
            {
                error = "根の行が兄弟を持っている";
                return false;
            }
            const Ecs::EcsEntity firstChild = hierarchy->FirstChild;
            const Ecs::EcsEntity lastChild = hierarchy->LastChild;
            const Container::VariableArray<Entity*> children = entity.GetChildEntities();
            Ecs::EcsEntity expected = firstChild;
            Ecs::EcsEntity previous;
            for (Entity* child : children)
            {
                const Ecs::EcsEntity childRow = child->GetEcsEntity();
                const Ecs::EcsHierarchy* childHierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(childRow);
                if (expected != childRow || !childHierarchy || childHierarchy->PrevSibling != previous)
                {
                    error = "EcsHierarchy の子の並びが Inner の並びと一致しない";
                    return false;
                }
                previous = childRow;
                expected = childHierarchy->NextSibling;
            }
            if (!expected.IsNull() || lastChild != previous)
            {
                error = "EcsHierarchy の子の連結の端が一致しない";
                return false;
            }
            // Component の行: 所有者の子の葉で、所有者の Component の連結に Inner の並びで入っている
            Ecs::EcsEntity expectedComponent = hierarchy->FirstComponent;
            Ecs::EcsEntity previousComponent;
            for (Component::Component* component : entity.GetComponents())
            {
                ++componentCount;
                const Ecs::EcsEntity componentRow = component->GetEcsEntity();
                if (componentRow.IsNull() || !ecs.IsAlive(componentRow))
                {
                    error = "Component の行が無い（または死んでいる）";
                    return false;
                }
                if (ecs.IsOrphan(componentRow))
                {
                    error = "生きた Component の行が孤児になっている";
                    return false;
                }
                const Ecs::EcsObjectRef* componentRef = ecs.GetColumnValue<Ecs::EcsObjectRef>(componentRow);
                if (!componentRef || componentRef->Object != static_cast<void*>(component) ||
                    componentRef->Generation != componentRow.Generation)
                {
                    error = "行の EcsObjectRef が Component を指していない";
                    return false;
                }
                const Ecs::EcsHierarchy* componentHierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(componentRow);
                if (!componentHierarchy || ecs.GetColumnValue<Ecs::EcsComponentFlags>(componentRow) == nullptr ||
                    ecs.GetColumnValue<Ecs::EcsTickConfig>(componentRow) == nullptr)
                {
                    error = "Component の行に EcsHierarchy・EcsComponentFlags・EcsTickConfig が無い";
                    return false;
                }
                {
                    const Ecs::EcsComponentRowKind rowKind = component->GetEcsRowKind();
                    const bool bHasMesh = ecs.GetColumnValue<Ecs::EcsMeshRender>(componentRow) != nullptr;
                    const bool bHasMega = ecs.GetColumnValue<Ecs::EcsMegaMesh>(componentRow) != nullptr;
                    const bool bHasSkinned = ecs.GetColumnValue<Ecs::EcsSkinnedRender>(componentRow) != nullptr;
                    const bool bExpectMesh = rowKind == Ecs::EcsComponentRowKind::Mesh || rowKind == Ecs::EcsComponentRowKind::MegaGeometry;
                    if (bHasMesh != bExpectMesh || bHasMega != (rowKind == Ecs::EcsComponentRowKind::MegaGeometry) ||
                        bHasSkinned != (rowKind == Ecs::EcsComponentRowKind::SkinnedMesh))
                    {
                        error = "Component の行の描画の値の列が、Component の種類と一致しない";
                        return false;
                    }
                }
                if (componentHierarchy->Kind != Ecs::kEcsHierarchyKindComponent || componentHierarchy->Parent != row ||
                    componentHierarchy->Depth != depth + 1 || !componentHierarchy->FirstChild.IsNull() ||
                    !componentHierarchy->LastChild.IsNull())
                {
                    error = "Component の EcsHierarchy が所有者の子の葉になっていない";
                    return false;
                }
                if (expectedComponent != componentRow || componentHierarchy->PrevSibling != previousComponent)
                {
                    error = "EcsHierarchy の Component の並びが Inner の並びと一致しない";
                    return false;
                }
                previousComponent = componentRow;
                expectedComponent = componentHierarchy->NextSibling;
            }
            if (!expectedComponent.IsNull() || hierarchy->LastComponent != previousComponent)
            {
                error = "EcsHierarchy の Component の連結の端が一致しない";
                return false;
            }
            for (Entity* child : children)
            {
                if (!VerifyEntityRows(ecs, *child, row, depth + 1, count, componentCount, error))
                {
                    return false;
                }
            }
            return true;
        }

        /// 順序を無視して、2 つのポインタの集合が一致するか（検査 WORLD_CLEANUP の照合用）
        template <typename T>
        bool SamePointerSet(const Container::VariableArray<T*>& lhs, const Container::VariableArray<T*>& rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            Container::VariableArray<T*> sortedLhs(lhs);
            Container::VariableArray<T*> sortedRhs(rhs);
            std::sort(sortedLhs.begin(), sortedLhs.end());
            std::sort(sortedRhs.begin(), sortedRhs.end());
            return sortedLhs == sortedRhs;
        }

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

        Entity* FindEntityByObjectIdRecursive(Entity* entity, uint64_t id)
        {
            if (!entity || entity->IsPendingDestroy())
            {
                return nullptr;
            }

            if (entity->GetObjectId() == id)
            {
                return entity;
            }

            for (Entity* child : entity->GetChildEntities())
            {
                if (Entity* found = FindEntityByObjectIdRecursive(child, id))
                {
                    return found;
                }
            }

            return nullptr;
        }

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
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;
        InitializeSchedule();
    }

    World::World(const FieldInitializer* initializer)
        : Object(initializer)
    {
        NextObjectId = 1;
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;
        InitializeSchedule();
    }

    World::World(const IUnknown* sourceObject)
        : Object(sourceObject)
    {
        NextObjectId = 1;
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;
        InitializeSchedule();
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
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;
        m_SceneView = nullptr;

        Object::Initialize();
    }

    void World::Finalize()
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        if (IsDeferringObjectRemoval())
        {
            LOG_ERROR("World::Finalize cannot run during update or cleanup");
            return;
        }
        EndTickCollection();
        m_TickEntries.clear();
        m_FixedTickEntries.clear();
        m_bHasTickSnapshot = false;
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

        FlushDeferredEntityRows();
        RunEcsBindingCheck(true);

        // 行はすべて消えた。補間の一覧は空にする
        m_RenderRows.clear();
        m_RenderReconcile.clear();
        m_RenderChangedRows.clear();
        m_bRenderRowsUnsorted = false;

        m_PendingDestroyEntities.clear();
        m_PendingDestroyComponents.clear();

        m_SceneView = nullptr;
        m_ScreenSpaceBoardSink = nullptr;
        NextObjectId = 1;
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;

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

    bool World::RemoveInner(IUnknown* inner)
    {
        // 公開の RemoveInner で切り離すものも、待ち参照と破棄予約の一覧から外す（外した後に削除されても壊れない）
        if (inner && inner->GetOuter() == this)
        {
            if (auto* entity = CastTo<Entity>(inner))
            {
                InvalidateTickEntitySubtree(*entity);
                ForgetPendingDestroySubtree(*entity);
                UnbindEntitySubtreeRows(*entity);
            }
            else if (auto* component = CastTo<Component::Component>(inner))
            {
                InvalidateTickComponent(*component);
                ForgetPendingDestroy(*inner);
            }
        }

        return UnknownImpl::RemoveInner(inner);
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

        if (IsDeferringObjectRemoval())
        {
            object->MarkForDestroy();
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

        if (IsDeferringObjectRemoval())
        {
            entity->MarkForDestroy();
            return true;
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

    Entity* World::FindEntityByObjectId(uint64_t id) const
    {
        if (id == 0)
        {
            return nullptr;
        }

        for (Entity* root : GetRootEntities())
        {
            if (Entity* found = FindEntityByObjectIdRecursive(root, id))
            {
                return found;
            }
        }

        return nullptr;
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
        TickWithFrameTimes(nullptr, deltaTime);
    }
    void World::Tick(const Engine::FrameTimes& times)
    {
        if (!times.IsValid())
            return;
        const auto snapshot = times;
        TickWithFrameTimes(&snapshot, 0);
    }
    void World::TickWithFrameTimes(const Engine::FrameTimes* times, float deltaTime)
    {
        if (!HasFlag(OF_Initialized) || IsDeferringObjectRemoval())
        {
            return;
        }
        // 前のフレームの LateTick が来なかったときの収集を閉じてから、このフレームの収集を開く
        EndTickCollection();
        m_Scheduler->BeginCollection();
        m_bTickCollectionOpen = true;
        m_StageTimes = times;
        m_StageDelta = deltaTime;
        RunWorldStage(m_StageTick);
    }

    void World::LateTick(float deltaTime)
    {
        LateTickWithFrameTimes(nullptr, deltaTime);
    }
    void World::LateTick(const Engine::FrameTimes& times)
    {
        if (!times.IsValid())
            return;
        const auto snapshot = times;
        LateTickWithFrameTimes(&snapshot, 0);
    }
    void World::LateTickWithFrameTimes(const Engine::FrameTimes* times, float deltaTime)
    {
        if (!HasFlag(OF_Initialized) || !m_bHasTickSnapshot || IsDeferringObjectRemoval())
        {
            return;
        }
        m_StageTimes = times;
        m_StageDelta = deltaTime;
        RunWorldStage(m_StageLateTick);
        EndTickCollection();
        RunEcsBindingCheck(false);
    }

    void World::EndTickCollection()
    {
        if (m_bTickCollectionOpen)
        {
            m_bTickCollectionOpen = false;
            m_Scheduler->EndCollection();
        }
    }

    void World::RunWorldStage(uint32_t stage)
    {
        m_Scheduler->RunStage(stage);
    }

    void World::InitializeSchedule()
    {
        m_EcsTransformSystem.reset();
        m_EcsWorld = Container::MakeUnique<Ecs::EcsWorld>();
        // 階層のシステムのきっかけになる列は追跡する（最初のアーキタイプを作る前に決める）。消費者がいない間はログを積まない
        m_EcsWorld->SetColumnTrackedOf<Ecs::EcsLocalTransform>();
        m_EcsWorld->SetColumnTrackedOf<Ecs::EcsHierarchy>();
        m_EcsWorld->SetColumnTrackedOf<Ecs::EcsEntityFlags>();
        m_Scheduler = Container::MakeUnique<Ecs::SystemScheduler>(*m_EcsWorld);
        Ecs::SystemScheduler& scheduler = *m_Scheduler;

        m_StageTick = scheduler.AddStage("Tick");
        m_StageLateTick = scheduler.AddStage("LateTick");
        m_StageFixedBegin = scheduler.AddStage("FixedBegin");
        m_StageFixedTick = scheduler.AddStage("FixedTick");
        m_StageFixedEnd = scheduler.AddStage("FixedEnd");
        m_StageSync = scheduler.AddStage("Sync");

        // 段の GameThreadOnly のシステムは登録順に単独の波で走る。従来の処理を同じ順・同じ位置で呼ぶ。
        auto add = [&](uint32_t stage, const char* name, Ecs::EcsSystemFn fn)
        {
            Ecs::SystemDesc desc;
            desc.Name = name;
            desc.Stage = stage;
            desc.Thread = ThreadPolicy::GameThreadOnly;
            desc.Run = fn;
            desc.User = this;
            const Ecs::EcsSystemId id = scheduler.RegisterSystem(desc);
            if (id == Ecs::kInvalidEcsSystem)
            {
                LOG_ERROR("World: failed to register ECS system %s", name);
            }
        };
        using Context = Ecs::EcsSystemContext;

        add(m_StageTick, "Tick.BuildSnapshot",
            [](void* user, const Context&) { static_cast<World*>(user)->BuildTickSnapshot(); });
        add(m_StageTick, "Tick.UpdateTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateWorldTransforms(); });
        add(m_StageTick, "Tick.DispatchGroups",
            [](void* user, const Context&)
            {
                auto* world = static_cast<World*>(user);
                world->DispatchTickGroups(Component::ETickGroup::Input, Component::ETickGroup::PoseFinalize,
                                          world->m_StageDelta, world->m_StageTimes);
            });
        add(m_StageTick, "Tick.Cleanup",
            [](void* user, const Context&) { static_cast<World*>(user)->CleanupDestroyedObjects(); });

        add(m_StageLateTick, "LateTick.UpdateTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateWorldTransforms(); });
        add(m_StageLateTick, "LateTick.DispatchPostPhysics",
            [](void* user, const Context&)
            {
                auto* world = static_cast<World*>(user);
                world->DispatchTickGroups(Component::ETickGroup::PostPhysics, Component::ETickGroup::PostPhysics,
                                          world->m_StageDelta, world->m_StageTimes);
            });
        add(m_StageLateTick, "LateTick.UpdateRenderTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateRenderTransforms(); });
        add(m_StageLateTick, "LateTick.DispatchCameraToPreRender",
            [](void* user, const Context&)
            {
                auto* world = static_cast<World*>(user);
                world->DispatchTickGroups(Component::ETickGroup::Camera, Component::ETickGroup::PreRender,
                                          world->m_StageDelta, world->m_StageTimes);
            });
        add(m_StageLateTick, "LateTick.Cleanup",
            [](void* user, const Context&) { static_cast<World*>(user)->CleanupDestroyedObjects(); });
        add(m_StageLateTick, "LateTick.EndSnapshot",
            [](void* user, const Context&)
            {
                auto* world = static_cast<World*>(user);
                world->m_TickEntries.clear();
                world->m_FixedTickEntries.clear();
                world->m_bHasTickSnapshot = false;
            });

        add(m_StageFixedBegin, "FixedBegin.UpdateTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateWorldTransforms(); });
        add(m_StageFixedBegin, "FixedBegin.PrepareInterpolation",
            [](void* user, const Context&) { static_cast<World*>(user)->PrepareRenderInterpolationStep(); });

        add(m_StageFixedTick, "FixedTick.Dispatch",
            [](void* user, const Context&) { static_cast<World*>(user)->DispatchFixedTickBody(); });

        add(m_StageFixedEnd, "FixedEnd.UpdateTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateWorldTransforms(); });
        add(m_StageFixedEnd, "FixedEnd.CaptureInterpolation",
            [](void* user, const Context&) { static_cast<World*>(user)->CaptureRenderInterpolationStep(); });
        add(m_StageFixedEnd, "FixedEnd.Cleanup",
            [](void* user, const Context&) { static_cast<World*>(user)->CleanupDestroyedObjects(); });

        add(m_StageSync, "Sync.SyncToSceneView",
            [](void* user, const Context&) { static_cast<World*>(user)->SyncToSceneViewBody(); });

        // 補間の列は行へ後から付け外しされる。型の番号は並列が始まる前に固定しておく
        (void)Ecs::EcsRegisterColumn<Ecs::EcsRenderTransform>();
        (void)Ecs::EcsRegisterColumn<Ecs::EcsRenderInterpolated>();
        (void)Ecs::EcsRegisterColumn<Ecs::EcsTransformHistory>();

        // World の木にいる全 Entity の行: Object への弱い参照・階層・flags・時間倍率・変換（局所とワールドのキャッシュ）・階層のシステムの比較用の列
        const Ecs::EcsColumnInfo entityColumns[] = {
            Ecs::EcsRegisterColumn<Ecs::EcsObjectRef>(),
            Ecs::EcsRegisterColumn<Ecs::EcsHierarchy>(),
            Ecs::EcsRegisterColumn<Ecs::EcsEntityFlags>(),
            Ecs::EcsRegisterColumn<Ecs::EcsTimeDilation>(),
            Ecs::EcsRegisterColumn<Ecs::EcsLocalTransform>(),
            Ecs::EcsRegisterColumn<Ecs::EcsWorldTransform>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPropagatedTransform>(),
        };
        m_EntityArchetype = m_EcsWorld->GetOrCreateArchetype(
            Container::Span<const Ecs::EcsColumnInfo>(entityColumns, 7));
        if (m_EntityArchetype == Ecs::kInvalidEcsArchetype)
        {
            LOG_ERROR("World: Entity 用の ECS のアーキタイプを作れなかった");
        }

        // Component の行: 所有者の子の葉。flags と Tick の設定を列に持ち、Mesh 系は描画の値の列も持つ
        const Ecs::EcsColumnInfo basicColumns[] = {
            Ecs::EcsRegisterColumn<Ecs::EcsObjectRef>(),
            Ecs::EcsRegisterColumn<Ecs::EcsHierarchy>(),
            Ecs::EcsRegisterColumn<Ecs::EcsComponentFlags>(),
            Ecs::EcsRegisterColumn<Ecs::EcsTickConfig>(),
        };
        const Ecs::EcsColumnInfo meshColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsMeshRender>(),
        };
        const Ecs::EcsColumnInfo megaColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsMeshRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsMegaMesh>(),
        };
        const Ecs::EcsColumnInfo skinnedColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsSkinnedRender>(),
        };
        const auto createComponentArchetype = [this](Ecs::EcsComponentRowKind kind, Container::Span<const Ecs::EcsColumnInfo> columns)
        {
            Ecs::EcsArchetypeId& archetype = m_ComponentArchetypes[static_cast<size_t>(kind)];
            archetype = m_EcsWorld->GetOrCreateArchetype(columns);
            if (archetype == Ecs::kInvalidEcsArchetype)
            {
                LOG_ERROR("World: Component 用の ECS のアーキタイプを作れなかった");
            }
        };
        createComponentArchetype(Ecs::EcsComponentRowKind::Basic, Container::Span<const Ecs::EcsColumnInfo>(basicColumns, 4));
        createComponentArchetype(Ecs::EcsComponentRowKind::Mesh, Container::Span<const Ecs::EcsColumnInfo>(meshColumns, 5));
        createComponentArchetype(Ecs::EcsComponentRowKind::MegaGeometry, Container::Span<const Ecs::EcsColumnInfo>(megaColumns, 6));
        createComponentArchetype(Ecs::EcsComponentRowKind::SkinnedMesh, Container::Span<const Ecs::EcsColumnInfo>(skinnedColumns, 5));

        // 段の最中に孤児にした行は、各段の同期点で消す
        const uint32_t stages[] = {m_StageTick, m_StageLateTick, m_StageFixedBegin,
                                   m_StageFixedTick, m_StageFixedEnd, m_StageSync};
        for (const uint32_t stage : stages)
        {
            scheduler.SetSyncHook(stage,
                [](void* user, Ecs::EcsWorld&) { static_cast<World*>(user)->FlushDeferredEntityRows(); }, this);
        }
    }

    void World::BeginFixedStep()
    {
        RunWorldStage(m_StageFixedBegin);
    }

    void World::EndFixedStep()
    {
        RunWorldStage(m_StageFixedEnd);
    }

    void World::BuildTickSnapshot()
    {
        if (++m_TickSerial == 0)
        {
            ++m_TickSerial;
        }
        m_TickEntries.clear();
        m_FixedTickEntries.clear();
        size_t ordinal = 0;
        for (IUnknown* inner : GetInners())
        {
            if (auto* entity = CastTo<Entity>(inner))
            {
                CollectTickEntries(*entity, ordinal);
            }
        }
        std::sort(m_TickEntries.begin(), m_TickEntries.end(), TickDispatchLess);
        m_bHasTickSnapshot = true;
    }

    void World::CollectTickEntries(Entity& entity, size_t& ordinal)
    {
        if (entity.IsPendingDestroy())
        {
            return;
        }
        int16_t entityPriority = 0;
        bool bFoundDefault = false;
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* component = CastTo<Component::Component>(inner))
            {
                if ((component->GetTickGroupMask() & Component::TickGroupBit(Component::ETickGroup::Default)) != 0)
                {
                    if (!bFoundDefault || component->GetTickPriority() < entityPriority)
                    {
                        entityPriority = component->GetTickPriority();
                    }
                    bFoundDefault = true;
                }
            }
        }
        m_TickEntries.push_back({&entity, nullptr, Component::ETickGroup::Default,
            Component::ETickGroup::Default, entityPriority, ordinal++});
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* component = CastTo<Component::Component>(inner))
            {
                const size_t order = ordinal++;
                const auto primary = component->GetTickGroup();
                m_FixedTickEntries.push_back({&entity, component, Component::ETickGroup::Default, primary, 0, order});
                for (uint8_t index = 0; index < Component::TickGroupCount; ++index)
                {
                    const auto group = static_cast<Component::ETickGroup>(index);
                    if ((component->GetTickGroupMask() & Component::TickGroupBit(group)) != 0)
                    {
                        m_TickEntries.push_back({&entity, component, group, primary, component->GetTickPriority(), order});
                    }
                }
            }
        }
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* child = CastTo<Entity>(inner))
            {
                CollectTickEntries(*child, ordinal);
            }
        }
    }

    bool World::CanDispatchEntity(const Entity& entity) const
    {
        if (entity.GetWorld() != this || !entity.IsTickEnabled())
        {
            return false;
        }
        // 自分の今の状態は常に見る。祖先を含めた判定は、階層のシステムが求めた EffectiveActive を読む
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            return false;
        }
        // 作成予約中の行・付け替え待ちがある間と、Active・PendingDestroy の変更がまだ伝播されていない間は、キャッシュが古いので祖先をたどる
        if (!m_bLegacyTransformPropagation && m_EcsWorld && m_PendingEntityRelinks.empty() && m_PendingEntityCreates.empty() &&
            m_EcsTransformSystem && !m_EcsTransformSystem->HasUnpropagatedFlagChanges())
        {
            const Ecs::EcsEntity row = entity.GetEcsEntity();
            if (!row.IsNull() && m_EcsWorld->IsAlive(row))
            {
                const Ecs::EcsPropagatedTransform* propagated = m_EcsWorld->GetColumnValue<Ecs::EcsPropagatedTransform>(row);
                if (propagated != nullptr && propagated->bValid != 0)
                {
                    return propagated->bEffectiveActive != 0;
                }
            }
        }
        // 行が無い・まだ求めていない・旧の経路のときは、祖先をたどる
        for (const Entity* current = &entity; current; current = current->GetParentEntity())
        {
            if (!current->IsActive() || current->IsPendingDestroy())
            {
                return false;
            }
        }
        return true;
    }

    bool World::TryResolveTickDelta(const Entity& owner, const Component::Component* component, float& out) const
    {
        if (!m_bHasActiveTickTime || owner.GetWorld() != this)
            return false;
        const auto channel = component ? component->GetTimeChannel() : Engine::TimeChannel::World;
        if (!Engine::IsValidTimeChannel(channel))
            return false;
        const float base = m_ActiveTickTimes ? m_ActiveTickTimes->GetDelta(channel) : m_ActiveTickDelta;
        const bool local = channel == Engine::TimeChannel::World || channel == Engine::TimeChannel::Animation;
        const double delta = double(base) * (local ? owner.GetCustomTimeDilation() : 1.f);
        if (!std::isfinite(delta) || delta < 0 || delta > std::numeric_limits<float>::max())
            return false;
        out = static_cast<float>(delta);
        return true;
    }
    bool World::TryGetComponentTickDelta(const Component::Component& component, float& out) const
    {
        const auto* owner = component.GetOwner();
        return owner && TryResolveTickDelta(*owner, &component, out);
    }
    void World::DispatchTickGroups(Component::ETickGroup first, Component::ETickGroup last, float deltaTime,
                                   const Engine::FrameTimes* times)
    {
        ScopedWorldFlag dispatch(m_bDispatchingTicks);
        struct TickTimeContext
        {
            World& Owner;
            const Engine::FrameTimes* PreviousTimes;
            float PreviousDelta;
            bool PreviousActive;
            TickTimeContext(World& owner, const Engine::FrameTimes* times, float delta)
                : Owner(owner), PreviousTimes(owner.m_ActiveTickTimes), PreviousDelta(owner.m_ActiveTickDelta),
                  PreviousActive(owner.m_bHasActiveTickTime)
            {
                owner.m_ActiveTickTimes = times;
                owner.m_ActiveTickDelta = delta;
                owner.m_bHasActiveTickTime = true;
            }
            ~TickTimeContext()
            {
                Owner.m_ActiveTickTimes = PreviousTimes;
                Owner.m_ActiveTickDelta = PreviousDelta;
                Owner.m_bHasActiveTickTime = PreviousActive;
            }
        } context(*this, times, deltaTime);
        auto publishedGroup = Component::ETickGroup::Count;
        for (const TickDispatchEntry& slot : m_TickEntries)
        {
            const TickDispatchEntry entry = slot;
            if (entry.Group < first || entry.Group > last)
            {
                continue;
            }
            if (publishedGroup != Component::ETickGroup::Count && entry.Group != publishedGroup)
            {
                // 前の群で変更された親の姿勢を子階層へ公開する。同群内では確定しない。
                UpdateWorldTransforms();
            }
            publishedGroup = entry.Group;
            if (!entry.Owner || !CanDispatchEntity(*entry.Owner))
            {
                continue;
            }
            if (!entry.Target)
            {
                float tickDelta = 0;
                if (!TryResolveTickDelta(*entry.Owner, nullptr, tickDelta))
                    continue;
                entry.Owner->Tick(tickDelta);
            }
            else if (entry.Target->GetOwner() == entry.Owner && entry.Target->IsActive() &&
                     entry.Target->IsTickEnabled() && !entry.Target->IsPendingDestroy())
            {
                float tickDelta = 0;
                if (!TryGetComponentTickDelta(*entry.Target, tickDelta))
                    continue;
                const auto previousGroup = entry.Target->m_DispatchPrimaryGroup;
                const bool bPrevious = entry.Target->m_bHasDispatchPrimaryGroup;
                entry.Target->m_DispatchPrimaryGroup = entry.PrimaryGroup;
                entry.Target->m_bHasDispatchPrimaryGroup = true;
                // callback自身がheapから破棄されても、その後にTargetを参照解除しない。
                auto restore = [&]() {
                    if (slot.Owner == entry.Owner && slot.Target == entry.Target)
                    {
                        entry.Target->m_DispatchPrimaryGroup = previousGroup;
                        entry.Target->m_bHasDispatchPrimaryGroup = bPrevious;
                    }
                };
                try
                {
                    entry.Target->OnTickGroup(entry.Group, tickDelta);
                }
                catch (...)
                {
                    restore();
                    throw;
                }
                restore();
            }
        }
    }

    void World::InvalidateTickComponent(Component::Component& component)
    {
        component.m_bHasDispatchPrimaryGroup = false;
        for (auto& pending : m_PendingTickComponents)
        {
            if (pending == &component)
            {
                pending = nullptr;
            }
        }
        InvalidateTickTarget(Container::Span<TickDispatchEntry>(m_TickEntries.data(), m_TickEntries.size()), &component);
        InvalidateTickTarget(Container::Span<TickDispatchEntry>(m_FixedTickEntries.data(), m_FixedTickEntries.size()), &component);
    }

    void World::InvalidateTickEntitySubtree(Entity& entity)
    {
        for (auto& pending : m_PendingTickEntities)
        {
            if (pending == &entity)
            {
                pending = nullptr;
            }
        }
        InvalidateTickOwner(Container::Span<TickDispatchEntry>(m_TickEntries.data(), m_TickEntries.size()), &entity);
        InvalidateTickOwner(Container::Span<TickDispatchEntry>(m_FixedTickEntries.data(), m_FixedTickEntries.size()), &entity);
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* child = CastTo<Entity>(inner))
            {
                InvalidateTickEntitySubtree(*child);
            }
            else if (auto* component = CastTo<Component::Component>(inner))
            {
                InvalidateTickComponent(*component);
            }
        }
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

    void World::SyncToSceneView(const Rendering::MaterialResources* materials,
                                const Rendering::MeshResources* meshes)
    {
        m_StageMaterials = materials;
        m_StageMeshes = meshes;
        if (m_Scheduler->IsRunning())
        {
            // 段の最中に呼ばれたときは再入できないので、段を通さずに同じ処理を行う
            SyncToSceneViewBody();
            return;
        }
        RunWorldStage(m_StageSync);
    }

    void World::SyncToSceneViewBody()
    {
        const Rendering::MaterialResources* materials = m_StageMaterials;
        const Rendering::MeshResources* meshes = m_StageMeshes;
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

        UpdateRenderTransforms();

        Container::UnorderedSet<uint64_t> liveMeshComponentIds;
        Container::UnorderedSet<uint64_t> liveSkinnedMeshComponentIds;
        Container::UnorderedSet<uint64_t> liveMegaGeometryObjectIds;
        Container::UnorderedSet<uint64_t> liveLightIds;
        Container::UnorderedSet<uint64_t> liveScreenBoardComponentIds;
        Container::UnorderedSet<uint64_t> liveWorldBoardComponentIds;
        liveMeshComponentIds.reserve(m_Inners.size());
        liveSkinnedMeshComponentIds.reserve(m_Inners.size());
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
                meshes,
                liveMeshComponentIds,
                liveSkinnedMeshComponentIds,
                liveMegaGeometryObjectIds,
                liveLightIds,
                liveScreenBoardComponentIds,
                liveWorldBoardComponentIds,
                enabledComponentDataRegistry);
        }

        if (m_SceneView)
        {
            m_SceneView->RemoveStaleMeshProxies(liveMeshComponentIds);
            m_SceneView->RemoveStaleSkinnedMeshProxies(liveSkinnedMeshComponentIds);
            m_SceneView->RemoveStaleMegaGeometryProxies(liveMegaGeometryObjectIds);
            m_SceneView->RemoveStaleLightProxies(liveLightIds);
            m_SceneView->RemoveStaleBoardProxies(liveWorldBoardComponentIds);
        }

        if (m_ScreenSpaceBoardSink)
        {
            m_ScreenSpaceBoardSink->RemoveStaleBoardProxies(liveScreenBoardComponentIds);
        }

    }

    void World::CollectTransientBoardProxies(Container::VariableArray<Rendering::BoardProxy>& outProxies)
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        auto roots = GetRootEntities();
        for (Entity* entity : roots)
        {
            CollectTransientBoardProxiesRecursive(*entity, outProxies);
        }
    }

    void World::UpdateWorldTransforms()
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        // 同期点まで待たせた付け替えと作成予約（行がまだ無い Entity）がある間は、階層の列が Outer/Inner と食い違う。この間は Outer/Inner を正とする旧の再帰で求める
        const bool bLegacy = m_bLegacyTransformPropagation || !m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype ||
                             !m_PendingEntityRelinks.empty() || !m_PendingEntityCreates.empty();
        if (bLegacy)
        {
            auto roots = GetRootEntities();
            for (auto* entity : roots)
            {
                UpdateEntityTransformRecursive(*entity, Math::Transform::Identity);
            }
        }
        else
        {
            (void)PropagateEcsTransforms();
        }
        // 検査 ECS_TRANSFORM が有効なときだけ、全行を根から求め直した値と照合する
        RunEcsTransformCheck();
    }

    bool World::SetRenderInterpolationAlpha(float alpha)
    {
        if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
            return false;
        m_RenderInterpolationAlpha = alpha;
        return true;
    }
    namespace
    {
        /// 行の履歴を捨て、描画の変換を求め直させる（列が無ければ何もしない）
        void ResetRenderColumns(Ecs::EcsWorld& ecs, Ecs::EcsEntity row)
        {
            if (Ecs::EcsTransformHistory* history = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row))
            {
                history->History.Clear();
            }
            if (Ecs::EcsRenderTransform* render = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row))
            {
                render->bValid = 0;
            }
        }

        /// 描画の変換の列の値 ↔ Math::Transform
        Math::Transform LoadRenderTransform(const Ecs::EcsRenderTransform& column)
        {
            return Math::Transform(column.position, column.rotation, column.scale);
        }

        void StoreRenderTransform(Ecs::EcsRenderTransform& column, const Math::Transform& transform)
        {
            column.position = transform.position;
            column.rotation = transform.rotation;
            column.scale = transform.scale;
        }

        /// 検査 ECS_RENDER_TRANSFORM の比較（成分ごとに数値として等しいか。NaN は同じ bit のときだけ等しい）
        bool SameRenderTransform(const Math::Transform& lhs, const Math::Transform& rhs)
        {
            return Ecs::EcsEqualWorldTransform(Ecs::EcsWorldTransform{lhs.position, lhs.rotation, lhs.scale},
                                               Ecs::EcsWorldTransform{rhs.position, rhs.rotation, rhs.scale});
        }

        /// 根から数えた Entity の深さ（Outer/Inner の木で数える。階層の列が同期点まで古い間も正しい）
        uint32_t CountEntityDepth(const Entity& entity)
        {
            uint32_t depth = 0;
            for (const Entity* cursor = entity.GetParentEntity(); cursor; cursor = cursor->GetParentEntity())
            {
                ++depth;
            }
            return depth;
        }

        /// 補間の列（タグ・描画の変換・履歴）のどれかを持つか
        bool HasAnyRenderColumn(Ecs::EcsWorld& ecs, Ecs::EcsEntity row)
        {
            return ecs.GetColumnValue<Ecs::EcsRenderInterpolated>(row) != nullptr ||
                   ecs.GetColumnValue<Ecs::EcsRenderTransform>(row) != nullptr ||
                   ecs.GetColumnValue<Ecs::EcsTransformHistory>(row) != nullptr;
        }
    } // namespace

    void World::SetRenderInterpolationAllowed(bool allowed)
    {
        if (m_bRenderInterpolationAllowed == allowed)
            return;
        m_bRenderInterpolationAllowed = allowed;
        // 履歴と描画の変換の列は許可に合わせて付け外しされる。次の更新で見直し、それまでも古い値を返さないよう値を下ろす
        if (!m_EcsWorld)
            return;
        for (const Ecs::EcsEntity row : m_RenderRows)
        {
            m_RenderReconcile.push_back(row);
            ResetRenderColumns(*m_EcsWorld, row);
        }
    }

    Entity* World::ResolveRenderEntity(Ecs::EcsEntity row) const
    {
        if (!m_EcsWorld || row.IsNull() || !m_EcsWorld->IsAlive(row))
            return nullptr;
        const Ecs::EcsObjectRef* ref = m_EcsWorld->GetColumnValue<Ecs::EcsObjectRef>(row);
        const Ecs::EcsHierarchy* hierarchy = m_EcsWorld->GetColumnValue<Ecs::EcsHierarchy>(row);
        if (!ref || ref->Object == nullptr || ref->Generation != row.Generation || !hierarchy ||
            hierarchy->Kind != Ecs::kEcsHierarchyKindEntity)
            return nullptr;
        return static_cast<Entity*>(ref->Object);
    }

    bool World::IsRenderParticipating(const Entity& entity) const
    {
        if (!m_bRenderInterpolationAllowed)
            return false;
        for (const Entity* cursor = &entity; cursor; cursor = cursor->GetParentEntity())
            if (cursor->m_bRenderInterpolationEnabled)
                return true;
        return false;
    }

    void World::NoteRenderInterpolationChange(Entity& entity)
    {
        if (!m_EcsWorld || entity.m_EcsEntity.IsNull())
            return;
        m_RenderReconcile.push_back(entity.m_EcsEntity);
        // 付け替えや要求の変化で親子の前後が変わりうる。次の処理の前に並べ直す
        if (!m_RenderRows.empty())
            m_bRenderRowsUnsorted = true;
    }

    void World::NoteRenderRowCreated(Entity& entity)
    {
        const Ecs::EcsEntity row = entity.m_EcsEntity;
        if (!m_EcsWorld || row.IsNull())
            return;
        // 作られた行は、ワールドの変換を初めて求めたときに描画の更新へ知らせる。旧の再帰では行の作成が唯一の目印なので、ここでも積む
        QueueRenderChange(row);
        if (entity.m_bRenderInterpolationEnabled || (!m_RenderRows.empty() && IsRenderParticipating(entity)))
            m_RenderReconcile.push_back(row);
    }

    void World::ResetEntityRenderInterpolation(Entity& entity)
    {
        if (m_EcsWorld)
            ResetRenderColumns(*m_EcsWorld, entity.m_EcsEntity);
        for (auto* inner : entity.GetInners())
            if (auto* child = CastTo<Entity>(inner))
                ResetEntityRenderInterpolation(*child);
    }

    bool World::ReconcileRenderRow(Entity& entity, Ecs::EcsEntity row)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        // 要求と許可から決まる組: タグ = 要求した、描画の変換 = 要求した行かその子孫（許可されている間）、履歴 = 要求して許可されている
        const bool bTag = entity.m_bRenderInterpolationEnabled;
        const bool bRender = IsRenderParticipating(entity);
        const bool bHistory = m_bRenderInterpolationAllowed && bTag;
        const bool bHadTag = ecs.GetColumnValue<Ecs::EcsRenderInterpolated>(row) != nullptr;
        const bool bHadRender = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row) != nullptr;
        const bool bHadHistory = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row) != nullptr;

        if (bTag != bHadTag)
        {
            if (bTag)
                (void)ecs.AddColumnOf(row, Ecs::EcsRenderInterpolated{});
            else
                (void)ecs.RemoveColumnOf<Ecs::EcsRenderInterpolated>(row);
        }
        if (bRender != bHadRender)
        {
            if (bRender)
            {
                // 値は 0（bValid が 0）。次の更新が求める
                (void)ecs.AddColumnOf(row, Ecs::EcsRenderTransform{});
            }
            else
            {
                (void)ecs.RemoveColumnOf<Ecs::EcsRenderTransform>(row);
                // 描画の値が補間の結果からワールドの変換へ戻る。所有する描画状態を直させる
                QueueRenderChange(row);
            }
        }
        if (bHistory != bHadHistory)
        {
            if (bHistory)
                (void)ecs.AddColumnOf(row, Ecs::EcsTransformHistory{});
            else
                (void)ecs.RemoveColumnOf<Ecs::EcsTransformHistory>(row);
        }
        if (!bHadTag && !bHadRender && !bHadHistory && (bTag || bRender || bHistory))
        {
            m_RenderRows.push_back(row);
            m_bRenderRowsUnsorted = true;
        }
        return bRender != bHadRender;
    }

    void World::ReconcileRenderInterpolation()
    {
        if (m_RenderReconcile.empty() || !m_EcsWorld)
            return;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        struct Item
        {
            uint32_t Depth;
            Ecs::EcsEntity Row;
        };
        Container::VariableArray<Item> work;
        for (const Ecs::EcsEntity row : m_RenderReconcile)
            if (const Entity* entity = ResolveRenderEntity(row))
                work.push_back(Item{CountEntityDepth(*entity), row});
        m_RenderReconcile.clear();

        bool bLostRows = false;
        Container::VariableArray<Item> current;
        while (!work.empty())
        {
            // 親を先に直す（子の要求は親の描画の変換の有無で決まる）。最も浅い深さの行をまとめて取り出す
            uint32_t depth = (std::numeric_limits<uint32_t>::max)();
            for (const Item& item : work)
                depth = (std::min)(depth, item.Depth);
            current.clear();
            size_t kept = 0;
            for (size_t i = 0; i < work.size(); ++i)
            {
                if (work[i].Depth == depth)
                    current.push_back(work[i]);
                else
                    work[kept++] = work[i];
            }
            work.resize(kept);
            std::sort(current.begin(), current.end(), [](const Item& lhs, const Item& rhs)
                      { return lhs.Row.Index != rhs.Row.Index ? lhs.Row.Index < rhs.Row.Index
                                                              : lhs.Row.Generation < rhs.Row.Generation; });
            current.erase(std::unique(current.begin(), current.end(),
                                      [](const Item& lhs, const Item& rhs) { return lhs.Row == rhs.Row; }),
                          current.end());
            for (const Item& item : current)
            {
                Entity* entity = ResolveRenderEntity(item.Row);
                if (!entity)
                    continue;
                if (ReconcileRenderRow(*entity, item.Row))
                {
                    // 描画の変換の有無が変わった行の子は、要求の組が変わりうる
                    for (Entity* child : entity->GetChildEntities())
                        if (!child->m_EcsEntity.IsNull())
                            work.push_back(Item{depth + 1, child->m_EcsEntity});
                }
                if (!HasAnyRenderColumn(ecs, item.Row))
                    bLostRows = true;
            }
        }
        if (bLostRows)
            CompactRenderRows();
    }

    void World::CompactRenderRows()
    {
        if (!m_EcsWorld)
        {
            m_RenderRows.clear();
            return;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        size_t kept = 0;
        for (const Ecs::EcsEntity row : m_RenderRows)
        {
            if (ResolveRenderEntity(row) != nullptr && HasAnyRenderColumn(ecs, row))
                m_RenderRows[kept++] = row;
        }
        m_RenderRows.resize(kept);
    }

    void World::SortRenderRows()
    {
        m_bRenderRowsUnsorted = false;
        struct Key
        {
            uint32_t Depth;
            Ecs::EcsEntity Row;
        };
        Container::VariableArray<Key> keys;
        for (const Ecs::EcsEntity row : m_RenderRows)
            if (const Entity* entity = ResolveRenderEntity(row))
                keys.push_back(Key{CountEntityDepth(*entity), row});
        std::stable_sort(keys.begin(), keys.end(), [](const Key& lhs, const Key& rhs)
                         { return lhs.Depth != rhs.Depth ? lhs.Depth < rhs.Depth : lhs.Row.Index < rhs.Row.Index; });
        m_RenderRows.clear();
        for (const Key& key : keys)
            m_RenderRows.push_back(key.Row);
    }

    void World::QueueRenderChange(Ecs::EcsEntity row)
    {
        if (!m_EcsWorld)
            return;
        Ecs::EcsPropagatedTransform* propagated = m_EcsWorld->GetColumnValue<Ecs::EcsPropagatedTransform>(row);
        if (!propagated || propagated->bRenderQueued != 0)
            return;
        propagated->bRenderQueued = 1;
        m_RenderChangedRows.push_back(row);
    }

    void World::NoteRenderTransformChanged(Entity& entity)
    {
        if (entity.m_RenderTransformVersion != UINT64_MAX)
            ++entity.m_RenderTransformVersion;
        // simulation版のversionは触らない。alphaだけ変わる0step frameでもproxyを更新する。
        for (auto* inner : entity.GetInners())
            if (auto* component = CastTo<Component::Component>(inner))
                if (CastTo<Component::MeshComponent>(component) || CastTo<Component::SkinnedMeshComponent>(component))
                    component->MarkRenderStateDirty();
    }

    void World::FlushRenderChangedRows()
    {
        if (m_RenderChangedRows.empty() || !m_EcsWorld)
            return;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        for (const Ecs::EcsEntity row : m_RenderChangedRows)
        {
            if (Ecs::EcsPropagatedTransform* propagated = ecs.GetColumnValue<Ecs::EcsPropagatedTransform>(row))
                propagated->bRenderQueued = 0;
            Entity* entity = ResolveRenderEntity(row);
            // 補間する行とその子孫は UpdateRenderRows が見る。破棄予約の部分木は旧と同じく触らない
            if (!entity || entity->IsPendingDestroy() || HasPendingEntityAncestor(*entity) ||
                ecs.GetColumnValue<Ecs::EcsRenderTransform>(row) != nullptr)
                continue;
            NoteRenderTransformChanged(*entity);
        }
        m_RenderChangedRows.clear();
    }

    void World::StepRenderHistory(bool bCapture)
    {
        if (!HasFlag(OF_Initialized) || !m_bRenderInterpolationAllowed || !m_EcsWorld)
            return;
        ReconcileRenderInterpolation();
        if (m_RenderRows.empty())
            return;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        for (const Ecs::EcsEntity row : m_RenderRows)
        {
            // 履歴を持つのは、補間を要求して許可されている行だけ（子孫は履歴を使わない）
            Ecs::EcsTransformHistory* history = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row);
            if (!history)
                continue;
            Entity* entity = ResolveRenderEntity(row);
            if (!entity || entity->IsPendingDestroy() || HasPendingEntityAncestor(*entity))
                continue;
            const Entity* parent = entity->GetParentEntity();
            const uint64_t parentId = parent ? parent->GetObjectId() : 0;
            if (Ecs::EcsRenderTransform* render = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row))
            {
                if (render->ParentId != parentId)
                {
                    history->History.Clear();
                    render->ParentId = parentId;
                }
            }
            if (bCapture)
                history->History.Capture(entity->GetWorldTransform());
            else
                history->History.Prepare(entity->GetWorldTransform());
        }
    }

    void World::PrepareRenderInterpolationStep()
    {
        StepRenderHistory(false);
    }

    void World::CaptureRenderInterpolationStep()
    {
        StepRenderHistory(true);
    }

    void World::UpdateRenderRows()
    {
        if (m_RenderRows.empty() || !m_EcsWorld)
            return;
        if (m_bRenderRowsUnsorted)
            SortRenderRows();
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        bool bLostRows = false;
        // 親が先（深さの順）。補間する行は履歴を評価し、その子孫は「親の描画の変換 × 局所の変換」で求める
        for (const Ecs::EcsEntity row : m_RenderRows)
        {
            Entity* entity = ResolveRenderEntity(row);
            if (!entity)
            {
                bLostRows = true;
                continue;
            }
            Ecs::EcsRenderTransform* render = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row);
            if (!render || entity->IsPendingDestroy() || HasPendingEntityAncestor(*entity))
                continue;
            Ecs::EcsTransformHistory* history = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row);
            const bool bTag = ecs.GetColumnValue<Ecs::EcsRenderInterpolated>(row) != nullptr;
            const Entity* parent = entity->GetParentEntity();
            const uint64_t parentId = parent ? parent->GetObjectId() : 0;
            const Ecs::EcsRenderTransform* parentRender =
                parent ? ecs.GetColumnValue<Ecs::EcsRenderTransform>(parent->m_EcsEntity) : nullptr;
            const bool bInherited = m_bRenderInterpolationAllowed && parentRender != nullptr;
            if (render->ParentId != parentId || (render->bInherited != 0) != bInherited)
            {
                if (history)
                    history->History.Clear();
            }
            render->ParentId = parentId;
            render->bInherited = bInherited ? 1 : 0;
            const bool bInterpolated = m_bRenderInterpolationAllowed && (bInherited || bTag);
            const Math::Transform world = entity->GetWorldTransform();
            Math::Transform value = world;
            if (bInherited)
                value = LoadRenderTransform(*parentRender) * entity->GetLocalTransform();
            else if (bInterpolated && history && history->History.Prepare(world))
                (void)history->History.Evaluate(m_RenderInterpolationAlpha, value);
            const bool bChanged = render->bValid == 0 || LoadRenderTransform(*render) != value;
            StoreRenderTransform(*render, value);
            render->bValid = 1;
            if (bChanged)
                NoteRenderTransformChanged(*entity);
        }
        if (bLostRows)
            CompactRenderRows();
    }

    void World::UpdateRenderTransforms()
    {
        if (!HasFlag(OF_Initialized))
            return;
        UpdateWorldTransforms();
        ReconcileRenderInterpolation();
        UpdateRenderRows();
        FlushRenderChangedRows();
        // 検査 ECS_RENDER_TRANSFORM が有効なときだけ、全行を旧の式で求め直した値と照合する
        RunEcsRenderTransformCheck();
    }

    bool World::VerifyRenderEntity(Entity& entity, const Math::Transform& parentRender, bool bParentInterpolated,
                                   uint32_t& count, const char*& error)
    {
        // 旧の再帰と同じく、破棄予約の部分木は見ない
        if (entity.IsPendingDestroy())
            return true;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        const bool bInherited = m_bRenderInterpolationAllowed && bParentInterpolated;
        const bool bInterpolated = m_bRenderInterpolationAllowed && (bInherited || entity.IsRenderInterpolationEnabled());
        const Math::Transform world = entity.GetWorldTransform();
        const Ecs::EcsEntity row = entity.GetEcsEntity();
        const bool bHasRow = !row.IsNull() && ecs.IsAlive(row);
        Math::Transform expected = world;
        if (bInherited)
        {
            expected = parentRender * entity.GetLocalTransform();
        }
        else if (bInterpolated)
        {
            const Ecs::EcsTransformHistory* history = bHasRow ? ecs.GetColumnValue<Ecs::EcsTransformHistory>(row) : nullptr;
            if (history)
            {
                // 履歴の写しで求める（本体の履歴は変えない）
                Scene::RenderTransformHistory copy = history->History;
                if (copy.Prepare(world))
                    (void)copy.Evaluate(m_RenderInterpolationAlpha, expected);
            }
        }
        if (bHasRow)
        {
            ++count;
            const bool bTag = ecs.GetColumnValue<Ecs::EcsRenderInterpolated>(row) != nullptr;
            const bool bRender = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row) != nullptr;
            const bool bHistory = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row) != nullptr;
            if (bTag != entity.IsRenderInterpolationEnabled())
            {
                error = "補間のタグの有無が補間の要求と一致しない";
                return false;
            }
            if (bRender != bInterpolated)
            {
                error = "描画の変換の列の有無が補間の要求と許可から決まる組と一致しない";
                return false;
            }
            if (bHistory != (m_bRenderInterpolationAllowed && entity.IsRenderInterpolationEnabled()))
            {
                error = "履歴の列の有無が補間の要求と許可から決まる組と一致しない";
                return false;
            }
            if (!SameRenderTransform(entity.GetRenderWorldTransform(), expected))
            {
                error = "描画の変換が旧の式で求め直した値と一致しない";
                return false;
            }
        }
        for (Entity* child : entity.GetChildEntities())
        {
            if (!VerifyRenderEntity(*child, expected, bInterpolated, count, error))
                return false;
        }
        return true;
    }

    bool World::VerifyEcsRenderTransform(uint32_t* outCount)
    {
        if (outCount)
        {
            *outCount = 0;
        }
        // 同期点まで待たせた付け替え・作成予約・列の見直しがある間は、列が Outer/Inner と食い違うのが正当。この間は照合しない
        if (!m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype || !m_PendingEntityRelinks.empty() ||
            !m_PendingEntityCreates.empty() || !m_RenderReconcile.empty())
        {
            return true;
        }
        uint32_t count = 0;
        const char* error = nullptr;
        for (Entity* root : GetRootEntities())
        {
            if (!VerifyRenderEntity(*root, Math::Transform::Identity, false, count, error))
                break;
        }
        if (outCount)
        {
            *outCount = count;
        }
        if (error)
        {
            LOG_ERROR("ECS_RENDER_TRANSFORM: %s", error);
            return false;
        }
        return true;
    }

    void World::RunEcsRenderTransformCheck()
    {
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_RENDER_TRANSFORM"))
        {
            return;
        }
        ++m_RenderCheckCalls;
        uint32_t count = 0;
        const bool bPass = VerifyEcsRenderTransform(&count);
        // 呼び出しが多いので、fail は毎回、pass は最初に Entity が居た回と 64 回ごとに出す
        const bool bReportPass = bPass && count > 0 && (m_RenderCheckLastReport == 0 || m_RenderCheckCalls % 64 == 0);
        if (!bPass || bReportPass)
        {
            checks->Report("ECS_RENDER_TRANSFORM", bPass, count);
            m_RenderCheckLastReport = m_RenderCheckCalls;
        }
    }

    void World::UpdateEntityTransformRecursive(Entity& entity, const Math::Transform& parentWorld)
    {
        const uint64_t versionBefore = entity.m_TransformVersion;
        entity.RecomputeWorldTransform(parentWorld);
        if (entity.m_TransformVersion != versionBefore)
        {
            // 旧の再帰でも、ワールドの変換が変わった行を描画の更新へ知らせる
            QueueRenderChange(entity.m_EcsEntity);
        }
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
        const Rendering::MeshResources* meshes,
        Container::UnorderedSet<uint64_t>& liveMeshComponentIds,
        Container::UnorderedSet<uint64_t>& liveSkinnedMeshComponentIds,
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
            auto* skinnedMeshComp = CastTo<Component::SkinnedMeshComponent>(comp);
            if (m_SceneView && skinnedMeshComp)
            {
                const bool bNeedsSync = skinnedMeshComp->IsRenderStateDirty() ||
                                        skinnedMeshComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (!bNeedsSync)
                {
                    if (skinnedMeshComp->IsVisible())
                    {
                        liveSkinnedMeshComponentIds.insert(skinnedMeshComp->GetComponentId());
                    }
                }
                else if (!skinnedMeshComp->IsVisible())
                {
                    // Hidden is an acknowledged non-publishing state, not a retryable failure.
                    skinnedMeshComp->ClearRenderStateDirty();
                    skinnedMeshComp->SetLastSyncedTransformVersion(ownerVersion);
                }
                else
                {
                    Rendering::SkinnedMeshProxy skinnedMeshProxy;
                    if (skinnedMeshComp->BuildSkinnedMeshProxy(skinnedMeshProxy))
                    {
                        skinnedMeshProxy.ObjectId = entity.GetObjectId();
                        skinnedMeshProxy.ComponentId = skinnedMeshComp->GetComponentId();
                        m_SceneView->UpdateSkinnedMeshProxy(skinnedMeshProxy);
                        liveSkinnedMeshComponentIds.insert(skinnedMeshProxy.ComponentId);
                        skinnedMeshComp->ClearRenderStateDirty();
                        skinnedMeshComp->SetLastSyncedTransformVersion(ownerVersion);
                    }
                }
            }

            auto* megaComp = CastTo<Component::MegaGeometryComponent>(comp);

            auto* meshComp = CastTo<Component::MeshComponent>(comp);
            if (m_SceneView && meshComp && !megaComp)
            {
                const bool bNeedsSync = meshComp->IsRenderStateDirty() ||
                                        meshComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (!bNeedsSync)
                {
                    liveMeshComponentIds.insert(meshComp->GetComponentId());
                    Rendering::MeshProxy meshProxy;
                    if (meshComp->BuildMeshProxy(meshProxy, materials, meshes))
                    {
                        meshProxy.ObjectId = entity.GetObjectId();
                        meshProxy.ComponentId = meshComp->GetComponentId();
                        m_SceneView->UpdateMeshProxy(meshProxy);
                        if (componentDataRegistry)
                        {
                            componentDataRegistry->PublishMeshProxy(entity, meshProxy);
                        }
                    }
                }
                else
                {
                    meshComp->RefreshRenderTransformCache();

                    Rendering::MeshProxy meshProxy;
                    if (meshComp->BuildMeshProxy(meshProxy, materials, meshes))
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
                meshComp->CommitRenderTransformHistory();
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

            auto* textComp = CastTo<Component::TextComponent>(comp);
            if (textComp)
            {
                textComp->RefreshRenderTransformCache();
                continue;
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
                meshes,
                liveMeshComponentIds,
                liveSkinnedMeshComponentIds,
                liveMegaGeometryObjectIds,
                liveLightIds,
                liveScreenBoardComponentIds,
                liveWorldBoardComponentIds,
                componentDataRegistry);
        }
    }

    void World::DispatchFixedTick(float fixedDeltaTime)
    {
        if (!HasFlag(OF_Initialized) || IsDeferringObjectRemoval())
        {
            return;
        }
        m_StageFixedDelta = fixedDeltaTime;
        RunWorldStage(m_StageFixedTick);
    }

    void World::DispatchFixedTickBody()
    {
        const float fixedDeltaTime = m_StageFixedDelta;
        const bool bStandalone = !m_bHasTickSnapshot;
        if (bStandalone)
        {
            BuildTickSnapshot();
        }
        {
            ScopedWorldFlag dispatch(m_bDispatchingTicks);
            for (const TickDispatchEntry& slot : m_FixedTickEntries)
            {
                const TickDispatchEntry entry = slot;
                if (entry.Owner && entry.Target && CanDispatchEntity(*entry.Owner)
                    && entry.Target->GetOwner() == entry.Owner && entry.Target->IsActive()
                    && entry.Target->IsTickEnabled() && !entry.Target->IsPendingDestroy())
                {
                    entry.Target->FixedTick(fixedDeltaTime);
                }
            }
        }
        if (bStandalone)
        {
            m_TickEntries.clear();
            m_FixedTickEntries.clear();
            m_bHasTickSnapshot = false;
        }
    }

    void World::CleanupAfterFixedStep()
    {
        CleanupDestroyedObjects();
    }

    void World::FixedTickEntityRecursive(Entity& entity, float fixedDeltaTime)
    {
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            return;
        }

        if (entity.IsTickEnabled())
        {
            for (Component::Component* component : entity.GetComponents())
            {
                if (component && component->IsActive() && component->IsTickEnabled())
                {
                    component->FixedTick(fixedDeltaTime);
                }
            }
        }

        for (Entity* child : entity.GetChildEntities())
        {
            FixedTickEntityRecursive(*child, fixedDeltaTime);
        }
    }

    void World::CollectTransientBoardProxiesRecursive(
        Entity& entity,
        Container::VariableArray<Rendering::BoardProxy>& outProxies)
    {
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            return;
        }

        for (Component::Component* component : entity.GetComponents())
        {
            if (Component::TextComponent* textComponent = CastTo<Component::TextComponent>(component))
            {
                textComponent->BuildGlyphBoardProxies(outProxies);
            }
        }

        for (Entity* child : entity.GetChildEntities())
        {
            CollectTransientBoardProxiesRecursive(*child, outProxies);
        }
    }

    void World::CleanupDestroyedObjects()
    {
        if (IsDeferringObjectRemoval())
        {
            return;
        }
        ScopedWorldFlag cleaning(m_bCleaningObjects);

        // 検査 WORLD_CLEANUP: 全木を走査して見つけた予約と、一覧から求めた処理対象が一致するか照合する。
        // 有効なときだけ全木を走査する（通常は一覧だけを処理する）。
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        const bool bCheck = checks != nullptr && checks->IsEnabled("WORLD_CLEANUP");
        bool bCheckPass = true;
        uint32_t checkCount = 0;

        // 木から外す Component を先に処理する。所有する Entity か祖先が破棄予約なら、
        // Entity ごと破棄されるので飛ばす。
        auto& pendingComponents = m_PendingTickComponents;
        pendingComponents.clear();
        for (Component::Component* component : m_PendingDestroyComponents)
        {
            component->SetFlag(OF_InDestroyList, false);
            Entity* owner = component->GetOwner();
            if (owner && owner->GetWorld() == this && component->IsPendingDestroy() &&
                !owner->IsPendingDestroy() && !HasPendingEntityAncestor(*owner))
            {
                pendingComponents.push_back(component);
            }
        }
        m_PendingDestroyComponents.clear();

        if (bCheck)
        {
            Container::VariableArray<Component::Component*> scanned;
            for (Entity* root : GetRootEntities())
            {
                CollectPendingComponents(*root, scanned);
            }
            bCheckPass = SamePointerSet(scanned, pendingComponents) && bCheckPass;
            checkCount += static_cast<uint32_t>(scanned.size());
        }

        // 途中で別の Component が消えると、InvalidateTickComponent が待ち参照を nullptr にする
        for (Component::Component* component : pendingComponents)
        {
            if (!component)
            {
                continue;
            }
            if (Entity* owner = component->GetOwner())
            {
                owner->RemoveComponentImmediately(component);
            }
        }

        // 破棄予約の Entity。祖先が先に予約されているものは、その祖先の部分木ごと破棄される。
        // 破棄の前に判定を全部済ませる（先に破棄した部分木の中の Entity を後で参照しないため）。
        auto& toRemove = m_PendingTickEntities;
        toRemove.clear();
        for (Entity* entity : m_PendingDestroyEntities)
        {
            entity->SetFlag(OF_InDestroyList, false);
            if (entity->GetWorld() == this && entity->IsPendingDestroy() && !HasPendingEntityAncestor(*entity))
            {
                toRemove.push_back(entity);
            }
        }
        m_PendingDestroyEntities.clear();

        if (bCheck)
        {
            Container::VariableArray<Entity*> scanned;
            for (Entity* root : GetRootEntities())
            {
                CollectPendingDestroyRecursive(*root, scanned);
            }
            bCheckPass = SamePointerSet(scanned, toRemove) && bCheckPass;
            checkCount += static_cast<uint32_t>(scanned.size());
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

        // 破棄が無い回は、最初の 1 回だけ pass を出す（毎フレームのログを避けつつ、検査が走ったことを示す）
        if (bCheck && (!bCheckPass || checkCount > 0 || !m_bCleanupCheckReported))
        {
            checks->Report("WORLD_CLEANUP", bCheckPass, checkCount);
            m_bCleanupCheckReported = true;
        }
    }

    void World::RegisterPendingDestroy(IUnknown& object)
    {
        if (object.HasFlag(OF_InDestroyList))
        {
            return;
        }
        if (auto* entity = CastTo<Entity>(&object))
        {
            m_PendingDestroyEntities.push_back(entity);
        }
        else if (auto* component = CastTo<Component::Component>(&object))
        {
            m_PendingDestroyComponents.push_back(component);
        }
        else
        {
            return;
        }
        object.SetFlag(OF_InDestroyList, true);
    }

    void World::ForgetPendingDestroy(IUnknown& object)
    {
        if (!object.HasFlag(OF_InDestroyList))
        {
            return;
        }
        object.SetFlag(OF_InDestroyList, false);
        if (auto* entity = CastTo<Entity>(&object))
        {
            auto it = std::find(m_PendingDestroyEntities.begin(), m_PendingDestroyEntities.end(), entity);
            if (it != m_PendingDestroyEntities.end())
            {
                m_PendingDestroyEntities.erase(it);
            }
        }
        else if (auto* component = CastTo<Component::Component>(&object))
        {
            auto it = std::find(m_PendingDestroyComponents.begin(), m_PendingDestroyComponents.end(), component);
            if (it != m_PendingDestroyComponents.end())
            {
                m_PendingDestroyComponents.erase(it);
            }
        }
    }

    void World::ForgetPendingDestroySubtree(Entity& entity)
    {
        if (m_PendingDestroyEntities.empty() && m_PendingDestroyComponents.empty())
        {
            return;
        }
        ForgetPendingDestroy(entity);
        for (IUnknown* inner : entity.GetInners())
        {
            if (m_PendingDestroyEntities.empty() && m_PendingDestroyComponents.empty())
            {
                return;
            }
            if (auto* child = CastTo<Entity>(inner))
            {
                ForgetPendingDestroySubtree(*child);
            }
            else if (inner->HasFlag(OF_InDestroyList))
            {
                ForgetPendingDestroy(*inner);
            }
        }
    }

    void World::CollectPendingComponents(Entity& entity, Container::VariableArray<Component::Component*>& output)
    {
        if (entity.IsPendingDestroy())
        {
            return;
        }
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* component = CastTo<Component::Component>(inner))
            {
                if (component->IsPendingDestroy())
                {
                    output.push_back(component);
                }
            }
            else if (auto* child = CastTo<Entity>(inner))
            {
                CollectPendingComponents(*child, output);
            }
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

        BindEntitySubtreeRows(*entity);
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

        BindEntitySubtreeRows(*child);
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

        RelinkEntityRow(entity);
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

        ScopedWorldFlag removing(m_bCleaningObjects);
        UnregisterEntitySubtreeForComponentData(entity);

        RemoveEntitySubtreeProxies(entity);
        NotifyEntitySubtreeRemoved(entity);
        InvalidateTickEntitySubtree(entity);

        // 部分木の行を、木から切り離す前に孤児にして消す（Object の解放より前に行を切る）
        UnbindEntitySubtreeRows(entity);

        // 破棄する部分木の中の予約を一覧から外す。除去の通知（OnRemovedFromWorld）が再び予約しても
        // 取りこぼさないよう、通知が済んだ後・木を切り離す直前に行う（削除後に一覧が壊れたポインタを持たないため）
        ForgetPendingDestroySubtree(entity);

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

    void World::BindEntitySubtreeRows(Entity& entity)
    {
        if (m_EntityArchetype == Ecs::kInvalidEcsArchetype)
        {
            return;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (entity.m_EcsCreateSlot == kNoEntityCreateSlot && !ecs.IsAlive(entity.m_EcsEntity) && m_Scheduler->IsRunning())
        {
            // 段の最中は作成を予約し、行は段の終わりの同期点で作る
            ReserveEntityRowCreate(entity);
        }
        else if (entity.m_EcsCreateSlot == kNoEntityCreateSlot && !ecs.IsAlive(entity.m_EcsEntity))
        {
            const Ecs::EcsEntity row = ecs.CreateEntity(m_EntityArchetype);
            if (row.IsNull())
            {
                LOG_ERROR("World: Entity の ECS の行を作れなかった");
            }
            else
            {
                entity.m_EcsEntity = row;
                if (Ecs::EcsObjectRef* ref = ecs.GetColumnValue<Ecs::EcsObjectRef>(row))
                {
                    // 作った直後の行なので、そのまま書く（作成が全列を「変わった」にする）
                    ref->Object = static_cast<void*>(&entity);
                    ref->Generation = row.Generation;
                }
                MoveEntityValuesToRow(entity);
                const Entity* parent = entity.GetParentEntity();
                LinkEntityRowToParent(row, parent ? parent->m_EcsEntity : Ecs::kNullEcsEntity);
                ++m_EcsBindingEpoch;
                NoteRenderRowCreated(entity);
            }
        }
        for (Component::Component* component : entity.GetComponents())
        {
            BindComponentRow(*component);
        }
        for (Entity* child : entity.GetChildEntities())
        {
            BindEntitySubtreeRows(*child);
        }
    }

    Ecs::EcsArchetypeId World::ComponentArchetypeFor(const Component::Component& component) const
    {
        const size_t kind = static_cast<size_t>(component.GetEcsRowKind());
        if (kind >= static_cast<size_t>(Ecs::EcsComponentRowKind::Count))
        {
            return Ecs::kInvalidEcsArchetype;
        }
        return m_ComponentArchetypes[kind];
    }

    void World::BindComponentRow(Component::Component& component)
    {
        if (ComponentArchetypeFor(component) == Ecs::kInvalidEcsArchetype || !m_EcsWorld)
        {
            return;
        }
        Entity* owner = component.GetOwner();
        if (!owner || owner->GetWorld() != this)
        {
            return;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (component.m_EcsCreateSlot != kNoEntityCreateSlot || ecs.IsAlive(component.m_EcsEntity))
        {
            // 行は付いている（または作成を予約済み）
            return;
        }
        const bool bOwnerReserved = owner->m_EcsCreateSlot != kNoEntityCreateSlot;
        if (!bOwnerReserved && !ecs.IsAlive(owner->m_EcsEntity))
        {
            // 所有者に行が無い（再生に失敗した）。値は Object に残し、行なしのまま
            return;
        }
        if (bOwnerReserved || m_Scheduler->IsRunning())
        {
            // 段の最中は作成を予約し、行は段の終わりの同期点で作る
            ReserveComponentRowCreate(component);
            return;
        }
        const Ecs::EcsEntity row = ecs.CreateEntity(ComponentArchetypeFor(component));
        if (row.IsNull())
        {
            LOG_ERROR("World: Component の ECS の行を作れなかった");
            return;
        }
        component.m_EcsEntity = row;
        if (Ecs::EcsObjectRef* ref = ecs.GetColumnValue<Ecs::EcsObjectRef>(row))
        {
            ref->Object = static_cast<void*>(&component);
            ref->Generation = row.Generation;
        }
        MoveComponentValuesToRow(component);
        LinkEntityRowToParent(row, owner->m_EcsEntity, Ecs::kEcsHierarchyKindComponent);
        ++m_EcsBindingEpoch;
    }

    void World::MoveEntityValuesToRow(Entity& entity)
    {
        // 時間倍率とワールドの変換のキャッシュは保存の対象でないので、予備の値を直接列へ写す。
        // flags と局所の変換は ECS_PROPERTY の移動に任せる
        m_EcsWorld->WriteColumnValueOf(entity.m_EcsEntity, Ecs::EcsTimeDilation{entity.m_CustomTimeDilation});
        const Math::Transform& cache = entity.m_WorldTransformFallback;
        m_EcsWorld->WriteColumnValueOf(entity.m_EcsEntity, Ecs::EcsWorldTransform{cache.position, cache.rotation, cache.scale});
        MoveEcsPropertyValuesToColumns(&entity);
    }

    void World::MoveComponentValuesToRow(Component::Component& component)
    {
        Ecs::EcsTickConfig config{};
        config.Group = static_cast<uint8_t>(component.m_TickConfiguration.GetGroup());
        config.Priority = component.m_TickConfiguration.GetPriority();
        config.Mask = component.m_TickConfiguration.GetMask();
        m_EcsWorld->WriteColumnValueOf(component.m_EcsEntity, config);
        MoveEcsPropertyValuesToColumns(&component);
        component.OnEcsRowAttached();
    }

    void World::MoveEntityValuesToFallback(Entity& entity)
    {
        if (const Ecs::EcsTimeDilation* dilation = m_EcsWorld->GetColumnValue<Ecs::EcsTimeDilation>(entity.m_EcsEntity))
        {
            entity.m_CustomTimeDilation = dilation->CustomTimeDilation;
        }
        if (const Ecs::EcsWorldTransform* cache = m_EcsWorld->GetColumnValue<Ecs::EcsWorldTransform>(entity.m_EcsEntity))
        {
            entity.m_WorldTransformFallback = Math::Transform(cache->position, cache->rotation, cache->scale);
        }
        MoveEcsPropertyValuesToFallback(&entity);
    }

    void World::MoveComponentValuesToFallback(Component::Component& component)
    {
        component.OnEcsRowDetaching();
        if (const Ecs::EcsTickConfig* config = m_EcsWorld->GetColumnValue<Ecs::EcsTickConfig>(component.m_EcsEntity))
        {
            // 列の値は検証済みの設定から写したものなので、群 → 優先度 → mask の順に戻せる
            Component::TickGroupConfiguration restored;
            restored.SetGroup(static_cast<Component::ETickGroup>(config->Group));
            restored.SetPriority(config->Priority);
            restored.SetMask(config->Mask);
            component.m_TickConfiguration = restored;
        }
        MoveEcsPropertyValuesToFallback(&component);
    }

    void World::LinkEntityRowToParent(Ecs::EcsEntity row, Ecs::EcsEntity parentRow, uint32_t kind)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        const Ecs::EcsHierarchy* parentHierarchy = parentRow.IsNull() ? nullptr : ecs.GetColumnValue<Ecs::EcsHierarchy>(parentRow);
        if (!parentHierarchy)
        {
            // 根（World 直下）。兄弟の連結は持たない
            EditHierarchy(ecs, row, [kind](Ecs::EcsHierarchy& self)
                          {
                              self.Parent = Ecs::kNullEcsEntity;
                              self.PrevSibling = Ecs::kNullEcsEntity;
                              self.NextSibling = Ecs::kNullEcsEntity;
                              self.Depth = 0;
                              self.Kind = kind;
                          });
            return;
        }
        // Component は所有者の Component の連結、Entity は子 Entity の連結の末尾へ付ける
        const bool bComponent = kind == Ecs::kEcsHierarchyKindComponent;
        const Ecs::EcsEntity tail = bComponent ? parentHierarchy->LastComponent : parentHierarchy->LastChild;
        const uint32_t depth = parentHierarchy->Depth + 1;
        EditHierarchy(ecs, row, [&](Ecs::EcsHierarchy& self)
                      {
                          self.Parent = parentRow;
                          self.PrevSibling = tail;
                          self.NextSibling = Ecs::kNullEcsEntity;
                          self.Depth = depth;
                          self.Kind = kind;
                      });
        if (!tail.IsNull())
        {
            EditHierarchy(ecs, tail, [&](Ecs::EcsHierarchy& sibling) { sibling.NextSibling = row; });
        }
        EditHierarchy(ecs, parentRow, [&](Ecs::EcsHierarchy& parent)
                      {
                          Ecs::EcsEntity& first = bComponent ? parent.FirstComponent : parent.FirstChild;
                          Ecs::EcsEntity& last = bComponent ? parent.LastComponent : parent.LastChild;
                          last = row;
                          if (first.IsNull())
                          {
                              first = row;
                          }
                      });
    }

    void World::UnlinkEntityRow(Ecs::EcsEntity row)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        const Ecs::EcsHierarchy* hierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(row);
        if (!hierarchy)
        {
            return;
        }
        const Ecs::EcsHierarchy mine = *hierarchy;
        const bool bComponent = mine.Kind == Ecs::kEcsHierarchyKindComponent;
        if (!mine.Parent.IsNull())
        {
            if (mine.PrevSibling.IsNull())
            {
                EditHierarchy(ecs, mine.Parent, [&](Ecs::EcsHierarchy& parent)
                              { (bComponent ? parent.FirstComponent : parent.FirstChild) = mine.NextSibling; });
            }
            else
            {
                EditHierarchy(ecs, mine.PrevSibling, [&](Ecs::EcsHierarchy& sibling) { sibling.NextSibling = mine.NextSibling; });
            }
            if (mine.NextSibling.IsNull())
            {
                EditHierarchy(ecs, mine.Parent, [&](Ecs::EcsHierarchy& parent)
                              { (bComponent ? parent.LastComponent : parent.LastChild) = mine.PrevSibling; });
            }
            else
            {
                EditHierarchy(ecs, mine.NextSibling, [&](Ecs::EcsHierarchy& sibling) { sibling.PrevSibling = mine.PrevSibling; });
            }
        }
        EditHierarchy(ecs, row, [](Ecs::EcsHierarchy& self)
                      {
                          self.Parent = Ecs::kNullEcsEntity;
                          self.PrevSibling = Ecs::kNullEcsEntity;
                          self.NextSibling = Ecs::kNullEcsEntity;
                      });
    }

    void World::RelinkEntityRow(Entity& entity)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (!ecs.IsAlive(entity.m_EcsEntity))
        {
            // 作成を予約中の Entity は行が無い。同期点で今の親の下に結ばれる
            return;
        }
        UnlinkEntityRow(entity.m_EcsEntity);
        const Entity* parent = entity.GetParentEntity();
        if (parent && parent->m_EcsCreateSlot != kNoEntityCreateSlot)
        {
            // 新しい親の行はまだ無い。同期点で親の行ができてから結び直す
            m_PendingEntityRelinks.push_back(entity.m_EcsEntity);
        }
        LinkEntityRowToParent(entity.m_EcsEntity, parent ? parent->m_EcsEntity : Ecs::kNullEcsEntity);
        uint32_t depth = 0;
        if (const Ecs::EcsHierarchy* hierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(entity.m_EcsEntity))
        {
            depth = hierarchy->Depth;
        }
        UpdateEntitySubtreeDepth(entity, depth);
        ++m_EcsBindingEpoch;
        // 新しい親の下で補間する祖先が変わりうる。履歴は親の ID が変わったことで捨てられる
        NoteRenderInterpolationChange(entity);
    }

    void World::UpdateEntitySubtreeDepth(Entity& entity, uint32_t depth)
    {
        for (Component::Component* component : entity.GetComponents())
        {
            const Ecs::EcsEntity componentRow = component->m_EcsEntity;
            const Ecs::EcsHierarchy* hierarchy = m_EcsWorld->GetColumnValue<Ecs::EcsHierarchy>(componentRow);
            if (hierarchy && hierarchy->Depth != depth + 1)
            {
                EditHierarchy(*m_EcsWorld, componentRow, [&](Ecs::EcsHierarchy& self) { self.Depth = depth + 1; });
            }
        }
        for (Entity* child : entity.GetChildEntities())
        {
            const Ecs::EcsEntity childRow = child->m_EcsEntity;
            const Ecs::EcsHierarchy* hierarchy = m_EcsWorld->GetColumnValue<Ecs::EcsHierarchy>(childRow);
            if (hierarchy && hierarchy->Depth != depth + 1)
            {
                EditHierarchy(*m_EcsWorld, childRow, [&](Ecs::EcsHierarchy& self) { self.Depth = depth + 1; });
            }
            UpdateEntitySubtreeDepth(*child, depth + 1);
        }
    }

    void World::UnbindEntitySubtreeRows(Entity& entity)
    {
        if (!m_EcsWorld)
        {
            return;
        }
        // 部分木の最上位だけを親の連結から外す（子孫は行ごと消えるので連結は直さない）
        if (!entity.m_EcsEntity.IsNull())
        {
            UnlinkEntityRow(entity.m_EcsEntity);
        }
        ReleaseEntitySubtreeRows(entity);
    }

    void World::ReleaseEntitySubtreeRows(Entity& entity)
    {
        for (Entity* child : entity.GetChildEntities())
        {
            ReleaseEntitySubtreeRows(*child);
        }
        // Component は所有者の葉。所有者の行ごと消えるので、連結は直さない
        for (Component::Component* component : entity.GetComponents())
        {
            ReleaseComponentRow(*component, false);
        }
        ReleaseEntityRow(entity);
    }

    void World::ReleaseEntityRow(Entity& entity)
    {
        if (entity.m_EcsCreateSlot != kNoEntityCreateSlot)
        {
            // 作成の予約がまだ再生されていない: 札を取り消す（行は無いので消すものも無い）
            const uint32_t slot = entity.m_EcsCreateSlot;
            entity.m_EcsCreateSlot = kNoEntityCreateSlot;
            if (slot < m_PendingEntityCreates.size() && m_PendingEntityCreates[slot].Target == &entity)
            {
                m_EntityCreateCommands.CancelCreate(m_PendingEntityCreates[slot].Ticket);
                m_PendingEntityCreates[slot].Target = nullptr;
            }
            ++m_EcsBindingEpoch;
            return;
        }
        const Ecs::EcsEntity row = entity.m_EcsEntity;
        if (row.IsNull())
        {
            return;
        }
        // 並列の段の中で Object を解放してはならない（ワーカーは Object に触れない）
        assert(!Ecs::Detail::EcsIsParallelActive());
        ++m_EcsBindingEpoch;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (ecs.IsAlive(row))
        {
            // 値を予備へ戻す（行を外す前でないと、行を引けない）
            MoveEntityValuesToFallback(entity);
        }
        entity.m_EcsEntity = Ecs::kNullEcsEntity;
        if (!ecs.IsAlive(row))
        {
            return;
        }
        // Object への参照を先に切り、以後どのクエリにも出ない孤児にする
        ecs.WriteColumnValueOf(row, Ecs::EcsObjectRef{});
        ecs.MarkOrphan(row);
        if (m_Scheduler->IsRunning())
        {
            m_DeferredEntityRows.push_back(row);
        }
        else
        {
            ecs.DestroyEntity(row);
        }
    }

    void World::ReleaseComponentRow(Component::Component& component, bool bUnlink)
    {
        if (component.m_EcsCreateSlot != kNoEntityCreateSlot)
        {
            // 作成の予約がまだ再生されていない: 札を取り消す（行は無いので消すものも無い）
            const uint32_t slot = component.m_EcsCreateSlot;
            component.m_EcsCreateSlot = kNoEntityCreateSlot;
            if (slot < m_PendingEntityCreates.size() && m_PendingEntityCreates[slot].ComponentTarget == &component)
            {
                m_EntityCreateCommands.CancelCreate(m_PendingEntityCreates[slot].Ticket);
                m_PendingEntityCreates[slot].ComponentTarget = nullptr;
            }
            ++m_EcsBindingEpoch;
            return;
        }
        const Ecs::EcsEntity row = component.m_EcsEntity;
        if (row.IsNull())
        {
            return;
        }
        // 並列の段の中で Object を解放してはならない（ワーカーは Object に触れない）
        assert(!Ecs::Detail::EcsIsParallelActive());
        ++m_EcsBindingEpoch;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (ecs.IsAlive(row))
        {
            if (bUnlink)
            {
                UnlinkEntityRow(row);
            }
            // 値を予備へ戻す（行を外す前でないと、行を引けない）
            MoveComponentValuesToFallback(component);
        }
        component.m_EcsEntity = Ecs::kNullEcsEntity;
        if (!ecs.IsAlive(row))
        {
            return;
        }
        // Object への参照を先に切り、以後どのクエリにも出ない孤児にする
        ecs.WriteColumnValueOf(row, Ecs::EcsObjectRef{});
        ecs.MarkOrphan(row);
        if (m_Scheduler->IsRunning())
        {
            m_DeferredEntityRows.push_back(row);
        }
        else
        {
            ecs.DestroyEntity(row);
        }
    }

    void World::FlushDeferredEntityRows()
    {
        if (!m_DeferredEntityRows.empty())
        {
            for (const Ecs::EcsEntity row : m_DeferredEntityRows)
            {
                m_EcsWorld->DestroyEntity(row);
            }
            m_DeferredEntityRows.clear();
        }
        PlaybackEntityRowCreates();
    }

    void World::ReserveComponentRowCreate(Component::Component& component)
    {
        // 並列の段の中で Object を木に付けてはならない（ワーカーは Object に触れない）
        assert(!Ecs::Detail::EcsIsParallelActive());
        PendingEntityCreate pending;
        pending.ComponentTarget = &component;
        pending.Ticket = m_EntityCreateCommands.Create(ComponentArchetypeFor(component));
        component.m_EcsCreateSlot = static_cast<uint32_t>(m_PendingEntityCreates.size());
        m_PendingEntityCreates.push_back(pending);
        ++m_EcsBindingEpoch;
    }

    void World::ReserveEntityRowCreate(Entity& entity)
    {
        // 並列の段の中で Object を木に付けてはならない（ワーカーは Object に触れない）
        assert(!Ecs::Detail::EcsIsParallelActive());
        PendingEntityCreate pending;
        pending.Target = &entity;
        pending.Ticket = m_EntityCreateCommands.Create(m_EntityArchetype);
        entity.m_EcsCreateSlot = static_cast<uint32_t>(m_PendingEntityCreates.size());
        m_PendingEntityCreates.push_back(pending);
        ++m_EcsBindingEpoch;
    }

    void World::PlaybackEntityRowCreates()
    {
        if (m_PendingEntityCreates.empty() && m_PendingEntityRelinks.empty())
        {
            return;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        Ecs::EcsCommandBuffer* buffers[] = {&m_EntityCreateCommands};
        Ecs::EcsCommandBuffer::Replay(ecs, Container::Span<Ecs::EcsCommandBuffer* const>(buffers, 1));

        // 階層を直す親（行を持つ Entity）。Inner の並びから子と Component の連結を作り直す
        Container::VariableArray<Entity*> dirtyParents;
        Container::UnorderedSet<Entity*> dirtySet;
        const auto markEntity = [&](Entity* parent)
        {
            if (parent && dirtySet.insert(parent).second)
            {
                dirtyParents.push_back(parent);
            }
        };
        const auto markParent = [&](Entity& entity) { markEntity(entity.GetParentEntity()); };

        uint32_t failed = 0;
        uint32_t created = 0;
        for (const PendingEntityCreate& pending : m_PendingEntityCreates)
        {
            if (pending.ComponentTarget)
            {
                Component::Component& component = *pending.ComponentTarget;
                component.m_EcsCreateSlot = kNoEntityCreateSlot;
                const Ecs::EcsEntity componentRow = m_EntityCreateCommands.ResolveCreate(pending.Ticket);
                if (componentRow.IsNull())
                {
                    // 再生に失敗した Component は、値を Object に残したまま行なしで残る
                    ++failed;
                    continue;
                }
                ++created;
                component.m_EcsEntity = componentRow;
                Ecs::EcsObjectRef componentRef;
                componentRef.Object = static_cast<void*>(&component);
                componentRef.Generation = componentRow.Generation;
                ecs.WriteColumnValueOf(componentRow, componentRef);
                MoveComponentValuesToRow(component);
                markEntity(component.GetOwner());
                continue;
            }
            if (!pending.Target)
            {
                continue;
            }
            Entity& entity = *pending.Target;
            entity.m_EcsCreateSlot = kNoEntityCreateSlot;
            const Ecs::EcsEntity row = m_EntityCreateCommands.ResolveCreate(pending.Ticket);
            if (row.IsNull())
            {
                // 再生に失敗した Entity は、値を Object に残したまま行なしで残る
                ++failed;
                continue;
            }
            ++created;
            entity.m_EcsEntity = row;
            Ecs::EcsObjectRef ref;
            ref.Object = static_cast<void*>(&entity);
            ref.Generation = row.Generation;
            ecs.WriteColumnValueOf(row, ref);
            MoveEntityValuesToRow(entity);
            markParent(entity);
            NoteRenderRowCreated(entity);
        }
        for (const Ecs::EcsEntity row : m_PendingEntityRelinks)
        {
            const Ecs::EcsObjectRef* ref = ecs.IsAlive(row) ? ecs.GetColumnValue<Ecs::EcsObjectRef>(row) : nullptr;
            if (ref && ref->Object != nullptr && ref->Generation == row.Generation)
            {
                markParent(*static_cast<Entity*>(ref->Object));
                NoteRenderInterpolationChange(*static_cast<Entity*>(ref->Object));
            }
        }
        m_PendingEntityCreates.clear();
        m_PendingEntityRelinks.clear();
        m_EntityCreateCommands.Clear();

        // 祖先の少ない親から順に直す（子の深さは親の深さから決まる）
        Container::VariableArray<std::pair<uint32_t, Entity*>> ordered;
        for (Entity* parent : dirtyParents)
        {
            uint32_t ancestors = 0;
            for (const Entity* cursor = parent->GetParentEntity(); cursor; cursor = cursor->GetParentEntity())
            {
                ++ancestors;
            }
            ordered.push_back({ancestors, parent});
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const std::pair<uint32_t, Entity*>& lhs, const std::pair<uint32_t, Entity*>& rhs)
                         { return lhs.first < rhs.first; });
        for (const auto& item : ordered)
        {
            RebuildEntityRowChildren(*item.second);
        }
        ++m_EcsBindingEpoch;

        if (failed != 0)
        {
            LOG_ERROR("World: 予約した Entity の行の作成を再生できなかった（%u 件。値は Object に残り、行なしになる）", failed);
        }
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks != nullptr && checks->IsEnabled("ECS_PLAYBACK") && (created != 0 || failed != 0))
        {
            checks->Report("ECS_PLAYBACK", failed == 0, created);
        }
    }

    void World::RebuildEntityRowChildren(Entity& parent)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        const Ecs::EcsEntity parentRow = parent.m_EcsEntity;
        const Ecs::EcsHierarchy* parentHierarchy = ecs.IsAlive(parentRow) ? ecs.GetColumnValue<Ecs::EcsHierarchy>(parentRow) : nullptr;
        if (!parentHierarchy)
        {
            return;
        }
        const uint32_t parentDepth = parentHierarchy->Depth;
        Ecs::EcsEntity first = Ecs::kNullEcsEntity;
        Ecs::EcsEntity previous = Ecs::kNullEcsEntity;
        for (Entity* child : parent.GetChildEntities())
        {
            const Ecs::EcsEntity row = child->m_EcsEntity;
            if (!ecs.IsAlive(row))
            {
                continue;
            }
            const Ecs::EcsEntity before = previous;
            EditHierarchy(ecs, row, [&](Ecs::EcsHierarchy& self)
                          {
                              self.Parent = parentRow;
                              self.PrevSibling = before;
                              self.NextSibling = Ecs::kNullEcsEntity;
                              self.Depth = parentDepth + 1;
                          });
            if (!previous.IsNull())
            {
                EditHierarchy(ecs, previous, [&](Ecs::EcsHierarchy& sibling) { sibling.NextSibling = row; });
            }
            else
            {
                first = row;
            }
            previous = row;
        }
        // Component の連結も Inner の並び（Component だけ）から作り直す
        Ecs::EcsEntity firstComponent = Ecs::kNullEcsEntity;
        Ecs::EcsEntity previousComponent = Ecs::kNullEcsEntity;
        for (Component::Component* component : parent.GetComponents())
        {
            const Ecs::EcsEntity row = component->m_EcsEntity;
            if (!ecs.IsAlive(row))
            {
                continue;
            }
            const Ecs::EcsEntity before = previousComponent;
            EditHierarchy(ecs, row, [&](Ecs::EcsHierarchy& self)
                          {
                              self.Parent = parentRow;
                              self.PrevSibling = before;
                              self.NextSibling = Ecs::kNullEcsEntity;
                              self.Depth = parentDepth + 1;
                              self.Kind = Ecs::kEcsHierarchyKindComponent;
                          });
            if (!previousComponent.IsNull())
            {
                EditHierarchy(ecs, previousComponent, [&](Ecs::EcsHierarchy& sibling) { sibling.NextSibling = row; });
            }
            else
            {
                firstComponent = row;
            }
            previousComponent = row;
        }
        EditHierarchy(ecs, parentRow, [&](Ecs::EcsHierarchy& self)
                      {
                          self.FirstChild = first;
                          self.LastChild = previous;
                          self.FirstComponent = firstComponent;
                          self.LastComponent = previousComponent;
                      });
        UpdateEntitySubtreeDepth(parent, parentDepth);
    }

    bool World::VerifyEcsBinding(uint32_t* outCount)
    {
        if (outCount)
        {
            *outCount = 0;
        }
        if (!m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype || m_Scheduler->IsRunning())
        {
            return true;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        uint32_t count = 0;
        uint32_t componentCount = 0;
        const char* error = nullptr;
        for (Entity* root : GetRootEntities())
        {
            if (!VerifyEntityRows(ecs, *root, Ecs::kNullEcsEntity, 0, count, componentCount, error))
            {
                break;
            }
        }
        if (outCount)
        {
            *outCount = count;
        }
        if (!error)
        {
            // 行の側からも数える: EcsObjectRef を持つ行（孤児を含む）は、生きた Entity と同じ数だけある
            Ecs::EcsQuery query;
            query.All.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsObjectRef>().Type);
            query.bIncludeOrphans = true;
            Container::VariableArray<Ecs::EcsArchetypeId> archetypes;
            ecs.MatchArchetypes(query, archetypes);
            uint64_t rows = 0;
            for (const Ecs::EcsArchetypeId archetype : archetypes)
            {
                rows += ecs.GetArchetypeRowCount(archetype);
            }
            query.AnyTags = Ecs::kEcsTagOrphan;
            archetypes.clear();
            ecs.MatchArchetypes(query, archetypes);
            uint64_t orphans = 0;
            for (const Ecs::EcsArchetypeId archetype : archetypes)
            {
                orphans += ecs.GetArchetypeRowCount(archetype);
            }
            if (rows != static_cast<uint64_t>(count) + componentCount)
            {
                error = "生きた Entity・Component の数と行の数が一致しない";
            }
            else if (orphans != 0 || !m_DeferredEntityRows.empty())
            {
                error = "孤児の行が残っている";
            }
            else if (!m_PendingEntityRelinks.empty())
            {
                error = "同期点まで待たせた付け替えが残っている";
            }
            else
            {
                for (const PendingEntityCreate& pending : m_PendingEntityCreates)
                {
                    if (pending.Target || pending.ComponentTarget)
                    {
                        error = "再生されていない行の作成の予約が残っている";
                        break;
                    }
                }
            }
        }
        if (error)
        {
            LOG_ERROR("ECS_BIND: %s", error);
            return false;
        }
        return true;
    }

    Ecs::EcsTransformSystem& World::GetEcsTransformSystem()
    {
        if (!m_EcsTransformSystem)
        {
            m_EcsTransformSystem = Container::MakeUnique<Ecs::EcsTransformSystem>(*m_EcsWorld);
            // 行の少ないバケツは直列のまま（既定の閾値）。並列でも直列でも列の値と処理した順は同じ
            m_EcsTransformSystem->SetParallel(true);
        }
        return *m_EcsTransformSystem;
    }

    uint32_t World::PropagateEcsTransforms()
    {
        if (!m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype)
        {
            return 0;
        }
        Ecs::EcsTransformSystem& system = GetEcsTransformSystem();
        const Ecs::EcsTransformSystem::RunResult run = system.Run();
        for (const Ecs::EcsTransformSystem::AppliedRow& applied : system.GetApplied())
        {
            if (applied.bWorldChanged || applied.bFirst)
            {
                // 補間しない行の描画の変換はワールドの変換と同じ。変わった行だけ、描画の更新が版と描画状態を直す
                QueueRenderChange(applied.Entity);
            }
            const Ecs::EcsObjectRef* ref = m_EcsWorld->GetColumnValue<Ecs::EcsObjectRef>(applied.Entity);
            if (ref == nullptr || ref->Object == nullptr)
            {
                continue;
            }
            Entity* entity = static_cast<Entity*>(ref->Object);
            if (applied.bWorldChanged)
            {
                // 旧の再帰と同じく、ワールドの変換が変わった Entity だけ版を進める
                ++entity->m_TransformVersion;
            }
            entity->m_bWorldTransformDirty = false;
        }
        return run.Unresolved;
    }

    bool World::VerifyEcsTransform(uint32_t* outCount)
    {
        if (outCount)
        {
            *outCount = 0;
        }
        // 予約中の親の下へ付け替えた行は、同期点まで階層の列が古い（Outer/Inner と食い違うのは正当）。この間は伝播も照合もしない
        if (!m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype || !m_PendingEntityRelinks.empty())
        {
            return true;
        }
        const uint32_t unresolved = PropagateEcsTransforms();
        uint32_t count = 0;
        const char* error = nullptr;
        if (unresolved != 0)
        {
            error = "階層のシステムが親の値を求められなかった行がある";
        }
        else
        {
            for (Entity* root : GetRootEntities())
            {
                const Math::Transform identity = Math::Transform::Identity;
                const Ecs::EcsWorldTransform identityColumn{identity.position, identity.rotation, identity.scale};
                if (!VerifyEntityTransforms(*m_EcsWorld, *root, identityColumn, true, count, error))
                {
                    break;
                }
            }
        }
        if (outCount)
        {
            *outCount = count;
        }
        if (error)
        {
            LOG_ERROR("ECS_TRANSFORM: %s", error);
            return false;
        }
        return true;
    }

    void World::RunEcsTransformCheck()
    {
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_TRANSFORM"))
        {
            return;
        }
        ++m_EcsTransformCheckCalls;
        uint32_t count = 0;
        const bool bPass = VerifyEcsTransform(&count);
        // 呼び出しが多いので、fail は毎回、pass は最初に Entity が居た回と 64 回ごとに出す
        const bool bReportPass = bPass && count > 0 &&
                                 (m_EcsTransformLastReport == 0 || m_EcsTransformCheckCalls % 64 == 0);
        if (!bPass || bReportPass)
        {
            checks->Report("ECS_TRANSFORM", bPass, count);
            m_EcsTransformLastReport = m_EcsTransformCheckCalls;
        }
    }

    void World::RunEcsBindingCheck(bool bFinal)
    {
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_BIND"))
        {
            return;
        }
        // 結び付けに変化が無い回は照合しない（World の終了時は必ず行う）
        if (!bFinal && m_EcsBindingEpoch == m_VerifiedBindingEpoch)
        {
            return;
        }
        uint32_t count = 0;
        const bool bPass = VerifyEcsBinding(&count);
        m_VerifiedBindingEpoch = m_EcsBindingEpoch;
        checks->Report("ECS_BIND", bPass, count);
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

        // 木に入る前に破棄予約された Entity・Component を、一覧へ積む（World の外では積まれないため）
        if (entity.IsPendingDestroy())
        {
            RegisterPendingDestroy(entity);
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            AssignFreshObjectIdsRecursive(*child);
        }
        for (IUnknown* inner : entity.GetInners())
        {
            if (inner->HasFlag(OF_PendingDestroy) && !CastTo<Entity>(inner))
            {
                RegisterPendingDestroy(*inner);
            }
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
                if (auto* skinnedMeshComp = CastTo<Component::SkinnedMeshComponent>(comp))
                {
                    m_SceneView->RemoveSkinnedMeshProxy(skinnedMeshComp->GetComponentId());
                }
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
