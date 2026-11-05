#include "Save/SettingsStore.h"

#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include <cmath>

namespace NorvesLib::Core::Save
{
    namespace
    {
        constexpr double MaxExactInteger = 9007199254740992.0;

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

        float NormalizeVolume(double value)
        {
            if (std::isnan(value))
            {
                return 1.0f;
            }
            return value < 0.0 ? 0.0f : (value > 1.0 ? 1.0f : static_cast<float>(value));
        }

        bool ReadVolume(const JsonValue& parent, const char* key, float& out)
        {
            const auto value = parent.FindMember(key);
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsNumber())
            {
                return false;
            }
            out = NormalizeVolume(value.AsNumber());
            return true;
        }

        bool ReadDimension(const JsonValue& parent, const char* key, uint32_t& out)
        {
            const auto value = parent.FindMember(key);
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsIntegerLiteral() || value.AsNumber() < 0.0 || value.AsNumber() > UINT32_MAX)
            {
                return false;
            }
            out = value.AsUInt32();
            return true;
        }

        enum class CandidateState : uint8_t { Valid, Missing, Corrupt, Newer, IoError };

        struct Candidate
        {
            CandidateState State = CandidateState::Corrupt;
            UserSettings Value;
            Container::String Message;
        };

        Candidate ReadCandidate(const Container::String& path, Platform::IFileOperations& ops)
        {
            Candidate candidate;
            Container::VariableArray<uint8_t> bytes;
            const auto read = Platform::ReadFileBytes(path, bytes, ops);
            if (!read.bSuccess)
            {
                candidate.State = read.Kind == Platform::FileErrorKind::NotFound ?
                    CandidateState::Missing : CandidateState::IoError;
                candidate.Message = read.Message;
                return candidate;
            }
            JsonDocument document;
            if (!JsonDocument::TryParseUtf8(bytes, document))
            {
                return candidate;
            }
            const auto root = document.GetRoot();
            const auto version = root.FindMember("formatVersion");
            if (!root.IsObject() || !version.IsIntegerLiteral() || version.AsNumber() < 1.0)
            {
                return candidate;
            }
            // 重複した版の欄はどちらが正しいか決めず、破損として扱う。
            size_t versionCount = 0;
            for (size_t index = 0; index < root.GetObjectSize(); ++index)
            {
                if (root.GetMemberName(index) == "formatVersion")
                {
                    ++versionCount;
                }
            }
            if (versionCount != 1)
            {
                return candidate;
            }
            if (version.AsNumber() > SettingsFormatVersion)
            {
                candidate.State = CandidateState::Newer;
                return candidate;
            }
            const auto audio = root.FindMember("audio");
            if (audio.IsValid() && (!audio.IsObject() ||
                !ReadVolume(audio, "master", candidate.Value.MasterVolume) ||
                !ReadVolume(audio, "effect", candidate.Value.EffectVolume) ||
                !ReadVolume(audio, "ambience", candidate.Value.AmbienceVolume)))
            {
                return candidate;
            }
            const auto display = root.FindMember("display");
            const auto fullscreen = display.FindMember("fullscreen");
            if (display.IsValid() && (!display.IsObject() ||
                !ReadDimension(display, "width", candidate.Value.ResolutionWidth) ||
                !ReadDimension(display, "height", candidate.Value.ResolutionHeight) ||
                (fullscreen.IsValid() && !fullscreen.IsBoolean())))
            {
                return candidate;
            }
            candidate.Value.bFullscreen = fullscreen.AsBool();
            const auto language = root.FindMember("language");
            if (language.IsValid() && !language.IsString())
            {
                return candidate;
            }
            candidate.Value.Language = language.AsString();
            const auto bindings = root.FindMember("keyBindings");
            if (bindings.IsValid())
            {
                JsonWriter writer;
                if (!bindings.IsObject() || !WriteValue(writer, nullptr, bindings) || !writer.IsComplete())
                {
                    return candidate;
                }
                candidate.Value.KeyBindingsJson = writer.ToString();
            }
            candidate.State = CandidateState::Valid;
            return candidate;
        }

        Platform::FileOperationResult Failure(const char* message)
        {
            return {false, Platform::FileErrorKind::IoError, 0, Container::String(message)};
        }
    }

    SettingsStore::SettingsStore(const Container::String& filePath)
        : SettingsStore(filePath, Platform::GetDefaultFileOperations())
    {
    }

    SettingsStore::SettingsStore(const Container::String& filePath, Platform::IFileOperations& ops)
        : m_FilePath(filePath), m_Operations(ops)
    {
    }

    bool SettingsStore::Set(const UserSettings& value)
    {
        UserSettings normalized = value;
        normalized.MasterVolume = NormalizeVolume(value.MasterVolume);
        normalized.EffectVolume = NormalizeVolume(value.EffectVolume);
        normalized.AmbienceVolume = NormalizeVolume(value.AmbienceVolume);
        if (!value.KeyBindingsJson.empty())
        {
            JsonDocument document;
            JsonWriter writer;
            if (JsonDocument::TryParse(value.KeyBindingsJson, document) && document.GetRoot().IsObject() &&
                WriteValue(writer, nullptr, document.GetRoot()) && writer.IsComplete())
            {
                normalized.KeyBindingsJson = writer.ToString();
            }
        }
        if (m_Value == normalized)
        {
            return false;
        }
        m_Value = normalized;
        // 購読解除や再入で通知の参照先と列挙対象が変わらないよう、値と購読を固定する。
        const UserSettings snapshot = m_Value;
        const auto subscribers = m_OnValueChanged;
        subscribers.Broadcast(snapshot);
        return true;
    }

    SettingsLoadResult SettingsStore::Load()
    {
        auto candidate = ReadCandidate(m_FilePath, m_Operations);
        bool bBackup = false;
        if (candidate.State == CandidateState::Missing || candidate.State == CandidateState::Corrupt)
        {
            auto backup = m_FilePath;
            backup.append(".bak");
            candidate = ReadCandidate(backup, m_Operations);
            bBackup = true;
        }
        if (candidate.State == CandidateState::Newer)
        {
            m_bSaveBlocked = true;
            return {SettingsLoadStatus::RejectedNewerVersion, "新しい版の設定があるため、読み込みと保存を中止しました。"};
        }
        if (candidate.State == CandidateState::IoError)
        {
            return {SettingsLoadStatus::IoError, candidate.Message};
        }
        if (candidate.State == CandidateState::Valid)
        {
            Set(candidate.Value);
            return {bBackup ? SettingsLoadStatus::LoadedFromBackup : SettingsLoadStatus::Loaded,
                bBackup ? "バックアップから設定を読み込みました。" : "設定を読み込みました。"};
        }
        Set(UserSettings{});
        return {SettingsLoadStatus::Defaults, "読み込める設定がないため、既定値を使います。"};
    }

    Platform::FileOperationResult SettingsStore::Save()
    {
        if (m_bSaveBlocked)
        {
            return Failure("新しい版の設定を保全するため、保存を中止しました。");
        }
        JsonDocument bindings;
        if (!m_Value.KeyBindingsJson.empty() &&
            (!JsonDocument::TryParse(m_Value.KeyBindingsJson, bindings) || !bindings.GetRoot().IsObject()))
        {
            return Failure("キー割当は有効な JSON オブジェクトで指定してください。");
        }
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteUInt64("formatVersion", SettingsFormatVersion);
        writer.BeginObject("audio");
        writer.WriteNumber("master", m_Value.MasterVolume);
        writer.WriteNumber("effect", m_Value.EffectVolume);
        writer.WriteNumber("ambience", m_Value.AmbienceVolume);
        writer.EndObject();
        writer.BeginObject("display");
        writer.WriteUInt64("width", m_Value.ResolutionWidth);
        writer.WriteUInt64("height", m_Value.ResolutionHeight);
        writer.WriteBool("fullscreen", m_Value.bFullscreen);
        writer.EndObject();
        writer.WriteString("language", m_Value.Language);
        if (!m_Value.KeyBindingsJson.empty() && !WriteValue(writer, "keyBindings", bindings.GetRoot()))
        {
            return Failure("キー割当を情報を失わずに書き出せません。");
        }
        writer.EndObject();
        if (!writer.IsComplete())
        {
            return Failure("設定の JSON を書き出せません。");
        }
        const auto text = writer.ToString();
        const Container::Span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(text.data()), text.size()};
        // 不正な UTF-8 を保存せず、ファイル操作の前に文書全体を検証する。
        JsonDocument verified;
        if (!JsonDocument::TryParseUtf8(bytes, verified))
        {
            return Failure("設定に不正な UTF-8 が含まれています。");
        }
        return Platform::WriteFileAtomic(m_FilePath, bytes, true, m_Operations);
    }
} // namespace NorvesLib::Core::Save
