// 集合衝突検査のためのWindows file identity観測。公開/所有権の取得は行わない。
#include "CookPathIdentity.h"
#include "Resource/GltfNativePath.h"
#include "CookOutputPaths.h"
#include <algorithm>
#include <cstring>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace NorvesLib::Tools::AssetCook::Detail
{
    namespace
    {
        using IdentityText = Core::Container::AnsiString;
        bool Fail(IdentityText& error, const char* code)
        {
            error = "cook_path_identity: ";
            error.append(code);
            return false;
        }
#if defined(_WIN32)
        bool EqualAscii(const std::filesystem::path& part, const char* wanted, size_t end)
        {
            if (end != std::strlen(wanted))
            {
                return false;
            }
            const auto& text = part.native();
            for (size_t i = 0; i < end; ++i)
            {
                wchar_t c = text[i];
                if (c >= L'a' && c <= L'z')
                {
                    c -= L'a' - L'A';
                }
                if (c != wanted[i])
                {
                    return false;
                }
            }
            return true;
        }
        bool SafeComponent(const std::filesystem::path& part)
        {
            const auto& name = part.native();
            if (name.empty() || name.back() == L'.' || name.back() == L' ')
            {
                return false;
            }
            size_t stem = name.size();
            for (size_t i = 0; i < name.size(); ++i)
            {
                const wchar_t c = name[i];
                if (c < 32 || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' ||
                    c == L'/' || c == L'\\')
                {
                    return false;
                }
                if (c == L'.' && stem == name.size())
                {
                    stem = i;
                }
            }
            if (stem && name[stem - 1] == L' ')
            {
                return false;
            }
            for (const char* reserved : {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"})
            {
                if (EqualAscii(part, reserved, stem))
                {
                    return false;
                }
            }
            if (stem == 4 && (EqualAscii(part, "COM", 3) || EqualAscii(part, "LPT", 3)))
            {
                const wchar_t c = name[3];
                if ((c >= L'1' && c <= L'9') || c == 0xb9 || c == 0xb2 || c == 0xb3)
                {
                    return false;
                }
            }
            return true;
        }
        int CompareUnits(const wchar_t* a, size_t an, const wchar_t* b, size_t bn)
        {
            const int result = CompareStringOrdinal(a, static_cast<int>(an), b, static_cast<int>(bn), TRUE);
            return result == CSTR_LESS_THAN ? -1 : result == CSTR_GREATER_THAN ? 1 : 0;
        }
        int ComparePart(const CookPathIdentity& a, size_t ai, const CookPathIdentity& b, size_t bi)
        {
            const auto& x = a.Components[ai];
            const auto& y = b.Components[bi];
            return CompareUnits(a.Canonical.native().data() + x.Offset, x.Length,
                                b.Canonical.native().data() + y.Offset, y.Length);
        }
        bool BuildComponents(CookPathIdentity& identity, IdentityText& error)
        {
            const auto& s = identity.Canonical.native();
            if (s.empty() || s.size() > MaximumCookLocatorUnits)
            {
                return Fail(error, "physical_path_limit");
            }
            size_t start = 0;
            for (size_t i = 0; i <= s.size(); ++i)
            {
                if (i < s.size() && s[i] != L'\\' && s[i] != L'/')
                {
                    continue;
                }
                if (i > start)
                {
                    if (identity.Components.size() == MaximumCookLocatorComponents)
                    {
                        return Fail(error, "physical_component_limit");
                    }
                    identity.Components.push_back({static_cast<uint32_t>(start), static_cast<uint32_t>(i - start)});
                }
                start = i + 1;
            }
            constexpr wchar_t prefix[] = L"\\\\?\\Volume{";
            constexpr size_t n = sizeof(prefix) / sizeof(wchar_t) - 1;
            if (s.size() <= n || CompareUnits(s.data(), n, prefix, n) != 0 || identity.Components.size() < 3)
            {
                return Fail(error, "volume_guid_required");
            }
            const auto volume = identity.Components[1];
            if (volume.Length < 9 || s[volume.Offset + volume.Length - 1] != L'}')
            {
                return Fail(error, "volume_guid_required");
            }
            return true;
        }
#endif
    } // namespace
    bool NormalizeCookGuardLocator(const std::filesystem::path& input, std::filesystem::path& out, IdentityText& error)
    {
#if !defined(_WIN32)
        (void)input;
        (void)out;
        return Fail(error, "windows_required");
#else
        const auto& name = input.native();
        const auto root = input.root_name();
        const auto& drive = root.native();
        if (name.empty() || name.size() > MaximumCookLocatorUnits || !Core::Gltf::IsValidNativeSourcePath(input) ||
            !input.is_absolute() || drive.size() != 2 || drive[1] != L':' ||
            !((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z')) ||
            !input.has_filename())
        {
            return Fail(error, "unsupported_locator");
        }
        const auto separator = [](wchar_t c)
        {
            return c == L'/' || c == L'\\';
        };
        for (size_t i = 3; i < name.size(); ++i)
        {
            if (separator(name[i - 1]) && separator(name[i]))
            {
                return Fail(error, "duplicate_separator");
            }
        }
        size_t count = 0;
        for (const auto& part : input.relative_path())
        {
            if (++count > MaximumCookLocatorComponents || !SafeComponent(part))
            {
                return Fail(error, "unsafe_component");
            }
        }
        if (count == 0)
        {
            return Fail(error, "file_endpoint_required");
        }
        auto candidate = input;
        candidate.make_preferred();
        // drive文字だけはcase-insensitiveであり、componentのcase-sensitive設定とは独立。
        const auto& source = candidate.native();
        Core::Container::VariableArray<wchar_t> native(source.begin(), source.end());
        if (native[0] >= L'a' && native[0] <= L'z')
        {
            native[0] -= L'a' - L'A';
        }
        out = std::filesystem::path(native.data(), native.data() + native.size());
        return true;
#endif
    }
    bool ObserveCookPathIdentity(const std::filesystem::path& locator, CookPathIdentity& out, IdentityText& error)
    {
#if !defined(_WIN32)
        (void)locator;
        (void)out;
        return Fail(error, "windows_required");
#else
        std::filesystem::path normalized;
        if (!NormalizeCookGuardLocator(locator, normalized, error) || !CookOutputPaths::LocalDrivePath(normalized))
        {
            if (error.empty())
            {
                Fail(error, "local_volume_required");
            }
            return false;
        }
        auto prefix = normalized.root_path(), existing = prefix;
        const DWORD rootAttributes = GetFileAttributesW(prefix.c_str());
        if (rootAttributes == INVALID_FILE_ATTRIBUTES || (rootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            return Fail(error, "unsafe_root");
        }
        Core::Container::VariableArray<std::filesystem::path> missing;
        bool bAbsent = false;
        for (const auto& part : normalized.relative_path())
        {
            prefix /= part;
            if (bAbsent)
            {
                missing.push_back(part);
                continue;
            }
            const DWORD attributes = GetFileAttributesW(prefix.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD code = GetLastError();
                if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
                {
                    return Fail(error, "status_failed");
                }
                bAbsent = true;
                missing.push_back(part);
                continue;
            }
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                return Fail(error, "reparse_not_supported");
            }
            const bool bLeaf = prefix == normalized;
            if (((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == bLeaf)
            {
                return Fail(error, "endpoint_or_parent_type");
            }
            existing = prefix;
        }
        HANDLE h = CreateFileW(existing.c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | (bAbsent ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            return Fail(error, "identity_open_failed");
        }
        struct Close
        {
            HANDLE Handle;
            ~Close()
            {
                CloseHandle(Handle);
            }
        } close{h};
        FILE_ATTRIBUTE_TAG_INFO tag{};
        if (GetFileType(h) != FILE_TYPE_DISK ||
            !GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) ||
            (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != bAbsent)
        {
            return Fail(error, "identity_type_failed");
        }
        const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_GUID;
        const DWORD required = GetFinalPathNameByHandleW(h, nullptr, 0, flags);
        if (required == 0 || required > MaximumCookLocatorUnits)
        {
            return Fail(error, "final_name_failed");
        }
        Core::Container::VariableArray<wchar_t> buffer(static_cast<size_t>(required) + 1, 0);
        const DWORD count = GetFinalPathNameByHandleW(h, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
        if (count == 0 || count >= buffer.size())
        {
            return Fail(error, "final_name_failed");
        }
        CookPathIdentity candidate;
        candidate.Canonical = std::filesystem::path(buffer.data(), buffer.data() + count);
        for (const auto& part : missing)
        {
            candidate.Canonical /= part;
        }
        candidate.Canonical.make_preferred();
        candidate.bPresent = !bAbsent;
        if (!bAbsent)
        {
            FILE_ID_INFO id{};
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof(id)) ||
                !GetFileInformationByHandle(h, &info) || info.nNumberOfLinks == 0)
            {
                return Fail(error, "file_id_not_supported");
            }
            candidate.VolumeSerial = id.VolumeSerialNumber;
            candidate.LinkCount = info.nNumberOfLinks;
            std::memcpy(candidate.FileId.data(), id.FileId.Identifier, 16);
        }
        if (!BuildComponents(candidate, error))
        {
            return false;
        }
        out = std::move(candidate);
        return true;
#endif
    }
    int CompareCookPhysicalPath(const CookPathIdentity& a, const CookPathIdentity& b)
    {
#if defined(_WIN32)
        for (size_t i = 0; i < std::min(a.Components.size(), b.Components.size()); ++i)
        {
            const int c = ComparePart(a, i, b, i);
            if (c)
            {
                return c;
            }
        }
#endif
        return a.Components.size() == b.Components.size() ? 0 : (a.Components.size() < b.Components.size() ? -1 : 1);
    }
    bool CookPhysicalAncestor(const CookPathIdentity& a, const CookPathIdentity& b)
    {
        if (a.Components.size() >= b.Components.size())
        {
            return false;
        }
#if defined(_WIN32)
        for (size_t i = 0; i < a.Components.size(); ++i)
        {
            if (ComparePart(a, i, b, i))
            {
                return false;
            }
        }
#endif
        return true;
    }
    bool SameCookManifestEndpoint(const CookPathIdentity& a, const CookPathIdentity& b)
    {
        if (CompareCookPhysicalPath(a, b) != 0 || a.bPresent != b.bPresent)
        {
            return false;
        }
        if (a.bPresent)
        {
            return CompareCookFileIdentity(a, b) == 0;
        }
        // 不在leafのcase同値が同じfileを指すことは観測できない。
        return a.Canonical.native() == b.Canonical.native();
    }
    int CompareCookFileIdentity(const CookPathIdentity& a, const CookPathIdentity& b)
    {
#if defined(_WIN32)
        // volume serialだけが同じ別volumeを誤って同一fileとしない。
        const int volume = ComparePart(a, 1, b, 1);
        if (volume)
        {
            return volume;
        }
#endif
        if (a.VolumeSerial != b.VolumeSerial)
        {
            return a.VolumeSerial < b.VolumeSerial ? -1 : 1;
        }
        return std::memcmp(a.FileId.data(), b.FileId.data(), 16);
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
