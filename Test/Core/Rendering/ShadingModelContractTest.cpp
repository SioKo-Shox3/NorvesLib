// GBuffer の定数表と、シェーダーの符号化・復号を通る境界を検査する。
#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Platform/PlatformFile.h"
#include "Rendering/ShadingModelIds.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <system_error>

using NorvesLib::Core::Container::String;
using NorvesLib::Core::Container::VariableArray;
using NorvesLib::Core::Container::WideString;
namespace GBuffer = NorvesLib::Core::Rendering::GBufferContract;
namespace Platform = NorvesLib::Core::Platform;
namespace Fs = std::filesystem;

namespace
{
    constexpr size_t InvalidIndex = static_cast<size_t>(-1);

    struct Token
    {
        size_t Begin;
        size_t End;
        size_t Line;
    };

    struct LexedSource
    {
        String Clean;
        VariableArray<Token> Tokens;
        VariableArray<size_t> ClosingParen;
    };

    struct Constant
    {
        String Name;
        uint32_t Value;
    };

    enum class Rule
    {
        Output,
        ImageStore,
        Sample
    };

    struct Violation
    {
        Rule BrokenRule;
        size_t Offset;
        size_t Line;
    };

    struct ScanResult
    {
        size_t Outputs = 0;
        size_t ImageStores = 0;
        size_t Samples = 0;
        VariableArray<Violation> Violations;
    };

