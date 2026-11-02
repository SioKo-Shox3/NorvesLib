// 契約: PT の材質配列の標本・ミップ照会に渡す画像、元の load と access chain の NonUniform を照合する。
// sampler を関数の引数で渡す形に戻すと装飾が落ちる。NVIDIA では撮影・検証レイヤーで気づけない。
// 装飾に頼る GPU では wave 内の別材質を黙って取り違えるため、実際の SPIR-V を CPU で検査する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include "FileStream/FileStream.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <spirv-headers/spirv.hpp>

namespace
{
    using NorvesLib::Core::Container::String;
    using NorvesLib::Core::Container::VariableArray;
    using NorvesLib::RHI::ShaderCompileResult;
    using NorvesLib::RHI::ShaderStage;
    using NorvesLib::RHI::Vulkan::VulkanShaderCompiler;

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

    struct Instruction
    {
        std::size_t Offset;
        std::uint32_t Opcode;
    };

    struct Definition
    {
        std::uint32_t Id;
        std::uint32_t Source;
        std::uint32_t Opcode;
    };

    struct MaterialOrigin
    {
        std::uint32_t Load = 0;
        std::uint32_t Access = 0;
        std::uint32_t Image = 0;
    };

    bool Contains(const VariableArray<std::uint32_t>& ids, std::uint32_t id)
    {
        for (const auto candidate : ids)
        {
            if (candidate == id)
            {
                return true;
            }
        }
        return false;
    }

    bool TraceMaterial(std::uint32_t id, std::uint32_t material,
                       const VariableArray<Definition>& definitions, MaterialOrigin& origin)
    {
        // CopyObject と入れ子の access chain も辿る。循環した定義は照合しない。
        for (std::size_t step = 0; step <= definitions.size(); ++step)
        {
            if (id == material)
            {
                return origin.Load != 0 && origin.Access != 0;
            }
            const Definition* found = nullptr;
            for (const auto& definition : definitions)
            {
                if (definition.Id == id)
                {
                    found = &definition;
                    break;
                }
            }
            if (found == nullptr)
            {
                return false;
            }
            if (found->Opcode == spv::OpLoad)
            {
                origin.Load = id;
            }
            else if (found->Opcode == spv::OpAccessChain || found->Opcode == spv::OpInBoundsAccessChain)
            {
                if (origin.Access == 0)
                {
                    origin.Access = id;
                }
            }
            else if (found->Opcode == spv::OpImage)
            {
                origin.Image = id;
            }
            id = found->Source;
        }
        return false;
    }

    const char* OpcodeName(std::uint32_t opcode)
    {
        switch (opcode)
        {
        case spv::OpImageSampleExplicitLod:
            return "OpImageSampleExplicitLod";
        case spv::OpImageSparseSampleExplicitLod:
            return "OpImageSparseSampleExplicitLod";
        default:
            return "OpImageQueryLevels";
        }
    }

