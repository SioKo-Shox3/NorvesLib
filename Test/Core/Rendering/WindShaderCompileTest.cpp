// 共通の風と、それを使う草・低木と影の頂点シェーダーを CPU でコンパイルする。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include "Rendering/FoliagePass.h"
#include "Rendering/VirtualShadowMapRaster.h"
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

    struct ShaderFieldContract
    {
        std::size_t UniformSize;
        std::size_t FieldOffset;
        std::uint32_t CurrentBinding;
        std::uint32_t PreviousBinding;
        bool bHasPrevious;
    };

    struct SpirvId
    {
        std::uint32_t Kind = 0;
        std::uint32_t Element = 0;
        std::uint32_t Count = 0;
        std::uint32_t Storage = 0;
        std::uint32_t Binding = ~0u;
        std::uint32_t Set = ~0u;
        bool bBlock = false;
        VariableArray<std::uint32_t> Members;
        VariableArray<std::uint32_t> Offsets;
    };

    // この試験の UBO が使うスカラー・ベクトル・mat4・構造体だけの std140 サイズを求める。
    std::size_t SpirvTypeSize(const VariableArray<SpirvId>& ids, std::uint32_t id, unsigned depth = 0)
    {
        if (id >= ids.size() || depth > 8)
        {
            return 0;
        }
        const SpirvId& type = ids[id];
        if (type.Kind == spv::OpTypeFloat || type.Kind == spv::OpTypeInt)
        {
            return type.Count / 8u;
        }
        if (type.Kind == spv::OpTypeVector || type.Kind == spv::OpTypeMatrix)
        {
            return SpirvTypeSize(ids, type.Element, depth + 1) * type.Count;
        }
        if (type.Kind != spv::OpTypeStruct || type.Offsets.size() < type.Members.size())
        {
            return 0;
        }
        std::size_t end = 0;
        for (std::size_t i = 0; i < type.Members.size(); ++i)
        {
            const std::size_t size = SpirvTypeSize(ids, type.Members[i], depth + 1);
            if (size == 0 || type.Offsets[i] == ~0u)
            {
                return 0;
            }
            const std::size_t memberEnd = type.Offsets[i] + size;
            if (memberEnd > end)
            {
                end = memberEnd;
            }
        }
        return (end + 15u) & ~std::size_t(15u);
    }

    // SPIR-V の装飾と型から、セット 0 の UBO と CombinedImageSampler の実際の配置を調べる。
    bool CheckWorldFieldContract(const ShaderCompileResult& result, const ShaderFieldContract& contract)
    {
        const auto& bytes = result.ByteCode;
        if (bytes.size() < 5u * sizeof(std::uint32_t) || bytes.size() % sizeof(std::uint32_t) != 0)
        {
            return false;
        }
        const auto word = [&bytes](std::size_t index)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, bytes.data() + index * sizeof(value), sizeof(value));
            return value;
        };
        const std::size_t wordCount = bytes.size() / sizeof(std::uint32_t);
        const std::uint32_t bound = word(3);
        if (word(0) != spv::MagicNumber || bound == 0 || bound > 1000000u)
        {
            return false;
        }
        VariableArray<SpirvId> ids(bound);
        for (std::size_t cursor = 5; cursor < wordCount;)
        {
            const std::uint32_t instruction = word(cursor);
            const std::uint32_t op = instruction & spv::OpCodeMask;
            const std::size_t count = instruction >> spv::WordCountShift;
            if (count == 0 || count > wordCount - cursor)
            {
                return false;
            }
            if (op == spv::OpDecorate && count >= 3 && word(cursor + 1) < bound)
            {
                SpirvId& item = ids[word(cursor + 1)];
                const std::uint32_t decoration = word(cursor + 2);
                if (decoration == spv::DecorationBlock) item.bBlock = true;
                if (count >= 4 && decoration == spv::DecorationBinding) item.Binding = word(cursor + 3);
                if (count >= 4 && decoration == spv::DecorationDescriptorSet) item.Set = word(cursor + 3);
            }
            else if (op == spv::OpMemberDecorate && count >= 5 && word(cursor + 1) < bound &&
                     word(cursor + 3) == spv::DecorationOffset)
            {
                SpirvId& item = ids[word(cursor + 1)];
                const std::uint32_t member = word(cursor + 2);
                if (member >= 4096u) return false;
                if (item.Offsets.size() <= member) item.Offsets.resize(member + 1u, ~0u);
                item.Offsets[member] = word(cursor + 4);
            }
            else if ((op == spv::OpTypeInt || op == spv::OpTypeFloat) && count >= 3 && word(cursor + 1) < bound)
            {
                SpirvId& item = ids[word(cursor + 1)];
                item.Kind = op;
                item.Count = word(cursor + 2);
            }
            else if ((op == spv::OpTypeVector || op == spv::OpTypeMatrix) && count >= 4 && word(cursor + 1) < bound)
            {
                SpirvId& item = ids[word(cursor + 1)];
                item.Kind = op;
                item.Element = word(cursor + 2);
                item.Count = word(cursor + 3);
            }
            else if (op == spv::OpTypeStruct && count >= 2 && word(cursor + 1) < bound)
            {
                SpirvId& item = ids[word(cursor + 1)];
                item.Kind = op;
                for (std::size_t i = 2; i < count; ++i) item.Members.push_back(word(cursor + i));
            }
            else if (op == spv::OpTypePointer && count >= 4 && word(cursor + 1) < bound)
            {
                SpirvId& item = ids[word(cursor + 1)];
                item.Kind = op;
                item.Element = word(cursor + 3);
            }
            else if (op == spv::OpTypeSampledImage && count >= 3 && word(cursor + 1) < bound)
            {
                ids[word(cursor + 1)].Kind = op;
            }
            else if (op == spv::OpVariable && count >= 4 && word(cursor + 2) < bound)
            {
                SpirvId& item = ids[word(cursor + 2)];
                item.Kind = op;
                item.Element = word(cursor + 1);
                item.Storage = word(cursor + 3);
            }
            cursor += count;
        }
        const auto bindingType = [&ids](std::uint32_t binding, std::uint32_t storage) -> std::uint32_t
        {
            std::uint32_t type = 0;
            for (const SpirvId& item : ids)
            {
                if (item.Kind != spv::OpVariable || item.Set != 0u || item.Binding != binding || item.Storage != storage)
                    continue;
                if (type != 0u || item.Element >= ids.size() || ids[item.Element].Kind != spv::OpTypePointer)
                    return 0;
                type = ids[item.Element].Element;
            }
            return type < ids.size() ? type : 0;
        };
        const std::uint32_t uniform = bindingType(0, spv::StorageClassUniform);
        const std::uint32_t current = bindingType(contract.CurrentBinding, spv::StorageClassUniformConstant);
        const std::uint32_t previous = contract.bHasPrevious ?
            bindingType(contract.PreviousBinding, spv::StorageClassUniformConstant) : current;
        if (uniform == 0 || current == 0 || previous == 0 || !ids[uniform].bBlock ||
            ids[current].Kind != spv::OpTypeSampledImage || ids[previous].Kind != spv::OpTypeSampledImage ||
            ids[uniform].Offsets.empty() || ids[uniform].Offsets.back() != contract.FieldOffset ||
            SpirvTypeSize(ids, uniform) != contract.UniformSize)
        {
            return false;
        }
        return true;
    }

    bool Compile(VulkanShaderCompiler& compiler, const String& source, ShaderStage stage,
                 const char* name, std::uint32_t expectedModel, const ShaderFieldContract* fieldContract = nullptr)
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
        if (fieldContract && !CheckWorldFieldContract(result, *fieldContract))
        {
            std::cerr << "[失敗] " << name << " のフィールドの束縛または UBO の並びが一致しません。" << std::endl;
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
        ShaderFieldContract Field;
    };
    using NorvesLib::Core::Rendering::FoliageUniform;
    using NorvesLib::Core::Rendering::VirtualShadowMapRasterParams;
    const VertexCase cases[] = {
        {"foliage.vert", "foliage.windField.x", {sizeof(FoliageUniform), offsetof(FoliageUniform, worldField), 1u, 2u, true}},
        {"vsm_draw.vert", "params.foliageWind.w", {sizeof(VirtualShadowMapRasterParams), offsetof(VirtualShadowMapRasterParams, worldField), 6u, 0u, false}},
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
        if (!Compile(compiler, source, ShaderStage::Vertex, test.Path, spv::ExecutionModelVertex, &test.Field))
        {
            bPassed = false;
        }
    }
    return bPassed ? 0 : 1;
}
