#include "Text/JsonDocument.h"
#include "Text/JsonUnicodeScalar.h"
#include "Text/UnicodeText.h"
#include <charconv>

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

            const auto ch = Peek();
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
                if (ch == '-' || TextDetail::IsJsonDigit(ch))
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
            else if (TextDetail::IsJsonDigit(Peek()))
            {
                while (!IsAtEnd() && TextDetail::IsJsonDigit(Peek()))
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
                if (IsAtEnd() || !TextDetail::IsJsonDigit(Peek()))
                {
                    return SetError("Invalid fractional part");
                }

                while (!IsAtEnd() && TextDetail::IsJsonDigit(Peek()))
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

                if (IsAtEnd() || !TextDetail::IsJsonDigit(Peek()))
                {
                    return SetError("Invalid exponent");
                }

                while (!IsAtEnd() && TextDetail::IsJsonDigit(Peek()))
                {
                    Advance();
                }
            }

            Container::AnsiString numberLiteral;
            numberLiteral.reserve(m_Position - numberStart);
            for (size_t index = numberStart; index < m_Position; ++index)
            {
                numberLiteral.push_back(static_cast<char>(m_Text[index]));
            }
            double numberValue = 0.0;
            const char* begin = numberLiteral.data();
            const char* end = begin + numberLiteral.size();
            // C localeに依存せず、overflow/underflowとtoken途中までの変換を拒否する。
            const auto parsed = std::from_chars(begin, end, numberValue, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != end)
            {
                return SetError("JSON数値がdoubleの範囲外または不正です");
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

            // Stringの再確保時のNUL終端copyを避け、この文字列だけのsource長を先に確保する。
            // escapeの出力code unit数は元表現以下。ファイル残り全体を各文字列へ確保しない。
            size_t payloadLength = 0;
            bool escaped = false;
            while (payloadLength < m_Text.size() - m_Position)
            {
                const auto unit = m_Text[m_Position + payloadLength];
                if (!escaped && unit == '"')
                {
                    break;
                }
                escaped = !escaped && unit == '\\';
                ++payloadLength;
            }
            outString.clear();
            outString.reserve(payloadLength);

            while (!IsAtEnd())
            {
                const auto ch = Advance();
                if (ch == '"')
                {
                    return true;
                }

                if (static_cast<std::make_unsigned_t<Container::String::value_type>>(ch) < 0x20)
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

                const auto escape = Advance();
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
                    AppendUnicodeScalar(outString, codePoint);
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
            TextDetail::JsonUnicodeEscape decoded;
            if (!TextDetail::DecodeJsonUnicodeEscape<Container::String::value_type>(
                    {m_Text.data() + m_Position, m_Text.size() - m_Position}, decoded))
            {
                return SetError("不正または不完全なUnicodeエスケープです");
            }
            m_Position += decoded.Consumed;
            outCodePoint = decoded.Scalar;
            return true;
        }

        void AppendUnicodeScalar(Container::String& outString, uint32_t codePoint) const
        {
            const auto encoded = TextDetail::EncodeJsonUnicodeScalar<Container::String::value_type>(codePoint);
            for (size_t index = 0; index < encoded.Count; ++index)
            {
                outString.push_back(encoded.Units[index]);
            }
        }

        void SkipWhitespace()
        {
            while (!IsAtEnd() && TextDetail::IsJsonWhitespace(Peek()))
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

        Container::String::value_type Peek() const
        {
            return m_Text[m_Position];
        }

        Container::String::value_type Advance()
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
        if (!TextDetail::ForEachUnicodeScalar<Container::String::value_type>(
                {text.data(), text.size()}, [](uint32_t) {}))
        {
            outDocument.Reset();
            if (pOutError)
            {
                *pOutError = TEXT("JSON入力のUnicode表現が不正です");
            }
            return false;
        }
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

    bool JsonDocument::TryParseUtf8(Container::Span<const uint8_t> bytes, JsonDocument& outDocument,
                                    Container::String* pOutError)
    {
        Container::String text;
        const bool valid = TextDetail::ForEachUnicodeScalar<uint8_t>(bytes, [&text](uint32_t scalar)
        {
            const auto encoded = TextDetail::EncodeJsonUnicodeScalar<Container::String::value_type>(scalar);
            for (size_t index = 0; index < encoded.Count; ++index)
            {
                text.push_back(encoded.Units[index]);
            }
        });
        if (!valid)
        {
            outDocument.Reset();
            if (pOutError)
            {
                *pOutError = TEXT("JSON入力のUTF8表現が不正です");
            }
            return false;
        }
        return TryParse(text, outDocument, pOutError);
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
