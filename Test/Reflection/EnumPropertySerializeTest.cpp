#include "Object/Object.h"
#include "Object/Reflection.h"
#include "Object/RuntimeSchema.h"
#include "Object/SchemaProjection.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    enum class RenderBlendMode : uint8_t
    {
        Opaque = 0,
        Masked = 1,
        Translucent = 2
    };

    enum class SignedMode : int16_t
    {
        Negative = -5,
        Zero = 0,
        Positive = 7
    };

    class EnumHolder : public Object
    {
        REFLECTION_CLASS(EnumHolder, Object)

    public:
        void SetBlendMode(RenderBlendMode mode)
        {
            BlendMode = mode;
        }

        RenderBlendMode GetBlendMode() const
        {
            return BlendMode;
        }

        void SetLodBias(uint8_t bias)
        {
            LodBias = bias;
        }

        uint8_t GetLodBias() const
        {
            return LodBias;
        }

    protected:
        PROPERTY(RenderBlendMode, BlendMode)
        PROPERTY(uint8_t, LodBias)
    };

    IMPLEMENT_CLASS(EnumHolder, Object)

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

    StablePropertyId MakeHolderPropertyId(const char* name)
    {
        return MakeStableSchemaId(
            "NorvesLib",
            "Property",
            EnumHolder::StaticClass()->GetClassName().GetView(),
            Identity(name).GetView());
    }

    void TestEnumDetailRoundTrip()
    {
        const RenderBlendMode mode = RenderBlendMode::Translucent;
        Container::String serialized;
        assert(Detail::SerializeValue<RenderBlendMode>(&mode, serialized));
        assert(serialized == "2");

        RenderBlendMode parsed = RenderBlendMode::Opaque;
        assert(Detail::DeserializeValue<RenderBlendMode>(serialized, &parsed));
        assert(parsed == RenderBlendMode::Translucent);

        const SignedMode negative = SignedMode::Negative;
        Container::String serializedNegative;
        assert(Detail::SerializeValue<SignedMode>(&negative, serializedNegative));
        assert(serializedNegative == "-5");

        SignedMode parsedNegative = SignedMode::Zero;
        assert(Detail::DeserializeValue<SignedMode>(serializedNegative, &parsedNegative));
        assert(parsedNegative == SignedMode::Negative);

        // underlying範囲外・非数値は拒否する
        RenderBlendMode rejectTarget = RenderBlendMode::Opaque;
        assert(!Detail::DeserializeValue<RenderBlendMode>(Container::String("256"), &rejectTarget));
        assert(!Detail::DeserializeValue<RenderBlendMode>(Container::String("-1"), &rejectTarget));
        assert(!Detail::DeserializeValue<RenderBlendMode>(Container::String("abc"), &rejectTarget));
        assert(rejectTarget == RenderBlendMode::Opaque);
    }

    void TestEightBitIntegersAreNumericized()
    {
        const uint8_t value = 7;
        Container::String serialized;
        assert(Detail::SerializeValue<uint8_t>(&value, serialized));
        assert(serialized == "7"); // 旧実装はBEL(0x07)1文字だった

        uint8_t parsed = 0;
        assert(Detail::DeserializeValue<uint8_t>(serialized, &parsed));
        assert(parsed == 7);

        const int8_t negativeValue = -12;
        Container::String serializedNegative;
        assert(Detail::SerializeValue<int8_t>(&negativeValue, serializedNegative));
        assert(serializedNegative == "-12");

        int8_t parsedNegative = 0;
        assert(Detail::DeserializeValue<int8_t>(serializedNegative, &parsedNegative));
        assert(parsedNegative == -12);

        uint8_t overflowTarget = 0;
        assert(!Detail::DeserializeValue<uint8_t>(Container::String("256"), &overflowTarget));
        assert(!Detail::DeserializeValue<uint8_t>(Container::String("-1"), &overflowTarget));

        int8_t signedOverflowTarget = 0;
        assert(!Detail::DeserializeValue<int8_t>(Container::String("128"), &signedOverflowTarget));

        // boolは従来どおり "1"/"0"
        const bool flag = true;
        Container::String serializedFlag;
        assert(Detail::SerializeValue<bool>(&flag, serializedFlag));
        assert(serializedFlag == "1");
    }

    void TestTrailingJunkIsRejected()
    {
        // prefix-parse成功値（"1abc"→1等）を拒否する厳格仕様の検証
        int32_t intTarget = 0;
        assert(!Detail::DeserializeValue<int32_t>(Container::String("1abc"), &intTarget));
        assert(intTarget == 0); // 失敗時にoutValueを汚さない

        uint8_t byteTarget = 0;
        assert(!Detail::DeserializeValue<uint8_t>(Container::String("7x"), &byteTarget));

        float floatTarget = 0.0f;
        assert(!Detail::DeserializeValue<float>(Container::String("64.5junk"), &floatTarget));

        RenderBlendMode enumTarget = RenderBlendMode::Opaque;
        assert(!Detail::DeserializeValue<RenderBlendMode>(Container::String("2x"), &enumTarget));
        assert(enumTarget == RenderBlendMode::Opaque);

        // 正常値は引き続き通る（末尾空白のみは許容）
        assert(Detail::DeserializeValue<int32_t>(Container::String("42"), &intTarget));
        assert(intTarget == 42);
    }

    void TestPropertyValueEnumRoundTrip()
    {
        PropertyValue value = PropertyValue::Create(RenderBlendMode::Masked);
        Container::String serialized;
        assert(value.Serialize(serialized));
        assert(serialized == "1");

        const TypeInfo* typeInfo = TypeRegistry::Get().Find(TypeRegistry::Get().GetTypeId<RenderBlendMode>());
        assert(typeInfo != nullptr);
        assert(typeInfo->Kind == TypeKind::Enum);

        PropertyValue parsed;
        assert(parsed.DeserializeStable(typeInfo->StableId, serialized));
        const RenderBlendMode* parsedMode = parsed.Get<RenderBlendMode>();
        assert(parsedMode != nullptr);
        assert(*parsedMode == RenderBlendMode::Masked);
    }

    void TestObjectSnapshotContainsEnumProperty()
    {
        EnumHolder holder;
        holder.Initialize();
        holder.SetBlendMode(RenderBlendMode::Translucent);
        holder.SetLodBias(3);

        StableObjectRef ref;
        ref.Path = "Scene/EnumHolder";
        const ObjectSnapshot snapshot = RuntimeSchemaProjector::BuildObjectSnapshot(holder, ref);

        const ProjectedPropertyValue* blendMode = FindProjectedValue(snapshot, MakeHolderPropertyId("BlendMode"));
        assert(blendMode != nullptr); // 旧実装ではsilently dropされてnullptrだった
        assert(blendMode->SerializedValue == "2");

        const ProjectedPropertyValue* lodBias = FindProjectedValue(snapshot, MakeHolderPropertyId("LodBias"));
        assert(lodBias != nullptr);
        assert(lodBias->SerializedValue == "3");

        // DeserializeStable→ApplyValue（SpawnPrefabのApplyPrefabValueと同型経路）で復元できること
        EnumHolder target;
        target.Initialize();
        const ClassProperty* property = EnumHolder::StaticClass()->GetProperty(Identity("BlendMode"));
        assert(property != nullptr);

        PropertyValue parsedValue;
        assert(parsedValue.DeserializeStable(blendMode->Type, blendMode->SerializedValue));
        assert(property->ApplyValue(&target, parsedValue));
        assert(target.GetBlendMode() == RenderBlendMode::Translucent);

        target.Finalize();
        holder.Finalize();
    }

    void TestBoundaryValues()
    {
        // 8bit整数の厳密な境界値
        const int8_t i8min = -128;
        const int8_t i8max = 127;
        const uint8_t u8max = 255;
        Container::String s;
        assert(Detail::SerializeValue<int8_t>(&i8min, s) && s == "-128");
        assert(Detail::SerializeValue<int8_t>(&i8max, s) && s == "127");
        assert(Detail::SerializeValue<uint8_t>(&u8max, s) && s == "255");
        int8_t i8 = 0;
        assert(Detail::DeserializeValue<int8_t>(Container::String("-128"), &i8) && i8 == -128);
        assert(Detail::DeserializeValue<int8_t>(Container::String("127"), &i8) && i8 == 127);
        uint8_t u8 = 0;
        assert(Detail::DeserializeValue<uint8_t>(Container::String("255"), &u8) && u8 == 255);

        // 末尾空白のみは許容、先頭符号"+"は受理、空文字列は拒否
        int32_t i32 = 0;
        assert(Detail::DeserializeValue<int32_t>(Container::String("42 "), &i32) && i32 == 42);
        assert(Detail::DeserializeValue<int32_t>(Container::String("+7"), &i32) && i32 == 7);
        assert(!Detail::DeserializeValue<int32_t>(Container::String(""), &i32));
    }
}

int main()
{
    std::cout << "EnumPropertySerializeTest start\n";

    TestEnumDetailRoundTrip();
    TestEightBitIntegersAreNumericized();
    TestTrailingJunkIsRejected();
    TestBoundaryValues();
    TestPropertyValueEnumRoundTrip();
    TestObjectSnapshotContainsEnumProperty();

    std::cout << "EnumPropertySerializeTest passed\n";
    return 0;
}
