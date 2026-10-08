// 既知objectの公開・条件付き復旧・退役を一か所で管理する。
#include "CookManagedTransactionController.h"
#include "CookManagedStoreNative.h"
#include "CookManagedTransactionIntent.h"
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
namespace NorvesLib::Tools::AssetCook::Detail::ManagedTransaction
{
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
    bool Find(const Identity& parent, const std::filesystem::path& leaf, const N::Entry*& found, N::Entries& entries,
              Text& error)
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
                   bool bEnumerate)
    {
        N::Entries entries;
        const N::Entry* found = nullptr;
        if (bEnumerate && (!Find(parent, leaf, found, entries, error) || !found))
        {
            return Fail(error, "fixed_file_missing");
        }
        Handle h;
        h.Value = CreateFileW((parent.Canonical / leaf).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
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
        h.Value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
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
                const CookManagedFileImage* file, Text& error, bool* bMoved)
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
        h.Value = CreateFileW((from.Canonical / oldLeaf).c_str(),
                              DELETE | FILE_READ_ATTRIBUTES | (bDirectory ? 0 : GENERIC_READ),
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
            if (!N::Enumerate(store, controls, budget, error) || !N::Fixed(controls, L"pending", false, pending, error))
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
                    !ParseCookManagedStoreIndex(file.Data, view.StoreId, MaximumCookStoreRoots - roots, index, error) ||
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
                                   match ? static_cast<size_t>(match->Image.Size) : MaximumPackageBytes, file, error,
                                   false))
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
    bool DeleteKnown(const Identity& parent, const std::filesystem::path& leaf, const CookManagedObjectId& object,
                     bool bDirectory, const CookManagedFileImage* file, Text& error)
    {
        if (!RecheckDirectory(parent, error))
        {
            return false;
        }
        Handle h;
        h.Value = CreateFileW((parent.Canonical / leaf).c_str(),
                              DELETE | FILE_READ_ATTRIBUTES | (bDirectory ? 0 : GENERIC_READ),
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
        return (SetFileInformationByHandle(h.Value, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE &&
                h.Close()) ||
               Fail(error, "cleanup_disposition");
    }
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
    bool KnownDirectory(const Identity& parent, const std::filesystem::path& leaf, const CookManagedObjectId& expected,
                        bool& bPresent, Identity& out, Text& error)
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
        return (N::Directory(parent.Canonical / leaf, out, error) && N::Direct(parent, out) && Same(out, expected) &&
                out.Canonical.filename().native() == leaf.native()) ||
               Fail(error, "root_candidate_changed");
    }
    std::filesystem::path StateLeaf(View claim)
    {
        Text leaf = "state-";
        leaf.append(claim);
        leaf.append(".json");
        return Leaf(leaf);
    }
    std::filesystem::path PackageSlot(bool bBefore, size_t ordinal)
    {
        Text name = bBefore ? "package.before-" : "package.after-";
        char digits[16];
        const auto written = std::to_chars(digits, digits + sizeof(digits), ordinal, 16);
        const size_t count = static_cast<size_t>(written.ptr - digits);
        if (count < 8)
        {
            name.append(8 - count, '0');
        }
        name.append(digits, count);
        return Leaf(name);
    }
    bool ExistingParent(const Identity& root, View relative, Identity& parent, std::filesystem::path& leaf, Text& error)
    {
        if (!P::SafeOutputName(relative) || relative.size() > MaximumCookStateStringBytes)
        {
            return Fail(error, "relative_package");
        }
        Identity current = root;
        size_t start = 0, depth = 0;
        for (size_t i = 0; i < relative.size(); ++i)
        {
            if (relative[i] != '/')
            {
                continue;
            }
            if (++depth >= Detail::MaximumCookLocatorComponents)
            {
                return Fail(error, "parent_depth");
            }
            const auto part = Leaf({relative.data() + start, i - start});
            const N::Entry* found = nullptr;
            N::Entries entries;
            Identity next;
            if (!Find(current, part, found, entries, error) || !found ||
                !N::Directory(current.Canonical / part, next, error) || !N::Direct(current, next) ||
                next.Canonical.filename().native() != part.native())
            {
                return Fail(error, "existing_parent_required");
            }
            current = std::move(next);
            start = i + 1;
        }
        parent = std::move(current);
        leaf = Leaf({relative.data() + start, relative.size() - start});
        return true;
    }
    struct ClassifiedFile
    {
        File Before, After;
        Identity Parent;
        std::filesystem::path LeafName;
        bool bBeforeFinal = false, bAfterFinal = false;
    };
    bool ClassifyFile(const Identity& pending, const Identity& parent, const std::filesystem::path& leaf,
                      const std::filesystem::path& beforeSlot, const std::filesystem::path& afterSlot,
                      const CookManagedBeforeImage& expectedBefore, const CookManagedFileImage& expectedAfter,
                      size_t limit, ClassifiedFile& out, Text& error)
    {
        if (!Same(parent, expectedBefore.Parent))
        {
            return Fail(error, "target_parent_identity");
        }
        OptionalFile current, before, after;
        if (!ReadOptional(parent, leaf, limit, current, error) ||
            !ReadOptional(pending, beforeSlot, limit, before, error) ||
            !ReadOptional(pending, afterSlot, limit, after, error))
        {
            return false;
        }
        ClassifiedFile value;
        value.Parent = parent;
        value.LeafName = leaf;
        bool bFoundBefore = false, bFoundAfter = false;
        if (current.bPresent)
        {
            if (expectedBefore.bPresent && Same(current.Value.Image, expectedBefore.File))
            {
                bFoundBefore = value.bBeforeFinal = true;
                value.Before = std::move(current.Value);
            }
            else if (Same(current.Value.Image, expectedAfter))
            {
                bFoundAfter = value.bAfterFinal = true;
                value.After = std::move(current.Value);
            }
            else
            {
                return Fail(error, "unknown_final_image");
            }
        }
        else if (!expectedBefore.bPresent)
        {
            value.bBeforeFinal = true;
        }
        if (before.bPresent)
        {
            if (!expectedBefore.bPresent || bFoundBefore || !Same(before.Value.Image, expectedBefore.File))
            {
                return Fail(error, "before_slot_identity");
            }
            bFoundBefore = true;
            value.Before = std::move(before.Value);
        }
        if (after.bPresent)
        {
            if (bFoundAfter || !Same(after.Value.Image, expectedAfter))
            {
                return Fail(error, "after_slot_identity");
            }
            bFoundAfter = true;
            value.After = std::move(after.Value);
        }
        if (bFoundBefore != expectedBefore.bPresent || !bFoundAfter)
        {
            return Fail(error, "transaction_image_missing");
        }
        out = std::move(value);
        return true;
    }
    CookManagedBeforeImage PresentImage(const Identity& parent, const CookManagedFileImage& image)
    {
        return {true, Object(parent), image};
    }
    bool KnownUpdatePending(const Identity& pending, const CookManagedIntentDraft& draft, Text& error)
    {
        N::Entries entries;
        N::Budget budget;
        if (!N::Enumerate(pending, entries, budget, error))
        {
            return false;
        }
        for (const auto& entry : entries)
        {
            Text name;
            if (!P::AsciiPath(entry.Name, name))
            {
                return Fail(error, "pending_name");
            }
            const char* fixed[] = {"intent.json",     "index.before",   "index.after",   "state.before",  "state.after",
                                   "manifest.before", "manifest.after", "receipt.stage", "receipt.commit"};
            bool bKnown = false;
            for (const auto* item : fixed)
            {
                bKnown |= name == item;
            }
            if (bKnown)
            {
                continue;
            }
            const bool bBefore = name.size() == 23 && std::memcmp(name.data(), "package.before-", 15) == 0;
            const bool bAfter = name.size() == 22 && std::memcmp(name.data(), "package.after-", 14) == 0;
            if (!bBefore && !bAfter)
            {
                return Fail(error, "unknown_pending_entry");
            }
            const size_t offset = bBefore ? 15 : 14;
            size_t ordinal = 0;
            const auto parsed = std::from_chars(name.data() + offset, name.data() + name.size(), ordinal, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != name.data() + name.size() ||
                ordinal >= draft.Packages.size() || entry.Name != PackageSlot(bBefore, ordinal) ||
                (bBefore && !draft.Packages[ordinal].Before.bPresent))
            {
                return Fail(error, "unknown_package_slot");
            }
        }
        return true;
    }
    struct ActiveTransaction
    {
        CookManagedTransactionIntent Intent;
        File IntentFile, Manifest, Receipt;
        ClassifiedFile UpdateManifest;
        Array<ClassifiedFile> Packages;
        bool bStateBeforeFinal = false, bUpdate = false;
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
            !RecheckDirectory(scope.Pending, error))
        {
            return false;
        }
        File header;
        if (!ReadChild(scope.Store, L"header.json", 16384, header, error) || !Same(header.Image, scope.Header.Image))
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
        value.bUpdate = envelope.IsUpdate();
        if (!value.bUpdate && !OnlyKnownPending(scope.Pending, error))
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
        if (value.bUpdate)
        {
            ClassifiedFile state;
            if (!ClassifyFile(scope.Pending, scope.Store, stateLeaf, L"state.before", L"state.after",
                              PresentImage(scope.Store, *envelope.Control(CookManagedControlRole::StateBefore)),
                              *envelope.Control(CookManagedControlRole::StateAfter), MaximumCookStateBytes, state,
                              error))
            {
                return false;
            }
            value.bStateFinal = state.bAfterFinal;
            value.bStateBeforeFinal = state.bBeforeFinal;
            value.Controls[2] = std::move(state.Before);
            value.Controls[3] = std::move(state.After);
        }
        else
        {
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
        }
        auto docs = Documents(value);
        CookManagedRecoveryReadScope readScope;
        if (!BindCookManagedRecoveryReadScope(envelope, docs, readScope, error))
        {
            return false;
        }
        if (value.bUpdate)
        {
            bool bPresent = false;
            if (!KnownDirectory(scope.Workspace, Leaf(readScope.RootLeaf), readScope.Root, bPresent, value.Root,
                                error) ||
                !bPresent ||
                !ClassifyFile(scope.Pending, value.Root, Leaf(readScope.ManifestName), L"manifest.before",
                              L"manifest.after", readScope.ManifestBefore, readScope.ManifestAfter,
                              MaximumCookStateBytes, value.UpdateManifest, error))
            {
                return Fail(error, "update_root_or_manifest");
            }
            value.bRootFinal = true;
            value.Manifest = value.UpdateManifest.After;
        }
        else
        {
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
        bool bTargetsFinal = true;
        if (value.bUpdate)
        {
            if (!KnownUpdatePending(scope.Pending, d, error))
            {
                return false;
            }
            bTargetsFinal = value.UpdateManifest.bAfterFinal;
            for (size_t i = 0; i < d.Packages.size(); ++i)
            {
                const auto& package = d.Packages[i];
                Identity parent;
                std::filesystem::path leaf;
                ClassifiedFile file;
                if (package.Before.File.Size > MaximumPackageBytes || package.After.Size > MaximumPackageBytes ||
                    !ExistingParent(value.Root, package.Package, parent, leaf, error) ||
                    !ClassifyFile(scope.Pending, parent, leaf, PackageSlot(true, i), PackageSlot(false, i),
                                  package.Before, package.After, MaximumPackageBytes, file, error))
                {
                    return Fail(error, "update_package_image");
                }
                bTargetsFinal &= file.bAfterFinal;
                // 復旧に必要なpackage値はID/size/hash。全package payloadを一度に保持しない。
                file.Before.Data.clear();
                file.Before.Data.shrink_to_fit();
                file.After.Data.clear();
                file.After.Data.shrink_to_fit();
                value.Packages.push_back(std::move(file));
            }
        }
        else
        {
            Array<TreeNode> tree;
            if (!ScanTree(value.Root, tree, &d, error))
            {
                return false;
            }
        }
        if (value.bCommitted && (!value.bRootFinal || !value.bStateFinal || !value.bIndexAfterFinal || !bTargetsFinal))
        {
            return Fail(error, "commit_postimage_missing");
        }
        tx = std::move(value);
        return true;
    }
    bool Checkpoint(Operation& op, Point point, size_t ordinal)
    {
        if (!op.Test || !op.Test->Checkpoint)
        {
            return true;
        }
        const auto& runtime = op.RuntimeLocator;
        return op.Test->Checkpoint(point, op.Scope.Pending.Canonical, runtime, ordinal, op.Test->Context) ||
               Fail(op.Error, "checkpoint_failure");
    }
    bool Terminal(const ActiveTransaction& tx)
    {
        if (tx.bUpdate)
        {
            bool bTargets = tx.bCommitted ? tx.UpdateManifest.bAfterFinal : tx.UpdateManifest.bBeforeFinal;
            for (const auto& package : tx.Packages)
            {
                bTargets &= tx.bCommitted ? package.bAfterFinal : package.bBeforeFinal;
            }
            return bTargets && tx.bRootFinal &&
                   (tx.bCommitted ? (tx.bStateFinal && tx.bIndexAfterFinal)
                                  : (tx.bStateBeforeFinal && tx.bIndexBeforeFinal));
        }
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
                if (!node.bDirectory && !DeleteKnown(node.Parent, node.Native.Canonical.filename(), Object(node.Native),
                                                     false, &node.Image, cleanup))
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
                if (node.bDirectory && !DeleteKnown(node.Parent, node.Native.Canonical.filename(), Object(node.Native),
                                                    true, nullptr, cleanup))
                {
                    return false;
                }
            }
            if (!DeleteKnown(op.Scope.Pending, L"root.stage", Object(tx.Root), true, nullptr, cleanup))
            {
                return false;
            }
        }
        if (tx.bUpdate)
        {
            auto clean = [&](const File& file)
            {
                return file.Native.Canonical.parent_path() != op.Scope.Pending.Canonical ||
                       DeleteKnown(op.Scope.Pending, file.Native.Canonical.filename(), file.Image.Object, false,
                                   &file.Image, cleanup);
            };
            for (const auto& package : tx.Packages)
            {
                if (!clean(package.Before) || !clean(package.After))
                {
                    return false;
                }
            }
            if (!clean(tx.UpdateManifest.Before) || !clean(tx.UpdateManifest.After) || !clean(tx.Controls[2]))
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
        op.Outcome.StateGeneration = moved.bCommitted ? moved.Intent.Value().StateGeneration
                                                      : (moved.bUpdate ? moved.Intent.Value().StateGeneration - 1 : 0);
        if (!CleanupRetired(op, moved))
        {
            op.Outcome.bCleanupIncomplete = true;
            op.Outcome.RetiredDirectory = op.Scope.Pending.Canonical;
        }
        op.Error.clear();
        return true;
    }
    bool PublishFile(Operation& op, const Identity& parent, const std::filesystem::path& leaf,
                     const std::filesystem::path& beforeSlot, const std::filesystem::path& afterSlot,
                     const CookManagedBeforeImage& before, const CookManagedFileImage& after, size_t limit,
                     Point backedUp, Point published, size_t ordinal = 0)
    {
        ClassifiedFile current;
        if (!ClassifyFile(op.Scope.Pending, parent, leaf, beforeSlot, afterSlot, before, after, limit, current,
                          op.Error) ||
            !current.bBeforeFinal || current.bAfterFinal)
        {
            return Fail(op.Error, "publish_before_required");
        }
        if (before.bPresent &&
            !Rename(parent, leaf, before.File.Object, op.Scope.Pending, beforeSlot, false, &before.File, op.Error))
        {
            return false;
        }
        if (!Checkpoint(op, backedUp, ordinal) ||
            !Rename(op.Scope.Pending, afterSlot, after.Object, parent, leaf, false, &after, op.Error) ||
            !Checkpoint(op, published, ordinal))
        {
            return false;
        }
        return true;
    }
    bool RestoreFile(Operation& op, const Identity& parent, const std::filesystem::path& leaf,
                     const std::filesystem::path& beforeSlot, const std::filesystem::path& afterSlot,
                     const CookManagedBeforeImage& before, const CookManagedFileImage& after, size_t limit,
                     Point removed, Point restored, size_t ordinal = 0)
    {
        ClassifiedFile current;
        if (!ClassifyFile(op.Scope.Pending, parent, leaf, beforeSlot, afterSlot, before, after, limit, current,
                          op.Error))
        {
            return false;
        }
        if (current.bAfterFinal)
        {
            if (!Rename(parent, leaf, after.Object, op.Scope.Pending, afterSlot, false, &after, op.Error) ||
                !Checkpoint(op, removed, ordinal))
            {
                return false;
            }
        }
        if (before.bPresent && !current.bBeforeFinal)
        {
            if (!Rename(op.Scope.Pending, beforeSlot, before.File.Object, parent, leaf, false, &before.File,
                        op.Error) ||
                !Checkpoint(op, restored, ordinal))
            {
                return false;
            }
        }
        return true;
    }
    bool PublishUpdate(Operation& op)
    {
        ActiveTransaction tx;
        if (!LoadActive(op.Scope, tx, op.Error) || !tx.bUpdate || tx.bCommitted || !Terminal(tx))
        {
            return Fail(op.Error, "update_start_state");
        }
        const auto& d = tx.Intent.Value();
        for (size_t i = 0; i < d.Packages.size(); ++i)
        {
            const auto& target = tx.Packages[i];
            const auto& package = d.Packages[i];
            if (!PublishFile(op, target.Parent, target.LeafName, PackageSlot(true, i), PackageSlot(false, i),
                             package.Before, package.After, MaximumPackageBytes, Point::PackageBackedUp,
                             Point::PackagePublished, i))
            {
                return false;
            }
        }
        if (!PublishFile(op, tx.Root, Leaf(d.Anchor.Binding.ManifestName), L"manifest.before", L"manifest.after",
                         d.ManifestBefore, d.ManifestAfter, MaximumCookStateBytes, Point::ManifestBackedUp,
                         Point::ManifestPublished) ||
            !PublishFile(op, op.Scope.Store, StateLeaf(d.ClaimId), L"state.before", L"state.after",
                         PresentImage(op.Scope.Store, d.Controls[2]), d.Controls[3], MaximumCookStateBytes,
                         Point::StateBackedUp, Point::StatePublished) ||
            !PublishFile(op, op.Scope.Store, L"roots.json", L"index.before", L"index.after",
                         PresentImage(op.Scope.Store, d.Controls[0]), d.Controls[1], MaximumCookStoreIndexBytes,
                         Point::IndexBackedUp, Point::IndexPublished) ||
            !Checkpoint(op, Point::BeforeReceipt))
        {
            return false;
        }
        ActiveTransaction after;
        if (!LoadActive(op.Scope, after, op.Error) || !after.bUpdate || after.bCommitted || !after.bStateFinal ||
            !after.bIndexAfterFinal || !after.UpdateManifest.bAfterFinal)
        {
            return Fail(op.Error, "update_postimage_required");
        }
        for (const auto& package : after.Packages)
        {
            if (!package.bAfterFinal)
            {
                return Fail(op.Error, "update_package_postimage_required");
            }
        }
        const auto image = after.Intent.Value().Receipt;
        if (!Rename(op.Scope.Pending, L"receipt.stage", image.Object, op.Scope.Pending, L"receipt.commit", false,
                    &image, op.Error, &op.bCommitted) ||
            !Checkpoint(op, Point::ReceiptCommitted))
        {
            return false;
        }
        return Retire(op);
    }
    bool RollbackUpdate(Operation& op, const ActiveTransaction& tx)
    {
        const auto& d = tx.Intent.Value();
        if (!RestoreFile(op, op.Scope.Store, L"roots.json", L"index.before", L"index.after",
                         PresentImage(op.Scope.Store, d.Controls[0]), d.Controls[1], MaximumCookStoreIndexBytes,
                         Point::RollbackIndexRemoved, Point::RollbackIndexRestored) ||
            !RestoreFile(op, op.Scope.Store, StateLeaf(d.ClaimId), L"state.before", L"state.after",
                         PresentImage(op.Scope.Store, d.Controls[2]), d.Controls[3], MaximumCookStateBytes,
                         Point::RollbackStateRemoved, Point::RollbackStateRestored) ||
            !RestoreFile(op, tx.Root, Leaf(d.Anchor.Binding.ManifestName), L"manifest.before", L"manifest.after",
                         d.ManifestBefore, d.ManifestAfter, MaximumCookStateBytes, Point::RollbackManifestRemoved,
                         Point::RollbackManifestRestored))
        {
            return false;
        }
        for (size_t i = d.Packages.size(); i > 0; --i)
        {
            const size_t ordinal = i - 1;
            const auto& target = tx.Packages[ordinal];
            const auto& package = d.Packages[ordinal];
            if (!RestoreFile(op, target.Parent, target.LeafName, PackageSlot(true, ordinal),
                             PackageSlot(false, ordinal), package.Before, package.After, MaximumPackageBytes,
                             Point::RollbackPackageRemoved, Point::RollbackPackageRestored, ordinal))
            {
                return false;
            }
        }
        return Retire(op);
    }
    bool Rollback(Operation& op)
    {
        ActiveTransaction tx;
        if (!LoadActive(op.Scope, tx, op.Error) || tx.bCommitted)
        {
            return Fail(op.Error, "rollback_uncommitted_required");
        }
        if (tx.bUpdate)
        {
            return RollbackUpdate(op, tx);
        }
        if (tx.bIndexAfterFinal)
        {
            const auto image = tx.Intent.Value().Controls[1];
            if (!Rename(op.Scope.Store, L"roots.json", image.Object, op.Scope.Pending, L"index.after", false, &image,
                        op.Error) ||
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
            if (!Rename(op.Scope.Pending, L"index.before", image.Object, op.Scope.Store, L"roots.json", false, &image,
                        op.Error) ||
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
    bool ValidatePrepared(Operation& op)
    {
        ActiveTransaction tx;
        return LoadActive(op.Scope, tx, op.Error);
    }
    struct RecoveryOperation
    {
        Operation Transaction;
        RecoveryResult Status = RecoveryResult::Error;
    };
    bool RecoveryCallback(const CookDestinationLockContext& lock, void* data, Text& error)
    {
        auto& work = *static_cast<RecoveryOperation*>(data);
        auto& op = work.Transaction;
        if (op.Test && op.Test->bAbandonedObserved)
        {
            *op.Test->bAbandonedObserved = lock.bAbandoned;
        }
        if (!OpenStore(lock, op.Scope, op.Error))
        {
            work.Status = RecoveryResult::Conflict;
            error = op.Error;
            return false;
        }
        if (!op.Scope.bPending)
        {
            work.Status = RecoveryResult::NoPending;
            error.clear();
            return true;
        }
        op.bPendingPublished = true;
        ActiveTransaction tx;
        if (!LoadActive(op.Scope, tx, op.Error))
        {
            work.Status = RecoveryResult::Conflict;
            error = op.Error;
            return false;
        }
        const bool bCommitted = tx.bCommitted;
        op.bCommitted = bCommitted;
        const bool bSuccess = bCommitted ? Retire(op) : Rollback(op);
        if (bSuccess)
        {
            work.Status = bCommitted ? RecoveryResult::Committed : RecoveryResult::RolledBack;
        }
        error = op.Error;
        return bSuccess;
    }
#endif
    RecoveryResult Recover(const std::filesystem::path& runtime, const Probe* probe, CookManagedBootstrapOutcome& out,
                           Text& error)
    {
        error.clear();
#if !defined(_WIN32)
        (void)runtime;
        (void)probe;
        (void)out;
        Fail(error, "windows_required");
        return RecoveryResult::Error;
#else
        RecoveryOperation work;
        auto& op = work.Transaction;
        op.Test = probe;
        op.RuntimeLocator = runtime;
        try
        {
            const auto result = probe ? Detail::WithCookDestinationLockForTest({runtime}, RecoveryCallback, &work,
                                                                               error, probe->LockFault)
                                      : WithCookDestinationLock({runtime}, RecoveryCallback, &work, error);
            if (result == CookDestinationLockResult::Busy)
            {
                return RecoveryResult::Busy;
            }
            if (result == CookDestinationLockResult::Executed)
            {
                if (work.Status == RecoveryResult::Committed || work.Status == RecoveryResult::RolledBack)
                {
                    out = std::move(op.Outcome);
                }
                error.clear();
                return work.Status;
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
        return work.Status == RecoveryResult::Conflict ? RecoveryResult::Conflict : RecoveryResult::Error;
#endif
    }
} // 名前空間 NorvesLib::Tools::AssetCook::Detail::ManagedTransaction
