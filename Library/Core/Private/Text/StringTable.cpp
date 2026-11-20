// 言語別の表示文字列を読み込み、キーの検索・引数の置換・文字集合の収集を行う。
#include "Text/StringTable.h"
#include "Asset/AssetFileReader.h"
#include "Container/UnorderedSet.h"
#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"
#include "Text/Utf8.h"
#include <algorithm>
#include <charconv>

namespace NorvesLib::Core
{
    namespace
    {
        using Container::AnsiString;
        using Container::AnsiStringView;

        bool IsDigit(char ch)
        {
            return ch >= '0' && ch <= '9';
        }

        bool IsLanguageName(AnsiStringView name)
        {
            return !name.empty() && std::all_of(name.begin(), name.end(), [](char ch)
            {
                return (ch >= 'a' && ch <= 'z') || IsDigit(ch) || ch == '-';
            });
        }

        // JSON エスケープで表せる U+0000 の後ろも含めてコピーする。
        AnsiString CopyBytes(AnsiStringView source)
        {
            AnsiString result(source.size(), '\0');
            if (!source.empty())
            {
                std::copy_n(source.data(), source.size(), result.begin());
            }
            return result;
        }

        const TextFormatArg* FindArgument(AnsiStringView token, Container::Span<const TextFormatArg> args)
        {
            if (token.empty())
            {
                return nullptr;
            }
            if (std::all_of(token.begin(), token.end(), IsDigit))
            {
                size_t position = 0;
                if (std::from_chars(token.begin(), token.end(), position).ec != std::errc{})
                {
                    return nullptr;
                }
                for (const auto& arg : args)
                {
                    if (arg.Name.empty())
                    {
                        if (position == 0)
                        {
                            return &arg;
                        }
                        --position;
                    }
                }
            }
            else
            {
                for (const auto& arg : args)
                {
                    if (arg.Name.size() == token.size() && std::equal(token.begin(), token.end(), arg.Name.begin()))
                    {
                        return &arg;
                    }
                }
            }
            return nullptr;
        }
    }

    bool StringTable::LoadLanguage(AnsiStringView language, const Asset::AssetFileReader& reader)
    {
        if (!IsLanguageName(language))
        {
            NORVES_LOG_ERROR("StringTable", "言語名には英小文字・数字・ハイフンを指定してください");
            return false;
        }
        AnsiString path("Text/");
        path.append(language.data(), language.size()).append(".strings.json");
        const auto fail = [&](const char* reason)
        {
            NORVES_LOG_ERROR("StringTable", "%s の読み込みに失敗しました。%s", path.c_str(), reason);
            return false;
        };
        const auto file = reader.Read(path);
        if (!file.Succeeded())
        {
            return fail("ファイルを読めません");
        }
        const auto bytes = file.Blob.GetSpan();
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        {
            return fail("UTF-8 の BOM が付いています");
        }
        if (!IsValidUtf8({reinterpret_cast<const char*>(bytes.data()), bytes.size()}))
        {
            return fail("UTF-8 として不正なバイト列です");
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(bytes, document))
        {
            return fail("JSON の構文が不正です");
        }
        const auto root = document.GetRoot();
        if (!root.IsObject())
        {
            return fail("ルートにはオブジェクトが必要です");
        }
        Container::UnorderedSet<uint64_t> fields;
        for (size_t index = 0; index < root.GetObjectSize(); ++index)
        {
            const auto& name = root.GetMemberName(index);
            if (!fields.insert(Fnv1aRaw(name.data(), name.size())).second)
            {
                return fail("ルートのキーが重複しています");
            }
        }
        const auto version = root.FindMember("version");
        if (!version.IsIntegerLiteral() || version.AsNumber() != 1)
        {
            return fail("version には整数の 1 が必要です");
        }
        if (AnsiStringView(root.FindMember("language").AsString()) != language)
        {
            return fail("language がファイル名の言語と一致しません");
        }
        LanguageTable table;
        const auto fallback = root.FindMember("fallback");
        if (fallback.IsValid())
        {
            if (!IsLanguageName(fallback.AsString()))
            {
                return fail("fallback の言語名が不正です");
            }
            table.Fallback = fallback.AsString();
        }
        const auto strings = root.FindMember("strings");
        if (!strings.IsObject())
        {
            return fail("strings にはオブジェクトが必要です");
        }
        table.Strings.reserve(strings.GetObjectSize());
        for (size_t index = 0; index < strings.GetObjectSize(); ++index)
        {
            const auto value = strings.GetMemberValue(index);
            if (!value.IsString())
            {
                return fail("strings の値には文字列が必要です");
            }
            const auto& name = strings.GetMemberName(index);
            const auto key = IdentityPool::Get().CreateIdentity(name);
            if (!table.Strings.emplace(key.GetHash(), Translation{CopyBytes(name), CopyBytes(value.AsString())}).second)
            {
                return fail("strings のキーが重複しています");
            }
        }
        m_Languages.insert_or_assign(AnsiString(language), std::move(table));
        return true;
    }

