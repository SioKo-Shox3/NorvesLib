#include "Object/PrefabAsset.h"

#include "Component/Component.h"
#include "Component/MeshComponent.h"
#include "Object/Entity.h"
#include "Object/ObjectCast.h"
#include <utility>

namespace NorvesLib::Core
{
    IMPLEMENT_CLASS(PrefabAsset, Resource)

    struct PrefabNestedPrefabStorage
    {
        Container::VariableArray<PrefabNestedPrefabInstance> Instances;
    };

    namespace
    {
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

        const IClass* ResolveClassByStableId(StableClassId stableClassId)
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

        const ClassProperty* FindPropertyByStableId(const IClass& cls, StablePropertyId propertyId)
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

        const ProjectedPropertyValue* FindProjectedValue(
            const ObjectSnapshot& snapshot,
            StablePropertyId propertyId)
        {
            for (const ProjectedPropertyValue& value : snapshot.Properties)
            {
                if (value.Property == propertyId)
                {
                    return &value;
                }
            }
            return nullptr;
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

        bool ContainsPrefabRecursive(
            const PrefabAsset& current,
            const PrefabAsset& target,
            Container::VariableArray<const PrefabAsset*>& stack,
            uint32_t depth)
        {
            constexpr uint32_t MaxCycleCheckDepth = 1024;

            if (&current == &target)
            {
                return true;
            }

            if (depth > MaxCycleCheckDepth || ContainsPrefabInStack(stack, current))
            {
                return false;
            }

            stack.push_back(&current);

            for (const PrefabNestedPrefabInstance& nested : current.GetNestedPrefabs())
            {
                const PrefabAsset* nestedPrefab = nested.Prefab.Get();
                if (nestedPrefab &&
                    ContainsPrefabRecursive(*nestedPrefab, target, stack, depth + 1))
                {
                    stack.pop_back();
                    return true;
                }
            }

            stack.pop_back();
            return false;
        }

        bool WouldCreateNestedPrefabCycle(const PrefabAsset& owner, const PrefabAsset& candidate)
        {
            Container::VariableArray<const PrefabAsset*> stack;
            return ContainsPrefabRecursive(candidate, owner, stack, 1);
        }

        const ProjectedPropertyValue* FindEffectiveBaseValue(
            const PrefabOverrideSet& baseOverrides,
            SubtreeSnapshotAliasId alias,
            StablePropertyId propertyId)
        {
            const ProjectedPropertyValue* result = nullptr;
            for (const PrefabPropertyOverride& overrideValue : baseOverrides.Properties)
            {
                if (overrideValue.Target.Alias == alias &&
                    overrideValue.Target.NestedInstances.empty() &&
                    overrideValue.Value.Property == propertyId)
                {
                    result = &overrideValue.Value;
                }
            }
            return result;
        }

        void AppendShiftedNestedOverrides(
            const PrefabOverrideSet& sourceOverrides,
            PrefabNestedInstanceId nestedInstanceId,
            PrefabOverrideSet& outOverrides)
        {
            for (const PrefabPropertyOverride& overrideValue : sourceOverrides.Properties)
            {
                if (overrideValue.Target.NestedInstances.empty() ||
                    overrideValue.Target.NestedInstances[0] != nestedInstanceId)
                {
                    continue;
                }

                PrefabPropertyOverride shiftedOverride;
                for (size_t index = 1; index < overrideValue.Target.NestedInstances.size(); ++index)
                {
                    shiftedOverride.Target.NestedInstances.push_back(overrideValue.Target.NestedInstances[index]);
                }
                shiftedOverride.Target.Alias = overrideValue.Target.Alias;
                shiftedOverride.Value = overrideValue.Value;
                outOverrides.Properties.push_back(std::move(shiftedOverride));
            }
        }

        Container::VariableArray<PrefabNestedInstanceId> MakeNestedPath(
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            PrefabNestedInstanceId nestedInstanceId)
        {
            Container::VariableArray<PrefabNestedInstanceId> result = path;
            result.push_back(nestedInstanceId);
            return result;
        }

        void BuildObjectOverrides(
            const ObjectSnapshot& baseSnapshot,
            const ObjectSnapshot& editedSnapshot,
            const IClass& baseClass,
            SubtreeSnapshotAliasId targetAlias,
            const Container::VariableArray<PrefabNestedInstanceId>& targetPath,
            const PrefabOverrideSet& baseOverrides,
            PrefabOverrideSet& outOverrides)
        {
            if (editedSnapshot.Class != baseSnapshot.Class)
            {
                return;
            }

            for (const ProjectedPropertyValue& baseValue : baseSnapshot.Properties)
            {
                const ClassProperty* property = FindPropertyByStableId(baseClass, baseValue.Property);
                if (!property || ShouldSkipPrefabRestoreProperty(baseClass, *property))
                {
                    continue;
                }

                const ProjectedPropertyValue* editedValue = FindProjectedValue(editedSnapshot, baseValue.Property);
                if (!editedValue)
                {
                    continue;
                }

                const ProjectedPropertyValue* effectiveBaseValue = FindEffectiveBaseValue(
                    baseOverrides,
                    targetAlias,
                    baseValue.Property);
                if (!effectiveBaseValue)
                {
                    effectiveBaseValue = &baseValue;
                }

                if (effectiveBaseValue->Type == editedValue->Type &&
                    effectiveBaseValue->SerializedValue == editedValue->SerializedValue)
                {
                    continue;
                }

                PrefabPropertyOverride overrideValue;
                overrideValue.Target.NestedInstances = targetPath;
                overrideValue.Target.Alias = targetAlias;
                overrideValue.Value = *editedValue;
                outOverrides.Properties.push_back(std::move(overrideValue));
            }
        }

        void BuildNodeOverrides(
            const PrefabAsset& prefab,
            const EntitySubtreeSnapshotNode& baseNode,
            const EntitySubtreeSnapshotNode& editedNode,
            const Container::VariableArray<PrefabNestedInstanceId>& targetPath,
            const PrefabOverrideSet& baseOverrides,
            PrefabOverrideSet& outOverrides)
        {
            const IClass* entityClass = ResolveClassByStableId(baseNode.Object.Class);
            if (!entityClass || editedNode.Object.Class != baseNode.Object.Class)
            {
                return;
            }

            BuildObjectOverrides(
                baseNode.Object,
                editedNode.Object,
                *entityClass,
                baseNode.Alias,
                targetPath,
                baseOverrides,
                outOverrides);

            const size_t componentCount = baseNode.Components.size() < editedNode.Components.size()
                ? baseNode.Components.size()
                : editedNode.Components.size();
            for (size_t index = 0; index < componentCount; ++index)
            {
                const ComponentSubtreeSnapshot& baseComponent = baseNode.Components[index];
                const ComponentSubtreeSnapshot& editedComponent = editedNode.Components[index];
                const IClass* componentClass = ResolveClassByStableId(baseComponent.Object.Class);
                if (!componentClass)
                {
                    continue;
                }

                BuildObjectOverrides(
                    baseComponent.Object,
                    editedComponent.Object,
                    *componentClass,
                    baseComponent.Alias,
                    targetPath,
                    baseOverrides,
                    outOverrides);
            }

            const size_t childCount = baseNode.Children.size() < editedNode.Children.size()
                ? baseNode.Children.size()
                : editedNode.Children.size();
            for (size_t index = 0; index < childCount; ++index)
            {
                BuildNodeOverrides(
                    prefab,
                    baseNode.Children[index],
                    editedNode.Children[index],
                    targetPath,
                    baseOverrides,
                    outOverrides);
            }

            size_t nestedOrdinal = 0;
            for (const PrefabNestedPrefabInstance& nested : prefab.GetNestedPrefabs())
            {
                if (nested.ParentAlias != baseNode.Alias)
                {
                    continue;
                }

                const size_t editedNestedIndex = baseNode.Children.size() + nestedOrdinal;
                ++nestedOrdinal;
                if (editedNestedIndex >= editedNode.Children.size())
                {
                    continue;
                }

                const PrefabAsset* nestedPrefab = nested.Prefab.Get();
                if (!nestedPrefab || !nestedPrefab->HasTree())
                {
                    continue;
                }

                PrefabOverrideSet nestedBaseOverrides = nested.Overrides;
                AppendShiftedNestedOverrides(baseOverrides, nested.InstanceId, nestedBaseOverrides);

                BuildNodeOverrides(
                    *nestedPrefab,
                    nestedPrefab->GetTree().Root,
                    editedNode.Children[editedNestedIndex],
                    MakeNestedPath(targetPath, nested.InstanceId),
                    nestedBaseOverrides,
                    outOverrides);
            }
        }
    } // namespace