    bool IsIdentifierStart(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    bool IsDigit(char c)
    {
        return c >= '0' && c <= '9';
    }

    bool IsIdentifierPart(char c)
    {
        return IsIdentifierStart(c) || IsDigit(c);
    }

    bool IsSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

    // コメントと文字列を空白に置き換え、元の位置と行番号を保つ。
    LexedSource Lex(const String& source)
    {
        LexedSource result;
        result.Clean = source;
        for (size_t i = 0; i < source.size();)
        {
            const bool bLineComment = source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/';
            const bool bBlockComment = source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*';
            const bool bQuoted = source[i] == '"' || source[i] == '\'';
            if (!bLineComment && !bBlockComment && !bQuoted)
            {
                ++i;
                continue;
            }

            const char quote = source[i];
            const size_t start = i;
            i += bQuoted ? 1 : 2;
            while (i < source.size())
            {
                if (bLineComment && source[i] == '\n')
                {
                    break;
                }
                if (bBlockComment && source[i] == '*' && i + 1 < source.size() && source[i + 1] == '/')
                {
                    i += 2;
                    break;
                }
                if (bQuoted && source[i] == '\\' && i + 1 < source.size())
                {
                    i += 2;
                    continue;
                }
                if (bQuoted && source[i] == quote)
                {
                    ++i;
                    break;
                }
                ++i;
            }
            for (size_t j = start; j < i; ++j)
            {
                if (source[j] != '\n' && source[j] != '\r')
                {
                    result.Clean[j] = ' ';
                }
            }
        }

        size_t line = 1;
        for (size_t i = 0; i < result.Clean.size();)
        {
            if (IsSpace(result.Clean[i]))
            {
                if (result.Clean[i] == '\n')
                {
                    ++line;
                }
                ++i;
                continue;
            }
            const size_t begin = i++;
            if (IsIdentifierPart(result.Clean[begin]))
            {
                while (i < result.Clean.size() && IsIdentifierPart(result.Clean[i]))
                {
                    ++i;
                }
            }
            result.Tokens.push_back({begin, i, line});
        }

        result.ClosingParen.resize(result.Tokens.size(), InvalidIndex);
        VariableArray<size_t> open;
        for (size_t i = 0; i < result.Tokens.size(); ++i)
        {
            const char c = result.Clean[result.Tokens[i].Begin];
            if (result.Tokens[i].End == result.Tokens[i].Begin + 1 && c == '(')
            {
                open.push_back(i);
            }
            else if (result.Tokens[i].End == result.Tokens[i].Begin + 1 && c == ')' && !open.empty())
            {
                result.ClosingParen[open.back()] = i;
                open.pop_back();
            }
        }
        return result;
    }

    bool TokenIs(const LexedSource& source, size_t index, const char* value)
    {
        if (index >= source.Tokens.size())
        {
            return false;
        }
        const Token& token = source.Tokens[index];
        const size_t length = std::strlen(value);
        return token.End - token.Begin == length && std::memcmp(source.Clean.c_str() + token.Begin, value, length) == 0;
    }

    bool TokenStartsWith(const LexedSource& source, size_t index, const char* prefix)
    {
        if (index >= source.Tokens.size())
        {
            return false;
        }
        const Token& token = source.Tokens[index];
        const size_t length = std::strlen(prefix);
        return token.End - token.Begin >= length && std::memcmp(source.Clean.c_str() + token.Begin, prefix, length) == 0;
    }

    String TokenText(const LexedSource& source, size_t index)
    {
        const Token& token = source.Tokens[index];
        return source.Clean.substr(token.Begin, token.End - token.Begin);
    }

    bool ParseUint(const String& text, uint32_t& value)
    {
        uint64_t number = 0;
        size_t i = 0;
        while (i < text.size() && IsDigit(text[i]))
        {
            number = number * 10 + static_cast<uint32_t>(text[i] - '0');
            if (number > UINT32_MAX)
            {
                return false;
            }
            ++i;
        }
        if (i == 0 || (i != text.size() && !(i + 1 == text.size() && text[i] == 'u')))
        {
            return false;
        }
        value = static_cast<uint32_t>(number);
        return true;
    }

    VariableArray<Constant> FindConstants(const String& text, bool bCpp)
    {
        const LexedSource source = Lex(text);
        VariableArray<Constant> constants;
        for (size_t i = 0; i < source.Tokens.size(); ++i)
        {
            size_t nameIndex = InvalidIndex;
            if (bCpp && TokenIs(source, i, "inline") && TokenIs(source, i + 1, "constexpr") &&
                TokenIs(source, i + 2, "uint32_t"))
            {
                nameIndex = i + 3;
            }
            else if (!bCpp && TokenIs(source, i, "const") && TokenIs(source, i + 1, "uint") &&
                     TokenStartsWith(source, i + 2, "GBUFFER_"))
            {
                nameIndex = i + 2;
            }
            uint32_t value = 0;
            if (nameIndex != InvalidIndex && nameIndex + 3 < source.Tokens.size() &&
                IsIdentifierStart(source.Clean[source.Tokens[nameIndex].Begin]) &&
                TokenIs(source, nameIndex + 1, "=") && TokenIs(source, nameIndex + 3, ";") &&
                ParseUint(TokenText(source, nameIndex + 2), value))
            {
                constants.push_back({TokenText(source, nameIndex), value});
            }
        }
        return constants;
    }

    String GlslName(const String& cppName)
    {
        String name = "GBUFFER_";
        for (size_t i = 0; i < cppName.size(); ++i)
        {
            const char c = cppName[i];
            const bool bUpper = c >= 'A' && c <= 'Z';
            const bool bPreviousLower = i > 0 && ((cppName[i - 1] >= 'a' && cppName[i - 1] <= 'z') || IsDigit(cppName[i - 1]));
            const bool bAcronymEnd = i > 0 && cppName[i - 1] >= 'A' && cppName[i - 1] <= 'Z' &&
                                     i + 1 < cppName.size() && cppName[i + 1] >= 'a' && cppName[i + 1] <= 'z';
            if (bUpper && (bPreviousLower || bAcronymEnd))
            {
                name += '_';
            }
            name += bUpper ? c : (c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        }
        return name;
    }

    const Constant* FindConstant(const VariableArray<Constant>& constants, const String& name)
    {
        for (const Constant& constant : constants)
        {
            if (constant.Name == name)
            {
                return &constant;
            }
        }
        return nullptr;
    }

    bool CheckConstants(const String& cppText, const String& glslText)
    {
        const VariableArray<Constant> cpp = FindConstants(cppText, true);
        const VariableArray<Constant> glsl = FindConstants(glslText, false);
        bool bOk = true;
        std::cout << "定数の検出数: C++=" << cpp.size() << " GLSL=" << glsl.size() << '\n';
        if (cpp.empty() || glsl.empty())
        {
            std::cerr << "定数表が空です。\n";
            bOk = false;
        }
        for (size_t i = 0; i < cpp.size(); ++i)
        {
            const String name = GlslName(cpp[i].Name);
            const Constant* match = FindConstant(glsl, name);
            if (match == nullptr)
            {
                std::cerr << "GLSL に対応する定数がありません: " << cpp[i].Name << " -> " << name << '\n';
                bOk = false;
            }
            else if (match->Value != cpp[i].Value)
            {
                std::cerr << "定数の値が違います: " << cpp[i].Name << "=" << cpp[i].Value << ", "
                          << name << "=" << match->Value << '\n';
                bOk = false;
            }
            for (size_t j = 0; j < i; ++j)
            {
                if (cpp[i].Name == cpp[j].Name || name == GlslName(cpp[j].Name))
                {
                    std::cerr << "C++ の定数名または変換後の名前が重複しています: " << cpp[i].Name << '\n';
                    bOk = false;
                }
            }
        }
        for (size_t i = 0; i < glsl.size(); ++i)
        {
            bool bFound = false;
            for (const Constant& constant : cpp)
            {
                if (GlslName(constant.Name) == glsl[i].Name)
                {
                    bFound = true;
                }
            }
            if (!bFound)
            {
                std::cerr << "C++ に対応する定数がありません: " << glsl[i].Name << '\n';
                bOk = false;
            }
            for (size_t j = 0; j < i; ++j)
            {
                if (glsl[i].Name == glsl[j].Name)
                {
                    std::cerr << "GLSL の定数名が重複しています: " << glsl[i].Name << '\n';
                    bOk = false;
                }
            }
        }

        struct ExpectedConstant
        {
            const char* Name;
            uint32_t Compiled;
            uint32_t Expected;
        };
        const ExpectedConstant required[] = {
            {"ShadingModelDefaultLit", GBuffer::ShadingModelDefaultLit, 0u},
            {"ShadingModelFur", GBuffer::ShadingModelFur, 1u},
            {"ShadingModelEye", GBuffer::ShadingModelEye, 2u},
            {"ShadingModelPaleFlat", GBuffer::ShadingModelPaleFlat, 3u},
            {"ShadingModelIdBits", GBuffer::ShadingModelIdBits, 4u},
            {"ShadingModelIdMask", GBuffer::ShadingModelIdMask, 15u},
            {"ReceiverFlagTerrain", GBuffer::ReceiverFlagTerrain, 1u},
            {"ReceiverFlagTightTerrainShadow", GBuffer::ReceiverFlagTightTerrainShadow, 2u},
            {"ReceiverFlagFoliageClump", GBuffer::ReceiverFlagFoliageClump, 4u},
            {"ReceiverFlagDiffuseOnly", GBuffer::ReceiverFlagDiffuseOnly, 8u},
            {"ReceiverFlagMask", GBuffer::ReceiverFlagMask, 15u},
        };
        for (const ExpectedConstant& expected : required)
        {
            const Constant* found = FindConstant(cpp, expected.Name);
            if (found == nullptr || found->Value != expected.Compiled || expected.Compiled != expected.Expected)
            {
                std::cerr << "必須の定数がないか値が違います: " << expected.Name << "（期待値 " << expected.Expected << "）\n";
                bOk = false;
            }
        }
        return bOk;
    }

    bool IsOutput(const LexedSource& source, size_t i)
    {
        return TokenIs(source, i, "outNormal") || TokenIs(source, i, "outMaterial") || TokenIs(source, i, "outEmissive");
    }

    bool IsImage(const LexedSource& source, size_t i)
    {
        return TokenIs(source, i, "normalImage") || TokenIs(source, i, "materialImage") || TokenIs(source, i, "emissiveImage");
    }

    bool IsSampler(const LexedSource& source, size_t i)
    {
        return TokenIs(source, i, "gbufferNormal") || TokenIs(source, i, "gbufferMaterial") ||
               TokenIs(source, i, "gbufferEmissive") || TokenIs(source, i, "gbufferNormalCopy");
    }

    // 第３引数だけを調べ、座標式や別の文にある符号化関数との誤認を防ぐ。
    bool ImageValueHasEncoder(const LexedSource& source, size_t open)
    {
        const size_t close = source.ClosingParen[open];
        if (close == InvalidIndex)
        {
            return false;
        }
        size_t commaCount = 0;
        size_t brackets = 0;
        for (size_t i = open + 1; i < close; ++i)
        {
            if (TokenIs(source, i, "("))
            {
                if (commaCount >= 2 && i > 0 && TokenStartsWith(source, i - 1, "GBufferEncode"))
                {
                    return true;
                }
                // 値の式の中は入れ子の符号化呼び出しも探す。
                if (commaCount < 2)
                {
                    if (source.ClosingParen[i] == InvalidIndex || source.ClosingParen[i] >= close)
                    {
                        return false;
                    }
                    i = source.ClosingParen[i];
                }
            }
            else if (TokenIs(source, i, "["))
            {
                ++brackets;
            }
            else if (TokenIs(source, i, "]") && brackets > 0)
            {
                --brackets;
            }
            else if (TokenIs(source, i, ",") && brackets == 0 && commaCount < 2)
            {
                ++commaCount;
            }
        }
        return false;
    }

    bool IsDecodedArgument(const LexedSource& source, const VariableArray<size_t>& open)
    {
        for (size_t paren : open)
        {
            if (paren > 0 && (TokenStartsWith(source, paren - 1, "GBufferDecode") ||
                              TokenIs(source, paren - 1, "GBufferHasReceiverFlag")))
            {
                return true;
            }
        }
        return false;
    }

    bool HasSafeSwizzle(const LexedSource& source, size_t close)
    {
        if (!TokenIs(source, close + 1, ".") || close + 2 >= source.Tokens.size())
        {
            return false;
        }
        const String swizzle = TokenText(source, close + 2);
        if (swizzle.empty() || !IsIdentifierStart(swizzle[0]))
        {
            return false;
        }
        return swizzle.find('w') == String::npos && swizzle.find('a') == String::npos && swizzle.find('q') == String::npos;
    }

    ScanResult ScanShader(const String& text)
    {
        const LexedSource source = Lex(text);
        ScanResult result;
        VariableArray<size_t> open;
        for (size_t i = 0; i < source.Tokens.size(); ++i)
        {
            const Token& token = source.Tokens[i];
            if (IsOutput(source, i) && TokenIs(source, i + 1, "=") && !TokenIs(source, i + 2, "="))
            {
                ++result.Outputs;
                if (!TokenStartsWith(source, i + 2, "GBufferEncode") || !TokenIs(source, i + 3, "("))
                {
                    result.Violations.push_back({Rule::Output, token.Begin, token.Line});
                }
            }
            if (TokenIs(source, i, "imageStore") && TokenIs(source, i + 1, "(") &&
                IsImage(source, i + 2) && TokenIs(source, i + 3, ","))
            {
                ++result.ImageStores;
                if (!ImageValueHasEncoder(source, i + 1))
                {
                    result.Violations.push_back({Rule::ImageStore, token.Begin, token.Line});
                }
            }
            if ((TokenIs(source, i, "texelFetch") || TokenIs(source, i, "texture") || TokenIs(source, i, "textureLod")) &&
                TokenIs(source, i + 1, "(") && IsSampler(source, i + 2) && TokenIs(source, i + 3, ","))
            {
                ++result.Samples;
                const size_t close = source.ClosingParen[i + 1];
                if (!IsDecodedArgument(source, open) && (close == InvalidIndex || !HasSafeSwizzle(source, close)))
                {
                    result.Violations.push_back({Rule::Sample, token.Begin, token.Line});
                }
            }
            if (TokenIs(source, i, "("))
            {
                open.push_back(i);
            }
            else if (TokenIs(source, i, ")") && !open.empty())
            {
                open.pop_back();
            }
        }
        return result;
    }

    bool CheckScanner()
    {
        const String example =
            "outNormal = vec4(n, 1.0);\n"
            "float a = texelFetch(gbufferMaterial, p, 0).a;\n"
            "vec4 s = texture(gbufferNormal, uv);\n"
            "outNormal = GBufferEncodeNormal(n, 0u, 0u);\n"
            "vec3 n = texture(gbufferNormal, uv).xyz;\n"
            "GBufferDecodeNormal(texelFetch(gbufferNormal, p, 0));\n"
            "imageStore(normalImage, ivec2(0),\n"
            "    vec4(0.0));\n"
            "imageStore(materialImage, ivec2(0),\n"
            "    GBufferEncodeMaterial(vec3(0.0), 0.0));\n"
            "float b = textureLod(gbufferEmissive, uv, 0.0).rgbq;\n"
            "// outMaterial = vec4(0.0); texture(gbufferNormal, uv);\n"
            "/* imageStore(normalImage, p, vec4(0.0));\n"
            "   outEmissive = vec4(0.0); */\n"
            "bool same = outNormal == vec4(0.0);\n";
        const ScanResult result = ScanShader(example);
        const size_t expectedLines[] = {1, 2, 3, 7, 11};
        const Rule expectedRules[] = {Rule::Output, Rule::Sample, Rule::Sample, Rule::ImageStore, Rule::Sample};
        bool bOk = result.Outputs == 2 && result.ImageStores == 2 && result.Samples == 5 &&
                   result.Violations.size() == sizeof(expectedLines) / sizeof(expectedLines[0]);
        if (bOk)
        {
            for (size_t i = 0; i < result.Violations.size(); ++i)
            {
                if (result.Violations[i].Line != expectedLines[i] || result.Violations[i].BrokenRule != expectedRules[i])
                {
                    bOk = false;
                }
            }
        }
        std::cout << "走査器の自己点検: " << (bOk ? "成功" : "失敗") << '\n';
        return bOk;
    }

    Fs::path SourceRoot()
    {
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, NORVES_SOURCE_ROOT, -1, nullptr, 0);
        if (length <= 1)
        {
            return {};
        }
        WideString path(static_cast<size_t>(length - 1), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, NORVES_SOURCE_ROOT, -1, &path[0], length) == 0)
        {
            return {};
        }
        return Fs::path(path.c_str());
    }

