#include "Object/Entity.h"
#include "Object/RuntimeSchema.h"
#include "Object/SchemaProjection.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
namespace Math = NorvesLib::Math;

namespace
{
    struct UnsupportedStruct
    {
        int Value = 0;
    };

    const ProjectedPropertyValue* FindProjectedValue(const ObjectSnapshot& snapshot, StablePropertyId propertyId)
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

    StablePropertyId MakeEntityPropertyId(const char* name)
    {
        return MakeStableSchemaId(
            "NorvesLib",
            "Property",
            Entity::StaticClass()->GetClassName().GetView(),
            Identity(name).GetView());
    }

    StableTypeId MakeTypeStableId(const char* name)
    {
        return MakeStableSchemaId("NorvesLib", "Type", Container::StringView(name));
    }

    template <typename T>
    void AssertTypeInfo(const char* expectedName, const T& sampleValue, const char* expectedSerialized)
    {
        TypeRegistry& registry = TypeRegistry::Get();
        const TypeId typeId = registry.GetTypeId<T>();
        const TypeInfo* typeInfo = registry.Find(typeId);
        assert(typeInfo != nullptr);
        assert(typeInfo->Kind == TypeKind::Struct);
        assert(typeInfo->Name == expectedName);
        assert(typeInfo->StableId == MakeTypeStableId(expectedName));
        assert(registry.FindStable(typeInfo->StableId) == typeInfo);
        assert(typeInfo->Ops.Serialize != nullptr);
        assert(typeInfo->Ops.Deserialize != nullptr);

        Container::String serialized;
        assert(typeInfo->Ops.Serialize(&sampleValue, serialized));
        assert(serialized == expectedSerialized);

        T parsedValue;
        assert(typeInfo->Ops.Deserialize(serialized, &parsedValue));
        assert(parsedValue == sampleValue);
    }

    template <typename T>
    bool TryDeserialize(const char* text)
    {
        T value;
        return Detail::DeserializeValue<T>(Container::String(text), &value);
    }

    template <typename T>
    void AssertDetailRoundTrip(const T& value, const char* expectedSerialized)
    {
        Container::String serialized;
        assert(Detail::SerializeValue<T>(&value, serialized));
        assert(serialized == expectedSerialized);

        T parsed;
        assert(Detail::DeserializeValue<T>(serialized, &parsed));
        assert(parsed == value);
    }

    void TestDetailRoundTrip()
    {
        AssertDetailRoundTrip(
            Math::Vector3(1.25f, -2.5f, 3.75f),
            "Vector3(1.25,-2.5,3.75)");
        AssertDetailRoundTrip(
            Math::Quaternion(0.25f, -0.5f, 0.75f, 1.0f),
            "Quaternion(0.25,-0.5,0.75,1)");
        AssertDetailRoundTrip(
            Math::Transform(
                Math::Vector3(1.0f, 2.0f, 3.0f),
                Math::Quaternion(0.25f, -0.5f, 0.75f, 1.0f),
                Math::Vector3(2.0f, 3.0f, 4.0f)),
            "Transform(Vector3(1,2,3),Quaternion(0.25,-0.5,0.75,1),Vector3(2,3,4))");
    }

    void TestPropertyValueRoundTrip()
    {
        const Math::Vector3 vectorValue(1.25f, -2.5f, 3.75f);
        PropertyValue vectorProperty = PropertyValue::Create(vectorValue);
        Container::String serializedVector;
        assert(vectorProperty.Serialize(serializedVector));
        assert(serializedVector == "Vector3(1.25,-2.5,3.75)");

        PropertyValue parsedVectorProperty;
        assert(parsedVectorProperty.Deserialize<Math::Vector3>(serializedVector));
        const Math::Vector3* parsedVector = parsedVectorProperty.Get<Math::Vector3>();
        assert(parsedVector != nullptr);
        assert(*parsedVector == vectorValue);

        const Math::Quaternion quaternionValue(0.25f, -0.5f, 0.75f, 1.0f);
        PropertyValue quaternionProperty = PropertyValue::Create(quaternionValue);
        Container::String serializedQuaternion;
        assert(quaternionProperty.Serialize(serializedQuaternion));
        assert(serializedQuaternion == "Quaternion(0.25,-0.5,0.75,1)");

        PropertyValue parsedQuaternionProperty;
        assert(parsedQuaternionProperty.Deserialize<Math::Quaternion>(serializedQuaternion));
        const Math::Quaternion* parsedQuaternion = parsedQuaternionProperty.Get<Math::Quaternion>();
        assert(parsedQuaternion != nullptr);
        assert(*parsedQuaternion == quaternionValue);

        const Math::Transform transformValue(
            Math::Vector3(1.0f, 2.0f, 3.0f),
            Math::Quaternion(0.25f, -0.5f, 0.75f, 1.0f),
            Math::Vector3(2.0f, 3.0f, 4.0f));
        PropertyValue transformProperty = PropertyValue::Create(transformValue);
        Container::String serializedTransform;
        assert(transformProperty.Serialize(serializedTransform));
        assert(serializedTransform == "Transform(Vector3(1,2,3),Quaternion(0.25,-0.5,0.75,1),Vector3(2,3,4))");

        PropertyValue parsedTransformProperty;
        assert(parsedTransformProperty.Deserialize<Math::Transform>(serializedTransform));
        const Math::Transform* parsedTransform = parsedTransformProperty.Get<Math::Transform>();
        assert(parsedTransform != nullptr);
        assert(*parsedTransform == transformValue);
    }

