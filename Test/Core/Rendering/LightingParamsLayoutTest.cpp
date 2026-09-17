#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/LightingPass.h"
#include "Rendering/RenderTypes.h"

#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core::Rendering;

namespace
{
    using TestString = NorvesLib::Core::Container::TString<char>;
#ifndef NORVES_SHADER_DIR
#error NORVES_SHADER_DIR must be defined for LightingParamsLayoutTest.
#endif

    void ConfigureAssertOutput()
    {
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    }

    TestString FormatUnsigned(uint32_t value)
    {
        char buffer[16] = {};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
        assert(result.ec == std::errc{});
        *result.ptr = '\0';
        return TestString(buffer);
    }

    TestString ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        assert(file.is_open());
        TestString result;
        char buffer[4096];
        while (file)
        {
            file.read(buffer, sizeof(buffer));
            const std::streamsize count = file.gcount();
            if (count > 0)
            {
                result.append(buffer, static_cast<TestString::size_type>(count));
            }
        }
        return result;
    }

    bool ContainsText(const TestString& source, const TestString& expected)
    {
        return source.find(expected) != TestString::npos;
    }

    std::size_t FindText(const TestString& source, const TestString& expected)
    {
        const std::size_t position = source.find(expected);
        assert(position != TestString::npos);
        return position;
    }

    std::size_t FindTextAfter(const TestString& source,
                              const TestString& expected,
                              std::size_t startPosition)
    {
        const std::size_t position = source.find(expected, startPosition);
        assert(position != TestString::npos);
        return position;
    }

    enum class ShaderLexicalState
    {
        Code,
        LineComment,
        BlockComment,
        StringLiteral,
        CharacterLiteral
    };

    bool IsIdentifierCharacter(const char character)
    {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') ||
               character == '_';
    }

    bool IsWhitespaceCharacter(const char character)
    {
        return character == ' ' || character == '\t' || character == '\r' ||
               character == '\n' || character == '\f';
    }

    TestString MaskShaderNonCode(const TestString& source)
    {
        TestString masked = source;
        ShaderLexicalState state = ShaderLexicalState::Code;
        bool bEscaped = false;

        for (std::size_t position = 0; position < source.size(); ++position)
        {
            const char character = source[position];

            if (state == ShaderLexicalState::Code)
            {
                if (character == '/' && position + 1 < source.size() &&
                    source[position + 1] == '/')
                {
                    masked[position] = ' ';
                    masked[position + 1] = ' ';
                    ++position;
                    state = ShaderLexicalState::LineComment;
                }
                else if (character == '/' && position + 1 < source.size() &&
                         source[position + 1] == '*')
                {
                    masked[position] = ' ';
                    masked[position + 1] = ' ';
                    ++position;
                    state = ShaderLexicalState::BlockComment;
                }
                else if (character == '"')
                {
                    masked[position] = ' ';
                    bEscaped = false;
                    state = ShaderLexicalState::StringLiteral;
                }
                else if (character == '\'')
                {
                    masked[position] = ' ';
                    bEscaped = false;
                    state = ShaderLexicalState::CharacterLiteral;
                }
            }
            else if (state == ShaderLexicalState::LineComment)
            {
                if (character == '\r' || character == '\n')
                {
                    state = ShaderLexicalState::Code;
                }
                else
                {
                    masked[position] = ' ';
                }
            }
            else if (state == ShaderLexicalState::BlockComment)
            {
                if (character == '*' && position + 1 < source.size() &&
                    source[position + 1] == '/')
                {
                    masked[position] = ' ';
                    masked[position + 1] = ' ';
                    ++position;
                    state = ShaderLexicalState::Code;
                }
                else if (character != '\r' && character != '\n')
                {
                    masked[position] = ' ';
                }
            }
            else
            {
                if (bEscaped)
                {
                    masked[position] =
                        (character == '\r' || character == '\n') ? character : ' ';
                    bEscaped = false;
                }
                else if (character == '\\')
                {
                    masked[position] = ' ';
                    bEscaped = true;
                }
                else if ((state == ShaderLexicalState::StringLiteral && character == '"') ||
                         (state == ShaderLexicalState::CharacterLiteral && character == '\''))
                {
                    masked[position] = ' ';
                    state = ShaderLexicalState::Code;
                }
                else if (character != '\r' && character != '\n')
                {
                    masked[position] = ' ';
                }
            }
        }

        assert(state != ShaderLexicalState::BlockComment);
        assert(state != ShaderLexicalState::StringLiteral);
        assert(state != ShaderLexicalState::CharacterLiteral);
        return masked;
    }

    std::size_t FindMatchingBrace(const TestString& source, std::size_t openingPosition)
    {
        assert(openingPosition < source.size());
        assert(source[openingPosition] == '{');

        const TestString masked = MaskShaderNonCode(source);
        std::size_t depth = 0;
        for (std::size_t position = openingPosition; position < source.size(); ++position)
        {
            if (masked[position] == '{')
            {
                ++depth;
            }
            else if (masked[position] == '}')
            {
                --depth;
                if (depth == 0)
                {
                    return position;
                }
            }
        }

        assert(false);
        return TestString::npos;
    }

    bool IsIdentifierTokenAt(const TestString& source,
                             std::size_t position,
                             const TestString& token)
    {
        if (position + token.size() > source.size() ||
            source.find(token, position) != position)
        {
            return false;
        }

        const bool bHasPreviousIdentifier =
            position > 0 && IsIdentifierCharacter(source[position - 1]);
        const std::size_t endPosition = position + token.size();
        const bool bHasNextIdentifier =
            endPosition < source.size() && IsIdentifierCharacter(source[endPosition]);
        return !bHasPreviousIdentifier && !bHasNextIdentifier;
    }

    std::size_t CountIdentifierToken(const TestString& source,
                                     const TestString& token,
                                     std::size_t beginPosition = 0,
                                     std::size_t endPosition = TestString::npos)
    {
        const std::size_t limit = endPosition == TestString::npos ? source.size() : endPosition;
        std::size_t count = 0;
        for (std::size_t position = beginPosition;
             position + token.size() <= limit;
             ++position)
        {
            if (IsIdentifierTokenAt(source, position, token))
            {
                ++count;
            }
        }

        return count;
    }

    std::size_t CountTopLevelIdentifierToken(const TestString& source,
                                             const TestString& token,
                                             std::size_t beginPosition,
                                             std::size_t endPosition)
    {
        std::size_t depth = 0;
        std::size_t count = 0;
        for (std::size_t position = beginPosition; position < endPosition; ++position)
        {
            if (source[position] == '{')
            {
                ++depth;
                continue;
            }

            if (source[position] == '}')
            {
                assert(depth > 0);
                --depth;
                continue;
            }

            if (depth == 0 && IsIdentifierTokenAt(source, position, token))
            {
                ++count;
            }
        }

        assert(depth == 0);
        return count;
    }

    std::size_t FindNextCodeCharacter(const TestString& maskedSource,
                                      char character,
                                      std::size_t beginPosition)
    {
        const std::size_t position = maskedSource.find(character, beginPosition);
        assert(position != TestString::npos);
        return position;
    }

    std::size_t FindMatchingParenthesis(const TestString& maskedSource,
                                        std::size_t openingPosition)
    {
        assert(openingPosition < maskedSource.size());
        assert(maskedSource[openingPosition] == '(');

        std::size_t depth = 0;
        for (std::size_t position = openingPosition; position < maskedSource.size(); ++position)
        {
            if (maskedSource[position] == '(')
            {
                ++depth;
            }
            else if (maskedSource[position] == ')')
            {
                assert(depth > 0);
                --depth;
                if (depth == 0)
                {
                    return position;
                }
            }
        }

        assert(false);
        return TestString::npos;
    }

    std::size_t FindCallOpeningParenthesis(const TestString& maskedSource,
                                           std::size_t functionNamePosition,
                                           const TestString& functionName)
    {
        std::size_t position = functionNamePosition + functionName.size();
        while (position < maskedSource.size() && IsWhitespaceCharacter(maskedSource[position]))
        {
            ++position;
        }

        assert(position < maskedSource.size());
        assert(maskedSource[position] == '(');
        return position;
    }

    std::size_t CountFunctionCalls(const TestString& maskedSource,
                                   const TestString& functionName)
    {
        std::size_t count = 0;
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t position = maskedSource.find(functionName, searchPosition);
            if (position == TestString::npos)
            {
                return count;
            }

            if (IsIdentifierTokenAt(maskedSource, position, functionName))
            {
                const std::size_t openingPosition =
                    FindCallOpeningParenthesis(maskedSource, position, functionName);
                (void)openingPosition;
                ++count;
            }

            searchPosition = position + functionName.size();
        }
    }

    std::size_t FindTopLevelFunctionCall(const TestString& maskedSource,
                                         const TestString& functionName,
                                         std::size_t beginPosition,
                                         std::size_t endPosition)
    {
        std::size_t depth = 0;
        for (std::size_t position = beginPosition; position < endPosition; ++position)
        {
            if (maskedSource[position] == '{')
            {
                ++depth;
                continue;
            }

            if (maskedSource[position] == '}')
            {
                assert(depth > 0);
                --depth;
                continue;
            }

            if (depth == 0 && IsIdentifierTokenAt(maskedSource, position, functionName))
            {
                const std::size_t openingPosition =
                    FindCallOpeningParenthesis(maskedSource, position, functionName);
                (void)openingPosition;
                return position;
            }
        }

        assert(false);
        return TestString::npos;
    }

    void AssertNoEndpointForbiddenTokens(const TestString& maskedSource,
                                         std::size_t beginPosition,
                                         std::size_t endPosition)
    {
        for (const TestString& token : {"debugViewMode",
                                         "DEBUG_VIEW_MODE_VALIDATION_PBR",
                                         "bValidationPBR",
                                         "if",
                                         "else",
                                         "switch",
                                         "case",
                                         "for",
                                         "while",
                                         "do"})
        {
            assert(CountIdentifierToken(maskedSource, token, beginPosition, endPosition) == 0);
        }

        assert(CountIdentifierToken(maskedSource, "254u", beginPosition, endPosition) == 0);

        for (std::size_t position = beginPosition; position < endPosition; ++position)
        {
            assert(maskedSource[position] != '?');
            assert(maskedSource[position] != ':');
        }
    }

    std::size_t CountTopLevelSamplerSamples(const TestString& maskedSource,
                                            const TestString& samplerName,
                                            std::size_t beginPosition,
                                            std::size_t endPosition)
    {
        std::size_t depth = 0;
        std::size_t count = 0;
        for (std::size_t position = beginPosition; position < endPosition; ++position)
        {
            if (maskedSource[position] == '{')
            {
                ++depth;
                continue;
            }

            if (maskedSource[position] == '}')
            {
                assert(depth > 0);
                --depth;
                continue;
            }

            if (depth != 0 ||
                (!IsIdentifierTokenAt(maskedSource, position, "texture") &&
                 !IsIdentifierTokenAt(maskedSource, position, "textureLod")))
            {
                continue;
            }

            const TestString functionName =
                IsIdentifierTokenAt(maskedSource, position, "textureLod") ? "textureLod" : "texture";
            const std::size_t openingPosition =
                FindCallOpeningParenthesis(maskedSource, position, functionName);
            std::size_t argumentPosition = openingPosition + 1;
            while (argumentPosition < endPosition &&
                   IsWhitespaceCharacter(maskedSource[argumentPosition]))
            {
                ++argumentPosition;
            }

            if (IsIdentifierTokenAt(maskedSource, argumentPosition, samplerName))
            {
                ++count;
            }
        }

        assert(depth == 0);
        return count;
    }

    std::size_t CountSamplerSamples(const TestString& maskedSource,
                                    const TestString& samplerName,
                                    std::size_t beginPosition,
                                    std::size_t endPosition)
    {
        std::size_t count = 0;
        for (std::size_t position = beginPosition; position < endPosition; ++position)
        {
            if (!IsIdentifierTokenAt(maskedSource, position, "texture") &&
                !IsIdentifierTokenAt(maskedSource, position, "textureLod"))
            {
                continue;
            }

            const TestString functionName =
                IsIdentifierTokenAt(maskedSource, position, "textureLod") ?
                    "textureLod" :
                    "texture";
            const std::size_t openingPosition =
                FindCallOpeningParenthesis(maskedSource, position, functionName);
            std::size_t argumentPosition = openingPosition + 1;
            while (argumentPosition < endPosition &&
                   IsWhitespaceCharacter(maskedSource[argumentPosition]))
            {
                ++argumentPosition;
            }

            if (IsIdentifierTokenAt(maskedSource, argumentPosition, samplerName))
            {
                ++count;
            }
        }

        return count;
    }

    std::size_t CountText(const TestString& source, const TestString& expected)
    {
        std::size_t count = 0;
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t position = source.find(expected, searchPosition);
            if (position == TestString::npos)
            {
                return count;
            }

            ++count;
            searchPosition = position + expected.size();
        }
    }

    TestString RemoveWhitespace(const TestString& source)
    {
        TestString result;
        result.reserve(source.size());
        for (const char character : source)
        {
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
            {
                result.push_back(character);
            }
        }

        return result;
    }

    struct ShaderLayoutDeclaration
    {
        std::size_t layoutBegin = TestString::npos;
        std::size_t layoutEnd = TestString::npos;
        std::size_t declarationEnd = TestString::npos;
        uint32_t set = 0;
        uint32_t binding = 0;
        bool bHasSet = false;
        bool bHasBinding = false;
        TestString resourceType;
        TestString resourceName;
    };

    std::size_t FindNextNonWhitespace(const TestString& source,
                                      std::size_t beginPosition,
                                      std::size_t endPosition)
    {
        std::size_t position = beginPosition;
        while (position < endPosition && IsWhitespaceCharacter(source[position]))
        {
            ++position;
        }

        return position;
    }

    std::size_t FindNextIdentifierStart(const TestString& source,
                                        std::size_t beginPosition,
                                        std::size_t endPosition)
    {
        std::size_t position = beginPosition;
        while (position < endPosition && !IsIdentifierCharacter(source[position]))
        {
            ++position;
        }

        return position;
    }

    TestString ReadIdentifier(const TestString& source,
                               std::size_t beginPosition,
                               std::size_t endPosition,
                               std::size_t& identifierEndPosition)
    {
        const std::size_t identifierPosition =
            FindNextIdentifierStart(source, beginPosition, endPosition);
        assert(identifierPosition < endPosition);
        identifierEndPosition = identifierPosition;
        while (identifierEndPosition < endPosition &&
               IsIdentifierCharacter(source[identifierEndPosition]))
        {
            ++identifierEndPosition;
        }

        return source.substr(identifierPosition, identifierEndPosition - identifierPosition);
    }

    uint32_t ReadUnsignedLiteral(const TestString& source,
                                 std::size_t beginPosition,
                                 std::size_t endPosition,
                                 std::size_t& literalEndPosition)
    {
        std::size_t position = FindNextNonWhitespace(source, beginPosition, endPosition);
        assert(position < endPosition);
        assert(source[position] >= '0' && source[position] <= '9');

        uint32_t value = 0;
        while (position < endPosition && source[position] >= '0' && source[position] <= '9')
        {
            value = value * 10u + static_cast<uint32_t>(source[position] - '0');
            ++position;
        }

        if (position < endPosition && source[position] == 'u')
        {
            ++position;
        }

        literalEndPosition = position;
        return value;
    }

    std::size_t FindLayoutDeclarationEnd(const TestString& maskedSource,
                                         std::size_t beginPosition)
    {
        std::size_t braceDepth = 0;
        std::size_t parenthesisDepth = 0;
        for (std::size_t position = beginPosition; position < maskedSource.size(); ++position)
        {
            if (maskedSource[position] == '{')
            {
                ++braceDepth;
            }
            else if (maskedSource[position] == '}')
            {
                assert(braceDepth > 0);
                --braceDepth;
            }
            else if (maskedSource[position] == '(')
            {
                ++parenthesisDepth;
            }
            else if (maskedSource[position] == ')')
            {
                assert(parenthesisDepth > 0);
                --parenthesisDepth;
            }
            else if (maskedSource[position] == ';' && braceDepth == 0 && parenthesisDepth == 0)
            {
                return position;
            }
        }

        assert(false);
        return TestString::npos;
    }

    void ParseLayoutQualifier(const TestString& maskedSource,
                              std::size_t beginPosition,
                              std::size_t endPosition,
                              const TestString& qualifierName,
                              bool& bHasQualifier,
                              uint32_t& qualifierValue)
    {
        std::size_t position = beginPosition;
        while (position < endPosition)
        {
            const std::size_t tokenPosition =
                FindNextIdentifierStart(maskedSource, position, endPosition);
            if (tokenPosition >= endPosition)
            {
                return;
            }

            std::size_t tokenEndPosition = tokenPosition;
            const TestString token =
                ReadIdentifier(maskedSource, tokenPosition, endPosition, tokenEndPosition);
            position = tokenEndPosition;
            if (token != qualifierName)
            {
                continue;
            }

            const std::size_t equalsPosition =
                FindNextNonWhitespace(maskedSource, position, endPosition);
            if (equalsPosition >= endPosition || maskedSource[equalsPosition] != '=')
            {
                continue;
            }

            assert(!bHasQualifier);
            std::size_t literalEndPosition = equalsPosition + 1;
            qualifierValue = ReadUnsignedLiteral(maskedSource,
                                                  equalsPosition + 1,
                                                  endPosition,
                                                  literalEndPosition);
            bHasQualifier = true;
            position = literalEndPosition;
        }
    }

    std::size_t FindIdentifierToken(const TestString& source,
                                    const TestString& token,
                                    std::size_t beginPosition,
                                    std::size_t endPosition)
    {
        std::size_t position = beginPosition;
        while (position < endPosition)
        {
            position = source.find(token, position);
            if (position == TestString::npos || position >= endPosition)
            {
                return TestString::npos;
            }

            if (IsIdentifierTokenAt(source, position, token))
            {
                return position;
            }

            position += token.size();
        }

        return TestString::npos;
    }

    std::size_t ParseShaderLayoutDeclarations(const TestString& source,
                                              ShaderLayoutDeclaration* declarations,
                                              std::size_t declarationCapacity)
    {
        const TestString maskedSource = MaskShaderNonCode(source);
        std::size_t declarationCount = 0;
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t layoutPosition =
                FindIdentifierToken(maskedSource,
                                    "layout",
                                    searchPosition,
                                    maskedSource.size());
            if (layoutPosition == TestString::npos)
            {
                break;
            }

            const std::size_t openingPosition =
                FindNextNonWhitespace(maskedSource,
                                      layoutPosition + TestString("layout").size(),
                                      maskedSource.size());
            assert(openingPosition < maskedSource.size());
            assert(maskedSource[openingPosition] == '(');
            const std::size_t closingPosition =
                FindMatchingParenthesis(maskedSource, openingPosition);
            const std::size_t declarationEnd =
                FindLayoutDeclarationEnd(maskedSource, closingPosition + 1);

            ShaderLayoutDeclaration declaration;
            declaration.layoutBegin = layoutPosition;
            declaration.layoutEnd = closingPosition + 1;
            declaration.declarationEnd = declarationEnd;
            ParseLayoutQualifier(maskedSource,
                                 openingPosition + 1,
                                 closingPosition,
                                 "set",
                                 declaration.bHasSet,
                                 declaration.set);
            ParseLayoutQualifier(maskedSource,
                                 openingPosition + 1,
                                 closingPosition,
                                 "binding",
                                 declaration.bHasBinding,
                                 declaration.binding);

            const std::size_t samplerTypePosition =
                FindIdentifierToken(maskedSource,
                                    "sampler2D",
                                    closingPosition + 1,
                                    declarationEnd);
            if (samplerTypePosition != TestString::npos)
            {
                declaration.resourceType = "sampler2D";
                std::size_t resourceNameEndPosition = samplerTypePosition;
                declaration.resourceName = ReadIdentifier(maskedSource,
                                                          samplerTypePosition +
                                                              TestString("sampler2D").size(),
                                                          declarationEnd,
                                                          resourceNameEndPosition);
            }
            else
            {
                const std::size_t bufferTypePosition =
                    FindIdentifierToken(maskedSource,
                                        "buffer",
                                        closingPosition + 1,
                                        declarationEnd);
                if (bufferTypePosition != TestString::npos)
                {
                    declaration.resourceType = "buffer";
                    std::size_t blockNameEndPosition = bufferTypePosition;
                    const TestString blockName =
                        ReadIdentifier(maskedSource,
                                       bufferTypePosition + TestString("buffer").size(),
                                       declarationEnd,
                                       blockNameEndPosition);
                    const std::size_t closingBlockPosition =
                        maskedSource.substr(0, declarationEnd + 1).FindLast('}');
                    assert(closingBlockPosition != TestString::npos);
                    std::size_t resourceNameEndPosition = closingBlockPosition + 1;
                    const TestString instanceName =
                        ReadIdentifier(maskedSource,
                                       closingBlockPosition + 1,
                                       declarationEnd,
                                       resourceNameEndPosition);
                    declaration.resourceName = instanceName.empty() ? blockName : instanceName;
                }
                else
                {
                    const std::size_t uniformTypePosition =
                        FindIdentifierToken(maskedSource,
                                            "uniform",
                                            closingPosition + 1,
                                            declarationEnd);
                    if (uniformTypePosition != TestString::npos)
                    {
                        declaration.resourceType = "uniform";
                        const std::size_t closingBlockPosition =
                            maskedSource.substr(0, declarationEnd + 1).FindLast('}');
                        if (closingBlockPosition != TestString::npos)
                        {
                            std::size_t resourceNameEndPosition = closingBlockPosition + 1;
                            declaration.resourceName =
                                ReadIdentifier(maskedSource,
                                               closingBlockPosition + 1,
                                               declarationEnd,
                                               resourceNameEndPosition);
                        }
                        else
                        {
                            std::size_t resourceNameEndPosition = uniformTypePosition;
                            declaration.resourceName =
                                ReadIdentifier(maskedSource,
                                               uniformTypePosition + TestString("uniform").size(),
                                               declarationEnd,
                                               resourceNameEndPosition);
                        }
                    }
                }
            }

            assert(declarationCount < declarationCapacity);
            declarations[declarationCount] = declaration;
            ++declarationCount;
            searchPosition = declarationEnd + 1;
        }

        return declarationCount;
    }

    ShaderLayoutDeclaration FindShaderLayoutDeclaration(const TestString& source,
                                                        uint32_t binding)
    {
        ShaderLayoutDeclaration declarations[32];
        const std::size_t declarationCount =
            ParseShaderLayoutDeclarations(source, declarations, 32);
        ShaderLayoutDeclaration result;
        std::size_t matchCount = 0;
        for (std::size_t index = 0; index < declarationCount; ++index)
        {
            if (declarations[index].bHasSet && declarations[index].set == 0 &&
                declarations[index].bHasBinding && declarations[index].binding == binding)
            {
                result = declarations[index];
                ++matchCount;
            }
        }

        assert(matchCount == 1);
        return result;
    }

    void AssertShaderBinding(const TestString& source, uint32_t binding)
    {
        ShaderLayoutDeclaration declarations[32];
        const std::size_t declarationCount =
            ParseShaderLayoutDeclarations(source, declarations, 32);
        std::size_t matchCount = 0;
        for (std::size_t index = 0; index < declarationCount; ++index)
        {
            if (declarations[index].bHasSet && declarations[index].set == 0 &&
                declarations[index].bHasBinding && declarations[index].binding == binding)
            {
                ++matchCount;
            }
        }

        assert(matchCount == 1);
    }

    void AssertProductionDescriptorBinding(const TestString& source, uint32_t binding)
    {
        const TestString normalizedSource = RemoveWhitespace(MaskShaderNonCode(source));
        const TestString bindingText = ".binding=" + FormatUnsigned(binding) + ";";
        assert(CountText(normalizedSource, bindingText) == 1);
    }

    void AssertProductionDescriptorType(const TestString& source,
                                        uint32_t binding,
                                        const TestString& expectedType)
    {
        const TestString normalizedSource = RemoveWhitespace(MaskShaderNonCode(source));
        const TestString bindingText = ".binding=" + FormatUnsigned(binding) + ";";
        const std::size_t bindingPosition = FindText(normalizedSource, bindingText);
        const std::size_t nextBindingDeclaration =
            normalizedSource.find("RHI::DescriptorBinding", bindingPosition + bindingText.size());
        const std::size_t typePosition =
            normalizedSource.find(".type=", bindingPosition + bindingText.size());
        assert(typePosition != TestString::npos);
        assert(nextBindingDeclaration == TestString::npos ||
               typePosition < nextBindingDeclaration);
        const TestString typeText =
            ".type=RHI::ResourceBindType::" + expectedType + ";";
        assert(normalizedSource.find(typeText, typePosition) == typePosition);
    }

    struct ShaderStatement
    {
        std::size_t begin = TestString::npos;
        std::size_t end = TestString::npos;
        TestString raw;
        TestString normalized;
        TestString lhs;
        TestString rhs;
    };

    TestString ExtractLastIdentifierBefore(const TestString& source,
                                            std::size_t endPosition)
    {
        std::size_t position = endPosition;
        while (position > 0 && !IsIdentifierCharacter(source[position - 1]))
        {
            --position;
        }

        const std::size_t identifierEndPosition = position;
        while (position > 0 && IsIdentifierCharacter(source[position - 1]))
        {
            --position;
        }

        assert(identifierEndPosition > position);
        return source.substr(position, identifierEndPosition - position);
    }

    TestString ExtractAssignmentLhs(const TestString& rawStatement)
    {
        const std::size_t equalsPosition = rawStatement.find('=');
        if (equalsPosition == TestString::npos)
        {
            return {};
        }

        return ExtractLastIdentifierBefore(rawStatement, equalsPosition);
    }

    TestString ExtractAssignmentRhs(const TestString& normalizedStatement)
    {
        const std::size_t equalsPosition = normalizedStatement.find('=');
        if (equalsPosition == TestString::npos)
        {
            return {};
        }

        return normalizedStatement.substr(equalsPosition + 1);
    }

    template <typename Visitor>
    void VisitTopLevelStatements(const TestString& maskedSource,
                                 std::size_t beginPosition,
                                 std::size_t endPosition,
                                 Visitor&& visitor)
    {
        std::size_t statementBegin = beginPosition;
        std::size_t parenthesisDepth = 0;
        std::size_t braceDepth = 0;
        for (std::size_t position = beginPosition; position < endPosition; ++position)
        {
            if (maskedSource[position] == '(')
            {
                ++parenthesisDepth;
            }
            else if (maskedSource[position] == ')')
            {
                assert(parenthesisDepth > 0);
                --parenthesisDepth;
            }
            else if (maskedSource[position] == '{')
            {
                ++braceDepth;
            }
            else if (maskedSource[position] == '}')
            {
                assert(braceDepth > 0);
                --braceDepth;
            }
            else if (maskedSource[position] == ';' && parenthesisDepth == 0 && braceDepth == 0)
            {
                const TestString rawStatement =
                    maskedSource.substr(statementBegin, position + 1 - statementBegin);
                const TestString normalizedStatement = RemoveWhitespace(rawStatement);
                if (!normalizedStatement.empty())
                {
                    visitor(statementBegin,
                            position + 1,
                            rawStatement,
                            normalizedStatement);
                }

                statementBegin = position + 1;
            }
        }

        assert(parenthesisDepth == 0);
        assert(braceDepth == 0);
        const TestString rawStatement =
            maskedSource.substr(statementBegin, endPosition - statementBegin);
        const TestString normalizedStatement = RemoveWhitespace(rawStatement);
        if (!normalizedStatement.empty())
        {
            visitor(statementBegin, endPosition, rawStatement, normalizedStatement);
        }
    }

    template <typename Predicate>
    std::size_t CountStatements(const TestString& maskedSource,
                                std::size_t beginPosition,
                                std::size_t endPosition,
                                Predicate&& predicate)
    {
        std::size_t count = 0;
        VisitTopLevelStatements(maskedSource,
                                beginPosition,
                                endPosition,
                                [&](std::size_t,
                                    std::size_t,
                                    const TestString& rawStatement,
                                    const TestString& normalizedStatement)
                                {
                                    if (predicate(rawStatement, normalizedStatement))
                                    {
                                        ++count;
                                    }
                                });
        return count;
    }

    template <typename Predicate>
    ShaderStatement FindSingleStatementMatching(const TestString& maskedSource,
                                                std::size_t beginPosition,
                                                std::size_t endPosition,
                                                Predicate&& predicate)
    {
        ShaderStatement result;
        std::size_t count = 0;
        VisitTopLevelStatements(maskedSource,
                                beginPosition,
                                endPosition,
                                [&](std::size_t statementBegin,
                                    std::size_t statementEnd,
                                    const TestString& rawStatement,
                                    const TestString& normalizedStatement)
                                {
                                    if (!predicate(rawStatement, normalizedStatement))
                                    {
                                        return;
                                    }

                                    ++count;
                                    result.begin = statementBegin;
                                    result.end = statementEnd;
                                    result.raw = rawStatement;
                                    result.normalized = normalizedStatement;
                                    result.lhs = ExtractAssignmentLhs(rawStatement);
                                    result.rhs = ExtractAssignmentRhs(normalizedStatement);
                                });
        if (count != 1)
        {
            std::cerr << "FindSingleStatementMatching count=" << count
                      << " begin=" << beginPosition << " end=" << endPosition << "\n";
        }
        assert(count == 1);
        return result;
    }

    std::size_t CountTokenInStatementRange(const TestString& maskedSource,
                                           std::size_t beginPosition,
                                           std::size_t endPosition,
                                           const TestString& token)
    {
        return CountStatements(maskedSource,
                                beginPosition,
                                endPosition,
                                [&](const TestString&, const TestString& normalizedStatement)
                                {
                                    return normalizedStatement.find(token) != TestString::npos;
                                });
    }

    std::size_t SplitFunctionArguments(const TestString& normalizedCall,
                                       std::size_t openingPosition,
                                       std::size_t closingPosition,
                                       TestString* arguments,
                                       std::size_t argumentCapacity)
    {
        std::size_t argumentCount = 0;
        std::size_t argumentBegin = openingPosition + 1;
        std::size_t parenthesisDepth = 0;
        for (std::size_t position = argumentBegin; position < closingPosition; ++position)
        {
            if (normalizedCall[position] == '(')
            {
                ++parenthesisDepth;
            }
            else if (normalizedCall[position] == ')')
            {
                assert(parenthesisDepth > 0);
                --parenthesisDepth;
            }
            else if (normalizedCall[position] == ',' && parenthesisDepth == 0)
            {
                assert(argumentCount < argumentCapacity);
                arguments[argumentCount] =
                    normalizedCall.substr(argumentBegin, position - argumentBegin);
                ++argumentCount;
                argumentBegin = position + 1;
            }
        }

        if (argumentBegin < closingPosition)
        {
            assert(argumentCount < argumentCapacity);
            arguments[argumentCount] =
                normalizedCall.substr(argumentBegin, closingPosition - argumentBegin);
            ++argumentCount;
        }

        return argumentCount;
    }

    bool ContainsEpsilonLiteral(const TestString& source)
    {
        return ContainsText(source, "0.0001") || ContainsText(source, "1e-4") ||
               ContainsText(source, "1E-4");
    }

    void AssertDirectSmithFormula(const TestString& maskedSource)
    {
        const std::size_t functionPosition =
            FindIdentifierToken(maskedSource,
                                "GeometrySchlickGGX",
                                0,
                                maskedSource.size());
        assert(functionPosition != TestString::npos);
        const std::size_t openingParenthesis =
            FindCallOpeningParenthesis(maskedSource,
                                       functionPosition,
                                       "GeometrySchlickGGX");
        const std::size_t openingBrace =
            FindNextCodeCharacter(maskedSource, '{', openingParenthesis + 1);
        const std::size_t closingBrace =
            FindMatchingBrace(maskedSource, openingBrace);
        const TestString body =
            RemoveWhitespace(maskedSource.substr(openingBrace + 1,
                                                 closingBrace - openingBrace - 1));
        assert(CountText(body, "roughness+1.0") == 1);
        assert(CountText(body, "/8.0") == 1 || CountText(body, "/8") == 1);
    }

    void AssertEndpointHelperDataflow(const TestString& maskedSource,
                                      std::size_t helperBodyBegin,
                                      std::size_t helperBodyEnd)
    {
        const ShaderStatement f0dStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("=vec3(0.04)") !=
                                                       TestString::npos &&
                                                   normalized.find("F0d=") !=
                                                       TestString::npos;
                                        });
        const ShaderStatement f0cStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("F0c=") !=
                                                       TestString::npos &&
                                                   normalized.find("albedo") !=
                                                       TestString::npos &&
                                                   normalized.find("mix(") ==
                                                       TestString::npos;
                                        });
        assert(f0dStatement.lhs == "F0d");
        assert(f0cStatement.lhs == "F0c");
        assert(f0dStatement.lhs != f0cStatement.lhs);

        const ShaderStatement essStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("Ess=") != TestString::npos &&
                                                   normalized.find("dfg.x") != TestString::npos &&
                                                   normalized.find("dfg.y") != TestString::npos &&
                                                   normalized.find("max(") != TestString::npos &&
                                                   ContainsEpsilonLiteral(normalized);
                                        });
        assert(essStatement.lhs == "Ess");

        const std::size_t inlineDirectSmithStatementCount =
            CountStatements(maskedSource,
                            helperBodyBegin,
                            helperBodyEnd,
                            [](const TestString&, const TestString& normalized)
                            {
                                return normalized.find("roughness+1.0") !=
                                           TestString::npos &&
                                       (normalized.find("/8.0") != TestString::npos ||
                                        normalized.find("/8") != TestString::npos);
                            });
        if (inlineDirectSmithStatementCount == 0)
        {
            const std::size_t directSmithRCount =
                CountTokenInStatementRange(maskedSource,
                                            helperBodyBegin,
                                            helperBodyEnd,
                                            "roughness+1.0");
            const std::size_t directSmithKCount =
                CountStatements(maskedSource,
                                helperBodyBegin,
                                helperBodyEnd,
                                [](const TestString&, const TestString& normalized)
                                {
                                    return normalized.find("/8.0") != TestString::npos ||
                                           normalized.find("/8") != TestString::npos;
                                });
            if (directSmithRCount == 1 && directSmithKCount == 1)
            {
                assert(CountIdentifierToken(maskedSource,
                                            "roughness",
                                            helperBodyBegin,
                                            helperBodyEnd) >= 1);
            }
            else
            {
                assert(CountIdentifierToken(maskedSource,
                                            "GeometrySmith",
                                            helperBodyBegin,
                                            helperBodyEnd) == 1);
                AssertDirectSmithFormula(maskedSource);
            }
        }
        else
        {
            assert(inlineDirectSmithStatementCount == 1);
            assert(CountTokenInStatementRange(maskedSource,
                                               helperBodyBegin,
                                               helperBodyEnd,
                                               "roughness+1.0") == 1);
        }

        const ShaderStatement commonStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("=D") != TestString::npos &&
                                                   normalized.find("G") != TestString::npos &&
                                                   normalized.find("NdotV") != TestString::npos &&
                                                   normalized.find("NdotL") != TestString::npos &&
                                                   normalized.find("4.0") != TestString::npos &&
                                                   ContainsEpsilonLiteral(normalized);
                                        });
        assert(commonStatement.lhs != "");
        assert(commonStatement.normalized.find("NdotV") != TestString::npos);
        assert(commonStatement.normalized.find("NdotL") != TestString::npos);
        assert(commonStatement.normalized.find("4.0*NdotV*NdotL") !=
                   TestString::npos ||
               commonStatement.normalized.find("4*NdotV*NdotL") != TestString::npos);

        const ShaderStatement fresnelDStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [&](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("FresnelSchlick") !=
                                                       TestString::npos &&
                                                   normalized.find(f0dStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(f0cStatement.lhs) ==
                                                       TestString::npos;
                                        });
        const ShaderStatement fresnelCStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [&](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("FresnelSchlick") !=
                                                       TestString::npos &&
                                                   normalized.find(f0cStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(f0dStatement.lhs) ==
                                                       TestString::npos;
                                        });
        assert(fresnelDStatement.lhs != "");
        assert(fresnelCStatement.lhs != "");
        assert(fresnelDStatement.lhs != fresnelCStatement.lhs);
        assert(fresnelDStatement.normalized.find("VdotH") != TestString::npos ||
               fresnelDStatement.normalized.find("dot(H,V)") != TestString::npos ||
               fresnelDStatement.normalized.find("dot(V,H)") != TestString::npos);
        assert(fresnelCStatement.normalized.find("VdotH") != TestString::npos ||
               fresnelCStatement.normalized.find("dot(H,V)") != TestString::npos ||
               fresnelCStatement.normalized.find("dot(V,H)") != TestString::npos);

        const ShaderStatement compDStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [&](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("=") != TestString::npos &&
                                                   normalized.find(f0dStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(essStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(f0cStatement.lhs) ==
                                                       TestString::npos;
                                        });
        const ShaderStatement compCStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [&](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("=") != TestString::npos &&
                                                   normalized.find(f0cStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(essStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(f0dStatement.lhs) ==
                                                       TestString::npos;
                                        });
        assert(compDStatement.lhs != "");
        assert(compCStatement.lhs != "");
        assert(compDStatement.lhs != compCStatement.lhs);
        assert(compDStatement.normalized.find("1.0-Ess") != TestString::npos);
        assert(compCStatement.normalized.find("1.0-Ess") != TestString::npos);
        assert(compDStatement.normalized.find("/") != TestString::npos);
        assert(compCStatement.normalized.find("/") != TestString::npos);

        const ShaderStatement dielectricSpecStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [&](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("=") != TestString::npos &&
                                                   normalized.find(fresnelDStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(compDStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(fresnelCStatement.lhs) ==
                                                       TestString::npos;
                                        });
        const ShaderStatement conductorSpecStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [&](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("=") != TestString::npos &&
                                                   normalized.find(fresnelCStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(compCStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(fresnelDStatement.lhs) ==
                                                       TestString::npos;
                                        });
        assert(dielectricSpecStatement.lhs != "");
        assert(conductorSpecStatement.lhs != "");
        assert(dielectricSpecStatement.lhs != conductorSpecStatement.lhs);
        assert(dielectricSpecStatement.begin > compDStatement.end);
        assert(conductorSpecStatement.begin > compCStatement.end);

        const ShaderStatement diffuseEndpointStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [&](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("=") != TestString::npos &&
                                                   normalized.find("albedo") != TestString::npos &&
                                                   normalized.find("PI") != TestString::npos &&
                                                   normalized.find(fresnelDStatement.lhs) !=
                                                       TestString::npos &&
                                                   normalized.find(compDStatement.lhs) ==
                                                       TestString::npos &&
                                                   normalized.find(compCStatement.lhs) ==
                                                       TestString::npos;
                                        });
        assert(diffuseEndpointStatement.lhs != "");

        const ShaderStatement diffuseOutputStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("diffuseBrdf=") !=
                                                       TestString::npos;
                                        });
        const ShaderStatement specularOutputStatement =
            FindSingleStatementMatching(maskedSource,
                                        helperBodyBegin,
                                        helperBodyEnd,
                                        [](const TestString&, const TestString& normalized)
                                        {
                                            return normalized.find("specularBrdf=") !=
                                                       TestString::npos;
                                        });
        assert(diffuseOutputStatement.begin >= diffuseEndpointStatement.end);
        assert(specularOutputStatement.begin >= dielectricSpecStatement.end);
        assert(specularOutputStatement.begin >= conductorSpecStatement.end);

        const auto AssertMetallicEndpointBlend =
            [&](const ShaderStatement& output,
                const TestString& requiredEndpointA,
                const TestString& requiredEndpointB)
            {
                assert(output.normalized.find("metallic") != TestString::npos);
                assert(output.normalized.find("1.0-metallic") != TestString::npos ||
                       output.normalized.find("(1-metallic)") != TestString::npos);
                assert(output.normalized.find(requiredEndpointA) != TestString::npos);
                assert(output.normalized.find(requiredEndpointB) != TestString::npos);
                assert(output.normalized.find("mix(") == TestString::npos);
            };
        AssertMetallicEndpointBlend(diffuseOutputStatement,
                                    diffuseEndpointStatement.lhs,
                                    diffuseEndpointStatement.lhs);
        AssertMetallicEndpointBlend(specularOutputStatement,
                                    dielectricSpecStatement.lhs,
                                    conductorSpecStatement.lhs);

        assert(diffuseOutputStatement.normalized.find(compDStatement.lhs) ==
               TestString::npos);
        assert(diffuseOutputStatement.normalized.find(compCStatement.lhs) ==
               TestString::npos);
        assert(CountStatements(maskedSource,
                               helperBodyBegin,
                               helperBodyEnd,
                               [&](const TestString&, const TestString& normalized)
                               {
                                   return normalized.find("mix(") != TestString::npos ||
                                          (normalized.find(f0dStatement.lhs) !=
                                               TestString::npos &&
                                           normalized.find(f0cStatement.lhs) !=
                                               TestString::npos);
                               }) == 0);
    }

    void AssertNoTopLevelControlFlow(const TestString& maskedSource,
                                     std::size_t beginPosition,
                                     std::size_t endPosition)
    {
        for (const TestString& token : {"if", "else", "switch", "case", "for", "while", "do"})
        {
            assert(CountTopLevelIdentifierToken(maskedSource,
                                                token,
                                                beginPosition,
                                                endPosition) == 0);
        }

        for (std::size_t position = beginPosition; position < endPosition; ++position)
        {
            assert(maskedSource[position] != '?');
            assert(maskedSource[position] != ':');
        }
    }

    struct SourceRange
    {
        std::size_t begin = TestString::npos;
        std::size_t end = TestString::npos;
    };

    bool IsPositionInSourceRange(std::size_t position, const SourceRange& range)
    {
        return range.begin != TestString::npos && range.end != TestString::npos &&
               position >= range.begin && position < range.end;
    }

    bool IsPositionInAnySourceRange(const std::size_t position,
                                    const SourceRange* ranges,
                                    std::size_t rangeCount)
    {
        for (std::size_t index = 0; index < rangeCount; ++index)
        {
            if (IsPositionInSourceRange(position, ranges[index]))
            {
                return true;
            }
        }

        return false;
    }

    void AssertTokenOnlyInRanges(const TestString& maskedSource,
                                 const TestString& token,
                                 const SourceRange* allowedRanges,
                                 std::size_t allowedRangeCount)
    {
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t tokenPosition =
                FindIdentifierToken(maskedSource,
                                    token,
                                    searchPosition,
                                    maskedSource.size());
            if (tokenPosition == TestString::npos)
            {
                return;
            }

            assert(IsPositionInAnySourceRange(tokenPosition,
                                              allowedRanges,
                                              allowedRangeCount));
            searchPosition = tokenPosition + token.size();
        }
    }

    bool IsSimpleAssignmentTo(const TestString& maskedSource,
                              std::size_t tokenPosition,
                              const TestString& token)
    {
        if (!IsIdentifierTokenAt(maskedSource, tokenPosition, token))
        {
            return false;
        }

        std::size_t equalsPosition = tokenPosition + token.size();
        while (equalsPosition < maskedSource.size() &&
               IsWhitespaceCharacter(maskedSource[equalsPosition]))
        {
            ++equalsPosition;
        }

        return equalsPosition < maskedSource.size() && maskedSource[equalsPosition] == '=' &&
               (equalsPosition + 1 >= maskedSource.size() ||
                maskedSource[equalsPosition + 1] != '=');
    }

    SourceRange FindStatementRangeContaining(const TestString& maskedSource,
                                             std::size_t tokenPosition)
    {
        std::size_t beginPosition = tokenPosition;
        while (beginPosition > 0 && maskedSource[beginPosition - 1] != ';' &&
               maskedSource[beginPosition - 1] != '{' &&
               maskedSource[beginPosition - 1] != '}')
        {
            --beginPosition;
        }

        if (beginPosition < maskedSource.size() &&
            (maskedSource[beginPosition] == ';' || maskedSource[beginPosition] == '{' ||
             maskedSource[beginPosition] == '}'))
        {
            ++beginPosition;
        }

        std::size_t endPosition = maskedSource.find(';', tokenPosition);
        assert(endPosition != TestString::npos);
        ++endPosition;
        return {beginPosition, endPosition};
    }

    SourceRange FindSingleSimpleAssignmentRange(const TestString& maskedSource,
                                                const TestString& token)
    {
        SourceRange result;
        std::size_t assignmentCount = 0;
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t tokenPosition =
                FindIdentifierToken(maskedSource,
                                    token,
                                    searchPosition,
                                    maskedSource.size());
            if (tokenPosition == TestString::npos)
            {
                break;
            }

            if (IsSimpleAssignmentTo(maskedSource, tokenPosition, token))
            {
                ++assignmentCount;
                result = FindStatementRangeContaining(maskedSource, tokenPosition);
            }
            searchPosition = tokenPosition + token.size();
        }

        assert(assignmentCount == 1);
        return result;
    }

    TestString NormalizeRange(const TestString& maskedSource, const SourceRange& range)
    {
        assert(range.begin <= range.end);
        return RemoveWhitespace(maskedSource.substr(range.begin, range.end - range.begin));
    }

    bool ContainsComparisonToDebugConstant(const TestString& maskedSource,
                                           std::size_t tokenPosition,
                                           const TestString& constantName)
    {
        const std::size_t comparisonPosition =
            maskedSource.substr(0, tokenPosition + 1).FindLast("params.debugViewMode");
        if (comparisonPosition == TestString::npos)
        {
            return false;
        }

        std::size_t statementBoundary = TestString::npos;
        for (std::size_t scanPosition = tokenPosition + 1; scanPosition > 0; --scanPosition)
        {
            const char candidate = maskedSource[scanPosition - 1];
            if (candidate == ';' || candidate == '{' || candidate == '}')
            {
                statementBoundary = scanPosition - 1;
                break;
            }
        }
        if (statementBoundary != TestString::npos &&
            comparisonPosition < statementBoundary)
        {
            return false;
        }

        const TestString comparisonPrefix =
            maskedSource.substr(comparisonPosition,
                                tokenPosition + constantName.size() - comparisonPosition);
        const TestString normalizedPrefix = RemoveWhitespace(comparisonPrefix);
        return normalizedPrefix.find("params.debugViewMode==" + constantName) !=
               TestString::npos;
    }

    void AssertRaw254LexicalPolicy(const TestString& maskedSource,
                                   const TestString& validationPbrMode,
                                   const TestString& commonBranchCondition,
                                   std::size_t commonBranchPosition,
                                   std::size_t commonBranchConditionEnd,
                                   std::size_t commonBranchOpeningBrace)
    {
        const TestString normalizedSource = RemoveWhitespace(maskedSource);
        const TestString normalizedCommonBranchCondition =
            RemoveWhitespace(commonBranchCondition);
        const std::size_t commonConditionOpeningPosition =
            normalizedCommonBranchCondition.find('(');
        const std::size_t commonConditionClosingPosition =
            normalizedCommonBranchCondition.FindLast(')');
        assert(commonConditionOpeningPosition != TestString::npos);
        assert(commonConditionClosingPosition > commonConditionOpeningPosition);
        const TestString normalizedCommonCondition =
            normalizedCommonBranchCondition.substr(
                commonConditionOpeningPosition + 1,
                commonConditionClosingPosition - commonConditionOpeningPosition - 1);
        const TestString constantDefinition =
            "constuintDEBUG_VIEW_MODE_VALIDATION_PBR=254u;";
        assert(CountText(normalizedSource, constantDefinition) == 1);
        assert(CountIdentifierToken(maskedSource, "254u") == 1);
        assert(CountIdentifierToken(maskedSource,
                                    "DEBUG_VIEW_MODE_VALIDATION_PBR") >= 2);

        const SourceRange validationAssignment =
            FindSingleSimpleAssignmentRange(maskedSource, "bValidationPBR");
        const TestString validationAssignmentText =
            NormalizeRange(maskedSource, validationAssignment);
        assert(validationAssignmentText.find("bValidationPBR=") == 0 ||
               validationAssignmentText.find("boolbValidationPBR=") == 0);
        assert(validationAssignmentText.find("params.debugViewMode==" + validationPbrMode) !=
               TestString::npos);

        const SourceRange commonConditionRange =
            {commonBranchPosition, commonBranchConditionEnd};
        assert(NormalizeRange(maskedSource, commonConditionRange).find(
                   RemoveWhitespace(commonBranchCondition)) != TestString::npos);

        SourceRange ambientGuardRange;
        std::size_t ambientGuardCount = 0;
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t ifPosition =
                FindIdentifierToken(maskedSource,
                                    "if",
                                    searchPosition,
                                    maskedSource.size());
            if (ifPosition == TestString::npos)
            {
                break;
            }

            std::size_t openingPosition = ifPosition + TestString("if").size();
            while (openingPosition < maskedSource.size() &&
                   IsWhitespaceCharacter(maskedSource[openingPosition]))
            {
                ++openingPosition;
            }
            assert(openingPosition < maskedSource.size());
            assert(maskedSource[openingPosition] == '(');
            const std::size_t closingPosition =
                FindMatchingParenthesis(maskedSource, openingPosition);
            const TestString condition =
                RemoveWhitespace(maskedSource.substr(openingPosition + 1,
                                                     closingPosition - openingPosition - 1));
            if (condition == "!bValidationLambert&&!bValidationPBR")
            {
                ++ambientGuardCount;
                ambientGuardRange = {openingPosition + 1, closingPosition};
            }
            searchPosition = closingPosition + 1;
        }
        assert(ambientGuardCount == 1);

        const SourceRange allowedValidationRanges[] = {
            validationAssignment,
            commonConditionRange,
            ambientGuardRange};
        AssertTokenOnlyInRanges(maskedSource,
                                "bValidationPBR",
                                allowedValidationRanges,
                                sizeof(allowedValidationRanges) /
                                    sizeof(allowedValidationRanges[0]));

        std::size_t debugConstantSearchPosition = 0;
        while (true)
        {
            const std::size_t constantPosition =
                FindIdentifierToken(maskedSource,
                                    "DEBUG_VIEW_MODE_VALIDATION_PBR",
                                    debugConstantSearchPosition,
                                    maskedSource.size());
            if (constantPosition == TestString::npos)
            {
                break;
            }

            const SourceRange constantStatement =
                FindStatementRangeContaining(maskedSource, constantPosition);
            const bool bInConstantDefinition =
                NormalizeRange(maskedSource, constantStatement) == constantDefinition;
            const bool bInValidationAssignment =
                IsPositionInSourceRange(constantPosition, validationAssignment);
            const bool bInDebugModeComparison =
                ContainsComparisonToDebugConstant(maskedSource,
                                                 constantPosition,
                                                 "DEBUG_VIEW_MODE_VALIDATION_PBR");
            const SourceRange debugStatement =
                FindStatementRangeContaining(maskedSource, constantPosition);
            const TestString normalizedDebugStatement =
                NormalizeRange(maskedSource, debugStatement);
            const bool bInPreExposureReturn =
                normalizedDebugStatement.find("return") != TestString::npos &&
                bInDebugModeComparison;
            assert(bInConstantDefinition || bInValidationAssignment ||
                   bInPreExposureReturn);
            debugConstantSearchPosition = constantPosition +
                                          TestString("DEBUG_VIEW_MODE_VALIDATION_PBR").size();
        }

        std::size_t ifSearchPosition = 0;
        while (true)
        {
            const std::size_t ifPosition =
                FindIdentifierToken(maskedSource,
                                    "if",
                                    ifSearchPosition,
                                    maskedSource.size());
            if (ifPosition == TestString::npos)
            {
                break;
            }

            std::size_t openingPosition = ifPosition + TestString("if").size();
            while (openingPosition < maskedSource.size() &&
                   IsWhitespaceCharacter(maskedSource[openingPosition]))
            {
                ++openingPosition;
            }
            assert(openingPosition < maskedSource.size());
            assert(maskedSource[openingPosition] == '(');
            const std::size_t closingPosition =
                FindMatchingParenthesis(maskedSource, openingPosition);
            const TestString condition =
                RemoveWhitespace(maskedSource.substr(openingPosition + 1,
                                                     closingPosition - openingPosition - 1));
            const bool bHasRaw254Token =
                CountIdentifierToken(maskedSource,
                                     "254u",
                                     openingPosition + 1,
                                     closingPosition) != 0 ||
                CountIdentifierToken(maskedSource,
                                     "DEBUG_VIEW_MODE_VALIDATION_PBR",
                                     openingPosition + 1,
                                     closingPosition) != 0 ||
                CountIdentifierToken(maskedSource,
                                     "bValidationPBR",
                                     openingPosition + 1,
                                     closingPosition) != 0;
            if (bHasRaw254Token)
            {
                assert(condition == normalizedCommonCondition ||
                       condition == "!bValidationLambert&&!bValidationPBR");
                std::size_t bodyOpeningPosition = closingPosition + 1;
                while (bodyOpeningPosition < maskedSource.size() &&
                       IsWhitespaceCharacter(maskedSource[bodyOpeningPosition]))
                {
                    ++bodyOpeningPosition;
                }
                if (condition == normalizedCommonCondition)
                {
                    assert(bodyOpeningPosition == commonBranchOpeningBrace);
                }
                else
                {
                    assert(bodyOpeningPosition < maskedSource.size());
                }
            }
            ifSearchPosition = closingPosition + 1;
        }

        for (const TestString& token : {"switch", "case"})
        {
            std::size_t tokenSearchPosition = 0;
            while (true)
            {
                const std::size_t tokenPosition =
                    FindIdentifierToken(maskedSource,
                                        token,
                                        tokenSearchPosition,
                                        maskedSource.size());
                if (tokenPosition == TestString::npos)
                {
                    break;
                }

                const SourceRange statementRange =
                    FindStatementRangeContaining(maskedSource, tokenPosition);
                const TestString statement = NormalizeRange(maskedSource, statementRange);
                assert(statement.find("254u") == TestString::npos);
                assert(statement.find("DEBUG_VIEW_MODE_VALIDATION_PBR") ==
                       TestString::npos);
                assert(statement.find("bValidationPBR") == TestString::npos);
                tokenSearchPosition = tokenPosition + token.size();
            }
        }

        for (const char controlCharacter : {'?', ':'})
        {
            std::size_t controlSearchPosition = 0;
            while (true)
            {
                const std::size_t controlPosition =
                    maskedSource.find(controlCharacter, controlSearchPosition);
                if (controlPosition == TestString::npos)
                {
                    break;
                }

                const SourceRange statementRange =
                    FindStatementRangeContaining(maskedSource, controlPosition);
                const TestString statement = NormalizeRange(maskedSource, statementRange);
                assert(statement.find("254u") == TestString::npos);
                assert(statement.find("DEBUG_VIEW_MODE_VALIDATION_PBR") ==
                       TestString::npos);
                assert(statement.find("bValidationPBR") == TestString::npos);
                controlSearchPosition = controlPosition + 1;
            }
        }
    }

    std::size_t CollectBindingCallExpressions(const TestString& normalizedSource,
                                              const TestString& functionName,
                                              uint32_t binding,
                                              TestString* expressions,
                                              std::size_t expressionCapacity)
    {
        std::size_t count = 0;
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t functionPosition =
                FindIdentifierToken(normalizedSource,
                                    functionName,
                                    searchPosition,
                                    normalizedSource.size());
            if (functionPosition == TestString::npos)
            {
                break;
            }

            const std::size_t openingPosition =
                FindCallOpeningParenthesis(normalizedSource,
                                           functionPosition,
                                           functionName);
            const std::size_t closingPosition =
                FindMatchingParenthesis(normalizedSource, openingPosition);
            TestString arguments[8];
            const std::size_t argumentCount =
                SplitFunctionArguments(normalizedSource,
                                       openingPosition,
                                       closingPosition,
                                       arguments,
                                       8);
            assert(argumentCount >= 2);
            if (arguments[0] == FormatUnsigned(binding))
            {
                assert(count < expressionCapacity);
                expressions[count] = arguments[1];
                ++count;
            }

            searchPosition = closingPosition + 1;
        }

        return count;
    }

    void AssertBindingCallCount(const TestString& source,
                                const TestString& functionName,
                                uint32_t binding,
                                std::size_t expectedCount)
    {
        const TestString normalizedSource = RemoveWhitespace(MaskShaderNonCode(source));
        TestString expressions[32];
        const std::size_t count =
            CollectBindingCallExpressions(normalizedSource,
                                          functionName,
                                          binding,
                                          expressions,
                                          32);
        assert(count == expectedCount);
    }

    TestString FindSamplerDescriptorSource(const TestString& source,
                                             const TestString& samplerExpression)
    {
        const TestString normalizedSource = RemoveWhitespace(MaskShaderNonCode(source));
        const TestString createPrefix = samplerExpression + "=m_Device->CreateSampler(";
        assert(CountText(normalizedSource, createPrefix) == 1);
        const std::size_t createPosition = FindText(normalizedSource, createPrefix);
        const std::size_t openingPosition =
            createPosition + createPrefix.size() - 1;
        const std::size_t closingPosition =
            FindMatchingParenthesis(normalizedSource, openingPosition);
        TestString arguments[2];
        const std::size_t argumentCount =
            SplitFunctionArguments(normalizedSource,
                                   openingPosition,
                                   closingPosition,
                                   arguments,
                                   2);
        assert(argumentCount == 1);
        TestString descriptorName = arguments[0];
        std::size_t descriptorSearchEnd = createPosition;
        arguments[1].clear();
        bool bFoundCopyOrigin = false;
        for (std::size_t copyDepth = 0; copyDepth < 8; ++copyDepth)
        {
            const std::size_t declarationPosition =
                normalizedSource.find("RHI::SamplerDesc" + descriptorName);
            assert(declarationPosition != TestString::npos);
            assert(declarationPosition < descriptorSearchEnd);
            const std::size_t declarationEndPosition =
                normalizedSource.find(';', declarationPosition);
            assert(declarationEndPosition != TestString::npos);
            assert(declarationEndPosition < descriptorSearchEnd);

            arguments[1] =
                normalizedSource.substr(declarationPosition,
                                        descriptorSearchEnd - declarationPosition) +
                arguments[1];

            const std::size_t equalsPosition =
                normalizedSource.find('=', declarationPosition);
            if (equalsPosition == TestString::npos ||
                equalsPosition >= declarationEndPosition)
            {
                bFoundCopyOrigin = true;
                break;
            }

            descriptorName = normalizedSource.substr(equalsPosition + 1,
                                                     declarationEndPosition -
                                                         equalsPosition - 1);
            assert(!descriptorName.empty());
            descriptorSearchEnd = declarationPosition;
        }

        assert(bFoundCopyOrigin);
        return arguments[1];
    }

    void AssertSamplerDescriptor(const TestString& source,
                                 uint32_t binding,
                                 const TestString& addressU,
                                 const TestString& addressV,
                                 const TestString& filterMip)
    {
        const TestString normalizedSource = RemoveWhitespace(MaskShaderNonCode(source));
        TestString samplerExpressions[16];
        const std::size_t samplerCount =
            CollectBindingCallExpressions(normalizedSource,
                                          "BindSampler",
                                          binding,
                                          samplerExpressions,
                                          16);
        assert(samplerCount >= 1);
        for (std::size_t index = 0; index < samplerCount; ++index)
        {
            TestString descriptorExpression = samplerExpressions[index];
            if (ContainsText(descriptorExpression, "?"))
            {
                // Dynamic sky selection still falls back to the existing IBL sampler.
                descriptorExpression = "m_IBLSampler";
            }
            const TestString descriptorSource =
                FindSamplerDescriptorSource(source, descriptorExpression);
            const auto AssertFinalFieldValue =
                [&](const char* field, const char* expectedValue)
                {
                    std::size_t searchPosition = 0;
                    std::size_t lastAssignmentPosition = TestString::npos;
                    while ((searchPosition = descriptorSource.find(field, searchPosition)) !=
                           TestString::npos)
                    {
                        const std::size_t equalsPosition =
                            descriptorSource.find('=', searchPosition);
                        assert(equalsPosition != TestString::npos);
                        lastAssignmentPosition = equalsPosition;
                        searchPosition = equalsPosition + 1;
                    }

                    assert(lastAssignmentPosition != TestString::npos);
                    const std::size_t assignmentEndPosition =
                        descriptorSource.find(';', lastAssignmentPosition);
                    assert(assignmentEndPosition != TestString::npos);
                    assert(descriptorSource.substr(
                               lastAssignmentPosition + 1,
                               assignmentEndPosition - lastAssignmentPosition - 1) ==
                           expectedValue);
                };
            AssertFinalFieldValue("filterMin", "RHI::FilterMode::Linear");
            AssertFinalFieldValue("filterMag", "RHI::FilterMode::Linear");
            AssertFinalFieldValue("filterMip", filterMip.c_str());
            AssertFinalFieldValue("addressU", addressU.c_str());
            AssertFinalFieldValue("addressV", addressV.c_str());
        }
    }

    void AssertTextureBindingResource(const TestString& source,
                                      uint32_t binding,
                                      const TestString* semantics,
                                      std::size_t semanticCount)
    {
        const TestString normalizedSource = RemoveWhitespace(MaskShaderNonCode(source));
        TestString textureExpressions[16];
        const std::size_t textureCount =
            CollectBindingCallExpressions(normalizedSource,
                                          "BindTexture",
                                          binding,
                                          textureExpressions,
                                          16);
        assert(textureCount >= 1);
        const bool bRequiresTextureSelector = binding == 8 || binding == 12 || binding == 13;
        if (bRequiresTextureSelector)
        {
            assert(textureCount == 1);
        }
        for (std::size_t index = 0; index < textureCount; ++index)
        {
            bool bSemanticMatch = false;
            if (bRequiresTextureSelector)
            {
                const std::size_t selectorPosition =
                    normalizedSource.find("RHI::TexturePtr&" + textureExpressions[index] + "=");
                assert(selectorPosition != TestString::npos);
                const std::size_t bindPosition =
                    normalizedSource.find("BindTexture(" + FormatUnsigned(binding) + "," +
                                             textureExpressions[index] + ")",
                                         selectorPosition);
                assert(bindPosition != TestString::npos);
                assert(selectorPosition < bindPosition);
                const std::size_t selectorEndPosition =
                    normalizedSource.find(';', selectorPosition);
                assert(selectorEndPosition != TestString::npos);
                const std::size_t expectedSelectorCount = 6;
                std::size_t selectorCount = 0;
                for (std::size_t position = selectorPosition;
                     position < selectorEndPosition;
                     ++position)
                {
                    if (normalizedSource[position] == '?')
                    {
                        ++selectorCount;
                    }
                }
                assert(selectorCount == expectedSelectorCount);

                assert(semanticCount >= 2);
                const std::size_t actualSemanticCount = semanticCount - 1;
                bool bActualResource = false;
                for (std::size_t semanticIndex = 0;
                     semanticIndex < actualSemanticCount;
                     ++semanticIndex)
                {
                    const std::size_t semanticPosition =
                        normalizedSource.find(semantics[semanticIndex], selectorPosition);
                    if (semanticPosition != TestString::npos &&
                        semanticPosition < selectorEndPosition)
                    {
                        bActualResource = true;
                        break;
                    }
                }
                bool bFallbackResource = false;
                for (std::size_t semanticIndex = actualSemanticCount;
                     semanticIndex < semanticCount;
                     ++semanticIndex)
                {
                    const std::size_t semanticPosition =
                        normalizedSource.find(semantics[semanticIndex], selectorPosition);
                    if (semanticPosition != TestString::npos &&
                        semanticPosition < selectorEndPosition)
                    {
                        bFallbackResource = true;
                        break;
                    }
                }
                assert(bActualResource);
                assert(bFallbackResource);
                const std::size_t defaultBlackTexturePosition =
                    normalizedSource.find("m_DefaultBlackTexture", selectorPosition);
                const std::size_t blackTexturePosition =
                    normalizedSource.find("m_BlackTexture", selectorPosition);
                assert((defaultBlackTexturePosition != TestString::npos &&
                        defaultBlackTexturePosition < selectorEndPosition) ||
                       (blackTexturePosition != TestString::npos &&
                        blackTexturePosition < selectorEndPosition));
                if (binding == 8)
                {
                    assert(normalizedSource.find(
                               "bValidationRaw252&&m_ValidationRaw252EnvironmentTexture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "bValidationRaw250&&m_ValidationRaw250EnvironmentTexture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "m_bIBLAvailable&&m_EnvironmentTexture?",
                               selectorPosition) < selectorEndPosition);
                }
                else if (binding == 12)
                {
                    assert(normalizedSource.find(
                               "bValidationRaw252&&m_ValidationRaw252DiffuseIrradianceTexture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "bValidationRaw250&&m_ValidationRaw250DiffuseIrradianceTexture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "bSkyAtmosphereAvailable&&m_SkyAtmosphereDiffuseIrradianceTexture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "bSkyAtmosphereRequested?m_DefaultBlackTexture",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "m_bIBLAvailable&&m_DiffuseIrradianceTexture?",
                               selectorPosition) < selectorEndPosition);
                }
                else
                {
                    assert(normalizedSource.find(
                               "bValidationRaw252&&m_ValidationRaw252PrefilteredSpecularTexture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find("bValidationRaw250", selectorPosition) <
                           selectorEndPosition);
                    assert(normalizedSource.find("m_ValidationRaw250Texture", selectorPosition) <
                           selectorEndPosition);
                    assert(normalizedSource.find(
                               "bValidationRaw250&&m_ValidationRaw250Texture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "bSkyAtmosphereAvailable&&m_SkyAtmospherePrefilteredSpecularTexture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "bSkyAtmosphereRequested?m_DefaultBlackTexture",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find(
                               "m_bIBLAvailable&&m_PrefilteredSpecularTexture?",
                               selectorPosition) < selectorEndPosition);
                    assert(normalizedSource.find("m_bIBLAvailable", selectorPosition) <
                           selectorEndPosition);
                }
                bSemanticMatch = true;
            }
            else
            {
                for (std::size_t semanticIndex = 0;
                     semanticIndex < semanticCount;
                     ++semanticIndex)
                {
                    if (ContainsText(textureExpressions[index], semantics[semanticIndex]))
                    {
                        bSemanticMatch = true;
                        break;
                    }
                }
            }
            assert(bSemanticMatch);
        }
    }

    void AssertStorageBindingResource(const TestString& source,
                                      uint32_t binding,
                                      const TestString* semantics,
                                      std::size_t semanticCount)
    {
        const TestString normalizedSource = RemoveWhitespace(MaskShaderNonCode(source));
        TestString bufferExpressions[16];
        const std::size_t bufferCount =
            CollectBindingCallExpressions(normalizedSource,
                                          "BindStorageBuffer",
                                          binding,
                                          bufferExpressions,
                                          16);
        assert(bufferCount >= 1);
        for (std::size_t index = 0; index < bufferCount; ++index)
        {
            bool bSemanticMatch = false;
            for (std::size_t semanticIndex = 0;
                 semanticIndex < semanticCount;
                 ++semanticIndex)
            {
                if (ContainsText(bufferExpressions[index], semantics[semanticIndex]))
                {
                    bSemanticMatch = true;
                    break;
                }
            }
            assert(bSemanticMatch);
        }
    }

    void AssertShaderResourceDeclaration(const TestString& source,
                                         uint32_t binding,
                                         const TestString& expectedType,
                                         const TestString& semanticA,
                                         const TestString& semanticB,
                                         const TestString& semanticC)
    {
        const ShaderLayoutDeclaration declaration =
            FindShaderLayoutDeclaration(source, binding);
        assert(declaration.bHasSet);
        assert(declaration.bHasBinding);
        assert(declaration.resourceType == expectedType);
        assert(!declaration.resourceName.empty());
        assert(ContainsText(declaration.resourceName, semanticA) ||
               ContainsText(declaration.resourceName, semanticB) ||
               ContainsText(declaration.resourceName, semanticC));
    }

    std::size_t CountSimpleAssignmentsTo(const TestString& maskedSource,
                                         const TestString& token)
    {
        std::size_t count = 0;
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t tokenPosition =
                FindIdentifierToken(maskedSource,
                                    token,
                                    searchPosition,
                                    maskedSource.size());
            if (tokenPosition == TestString::npos)
            {
                return count;
            }

            if (IsSimpleAssignmentTo(maskedSource, tokenPosition, token))
            {
                ++count;
            }
            searchPosition = tokenPosition + token.size();
        }
    }

    void AssertEnvironmentLuminanceScaleGuard(const TestString& source)
    {
        const TestString maskedSource = MaskShaderNonCode(source);
        std::size_t guardCount = 0;
        SourceRange guardConditionRange;
        SourceRange guardBodyRange;

        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t ifPosition =
                FindIdentifierToken(maskedSource,
                                    "if",
                                    searchPosition,
                                    maskedSource.size());
            if (ifPosition == TestString::npos)
            {
                break;
            }

            std::size_t openingPosition = ifPosition + TestString("if").size();
            while (openingPosition < maskedSource.size() &&
                   IsWhitespaceCharacter(maskedSource[openingPosition]))
            {
                ++openingPosition;
            }
            assert(openingPosition < maskedSource.size());
            assert(maskedSource[openingPosition] == '(');
            const std::size_t closingPosition =
                FindMatchingParenthesis(maskedSource, openingPosition);
            const TestString condition =
                RemoveWhitespace(maskedSource.substr(openingPosition + 1,
                                                     closingPosition - openingPosition - 1));
            const bool bReferencesScale =
                condition.find("EnvironmentLuminanceScaleNits") != TestString::npos;
            const bool bIsInputScaleGuard =
                bReferencesScale &&
                condition.find("isfinite") != TestString::npos &&
                (condition.find("!std::isfinite(") != TestString::npos ||
                 condition.find("!isfinite(") != TestString::npos) &&
                (condition.find("<0.0") != TestString::npos ||
                 condition.find("<0") != TestString::npos);
            if (bIsInputScaleGuard)
            {
                ++guardCount;
                guardConditionRange = {openingPosition + 1, closingPosition};
                assert(condition.find("isfinite") != TestString::npos);
                assert(condition.find("!std::isfinite(") != TestString::npos ||
                       condition.find("!isfinite(") != TestString::npos);
                assert(condition.find("<0.0") != TestString::npos ||
                       condition.find("<0") != TestString::npos);
                assert(condition.find("<=") == TestString::npos);
                assert(condition.find("max(") == TestString::npos);
                assert(condition.find("min(") == TestString::npos);
                assert(condition.find("clamp(") == TestString::npos);

                std::size_t bodyOpeningPosition = closingPosition + 1;
                while (bodyOpeningPosition < maskedSource.size() &&
                       IsWhitespaceCharacter(maskedSource[bodyOpeningPosition]))
                {
                    ++bodyOpeningPosition;
                }
                assert(bodyOpeningPosition < maskedSource.size());
                assert(maskedSource[bodyOpeningPosition] == '{');
                const std::size_t bodyClosingPosition =
                    FindMatchingBrace(maskedSource, bodyOpeningPosition);
                guardBodyRange = {bodyOpeningPosition + 1, bodyClosingPosition};
                const TestString body =
                    RemoveWhitespace(maskedSource.substr(guardBodyRange.begin,
                                                         guardBodyRange.end -
                                                             guardBodyRange.begin));
                assert(CountText(body, "returnfalse;") == 1);
                assert(CountText(body, "return") == 1);
            }
            searchPosition = closingPosition + 1;
        }

        assert(guardCount == 1);
        assert(CountSimpleAssignmentsTo(maskedSource,
                                        "EnvironmentLuminanceScaleNits") == 0);

        std::size_t scaleSearchPosition = 0;
        while (true)
        {
            const std::size_t scalePosition =
                FindIdentifierToken(maskedSource,
                                    "EnvironmentLuminanceScaleNits",
                                    scaleSearchPosition,
                                    maskedSource.size());
            if (scalePosition == TestString::npos)
            {
                break;
            }

            assert(guardConditionRange.begin != TestString::npos);
            assert(scalePosition >= guardConditionRange.begin);
            scaleSearchPosition =
                scalePosition + TestString("EnvironmentLuminanceScaleNits").size();
        }
    }

    void AssertPreExposurePolicy(const TestString& maskedSource,
                                 std::size_t beginPosition,
                                 std::size_t endPosition,
                                 const TestString& validationRaw252Mode,
                                 const TestString& validationLambertMode,
                                 const TestString& validationPbrMode,
                                 const TestString& validationRaw250Mode,
                                 const TestString& validationRaw251Mode)
    {
        const TestString normalizedPolicy =
            RemoveWhitespace(maskedSource.substr(beginPosition,
                                                 endPosition - beginPosition));
        const std::size_t returnPosition = normalizedPolicy.find("return");
        assert(returnPosition != TestString::npos);
        const std::size_t semicolonPosition =
            normalizedPolicy.find(';', returnPosition);
        assert(semicolonPosition != TestString::npos);
        const TestString expression =
            normalizedPolicy.substr(returnPosition + TestString("return").size(),
                                    semicolonPosition -
                                        returnPosition - TestString("return").size());
        assert(CountText(normalizedPolicy, "return") == 1);
        assert(CountText(expression, "params.debugViewMode==") == 4);
        assert(CountText(expression, "==") == 4);
        assert(CountText(expression, "||") == 3);
        assert(expression.find("!=") == TestString::npos);
        assert(expression.find("&&") == TestString::npos);
        assert(expression.find('!') == TestString::npos);
        assert(expression.find('?') == TestString::npos);
        assert(expression.find(':') == TestString::npos);
        assert(CountText(expression, "params.debugViewMode==DEBUG_VIEW_MODE_NORMAL") == 1);
        assert(CountText(expression, "params.debugViewMode==" + validationRaw252Mode) == 1);
        assert(CountText(expression, "params.debugViewMode==" + validationLambertMode) == 1);
        assert(CountText(expression, "params.debugViewMode==" + validationPbrMode) == 1);
        assert(CountText(expression, validationRaw250Mode) == 0);
        assert(CountText(expression, validationRaw251Mode) == 0);
        assert(CountText(expression, "DEBUG_VIEW_MODE_") == 4);

        const TestString expectedTerms[] = {
            "params.debugViewMode==DEBUG_VIEW_MODE_NORMAL",
            "params.debugViewMode==" + validationRaw252Mode,
            "params.debugViewMode==" + validationLambertMode,
            "params.debugViewMode==" + validationPbrMode};
        TestString remainingExpression = expression;
        for (const TestString& term : expectedTerms)
        {
            const std::size_t termPosition = remainingExpression.find(term);
            assert(termPosition != TestString::npos);
            remainingExpression.replace(termPosition, term.size(), TestString{});
        }
        std::size_t orPosition = 0;
        while ((orPosition = remainingExpression.find("||", orPosition)) !=
               TestString::npos)
        {
            remainingExpression.replace(orPosition, 2, TestString{});
        }
        assert(remainingExpression.empty());
    }

    template <typename T>
    std::size_t PreExposureOffset()
    {
        if constexpr (requires(T value) { value.preExposure; })
        {
            return offsetof(T, preExposure);
        }

        return static_cast<std::size_t>(-1);
    }

    TestString FindShaderUintConstantName(const TestString& source, uint32_t value)
    {
        const TestString normalizedSource =
            RemoveWhitespace(MaskShaderNonCode(source));
        const TestString valueText = "=" + FormatUnsigned(value) + "u;";
        assert(CountText(normalizedSource, valueText) == 1);
        const std::size_t valuePosition = FindText(normalizedSource, valueText);
        const std::size_t declarationPosition =
            normalizedSource.substr(0, valuePosition + 1).FindLast("constuint");
        assert(declarationPosition != TestString::npos);

        const std::size_t nameStart = declarationPosition + TestString("constuint").size();
        const std::size_t nameEnd = normalizedSource.find('=', nameStart);
        assert(nameEnd != TestString::npos);
        assert(declarationPosition < nameStart);
        assert(nameEnd == valuePosition);
        return normalizedSource.substr(nameStart, nameEnd - nameStart);
    }

    void AssertShaderDebugModeConstant(const TestString& shaderSource,
                                       const TestString& constantName,
                                       uint32_t expectedValue)
    {
        const TestString normalizedSource =
            RemoveWhitespace(MaskShaderNonCode(shaderSource));
        const TestString expectedText =
            "constuint" + constantName + "=" + FormatUnsigned(expectedValue) + "u;";
        assert(CountText(normalizedSource, expectedText) == 1);
    }
} // namespace

