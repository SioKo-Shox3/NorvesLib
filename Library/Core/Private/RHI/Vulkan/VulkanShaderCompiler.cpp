#include "VulkanShaderCompiler.h"
#include "Logging/LogMacros.h"

#include <shaderc/shaderc.hpp>
#include <fstream>
#include <sstream>

namespace NorvesLib::RHI::Vulkan
{

    // ========================================
    // ヘルパー: ShaderStage → shaderc_shader_kind 変換
    // ========================================
    static shaderc_shader_kind ToShadercKind(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::Vertex:
            return shaderc_vertex_shader;
        case ShaderStage::Pixel:
            return shaderc_fragment_shader;
        case ShaderStage::Geometry:
            return shaderc_geometry_shader;
        case ShaderStage::Hull:
            return shaderc_tess_control_shader;
        case ShaderStage::Domain:
            return shaderc_tess_evaluation_shader;
        case ShaderStage::Compute:
            return shaderc_compute_shader;
        default:
            return shaderc_glsl_infer_from_source;
        }
    }

    // ========================================
    // VulkanShaderCompiler
    // ========================================

    VulkanShaderCompiler::VulkanShaderCompiler()
    {
        m_Compiler = new shaderc::Compiler();
    }

    VulkanShaderCompiler::~VulkanShaderCompiler()
    {
        if (m_Compiler)
        {
            delete static_cast<shaderc::Compiler *>(m_Compiler);
            m_Compiler = nullptr;
        }
    }

    ShaderCompileResult VulkanShaderCompiler::CompileFromSource(
        const String &source,
        ShaderStage stage,
        const String &filename,
        const String &entryPoint)
    {
        ShaderCompileResult result;

        if (!m_Compiler)
        {
            result.bSuccess = false;
            result.ErrorMessage = "VulkanShaderCompiler: Compiler not initialized";
            NORVES_LOG_ERROR("ShaderCompiler", "Compiler not initialized");
            return result;
        }

        auto *compiler = static_cast<shaderc::Compiler *>(m_Compiler);

        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        shaderc_shader_kind kind = ToShadercKind(stage);

        shaderc::SpvCompilationResult module = compiler->CompileGlslToSpv(
            source.c_str(), source.size(),
            kind,
            filename.c_str(),
            entryPoint.c_str(),
            options);

        if (module.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            result.bSuccess = false;
            result.ErrorMessage = module.GetErrorMessage().c_str();
            String errorLog = "Failed to compile shader [" + filename + "]: " + result.ErrorMessage;
            NORVES_LOG_ERROR("ShaderCompiler", errorLog.c_str());
            return result;
        }

        // SPIR-Vバイトコードをコピー（uint32_t → uint8_t変換）
        const uint32_t *spirvBegin = module.cbegin();
        const uint32_t *spirvEnd = module.cend();
        size_t spirvSizeBytes = (spirvEnd - spirvBegin) * sizeof(uint32_t);

        result.ByteCode.resize(spirvSizeBytes);
        std::memcpy(result.ByteCode.data(), spirvBegin, spirvSizeBytes);
        result.bSuccess = true;

        if (module.GetNumWarnings() > 0)
        {
            String warnMsg = "Shader [" + filename + "] compiled with warnings: " + String(module.GetErrorMessage().c_str());
            NORVES_LOG_WARNING("ShaderCompiler", warnMsg.c_str());
        }

        NORVES_LOG_DEBUG("ShaderCompiler", ("Compiled shader: " + filename).c_str());

        return result;
    }

    ShaderCompileResult VulkanShaderCompiler::CompileFromFile(
        const String &filePath,
        ShaderStage stage,
        const String &entryPoint)
    {
        ShaderCompileResult result;

        // ファイル読み込み
        std::ifstream file(filePath.c_str(), std::ios::in);
        if (!file.is_open())
        {
            result.bSuccess = false;
            result.ErrorMessage = "Failed to open shader file: " + filePath;
            NORVES_LOG_ERROR("ShaderCompiler", ("Failed to open shader file: " + filePath).c_str());
            return result;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        file.close();

        std::string rawSource = ss.str();

        // UTF-8 BOM (0xEF 0xBB 0xBF) をスキップ
        if (rawSource.size() >= 3 &&
            static_cast<unsigned char>(rawSource[0]) == 0xEF &&
            static_cast<unsigned char>(rawSource[1]) == 0xBB &&
            static_cast<unsigned char>(rawSource[2]) == 0xBF)
        {
            rawSource = rawSource.substr(3);
        }

        String source = rawSource.c_str();

        // ファイル名を抽出（パスの最後の部分）
        String filename = filePath;
        auto lastSlash = filePath.FindLast('/');
        auto lastBackslash = filePath.FindLast('\\');
        size_t pos = String::npos;
        if (lastSlash != String::npos && lastBackslash != String::npos)
        {
            pos = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
        }
        else if (lastSlash != String::npos)
        {
            pos = lastSlash;
        }
        else if (lastBackslash != String::npos)
        {
            pos = lastBackslash;
        }

        if (pos != String::npos)
        {
            filename = filePath.substr(pos + 1);
        }

        return CompileFromSource(source, stage, filename, entryPoint);
    }

} // namespace NorvesLib::RHI::Vulkan
