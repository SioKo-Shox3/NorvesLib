// 草の生成・引数の初期化・描画を、GPU を使わず実際のシェーダーからコンパイルする。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include "FileStream/FileStream.h"
#include <spirv-headers/spirv.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    using NorvesLib::Core::Container::String;
    using NorvesLib::Core::Container::VariableArray;
    using NorvesLib::RHI::ShaderCompileResult;
    using NorvesLib::RHI::ShaderStage;
    using NorvesLib::RHI::Vulkan::VulkanShaderCompiler;

    // ShaderManager と同じシェーダールートを基準に include を展開する。
    bool ExpandShader(const String& root, const String& relativePath, String& expanded, unsigned depth = 0)
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
                if (!ExpandShader(root, include, expanded, depth + 1))
                {
                    return false;
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

    bool TryReadExecutionModel(const ShaderCompileResult& result, std::uint32_t& executionModel)
    {
        const auto& byteCode = result.ByteCode;
        if (byteCode.size() % sizeof(std::uint32_t) != 0)
        {
            return false;
        }
        const std::size_t wordCount = byteCode.size() / sizeof(std::uint32_t);
        if (wordCount < 5 || byteCode.data() == nullptr)
        {
            return false;
        }
        const auto readWord = [&byteCode](std::size_t index)
        {
            std::uint32_t word = 0;
            std::memcpy(&word, byteCode.data() + index * sizeof(std::uint32_t), sizeof(word));
            return word;
        };
        if (readWord(0) != spv::MagicNumber)
        {
            return false;
        }
        std::size_t cursor = 5;
        std::uint32_t entryPointCount = 0;
        while (cursor < wordCount)
        {
            const std::uint32_t instruction = readWord(cursor);
            const std::size_t instructionWordCount = instruction >> spv::WordCountShift;
            const std::uint32_t opcode = instruction & spv::OpCodeMask;
            if (instructionWordCount == 0 || instructionWordCount > wordCount - cursor)
            {
                return false;
            }
            if (opcode == static_cast<std::uint32_t>(spv::OpEntryPoint))
            {
                if (instructionWordCount < 4 || ++entryPointCount != 1)
                {
                    return false;
                }
                executionModel = readWord(cursor + 1);
            }
            cursor += instructionWordCount;
        }
        return entryPointCount == 1;
    }
}

int main()
{
    const String root = NORVES_SOURCE_ROOT "/Assets/Shaders/";
    const struct ShaderCase
    {
        const char* Path;
        ShaderStage Stage;
        spv::ExecutionModel Model;
    } paths[] = {
        {"Grass/GrassArgsInit.comp", ShaderStage::Compute, spv::ExecutionModelGLCompute},
        {"Grass/GrassGenerate.comp", ShaderStage::Compute, spv::ExecutionModelGLCompute},
        {"Grass/grass.vert", ShaderStage::Vertex, spv::ExecutionModelVertex},
        {"Grass/grass.frag", ShaderStage::Pixel, spv::ExecutionModelFragment}};
    VulkanShaderCompiler compiler;
    bool allPassed = true;
    for (const auto& shader : paths)
    {
        const char* path = shader.Path;
        String source;
        if (!ExpandShader(root, path, source))
        {
            allPassed = false;
            continue;
        }
        const ShaderCompileResult result = compiler.CompileFromSource(source, shader.Stage, path);
        if (!result.bSuccess || result.ByteCode.empty())
        {
            std::cerr << "[失敗] " << path << " のコンパイルまたは SPIR-V の生成に失敗しました: "
                      << result.ErrorMessage.c_str() << std::endl;
            allPassed = false;
            continue;
        }
        std::uint32_t executionModel = 0;
        if (!TryReadExecutionModel(result, executionModel) || executionModel != shader.Model)
        {
            std::cerr << "[失敗] " << path << " の実行モデルが指定したステージと一致しません。" << std::endl;
            allPassed = false;
            continue;
        }
        std::cout << "[成功] " << path << " を指定したステージでコンパイルしました。" << std::endl;
    }
    return allPassed ? 0 : 1;
}
