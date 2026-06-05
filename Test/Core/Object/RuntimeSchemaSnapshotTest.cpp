#include "Animal.h"
#include "Object/IClass.h"
#include "Object/RuntimeSchema.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test;

namespace
{
    const PropertyDesc *FindProperty(const ClassInfo &info, const char *name)
    {
        const Identity id(name);
        for (const PropertyDesc &property : info.Properties)
        {
            if (Identity(property.Name) == id)
            {
                return &property;
            }
        }
        return nullptr;
    }

    const FunctionDesc *FindFunction(const ClassInfo &info, const char *name)
    {
        const Identity id(name);
        for (const FunctionDesc &function : info.Functions)
        {
            if (Identity(function.Name) == id)
            {
                return &function;
            }
        }
        return nullptr;
    }
}

int main()
{
    std::cout << "RuntimeSchemaSnapshotTest start\n";

    TypeRegistry &typeRegistry = TypeRegistry::Get();
    const TypeId intType = typeRegistry.GetTypeId<int>();
    const TypeId floatType = typeRegistry.GetTypeId<float>();
    const TypeId boolType = typeRegistry.GetTypeId<bool>();

    const TypeInfo *intInfo = typeRegistry.Find(intType);
    const TypeInfo *floatInfo = typeRegistry.Find(floatType);
    const TypeInfo *boolInfo = typeRegistry.Find(boolType);
    assert(intInfo != nullptr);
    assert(floatInfo != nullptr);
    assert(boolInfo != nullptr);
    assert(intInfo->Id != floatInfo->Id);
    assert(intInfo->StableId == MakeStableSchemaId("NorvesLib", "Type", Container::StringView("int32")));
    assert(typeRegistry.FindStable(intInfo->StableId) == intInfo);

    PropertyValue value = PropertyValue::Create<int>(42);
    assert(value.IsValid());
    assert(value.GetType() == intType);
    assert(value.Get<int>() != nullptr);
    assert(*value.Get<int>() == 42);

    PropertyValue copiedValue = value;
    assert(copiedValue.Get<int>() != nullptr);
    assert(*copiedValue.Get<int>() == 42);
    assert(copiedValue.Equals(value));

    Container::String serialized;
    assert(value.Serialize(serialized));
    assert(serialized == "42");

    const IClass *dogClass = Dog::StaticClass();
    assert(dogClass != nullptr);

    ClassInfo firstSnapshot = BuildClassInfoSnapshot(*dogClass);
    ClassInfo secondSnapshot = BuildClassInfoSnapshot(*dogClass);
    assert(firstSnapshot.Id == dogClass->GetClassId());
    assert(firstSnapshot.StableId == secondSnapshot.StableId);
    assert(firstSnapshot.StableId == MakeStableSchemaId("NorvesLib", "Class", dogClass->GetClassName().GetView()));

    const PropertyDesc *ageProperty = FindProperty(firstSnapshot, "Age");
    const PropertyDesc *goodBoyProperty = FindProperty(firstSnapshot, "IsGoodBoy");
    assert(ageProperty != nullptr);
    assert(goodBoyProperty != nullptr);
    assert(ageProperty->Type == intType);
    assert(goodBoyProperty->Type == boolType);
    assert(ageProperty->StableId == MakeStableSchemaId("NorvesLib", "Property", dogClass->GetClassName().GetView(), Identity("Age").GetView()));

    const FunctionDesc *bmiFunction = FindFunction(firstSnapshot, "GetBMI");
    assert(bmiFunction != nullptr);
    assert(bmiFunction->ReturnType == floatType);
    assert(bmiFunction->StableId == MakeStableSchemaId("NorvesLib", "Function", dogClass->GetClassName().GetView(), Identity("GetBMI").GetView()));

    ClassDefaults defaultsA;
    defaultsA.SetDefault(ageProperty->StableId, PropertyValue::Create<int>(3));

    ClassDefaults defaultsB;
    defaultsB.SetDefault(ageProperty->StableId, PropertyValue::Create<int>(5));

    auto diff = defaultsA.Diff(defaultsB);
    assert(diff.size() == 1);
    assert(diff[0] == ageProperty->StableId);

    std::cout << "RuntimeSchemaSnapshotTest passed\n";
    return 0;
}