    bool StringTable::SetActiveLanguage(AnsiStringView language)
    {
        if (!IsLanguageName(language))
        {
            return false;
        }
        const auto found = m_Languages.find(AnsiString(language));
        if (found == m_Languages.end())
        {
            return false;
        }
        if (found->first != m_ActiveLanguage)
        {
            m_ActiveLanguage = found->first;
            OnLanguageChanged.Broadcast(found->first);
        }
        return true;
    }

    Container::AnsiStringView StringTable::Get(Identity key) const
    {
        const auto active = m_Languages.find(m_ActiveLanguage);
        if (active != m_Languages.end())
        {
            const auto& table = active->second;
            const auto entry = table.Strings.find(key.GetHash());
            if (entry != table.Strings.end())
            {
                return entry->second.Value;
            }
#if !NORVES_BUILD_DEVELOPMENT
            const auto fallback = m_Languages.find(table.Fallback);
            if (fallback != m_Languages.end() && fallback != active)
            {
                const auto translation = fallback->second.Strings.find(key.GetHash());
                if (translation != fallback->second.Strings.end())
                {
                    return translation->second.Value;
                }
            }
#endif
        }
#if NORVES_BUILD_DEVELOPMENT
        bool firstWarning;
        {
            Thread::ScopedLock lock(m_WarningMutex);
            firstWarning = m_WarnedKeys.insert(key.GetHash()).second;
        }
        if (firstWarning)
        {
            NORVES_LOG_WARNING("StringTable", "使用言語「%s」にキー「%s」の訳がありません",
                               m_ActiveLanguage.c_str(), key.ToString().c_str());
        }
#endif
        return key.GetView();
    }

    Container::AnsiString StringTable::Format(AnsiStringView pattern, Container::Span<const TextFormatArg> args)
    {
        Container::VariableArray<char> result;
        result.reserve(pattern.size());
        for (size_t pos = 0; pos < pattern.size();)
        {
            const char ch = pattern[pos++];
            if ((ch == '{' || ch == '}') && pos < pattern.size() && pattern[pos] == ch)
            {
                result.push_back(ch);
                ++pos;
                continue;
            }
            if (ch != '{')
            {
                result.push_back(ch);
                continue;
            }
            // 次の開き括弧に達したら、そこから新しい置換候補を読む。
            const size_t start = pos - 1;
            const size_t boundary = pattern.find_first_of("{}", pos);
            const size_t end = boundary == AnsiStringView::npos ? pattern.size() : boundary;
            AnsiStringView piece;
            if (end < pattern.size() && pattern[end] == '}')
            {
                const auto* arg = FindArgument(pattern.substr(pos, end - pos), args);
                pos = end + 1;
                piece = arg ? arg->Value : pattern.substr(start, pos - start);
            }
            else
            {
                pos = end;
                piece = pattern.substr(start, pos - start);
            }
            if (!piece.empty())
            {
                result.insert(result.end(), piece.begin(), piece.end());
            }
        }
        return CopyBytes({result.data(), result.size()});
    }

    Container::AnsiString StringTable::Format(Identity key, Container::Span<const TextFormatArg> args) const
    {
        return Format(Get(key), args);
    }

    void StringTable::CollectCodepoints(Container::VariableArray<uint32_t>& out, AnsiStringView keyPrefix) const
    {
        Container::UnorderedSet<uint32_t> codepoints;
        for (uint32_t ch = 0x20; ch <= 0x7e; ++ch)
        {
            codepoints.insert(ch);
        }
        for (const auto& [language, table] : m_Languages)
        {
            for (const auto& [hash, translation] : table.Strings)
            {
                const auto& name = translation.KeyName;
                if (keyPrefix.empty() || (name.size() >= keyPrefix.size() &&
                    std::equal(keyPrefix.begin(), keyPrefix.end(), name.begin())))
                {
                    ForEachUtf8Codepoint(AnsiStringView(translation.Value).as_span(), [&](uint32_t ch) { codepoints.insert(ch); });
                }
            }
        }
        out.assign(codepoints.begin(), codepoints.end());
        std::sort(out.begin(), out.end());
    }
}
