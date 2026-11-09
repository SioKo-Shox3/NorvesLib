// 共通の風と、それを使う草・低木と影の頂点シェーダーを CPU でコンパイルする。
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


    // 試験内の include も、実ファイルと同じルートから展開する。
    bool ExpandProbe(const String& root, const String& source, String& expanded)
    {
        constexpr const char* include = "#include \"Common/Wind.glsl\"";
        const auto start = source.find(include);
        if (start == String::npos || source.find(include, start + 1) != String::npos)
        {
            return false;
        }
        expanded += source.substr(0, start);
        if (!ExpandShader(root, "Common/Wind.glsl", expanded))
        {
            return false;
        }
        expanded += source.substr(start + std::strlen(include));
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

    bool Compile(VulkanShaderCompiler& compiler, const String& source, ShaderStage stage,
                 const char* name, std::uint32_t expectedModel)
    {
        const ShaderCompileResult result = compiler.CompileFromSource(source, stage, name);
        std::uint32_t model = 0;
        if (!result.bSuccess || result.ByteCode.empty() ||
            !TryReadExecutionModel(result, model) || model != expectedModel)
        {
            std::cerr << "[失敗] " << name << " のコンパイルまたは実行モデル: "
                      << result.ErrorMessage.c_str() << std::endl;
            return false;
        }
        std::cout << "[成功] " << name << " をコンパイルしました。" << std::endl;
        return true;
    }
}

int main()
{
    const String root = NORVES_SOURCE_ROOT "/Assets/Shaders/";
    const String probe = R"glsl(#version 450
#include "Common/Wind.glsl"
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Output { vec4 value; } outputData;
void main()
{
    vec3 velocity = WindVelocity(vec3(1.0, 2.0, 3.0), 4.0, vec2(1.0, 0.0),
                                 3.0, 0.4, 0.125, 20.0, 0.15);
    float gust = WindGustNoise(vec2(1.0, 3.0), 4.0, vec2(1.0, 0.0), 20.0, 0.125);
    outputData.value = vec4(velocity, gust);
}
)glsl";
    VulkanShaderCompiler compiler;
    bool bPassed = true;
    String expandedProbe;
    if (!ExpandProbe(root, probe, expandedProbe) ||
        !Compile(compiler, expandedProbe, ShaderStage::Compute, "WindProbe.comp", spv::ExecutionModelGLCompute))
    {
        bPassed = false;
    }

    struct VertexCase
    {
        const char* Path;
        const char* BranchReference;
    };
    const VertexCase cases[] = {
        {"foliage.vert", "foliage.windField.x"},
        {"vsm_draw.vert", "params.foliageWind.w"},
    };
    for (const VertexCase& test : cases)
    {
        String source;
        if (!ExpandShader(root, test.Path, source))
        {
            bPassed = false;
            continue;
        }
        const auto definition = source.find("FoliageWindOffsetShared(");
        if (definition == String::npos ||
            source.find("FoliageWindOffsetShared(", definition + 1) == String::npos ||
            source.find(test.BranchReference) == String::npos)
        {
            std::cerr << "[失敗] " << test.Path << " に共通の風の分岐がありません。" << std::endl;
            bPassed = false;
        }
        if (!Compile(compiler, source, ShaderStage::Vertex, test.Path, spv::ExecutionModelVertex))
        {
            bPassed = false;
        }
    }
    return bPassed ? 0 : 1;
}
