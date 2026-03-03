#pragma once

#include "RHI/IShaderCompiler.h"
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言
    using ::NorvesLib::Core::Container::String;
    using ::NorvesLib::Core::Container::VariableArray;

    /**
     * @brief Vulkan向けシェーダーコンパイラ実装
     *
     * shaderc (Vulkan SDK同梱)を使用してGLSLソースコードを
     * SPIR-Vバイトコードにランタイムでコンパイルします。
     */
    class VulkanShaderCompiler : public IShaderCompiler
    {
    public:
        VulkanShaderCompiler();
        ~VulkanShaderCompiler() override;

        // コピー禁止
        VulkanShaderCompiler(const VulkanShaderCompiler &) = delete;
        VulkanShaderCompiler &operator=(const VulkanShaderCompiler &) = delete;

        // IShaderCompiler実装
        ShaderCompileResult CompileFromSource(
            const String &source,
            ShaderStage stage,
            const String &filename = "shader",
            const String &entryPoint = "main") override;

        ShaderCompileResult CompileFromFile(
            const String &filePath,
            ShaderStage stage,
            const String &entryPoint = "main") override;

    private:
        /** @brief shaderc内部コンパイラハンドル（<shaderc/shaderc.hpp>への依存をヘッダーに出さない） */
        void *m_Compiler = nullptr;
    };

} // namespace NorvesLib::RHI::Vulkan
