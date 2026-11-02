#include "BridgeAssetPropertyEditor.h"

#include "FileStream/FileStream.h"
#include "Particle/VfxEffectSerialization.h"
#include "Particle/VfxSchema.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

#include <charconv>
#include <cmath>
#include <cwchar>
#include <limits>
#include <utility>
#include <Windows.h>

namespace Game::Bridge
{
    namespace Core = NorvesLib::Core;
    namespace Particle = NorvesLib::Core::Particle;
    namespace FileStream = NorvesLib::FileStream;

    namespace
    {
        BridgeAssetPropertyResult Success()
        {
            BridgeAssetPropertyResult result;
            result.Success = true;
            return result;
        }

        BridgeAssetPropertyResult Failure(const char* code, const char8_t* message)
        {
            BridgeAssetPropertyResult result;
            result.Code = code;
            result.Message = reinterpret_cast<const char*>(message);
            return result;
        }

        BridgeAssetPropertyResult InvalidDocument()
        {
            return Failure("invalid_document", u8"文書が開かれていません。");
        }

        BridgeAssetPropertyResult InvalidPath()
        {
            return Failure("invalid_path", u8"資産ルート内の相対パスを指定してください。");
        }

        BridgeAssetPropertyResult LoadFailed()
        {
            return Failure("load_failed", u8"資産ファイルを読み込めません。");
        }

        BridgeAssetPropertyResult SaveFailed()
        {
            return Failure("save_failed", u8"資産ファイルを保存できません。");
        }

        BridgeAssetPropertyResult UnknownProperty()
        {
            return Failure("unknown_property", u8"プロパティが見つかりません。");
        }

        BridgeAssetPropertyResult TypeMismatch()
        {
            return Failure("type_mismatch", u8"値の JSON 形式または型が一致しません。");
        }

        BridgeAssetPropertyResult OutOfRange()
        {
            return Failure("out_of_range", u8"値が許容範囲または資産の制約に合いません。");
        }

        struct NativeHandle
        {
            HANDLE Value = INVALID_HANDLE_VALUE;

            explicit NativeHandle(HANDLE handle = INVALID_HANDLE_VALUE) : Value(handle) {}
            NativeHandle(const NativeHandle&) = delete;
            NativeHandle& operator=(const NativeHandle&) = delete;
            ~NativeHandle() { Close(); }

            bool Close()
            {
                if (Value == INVALID_HANDLE_VALUE)
                {
                    return true;
                }
                const HANDLE handle = Value;
                Value = INVALID_HANDLE_VALUE;
                return CloseHandle(handle) != 0;
            }
        };

        struct TemporaryFile
        {
            Container::WideString Path;
            bool bOwned = false;

            ~TemporaryFile()
            {
                if (bOwned)
                {
                    DeleteFileW(Path.c_str());
                }
            }
        };

        bool Utf8ToWide(const Container::String& text, Container::WideString& out)
        {
            out.clear();
            if (text.empty() || text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            {
                return false;
            }
            for (size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\0')
                {
                    return false;
                }
            }
            const int length = static_cast<int>(text.size());
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
            if (count <= 0)
            {
                return false;
            }
            Container::VariableArray<wchar_t> buffer(static_cast<size_t>(count));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, buffer.data(), count) != count)
            {
                return false;
            }
            out.append(buffer.data(), buffer.size());
            return true;
        }

