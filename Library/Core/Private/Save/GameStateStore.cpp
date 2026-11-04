#include "Save/GameStateStore.h"

#include "Logging/LogMacros.h"
#include "Platform/PlatformFile.h"
#include "Text/JsonDocument.h"
#include <charconv>
#include <utility>

namespace NorvesLib::Core::Save
{
    namespace
    {
        bool LedgerError(Container::String* pOutError, const char* message)
        {
            if (pOutError)
            {
                *pOutError = message;
            }
            NORVES_LOG_WARNING("GameStateStore", "台帳の読み込みに失敗しました: %s", message);
            return false;
        }

        bool HasDuplicateMembers(const JsonValue& object)
        {
            for (size_t left = 0; left < object.GetObjectSize(); ++left)
            {
                for (size_t right = left + 1; right < object.GetObjectSize(); ++right)
                {
                    if (object.GetMemberName(left) == object.GetMemberName(right))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        // JsonDocument は数値を double で保持するため、整数の字句を別の文書に残す。
        // 元の JSON は先に検証されるので、ここでは文字列と数値の境界だけを走査する。
        Container::String PreserveIntegerLiterals(const Container::String& text)
        {
            Container::String result;
            bool inString = false;
            for (size_t index = 0; index < text.size();)
            {
                const char ch = text[index];
                if (inString)
                {
                    result.push_back(ch);
                    ++index;
                    if (ch == '\\' && index < text.size())
                    {
                        result.push_back(text[index++]);
                    }
                    else if (ch == '"')
                    {
                        inString = false;
                    }
                }
                else if (ch == '"')
                {
                    inString = true;
                    result.push_back(ch);
                    ++index;
                }
                else if (ch == '-' || (ch >= '0' && ch <= '9'))
                {
                    const size_t start = index;
                    bool integer = true;
                    while (index < text.size())
                    {
                        const char part = text[index];
                        if ((part >= '0' && part <= '9') || part == '-' || part == '+' ||
                            part == '.' || part == 'e' || part == 'E')
                        {
                            if (part == '.' || part == 'e' || part == 'E') integer = false;
                            ++index;
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (integer) result.push_back('"');
                    result.append(text.data() + start, index - start);
                    if (integer) result.push_back('"');
                }
                else
                {
                    result.push_back(ch);
                    ++index;
                }
            }
            return result;
        }

        bool ParseInt64Literal(const JsonValue& value, const JsonValue& preserved, int64_t& out)
        {
            if (!value.IsIntegerLiteral() || !preserved.IsString())
            {
                return false;
            }
            const Container::String& literal = preserved.AsString();
            const char* begin = literal.data();
            const char* end = begin + literal.size();
            const auto parsed = std::from_chars(begin, end, out);
            return parsed.ec == std::errc{} && parsed.ptr == end;
        }
    }

    bool GameStateStore::LoadKeyLedgerFromText(const Container::String& jsonText, Container::String* pOutError)
    {
        JsonDocument document;
        if (!JsonDocument::TryParse(jsonText, document))
        {
            return LedgerError(pOutError, "JSON が不正です");
        }
        JsonDocument preserved;
        if (!JsonDocument::TryParse(PreserveIntegerLiterals(jsonText), preserved))
        {
            return LedgerError(pOutError, "整数の読み取りに失敗しました");
        }
        const JsonValue root = document.GetRoot();
        const JsonValue version = root.FindMember("formatVersion");
        const JsonValue keys = root.FindMember("keys");
        if (!root.IsObject() || HasDuplicateMembers(root) || !version.IsIntegerLiteral() ||
            version.AsNumber() != 1.0 || !keys.IsArray())
        {
            return LedgerError(pOutError, "台帳の形式または版が不正です");
        }

        Container::VariableArray<Record> records;
        Container::UnorderedMap<uint64_t, size_t> indices;
        const JsonValue preservedKeys = preserved.GetRoot().FindMember("keys");
        records.reserve(keys.GetArraySize());
        for (size_t index = 0; index < keys.GetArraySize(); ++index)
        {
            const JsonValue item = keys.GetArrayElement(index);
            const JsonValue name = item.FindMember("name");
            const JsonValue type = item.FindMember("type");
            const JsonValue scope = item.FindMember("scope");
            const JsonValue defaultValue = item.FindMember("default");
            if (!item.IsObject() || HasDuplicateMembers(item) || !name.IsString() ||
                name.AsString().empty() || name.AsString().find('\0') != Container::String::npos ||
                !type.IsString() || !scope.IsString())
            {
                return LedgerError(pOutError, "キーの定義が不正です");
            }
            Record record;
            record.Name = name.AsString();
            const uint64_t hash = IdentityHash(record.Name.data(), record.Name.size());
            if (indices.find(hash) != indices.end())
            {
                return LedgerError(pOutError, "キー名が重複しているかハッシュが衝突しています");
            }
            if (scope.AsString() == "persistent") record.Scope = GameStateScope::Persistent;
            else if (scope.AsString() == "session") record.Scope = GameStateScope::Session;
            else return LedgerError(pOutError, "キーのスコープが不明です");

            if (type.AsString() == "bool")
            {
                record.Type = GameStateValueType::Bool;
                if (defaultValue.IsValid() && !defaultValue.IsBoolean())
                    return LedgerError(pOutError, "bool の既定値が不正です");
                record.DefaultBool = defaultValue.AsBool();
                record.BoolValue = record.DefaultBool;
            }
            else if (type.AsString() == "int64")
            {
                record.Type = GameStateValueType::Int64;
                const JsonValue preservedDefault = preservedKeys.GetArrayElement(index).FindMember("default");
                if (defaultValue.IsValid() && !ParseInt64Literal(defaultValue, preservedDefault, record.DefaultInt))
                    return LedgerError(pOutError, "int64 の既定値が不正または範囲外です");
                record.IntValue = record.DefaultInt;
            }
            else if (type.AsString() == "double")
            {
                record.Type = GameStateValueType::Double;
                if (defaultValue.IsValid() && !defaultValue.IsNumber())
                    return LedgerError(pOutError, "double の既定値が不正です");
                record.DefaultDouble = defaultValue.AsNumber();
                record.DoubleValue = record.DefaultDouble;
            }
            else
            {
                return LedgerError(pOutError, "キーの型が不明です");
            }
            indices.emplace(hash, records.size());
            records.push_back(std::move(record));
        }
        m_Records = std::move(records);
        m_Indices = std::move(indices);
        ++m_LedgerRevision;
        if (pOutError) pOutError->clear();
        return true;
    }

    bool GameStateStore::LoadKeyLedgerFile(const Container::String& utf8Path)
    {
        Container::VariableArray<uint8_t> bytes;
        const auto result = Platform::ReadFileBytes(utf8Path, bytes);
        if (!result.bSuccess)
        {
            NORVES_LOG_WARNING("GameStateStore", "台帳ファイルを読み込めません: %s", utf8Path.c_str());
            return false;
        }
        size_t offset = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) offset = 3;
        Container::String text;
        if (bytes.size() > offset)
            text.append(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
        return LoadKeyLedgerFromText(text);
    }

    const GameStateStore::Record* GameStateStore::FindTyped(Identity key, GameStateValueType type) const
    {
        const auto found = m_Indices.find(key.GetHash());
        if (!key.IsValid() || found == m_Indices.end() || m_Records[found->second].Type != type)
        {
            NORVES_LOG_WARNING("GameStateStore", "未登録または型の異なるキーです: %016llx",
                static_cast<unsigned long long>(key.GetHash()));
            return nullptr;
        }
        return &m_Records[found->second];
    }

    GameStateStore::Record* GameStateStore::FindTyped(Identity key, GameStateValueType type)
    {
        return const_cast<Record*>(static_cast<const GameStateStore*>(this)->FindTyped(key, type));
    }

    bool GameStateStore::CheckPersistent(const Container::String& name, GameStateValueType type) const
    {
        const uint64_t hash = IdentityHash(name.data(), name.size());
        const auto found = m_Indices.find(hash);
        if (name.empty() || found == m_Indices.end() || m_Records[found->second].Name != name ||
            m_Records[found->second].Scope != GameStateScope::Persistent || m_Records[found->second].Type != type)
        {
            NORVES_LOG_WARNING("GameStateStore", "保存対象でないか型の異なるキーです: %s", name.c_str());
            return false;
        }
        return true;
    }

    void GameStateStore::NotifyChanged(const Container::String& name)
    {
        // 通知先が台帳を置き換えても、通知中の Identity は名前プールに保持される。
        const Identity key(name);
        m_OnValueChanged.Broadcast(key);
    }

    bool GameStateStore::TryGetBool(Identity key, bool& out) const
    {
        const Record* record = FindTyped(key, GameStateValueType::Bool);
        if (!record) return false;
        out = record->BoolValue;
        return true;
    }

    bool GameStateStore::TryGetInt64(Identity key, int64_t& out) const
    {
        const Record* record = FindTyped(key, GameStateValueType::Int64);
        if (!record) return false;
        out = record->IntValue;
        return true;
    }

    bool GameStateStore::TryGetDouble(Identity key, double& out) const
    {
        const Record* record = FindTyped(key, GameStateValueType::Double);
        if (!record) return false;
        out = record->DoubleValue;
        return true;
    }

    bool GameStateStore::SetBool(Identity key, bool value)
    {
        Record* record = FindTyped(key, GameStateValueType::Bool);
        if (!record) return false;
        if (record->BoolValue != value)
        {
            record->BoolValue = value;
            NotifyChanged(record->Name);
        }
        return true;
    }

    bool GameStateStore::SetInt64(Identity key, int64_t value)
    {
        Record* record = FindTyped(key, GameStateValueType::Int64);
        if (!record) return false;
        if (record->IntValue != value)
        {
            record->IntValue = value;
            NotifyChanged(record->Name);
        }
        return true;
    }

    bool GameStateStore::SetDouble(Identity key, double value)
    {
        Record* record = FindTyped(key, GameStateValueType::Double);
        if (!record) return false;
        if (record->DoubleValue != value)
        {
            record->DoubleValue = value;
            NotifyChanged(record->Name);
        }
        return true;
    }

    void GameStateStore::CollectPersistent(Container::VariableArray<GameStateEntry>& out) const
    {
        out.clear();
        for (const Record& record : m_Records)
        {
            if (record.Scope != GameStateScope::Persistent) continue;
            GameStateEntry entry;
            entry.Name = record.Name;
            entry.Type = record.Type;
            entry.BoolValue = record.BoolValue;
            entry.IntValue = record.IntValue;
            entry.DoubleValue = record.DoubleValue;
            out.push_back(std::move(entry));
        }
    }

    bool GameStateStore::ApplyPersistentBool(const Container::String& name, bool value)
    {
        return CheckPersistent(name, GameStateValueType::Bool) && SetBool(Identity(name), value);
    }

    bool GameStateStore::ApplyPersistentInt64(const Container::String& name, int64_t value)
    {
        return CheckPersistent(name, GameStateValueType::Int64) && SetInt64(Identity(name), value);
    }

    bool GameStateStore::ApplyPersistentDouble(const Container::String& name, double value)
    {
        return CheckPersistent(name, GameStateValueType::Double) && SetDouble(Identity(name), value);
    }

    void GameStateStore::ResetScope(GameStateScope scope)
    {
        Container::VariableArray<Identity> keys;
        for (const Record& record : m_Records)
            if (record.Scope == scope) keys.emplace_back(record.Name);
        const uint64_t revision = m_LedgerRevision;
        for (const Identity key : keys)
        {
            if (revision != m_LedgerRevision) break;
            const auto found = m_Indices.find(key.GetHash());
            if (found == m_Indices.end()) continue;
            const Record& record = m_Records[found->second];
            switch (record.Type)
            {
            case GameStateValueType::Bool: SetBool(key, record.DefaultBool); break;
            case GameStateValueType::Int64: SetInt64(key, record.DefaultInt); break;
            case GameStateValueType::Double: SetDouble(key, record.DefaultDouble); break;
            }
        }
    }
}
