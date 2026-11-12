// asset-set の generator の入力をハッシュ化し、変更されたコマンドを直接起動する。
#include "TextureAssetSetGenerators.h"
#include "TextureAssetSetCook.h"
#include "Sha256Digest.h"
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include "CookManagedStoreNative.h"
#endif

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Text = Core::Container::AnsiString;
#if defined(_WIN32)
        using Wide = Core::Container::VariableArray<wchar_t>;
        struct ResolvedGenerator
        {
            std::filesystem::path Cwd;
            Core::Container::VariableArray<std::filesystem::path> Inputs, Outputs;
        };

        void AppendDecimal(Text& text, uint64_t value)
        {
            char digits[32];
            const auto converted = std::to_chars(digits, digits + sizeof(digits), value);
            text.append(digits, static_cast<size_t>(converted.ptr - digits));
        }

        bool Fail(Text& error, const char* code, const Text& name)
        {
            error = code;
            error += " name=";
            error += name;
            return false;
        }

        bool LaunchFailed(Text& error, const Text& name, DWORD code)
        {
            (void)Fail(error, "generator_launch_failed", name);
            error += " win32=";
            AppendDecimal(error, code);
            return false;
        }

        // 正規化した成分を比較する。文字列の前方一致では兄弟名を祖先と誤認する。
        bool IsAncestorOrEqual(const std::filesystem::path& ancestor, const std::filesystem::path& path)
        {
            const auto normalizedAncestor = ancestor.lexically_normal();
            const auto normalizedPath = path.lexically_normal();
            auto child = normalizedPath.begin();
            for (const auto& component : normalizedAncestor)
            {
                // ディレクトリの末尾区切りが生む空成分は階層として数えない。
                if (component.empty())
                {
                    continue;
                }
                if (child == normalizedPath.end() ||
                    CompareStringOrdinal(component.c_str(), -1, child->c_str(), -1, TRUE) != CSTR_EQUAL)
                {
                    return false;
                }
                ++child;
            }
            return true;
        }

        bool ResolveAll(const TextureAssetSetGeneratorRunRequest& request,
                        Core::Container::VariableArray<ResolvedGenerator>& resolved, size_t& active, Text& error)
        {
            auto reportLeaf = request.RuntimeRoot.filename();
            reportLeaf += L".reports";
            const std::filesystem::path protectedRoots[] = {
                request.RuntimeRoot,
                request.RuntimeRoot.parent_path() / Detail::ManagedStoreNative::StoreLeaf,
                request.RuntimeRoot.parent_path() / reportLeaf};
            resolved.resize(request.Generators.size());
            for (active = 0; active < request.Generators.size(); ++active)
            {
                const auto& generator = request.Generators[active];
                auto& paths = resolved[active];
                if (generator.Command.empty() || generator.Command[0].empty() ||
                    generator.Command[0].find('"') != Text::npos)
                {
                    return Fail(error, "generator_command", generator.Name);
                }
                paths.Cwd = request.SourceRoot;
                if (!generator.Cwd.empty() &&
                    !Detail::ResolveTextureCliPath(
                        std::filesystem::u8path(generator.Cwd.begin(), generator.Cwd.end()), request.SourceRoot,
                        paths.Cwd, error, true))
                {
                    return false;
                }
                for (const auto& input : generator.Inputs)
                {
                    std::filesystem::path path;
                    if (!Detail::ResolveTextureCliPath(std::filesystem::u8path(input.begin(), input.end()),
                                                       request.SourceRoot, path, error))
                    {
                        return false;
                    }
                    paths.Inputs.push_back(std::move(path));
                }
                for (const auto& output : generator.Outputs)
                {
                    std::filesystem::path path;
                    if (!Detail::ResolveTextureCliPath(std::filesystem::u8path(output.begin(), output.end()),
                                                       request.SourceRoot, path, error))
                    {
                        return false;
                    }
                    bool bProtected = IsAncestorOrEqual(path, request.SpecPath);
                    for (const auto& root : protectedRoots)
                    {
                        bProtected |= IsAncestorOrEqual(root, path) || IsAncestorOrEqual(path, root);
                    }
                    if (bProtected)
                    {
                        (void)Fail(error, "generator_output_protected", generator.Name);
                        error += " output=";
                        error += output;
                        return false;
                    }
                    paths.Outputs.push_back(std::move(path));
                }
            }
            return true;
        }

        struct FileHash
        {
            BCRYPT_ALG_HANDLE Algorithm = nullptr;
            BCRYPT_HASH_HANDLE Hash = nullptr;
            ~FileHash()
            {
                if (Hash)
                {
                    BCryptDestroyHash(Hash);
                }
                if (Algorithm)
                {
                    BCryptCloseAlgorithmProvider(Algorithm, 0);
                }
            }
        };

        bool HashFile(const std::filesystem::path& path, uint64_t& byteCount, Text& hex)
        {
            std::ifstream input(path, std::ios::binary);
            FileHash hash;
            if (!input || BCryptOpenAlgorithmProvider(&hash.Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
                BCryptCreateHash(hash.Algorithm, &hash.Hash, nullptr, 0, nullptr, 0, 0) < 0)
            {
                return false;
            }
            Core::Container::VariableArray<uint8_t> buffer(1024 * 1024);
            byteCount = 0;
            for (;;)
            {
                input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                const auto count = static_cast<uint64_t>(input.gcount());
                if (count > UINT64_MAX - byteCount ||
                    (count != 0 && BCryptHashData(hash.Hash, buffer.data(), static_cast<ULONG>(count), 0) < 0))
                {
                    return false;
                }
                byteCount += count;
                if (input.bad() || (input.fail() && !input.eof()))
                {
                    return false;
                }
                if (input.eof())
                {
                    break;
                }
            }
            uint8_t digest[32];
            if (BCryptFinishHash(hash.Hash, digest, static_cast<ULONG>(sizeof(digest)), 0) < 0)
            {
                return false;
            }
            constexpr char digits[] = "0123456789abcdef";
            hex.clear();
            hex.reserve(64);
            for (const uint8_t value : digest)
            {
                hex += digits[value >> 4];
                hex += digits[value & 15];
            }
            return true;
        }

        bool HashInputs(const TextureAssetSetGenerator& generator, const ResolvedGenerator& paths,
                        Text& outHash, Text& error)
        {
            Text canonical;
            for (size_t i = 0; i < paths.Inputs.size(); ++i)
            {
                std::error_code code;
                if (!std::filesystem::is_regular_file(paths.Inputs[i], code) || code)
                {
                    (void)Fail(error, "generator_input_missing", generator.Name);
                    error += " input=";
                    error += generator.Inputs[i];
                    return false;
                }
                Text contentHash;
                uint64_t byteCount = 0;
                if (!HashFile(paths.Inputs[i], byteCount, contentHash))
                {
                    (void)Fail(error, "generator_input_read", generator.Name);
                    error += " input=";
                    error += generator.Inputs[i];
                    return false;
                }
                canonical += generator.Inputs[i];
                canonical += '\n';
                AppendDecimal(canonical, byteCount);
                canonical += '\n';
                canonical += contentHash;
                canonical += '\n';
            }
            if (!ComputeSha256Hex(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size(), outHash))
            {
                return Fail(error, "generator_hash", generator.Name);
            }
            return true;
        }

        void AppendSized(Text& canonical, const Text& value)
        {
            AppendDecimal(canonical, value.size());
            canonical += ':';
            canonical += value;
            canonical += '\n';
        }

        bool HashCommand(const TextureAssetSetGenerator& generator, Text& outHash, Text& error)
        {
            Text canonical = "command\n";
            for (const auto& argument : generator.Command)
            {
                AppendSized(canonical, argument);
            }
            canonical += "cwd\n";
            AppendSized(canonical, generator.Cwd);
            canonical += "outputs\n";
            for (const auto& output : generator.Outputs)
            {
                AppendSized(canonical, output);
            }
            if (!ComputeSha256Hex(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size(), outHash))
            {
                return Fail(error, "generator_hash", generator.Name);
            }
            return true;
        }

        size_t MissingOutput(const ResolvedGenerator& paths)
        {
            for (size_t i = 0; i < paths.Outputs.size(); ++i)
            {
                std::error_code code;
                if (!std::filesystem::exists(paths.Outputs[i], code) || code)
                {
                    return i;
                }
            }
            return paths.Outputs.size();
        }

        bool ToWide(const Text& text, Wide& wide)
        {
            wide.clear();
            if (text.empty())
            {
                return true;
            }
            if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
                text.find('\0') != Text::npos)
            {
                return false;
            }
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                 static_cast<int>(text.size()), nullptr, 0);
            if (count == 0)
            {
                return false;
            }
            wide.resize(static_cast<size_t>(count));
            return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                      wide.data(), count) == count;
        }

        void AppendArgument(Wide& commandLine, const Wide& argument)
        {
            bool bQuote = argument.empty();
            for (const wchar_t unit : argument)
            {
                bQuote |= unit == L' ' || unit == L'\t' || unit == L'\n' || unit == L'\v' || unit == L'"';
            }
            if (!bQuote)
            {
                commandLine.insert(commandLine.end(), argument.begin(), argument.end());
                return;
            }
            commandLine.push_back(L'"');
            size_t slashes = 0;
            for (const wchar_t unit : argument)
            {
                if (unit == L'\\')
                {
                    ++slashes;
                    continue;
                }
                // 引用符の直前は 2n+1 本、それ以外は n 本の逆斜線を出す。
                commandLine.insert(commandLine.end(), unit == L'"' ? slashes * 2 + 1 : slashes, L'\\');
                commandLine.push_back(unit);
                slashes = 0;
            }
            commandLine.insert(commandLine.end(), slashes * 2, L'\\');
            commandLine.push_back(L'"');
        }

        bool BuildCommandLine(const TextureAssetSetGenerator& generator, const ResolvedGenerator& paths,
                              Wide& commandLine, std::filesystem::path& application, Text& error)
        {
            for (size_t i = 0; i < generator.Command.size(); ++i)
            {
                Wide argument;
                if (!ToWide(generator.Command[i], argument))
                {
                    return Fail(error, "generator_command", generator.Name);
                }
                if (i == 0)
                {
                    const std::filesystem::path program(argument.begin(), argument.end());
                    if (program.is_absolute())
                    {
                        application = program;
                    }
                    else if (generator.Command[0].find('/') != Text::npos ||
                             generator.Command[0].find('\\') != Text::npos)
                    {
                        application = std::filesystem::absolute(paths.Cwd / program).lexically_normal();
                    }
                    // argv[0] は通常の引数と異なり、常に引用符で囲むだけにする。
                    commandLine.push_back(L'"');
                    commandLine.insert(commandLine.end(), argument.begin(), argument.end());
                    commandLine.push_back(L'"');
                }
                else
                {
                    commandLine.push_back(L' ');
                    AppendArgument(commandLine, argument);
                }
                if (commandLine.size() > 32766)
                {
                    return Fail(error, "generator_command_too_long", generator.Name);
                }
            }
            commandLine.push_back(L'\0');
            return true;
        }

        struct StandardHandles
        {
            HANDLE Originals[3] = {}, Duplicates[3] = {};
            size_t Count = 0;
            Core::Container::VariableArray<std::max_align_t> AttributeStorage;
            LPPROC_THREAD_ATTRIBUTE_LIST Attributes = nullptr;

            ~StandardHandles()
            {
                if (Attributes)
                {
                    DeleteProcThreadAttributeList(Attributes);
                }
                for (size_t i = 0; i < Count; ++i)
                {
                    CloseHandle(Duplicates[i]);
                }
            }

            bool Initialize(STARTUPINFOEXW& startup, DWORD& code)
            {
                const DWORD kinds[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
                HANDLE* destinations[] = {&startup.StartupInfo.hStdInput, &startup.StartupInfo.hStdOutput,
                                           &startup.StartupInfo.hStdError};
                for (size_t i = 0; i < 3; ++i)
                {
                    const HANDLE original = GetStdHandle(kinds[i]);
                    DWORD flags = 0;
                    if (!original || original == INVALID_HANDLE_VALUE || !GetHandleInformation(original, &flags))
                    {
                        continue;
                    }
                    size_t index = 0;
                    while (index < Count && Originals[index] != original)
                    {
                        ++index;
                    }
                    if (index == Count)
                    {
                        if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &Duplicates[index],
                                             0, TRUE, DUPLICATE_SAME_ACCESS))
                        {
                            code = GetLastError();
                            return false;
                        }
                        Originals[index] = original;
                        ++Count;
                    }
                    *destinations[i] = Duplicates[index];
                }
                if (Count == 0)
                {
                    return true;
                }
                SIZE_T bytes = 0;
                (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
                if (bytes == 0)
                {
                    code = GetLastError();
                    return false;
                }
                AttributeStorage.resize((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
                const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(AttributeStorage.data());
                if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes))
                {
                    code = GetLastError();
                    return false;
                }
                Attributes = attributes;
                if (!UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, Duplicates,
                                               Count * sizeof(HANDLE), nullptr, nullptr))
                {
                    code = GetLastError();
                    return false;
                }
                startup.lpAttributeList = Attributes;
                startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
                startup.StartupInfo.cb = sizeof(startup);
                return true;
            }
        };

        struct ProcessHandles
        {
            PROCESS_INFORMATION Info = {};
            ~ProcessHandles()
            {
                if (Info.hThread)
                {
                    CloseHandle(Info.hThread);
                }
                if (Info.hProcess)
                {
                    CloseHandle(Info.hProcess);
                }
            }
        };

        bool Launch(const TextureAssetSetGenerator& generator, const ResolvedGenerator& paths,
                    DWORD& exitCode, Text& error)
        {
            Wide commandLine;
            std::filesystem::path application;
            if (!BuildCommandLine(generator, paths, commandLine, application, error))
            {
                return false;
            }
            ProcessHandles process;
            {
                // 継承用の複製と属性リストは起動の直後に解放する。
                StandardHandles handles;
                STARTUPINFOEXW startup = {};
                startup.StartupInfo.cb = sizeof(STARTUPINFOW);
                DWORD code = 0;
                if (!handles.Initialize(startup, code))
                {
                    return LaunchFailed(error, generator.Name, code);
                }
                const bool bInherit = handles.Count != 0;
                if (!CreateProcessW(application.empty() ? nullptr : application.c_str(), commandLine.data(),
                                    nullptr, nullptr, bInherit ? TRUE : FALSE,
                                    bInherit ? EXTENDED_STARTUPINFO_PRESENT : 0, nullptr, paths.Cwd.c_str(),
                                    &startup.StartupInfo, &process.Info))
                {
                    return LaunchFailed(error, generator.Name, GetLastError());
                }
            }
            if (WaitForSingleObject(process.Info.hProcess, INFINITE) != WAIT_OBJECT_0 ||
                !GetExitCodeProcess(process.Info.hProcess, &exitCode))
            {
                return LaunchFailed(error, generator.Name, GetLastError());
            }
            return true;
        }

        bool RunWindows(const TextureAssetSetGeneratorRunRequest& request, TextureAssetSetGeneratorRunResult& out,
                        size_t& active, Text& error)
        {
            Core::Container::VariableArray<ResolvedGenerator> resolved;
            if (!ResolveAll(request, resolved, active, error))
            {
                return false;
            }
            for (active = 0; active < request.Generators.size(); ++active)
            {
                const auto& generator = request.Generators[active];
                const auto& paths = resolved[active];
                CookGeneratorRecord record;
                record.Name = generator.Name;
                // 前段の出力を後段が読めるように、この段を判断する直前に求める。
                if (!HashInputs(generator, paths, record.InputsHash, error) ||
                    !HashCommand(generator, record.CommandHash, error))
                {
                    return false;
                }
                const CookGeneratorRecord* previous = nullptr;
                for (const auto& candidate : request.Previous)
                {
                    if (candidate.Name == generator.Name)
                    {
                        previous = &candidate;
                        break;
                    }
                }
                const bool bRun = !previous || previous->InputsHash != record.InputsHash ||
                                  previous->CommandHash != record.CommandHash ||
                                  MissingOutput(paths) != paths.Outputs.size();
                if (bRun)
                {
                    std::fprintf(stderr, "AssetCook generator running name=%s\n", generator.Name.c_str());
                    std::fflush(stdout);
                    std::fflush(stderr);
                    DWORD exitCode = 0;
                    if (!Launch(generator, paths, exitCode, error))
                    {
                        return false;
                    }
                    if (exitCode != 0)
                    {
                        (void)Fail(error, "generator_failed", generator.Name);
                        error += " exit_code=";
                        AppendDecimal(error, exitCode);
                        return false;
                    }
                    const size_t missing = MissingOutput(paths);
                    if (missing != paths.Outputs.size())
                    {
                        (void)Fail(error, "generator_failed", generator.Name);
                        error += " exit_code=0 missing_output=";
                        error += generator.Outputs[missing];
                        return false;
                    }
                }
                out.Records.push_back(std::move(record));
                out.Rows[active].Status = bRun ? "ran" : "skipped";
            }
            return true;
        }
#endif
    } // 名前空間

    bool RunTextureAssetSetGenerators(const TextureAssetSetGeneratorRunRequest& request,
                                      TextureAssetSetGeneratorRunResult& out, Text& error)
    {
        out.Records.clear();
        out.Rows.clear();
        error.clear();
        for (const auto& generator : request.Generators)
        {
            out.Rows.push_back({generator.Name, "not_run"});
        }
#if !defined(_WIN32)
        error = "windows_required";
        return false;
#else
        size_t active = 0;
        try
        {
            if (RunWindows(request, out, active, error))
            {
                return true;
            }
        }
        catch (const std::exception&)
        {
            if (active < request.Generators.size())
            {
                (void)Fail(error, "generator_failed", request.Generators[active].Name);
                error += " reason=exception";
            }
            else
            {
                error = "generator_failed reason=exception";
            }
        }
        if (active < out.Rows.size())
        {
            out.Rows[active].Status = "failed";
        }
        return false;
#endif
    }
} // 名前空間 NorvesLib::Tools::AssetCook