        bool ResolveRoot(const Container::String& root, Container::WideString& out)
        {
            Container::WideString wide;
            if (!Utf8ToWide(root, wide))
            {
                return false;
            }
            Container::VariableArray<wchar_t> buffer(32768);
            const DWORD count = GetFullPathNameW(wide.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
            if (count == 0 || count >= buffer.size())
            {
                return false;
            }
            out.append(buffer.data(), count);
            return true;
        }

        bool IsRelativeAssetPath(const Container::String& path)
        {
            if (path.empty() || path[0] == '/' || path[0] == '\\')
            {
                return false;
            }
            size_t segmentStart = 0;
            for (size_t index = 0; index <= path.size(); ++index)
            {
                if (index < path.size())
                {
                    const unsigned char c = static_cast<unsigned char>(path[index]);
                    if (c < 32 || c == ':' || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' || c == '*')
                    {
                        return false;
                    }
                    if (c != '/' && c != '\\')
                    {
                        continue;
                    }
                }
                const size_t length = index - segmentStart;
                if (length == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.')
                {
                    return false;
                }
                if (length > 0 && !(length == 1 && path[segmentStart] == '.') &&
                    (path[index - 1] == '.' || path[index - 1] == ' '))
                {
                    return false;
                }
                segmentStart = index + 1;
            }
            return true;
        }

        bool FinalPath(HANDLE handle, Container::WideString& out)
        {
            Container::VariableArray<wchar_t> buffer(32768);
            const DWORD count = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (count == 0 || count >= buffer.size())
            {
                return false;
            }
            out.clear();
            out.append(buffer.data(), count);
            return true;
        }

        bool IsWithinRoot(const Container::WideString& root, const Container::WideString& path)
        {
            if (root.empty() || path.size() <= root.size() || root.size() > static_cast<size_t>(INT_MAX) ||
                CompareStringOrdinal(root.data(), static_cast<int>(root.size()), path.data(),
                    static_cast<int>(root.size()), TRUE) != CSTR_EQUAL)
            {
                return false;
            }
            return root[root.size() - 1] == L'\\' || path[root.size()] == L'\\';
        }

        bool OpenRoot(const Container::WideString& root, Container::WideString& outFinal)
        {
            NativeHandle directory(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            BY_HANDLE_FILE_INFORMATION info{};
            return directory.Value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(directory.Value, &info) &&
                (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && FinalPath(directory.Value, outFinal);
        }

        bool WideToFileStreamPath(const Container::WideString& path, Container::String& out)
        {
            if (path.empty() || path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            {
                return false;
            }
            const UINT codePage = GetACP();
            const DWORD flags = codePage == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
            BOOL bUsedDefault = FALSE;
            BOOL* pUsedDefault = codePage == CP_UTF8 ? nullptr : &bUsedDefault;
            const int length = static_cast<int>(path.size());
            const int count = WideCharToMultiByte(codePage, flags, path.data(), length, nullptr, 0, nullptr, pUsedDefault);
            if (count <= 0 || bUsedDefault)
            {
                return false;
            }
            Container::VariableArray<char> bytes(static_cast<size_t>(count));
            if (WideCharToMultiByte(codePage, flags, path.data(), length, bytes.data(), count, nullptr, pUsedDefault) != count || bUsedDefault)
            {
                return false;
            }
            out.append(bytes.data(), bytes.size());
            return true;
        }

        bool ReadText(HANDLE handle, const Container::WideString& path, Container::String& out)
        {
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
                static_cast<uint64_t>(size.QuadPart) > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
            {
                return false;
            }
            Container::VariableArray<char> bytes(static_cast<size_t>(size.QuadPart));
            bool bRead = false;
            Container::String streamPath;
            if (WideToFileStreamPath(path, streamPath))
            {
                auto stream = FileStream::FileStream::CreateUnique(streamPath, FileStream::FileMode::Read,
                    FileStream::FileAccess::Read, FileStream::FileShare::Read);
                if (stream && stream->GetSize() == size.QuadPart)
                {
                    bRead = bytes.empty() || stream->Read(bytes.data(), bytes.size()) == bytes.size();
                }
            }
            if (!bRead)
            {
                size_t offset = 0;
                while (offset < bytes.size())
                {
                    const size_t remaining = bytes.size() - offset;
                    const DWORD request = static_cast<DWORD>(remaining > 0x40000000u ? 0x40000000u : remaining);
                    DWORD count = 0;
                    if (!ReadFile(handle, bytes.data() + offset, request, &count, nullptr) || count == 0)
                    {
                        return false;
                    }
                    offset += count;
                }
            }
            size_t start = 0;
            if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
                static_cast<unsigned char>(bytes[1]) == 0xbb && static_cast<unsigned char>(bytes[2]) == 0xbf)
            {
                start = 3;
            }
            for (size_t index = start; index < bytes.size(); ++index)
            {
                if (bytes[index] == '\0')
                {
                    return false;
                }
            }
            if (start < bytes.size())
            {
                out.append(bytes.data() + start, bytes.size() - start);
            }
            return true;
        }

        bool WriteAtomically(const Container::WideString& destination, const Container::String& text, uint64_t& sequence)
        {
            const size_t separator = destination.FindLast(L'\\');
            if (separator == Container::WideString::npos)
            {
                return false;
            }
            TemporaryFile temporary;
            NativeHandle file;
            for (uint32_t attempt = 0; attempt < 64; ++attempt)
            {
                wchar_t suffix[100]{};
                const int count = std::swprintf(suffix, sizeof(suffix) / sizeof(suffix[0]), L".norves-%lu-%llu-%llu.tmp",
                    static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long long>(GetTickCount64()),
                    static_cast<unsigned long long>(++sequence));
                if (count <= 0 || static_cast<size_t>(count) >= sizeof(suffix) / sizeof(suffix[0]))
                {
                    return false;
                }
                Container::WideString candidate = destination.substr(0, separator + 1);
                candidate.append(suffix, static_cast<size_t>(count));
                const HANDLE handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle != INVALID_HANDLE_VALUE)
                {
                    temporary.Path = std::move(candidate);
                    temporary.bOwned = true;
                    file.Value = handle;
                    break;
                }
                const DWORD error = GetLastError();
                if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
                {
                    return false;
                }
            }
            if (file.Value == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            size_t offset = 0;
            while (offset < text.size())
            {
                const size_t remaining = text.size() - offset;
                const DWORD request = static_cast<DWORD>(remaining > 0x40000000u ? 0x40000000u : remaining);
                DWORD count = 0;
                if (!WriteFile(file.Value, text.data() + offset, request, &count, nullptr) || count == 0)
                {
                    return false;
                }
                offset += count;
            }
            if (!FlushFileBuffers(file.Value) || !file.Close())
            {
                return false;
            }
            if (!MoveFileExW(temporary.Path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                return false;
            }
            temporary.bOwned = false;
            return true;
        }

        bool HasExtension(const Container::String& path, const char* extension)
        {
            size_t length = 0;
            while (extension[length] != '\0')
            {
                ++length;
            }
            if (path.size() < length)
            {
                return false;
            }
            for (size_t index = 0; index < length; ++index)
            {
                char c = path[path.size() - length + index];
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if (c != extension[index])
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    class IBridgeAssetPropertyData
    {
    public:
        virtual ~IBridgeAssetPropertyData() = default;
    };

    class IBridgeAssetPropertyKind
    {
    public:
        virtual ~IBridgeAssetPropertyKind() = default;
        virtual const char* Extension() const = 0;
        virtual BridgeAssetPropertyResult Load(const Container::String& text,
            Container::TUniquePtr<IBridgeAssetPropertyData>& out) const = 0;
        virtual BridgeAssetPropertyResult List(const IBridgeAssetPropertyData& data,
            Container::VariableArray<BridgeAssetProperty>& out) const = 0;
        virtual BridgeAssetPropertyResult Set(IBridgeAssetPropertyData& data, const Container::String& path,
            const Container::String& text) const = 0;
        virtual BridgeAssetPropertyResult Serialize(const IBridgeAssetPropertyData& data, Container::String& out) const = 0;
    };

    struct BridgeAssetPropertyDocument
    {
        BridgeAssetDocumentId Id = 0;
        Container::WideString Path;
        const IBridgeAssetPropertyKind* Kind = nullptr;
        Container::TUniquePtr<IBridgeAssetPropertyData> Data;
    };

    namespace
    {
        struct VfxPropertyData final : IBridgeAssetPropertyData
        {
            Particle::VfxEffectAsset Asset;
        };

        const char* TypeName(Particle::VfxValueType type)
        {
            switch (type)
            {
            case Particle::VfxValueType::Float: return "Float";
            case Particle::VfxValueType::UInt: return "UInt";
            case Particle::VfxValueType::Bool: return "Bool";
            case Particle::VfxValueType::Vector2: return "Vector2";
            case Particle::VfxValueType::Vector3: return "Vector3";
            case Particle::VfxValueType::Color: return "Color";
            case Particle::VfxValueType::Enum: return "Enum";
            case Particle::VfxValueType::Name: return "Name";
            default: return "";
            }
        }

        bool ValueToJson(const Particle::VfxParameterSchema& schema, const Particle::VfxValue& value,
            Container::String& out)
        {
            Core::JsonWriter writer;
            writer.BeginObject();
            switch (schema.Type)
            {
            case Particle::VfxValueType::Float:
                writer.WriteNumber("v", value.Vector.x);
                break;
            case Particle::VfxValueType::UInt:
                writer.WriteUInt64("v", value.UInt);
                break;
            case Particle::VfxValueType::Bool:
                writer.WriteBool("v", value.UInt != 0);
                break;
            case Particle::VfxValueType::Vector2:
            case Particle::VfxValueType::Vector3:
            case Particle::VfxValueType::Color:
                writer.BeginArray("v");
                writer.WriteNumberElement(value.Vector.x);
                writer.WriteNumberElement(value.Vector.y);
                if (schema.Type != Particle::VfxValueType::Vector2)
                {
                    writer.WriteNumberElement(value.Vector.z);
                }
                if (schema.Type == Particle::VfxValueType::Color)
                {
                    writer.WriteNumberElement(value.Vector.w);
                }
                writer.EndArray();
                break;
            case Particle::VfxValueType::Enum:
                if (value.UInt >= schema.EnumCount)
                {
                    return false;
                }
                writer.WriteString("v", schema.EnumNames[value.UInt]);
                break;
            case Particle::VfxValueType::Name:
                writer.WriteString("v", value.Name);
                break;
            default:
                return false;
            }
            writer.EndObject();
            if (!writer.IsComplete())
            {
                return false;
            }
            const Container::String object = writer.ToString();
            if (object.size() < 6)
            {
                return false;
            }
            out = object.substr(5, object.size() - 6);
            return true;
        }

        bool AppendProperty(const Container::String& path, const Particle::VfxParameterSchema& schema,
            const Particle::VfxValue& value, Container::VariableArray<BridgeAssetProperty>& out)
        {
            BridgeAssetProperty property;
            property.Path = path;
            property.TypeName = TypeName(schema.Type);
            property.Unit = schema.Unit;
            property.Min = schema.Min;
            property.Max = schema.Max;
            if (!ValueToJson(schema, value, property.ValueJson))
            {
                return false;
            }
            out.push_back(std::move(property));
            return true;
        }

        void SkipJsonWhitespace(const Container::String& text, size_t& position)
        {
            while (position < text.size() && (text[position] == ' ' || text[position] == '\t' ||
                text[position] == '\r' || text[position] == '\n'))
            {
                ++position;
            }
        }

        bool ConsumeJsonNumber(const Container::String& text, size_t& position, bool& bOverflow, bool& bTiny)
        {
            if (text.size() > static_cast<size_t>((std::numeric_limits<int64_t>::max)() / 4))
            {
                return false;
            }
            const size_t start = position;
            if (position < text.size() && text[position] == '-')
            {
                ++position;
            }
            int64_t digits = 0;
            int64_t digitsBeforePoint = 0;
            int64_t firstNonZero = -1;
            const auto consumeDigit = [&](char c, bool bBeforePoint)
            {
                if (c != '0' && firstNonZero < 0)
                {
                    firstNonZero = digits;
                }
                ++digits;
                if (bBeforePoint)
                {
                    ++digitsBeforePoint;
                }
            };
            if (position == text.size())
            {
                return false;
            }
            if (text[position] == '0')
            {
                consumeDigit(text[position++], true);
            }
            else if (text[position] >= '1' && text[position] <= '9')
            {
                while (position < text.size() && text[position] >= '0' && text[position] <= '9')
                {
                    consumeDigit(text[position++], true);
                }
            }
            else
            {
                return false;
            }
            if (position < text.size() && text[position] == '.')
            {
                ++position;
                const size_t fractionStart = position;
                while (position < text.size() && text[position] >= '0' && text[position] <= '9')
                {
                    consumeDigit(text[position++], false);
                }
                if (position == fractionStart)
                {
                    return false;
                }
            }
            int64_t exponent = 0;
            if (position < text.size() && (text[position] == 'e' || text[position] == 'E'))
            {
                ++position;
                bool bNegative = false;
                if (position < text.size() && (text[position] == '+' || text[position] == '-'))
                {
                    bNegative = text[position] == '-';
                    ++position;
                }
                const size_t exponentStart = position;
                constexpr int64_t limit = (std::numeric_limits<int64_t>::max)() / 4;
                while (position < text.size() && text[position] >= '0' && text[position] <= '9')
                {
                    const int64_t digit = text[position++] - '0';
                    exponent = exponent > (limit - digit) / 10 ? limit : exponent * 10 + digit;
                }
                if (position == exponentStart)
                {
                    return false;
                }
                if (bNegative)
                {
                    exponent = -exponent;
                }
            }
            double value = 0.0;
            const char* begin = text.data() + start;
            const char* end = text.data() + position;
            const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
            if (parsed.ptr != end)
            {
                return false;
            }
            if (parsed.ec == std::errc::result_out_of_range)
            {
                bOverflow = true;
                bTiny = firstNonZero >= 0 && digitsBeforePoint - firstNonZero - 1 + exponent < 0;
                return true;
            }
            return parsed.ec == std::errc{};
        }

        // JSON パーサーが double の範囲外で失敗したときも、値の型と範囲を分ける。
        bool HasNumericOverflow(const Particle::VfxParameterSchema& schema, const Container::String& text)
        {
            size_t count = 1;
            const bool bArray = schema.Type == Particle::VfxValueType::Vector2 ||
                schema.Type == Particle::VfxValueType::Vector3 || schema.Type == Particle::VfxValueType::Color;
            if (bArray)
            {
                count = schema.Type == Particle::VfxValueType::Vector2 ? 2u :
                    schema.Type == Particle::VfxValueType::Vector3 ? 3u : 4u;
            }
            else if (schema.Type != Particle::VfxValueType::Float && schema.Type != Particle::VfxValueType::UInt)
            {
                return false;
            }
            size_t position = 0;
            SkipJsonWhitespace(text, position);
            if (bArray)
            {
                if (position == text.size() || text[position++] != '[')
                {
                    return false;
                }
            }
            bool bOverflow = false;
            for (size_t index = 0; index < count; ++index)
            {
                SkipJsonWhitespace(text, position);
                bool bComponentOverflow = false;
                bool bTiny = false;
                if (!ConsumeJsonNumber(text, position, bComponentOverflow, bTiny) ||
                    (schema.Type == Particle::VfxValueType::UInt && bComponentOverflow && bTiny))
                {
                    return false;
                }
                bOverflow = bOverflow || bComponentOverflow;
                SkipJsonWhitespace(text, position);
                if (bArray && (position == text.size() || text[position++] != (index + 1 == count ? ']' : ',')))
                {
                    return false;
                }
            }
            SkipJsonWhitespace(text, position);
            return bOverflow && position == text.size();
        }

        BridgeAssetPropertyResult ParseValue(const Particle::VfxParameterSchema& schema, const Container::String& text,
            Particle::VfxValue& out)
        {
            Core::JsonDocument document;
            if (!Core::JsonDocument::TryParse(text, document))
            {
                return HasNumericOverflow(schema, text) ? OutOfRange() : TypeMismatch();
            }
            const Core::JsonValue json = document.GetRoot();
            Particle::VfxValue value;
            value.Type = schema.Type;
            switch (schema.Type)
            {
            case Particle::VfxValueType::Float:
                if (!json.IsNumber())
                {
                    return TypeMismatch();
                }
                if (!std::isfinite(json.AsNumber()) || std::abs(json.AsNumber()) > (std::numeric_limits<float>::max)())
                {
                    return OutOfRange();
                }
                value.Vector.x = static_cast<float>(json.AsNumber());
                break;
            case Particle::VfxValueType::UInt:
                if (!json.IsNumber() || std::trunc(json.AsNumber()) != json.AsNumber())
                {
                    return TypeMismatch();
                }
                if (!std::isfinite(json.AsNumber()) || json.AsNumber() < 0.0 || json.AsNumber() > static_cast<double>(UINT32_MAX))
                {
                    return OutOfRange();
                }
                value.UInt = static_cast<uint32_t>(json.AsNumber());
                break;
            case Particle::VfxValueType::Bool:
                if (!json.IsBoolean())
                {
                    return TypeMismatch();
                }
                value.UInt = json.AsBool() ? 1u : 0u;
                break;
            case Particle::VfxValueType::Vector2:
            case Particle::VfxValueType::Vector3:
            case Particle::VfxValueType::Color:
            {
                const size_t count = schema.Type == Particle::VfxValueType::Vector2 ? 2u :
                    schema.Type == Particle::VfxValueType::Vector3 ? 3u : 4u;
                if (!json.IsArray() || json.GetArraySize() != count)
                {
                    return TypeMismatch();
                }
                float components[4]{};
                for (size_t index = 0; index < count; ++index)
                {
                    if (!json.GetArrayElement(index).IsNumber())
                    {
                        return TypeMismatch();
                    }
                }
                for (size_t index = 0; index < count; ++index)
                {
                    const Core::JsonValue component = json.GetArrayElement(index);
                    if (!std::isfinite(component.AsNumber()) || std::abs(component.AsNumber()) > (std::numeric_limits<float>::max)())
                    {
                        return OutOfRange();
                    }
                    components[index] = static_cast<float>(component.AsNumber());
                }
                value.Vector = NorvesLib::Math::Vector4(components[0], components[1], components[2], components[3]);
                break;
            }
            case Particle::VfxValueType::Enum:
            {
                if (!json.IsString())
                {
                    return TypeMismatch();
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
                    return OutOfRange();
                }
                break;
            }
            case Particle::VfxValueType::Name:
                if (!json.IsString())
                {
                    return TypeMismatch();
                }
                for (size_t index = 0; index < json.AsString().size(); ++index)
                {
                    if (json.AsString()[index] == '\0')
                    {
                        return OutOfRange();
                    }
                }
                value.Name = json.AsString();
                break;
            default:
                return TypeMismatch();
            }
            Container::String error;
            if (!Particle::ValidateVfxValue(schema, value, &error))
            {
                return OutOfRange();
            }
            out = std::move(value);
            return Success();
        }

        bool ParseIndex(const Container::String& text, size_t& out)
        {
            if (text.empty())
            {
                return false;
            }
            out = 0;
            for (size_t index = 0; index < text.size(); ++index)
            {
                const char c = text[index];
                if (c < '0' || c > '9')
                {
                    return false;
                }
                const size_t digit = static_cast<size_t>(c - '0');
                if (out > ((std::numeric_limits<size_t>::max)() - digit) / 10)
                {
                    return false;
                }
                out = out * 10 + digit;
            }
            return true;
        }

        bool HasEmbeddedNull(const Container::String& text)
        {
            for (size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\0')
                {
                    return true;
                }
            }
            return false;
        }

        class VfxPropertyKind final : public IBridgeAssetPropertyKind
        {
        public:
            const char* Extension() const override { return ".vfx.json"; }

            BridgeAssetPropertyResult Load(const Container::String& text,
                Container::TUniquePtr<IBridgeAssetPropertyData>& out) const override
            {
                auto data = Container::MakeUnique<VfxPropertyData>();
                Container::String error;
                if (!Particle::LoadVfxEffectFromJson(text, data->Asset, error))
                {
                    return LoadFailed();
                }
                // Core の文字列は埋込 NUL より後をコピー時に保てないため、上書きを防ぐ。
                if (HasEmbeddedNull(data->Asset.Name))
                {
                    return LoadFailed();
                }
                for (const auto& flipbook : data->Asset.Flipbooks)
                {
                    if (HasEmbeddedNull(flipbook.Texture))
                    {
                        return LoadFailed();
                    }
                }
                out = std::move(data);
                return Success();
            }

            BridgeAssetPropertyResult List(const IBridgeAssetPropertyData& input,
                Container::VariableArray<BridgeAssetProperty>& out) const override
            {
                const auto& asset = static_cast<const VfxPropertyData&>(input).Asset;
                const Particle::VfxObjectSchema& emitterSchema = Particle::GetVfxEmitterSchema();
                for (const auto& emitter : asset.Emitters)
                {
                    Container::String base = "emitters/";
                    base += emitter.Name;
                    base += "/";
                    for (uint32_t index = 0; index < emitterSchema.ParameterCount; ++index)
                    {
                        if (!AppendProperty(base + emitterSchema.Parameters[index].Name, emitterSchema.Parameters[index],
                            emitter.Properties[index], out))
                        {
                            out.clear();
                            return LoadFailed();
                        }
                    }
                    for (size_t index = 0; index < emitter.Modules.size(); ++index)
                    {
                        const auto& module = emitter.Modules[index];
                        const auto* schema = Particle::FindVfxModuleSchema(module.Type);
                        if (schema == nullptr)
                        {
                            out.clear();
                            return LoadFailed();
                        }
                        char number[32]{};
                        const auto converted = std::to_chars(number, number + sizeof(number), index);
                        if (converted.ec != std::errc{})
                        {
                            out.clear();
                            return LoadFailed();
                        }
                        Container::String moduleBase = base + "modules/";
                        moduleBase.append(number, static_cast<size_t>(converted.ptr - number));
                        moduleBase += "/";
                        for (uint32_t parameter = 0; parameter < schema->ParameterCount; ++parameter)
                        {
                            if (!AppendProperty(moduleBase + schema->Parameters[parameter].Name,
                                schema->Parameters[parameter], module.Values[parameter], out))
                            {
                                out.clear();
                                return LoadFailed();
                            }
                        }
                    }
                }
                return Success();
            }

            BridgeAssetPropertyResult Set(IBridgeAssetPropertyData& input, const Container::String& path,
                const Container::String& text) const override
            {
                if (HasEmbeddedNull(path))
                {
                    return UnknownProperty();
                }
                Container::VariableArray<Container::String> parts;
                size_t start = 0;
                while (start <= path.size())
                {
                    const size_t end = path.find('/', start);
                    const size_t length = (end == Container::String::npos ? path.size() : end) - start;
                    if (length == 0 || parts.size() == 5)
                    {
                        return UnknownProperty();
                    }
                    parts.push_back(path.substr(start, length));
                    if (end == Container::String::npos)
                    {
                        break;
                    }
                    start = end + 1;
                }
                if ((parts.size() != 3 && parts.size() != 5) || parts[0] != "emitters" ||
                    (parts.size() == 5 && parts[2] != "modules"))
                {
                    return UnknownProperty();
                }
                auto& asset = static_cast<VfxPropertyData&>(input).Asset;
                size_t emitterIndex = 0;
                while (emitterIndex < asset.Emitters.size() && asset.Emitters[emitterIndex].Name != parts[1])
                {
                    ++emitterIndex;
                }
                if (emitterIndex == asset.Emitters.size())
                {
                    return UnknownProperty();
                }
                size_t moduleIndex = 0;
                const Particle::VfxObjectSchema* schema = &Particle::GetVfxEmitterSchema();
                const Container::String* parameter = &parts[2];
                if (parts.size() == 5)
                {
                    if (!ParseIndex(parts[3], moduleIndex) || moduleIndex >= asset.Emitters[emitterIndex].Modules.size())
                    {
                        return UnknownProperty();
                    }
                    schema = Particle::FindVfxModuleSchema(asset.Emitters[emitterIndex].Modules[moduleIndex].Type);
                    parameter = &parts[4];
                }
                if (schema == nullptr)
                {
                    return UnknownProperty();
                }
                const int32_t parameterIndex = Particle::FindVfxParameterIndex(*schema, parameter->c_str());
                if (parameterIndex < 0)
                {
                    return UnknownProperty();
                }
                Particle::VfxValue value;
                BridgeAssetPropertyResult result = ParseValue(schema->Parameters[static_cast<uint32_t>(parameterIndex)], text, value);
                if (!result.Success)
                {
                    return result;
                }
                Particle::VfxEffectAsset candidate = asset;
                Container::String error;
                bool bSet = false;
                if (parts.size() == 3)
                {
                    bSet = Particle::SetVfxEmitterProperty(candidate.Emitters[emitterIndex], parameter->c_str(), value, &error);
                }
                else
                {
                    bSet = Particle::SetVfxModuleParameter(candidate.Emitters[emitterIndex].Modules[moduleIndex],
                        parameter->c_str(), value, &error);
                }
                if (!bSet || !Particle::ValidateVfxEffect(candidate, &error))
                {
                    return OutOfRange();
                }
                asset = std::move(candidate);
                return Success();
            }

            BridgeAssetPropertyResult Serialize(const IBridgeAssetPropertyData& input, Container::String& out) const override
            {
                Container::String error;
                return Particle::SaveVfxEffectToJson(static_cast<const VfxPropertyData&>(input).Asset, out, error) ?
                    Success() : SaveFailed();
            }
        };
    } // namespace

    BridgeAssetPropertyEditor::BridgeAssetPropertyEditor(const Container::String& assetRoot)
    {
        ResolveRoot(assetRoot, m_RootPath);
        m_Kinds.push_back(Container::MakeUnique<VfxPropertyKind>());
    }

    BridgeAssetPropertyEditor::~BridgeAssetPropertyEditor() = default;

    BridgeAssetPropertyDocument* BridgeAssetPropertyEditor::FindDocument(BridgeAssetDocumentId document) const
    {
        for (const auto& candidate : m_Documents)
        {
            if (candidate->Id == document)
            {
                return candidate.get();
            }
        }
        return nullptr;
    }

    BridgeAssetPropertyResult BridgeAssetPropertyEditor::Open(const Container::String& relativePath,
        BridgeAssetDocumentId& outDocument)
    {
        outDocument = 0;
        Container::WideString relative;
        if (m_RootPath.empty() || !IsRelativeAssetPath(relativePath) || !Utf8ToWide(relativePath, relative))
        {
            return InvalidPath();
        }
        const IBridgeAssetPropertyKind* kind = nullptr;
        for (const auto& candidate : m_Kinds)
        {
            if (HasExtension(relativePath, candidate->Extension()))
            {
                kind = candidate.get();
                break;
            }
        }
        if (kind == nullptr)
        {
            return Failure("unsupported_asset_kind", u8"この種類の資産には対応していません。");
        }
        Container::WideString root;
        if (!OpenRoot(m_RootPath, root))
        {
            return LoadFailed();
        }
        Container::WideString path = m_RootPath;
        if (path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
        {
            path += L'\\';
        }
        path += relative;
        NativeHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        Container::WideString finalPath;
        if (file.Value == INVALID_HANDLE_VALUE || GetFileType(file.Value) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(file.Value, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            !FinalPath(file.Value, finalPath))
        {
            return LoadFailed();
        }
        if (!IsWithinRoot(root, finalPath))
        {
            return InvalidPath();
        }
        Container::String text;
        if (!ReadText(file.Value, finalPath, text))
        {
            return LoadFailed();
        }
        auto document = Container::MakeUnique<BridgeAssetPropertyDocument>();
        BridgeAssetPropertyResult result = kind->Load(text, document->Data);
        if (!result.Success)
        {
            return result;
        }
        if (m_NextDocumentId == 0)
        {
            return Failure("open_failed", u8"これ以上文書を開けません。");
        }
        document->Id = m_NextDocumentId++;
        document->Path = std::move(finalPath);
        document->Kind = kind;
        outDocument = document->Id;
        m_Documents.push_back(std::move(document));
        return Success();
    }

    BridgeAssetPropertyResult BridgeAssetPropertyEditor::ListProperties(BridgeAssetDocumentId document,
        Container::VariableArray<BridgeAssetProperty>& outProperties) const
    {
        outProperties.clear();
        const auto* found = FindDocument(document);
        return found != nullptr ? found->Kind->List(*found->Data, outProperties) : InvalidDocument();
    }

    BridgeAssetPropertyResult BridgeAssetPropertyEditor::SetProperty(BridgeAssetDocumentId document,
        const Container::String& path, const Container::String& valueJsonText)
    {
        auto* found = FindDocument(document);
        return found != nullptr ? found->Kind->Set(*found->Data, path, valueJsonText) : InvalidDocument();
    }

    BridgeAssetPropertyResult BridgeAssetPropertyEditor::Save(BridgeAssetDocumentId document)
    {
        const auto* found = FindDocument(document);
        if (found == nullptr)
        {
            return InvalidDocument();
        }
        Container::String text;
        BridgeAssetPropertyResult result = found->Kind->Serialize(*found->Data, text);
        if (!result.Success)
        {
            return result;
        }
        Container::WideString root;
        if (!OpenRoot(m_RootPath, root))
        {
            return SaveFailed();
        }
        {
            NativeHandle file(CreateFileW(found->Path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr));
            Container::WideString finalPath;
            if (file.Value == INVALID_HANDLE_VALUE || !FinalPath(file.Value, finalPath) || !IsWithinRoot(root, finalPath))
            {
                return SaveFailed();
            }
        }
        return WriteAtomically(found->Path, text, m_SaveSequence) ? Success() : SaveFailed();
    }

    BridgeAssetPropertyResult BridgeAssetPropertyEditor::Close(BridgeAssetDocumentId document)
    {
        for (auto iterator = m_Documents.begin(); iterator != m_Documents.end(); ++iterator)
        {
            if ((*iterator)->Id == document)
            {
                m_Documents.erase(iterator);
                return Success();
            }
        }
        return InvalidDocument();
    }
} // namespace Game::Bridge
