// 所有stateの排他新規保存。既存rootの更新/journalはここでは実行しない。
#include "CookStateFile.h"
#include "CookStateFileTestAccess.h"
#include "CookOutputPaths.h"
#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <climits>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using StateIOText = Core::Container::AnsiString;
        using StateIOBytes = Core::Container::VariableArray<uint8_t>;
        using StateIOView = Core::Container::AnsiStringView;
        using Fault = Detail::CookStateFileFault;
        using Probe = Detail::CookStateFileProbe;
        namespace Paths = Detail::CookOutputPaths;
        bool Fail(StateIOText& error, const char* code)
        {
            error = "cook_state_file: ";
            error.append(code);
            return false;
        }
        bool BindingEqual(const CookStateBinding& a, const CookStateBinding& b)
        {
            return Paths::EqualName(a.OwnerId, b.OwnerId) &&
                   Paths::EqualName(a.RuntimeRootIdentity, b.RuntimeRootIdentity) &&
                   Paths::EqualName(a.ManifestName, b.ManifestName);
        }
#if defined(_WIN32)
        class Handle
        {
          public:
            HANDLE Value = INVALID_HANDLE_VALUE;
            Handle() = default;
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            ~Handle()
            {
                if (Value != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(Value);
                }
            }
            bool Close()
            {
                if (Value == INVALID_HANDLE_VALUE)
                {
                    return true;
                }
                const auto h = Value;
                Value = INVALID_HANDLE_VALUE;
                return CloseHandle(h) != FALSE;
            }
        };
        bool RawPath(const std::filesystem::path& path)
        {
            StateIOText full, relative;
            return path.is_absolute() && Paths::LocalDrivePath(path) && Paths::AsciiPath(path, full) &&
                   Paths::AsciiPath(path.relative_path(), relative) && Paths::SafeOutputName(relative);
        }
        bool DirectoryChain(const std::filesystem::path& path)
        {
            auto prefix = path.root_path();
            const auto valid = [](const std::filesystem::path& value)
            {
                const DWORD a = GetFileAttributesW(value.c_str());
                return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                       (a & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
            };
            if (!valid(prefix))
            {
                return false;
            }
            for (const auto& part : path.relative_path())
            {
                prefix /= part;
                if (!valid(prefix))
                {
                    return false;
                }
            }
            return true;
        }
        bool HandleType(HANDLE h, bool bDirectory)
        {
            FILE_ATTRIBUTE_TAG_INFO info{};
            return GetFileType(h) == FILE_TYPE_DISK &&
                   GetFileInformationByHandleEx(h, FileAttributeTagInfo, &info, sizeof(info)) &&
                   (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                   ((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == bDirectory;
        }
        bool OpenDirectory(const std::filesystem::path& path, Handle& out)
        {
            out.Value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            return out.Value != INVALID_HANDLE_VALUE && HandleType(out.Value, true);
        }
        bool FinalPath(HANDLE h, std::filesystem::path& out)
        {
            const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_GUID;
            const DWORD required = GetFinalPathNameByHandleW(h, nullptr, 0, flags);
            if (required == 0 || required > 32767)
            {
                return false;
            }
            Core::Container::VariableArray<wchar_t> buffer(static_cast<size_t>(required) + 1, 0);
            const DWORD length = GetFinalPathNameByHandleW(h, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
            if (length == 0 || length >= buffer.size())
            {
                return false;
            }
            out = std::filesystem::path(buffer.data(), buffer.data() + length);
            return true;
        }
        bool EqualWide(const wchar_t* a, size_t an, const wchar_t* b, size_t bn)
        {
            if (an > INT_MAX || bn > INT_MAX)
            {
                return false;
            }
            return CompareStringOrdinal(a, static_cast<int>(an), b, static_cast<int>(bn), TRUE) == CSTR_EQUAL;
        }
        bool PhysicalEqual(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            return EqualWide(a.native().data(), a.native().size(), b.native().data(), b.native().size());
        }
        bool PhysicalPrefix(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            const auto& x = a.native();
            const auto& y = b.native();
            return x.size() <= y.size() && EqualWide(x.data(), x.size(), y.data(), x.size()) &&
                   (x.size() == y.size() || y[x.size()] == '\\' || y[x.size()] == '/');
        }
        size_t VolumeLength(const std::filesystem::path& path)
        {
            const auto& s = path.native();
            constexpr wchar_t prefix[] = L"\\\\?\\Volume{";
            constexpr size_t count = sizeof(prefix) / sizeof(wchar_t) - 1;
            if (s.size() <= count || !EqualWide(s.data(), count, prefix, count))
            {
                return 0;
            }
            for (size_t i = count; i + 1 < s.size(); ++i)
            {
                if (s[i] == L'}' && s[i + 1] == L'\\')
                {
                    return i + 2;
                }
            }
            return 0;
        }
        bool SameVolume(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            const auto n = VolumeLength(a), m = VolumeLength(b);
            return n && m && EqualWide(a.native().data(), n, b.native().data(), m);
        }
        struct Scope
        {
            Handle StateParent, RuntimeParent, Runtime;
            std::filesystem::path PhysicalStateParent, PhysicalRuntime, PhysicalTarget;
        };
        bool Prepare(const CookStateFileRequest& request, Scope& scope, StateIOText& error)
        {
            if (!IsValidCookStateBinding(request.ExpectedBinding) || !RawPath(request.StatePath) ||
                !RawPath(request.RuntimeRoot))
            {
                return Fail(error, "invalid_binding_or_raw_locator");
            }
            StateIOText rootText;
            if (!Paths::AsciiPath(request.RuntimeRoot.lexically_normal(), rootText) ||
                !Paths::EqualName(rootText, request.ExpectedBinding.RuntimeRootIdentity))
            {
                return Fail(error, "runtime_binding_mismatch");
            }
            const auto stateParent = request.StatePath.parent_path(), runtimeParent = request.RuntimeRoot.parent_path();
            if (!DirectoryChain(stateParent) || !DirectoryChain(runtimeParent) ||
                !OpenDirectory(stateParent, scope.StateParent) || !OpenDirectory(runtimeParent, scope.RuntimeParent) ||
                !FinalPath(scope.StateParent.Value, scope.PhysicalStateParent))
            {
                return Fail(error, "parent_missing_or_unsafe");
            }
            const DWORD attributes = GetFileAttributesW(request.RuntimeRoot.c_str());
            // 未作成directoryの将来の8.3 aliasは解決できないため、runtimeも既存を必須とする。
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || !OpenDirectory(request.RuntimeRoot, scope.Runtime) ||
                !FinalPath(scope.Runtime.Value, scope.PhysicalRuntime))
            {
                return Fail(error, "runtime_missing_not_directory_or_reparse");
            }
            scope.PhysicalTarget = scope.PhysicalStateParent / request.StatePath.filename();
            if (!SameVolume(scope.PhysicalStateParent, scope.PhysicalRuntime) ||
                PhysicalPrefix(scope.PhysicalRuntime, scope.PhysicalTarget) ||
                PhysicalPrefix(scope.PhysicalTarget, scope.PhysicalRuntime))
            {
                return Fail(error, "state_scope_conflict");
            }
            return true;
        }
        bool ReadAll(HANDLE h, StateIOBytes& out, StateIOText& error)
        {
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
                static_cast<uint64_t>(size.QuadPart) > MaximumCookStateBytes)
            {
                return Fail(error, "invalid_file_size");
            }
            StateIOBytes bytes(static_cast<size_t>(size.QuadPart));
            size_t offset = 0;
            while (offset < bytes.size())
            {
                DWORD count = 0;
                if (!ReadFile(h, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &count, nullptr) ||
                    count == 0)
                {
                    return Fail(error, "read_failed_or_short");
                }
                offset += count;
            }
            uint8_t extra = 0;
            DWORD count = 0;
            const BOOL read = ReadFile(h, &extra, 1, &count, nullptr);
            if ((!read && GetLastError() != ERROR_HANDLE_EOF) || count != 0)
            {
                return Fail(error, "file_grew_during_read");
            }
            LARGE_INTEGER after{};
            if (!GetFileSizeEx(h, &after) || after.QuadPart != size.QuadPart)
            {
                return Fail(error, "file_size_changed");
            }
            out = std::move(bytes);
            return true;
        }
        class OwnedTemp
        {
          public:
            Handle File;
            std::filesystem::path Path;
            bool bPublished = false;
            bool bFailCleanup = false;
            ~OwnedTemp()
            {
                if (File.Value != INVALID_HANDLE_VALUE && !bPublished)
                {
                    FILE_DISPOSITION_INFO info{TRUE};
                    SetFileInformationByHandle(File.Value, FileDispositionInfo, &info, sizeof(info));
                }
            }
            bool Abort(StateIOText& error)
            {
                bool bDeleted = true;
                if (File.Value != INVALID_HANDLE_VALUE && !bPublished)
                {
                    FILE_DISPOSITION_INFO info{TRUE};
                    bDeleted = !bFailCleanup &&
                               SetFileInformationByHandle(File.Value, FileDispositionInfo, &info, sizeof(info));
                }
                const bool bClosed = File.Close();
                if (!bDeleted || !bClosed)
                {
                    StateIOText path;
                    Paths::AsciiPath(Path, path);
                    error.append("; cleanup_failed_orphan=");
                    error.append(path);
                }
                return false;
            }
        };
        bool CreateTemp(const CookStateFileRequest& request, const Scope& scope, const Probe* probe, OwnedTemp& owner,
                        StateIOText& error)
        {
            for (unsigned attempt = 0; attempt < 32; ++attempt)
            {
                StateIOText leaf;
                if (attempt == 0 && probe && !probe->FirstTempLeaf.empty())
                {
                    leaf = probe->FirstTempLeaf;
                }
                else
                {
                    char suffix[100];
                    std::snprintf(suffix, sizeof(suffix), ".tmp-%lu-%lu-%llu-%u", GetCurrentProcessId(),
                                  GetCurrentThreadId(), static_cast<unsigned long long>(GetTickCount64()), attempt);
                    Paths::AsciiPath(request.StatePath.filename(), leaf);
                    leaf.append(suffix);
                }
                if (!Paths::SafeOutputName(leaf) || std::strchr(leaf.c_str(), '/'))
                {
                    return Fail(error, "invalid_temp_leaf");
                }
                const auto path = request.StatePath.parent_path() / std::filesystem::path(leaf.c_str());
                const auto physical = scope.PhysicalStateParent / path.filename();
                if (PhysicalEqual(physical, scope.PhysicalTarget) || PhysicalPrefix(physical, scope.PhysicalRuntime) ||
                    PhysicalPrefix(scope.PhysicalRuntime, physical))
                {
                    return Fail(error, "temp_scope_conflict");
                }
                HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (h != INVALID_HANDLE_VALUE)
                {
                    owner.File.Value = h;
                    owner.Path = path;
                    return HandleType(h, false) || Fail(error, "temp_not_regular");
                }
                const DWORD code = GetLastError();
                if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS)
                {
                    return Fail(error, "temp_create_failed");
                }
            }
            return Fail(error, "temp_collision_limit");
        }
        bool RenameHandle(HANDLE h, const std::filesystem::path& destination)
        {
            const auto& name = destination.native();
            if (name.size() > 32767)
            {
                return false;
            }
            const size_t size = offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t);
            // Win32構造体を置くため、custom allocatorのmax_align_t storageを確保する。
            Core::Container::VariableArray<std::max_align_t> storage((size + sizeof(std::max_align_t) - 1) /
                                                                     sizeof(std::max_align_t));
            std::memset(storage.data(), 0, storage.size() * sizeof(std::max_align_t));
            auto* info = ::new (static_cast<void*>(storage.data())) FILE_RENAME_INFO{};
            info->ReplaceIfExists = FALSE;
            info->RootDirectory = nullptr;
            info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
            std::memcpy(reinterpret_cast<uint8_t*>(storage.data()) + offsetof(FILE_RENAME_INFO, FileName), name.data(),
                        name.size() * sizeof(wchar_t));
            return SetFileInformationByHandle(h, FileRenameInfo, info, static_cast<DWORD>(size)) != FALSE;
        }
        bool Save(const CookStateFileRequest& request, const CookOwnedState& state, const Probe* probe,
                  StateIOText& error)
        {
            error.clear();
            OwnedTemp owner;
            try
            {
                if (!BindingEqual(state.Binding, request.ExpectedBinding))
                {
                    return Fail(error, "state_binding_mismatch");
                }
                StateIOText json;
                if (!SerializeCookOwnedState(state, json, error))
                {
                    return false;
                }
                Scope scope;
                if (!Prepare(request, scope, error))
                {
                    return false;
                }
                const auto fault = probe ? probe->Fault : Fault::None;
                if (!CreateTemp(request, scope, probe, owner, error))
                {
                    return owner.Abort(error);
                }
                owner.bFailCleanup = fault == Fault::CleanupDisposition;
                const auto stop = [&](const char* code)
                {
                    Fail(error, code);
                    return owner.Abort(error);
                };
                if (fault == Fault::AfterCreate || fault == Fault::CleanupDisposition)
                {
                    return stop("injected_after_create");
                }
                size_t offset = 0;
                while (offset < json.size())
                {
                    DWORD count = 0;
                    const size_t requested =
                        fault == Fault::PartialWrite ? std::max(size_t{1}, json.size() / 2) : json.size() - offset;
                    if (fault == Fault::ZeroWrite ||
                        !WriteFile(owner.File.Value, json.data() + offset, static_cast<DWORD>(requested), &count,
                                   nullptr) ||
                        count == 0)
                    {
                        return stop("write_failed_or_zero_progress");
                    }
                    offset += count;
                    if (fault == Fault::PartialWrite)
                    {
                        return stop("injected_partial_write");
                    }
                }
                if (fault == Fault::Flush || !FlushFileBuffers(owner.File.Value))
                {
                    return stop("flush_failed");
                }
                LARGE_INTEGER zero{};
                if (fault == Fault::Seek || !SetFilePointerEx(owner.File.Value, zero, nullptr, FILE_BEGIN))
                {
                    return stop("seek_failed");
                }
                StateIOBytes bytes;
                if (fault == Fault::ReadBack || !ReadAll(owner.File.Value, bytes, error))
                {
                    if (error.empty())
                    {
                        Fail(error, "injected_readback");
                    }
                    return owner.Abort(error);
                }
                if (fault == Fault::ByteMismatch && !bytes.empty())
                {
                    bytes[0] ^= 1;
                }
                if (bytes.size() != json.size() || std::memcmp(bytes.data(), json.data(), bytes.size()) != 0)
                {
                    return stop("readback_mismatch");
                }
                CookOwnedState verified;
                if (fault == Fault::ParseVerification ||
                    !ParseCookOwnedState(bytes, request.ExpectedBinding, verified, error))
                {
                    if (error.empty())
                    {
                        Fail(error, "injected_parse_verification");
                    }
                    return owner.Abort(error);
                }
                if (probe && probe->BeforePublish)
                {
                    probe->BeforePublish(request.StatePath, probe->Context);
                }
                Scope current;
                if (!Prepare(request, current, error) || !PhysicalEqual(scope.PhysicalTarget, current.PhysicalTarget) ||
                    !PhysicalEqual(scope.PhysicalRuntime, current.PhysicalRuntime))
                {
                    if (error.empty())
                    {
                        Fail(error, "scope_changed");
                    }
                    return owner.Abort(error);
                }
                if (fault == Fault::Rename || !RenameHandle(owner.File.Value, request.StatePath))
                {
                    return stop("publish_no_replace_failed");
                }
                owner.bPublished = true;
                // この後の失敗でも公開済みfileを消さない。盲目的な再試行で成功を偽装しない。
                if (!owner.File.Close())
                {
                    return Fail(error, "published_but_close_failed");
                }
                return true;
            }
            catch (const std::exception&)
            {
                Fail(error, "operation_exception");
                return owner.Abort(error);
            }
        }
#endif
    } // namespace
    CookStateLoadResult LoadCookOwnedState(const CookStateFileRequest& request, CookOwnedState& out, StateIOText& error)
    {
        error.clear();
#if !defined(_WIN32)
        (void)request;
        (void)out;
        Fail(error, "windows_required");
        return CookStateLoadResult::Error;
#else
        try
        {
            Scope scope;
            if (!Prepare(request, scope, error))
            {
                return CookStateLoadResult::Error;
            }
            Handle file;
            file.Value = CreateFileW(request.StatePath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file.Value == INVALID_HANDLE_VALUE)
            {
                if (GetLastError() == ERROR_FILE_NOT_FOUND)
                {
                    Scope current;
                    if (Prepare(request, current, error) && PhysicalEqual(scope.PhysicalTarget, current.PhysicalTarget))
                    {
                        return CookStateLoadResult::Missing;
                    }
                }
                if (error.empty())
                {
                    Fail(error, "state_open_failed");
                }
                return CookStateLoadResult::Error;
            }
            if (!HandleType(file.Value, false))
            {
                Fail(error, "state_not_regular_or_reparse");
                return CookStateLoadResult::Error;
            }
            StateIOBytes bytes;
            CookOwnedState candidate;
            if (!ReadAll(file.Value, bytes, error) ||
                !ParseCookOwnedState(bytes, request.ExpectedBinding, candidate, error))
            {
                return CookStateLoadResult::Error;
            }
            if (!file.Close())
            {
                Fail(error, "state_close_failed");
                return CookStateLoadResult::Error;
            }
            out = std::move(candidate);
            return CookStateLoadResult::Loaded;
        }
        catch (const std::exception&)
        {
            Fail(error, "operation_exception");
            return CookStateLoadResult::Error;
        }
#endif
    }
    bool WriteNewCookOwnedState(const CookStateFileRequest& request, const CookOwnedState& state, StateIOText& error)
    {
#if defined(_WIN32)
        return Save(request, state, nullptr, error);
#else
        (void)request;
        (void)state;
        return Fail(error, "windows_required");
#endif
    }
    namespace Detail
    {
        bool WriteNewCookOwnedStateForTest(const CookStateFileRequest& request, const CookOwnedState& state,
                                           const CookStateFileProbe& probe, StateIOText& error)
        {
#if defined(_WIN32)
            return Save(request, state, &probe, error);
#else
            (void)request;
            (void)state;
            (void)probe;
            return Fail(error, "windows_required");
#endif
        }
    } // namespace Detail
} // namespace NorvesLib::Tools::AssetCook
