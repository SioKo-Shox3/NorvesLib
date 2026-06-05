#pragma once

#include "Object/IClass.h"
#include "Object/ObjectHandle.h"
#include "Object/ResourceRegistry.h"
#include "Object/RuntimeSchema.h"
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    struct ProjectedPropertyValue
    {
        StablePropertyId Property = InvalidSchemaId;
        StableTypeId Type = InvalidSchemaId;
        Container::String SerializedValue;
    };

    struct PropertySchemaProjection
    {
        StablePropertyId StableId = InvalidSchemaId;
        Container::String Name;
        StableTypeId Type = InvalidSchemaId;
        PropertyFlags Flags = PropertyFlags::None;
        StorageKind Storage = StorageKind::Member;
    };

    struct FunctionSchemaProjection
    {
        StableFunctionId StableId = InvalidSchemaId;
        Container::String Name;
        StableTypeId ReturnType = InvalidSchemaId;
        FunctionFlags Flags = FunctionFlags::None;
        ThreadPolicy Thread = ThreadPolicy::GameThreadOnly;
    };

    struct ClassSchemaProjection
    {
        StableClassId StableId = InvalidSchemaId;
        StableClassId ParentStableId = InvalidSchemaId;
        Container::String Name;
        uint32_t SchemaVersion = 1;
        Container::VariableArray<PropertySchemaProjection> Properties;
        Container::VariableArray<FunctionSchemaProjection> Functions;
    };

    struct ClassSchemaSnapshot
    {
        Container::VariableArray<ClassSchemaProjection> Classes;
        Container::VariableArray<TypeInfo> Types;
        uint32_t SchemaVersion = 1;
    };

    struct ObjectSnapshot
    {
        StableObjectRef Ref;
        StableClassId Class = InvalidSchemaId;
        Container::String Path;
        Container::VariableArray<ProjectedPropertyValue> Properties;
    };

    struct PropertyDelta
    {
        StableObjectRef Object;
        ProjectedPropertyValue Value;
    };

    struct SetPropertyRequest
    {
        StableObjectRef Object;
        StablePropertyId Property = InvalidSchemaId;
        ProjectedPropertyValue Value;
        uint64_t RequestId = 0;
    };

    struct InvokeFunctionRequest
    {
        StableObjectRef Object;
        StableFunctionId Function = InvalidSchemaId;
        Container::VariableArray<ProjectedPropertyValue> Arguments;
        uint64_t RequestId = 0;
    };

    struct ResourceSnapshot
    {
        ResourceId Id = 0;
        Container::String URI;
        ResourceType Type;
        ResourceLoadState LoadState = ResourceState::Unloaded;
        uint64_t VersionHash = 0;
        size_t DependencyCount = 0;
        size_t MemoryUsage = 0;
    };

    struct ResourceList
    {
        Container::VariableArray<ResourceSnapshot> Resources;
    };

    class RuntimeSchemaProjector
    {
    public:
        static ClassSchemaSnapshot BuildClassSchemaSnapshot(const char *moduleName = "NorvesLib");
        static ClassSchemaProjection ProjectClass(const IClass &cls, const char *moduleName = "NorvesLib");
        static ObjectSnapshot BuildObjectSnapshot(const Object &object, StableObjectRef ref, const char *moduleName = "NorvesLib");
        static ResourceSnapshot ProjectResource(const ResourceRecord &record);
        static ResourceList ProjectResources(const Container::VariableArray<ResourceRecord> &records);

    private:
        static StableTypeId GetStableTypeId(TypeId type);
    };

} // namespace NorvesLib::Core
