#include "Object/SchemaProjection.h"

#include "Component/Component.h"
#include "Object/Entity.h"

namespace NorvesLib::Core
{
    namespace
    {
        Container::String GetObjectClassName(const Object& object, const char* fallbackName)
        {
            const IClass* cls = object.GetClass();
            if (!cls)
            {
                return fallbackName;
            }

            return cls->GetClassName().ToString();
        }

        StableObjectRef MakeSnapshotRef(const StableObjectRef& rootRef, const Container::String& path, bool bRoot)
        {
            StableObjectRef ref;
            if (bRoot)
            {
                ref = rootRef;
            }
            else
            {
                ref.SceneId = rootRef.SceneId;
            }

            ref.Path = path;
            return ref;
        }

        size_t ConsumeSiblingIndex(
            Container::UnorderedMap<Identity, size_t, Identity::Hasher>& siblingCounts,
            const IClass& cls)
        {
            size_t& nextIndex = siblingCounts[cls.GetClassName()];
            const size_t index = nextIndex;
            ++nextIndex;
            return index;
        }

        Container::String BuildRelationshipPath(
            const Container::String& parentPath,
            const char* relationshipName,
            const IClass& cls,
            size_t siblingIndex)
        {
            Container::StringBuilder builder(parentPath);
            builder.Append("/");
            builder.Append(relationshipName);
            builder.Append("/");
            builder.Append(cls.GetClassName().ToString());
            builder.Append("[");
            builder.AppendFormat("%zu", siblingIndex);
            builder.Append("]");
            return builder.ToString();
        }

        EntitySubtreeSnapshotNode BuildEntitySubtreeSnapshotNode(
            const Entity& entity,
            const StableObjectRef& rootRef,
            const Container::String& path,
            SubtreeSnapshotAliasId alias,
            SubtreeSnapshotAliasId parentAlias,
            SubtreeSnapshotAliasId& nextAlias,
            const char* moduleName,
            bool bRoot)
        {
            EntitySubtreeSnapshotNode node;
            node.Alias = alias;
            node.ParentAlias = parentAlias;
            node.Object = RuntimeSchemaProjector::BuildObjectSnapshot(
                entity,
                MakeSnapshotRef(rootRef, path, bRoot),
                moduleName);

            Container::VariableArray<Component::Component*> components = entity.GetComponents();
            node.Components.reserve(components.size());
            Container::UnorderedMap<Identity, size_t, Identity::Hasher> componentSiblingCounts;
            for (Component::Component* component : components)
            {
                if (!component || !component->GetClass())
                {
                    continue;
                }

                const IClass& componentClass = *component->GetClass();
                const size_t siblingIndex = ConsumeSiblingIndex(componentSiblingCounts, componentClass);
                const Container::String componentPath = BuildRelationshipPath(
                    path,
                    "Components",
                    componentClass,
                    siblingIndex);

                ComponentSubtreeSnapshot componentSnapshot;
                componentSnapshot.Alias = nextAlias++;
                componentSnapshot.OwnerAlias = alias;
                componentSnapshot.Object = RuntimeSchemaProjector::BuildObjectSnapshot(
                    *component,
                    MakeSnapshotRef(rootRef, componentPath, false),
                    moduleName);
                node.Components.push_back(std::move(componentSnapshot));
            }

            Container::VariableArray<Entity*> children = entity.GetChildEntities();
            node.Children.reserve(children.size());
            Container::UnorderedMap<Identity, size_t, Identity::Hasher> childSiblingCounts;
            for (Entity* child : children)
            {
                if (!child || !child->GetClass())
                {
                    continue;
                }

                const IClass& childClass = *child->GetClass();
                const size_t siblingIndex = ConsumeSiblingIndex(childSiblingCounts, childClass);
                const Container::String childPath = BuildRelationshipPath(
                    path,
                    "Children",
                    childClass,
                    siblingIndex);

                const SubtreeSnapshotAliasId childAlias = nextAlias++;
                node.Children.push_back(BuildEntitySubtreeSnapshotNode(
                    *child,
                    rootRef,
                    childPath,
                    childAlias,
                    alias,
                    nextAlias,
                    moduleName,
                    false));
            }

            return node;
        }
    } // namespace

    ClassSchemaSnapshot RuntimeSchemaProjector::BuildClassSchemaSnapshot(const char *moduleName)
    {
        ClassSchemaSnapshot snapshot;

        Container::VariableArray<const IClass *> classes = ClassRegistry::Get().GetAllClasses();
        snapshot.Classes.reserve(classes.size());
        for (const IClass *cls : classes)
        {
            if (cls)
            {
                snapshot.Classes.push_back(ProjectClass(*cls, moduleName));
            }
        }

        snapshot.Types = TypeRegistry::Get().GetAllTypes();
        return snapshot;
    }

