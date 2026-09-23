#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "Logging/LogMacros.h"
#include "FileStream/FileStream.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        bool ReadShaderText(const String &path, String &source)
        {
            auto file = NorvesLib::FileStream::FileStream::CreateUnique(
                path,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read);
            if (!file)
            {
                return false;
            }

            source = file->ReadString();
            if (source.size() >= 3 &&
                static_cast<unsigned char>(source[0]) == 0xEF &&
                static_cast<unsigned char>(source[1]) == 0xBB &&
                static_cast<unsigned char>(source[2]) == 0xBF)
            {
                source = source.substr(3);
            }
            return true;
        }

        bool ParseShaderInclude(const String &line,
                                bool &bIsInclude,
                                String &includePath,
                                String &error)
        {
            bIsInclude = false;
            size_t cursor = 0;
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
            {
                ++cursor;
            }

            static constexpr char IncludeToken[] = "#include";
            if (line.find(IncludeToken, cursor, sizeof(IncludeToken) - 1) != cursor)
            {
                return true;
            }
            const size_t tokenEnd = cursor + sizeof(IncludeToken) - 1;
            if (tokenEnd < line.size() && line[tokenEnd] != ' ' && line[tokenEnd] != '\t')
            {
                return true;
            }

            bIsInclude = true;
            cursor = tokenEnd;
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
            {
                ++cursor;
            }
            if (cursor >= line.size() || line[cursor] != '"')
            {
                error = "シェーダーincludeは引用符付きの相対パスで指定してください";
                return false;
            }

            const size_t pathStart = ++cursor;
            const size_t pathEnd = line.find('"', pathStart);
            if (pathEnd == String::npos || pathEnd == pathStart)
            {
                error = "シェーダーincludeのパスが不正です";
                return false;
            }

            includePath = line.substr(pathStart, pathEnd - pathStart);
            cursor = pathEnd + 1;
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
            {
                ++cursor;
            }
            if (cursor < line.size() && line.find("//", cursor, 2) != cursor)
            {
                error = "シェーダーincludeの後ろに不正な文字があります";
                return false;
            }

            if (includePath[0] == '/' || includePath[0] == '\\' ||
                includePath.find("..") != String::npos ||
                includePath.find(':') != String::npos ||
                includePath.find('\\') != String::npos)
            {
                error = "シェーダーincludeはshaderDirectory内の相対パスに限定されます";
                return false;
            }
            return true;
        }

        bool ExpandShaderIncludes(const String &source,
                                  const String &shaderDirectory,
                                  VariableArray<String> &activeFiles,
                                  String &expandedSource,
                                  String &error)
        {
            size_t lineStart = 0;
            while (lineStart < source.size())
            {
                const size_t lineEnd = source.find('\n', lineStart);
                const size_t contentEnd = lineEnd == String::npos ? source.size() : lineEnd;
                String line = source.substr(lineStart, contentEnd - lineStart);
                if (!line.empty() && line.back() == '\r')
                {
                    line = line.substr(0, line.size() - 1);
                }

                bool bIsInclude = false;
                String includePath;
                if (!ParseShaderInclude(line, bIsInclude, includePath, error))
                {
                    return false;
                }

                if (bIsInclude)
                {
                    if (activeFiles.size() >= 16)
                    {
                        error = "シェーダーincludeの深さが上限を超えました";
                        return false;
                    }

                    const String fullIncludePath = shaderDirectory + includePath;
                    for (const String &activePath : activeFiles)
                    {
                        if (activePath == fullIncludePath)
                        {
                            error = "シェーダーincludeに循環参照があります: " + fullIncludePath;
                            return false;
                        }
                    }

                    String includeSource;
                    if (!ReadShaderText(fullIncludePath, includeSource))
                    {
                        error = "シェーダーincludeを読み込めません: " + fullIncludePath;
                        return false;
                    }

                    activeFiles.push_back(fullIncludePath);
                    const bool bExpanded = ExpandShaderIncludes(
                        includeSource, shaderDirectory, activeFiles, expandedSource, error);
                    activeFiles.pop_back();
                    if (!bExpanded)
                    {
                        return false;
                    }

                    if (lineEnd != String::npos &&
                        (expandedSource.empty() || expandedSource.back() != '\n'))
                    {
                        expandedSource += "\n";
                    }
                }
                else
                {
                    expandedSource += line;
                    if (lineEnd != String::npos)
                    {
                        expandedSource += "\n";
                    }
                }

                if (lineEnd == String::npos)
                {
                    break;
                }
                lineStart = lineEnd + 1;
            }
            return true;
        }

        RHI::ShaderCompileResult CompileShaderFile(
            RHI::IShaderCompiler *compiler,
            const String &fullPath,
            const String &filename,
            const String &shaderDirectory,
            RHI::ShaderStage stage,
            const String &entryPoint,
            bool bExpandIncludes)
        {
            if (!bExpandIncludes)
            {
                return compiler->CompileFromFile(fullPath, stage, entryPoint);
            }

            RHI::ShaderCompileResult result;
            String source;
            if (!ReadShaderText(fullPath, source))
            {
                result.bSuccess = false;
                result.ErrorMessage = "シェーダーファイルを読み込めません: " + fullPath;
                return result;
            }

            VariableArray<String> activeFiles;
            activeFiles.push_back(fullPath);
            String expandedSource;
            if (!ExpandShaderIncludes(source, shaderDirectory, activeFiles, expandedSource,
                                      result.ErrorMessage))
            {
                result.bSuccess = false;
                return result;
            }

            String sourceName = filename;
            const size_t lastSlash = sourceName.FindLast('/');
            const size_t lastBackslash = sourceName.FindLast('\\');
            size_t lastSeparator = String::npos;
            if (lastSlash != String::npos && lastBackslash != String::npos)
            {
                lastSeparator = lastSlash > lastBackslash ? lastSlash : lastBackslash;
            }
            else if (lastSlash != String::npos)
            {
                lastSeparator = lastSlash;
            }
            else if (lastBackslash != String::npos)
            {
                lastSeparator = lastBackslash;
            }
            if (lastSeparator != String::npos)
            {
                sourceName = filename.substr(lastSeparator + 1);
            }

            return compiler->CompileFromSource(expandedSource, stage, sourceName, entryPoint);
        }
    }

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
        auto *compiler = GetCompilerForFile(filename);
        if (!compiler)
        {
            String errorLog = "No suitable compiler for shader: " + filename;
            NORVES_LOG_ERROR("ShaderManager", errorLog.c_str());
            return nullptr;
        }

        RHI::ShaderCompileResult compileResult = CompileShaderFile(
            compiler, fullPath, filename, m_ShaderDirectory, stage, entryPoint,
            !IsSlangFile(filename));

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
            RHI::ShaderCompileResult compileResult = CompileShaderFile(
                m_Compiler.get(), fullPath, cached.Filename, m_ShaderDirectory,
                cached.Stage, cached.EntryPoint, !IsSlangFile(cached.Filename));

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
        auto *compiler = GetCompilerForFile(cached.Filename);
        if (!compiler)
        {
            NORVES_LOG_ERROR("ShaderManager", ("No suitable compiler for: " + cached.Filename).c_str());
            return false;
        }

        RHI::ShaderCompileResult compileResult = CompileShaderFile(
            compiler, fullPath, cached.Filename, m_ShaderDirectory,
            cached.Stage, cached.EntryPoint, !IsSlangFile(cached.Filename));

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

    void ShaderManager::SetSlangCompiler(const RHI::ShaderCompilerPtr &slangCompiler)
    {
        m_SlangCompiler = slangCompiler;
        if (m_SlangCompiler)
        {
            NORVES_LOG_INFO("ShaderManager", "Slang compiler set - .slang shader compilation enabled");
        }
    }

    bool ShaderManager::IsSlangFile(const String &filename) const
    {
        // .slang 拡張子を判定
        const String slangExt = ".slang";
        if (filename.size() >= slangExt.size())
        {
            return filename.substr(filename.size() - slangExt.size()) == slangExt;
        }
        return false;
    }

    RHI::IShaderCompiler *ShaderManager::GetCompilerForFile(const String &filename) const
    {
        if (IsSlangFile(filename))
        {
            return m_SlangCompiler.get();
        }
        return m_Compiler.get();
    }

} // namespace NorvesLib::Core::Rendering
