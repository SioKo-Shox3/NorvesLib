// 固定control領域の物理祖先探索。保存pathを開かず、変更・採用・回復は行わない。
#include "CookManagedStoreObservation.h"
#include "CookDestinationLock.h"
#include "CookPathIdentity.h"
#include "CookOutputPaths.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <exception>
#include <initializer_list>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Text = Core::Container::AnsiString;
        using View = Core::Container::AnsiStringView;
        using Bytes = Core::Container::VariableArray<uint8_t>;
        using Id = Core::Container::FixedArray<uint8_t, 16>;
        using Result = CookManagedStoreResult;
        using Core::JsonValue;
        namespace Paths = Detail::CookOutputPaths;
        bool Fail(Text& error, const char* code)
        {
            error = "cook_managed_store: ";
            error.append(code);
            return false;
        }
#if defined(_WIN32)
        constexpr size_t VolumeUnits = 49;
        constexpr wchar_t StoreLeaf[] = L".norves-assetcook";
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
            Id FileId;
            uint64_t Volume = 0;
        };
        bool SameId(const Id& a, const Id& b)
        {
            return std::memcmp(a.data(), b.data(), 16) == 0;
        }
        bool Same(const Identity& a, const Identity& b)
        {
            return a.Volume == b.Volume && SameId(a.FileId, b.FileId);
        }
        bool Fold(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            return CompareStringOrdinal(a.c_str(), static_cast<int>(a.native().size()), b.c_str(),
                                        static_cast<int>(b.native().size()), TRUE) == CSTR_EQUAL;
        }
        bool GuidRoot(const std::filesystem::path& p)
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
        bool ObserveHandle(HANDLE h, bool bDirectory, Identity& out, Text& error)
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
        bool Directory(const std::filesystem::path& locator, Identity& out, Text& error)
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
        std::filesystem::path Parent(const std::filesystem::path& p)
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
        bool Enumerate(const Identity& directory, Entries& out, Budget& budget, Text& error)
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
            find.Value =
                FindFirstFileExW(pattern.c_str(), FindExInfoStandard, &data, FindExSearchNameMatch, nullptr, 0);
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
        bool Fixed(const Entries& entries, const wchar_t* name, bool bRequired, const Entry*& found, Text& error)
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
        bool Direct(const Identity& parent, const Identity& child)
        {
            return child.Volume == parent.Volume && Parent(child.Canonical).native() == parent.Canonical.native();
        }
        bool Read(const Identity& parent, const Entry& entry, size_t limit, Bytes& bytes, Budget& budget, Text& error)
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
            if (!ReadFile(h.Value, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) ||
                got != bytes.size() || !ObserveHandle(h.Value, false, after, error) || !Same(before, after) ||
                before.Canonical != after.Canonical || !GetFileSizeEx(h.Value, &end) || end.QuadPart != size.QuadPart)
            {
                return Fail(error, "control_read_changed");
            }
            return h.Close() || Fail(error, "control_close");
        }
        bool Key(const Core::Container::String& value, const char* text)
        {
            if (value.size() != std::strlen(text))
            {
                return false;
            }
            for (size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] != text[i])
                {
                    return false;
                }
            }
            return true;
        }
        bool Shape(const JsonValue& v, std::initializer_list<const char*> fields)
        {
            if (!v.IsObject() || v.GetObjectSize() != fields.size())
            {
                return false;
            }
            uint32_t seen = 0;
            for (size_t i = 0; i < v.GetObjectSize(); ++i)
            {
                size_t n = 0;
                for (const char* f : fields)
                {
                    if (Key(v.GetMemberName(i), f))
                    {
                        break;
                    }
                    ++n;
                }
                if (n == fields.size() || (seen & (1u << n)))
                {
                    return false;
                }
                seen |= 1u << n;
            }
            return true;
        }
        bool String(const JsonValue& v, Text& out)
        {
            if (!v.IsString() || v.AsString().empty() || v.AsString().size() > 255)
            {
                return false;
            }
            for (auto c : v.AsString())
            {
                if (c < 32 || c >= 127)
                {
                    return false;
                }
                out.push_back(static_cast<char>(c));
            }
            return true;
        }
        bool Hex(const JsonValue& v, size_t count, Text& out, bool bNonzero = true)
        {
            if (!String(v, out) || out.size() != count)
            {
                return false;
            }
            bool bAny = false;
            for (char c : out)
            {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                {
                    return false;
                }
                bAny |= c != '0';
            }
            return bAny || !bNonzero;
        }
        unsigned Digit(char c)
        {
            return c <= '9' ? c - '0' : c - 'a' + 10;
        }
        bool ReadId(const JsonValue& v, Id& out)
        {
            Text s;
            if (!Hex(v, 32, s))
            {
                return false;
            }
            for (size_t i = 0; i < 16; ++i)
            {
                out[i] = static_cast<uint8_t>(Digit(s[i * 2]) * 16 + Digit(s[i * 2 + 1]));
            }
            return true;
        }
        bool ReadU64(const JsonValue& v, uint64_t& out, bool bNonzero)
        {
            Text s;
            if (!Hex(v, 16, s, bNonzero))
            {
                return false;
            }
            out = 0;
            for (char c : s)
            {
                out = (out << 4) | Digit(c);
            }
            return true;
        }
        bool Json(const Bytes& bytes, Core::JsonDocument& out)
        {
            size_t depth = 0, tokens = 0;
            bool bString = false, bEscape = false;
            for (uint8_t c : bytes)
            {
                if (bString)
                {
                    if (bEscape)
                    {
                        bEscape = false;
                    }
                    else if (c == '\\')
                    {
                        bEscape = true;
                    }
                    else if (c == '"')
                    {
                        bString = false;
                    }
                }
                else if (c == '"')
                {
                    bString = true;
                    if (++tokens > 100000)
                    {
                        return false;
                    }
                }
                else if (c == '{' || c == '[')
                {
                    if (++depth > 8 || ++tokens > 100000)
                    {
                        return false;
                    }
                }
                else if (c == '}' || c == ']')
                {
                    if (!depth)
                    {
                        return false;
                    }
                    --depth;
                }
                else if (c == ',' || c == ':')
                {
                    if (++tokens > 100000)
                    {
                        return false;
                    }
                }
            }
            return !bString && !depth && Core::JsonDocument::TryParseUtf8(bytes, out);
        }
        bool Version(const JsonValue& v)
        {
            return v.IsIntegerLiteral() && v.AsNumber() == 1;
        }
        bool Header(const Bytes& bytes, const Identity& workspace, const Identity& store, const Text& volumeGuid,
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
                !ReadU64(v.FindMember("volume_serial"), out.VolumeSerial, false) ||
                out.VolumeSerial != workspace.Volume || !ReadId(v.FindMember("workspace_id"), out.WorkspaceId) ||
                !SameId(out.WorkspaceId, workspace.FileId) ||
                !ReadId(v.FindMember("store_directory_id"), out.StoreDirectoryId) ||
                !SameId(out.StoreDirectoryId, store.FileId))
            {
                return false;
            }
            return true;
        }
        bool RootLeaf(const Text& leaf)
        {
            return Paths::SafeOutputName(leaf) && !std::strchr(leaf.c_str(), '/') &&
                   !Fold(std::filesystem::path(leaf.c_str()), StoreLeaf);
        }
        bool Index(const Bytes& bytes, CookManagedStoreView& out, Budget& budget)
        {
            Core::JsonDocument doc;
            if (!Json(bytes, doc))
            {
                return false;
            }
            const auto v = doc.GetRoot();
            Text id;
            if (!Shape(v, {"schema", "store_id", "generation", "roots"}) || !Version(v.FindMember("schema")) ||
                !Hex(v.FindMember("store_id"), 32, id) || id != out.StoreId ||
                !ReadU64(v.FindMember("generation"), out.IndexGeneration, true))
            {
                return false;
            }
            const auto roots = v.FindMember("roots");
            if (!roots.IsArray() || roots.GetArraySize() > MaximumCookStoreRoots - budget.Roots)
            {
                return false;
            }
            budget.Roots += roots.GetArraySize();
            for (size_t i = 0; i < roots.GetArraySize(); ++i)
            {
                auto row = roots.GetArrayElement(i);
                CookManagedRootClaim c;
                if (!Shape(row, {"claim_id", "leaf", "directory_id", "owner_id"}) ||
                    !Hex(row.FindMember("claim_id"), 32, c.ClaimId) || !String(row.FindMember("leaf"), c.RootLeaf) ||
                    !RootLeaf(c.RootLeaf) || !ReadId(row.FindMember("directory_id"), c.DirectoryId) ||
                    !Hex(row.FindMember("owner_id"), 32, c.OwnerId))
                {
                    return false;
                }
                out.Roots.push_back(std::move(c));
            }
            // 初期4096件の索引をsortして重複を拒否し、record順は保持する。
            Core::Container::VariableArray<size_t> order;
            for (size_t i = 0; i < out.Roots.size(); ++i)
            {
                order.push_back(i);
            }
            for (unsigned kind = 0; kind < 3; ++kind)
            {
                const auto compare = [&](size_t a, size_t b)
                {
                    const auto& x = out.Roots[a];
                    const auto& y = out.Roots[b];
                    if (kind == 0)
                    {
                        return std::strcmp(x.ClaimId.c_str(), y.ClaimId.c_str());
                    }
                    if (kind == 1)
                    {
                        return std::memcmp(x.DirectoryId.data(), y.DirectoryId.data(), 16);
                    }
                    const auto& aLeaf = x.RootLeaf;
                    const auto& bLeaf = y.RootLeaf;
                    for (size_t i = 0; i < std::min(aLeaf.size(), bLeaf.size()); ++i)
                    {
                        const auto lower = [](char c)
                        {
                            return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
                        };
                        const int delta = lower(aLeaf[i]) - lower(bLeaf[i]);
                        if (delta)
                        {
                            return delta;
                        }
                    }
                    return aLeaf.size() == bLeaf.size() ? 0 : (aLeaf.size() < bLeaf.size() ? -1 : 1);
                };
                std::sort(order.begin(), order.end(),
                          [&](size_t a, size_t b)
                          {
                              return compare(a, b) < 0;
                          });
                for (size_t i = 1; i < order.size(); ++i)
                {
                    if (compare(order[i - 1], order[i]) == 0)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        struct Work
        {
            const CookOwnerResolveRequest* Request = nullptr;
            CookManagedStoreObservation Candidate;
            Text Reason;
            Result Status = Result::Error;
        };
        bool Inspect(const CookDestinationLockContext& lock, void* data, Text& error)
        {
            auto& work = *static_cast<Work*>(data);
            auto& out = work.Candidate;
            Budget budget;
            if (!ResolveCookOwnerBinding(*work.Request, out.Owner, error))
            {
                return false;
            }
            if (out.Owner.Identity.CanonicalFinalRuntimeRootIdentity != lock.CanonicalFinalRuntimeRootIdentity ||
                out.Owner.bFinalRuntimeRootPresent != lock.bFinalRuntimeRootPresent)
            {
                return Fail(error, "locked_root_changed");
            }
            out.CanonicalVolumeGuid = lock.CanonicalVolumeGuid;
            const auto conflict = [&](const char* reason)
            {
                work.Status = Result::Conflict;
                Fail(work.Reason, reason);
                return true;
            };
            const auto callerParent = out.Owner.FinalRuntimeRootLocator.parent_path();
            Identity workspace, root;
            if (!Directory(callerParent, workspace, error))
            {
                return false;
            }
            if (workspace.Canonical.native().size() == VolumeUnits)
            {
                return conflict("volume_root_workspace");
            }
            if (Fold(out.Owner.FinalRuntimeRootLocator.filename(), StoreLeaf))
            {
                return conflict("reserved_root_leaf");
            }
            if (out.Owner.bFinalRuntimeRootPresent &&
                (!Directory(out.Owner.FinalRuntimeRootLocator, root, error) || !Direct(workspace, root)))
            {
                return false;
            }
            Core::Container::VariableArray<Identity> chain;
            auto current = workspace;
            for (;;)
            {
                if (chain.size() == Detail::MaximumCookLocatorComponents ||
                    !budget.Add(current.Canonical.native().size() * sizeof(wchar_t), error))
                {
                    return Fail(error, "ancestry_limit");
                }
                chain.push_back(current);
                if (current.Canonical.native().size() == VolumeUnits)
                {
                    break;
                }
                const auto parent = Parent(current.Canonical);
                Identity next;
                if (!Directory(parent, next, error) || next.Volume != workspace.Volume ||
                    next.Canonical.native() != parent.native())
                {
                    return Fail(error, "physical_ancestry_changed");
                }
                current = std::move(next);
            }
            bool bOwnStore = false;
            for (size_t n = 0; n < chain.size(); ++n)
            {
                const auto& place = chain[n];
                Entries entries;
                if (!Enumerate(place, entries, budget, error))
                {
                    return false;
                }
                const Entry* entry = nullptr;
                if (!Fixed(entries, StoreLeaf, false, entry, error))
                {
                    return conflict("store_name_collision");
                }
                if (!entry)
                {
                    continue;
                }
                Identity store;
                if (!Directory(place.Canonical / entry->Name, store, error) || !Direct(place, store) ||
                    store.Canonical.filename().native() != StoreLeaf)
                {
                    return conflict("invalid_store_directory");
                }
                if (n == chain.size() - 1)
                {
                    return conflict("volume_root_store_unsupported");
                }
                if (out.Owner.bFinalRuntimeRootPresent && Same(store, root))
                {
                    return conflict("root_is_store");
                }
                for (const auto& ancestor : chain)
                {
                    if (Same(ancestor, store))
                    {
                        return conflict("inside_control_subtree");
                    }
                }
                Entries controls;
                if (!Enumerate(store, controls, budget, error))
                {
                    return false;
                }
                const Entry *header = nullptr, *index = nullptr, *pending = nullptr;
                if (!Fixed(controls, L"header.json", true, header, error) ||
                    !Fixed(controls, L"roots.json", false, index, error) ||
                    !Fixed(controls, L"pending", false, pending, error))
                {
                    return conflict("control_names");
                }
                CookManagedStoreView view;
                view.CanonicalWorkspace = place.Canonical;
                view.CanonicalStore = store.Canonical;
                view.bCurrentWorkspace = n == 0;
                Bytes bytes;
                if (!Read(store, *header, 16384, bytes, budget, error))
                {
                    return false;
                }
                if (!Header(bytes, place, store, lock.CanonicalVolumeGuid, view))
                {
                    return conflict("header_binding");
                }
                // pending中のindexは途中状態かもしれない。内容を採用せず、どのownerでも必ず止まる。
                if (pending)
                {
                    work.Status = Result::NeedsRecovery;
                    Fail(work.Reason, "pending_recovery_required");
                    return true;
                }
                if (!index)
                {
                    return conflict("index_missing");
                }
                if (!Read(store, *index, 1024 * 1024, bytes, budget, error))
                {
                    return false;
                }
                if (!Index(bytes, view, budget))
                {
                    return conflict("index_schema_or_duplicate");
                }
                // 各claimの長名を実列挙と照合。保存pathではなく今回のentryを開く。
                Core::Container::VariableArray<size_t> order;
                for (size_t i = 0; i < entries.size(); ++i)
                {
                    order.push_back(i);
                }
                std::sort(order.begin(), order.end(),
                          [&](size_t a, size_t b)
                          {
                              return entries[a].Name.native() < entries[b].Name.native();
                          });
                for (const auto& claim : view.Roots)
                {
                    const std::filesystem::path wanted(claim.RootLeaf.c_str());
                    auto it = std::lower_bound(order.begin(), order.end(), wanted,
                                               [&](size_t i, const auto& value)
                                               {
                                                   return entries[i].Name.native() < value.native();
                                               });
                    if (it == order.end() || entries[*it].Name.native() != wanted.native())
                    {
                        return conflict("active_root_missing_or_renamed");
                    }
                    Identity actual;
                    if (!Directory(place.Canonical / entries[*it].Name, actual, error) || !Direct(place, actual) ||
                        actual.Canonical.filename().native() != entries[*it].Name.native() ||
                        !SameId(actual.FileId, claim.DirectoryId))
                    {
                        return conflict("active_root_changed");
                    }
                    if (Same(actual, store))
                    {
                        return conflict("active_root_is_store");
                    }
                    for (const auto& ancestor : chain)
                    {
                        if (Same(actual, ancestor))
                        {
                            return conflict("nested_managed_root");
                        }
                    }
                    if (n == 0 && out.Owner.bFinalRuntimeRootPresent && Same(actual, root))
                    {
                        if (claim.OwnerId != out.Owner.ExpectedBinding.OwnerId)
                        {
                            return conflict("root_owner_mismatch");
                        }
                        out.bRuntimeRootClaimed = true;
                        out.CurrentRootClaimId = claim.ClaimId;
                    }
                }
                bOwnStore |= n == 0;
                out.Stores.push_back(std::move(view));
            }
            if (out.Owner.bFinalRuntimeRootPresent && !out.bRuntimeRootClaimed)
            {
                return conflict("existing_unowned_root");
            }
            // lock中でも観測後のroot/spec同一性を確認。非協調writerへのsnapshot保証ではない。
            Identity workspaceAfter, rootAfter;
            if (!Directory(callerParent, workspaceAfter, error) || !Same(workspace, workspaceAfter) ||
                workspace.Canonical != workspaceAfter.Canonical ||
                (out.Owner.bFinalRuntimeRootPresent &&
                 (!Directory(out.Owner.FinalRuntimeRootLocator, rootAfter, error) || !Same(root, rootAfter))))
            {
                return Fail(error, "workspace_or_root_changed");
            }
            CookResolvedOwnerBinding final;
            if (!ResolveCookOwnerBinding(*work.Request, final, error) ||
                final.Identity.CanonicalSpecLocator != out.Owner.Identity.CanonicalSpecLocator ||
                final.Identity.CanonicalFinalRuntimeRootIdentity !=
                    out.Owner.Identity.CanonicalFinalRuntimeRootIdentity ||
                final.bFinalRuntimeRootPresent != out.Owner.bFinalRuntimeRootPresent ||
                final.ExpectedBinding.OwnerId != out.Owner.ExpectedBinding.OwnerId)
            {
                return Fail(error, "owner_changed");
            }
            work.Status = bOwnStore ? Result::Observed : Result::StoreMissing;
            error.clear();
            return true;
        }
#endif
    } // namespace
    CookManagedStoreResult ObserveCookManagedStore(const CookOwnerResolveRequest& request,
                                                   CookManagedStoreObservation& out, Text& error)
    {
        error.clear();
#if !defined(_WIN32)
        (void)request;
        (void)out;
        Fail(error, "windows_required");
        return Result::Error;
#else
        Work work;
        work.Request = &request;
        const auto status = WithCookDestinationLock({request.FinalRuntimeRoot}, Inspect, &work, error);
        if (status == CookDestinationLockResult::Busy)
        {
            Fail(error, "volume_busy");
            return Result::Busy;
        }
        if (status != CookDestinationLockResult::Executed)
        {
            return Result::Error;
        }
        if (work.Status == Result::Observed || work.Status == Result::StoreMissing)
        {
            out = std::move(work.Candidate);
        }
        else
        {
            error = std::move(work.Reason);
        }
        return work.Status;
#endif
    }
} // namespace NorvesLib::Tools::AssetCook
