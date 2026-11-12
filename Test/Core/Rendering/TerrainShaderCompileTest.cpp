// 地形のシェーダーを CPU でコンパイルし、C++ と SPIR-V の束縛・型・定数の大きさを照合する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include "Rendering/TerrainBlendShading.h"
#include "FileStream/FileStream.h"
#include <spirv-headers/spirv.hpp>
#include <chrono>
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

    // ShaderManager と同じく、include は取り込み元でなくシェーダールートから解決する。
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

    struct ReflectedId
    {
        uint32_t Opcode = 0;
        uint32_t Type = 0;
        uint32_t Size = 0;
        uint32_t Length = 0;
        uint32_t Value = 0;
        uint32_t ArrayStride = 0;
        uint32_t Binding = UINT32_MAX;
        uint32_t Set = UINT32_MAX;
        uint32_t Storage = UINT32_MAX;
        uint32_t Arrayed = UINT32_MAX;
        bool bImage2D = false;
        bool bBlock = false;
        VariableArray<uint32_t> Members;
        VariableArray<uint32_t> Offsets;
    };

    // 装飾は型宣言より前に現れるので、配列の stride とメンバの offset を保存してから大きさを求める。
    uint32_t TypeSize(const VariableArray<ReflectedId>& ids, uint32_t type, unsigned depth = 0)
    {
        if (type >= ids.size() || depth >= 16)
        {
            return 0;
        }
        const auto& id = ids[type];
        if (id.Opcode == spv::OpTypeFloat || id.Opcode == spv::OpTypeInt)
        {
            return id.Size;
        }
        if (id.Opcode == spv::OpTypeVector)
        {
            return TypeSize(ids, id.Type, depth + 1) * id.Length;
        }
        if (id.Opcode == spv::OpTypeArray && id.ArrayStride != 0 && id.Length < ids.size() &&
            ids[id.Length].Opcode == spv::OpConstant)
        {
            const uint32_t elementSize = TypeSize(ids, id.Type, depth + 1);
            return elementSize != 0 && elementSize <= id.ArrayStride
                ? id.ArrayStride * ids[id.Length].Value : 0;
        }
        if (id.Opcode == spv::OpTypeStruct && !id.Members.empty() && id.Offsets.size() == id.Members.size())
        {
            uint32_t size = 0;
            for (std::size_t member = 0; member < id.Members.size(); ++member)
            {
                const uint32_t memberSize = TypeSize(ids, id.Members[member], depth + 1);
                if (id.Offsets[member] == UINT32_MAX || memberSize == 0)
                {
                    return 0;
                }
                const uint32_t end = id.Offsets[member] + memberSize;
                size = end > size ? end : size;
            }
            return size;
        }
        return 0;
    }

    // shaderc が生成したモジュールだけを対象に、実行モデル・変数・型・必要な装飾を読む。
    bool Reflect(const ShaderCompileResult& result, spv::ExecutionModel model, VariableArray<ReflectedId>& ids)
    {
        const auto& bytes = result.ByteCode;
        if (bytes.size() < 5 * sizeof(uint32_t) || bytes.size() % sizeof(uint32_t) != 0)
        {
            return false;
        }
        const auto readWord = [&bytes](std::size_t index)
        {
            uint32_t word = 0;
            std::memcpy(&word, bytes.data() + index * sizeof(word), sizeof(word));
            return word;
        };
        if (readWord(0) != spv::MagicNumber || readWord(3) == 0)
        {
            return false;
        }
        ids.resize(readWord(3));
        const std::size_t wordCount = bytes.size() / sizeof(uint32_t);
        uint32_t entryCount = 0;
        for (std::size_t cursor = 5; cursor < wordCount;)
        {
            const uint32_t instruction = readWord(cursor);
            const uint32_t count = instruction >> spv::WordCountShift;
            const uint32_t opcode = instruction & spv::OpCodeMask;
            if (count == 0 || count > wordCount - cursor)
            {
                return false;
            }
            const auto operand = [&](uint32_t index) { return readWord(cursor + index); };
            // 必要なオペランド数と、実行モデル以外の先頭オペランドの ID 境界を確かめる。
            uint32_t minimum = 0;
            switch (opcode)
            {
            case spv::OpEntryPoint: minimum = 4; break;
            case spv::OpDecorate: minimum = 3; break;
            case spv::OpMemberDecorate: minimum = 4; break;
            case spv::OpTypeFloat: minimum = 3; break;
            case spv::OpTypeInt: minimum = 4; break;
            case spv::OpTypeVector:
            case spv::OpTypeArray:
            case spv::OpTypePointer:
            case spv::OpConstant:
            case spv::OpVariable: minimum = 4; break;
            case spv::OpTypeStruct:
            case spv::OpTypeSampledImage: minimum = 3; break;
            case spv::OpTypeImage: minimum = 9; break;
            default: break;
            }
            if (minimum != 0 && (count < minimum || (opcode != spv::OpEntryPoint && operand(1) >= ids.size())))
            {
                return false;
            }
            switch (opcode)
            {
            case spv::OpEntryPoint:
                if (++entryCount != 1 || operand(1) != model)
                {
                    return false;
                }
                break;
            case spv::OpDecorate:
            {
                auto& id = ids[operand(1)];
                if (operand(2) == spv::DecorationBlock)
                {
                    id.bBlock = true;
                }
                else if (operand(2) == spv::DecorationBinding || operand(2) == spv::DecorationDescriptorSet ||
                         operand(2) == spv::DecorationArrayStride)
                {
                    if (count < 4)
                    {
                        return false;
                    }
                    if (operand(2) == spv::DecorationBinding)
                    {
                        id.Binding = operand(3);
                    }
                    else if (operand(2) == spv::DecorationDescriptorSet)
                    {
                        id.Set = operand(3);
                    }
                    else
                    {
                        id.ArrayStride = operand(3);
                    }
                }
                break;
            }
            case spv::OpMemberDecorate:
                if (operand(3) == spv::DecorationOffset)
                {
                    if (count < 5)
                    {
                        return false;
                    }
                    auto& offsets = ids[operand(1)].Offsets;
                    if (offsets.size() <= operand(2))
                    {
                        offsets.resize(operand(2) + 1, UINT32_MAX);
                    }
                    offsets[operand(2)] = operand(4);
                }
                break;
            case spv::OpTypeFloat:
            case spv::OpTypeInt:
                ids[operand(1)].Opcode = opcode;
                ids[operand(1)].Size = operand(2) / 8;
                break;
            case spv::OpTypeVector:
            case spv::OpTypeArray:
                ids[operand(1)].Opcode = opcode;
                ids[operand(1)].Type = operand(2);
                ids[operand(1)].Length = operand(3);
                break;
            case spv::OpTypeStruct:
                ids[operand(1)].Opcode = opcode;
                for (uint32_t member = 2; member < count; ++member)
                {
                    ids[operand(1)].Members.push_back(operand(member));
                }
                break;
            case spv::OpTypeImage:
                ids[operand(1)].Opcode = opcode;
                ids[operand(1)].bImage2D = operand(3) == spv::Dim2D && operand(4) == 0 &&
                    operand(6) == 0 && operand(7) == 1;
                ids[operand(1)].Arrayed = operand(5);
                break;
            case spv::OpTypeSampledImage:
                ids[operand(1)].Opcode = opcode;
                ids[operand(1)].Type = operand(2);
                break;
            case spv::OpTypePointer:
                ids[operand(1)].Opcode = opcode;
                ids[operand(1)].Storage = operand(2);
                ids[operand(1)].Type = operand(3);
                break;
            case spv::OpConstant:
                if (operand(2) >= ids.size())
                {
                    return false;
                }
                ids[operand(2)].Opcode = opcode;
                ids[operand(2)].Value = operand(3);
                break;
            case spv::OpVariable:
                if (operand(2) >= ids.size())
                {
                    return false;
                }
                ids[operand(2)].Opcode = opcode;
                ids[operand(2)].Type = operand(1);
                ids[operand(2)].Storage = operand(3);
                break;
            default:
                break;
            }
            cursor += count;
        }
        return entryCount == 1;
    }

    const ReflectedId* FindBindingType(const VariableArray<ReflectedId>& ids, uint32_t binding, uint32_t storage)
    {
        const ReflectedId* found = nullptr;
        for (const auto& id : ids)
        {
            if (id.Opcode != spv::OpVariable || id.Set != 0 || id.Binding != binding)
            {
                continue;
            }
            // 同じ束縛の重複や、変数とポインタの記憶域の食い違いも拒む。
            if (found != nullptr || id.Storage != storage || id.Type >= ids.size())
            {
                return nullptr;
            }
            const auto& pointer = ids[id.Type];
            if (pointer.Opcode != spv::OpTypePointer || pointer.Storage != storage || pointer.Type >= ids.size())
            {
                return nullptr;
            }
            found = &ids[pointer.Type];
        }
        return found;
    }

    bool CheckSampler(const VariableArray<ReflectedId>& ids, uint32_t binding, bool bArrayed)
    {
        const auto* sampled = FindBindingType(ids, binding, spv::StorageClassUniformConstant);
        if (sampled == nullptr || sampled->Opcode != spv::OpTypeSampledImage || sampled->Type >= ids.size())
        {
            std::cerr << "[失敗] set 0・binding " << binding << " に sampled image がありません。" << std::endl;
            return false;
        }
        const auto& image = ids[sampled->Type];
        if (image.Opcode != spv::OpTypeImage || !image.bImage2D || image.Arrayed != (bArrayed ? 1u : 0u))
        {
            std::cerr << "[失敗] set 0・binding " << binding << " の画像型が 2D・配列="
                      << bArrayed << " と一致しません。" << std::endl;
            return false;
        }
        return true;
    }

    enum class BindingContract
    {
        CompileOnly,
        Mega,
        Tiles,
        Rtgi
    };

    bool CheckBindings(const VariableArray<ReflectedId>& ids, BindingContract contract)
    {
        using namespace NorvesLib::Core::Rendering;
        const uint32_t mega[] = {TERRAIN_SPLAT_MEGA_ALBEDO_BINDING, TERRAIN_SPLAT_MEGA_NORMAL_BINDING,
                                TERRAIN_SPLAT_MEGA_ARM_BINDING, TERRAIN_SPLAT_MEGA_SPLAT_BINDING};
        const uint32_t tiles[] = {TERRAIN_SPLAT_VIS_ALBEDO_BINDING, TERRAIN_SPLAT_VIS_NORMAL_BINDING,
                                 TERRAIN_SPLAT_VIS_ARM_BINDING, TERRAIN_SPLAT_VIS_SPLAT_BINDING};
        const uint32_t rtgi[] = {TERRAIN_SPLAT_RTGI_ALBEDO_BINDING, TERRAIN_SPLAT_RTGI_SPLAT_BINDING};
        const uint32_t* bindings = contract == BindingContract::Mega ? mega :
                                   contract == BindingContract::Tiles ? tiles : rtgi;
        const uint32_t count = contract == BindingContract::CompileOnly ? 0u :
                               contract == BindingContract::Rtgi ? 2u : 4u;
        bool bPassed = true;
        for (uint32_t index = 0; index < count; ++index)
        {
            bPassed = CheckSampler(ids, bindings[index], true) && bPassed;
        }
        if (contract == BindingContract::Mega || contract == BindingContract::Tiles)
        {
            const uint32_t first = contract == BindingContract::Mega ? 10u : 23u;
            for (uint32_t binding = first; binding < first + 8u; ++binding)
            {
                bPassed = CheckSampler(ids, binding, false) && bPassed;
            }
        }
        if (contract == BindingContract::Mega)
        {
            const auto* block = FindBindingType(ids, TERRAIN_SPLAT_MEGA_PARAMS_BINDING, spv::StorageClassUniform);
            const uint32_t size = block != nullptr ? TypeSize(ids, static_cast<uint32_t>(block - ids.data())) : 0u;
            if (block == nullptr || block->Opcode != spv::OpTypeStruct || !block->bBlock ||
                size != TERRAIN_SPLAT_PARAM_FLOATS * 4u)
            {
                std::cerr << "[失敗] set 0・binding " << TERRAIN_SPLAT_MEGA_PARAMS_BINDING
                          << " の Uniform block が存在しないか、大きさが一致しません。期待="
                          << TERRAIN_SPLAT_PARAM_FLOATS * 4u << " バイト、実際=" << size << " バイト。" << std::endl;
                bPassed = false;
            }
        }
        return bPassed;
    }
} // 無名名前空間

