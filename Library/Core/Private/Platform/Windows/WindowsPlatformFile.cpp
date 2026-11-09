#include "Platform/PlatformFile.h"
#include <climits>
#include <cstring>
#include <cwchar>
#include <limits>
#include <Windows.h>

namespace NorvesLib::Core::Platform
{
    namespace
    {
        constexpr uint32_t RetryLimit = 5;
        constexpr uint32_t RetryDelayMilliseconds = 20;
        volatile LONG64 TemporarySequence = 0;

        bool IsSharingError(uint32_t error)
        {
            return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
        }

        template <typename Operation>
        uint32_t RetrySharing(IFileOperations& ops, Operation operation)
        {
            for (uint32_t retry = 0; ; ++retry)
            {
                const uint32_t error = operation();
                if (!IsSharingError(error) || retry == RetryLimit)
                {
                    return error;
                }
                ops.Sleep(RetryDelayMilliseconds);
            }
        }

        FileOperationResult Success()
        {
            FileOperationResult result;
            result.bSuccess = true;
            return result;
        }

        FileOperationResult Failure(uint32_t error, const char* message)
        {
            FileOperationResult result;
            result.NativeError = error;
            result.Message = message;
            switch (error)
            {
            case ERROR_INVALID_NAME:
            case ERROR_BAD_PATHNAME:
            case ERROR_NO_UNICODE_TRANSLATION:
            case ERROR_FILENAME_EXCED_RANGE:
                result.Kind = FileErrorKind::InvalidPath;
                break;
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                result.Kind = FileErrorKind::NotFound;
                break;
            case ERROR_SHARING_VIOLATION:
            case ERROR_LOCK_VIOLATION:
                result.Kind = FileErrorKind::SharingViolation;
                break;
            case ERROR_ACCESS_DENIED:
                result.Kind = FileErrorKind::AccessDenied;
                break;
            default:
                result.Kind = FileErrorKind::IoError;
                break;
            }
            return result;
        }

        uint32_t ResolvePath(const Container::String& text, Container::WideString& out)
        {
            if (text.empty() || text.size() > INT_MAX)
            {
                return ERROR_INVALID_NAME;
            }
            for (const char value : text)
            {
                if (value == '\0')
                {
                    return ERROR_INVALID_NAME;
                }
            }
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (count == 0)
            {
                return GetLastError();
            }
            Container::VariableArray<wchar_t> wide(static_cast<size_t>(count) + 1, L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), count) != count)
            {
                return GetLastError();
            }
            // 相対パスを一度だけ絶対化し、途中の作業ディレクトリ変更の影響を避ける。
            Container::VariableArray<wchar_t> buffer(32768);
            const DWORD length = GetFullPathNameW(wide.data(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
            if (length == 0)
            {
                return GetLastError();
            }
            if (length >= buffer.size())
            {
                return ERROR_FILENAME_EXCED_RANGE;
            }
            out.clear();
            out.append(buffer.data(), length);
            return ERROR_SUCCESS;
        }

        class WindowsFileOperations final : public IFileOperations
        {
        public:
            uint32_t CreateNew(const Container::WideString& path, Handle& outHandle) override
            {
                const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                {
                    return GetLastError();
                }
                outHandle = handle;
                return ERROR_SUCCESS;
            }

            uint32_t Write(Handle handle, Container::Span<const uint8_t> bytes) override
            {
                size_t offset = 0;
                while (offset < bytes.size())
                {
                    const size_t remaining = bytes.size() - offset;
                    const DWORD requested = static_cast<DWORD>(remaining > MAXDWORD ? MAXDWORD : remaining);
                    DWORD written = 0;
                    if (!WriteFile(handle, bytes.data() + offset, requested, &written, nullptr))
                    {
                        return GetLastError();
                    }
                    if (written == 0)
                    {
                        return ERROR_WRITE_FAULT;
                    }
                    offset += written;
                }
                return ERROR_SUCCESS;
            }

            uint32_t Flush(Handle handle) override
            {
                return FlushFileBuffers(handle) ? ERROR_SUCCESS : GetLastError();
            }

            uint32_t Close(Handle handle) override
            {
                return CloseHandle(handle) ? ERROR_SUCCESS : GetLastError();
            }

            uint32_t Replace(const Container::WideString& path, const Container::WideString& temporary,
                const Container::WideString& backup) override
            {
                return ReplaceFileW(path.c_str(), temporary.c_str(), backup.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS,
                    nullptr, nullptr) ? ERROR_SUCCESS : GetLastError();
            }

            uint32_t MoveReplace(const Container::WideString& from, const Container::WideString& to) override
            {
                return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? ERROR_SUCCESS : GetLastError();
            }

            uint32_t Delete(const Container::WideString& path) override
            {
                return DeleteFileW(path.c_str()) ? ERROR_SUCCESS : GetLastError();
            }

            uint32_t ReadAll(const Container::WideString& path, Container::VariableArray<uint8_t>& outBytes) override
            {
                const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                {
                    return GetLastError();
                }
                struct ReadHandle
                {
                    HANDLE Value;
                    ~ReadHandle()
                    {
                        CloseHandle(Value);
                    }
                } owned{handle};
                if (GetFileType(handle) != FILE_TYPE_DISK)
                {
                    return ERROR_INVALID_DATA;
                }
                LARGE_INTEGER size{};
                if (!GetFileSizeEx(handle, &size))
                {
                    return GetLastError();
                }
                if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > (std::numeric_limits<size_t>::max)())
                {
                    return ERROR_FILE_TOO_LARGE;
                }
                Container::VariableArray<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
                size_t offset = 0;
                while (offset < bytes.size())
                {
                    const size_t remaining = bytes.size() - offset;
                    const DWORD requested = static_cast<DWORD>(remaining > MAXDWORD ? MAXDWORD : remaining);
                    DWORD read = 0;
                    if (!ReadFile(handle, bytes.data() + offset, requested, &read, nullptr))
                    {
                        return GetLastError();
                    }
                    if (read == 0)
                    {
                        return ERROR_HANDLE_EOF;
                    }
                    offset += read;
                }
                uint8_t extra = 0;
                DWORD read = 0;
                if (!ReadFile(handle, &extra, 1, &read, nullptr))
                {
                    return GetLastError();
                }
                if (read != 0)
                {
                    return ERROR_INVALID_DATA;
                }
                outBytes.swap(bytes);
                return ERROR_SUCCESS;
            }

            void Sleep(uint32_t milliseconds) override
            {
                ::Sleep(milliseconds);
            }
        } DefaultOperations;

