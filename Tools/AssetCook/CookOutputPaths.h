#pragma once
// batchと増分照合で共有する、既存Windows出力pathのprivate検査。
#include "Container/String.h"
#include "Container/StringView.h"
#include <filesystem>
#include <cstring>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace NorvesLib::Tools::AssetCook::Detail::CookOutputPaths
{
    using Core::Container::AnsiString;
    using Core::Container::AnsiStringView;
    inline bool EqualName(AnsiStringView a, AnsiStringView b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }
    inline bool AsciiPath(const std::filesystem::path& path, AnsiString& out)
    {
        out.clear();
        for (const auto unit : path.native())
        {
            if (unit < 32 || unit >= 127)
            {
                return false;
            }
            out.push_back(unit == '\\' ? '/' : static_cast<char>(unit));
        }
        return true;
    }
    inline bool SafeOutputName(AnsiStringView name)
    {
        if (name.empty())
        {
            return false;
        }
        size_t segment = 0;
        for (size_t i = 0; i <= name.size(); ++i)
        {
            if (i < name.size())
            {
                const unsigned char c = name[i];
                if (c < 32 || c >= 127 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                    c == '>' || c == '|')
                {
                    return false;
                }
                if (c != '/')
                {
                    continue;
                }
            }
            if (i == segment || name[i - 1] == '.' || name[i - 1] == ' ')
            {
                return false;
            }
            AnsiString stem;
            for (size_t j = segment; j < i && name[j] != '.'; ++j)
            {
                const char c = name[j];
                stem.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
            }
            if ((!stem.empty() && stem.back() == ' ') || EqualName(stem, "CON") || EqualName(stem, "CONIN$") ||
                EqualName(stem, "CONOUT$") || EqualName(stem, "PRN") || EqualName(stem, "AUX") ||
                EqualName(stem, "NUL") || EqualName(stem, "CLOCK$") ||
                (stem.size() == 4 &&
                 (std::memcmp(stem.data(), "COM", 3) == 0 || std::memcmp(stem.data(), "LPT", 3) == 0) &&
                 stem[3] >= '1' && stem[3] <= '9'))
            {
                return false;
            }
            segment = i + 1;
        }
        return true;
    }
#if defined(_WIN32)
    inline bool NoReparse(const std::filesystem::path& path)
    {
        std::filesystem::path prefix = path.root_path();
        const auto rootAttributes = GetFileAttributesW(prefix.c_str());
        if (rootAttributes == INVALID_FILE_ATTRIBUTES || (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            return false;
        }
        for (const auto& part : path.relative_path())
        {
            prefix /= part;
            const auto attr = GetFileAttributesW(prefix.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES)
            {
                const auto code = GetLastError();
                return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
            }
            if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                return false;
            }
        }
        return true;
    }
    inline bool LocalDrivePath(const std::filesystem::path& path)
    {
        const auto rootPath = path.root_name();
        const auto& root = rootPath.native();
        if (!path.is_absolute() || root.size() != 2 || root[1] != ':' ||
            !((root[0] >= 'A' && root[0] <= 'Z') || (root[0] >= 'a' && root[0] <= 'z')))
        {
            return false;
        }
        const auto type = GetDriveTypeW(path.root_path().c_str());
        return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_CDROM || type == DRIVE_RAMDISK;
    }
    inline bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
    }
#endif
} // namespace NorvesLib::Tools::AssetCook::Detail::CookOutputPaths
