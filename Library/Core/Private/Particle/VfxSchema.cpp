#include "Particle/VfxSchema.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        template <typename T, size_t N>
        constexpr uint32_t CountOf(const T (&)[N])
        {
            return static_cast<uint32_t>(N);
        }

        constexpr VfxParameterSchema VectorParameter(const char* name, VfxValueType type, double min, double max,
            const char* unit, float x, float y, float z, float w, const char* description)
        {
            return {name, type, min, max, unit, {x, y, z, w}, 0u, "", nullptr, 0u, description};
        }

        constexpr VfxParameterSchema UIntParameter(const char* name, uint32_t min, uint32_t max,
            const char* unit, uint32_t value, const char* description)
        {
            return {name, VfxValueType::UInt, static_cast<double>(min), static_cast<double>(max), unit,
                {0.0f, 0.0f, 0.0f, 0.0f}, value, "", nullptr, 0u, description};
        }

        constexpr VfxParameterSchema BoolParameter(const char* name, bool value, const char* description)
        {
            return {name, VfxValueType::Bool, 0.0, 1.0, "", {0.0f, 0.0f, 0.0f, 0.0f},
                value ? 1u : 0u, "", nullptr, 0u, description};
        }

        constexpr VfxParameterSchema EnumParameter(const char* name, const char* const* names,
            uint32_t count, uint32_t value, const char* description)
        {
            return {name, VfxValueType::Enum, 0.0, static_cast<double>(count - 1u), "",
                {0.0f, 0.0f, 0.0f, 0.0f}, value, "", names, count, description};
        }

        constexpr VfxParameterSchema NameParameter(const char* name, uint32_t min, uint32_t max,
            const char* value, const char* description)
        {
            return {name, VfxValueType::Name, static_cast<double>(min), static_cast<double>(max), "",
                {0.0f, 0.0f, 0.0f, 0.0f}, 0u, value, nullptr, 0u, description};
        }

        constexpr const char* kSpaceNames[] = {"Screen", "World"};
        constexpr const char* kLayerNames[] = {"Default", "Transparent", "UI"};
        constexpr const char* kBlendNames[] = {"Opaque", "Masked", "Translucent", "Additive", "Modulate"};

        constexpr VfxParameterSchema kEmitterParameters[] = {
            UIntParameter("MaxParticles", 1u, 65536u, "count", 128u, "粒の最大数"),
            EnumParameter("Space", kSpaceNames, CountOf(kSpaceNames), 0u, "シミュレーション空間"),
            UIntParameter("Seed", 0u, 4294967295u, "", 0u, "エミッタの乱数種")
        };
        constexpr VfxParameterSchema kFlipbookParameters[] = {
            UIntParameter("Columns", 1u, 64u, "count", 1u, "列数"),
            UIntParameter("Rows", 1u, 64u, "count", 1u, "行数"),
            UIntParameter("FrameCount", 1u, 4096u, "count", 1u, "フレーム数"),
            VectorParameter("FramesPerSecond", VfxValueType::Float, 0.0, 240.0, "1/s", 0.0f, 0.0f, 0.0f, 0.0f, "毎秒のフレーム数"),
            UIntParameter("StartFrame", 0u, 4095u, "count", 0u, "開始フレーム"),
            BoolParameter("Loop", true, "繰り返し再生")
        };
        constexpr VfxParameterSchema kSpawnRateParameters[] = {
            VectorParameter("Rate", VfxValueType::Float, 0.0, 100000.0, "1/s", 10.0f, 0.0f, 0.0f, 0.0f, "毎秒の生成数")
        };
        constexpr VfxParameterSchema kSpawnBurstParameters[] = {
            UIntParameter("Count", 0u, 65536u, "count", 10u, "一度に生成する粒の数"),
            VectorParameter("Time", VfxValueType::Float, 0.0, 3600.0, "s", 0.0f, 0.0f, 0.0f, 0.0f, "生成時刻")
        };
        constexpr VfxParameterSchema kSpawnOnEventParameters[] = {
            NameParameter("EventName", 1u, 63u, "Event", "生成の契機となるイベント名"),
            UIntParameter("Count", 1u, 65536u, "count", 1u, "イベントごとの生成数")
        };
        constexpr VfxParameterSchema kInitPositionParameters[] = {
            VectorParameter("Offset", VfxValueType::Vector3, -1e6, 1e6, "space", 0.0f, 0.0f, 0.0f, 0.0f, "初期位置のオフセット")
        };
        constexpr VfxParameterSchema kInitVelocityBoxParameters[] = {
            VectorParameter("Min", VfxValueType::Vector3, -1e5, 1e5, "space/s", 0.0f, 0.0f, 0.0f, 0.0f, "初速度の下限"),
            VectorParameter("Max", VfxValueType::Vector3, -1e5, 1e5, "space/s", 0.0f, 0.0f, 0.0f, 0.0f, "初速度の上限")
        };
        constexpr VfxParameterSchema kInitLifetimeParameters[] = {
            VectorParameter("Lifetime", VfxValueType::Float, 0.001, 3600.0, "s", 1.0f, 0.0f, 0.0f, 0.0f, "粒の寿命")
        };
        constexpr VfxParameterSchema kInitColorParameters[] = {
            VectorParameter("Color", VfxValueType::Color, 0.0, 64.0, "linear_rgba", 1.0f, 1.0f, 1.0f, 1.0f, "粒の初期色")
        };
        constexpr VfxParameterSchema kInitSizeParameters[] = {
            VectorParameter("Size", VfxValueType::Vector2, 0.0001, 100000.0, "space", 8.0f, 8.0f, 0.0f, 0.0f, "粒の初期サイズ")
        };
        constexpr VfxParameterSchema kAccelerationParameters[] = {
            VectorParameter("Acceleration", VfxValueType::Vector3, -1e5, 1e5, "space/s^2", 0.0f, 0.0f, 0.0f, 0.0f, "粒に加える加速度")
        };
        constexpr VfxParameterSchema kSpriteRendererParameters[] = {
            EnumParameter("Layer", kLayerNames, CountOf(kLayerNames), 2u, "描画レイヤー"),
            EnumParameter("Blend", kBlendNames, CountOf(kBlendNames), 2u, "ブレンド方式"),
            UIntParameter("SortKey", 0u, 4294967295u, "", 0u, "描画順のキー"),
            NameParameter("Flipbook", 0u, 63u, "", "フリップブック名")
        };

        constexpr VfxObjectSchema kEmitterSchema = {
            "Emitter", static_cast<VfxModuleType>(0), VfxModuleStage::Object,
            kEmitterParameters, CountOf(kEmitterParameters)
        };
        constexpr VfxObjectSchema kFlipbookSchema = {
            "Flipbook", static_cast<VfxModuleType>(0), VfxModuleStage::Object,
            kFlipbookParameters, CountOf(kFlipbookParameters)
        };
        constexpr VfxObjectSchema kModuleSchemas[] = {
            {"SpawnRate", VfxModuleType::SpawnRate, VfxModuleStage::Spawn, kSpawnRateParameters, CountOf(kSpawnRateParameters)},
            {"SpawnBurst", VfxModuleType::SpawnBurst, VfxModuleStage::Spawn, kSpawnBurstParameters, CountOf(kSpawnBurstParameters)},
            {"SpawnOnEvent", VfxModuleType::SpawnOnEvent, VfxModuleStage::Spawn, kSpawnOnEventParameters, CountOf(kSpawnOnEventParameters)},
            {"InitPosition", VfxModuleType::InitPosition, VfxModuleStage::Initialize, kInitPositionParameters, CountOf(kInitPositionParameters)},
            {"InitVelocityBox", VfxModuleType::InitVelocityBox, VfxModuleStage::Initialize, kInitVelocityBoxParameters, CountOf(kInitVelocityBoxParameters)},
            {"InitLifetime", VfxModuleType::InitLifetime, VfxModuleStage::Initialize, kInitLifetimeParameters, CountOf(kInitLifetimeParameters)},
            {"InitColor", VfxModuleType::InitColor, VfxModuleStage::Initialize, kInitColorParameters, CountOf(kInitColorParameters)},
            {"InitSize", VfxModuleType::InitSize, VfxModuleStage::Initialize, kInitSizeParameters, CountOf(kInitSizeParameters)},
            {"Acceleration", VfxModuleType::Acceleration, VfxModuleStage::Update, kAccelerationParameters, CountOf(kAccelerationParameters)},
            {"SpriteRenderer", VfxModuleType::SpriteRenderer, VfxModuleStage::Render, kSpriteRendererParameters, CountOf(kSpriteRendererParameters)}
        };

        bool Fail(Container::String* outError, const char* code, const char* object, const char* parameter)
        {
            if (outError != nullptr)
            {
                *outError = code;
                *outError += " ";
                *outError += object;
                *outError += ".";
                *outError += parameter != nullptr ? parameter : "Unknown";
            }
            return false;
        }

        const char* FindParameterOwner(const VfxParameterSchema& parameter)
        {
            const char* owner = nullptr;
            bool bAmbiguous = false;
            const auto inspect = [&](const VfxObjectSchema& object)
            {
                for (uint32_t index = 0u; index < object.ParameterCount; ++index)
                {
                    const VfxParameterSchema& candidate = object.Parameters[index];
                    if (&candidate == &parameter)
                    {
                        owner = object.Name;
                        return true;
                    }
                    if (parameter.Name != nullptr && candidate.Type == parameter.Type &&
                        candidate.Min == parameter.Min && candidate.Max == parameter.Max &&
                        std::strcmp(candidate.Name, parameter.Name) == 0)
                    {
                        if (owner != nullptr && std::strcmp(owner, object.Name) != 0)
                        {
                            bAmbiguous = true;
                        }
                        else
                        {
                            owner = object.Name;
                        }
                    }
                }
                return false;
            };
            if (inspect(kEmitterSchema) || inspect(kFlipbookSchema))
            {
                return owner;
            }
            for (const VfxObjectSchema& object : kModuleSchemas)
            {
                if (inspect(object))
                {
                    return owner;
                }
            }
            return owner != nullptr && !bAmbiguous ? owner : "Value";
        }

        bool ValidateName(const Container::String& name, double min, double max,
            const char* object, const char* parameter, Container::String* outError)
        {
            const double length = static_cast<double>(name.size());
            if (length < min || length > max)
            {
                return Fail(outError, "invalid_name", object, parameter);
            }
            for (size_t index = 0u; index < name.size(); ++index)
            {
                const char c = name[index];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
                {
                    return Fail(outError, "invalid_name", object, parameter);
                }
            }
            return true;
        }

        float Component(const Math::Vector4& value, uint32_t index)
        {
            switch (index)
            {
            case 0u: return value.x;
            case 1u: return value.y;
            case 2u: return value.z;
            default: return value.w;
            }
        }

        bool ValidateValue(const VfxParameterSchema& schema, const VfxValue& value,
            const char* object, Container::String* outError)
        {
            if (schema.Type != value.Type)
            {
                return Fail(outError, "type_mismatch", object, schema.Name);
            }

            uint32_t componentCount = 0u;
            switch (value.Type)
            {
            case VfxValueType::Float: componentCount = 1u; break;
            case VfxValueType::Vector2: componentCount = 2u; break;
            case VfxValueType::Vector3: componentCount = 3u; break;
            case VfxValueType::Color: componentCount = 4u; break;
            case VfxValueType::UInt:
            case VfxValueType::Bool:
            case VfxValueType::Enum:
                if (static_cast<double>(value.UInt) < schema.Min || static_cast<double>(value.UInt) > schema.Max ||
                    (value.Type == VfxValueType::Bool && value.UInt > 1u) ||
                    (value.Type == VfxValueType::Enum && value.UInt >= schema.EnumCount))
                {
                    return Fail(outError, "out_of_range", object, schema.Name);
                }
                return true;
            case VfxValueType::Name:
                return ValidateName(value.Name, schema.Min, schema.Max, object, schema.Name, outError);
            default:
                return Fail(outError, "type_mismatch", object, schema.Name);
            }

            for (uint32_t index = 0u; index < componentCount; ++index)
            {
                if (!std::isfinite(Component(value.Vector, index)))
                {
                    return Fail(outError, "not_finite", object, schema.Name);
                }
            }
            const float min = static_cast<float>(schema.Min);
            const float max = static_cast<float>(schema.Max);
            for (uint32_t index = 0u; index < componentCount; ++index)
            {
                const float component = Component(value.Vector, index);
                if (component < min || component > max)
                {
                    return Fail(outError, "out_of_range", object, schema.Name);
                }
            }
            return true;
        }
    } // namespace

    VfxValue VfxValue::MakeFloat(float value)
    {
        VfxValue result;
        result.Vector.x = value;
        return result;
    }

    VfxValue VfxValue::MakeUInt(uint32_t value)
    {
        VfxValue result;
        result.Type = VfxValueType::UInt;
        result.UInt = value;
        return result;
    }

    VfxValue VfxValue::MakeBool(bool value)
    {
        VfxValue result;
        result.Type = VfxValueType::Bool;
        result.UInt = value ? 1u : 0u;
        return result;
    }

    VfxValue VfxValue::MakeVector2(const Math::Vector2& value)
    {
        VfxValue result;
        result.Type = VfxValueType::Vector2;
        result.Vector.x = value.x;
        result.Vector.y = value.y;
        return result;
    }

    VfxValue VfxValue::MakeVector3(const Math::Vector3& value)
    {
        VfxValue result;
        result.Type = VfxValueType::Vector3;
        result.Vector.x = value.x;
        result.Vector.y = value.y;
        result.Vector.z = value.z;
        return result;
    }

    VfxValue VfxValue::MakeColor(const Math::Vector4& value)
    {
        VfxValue result;
        result.Type = VfxValueType::Color;
        result.Vector = value;
        return result;
    }

    VfxValue VfxValue::MakeEnum(uint32_t value)
    {
        VfxValue result;
        result.Type = VfxValueType::Enum;
        result.UInt = value;
        return result;
    }

    VfxValue VfxValue::MakeName(const char* value)
    {
        VfxValue result;
        result.Type = VfxValueType::Name;
        result.Name = value;
        return result;
    }

    bool VfxValue::operator==(const VfxValue& other) const
    {
        if (Type != other.Type)
        {
            return false;
        }
        switch (Type)
        {
        case VfxValueType::Float:
            return Vector.x == other.Vector.x;
        case VfxValueType::Vector2:
            return Vector.x == other.Vector.x && Vector.y == other.Vector.y;
        case VfxValueType::Vector3:
            return Vector.x == other.Vector.x && Vector.y == other.Vector.y && Vector.z == other.Vector.z;
        case VfxValueType::Color:
            return Vector == other.Vector;
        case VfxValueType::UInt:
        case VfxValueType::Bool:
        case VfxValueType::Enum:
            return UInt == other.UInt;
        case VfxValueType::Name:
            if (Name.size() != other.Name.size())
            {
                return false;
            }
            for (size_t index = 0u; index < Name.size(); ++index)
            {
                if (Name[index] != other.Name[index])
                {
                    return false;
                }
            }
            return true;
        default:
            return false;
        }
    }

    Container::Span<const VfxObjectSchema> GetVfxModuleSchemas()
    {
        return Container::Span<const VfxObjectSchema>(kModuleSchemas);
    }

    const VfxObjectSchema* FindVfxModuleSchema(VfxModuleType type)
    {
        for (const VfxObjectSchema& schema : kModuleSchemas)
        {
            if (schema.ModuleType == type)
            {
                return &schema;
            }
        }
        return nullptr;
    }

    const VfxObjectSchema* FindVfxModuleSchemaByName(const char* name)
    {
        if (name == nullptr)
        {
            return nullptr;
        }
        for (const VfxObjectSchema& schema : kModuleSchemas)
        {
            if (std::strcmp(schema.Name, name) == 0)
            {
                return &schema;
            }
        }
        return nullptr;
    }

    const VfxObjectSchema& GetVfxEmitterSchema()
    {
        return kEmitterSchema;
    }

    const VfxObjectSchema& GetVfxFlipbookSchema()
    {
        return kFlipbookSchema;
    }

    int32_t FindVfxParameterIndex(const VfxObjectSchema& schema, const char* name)
    {
        if (name == nullptr)
        {
            return -1;
        }
        for (uint32_t index = 0u; index < schema.ParameterCount; ++index)
        {
            if (std::strcmp(schema.Parameters[index].Name, name) == 0)
            {
                return static_cast<int32_t>(index);
            }
        }
        return -1;
    }

    VfxValue MakeVfxDefaultValue(const VfxParameterSchema& schema)
    {
        switch (schema.Type)
        {
        case VfxValueType::Float:
            return VfxValue::MakeFloat(schema.DefaultVector[0]);
        case VfxValueType::UInt:
            return VfxValue::MakeUInt(schema.DefaultUInt);
        case VfxValueType::Bool:
            return VfxValue::MakeBool(schema.DefaultUInt != 0u);
        case VfxValueType::Vector2:
            return VfxValue::MakeVector2(Math::Vector2(schema.DefaultVector[0], schema.DefaultVector[1]));
        case VfxValueType::Vector3:
            return VfxValue::MakeVector3(Math::Vector3(schema.DefaultVector[0], schema.DefaultVector[1], schema.DefaultVector[2]));
        case VfxValueType::Color:
            return VfxValue::MakeColor(Math::Vector4(schema.DefaultVector[0], schema.DefaultVector[1], schema.DefaultVector[2], schema.DefaultVector[3]));
        case VfxValueType::Enum:
            return VfxValue::MakeEnum(schema.DefaultUInt);
        case VfxValueType::Name:
            return VfxValue::MakeName(schema.DefaultName);
        default:
            return {};
        }
    }

    bool ValidateVfxValue(const VfxParameterSchema& schema, const VfxValue& value, Container::String* outError)
    {
        return ValidateValue(schema, value, FindParameterOwner(schema), outError);
    }

    VfxModule MakeVfxDefaultModule(VfxModuleType type)
    {
        VfxModule module;
        module.Type = type;
        const VfxObjectSchema* schema = FindVfxModuleSchema(type);
        if (schema != nullptr)
        {
            module.Values.reserve(schema->ParameterCount);
            for (uint32_t index = 0u; index < schema->ParameterCount; ++index)
            {
                module.Values.push_back(MakeVfxDefaultValue(schema->Parameters[index]));
            }
        }
        return module;
    }

    VfxEmitterAsset MakeVfxDefaultEmitter(const char* name)
    {
        VfxEmitterAsset emitter;
        emitter.Name = name;
        emitter.Properties.reserve(kEmitterSchema.ParameterCount);
        for (uint32_t index = 0u; index < kEmitterSchema.ParameterCount; ++index)
        {
            emitter.Properties.push_back(MakeVfxDefaultValue(kEmitterSchema.Parameters[index]));
        }
        return emitter;
    }

    bool ValidateVfxModule(const VfxModule& module, Container::String* outError)
    {
        const VfxObjectSchema* schema = FindVfxModuleSchema(module.Type);
        if (schema == nullptr)
        {
            return Fail(outError, "unknown_module", "Module", "Type");
        }
        if (module.Values.size() != schema->ParameterCount)
        {
            return Fail(outError, "parameter_count", schema->Name, "Values");
        }
        for (uint32_t index = 0u; index < schema->ParameterCount; ++index)
        {
            if (!ValidateValue(schema->Parameters[index], module.Values[index], schema->Name, outError))
            {
                return false;
            }
        }
        if (module.Type == VfxModuleType::InitVelocityBox)
        {
            const Math::Vector4& min = module.Values[0].Vector;
            const Math::Vector4& max = module.Values[1].Vector;
            if (min.x > max.x || min.y > max.y || min.z > max.z)
            {
                return Fail(outError, "min_greater_than_max", schema->Name, "Min");
            }
        }
        return true;
    }

    bool ValidateVfxEmitter(const VfxEmitterAsset& emitter, Container::String* outError)
    {
        if (!ValidateName(emitter.Name, 1.0, 63.0, "Emitter", "Name", outError))
        {
            return false;
        }
        if (emitter.Properties.size() != kEmitterSchema.ParameterCount)
        {
            return Fail(outError, "parameter_count", "Emitter", "Properties");
        }
        for (uint32_t index = 0u; index < kEmitterSchema.ParameterCount; ++index)
        {
            if (!ValidateValue(kEmitterSchema.Parameters[index], emitter.Properties[index], "Emitter", outError))
            {
                return false;
            }
        }
        bool bHasRenderer = false;
        for (const VfxModule& module : emitter.Modules)
        {
            if (!ValidateVfxModule(module, outError))
            {
                return false;
            }
            if (module.Type == VfxModuleType::SpriteRenderer)
            {
                if (bHasRenderer)
                {
                    return Fail(outError, "multiple_renderers", "Emitter", "Modules");
                }
                bHasRenderer = true;
            }
        }
        return true;
    }

    bool ValidateVfxFlipbook(const VfxFlipbookImportSettings& flipbook, Container::String* outError)
    {
        if (!ValidateName(flipbook.Name, 1.0, 63.0, "Flipbook", "Name", outError))
        {
            return false;
        }
        const VfxValue values[] = {
            VfxValue::MakeUInt(flipbook.Columns),
            VfxValue::MakeUInt(flipbook.Rows),
            VfxValue::MakeUInt(flipbook.FrameCount),
            VfxValue::MakeFloat(flipbook.FramesPerSecond),
            VfxValue::MakeUInt(flipbook.StartFrame),
            VfxValue::MakeBool(flipbook.bLoop)
        };
        for (uint32_t index = 0u; index < kFlipbookSchema.ParameterCount; ++index)
        {
            if (!ValidateValue(kFlipbookSchema.Parameters[index], values[index], "Flipbook", outError))
            {
                return false;
            }
        }
        if (flipbook.FrameCount > flipbook.Columns * flipbook.Rows)
        {
            return Fail(outError, "out_of_range", "Flipbook", "FrameCount");
        }
        if (flipbook.StartFrame >= flipbook.FrameCount)
        {
            return Fail(outError, "out_of_range", "Flipbook", "StartFrame");
        }
        return true;
    }

    bool ValidateVfxEffect(const VfxEffectAsset& effect, Container::String* outError)
    {
        for (size_t index = 0u; index < effect.Flipbooks.size(); ++index)
        {
            if (!ValidateVfxFlipbook(effect.Flipbooks[index], outError))
            {
                return false;
            }
            for (size_t previous = 0u; previous < index; ++previous)
            {
                if (effect.Flipbooks[index].Name == effect.Flipbooks[previous].Name)
                {
                    return Fail(outError, "duplicate_name", "Effect", "Flipbooks");
                }
            }
        }
        for (size_t index = 0u; index < effect.Emitters.size(); ++index)
        {
            const VfxEmitterAsset& emitter = effect.Emitters[index];
            if (!ValidateVfxEmitter(emitter, outError))
            {
                return false;
            }
            for (size_t previous = 0u; previous < index; ++previous)
            {
                if (emitter.Name == effect.Emitters[previous].Name)
                {
                    return Fail(outError, "duplicate_name", "Effect", "Emitters");
                }
            }
            for (const VfxModule& module : emitter.Modules)
            {
                if (module.Type != VfxModuleType::SpriteRenderer)
                {
                    continue;
                }
                const Container::String& flipbookName = module.Values[3].Name;
                if (flipbookName.empty())
                {
                    continue;
                }
                bool bFound = false;
                for (const VfxFlipbookImportSettings& flipbook : effect.Flipbooks)
                {
                    if (flipbook.Name == flipbookName)
                    {
                        bFound = true;
                        break;
                    }
                }
                if (!bFound)
                {
                    return Fail(outError, "unknown_flipbook", "SpriteRenderer", "Flipbook");
                }
            }
        }
        return true;
    }

    bool SetVfxModuleParameter(VfxModule& module, const char* name, const VfxValue& value, Container::String* outError)
    {
        const VfxObjectSchema* schema = FindVfxModuleSchema(module.Type);
        if (schema == nullptr)
        {
            return Fail(outError, "unknown_module", "Module", "Type");
        }
        const int32_t index = FindVfxParameterIndex(*schema, name);
        if (index < 0)
        {
            return Fail(outError, "unknown_parameter", schema->Name, name);
        }
        if (module.Values.size() != schema->ParameterCount)
        {
            return Fail(outError, "parameter_count", schema->Name, "Values");
        }
        VfxModule candidate = module;
        candidate.Values[static_cast<uint32_t>(index)] = value;
        if (!ValidateVfxModule(candidate, outError))
        {
            return false;
        }
        module = std::move(candidate);
        return true;
    }

    bool SetVfxEmitterProperty(VfxEmitterAsset& emitter, const char* name, const VfxValue& value, Container::String* outError)
    {
        const int32_t index = FindVfxParameterIndex(kEmitterSchema, name);
        if (index < 0)
        {
            return Fail(outError, "unknown_parameter", "Emitter", name);
        }
        if (emitter.Properties.size() != kEmitterSchema.ParameterCount)
        {
            return Fail(outError, "parameter_count", "Emitter", "Properties");
        }
        VfxEmitterAsset candidate = emitter;
        candidate.Properties[static_cast<uint32_t>(index)] = value;
        if (!ValidateVfxEmitter(candidate, outError))
        {
            return false;
        }
        emitter = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Core::Particle
