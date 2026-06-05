#include "Animal.h"
#include "Object/ResourceRegistry.h"
#include "Object/SchemaProjection.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test;

namespace
{
    const ClassSchemaProjection *FindClass(const ClassSchemaSnapshot &snapshot, const char *name)
    {
        const Identity id(name);
        for (const ClassSchemaProjection &cls : snapshot.Classes)
        {
            if (Identity(cls.Name) == id)
            {
                return &cls;
            }
        }
        return nullptr;
    }

    const PropertySchemaProjection *FindProperty(const ClassSchemaProjection &cls, const char *name)
    {
        const Identity id(name);
        for (const PropertySchemaProjection &property : cls.Properties)
        {
            if (Identity(property.Name) == id)
            {
                return &property;
            }
        }
        return nullptr;
    }

    const ProjectedPropertyValue *FindProjectedValue(const ObjectSnapshot &snapshot, StablePropertyId propertyId)
    {
        for (const ProjectedPropertyValue &value : snapshot.Properties)
        {
            if (value.Property == propertyId)
            {
                return &value;
            }
        }
        return nullptr;
    }
}

int main()
{
    std::cout << "SchemaProjectionTest start\n";

    const IClass *dogClass = Dog::StaticClass();
    assert(dogClass != nullptr);

    ClassSchemaSnapshot schemaSnapshot = RuntimeSchemaProjector::BuildClassSchemaSnapshot();
    const ClassSchemaProjection *dogSchema = FindClass(schemaSnapshot, "Dog");
    assert(dogSchema != nullptr);
    assert(dogSchema->StableId == MakeStableSchemaId("NorvesLib", "Class", dogClass->GetClassName().GetView()));
    assert(dogSchema->ParentStableId == MakeStableSchemaId("NorvesLib", "Class", Animal::StaticClass()->GetClassName().GetView()));

    const PropertySchemaProjection *ageSchema = FindProperty(*dogSchema, "Age");
    assert(ageSchema != nullptr);
    assert(ageSchema->StableId == MakeStableSchemaId("NorvesLib", "Property", dogClass->GetClassName().GetView(), Identity("Age").GetView()));
    assert(ageSchema->Type == MakeStableSchemaId("NorvesLib", "Type", Container::StringView("int32")));

    bool bFoundAgeType = false;
    for (const TypeInfo &type : schemaSnapshot.Types)
    {
        if (type.StableId == ageSchema->Type)
        {
            bFoundAgeType = true;
            break;
        }
    }
    assert(bFoundAgeType);

    Dog dog;
    dog.Initialize();

    StableObjectRef ref;
    ref.SceneId = 123;
    ref.Path = "Scene/Dog";
    ObjectSnapshot objectSnapshot = RuntimeSchemaProjector::BuildObjectSnapshot(dog, ref);
    assert(objectSnapshot.Ref.SceneId == 123);
    assert(objectSnapshot.Path == "Scene/Dog");
    assert(objectSnapshot.Class == dogSchema->StableId);

    const ProjectedPropertyValue *ageValue = FindProjectedValue(objectSnapshot, ageSchema->StableId);
    assert(ageValue != nullptr);
    assert(ageValue->Type == ageSchema->Type);
    assert(ageValue->SerializedValue == "3");

    ResourceRegistry registry;
    assert(registry.Initialize());
    auto resource = registry.Load<Resource>("Assets/Data/Projection.resource");
    assert(resource != nullptr);

    ResourceList resources = RuntimeSchemaProjector::ProjectResources(registry.GetRecords());
    assert(resources.Resources.size() == 1);
    assert(resources.Resources[0].URI == "Assets/Data/Projection.resource");
    assert(resources.Resources[0].Type == Identity("Resource"));
    registry.Shutdown();

    std::cout << "SchemaProjectionTest passed\n";
    return 0;
}
