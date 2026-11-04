#include "Platform/PlatformPaths.h"
#include "Container/VariableArray.h"
#include <climits>
#include <Windows.h>
#include <ShlObj.h>

namespace NorvesLib::Core::Platform
{
    namespace
    {
        bool DecodePath(const Container::String& text, Container::WideString& out)
        {
            if (text.empty() || text.size() > INT_MAX)
            {
                return false;
            }
            for (const char value : text)
            {
                if (value == '\0')
                {
                    return false;
                }
            }
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (count <= 0)
            {
                return false;
            }
            Container::VariableArray<wchar_t> buffer(count);
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), buffer.data(), count) != count)
            {
                return false;
            }
            out.clear();
            out.append(buffer.data(), buffer.size());
            return true;
        }

        Container::String EncodePath(const wchar_t* path)
        {
            const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, nullptr, 0, nullptr, nullptr);
            if (count <= 0)
            {
                return {};
            }
            Container::VariableArray<char> buffer(count);
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, buffer.data(), count, nullptr, nullptr) != count)
            {
                return {};
            }
            Container::String result;
            result.append(buffer.data(), static_cast<size_t>(count - 1));
            return result;
        }

        bool IsAppNameValid(const Container::WideString& name)
        {
            if (name.empty() || name == L"." || name == L"..")
            {
                return false;
            }
            // Win32 が末尾を除去して別名や親ディレクトリとして扱うことを防ぐ。
            if (name.back() == L'.' || name.back() == L' ')
            {
                return false;
            }
            for (const wchar_t value : name)
            {
                if (value < 32 || (value >= 127 && value <= 159) || value == L'\\' || value == L'/' ||
                    value == L':' || value == L'*' || value == L'?' || value == L'"' || value == L'<' || value == L'>' || value == L'|')
                {
                    return false;
                }
            }
            return true;
        }

        bool EnsureDirectory(const Container::WideString& path)
        {
            if (CreateDirectoryW(path.c_str(), nullptr))
            {
                return true;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                return false;
            }
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
    }

    Container::String GetUserDataDirectoryUnder(const Container::String& rootDirectory, const Container::String& appName)
    {
        Container::WideString root;
        Container::WideString name;
        if (!DecodePath(rootDirectory, root) || !DecodePath(appName, name) || !IsAppNameValid(name))
        {
            return {};
        }
        Container::VariableArray<wchar_t> buffer(32768);
        const DWORD length = GetFullPathNameW(root.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length == 0 || length >= buffer.size())
        {
            return {};
        }
        Container::WideString path;
        for (DWORD index = 0; index < length; ++index)
        {
            const wchar_t value = buffer[index] == L'/' ? L'\\' : buffer[index];
            path.append(&value, 1);
        }
        if (path.back() != L'\\')
        {
            path.append(L"\\");
        }
        path.append(L"NorvesLib\\");
        path.append(name);
        path.append(L"\\");

        // ドライブの根、UNC のサーバー名と共有名は作成対象にしない。
        size_t start = 3;
        if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
        {
            size_t separators = (path.size() >= 8 && path.Substring(0, 8) == L"\\\\?\\UNC\\") ? 4 : 2;
            start = 2;
            while (start < path.size() && separators > 0)
            {
                if (path[start++] == L'\\')
                {
                    --separators;
                }
            }
        }
        for (size_t index = start; index < path.size(); ++index)
        {
            if (path[index] == L'\\' && !EnsureDirectory(path.Substring(0, index)))
            {
                return {};
            }
        }
        return EncodePath(path.c_str());
    }

    Container::String GetUserDataDirectory(const Container::String& appName)
    {
        PWSTR knownPath = nullptr;
        const HRESULT status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &knownPath);
        if (SUCCEEDED(status))
        {
            const Container::String root = knownPath ? EncodePath(knownPath) : Container::String{};
            CoTaskMemFree(knownPath);
            return GetUserDataDirectoryUnder(root, appName);
        }
        CoTaskMemFree(knownPath);
        const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (required == 0)
        {
            return {};
        }
        Container::VariableArray<wchar_t> buffer(required);
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required);
        if (length == 0 || length >= required)
        {
            return {};
        }
        return GetUserDataDirectoryUnder(EncodePath(buffer.data()), appName);
    }
} // namespace NorvesLib::Core::Platform
