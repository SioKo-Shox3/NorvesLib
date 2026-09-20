#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include <spirv-headers/spirv.hpp>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <iostream>

namespace
{
    using NorvesLib::RHI::ShaderCompileResult;
    using NorvesLib::RHI::ShaderStage;
    using NorvesLib::RHI::Vulkan::VulkanShaderCompiler;

    struct StageFixture
    {
        ShaderStage Stage;
        std::uint32_t ExpectedExecutionModel;
        const char *Name;
    };

    bool TryReadExecutionModel(const ShaderCompileResult &result, std::uint32_t &executionModel)
    {
        const auto &byteCode = result.ByteCode;
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
    constexpr StageFixture stages[] = {
        {ShaderStage::RayGen, spv::ExecutionModelRayGenerationKHR, "RayGen"},
        {ShaderStage::Miss, spv::ExecutionModelMissKHR, "Miss"},
        {ShaderStage::ClosestHit, spv::ExecutionModelClosestHitKHR, "ClosestHit"},
        {ShaderStage::AnyHit, spv::ExecutionModelAnyHitKHR, "AnyHit"},
        {ShaderStage::Intersection, spv::ExecutionModelIntersectionKHR, "Intersection"},
        {ShaderStage::Callable, spv::ExecutionModelCallableKHR, "Callable"},
    };

    const NorvesLib::Core::Container::String fixturePath =
        NORVES_SOURCE_ROOT "/Assets/Shaders/RayTracing/RayTracingStageFixture.glsl";
    VulkanShaderCompiler compiler;
    bool allStagesPassed = true;

    for (const StageFixture &stage : stages)
    {
        const ShaderCompileResult result = compiler.CompileFromFile(fixturePath, stage.Stage);
        if (!result.bSuccess)
        {
            std::cerr << "[失敗] " << stage.Name << " のコンパイルに失敗しました: "
                      << result.ErrorMessage.c_str() << std::endl;
            allStagesPassed = false;
            continue;
        }

        std::uint32_t actualExecutionModel = 0;
        if (!TryReadExecutionModel(result, actualExecutionModel))
        {
            std::cerr << "[失敗] " << stage.Name << " のSPIR-Vエントリポイントを読み取れません。" << std::endl;
            allStagesPassed = false;
            continue;
        }

        if (actualExecutionModel != stage.ExpectedExecutionModel)
        {
            std::cerr << "[失敗] " << stage.Name << " の実行モデルが一致しません。期待値="
                      << stage.ExpectedExecutionModel << " 実際=" << actualExecutionModel << std::endl;
            allStagesPassed = false;
            continue;
        }

        std::cout << "[成功] " << stage.Name << " を実行モデル " << actualExecutionModel
                  << " としてコンパイルしました。" << std::endl;
    }

    return allStagesPassed ? 0 : 1;
}
