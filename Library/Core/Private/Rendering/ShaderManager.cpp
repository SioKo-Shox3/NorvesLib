#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    bool ShaderManager::Initialize(RHI::IDevice *device, const String &shaderDirectory)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!device)
        {
            NORVES_LOG_ERROR("ShaderManager", "Device is null");
            return false;
        }

        m_Device = device;
        m_ShaderDirectory = shaderDirectory;

        // デバイスからシェーダーコンパイラを取得
        m_Compiler = m_Device->CreateShaderCompiler();
        if (!m_Compiler)
        {
            NORVES_LOG_ERROR("ShaderManager", "Failed to create shader compiler from device");
            return false;
        }

        // 末尾にスラッシュがなければ追加
        if (!m_ShaderDirectory.empty())
        {
            char lastChar = m_ShaderDirectory.back();
            if (lastChar != '/' && lastChar != '\\')
            {
                m_ShaderDirectory += '/';
            }
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("ShaderManager", ("Initialized with shader directory: " + m_ShaderDirectory).c_str());
        return true;
    }

    void ShaderManager::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        ClearCache();
        m_Compiler.reset();
        m_Device = nullptr;
        m_bInitialized = false;
        NORVES_LOG_INFO("ShaderManager", "Shutdown");
    }

    TSharedPtr<RHI::IShader> ShaderManager::LoadShader(
        const String &filename,
        RHI::ShaderStage stage,
        const String &entryPoint)
    {
        if (!m_bInitialized || !m_Device)
        {
            NORVES_LOG_ERROR("ShaderManager", "Not initialized");
            return nullptr;
        }

        // キャッシュ確認
        Identity cacheKey(filename.c_str());
        auto it = m_Cache.find(cacheKey);
        if (it != m_Cache.end())
        {
            return it->second.Shader;
        }

        // フルパスを構築してコンパイル
        String fullPath = BuildFullPath(filename);
        RHI::ShaderCompileResult compileResult = m_Compiler->CompileFromFile(fullPath, stage, entryPoint);

        if (!compileResult.bSuccess)
        {
            String errorLog = "Failed to compile shader [" + filename + "]: " + compileResult.ErrorMessage;
            NORVES_LOG_ERROR("ShaderManager", errorLog.c_str());
            return nullptr;
        }

        // RHIシェーダーオブジェクトを作成
        RHI::ShaderDesc shaderDesc;
        shaderDesc.stage = stage;
        shaderDesc.entryPoint = entryPoint;
        shaderDesc.byteCode = std::move(compileResult.ByteCode);

        auto shader = m_Device->CreateShader(shaderDesc);
        if (!shader)
        {
            NORVES_LOG_ERROR("ShaderManager", ("Failed to create RHI shader for: " + filename).c_str());
            return nullptr;
        }

        // キャッシュに登録
        CachedShader cached;
        cached.Shader = shader;
        cached.Stage = stage;
        cached.EntryPoint = entryPoint;
        cached.Filename = filename;
        m_Cache[cacheKey] = std::move(cached);

        NORVES_LOG_INFO("ShaderManager", ("Loaded and compiled shader: " + filename).c_str());
        return shader;
    }

    uint32_t ShaderManager::ReloadAll()
    {
        if (!m_bInitialized || !m_Device)
        {
            return 0;
        }

        uint32_t successCount = 0;

        // キャッシュ内の全シェーダーを再コンパイル
        for (auto &[key, cached] : m_Cache)
        {
            String fullPath = BuildFullPath(cached.Filename);
            RHI::ShaderCompileResult compileResult = m_Compiler->CompileFromFile(
                fullPath, cached.Stage, cached.EntryPoint);

            if (!compileResult.bSuccess)
            {
                String errorLog = "Failed to reload shader [" + cached.Filename + "]: " + compileResult.ErrorMessage;
                NORVES_LOG_ERROR("ShaderManager", errorLog.c_str());
                continue;
            }

            RHI::ShaderDesc shaderDesc;
            shaderDesc.stage = cached.Stage;
            shaderDesc.entryPoint = cached.EntryPoint;
            shaderDesc.byteCode = std::move(compileResult.ByteCode);

            auto newShader = m_Device->CreateShader(shaderDesc);
            if (newShader)
            {
                cached.Shader = newShader;
                successCount++;
                NORVES_LOG_INFO("ShaderManager", ("Reloaded shader: " + cached.Filename).c_str());
            }
            else
            {
                NORVES_LOG_ERROR("ShaderManager", ("Failed to create RHI shader on reload: " + cached.Filename).c_str());
            }
        }

        NORVES_LOG_INFO("ShaderManager", "Shader reload completed");
        return successCount;
    }

    bool ShaderManager::ReloadShader(const String &filename)
    {
        Identity cacheKey(filename.c_str());
        auto it = m_Cache.find(cacheKey);
        if (it == m_Cache.end())
        {
            NORVES_LOG_WARNING("ShaderManager", ("Shader not in cache, loading fresh: " + filename).c_str());
            return false;
        }

        auto &cached = it->second;
        String fullPath = BuildFullPath(cached.Filename);
        RHI::ShaderCompileResult compileResult = m_Compiler->CompileFromFile(
            fullPath, cached.Stage, cached.EntryPoint);

        if (!compileResult.bSuccess)
        {
            String errorLog = "Failed to reload shader [" + cached.Filename + "]: " + compileResult.ErrorMessage;
            NORVES_LOG_ERROR("ShaderManager", errorLog.c_str());
            return false;
        }

        RHI::ShaderDesc shaderDesc;
        shaderDesc.stage = cached.Stage;
        shaderDesc.entryPoint = cached.EntryPoint;
        shaderDesc.byteCode = std::move(compileResult.ByteCode);

        auto newShader = m_Device->CreateShader(shaderDesc);
        if (!newShader)
        {
            NORVES_LOG_ERROR("ShaderManager", ("Failed to create RHI shader on reload: " + cached.Filename).c_str());
            return false;
        }

        cached.Shader = newShader;
        NORVES_LOG_INFO("ShaderManager", ("Reloaded shader: " + cached.Filename).c_str());
        return true;
    }

    void ShaderManager::ClearCache()
    {
        m_Cache.clear();
    }

    String ShaderManager::BuildFullPath(const String &filename) const
    {
        return m_ShaderDirectory + filename;
    }

} // namespace NorvesLib::Core::Rendering
