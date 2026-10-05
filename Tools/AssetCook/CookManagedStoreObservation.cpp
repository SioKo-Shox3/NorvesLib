// 固定control領域の観測とfresh初期化。保存pathからの採用や資産本体の更新は行わない。
#include "CookManagedStoreObservation.h"
#include "CookManagedStoreIndex.h"
#include "ManagedStoreJson.h"
#include "CookManagedStoreInitialization.h"
#include "CookManagedStoreInitializationTestAccess.h"
#include "CookDestinationLockTestAccess.h"
#include "NativeCookPath.h"
#include "CookDestinationLock.h"
#include "CookPathIdentity.h"
#include "CookOutputPaths.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <exception>
#include <initializer_list>
#include <cstddef>
#include <new>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#include <bcrypt.h>
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
        using Detail::ManagedStoreJson::Shape;
        using Detail::ManagedStoreJson::String;
        using Detail::ManagedStoreJson::Hex;
        using Detail::ManagedStoreJson::ReadId;
        using Detail::ManagedStoreJson::ReadU64;
        using Detail::ManagedStoreJson::Json;
        using Detail::ManagedStoreJson::Version;
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
        bool Index(const Bytes& bytes, CookManagedStoreView& out, Budget& budget)
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
        struct Work
        {
            const CookOwnerResolveRequest* Request = nullptr;
            CookManagedStoreObservation Candidate;
            Identity WorkspaceSnapshot;
            Text Reason;
            Result Status = Result::Error;
        };
        bool ObserveLocked(const CookDestinationLockContext& lock, Work& work, Text& error)
        {
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
            work.WorkspaceSnapshot = workspace;
            error.clear();
            return true;
        }
        bool ObserveCallback(const CookDestinationLockContext& lock, void* data, Text& error)
        {
            return ObserveLocked(lock, *static_cast<Work*>(data), error);
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
        const auto status = WithCookDestinationLock({request.FinalRuntimeRoot}, ObserveCallback, &work, error);
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

    namespace
    {
        using InitResult = CookManagedStoreInitializationResult;
        using InitFault = Detail::CookStoreInitFault;
        using InitPoint = Detail::CookStoreInitPoint;
        bool InitFail(Text& error, const char* reason)
        {
            error = "cook_store_initialization: ";
            error.append(reason);
            return false;
        }
#if defined(_WIN32)
        constexpr char StagePrefix[] = ".assetcook-store-stage-";
        struct InitWork
        {
            const CookOwnerResolveRequest* Request = nullptr;
            const Detail::CookStoreInitProbe* Probe = nullptr;
            CookManagedStoreObservation Candidate;
            InitResult Status = InitResult::Error;
            Text Reason, OrphanLocator;
            bool bPublished = false, bCleanupIncomplete = false, bCloseFailed = false;
        };
        bool Dispose(HANDLE h)
        {
            FILE_DISPOSITION_INFO info{};
            info.DeleteFile = TRUE;
            return SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info)) != FALSE;
        }
        struct InitOwner
        {
            InitWork& Work;
            Handle Workspace, Stage, HeaderFile, IndexFile;
            Identity WorkspaceId, StageId;
            std::filesystem::path StageLocator, Destination;
            bool bStageCreated = false, bStageKnown = false, bHeaderKnown = false, bIndexKnown = false;
            explicit InitOwner(InitWork& work) : Work(work)
            {
            }
            ~InitOwner()
            {
                const bool bFault = Work.Probe && Work.Probe->Fault == InitFault::CleanupDisposition;
                const bool bClosedChildren = (bHeaderKnown && HeaderFile.Value == INVALID_HANDLE_VALUE) ||
                                             (bIndexKnown && IndexFile.Value == INVALID_HANDLE_VALUE);
                // 開いたまま所有を確認できるobjectだけを消す。閉じたchildは再openせずorphanに残す。
                if (!Work.bPublished)
                {
                    if (bHeaderKnown && HeaderFile.Value != INVALID_HANDLE_VALUE &&
                        (bFault || !Dispose(HeaderFile.Value)))
                    {
                        Work.bCleanupIncomplete = true;
                    }
                    if (bIndexKnown && IndexFile.Value != INVALID_HANDLE_VALUE && (bFault || !Dispose(IndexFile.Value)))
                    {
                        Work.bCleanupIncomplete = true;
                    }
                }
                if (!HeaderFile.Close())
                {
                    Work.bCloseFailed = true;
                }
                if (!IndexFile.Close())
                {
                    Work.bCloseFailed = true;
                }
                if (!Work.bPublished && bStageCreated)
                {
                    if (!bStageKnown || Stage.Value == INVALID_HANDLE_VALUE || bClosedChildren || bFault ||
                        !Dispose(Stage.Value))
                    {
                        Work.bCleanupIncomplete = true;
                    }
                }
                if (!Stage.Close())
                {
                    Work.bCloseFailed = true;
                }
                if (!Workspace.Close())
                {
                    Work.bCloseFailed = true;
                }
            }
        };
        Text HexBytes(const uint8_t* bytes, size_t size)
        {
            constexpr char digits[] = "0123456789abcdef";
            Text out;
            for (size_t i = 0; i < size; ++i)
            {
                out.push_back(digits[bytes[i] >> 4]);
                out.push_back(digits[bytes[i] & 15]);
            }
            return out;
        }
        Text HexNumber(uint64_t number)
        {
            uint8_t bytes[8]{};
            for (size_t i = 0; i < 8; ++i)
            {
                bytes[7 - i] = static_cast<uint8_t>(number >> (8 * i));
            }
            return HexBytes(bytes, 8);
        }
        bool InitFailWin32(Text& error, const char* reason)
        {
            const DWORD code = GetLastError();
            InitFail(error, reason);
            error.append("; win32_hex=");
            error.append(HexNumber(code));
            return false;
        }
        bool RandomToken(Text& out, InitFault fault, Text& error)
        {
            Core::Container::FixedArray<uint8_t, 16> bytes;
            if (fault == InitFault::Rng || BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                                                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            {
                return InitFail(error, "rng_failed");
            }
            bool bNonzero = false;
            for (uint8_t b : bytes)
            {
                bNonzero |= b != 0;
            }
            if (!bNonzero)
            {
                return InitFail(error, "rng_zero_identifier");
            }
            out = HexBytes(bytes.data(), bytes.size());
            return true;
        }
        bool StageLeafValid(const Text& text)
        {
            constexpr size_t prefix = sizeof(StagePrefix) - 1;
            if (text.size() != prefix + 32 || std::memcmp(text.data(), StagePrefix, prefix) != 0)
            {
                return false;
            }
            for (size_t i = prefix; i < text.size(); ++i)
            {
                const char c = text[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                {
                    return false;
                }
            }
            return true;
        }
        void Point(InitWork& work, InitPoint point, const InitOwner& owner)
        {
            if (work.Probe && work.Probe->Checkpoint)
            {
                work.Probe->Checkpoint(point, owner.StageLocator, owner.Destination, work.Probe->Context);
            }
        }
        bool OwnerEqual(const CookResolvedOwnerBinding& a, const CookResolvedOwnerBinding& b)
        {
            return a.bFinalRuntimeRootPresent == b.bFinalRuntimeRootPresent &&
                   a.Identity.CanonicalSpecLocator == b.Identity.CanonicalSpecLocator &&
                   a.Identity.CanonicalFinalRuntimeRootIdentity == b.Identity.CanonicalFinalRuntimeRootIdentity &&
                   a.ExpectedBinding.OwnerId == b.ExpectedBinding.OwnerId &&
                   a.ExpectedBinding.RuntimeRootIdentity == b.ExpectedBinding.RuntimeRootIdentity &&
                   a.ExpectedBinding.ManifestName == b.ExpectedBinding.ManifestName;
        }
        Result RunObservation(const CookDestinationLockContext& lock, const CookOwnerResolveRequest& request,
                              CookManagedStoreObservation& out, Identity& workspace, Text& error)
        {
            Work work;
            work.Request = &request;
            if (!ObserveLocked(lock, work, error))
            {
                return Result::Error;
            }
            if (work.Status == Result::Observed || work.Status == Result::StoreMissing)
            {
                out = std::move(work.Candidate);
                workspace = std::move(work.WorkspaceSnapshot);
                error.clear();
            }
            else
            {
                error = std::move(work.Reason);
            }
            return work.Status;
        }
        InitResult BeforeResult(Result result)
        {
            if (result == Result::Observed)
            {
                return InitResult::StoreExists;
            }
            if (result == Result::NeedsRecovery)
            {
                return InitResult::NeedsRecovery;
            }
            if (result == Result::Conflict)
            {
                return InitResult::Conflict;
            }
            return InitResult::Error;
        }
        bool RecheckMissing(const CookDestinationLockContext& lock, InitOwner& owner,
                            const CookManagedStoreObservation& first, Text& error)
        {
            CookManagedStoreObservation current;
            Identity workspace;
            if (RunObservation(lock, *owner.Work.Request, current, workspace, error) != Result::StoreMissing ||
                !OwnerEqual(first.Owner, current.Owner) || !Same(workspace, owner.WorkspaceId) ||
                workspace.Canonical != owner.WorkspaceId.Canonical)
            {
                if (error.empty())
                {
                    InitFail(error, "scope_changed_before_publish");
                }
                return false;
            }
            Identity held;
            return ObserveHandle(owner.Workspace.Value, true, held, error) && Same(held, owner.WorkspaceId) &&
                   held.Canonical == owner.WorkspaceId.Canonical;
        }
        bool CreateStage(InitOwner& owner, InitFault fault, Text& error)
        {
            const auto parent = owner.Work.Request->FinalRuntimeRoot.parent_path();
            if (owner.Work.Probe && !owner.Work.Probe->FirstStageLeaf.empty() &&
                !StageLeafValid(owner.Work.Probe->FirstStageLeaf))
            {
                return InitFail(error, "invalid_stage_override");
            }
            for (size_t attempt = 0; attempt < 32; ++attempt)
            {
                Text leaf;
                if (attempt == 0 && owner.Work.Probe && !owner.Work.Probe->FirstStageLeaf.empty())
                {
                    leaf = owner.Work.Probe->FirstStageLeaf;
                }
                else
                {
                    Text token;
                    if (!RandomToken(token, fault, error))
                    {
                        return false;
                    }
                    leaf = StagePrefix;
                    leaf.append(token);
                }
                owner.StageLocator = parent / std::filesystem::path(leaf.c_str());
                if (owner.StageLocator.native().size() > Detail::MaximumCookLocatorUnits)
                {
                    return InitFail(error, "stage_path_limit");
                }
                if (!CreateDirectoryW(owner.StageLocator.c_str(), nullptr))
                {
                    const DWORD code = GetLastError();
                    if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS)
                    {
                        continue;
                    }
                    return InitFailWin32(error, "stage_create_failed");
                }
                owner.bStageCreated = true;
                (void)Detail::EncodeCookPathUtf8(owner.StageLocator, owner.Work.OrphanLocator);
                Point(owner.Work, InitPoint::StageCreated, owner);
                if (fault == InitFault::StageOpen)
                {
                    return InitFail(error, "stage_open_injected");
                }
                owner.Stage.Value = CreateFileW(owner.StageLocator.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (owner.Stage.Value == INVALID_HANDLE_VALUE || fault == InitFault::StageIdentity ||
                    !ObserveHandle(owner.Stage.Value, true, owner.StageId, error) ||
                    !Direct(owner.WorkspaceId, owner.StageId) ||
                    owner.StageId.Canonical.filename().native() != owner.StageLocator.filename().native() ||
                    !std::any_of(owner.StageId.FileId.begin(), owner.StageId.FileId.end(),
                                 [](uint8_t b)
                                 {
                                     return b != 0;
                                 }))
                {
                    return InitFail(error, "stage_identity_unverified");
                }
                owner.bStageKnown = true;
                return true;
            }
            return InitFail(error, "stage_collision_limit");
        }
        void AddField(Text& out, const char* key, const Text& value)
        {
            out.push_back('"');
            out.append(key);
            out.append("\":\"");
            out.append(value);
            out.push_back('"');
        }
        Text MakeHeader(const InitOwner& owner, const Text& storeId, const Text& volumeGuid)
        {
            Text out = "{\"producer\":\"NorvesLib.AssetCook\",\"schema\":1,";
            AddField(out, "store_id", storeId);
            out.push_back(',');
            AddField(out, "volume_guid", volumeGuid);
            out.push_back(',');
            AddField(out, "volume_serial", HexNumber(owner.WorkspaceId.Volume));
            out.push_back(',');
            AddField(out, "workspace_id", HexBytes(owner.WorkspaceId.FileId.data(), 16));
            out.push_back(',');
            AddField(out, "store_directory_id", HexBytes(owner.StageId.FileId.data(), 16));
            out.push_back('}');
            return out;
        }
        bool MakeIndex(const Text& storeId, Text& out, Text& error)
        {
            CookManagedStoreIndex index;
            index.StoreId = storeId;
            return SerializeCookManagedStoreIndex(index, out, error);
        }
        bool WriteControl(InitOwner& owner, const wchar_t* leaf, const Text& text, Handle& file, bool& bKnown,
                          bool bHeader, InitFault fault, Bytes& verified, Text& error)
        {
            if (fault == (bHeader ? InitFault::HeaderCreate : InitFault::IndexCreate))
            {
                return InitFail(error, "control_create_injected");
            }
            const auto path = owner.StageId.Canonical / leaf;
            file.Value = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file.Value == INVALID_HANDLE_VALUE)
            {
                return InitFailWin32(error, "control_create_failed");
            }
            Identity identity;
            FILE_STANDARD_INFO standard{};
            if (!ObserveHandle(file.Value, false, identity, error) || !Direct(owner.StageId, identity) ||
                identity.Canonical.filename().native() != leaf ||
                !GetFileInformationByHandleEx(file.Value, FileStandardInfo, &standard, sizeof(standard)) ||
                standard.NumberOfLinks != 1)
            {
                return InitFail(error, "created_control_identity");
            }
            bKnown = true;
            size_t offset = 0;
            while (offset < text.size())
            {
                const bool bPartial = fault == (bHeader ? InitFault::HeaderPartialWrite : InitFault::IndexPartialWrite);
                DWORD wrote = 0;
                const size_t count = bPartial ? std::max(size_t{1}, text.size() / 2) : text.size() - offset;
                if (fault == InitFault::ZeroWrite ||
                    !WriteFile(file.Value, text.data() + offset, static_cast<DWORD>(count), &wrote, nullptr) || !wrote)
                {
                    return InitFail(error, "write_failed_or_zero");
                }
                offset += wrote;
                if (bPartial)
                {
                    return InitFail(error, "partial_write_injected");
                }
            }
            if (fault == InitFault::Flush || !FlushFileBuffers(file.Value))
            {
                return InitFail(error, "flush_failed");
            }
            LARGE_INTEGER zero{}, size{};
            if (fault == InitFault::Seek || !SetFilePointerEx(file.Value, zero, nullptr, FILE_BEGIN))
            {
                return InitFail(error, "seek_failed");
            }
            verified.resize(text.size());
            DWORD got = 0;
            uint8_t extra = 0;
            DWORD tail = 0;
            if (fault == InitFault::ReadBack ||
                !ReadFile(file.Value, verified.data(), static_cast<DWORD>(verified.size()), &got, nullptr) ||
                got != verified.size() || !ReadFile(file.Value, &extra, 1, &tail, nullptr) || tail != 0 ||
                !GetFileSizeEx(file.Value, &size) || size.QuadPart < 0 ||
                static_cast<uint64_t>(size.QuadPart) != text.size())
            {
                return InitFail(error, "readback_size_or_eof");
            }
            if (fault == InitFault::ByteMismatch)
            {
                verified[0] ^= 1;
            }
            if (std::memcmp(verified.data(), text.data(), text.size()) != 0)
            {
                return InitFail(error, "readback_mismatch");
            }
            Identity current;
            if (!ObserveHandle(file.Value, false, current, error) || !Same(current, identity) ||
                current.Canonical != identity.Canonical)
            {
                return InitFail(error, "control_changed");
            }
            return true;
        }
        bool OnlyOwnedChildren(InitOwner& owner, Text& error)
        {
            Entries entries;
            Budget budget;
            if (!Enumerate(owner.StageId, entries, budget, error))
            {
                return false;
            }
            const Entry *header = nullptr, *index = nullptr;
            return entries.size() == 2 && Fixed(entries, L"header.json", true, header, error) &&
                   Fixed(entries, L"roots.json", true, index, error);
        }
        bool RenameStage(InitOwner& owner)
        {
            // Windows runnerでrelative形式が拒否されたため、live workspaceからの絶対native名に統一する。
            const auto destination = owner.WorkspaceId.Canonical / StoreLeaf;
            const auto& target = destination.native();
            const size_t units = target.size();
            if (units > Detail::MaximumCookLocatorUnits)
            {
                SetLastError(ERROR_FILENAME_EXCED_RANGE);
                return false;
            }
            const size_t bytes = sizeof(FILE_RENAME_INFO) + (units + 1) * sizeof(wchar_t);
            Core::Container::VariableArray<std::max_align_t> storage((bytes + sizeof(std::max_align_t) - 1) /
                                                                     sizeof(std::max_align_t));
            std::memset(storage.data(), 0, storage.size() * sizeof(std::max_align_t));
            auto* info = ::new (static_cast<void*>(storage.data())) FILE_RENAME_INFO{};
            info->ReplaceIfExists = FALSE;
            info->RootDirectory = nullptr;
            info->FileNameLength = static_cast<DWORD>(units * sizeof(wchar_t));
            std::memcpy(reinterpret_cast<uint8_t*>(storage.data()) + offsetof(FILE_RENAME_INFO, FileName), target.data(),
                        units * sizeof(wchar_t));
            return SetFileInformationByHandle(owner.Stage.Value, FileRenameInfo, info, static_cast<DWORD>(bytes)) !=
                   FALSE;
        }
        InitResult InitializeLocked(const CookDestinationLockContext& lock, InitWork& work)
        {
            const InitFault fault = work.Probe ? work.Probe->Fault : InitFault::None;
            InitOwner owner(work);
            CookManagedStoreObservation first;
            const auto initial = RunObservation(lock, *work.Request, first, owner.WorkspaceId, work.Reason);
            if (initial != Result::StoreMissing)
            {
                return BeforeResult(initial);
            }
            if (first.Owner.bFinalRuntimeRootPresent)
            {
                InitFail(work.Reason, "absent_root_required");
                return InitResult::Conflict;
            }
            const auto parent = first.Owner.FinalRuntimeRootLocator.parent_path();
            owner.Destination = parent / StoreLeaf;
            owner.Workspace.Value =
                CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            Identity held;
            if (owner.Workspace.Value == INVALID_HANDLE_VALUE ||
                !ObserveHandle(owner.Workspace.Value, true, held, work.Reason) || !Same(held, owner.WorkspaceId) ||
                held.Canonical != owner.WorkspaceId.Canonical)
            {
                InitFail(work.Reason, "workspace_changed");
                return InitResult::Error;
            }
            Text storeId;
            if (!RandomToken(storeId, fault, work.Reason) || !CreateStage(owner, fault, work.Reason) ||
                !RecheckMissing(lock, owner, first, work.Reason))
            {
                return InitResult::Error;
            }
            if (fault == InitFault::CleanupDisposition)
            {
                InitFail(work.Reason, "cleanup_disposition_injected");
                return InitResult::Error;
            }
            Bytes headerBytes, indexBytes;
            if (!WriteControl(owner, L"header.json", MakeHeader(owner, storeId, lock.CanonicalVolumeGuid),
                              owner.HeaderFile, owner.bHeaderKnown, true, fault, headerBytes, work.Reason))
            {
                return InitResult::Error;
            }
            Point(work, InitPoint::HeaderWritten, owner);
            Text indexJson;
            if (!MakeIndex(storeId, indexJson, work.Reason) ||
                !WriteControl(owner, L"roots.json", indexJson, owner.IndexFile, owner.bIndexKnown, false, fault,
                              indexBytes, work.Reason))
            {
                return InitResult::Error;
            }
            Point(work, InitPoint::IndexWritten, owner);
            CookManagedStoreView parsed;
            Budget budget;
            if (fault == InitFault::Parse ||
                !Header(headerBytes, owner.WorkspaceId, owner.StageId, lock.CanonicalVolumeGuid, parsed) ||
                !Index(indexBytes, parsed, budget) || parsed.StoreId != storeId || parsed.IndexGeneration != 1 ||
                !parsed.Roots.empty() || !OnlyOwnedChildren(owner, work.Reason))
            {
                InitFail(work.Reason, "stage_semantics_or_unknown_child");
                return InitResult::Error;
            }
            const bool bHeaderClosed = owner.HeaderFile.Close(), bIndexClosed = owner.IndexFile.Close();
            if (!bHeaderClosed || !bIndexClosed || fault == InitFault::ChildClose)
            {
                InitFail(work.Reason, "child_close_failed");
                return InitResult::Error;
            }
            Point(work, InitPoint::ChildrenClosed, owner);
            if (!RecheckMissing(lock, owner, first, work.Reason))
            {
                return InitResult::Error;
            }
            Point(work, InitPoint::BeforeRename, owner);
            if (fault == InitFault::Rename)
            {
                InitFail(work.Reason, "rename_injected");
                return InitResult::Error;
            }
            if (!RenameStage(owner))
            {
                InitFailWin32(work.Reason, "publish_no_replace_failed");
                return InitResult::Error;
            }
            work.bPublished = true;
            Point(work, InitPoint::Renamed, owner);
            Identity published;
            if (fault == InitFault::AfterPublishIdentity ||
                !ObserveHandle(owner.Stage.Value, true, published, work.Reason) || !Same(published, owner.StageId) ||
                !Direct(owner.WorkspaceId, published) || published.Canonical.filename().native() != StoreLeaf)
            {
                InitFail(work.Reason, "published_identity_failed");
                return InitResult::PublishedButError;
            }
            // DELETE handleが残ると既存observerの共有規約と衝突する。検証後に閉じてから再観測する。
            if (!owner.Stage.Close() || fault == InitFault::AfterPublishClose)
            {
                InitFail(work.Reason, "published_close_failed");
                return InitResult::PublishedButError;
            }
            CookManagedStoreObservation after;
            Identity afterWorkspace;
            if (fault == InitFault::AfterPublishObservation ||
                RunObservation(lock, *work.Request, after, afterWorkspace, work.Reason) != Result::Observed ||
                !OwnerEqual(first.Owner, after.Owner) || !Same(afterWorkspace, owner.WorkspaceId) ||
                afterWorkspace.Canonical != owner.WorkspaceId.Canonical)
            {
                InitFail(work.Reason, "published_observation_failed");
                return InitResult::PublishedButError;
            }
            const CookManagedStoreView* current = nullptr;
            for (const auto& view : after.Stores)
            {
                if (view.bCurrentWorkspace)
                {
                    current = &view;
                    break;
                }
            }
            if (!current || current->StoreId != storeId || current->VolumeSerial != owner.StageId.Volume ||
                !SameId(current->StoreDirectoryId, owner.StageId.FileId) ||
                !SameId(current->WorkspaceId, owner.WorkspaceId.FileId) || current->IndexGeneration != 1 ||
                !current->Roots.empty())
            {
                InitFail(work.Reason, "published_store_mismatch");
                return InitResult::PublishedButError;
            }
            work.Candidate = std::move(after);
            work.Reason.clear();
            return InitResult::Created;
        }
        bool InitializeCallback(const CookDestinationLockContext& lock, void* data, Text& error)
        {
            (void)error;
            auto& work = *static_cast<InitWork*>(data);
            work.Status = InitializeLocked(lock, work);
            return true;
        }
#endif
        InitResult InitializeImpl(const CookOwnerResolveRequest& request, const Detail::CookStoreInitProbe* probe,
                                  CookManagedStoreObservation& out, Text& error)
        {
            error.clear();
#if !defined(_WIN32)
            (void)request;
            (void)probe;
            (void)out;
            InitFail(error, "windows_required");
            return InitResult::Error;
#else
            InitWork work;
            work.Request = &request;
            work.Probe = probe;
            const auto lockFault = probe && probe->Fault == InitFault::MutexRelease ? Detail::CookLockFault::Release
                                                                                    : Detail::CookLockFault::None;
            const auto locked = Detail::WithCookDestinationLockForTest({request.FinalRuntimeRoot}, InitializeCallback,
                                                                       &work, error, lockFault);
            if (locked == CookDestinationLockResult::Busy)
            {
                InitFail(error, "volume_busy");
                return InitResult::Busy;
            }
            InitResult result = work.Status;
            if (locked != CookDestinationLockResult::Executed || work.bCloseFailed)
            {
                result = work.bPublished ? InitResult::PublishedButError : InitResult::Error;
            }
            if (result != InitResult::Created)
            {
                if (!work.Reason.empty())
                {
                    if (!error.empty())
                    {
                        error.append("; ");
                    }
                    error.append(work.Reason);
                }
                if (error.empty())
                {
                    InitFail(error, result == InitResult::StoreExists ? "store_exists" : "operation_failed");
                }
                if (work.bCloseFailed)
                {
                    error.append("; handle_close_failed");
                }
                if (work.bCleanupIncomplete)
                {
                    error.append("; orphan_preserved=");
                    error.append(work.OrphanLocator);
                }
                if (work.bPublished)
                {
                    error.append("; published_store_preserved");
                }
                return result;
            }
            out = std::move(work.Candidate);
            error.clear();
            return result;
#endif
        }
    } // namespace
    CookManagedStoreInitializationResult InitializeNewCookManagedStore(const CookOwnerResolveRequest& request,
                                                                       CookManagedStoreObservation& out, Text& error)
    {
        return InitializeImpl(request, nullptr, out, error);
    }
    CookManagedStoreInitializationResult Detail::InitializeNewCookManagedStoreForTest(
        const CookOwnerResolveRequest& request, const CookStoreInitProbe& probe, CookManagedStoreObservation& out,
        Text& error)
    {
        return InitializeImpl(request, &probe, out, error);
    }
} // namespace NorvesLib::Tools::AssetCook
