// 草の生成・引数の初期化・描画を、GPU を使わず実際のシェーダーからコンパイルする。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include "Rendering/GrassPass.h"
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

    // SPIR-V の型・メンバオフセット・束縛を反射し、描画 UBO と 2D sampler の契約を確かめる。
    bool CheckWorldFieldBindings(const ShaderCompileResult& result)
    {
        const auto& bytes = result.ByteCode;
        const auto readWord = [&bytes](std::size_t index)
        {
            uint32_t word = 0;
            std::memcpy(&word, bytes.data() + index * sizeof(word), sizeof(word));
            return word;
        };
        struct ReflectedId
        {
            uint32_t Size = 0;
            uint32_t Type = 0;
            uint32_t Binding = UINT32_MAX;
            uint32_t Set = UINT32_MAX;
            uint32_t Storage = UINT32_MAX;
            bool bImage2D = false;
            bool bSampled2D = false;
            bool bBlock = false;
            VariableArray<uint32_t> MemberOffsets;
        };
        VariableArray<ReflectedId> ids(readWord(3));
        const std::size_t wordCount = bytes.size() / sizeof(uint32_t);
        for (std::size_t cursor = 5; cursor < wordCount;)
        {
            const uint32_t instruction = readWord(cursor);
            const uint32_t count = instruction >> spv::WordCountShift;
            const uint32_t opcode = instruction & spv::OpCodeMask;
            const auto operand = [&](uint32_t index) { return readWord(cursor + index); };
            switch (opcode)
            {
            case spv::OpDecorate:
            {
                ReflectedId& id = ids[operand(1)];
                if (operand(2) == spv::DecorationBinding)
                {
                    id.Binding = operand(3);
                }
                else if (operand(2) == spv::DecorationDescriptorSet)
                {
                    id.Set = operand(3);
                }
                else if (operand(2) == spv::DecorationBlock)
                {
                    id.bBlock = true;
                }
                break;
            }
            case spv::OpMemberDecorate:
                if (operand(3) == spv::DecorationOffset)
                {
                    auto& offsets = ids[operand(1)].MemberOffsets;
                    if (offsets.size() <= operand(2))
                    {
                        offsets.resize(operand(2) + 1, UINT32_MAX);
                    }
                    offsets[operand(2)] = operand(4);
                }
                break;
            case spv::OpTypeFloat:
            case spv::OpTypeInt:
                ids[operand(1)].Size = operand(2) / 8;
                break;
            case spv::OpTypeVector:
            case spv::OpTypeMatrix:
                ids[operand(1)].Size = ids[operand(2)].Size * operand(3);
                break;
            case spv::OpTypeStruct:
            {
                ReflectedId& id = ids[operand(1)];
                // 描画 UBO は mat4 とベクトルとフィールドの構造体だけで、配列は持たない。
                if (id.MemberOffsets.size() == count - 2)
                {
                    for (uint32_t member = 0; member < count - 2; ++member)
                    {
                        id.Size = id.MemberOffsets[member] + ids[operand(member + 2)].Size;
                    }
                    id.Size = (id.Size + 15u) & ~15u;
                }
                break;
            }
            case spv::OpTypeImage:
                ids[operand(1)].bImage2D = operand(3) == spv::Dim2D && operand(4) == 0 &&
                    operand(5) == 0 && operand(6) == 0 && operand(7) == 1;
                break;
            case spv::OpTypeSampledImage:
                ids[operand(1)].bSampled2D = ids[operand(2)].bImage2D;
                break;
            case spv::OpTypePointer:
                ids[operand(1)].Type = operand(3);
                break;
            case spv::OpVariable:
                ids[operand(2)].Type = operand(1);
                ids[operand(2)].Storage = operand(3);
                break;
            default:
                break;
            }
            cursor += count;
        }
        bool bConstants = false;
        bool bSamplers[2] = {};
        for (const ReflectedId& id : ids)
        {
            if (id.Set != 0 || id.Type == 0)
            {
                continue;
            }
            const ReflectedId& type = ids[ids[id.Type].Type];
            if (id.Binding == 0 && id.Storage == spv::StorageClassUniform)
            {
                bConstants = type.bBlock &&
                    type.Size == sizeof(NorvesLib::Core::Rendering::GrassDrawWorldFieldConstants);
            }
            else if (id.Binding >= 4 && id.Binding <= 5 && id.Storage == spv::StorageClassUniformConstant)
            {
                bSamplers[id.Binding - 4] = type.bSampled2D;
            }
        }
        return bConstants && bSamplers[0] && bSamplers[1];
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
        if (shader.Stage == ShaderStage::Vertex && !CheckWorldFieldBindings(result))
        {
            std::cerr << "[失敗] 草の世界フィールドの sampler 束縛 4/5 または描画 UBO のサイズが一致しません。" << std::endl;
            allPassed = false;
            continue;
        }
        std::cout << "[成功] " << path << " を指定したステージでコンパイルしました。" << std::endl;
    }
    return allPassed ? 0 : 1;
}
