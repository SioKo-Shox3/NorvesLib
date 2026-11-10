// 世界フィールドの更新シェーダーを CPU でコンパイルし、実行モデルと作業グループを確かめる。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include <spirv-headers/spirv.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    using NorvesLib::RHI::ShaderCompileResult;

    bool CheckComputeEntryPoint(const ShaderCompileResult& result)
    {
        const auto& bytes = result.ByteCode;
        if (bytes.size() % sizeof(uint32_t) != 0 || bytes.size() < 5 * sizeof(uint32_t))
        {
            return false;
        }
        const size_t wordCount = bytes.size() / sizeof(uint32_t);
        const auto readWord = [&bytes](size_t index)
        {
            uint32_t word = 0;
            std::memcpy(&word, bytes.data() + index * sizeof(uint32_t), sizeof(word));
            return word;
        };
        if (readWord(0) != spv::MagicNumber)
        {
            return false;
        }
        uint32_t entryPoint = 0;
        uint32_t localSizeEntryPoint = 0;
        uint32_t entryCount = 0;
        uint32_t localSizeCount = 0;
        for (size_t cursor = 5; cursor < wordCount;)
        {
            const uint32_t instruction = readWord(cursor);
            const size_t count = instruction >> spv::WordCountShift;
            const uint32_t opcode = instruction & spv::OpCodeMask;
            if (count == 0 || count > wordCount - cursor)
            {
                return false;
            }
            if (opcode == spv::OpEntryPoint)
            {
                if (count < 4 || ++entryCount != 1 || readWord(cursor + 1) != spv::ExecutionModelGLCompute)
                {
                    return false;
                }
                entryPoint = readWord(cursor + 2);
            }
            if (opcode == spv::OpExecutionMode)
            {
                if (count < 3)
                {
                    return false;
                }
                if (readWord(cursor + 2) == spv::ExecutionModeLocalSize)
                {
                    if (count != 6 || ++localSizeCount != 1 || readWord(cursor + 3) != 8 ||
                        readWord(cursor + 4) != 8 || readWord(cursor + 5) != 1)
                    {
                        return false;
                    }
                    localSizeEntryPoint = readWord(cursor + 1);
                }
            }
            cursor += count;
        }
        return entryCount == 1 && localSizeCount == 1 && entryPoint == localSizeEntryPoint;
    }
} // 無名名前空間

int main()
{
    NorvesLib::RHI::Vulkan::VulkanShaderCompiler compiler;
    const ShaderCompileResult result = compiler.CompileFromFile(
        NORVES_SOURCE_ROOT "/Assets/Shaders/WorldField/WorldFieldUpdate.comp", NorvesLib::RHI::ShaderStage::Compute);
    if (!result.bSuccess || result.ByteCode.empty())
    {
        std::cerr << "WorldFieldShaderCompileTest 失敗: コンパイルまたは SPIR-V の生成に失敗しました: "
                  << result.ErrorMessage.c_str() << '\n';
        return 1;
    }
    if (!CheckComputeEntryPoint(result))
    {
        std::cerr << "WorldFieldShaderCompileTest 失敗: GLCompute・作業グループ 8x8x1 の契約と違います\n";
        return 1;
    }
    std::cout << "WorldFieldShaderCompileTest 合格: GLCompute・作業グループ 8x8x1\n";
    return 0;
}
