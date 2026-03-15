#include "VulkanSlangCompiler.h"
#include "Logging/LogMacros.h"

#include <fstream>
#include <sstream>

// Slang SDK が利用可能な場合のみインクルード
#ifdef NORVES_HAS_SLANG
#include <slang.h>
#include <slang-com-ptr.h>
#endif

namespace NorvesLib::RHI::Vulkan
{

    VulkanSlangCompiler::VulkanSlangCompiler()
    {
#ifdef NORVES_HAS_SLANG
        // Slang Global Session を作成
        SlangResult result = slang::createGlobalSession(
            reinterpret_cast<slang::IGlobalSession **>(&m_GlobalSession));

        if (SLANG_SUCCEEDED(result) && m_GlobalSession)
        {
            m_bInitialized = true;
            NORVES_LOG_INFO("SlangCompiler", "Slang compiler initialized successfully");
        }
        else
        {
            m_bInitialized = false;
            NORVES_LOG_WARNING("SlangCompiler", "Failed to initialize Slang compiler session");
        }
#else
        m_bInitialized = false;
        NORVES_LOG_INFO("SlangCompiler", "Slang SDK not available (NORVES_HAS_SLANG not defined). "
                                         "Neural shader compilation disabled.");
#endif
    }

    VulkanSlangCompiler::~VulkanSlangCompiler()
    {
#ifdef NORVES_HAS_SLANG
        if (m_GlobalSession)
        {
            auto *session = static_cast<slang::IGlobalSession *>(m_GlobalSession);
            session->release();
            m_GlobalSession = nullptr;
        }
#endif
    }

    bool VulkanSlangCompiler::IsAvailable() const
    {
        return m_bInitialized;
    }

    ShaderCompileResult VulkanSlangCompiler::CompileFromSource(
        const String &source,
        ShaderStage stage,
        const String &filename,
        const String &entryPoint)
    {
        ShaderCompileResult result;

#ifdef NORVES_HAS_SLANG
        if (!m_bInitialized || !m_GlobalSession)
        {
            result.bSuccess = false;
            result.ErrorMessage = "SlangCompiler: not initialized";
            return result;
        }

        auto *globalSession = static_cast<slang::IGlobalSession *>(m_GlobalSession);

        // セッションの設定
        slang::SessionDesc sessionDesc{};
        slang::TargetDesc targetDesc{};
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = globalSession->findProfile("spirv_1_5");

        // Cooperative Vector 機能を有効化
        const char *capabilityNames[] = {"SPV_NV_cooperative_vector"};
        slang::PreprocessorMacroDesc macros[] = {};

        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        Slang::ComPtr<slang::ISession> session;
        SlangResult res = globalSession->createSession(sessionDesc, session.writeRef());
        if (SLANG_FAILED(res))
        {
            result.bSuccess = false;
            result.ErrorMessage = "Failed to create Slang compilation session";
            return result;
        }

        // ソースBlobを作成
        Slang::ComPtr<slang::IBlob> sourceBlob;
        globalSession->createStringBlob(source.c_str(), source.size(), sourceBlob.writeRef());

        // モジュールを生成
        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule *module = session->loadModuleFromSource(
            filename.c_str(), filename.c_str(), sourceBlob, diagnostics.writeRef());

        if (!module)
        {
            result.bSuccess = false;
            if (diagnostics)
            {
                result.ErrorMessage = static_cast<const char *>(diagnostics->getBufferPointer());
            }
            else
            {
                result.ErrorMessage = "Failed to load Slang module from source";
            }
            NORVES_LOG_ERROR("SlangCompiler", ("Compile error [" + filename + "]: " + result.ErrorMessage).c_str());
            return result;
        }

        // エントリーポイントを検索
        Slang::ComPtr<slang::IEntryPoint> entryPointObj;
        SlangStage slangStage;
        switch (stage)
        {
        case ShaderStage::Vertex:
            slangStage = SLANG_STAGE_VERTEX;
            break;
        case ShaderStage::Pixel:
            slangStage = SLANG_STAGE_FRAGMENT;
            break;
        case ShaderStage::Compute:
            slangStage = SLANG_STAGE_COMPUTE;
            break;
        case ShaderStage::Geometry:
            slangStage = SLANG_STAGE_GEOMETRY;
            break;
        case ShaderStage::Hull:
            slangStage = SLANG_STAGE_HULL;
            break;
        case ShaderStage::Domain:
            slangStage = SLANG_STAGE_DOMAIN;
            break;
        default:
            slangStage = SLANG_STAGE_NONE;
            break;
        }

        module->findEntryPointByName(entryPoint.c_str(), entryPointObj.writeRef());
        if (!entryPointObj)
        {
            result.bSuccess = false;
            result.ErrorMessage = "Entry point '" + entryPoint + "' not found in " + filename;
            return result;
        }

        // コンポジットプログラムを作成してSPIR-Vを生成
        slang::IComponentType *components[] = {module, entryPointObj};
        Slang::ComPtr<slang::IComponentType> composedProgram;
        session->createCompositeComponentType(
            components, 2, composedProgram.writeRef(), diagnostics.writeRef());

        if (!composedProgram)
        {
            result.bSuccess = false;
            result.ErrorMessage = "Failed to compose Slang program";
            return result;
        }

        Slang::ComPtr<slang::IComponentType> linkedProgram;
        composedProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());

        if (!linkedProgram)
        {
            result.bSuccess = false;
            if (diagnostics)
            {
                result.ErrorMessage = static_cast<const char *>(diagnostics->getBufferPointer());
            }
            else
            {
                result.ErrorMessage = "Failed to link Slang program";
            }
            return result;
        }

        // SPIR-Vコードを取得
        Slang::ComPtr<slang::IBlob> spirvCode;
        linkedProgram->getTargetCode(0, spirvCode.writeRef(), diagnostics.writeRef());

        if (!spirvCode || spirvCode->getBufferSize() == 0)
        {
            result.bSuccess = false;
            if (diagnostics)
            {
                result.ErrorMessage = static_cast<const char *>(diagnostics->getBufferPointer());
            }
            else
            {
                result.ErrorMessage = "No SPIR-V code generated";
            }
            return result;
        }

        // バイトコードをコピー
        size_t codeSize = spirvCode->getBufferSize();
        result.ByteCode.resize(codeSize);
        std::memcpy(result.ByteCode.data(), spirvCode->getBufferPointer(), codeSize);
        result.bSuccess = true;

        NORVES_LOG_DEBUG("SlangCompiler", ("Compiled Slang shader: " + filename).c_str());
#else
        result.bSuccess = false;
        result.ErrorMessage = "Slang SDK not available. Rebuild with NORVES_HAS_SLANG to enable.";
        NORVES_LOG_ERROR("SlangCompiler", ("Cannot compile [" + filename + "]: " + result.ErrorMessage).c_str());
#endif

        return result;
    }

    ShaderCompileResult VulkanSlangCompiler::CompileFromFile(
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
            result.ErrorMessage = "Failed to open Slang shader file: " + filePath;
            NORVES_LOG_ERROR("SlangCompiler", result.ErrorMessage.c_str());
            return result;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        file.close();

        std::string rawSource = ss.str();

        // UTF-8 BOM をスキップ
        if (rawSource.size() >= 3 &&
            static_cast<unsigned char>(rawSource[0]) == 0xEF &&
            static_cast<unsigned char>(rawSource[1]) == 0xBB &&
            static_cast<unsigned char>(rawSource[2]) == 0xBF)
        {
            rawSource = rawSource.substr(3);
        }

        String source = rawSource.c_str();

        // ファイル名を抽出
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
