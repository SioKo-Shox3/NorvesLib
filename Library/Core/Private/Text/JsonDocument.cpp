#include "Text/JsonDocument.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace NorvesLib::Core
{
    class JsonDocumentParser
    {
    public:
        JsonDocumentParser(const Container::String& text, JsonDocument& document, Container::String* pOutError)
            : m_Text(text), m_Document(document), m_pOutError(pOutError)
        {
        }

        bool Parse()
        {
            m_Document.Reset();
            SkipWhitespace();

            size_t rootNodeIndex = JsonDocument::InvalidNodeIndex;
            if (!ParseValue(rootNodeIndex))
            {
                return false;
            }

            SkipWhitespace();
            if (!IsAtEnd())
            {
                return SetError("Unexpected trailing characters");
            }

            m_Document.m_RootNodeIndex = rootNodeIndex;
            return true;
        }

    private:
        bool ParseValue(size_t& outNodeIndex)
        {
            if (IsAtEnd())
            {
                return SetError("Unexpected end of input");
            }

            char ch = Peek();
            switch (ch)
            {
            case '{':
                return ParseObject(outNodeIndex);
            case '[':
                return ParseArray(outNodeIndex);
            case '"':
                return ParseStringValue(outNodeIndex);
            case 't':
                return ParseLiteral("true", JsonType::Boolean, outNodeIndex, true);
            case 'f':
                return ParseLiteral("false", JsonType::Boolean, outNodeIndex, false);
            case 'n':
                return ParseLiteral("null", JsonType::Null, outNodeIndex, false);
            default:
                if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)))
                {
                    return ParseNumberValue(outNodeIndex);
                }
                break;
            }

            return SetError("Unexpected token");
        }

        bool ParseObject(size_t& outNodeIndex)
        {
            outNodeIndex = m_Document.CreateNode(JsonType::Object);

            Advance(); // {
            SkipWhitespace();

            if (ConsumeIf('}'))
            {
                return true;
            }

            while (true)
            {
                Container::String key;
                if (!ParseStringLiteral(key))
                {
                    return false;
                }

                SkipWhitespace();
                if (!ConsumeIf(':'))
                {
                    return SetError("Expected ':' after object key");
                }

                SkipWhitespace();

                size_t childNodeIndex = JsonDocument::InvalidNodeIndex;
                if (!ParseValue(childNodeIndex))
                {
                    return false;
                }

                JsonDocument::JsonObjectEntry entry;
                entry.Key = std::move(key);
                entry.NodeIndex = childNodeIndex;
                m_Document.m_Nodes[outNodeIndex].ObjectChildren.push_back(std::move(entry));

                SkipWhitespace();
                if (ConsumeIf('}'))
                {
                    return true;
                }

                if (!ConsumeIf(','))
                {
                    return SetError("Expected ',' or '}' in object");
                }

                SkipWhitespace();
            }
        }

        bool ParseArray(size_t& outNodeIndex)
        {
            outNodeIndex = m_Document.CreateNode(JsonType::Array);

            Advance(); // [
            SkipWhitespace();

            if (ConsumeIf(']'))
            {
                return true;
            }

            while (true)
            {
                size_t childNodeIndex = JsonDocument::InvalidNodeIndex;
                if (!ParseValue(childNodeIndex))
                {
                    return false;
                }

                m_Document.m_Nodes[outNodeIndex].ArrayChildren.push_back(childNodeIndex);

                SkipWhitespace();
                if (ConsumeIf(']'))
                {
                    return true;
                }

                if (!ConsumeIf(','))
                {
                    return SetError("Expected ',' or ']' in array");
                }

                SkipWhitespace();
            }
        }

        bool ParseStringValue(size_t& outNodeIndex)
        {
            Container::String parsedString;
            if (!ParseStringLiteral(parsedString))
            {
                return false;
            }

            outNodeIndex = m_Document.CreateNode(JsonType::String);
            m_Document.m_Nodes[outNodeIndex].StringValue = std::move(parsedString);
            return true;
        }

        bool ParseNumberValue(size_t& outNodeIndex)
        {
            size_t numberStart = m_Position;

            if (Peek() == '-')
            {
                Advance();
            }

            if (IsAtEnd())
            {
                return SetError("Unexpected end while parsing number");
            }

            if (Peek() == '0')
            {
                Advance();
            }
            else if (std::isdigit(static_cast<unsigned char>(Peek())))
            {
                while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek())))
                {
                    Advance();
                }
            }
            else
            {
                return SetError("Invalid number");
            }

            if (!IsAtEnd() && Peek() == '.')
            {
                Advance();
                if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek())))
                {
                    return SetError("Invalid fractional part");
                }

                while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek())))
                {
                    Advance();
                }
            }

            if (!IsAtEnd() && (Peek() == 'e' || Peek() == 'E'))
            {
                Advance();
                if (!IsAtEnd() && (Peek() == '+' || Peek() == '-'))
                {
                    Advance();
                }

                if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek())))
                {
                    return SetError("Invalid exponent");
                }

                while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek())))
                {
                    Advance();
                }
            }

            Container::String numberLiteral = m_Text.substr(numberStart, m_Position - numberStart);
            char* endPointer = nullptr;
            double numberValue = std::strtod(numberLiteral.c_str(), &endPointer);
            if (endPointer == numberLiteral.c_str())
            {
                return SetError("Failed to parse number");
            }

            outNodeIndex = m_Document.CreateNode(JsonType::Number);
            m_Document.m_Nodes[outNodeIndex].NumberValue = numberValue;
            return true;
        }

        bool ParseLiteral(const char* literal, JsonType type, size_t& outNodeIndex, bool boolValue)
        {
            for (size_t index = 0; literal[index] != '\0'; ++index)
            {
                if (IsAtEnd() || Peek() != literal[index])
                {
                    return SetError("Invalid literal");
                }
                Advance();
            }

            outNodeIndex = m_Document.CreateNode(type);
            if (type == JsonType::Boolean)
            {
                m_Document.m_Nodes[outNodeIndex].bBoolValue = boolValue;
            }
            return true;
        }

        bool ParseStringLiteral(Container::String& outString)
        {
            if (!ConsumeIf('"'))
            {
                return SetError("Expected string");
            }

            outString.clear();

            while (!IsAtEnd())
            {
                char ch = Advance();
                if (ch == '"')
                {
                    return true;
                }

                if (static_cast<unsigned char>(ch) < 0x20)
                {
                    return SetError("Control character in string");
                }

                if (ch != '\\')
                {
                    outString.push_back(ch);
                    continue;
                }

                if (IsAtEnd())
                {
                    return SetError("Unterminated escape sequence");
                }

                char escape = Advance();
                switch (escape)
                {
                case '"':
                case '\\':
                case '/':
                    outString.push_back(escape);
                    break;
                case 'b':
                    outString.push_back('\b');
                    break;
                case 'f':
                    outString.push_back('\f');
                    break;
                case 'n':
                    outString.push_back('\n');
                    break;
                case 'r':
                    outString.push_back('\r');
                    break;
                case 't':
                    outString.push_back('\t');
                    break;
                case 'u':
                {
                    uint32_t codePoint = 0;
                    if (!ParseUnicodeEscape(codePoint))
                    {
                        return false;
                    }
                    AppendUtf8(outString, codePoint);
                    break;
                }
                default:
                    return SetError("Unsupported escape sequence");
                }
            }

            return SetError("Unterminated string");
        }

        bool ParseUnicodeEscape(uint32_t& outCodePoint)
        {
            outCodePoint = 0;

            for (int index = 0; index < 4; ++index)
            {
                if (IsAtEnd())
                {
                    return SetError("Incomplete unicode escape");
                }

                char ch = Advance();
                outCodePoint <<= 4;
                if (ch >= '0' && ch <= '9')
                {
                    outCodePoint |= static_cast<uint32_t>(ch - '0');
                }
                else if (ch >= 'a' && ch <= 'f')
                {
                    outCodePoint |= static_cast<uint32_t>(10 + ch - 'a');
                }
                else if (ch >= 'A' && ch <= 'F')
                {
                    outCodePoint |= static_cast<uint32_t>(10 + ch - 'A');
                }
                else
                {
                    return SetError("Invalid unicode escape");
                }
            }

            return true;
        }

        void AppendUtf8(Container::String& outString, uint32_t codePoint) const
        {
            if (codePoint <= 0x7F)
            {
                outString.push_back(static_cast<char>(codePoint));
            }
            else if (codePoint <= 0x7FF)
            {
                outString.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
                outString.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else
            {
                outString.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
                outString.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                outString.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        }

        void SkipWhitespace()
        {
            while (!IsAtEnd() && std::isspace(static_cast<unsigned char>(Peek())))
            {
                Advance();
            }
        }

        bool ConsumeIf(char expected)
        {
            if (IsAtEnd() || Peek() != expected)
            {
                return false;
            }

            Advance();
            return true;
        }

        bool SetError(const char* message)
        {
            if (m_pOutError != nullptr)
            {
                *m_pOutError = Container::String(message) + " at position " +
                               Container::String(std::to_string(m_Position).c_str());
            }
            return false;
        }

        bool IsAtEnd() const
        {
            return m_Position >= m_Text.size();
        }

        char Peek() const
        {
            return m_Text[m_Position];
        }

        char Advance()
        {
            return m_Text[m_Position++];
        }

        const Container::String& m_Text;
        JsonDocument& m_Document;
        Container::String* m_pOutError = nullptr;
        size_t m_Position = 0;
    };

    bool JsonDocument::TryParse(const Container::String& text, JsonDocument& outDocument,
                                Container::String* pOutError)
    {
        JsonDocument parsedDocument;
        JsonDocumentParser parser(text, parsedDocument, pOutError);
        if (!parser.Parse())
        {
            outDocument.Reset();
            return false;
        }

        outDocument = std::move(parsedDocument);
        return true;
    }

    void JsonDocument::Reset()
    {
        m_Nodes.clear();
        m_RootNodeIndex = InvalidNodeIndex;
    }

    JsonValue JsonDocument::GetRoot() const
    {
        if (!HasRoot())
        {
            return {};
        }

        return JsonValue(this, m_RootNodeIndex);
    }

    size_t JsonDocument::CreateNode(JsonType type)
    {
        JsonNode node;
        node.Type = type;
        m_Nodes.push_back(std::move(node));
        return m_Nodes.size() - 1;
    }

} // namespace NorvesLib::Core