    void TestTypeInfo()
    {
        AssertTypeInfo(
            "Math::Vector3",
            Math::Vector3(1.25f, -2.5f, 3.75f),
            "Vector3(1.25,-2.5,3.75)");
        AssertTypeInfo(
            "Math::Quaternion",
            Math::Quaternion(0.25f, -0.5f, 0.75f, 1.0f),
            "Quaternion(0.25,-0.5,0.75,1)");
        AssertTypeInfo(
            "Math::Transform",
            Math::Transform(
                Math::Vector3(1.0f, 2.0f, 3.0f),
                Math::Quaternion(0.25f, -0.5f, 0.75f, 1.0f),
                Math::Vector3(2.0f, 3.0f, 4.0f)),
            "Transform(Vector3(1,2,3),Quaternion(0.25,-0.5,0.75,1),Vector3(2,3,4))");
    }

    void TestEntitySnapshotProjection()
    {
        Entity entity;
        entity.Initialize();
        entity.SetLocalPosition(Math::Vector3(1.0f, 2.0f, 3.0f));
        entity.SetLocalRotation(Math::Quaternion(0.25f, -0.5f, 0.75f, 1.0f));
        entity.SetLocalScale(Math::Vector3(2.0f, 3.0f, 4.0f));

        StableObjectRef ref;
        ref.SceneId = 17;
        ref.Path = "Scene/Entity";
        ObjectSnapshot snapshot = RuntimeSchemaProjector::BuildObjectSnapshot(entity, ref);
        assert(snapshot.Ref.SceneId == 17);
        assert(snapshot.Path == "Scene/Entity");
        assert(snapshot.Class == MakeStableSchemaId("NorvesLib", "Class", Entity::StaticClass()->GetClassName().GetView()));

        const ProjectedPropertyValue* position = FindProjectedValue(snapshot, MakeEntityPropertyId("Position"));
        const ProjectedPropertyValue* rotation = FindProjectedValue(snapshot, MakeEntityPropertyId("Rotation"));
        const ProjectedPropertyValue* scale = FindProjectedValue(snapshot, MakeEntityPropertyId("Scale"));
        assert(position != nullptr);
        assert(rotation != nullptr);
        assert(scale != nullptr);
        assert(position->Type == MakeTypeStableId("Math::Vector3"));
        assert(rotation->Type == MakeTypeStableId("Math::Quaternion"));
        assert(scale->Type == MakeTypeStableId("Math::Vector3"));
        assert(position->SerializedValue == "Vector3(1,2,3)");
        assert(rotation->SerializedValue == "Quaternion(0.25,-0.5,0.75,1)");
        assert(scale->SerializedValue == "Vector3(2,3,4)");

        entity.Finalize();
    }

    void TestNegativeParses()
    {
        assert(!TryDeserialize<Math::Vector3>("Vector3(1,2)"));
        assert(!TryDeserialize<Math::Quaternion>("Quaternion(0,0,0)"));
        assert(!TryDeserialize<Math::Transform>("Transform(Vector3(1,2,3),Quaternion(0,0,0,1),Vector3(1,2))"));
        assert(!TryDeserialize<Math::Vector3>("Vector3(1,2,3)junk"));
        assert(!TryDeserialize<Math::Transform>("Transform(Quaternion(0,0,0,1),Vector3(1,2,3),Vector3(1,1,1))"));

        UnsupportedStruct unsupported;
        Container::String unsupportedSerialized;
        assert(!Detail::SerializeValue<UnsupportedStruct>(&unsupported, unsupportedSerialized));
        assert(!Detail::DeserializeValue<UnsupportedStruct>(Container::String("UnsupportedStruct(1)"), &unsupported));

        const Math::Vector3 oldValue(9.0f, 8.0f, 7.0f);
        PropertyValue preserved = PropertyValue::Create(oldValue);
        const TypeId oldType = preserved.GetType();
        assert(!preserved.Deserialize<Math::Vector3>(Container::String("Vector3(1,2)")));
        assert(preserved.GetType() == oldType);
        const Math::Vector3* preservedValue = preserved.Get<Math::Vector3>();
        assert(preservedValue != nullptr);
        assert(*preservedValue == oldValue);
    }
}

int main()
{
    std::cout << "StructPropertySerializeTest start\n";

    TestDetailRoundTrip();
    TestPropertyValueRoundTrip();
    TestTypeInfo();
    TestEntitySnapshotProjection();
    TestNegativeParses();

    std::cout << "StructPropertySerializeTest passed\n";
    return 0;
}
