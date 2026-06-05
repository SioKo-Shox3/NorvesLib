#include "Object/SchemaProjection.h"

namespace NorvesLib::Core
{
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
