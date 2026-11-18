// 資産ルートの解決と配下のパス検査に用いる共通処理を実装する。
#include "BridgeAssetPath.h"

#include <climits>
#include <limits>

namespace Game::Bridge::AssetPath
{
    bool Utf8ToWide(const Container::String& text, Container::WideString& out)
    {
        out.clear();
        if (text.empty() || text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        {
            return false;
        }
        for (size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] == '\0')
            {
                return false;
            }
        }
        const int length = static_cast<int>(text.size());
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
        if (count <= 0)
        {
            return false;
        }
        Container::VariableArray<wchar_t> buffer(static_cast<size_t>(count));
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, buffer.data(), count) != count)
        {
            return false;
        }
        out.append(buffer.data(), buffer.size());
        return true;
    }

    bool ResolveRoot(const Container::String& root, Container::WideString& out)
    {
        Container::WideString wide;
        if (!Utf8ToWide(root, wide))
        {
            return false;
        }
        Container::VariableArray<wchar_t> buffer(32768);
        const DWORD count = GetFullPathNameW(wide.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (count == 0 || count >= buffer.size())
        {
            return false;
        }
        out.append(buffer.data(), count);
        return true;
    }

    bool IsRelativeAssetPath(const Container::String& path)
    {
        if (path.empty() || path[0] == '/' || path[0] == '\\')
        {
            return false;
        }
        size_t segmentStart = 0;
        for (size_t index = 0; index <= path.size(); ++index)
        {
            if (index < path.size())
            {
                const unsigned char c = static_cast<unsigned char>(path[index]);
                if (c < 32 || c == ':' || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' || c == '*')
                {
                    return false;
                }
                if (c != '/' && c != '\\')
                {
                    continue;
                }
            }
            const size_t length = index - segmentStart;
            if (length == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.')
            {
                return false;
            }
            if (length > 0 && !(length == 1 && path[segmentStart] == '.') &&
                (path[index - 1] == '.' || path[index - 1] == ' '))
            {
                return false;
            }
            segmentStart = index + 1;
        }
        return true;
    }

    bool FinalPath(HANDLE handle, Container::WideString& out)
    {
        Container::VariableArray<wchar_t> buffer(32768);
        const DWORD count = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (count == 0 || count >= buffer.size())
        {
            return false;
        }
        out.clear();
        out.append(buffer.data(), count);
        return true;
    }

    bool IsWithinRoot(const Container::WideString& root, const Container::WideString& path)
    {
        if (root.empty() || path.size() <= root.size() || root.size() > static_cast<size_t>(INT_MAX) ||
            CompareStringOrdinal(root.data(), static_cast<int>(root.size()), path.data(),
                static_cast<int>(root.size()), TRUE) != CSTR_EQUAL)
        {
            return false;
        }
        return root[root.size() - 1] == L'\\' || path[root.size()] == L'\\';
    }

    bool OpenRoot(const Container::WideString& root, Container::WideString& outFinal)
    {
        NativeHandle directory(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        return directory.Value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(directory.Value, &info) &&
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && FinalPath(directory.Value, outFinal);
    }

    bool WideToFileStreamPath(const Container::WideString& path, Container::String& out)
    {
        if (path.empty() || path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        {
            return false;
        }
        const UINT codePage = GetACP();
        const DWORD flags = codePage == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        BOOL bUsedDefault = FALSE;
        BOOL* pUsedDefault = codePage == CP_UTF8 ? nullptr : &bUsedDefault;
        const int length = static_cast<int>(path.size());
        const int count = WideCharToMultiByte(codePage, flags, path.data(), length, nullptr, 0, nullptr, pUsedDefault);
        if (count <= 0 || bUsedDefault)
        {
            return false;
        }
        Container::VariableArray<char> bytes(static_cast<size_t>(count));
        if (WideCharToMultiByte(codePage, flags, path.data(), length, bytes.data(), count, nullptr, pUsedDefault) != count || bUsedDefault)
        {
            return false;
        }
        out.append(bytes.data(), bytes.size());
        return true;
    }

    bool HasExtension(const Container::String& path, const char* extension)
    {
        size_t length = 0;
        while (extension[length] != '\0')
        {
            ++length;
        }
        if (path.size() < length)
        {
            return false;
        }
        for (size_t index = 0; index < length; ++index)
        {
            char c = path[path.size() - length + index];
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
            if (c != extension[index])
            {
                return false;
            }
        }
        return true;
    }
} // namespace Game::Bridge::AssetPath