int main()
{
    const String root = NORVES_SOURCE_ROOT "/Assets/Shaders/";
    const struct ShaderCase
    {
        const char* Path;
        ShaderStage Stage;
        spv::ExecutionModel Model;
        BindingContract Contract;
    } shaders[] = {
        {"megageometry.frag", ShaderStage::Pixel, spv::ExecutionModelFragment, BindingContract::Mega},
        {"visbuffer_resolve.comp", ShaderStage::Compute, spv::ExecutionModelGLCompute, BindingContract::CompileOnly},
        {"visbuffer_resolve_tiles.comp", ShaderStage::Compute, spv::ExecutionModelGLCompute, BindingContract::Tiles},
        {"RTGI/DiffuseIndirect.comp", ShaderStage::Compute, spv::ExecutionModelGLCompute, BindingContract::Rtgi}};
    // 各マクロの #if の両方の向きを通す最小の組として、実行時にあり得る全部無効・全部有効を使う。
    // Debug の shaderc の最適化が遅いため、中間の組の重複したコンパイルを省く。
    const struct MacroCase
    {
        bool bSparse;
        bool bFeedback;
        bool bStats;
    } macros[] = {{false, false, false}, {true, true, true}};
    VulkanShaderCompiler compiler;
    bool bAllPassed = true;
    for (const auto& shader : shaders)
    {
        String source;
        if (!ExpandShader(root, shader.Path, source))
        {
            bAllPassed = false;
            continue;
        }
        // 本文に現れないマクロは無効へ射影し、実行時の組のうち同じ内容になるものを一度だけコンパイルする。
        const bool bUsesSparse = source.find("NORVES_SPARSE_RESIDENCY_SHADING") != String::npos;
        const bool bUsesFeedback = source.find("NORVES_VT_FEEDBACK") != String::npos;
        const bool bUsesStats = source.find("NORVES_VSM_STATS") != String::npos;
        bool bCompiled[8] = {};
        for (const auto& macro : macros)
        {
            const MacroCase effective = {macro.bSparse && bUsesSparse, macro.bFeedback && bUsesFeedback,
                                         macro.bStats && bUsesStats};
            const uint32_t combination = (effective.bSparse ? 1u : 0u) | (effective.bFeedback ? 2u : 0u) |
                                         (effective.bStats ? 4u : 0u);
            if (bCompiled[combination])
            {
                continue;
            }
            bCompiled[combination] = true;
            std::cout << "[検査] " << shader.Path << " NORVES_SPARSE_RESIDENCY_SHADING=" << effective.bSparse
                      << " NORVES_VT_FEEDBACK=" << effective.bFeedback << " NORVES_VSM_STATS=" << effective.bStats;
            if (!bUsesSparse || !bUsesFeedback || !bUsesStats)
            {
                std::cout << " 本文に参照がないため省略:";
                if (!bUsesSparse)
                {
                    std::cout << " NORVES_SPARSE_RESIDENCY_SHADING";
                }
                if (!bUsesFeedback)
                {
                    std::cout << " NORVES_VT_FEEDBACK";
                }
                if (!bUsesStats)
                {
                    std::cout << " NORVES_VSM_STATS";
                }
            }
            std::cout << std::endl;
            compiler.SetSparseResidencyShadingEnabled(effective.bSparse);
            compiler.SetVirtualTextureFeedbackEnabled(effective.bFeedback);
            compiler.SetVsmLightingStatsEnabled(effective.bStats);
            const auto compileStart = std::chrono::steady_clock::now();
            const auto result = compiler.CompileFromSource(source, shader.Stage, shader.Path);
            const auto compileMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - compileStart).count();
            if (!result.bSuccess || result.ByteCode.empty())
            {
                std::cerr << "[失敗] " << shader.Path << " のコンパイルまたは SPIR-V の生成に失敗しました: "
                          << result.ErrorMessage.c_str() << std::endl;
                bAllPassed = false;
                continue;
            }
            VariableArray<ReflectedId> ids;
            if (!Reflect(result, shader.Model, ids))
            {
                std::cerr << "[失敗] " << shader.Path << " の SPIR-V を読めないか、実行モデルが一致しません。" << std::endl;
                bAllPassed = false;
                continue;
            }
            if (!CheckBindings(ids, shader.Contract))
            {
                std::cerr << "[失敗] " << shader.Path << " の地形の束縛が C++ の契約と一致しません。" << std::endl;
                bAllPassed = false;
                continue;
            }
            std::cout << "[成功] " << shader.Path << " の実行モデルと地形の束縛を確認しました。コンパイル="
                      << compileMilliseconds << " ms" << std::endl;
        }
    }
    return bAllPassed ? 0 : 1;
}
