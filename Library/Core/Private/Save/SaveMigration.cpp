#include "Save/SaveMigration.h"

#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include <utility>

namespace NorvesLib::Core::Save
{
    namespace
    {
        using Bytes = Container::VariableArray<uint8_t>;
        using MigrationFunction = bool (*)(const JsonDocument&, JsonWriter&, Container::String*);
        constexpr double MaxExactInteger = 9007199254740992.0;

        void SetError(Container::String* pOutError, const char* message)
        {
            if (pOutError)
            {
                *pOutError = message;
            }
        }

        bool TryGetVersion(const JsonValue& root, double& outVersion)
        {
            if (!root.IsObject())
            {
                return false;
            }
            JsonValue version;
            for (size_t index = 0; index < root.GetObjectSize(); ++index)
            {
                if (root.GetMemberName(index) == "formatVersion")
                {
                    if (version.IsValid())
                    {
                        return false;
                    }
                    version = root.GetMemberValue(index);
                }
            }
            if (!version.IsIntegerLiteral() || version.AsNumber() < 1.0)
            {
                return false;
            }
            outVersion = version.AsNumber();
            return true;
        }

        bool HasNullKey(const JsonValue& value)
        {
            if (value.IsObject())
            {
                for (size_t index = 0; index < value.GetObjectSize(); ++index)
                {
                    if (value.GetMemberName(index).find('\0') != Container::String::npos ||
                        HasNullKey(value.GetMemberValue(index)))
                    {
                        return true;
                    }
                }
            }
            else if (value.IsArray())
            {
                for (size_t index = 0; index < value.GetArraySize(); ++index)
                {
                    if (HasNullKey(value.GetArrayElement(index)))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        bool WriteValue(JsonWriter& writer, const char* key, const JsonValue& value)
        {
            if (value.IsObject())
            {
                if (key)
                {
                    writer.BeginObject(key);
                }
                else
                {
                    writer.BeginObject();
                }
                for (size_t index = 0; index < value.GetObjectSize(); ++index)
                {
                    const Container::String& name = value.GetMemberName(index);
                    if (name.find('\0') != Container::String::npos ||
                        !WriteValue(writer, name.c_str(), value.GetMemberValue(index)))
                    {
                        return false;
                    }
                }
                writer.EndObject();
            }
            else if (value.IsArray())
            {
                if (key)
                {
                    writer.BeginArray(key);
                }
                else
                {
                    writer.BeginArray();
                }
                for (size_t index = 0; index < value.GetArraySize(); ++index)
                {
                    if (!WriteValue(writer, nullptr, value.GetArrayElement(index)))
                    {
                        return false;
                    }
                }
                writer.EndArray();
            }
            else if (value.IsString())
            {
                if (key)
                {
                    writer.WriteString(key, value.AsString());
                }
                else
                {
                    writer.WriteStringElement(value.AsString());
                }
            }
            else if (value.IsNumber())
            {
                const double number = value.AsNumber();
                if (value.IsIntegerLiteral() && (number <= -MaxExactInteger || number >= MaxExactInteger))
                {
                    return false;
                }
                if (key && value.IsIntegerLiteral())
                {
                    writer.WriteInt64(key, static_cast<int64_t>(number));
                }
                else if (key)
                {
                    writer.WriteNumber(key, number);
                }
                else
                {
                    // 配列には整数専用 API がないが、この範囲の整数は double で正確に書ける。
                    writer.WriteNumberElement(number);
                }
            }
            else if (value.IsBoolean())
            {
                if (key)
                {
                    writer.WriteBool(key, value.AsBool());
                }
                else
                {
                    writer.WriteBoolElement(value.AsBool());
                }
            }
            else if (value.IsNull())
            {
                if (key)
                {
                    writer.WriteNull(key);
                }
                else
                {
                    writer.WriteNullElement();
                }
            }
            else
            {
                return false;
            }
            return !writer.HasError();
        }

        bool MigrateV1ToV2(const JsonDocument& document, JsonWriter& writer, Container::String* pOutError)
        {
            const JsonValue root = document.GetRoot();
            double version = 0.0;
            if (!TryGetVersion(root, version) || version != 1.0)
            {
                SetError(pOutError, "v1 以外のセーブデータは移行できません");
                return false;
            }
            if (root.HasMember("savedAtUnix") || root.HasMember("playSeconds"))
            {
                SetError(pOutError, "移行先の欄と既存の欄が重複しています");
                return false;
            }
            writer.BeginObject();
            bool bHasAppVersion = false;
            bool bHasPlayTime = false;
            for (size_t index = 0; index < root.GetObjectSize(); ++index)
            {
                const Container::String& name = root.GetMemberName(index);
                if (name == "formatVersion")
                {
                    writer.WriteInt64("formatVersion", 2);
                    continue;
                }
                const char* outputName = name.c_str();
                if (name == "savedAt")
                {
                    outputName = "savedAtUnix";
                }
                else if (name == "playTime")
                {
                    bHasPlayTime = true;
                    outputName = "playSeconds";
                }
                else if (name == "appVersion")
                {
                    bHasAppVersion = true;
                }
                if (!WriteValue(writer, outputName, root.GetMemberValue(index)))
                {
                    SetError(pOutError, "セーブデータの JSON を書き出せませんでした");
                    return false;
                }
            }
            if (!bHasAppVersion)
            {
                writer.WriteString("appVersion", "");
            }
            if (!bHasPlayTime)
            {
                writer.WriteInt64("playSeconds", 0);
            }
            writer.EndObject();
            return writer.IsComplete();
        }

        struct MigrationStep
        {
            uint32_t FromVersion;
            MigrationFunction Function;
        };
        constexpr MigrationStep MigrationSteps[] = {{1, &MigrateV1ToV2}};
    } // namespace

    SaveMigrationResult MigrateSavePayload(Container::Span<const uint8_t> payloadUtf8,
                                           Container::VariableArray<uint8_t>& outPayloadUtf8,
                                           Container::String* pOutError)
    {
        // 出力が入力を所有する場合にも先に退避する。
        Bytes current;
        current.reserve(payloadUtf8.size());
        for (size_t index = 0; index < payloadUtf8.size(); ++index)
        {
            current.push_back(payloadUtf8[index]);
        }
        outPayloadUtf8.clear();
        if (pOutError)
        {
            pOutError->clear();
        }

        JsonDocument document;
        double parsedVersion = 0.0;
        if (!JsonDocument::TryParseUtf8(current, document) || !TryGetVersion(document.GetRoot(), parsedVersion))
        {
            SetError(pOutError, "セーブデータの JSON または版が不正です");
            return SaveMigrationResult::Invalid;
        }
        if (HasNullKey(document.GetRoot()))
        {
            SetError(pOutError, "セーブデータの欄名に NUL が含まれています");
            return SaveMigrationResult::Invalid;
        }
        if (parsedVersion > static_cast<double>(SavePayloadFormatVersion))
        {
            SetError(pOutError, "新しい版のセーブデータは読み込めません");
            return SaveMigrationResult::RejectedNewerVersion;
        }
        uint32_t version = static_cast<uint32_t>(parsedVersion);
        if (version == SavePayloadFormatVersion)
        {
            outPayloadUtf8 = std::move(current);
            return SaveMigrationResult::UpToDate;
        }

        while (version < SavePayloadFormatVersion)
        {
            const MigrationStep* pStep = nullptr;
            for (const MigrationStep& step : MigrationSteps)
            {
                if (step.FromVersion == version)
                {
                    pStep = &step;
                    break;
                }
            }
            if (!pStep)
            {
                SetError(pOutError, "セーブデータの移行手順がありません");
                return SaveMigrationResult::Invalid;
            }
            JsonWriter writer;
            if (!pStep->Function(document, writer, pOutError) || !writer.IsComplete())
            {
                if (pOutError && pOutError->empty())
                {
                    SetError(pOutError, "セーブデータの JSON を書き出せませんでした");
                }
                return SaveMigrationResult::Invalid;
            }
            const Container::String text = writer.ToString();
            Bytes next;
            next.reserve(text.size());
            for (size_t index = 0; index < text.size(); ++index)
            {
                next.push_back(static_cast<uint8_t>(text[index]));
            }
            if (!JsonDocument::TryParseUtf8(next, document) || !TryGetVersion(document.GetRoot(), parsedVersion) ||
                parsedVersion != static_cast<double>(version + 1))
            {
                SetError(pOutError, "移行後のセーブデータの JSON または版が不正です");
                return SaveMigrationResult::Invalid;
            }
            ++version;
            current = std::move(next);
        }
        outPayloadUtf8 = std::move(current);
        return SaveMigrationResult::Migrated;
    }
} // namespace NorvesLib::Core::Save