    bool VerifySpirv(const ShaderCompileResult& result, bool bSparse)
    {
        const auto& bytes = result.ByteCode;
        if (bytes.size() < 5 * sizeof(std::uint32_t) || bytes.size() % sizeof(std::uint32_t) != 0)
        {
            std::cerr << "[失敗] SPIR-V のサイズが不正です。" << std::endl;
            return false;
        }
        const auto word = [&bytes](std::size_t index)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, bytes.data() + index * sizeof(value), sizeof(value));
            return value;
        };
        if (word(0) != spv::MagicNumber)
        {
            std::cerr << "[失敗] SPIR-V のマジック値が不正です。" << std::endl;
            return false;
        }

        VariableArray<Instruction> instructions;
        VariableArray<Definition> definitions;
        VariableArray<std::uint32_t> nonUniform;
        VariableArray<std::uint32_t> bindingEight;
        VariableArray<std::uint32_t> setZero;
        VariableArray<std::uint32_t> variables;
        const auto wordCount = bytes.size() / sizeof(std::uint32_t);
        for (std::size_t offset = 5; offset < wordCount;)
        {
            const auto count = word(offset) >> spv::WordCountShift;
            const auto opcode = word(offset) & spv::OpCodeMask;
            const bool bSample = opcode == spv::OpImageSampleExplicitLod ||
                                 opcode == spv::OpImageSparseSampleExplicitLod;
            const bool bDefinition = opcode == spv::OpAccessChain || opcode == spv::OpInBoundsAccessChain ||
                                     opcode == spv::OpLoad || opcode == spv::OpCopyObject || opcode == spv::OpImage;
            if (count == 0 || count > wordCount - offset ||
                ((bDefinition || bSample || opcode == spv::OpImageQueryLevels || opcode == spv::OpVariable) && count < 4) ||
                (opcode == spv::OpDecorate && count < 3))
            {
                std::cerr << "[失敗] SPIR-V の命令長が不正です。語位置=" << offset << std::endl;
                return false;
            }
            instructions.push_back({offset, opcode});
            if (bDefinition)
            {
                definitions.push_back({word(offset + 2), word(offset + 3), opcode});
            }
            if (opcode == spv::OpVariable)
            {
                variables.push_back(word(offset + 2));
            }
            if (opcode == spv::OpDecorate)
            {
                const auto id = word(offset + 1);
                const auto decoration = word(offset + 2);
                if (decoration == spv::DecorationNonUniform)
                {
                    nonUniform.push_back(id);
                }
                if ((decoration == spv::DecorationBinding || decoration == spv::DecorationDescriptorSet) && count < 4)
                {
                    std::cerr << "[失敗] SPIR-V の装飾の引数が不足しています。" << std::endl;
                    return false;
                }
                if (decoration == spv::DecorationBinding && word(offset + 3) == 8)
                {
                    bindingEight.push_back(id);
                }
                if (decoration == spv::DecorationDescriptorSet && word(offset + 3) == 0)
                {
                    setZero.push_back(id);
                }
            }
            offset += count;
        }

        std::uint32_t material = 0;
        for (const auto id : variables)
        {
            if (Contains(bindingEight, id) && Contains(setZero, id))
            {
                if (material != 0)
                {
                    std::cerr << "[失敗] set=0 binding=8 の変数が複数あります。" << std::endl;
                    return false;
                }
                material = id;
            }
        }
        if (material == 0)
        {
            std::cerr << "[失敗] set=0 binding=8 の材質配列が見つかりません。" << std::endl;
            return false;
        }

        bool bPassed = true;
        std::size_t samples = 0;
        std::size_t sparseSamples = 0;
        std::size_t queries = 0;
        for (const auto& instruction : instructions)
        {
            const auto opcode = instruction.Opcode;
            const bool bSample = opcode == spv::OpImageSampleExplicitLod ||
                                 opcode == spv::OpImageSparseSampleExplicitLod;
            if (!bSample && opcode != spv::OpImageQueryLevels)
            {
                continue;
            }
            const auto resultId = word(instruction.Offset + 2);
            const auto operand = word(instruction.Offset + 3);
            MaterialOrigin origin;
            if (!TraceMaterial(operand, material, definitions, origin))
            {
                continue;
            }
            const auto require = [&](std::uint32_t id, const char* role)
            {
                if (!Contains(nonUniform, id))
                {
                    std::cerr << "[失敗] sparse=" << (bSparse ? "有効" : "無効") << " " << OpcodeName(opcode)
                              << " (opcode=" << opcode << ", 結果=%" << resultId << ") の画像 operand=%" << operand
                              << " の " << role << "=%" << id << " に NonUniform がありません。" << std::endl;
                    bPassed = false;
                }
            };
            require(operand, "画像 operand");
            require(origin.Load, "元の OpLoad の結果");
            require(origin.Access, "元の access chain の結果");
            if (bSample)
            {
                if (opcode == spv::OpImageSparseSampleExplicitLod)
                {
                    ++sparseSamples;
                }
                else
                {
                    ++samples;
                }
            }
            else
            {
                if (origin.Image == 0)
                {
                    std::cerr << "[失敗] OpImageQueryLevels の材質画像に OpImage が見つかりません。結果=%"
                              << resultId << std::endl;
                    bPassed = false;
                }
                else
                {
                    require(origin.Image, "OpImage の結果");
                }
                ++queries;
            }
        }
        if ((bSparse ? sparseSamples : samples) == 0)
        {
            std::cerr << "[失敗] 材質配列由来の "
                      << OpcodeName(bSparse ? spv::OpImageSparseSampleExplicitLod : spv::OpImageSampleExplicitLod)
                      << " が見つからず、照合が空振りしました。" << std::endl;
            bPassed = false;
        }
        if (bPassed)
        {
            std::cout << "[成功] sparse=" << (bSparse ? "有効" : "無効") << " の材質配列の NonUniform を確認しました。"
                      << " 通常標本=" << samples << " sparse標本=" << sparseSamples << " ミップ照会=" << queries << std::endl;
        }
        return bPassed;
    }
} // namespace

int main(int argumentCount, char** arguments)
{
    String root = NORVES_SOURCE_ROOT "/Assets/Shaders/";
    constexpr const char* Prefix = "--shader-root=";
    for (int index = 1; index < argumentCount; ++index)
    {
        if (std::strncmp(arguments[index], Prefix, std::strlen(Prefix)) != 0 ||
            arguments[index][std::strlen(Prefix)] == '\0')
        {
            std::cerr << "[失敗] 引数は --shader-root=<ディレクトリ> で指定してください。" << std::endl;
            return 1;
        }
        root = arguments[index] + std::strlen(Prefix);
    }
    if (root.back() != '/' && root.back() != '\\')
    {
        root += "/";
    }
    String source;
    if (!ExpandShader(root, "PathTracing/PathTracingClosestHit.glsl", source))
    {
        return 1;
    }
    VulkanShaderCompiler compiler;
    bool bPassed = true;
    for (const bool bSparse : {true, false})
    {
        compiler.SetSparseResidencyShadingEnabled(bSparse);
        const auto result = compiler.CompileFromSource(source, ShaderStage::ClosestHit, "PathTracingClosestHit.glsl");
        if (!result.bSuccess)
        {
            std::cerr << "[失敗] sparse=" << (bSparse ? "有効" : "無効")
                      << " の closest hit をコンパイルできません: " << result.ErrorMessage.c_str() << std::endl;
            bPassed = false;
        }
        else if (!VerifySpirv(result, bSparse))
        {
            bPassed = false;
        }
    }
    return bPassed ? 0 : 1;
}
