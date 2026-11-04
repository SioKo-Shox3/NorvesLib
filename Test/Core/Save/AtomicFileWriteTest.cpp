#include "Platform/PlatformFile.h"
#include "Platform/PlatformPaths.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <Windows.h>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Platform;

namespace
{
#define SAVE_CHECK(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            std::printf("%s: %s (%d)\n", reinterpret_cast<const char*>(u8"確認失敗"), #expression, __LINE__); \
            return false; \
        } \
    } while (false)

    Container::String ToUtf8(const wchar_t* text)
    {
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
        if (count <= 0)
        {
            return {};
        }
        Container::VariableArray<char> buffer(count);
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, buffer.data(), count, nullptr, nullptr) != count)
        {
            return {};
        }
        return Container::String(buffer.data());
    }

    void RemoveTestDirectory(const Container::WideString& directory)
    {
        Container::WideString pattern = directory;
        pattern.append(L"\\*");
        WIN32_FIND_DATAW entry{};
        const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
        if (search != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (std::wcscmp(entry.cFileName, L".") == 0 || std::wcscmp(entry.cFileName, L"..") == 0)
                {
                    continue;
                }
                Container::WideString child = directory;
                child.append(L"\\");
                child.append(entry.cFileName);
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                    {
                        RemoveTestDirectory(child);
                    }
                }
                else
                {
                    DeleteFileW(child.c_str());
                }
            } while (FindNextFileW(search, &entry));
            FindClose(search);
        }
        RemoveDirectoryW(directory.c_str());
    }

    struct TestDirectory
    {
        Container::WideString Wide;
        Container::String Utf8;

        TestDirectory()
        {
            wchar_t temporary[32768]{};
            const DWORD length = GetTempPathW(static_cast<DWORD>(sizeof(temporary) / sizeof(temporary[0])), temporary);
            if (length == 0 || length >= sizeof(temporary) / sizeof(temporary[0]))
            {
                return;
            }
            for (unsigned attempt = 0; attempt < 64; ++attempt)
            {
                wchar_t name[128]{};
                std::swprintf(name, sizeof(name) / sizeof(name[0]), L"NorvesLib-AtomicFileWriteTest-%lu-%llu-%u",
                    static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long long>(GetTickCount64()), attempt);
                Container::WideString candidate(temporary);
                candidate.append(name);
                if (CreateDirectoryW(candidate.c_str(), nullptr))
                {
                    Wide = candidate;
                    Utf8 = ToUtf8(Wide.c_str());
                    return;
                }
                if (GetLastError() != ERROR_ALREADY_EXISTS)
                {
                    return;
                }
            }
        }

        ~TestDirectory()
        {
            if (!Wide.empty())
            {
                RemoveTestDirectory(Wide);
            }
        }

        Container::String File(const char* name) const
        {
            Container::String path = Utf8;
            path.append("\\");
            path.append(name);
            return path;
        }

        bool HasNoTemporaries() const
        {
            Container::WideString pattern = Wide;
            pattern.append(L"\\*.tmp-*");
            WIN32_FIND_DATAW entry{};
            const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
            if (search == INVALID_HANDLE_VALUE)
            {
                return GetLastError() == ERROR_FILE_NOT_FOUND;
            }
            FindClose(search);
            return false;
        }
    };

    enum class InjectedFault
    {
        None,
        Flush,
        Close,
        BeforeReplace,
        Replace1175,
        Replace1176,
        Replace1177
    };

    class FaultOperations final : public IFileOperations
    {
    public:
        InjectedFault Fault = InjectedFault::None;
        uint32_t ReplaceSharingErrors = 0;
        uint32_t CreateSharingErrors = 0;
        uint32_t CreateCollisions = 0;
        uint32_t MoveSharingErrors = 0;
        uint32_t ReplaceCalls = 0;
        uint32_t CreateCalls = 0;
        uint32_t SleepCalls = 0;
        uint32_t MoveCalls = 0;
        bool bIncorrectRead = false;
        bool bFailRecoveryMove = false;
        bool bWrongSleepDuration = false;

        uint32_t CreateNew(const Container::WideString& path, Handle& outHandle) override
        {
            ++CreateCalls;
            if (CreateCollisions > 0)
            {
                --CreateCollisions;
                return ERROR_FILE_EXISTS;
            }
            if (CreateSharingErrors > 0)
            {
                --CreateSharingErrors;
                return ERROR_LOCK_VIOLATION;
            }
            return m_Inner.CreateNew(path, outHandle);
        }

        uint32_t Write(Handle handle, Container::Span<const uint8_t> bytes) override
        {
            return m_Inner.Write(handle, bytes);
        }

        uint32_t Flush(Handle handle) override
        {
            return Fault == InjectedFault::Flush ? ERROR_WRITE_FAULT : m_Inner.Flush(handle);
        }

        uint32_t Close(Handle handle) override
        {
            const uint32_t error = m_Inner.Close(handle);
            return Fault == InjectedFault::Close ? ERROR_WRITE_FAULT : error;
        }

        uint32_t Replace(const Container::WideString& path, const Container::WideString& temporary,
            const Container::WideString& backup) override
        {
            ++ReplaceCalls;
            if (ReplaceSharingErrors > 0)
            {
                --ReplaceSharingErrors;
                return ERROR_SHARING_VIOLATION;
            }
            switch (Fault)
            {
            case InjectedFault::BeforeReplace:
                return ERROR_ACCESS_DENIED;
            case InjectedFault::Replace1175:
                return ERROR_UNABLE_TO_REMOVE_REPLACED;
            case InjectedFault::Replace1176:
                return ERROR_UNABLE_TO_MOVE_REPLACEMENT;
            case InjectedFault::Replace1177:
            {
                // 1177 の時点で成立するディスク上の状態を再現する。
                const uint32_t error = m_Inner.MoveReplace(path, backup);
                return error == ERROR_SUCCESS ? ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 : error;
            }
            default:
                return m_Inner.Replace(path, temporary, backup);
            }
        }

        uint32_t MoveReplace(const Container::WideString& from, const Container::WideString& to) override
        {
            ++MoveCalls;
            if (MoveSharingErrors > 0)
            {
                --MoveSharingErrors;
                return ERROR_SHARING_VIOLATION;
            }
            if (bFailRecoveryMove && from.FindLast(L".tmp-") != Container::WideString::npos)
            {
                return ERROR_ACCESS_DENIED;
            }
            return m_Inner.MoveReplace(from, to);
        }

        uint32_t Delete(const Container::WideString& path) override
        {
            return m_Inner.Delete(path);
        }

        uint32_t ReadAll(const Container::WideString& path, Container::VariableArray<uint8_t>& outBytes) override
        {
            const uint32_t error = m_Inner.ReadAll(path, outBytes);
            if (error == ERROR_SUCCESS && bIncorrectRead)
            {
                outBytes.push_back(0xff);
            }
            return error;
        }

        void Sleep(uint32_t milliseconds) override
        {
            ++SleepCalls;
            bWrongSleepDuration = bWrongSleepDuration || milliseconds != 20;
        }

    private:
        IFileOperations& m_Inner = GetDefaultFileOperations();
    };

    const uint8_t OldBytes[] = {0, 1, 2, 3, 0xff};
    const uint8_t NewBytes[] = {9, 8, 7, 0, 6, 5};

    bool HasBytes(const Container::String& path, Container::Span<const uint8_t> expected)
    {
        Container::VariableArray<uint8_t> actual;
        return ReadFileBytes(path, actual).bSuccess && actual.size() == expected.size() &&
            (actual.empty() || std::memcmp(actual.data(), expected.data(), actual.size()) == 0);
    }

    bool TestInitialAndBackup(const TestDirectory& directory)
    {
        const Container::String path = directory.File("generations.sav");
        Container::String backup = path;
        backup.append(".bak");
        SAVE_CHECK(WriteFileAtomic(path, OldBytes, true).bSuccess);
        SAVE_CHECK(HasBytes(path, OldBytes));
        SAVE_CHECK(!FileExists(backup));
        SAVE_CHECK(directory.HasNoTemporaries());
        SAVE_CHECK(WriteFileAtomic(path, NewBytes, true).bSuccess);
        SAVE_CHECK(HasBytes(path, NewBytes));
        SAVE_CHECK(HasBytes(backup, OldBytes));
        SAVE_CHECK(directory.HasNoTemporaries());
        return true;
    }

    bool TestFailures(const TestDirectory& directory)
    {
        const Container::String path = directory.File("failures.sav");
        SAVE_CHECK(WriteFileAtomic(path, OldBytes, false).bSuccess);
        const InjectedFault faults[] = {InjectedFault::Flush, InjectedFault::Close, InjectedFault::BeforeReplace,
            InjectedFault::Replace1175, InjectedFault::Replace1176};
        for (const InjectedFault fault : faults)
        {
            FaultOperations ops;
            ops.Fault = fault;
            const auto result = WriteFileAtomic(path, NewBytes, true, ops);
            SAVE_CHECK(!result.bSuccess);
            SAVE_CHECK(result.NativeError != 0 && !result.Message.empty());
            SAVE_CHECK(HasBytes(path, OldBytes));
            SAVE_CHECK(directory.HasNoTemporaries());
        }
        return true;
    }

    bool TestPartialReplacement(const TestDirectory& directory)
    {
        const Container::String path = directory.File("partial.sav");
        Container::String backup = path;
        backup.append(".bak");
        SAVE_CHECK(WriteFileAtomic(path, OldBytes, false).bSuccess);
        FaultOperations completes;
        completes.Fault = InjectedFault::Replace1177;
        SAVE_CHECK(WriteFileAtomic(path, NewBytes, true, completes).bSuccess);
        SAVE_CHECK(HasBytes(path, NewBytes) && HasBytes(backup, OldBytes));
        SAVE_CHECK(completes.MoveCalls == 1 && directory.HasNoTemporaries());

        SAVE_CHECK(WriteFileAtomic(path, OldBytes, false).bSuccess);
        FaultOperations restores;
        restores.Fault = InjectedFault::Replace1177;
        restores.bFailRecoveryMove = true;
        SAVE_CHECK(!WriteFileAtomic(path, NewBytes, true, restores).bSuccess);
        SAVE_CHECK(HasBytes(path, OldBytes));
        SAVE_CHECK(restores.MoveCalls == 2 && directory.HasNoTemporaries());
        return true;
    }

    bool TestVerifyMismatch(const TestDirectory& directory)
    {
        const Container::String path = directory.File("verify.sav");
        SAVE_CHECK(WriteFileAtomic(path, OldBytes, false).bSuccess);
        FaultOperations ops;
        ops.bIncorrectRead = true;
        const auto result = WriteFileAtomic(path, NewBytes, true, ops);
        SAVE_CHECK(!result.bSuccess && result.Kind == FileErrorKind::VerifyMismatch);
        Container::String backup = path;
        backup.append(".bak");
        SAVE_CHECK(HasBytes(backup, OldBytes));
        SAVE_CHECK(directory.HasNoTemporaries());
        return true;
    }

    bool TestSharingRetries(const TestDirectory& directory)
    {
        const Container::String path = directory.File("sharing.sav");
        SAVE_CHECK(WriteFileAtomic(path, OldBytes, false).bSuccess);
        FaultOperations retries;
        retries.ReplaceSharingErrors = 2;
        SAVE_CHECK(WriteFileAtomic(path, NewBytes, true, retries).bSuccess);
        SAVE_CHECK(retries.ReplaceCalls == 3 && retries.SleepCalls == 2 && !retries.bWrongSleepDuration);
        SAVE_CHECK(HasBytes(path, NewBytes));
        SAVE_CHECK(directory.HasNoTemporaries());

        SAVE_CHECK(WriteFileAtomic(path, OldBytes, false).bSuccess);
        FaultOperations exhausted;
        exhausted.ReplaceSharingErrors = 100;
        const auto result = WriteFileAtomic(path, NewBytes, true, exhausted);
        SAVE_CHECK(!result.bSuccess && result.Kind == FileErrorKind::SharingViolation);
        SAVE_CHECK(exhausted.ReplaceCalls == 6 && exhausted.SleepCalls == 5);
        SAVE_CHECK(HasBytes(path, OldBytes) && directory.HasNoTemporaries());

        FaultOperations creates;
        creates.CreateCollisions = 1;
        creates.CreateSharingErrors = 2;
        creates.MoveSharingErrors = 2;
        SAVE_CHECK(WriteFileAtomic(path, NewBytes, false, creates).bSuccess);
        SAVE_CHECK(creates.CreateCalls == 4 && creates.MoveCalls == 3 && creates.SleepCalls == 4);
        SAVE_CHECK(!creates.bWrongSleepDuration && HasBytes(path, NewBytes) && directory.HasNoTemporaries());

        FaultOperations createExhausted;
        createExhausted.CreateSharingErrors = 100;
        SAVE_CHECK(!WriteFileAtomic(path, OldBytes, false, createExhausted).bSuccess);
        SAVE_CHECK(createExhausted.CreateCalls == 6 && createExhausted.SleepCalls == 5);
        SAVE_CHECK(HasBytes(path, NewBytes) && directory.HasNoTemporaries());
        return true;
    }

    bool TestCleanup(const TestDirectory& directory)
    {
        const Container::String leftover = directory.File("x.sav.tmp-1-0");
        const Container::String unrelated[] = {directory.File("x.sav"), directory.File("x.tmpl"),
            directory.File("x.sav.tmp-a-0"), directory.File("x.sav.tmp-1-0-extra"), directory.File("x.sav.tmp-1-")};
        SAVE_CHECK(WriteFileAtomic(leftover, OldBytes, false).bSuccess);
        for (const auto& path : unrelated)
        {
            SAVE_CHECK(WriteFileAtomic(path, OldBytes, false).bSuccess);
        }
        SAVE_CHECK(CleanupAtomicWriteTemporaries(directory.Utf8) == 1);
        SAVE_CHECK(!FileExists(leftover));
        for (const auto& path : unrelated)
        {
            SAVE_CHECK(HasBytes(path, OldBytes));
            SAVE_CHECK(DeleteFileIfExists(path).bSuccess);
        }
        SAVE_CHECK(directory.HasNoTemporaries());
        return true;
    }

    bool TestUnicodePaths(const TestDirectory& directory)
    {
        const Container::String appName(reinterpret_cast<const char*>(u8"テスト"));
        const Container::String root = directory.File("nested\\root");
        const Container::String userDirectory = GetUserDataDirectoryUnder(root, appName);
        Container::String expected = root;
        expected.append("\\NorvesLib\\");
        expected.append(appName);
        expected.append("\\");
        SAVE_CHECK(userDirectory == expected);
        SAVE_CHECK(GetUserDataDirectoryUnder(root, appName) == expected);
        Container::String path = userDirectory;
        path.append(reinterpret_cast<const char*>(u8"保存.sav"));
        SAVE_CHECK(WriteFileAtomic(path, NewBytes, true).bSuccess);
        SAVE_CHECK(HasBytes(path, NewBytes));
        const char* invalidNames[] = {"", "a\\b", "..", ".", "a/b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b", "a\x01", ".. "};
        for (const char* name : invalidNames)
        {
            SAVE_CHECK(GetUserDataDirectoryUnder(directory.Utf8, name).empty());
        }
        const Container::String invalidUtf8("\xc0\xaf");
        SAVE_CHECK(GetUserDataDirectoryUnder(root, invalidUtf8).empty());
        SAVE_CHECK(!WriteFileAtomic(invalidUtf8, OldBytes, false).bSuccess);
        return true;
    }

    bool TestSmallOperations(const TestDirectory& directory)
    {
        const Container::String source = directory.File("source.sav");
        const Container::String target = directory.File("target.sav");
        SAVE_CHECK(WriteFileAtomic(source, OldBytes, false).bSuccess);
        SAVE_CHECK(WriteFileAtomic(target, NewBytes, false).bSuccess);
        SAVE_CHECK(!RenameFileNoReplace(source, target).bSuccess);
        SAVE_CHECK(HasBytes(source, OldBytes) && HasBytes(target, NewBytes));
        SAVE_CHECK(DeleteFileIfExists(target).bSuccess);
        SAVE_CHECK(DeleteFileIfExists(target).bSuccess);
        SAVE_CHECK(RenameFileNoReplace(source, target).bSuccess);
        SAVE_CHECK(!FileExists(source) && HasBytes(target, OldBytes));
        Container::VariableArray<uint8_t> bytes{42};
        const auto missing = ReadFileBytes(source, bytes);
        SAVE_CHECK(!missing.bSuccess && missing.Kind == FileErrorKind::NotFound);
        SAVE_CHECK(bytes.size() == 1 && bytes[0] == 42);
        SAVE_CHECK(WriteFileAtomic(source, Container::Span<const uint8_t>{}, false).bSuccess);
        SAVE_CHECK(ReadFileBytes(source, bytes).bSuccess && bytes.empty());
        return true;
    }
#undef SAVE_CHECK
}

int main()
{
    TestDirectory directory;
    if (directory.Utf8.empty())
    {
        std::printf("%s\n", reinterpret_cast<const char*>(u8"一時ディレクトリを作成できません"));
        return 1;
    }
    if (!TestInitialAndBackup(directory) || !TestFailures(directory) || !TestPartialReplacement(directory) ||
        !TestVerifyMismatch(directory) || !TestSharingRetries(directory) || !TestCleanup(directory) ||
        !TestUnicodePaths(directory) || !TestSmallOperations(directory))
    {
        return 1;
    }
    std::printf("%s\n", reinterpret_cast<const char*>(u8"原子的な保存とパスの検証に成功しました"));
    return 0;
}