    PrefabAsset::PrefabAsset()
        : Resource(),
          m_NestedStorage(Container::MakeUnique<PrefabNestedPrefabStorage>())
    {
        SetResourceType(Identity("PrefabAsset"));
    }

    PrefabAsset::PrefabAsset(const FieldInitializer* initializer)
        : Resource(initializer),
          m_NestedStorage(Container::MakeUnique<PrefabNestedPrefabStorage>())
    {
        SetResourceType(Identity("PrefabAsset"));
    }

    PrefabAsset::PrefabAsset(const IUnknown* sourceObject)
        : Resource(sourceObject),
          m_NestedStorage(Container::MakeUnique<PrefabNestedPrefabStorage>())
    {
        SetResourceType(Identity("PrefabAsset"));
    }

    PrefabAsset::~PrefabAsset()
    {
        Finalize();
    }

    void PrefabAsset::SetTree(const EntitySubtreeSnapshot& tree)
    {
        m_Tree = tree;
        m_bHasTree = m_Tree.RootAlias != InvalidSubtreeSnapshotAliasId &&
                     m_Tree.Root.Alias == m_Tree.RootAlias;
    }

    void PrefabAsset::SetTree(EntitySubtreeSnapshot&& tree)
    {
        m_Tree = std::move(tree);
        m_bHasTree = m_Tree.RootAlias != InvalidSubtreeSnapshotAliasId &&
                     m_Tree.Root.Alias == m_Tree.RootAlias;
    }