        struct PendingWrite
        {
            IFileOperations& Ops;
            Container::WideString Path;
            IFileOperations::Handle File = nullptr;
            bool bOwned = false;

            ~PendingWrite()
            {
                if (File)
                {
                    Ops.Close(File);
                }
                if (bOwned)
                {
                    RetrySharing(Ops, [&]() { return Ops.Delete(Path); });
                }
            }
        };

        bool IsTemporaryName(const wchar_t* name)
        {
            const Container::WideString text(name);
            const size_t marker = text.FindLast(L".tmp-");
            if (marker == Container::WideString::npos || marker == 0)
            {
                return false;
            }
            size_t index = marker + 5;
            const size_t pidStart = index;
            while (index < text.size() && text[index] >= L'0' && text[index] <= L'9')
            {
                ++index;
            }
            if (index == pidStart || index == text.size() || text[index++] != L'-')
            {
                return false;
            }
            const size_t sequenceStart = index;
            while (index < text.size() && text[index] >= L'0' && text[index] <= L'9')
            {
                ++index;
            }
            return index > sequenceStart && index == text.size();
        }
    }

    IFileOperations& GetDefaultFileOperations()
    {
        return DefaultOperations;
    }

    FileOperationResult WriteFileAtomic(const Container::String& path, Container::Span<const uint8_t> bytes, bool bKeepBackup)
    {
        return WriteFileAtomic(path, bytes, bKeepBackup, GetDefaultFileOperations());
    }

    FileOperationResult WriteFileAtomic(const Container::String& path, Container::Span<const uint8_t> bytes,
        bool bKeepBackup, IFileOperations& ops)
    {
        Container::WideString target;
        uint32_t error = ResolvePath(path, target);
        if (error != ERROR_SUCCESS)
        {
            return Failure(error, "保存先のパスが無効です");
        }
        PendingWrite pending{ops};
        for (uint32_t collision = 0; collision < 64; ++collision)
        {
            wchar_t suffix[96]{};
            const int length = std::swprintf(suffix, sizeof(suffix) / sizeof(suffix[0]), L".tmp-%lu-%llu",
                static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long long>(InterlockedIncrement64(&TemporarySequence)));
            if (length < 0 || target.size() + static_cast<size_t>(length) >= 32767)
            {
                return Failure(ERROR_FILENAME_EXCED_RANGE, "一時ファイルのパスが長すぎます");
            }
            pending.Path = target;
            pending.Path.append(suffix, static_cast<size_t>(length));
            error = RetrySharing(ops, [&]() { return ops.CreateNew(pending.Path, pending.File); });
            if (error == ERROR_SUCCESS)
            {
                pending.bOwned = true;
                break;
            }
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            {
                return Failure(error, "一時ファイルを作成できません");
            }
        }
        if (!pending.bOwned)
        {
            return Failure(error, "一時ファイル名の衝突を解消できません");
        }
        error = ops.Write(pending.File, bytes);
        if (error == ERROR_SUCCESS)
        {
            error = ops.Flush(pending.File);
        }
        const uint32_t closeError = ops.Close(pending.File);
        pending.File = nullptr;
        if (error == ERROR_SUCCESS)
        {
            error = closeError;
        }
        if (error != ERROR_SUCCESS)
        {
            return Failure(error, "一時ファイルの書き込みを完了できません");
        }

        Container::WideString backup = target;
        backup.append(L".bak");
        if (bKeepBackup)
        {
            error = RetrySharing(ops, [&]() { return ops.Replace(target, pending.Path, backup); });
            if (error == ERROR_FILE_NOT_FOUND)
            {
                error = RetrySharing(ops, [&]() { return ops.MoveReplace(pending.Path, target); });
            }
            else if (error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)
            {
                // 1177 は旧世代がバックアップへ移動済み。新世代を公開できなければ旧世代を戻す。
                error = RetrySharing(ops, [&]() { return ops.MoveReplace(pending.Path, target); });
                if (error != ERROR_SUCCESS)
                {
                    const uint32_t restoreError = RetrySharing(ops, [&]() { return ops.MoveReplace(backup, target); });
                    if (restoreError != ERROR_SUCCESS)
                    {
                        return Failure(restoreError, "保存先を復旧できません。旧世代はバックアップを確認してください");
                    }
                }
            }
            // 1175 と 1176 は元の名前に旧世代が残るため、追加の移動をしない。
        }
        else
        {
            error = RetrySharing(ops, [&]() { return ops.MoveReplace(pending.Path, target); });
        }
        if (error != ERROR_SUCCESS)
        {
            return Failure(error, "保存ファイルを置き換えられません");
        }
        pending.bOwned = false;
        Container::VariableArray<uint8_t> readBack;
        error = ops.ReadAll(target, readBack);
        if (error != ERROR_SUCCESS)
        {
            return Failure(error, "保存ファイルの読み戻しに失敗しました");
        }
        if (readBack.size() != bytes.size() || (!bytes.empty() && std::memcmp(readBack.data(), bytes.data(), bytes.size()) != 0))
        {
            FileOperationResult result = Failure(ERROR_INVALID_DATA, "保存ファイルの読み戻し内容が一致しません");
            result.Kind = FileErrorKind::VerifyMismatch;
            return result;
        }
        return Success();
    }

    FileOperationResult ReadFileBytes(const Container::String& path, Container::VariableArray<uint8_t>& outBytes)
    {
        return ReadFileBytes(path, outBytes, GetDefaultFileOperations());
    }

    FileOperationResult ReadFileBytes(const Container::String& path, Container::VariableArray<uint8_t>& outBytes, IFileOperations& ops)
    {
        Container::WideString wide;
        uint32_t error = ResolvePath(path, wide);
        if (error != ERROR_SUCCESS)
        {
            return Failure(error, "読み込み先のパスが無効です");
        }
        Container::VariableArray<uint8_t> bytes;
        error = ops.ReadAll(wide, bytes);
        if (error != ERROR_SUCCESS)
        {
            return Failure(error, "ファイルを読み込めません");
        }
        outBytes.swap(bytes);
        return Success();
    }

    uint32_t CleanupAtomicWriteTemporaries(const Container::String& directory)
    {
        Container::WideString wide;
        if (ResolvePath(directory, wide) != ERROR_SUCCESS)
        {
            return 0;
        }
        if (wide.back() != L'\\' && wide.back() != L'/')
        {
            wide.append(L"\\");
        }
        Container::WideString pattern = wide;
        pattern.append(L"*.tmp-*");
        WIN32_FIND_DATAW entry{};
        const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE)
        {
            return 0;
        }
        uint32_t deleted = 0;
        do
        {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && IsTemporaryName(entry.cFileName))
            {
                Container::WideString path = wide;
                path.append(entry.cFileName);
                if (DeleteFileW(path.c_str()))
                {
                    ++deleted;
                }
            }
        } while (FindNextFileW(search, &entry));
        FindClose(search);
        return deleted;
    }

    bool FileExists(const Container::String& path)
    {
        Container::WideString wide;
        if (ResolvePath(path, wide) != ERROR_SUCCESS)
        {
            return false;
        }
        const DWORD attributes = GetFileAttributesW(wide.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    FileOperationResult RenameFileNoReplace(const Container::String& from, const Container::String& to)
    {
        Container::WideString source;
        Container::WideString target;
        uint32_t error = ResolvePath(from, source);
        if (error == ERROR_SUCCESS)
        {
            error = ResolvePath(to, target);
        }
        if (error != ERROR_SUCCESS)
        {
            return Failure(error, "移動元または移動先のパスが無効です");
        }
        if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            return Failure(GetLastError(), "ファイル名を変更できません");
        }
        return Success();
    }

    FileOperationResult DeleteFileIfExists(const Container::String& path)
    {
        Container::WideString wide;
        uint32_t error = ResolvePath(path, wide);
        if (error != ERROR_SUCCESS)
        {
            return Failure(error, "削除するファイルのパスが無効です");
        }
        error = GetDefaultFileOperations().Delete(wide);
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        {
            return Failure(error, "ファイルを削除できません");
        }
        return Success();
    }
} // namespace NorvesLib::Core::Platform
