#include "Text/JsonWriter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace NorvesLib::Core
{
    JsonWriter::JsonWriter(bool bPretty)
        : m_bPretty(bPretty)
    {
        m_Buffer.reserve(256);
    }

    void JsonWriter::BeginObject()
    {
        BeginContainer(ScopeKind::Object);
    }

    void JsonWriter::BeginObject(const char* key)
    {
        BeginContainer(key, ScopeKind::Object);
    }

    void JsonWriter::EndObject()
    {
        EndContainer(ScopeKind::Object);
    }

    void JsonWriter::BeginArray()
    {
        BeginContainer(ScopeKind::Array);
    }

    void JsonWriter::BeginArray(const char* key)
    {
        BeginContainer(key, ScopeKind::Array);
    }

    void JsonWriter::EndArray()
    {
        EndContainer(ScopeKind::Array);
    }

    void JsonWriter::WriteString(const char* key, const Container::String& value)
    {
        if (!PrepareKeyedValue(key))
        {
            return;
        }
        AppendEscapedString(value.data(), value.size());
    }

    void JsonWriter::WriteString(const char* key, const char* value)
    {
        if (!value)
        {
            SetError();
            return;
        }
        if (!PrepareKeyedValue(key))
        {
            return;
        }
        AppendEscapedString(value, std::strlen(value));
    }

    void JsonWriter::WriteStringElement(const Container::String& value)
    {
        if (!PrepareElementValue())
        {
            return;
        }
        AppendEscapedString(value.data(), value.size());
    }

    void JsonWriter::WriteNumber(const char* key, double value)
    {
        if (!PrepareKeyedValue(key))
        {
            return;
        }
        AppendDouble(value);
    }

    void JsonWriter::WriteNumberElement(double value)
    {
        if (!PrepareElementValue())
        {
            return;
        }
        AppendDouble(value);
    }

    void JsonWriter::WriteInt64(const char* key, int64_t value)
    {
        if (!PrepareKeyedValue(key))
        {
            return;
        }
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        m_Buffer.append(buffer);
    }

    void JsonWriter::WriteUInt64(const char* key, uint64_t value)
    {
        if (!PrepareKeyedValue(key))
        {
            return;
        }
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
        m_Buffer.append(buffer);
    }

    void JsonWriter::WriteBool(const char* key, bool value)
    {
        if (!PrepareKeyedValue(key))
        {
            return;
        }
        m_Buffer.append(value ? "true" : "false");
    }

    void JsonWriter::WriteBoolElement(bool value)
    {
        if (!PrepareElementValue())
        {
            return;
        }
        m_Buffer.append(value ? "true" : "false");
    }

    void JsonWriter::WriteNull(const char* key)
    {
        if (!PrepareKeyedValue(key))
        {
            return;
        }
        m_Buffer.append("null");
    }

    void JsonWriter::WriteNullElement()
    {
        if (!PrepareElementValue())
        {
            return;
        }
        m_Buffer.append("null");
    }

    bool JsonWriter::PrepareKeyedValue(const char* key)
    {
        if (m_bError)
        {
            return false;
        }
        if (!key || m_Stack.empty() || m_Stack.back().Kind != ScopeKind::Object)
        {
            SetError();
            return false;
        }

        AppendSeparatorAndIndent();
        m_Stack.back().bHasElements = true;
        AppendKey(key);
        return true;
    }

    bool JsonWriter::PrepareElementValue()
    {
        if (m_bError)
        {
            return false;
        }
        if (m_Stack.empty())
        {
            if (m_bRootWritten)
            {
                SetError();
                return false;
            }
            m_bRootWritten = true;
            return true;
        }
        if (m_Stack.back().Kind != ScopeKind::Array)
        {
            SetError();
            return false;
        }

        AppendSeparatorAndIndent();
        m_Stack.back().bHasElements = true;
        return true;
    }

    void JsonWriter::BeginContainer(ScopeKind kind)
    {
        if (!PrepareElementValue())
        {
            return;
        }
        m_Buffer.push_back(kind == ScopeKind::Object ? '{' : '[');

        Scope scope;
        scope.Kind = kind;
        m_Stack.push_back(scope);
    }

    void JsonWriter::BeginContainer(const char* key, ScopeKind kind)
    {
        if (!PrepareKeyedValue(key))
        {
            return;
        }
        m_Buffer.push_back(kind == ScopeKind::Object ? '{' : '[');

        Scope scope;
        scope.Kind = kind;
        m_Stack.push_back(scope);
    }

    void JsonWriter::EndContainer(ScopeKind kind)
    {
        if (m_bError)
        {
            return;
        }
        if (m_Stack.empty() || m_Stack.back().Kind != kind)
        {
            SetError();
            return;
        }

        const bool bHadElements = m_Stack.back().bHasElements;
        m_Stack.pop_back();
        if (m_bPretty && bHadElements)
        {
            m_Buffer.push_back('\n');
            AppendIndent(m_Stack.size());
        }
        m_Buffer.push_back(kind == ScopeKind::Object ? '}' : ']');
    }

    void JsonWriter::AppendSeparatorAndIndent()
    {
        if (m_Stack.back().bHasElements)
        {
            m_Buffer.push_back(',');
        }
        if (m_bPretty)
        {
            m_Buffer.push_back('\n');
            AppendIndent(m_Stack.size());
        }
    }

    void JsonWriter::AppendIndent(size_t depth)
    {
        for (size_t level = 0; level < depth; ++level)
        {
            m_Buffer.append("  ");
        }
    }

    void JsonWriter::AppendKey(const char* key)
    {
        AppendEscapedString(key, std::strlen(key));
        m_Buffer.push_back(':');
        if (m_bPretty)
        {
            m_Buffer.push_back(' ');
        }
    }

    void JsonWriter::AppendEscapedString(const char* value, size_t length)
    {
        m_Buffer.push_back('"');
        for (size_t index = 0; index < length; ++index)
        {
            const char ch = value[index];
            switch (ch)
            {
            case '"':
                m_Buffer.append("\\\"");
                break;
            case '\\':
                m_Buffer.append("\\\\");
                break;
            case '\n':
                m_Buffer.append("\\n");
                break;
            case '\r':
                m_Buffer.append("\\r");
                break;
            case '\t':
                m_Buffer.append("\\t");
                break;
            case '\b':
                m_Buffer.append("\\b");
                break;
            case '\f':
                m_Buffer.append("\\f");
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20)
                {
                    char buffer[8] = {};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                  static_cast<unsigned int>(static_cast<unsigned char>(ch)));
                    m_Buffer.append(buffer);
                }
                else
                {
                    m_Buffer.push_back(ch);
                }
                break;
            }
        }
        m_Buffer.push_back('"');
    }

    void JsonWriter::AppendDouble(double value)
    {
        if (!std::isfinite(value))
        {
            SetError();
            m_Buffer.push_back('0');
            return;
        }

        std::basic_ostringstream<TCHAR> stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
        m_Buffer.append(Container::String(stream.str()));
    }

    void JsonWriter::SetError()
    {
        m_bError = true;
    }

} // namespace NorvesLib::Core