    const Container::VariableArray<PrefabNestedPrefabInstance>& PrefabAsset::GetNestedPrefabs() const
    {
        return m_NestedStorage->Instances;
    }

    PrefabNestedInstanceId PrefabAsset::AddNestedPrefab(
        SubtreeSnapshotAliasId parentAlias,
        const ResourceRef<PrefabAsset>& prefab,
        const PrefabOverrideSet& overrides)
    {
        if (!m_bHasTree ||
            parentAlias == InvalidSubtreeSnapshotAliasId ||
            !prefab.IsValid() ||
            !ContainsEntityAlias(m_Tree.Root, parentAlias))
        {
            return InvalidPrefabNestedInstanceId;
        }

        const PrefabAsset* nestedPrefab = prefab.Get();
        if (!nestedPrefab || WouldCreateNestedPrefabCycle(*this, *nestedPrefab))
        {
            return InvalidPrefabNestedInstanceId;
        }

        if (m_NextNestedInstanceId == InvalidPrefabNestedInstanceId)
        {
            ++m_NextNestedInstanceId;
        }

        PrefabNestedPrefabInstance instance;
        instance.InstanceId = m_NextNestedInstanceId++;
        instance.ParentAlias = parentAlias;
        instance.Prefab = prefab;
        instance.Overrides = overrides;
        m_NestedStorage->Instances.push_back(std::move(instance));
        return m_NestedStorage->Instances.back().InstanceId;
    }

    PrefabOverrideSet PrefabAsset::BuildOverridesFrom(const Entity& editedRoot) const
    {
        PrefabOverrideSet result;
        if (!m_bHasTree)
        {
            return result;
        }

        EntitySubtreeSnapshot editedTree = RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(editedRoot);
        Container::VariableArray<PrefabNestedInstanceId> rootPath;
        PrefabOverrideSet rootOverrides;
        BuildNodeOverrides(*this, m_Tree.Root, editedTree.Root, rootPath, rootOverrides, result);

        return result;
    }

    bool ShouldSkipPrefabRestoreProperty(const IClass& concreteClass, const ClassProperty& property)
    {
        const Identity& propertyName = property.GetName();

        if (concreteClass.IsChildOf(Entity::StaticClass()))
        {
            if (propertyName == Identity("ObjectId") ||
                propertyName == Identity("bPendingDestroy"))
            {
                return true;
            }
        }

        if (concreteClass.IsChildOf(Component::Component::StaticClass()))
        {
            if (propertyName == Identity("ComponentId") ||
                propertyName == Identity("bBegunPlay"))
            {
                return true;
            }
        }

        if (concreteClass.IsChildOf(Component::MeshComponent::StaticClass()) &&
            propertyName == Identity("CurrentLODLevel"))
        {
            return true;
        }

        return false;
    }

} // namespace NorvesLib::Core
