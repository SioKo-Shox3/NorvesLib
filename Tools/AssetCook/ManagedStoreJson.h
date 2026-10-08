#pragma once
// managed control schemaで共有する純値JSON検査。file I/Oは行わない。
#include "Container/String.h"
#include "Container/FixedArray.h"
#include "Container/VariableArray.h"
#include "Text/JsonDocument.h"
#include <cstring>
#include <initializer_list>
namespace NorvesLib::Tools::AssetCook::Detail::ManagedStoreJson
{
    using Text = Core::Container::AnsiString;
    using Bytes = Core::Container::VariableArray<uint8_t>;
    using Id = Core::Container::FixedArray<uint8_t, 16>;
    using Core::JsonValue;
    inline bool Key(const Core::Container::String& value, const char* text)
    {
        if (value.size() != std::strlen(text))
        {
            return false;
        }
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] != text[i])
            {
                return false;
            }
        }
        return true;
    }
    inline bool Shape(const JsonValue& v, std::initializer_list<const char*> fields)
    {
        if (fields.size() > 32 || !v.IsObject() || v.GetObjectSize() != fields.size())
        {
            return false;
        }
        uint32_t seen = 0;
        for (size_t i = 0; i < v.GetObjectSize(); ++i)
        {
            size_t n = 0;
            for (const char* f : fields)
            {
                if (Key(v.GetMemberName(i), f))
                {
                    break;
                }
                ++n;
            }
            if (n == fields.size() || (seen & (1u << n)))
            {
                return false;
            }
            seen |= 1u << n;
        }
        return true;
    }
    inline bool String(const JsonValue& v, Text& out, size_t maximum = 255)
    {
        if (!v.IsString() || v.AsString().empty() || v.AsString().size() > maximum)
        {
            return false;
        }
        for (auto c : v.AsString())
        {
            if (c < 32 || c >= 127)
            {
                return false;
            }
            out.push_back(static_cast<char>(c));
        }
        return true;
    }
    inline bool Hex(const JsonValue& v, size_t count, Text& out, bool bNonzero = true)
    {
        if (!String(v, out) || out.size() != count)
        {
            return false;
        }
        bool bAny = false;
        for (char c : out)
        {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            {
                return false;
            }
            bAny |= c != '0';
        }
        return bAny || !bNonzero;
    }
    inline unsigned Digit(char c)
    {
        return c <= '9' ? c - '0' : c - 'a' + 10;
    }
    inline bool ReadId(const JsonValue& v, Id& out)
    {
        Text s;
        if (!Hex(v, 32, s))
        {
            return false;
        }
        for (size_t i = 0; i < 16; ++i)
        {
            out[i] = static_cast<uint8_t>(Digit(s[i * 2]) * 16 + Digit(s[i * 2 + 1]));
        }
        return true;
    }
    inline bool ReadU64(const JsonValue& v, uint64_t& out, bool bNonzero)
    {
        Text s;
        if (!Hex(v, 16, s, bNonzero))
        {
            return false;
        }
        out = 0;
        for (char c : s)
        {
            out = (out << 4) | Digit(c);
        }
        return true;
    }
    inline bool Json(const Bytes& bytes, Core::JsonDocument& out)
    {
        size_t depth = 0, tokens = 0;
        bool bString = false, bEscape = false;
        for (uint8_t c : bytes)
        {
            if (bString)
            {
                if (bEscape)
                {
                    bEscape = false;
                }
                else if (c == '\\')
                {
                    bEscape = true;
                }
                else if (c == '"')
                {
                    bString = false;
                }
            }
            else if (c == '"')
            {
                bString = true;
                if (++tokens > 100000)
                {
                    return false;
                }
            }
            else if (c == '{' || c == '[')
            {
                if (++depth > 8 || ++tokens > 100000)
                {
                    return false;
                }
            }
            else if (c == '}' || c == ']')
            {
                if (!depth)
                {
                    return false;
                }
                --depth;
            }
            else if (c == ',' || c == ':')
            {
                if (++tokens > 100000)
                {
                    return false;
                }
            }
        }
        return !bString && !depth && Core::JsonDocument::TryParseUtf8(bytes, out);
    }
    inline bool Version(const JsonValue& v)
    {
        return v.IsIntegerLiteral() && v.AsNumber() == 1;
    }

    inline bool Token(Core::Container::AnsiStringView s, size_t size)
    {
        if (s.size() != size)
        {
            return false;
        }
        bool bNonzero = false;
        for (char c : s)
        {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            {
                return false;
            }
            bNonzero |= c != '0';
        }
        return bNonzero;
    }
} // namespace NorvesLib::Tools::AssetCook::Detail::ManagedStoreJson
