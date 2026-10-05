#pragma once
// store観測とtransaction controllerが共有するnative観測。元locatorとlive handle以外はI/Oへ渡さない。
#include "CookManagedStoreObservation.h"
#include "CookPathIdentity.h"
#include "CookOutputPaths.h"
#include "ManagedStoreJson.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
namespace NorvesLib::Tools::AssetCook::Detail::ManagedStoreNative
{
    using Text = Core::Container::AnsiString;
    using Bytes = Core::Container::VariableArray<uint8_t>;
    using Id = Core::Container::FixedArray<uint8_t, 16>;
    namespace Paths = CookOutputPaths;
    inline bool Fail(Text& error, const char* code)
    {
        error = "cook_managed_store: ";
        error.append(code);
        return false;
    }
    inline constexpr size_t VolumeUnits = 49;
    inline constexpr wchar_t StoreLeaf[] = L".norves-assetcook";
    struct Handle
    {
        HANDLE Value = INVALID_HANDLE_VALUE;
        Handle() = default;
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
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
        ~Handle()
        {
            (void)Close();
        }
    };
    struct Identity
    {
        std::filesystem::path Canonical;
        Id FileId = Id(uint8_t{0});
        uint64_t Volume = 0;
    };
    inline bool SameId(const Id& a, const Id& b)
    {
        return std::memcmp(a.data(), b.data(), 16) == 0;
    }
    inline bool Same(const Identity& a, const Identity& b)
    {
        return a.Volume == b.Volume && SameId(a.FileId, b.FileId);
    }
    inline bool Fold(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.native().size()), b.c_str(),
                                    static_cast<int>(b.native().size()), TRUE) == CSTR_EQUAL;
    }
    inline bool GuidRoot(const std::filesystem::path& p)
    {
        const auto& s = p.native();
        constexpr wchar_t prefix[] = L"\\\\?\\Volume{";
        if (s.size() < VolumeUnits || std::memcmp(s.data(), prefix, 11 * sizeof(wchar_t)) || s[47] != L'}' ||
            s[48] != L'\\')
        {
            return false;
        }
        for (size_t i = 0; i < 36; ++i)
        {
            const auto c = s[11 + i];
            if (i == 8 || i == 13 || i == 18 || i == 23)
            {
                if (c != L'-')
                {
                    return false;
                }
            }
            else if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')))
            {
                return false;
            }
        }
        return true;
    }
    inline bool ObserveHandle(HANDLE h, bool bDirectory, Identity& out, Text& error)
    {
        FILE_ATTRIBUTE_TAG_INFO tag{};
        FILE_ID_INFO id{};
        if (GetFileType(h) != FILE_TYPE_DISK ||
            !GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) ||
            (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != bDirectory ||
            !GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof(id)))
        {
            return Fail(error, "type_reparse_or_identity");
        }
        const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_GUID;
        const DWORD needed = GetFinalPathNameByHandleW(h, nullptr, 0, flags);
        if (!needed || needed > Detail::MaximumCookLocatorUnits)
        {
            return Fail(error, "canonical_limit");
        }
        Core::Container::VariableArray<wchar_t> buffer(static_cast<size_t>(needed) + 1, 0);
        const DWORD used = GetFinalPathNameByHandleW(h, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
        if (!used || used >= buffer.size())
        {
            return Fail(error, "canonical_read");
        }
        out.Canonical = std::filesystem::path(buffer.data(), buffer.data() + used);
        if (!GuidRoot(out.Canonical))
        {
            return Fail(error, "volume_guid_required");
        }
        out.Volume = id.VolumeSerialNumber;
        std::memcpy(out.FileId.data(), id.FileId.Identifier, 16);
        return true;
    }
    inline bool Directory(const std::filesystem::path& locator, Identity& out, Text& error)
    {
        Handle h;
        h.Value = CreateFileW(locator.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h.Value == INVALID_HANDLE_VALUE)
        {
            return Fail(error, "directory_open");
        }
        if (!ObserveHandle(h.Value, true, out, error))
        {
            return false;
        }
        return h.Close() || Fail(error, "directory_close");
    }
    inline std::filesystem::path Parent(const std::filesystem::path& p)
    {
        const auto& s = p.native();
        const auto cut = s.find_last_of(L'\\');
        return std::filesystem::path(s.data(), s.data() + std::max(VolumeUnits, cut));
    }
    struct Budget
    {
        size_t Entries = 0, Metadata = 0, Roots = 0;
        bool Add(size_t bytes, Text& error)
        {
            if (bytes > MaximumCookStoreMetadataBytes - Metadata)
            {
                return Fail(error, "metadata_limit");
            }
            Metadata += bytes;
            return true;
        }
    };
    struct Entry
    {
        std::filesystem::path Name, ShortName;
    };
    using Entries = Core::Container::VariableArray<Entry>;
    inline bool Enumerate(const Identity& directory, Entries& out, Budget& budget, Text& error)
    {
        WIN32_FIND_DATAW data{};
        const auto pattern = directory.Canonical / L"*";
        struct FindHandle
        {
            HANDLE Value = INVALID_HANDLE_VALUE;
            bool Close()
            {
                if (Value == INVALID_HANDLE_VALUE)
                {
                    return true;
                }
                const auto h = Value;
                Value = INVALID_HANDLE_VALUE;
                return FindClose(h) != FALSE;
            }
            ~FindHandle()
            {
                (void)Close();
            }
        } find;
        find.Value = FindFirstFileExW(pattern.c_str(), FindExInfoStandard, &data, FindExSearchNameMatch, nullptr, 0);
        if (find.Value == INVALID_HANDLE_VALUE)
        {
            return GetLastError() == ERROR_FILE_NOT_FOUND || Fail(error, "enumeration_open");
        }
        bool bGood = true;
        for (;;)
        {
            if (std::wcscmp(data.cFileName, L".") && std::wcscmp(data.cFileName, L".."))
            {
                if (budget.Entries == MaximumCookStoreEntries || out.size() == 16384)
                {
                    bGood = Fail(error, "entry_limit");
                    break;
                }
                ++budget.Entries;
                Entry e{data.cFileName, data.cAlternateFileName};
                if (!budget.Add((e.Name.native().size() + e.ShortName.native().size()) * sizeof(wchar_t), error))
                {
                    bGood = false;
                    break;
                }
                out.push_back(std::move(e));
            }
            if (!FindNextFileW(find.Value, &data))
            {
                if (GetLastError() != ERROR_NO_MORE_FILES)
                {
                    bGood = Fail(error, "enumeration_next");
                }
                break;
            }
        }
        if (!find.Close())
        {
            bGood = Fail(error, "enumeration_close");
        }
        return bGood;
    }
    // fold一致する実長名を全件確認。aliasやcase違いを第2の管理領域にしない。
    inline bool Fixed(const Entries& entries, const wchar_t* name, bool bRequired, const Entry*& found, Text& error)
    {
        found = nullptr;
        for (const auto& e : entries)
        {
            if (!Fold(e.Name, name) && (e.ShortName.empty() || !Fold(e.ShortName, name)))
            {
                continue;
            }
            if (found || e.Name.native() != name)
            {
                return Fail(error, "control_name_collision");
            }
            found = &e;
        }
        return found || !bRequired || Fail(error, "control_missing");
    }
    inline bool Direct(const Identity& parent, const Identity& child)
    {
        return child.Volume == parent.Volume && Parent(child.Canonical).native() == parent.Canonical.native();
    }
    inline bool Read(const Identity& parent, const Entry& entry, size_t limit, Bytes& bytes, Budget& budget,
                     Text& error)
    {
        Handle h;
        h.Value = CreateFileW((parent.Canonical / entry.Name).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h.Value == INVALID_HANDLE_VALUE)
        {
            return Fail(error, "control_open");
        }
        Identity before, after;
        LARGE_INTEGER size{}, end{};
        FILE_STANDARD_INFO standard{};
        if (!ObserveHandle(h.Value, false, before, error) || !Direct(parent, before) ||
            before.Canonical.filename().native() != entry.Name.native() ||
            !GetFileInformationByHandleEx(h.Value, FileStandardInfo, &standard, sizeof(standard)) ||
            standard.NumberOfLinks != 1 || !GetFileSizeEx(h.Value, &size) || size.QuadPart <= 0 ||
            static_cast<uint64_t>(size.QuadPart) > limit)
        {
            return Fail(error, "control_type_size_or_parent");
        }
        if (!budget.Add(static_cast<size_t>(size.QuadPart), error))
        {
            return false;
        }
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h.Value, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) || got != bytes.size() ||
            !ObserveHandle(h.Value, false, after, error) || !Same(before, after) ||
            before.Canonical != after.Canonical || !GetFileSizeEx(h.Value, &end) || end.QuadPart != size.QuadPart)
        {
            return Fail(error, "control_read_changed");
        }
        return h.Close() || Fail(error, "control_close");
    }
    using Detail::ManagedStoreJson::Hex;
    using Detail::ManagedStoreJson::Json;
    using Detail::ManagedStoreJson::ReadId;
    using Detail::ManagedStoreJson::ReadU64;
    using Detail::ManagedStoreJson::Shape;
    using Detail::ManagedStoreJson::String;
    using Detail::ManagedStoreJson::Version;
    inline bool Header(const Bytes& bytes, const Identity& workspace, const Identity& store, const Text& volumeGuid,
                       CookManagedStoreView& out)
    {
        Core::JsonDocument doc;
        if (!Json(bytes, doc))
        {
            return false;
        }
        const auto v = doc.GetRoot();
        Text producer, guid;
        if (!Shape(v, {"producer", "schema", "store_id", "volume_guid", "volume_serial", "workspace_id",
                       "store_directory_id"}) ||
            !Version(v.FindMember("schema")) || !String(v.FindMember("producer"), producer) ||
            producer != "NorvesLib.AssetCook" || !Hex(v.FindMember("store_id"), 32, out.StoreId) ||
            !String(v.FindMember("volume_guid"), guid) || guid != volumeGuid ||
            !ReadU64(v.FindMember("volume_serial"), out.VolumeSerial, false) || out.VolumeSerial != workspace.Volume ||
            !ReadId(v.FindMember("workspace_id"), out.WorkspaceId) || !SameId(out.WorkspaceId, workspace.FileId) ||
            !ReadId(v.FindMember("store_directory_id"), out.StoreDirectoryId) ||
            !SameId(out.StoreDirectoryId, store.FileId))
        {
            return false;
        }
        return true;
    }
    inline bool Index(const Bytes& bytes, CookManagedStoreView& out, Budget& budget)
    {
        CookManagedStoreIndex parsed;
        Text error;
        if (!ParseCookManagedStoreIndex(bytes, out.StoreId, MaximumCookStoreRoots - budget.Roots, parsed, error))
        {
            return false;
        }
        budget.Roots += parsed.Roots.size();
        out.IndexGeneration = parsed.Generation;
        out.Roots = std::move(parsed.Roots);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook::Detail::ManagedStoreNative
#endif