    // filesystem の Windows パスを、Core のファイル API が受け取る UTF-8 にする。
    String Utf8Path(const Fs::path& path)
    {
        const wchar_t* wide = path.c_str();
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1)
        {
            return {};
        }
        String result(static_cast<size_t>(length - 1), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, &result[0], length, nullptr, nullptr) == 0)
        {
            return {};
        }
        return result;
    }

    bool ReadText(const Fs::path& path, String& text)
    {
        const String utf8Path = Utf8Path(path);
        VariableArray<uint8_t> bytes;
        if (utf8Path.empty() || !Platform::ReadFileBytes(utf8Path, bytes).bSuccess)
        {
            std::cerr << "ファイルを読めません: " << utf8Path << '\n';
            return false;
        }
        text.clear();
        if (!bytes.empty())
        {
            text.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        return true;
    }

    bool IsShader(const Fs::path& path)
    {
        const Fs::path extension = path.extension();
        return extension == L".frag" || extension == L".vert" || extension == L".comp" || extension == L".glsl" ||
               extension == L".rgen" || extension == L".rchit" || extension == L".rmiss" || extension == L".rahit";
    }

    void ReportViolation(const Fs::path& path, const String& source, const Violation& violation)
    {
        size_t begin = violation.Offset;
        while (begin > 0 && source[begin - 1] != '\n')
        {
            --begin;
        }
        size_t end = violation.Offset;
        while (end < source.size() && source[end] != '\n' && source[end] != '\r')
        {
            ++end;
        }
        const char* reason = violation.BrokenRule == Rule::Output ? "出力の右辺が符号化関数ではありません" :
                             violation.BrokenRule == Rule::ImageStore ? "画像の値に符号化関数がありません" :
                             "第４成分を復号関数を通さずに読んでいます";
        std::cerr << Utf8Path(path) << ':' << violation.Line << ": " << source.substr(begin, end - begin)
                  << "（" << reason << "）\n";
    }

    bool CheckShaders(const Fs::path& root)
    {
        size_t files = 0;
        size_t outputs = 0;
        size_t stores = 0;
        size_t samples = 0;
        bool bOk = true;
        std::error_code error;
        Fs::recursive_directory_iterator iterator(root, error);
        const Fs::recursive_directory_iterator end;
        if (error)
        {
            std::cerr << "シェーダーディレクトリを走査できません: " << Utf8Path(root) << '\n';
            return false;
        }
        while (iterator != end)
        {
            const Fs::path path = iterator->path();
            const bool bRegular = iterator->is_regular_file(error);
            if (error)
            {
                std::cerr << "ファイル情報を取得できません: " << Utf8Path(path) << '\n';
                bOk = false;
                break;
            }
            if (bRegular && IsShader(path) && path != root / L"Common" / L"GBufferEncoding.glsl")
            {
                ++files;
                String text;
                if (!ReadText(path, text))
                {
                    bOk = false;
                }
                else
                {
                    const ScanResult result = ScanShader(text);
                    outputs += result.Outputs;
                    stores += result.ImageStores;
                    samples += result.Samples;
                    for (const Violation& violation : result.Violations)
                    {
                        ReportViolation(path, text, violation);
                        bOk = false;
                    }
                }
            }
            iterator.increment(error);
            if (error)
            {
                std::cerr << "シェーダーディレクトリの走査に失敗しました: " << Utf8Path(path) << '\n';
                bOk = false;
                break;
            }
        }
        std::cout << "走査したファイル数: " << files << '\n'
                  << "出力への代入: " << outputs << '\n'
                  << "画像への書き込み: " << stores << '\n'
                  << "標本の読み取り: " << samples << '\n';
        if (files == 0 || outputs == 0 || stores == 0 || samples == 0)
        {
            std::cerr << "ファイルまたはいずれかの規則の検出数がゼロです。\n";
            bOk = false;
        }
        return bOk;
    }
}

int main()
{
    const Fs::path root = SourceRoot();
    if (root.empty())
    {
        std::cerr << "ソースディレクトリのパスを変換できません。\n";
        return 1;
    }
    const Fs::path shaderRoot = root / L"Assets" / L"Shaders";
    String cppText;
    String glslText;
    const bool bCppRead = ReadText(root / L"Library" / L"Core" / L"Public" / L"Rendering" / L"ShadingModelIds.h", cppText);
    const bool bGlslRead = ReadText(shaderRoot / L"Common" / L"GBufferEncoding.glsl", glslText);
    const bool bConstants = bCppRead && bGlslRead && CheckConstants(cppText, glslText);
    const bool bScanner = CheckScanner();
    const bool bShaders = CheckShaders(shaderRoot);
    const bool bOk = bConstants && bScanner && bShaders;
    std::cout << "ShadingModelContractTest: " << (bOk ? "成功" : "失敗") << '\n';
    return bOk ? 0 : 1;
}
