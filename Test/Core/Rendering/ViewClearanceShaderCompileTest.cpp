// 切り抜きの共通定義を含むシェーダーを GPU を使わずコンパイルする。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include "FileStream/FileStream.h"
#include <cstddef>
#include <iostream>

namespace
{
    using NorvesLib::Core::Container::String;
    using NorvesLib::Core::Container::VariableArray;
    using NorvesLib::RHI::ShaderCompileResult;
    using NorvesLib::RHI::ShaderStage;
    using NorvesLib::RHI::Vulkan::VulkanShaderCompiler;

    // ShaderManager と同じシェーダールートを基準に include を展開する。
    bool ExpandShader(const String& root, const String& relativePath, String& expanded,
                      bool& bOpenedViewClearance, bool& bFoliageDataIncludesViewClearance, unsigned depth = 0)
    {
        if (depth >= 16)
        {
            std::cerr << "[失敗] include の再帰が深すぎます: " << relativePath.c_str() << std::endl;
            return false;
        }
        NorvesLib::FileStream::FileStream file;
        if (!file.Open(root + relativePath, NorvesLib::FileStream::FileMode::Read,
                       NorvesLib::FileStream::FileAccess::Read))
        {
            std::cerr << "[失敗] シェーダーを開けません: " << (root + relativePath).c_str() << std::endl;
            return false;
        }
        const auto length = file.GetSize();
        if (length < 0)
        {
            std::cerr << "[失敗] シェーダーのサイズを読めません。" << std::endl;
            return false;
        }
        VariableArray<char> bytes(static_cast<std::size_t>(length) + 1, '\0');
        if (file.Read(bytes.data(), static_cast<std::size_t>(length)) != static_cast<std::size_t>(length))
        {
            std::cerr << "[失敗] シェーダーを最後まで読めません。" << std::endl;
            return false;
        }
        if (relativePath == "Common/ViewClearance.glsl")
        {
            bOpenedViewClearance = true;
        }
        const bool bHasBom = length >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
                             static_cast<unsigned char>(bytes[1]) == 0xBB &&
                             static_cast<unsigned char>(bytes[2]) == 0xBF;
        const String source(bytes.data() + (bHasBom ? 3 : 0));
        std::size_t start = 0;
        while (start < source.size())
        {
            const auto newline = source.find('\n', start);
            const auto end = newline == String::npos ? source.size() : newline;
            const String line = source.substr(start, end - start);
            std::size_t cursor = 0;
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
            {
                ++cursor;
            }
            if (line.find("#include", cursor) == cursor)
            {
                cursor += 8;
                while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
                {
                    ++cursor;
                }
                const auto closing = line.find('"', cursor + 1);
                if (cursor >= line.size() || line[cursor] != '"' || closing == String::npos)
                {
                    std::cerr << "[失敗] 引用符付き include を解釈できません: " << line.c_str() << std::endl;
                    return false;
                }
                const String include = line.substr(cursor + 1, closing - cursor - 1);
                if (include.empty() || include[0] == '/' || include.find("..") != String::npos ||
                    include.find(':') != String::npos || include.find('\\') != String::npos)
                {
                    std::cerr << "[失敗] include はシェーダールート内の相対パスで指定してください。" << std::endl;
                    return false;
                }
                if (!ExpandShader(root, include, expanded, bOpenedViewClearance,
                                  bFoliageDataIncludesViewClearance, depth + 1))
                {
                    return false;
                }
                if (relativePath == "Common/FoliageData.glsl" && include == "Common/ViewClearance.glsl")
                {
                    bFoliageDataIncludesViewClearance = true;
                }
            }
            else
            {
                expanded += line;
            }
            expanded += "\n";
            start = end + 1;
        }
        return true;
    }

}

int main()
{
    const String root = NORVES_SOURCE_ROOT "/Assets/Shaders/";
    const struct ShaderCase
    {
        const char* Path;
        ShaderStage Stage;
        bool bRequireFoliageData;
    } shaders[] = {
        {"Grass/GrassGenerate.comp", ShaderStage::Compute, false},
        {"foliage.vert", ShaderStage::Vertex, false},
        {"foliage.frag", ShaderStage::Pixel, true}};

    VulkanShaderCompiler compiler;
    bool allPassed = true;
    for (const auto& shader : shaders)
    {
        String source;
        bool bOpenedViewClearance = false;
        bool bFoliageDataIncludesViewClearance = false;
        if (!ExpandShader(root, shader.Path, source, bOpenedViewClearance, bFoliageDataIncludesViewClearance))
        {
            allPassed = false;
            continue;
        }
        if (!bOpenedViewClearance)
        {
            std::cerr << "[失敗] " << shader.Path << " から切り抜きの共通定義を読み込んでいません。" << std::endl;
            allPassed = false;
            continue;
        }
        if (shader.bRequireFoliageData && !bFoliageDataIncludesViewClearance)
        {
            std::cerr << "[失敗] " << shader.Path << " が株の共通定義を経由して切り抜きを読み込んでいません。" << std::endl;
            allPassed = false;
            continue;
        }
        const ShaderCompileResult result = compiler.CompileFromSource(source, shader.Stage, shader.Path);
        if (!result.bSuccess || result.ByteCode.empty())
        {
            std::cerr << "[失敗] " << shader.Path << " のコンパイルまたは SPIR-V の生成に失敗しました: "
                      << result.ErrorMessage.c_str() << std::endl;
            allPassed = false;
            continue;
        }
        std::cout << "[成功] " << shader.Path << " の共通定義とコンパイルを確認しました。" << std::endl;
    }
    return allPassed ? 0 : 1;
}
