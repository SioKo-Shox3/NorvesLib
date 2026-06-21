#include "Animal.h"
#include "Object/IClass.h"
#include "Object/RuntimeSchema.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
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
    const TypeId vector2Type = typeRegistry.GetTypeId<NorvesLib::Math::Vector2>();
    const TypeId vector4Type = typeRegistry.GetTypeId<NorvesLib::Math::Vector4>();

    const TypeInfo *intInfo = typeRegistry.Find(intType);
    const TypeInfo *floatInfo = typeRegistry.Find(floatType);
    const TypeInfo *boolInfo = typeRegistry.Find(boolType);
    const TypeInfo *vector2Info = typeRegistry.Find(vector2Type);
    const TypeInfo *vector4Info = typeRegistry.Find(vector4Type);
    assert(intInfo != nullptr);
    assert(floatInfo != nullptr);
    assert(boolInfo != nullptr);
    assert(vector2Info != nullptr);
    assert(vector4Info != nullptr);
    assert(intInfo->Id != floatInfo->Id);
    assert(intInfo->StableId == MakeStableSchemaId("NorvesLib", "Type", Container::StringView("int32")));
    assert(typeRegistry.FindStable(intInfo->StableId) == intInfo);
    assert(vector2Info->Name == "Math::Vector2");
    assert(vector4Info->Name == "Math::Vector4");
    assert(vector2Info->StableId == MakeStableSchemaId("NorvesLib", "Type", Container::StringView("Math::Vector2")));
    assert(vector4Info->StableId == MakeStableSchemaId("NorvesLib", "Type", Container::StringView("Math::Vector4")));

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

    PropertyValue vector2Value = PropertyValue::Create(NorvesLib::Math::Vector2(12.5f, -3.25f));
    assert(vector2Value.Serialize(serialized));
    assert(serialized == "Vector2(12.5,-3.25)");

    PropertyValue vector2RoundTrip;
    assert(vector2RoundTrip.DeserializeStable(vector2Info->StableId, serialized));
    const NorvesLib::Math::Vector2 *roundTripVector2 = vector2RoundTrip.Get<NorvesLib::Math::Vector2>();
    assert(roundTripVector2 != nullptr);
    assert(roundTripVector2->x == 12.5f);
    assert(roundTripVector2->y == -3.25f);

    PropertyValue vector4Value = PropertyValue::Create(NorvesLib::Math::Vector4(0.25f, 0.5f, 0.75f, 1.0f));
    assert(vector4Value.Serialize(serialized));
    assert(serialized == "Vector4(0.25,0.5,0.75,1)");

    PropertyValue vector4RoundTrip;
    assert(vector4RoundTrip.DeserializeStable(vector4Info->StableId, serialized));
    const NorvesLib::Math::Vector4 *roundTripVector4 = vector4RoundTrip.Get<NorvesLib::Math::Vector4>();
    assert(roundTripVector4 != nullptr);
    assert(roundTripVector4->x == 0.25f);
    assert(roundTripVector4->y == 0.5f);
    assert(roundTripVector4->z == 0.75f);
    assert(roundTripVector4->w == 1.0f);

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
