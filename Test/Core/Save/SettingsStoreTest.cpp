#include "Save/SettingsStore.h"
#include "Text/JsonDocument.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <Windows.h>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Save;

namespace
{
#define SETTINGS_CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::fprintf(stderr, "確認失敗: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return false; \
        } \
    } while (false)

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
                std::swprintf(name, sizeof(name) / sizeof(name[0]), L"NorvesLib-SettingsStoreTest-%lu-%llu-%u",
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

    Container::Span<const uint8_t> JsonBytes(const char* text)
    {
        return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)};
    }

    bool EqualBytes(Container::Span<const uint8_t> left, Container::Span<const uint8_t> right)
    {
        return left.size() == right.size() &&
            (left.empty() || std::memcmp(left.data(), right.data(), left.size()) == 0);
    }

    bool TestRoundTrip()
    {
        TestDirectory directory;
        SETTINGS_CHECK(!directory.Utf8.empty());
        const auto path = directory.File("settings.json");
        SettingsStore store(path);
        UserSettings value;
        value.MasterVolume = 0.75f;
        value.EffectVolume = 0.5f;
        value.AmbienceVolume = 0.25f;
        value.ResolutionWidth = 1920;
        value.ResolutionHeight = 1080;
        value.bFullscreen = true;
        value.Language = "ja";
        value.KeyBindingsJson = R"({"game":{"jump":{"keys":["Space",32],"enabled":true,"scale":0.5,"fallback":null}}})";
        SETTINGS_CHECK(store.Set(value));
        SETTINGS_CHECK(store.Get() == value);
        SETTINGS_CHECK(store.Save().bSuccess);
        Bytes bytes;
        SETTINGS_CHECK(Platform::ReadFileBytes(path, bytes).bSuccess);
        JsonDocument document;
        SETTINGS_CHECK(JsonDocument::TryParseUtf8(bytes, document));
        const auto root = document.GetRoot();
        SETTINGS_CHECK(root.FindMember("formatVersion").IsIntegerLiteral());
        SETTINGS_CHECK(root.FindMember("formatVersion").AsUInt32() == 1);
        SETTINGS_CHECK(root.FindMember("keyBindings").IsObject());
        SettingsStore loaded(path);
        SETTINGS_CHECK(loaded.Load().Status == SettingsLoadStatus::Loaded);
        SETTINGS_CHECK(loaded.Get() == value);
        return true;
    }

    bool TestBackup()
    {
        TestDirectory directory;
        SETTINGS_CHECK(!directory.Utf8.empty());
        const auto path = directory.File("settings.json");
        SettingsStore store(path);
        UserSettings first;
        first.MasterVolume = 0.25f;
        SETTINGS_CHECK(store.Set(first));
        SETTINGS_CHECK(store.Save().bSuccess);
        auto second = first;
        second.MasterVolume = 0.75f;
        SETTINGS_CHECK(store.Set(second));
        SETTINGS_CHECK(store.Save().bSuccess);
        SETTINGS_CHECK(Platform::WriteFileAtomic(path, JsonBytes("{broken"), false).bSuccess);
        SettingsStore loaded(path);
        SETTINGS_CHECK(loaded.Load().Status == SettingsLoadStatus::LoadedFromBackup);
        SETTINGS_CHECK(loaded.Get() == first);
        return true;
    }

    bool TestNewerVersion()
    {
        TestDirectory directory;
        SETTINGS_CHECK(!directory.Utf8.empty());
        const auto path = directory.File("settings.json");
        auto backup = path;
        backup.append(".bak");
        SETTINGS_CHECK(Platform::WriteFileAtomic(path, JsonBytes(R"({"formatVersion":1,"language":"ja"})"), false).bSuccess);
        SETTINGS_CHECK(Platform::WriteFileAtomic(path, JsonBytes(R"({"formatVersion":2,"future":{"value":7}})"), true).bSuccess);
        Bytes primaryBefore;
        Bytes backupBefore;
        SETTINGS_CHECK(Platform::ReadFileBytes(path, primaryBefore).bSuccess);
        SETTINGS_CHECK(Platform::ReadFileBytes(backup, backupBefore).bSuccess);
        SettingsStore store(path);
        SETTINGS_CHECK(store.Load().Status == SettingsLoadStatus::RejectedNewerVersion);
        SETTINGS_CHECK(store.Get() == UserSettings{});
        UserSettings value;
        value.Language = "en";
        SETTINGS_CHECK(store.Set(value));
        SETTINGS_CHECK(!store.Save().bSuccess);
        Bytes primaryAfter;
        Bytes backupAfter;
        SETTINGS_CHECK(Platform::ReadFileBytes(path, primaryAfter).bSuccess);
        SETTINGS_CHECK(Platform::ReadFileBytes(backup, backupAfter).bSuccess);
        SETTINGS_CHECK(EqualBytes(primaryBefore, primaryAfter));
        SETTINGS_CHECK(EqualBytes(backupBefore, backupAfter));
        return true;
    }

    struct Subscriber
    {
        unsigned Calls = 0;
        UserSettings Last;

        void Changed(const UserSettings& value)
        {
            ++Calls;
            Last = value;
        }
    };

    bool TestNotifications()
    {
        TestDirectory directory;
        SETTINGS_CHECK(!directory.Utf8.empty());
        SettingsStore store(directory.File("settings.json"));
        Subscriber subscriber;
        store.OnValueChanged().Add(&subscriber, &Subscriber::Changed);
        UserSettings value;
        value.EffectVolume = 0.5f;
        SETTINGS_CHECK(store.Set(value));
        SETTINGS_CHECK(subscriber.Calls == 1 && subscriber.Last == value);
        SETTINGS_CHECK(!store.Set(value));
        SETTINGS_CHECK(subscriber.Calls == 1);
        store.OnValueChanged().Remove(&subscriber, &Subscriber::Changed);
        value.EffectVolume = 0.25f;
        SETTINGS_CHECK(store.Set(value));
        SETTINGS_CHECK(subscriber.Calls == 1);
        return true;
    }
}

int main()
{
    return TestRoundTrip() && TestBackup() && TestNewerVersion() && TestNotifications() ? 0 : 1;
}
