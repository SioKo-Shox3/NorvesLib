#include "Save/SaveSlotStore.h"
#include "Save/SaveContainer.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <Windows.h>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Save;

namespace
{
#define SLOT_CHECK(condition) \
    do { if (!(condition)) { std::fprintf(stderr, "確認失敗: %s (%s:%d)\n", #condition, __FILE__, __LINE__); return false; } } while (false)

    using Bytes = Container::VariableArray<uint8_t>;

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
                std::swprintf(name, sizeof(name) / sizeof(name[0]), L"NorvesLib-SaveSlotFallbackTest-%lu-%llu-%u",
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

    };

    Container::Span<const uint8_t> Payload(unsigned value)
    {
        const char* values[] = {
            "{\"formatVersion\":2,\"value\":0}", "{\"formatVersion\":2,\"value\":1}",
            "{\"formatVersion\":2,\"value\":2}", "{\"formatVersion\":2,\"value\":3}"};
        return {reinterpret_cast<const uint8_t*>(values[value]), std::strlen(values[value])};
    }

    bool EqualBytes(Container::Span<const uint8_t> left, Container::Span<const uint8_t> right)
    {
        return left.size() == right.size() &&
            (left.empty() || std::memcmp(left.data(), right.data(), left.size()) == 0);
    }

    bool Corrupt(const Container::String& path, Bytes& outBytes)
    {
        SLOT_CHECK(Platform::ReadFileBytes(path, outBytes).bSuccess);
        SLOT_CHECK(outBytes.size() > SaveContainerHeaderSize);
        outBytes[SaveContainerHeaderSize] ^= 1;
        SLOT_CHECK(Platform::WriteFileAtomic(path, outBytes, false).bSuccess);
        return true;
    }

    bool CheckQuarantine(const SaveLoadResult& result, const Container::String& original, const Bytes& corrupted)
    {
        SLOT_CHECK(!Platform::FileExists(original));
        auto prefix = original;
        prefix.append(".corrupt-");
        for (const auto& path : result.QuarantinedPaths)
        {
            if (path.find(prefix.c_str()) == 0)
            {
                Bytes bytes;
                SLOT_CHECK(Platform::ReadFileBytes(path, bytes).bSuccess);
                SLOT_CHECK(EqualBytes(bytes, corrupted));
                return true;
            }
        }
        SLOT_CHECK(false);
        return false;
    }

    bool TestBackup()
    {
        TestDirectory directory;
        SLOT_CHECK(!directory.Utf8.empty());
        SaveSlotStore store(directory.Utf8);
        SLOT_CHECK(store.SaveSlot(0, Payload(0)).bSuccess);
        SLOT_CHECK(store.SaveSlot(0, Payload(1)).bSuccess);
        Bytes corrupted;
        SLOT_CHECK(Corrupt(directory.File("slot_0.sav"), corrupted));
        const auto result = store.LoadSlot(0);
        SLOT_CHECK(result.Status == SaveLoadStatus::Loaded && result.Source == SaveLoadSource::Backup);
        SLOT_CHECK(result.WriteCounter == 1 && !result.bMigrated && EqualBytes(result.PayloadUtf8, Payload(0)));
        SLOT_CHECK(result.QuarantinedPaths.size() == 1);
        SLOT_CHECK(CheckQuarantine(result, directory.File("slot_0.sav"), corrupted));
        return true;
    }

    bool TestAutosaveRing()
    {
        TestDirectory directory;
        SLOT_CHECK(!directory.Utf8.empty());
        auto withSeparator = directory.Utf8;
        withSeparator.append("\\");
        SaveSlotStore store(withSeparator);
        SLOT_CHECK(store.SaveSlot(0, Payload(0)).bSuccess);
        SLOT_CHECK(store.SaveSlot(0, Payload(1)).bSuccess);
        for (uint32_t index = 0; index < 4; ++index)
        {
            uint32_t written = SaveAutosaveRingSize;
            SLOT_CHECK(store.SaveAutosave(Payload(index), &written).bSuccess);
            SLOT_CHECK(written == index % SaveAutosaveRingSize);
        }
        Bytes primary;
        Bytes backup;
        SLOT_CHECK(Corrupt(directory.File("slot_0.sav"), primary));
        SLOT_CHECK(Corrupt(directory.File("slot_0.sav.bak"), backup));
        const auto result = store.LoadSlot(0);
        SLOT_CHECK(result.Status == SaveLoadStatus::Loaded && result.Source == SaveLoadSource::Autosave);
        SLOT_CHECK(result.AutosaveIndex == 0 && result.WriteCounter == 4 && EqualBytes(result.PayloadUtf8, Payload(3)));
        SLOT_CHECK(result.QuarantinedPaths.size() == 2);
        SLOT_CHECK(CheckQuarantine(result, directory.File("slot_0.sav"), primary));
        SLOT_CHECK(CheckQuarantine(result, directory.File("slot_0.sav.bak"), backup));
        SLOT_CHECK(!Platform::FileExists(directory.File("autosave_0.sav.bak")));
        return true;
    }

    bool TestAllCorrupted()
    {
        TestDirectory directory;
        SLOT_CHECK(!directory.Utf8.empty());
        SaveSlotStore store(directory.Utf8);
        SLOT_CHECK(store.SaveSlot(0, Payload(0)).bSuccess);
        SLOT_CHECK(store.SaveSlot(0, Payload(1)).bSuccess);
        for (uint32_t index = 0; index < SaveAutosaveRingSize; ++index)
        {
            SLOT_CHECK(store.SaveAutosave(Payload(index)).bSuccess);
        }
        const char* names[] = {"slot_0.sav", "slot_0.sav.bak", "autosave_0.sav", "autosave_1.sav", "autosave_2.sav"};
        Bytes corrupted[5];
        for (size_t index = 0; index < 5; ++index)
        {
            SLOT_CHECK(Corrupt(directory.File(names[index]), corrupted[index]));
        }
        const auto result = store.LoadSlot(0);
        SLOT_CHECK(result.Status == SaveLoadStatus::NewGame && result.Source == SaveLoadSource::None);
        SLOT_CHECK(result.PayloadUtf8.empty() && result.QuarantinedPaths.size() == 5);
        for (size_t index = 0; index < 5; ++index)
        {
            SLOT_CHECK(CheckQuarantine(result, directory.File(names[index]), corrupted[index]));
        }
        return true;
    }

    bool TestSaveQuarantinesCorruption()
    {
        for (uint32_t scenario = 0; scenario < 2; ++scenario)
        {
            TestDirectory directory;
            SLOT_CHECK(!directory.Utf8.empty());
            SaveSlotStore store(directory.Utf8);
            SLOT_CHECK(store.SaveSlot(0, Payload(0)).bSuccess);
            SLOT_CHECK(store.SaveSlot(0, Payload(1)).bSuccess);
            const auto backupPath = directory.File("slot_0.sav.bak");
            Bytes backupBefore;
            SLOT_CHECK(Platform::ReadFileBytes(backupPath, backupBefore).bSuccess);
            const auto corruptedPath = directory.File(scenario == 0 ? "slot_0.sav" : "slot_0.sav.bak");
            Bytes corrupted;
            SLOT_CHECK(Corrupt(corruptedPath, corrupted));
            SLOT_CHECK(store.SaveSlot(0, Payload(2)).bSuccess);

            Container::VariableArray<Container::String> quarantinedPaths;
            auto pattern = directory.Wide;
            pattern.append(L"\\*.corrupt-*");
            WIN32_FIND_DATAW entry{};
            const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
            if (search != INVALID_HANDLE_VALUE)
            {
                do
                {
                    auto widePath = directory.Wide;
                    widePath.append(L"\\");
                    widePath.append(entry.cFileName);
                    quarantinedPaths.push_back(ToUtf8(widePath.c_str()));
                } while (FindNextFileW(search, &entry));
                FindClose(search);
            }
            SLOT_CHECK(quarantinedPaths.size() == 1);
            auto prefix = corruptedPath;
            prefix.append(".corrupt-");
            SLOT_CHECK(quarantinedPaths[0].find(prefix.c_str()) == 0);
            Bytes quarantinedBytes;
            SLOT_CHECK(Platform::ReadFileBytes(quarantinedPaths[0], quarantinedBytes).bSuccess);
            SLOT_CHECK(EqualBytes(quarantinedBytes, corrupted));
            if (scenario == 0)
            {
                Bytes backupAfter;
                SLOT_CHECK(Platform::ReadFileBytes(backupPath, backupAfter).bSuccess);
                SLOT_CHECK(EqualBytes(backupBefore, backupAfter));
            }
            const auto result = store.LoadSlot(0);
            SLOT_CHECK(result.Status == SaveLoadStatus::Loaded && result.Source == SaveLoadSource::Primary);
            SLOT_CHECK(EqualBytes(result.PayloadUtf8, Payload(2)) && result.QuarantinedPaths.empty());
        }
        return true;
    }

    bool TestCleanup()
    {
        TestDirectory directory;
        SLOT_CHECK(!directory.Utf8.empty());
        SaveSlotStore store(directory.Utf8);
        const auto path = directory.File("slot_0.sav.tmp-1-2");
        SLOT_CHECK(Platform::WriteFileAtomic(path, Payload(0), false).bSuccess);
        SLOT_CHECK(store.CleanupTemporaries() == 1);
        SLOT_CHECK(!Platform::FileExists(path));
        return true;
    }
}

int main()
{
    return TestBackup() && TestAutosaveRing() && TestAllCorrupted() && TestCleanup() && TestSaveQuarantinesCorruption() ? 0 : 1;
}