int main()
{
    ConfigureAssertOutput();

    std::cout << "LightingParamsLayoutTest start\n";

    const LightingPassSettings defaultSettings;
    assert(defaultSettings.EnvironmentLuminanceScaleNits == 1.0f);
    assert(std::isfinite(defaultSettings.EnvironmentLuminanceScaleNits));
    assert(defaultSettings.EnvironmentLuminanceScaleNits >= 0.0f);

    assert(sizeof(GPULightingParams) == 272);
    assert(sizeof(GPULightingParams) % 16 == 0);

    assert(offsetof(GPULightingParams, invViewProjection) == 0);
    assert(offsetof(GPULightingParams, cameraPosition) == 64);
    assert(offsetof(GPULightingParams, ambientColor) == 80);
    assert(offsetof(GPULightingParams, lightView) == 96);
    assert(offsetof(GPULightingParams, lightProjection) == 160);
    assert(offsetof(GPULightingParams, lightCount) == 224);
    assert(offsetof(GPULightingParams, bShadowEnabled) == 228);
    static_assert(std::is_same_v<decltype(GPULightingParams{}.prefilteredSpecularMipLevels), uint32_t>);
    assert(offsetof(GPULightingParams, prefilteredSpecularMipLevels) == 232);
    assert(offsetof(GPULightingParams, bIBLEnabled) == 236);
    assert(offsetof(GPULightingParams, bSSAOEnabled) == 240);
    assert(offsetof(GPULightingParams, bNeuralBRDFEnabled) == 244);
    assert(offsetof(GPULightingParams, debugViewMode) == 248);
    assert(PreExposureOffset<GPULightingParams>() == 252);
    assert(offsetof(GPULightingParams, skySunDirectionAndCosRadius) == 256);

    assert(static_cast<uint8_t>(DebugViewMode::Normal) == 0);
    assert(static_cast<uint8_t>(DebugViewMode::Unlit) == 1);
    assert(static_cast<uint8_t>(DebugViewMode::Wireframe) == 2);
    assert(static_cast<uint8_t>(DebugViewMode::MegaGeometryClusters) == 3);
    assert(static_cast<uint8_t>(DebugViewMode::GBufferAlbedo) == 4);
    assert(static_cast<uint8_t>(DebugViewMode::GBufferNormal) == 5);
    assert(static_cast<uint8_t>(DebugViewMode::GBufferMaterial) == 6);
    assert(static_cast<uint8_t>(DebugViewMode::GBufferDepth) == 7);
    assert(static_cast<uint8_t>(DebugViewMode::LODLevel) == 8);
    assert(static_cast<uint8_t>(DebugViewMode::Count) == 9);

    const TestString shaderPath = TestString(NORVES_SHADER_DIR) + "/lighting.frag";
    const TestString shaderSource =
        ReadTextFile(std::filesystem::path(shaderPath.c_str()));
    const std::filesystem::path sourceRoot =
        std::filesystem::path(NORVES_SHADER_DIR).parent_path().parent_path();
    const TestString lightingPassSource = ReadTextFile(
        sourceRoot / "Library/Core/Private/Rendering/LightingPass.cpp");
    const TestString maskedShaderSource = MaskShaderNonCode(shaderSource);
    const TestString maskedLightingPassSource = MaskShaderNonCode(lightingPassSource);
    for (uint32_t binding = 0; binding <= 15; ++binding)
    {
        AssertShaderBinding(shaderSource, binding);
    }

    const ShaderLayoutDeclaration lightingParamsDeclaration =
        FindShaderLayoutDeclaration(shaderSource, 4);
    assert(lightingParamsDeclaration.resourceType == "uniform");
    assert(lightingParamsDeclaration.resourceName == "params");
    for (uint32_t binding = 0; binding <= 15; ++binding)
    {
        const ShaderLayoutDeclaration declaration =
            FindShaderLayoutDeclaration(shaderSource, binding);
        assert(!declaration.resourceName.empty());
        if (binding == 4)
        {
            assert(declaration.resourceType == "uniform");
        }
        else if (binding == 5 || binding == 11)
        {
            assert(declaration.resourceType == "buffer");
        }
        else
        {
            assert(declaration.resourceType == "sampler2D");
        }
    }
    AssertShaderResourceDeclaration(shaderSource,
                                    8,
                                    "sampler2D",
                                    "env",
                                    "Environment",
                                    "radiance");
    AssertShaderResourceDeclaration(shaderSource,
                                    9,
                                    "sampler2D",
                                    "brdf",
                                    "DFG",
                                    "LUT");
    AssertShaderResourceDeclaration(shaderSource,
                                    11,
                                    "buffer",
                                    "Neural",
                                    "neural",
                                    "BRDF");
    AssertShaderResourceDeclaration(shaderSource,
                                    12,
                                    "sampler2D",
                                    "irradiance",
                                    "Irradiance",
                                    "diffuse");
    AssertShaderResourceDeclaration(shaderSource,
                                    13,
                                    "sampler2D",
                                    "prefilter",
                                    "Prefilter",
                                    "specular");
    AssertShaderResourceDeclaration(shaderSource,
                                    14,
                                    "sampler2D",
                                    "skySunDisk",
                                    "SunDisk",
                                    "sky");
    AssertShaderResourceDeclaration(shaderSource,
                                    15,
                                    "sampler2D",
                                    "skyTransmittance",
                                    "Transmittance",
                                    "sky");

    assert(ContainsText(shaderSource, "uint prefilteredSpecularMipLevels;"));
    assert(ContainsText(shaderSource, "vec4 skySunDirectionAndCosRadius;"));
    assert(!ContainsText(shaderSource, "uint envMapMipLevels;"));

    const std::size_t debugViewModeFieldPosition = FindText(shaderSource, "uint debugViewMode;");
    const std::size_t preExposureFieldPosition = FindText(shaderSource, "float preExposure;");
    assert(debugViewModeFieldPosition < preExposureFieldPosition);

    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_NORMAL",
                                  static_cast<uint32_t>(DebugViewMode::Normal));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_UNLIT",
                                  static_cast<uint32_t>(DebugViewMode::Unlit));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_WIREFRAME",
                                  static_cast<uint32_t>(DebugViewMode::Wireframe));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS",
                                  static_cast<uint32_t>(DebugViewMode::MegaGeometryClusters));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_GBUFFER_ALBEDO",
                                  static_cast<uint32_t>(DebugViewMode::GBufferAlbedo));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_GBUFFER_NORMAL",
                                  static_cast<uint32_t>(DebugViewMode::GBufferNormal));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_GBUFFER_MATERIAL",
                                  static_cast<uint32_t>(DebugViewMode::GBufferMaterial));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_GBUFFER_DEPTH",
                                  static_cast<uint32_t>(DebugViewMode::GBufferDepth));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_LOD_LEVEL",
                                  static_cast<uint32_t>(DebugViewMode::LODLevel));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_COUNT",
                                  static_cast<uint32_t>(DebugViewMode::Count));

    const TestString validationLambertMode = FindShaderUintConstantName(shaderSource, 253);
    const TestString validationPbrMode = FindShaderUintConstantName(shaderSource, 254);
    const TestString validationRaw250Mode = FindShaderUintConstantName(shaderSource, 250);
    const TestString validationRaw251Mode = FindShaderUintConstantName(shaderSource, 251);
    const TestString validationRaw252Mode = FindShaderUintConstantName(shaderSource, 252);
    const TestString normalizedShaderSource = RemoveWhitespace(maskedShaderSource);
    assert(CountText(normalizedShaderSource, "=253u;") == 1);
    assert(CountText(normalizedShaderSource, "=254u;") == 1);
    assert(CountText(normalizedShaderSource, "=250u;") == 1);
    assert(CountText(normalizedShaderSource, "=251u;") == 1);
    assert(CountText(normalizedShaderSource, "=252u;") == 1);

    const TestString commonBranchCondition =
        "else if (bValidationPBR || params.bNeuralBRDFEnabled == 0u)";
    assert(CountText(maskedShaderSource, commonBranchCondition) == 1);

    const std::size_t commonBranchPosition =
        FindText(maskedShaderSource, commonBranchCondition);
    const std::size_t commonBranchConditionEnd =
        commonBranchPosition + commonBranchCondition.size();
    const std::size_t commonBranchOpeningBrace =
        FindNextCodeCharacter(maskedShaderSource,
                              '{',
                              commonBranchConditionEnd);
    const std::size_t commonBranchClosingBrace =
        FindMatchingBrace(shaderSource, commonBranchOpeningBrace);
    const std::size_t commonBranchBodyBegin = commonBranchOpeningBrace + 1;
    const std::size_t commonBranchBodyEnd = commonBranchClosingBrace;

    const TestString endpointHelperName =
        "EvaluateAnalyticalDirectEndpointBRDF";
    const TestString endpointHelperSignature =
        "void EvaluateAnalyticalDirectEndpointBRDF("
        "vec3 albedo, float metallic, float roughness, vec3 N, vec3 V, vec3 L, vec3 H, "
        "vec2 dfg, out vec3 diffuseBrdf, out vec3 specularBrdf)";
    assert(CountText(RemoveWhitespace(maskedShaderSource),
                     RemoveWhitespace(endpointHelperSignature)) == 1);
    assert(CountFunctionCalls(maskedShaderSource, endpointHelperName) == 2);

    const std::size_t endpointDefinitionNamePosition =
        FindText(maskedShaderSource, endpointHelperName);
    const std::size_t endpointDefinitionOpeningParenthesis =
        FindCallOpeningParenthesis(maskedShaderSource,
                                   endpointDefinitionNamePosition,
                                   endpointHelperName);
    const std::size_t endpointDefinitionOpeningBrace =
        FindNextCodeCharacter(maskedShaderSource,
                              '{',
                              endpointDefinitionOpeningParenthesis + 1);
    const std::size_t endpointDefinitionClosingBrace =
        FindMatchingBrace(shaderSource, endpointDefinitionOpeningBrace);
    const std::size_t endpointDefinitionBodyBegin = endpointDefinitionOpeningBrace + 1;
    const std::size_t endpointDefinitionBodyEnd = endpointDefinitionClosingBrace;
    AssertNoEndpointForbiddenTokens(maskedShaderSource,
                                    endpointDefinitionBodyBegin,
                                    endpointDefinitionBodyEnd);
    AssertEndpointHelperDataflow(maskedShaderSource,
                                 endpointDefinitionBodyBegin,
                                 endpointDefinitionBodyEnd);

    assert(CountIdentifierToken(maskedShaderSource,
                                "F0d",
                                endpointDefinitionOpeningBrace + 1,
                                endpointDefinitionClosingBrace) >= 1);
    assert(CountIdentifierToken(maskedShaderSource,
                                "F0c",
                                endpointDefinitionOpeningBrace + 1,
                                endpointDefinitionClosingBrace) >= 1);
    assert(CountIdentifierToken(maskedShaderSource,
                                "Ess",
                                endpointDefinitionOpeningBrace + 1,
                                endpointDefinitionClosingBrace) >= 1);

    const std::size_t endpointCallPosition =
        FindTopLevelFunctionCall(maskedShaderSource,
                                 endpointHelperName,
                                 commonBranchBodyBegin,
                                 commonBranchBodyEnd);
    AssertNoTopLevelControlFlow(maskedShaderSource,
                                commonBranchBodyBegin,
                                commonBranchBodyEnd);
    assert(CountTopLevelIdentifierToken(maskedShaderSource,
                                        endpointHelperName,
                                        commonBranchBodyBegin,
                                        commonBranchBodyEnd) == 1);
    const std::size_t endpointCallOpeningParenthesis =
        FindCallOpeningParenthesis(maskedShaderSource,
                                   endpointCallPosition,
                                   endpointHelperName);
    const std::size_t endpointCallSemicolon =
        maskedShaderSource.find(';', endpointCallOpeningParenthesis);
    assert(endpointCallSemicolon != TestString::npos);
    assert(endpointCallSemicolon < commonBranchBodyEnd);
    const ShaderStatement endpointCallStatement =
        FindSingleStatementMatching(maskedShaderSource,
                                    commonBranchBodyBegin,
                                    commonBranchBodyEnd,
                                    [&](const TestString&, const TestString& normalized)
                                    {
                                        return normalized.find(endpointHelperName + "(") !=
                                                   TestString::npos;
                                    });
    AssertNoEndpointForbiddenTokens(maskedShaderSource,
                                    endpointCallPosition,
                                    endpointCallSemicolon + 1);

    const ShaderLayoutDeclaration binding9Declaration =
        FindShaderLayoutDeclaration(shaderSource, 9);
    const TestString binding9SamplerName = binding9Declaration.resourceName;
    assert(!binding9SamplerName.empty());
    assert(CountTopLevelSamplerSamples(maskedShaderSource,
                                       binding9SamplerName,
                                       commonBranchBodyBegin,
                                       commonBranchBodyEnd) == 1);
    const ShaderStatement dfgSampleStatement =
        FindSingleStatementMatching(maskedShaderSource,
                                    commonBranchBodyBegin,
                                    commonBranchBodyEnd,
                                    [&](const TestString&, const TestString& normalized)
                                    {
                                        return normalized.find(binding9SamplerName) !=
                                                   TestString::npos &&
                                               (normalized.find("texture(") !=
                                                    TestString::npos ||
                                                normalized.find("textureLod(") !=
                                                    TestString::npos);
                                    });
    assert(dfgSampleStatement.lhs != "");
    const TestString normalizedEndpointCall = endpointCallStatement.normalized;
    const std::size_t normalizedEndpointCallOpening =
        normalizedEndpointCall.find('(');
    assert(normalizedEndpointCallOpening != TestString::npos);
    const std::size_t normalizedEndpointCallClosing =
        FindMatchingParenthesis(normalizedEndpointCall,
                                normalizedEndpointCallOpening);
    TestString endpointCallArguments[12];
    const std::size_t endpointArgumentCount =
        SplitFunctionArguments(normalizedEndpointCall,
                               normalizedEndpointCallOpening,
                               normalizedEndpointCallClosing,
                               endpointCallArguments,
                               12);
    assert(endpointArgumentCount == 10);
    assert(endpointCallArguments[7] == dfgSampleStatement.lhs);
    assert(!endpointCallArguments[8].empty());
    assert(!endpointCallArguments[9].empty());
    assert(endpointCallArguments[8] != endpointCallArguments[9]);
    AssertRaw254LexicalPolicy(maskedShaderSource,
                              validationPbrMode,
                              commonBranchCondition,
                              commonBranchPosition,
                              commonBranchConditionEnd,
                              commonBranchOpeningBrace);

    assert(!ContainsText(maskedShaderSource, "direct-conductor-endpoint"));
    assert(!ContainsText(maskedShaderSource, "--r1-scenario"));

    const TestString validationLambertCheck =
        "params.debugViewMode==" + validationLambertMode;
    const std::size_t validationLambertCheckPosition =
        FindText(normalizedShaderSource, validationLambertCheck);
    const std::size_t validationPbrCheckPosition =
        FindText(normalizedShaderSource, "params.debugViewMode==" + validationPbrMode);
    assert(validationLambertCheckPosition != validationPbrCheckPosition);

    assert(FindText(normalizedShaderSource,
                    "params.debugViewMode==" + validationRaw250Mode) != TestString::npos);
    assert(FindText(normalizedShaderSource,
                    "params.debugViewMode==" + validationRaw251Mode) != TestString::npos);
    assert(FindText(normalizedShaderSource,
                    "params.debugViewMode==" + validationRaw252Mode) != TestString::npos);

    assert(!ContainsText(shaderSource, "float(params.envMapMipLevels - 1u)"));
    assert(!ContainsText(shaderSource, "float(params.prefilteredSpecularMipLevels - 1.0"));

    const std::size_t initializePosition =
        FindText(lightingPassSource, "bool LightingPass::Initialize");
    const std::size_t shutdownPosition =
        FindTextAfter(lightingPassSource, "void LightingPass::Shutdown", initializePosition);
    const TestString initializeSource =
        lightingPassSource.substr(initializePosition, shutdownPosition - initializePosition);
    assert(ContainsText(initializeSource, "EnvironmentLuminanceScaleNits"));
    assert(ContainsText(initializeSource, "std::isfinite") ||
           ContainsText(initializeSource, "isfinite"));
    assert(ContainsText(initializeSource, "EnvironmentLuminanceScaleNits < 0.0f") ||
           ContainsText(initializeSource, "EnvironmentLuminanceScaleNits < 0.0"));
    assert(ContainsText(initializeSource, "return false;"));
    assert(!ContainsText(initializeSource, "EnvironmentLuminanceScaleNits = 1.0f"));
    assert(!ContainsText(initializeSource, "EnvironmentLuminanceScaleNits = 1.0"));
    AssertEnvironmentLuminanceScaleGuard(initializeSource);
    const std::size_t validationModePosition =
        FindText(initializeSource, "bValidationSnapshotMode");
    const std::size_t environmentLoadPosition =
        FindText(initializeSource, "LoadEnvironmentMap");
    const std::size_t validationSnapshotPosition =
        FindText(initializeSource, "GenerateValidationSnapshots");
    assert(validationModePosition < environmentLoadPosition);
    assert(validationModePosition < validationSnapshotPosition);
    assert(ContainsText(initializeSource,
                        "!bValidationSnapshotMode && !m_Settings.EnvironmentMapPath.empty()"));
    assert(ContainsText(initializeSource,
                        "bValidationSnapshotMode && !GenerateValidationSnapshots()"));
    const std::size_t initialDescriptorPosition =
        FindText(initializeSource, "CreateLightingDescriptorSet(initialDescriptorSet)");
    const std::size_t commitPosition = FindText(initializeSource, "initializationRollback.Commit()");
    const std::size_t initializedPosition = FindText(initializeSource, "m_bInitialized = true");
    assert(validationSnapshotPosition < initialDescriptorPosition);
    assert(initialDescriptorPosition < commitPosition);
    assert(commitPosition < initializedPosition);
    assert(ContainsText(lightingPassSource,
                        "params.prefilteredSpecularMipLevels = 9"));
    assert(!ContainsText(lightingPassSource, "params.envMapMipLevels"));

    const std::size_t descriptorPosition =
        FindText(lightingPassSource, "CreateLightingDescriptorSetDesc");
    const std::size_t constructorPosition =
        FindTextAfter(lightingPassSource, "LightingPass::LightingPass", descriptorPosition);
    const TestString descriptorSource =
        maskedLightingPassSource.substr(descriptorPosition,
                                       constructorPosition - descriptorPosition);
    for (uint32_t binding = 0; binding <= 15; ++binding)
    {
        AssertProductionDescriptorBinding(descriptorSource, binding);
        if (binding == 4)
        {
            AssertProductionDescriptorType(descriptorSource,
                                           binding,
                                           "ConstantBuffer");
        }
        else if (binding == 5 || binding == 11)
        {
            AssertProductionDescriptorType(descriptorSource,
                                           binding,
                                           "StructuredBuffer");
        }
        else
        {
            AssertProductionDescriptorType(descriptorSource,
                                           binding,
                                           "CombinedImageSampler");
        }
    }

    const std::size_t firstDescriptorCondition = descriptorSource.find("if (");
    const std::size_t descriptorBinding9 = FindText(descriptorSource, ".binding = 9;");
    const std::size_t descriptorBinding11 = FindText(descriptorSource, ".binding = 11;");
    assert(firstDescriptorCondition == TestString::npos ||
           descriptorBinding9 < firstDescriptorCondition);
    assert(firstDescriptorCondition == TestString::npos ||
           descriptorBinding11 < firstDescriptorCondition);

    const std::size_t executePosition =
        FindText(lightingPassSource, "void LightingPass::ExecuteWithInputs");
    const std::size_t registerOutputsPosition =
        FindTextAfter(lightingPassSource, "void LightingPass::RegisterOutputs", executePosition);
    const TestString executeSource =
        RemoveWhitespace(maskedLightingPassSource.substr(executePosition,
                                                         registerOutputsPosition -
                                                             executePosition));
    for (const uint32_t binding : {8u, 12u, 13u, 14u, 15u})
    {
        const TestString bindingText = FormatUnsigned(binding) + ",";
        assert(CountText(executeSource, "BindTexture(" + bindingText) == 1);
        assert(CountText(executeSource, "BindSampler(" + bindingText) == 1);
    }

    assert(CountText(executeSource, "BindTexture(9,") == 1);
    assert(CountText(executeSource, "BindSampler(9,") == 1);
    assert(ContainsText(executeSource, "BindStorageBuffer(11,"));
    assert(ContainsText(executeSource, "bValidationRaw251=activeDebugMode==251u"));
    assert(CountText(executeSource, "bValidationRaw251?m_DefaultBlackTexture:") == 3);
    const std::size_t updateLightBufferPosition =
        FindText(lightingPassSource, "bool LightingPass::UpdateLightBuffer");
    const std::size_t loadEnvironmentPosition =
        FindTextAfter(lightingPassSource, "bool LightingPass::LoadEnvironmentMap",
                      updateLightBufferPosition);
    const TestString updateLightBufferSource = RemoveWhitespace(
        maskedLightingPassSource.substr(updateLightBufferPosition,
                                        loadEnvironmentPosition - updateLightBufferPosition));
    assert(ContainsText(updateLightBufferSource,
                        "params.bIBLEnabled=(!bValidationRaw251&&(m_bIBLAvailable||bValidationConstantIblAvailable))?1u:0u"));

    const TestString binding8Resources[] = {
        "m_ValidationRaw252EnvironmentTexture",
        "m_ValidationRaw250EnvironmentTexture",
        "context.SkyAtmosphere.RadianceTexture",
        "SkyAtmosphere.Radiance",
        "m_EnvironmentTexture",
        "EnvironmentTexture",
        "m_DefaultBlackTexture"};
    const TestString binding9Resources[] = {
        "m_BrdfLutTexture",
        "BrdfLut",
        "m_Dfg",
        "m_DFG",
        "Dfg"};
    const TestString binding10Resources[] = {
        "ssaoTexture",
        "m_SSAO",
        "m_Ssao",
        "m_DefaultWhiteTexture",
        "albedoTexture"};
    const TestString binding12Resources[] = {
        "m_ValidationRaw252DiffuseIrradianceTexture",
        "m_ValidationRaw250DiffuseIrradianceTexture",
        "m_SkyAtmosphereDiffuseIrradianceTexture",
        "m_DiffuseIrradianceTexture",
        "diffuseIrradiance",
        "m_Irradiance",
        "Irradiance",
        "m_DefaultBlackTexture"};
    const TestString binding13Resources[] = {
        "m_ValidationRaw252PrefilteredSpecularTexture",
        "m_ValidationRaw250Texture",
        "m_SkyAtmospherePrefilteredSpecularTexture",
        "m_PrefilteredSpecularTexture",
        "prefilteredSpecular",
        "m_Prefilter",
        "Prefilter",
        "m_DefaultBlackTexture"};
    const TestString binding14Resources[] = {
        "context.SkyAtmosphere.SunDiskTexture",
        "SkyAtmosphere.SunDisk",
        "m_DefaultBlackTexture"};
    const TestString binding15Resources[] = {
        "context.SkyAtmosphere.TransmittanceTexture",
        "SkyAtmosphere.Transmittance",
        "m_DefaultBlackTexture"};
    const TestString binding11Resources[] = {
        "m_NeuralBRDFWeightBuffer",
        "NeuralBRDF",
        "m_DefaultNeural",
        "neural"};
    AssertTextureBindingResource(executeSource,
                                 8,
                                 binding8Resources,
                                 sizeof(binding8Resources) / sizeof(binding8Resources[0]));
    AssertTextureBindingResource(lightingPassSource,
                                 9,
                                 binding9Resources,
                                 sizeof(binding9Resources) / sizeof(binding9Resources[0]));
    AssertTextureBindingResource(lightingPassSource,
                                 10,
                                 binding10Resources,
                                 sizeof(binding10Resources) / sizeof(binding10Resources[0]));
    AssertTextureBindingResource(executeSource,
                                 12,
                                 binding12Resources,
                                 sizeof(binding12Resources) / sizeof(binding12Resources[0]));
    AssertTextureBindingResource(executeSource,
                                 13,
                                 binding13Resources,
                                 sizeof(binding13Resources) / sizeof(binding13Resources[0]));
    AssertTextureBindingResource(executeSource,
                                 14,
                                 binding14Resources,
                                 sizeof(binding14Resources) / sizeof(binding14Resources[0]));
    AssertTextureBindingResource(executeSource,
                                 15,
                                 binding15Resources,
                                 sizeof(binding15Resources) / sizeof(binding15Resources[0]));
    AssertStorageBindingResource(lightingPassSource,
                                 11,
                                 binding11Resources,
                                 sizeof(binding11Resources) / sizeof(binding11Resources[0]));
    AssertBindingCallCount(lightingPassSource, "BindTexture", 9, 2);
    AssertBindingCallCount(lightingPassSource, "BindSampler", 9, 2);
    AssertSamplerDescriptor(lightingPassSource,
                            8,
                            "RHI::TextureAddressMode::Wrap",
                            "RHI::TextureAddressMode::Clamp",
                            "RHI::FilterMode::Point");
    AssertSamplerDescriptor(lightingPassSource,
                            9,
                            "RHI::TextureAddressMode::Clamp",
                            "RHI::TextureAddressMode::Clamp",
                            "RHI::FilterMode::Point");
    AssertSamplerDescriptor(lightingPassSource,
                            12,
                            "RHI::TextureAddressMode::Wrap",
                            "RHI::TextureAddressMode::Clamp",
                            "RHI::FilterMode::Point");
    AssertSamplerDescriptor(lightingPassSource,
                            13,
                            "RHI::TextureAddressMode::Wrap",
                            "RHI::TextureAddressMode::Clamp",
                            "RHI::FilterMode::Linear");

    uint32_t wrapPointSamplerCount = 0;
    uint32_t wrapLinearSamplerCount = 0;
    uint32_t clampPointSamplerCount = 0;
    std::size_t samplerPosition = 0;
    while ((samplerPosition = lightingPassSource.find("RHI::SamplerDesc", samplerPosition)) !=
           TestString::npos)
    {
        const std::size_t createSamplerPosition =
            FindTextAfter(lightingPassSource, "CreateSampler", samplerPosition);
        TestString samplerSource =
            RemoveWhitespace(MaskShaderNonCode(lightingPassSource.substr(
                samplerPosition,
                createSamplerPosition - samplerPosition)));
        if (ContainsText(samplerSource, "RHI::SamplerDescprefilterSamplerDesc"))
        {
            samplerSource =
                FindSamplerDescriptorSource(lightingPassSource,
                                            "m_PrefilteredSpecularSampler");
        }
        const bool bLinearMinMag =
            ContainsText(samplerSource, "filterMin=RHI::FilterMode::Linear") &&
            ContainsText(samplerSource, "filterMag=RHI::FilterMode::Linear");
        const bool bPointMip = ContainsText(samplerSource,
                                            "filterMip=RHI::FilterMode::Point");
        const bool bLinearMip = ContainsText(samplerSource,
                                             "filterMip=RHI::FilterMode::Linear");
        const bool bWrapU = ContainsText(samplerSource,
                                         "addressU=RHI::TextureAddressMode::Wrap");
        const bool bClampU = ContainsText(samplerSource,
                                          "addressU=RHI::TextureAddressMode::Clamp");
        const bool bClampV = ContainsText(samplerSource,
                                          "addressV=RHI::TextureAddressMode::Clamp");

        if (bLinearMinMag && bWrapU && bClampV && bPointMip)
        {
            ++wrapPointSamplerCount;
        }
        if (bLinearMinMag && bWrapU && bClampV && bLinearMip)
        {
            ++wrapLinearSamplerCount;
        }
        if (bLinearMinMag && bClampU && bClampV && bPointMip)
        {
            ++clampPointSamplerCount;
        }

        samplerPosition = createSamplerPosition + TestString("CreateSampler").size();
    }

    assert(wrapPointSamplerCount >= 1);
    assert(wrapLinearSamplerCount >= 1);
    assert(clampPointSamplerCount >= 1);

    const std::size_t descriptorFactoryPosition =
        FindText(lightingPassSource, "bool LightingPass::CreateLightingDescriptorSet");
    const std::size_t descriptorEnsurePosition =
        FindTextAfter(lightingPassSource,
                      "bool LightingPass::EnsureLightingDescriptorSet",
                      descriptorFactoryPosition);
    const TestString descriptorFactorySource = RemoveWhitespace(
        maskedLightingPassSource.substr(descriptorFactoryPosition,
                                        descriptorEnsurePosition - descriptorFactoryPosition));
    assert(ContainsText(descriptorFactorySource,
                        "BindConstantBuffer(4,m_LightDataBuffer,0u,LIGHTING_PARAMS_SIZE)"));
    assert(ContainsText(descriptorFactorySource,
                        "BindStorageBuffer(5,m_LightArrayBuffer,0u,GetLightArrayBufferSizeBytes())"));
    assert(ContainsText(descriptorFactorySource,
                        "BindTexture(8,m_DefaultBlackTexture)"));
    assert(ContainsText(descriptorFactorySource,
                        "BindTexture(9,m_BrdfLutTexture)"));
    assert(ContainsText(descriptorFactorySource,
                        "BindTexture(12,m_DefaultBlackTexture)"));
    assert(ContainsText(descriptorFactorySource,
                        "BindTexture(13,m_DefaultBlackTexture)"));
    assert(ContainsText(descriptorFactorySource,
                        "BindTexture(14,m_DefaultBlackTexture)"));
    assert(ContainsText(descriptorFactorySource,
                        "BindTexture(15,m_DefaultBlackTexture)"));
    assert(ContainsText(descriptorFactorySource,
                        "BindStorageBuffer(11,m_DefaultNeuralBRDFWeightBuffer,0u,4u)"));

    const std::size_t renderPassEnsurePosition =
        FindText(lightingPassSource, "bool LightingPass::EnsureLightingRenderPass");
    const std::size_t framebufferEnsurePosition =
        FindTextAfter(lightingPassSource,
                      "bool LightingPass::EnsureLightingFramebuffer",
                      renderPassEnsurePosition);
    const TestString renderPassEnsureSource = maskedLightingPassSource.substr(
        renderPassEnsurePosition,
        framebufferEnsurePosition - renderPassEnsurePosition);
    assert(!ContainsText(renderPassEnsureSource, "m_LightingDescriptorSet.reset"));

    assert(static_cast<uint8_t>(DebugViewMode::Normal) == 0);
    assert(static_cast<uint8_t>(DebugViewMode::Count) == 9);
    assert(CountText(shaderSource, "params.preExposure") == 1);

    const std::size_t preExposurePolicyPosition =
        FindText(shaderSource, "bool ShouldApplySceneColorPreExposure()");
    const std::size_t preExposureApplyPosition =
        FindTextAfter(shaderSource, "vec3 ApplySceneColorPreExposure", preExposurePolicyPosition);
    const TestString preExposurePolicy =
        shaderSource.substr(preExposurePolicyPosition,
                            preExposureApplyPosition - preExposurePolicyPosition);
    AssertPreExposurePolicy(maskedShaderSource,
                            preExposurePolicyPosition,
                            preExposureApplyPosition,
                            validationRaw252Mode,
                            validationLambertMode,
                            validationPbrMode,
                            validationRaw250Mode,
                            validationRaw251Mode);
    assert(ContainsText(preExposurePolicy, "DEBUG_VIEW_MODE_NORMAL"));
    assert(ContainsText(preExposurePolicy, validationRaw252Mode));
    assert(ContainsText(preExposurePolicy, validationLambertMode));
    assert(ContainsText(preExposurePolicy, validationPbrMode));
    assert(!ContainsText(preExposurePolicy, validationRaw250Mode));
    assert(!ContainsText(preExposurePolicy, validationRaw251Mode));

    const std::size_t lightDataPosition = FindText(shaderSource, "struct LightData");
    const std::size_t chromaticityPosition =
        FindTextAfter(shaderSource, "vec4 chromaticityAndIntensity;", lightDataPosition);
    const std::size_t lightDataEndPosition = FindTextAfter(shaderSource, "};", lightDataPosition);
    assert(chromaticityPosition < lightDataEndPosition);
    assert(!ContainsText(shaderSource, "vec4 color;"));
    assert(ContainsText(shaderSource,
                        "light.chromaticityAndIntensity.rgb * light.chromaticityAndIntensity.w"));
    assert(ContainsText(shaderSource,
                        "vec4 chromaticityAndIntensity; // xyz=Y=1 chromaticity, w=canonical lux/cd"));
    assert(!ContainsText(shaderSource, "float iblExposure = 0.15;"));
    assert(!ContainsText(shaderSource, "1.0) - exp(-"));

    const std::size_t directBrdfPosition =
        FindText(shaderSource, "void EvaluateAnalyticalDirectEndpointBRDF");
    const std::size_t firstIblPosition =
        FindTextAfter(shaderSource, "if (params.bIBLEnabled != 0u)", directBrdfPosition);
    const std::size_t iblPosition =
        FindTextAfter(shaderSource, "if (params.bIBLEnabled != 0u)",
                      firstIblPosition + TestString("if (params.bIBLEnabled != 0u)").size());
    const TestString directBrdfSource =
        shaderSource.substr(directBrdfPosition, iblPosition - directBrdfPosition);
    assert(ContainsText(directBrdfSource, "F0d"));
    assert(ContainsText(directBrdfSource, "F0c"));
    assert(ContainsText(directBrdfSource, "dielectricSpec"));
    assert(ContainsText(directBrdfSource, "conductorSpec"));
    assert(ContainsText(directBrdfSource, "dielectricSpec"));
    assert(ContainsText(directBrdfSource, "conductorSpec"));
    assert(ContainsText(directBrdfSource, "(1.0 - metallic) * dielectricSpec"));
    assert(ContainsText(directBrdfSource, "metallic * conductorSpec"));
    assert(!ContainsText(directBrdfSource, "F0 = mix(F0, albedo, metallic)"));

    const std::size_t iblOpeningBracePosition = shaderSource.find('{', iblPosition);
    const std::size_t iblEndPosition = FindMatchingBrace(shaderSource, iblOpeningBracePosition);
    const TestString iblSource = shaderSource.substr(iblPosition,
                                                      iblEndPosition + 1 - iblPosition);
    assert(ContainsText(iblSource, "vec2 brdf = texture(brdfLUT, dfgCoordinate).rg;"));
    assert(ContainsText(iblSource, "ambient = EvaluateIblEndpoint("));
    assert(!ContainsText(iblSource, "FresnelSchlickRoughness"));
    assert(!ContainsText(iblSource, "F_ambient * brdf"));
    assert(!ContainsText(iblSource, "exp("));

    const TestString prefilteredSamplerHelperName = "SamplePrefilteredSpecular";
    const TestString prefilteredSamplerHelperSignature =
        "vec3 SamplePrefilteredSpecular(vec3 direction, float roughness)";
    const TestString normalizedPrefilteredShaderSource = RemoveWhitespace(maskedShaderSource);
    assert(CountText(normalizedPrefilteredShaderSource,
                     RemoveWhitespace(prefilteredSamplerHelperSignature)) == 1);
    assert(CountFunctionCalls(maskedShaderSource, prefilteredSamplerHelperName) == 3);
    const ShaderLayoutDeclaration binding13Declaration =
        FindShaderLayoutDeclaration(shaderSource, 13);
    const TestString binding13SamplerName = binding13Declaration.resourceName;
    assert(!binding13SamplerName.empty());
    const std::size_t prefilteredSamplerHelperPosition =
        FindText(maskedShaderSource, prefilteredSamplerHelperSignature);
    const std::size_t prefilteredSamplerHelperOpeningBrace =
        FindNextCodeCharacter(maskedShaderSource,
                               '{',
                               prefilteredSamplerHelperPosition +
                                   prefilteredSamplerHelperSignature.size());
    const std::size_t prefilteredSamplerHelperClosingBrace =
        FindMatchingBrace(shaderSource, prefilteredSamplerHelperOpeningBrace);
    const TestString prefilteredSamplerHelperSource =
        RemoveWhitespace(maskedShaderSource.substr(
            prefilteredSamplerHelperPosition,
            prefilteredSamplerHelperClosingBrace + 1 - prefilteredSamplerHelperPosition));
    assert(ContainsText(prefilteredSamplerHelperSource,
                        "floatlod=roughness*float(params.prefilteredSpecularMipLevels-1u);"));
    assert(ContainsText(prefilteredSamplerHelperSource,
                        "vec2uv=EquirectangularUV(direction);"));
    assert(ContainsText(prefilteredSamplerHelperSource,
                        "returntextureLod(prefilteredSpecular,uv,lod).rgb;"));
    assert(CountTopLevelSamplerSamples(maskedShaderSource,
                                       binding13SamplerName,
                                       prefilteredSamplerHelperOpeningBrace + 1,
                                       prefilteredSamplerHelperClosingBrace) == 1);
    const std::size_t raw250Position =
        FindText(maskedShaderSource,
                 "if (params.debugViewMode == DEBUG_VIEW_MODE_RAW250)");
    const std::size_t raw250OpeningBrace =
        FindNextCodeCharacter(maskedShaderSource,
                               '{',
                               raw250Position + TestString(
                                   "if (params.debugViewMode == DEBUG_VIEW_MODE_RAW250)").size());
    const std::size_t raw250ClosingBrace =
        FindMatchingBrace(shaderSource, raw250OpeningBrace);
    const TestString maskedRaw250Source = MaskShaderNonCode(
        shaderSource.substr(raw250Position,
                            raw250ClosingBrace + 1 - raw250Position));
    assert(CountFunctionCalls(maskedRaw250Source, prefilteredSamplerHelperName) == 1);

    const std::size_t iblHelperPosition =
        FindText(shaderSource, "vec3 EvaluateIblEndpoint(");
    const std::size_t iblHelperOpeningBracePosition =
        shaderSource.find('{', iblHelperPosition);
    const std::size_t iblHelperEndPosition =
        FindMatchingBrace(shaderSource, iblHelperOpeningBracePosition);
    const TestString iblHelperSource = shaderSource.substr(
        iblHelperPosition,
        iblHelperEndPosition + 1 - iblHelperPosition);
    assert(ContainsText(iblHelperSource, "F0d"));
    assert(ContainsText(iblHelperSource, "F0c"));
    assert(ContainsText(iblHelperSource, "Ed"));
    assert(ContainsText(iblHelperSource, "Ec"));
    assert(ContainsText(iblHelperSource, "brdf.x"));
    assert(ContainsText(iblHelperSource, "brdf.y"));
    assert(!ContainsText(iblHelperSource, "FresnelSchlickRoughness"));
    assert(!ContainsText(iblHelperSource, "F_ambient * brdf"));
    assert(!ContainsText(iblHelperSource, "exp("));
    assert(CountFunctionCalls(MaskShaderNonCode(iblHelperSource),
                              prefilteredSamplerHelperName) == 1);

    assert(ContainsText(shaderSource,
                        "1.0 / max(distance * distance, 0.01 * 0.01)"));
    assert(!ContainsText(shaderSource, "1.0 / (distance * distance + 1.0)"));
    assert(ContainsText(shaderSource, "float CalculateRangeWindow(float distance, float range)"));
    assert(ContainsText(shaderSource, "max(range, 0.0001)"));
    assert(ContainsText(shaderSource, "return factor * factor;"));
    assert(CountText(shaderSource,
                     "CalculateInverseSquareAttenuation(distance) * CalculateRangeWindow(distance, " +
                         TestString("light.attenuation.x)")) == 2);

    assert(ContainsText(shaderSource, "float ComputeDebugDepth01(vec2 uv, float depth)"));
    assert(ContainsText(shaderSource, "vec3 worldPos = ReconstructWorldPosition(uv, depth);"));
    assert(ContainsText(shaderSource, "float cameraDistance = distance(params.cameraPosition.xyz, worldPos);"));
    assert(ContainsText(shaderSource, "float depth01 = cameraDistance / (cameraDistance + 25.0);"));
    assert(ContainsText(shaderSource, "return clamp(depth01, 0.0, 1.0);"));

    const std::size_t gbufferSamplePosition =
        FindText(shaderSource, "float depthSample = texture(gbufferDepth, fragUV).r;");
    const std::size_t rawAlbedoPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_ALBEDO", gbufferSamplePosition);
    const std::size_t rawNormalPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_NORMAL", gbufferSamplePosition);
    const std::size_t rawMaterialPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_MATERIAL", gbufferSamplePosition);
    const std::size_t rawDepthPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_DEPTH", gbufferSamplePosition);
    const TestString alphaGuardText = "if (albedoSample.a < 0.01)";
    const std::size_t depthAlphaGuardPosition =
        FindTextAfter(shaderSource, alphaGuardText, rawDepthPosition);
    const std::size_t skyBranchPosition =
        FindTextAfter(shaderSource,
                      alphaGuardText,
                      depthAlphaGuardPosition + alphaGuardText.size());

    assert(rawAlbedoPosition < skyBranchPosition);
    assert(rawNormalPosition < skyBranchPosition);
    assert(rawMaterialPosition < skyBranchPosition);
    assert(rawDepthPosition < skyBranchPosition);

    assert(ContainsText(shaderSource, "vec3 debugNormal = normalize(normalSample.xyz) * 0.5 + 0.5;"));
    assert(ContainsText(shaderSource, "float depth01 = ComputeDebugDepth01(fragUV, depthSample);"));

    const std::size_t rawAlbedoOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(albedoSample.rgb, 1.0);", rawAlbedoPosition);
    const std::size_t rawNormalOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(debugNormal, 1.0);", rawNormalPosition);
    const std::size_t rawMaterialOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(materialSample.rgb, 1.0);", rawMaterialPosition);
    const std::size_t rawDepthOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(vec3(depth01), 1.0);", rawDepthPosition);

    assert(rawAlbedoOutputPosition < rawNormalPosition);
    assert(rawNormalOutputPosition < rawMaterialPosition);
    assert(rawMaterialOutputPosition < rawDepthPosition);
    assert(rawDepthOutputPosition < skyBranchPosition);

    const std::size_t albedoPassthroughPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_UNLIT ||", skyBranchPosition);
    FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_WIREFRAME ||", albedoPassthroughPosition);
    FindTextAfter(shaderSource,
                  "params.debugViewMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS ||",
                  albedoPassthroughPosition);
    const std::size_t lodPassthroughPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_LOD_LEVEL", albedoPassthroughPosition);
    const std::size_t passthroughOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(albedoSample.rgb, 1.0);", albedoPassthroughPosition);

    assert(lodPassthroughPosition < passthroughOutputPosition);

    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_UNLIT ||"));
    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_WIREFRAME ||"));
    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS"));

    const std::size_t skyOutputPosition = shaderSource.find("outColor = vec4(skyColor, 1.0);", skyBranchPosition);
    assert(skyOutputPosition == TestString::npos);

    assert(ContainsText(shaderSource, "return sceneColor * params.preExposure;"));
    assert(CountText(shaderSource, "vec3 ApplySceneColorPreExposure(vec3 sceneColor)") == 1);
    assert(CountText(shaderSource, "ApplySceneColorPreExposure(skyColor)") == 1);
    assert(CountText(shaderSource, "ApplySceneColorPreExposure(color)") == 1);
    assert(ContainsText(shaderSource, "if (bValidationLambert)"));
    assert(ContainsText(shaderSource, "Lo_diffuse += (albedo / PI) * radiance;"));
    assert(ContainsText(shaderSource,
                        "else if (bValidationPBR || params.bNeuralBRDFEnabled == 0u)"));
    assert(ContainsText(shaderSource,
                        "if (!bValidationLambert && !bValidationPBR)"));
    assert(ContainsText(shaderSource,
                        "if (!bValidationLambert && lightType < 0.5 && params.bShadowEnabled != 0u)"));
    assert(ContainsText(shaderSource, "color = Lo_diffuse;"));

    std::cout << "LightingParamsLayoutTest passed\n";
    return 0;
}
