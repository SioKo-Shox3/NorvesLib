#include "Particle/VfxEffectSerialization.h"

#include "Particle/VfxSchema.h"
#include "Text/JsonDocument.h"
#include "Text/JsonUnicodeScalar.h"
#include "Text/JsonWriter.h"
#include "Text/UnicodeText.h"

#include <bit>
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        bool Fail(Container::String& error, const char* code, const char* object, const char* field = nullptr)
        {
            error = code;
            error += " ";
            error += object;
            if (field != nullptr)
            {
                error += ".";
                error += field;
            }
            return false;
        }

        bool FailKey(Container::String& error, const char* code, const char* object, const Container::String& key)
        {
            Fail(error, code, object);
            error += ".";
            for (size_t index = 0; index < key.size(); ++index)
            {
                error.push_back(key[index]);
            }
            return false;
        }

        void CopyString(const Container::String& source, Container::String& destination)
        {
            Container::String copy;
            copy.reserve(source.size());
            for (size_t index = 0; index < source.size(); ++index)
            {
                copy.push_back(source[index]);
            }
            destination = std::move(copy);
        }

        bool SameString(const Container::String& first, const Container::String& second)
        {
            if (first.size() != second.size())
            {
                return false;
            }
            for (size_t index = 0; index < first.size(); ++index)
            {
                if (first[index] != second[index])
                {
                    return false;
                }
            }
            return true;
        }

        bool CheckObject(const JsonValue& object, const char* location, Container::String& error)
        {
            if (!object.IsObject())
            {
                return Fail(error, "json_parse", location);
            }
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                for (size_t previous = 0; previous < index; ++previous)
                {
                    if (SameString(object.GetMemberName(index), object.GetMemberName(previous)))
                    {
                        return FailKey(error, "json_parse", location, object.GetMemberName(index));
                    }
                }
            }
            return true;
        }

        bool CheckKeys(const JsonValue& object, const char* location, const char* const* keys,
            size_t keyCount, Container::String& error)
        {
            if (!CheckObject(object, location, error))
            {
                return false;
            }
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                const Container::String& name = object.GetMemberName(index);
                bool bKnown = false;
                for (size_t keyIndex = 0; keyIndex < keyCount; ++keyIndex)
                {
                    if (name == keys[keyIndex])
                    {
                        bKnown = true;
                        break;
                    }
                }
                if (!bKnown)
                {
                    return FailKey(error, "unknown_key", location, name);
                }
            }
            return true;
        }

        bool RequireMember(const JsonValue& object, const char* location, const char* key,
            JsonValue& result, Container::String& error)
        {
            result = object.FindMember(key);
            if (!result.IsValid())
            {
                return Fail(error, "missing_key", location, key);
            }
            return true;
        }

        bool ReadJsonValue(const JsonValue& json, const VfxObjectSchema& object,
            const VfxParameterSchema& schema, VfxValue& result, Container::String& error)
        {
            VfxValue value;
            value.Type = schema.Type;
            switch (schema.Type)
            {
            case VfxValueType::Float:
                if (!json.IsNumber())
                {
                    return Fail(error, "type_mismatch", object.Name, schema.Name);
                }
                value.Vector.x = static_cast<float>(json.AsNumber());
                break;
            case VfxValueType::UInt:
                if (!json.IsIntegerLiteral())
                {
                    return Fail(error, "type_mismatch", object.Name, schema.Name);
                }
                if (json.AsNumber() < 0.0 || json.AsNumber() > static_cast<double>(UINT32_MAX))
                {
                    return Fail(error, "out_of_range", object.Name, schema.Name);
                }
                value.UInt = static_cast<uint32_t>(json.AsNumber());
                break;
            case VfxValueType::Bool:
                if (!json.IsBoolean())
                {
                    return Fail(error, "type_mismatch", object.Name, schema.Name);
                }
                value.UInt = json.AsBool() ? 1u : 0u;
                break;
            case VfxValueType::Vector2:
            case VfxValueType::Vector3:
            case VfxValueType::Color:
            {
                const size_t count = schema.Type == VfxValueType::Vector2 ? 2u :
                    schema.Type == VfxValueType::Vector3 ? 3u : 4u;
                if (!json.IsArray() || json.GetArraySize() != count)
                {
                    return Fail(error, "type_mismatch", object.Name, schema.Name);
                }
                float components[4] = {};
                for (size_t index = 0; index < count; ++index)
                {
                    const JsonValue component = json.GetArrayElement(index);
                    if (!component.IsNumber())
                    {
                        return Fail(error, "type_mismatch", object.Name, schema.Name);
                    }
                    components[index] = static_cast<float>(component.AsNumber());
                }
                value.Vector.x = components[0];
                value.Vector.y = components[1];
                value.Vector.z = components[2];
                value.Vector.w = components[3];
                break;
            }
            case VfxValueType::Enum:
            {
                if (!json.IsString())
                {
                    return Fail(error, "type_mismatch", object.Name, schema.Name);
                }
                bool bFound = false;
                for (uint32_t index = 0; index < schema.EnumCount; ++index)
                {
                    if (json.AsString() == schema.EnumNames[index])
                    {
                        value.UInt = index;
                        bFound = true;
                        break;
                    }
                }
                if (!bFound)
                {
                    return Fail(error, "out_of_range", object.Name, schema.Name);
                }
                break;
            }
            case VfxValueType::Name:
                if (!json.IsString())
                {
                    return Fail(error, "type_mismatch", object.Name, schema.Name);
                }
                CopyString(json.AsString(), value.Name);
                break;
            default:
                return Fail(error, "type_mismatch", object.Name, schema.Name);
            }
            if (!ValidateVfxValue(schema, value, &error))
            {
                return false;
            }
            result = std::move(value);
            return true;
        }

        bool ReadJsonParameters(const JsonValue& json, const VfxObjectSchema& schema,
            bool bHasType, VariableArray<VfxValue>& values, Container::String& error)
        {
            if (!CheckObject(json, schema.Name, error))
            {
                return false;
            }
            for (size_t index = 0; index < json.GetObjectSize(); ++index)
            {
                const Container::String& key = json.GetMemberName(index);
                if (bHasType && key == "type")
                {
                    continue;
                }
                int32_t parameterIndex = -1;
                for (uint32_t parameter = 0; parameter < schema.ParameterCount; ++parameter)
                {
                    if (key == schema.Parameters[parameter].Name)
                    {
                        parameterIndex = static_cast<int32_t>(parameter);
                        break;
                    }
                }
                if (parameterIndex < 0)
                {
                    return FailKey(error, "unknown_key", schema.Name, key);
                }
                const uint32_t parameter = static_cast<uint32_t>(parameterIndex);
                if (!ReadJsonValue(json.GetMemberValue(index), schema, schema.Parameters[parameter],
                    values[parameter], error))
                {
                    return false;
                }
            }
            return true;
        }

        bool ReadJsonFlipbook(const JsonValue& json, VfxFlipbookImportSettings& result, Container::String& error)
        {
            constexpr const char* keys[] = {
                "name", "texture", "columns", "rows", "frame_count", "frames_per_second", "start_frame", "loop"
            };
            if (!CheckKeys(json, "Flipbook", keys, sizeof(keys) / sizeof(keys[0]), error))
            {
                return false;
            }
            JsonValue name;
            JsonValue texture;
            if (!RequireMember(json, "Flipbook", "name", name, error) ||
                !RequireMember(json, "Flipbook", "texture", texture, error))
            {
                return false;
            }
            if (!name.IsString())
            {
                return Fail(error, "json_parse", "Flipbook", "name");
            }
            if (!texture.IsString())
            {
                return Fail(error, "json_parse", "Flipbook", "texture");
            }
            VfxFlipbookImportSettings flipbook;
            CopyString(name.AsString(), flipbook.Name);
            CopyString(texture.AsString(), flipbook.Texture);
            const VfxObjectSchema& schema = GetVfxFlipbookSchema();
            VfxValue values[6];
            for (uint32_t index = 0; index < 6u; ++index)
            {
                values[index] = MakeVfxDefaultValue(schema.Parameters[index]);
                const JsonValue member = json.FindMember(keys[index + 2u]);
                if (member.IsValid() && !ReadJsonValue(member, schema, schema.Parameters[index], values[index], error))
                {
                    return false;
                }
            }
            flipbook.Columns = values[0].UInt;
            flipbook.Rows = values[1].UInt;
            flipbook.FrameCount = values[2].UInt;
            flipbook.FramesPerSecond = values[3].Vector.x;
            flipbook.StartFrame = values[4].UInt;
            flipbook.bLoop = values[5].UInt != 0u;
            result = std::move(flipbook);
            return true;
        }

        bool ReadJsonEmitter(const JsonValue& json, VfxEmitterAsset& result, Container::String& error)
        {
            constexpr const char* keys[] = {"name", "properties", "modules"};
            if (!CheckKeys(json, "Emitter", keys, sizeof(keys) / sizeof(keys[0]), error))
            {
                return false;
            }
            JsonValue name;
            JsonValue modules;
            if (!RequireMember(json, "Emitter", "name", name, error) ||
                !RequireMember(json, "Emitter", "modules", modules, error))
            {
                return false;
            }
            if (!name.IsString())
            {
                return Fail(error, "json_parse", "Emitter", "name");
            }
            if (!modules.IsArray())
            {
                return Fail(error, "json_parse", "Emitter", "modules");
            }
            VfxEmitterAsset emitter = MakeVfxDefaultEmitter("");
            CopyString(name.AsString(), emitter.Name);
            const JsonValue properties = json.FindMember("properties");
            if (properties.IsValid() &&
                !ReadJsonParameters(properties, GetVfxEmitterSchema(), false, emitter.Properties, error))
            {
                return false;
            }
            for (size_t index = 0; index < modules.GetArraySize(); ++index)
            {
                const JsonValue moduleJson = modules.GetArrayElement(index);
                if (!CheckObject(moduleJson, "Module", error))
                {
                    return false;
                }
                JsonValue type;
                if (!RequireMember(moduleJson, "Module", "type", type, error))
                {
                    return false;
                }
                if (!type.IsString())
                {
                    return Fail(error, "json_parse", "Module", "type");
                }
                const VfxObjectSchema* schema = nullptr;
                for (const VfxObjectSchema& candidate : GetVfxModuleSchemas())
                {
                    if (type.AsString() == candidate.Name)
                    {
                        schema = &candidate;
                        break;
                    }
                }
                if (schema == nullptr)
                {
                    return Fail(error, "unknown_module", "Module", "Type");
                }
                VfxModule module = MakeVfxDefaultModule(schema->ModuleType);
                if (!ReadJsonParameters(moduleJson, *schema, true, module.Values, error))
                {
                    return false;
                }
                emitter.Modules.push_back(std::move(module));
            }
            result = std::move(emitter);
            return true;
        }

        bool ValidateJsonString(const Container::String& value, const char* object,
            const char* field, Container::String& error)
        {
            if (!TextDetail::ForEachUnicodeScalar<Container::String::value_type>(
                {value.data(), value.size()}, [](uint32_t) {}))
            {
                return Fail(error, "invalid_utf8", object, field);
            }
            return true;
        }

        bool WriteJsonValue(JsonWriter& writer, const VfxObjectSchema& object,
            const VfxParameterSchema& schema, const VfxValue& value, Container::String& error)
        {
            switch (schema.Type)
            {
            case VfxValueType::Float:
                writer.WriteNumber(schema.Name, value.Vector.x);
                break;
            case VfxValueType::UInt:
                writer.WriteUInt64(schema.Name, value.UInt);
                break;
            case VfxValueType::Bool:
                writer.WriteBool(schema.Name, value.UInt != 0u);
                break;
            case VfxValueType::Vector2:
            case VfxValueType::Vector3:
            case VfxValueType::Color:
                writer.BeginArray(schema.Name);
                writer.WriteNumberElement(value.Vector.x);
                writer.WriteNumberElement(value.Vector.y);
                if (schema.Type != VfxValueType::Vector2)
                {
                    writer.WriteNumberElement(value.Vector.z);
                }
                if (schema.Type == VfxValueType::Color)
                {
                    writer.WriteNumberElement(value.Vector.w);
                }
                writer.EndArray();
                break;
            case VfxValueType::Enum:
                writer.WriteString(schema.Name, schema.EnumNames[value.UInt]);
                break;
            case VfxValueType::Name:
                writer.WriteString(schema.Name, value.Name);
                break;
            default:
                return Fail(error, "type_mismatch", object.Name, schema.Name);
            }
            return true;
        }

        bool WriteJsonParameters(JsonWriter& writer, const VfxObjectSchema& schema,
            const VariableArray<VfxValue>& values, Container::String& error)
        {
            for (uint32_t index = 0; index < schema.ParameterCount; ++index)
            {
                if (!WriteJsonValue(writer, schema, schema.Parameters[index], values[index], error))
                {
                    return false;
                }
            }
            return true;
        }

        class CookedWriter
        {
        public:
            explicit CookedWriter(Container::String& error) : m_Error(error) {}

            template <typename T>
            bool WriteNumber(const T& value)
            {
                uint8_t bytes[sizeof(T)];
                std::memcpy(bytes, &value, sizeof(T));
                if constexpr (std::endian::native == std::endian::big)
                {
                    for (size_t index = 0; index < sizeof(T) / 2; ++index)
                    {
                        const uint8_t byte = bytes[index];
                        bytes[index] = bytes[sizeof(T) - 1u - index];
                        bytes[sizeof(T) - 1u - index] = byte;
                    }
                }
                return WriteBytes(bytes, sizeof(bytes));
            }

            bool WriteCount(size_t count)
            {
                if (count > UINT32_MAX)
                {
                    return Fail(m_Error, "out_of_range", "Effect", "Count");
                }
                return WriteNumber(static_cast<uint32_t>(count));
            }

            bool WriteString(const Container::String& value, const char* location)
            {
                const size_t lengthOffset = m_Bytes.size();
                if (!WriteNumber(uint32_t{0}))
                {
                    return false;
                }
                const size_t start = m_Bytes.size();
                bool bWritten = true;
                const bool bValid = TextDetail::ForEachUnicodeScalar<Container::String::value_type>(
                    {value.data(), value.size()}, [&](uint32_t scalar)
                    {
                        if (bWritten)
                        {
                            const auto encoded = TextDetail::EncodeJsonUnicodeScalar<uint8_t>(scalar);
                            bWritten = WriteBytes(encoded.Units, encoded.Count);
                        }
                    });
                if (!bValid)
                {
                    return Fail(m_Error, "invalid_utf8", location);
                }
                if (!bWritten)
                {
                    return false;
                }
                const size_t length = m_Bytes.size() - start;
                if (length > UINT32_MAX)
                {
                    return Fail(m_Error, "out_of_range", location);
                }
                SetNumber(lengthOffset, static_cast<uint32_t>(length));
                return true;
            }

            void SetNumber(size_t offset, uint32_t value)
            {
                uint8_t bytes[sizeof(value)];
                std::memcpy(bytes, &value, sizeof(value));
                if constexpr (std::endian::native == std::endian::big)
                {
                    for (size_t index = 0; index < sizeof(value) / 2; ++index)
                    {
                        const uint8_t byte = bytes[index];
                        bytes[index] = bytes[sizeof(value) - 1u - index];
                        bytes[sizeof(value) - 1u - index] = byte;
                    }
                }
                std::memcpy(m_Bytes.data() + offset, bytes, sizeof(bytes));
            }

            VariableArray<uint8_t> TakeBytes()
            {
                return std::move(m_Bytes);
            }

            size_t Size() const
            {
                return m_Bytes.size();
            }

        private:
            bool WriteBytes(const uint8_t* data, size_t count)
            {
                constexpr uint64_t maxSize = static_cast<uint64_t>(UINT32_MAX) + 12u;
                if (m_Bytes.size() > maxSize || count > maxSize - m_Bytes.size() ||
                    count > m_Bytes.max_size() - m_Bytes.size())
                {
                    return Fail(m_Error, "out_of_range", "Effect", "Payload");
                }
                if (count != 0u)
                {
                    const size_t start = m_Bytes.size();
                    m_Bytes.resize(start + count);
                    std::memcpy(m_Bytes.data() + start, data, count);
                }
                return true;
            }

            VariableArray<uint8_t> m_Bytes;
            Container::String& m_Error;
        };

        class CookedReader
        {
        public:
            CookedReader(Container::Span<const uint8_t> bytes, Container::String& error)
                : m_Bytes(bytes), m_Error(error) {}

            template <typename T>
            bool ReadNumber(T& value, const char* location)
            {
                if (sizeof(T) > Remaining() || m_Bytes.data() == nullptr)
                {
                    return Fail(m_Error, "truncated", location);
                }
                uint8_t bytes[sizeof(T)];
                std::memcpy(bytes, m_Bytes.data() + m_Offset, sizeof(bytes));
                m_Offset += sizeof(bytes);
                if constexpr (std::endian::native == std::endian::big)
                {
                    for (size_t index = 0; index < sizeof(T) / 2; ++index)
                    {
                        const uint8_t byte = bytes[index];
                        bytes[index] = bytes[sizeof(T) - 1u - index];
                        bytes[sizeof(T) - 1u - index] = byte;
                    }
                }
                std::memcpy(&value, bytes, sizeof(T));
                return true;
            }

            bool ReadString(Container::String& value, const char* location)
            {
                uint32_t length = 0;
                if (!ReadNumber(length, location))
                {
                    return false;
                }
                if (length > Remaining())
                {
                    return Fail(m_Error, "truncated", location);
                }
                Container::String decoded;
                decoded.reserve(length);
                const bool bValid = TextDetail::ForEachUnicodeScalar<uint8_t>(
                    {m_Bytes.data() + m_Offset, length}, [&](uint32_t scalar)
                    {
                        const auto units = TextDetail::EncodeJsonUnicodeScalar<Container::String::value_type>(scalar);
                        for (size_t index = 0; index < units.Count; ++index)
                        {
                            decoded.push_back(units.Units[index]);
                        }
                    });
                if (!bValid)
                {
                    return Fail(m_Error, "invalid_utf8", location);
                }
                m_Offset += length;
                value = std::move(decoded);
                return true;
            }

            size_t Remaining() const
            {
                return m_Bytes.size() - m_Offset;
            }

            bool CheckCount(uint32_t count, size_t minimumBytes, const char* location)
            {
                if (count > Remaining() / minimumBytes)
                {
                    return Fail(m_Error, "truncated", location);
                }
                return true;
            }

        private:
            Container::Span<const uint8_t> m_Bytes;
            Container::String& m_Error;
            size_t m_Offset = 0;
        };

        bool WriteValue(CookedWriter& writer, const VfxValue& value)
        {
            if (!writer.WriteNumber(static_cast<uint8_t>(value.Type)))
            {
                return false;
            }
            switch (value.Type)
            {
            case VfxValueType::Float:
                return writer.WriteNumber(value.Vector.x);
            case VfxValueType::UInt:
            case VfxValueType::Enum:
                return writer.WriteNumber(value.UInt);
            case VfxValueType::Bool:
                return writer.WriteNumber(static_cast<uint8_t>(value.UInt));
            case VfxValueType::Vector2:
                return writer.WriteNumber(value.Vector.x) && writer.WriteNumber(value.Vector.y);
            case VfxValueType::Vector3:
                return writer.WriteNumber(value.Vector.x) && writer.WriteNumber(value.Vector.y) &&
                    writer.WriteNumber(value.Vector.z);
            case VfxValueType::Color:
                return writer.WriteNumber(value.Vector.x) && writer.WriteNumber(value.Vector.y) &&
                    writer.WriteNumber(value.Vector.z) && writer.WriteNumber(value.Vector.w);
            case VfxValueType::Name:
                return writer.WriteString(value.Name, "Value.Name");
            default:
                return false;
            }
        }

        bool ReadValue(CookedReader& reader, const VfxObjectSchema& object, const VfxParameterSchema& schema,
            VfxValue& result, Container::String& error)
        {
            uint8_t type = 0;
            if (!reader.ReadNumber(type, object.Name))
            {
                return false;
            }
            if (type != static_cast<uint8_t>(schema.Type))
            {
                return Fail(error, "type_mismatch", object.Name, schema.Name);
            }
            VfxValue value;
            value.Type = schema.Type;
            switch (schema.Type)
            {
            case VfxValueType::Float:
                if (!reader.ReadNumber(value.Vector.x, object.Name))
                {
                    return false;
                }
                break;
            case VfxValueType::UInt:
            case VfxValueType::Enum:
                if (!reader.ReadNumber(value.UInt, object.Name))
                {
                    return false;
                }
                break;
            case VfxValueType::Bool:
            {
                uint8_t boolean = 0;
                if (!reader.ReadNumber(boolean, object.Name))
                {
                    return false;
                }
                value.UInt = boolean;
                break;
            }
            case VfxValueType::Vector2:
                if (!reader.ReadNumber(value.Vector.x, object.Name) || !reader.ReadNumber(value.Vector.y, object.Name))
                {
                    return false;
                }
                break;
            case VfxValueType::Vector3:
                if (!reader.ReadNumber(value.Vector.x, object.Name) || !reader.ReadNumber(value.Vector.y, object.Name) ||
                    !reader.ReadNumber(value.Vector.z, object.Name))
                {
                    return false;
                }
                break;
            case VfxValueType::Color:
                if (!reader.ReadNumber(value.Vector.x, object.Name) || !reader.ReadNumber(value.Vector.y, object.Name) ||
                    !reader.ReadNumber(value.Vector.z, object.Name) || !reader.ReadNumber(value.Vector.w, object.Name))
                {
                    return false;
                }
                break;
            case VfxValueType::Name:
                if (!reader.ReadString(value.Name, object.Name))
                {
                    return false;
                }
                break;
            default:
                return Fail(error, "type_mismatch", object.Name, schema.Name);
            }
            if (!ValidateVfxValue(schema, value, &error))
            {
                return false;
            }
            result = std::move(value);
            return true;
        }

        bool ReadValues(CookedReader& reader, const VfxObjectSchema& schema, const char* field,
            VariableArray<VfxValue>& values, Container::String& error)
        {
            uint32_t count = 0;
            if (!reader.ReadNumber(count, schema.Name) || !reader.CheckCount(count, 2u, schema.Name))
            {
                return false;
            }
            if (count != schema.ParameterCount)
            {
                return Fail(error, "parameter_count", schema.Name, field);
            }
            values.reserve(count);
            for (uint32_t index = 0; index < count; ++index)
            {
                VfxValue value;
                if (!ReadValue(reader, schema, schema.Parameters[index], value, error))
                {
                    return false;
                }
                values.push_back(std::move(value));
            }
            return true;
        }
    } // namespace

    bool LoadVfxEffectFromJson(const Container::String& text, VfxEffectAsset& outAsset, Container::String& outError)
    {
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document))
        {
            return Fail(outError, "json_parse", "Effect");
        }
        const JsonValue root = document.GetRoot();
        constexpr const char* keys[] = {"format", "version", "name", "flipbooks", "emitters"};
        if (!CheckKeys(root, "Effect", keys, sizeof(keys) / sizeof(keys[0]), outError))
        {
            return false;
        }
        JsonValue format;
        JsonValue version;
        JsonValue name;
        JsonValue emitters;
        if (!RequireMember(root, "Effect", "format", format, outError) ||
            !RequireMember(root, "Effect", "version", version, outError) ||
            !RequireMember(root, "Effect", "name", name, outError) ||
            !RequireMember(root, "Effect", "emitters", emitters, outError))
        {
            return false;
        }
        if (!format.IsString() || format.AsString() != "norves.vfx")
        {
            return Fail(outError, "bad_format", "Effect", "format");
        }
        if (!version.IsIntegerLiteral() || version.AsNumber() != 1.0)
        {
            return Fail(outError, "bad_version", "Effect", "version");
        }
        if (!name.IsString())
        {
            return Fail(outError, "json_parse", "Effect", "name");
        }
        if (!emitters.IsArray())
        {
            return Fail(outError, "json_parse", "Effect", "emitters");
        }

        VfxEffectAsset asset;
        CopyString(name.AsString(), asset.Name);
        const JsonValue flipbooks = root.FindMember("flipbooks");
        if (flipbooks.IsValid())
        {
            if (!flipbooks.IsArray())
            {
                return Fail(outError, "json_parse", "Effect", "flipbooks");
            }
            for (size_t index = 0; index < flipbooks.GetArraySize(); ++index)
            {
                VfxFlipbookImportSettings flipbook;
                if (!ReadJsonFlipbook(flipbooks.GetArrayElement(index), flipbook, outError))
                {
                    return false;
                }
                asset.Flipbooks.push_back(std::move(flipbook));
            }
        }
        for (size_t index = 0; index < emitters.GetArraySize(); ++index)
        {
            VfxEmitterAsset emitter;
            if (!ReadJsonEmitter(emitters.GetArrayElement(index), emitter, outError))
            {
                return false;
            }
            asset.Emitters.push_back(std::move(emitter));
        }
        if (!ValidateVfxEffect(asset, &outError))
        {
            return false;
        }
        outAsset = std::move(asset);
        outError.clear();
        return true;
    }

    bool SaveVfxEffectToJson(const VfxEffectAsset& asset, Container::String& outText, Container::String& outError)
    {
        if (!ValidateVfxEffect(asset, &outError) ||
            !ValidateJsonString(asset.Name, "Effect", "Name", outError))
        {
            return false;
        }
        for (const VfxFlipbookImportSettings& flipbook : asset.Flipbooks)
        {
            if (!ValidateJsonString(flipbook.Texture, "Flipbook", "Texture", outError))
            {
                return false;
            }
        }

        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteString("format", "norves.vfx");
        writer.WriteUInt64("version", 1u);
        writer.WriteString("name", asset.Name);
        writer.BeginArray("flipbooks");
        for (const VfxFlipbookImportSettings& flipbook : asset.Flipbooks)
        {
            writer.BeginObject();
            writer.WriteString("name", flipbook.Name);
            writer.WriteString("texture", flipbook.Texture);
            writer.WriteUInt64("columns", flipbook.Columns);
            writer.WriteUInt64("rows", flipbook.Rows);
            writer.WriteUInt64("frame_count", flipbook.FrameCount);
            writer.WriteNumber("frames_per_second", flipbook.FramesPerSecond);
            writer.WriteUInt64("start_frame", flipbook.StartFrame);
            writer.WriteBool("loop", flipbook.bLoop);
            writer.EndObject();
        }
        writer.EndArray();
        writer.BeginArray("emitters");
        for (const VfxEmitterAsset& emitter : asset.Emitters)
        {
            writer.BeginObject();
            writer.WriteString("name", emitter.Name);
            writer.BeginObject("properties");
            if (!WriteJsonParameters(writer, GetVfxEmitterSchema(), emitter.Properties, outError))
            {
                return false;
            }
            writer.EndObject();
            writer.BeginArray("modules");
            for (const VfxModule& module : emitter.Modules)
            {
                const VfxObjectSchema& schema = *FindVfxModuleSchema(module.Type);
                writer.BeginObject();
                writer.WriteString("type", schema.Name);
                if (!WriteJsonParameters(writer, schema, module.Values, outError))
                {
                    return false;
                }
                writer.EndObject();
            }
            writer.EndArray();
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
        if (!writer.IsComplete())
        {
            return Fail(outError, "json_write", "Effect");
        }
        outText = writer.ToString();
        outError.clear();
        return true;
    }

    bool CookVfxEffect(const VfxEffectAsset& asset, VariableArray<uint8_t>& outBytes, Container::String& outError)
    {
        if (!ValidateVfxEffect(asset, &outError))
        {
            return false;
        }
        CookedWriter writer(outError);
        if (!writer.WriteNumber(kVfxCookedMagic) || !writer.WriteNumber(kVfxCookedVersion) ||
            !writer.WriteNumber(uint32_t{0}) || !writer.WriteString(asset.Name, "Effect.Name") ||
            !writer.WriteCount(asset.Flipbooks.size()))
        {
            return false;
        }
        for (const VfxFlipbookImportSettings& flipbook : asset.Flipbooks)
        {
            if (!writer.WriteString(flipbook.Name, "Flipbook.Name") ||
                !writer.WriteString(flipbook.Texture, "Flipbook.Texture") ||
                !writer.WriteNumber(flipbook.Columns) || !writer.WriteNumber(flipbook.Rows) ||
                !writer.WriteNumber(flipbook.FrameCount) || !writer.WriteNumber(flipbook.FramesPerSecond) ||
                !writer.WriteNumber(flipbook.StartFrame) || !writer.WriteNumber(static_cast<uint8_t>(flipbook.bLoop)))
            {
                return false;
            }
        }
        if (!writer.WriteCount(asset.Emitters.size()))
        {
            return false;
        }
        for (const VfxEmitterAsset& emitter : asset.Emitters)
        {
            if (!writer.WriteString(emitter.Name, "Emitter.Name") || !writer.WriteCount(emitter.Properties.size()))
            {
                return false;
            }
            for (const VfxValue& value : emitter.Properties)
            {
                if (!WriteValue(writer, value))
                {
                    return false;
                }
            }
            if (!writer.WriteCount(emitter.Modules.size()))
            {
                return false;
            }
            for (const VfxModule& module : emitter.Modules)
            {
                if (!writer.WriteNumber(static_cast<uint16_t>(module.Type)) || !writer.WriteCount(module.Values.size()))
                {
                    return false;
                }
                for (const VfxValue& value : module.Values)
                {
                    if (!WriteValue(writer, value))
                    {
                        return false;
                    }
                }
            }
        }
        writer.SetNumber(8u, static_cast<uint32_t>(writer.Size() - 12u));
        outBytes = writer.TakeBytes();
        outError.clear();
        return true;
    }

    bool LoadCookedVfxEffect(Container::Span<const uint8_t> bytes, VfxEffectAsset& outAsset, Container::String& outError)
    {
        CookedReader reader(bytes, outError);
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t payloadBytes = 0;
        if (!reader.ReadNumber(magic, "Effect.Header"))
        {
            return false;
        }
        if (magic != kVfxCookedMagic)
        {
            return Fail(outError, "bad_magic", "Effect.Header");
        }
        if (!reader.ReadNumber(version, "Effect.Header"))
        {
            return false;
        }
        if (version != kVfxCookedVersion)
        {
            return Fail(outError, "bad_version", "Effect.Header");
        }
        if (!reader.ReadNumber(payloadBytes, "Effect.Header"))
        {
            return false;
        }
        if (payloadBytes > reader.Remaining())
        {
            return Fail(outError, "truncated", "Effect.Payload");
        }
        if (payloadBytes < reader.Remaining())
        {
            return Fail(outError, "trailing_bytes", "Effect.Payload");
        }

        VfxEffectAsset asset;
        uint32_t flipbookCount = 0;
        if (!reader.ReadString(asset.Name, "Effect.Name") ||
            !reader.ReadNumber(flipbookCount, "Effect.Flipbooks") ||
            !reader.CheckCount(flipbookCount, 29u, "Effect.Flipbooks"))
        {
            return false;
        }
        for (uint32_t index = 0; index < flipbookCount; ++index)
        {
            VfxFlipbookImportSettings flipbook;
            uint8_t loop = 0;
            if (!reader.ReadString(flipbook.Name, "Flipbook.Name") ||
                !reader.ReadString(flipbook.Texture, "Flipbook.Texture") ||
                !reader.ReadNumber(flipbook.Columns, "Flipbook.Columns") ||
                !reader.ReadNumber(flipbook.Rows, "Flipbook.Rows") ||
                !reader.ReadNumber(flipbook.FrameCount, "Flipbook.FrameCount") ||
                !reader.ReadNumber(flipbook.FramesPerSecond, "Flipbook.FramesPerSecond") ||
                !reader.ReadNumber(flipbook.StartFrame, "Flipbook.StartFrame") ||
                !reader.ReadNumber(loop, "Flipbook.Loop"))
            {
                return false;
            }
            if (loop > 1u)
            {
                return Fail(outError, "out_of_range", "Flipbook", "Loop");
            }
            flipbook.bLoop = loop != 0u;
            asset.Flipbooks.push_back(std::move(flipbook));
        }
        uint32_t emitterCount = 0;
        if (!reader.ReadNumber(emitterCount, "Effect.Emitters") ||
            !reader.CheckCount(emitterCount, 12u, "Effect.Emitters"))
        {
            return false;
        }
        const VfxObjectSchema& emitterSchema = GetVfxEmitterSchema();
        for (uint32_t index = 0; index < emitterCount; ++index)
        {
            VfxEmitterAsset emitter;
            if (!reader.ReadString(emitter.Name, "Emitter.Name") ||
                !ReadValues(reader, emitterSchema, "Properties", emitter.Properties, outError))
            {
                return false;
            }
            uint32_t moduleCount = 0;
            if (!reader.ReadNumber(moduleCount, "Emitter.Modules") ||
                !reader.CheckCount(moduleCount, 6u, "Emitter.Modules"))
            {
                return false;
            }
            for (uint32_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                uint16_t type = 0;
                if (!reader.ReadNumber(type, "Module.Type"))
                {
                    return false;
                }
                VfxModule module;
                module.Type = static_cast<VfxModuleType>(type);
                const VfxObjectSchema* schema = FindVfxModuleSchema(module.Type);
                if (schema == nullptr)
                {
                    return Fail(outError, "unknown_module", "Module", "Type");
                }
                if (!ReadValues(reader, *schema, "Values", module.Values, outError))
                {
                    return false;
                }
                emitter.Modules.push_back(std::move(module));
            }
            asset.Emitters.push_back(std::move(emitter));
        }
        if (reader.Remaining() != 0u)
        {
            return Fail(outError, "trailing_bytes", "Effect.Payload");
        }
        if (!ValidateVfxEffect(asset, &outError))
        {
            return false;
        }
        outAsset = std::move(asset);
        outError.clear();
        return true;
    }
} // namespace NorvesLib::Core::Particle
