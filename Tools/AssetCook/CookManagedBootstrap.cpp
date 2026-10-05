// texture v1の新規runtimeを、既知pending/receiptと条件付き復旧で公開する。
#include "CookManagedBootstrap.h"
#include "CookManagedBootstrapTestAccess.h"
#include "CookManagedStoreAccess.h"
#include "CookManagedStoreNative.h"
#include "CookManagedTransactionIntent.h"
#include "CookOutputSetGuard.h"
#include "CookReferenceValues.h"
#include "TextureAssetSetOutput.h"
#include "Text/JsonDocument.h"
#include "Text/UnicodeText.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
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
        template <class T> using Array = Core::Container::VariableArray<T>;
        using ByteView = Core::Container::Span<const uint8_t>;
        using BootResult = CookManagedBootstrapResult;
        using RecoveryResult = CookManagedRecoveryResult;
        using Point = Detail::CookManagedBootstrapPoint;
        using Probe = Detail::CookManagedBootstrapProbe;
        bool Fail(Text& error, const char* code)
        {
            error = "cook_managed_bootstrap: ";
            error.append(code);
            return false;
        }
#if defined(_WIN32)
        bool WinFail(Text& error, const char* reason)
        {
            const DWORD code = GetLastError();
            Fail(error, reason);
            error.append("; win32_hex=");
            char number[16];
            const auto end = std::to_chars(number, number + sizeof(number), code, 16);
            error.append(number, static_cast<size_t>(end.ptr - number));
            return false;
        }
        namespace N = Detail::ManagedStoreNative;
        namespace P = Detail::CookOutputPaths;
        using N::Handle;
        using N::Identity;
        constexpr size_t MaximumPackageBytes = 512ull * 1024 * 1024;
        struct File
        {
            Identity Native;
            CookManagedFileImage Image;
            Bytes Data;
        };
        CookManagedObjectId Object(const Identity& id)
        {
            CookManagedObjectId out;
            out.Volume = id.Volume;
            out.File = id.FileId;
            return out;
        }
        bool Same(const CookManagedObjectId& a, const CookManagedObjectId& b)
        {
            return a.Volume == b.Volume && std::memcmp(a.File.data(), b.File.data(), 16) == 0;
        }
        bool Same(const CookManagedFileImage& a, const CookManagedFileImage& b)
        {
            return Same(a.Object, b.Object) && a.Size == b.Size && a.ContentHash == b.ContentHash;
        }
        bool Same(const Identity& actual, const CookManagedObjectId& expected)
        {
            return actual.Volume == expected.Volume && std::memcmp(actual.FileId.data(), expected.File.data(), 16) == 0;
        }
        std::filesystem::path Leaf(View text)
        {
            Text owned;
            owned.append(text);
            return std::filesystem::path(owned.c_str());
        }
        ByteView BytesOf(const Text& text)
        {
            return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
        }
        bool EqualBytes(ByteView a, ByteView b)
        {
            return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
        }
        bool RecheckDirectory(const Identity& expected, Text& error)
        {
            Identity actual;
            return (N::Directory(expected.Canonical, actual, error) && N::Same(actual, expected) &&
                    actual.Canonical == expected.Canonical) ||
                   Fail(error, "directory_changed");
        }
        bool Find(const Identity& parent, const std::filesystem::path& leaf, const N::Entry*& found,
                  N::Entries& entries, Text& error)
        {
            N::Budget budget;
            return RecheckDirectory(parent, error) && N::Enumerate(parent, entries, budget, error) &&
                   N::Fixed(entries, leaf.c_str(), false, found, error);
        }
        bool Absent(const Identity& parent, const std::filesystem::path& leaf, Text& error)
        {
            N::Entries entries;
            const N::Entry* found = nullptr;
            return Find(parent, leaf, found, entries, error) && (!found || Fail(error, "destination_exists"));
        }
        bool ReadHandle(HANDLE handle, const Identity& parent, const std::filesystem::path& leaf, size_t limit,
                        bool bExactName, bool bSingleLink, File& out, Text& error)
        {
            Identity id, after;
            FILE_STANDARD_INFO standard{};
            LARGE_INTEGER size{}, zero{};
            if (!N::ObserveHandle(handle, false, id, error) || !N::Direct(parent, id) ||
                (bExactName && id.Canonical.filename().native() != leaf.native()) ||
                !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
                (bSingleLink && standard.NumberOfLinks != 1) || !GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
                static_cast<uint64_t>(size.QuadPart) > limit || !SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN))
            {
                return Fail(error, "file_scope_type_size_or_seek");
            }
            Bytes bytes;
            bytes.resize(static_cast<size_t>(size.QuadPart));
            size_t offset = 0;
            while (offset < bytes.size())
            {
                DWORD got = 0;
                const auto count = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
                if (!ReadFile(handle, bytes.data() + offset, count, &got, nullptr) || !got)
                {
                    return WinFail(error, "file_read");
                }
                offset += got;
            }
            uint8_t extra = 0;
            DWORD got = 0;
            LARGE_INTEGER finalSize{};
            if (!ReadFile(handle, &extra, 1, &got, nullptr) || got || !GetFileSizeEx(handle, &finalSize) ||
                finalSize.QuadPart != size.QuadPart || !N::ObserveHandle(handle, false, after, error) ||
                !N::Same(id, after) || id.Canonical != after.Canonical)
            {
                return Fail(error, "file_changed");
            }
            out.Native = std::move(id);
            out.Image.Object = Object(out.Native);
            out.Image.Size = bytes.size();
            out.Image.ContentHash = Core::Asset::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size());
            out.Data = std::move(bytes);
            return true;
        }
        bool ReadChild(const Identity& parent, const std::filesystem::path& leaf, size_t limit, File& out, Text& error,
                       bool bEnumerate = true)
        {
            N::Entries entries;
            const N::Entry* found = nullptr;
            if (bEnumerate && (!Find(parent, leaf, found, entries, error) || !found))
            {
                return Fail(error, "fixed_file_missing");
            }
            Handle h;
            h.Value = CreateFileW((parent.Canonical / leaf).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h.Value == INVALID_HANDLE_VALUE)
            {
                return WinFail(error, "fixed_file_open");
            }
            return ReadHandle(h.Value, parent, leaf, limit, true, true, out, error) &&
                   (h.Close() || Fail(error, "file_close"));
        }
        bool NewFile(const Identity& parent, const std::filesystem::path& leaf, ByteView bytes, File& out, Text& error)
        {
            if (bytes.size() > MaximumCookManagedIntentBytes || (!bytes.empty() && !bytes.data()) ||
                !Absent(parent, leaf, error))
            {
                return Fail(error, "new_file_scope_or_size");
            }
            Handle h;
            h.Value = CreateFileW((parent.Canonical / leaf).c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
                                  CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h.Value == INVALID_HANDLE_VALUE)
            {
                return WinFail(error, "new_file_create");
            }
            size_t offset = 0;
            while (offset < bytes.size())
            {
                DWORD written = 0;
                const auto count = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
                if (!WriteFile(h.Value, bytes.data() + offset, count, &written, nullptr) || !written)
                {
                    return WinFail(error, "new_file_write");
                }
                offset += written;
            }
            if (!FlushFileBuffers(h.Value) ||
                !ReadHandle(h.Value, parent, leaf, MaximumCookManagedIntentBytes, true, true, out, error) ||
                !EqualBytes(bytes, out.Data))
            {
                return Fail(error, "new_file_flush_or_readback");
            }
            return h.Close() || Fail(error, "new_file_close");
        }
        bool NewDirectory(const Identity& parent, const std::filesystem::path& leaf, Identity& out, Text& error)
        {
            if (!Absent(parent, leaf, error))
            {
                return false;
            }
            const auto path = parent.Canonical / leaf;
            if (!CreateDirectoryW(path.c_str(), nullptr))
            {
                return WinFail(error, "directory_create");
            }
            Handle h;
            h.Value =
                CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            // 作成直後のID取得失敗では名前だけで消さずorphanを保持する。
            if (h.Value == INVALID_HANDLE_VALUE || !N::ObserveHandle(h.Value, true, out, error) ||
                !N::Direct(parent, out) || out.Canonical.filename().native() != leaf.native())
            {
                return Fail(error, "new_directory_identity");
            }
            return h.Close() || Fail(error, "new_directory_close");
        }
        bool Rename(const Identity& from, const std::filesystem::path& oldLeaf, const CookManagedObjectId& object,
                    const Identity& to, const std::filesystem::path& newLeaf, bool bDirectory,
                    const CookManagedFileImage* file, Text& error, bool* bMoved = nullptr)
        {
            if (bMoved)
            {
                *bMoved = false;
            }
            if (!RecheckDirectory(from, error) || !RecheckDirectory(to, error) || from.Volume != to.Volume ||
                !Absent(to, newLeaf, error))
            {
                return false;
            }
            N::Entries entries;
            const N::Entry* source = nullptr;
            if (!Find(from, oldLeaf, source, entries, error) || !source)
            {
                return Fail(error, "rename_source_missing");
            }
            Handle h;
            h.Value = CreateFileW(
                (from.Canonical / oldLeaf).c_str(), DELETE | FILE_READ_ATTRIBUTES | (bDirectory ? 0 : GENERIC_READ),
                bDirectory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | (bDirectory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
            Identity observed;
            if (h.Value == INVALID_HANDLE_VALUE || !N::ObserveHandle(h.Value, bDirectory, observed, error) ||
                !Same(observed, object) || !N::Direct(from, observed) ||
                observed.Canonical.filename().native() != oldLeaf.native())
            {
                return Fail(error, "rename_source_identity");
            }
            if (!bDirectory)
            {
                File check;
                if (!file || !ReadHandle(h.Value, from, oldLeaf, MaximumPackageBytes, true, true, check, error) ||
                    !Same(check.Image, *file))
                {
                    return Fail(error, "rename_source_image");
                }
            }
            const auto target = to.Canonical / newLeaf;
            const auto& native = target.native();
            if (native.size() > Detail::MaximumCookLocatorUnits)
            {
                return Fail(error, "rename_target_limit");
            }
            const size_t bytes = sizeof(FILE_RENAME_INFO) + (native.size() + 1) * sizeof(wchar_t);
            Array<std::max_align_t> buffer((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
            std::memset(buffer.data(), 0, buffer.size() * sizeof(std::max_align_t));
            auto* info = ::new (static_cast<void*>(buffer.data())) FILE_RENAME_INFO{};
            info->ReplaceIfExists = FALSE;
            info->RootDirectory = nullptr;
            info->FileNameLength = static_cast<DWORD>(native.size() * sizeof(wchar_t));
            std::memcpy(reinterpret_cast<uint8_t*>(buffer.data()) + offsetof(FILE_RENAME_INFO, FileName), native.data(),
                        info->FileNameLength);
            if (!SetFileInformationByHandle(h.Value, FileRenameInfo, info, static_cast<DWORD>(bytes)))
            {
                return WinFail(error, "rename_no_replace");
            }
            if (bMoved)
            {
                *bMoved = true;
            }
            Identity after;
            if (!N::ObserveHandle(h.Value, bDirectory, after, error) || !Same(after, object) || !N::Direct(to, after) ||
                after.Canonical.filename().native() != newLeaf.native())
            {
                return Fail(error, "rename_published_identity");
            }
            return h.Close() || Fail(error, "rename_published_close");
        }
        bool Token(Text& out, Text& error)
        {
            uint8_t bytes[16]{};
            if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            {
                return Fail(error, "rng");
            }
            bool bAny = false;
            Text value;
            constexpr char hex[] = "0123456789abcdef";
            for (uint8_t b : bytes)
            {
                bAny |= b != 0;
                value.push_back(hex[b >> 4]);
                value.push_back(hex[b & 15]);
            }
            if (!bAny)
            {
                return Fail(error, "zero_rng");
            }
            out = std::move(value);
            return true;
        }
        bool LoadManifest(ByteView bytes, Core::Asset::AssetManifest& out, Text& error)
        {
            Core::Container::String text;
            if (bytes.empty() || bytes.size() > MaximumCookStateBytes ||
                !Core::TextDetail::ForEachUnicodeScalar<char>(
                    {reinterpret_cast<const char*>(bytes.data()), bytes.size()},
                    [&](uint32_t scalar)
                    {
                        if constexpr (sizeof(wchar_t) == 2)
                        {
                            if (scalar > 0xffff)
                            {
                                scalar -= 0x10000;
                                text.push_back(static_cast<wchar_t>(0xd800 + (scalar >> 10)));
                                text.push_back(static_cast<wchar_t>(0xdc00 + (scalar & 1023)));
                                return;
                            }
                        }
                        text.push_back(static_cast<wchar_t>(scalar));
                    }) ||
                !out.LoadFromJsonText(text))
            {
                return Fail(error, "manifest_read");
            }
            return true;
        }
        struct StoreScope
        {
            Identity Workspace, Store, Pending;
            File Header;
            Text StoreId;
            Array<Identity> Ancestors;
            size_t AncestorRoots = 0;
            bool bPending = false;
        };
        bool VerifyClaims(const Identity& workspace, const Identity& store, const CookManagedStoreIndex& index,
                          const Array<Identity>& chain, Text& error)
        {
            N::Entries entries;
            N::Budget budget;
            if (!N::Enumerate(workspace, entries, budget, error))
            {
                return false;
            }
            struct Name
            {
                const std::filesystem::path* Value = nullptr;
                const N::Entry* Entry = nullptr;
            };
            Array<Name> names;
            for (const auto& entry : entries)
            {
                names.push_back({&entry.Name, &entry});
                if (!entry.ShortName.empty() && !N::Fold(entry.ShortName, entry.Name))
                {
                    names.push_back({&entry.ShortName, &entry});
                }
            }
            const auto compare = [](const std::filesystem::path& a, const std::filesystem::path& b)
            {
                const int value = CompareStringOrdinal(a.c_str(), static_cast<int>(a.native().size()), b.c_str(),
                                                       static_cast<int>(b.native().size()), TRUE);
                if (!value)
                {
                    throw std::runtime_error("claim_name_comparison_failed");
                }
                return value - CSTR_EQUAL;
            };
            std::sort(names.begin(), names.end(),
                      [&](const Name& a, const Name& b)
                      {
                          return compare(*a.Value, *b.Value) < 0;
                      });
            for (const auto& claim : index.Roots)
            {
                const auto leaf = Leaf(claim.RootLeaf);
                const auto found = std::lower_bound(names.begin(), names.end(), leaf,
                                                    [&](const Name& row, const std::filesystem::path& wanted)
                                                    {
                                                        return compare(*row.Value, wanted) < 0;
                                                    });
                if (found == names.end() || compare(*found->Value, leaf) != 0 ||
                    found->Entry->Name.native() != leaf.native() ||
                    (found + 1 != names.end() && compare(*(found + 1)->Value, leaf) == 0))
                {
                    return Fail(error, "active_claim_name_or_alias");
                }
                const auto* entry = found->Entry;
                Identity actual;
                if (!N::Directory(workspace.Canonical / entry->Name, actual, error) || !N::Direct(workspace, actual) ||
                    actual.Canonical.filename().native() != entry->Name.native() ||
                    !N::SameId(actual.FileId, claim.DirectoryId) || N::Same(actual, store))
                {
                    return Fail(error, "active_claim_changed");
                }
                for (const auto& ancestor : chain)
                {
                    if (N::Same(actual, ancestor))
                    {
                        return Fail(error, "nested_managed_root");
                    }
                }
            }
            return true;
        }
        bool OpenStore(const CookDestinationLockContext& lock, StoreScope& out, Text& error)
        {
            StoreScope scope;
            if (!N::Directory(lock.FinalRuntimeRootLocator.parent_path(), scope.Workspace, error) ||
                scope.Workspace.Canonical.native().size() == N::VolumeUnits)
            {
                return Fail(error, "workspace_scope");
            }
            Identity current = scope.Workspace;
            while (true)
            {
                if (scope.Ancestors.size() == Detail::MaximumCookLocatorComponents)
                {
                    return Fail(error, "ancestor_limit");
                }
                scope.Ancestors.push_back(current);
                if (current.Canonical.native().size() == N::VolumeUnits)
                {
                    break;
                }
                const auto parent = N::Parent(current.Canonical);
                if (!N::Directory(parent, current, error) || current.Canonical != parent ||
                    current.Volume != scope.Workspace.Volume)
                {
                    return Fail(error, "ancestor_changed");
                }
            }
            bool bFound = false;
            size_t roots = 0;
            for (size_t level = 0; level < scope.Ancestors.size(); ++level)
            {
                const auto& workspace = scope.Ancestors[level];
                N::Entries entries;
                N::Budget budget;
                const N::Entry* entry = nullptr;
                if (!N::Enumerate(workspace, entries, budget, error) ||
                    !N::Fixed(entries, N::StoreLeaf, false, entry, error))
                {
                    return false;
                }
                if (!entry)
                {
                    continue;
                }
                if (workspace.Canonical.native().size() == N::VolumeUnits)
                {
                    return Fail(error, "volume_root_store");
                }
                Identity store;
                if (!N::Directory(workspace.Canonical / entry->Name, store, error) || !N::Direct(workspace, store))
                {
                    return false;
                }
                for (const auto& ancestor : scope.Ancestors)
                {
                    if (N::Same(ancestor, store))
                    {
                        return Fail(error, "inside_control_store");
                    }
                }
                File header;
                CookManagedStoreView view;
                if (!ReadChild(store, L"header.json", 16384, header, error) ||
                    !N::Header(header.Data, workspace, store, lock.CanonicalVolumeGuid, view))
                {
                    return Fail(error, "store_header");
                }
                N::Entries controls;
                const N::Entry* pending = nullptr;
                if (!N::Enumerate(store, controls, budget, error) ||
                    !N::Fixed(controls, L"pending", false, pending, error))
                {
                    return false;
                }
                if (level && pending)
                {
                    return Fail(error, "ancestor_pending");
                }
                if (level == 0)
                {
                    bFound = true;
                    scope.Store = store;
                    scope.StoreId = view.StoreId;
                    scope.Header = std::move(header);
                    scope.bPending = pending != nullptr;
                    if (pending && (!N::Directory(store.Canonical / pending->Name, scope.Pending, error) ||
                                    !N::Direct(store, scope.Pending)))
                    {
                        return false;
                    }
                }
                if (!pending)
                {
                    File file;
                    CookManagedStoreIndex index;
                    if (!ReadChild(store, L"roots.json", MaximumCookStoreIndexBytes, file, error) ||
                        !ParseCookManagedStoreIndex(file.Data, view.StoreId, MaximumCookStoreRoots - roots, index,
                                                    error) ||
                        !VerifyClaims(workspace, store, index, scope.Ancestors, error))
                    {
                        return false;
                    }
                    roots += index.Roots.size();
                    if (level)
                    {
                        scope.AncestorRoots += index.Roots.size();
                    }
                }
            }
            if (!bFound)
            {
                return Fail(error, "initialized_store_required");
            }
            out = std::move(scope);
            return true;
        }
        bool ReadSpec(const CookResolvedOwnerBinding& owner, File& out, Text& error)
        {
            Identity parent;
            if (!N::Directory(owner.SpecLocator.parent_path(), parent, error))
            {
                return false;
            }
            Handle h;
            h.Value = CreateFileW(owner.SpecLocator.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h.Value == INVALID_HANDLE_VALUE)
            {
                return WinFail(error, "spec_open");
            }
            return ReadHandle(h.Value, parent, owner.SpecLocator.filename(), MaximumCookStateBytes, false, false, out,
                              error) &&
                   (h.Close() || Fail(error, "spec_close"));
        }
        struct TreeNode
        {
            Text Relative;
            Identity Native, Parent;
            CookManagedFileImage Image;
            bool bDirectory = false;
        };
        struct ExpectedNode
        {
            View Relative;
            CookManagedObjectId Object, Parent;
            CookManagedFileImage Image;
            bool bDirectory = false;
        };
        int Compare(View a, View b)
        {
            const auto count = std::min(a.size(), b.size());
            const int result = count ? std::memcmp(a.data(), b.data(), count) : 0;
            return result ? result : (a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1);
        }
        bool ScanTree(const Identity& root, Array<TreeNode>& out, const CookManagedIntentDraft* expected, Text& error)
        {
            Array<ExpectedNode> wanted;
            if (expected)
            {
                if (!Same(root, expected->Root))
                {
                    return Fail(error, "tree_root_identity");
                }
                for (const auto& dir : expected->Directories)
                {
                    wanted.push_back({dir.Relative, dir.Object, dir.Parent, {}, true});
                }
                for (const auto& file : expected->Packages)
                {
                    if (file.After.Size > MaximumPackageBytes)
                    {
                        return Fail(error, "package_limit");
                    }
                    wanted.push_back({file.Package, file.After.Object, file.Before.Parent, file.After, false});
                }
                wanted.push_back({expected->Anchor.Binding.ManifestName, expected->ManifestAfter.Object, expected->Root,
                                  expected->ManifestAfter, false});
                std::sort(wanted.begin(), wanted.end(),
                          [](const auto& a, const auto& b)
                          {
                              return Compare(a.Relative, b.Relative) < 0;
                          });
            }
            struct DirectoryRow
            {
                Identity Native;
                Text Relative;
                size_t Depth = 0;
            };
            Array<DirectoryRow> stack;
            stack.push_back({root, {}, 0});
            Array<TreeNode> nodes;
            N::Budget budget;
            size_t metadata = 0;
            while (!stack.empty())
            {
                auto parent = std::move(stack.back());
                stack.pop_back();
                N::Entries entries;
                if (!RecheckDirectory(parent.Native, error) || !N::Enumerate(parent.Native, entries, budget, error))
                {
                    return false;
                }
                for (const auto& entry : entries)
                {
                    Text leaf;
                    if (!P::AsciiPath(entry.Name, leaf) || !P::SafeOutputName(leaf) || std::strchr(leaf.c_str(), '/'))
                    {
                        return Fail(error, "tree_name");
                    }
                    TreeNode node;
                    node.Relative = parent.Relative;
                    if (!node.Relative.empty())
                    {
                        node.Relative.push_back('/');
                    }
                    node.Relative.append(leaf);
                    node.Parent = parent.Native;
                    if (node.Relative.size() > MaximumCookStateStringBytes ||
                        nodes.size() == MaximumCookManagedTreeEntries ||
                        (metadata += sizeof(TreeNode) + node.Relative.size()) > MaximumCookManagedIntentBytes)
                    {
                        return Fail(error, "tree_limit");
                    }
                    const ExpectedNode* match = nullptr;
                    if (expected)
                    {
                        auto it = std::lower_bound(wanted.begin(), wanted.end(), View(node.Relative),
                                                   [](const auto& row, View name)
                                                   {
                                                       return Compare(row.Relative, name) < 0;
                                                   });
                        if (it == wanted.end() || Compare(it->Relative, node.Relative))
                        {
                            return Fail(error, "unknown_tree_entry");
                        }
                        match = &*it;
                    }
                    const auto path = parent.Native.Canonical / entry.Name;
                    const auto attributes = GetFileAttributesW(path.c_str());
                    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    {
                        return Fail(error, "tree_type");
                    }
                    node.bDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    if (match && match->bDirectory != node.bDirectory)
                    {
                        return Fail(error, "tree_type_changed");
                    }
                    if (node.bDirectory)
                    {
                        if (parent.Depth + 1 >= Detail::MaximumCookLocatorComponents ||
                            !N::Directory(path, node.Native, error) || !N::Direct(parent.Native, node.Native) ||
                            node.Native.Canonical.filename().native() != entry.Name.native())
                        {
                            return Fail(error, "tree_directory_identity");
                        }
                        stack.push_back({node.Native, node.Relative, parent.Depth + 1});
                    }
                    else
                    {
                        File file;
                        if (!ReadChild(parent.Native, entry.Name,
                                       match ? static_cast<size_t>(match->Image.Size) : MaximumPackageBytes, file,
                                       error, false))
                        {
                            return false;
                        }
                        node.Native = std::move(file.Native);
                        node.Image = file.Image;
                        if (match && !Same(node.Image, match->Image))
                        {
                            return Fail(error, "tree_file_image");
                        }
                    }
                    if (match && (!Same(node.Native, match->Object) || !Same(node.Parent, match->Parent)))
                    {
                        return Fail(error, "tree_object_or_parent_changed");
                    }
                    nodes.push_back(std::move(node));
                }
            }
            if (expected && nodes.size() != wanted.size())
            {
                return Fail(error, "tree_entry_missing");
            }
            if (!RecheckDirectory(root, error))
            {
                return false;
            }
            out = std::move(nodes);
            return true;
        }
        bool EnsureParents(const Identity& root, View relative, Identity& parent, Text& error)
        {
            if (!P::SafeOutputName(relative) || relative.size() > MaximumCookStateStringBytes)
            {
                return Fail(error, "relative_package");
            }
            Identity current = root;
            size_t start = 0;
            for (size_t i = 0; i < relative.size(); ++i)
            {
                if (relative[i] != '/')
                {
                    continue;
                }
                const auto leaf = Leaf({relative.data() + start, i - start});
                N::Entries entries;
                const N::Entry* existing = nullptr;
                if (!Find(current, leaf, existing, entries, error))
                {
                    return false;
                }
                Identity next;
                if (existing)
                {
                    if (!N::Directory(current.Canonical / leaf, next, error) || !N::Direct(current, next) ||
                        next.Canonical.filename().native() != leaf.native())
                    {
                        return false;
                    }
                }
                else if (!NewDirectory(current, leaf, next, error))
                {
                    return false;
                }
                current = std::move(next);
                start = i + 1;
            }
            parent = std::move(current);
            return true;
        }
        bool DeleteKnown(const Identity& parent, const std::filesystem::path& leaf, const CookManagedObjectId& object,
                         bool bDirectory, const CookManagedFileImage* file, Text& error)
        {
            if (!RecheckDirectory(parent, error))
            {
                return false;
            }
            Handle h;
            h.Value = CreateFileW(
                (parent.Canonical / leaf).c_str(), DELETE | FILE_READ_ATTRIBUTES | (bDirectory ? 0 : GENERIC_READ),
                bDirectory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | (bDirectory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
            Identity actual;
            if (h.Value == INVALID_HANDLE_VALUE || !N::ObserveHandle(h.Value, bDirectory, actual, error) ||
                !Same(actual, object) || !N::Direct(parent, actual) ||
                actual.Canonical.filename().native() != leaf.native())
            {
                return Fail(error, "cleanup_identity");
            }
            if (!bDirectory)
            {
                File check;
                if (!file || !ReadHandle(h.Value, parent, leaf, MaximumPackageBytes, true, true, check, error) ||
                    !Same(check.Image, *file))
                {
                    return Fail(error, "cleanup_image");
                }
            }
            // directoryの非空はOSが拒否する。未知childの探索削除はしない。
            FILE_DISPOSITION_INFO disposition{};
            disposition.DeleteFile = TRUE;
            return (SetFileInformationByHandle(h.Value, FileDispositionInfo, &disposition, sizeof(disposition)) !=
                        FALSE &&
                    h.Close()) ||
                   Fail(error, "cleanup_disposition");
        }
        struct OptionalFile
        {
            bool bPresent = false;
            File Value;
        };
        bool ReadOptional(const Identity& parent, const std::filesystem::path& leaf, size_t limit, OptionalFile& out,
                          Text& error)
        {
            N::Entries entries;
            const N::Entry* entry = nullptr;
            if (!Find(parent, leaf, entry, entries, error))
            {
                return false;
            }
            if (!entry)
            {
                return true;
            }
            out.bPresent = true;
            return ReadChild(parent, leaf, limit, out.Value, error);
        }
        bool KnownDirectory(const Identity& parent, const std::filesystem::path& leaf,
                            const CookManagedObjectId& expected, bool& bPresent, Identity& out, Text& error)
        {
            N::Entries entries;
            const N::Entry* entry = nullptr;
            if (!Find(parent, leaf, entry, entries, error))
            {
                return false;
            }
            bPresent = entry != nullptr;
            if (!bPresent)
            {
                return true;
            }
            return (N::Directory(parent.Canonical / leaf, out, error) && N::Direct(parent, out) &&
                    Same(out, expected) && out.Canonical.filename().native() == leaf.native()) ||
                   Fail(error, "root_candidate_changed");
        }
        std::filesystem::path StateLeaf(View claim)
        {
            Text leaf = "state-";
            leaf.append(claim);
            leaf.append(".json");
            return Leaf(leaf);
        }
        struct ActiveTransaction
        {
            CookManagedTransactionIntent Intent;
            File IntentFile, Manifest, Receipt;
            Core::Container::FixedArray<File, 4> Controls;
            Identity Root;
            bool bRootFinal = false, bStateFinal = false, bIndexBeforeFinal = false, bIndexAfterFinal = false,
                 bCommitted = false;
        };
        CookManagedControlDocuments Documents(ActiveTransaction& tx)
        {
            CookManagedControlDocuments docs;
            for (size_t i = 0; i < 4; ++i)
            {
                docs.Controls[i] = {tx.Controls[i].Image.Object, tx.Controls[i].Data};
            }
            docs.ManifestAfter = {tx.Manifest.Image.Object, tx.Manifest.Data};
            return docs;
        }
        bool OnlyKnownPending(const Identity& pending, Text& error)
        {
            N::Entries entries;
            N::Budget budget;
            if (!N::Enumerate(pending, entries, budget, error))
            {
                return false;
            }
            constexpr const wchar_t* names[] = {L"intent.json", L"index.before",  L"index.after",   L"state.after",
                                                L"root.stage",  L"receipt.stage", L"receipt.commit"};
            for (const auto& entry : entries)
            {
                bool bKnown = false;
                for (const auto* name : names)
                {
                    bKnown |= entry.Name.native() == name;
                }
                if (!bKnown)
                {
                    return Fail(error, "unknown_pending_entry");
                }
            }
            for (const auto* name : names)
            {
                const N::Entry* entry = nullptr;
                if (!N::Fixed(entries, name, false, entry, error))
                {
                    return false;
                }
            }
            return true;
        }
        bool LoadActive(const StoreScope& scope, ActiveTransaction& tx, Text& error)
        {
            if (!RecheckDirectory(scope.Workspace, error) || !RecheckDirectory(scope.Store, error) ||
                !RecheckDirectory(scope.Pending, error) || !OnlyKnownPending(scope.Pending, error))
            {
                return false;
            }
            File header;
            if (!ReadChild(scope.Store, L"header.json", 16384, header, error) ||
                !Same(header.Image, scope.Header.Image))
            {
                return Fail(error, "header_changed");
            }
            ActiveTransaction value;
            if (!ReadChild(scope.Pending, L"intent.json", MaximumCookManagedIntentBytes, value.IntentFile, error))
            {
                return false;
            }
            CookManagedStorageAnchor anchor{scope.StoreId, Object(scope.Workspace), Object(scope.Store),
                                            Object(scope.Pending)};
            CookManagedTransactionEnvelope envelope;
            if (!ParseCookManagedRecoveryEnvelope(value.IntentFile.Data, anchor, envelope, error))
            {
                return false;
            }
            OptionalFile current, before, after, stateStage, stateFinal;
            if (!ReadOptional(scope.Store, L"roots.json", MaximumCookStoreIndexBytes, current, error) ||
                !ReadOptional(scope.Pending, L"index.before", MaximumCookStoreIndexBytes, before, error) ||
                !ReadOptional(scope.Pending, L"index.after", MaximumCookStoreIndexBytes, after, error))
            {
                return false;
            }
            const auto& beforeImage = *envelope.Control(CookManagedControlRole::IndexBefore);
            const auto& afterImage = *envelope.Control(CookManagedControlRole::IndexAfter);
            bool bBefore = false, bAfter = false;
            if (current.bPresent)
            {
                if (Same(current.Value.Image, beforeImage))
                {
                    value.bIndexBeforeFinal = true;
                    bBefore = true;
                    value.Controls[0] = std::move(current.Value);
                }
                else if (Same(current.Value.Image, afterImage))
                {
                    value.bIndexAfterFinal = true;
                    bAfter = true;
                    value.Controls[1] = std::move(current.Value);
                }
                else
                {
                    return Fail(error, "unknown_current_index");
                }
            }
            if (before.bPresent)
            {
                if (bBefore || !Same(before.Value.Image, beforeImage))
                {
                    return Fail(error, "before_index_identity");
                }
                bBefore = true;
                value.Controls[0] = std::move(before.Value);
            }
            if (after.bPresent)
            {
                if (bAfter || !Same(after.Value.Image, afterImage))
                {
                    return Fail(error, "after_index_identity");
                }
                bAfter = true;
                value.Controls[1] = std::move(after.Value);
            }
            if (!bBefore || !bAfter)
            {
                return Fail(error, "index_image_missing");
            }
            const auto stateLeaf = StateLeaf(envelope.ClaimId());
            if (!ReadOptional(scope.Pending, L"state.after", MaximumCookStateBytes, stateStage, error) ||
                !ReadOptional(scope.Store, stateLeaf, MaximumCookStateBytes, stateFinal, error) ||
                stateStage.bPresent == stateFinal.bPresent)
            {
                return Fail(error, "state_slot_ambiguous");
            }
            value.bStateFinal = stateFinal.bPresent;
            value.Controls[3] = std::move(stateFinal.bPresent ? stateFinal.Value : stateStage.Value);
            if (!Same(value.Controls[3].Image, *envelope.Control(CookManagedControlRole::StateAfter)))
            {
                return Fail(error, "after_state_identity");
            }
            auto docs = Documents(value);
            CookManagedRecoveryReadScope readScope;
            if (!BindCookManagedRecoveryReadScope(envelope, docs, readScope, error))
            {
                return false;
            }
            bool bStage = false, bFinal = false;
            Identity staged, final;
            if (!KnownDirectory(scope.Pending, L"root.stage", readScope.Root, bStage, staged, error) ||
                !KnownDirectory(scope.Workspace, Leaf(readScope.RootLeaf), readScope.Root, bFinal, final, error) ||
                bStage == bFinal)
            {
                return Fail(error, "root_slot_ambiguous");
            }
            value.bRootFinal = bFinal;
            value.Root = bFinal ? std::move(final) : std::move(staged);
            if (!ReadChild(value.Root, Leaf(readScope.ManifestName), MaximumCookStateBytes, value.Manifest, error) ||
                !Same(value.Manifest.Image, readScope.ManifestAfter))
            {
                return Fail(error, "after_manifest_identity");
            }
            docs = Documents(value);
            if (!ParseCookManagedTransactionIntent(envelope, docs, value.Intent, error))
            {
                return false;
            }
            const auto& d = value.Intent.Value();
            CookManagedStoreIndex oldIndex, newIndex;
            if (!ParseCookManagedStoreIndex(value.Controls[0].Data, scope.StoreId,
                                            MaximumCookStoreRoots - scope.AncestorRoots, oldIndex, error) ||
                !ParseCookManagedStoreIndex(value.Controls[1].Data, scope.StoreId,
                                            MaximumCookStoreRoots - scope.AncestorRoots, newIndex, error) ||
                !VerifyClaims(scope.Workspace, scope.Store, oldIndex, scope.Ancestors, error))
            {
                return false;
            }
            OptionalFile receiptStage, receiptCommit;
            if (!ReadOptional(scope.Pending, L"receipt.stage", 16384, receiptStage, error) ||
                !ReadOptional(scope.Pending, L"receipt.commit", 16384, receiptCommit, error) ||
                receiptStage.bPresent == receiptCommit.bPresent)
            {
                return Fail(error, "receipt_slot_ambiguous");
            }
            value.bCommitted = receiptCommit.bPresent;
            value.Receipt = std::move(receiptCommit.bPresent ? receiptCommit.Value : receiptStage.Value);
            if (!Same(value.Receipt.Image, d.Receipt))
            {
                return Fail(error, "receipt_identity");
            }
            if (value.bCommitted && (!value.bRootFinal || !value.bStateFinal || !value.bIndexAfterFinal))
            {
                return Fail(error, "commit_postimage_missing");
            }
            Array<TreeNode> tree;
            if (!ScanTree(value.Root, tree, &d, error))
            {
                return false;
            }
            tx = std::move(value);
            return true;
        }
        struct Operation
        {
            const CookManagedBootstrapRequest* Request = nullptr;
            std::filesystem::path RuntimeLocator;
            const Probe* Test = nullptr;
            StoreScope Scope;
            CookManagedBootstrapOutcome Outcome;
            Text Error;
            bool bPendingPublished = false, bRetired = false, bCommitted = false, bConflict = false;
            BootResult BootstrapStatus = BootResult::Error;
            RecoveryResult RecoveryStatus = RecoveryResult::Error;
        };
        bool Checkpoint(Operation& op, Point point)
        {
            if (!op.Test || !op.Test->Checkpoint)
            {
                return true;
            }
            const auto& runtime = op.RuntimeLocator;
            return op.Test->Checkpoint(point, op.Scope.Pending.Canonical, runtime, op.Test->Context) ||
                   Fail(op.Error, "checkpoint_failure");
        }
        bool Terminal(const ActiveTransaction& tx)
        {
            return tx.bCommitted ? (tx.bRootFinal && tx.bStateFinal && tx.bIndexAfterFinal)
                                 : (!tx.bRootFinal && !tx.bStateFinal && tx.bIndexBeforeFinal);
        }
        bool CleanupRetired(Operation& op, ActiveTransaction& tx)
        {
            Text cleanup;
            if (!tx.bRootFinal)
            {
                Array<TreeNode> tree;
                if (!ScanTree(tx.Root, tree, &tx.Intent.Value(), cleanup))
                {
                    return false;
                }
                for (const auto& node : tree)
                {
                    if (!node.bDirectory && !DeleteKnown(node.Parent, node.Native.Canonical.filename(),
                                                         Object(node.Native), false, &node.Image, cleanup))
                    {
                        return false;
                    }
                }
                std::sort(tree.begin(), tree.end(),
                          [](const auto& a, const auto& b)
                          {
                              return a.Relative.size() > b.Relative.size();
                          });
                for (const auto& node : tree)
                {
                    if (node.bDirectory && !DeleteKnown(node.Parent, node.Native.Canonical.filename(),
                                                        Object(node.Native), true, nullptr, cleanup))
                    {
                        return false;
                    }
                }
                if (!DeleteKnown(op.Scope.Pending, L"root.stage", Object(tx.Root), true, nullptr, cleanup))
                {
                    return false;
                }
            }
            for (const auto* file : {&tx.Controls[0], &tx.Controls[1], &tx.Controls[3], &tx.Receipt, &tx.IntentFile})
            {
                if (file->Native.Canonical.parent_path() != op.Scope.Pending.Canonical)
                {
                    continue;
                }
                if (!DeleteKnown(op.Scope.Pending, file->Native.Canonical.filename(), file->Image.Object, false,
                                 &file->Image, cleanup))
                {
                    return false;
                }
            }
            return DeleteKnown(op.Scope.Store, op.Scope.Pending.Canonical.filename(), Object(op.Scope.Pending), true,
                               nullptr, cleanup);
        }
        bool Retire(Operation& op)
        {
            ActiveTransaction tx;
            if (!LoadActive(op.Scope, tx, op.Error) || !Terminal(tx) || !Checkpoint(op, Point::BeforeRetire) ||
                !LoadActive(op.Scope, tx, op.Error) || !Terminal(tx))
            {
                return Fail(op.Error, "retirement_requires_stable_state");
            }
            const auto& d = tx.Intent.Value();
            Text retired = ".transaction-retired-";
            retired.append(d.TransactionId);
            const auto oldId = Object(op.Scope.Pending);
            if (!Rename(op.Scope.Store, L"pending", oldId, op.Scope.Store, Leaf(retired), true, nullptr, op.Error,
                        &op.bRetired))
            {
                return false;
            }
            op.bRetired = true;
            op.bCommitted = tx.bCommitted;
            if (!N::Directory(op.Scope.Store.Canonical / Leaf(retired), op.Scope.Pending, op.Error) ||
                !Same(op.Scope.Pending, oldId) || !Checkpoint(op, Point::Retired))
            {
                return false;
            }
            // 退役後の同process内だけでknown objectを清掃する。次のprocessはこの名前を探索しない。
            ActiveTransaction moved;
            if (!LoadActive(op.Scope, moved, op.Error) || !Terminal(moved))
            {
                return false;
            }
            op.Outcome.ClaimId = moved.Intent.Value().ClaimId;
            op.Outcome.IndexGeneration = moved.Intent.Value().IndexGeneration - (moved.bCommitted ? 0 : 1);
            op.Outcome.StateGeneration = moved.bCommitted ? moved.Intent.Value().StateGeneration : 0;
            if (!CleanupRetired(op, moved))
            {
                op.Outcome.bCleanupIncomplete = true;
                op.Outcome.RetiredDirectory = op.Scope.Pending.Canonical;
            }
            op.Error.clear();
            return true;
        }
        bool Rollback(Operation& op)
        {
            ActiveTransaction tx;
            if (!LoadActive(op.Scope, tx, op.Error) || tx.bCommitted)
            {
                return Fail(op.Error, "rollback_uncommitted_required");
            }
            if (tx.bIndexAfterFinal)
            {
                const auto image = tx.Intent.Value().Controls[1];
                if (!Rename(op.Scope.Store, L"roots.json", image.Object, op.Scope.Pending, L"index.after", false,
                            &image, op.Error) ||
                    !Checkpoint(op, Point::RollbackIndexRemoved))
                {
                    return false;
                }
            }
            if (!LoadActive(op.Scope, tx, op.Error))
            {
                return false;
            }
            if (!tx.bIndexBeforeFinal)
            {
                const auto image = tx.Intent.Value().Controls[0];
                if (!Rename(op.Scope.Pending, L"index.before", image.Object, op.Scope.Store, L"roots.json", false,
                            &image, op.Error) ||
                    !Checkpoint(op, Point::RollbackIndexRestored))
                {
                    return false;
                }
            }
            if (!LoadActive(op.Scope, tx, op.Error))
            {
                return false;
            }
            if (tx.bStateFinal)
            {
                const auto image = tx.Intent.Value().Controls[3];
                if (!Rename(op.Scope.Store, StateLeaf(tx.Intent.Value().ClaimId), image.Object, op.Scope.Pending,
                            L"state.after", false, &image, op.Error) ||
                    !Checkpoint(op, Point::RollbackStateRestored))
                {
                    return false;
                }
            }
            if (!LoadActive(op.Scope, tx, op.Error))
            {
                return false;
            }
            if (tx.bRootFinal)
            {
                if (!Rename(op.Scope.Workspace, Leaf(tx.Intent.Value().Anchor.RootLeaf), tx.Intent.Value().Root,
                            op.Scope.Pending, L"root.stage", true, nullptr, op.Error) ||
                    !Checkpoint(op, Point::RollbackRootRestored))
                {
                    return false;
                }
            }
            return Retire(op);
        }
        bool PlanScope(const CookPreparedPlan& plan, const CookStateBinding& binding, Text& error)
        {
            if (plan.Outputs.size() != 1 || plan.Outputs[0].ExpectedIdentity.Kind != Core::Asset::AssetKind::Texture)
            {
                return Fail(error, "texture_v1_only");
            }
            Text target, manifest, expected = binding.RuntimeRootIdentity;
            expected.push_back('/');
            expected.append(plan.Outputs[0].ExpectedIdentity.CookedPackage);
            if (!P::AsciiPath(plan.Outputs[0].TargetPath.lexically_normal(), target) || target != expected)
            {
                return Fail(error, "final_package_scope");
            }
            expected = binding.RuntimeRootIdentity;
            expected.push_back('/');
            expected.append(binding.ManifestName);
            if (!P::AsciiPath(plan.Context.Request.ManifestPath.lexically_normal(), manifest) || manifest != expected)
            {
                return Fail(error, "final_manifest_scope");
            }
            return true;
        }
        bool SamePlan(const CookPreparedPlan& a, const CookPreparedPlan& b)
        {
            if (a.Context.Dependencies.SchemaVersion != b.Context.Dependencies.SchemaVersion ||
                a.Context.Dependencies.CookerRevision != b.Context.Dependencies.CookerRevision ||
                a.Context.Dependencies.Fingerprint != b.Context.Dependencies.Fingerprint ||
                a.Outputs.size() != b.Outputs.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.Outputs.size(); ++i)
            {
                if (!Detail::CookReferenceValues::SameIdentity(a.Outputs[i].ExpectedIdentity,
                                                               b.Outputs[i].ExpectedIdentity) ||
                    a.Outputs[i].TargetPath != b.Outputs[i].TargetPath)
                {
                    return false;
                }
            }
            return true;
        }
        bool Fresh(Operation& op, const CookDestinationLockContext& lock, const CookResolvedOwnerBinding& owner,
                   const File& spec, const File& beforeIndex, const Array<CookPreparedPlan>& plans, Text& error)
        {
            CookManagedStoreObservation observation;
            if (Detail::ObserveCookManagedStoreLocked(lock, op.Request->Owner, observation, error) !=
                    CookManagedStoreResult::Observed ||
                observation.Owner.bFinalRuntimeRootPresent ||
                observation.Owner.ExpectedBinding.OwnerId != owner.ExpectedBinding.OwnerId ||
                observation.Owner.Identity.CanonicalSpecLocator != owner.Identity.CanonicalSpecLocator ||
                observation.Owner.Identity.CanonicalFinalRuntimeRootIdentity !=
                    owner.Identity.CanonicalFinalRuntimeRootIdentity)
            {
                return Fail(error, "owner_or_store_changed");
            }
            File currentSpec, currentIndex, currentHeader;
            if (!ReadSpec(owner, currentSpec, error) || !Same(currentSpec.Image, spec.Image) ||
                !EqualBytes(currentSpec.Data, op.Request->ExpectedSpecBytes) ||
                !ReadChild(op.Scope.Store, L"roots.json", MaximumCookStoreIndexBytes, currentIndex, error) ||
                !Same(currentIndex.Image, beforeIndex.Image) ||
                !ReadChild(op.Scope.Store, L"header.json", 16384, currentHeader, error) ||
                !Same(currentHeader.Image, op.Scope.Header.Image))
            {
                return Fail(error, "control_or_spec_changed");
            }
            Array<CookPreparedPlan> fresh;
            for (const auto& plan : plans)
            {
                CookPreparedPlan candidate;
                if (!PrepareCookOutputPlan(plan.Context.Request, op.Request->CookerRevision, nullptr, candidate,
                                           error) ||
                    !SamePlan(plan, candidate))
                {
                    return Fail(error, "dependencies_changed");
                }
                fresh.push_back(std::move(candidate));
            }

            // guardのlocatorはdrive-formが必要。source specとFINAL全依存はここで再検査する。
            return ValidateCookOutputSet(fresh, {&owner.SpecLocator, 1}, error);
        }
        bool PrepareBootstrap(Operation& op, const CookDestinationLockContext& lock)
        {
            const auto& request = *op.Request;
            if (request.Assets.empty() || !request.Assets.data() || request.Assets.size() > MaximumCookSetPlans ||
                request.ExpectedSpecBytes.empty() || !request.ExpectedSpecBytes.data() ||
                request.ExpectedSpecBytes.size() > MaximumCookStateBytes || !request.CookerRevision)
            {
                return Fail(op.Error, "request_limits");
            }
            CookManagedStoreObservation observation;
            const auto observed = Detail::ObserveCookManagedStoreLocked(lock, request.Owner, observation, op.Error);
            if (observed != CookManagedStoreResult::Observed || observation.Owner.bFinalRuntimeRootPresent)
            {
                op.bConflict = true;
                return Fail(op.Error, "absent_runtime_and_initialized_store_required");
            }
            const auto& owner = observation.Owner;
            File spec, beforeIndex;
            if (!ReadSpec(owner, spec, op.Error) || !EqualBytes(spec.Data, request.ExpectedSpecBytes))
            {
                return Fail(op.Error, "parsed_spec_changed");
            }
            if (!ReadChild(op.Scope.Store, L"roots.json", MaximumCookStoreIndexBytes, beforeIndex, op.Error))
            {
                return false;
            }
            CookManagedStoreIndex index;
            if (!ParseCookManagedStoreIndex(beforeIndex.Data, op.Scope.StoreId,
                                            MaximumCookStoreRoots - op.Scope.AncestorRoots, index, op.Error) ||
                index.Generation == UINT64_MAX || index.Roots.size() + op.Scope.AncestorRoots >= MaximumCookStoreRoots)
            {
                return Fail(op.Error, "index_capacity");
            }
            Array<CookPreparedPlan> plans, stages;
            Array<Identity> workDirectories;
            Array<std::filesystem::path> workLeaves;
            for (const auto& asset : request.Assets)
            {
                if (asset.Kind != "texture")
                {
                    return Fail(op.Error, "texture_v1_only");
                }
                CookPreparedPlan plan;
                if (!PrepareCookOutputPlan(asset, request.CookerRevision, nullptr, plan, op.Error) ||
                    !PlanScope(plan, owner.ExpectedBinding, op.Error))
                {
                    return false;
                }
                plans.push_back(std::move(plan));
            }
            if (!ValidateCookOutputSet(plans, {&owner.SpecLocator, 1}, op.Error))
            {
                return false;
            }
            Text transaction, claim, stageName;
            if (!Token(transaction, op.Error) || !Token(claim, op.Error))
            {
                return false;
            }
            stageName = ".transaction-stage-";
            stageName.append(transaction);
            if (!NewDirectory(op.Scope.Store, Leaf(stageName), op.Scope.Pending, op.Error) ||
                !Absent(op.Scope.Store, StateLeaf(claim), op.Error))
            {
                return false;
            }
            Identity root;
            if (!NewDirectory(op.Scope.Pending, L"root.stage", root, op.Error))
            {
                return false;
            }
            const auto callerStage = owner.FinalRuntimeRootLocator.parent_path() / N::StoreLeaf / Leaf(stageName);
            for (size_t i = 0; i < plans.size(); ++i)
            {
                Text name = "work-";
                char number[32];
                const auto written = std::to_chars(number, number + sizeof(number), i);
                name.append(number, static_cast<size_t>(written.ptr - number));
                Identity directory;
                if (!NewDirectory(op.Scope.Pending, Leaf(name), directory, op.Error))
                {
                    return false;
                }
                CookPreparedPlan staged;
                if (!PrepareCookStagingPlan(plans[i], callerStage / Leaf(name), staged, op.Error))
                {
                    return false;
                }
                workLeaves.push_back(Leaf(name));
                workDirectories.push_back(std::move(directory));
                stages.push_back(std::move(staged));
            }
            if (!ValidateCookOutputSet(stages, {&owner.SpecLocator, 1}, op.Error))
            {
                return false;
            }
            CookManagedIntentBuildInput input;
            auto& draft = input.Draft;
            draft.TransactionId = transaction;
            draft.ClaimId = claim;
            draft.Anchor.StoreId = op.Scope.StoreId;
            draft.Anchor.Workspace = Object(op.Scope.Workspace);
            draft.Anchor.Store = Object(op.Scope.Store);
            draft.Anchor.Pending = Object(op.Scope.Pending);
            draft.Anchor.Binding = owner.ExpectedBinding;
            if (!P::AsciiPath(owner.FinalRuntimeRootLocator.filename(), draft.Anchor.RootLeaf))
            {
                return Fail(op.Error, "runtime_leaf");
            }
            draft.Root = Object(root);
            draft.IndexGeneration = index.Generation + 1;
            draft.StateGeneration = 1;
            draft.Controls[0] = beforeIndex.Image;
            draft.ManifestBefore.Parent = draft.Root;
            CookOwnedState state;
            state.Binding = owner.ExpectedBinding;
            Array<Core::Asset::AssetCookedReference> references;
            for (size_t i = 0; i < stages.size(); ++i)
            {
                if (!CookSingleAsset(stages[i].Context.Request, op.Error))
                {
                    return false;
                }
                File fragmentFile;
                Core::Asset::AssetManifest fragment;
                CookOutputRecord record;
                if (!ReadChild(workDirectories[i], Leaf(owner.ExpectedBinding.ManifestName), MaximumCookStateBytes,
                               fragmentFile, op.Error) ||
                    !LoadManifest(fragmentFile.Data, fragment, op.Error) ||
                    !CaptureStagedCookOutputRecord(plans[i], stages[i], fragment, record, op.Error) ||
                    record.Outputs.size() != 1)
                {
                    return Fail(op.Error, "staged_capture");
                }
                Array<TreeNode> work;
                if (!ScanTree(workDirectories[i], work, nullptr, op.Error))
                {
                    return false;
                }
                const auto& recorded = record.Outputs[0];
                const TreeNode* package = nullptr;
                for (const auto& node : work)
                {
                    if (node.bDirectory)
                    {
                        Text prefix = node.Relative;
                        prefix.push_back('/');
                        if (recorded.Reference.CookedPackage.size() <= prefix.size() ||
                            std::memcmp(recorded.Reference.CookedPackage.data(), prefix.data(), prefix.size()))
                        {
                            return Fail(op.Error, "unexpected_work_directory");
                        }
                    }
                    else if (node.Relative == recorded.Reference.CookedPackage)
                    {
                        package = &node;
                    }
                    else if (node.Relative != owner.ExpectedBinding.ManifestName)
                    {
                        return Fail(op.Error, "unexpected_work_file");
                    }
                }
                if (!package || package->Image.Size != recorded.Package.Size ||
                    package->Image.ContentHash != recorded.Package.ContentHash)
                {
                    return Fail(op.Error, "staged_package_changed");
                }
                Identity parent;
                if (!EnsureParents(root, recorded.Reference.CookedPackage, parent, op.Error) ||
                    !Rename(package->Parent, package->Native.Canonical.filename(), package->Image.Object, parent,
                            Leaf(recorded.Reference.CookedPackage).filename(), false, &package->Image, op.Error))
                {
                    return false;
                }
                CookManagedPackageMutation mutation;
                mutation.Package = recorded.Reference.CookedPackage;
                mutation.Before.Parent = Object(parent);
                mutation.After = package->Image;
                draft.Packages.push_back(std::move(mutation));
                references.push_back(recorded.Reference);
                CookOwnedRecord owned;
                owned.PrimaryKey = {recorded.Reference.LogicalPath, recorded.Reference.Kind,
                                    recorded.Reference.Variant};
                owned.Record = std::move(record);
                state.Records.push_back(std::move(owned));
                if (!DeleteKnown(workDirectories[i], Leaf(owner.ExpectedBinding.ManifestName),
                                 fragmentFile.Image.Object, false, &fragmentFile.Image, op.Error))
                {
                    return false;
                }
                std::sort(work.begin(), work.end(),
                          [](const auto& a, const auto& b)
                          {
                              return a.Relative.size() > b.Relative.size();
                          });
                for (const auto& node : work)
                {
                    if (node.bDirectory && !DeleteKnown(node.Parent, node.Native.Canonical.filename(),
                                                        Object(node.Native), true, nullptr, op.Error))
                    {
                        return false;
                    }
                }
                if (!DeleteKnown(op.Scope.Pending, workLeaves[i], Object(workDirectories[i]), true, nullptr, op.Error))
                {
                    return false;
                }
            }
            Text aggregate;
            File manifest;
            if (!Detail::SerializeLegacyTextureManifest(references, aggregate, op.Error) ||
                !NewFile(root, Leaf(owner.ExpectedBinding.ManifestName), BytesOf(aggregate), manifest, op.Error))
            {
                return false;
            }
            draft.ManifestAfter = manifest.Image;
            Array<TreeNode> tree;
            if (!ScanTree(root, tree, nullptr, op.Error))
            {
                return false;
            }
            for (const auto& node : tree)
            {
                if (node.bDirectory)
                {
                    draft.Directories.push_back({node.Relative, Object(node.Native), Object(node.Parent)});
                }
            }
            CookManagedRootClaim rootClaim;
            rootClaim.ClaimId = claim;
            rootClaim.RootLeaf = draft.Anchor.RootLeaf;
            rootClaim.OwnerId = owner.ExpectedBinding.OwnerId;
            rootClaim.DirectoryId = root.FileId;
            index.Generation = draft.IndexGeneration;
            index.Roots.push_back(rootClaim);
            Text indexText, stateText, receiptText, wire;
            File afterIndex, afterState, receipt, intentFile;
            if (!SerializeCookManagedStoreIndex(index, indexText, op.Error) ||
                !SerializeCookOwnedState(state, stateText, op.Error) ||
                !NewFile(op.Scope.Pending, L"index.after", BytesOf(indexText), afterIndex, op.Error) ||
                !NewFile(op.Scope.Pending, L"state.after", BytesOf(stateText), afterState, op.Error))
            {
                return false;
            }
            draft.Controls[1] = afterIndex.Image;
            draft.Controls[3] = afterState.Image;
            if (!MakeCookManagedReceiptBody(draft, receiptText, op.Error) ||
                !NewFile(op.Scope.Pending, L"receipt.stage", BytesOf(receiptText), receipt, op.Error))
            {
                return false;
            }
            draft.Receipt = receipt.Image;
            input.FinalPlans = plans;
            input.FinalRecords = state.Records;
            CookManagedControlDocuments docs;
            docs.Controls[0] = {beforeIndex.Image.Object, beforeIndex.Data};
            docs.Controls[1] = {afterIndex.Image.Object, afterIndex.Data};
            docs.Controls[3] = {afterState.Image.Object, afterState.Data};
            docs.ManifestAfter = {manifest.Image.Object, manifest.Data};
            CookManagedTransactionIntent intent;
            if (!BuildCookManagedTransactionIntent(input, docs, intent, op.Error) ||
                !SerializeCookManagedTransactionIntent(intent, wire, op.Error) ||
                !NewFile(op.Scope.Pending, L"intent.json", BytesOf(wire), intentFile, op.Error) ||
                !Checkpoint(op, Point::Prepared) || !Fresh(op, lock, owner, spec, beforeIndex, plans, op.Error) ||
                !Absent(op.Scope.Workspace, Leaf(draft.Anchor.RootLeaf), op.Error) ||
                !Absent(op.Scope.Store, StateLeaf(claim), op.Error))
            {
                return false;
            }
            ActiveTransaction check;
            if (!LoadActive(op.Scope, check, op.Error))
            {
                return false;
            }
            const auto id = Object(op.Scope.Pending);
            if (!Rename(op.Scope.Store, Leaf(stageName), id, op.Scope.Store, L"pending", true, nullptr, op.Error,
                        &op.bPendingPublished))
            {
                return false;
            }
            if (!N::Directory(op.Scope.Store.Canonical / L"pending", op.Scope.Pending, op.Error) ||
                !Same(op.Scope.Pending, id) || !Checkpoint(op, Point::PendingPublished))
            {
                return false;
            }
            return true;
        }
        bool PublishBootstrap(Operation& op)
        {
            ActiveTransaction tx;
            if (!LoadActive(op.Scope, tx, op.Error) || tx.bCommitted || tx.bRootFinal || tx.bStateFinal ||
                !tx.bIndexBeforeFinal)
            {
                return Fail(op.Error, "bootstrap_start_state");
            }
            if (!Rename(op.Scope.Pending, L"root.stage", tx.Intent.Value().Root, op.Scope.Workspace,
                        Leaf(tx.Intent.Value().Anchor.RootLeaf), true, nullptr, op.Error) ||
                !Checkpoint(op, Point::RootPublished) || !LoadActive(op.Scope, tx, op.Error))
            {
                return false;
            }
            auto image = tx.Intent.Value().Controls[3];
            if (!Rename(op.Scope.Pending, L"state.after", image.Object, op.Scope.Store,
                        StateLeaf(tx.Intent.Value().ClaimId), false, &image, op.Error) ||
                !Checkpoint(op, Point::StatePublished) || !LoadActive(op.Scope, tx, op.Error))
            {
                return false;
            }
            image = tx.Intent.Value().Controls[0];
            if (!Rename(op.Scope.Store, L"roots.json", image.Object, op.Scope.Pending, L"index.before", false, &image,
                        op.Error) ||
                !Checkpoint(op, Point::IndexBackedUp) || !LoadActive(op.Scope, tx, op.Error))
            {
                return false;
            }
            image = tx.Intent.Value().Controls[1];
            if (!Rename(op.Scope.Pending, L"index.after", image.Object, op.Scope.Store, L"roots.json", false, &image,
                        op.Error) ||
                !Checkpoint(op, Point::IndexPublished) || !Checkpoint(op, Point::BeforeReceipt) ||
                !LoadActive(op.Scope, tx, op.Error))
            {
                return false;
            }
            image = tx.Intent.Value().Receipt;
            if (!Rename(op.Scope.Pending, L"receipt.stage", image.Object, op.Scope.Pending, L"receipt.commit", false,
                        &image, op.Error, &op.bCommitted) ||
                !Checkpoint(op, Point::ReceiptCommitted))
            {
                return false;
            }
            return Retire(op);
        }
        bool BootstrapCallback(const CookDestinationLockContext& lock, void* data, Text& error)
        {
            auto& op = *static_cast<Operation*>(data);
            if (op.Test && op.Test->bAbandonedObserved)
            {
                *op.Test->bAbandonedObserved = lock.bAbandoned;
            }
            if (!OpenStore(lock, op.Scope, op.Error))
            {
                op.bConflict = true;
                error = op.Error;
                return false;
            }
            if (op.Scope.bPending)
            {
                op.bPendingPublished = true;
                return Fail(op.Error, "pending_requires_recovery");
            }
            const bool bSuccess = PrepareBootstrap(op, lock) && PublishBootstrap(op);
            if (bSuccess)
            {
                op.BootstrapStatus = BootResult::Created;
            }
            error = op.Error;
            return bSuccess;
        }
        bool RecoveryCallback(const CookDestinationLockContext& lock, void* data, Text& error)
        {
            auto& op = *static_cast<Operation*>(data);
            if (op.Test && op.Test->bAbandonedObserved)
            {
                *op.Test->bAbandonedObserved = lock.bAbandoned;
            }
            if (!OpenStore(lock, op.Scope, op.Error))
            {
                op.RecoveryStatus = RecoveryResult::Conflict;
                error = op.Error;
                return false;
            }
            if (!op.Scope.bPending)
            {
                op.RecoveryStatus = RecoveryResult::NoPending;
                error.clear();
                return true;
            }
            op.bPendingPublished = true;
            ActiveTransaction tx;
            if (!LoadActive(op.Scope, tx, op.Error))
            {
                op.RecoveryStatus = RecoveryResult::Conflict;
                error = op.Error;
                return false;
            }
            const bool bCommitted = tx.bCommitted;
            op.bCommitted = bCommitted;
            const bool bSuccess = bCommitted ? Retire(op) : Rollback(op);
            if (bSuccess)
            {
                op.RecoveryStatus = bCommitted ? RecoveryResult::Committed : RecoveryResult::RolledBack;
            }
            error = op.Error;
            return bSuccess;
        }
#endif
        BootResult BootstrapImpl(const CookManagedBootstrapRequest& request, const Probe* probe,
                                 CookManagedBootstrapOutcome& out, Text& error)
        {
            error.clear();
#if !defined(_WIN32)
            (void)request;
            (void)probe;
            (void)out;
            Fail(error, "windows_required");
            return BootResult::Error;
#else
            Operation op;
            op.Request = &request;
            op.Test = probe;
            op.RuntimeLocator = request.Owner.FinalRuntimeRoot;
            try
            {
                const auto result =
                    probe ? Detail::WithCookDestinationLockForTest({request.Owner.FinalRuntimeRoot}, BootstrapCallback,
                                                                   &op, error, probe->LockFault)
                          : WithCookDestinationLock({request.Owner.FinalRuntimeRoot}, BootstrapCallback, &op, error);
                if (result == CookDestinationLockResult::Busy)
                {
                    return BootResult::Busy;
                }
                if (result == CookDestinationLockResult::Executed && op.BootstrapStatus == BootResult::Created)
                {
                    out = std::move(op.Outcome);
                    error.clear();
                    return BootResult::Created;
                }
            }
            catch (const std::exception&)
            {
                Fail(error, "bootstrap_exception");
            }
            if (!op.Error.empty())
            {
                error = op.Error;
            }
            if (op.bPendingPublished && !op.bRetired)
            {
                error.append("; pending_preserved");
                return BootResult::NeedsRecovery;
            }
            if (op.bCommitted && op.bRetired)
            {
                error.append("; committed_state_preserved");
                return BootResult::CommittedButError;
            }
            return op.bConflict ? BootResult::Conflict : BootResult::Error;
#endif
        }
        RecoveryResult RecoveryImpl(const std::filesystem::path& runtime, const Probe* probe,
                                    CookManagedBootstrapOutcome& out, Text& error)
        {
            error.clear();
#if !defined(_WIN32)
            (void)runtime;
            (void)probe;
            (void)out;
            Fail(error, "windows_required");
            return RecoveryResult::Error;
#else
            Operation op;
            op.Test = probe;
            op.RuntimeLocator = runtime;
            try
            {
                const auto result = probe ? Detail::WithCookDestinationLockForTest({runtime}, RecoveryCallback, &op,
                                                                                   error, probe->LockFault)
                                          : WithCookDestinationLock({runtime}, RecoveryCallback, &op, error);
                if (result == CookDestinationLockResult::Busy)
                {
                    return RecoveryResult::Busy;
                }
                if (result == CookDestinationLockResult::Executed)
                {
                    if (op.RecoveryStatus == RecoveryResult::Committed ||
                        op.RecoveryStatus == RecoveryResult::RolledBack)
                    {
                        out = std::move(op.Outcome);
                    }
                    error.clear();
                    return op.RecoveryStatus;
                }
            }
            catch (const std::exception&)
            {
                Fail(error, "recovery_exception");
            }
            if (!op.Error.empty())
            {
                error = op.Error;
            }
            return op.RecoveryStatus == RecoveryResult::Conflict ? RecoveryResult::Conflict : RecoveryResult::Error;
#endif
        }
    } // namespace
    CookManagedBootstrapResult BootstrapCookManagedAssetSet(const CookManagedBootstrapRequest& request,
                                                            CookManagedBootstrapOutcome& out, Text& error)
    {
        return BootstrapImpl(request, nullptr, out, error);
    }
    CookManagedRecoveryResult RecoverCookManagedPending(const std::filesystem::path& runtime,
                                                        CookManagedBootstrapOutcome& out, Text& error)
    {
        return RecoveryImpl(runtime, nullptr, out, error);
    }
    CookManagedBootstrapResult Detail::BootstrapCookManagedAssetSetForTest(const CookManagedBootstrapRequest& request,
                                                                           const CookManagedBootstrapProbe& probe,
                                                                           CookManagedBootstrapOutcome& out,
                                                                           Text& error)
    {
        return BootstrapImpl(request, &probe, out, error);
    }
    CookManagedRecoveryResult Detail::RecoverCookManagedPendingForTest(const std::filesystem::path& runtime,
                                                                       const CookManagedBootstrapProbe& probe,
                                                                       CookManagedBootstrapOutcome& out, Text& error)
    {
        return RecoveryImpl(runtime, &probe, out, error);
    }
} // namespace NorvesLib::Tools::AssetCook