    ClassSchemaProjection RuntimeSchemaProjector::ProjectClass(const IClass &cls, const char *moduleName)
    {
        ClassInfo info = BuildClassInfoSnapshot(cls, moduleName);

        ClassSchemaProjection projection;
        projection.StableId = info.StableId;
        projection.Name = info.Name;
        projection.SchemaVersion = info.SchemaVersion;
        projection.ParentStableId = cls.GetParentClass()
            ? MakeStableSchemaId(moduleName, "Class", cls.GetParentClass()->GetClassName().GetView())
            : InvalidSchemaId;

        projection.Properties.reserve(info.Properties.size());
        for (const PropertyDesc &property : info.Properties)
        {
            PropertySchemaProjection propertyProjection;
            propertyProjection.StableId = property.StableId;
            propertyProjection.Name = property.Name;
            propertyProjection.Type = GetStableTypeId(property.Type);
            propertyProjection.Flags = property.Flags;
            propertyProjection.Storage = property.Storage;
            projection.Properties.push_back(std::move(propertyProjection));
        }

        projection.Functions.reserve(info.Functions.size());
        for (const FunctionDesc &function : info.Functions)
        {
            FunctionSchemaProjection functionProjection;
            functionProjection.StableId = function.StableId;
            functionProjection.Name = function.Name;
            functionProjection.ReturnType = GetStableTypeId(function.ReturnType);
            functionProjection.Flags = function.Flags;
            functionProjection.Thread = function.Thread;
            projection.Functions.push_back(std::move(functionProjection));
        }

        return projection;
    }

    ObjectSnapshot RuntimeSchemaProjector::BuildObjectSnapshot(const Object &object, StableObjectRef ref, const char *moduleName)
    {
        ObjectSnapshot snapshot;
        snapshot.Ref = std::move(ref);
        snapshot.Path = snapshot.Ref.Path;
        snapshot.Class = MakeStableSchemaId(moduleName, "Class", object.GetClass()->GetClassName().GetView());

        Container::VariableArray<const ClassProperty *> properties = object.GetClass()->GetAllProperties();
        snapshot.Properties.reserve(properties.size());
        for (const ClassProperty *property : properties)
        {
            if (!property)
            {
                continue;
            }

            const TypeInfo *typeInfo = TypeRegistry::Get().Find(property->GetRuntimeTypeId());
            const void *valuePtr = property->GetValuePtr(&object);
            if (!typeInfo || !typeInfo->Ops.Serialize || !valuePtr)
            {
                continue;
            }

            ProjectedPropertyValue value;
            value.Property = MakeStableSchemaId(
                moduleName,
                "Property",
                object.GetClass()->GetClassName().GetView(),
                property->GetName().GetView());
            value.Type = typeInfo->StableId;
            if (typeInfo->Ops.Serialize(valuePtr, value.SerializedValue))
            {
                snapshot.Properties.push_back(std::move(value));
            }
        }

        return snapshot;
    }

    EntitySubtreeSnapshot RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(
        const Entity& root,
        StableObjectRef rootRef,
        const char* moduleName)
    {
        EntitySubtreeSnapshot snapshot;
        snapshot.RootAlias = 1;
        snapshot.RootPath = rootRef.HasPath()
            ? rootRef.Path
            : GetObjectClassName(root, "Entity");

        rootRef.Path = snapshot.RootPath;

        SubtreeSnapshotAliasId nextAlias = snapshot.RootAlias + 1;
        snapshot.Root = BuildEntitySubtreeSnapshotNode(
            root,
            rootRef,
            snapshot.RootPath,
            snapshot.RootAlias,
            InvalidSubtreeSnapshotAliasId,
            nextAlias,
            moduleName,
            true);

        return snapshot;
    }

    ResourceSnapshot RuntimeSchemaProjector::ProjectResource(const ResourceRecord &record)
    {
        ResourceSnapshot snapshot;
        snapshot.Id = record.Id;
        snapshot.URI = record.URI;
        snapshot.Type = record.Type;
        snapshot.LoadState = record.LoadState;
        snapshot.VersionHash = record.VersionHash;
        snapshot.DependencyCount = record.DependencyCount;
        snapshot.MemoryUsage = record.MemoryUsage;
        return snapshot;
    }

    ResourceList RuntimeSchemaProjector::ProjectResources(const Container::VariableArray<ResourceRecord> &records)
    {
        ResourceList list;
        list.Resources.reserve(records.size());
        for (const ResourceRecord &record : records)
        {
            list.Resources.push_back(ProjectResource(record));
        }
        return list;
    }

    StableTypeId RuntimeSchemaProjector::GetStableTypeId(TypeId type)
    {
        const TypeInfo *typeInfo = TypeRegistry::Get().Find(type);
        return typeInfo ? typeInfo->StableId : InvalidSchemaId;
    }

} // namespace NorvesLib::Core
