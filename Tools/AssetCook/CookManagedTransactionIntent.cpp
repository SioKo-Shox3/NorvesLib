// managed更新の保存値を閉じた契約へ束縛する。I/O・lock・公開はcontrollerの責務。
#include "CookManagedTransactionIntent.h"
#include "CookInventoryValues.h"
#include "CookReferenceValues.h"
#include "CookOutputPaths.h"
#include "ManagedStoreJson.h"
#include "Text/UnicodeText.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Text = Core::Container::AnsiString;
        using View = Core::Container::AnsiStringView;
        template <class T> using Array = Core::Container::VariableArray<T>;
        using ByteView = Core::Container::Span<const uint8_t>;
        using Core::JsonValue;
        namespace J = Detail::ManagedStoreJson;
        namespace K = Detail::CookInventoryValues;
        namespace R = Detail::CookReferenceValues;
        namespace P = Detail::CookOutputPaths;
        constexpr size_t IndexBefore = 0, IndexAfter = 1, StateBefore = 2, StateAfter = 3;
        bool Fail(Text& error, const char* reason)
        {
            error = "cook_transaction_intent: ";
            error.append(reason);
            return false;
        }
        bool IdEqual(const CookManagedObjectId& a, const CookManagedObjectId& b)
        {
            return a.Volume == b.Volume && std::memcmp(a.File.data(), b.File.data(), 16) == 0;
        }
        bool IdValid(const CookManagedObjectId& id)
        {
            return std::any_of(id.File.begin(), id.File.end(),
                               [](uint8_t b)
                               {
                                   return b != 0;
                               });
        }
        bool ImageZero(const CookManagedFileImage& x)
        {
            return !x.Object.Volume && !IdValid(x.Object) && !x.Size && !x.ContentHash;
        }
        bool SameBinding(const CookStateBinding& a, const CookStateBinding& b)
        {
            return a.OwnerId == b.OwnerId && a.RuntimeRootIdentity == b.RuntimeRootIdentity &&
                   a.ManifestName == b.ManifestName;
        }
        bool SameAnchor(const CookManagedStoreAnchor& a, const CookManagedStoreAnchor& b)
        {
            return a.StoreId == b.StoreId && a.RootLeaf == b.RootLeaf && SameBinding(a.Binding, b.Binding) &&
                   IdEqual(a.Workspace, b.Workspace) && IdEqual(a.Store, b.Store) && IdEqual(a.Pending, b.Pending);
        }
        bool SafeRelative(View path)
        {
            return path.size() <= MaximumCookStateStringBytes && P::SafeOutputName(path);
        }
        bool AnchorValid(const CookManagedStoreAnchor& a)
        {
            if (!J::Token(a.StoreId, 32) || !IsValidCookStateBinding(a.Binding) || !SafeRelative(a.RootLeaf) ||
                a.RootLeaf.size() > 255 || std::strchr(a.RootLeaf.c_str(), '/') ||
                std::strchr(a.Binding.ManifestName.c_str(), '/') || !IdValid(a.Workspace) || !IdValid(a.Store) ||
                !IdValid(a.Pending) || a.Workspace.Volume != a.Store.Volume || a.Store.Volume != a.Pending.Volume ||
                IdEqual(a.Workspace, a.Store) || IdEqual(a.Pending, a.Store) || IdEqual(a.Pending, a.Workspace))
            {
                return false;
            }
            // root leafと比較専用bindingの末尾も一致させる。保存pathからfileを開かない。
            const auto& root = a.Binding.RuntimeRootIdentity;
            size_t start = 0;
            for (size_t i = 0; i < root.size(); ++i)
            {
                if (root[i] == '/')
                {
                    start = i + 1;
                }
            }
            if (!P::EqualName({root.data() + start, root.size() - start}, a.RootLeaf))
            {
                return false;
            }
            Text folded;
            for (char c : a.RootLeaf)
            {
                folded.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : c);
            }
            return folded != ".norves-assetcook";
        }
        bool ImageValid(const CookManagedFileImage& x, uint64_t volume)
        {
            return IdValid(x.Object) && x.Object.Volume == volume;
        }
        bool BeforeValid(const CookManagedBeforeImage& x, uint64_t volume)
        {
            return IdValid(x.Parent) && x.Parent.Volume == volume &&
                   (x.bPresent ? ImageValid(x.File, volume) && !IdEqual(x.Parent, x.File.Object) : ImageZero(x.File));
        }
        bool Matches(const CookManagedFileImage& image, const CookManagedDocumentBytes& doc, size_t limit)
        {
            return doc.Bytes.size() <= limit && (doc.Bytes.empty() || doc.Bytes.data()) &&
                   IdEqual(image.Object, doc.ObservedObject) && image.Size == doc.Bytes.size() &&
                   image.ContentHash == Core::Asset::ComputeAssetPackagePayloadHash(doc.Bytes.data(), doc.Bytes.size());
        }
        Text Hex(uint64_t value)
        {
            constexpr char digits[] = "0123456789abcdef";
            Text out;
            for (size_t i = 0; i < 16; ++i)
            {
                out.push_back(digits[(value >> (4 * (15 - i))) & 15]);
            }
            return out;
        }
        Text IdHex(const CookManagedObjectId& id)
        {
            constexpr char digits[] = "0123456789abcdef";
            Text out;
            for (uint8_t b : id.File)
            {
                out.push_back(digits[b >> 4]);
                out.push_back(digits[b & 15]);
            }
            return out;
        }
        void Quote(Text& out, View value)
        {
            out.push_back('"');
            for (char c : value)
            {
                if (c == '"' || c == '\\')
                {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            out.push_back('"');
        }
        void Field(Text& out, const char* name, View value)
        {
            Quote(out, name);
            out.push_back(':');
            Quote(out, value);
        }
        void Object(Text& out, const CookManagedObjectId& x)
        {
            out.push_back('{');
            Field(out, "volume", Hex(x.Volume));
            out.push_back(',');
            Field(out, "id", IdHex(x));
            out.push_back('}');
        }
        void Image(Text& out, const CookManagedFileImage& x)
        {
            out.append("{\"object\":");
            Object(out, x.Object);
            out.push_back(',');
            Field(out, "size", Hex(x.Size));
            out.push_back(',');
            Field(out, "hash", Hex(x.ContentHash));
            out.push_back('}');
        }
        void Before(Text& out, const CookManagedBeforeImage& x)
        {
            out.append("{\"present\":");
            out.append(x.bPresent ? "1" : "0");
            out.append(",\"parent\":");
            Object(out, x.Parent);
            out.append(",\"file\":");
            Image(out, x.File);
            out.push_back('}');
        }
        bool ReadObject(const JsonValue& v, CookManagedObjectId& x)
        {
            Text id;
            if (!J::Shape(v, {"volume", "id"}) || !J::ReadU64(v.FindMember("volume"), x.Volume, false) ||
                !J::Hex(v.FindMember("id"), 32, id, false))
            {
                return false;
            }
            for (size_t i = 0; i < 16; ++i)
            {
                x.File[i] = static_cast<uint8_t>((J::Digit(id[i * 2]) << 4) | J::Digit(id[i * 2 + 1]));
            }
            return true;
        }
        bool ReadImage(const JsonValue& v, CookManagedFileImage& x)
        {
            return J::Shape(v, {"object", "size", "hash"}) && ReadObject(v.FindMember("object"), x.Object) &&
                   J::ReadU64(v.FindMember("size"), x.Size, false) &&
                   J::ReadU64(v.FindMember("hash"), x.ContentHash, false);
        }
        bool ReadBefore(const JsonValue& v, CookManagedBeforeImage& x)
        {
            auto present = v.FindMember("present");
            if (!J::Shape(v, {"present", "parent", "file"}) || !present.IsIntegerLiteral() ||
                (present.AsNumber() != 0 && present.AsNumber() != 1))
            {
                return false;
            }
            x.bPresent = present.AsNumber() == 1;
            return ReadObject(v.FindMember("parent"), x.Parent) && ReadImage(v.FindMember("file"), x.File);
        }
        bool Bounded(ByteView bytes, size_t limit)
        {
            if (bytes.empty() || !bytes.data() || bytes.size() > limit)
            {
                return false;
            }
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
                    ++tokens;
                }
                else if (c == '{' || c == '[')
                {
                    if (++depth > 8)
                    {
                        return false;
                    }
                    ++tokens;
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
                    ++tokens;
                }
                if (tokens > 2000000)
                {
                    return false;
                }
            }
            return !bString && !depth;
        }
        bool DraftShape(const CookManagedIntentDraft& d, Text& error)
        {
            const auto volume = d.Anchor.Store.Volume;
            if (!AnchorValid(d.Anchor) || !J::Token(d.TransactionId, 32) || !J::Token(d.ClaimId, 32) ||
                (d.Mode != CookManagedIntentMode::Bootstrap && d.Mode != CookManagedIntentMode::Update) ||
                !IdValid(d.Root) || d.Root.Volume != volume || !d.IndexGeneration || !d.StateGeneration ||
                IdEqual(d.Root, d.Anchor.Workspace) || IdEqual(d.Root, d.Anchor.Store) ||
                IdEqual(d.Root, d.Anchor.Pending) || d.Packages.size() > MaximumCookStateOutputs ||
                d.Directories.size() > MaximumCookManagedTreeEntries ||
                d.Directories.size() + d.Packages.size() + 1 > MaximumCookManagedTreeEntries ||
                (d.Mode == CookManagedIntentMode::Update && !d.Directories.empty()))
            {
                return Fail(error, "scope_or_count");
            }
            size_t budget = sizeof(d);
            for (const auto* s :
                 {&d.TransactionId, &d.ClaimId, &d.Anchor.StoreId, &d.Anchor.RootLeaf, &d.Anchor.Binding.OwnerId,
                  &d.Anchor.Binding.RuntimeRootIdentity, &d.Anchor.Binding.ManifestName})
            {
                budget += s->size();
            }
            Array<CookManagedObjectId> objects;
            objects.push_back(d.Anchor.Workspace);
            objects.push_back(d.Anchor.Store);
            objects.push_back(d.Anchor.Pending);
            objects.push_back(d.Root);
            for (size_t i = 0; i < 4; ++i)
            {
                const auto& image = d.Controls[i];
                if (i == StateBefore && d.Mode == CookManagedIntentMode::Bootstrap)
                {
                    if (!ImageZero(image))
                    {
                        return Fail(error, "bootstrap_state_present");
                    }
                    continue;
                }
                const size_t limit = i < 2 ? MaximumCookStoreIndexBytes : MaximumCookStateBytes;
                if (!ImageValid(image, volume) || !image.Size || image.Size > limit)
                {
                    return Fail(error, "control_image");
                }
                objects.push_back(image.Object);
            }
            if (!BeforeValid(d.ManifestBefore, volume) || !IdEqual(d.ManifestBefore.Parent, d.Root) ||
                !ImageValid(d.ManifestAfter, volume) || !d.ManifestAfter.Size ||
                d.ManifestAfter.Size > MaximumCookStateBytes || !ImageValid(d.Receipt, volume) || !d.Receipt.Size ||
                d.Receipt.Size > 16384 || (d.Mode == CookManagedIntentMode::Bootstrap && d.ManifestBefore.bPresent))
            {
                return Fail(error, "manifest_or_receipt");
            }
            objects.push_back(d.ManifestAfter.Object);
            objects.push_back(d.Receipt.Object);
            if (d.ManifestBefore.bPresent)
            {
                objects.push_back(d.ManifestBefore.File.Object);
            }
            Array<Text> names;
            for (const auto& row : d.Packages)
            {
                if (!SafeRelative(row.Package) || !BeforeValid(row.Before, volume) || !ImageValid(row.After, volume) ||
                    !row.After.Size || (d.Mode == CookManagedIntentMode::Bootstrap && row.Before.bPresent))
                {
                    return Fail(error, "package_image");
                }
                budget += sizeof(row) + row.Package.size();
                if (budget > MaximumCookManagedIntentBytes)
                {
                    return Fail(error, "metadata_limit");
                }
                names.push_back(row.Package);
                objects.push_back(row.After.Object);
                if (row.Before.bPresent)
                {
                    objects.push_back(row.Before.File.Object);
                }
            }
            for (const auto& row : d.Directories)
            {
                if (!SafeRelative(row.Relative) || !IdValid(row.Object) || !IdValid(row.Parent) ||
                    row.Object.Volume != volume || row.Parent.Volume != volume)
                {
                    return Fail(error, "tree_directory");
                }
                budget += sizeof(row) + row.Relative.size();
                objects.push_back(row.Object);
            }
            if (budget > MaximumCookManagedIntentBytes)
            {
                return Fail(error, "metadata_limit");
            }
            std::sort(objects.begin(), objects.end(),
                      [](const auto& a, const auto& b)
                      {
                          return std::memcmp(a.File.data(), b.File.data(), 16) < 0;
                      });
            for (size_t i = 1; i < objects.size(); ++i)
            {
                if (IdEqual(objects[i - 1], objects[i]))
                {
                    return Fail(error, "duplicate_object");
                }
            }
            std::sort(names.begin(), names.end(),
                      [](const auto& a, const auto& b)
                      {
                          return K::Compare(a, b) < 0;
                      });
            for (size_t i = 1; i < names.size(); ++i)
            {
                if (names[i - 1] == names[i])
                {
                    return Fail(error, "duplicate_package");
                }
            }
            return true;
        }
        void WriteAnchor(Text& json, const CookManagedStoreAnchor& a)
        {
            json.push_back('{');
            Field(json, "store_id", a.StoreId);
            json.push_back(',');
            Field(json, "root_leaf", a.RootLeaf);
            json.append(",\"workspace\":");
            Object(json, a.Workspace);
            json.append(",\"store\":");
            Object(json, a.Store);
            json.append(",\"pending\":");
            Object(json, a.Pending);
            json.push_back(',');
            Field(json, "owner", a.Binding.OwnerId);
            json.push_back(',');
            Field(json, "root", a.Binding.RuntimeRootIdentity);
            json.push_back(',');
            Field(json, "manifest", a.Binding.ManifestName);
            json.push_back('}');
        }
        bool ReadAnchor(const JsonValue& v, CookManagedStoreAnchor& a)
        {
            return J::Shape(v,
                            {"store_id", "root_leaf", "workspace", "store", "pending", "owner", "root", "manifest"}) &&
                   J::String(v.FindMember("store_id"), a.StoreId) && J::String(v.FindMember("root_leaf"), a.RootLeaf) &&
                   ReadObject(v.FindMember("workspace"), a.Workspace) && ReadObject(v.FindMember("store"), a.Store) &&
                   ReadObject(v.FindMember("pending"), a.Pending) &&
                   J::String(v.FindMember("owner"), a.Binding.OwnerId) &&
                   J::String(v.FindMember("root"), a.Binding.RuntimeRootIdentity, MaximumCookStateStringBytes) &&
                   J::String(v.FindMember("manifest"), a.Binding.ManifestName, MaximumCookStateStringBytes);
        }
        bool Decode(ByteView bytes, CookManagedIntentDraft& d, Text& error)
        {
            Core::JsonDocument document;
            if (!Bounded(bytes, MaximumCookManagedIntentBytes) || !Core::JsonDocument::TryParseUtf8(bytes, document))
            {
                return Fail(error, "wire_limit_or_json");
            }
            const auto v = document.GetRoot();
            Text mode;
            if (!J::Shape(v, {"schema", "mode", "anchor", "transaction", "claim", "root", "index_generation",
                              "state_generation", "controls", "manifest_before", "manifest_after", "receipt",
                              "packages", "directories"}) ||
                !J::Version(v.FindMember("schema")) || !J::String(v.FindMember("mode"), mode) ||
                !ReadAnchor(v.FindMember("anchor"), d.Anchor) ||
                !J::String(v.FindMember("transaction"), d.TransactionId) ||
                !J::String(v.FindMember("claim"), d.ClaimId) || !ReadObject(v.FindMember("root"), d.Root) ||
                !J::ReadU64(v.FindMember("index_generation"), d.IndexGeneration, true) ||
                !J::ReadU64(v.FindMember("state_generation"), d.StateGeneration, true) ||
                !ReadBefore(v.FindMember("manifest_before"), d.ManifestBefore) ||
                !ReadImage(v.FindMember("manifest_after"), d.ManifestAfter) ||
                !ReadImage(v.FindMember("receipt"), d.Receipt))
            {
                return Fail(error, "wire_header");
            }
            if (mode == "bootstrap")
            {
                d.Mode = CookManagedIntentMode::Bootstrap;
            }
            else if (mode == "update")
            {
                d.Mode = CookManagedIntentMode::Update;
            }
            else
            {
                return Fail(error, "wire_mode");
            }
            const auto controls = v.FindMember("controls"), packages = v.FindMember("packages"),
                       directories = v.FindMember("directories");
            if (!controls.IsArray() || controls.GetArraySize() != 4 || !packages.IsArray() ||
                packages.GetArraySize() > MaximumCookStateOutputs || !directories.IsArray() ||
                directories.GetArraySize() > MaximumCookManagedTreeEntries)
            {
                return Fail(error, "wire_count");
            }
            for (size_t i = 0; i < 4; ++i)
            {
                if (!ReadImage(controls.GetArrayElement(i), d.Controls[i]))
                {
                    return Fail(error, "wire_control");
                }
            }
            for (size_t i = 0; i < packages.GetArraySize(); ++i)
            {
                const auto row = packages.GetArrayElement(i);
                CookManagedPackageMutation x;
                if (!J::Shape(row, {"package", "before", "after"}) ||
                    !J::String(row.FindMember("package"), x.Package, MaximumCookStateStringBytes) ||
                    !ReadBefore(row.FindMember("before"), x.Before) || !ReadImage(row.FindMember("after"), x.After))
                {
                    return Fail(error, "wire_package");
                }
                d.Packages.push_back(std::move(x));
            }
            for (size_t i = 0; i < directories.GetArraySize(); ++i)
            {
                const auto row = directories.GetArrayElement(i);
                CookManagedTreeDirectory x;
                if (!J::Shape(row, {"relative", "object", "parent"}) ||
                    !J::String(row.FindMember("relative"), x.Relative, MaximumCookStateStringBytes) ||
                    !ReadObject(row.FindMember("object"), x.Object) || !ReadObject(row.FindMember("parent"), x.Parent))
                {
                    return Fail(error, "wire_directory");
                }
                d.Directories.push_back(std::move(x));
            }
            return DraftShape(d, error);
        }
        bool Encode(const CookManagedIntentDraft& d, Text& json, Text& error)
        {
            if (!DraftShape(d, error))
            {
                return false;
            }
            json = "{\"schema\":1,";
            Field(json, "mode", d.Mode == CookManagedIntentMode::Bootstrap ? "bootstrap" : "update");
            json.append(",\"anchor\":");
            WriteAnchor(json, d.Anchor);
            json.push_back(',');
            Field(json, "transaction", d.TransactionId);
            json.push_back(',');
            Field(json, "claim", d.ClaimId);
            json.append(",\"root\":");
            Object(json, d.Root);
            json.push_back(',');
            Field(json, "index_generation", Hex(d.IndexGeneration));
            json.push_back(',');
            Field(json, "state_generation", Hex(d.StateGeneration));
            json.append(",\"controls\":[");
            for (size_t i = 0; i < 4; ++i)
            {
                if (i)
                {
                    json.push_back(',');
                }
                Image(json, d.Controls[i]);
            }
            json.append("],\"manifest_before\":");
            Before(json, d.ManifestBefore);
            json.append(",\"manifest_after\":");
            Image(json, d.ManifestAfter);
            json.append(",\"receipt\":");
            Image(json, d.Receipt);
            json.append(",\"packages\":[");
            for (size_t i = 0; i < d.Packages.size(); ++i)
            {
                const auto& row = d.Packages[i];
                if (i)
                {
                    json.push_back(',');
                }
                json.push_back('{');
                Field(json, "package", row.Package);
                json.append(",\"before\":");
                Before(json, row.Before);
                json.append(",\"after\":");
                Image(json, row.After);
                json.push_back('}');
                if (json.size() > MaximumCookManagedIntentBytes)
                {
                    return Fail(error, "wire_limit");
                }
            }
            json.append("],\"directories\":[");
            for (size_t i = 0; i < d.Directories.size(); ++i)
            {
                const auto& row = d.Directories[i];
                if (i)
                {
                    json.push_back(',');
                }
                json.push_back('{');
                Field(json, "relative", row.Relative);
                json.append(",\"object\":");
                Object(json, row.Object);
                json.append(",\"parent\":");
                Object(json, row.Parent);
                json.push_back('}');
                if (json.size() > MaximumCookManagedIntentBytes)
                {
                    return Fail(error, "wire_limit");
                }
            }
            json.append("]}");
            return json.size() <= MaximumCookManagedIntentBytes || Fail(error, "wire_limit");
        }
        bool SameClaim(const CookManagedRootClaim& a, const CookManagedRootClaim& b)
        {
            return a.ClaimId == b.ClaimId && a.RootLeaf == b.RootLeaf && a.OwnerId == b.OwnerId &&
                   std::memcmp(a.DirectoryId.data(), b.DirectoryId.data(), 16) == 0;
        }
        bool CheckIndex(const CookManagedIntentDraft& d, const CookManagedStoreIndex& before,
                        const CookManagedStoreIndex& after, Text& error)
        {
            const bool bBootstrap = d.Mode == CookManagedIntentMode::Bootstrap;
            if (before.Generation == UINT64_MAX || after.Generation != before.Generation + 1 ||
                after.Generation != d.IndexGeneration ||
                after.Roots.size() != before.Roots.size() + (bBootstrap ? 1 : 0))
            {
                return Fail(error, "index_generation_or_inventory");
            }
            Array<size_t> oldOrder, newOrder;
            for (size_t i = 0; i < before.Roots.size(); ++i)
            {
                oldOrder.push_back(i);
            }
            for (size_t i = 0; i < after.Roots.size(); ++i)
            {
                newOrder.push_back(i);
            }
            std::sort(oldOrder.begin(), oldOrder.end(),
                      [&](size_t a, size_t b)
                      {
                          return K::Compare(before.Roots[a].ClaimId, before.Roots[b].ClaimId) < 0;
                      });
            std::sort(newOrder.begin(), newOrder.end(),
                      [&](size_t a, size_t b)
                      {
                          return K::Compare(after.Roots[a].ClaimId, after.Roots[b].ClaimId) < 0;
                      });
            size_t cursor = 0, found = 0;
            for (size_t index : newOrder)
            {
                const auto& row = after.Roots[index];
                if (row.ClaimId == d.ClaimId)
                {
                    ++found;
                    if (row.RootLeaf != d.Anchor.RootLeaf || row.OwnerId != d.Anchor.Binding.OwnerId ||
                        std::memcmp(row.DirectoryId.data(), d.Root.File.data(), 16) != 0)
                    {
                        return Fail(error, "claim_scope");
                    }
                    if (bBootstrap)
                    {
                        continue;
                    }
                }
                if (cursor == oldOrder.size() || !SameClaim(before.Roots[oldOrder[cursor++]], row))
                {
                    return Fail(error, "unrelated_claim_changed");
                }
            }
            return (found == 1 && cursor == oldOrder.size()) || Fail(error, "claim_missing_or_adopted");
        }
        bool ReadManifest(ByteView bytes, Core::Asset::AssetManifest& out)
        {
            if (!Bounded(bytes, MaximumCookStateBytes))
            {
                return false;
            }
            Core::Container::String wide;
            const bool bValid = Core::TextDetail::ForEachUnicodeScalar<char>(
                {reinterpret_cast<const char*>(bytes.data()), bytes.size()},
                [&](uint32_t scalar)
                {
                    if constexpr (sizeof(wchar_t) == 2)
                    {
                        if (scalar > 0xffff)
                        {
                            scalar -= 0x10000;
                            wide.push_back(static_cast<wchar_t>(0xd800 + (scalar >> 10)));
                            wide.push_back(static_cast<wchar_t>(0xdc00 + (scalar & 1023)));
                            return;
                        }
                    }
                    wide.push_back(static_cast<wchar_t>(scalar));
                });
            return bValid && out.LoadFromJsonText(wide);
        }
        bool SameRecord(const CookOutputRecord& a, const CookOutputRecord& b)
        {
            if (a.SchemaVersion != b.SchemaVersion || a.DependencySchemaVersion != b.DependencySchemaVersion ||
                a.CookerRevision != b.CookerRevision || a.DependencyFingerprint != b.DependencyFingerprint ||
                a.Outputs.size() != b.Outputs.size())
            {
                return false;
            }
            Array<size_t> x, y;
            for (size_t i = 0; i < a.Outputs.size(); ++i)
            {
                x.push_back(i);
                y.push_back(i);
            }
            std::sort(x.begin(), x.end(),
                      [&](size_t i, size_t j)
                      {
                          return K::Compare(K::View(a.Outputs[i].Reference), K::View(a.Outputs[j].Reference)) < 0;
                      });
            std::sort(y.begin(), y.end(),
                      [&](size_t i, size_t j)
                      {
                          return K::Compare(K::View(b.Outputs[i].Reference), K::View(b.Outputs[j].Reference)) < 0;
                      });
            for (size_t i = 0; i < x.size(); ++i)
            {
                const auto& p = a.Outputs[x[i]];
                const auto& q = b.Outputs[y[i]];
                if (!R::SameReference(p.Reference, q.Reference) || p.Package.Size != q.Package.Size ||
                    p.Package.ContentHash != q.Package.ContentHash)
                {
                    return false;
                }
            }
            return true;
        }
        Text ParentName(View name)
        {
            size_t last = 0;
            for (size_t i = 0; i < name.size(); ++i)
            {
                if (name[i] == '/')
                {
                    last = i;
                }
            }
            Text out;
            if (last)
            {
                out.append(name.data(), last);
            }
            return out;
        }
        struct ParentEvidence
        {
            Text Name;
            CookManagedObjectId Object;
        };
        bool CheckParents(const CookManagedIntentDraft& d, Text& error)
        {
            Array<ParentEvidence> required;
            for (const auto& row : d.Packages)
            {
                Text parent = ParentName(row.Package);
                if (parent.empty())
                {
                    if (!IdEqual(row.Before.Parent, d.Root))
                    {
                        return Fail(error, "flat_parent_not_root");
                    }
                }
                else
                {
                    if (IdEqual(row.Before.Parent, d.Root) || IdEqual(row.Before.Parent, d.Anchor.Store) ||
                        IdEqual(row.Before.Parent, d.Anchor.Workspace) || IdEqual(row.Before.Parent, d.Anchor.Pending))
                    {
                        return Fail(error, "nested_parent_scope");
                    }
                    required.push_back({std::move(parent), row.Before.Parent});
                }
            }
            std::sort(required.begin(), required.end(),
                      [](const auto& a, const auto& b)
                      {
                          return K::Compare(a.Name, b.Name) < 0;
                      });
            Array<ParentEvidence> unique;
            for (const auto& row : required)
            {
                if (!unique.empty() && unique.back().Name == row.Name)
                {
                    if (!IdEqual(unique.back().Object, row.Object))
                    {
                        return Fail(error, "parent_identity_conflict");
                    }
                }
                else
                {
                    unique.push_back(row);
                }
            }
            // 同じparentを異なる相対名で採用しない。aliasの実観測はcontrollerが再確認する。
            Array<CookManagedObjectId> parentIds;
            for (const auto& row : unique)
            {
                parentIds.push_back(row.Object);
            }
            std::sort(parentIds.begin(), parentIds.end(),
                      [](const auto& a, const auto& b)
                      {
                          return std::memcmp(a.File.data(), b.File.data(), 16) < 0;
                      });
            for (size_t i = 1; i < parentIds.size(); ++i)
            {
                if (IdEqual(parentIds[i - 1], parentIds[i]))
                {
                    return Fail(error, "parent_alias");
                }
            }
            const auto bFileParent = [&](const CookManagedObjectId& id)
            {
                return std::binary_search(parentIds.begin(), parentIds.end(), id,
                                          [](const auto& a, const auto& b)
                                          {
                                              return std::memcmp(a.File.data(), b.File.data(), 16) < 0;
                                          });
            };
            for (const auto& image : d.Controls)
            {
                if (IdValid(image.Object) && bFileParent(image.Object))
                {
                    return Fail(error, "file_as_parent");
                }
            }
            for (const auto* image : {&d.ManifestAfter, &d.ManifestBefore.File, &d.Receipt})
            {
                if (IdValid(image->Object) && bFileParent(image->Object))
                {
                    return Fail(error, "file_as_parent");
                }
            }
            for (const auto& row : d.Packages)
            {
                if (bFileParent(row.After.Object) || (row.Before.bPresent && bFileParent(row.Before.File.Object)))
                {
                    return Fail(error, "file_as_parent");
                }
            }
            if (d.Mode == CookManagedIntentMode::Update)
            {
                return true;
            }
            // 既知directoryを索引化し、各親edgeを一度だけ保持する。
            // packageごとの全ancestor展開は長い共通prefixで増幅するため行わない。
            Array<size_t> order, parents;
            Array<uint8_t> requiredFlags;
            requiredFlags.resize(d.Directories.size(), uint8_t{0});
            parents.resize(d.Directories.size(), SIZE_MAX);
            for (size_t i = 0; i < d.Directories.size(); ++i)
            {
                order.push_back(i);
            }
            std::sort(order.begin(), order.end(),
                      [&](size_t a, size_t b)
                      {
                          return K::Compare(d.Directories[a].Relative, d.Directories[b].Relative) < 0;
                      });
            const auto find = [&](View name) -> size_t
            {
                const auto it = std::lower_bound(order.begin(), order.end(), name,
                                                 [&](size_t i, View n)
                                                 {
                                                     return K::Compare(d.Directories[i].Relative, n) < 0;
                                                 });
                return it != order.end() && P::EqualName(d.Directories[*it].Relative, name) ? *it : SIZE_MAX;
            };
            for (size_t i = 0; i < order.size(); ++i)
            {
                const auto index = order[i];
                const auto& dir = d.Directories[index];
                if (i && d.Directories[order[i - 1]].Relative == dir.Relative)
                {
                    return Fail(error, "duplicate_directory_name");
                }
                const auto parent = ParentName(dir.Relative);
                const auto parentIndex = parent.empty() ? SIZE_MAX : find(parent);
                if ((!parent.empty() && parentIndex == SIZE_MAX) ||
                    !IdEqual(dir.Parent, parentIndex == SIZE_MAX ? d.Root : d.Directories[parentIndex].Object))
                {
                    return Fail(error, "bootstrap_directory_parent");
                }
                parents[index] = parentIndex;
            }
            size_t requiredCount = 0;
            for (const auto& row : unique)
            {
                size_t index = find(row.Name);
                if (index == SIZE_MAX || !IdEqual(d.Directories[index].Object, row.Object))
                {
                    return Fail(error, "bootstrap_package_parent");
                }
                while (index != SIZE_MAX && !requiredFlags[index])
                {
                    requiredFlags[index] = 1;
                    ++requiredCount;
                    index = parents[index];
                }
            }
            return requiredCount == d.Directories.size() || Fail(error, "bootstrap_extra_directory");
        }
        class RecordIndex
        {
          public:
            explicit RecordIndex(const CookOwnedState* state)
            {
                if (state)
                {
                    for (const auto& row : state->Records)
                    {
                        m_Rows.push_back(&row);
                    }
                    std::sort(m_Rows.begin(), m_Rows.end(),
                              [](const auto* a, const auto* b)
                              {
                                  return K::Compare(K::View(a->PrimaryKey), K::View(b->PrimaryKey)) < 0;
                              });
                }
            }
            const CookOutputRecord* Find(const CookStateKey& key) const
            {
                const auto view = K::View(key);
                const auto it = std::lower_bound(m_Rows.begin(), m_Rows.end(), view,
                                                 [](const auto* row, K::Key target)
                                                 {
                                                     return K::Compare(K::View(row->PrimaryKey), target) < 0;
                                                 });
                return it != m_Rows.end() && K::Compare(K::View((*it)->PrimaryKey), view) == 0 ? &(*it)->Record
                                                                                               : nullptr;
            }

          private:
            Array<const CookOwnedRecord*> m_Rows;
        };
        bool CheckState(const CookManagedIntentDraft& d, const CookOwnedState* before, const CookOwnedState& after,
                        const Core::Asset::AssetManifest& manifest, Text& error)
        {
            if (after.Generation != d.StateGeneration ||
                (before ? (before->Generation == UINT64_MAX || after.Generation != before->Generation + 1)
                        : after.Generation != 1))
            {
                return Fail(error, "state_generation");
            }
            if (before && before->Records.size() != after.Records.size())
            {
                return Fail(error, "record_inventory_changed");
            }
            Array<const CookRecordedOutput*> all;
            Array<size_t> packageOrder;
            for (size_t i = 0; i < d.Packages.size(); ++i)
            {
                packageOrder.push_back(i);
            }
            std::sort(packageOrder.begin(), packageOrder.end(),
                      [&](size_t a, size_t b)
                      {
                          return K::Compare(d.Packages[a].Package, d.Packages[b].Package) < 0;
                      });
            RecordIndex oldRecords(before);
            size_t matched = 0;
            for (const auto& record : after.Records)
            {
                const auto* old = oldRecords.Find(record.PrimaryKey);
                if (before && (!old || old->Outputs.size() != record.Record.Outputs.size()))
                {
                    return Fail(error, "primary_or_output_inventory_changed");
                }
                Array<const CookRecordedOutput*> oldOutputs;
                if (old)
                {
                    for (const auto& output : old->Outputs)
                    {
                        oldOutputs.push_back(&output);
                    }
                    std::sort(oldOutputs.begin(), oldOutputs.end(),
                              [](const auto* a, const auto* b)
                              {
                                  return K::Compare(K::View(a->Reference), K::View(b->Reference)) < 0;
                              });
                }
                size_t mutations = 0;
                for (const auto& output : record.Record.Outputs)
                {
                    all.push_back(&output);
                    if (old)
                    {
                        auto it = std::lower_bound(oldOutputs.begin(), oldOutputs.end(), K::View(output.Reference),
                                                   [](const auto* a, K::Key key)
                                                   {
                                                       return K::Compare(K::View(a->Reference), key) < 0;
                                                   });
                        if (it == oldOutputs.end() || !K::SameFixedOutput((*it)->Reference, output.Reference))
                        {
                            return Fail(error, "output_key_or_package_changed");
                        }
                    }
                    auto it =
                        std::lower_bound(packageOrder.begin(), packageOrder.end(), View(output.Reference.CookedPackage),
                                         [&](size_t i, View name)
                                         {
                                             return K::Compare(d.Packages[i].Package, name) < 0;
                                         });
                    if (it != packageOrder.end() && d.Packages[*it].Package == output.Reference.CookedPackage)
                    {
                        const auto& image = d.Packages[*it].After;
                        if (image.Size != output.Package.Size || image.ContentHash != output.Package.ContentHash)
                        {
                            return Fail(error, "after_package_fingerprint");
                        }
                        ++mutations;
                        ++matched;
                    }
                }
                if ((mutations && mutations != record.Record.Outputs.size()) ||
                    (!before && mutations != record.Record.Outputs.size()))
                {
                    return Fail(error, "partial_asset_mutation");
                }
                if (!mutations && old && !SameRecord(*old, record.Record))
                {
                    return Fail(error, "skip_record_changed");
                }
            }
            if (matched != d.Packages.size() || manifest.GetReferenceCount() != all.size())
            {
                return Fail(error, "unlisted_package_or_manifest_count");
            }
            std::sort(all.begin(), all.end(),
                      [](const auto* a, const auto* b)
                      {
                          return K::Compare(K::View(a->Reference), K::View(b->Reference)) < 0;
                      });
            Array<size_t> manifestOrder;
            for (size_t i = 0; i < manifest.GetReferenceCount(); ++i)
            {
                manifestOrder.push_back(i);
            }
            std::sort(manifestOrder.begin(), manifestOrder.end(),
                      [&](size_t a, size_t b)
                      {
                          return K::Compare(K::View(manifest.GetReference(a)), K::View(manifest.GetReference(b))) < 0;
                      });
            for (size_t i = 0; i < all.size(); ++i)
            {
                if (!R::SameReference(all[i]->Reference, manifest.GetReference(manifestOrder[i])))
                {
                    return Fail(error, "manifest_reference_mismatch");
                }
            }
            return CheckParents(d, error);
        }
        struct ParsedDocuments
        {
            CookManagedStoreIndex BeforeIndex, AfterIndex;
            CookOwnedState BeforeState, AfterState;
            Core::Asset::AssetManifest Manifest;
        };
        bool Validate(const CookManagedIntentDraft& d, const CookManagedControlDocuments& docs, ParsedDocuments& parsed,
                      Text& error)
        {
            if (!DraftShape(d, error))
            {
                return false;
            }
            for (size_t i = 0; i < 4; ++i)
            {
                const auto& doc = docs.Controls[i];
                if (i == StateBefore && d.Mode == CookManagedIntentMode::Bootstrap)
                {
                    if (!doc.Bytes.empty() || IdValid(doc.ObservedObject) || doc.ObservedObject.Volume)
                    {
                        return Fail(error, "unexpected_before_state");
                    }
                    continue;
                }
                if (!Matches(d.Controls[i], doc, i < 2 ? MaximumCookStoreIndexBytes : MaximumCookStateBytes))
                {
                    return Fail(error, "control_bytes_or_identity");
                }
            }
            if (!Matches(d.ManifestAfter, docs.ManifestAfter, MaximumCookStateBytes))
            {
                return Fail(error, "manifest_bytes_or_identity");
            }
            if (!ParseCookManagedStoreIndex(docs.Controls[IndexBefore].Bytes, d.Anchor.StoreId, MaximumCookStoreRoots,
                                            parsed.BeforeIndex, error) ||
                !ParseCookManagedStoreIndex(docs.Controls[IndexAfter].Bytes, d.Anchor.StoreId, MaximumCookStoreRoots,
                                            parsed.AfterIndex, error) ||
                !ParseCookOwnedState(docs.Controls[StateAfter].Bytes, d.Anchor.Binding, parsed.AfterState, error))
            {
                return false;
            }
            if (d.Mode == CookManagedIntentMode::Update &&
                !ParseCookOwnedState(docs.Controls[StateBefore].Bytes, d.Anchor.Binding, parsed.BeforeState, error))
            {
                return false;
            }
            if (!CheckIndex(d, parsed.BeforeIndex, parsed.AfterIndex, error))
            {
                return false;
            }
            if (!ReadManifest(docs.ManifestAfter.Bytes, parsed.Manifest))
            {
                return Fail(error, "manifest_semantics");
            }
            Text receipt;
            if (!MakeCookManagedReceiptBody(d, receipt, error) || d.Receipt.Size != receipt.size() ||
                d.Receipt.ContentHash != Core::Asset::ComputeAssetPackagePayloadHash(
                                             reinterpret_cast<const uint8_t*>(receipt.data()), receipt.size()))
            {
                return Fail(error, "receipt_body");
            }
            return CheckState(d, d.Mode == CookManagedIntentMode::Update ? &parsed.BeforeState : nullptr,
                              parsed.AfterState, parsed.Manifest, error);
        }
        bool CheckLive(const CookManagedIntentBuildInput& input, const ParsedDocuments& parsed, Text& error)
        {
            const auto& state = parsed.AfterState;
            if (input.FinalPlans.size() != state.Records.size() || input.FinalRecords.size() != state.Records.size() ||
                (input.FinalPlans.size() && !input.FinalPlans.data()) ||
                (input.FinalRecords.size() && !input.FinalRecords.data()))
            {
                return Fail(error, "live_count");
            }
            // callerのcapture/Skip値を既存codecで検査する。wire beforeのbyteは置換しない。
            size_t liveOutputs = 0, liveBytes = 0;
            for (const auto& row : input.FinalRecords)
            {
                if (row.Record.Outputs.size() > MaximumCookStateOutputs - liveOutputs)
                {
                    return Fail(error, "live_output_limit");
                }
                liveOutputs += row.Record.Outputs.size();
                for (const auto* value : {&row.PrimaryKey.LogicalPath, &row.PrimaryKey.Variant})
                {
                    if (value->size() > MaximumCookStateStringBytes)
                    {
                        return Fail(error, "live_string_limit");
                    }
                    liveBytes += value->size();
                }
                for (const auto& output : row.Record.Outputs)
                {
                    const auto& ref = output.Reference;
                    for (const auto* value :
                         {&ref.LogicalPath, &ref.Variant, &ref.SourceHashHex, &ref.Format, &ref.CookedPackage,
                          &ref.EntryName, &ref.EntryTypeText, &ref.CookedHashHex})
                    {
                        if (value->size() > MaximumCookStateStringBytes)
                        {
                            return Fail(error, "live_string_limit");
                        }
                        liveBytes += value->size();
                    }
                }
                if (liveBytes > MaximumCookManagedIntentBytes)
                {
                    return Fail(error, "live_metadata_limit");
                }
            }
            CookOwnedState live;
            live.Binding = state.Binding;
            live.Generation = state.Generation;
            if (!input.FinalRecords.empty())
            {
                live.Records.assign(input.FinalRecords.begin(), input.FinalRecords.end());
            }
            Text checked;
            if (!SerializeCookOwnedState(live, checked, error))
            {
                return false;
            }
            RecordIndex liveRecords(&live), stateRecords(&state);
            for (const auto& row : state.Records)
            {
                const auto* record = liveRecords.Find(row.PrimaryKey);
                if (!record || !SameRecord(*record, row.Record))
                {
                    return Fail(error, "captured_record_mismatch");
                }
            }
            Array<K::Key> keys;
            for (const auto& plan : input.FinalPlans)
            {
                if (plan.Outputs.empty() || plan.Outputs.size() > MaximumCookStateOutputs)
                {
                    return Fail(error, "live_plan_inventory");
                }
                const auto& primary = plan.Outputs[0].ExpectedIdentity;
                CookStateKey key;
                key.Kind = primary.Kind;
                key.LogicalPath = primary.LogicalPath;
                key.Variant = primary.Variant;
                keys.push_back(K::View(primary));
                const auto* record = stateRecords.Find(key);
                if (!record || record->Outputs.size() != plan.Outputs.size() ||
                    record->CookerRevision != plan.Context.Dependencies.CookerRevision ||
                    record->DependencySchemaVersion != plan.Context.Dependencies.SchemaVersion ||
                    record->DependencyFingerprint != plan.Context.Dependencies.Fingerprint)
                {
                    return Fail(error, "live_dependency_or_inventory");
                }
                if (plan.Context.Request.ManifestPath.native().size() > MaximumCookStateStringBytes)
                {
                    return Fail(error, "live_locator_limit");
                }
                Text manifestPath;
                Text expectedManifest = state.Binding.RuntimeRootIdentity;
                expectedManifest.push_back('/');
                expectedManifest.append(state.Binding.ManifestName);
                if (!P::AsciiPath(plan.Context.Request.ManifestPath.lexically_normal(), manifestPath) ||
                    manifestPath != expectedManifest)
                {
                    return Fail(error, "live_manifest_scope");
                }
                Array<const Core::Asset::AssetCookedReference*> expected, actual;
                for (const auto& output : plan.Outputs)
                {
                    if (output.TargetPath.native().size() > MaximumCookStateStringBytes ||
                        output.ExpectedIdentity.CookedPackage.size() > MaximumCookStateStringBytes)
                    {
                        return Fail(error, "live_locator_limit");
                    }
                    Text target, expectedTarget = state.Binding.RuntimeRootIdentity;
                    expectedTarget.push_back('/');
                    expectedTarget.append(output.ExpectedIdentity.CookedPackage);
                    if (!P::AsciiPath(output.TargetPath.lexically_normal(), target) || target != expectedTarget)
                    {
                        return Fail(error, "live_package_scope");
                    }
                    expected.push_back(&output.ExpectedIdentity);
                }
                for (const auto& output : record->Outputs)
                {
                    actual.push_back(&output.Reference);
                }
                const auto compare = [](const auto* a, const auto* b)
                {
                    return K::Compare(K::View(*a), K::View(*b)) < 0;
                };
                std::sort(expected.begin(), expected.end(), compare);
                std::sort(actual.begin(), actual.end(), compare);
                for (size_t i = 0; i < expected.size(); ++i)
                {
                    if (!R::SameIdentity(*expected[i], *actual[i]))
                    {
                        return Fail(error, "live_output_identity");
                    }
                }
            }
            std::sort(keys.begin(), keys.end(),
                      [](K::Key a, K::Key b)
                      {
                          return K::Compare(a, b) < 0;
                      });
            for (size_t i = 1; i < keys.size(); ++i)
            {
                if (K::Compare(keys[i - 1], keys[i]) == 0)
                {
                    return Fail(error, "duplicate_live_plan");
                }
            }
            return true;
        }
    } // namespace
    bool MakeCookManagedReceiptBody(const CookManagedIntentDraft& d, Text& out, Text& error)
    {
        error.clear();
        try
        {
            if (!AnchorValid(d.Anchor) || !J::Token(d.TransactionId, 32) || !J::Token(d.ClaimId, 32) ||
                !IdValid(d.Root) || d.Root.Volume != d.Anchor.Store.Volume || !d.IndexGeneration || !d.StateGeneration)
            {
                return Fail(error, "receipt_scope");
            }
            Text text = "{\"schema\":1,";
            Field(text, "transaction", d.TransactionId);
            text.push_back(',');
            Field(text, "store", d.Anchor.StoreId);
            text.push_back(',');
            Field(text, "claim", d.ClaimId);
            text.push_back(',');
            Field(text, "root_leaf", d.Anchor.RootLeaf);
            text.append(",\"root\":");
            Object(text, d.Root);
            text.push_back(',');
            Field(text, "index_generation", Hex(d.IndexGeneration));
            text.push_back(',');
            Field(text, "state_generation", Hex(d.StateGeneration));
            text.push_back('}');
            if (text.size() > 16384)
            {
                return Fail(error, "receipt_limit");
            }
            out = std::move(text);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "receipt_exception");
        }
    }
    bool BuildCookManagedTransactionIntent(const CookManagedIntentBuildInput& input,
                                           const CookManagedControlDocuments& docs, CookManagedTransactionIntent& out,
                                           Text& error)
    {
        error.clear();
        try
        {
            ParsedDocuments parsed;
            if (!Validate(input.Draft, docs, parsed, error) || !CheckLive(input, parsed, error))
            {
                return false;
            }
            CookManagedTransactionIntent candidate;
            candidate.m_Value = input.Draft;
            candidate.m_bValid = true;
            Text wire;
            if (!Encode(candidate.m_Value, wire, error))
            {
                return false;
            }
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "build_exception");
        }
    }
    bool SerializeCookManagedTransactionIntent(const CookManagedTransactionIntent& intent, Text& out, Text& error)
    {
        error.clear();
        try
        {
            if (!intent.IsValid())
            {
                return Fail(error, "invalid_intent");
            }
            Text wire;
            if (!Encode(intent.Value(), wire, error))
            {
                return false;
            }
            out = std::move(wire);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "serialize_exception");
        }
    }
    bool ParseCookManagedTransactionEnvelope(ByteView bytes, const CookManagedStoreAnchor& anchor,
                                             CookManagedTransactionEnvelope& out, Text& error)
    {
        error.clear();
        try
        {
            if (!AnchorValid(anchor))
            {
                return Fail(error, "independent_anchor");
            }
            CookManagedTransactionEnvelope candidate;
            if (!Decode(bytes, candidate.m_Value, error) || !SameAnchor(candidate.m_Value.Anchor, anchor))
            {
                return Fail(error, "wire_or_anchor");
            }
            candidate.m_bValid = true;
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "envelope_exception");
        }
    }
    bool ParseCookManagedTransactionIntent(const CookManagedTransactionEnvelope& envelope,
                                           const CookManagedControlDocuments& docs, CookManagedTransactionIntent& out,
                                           Text& error)
    {
        error.clear();
        try
        {
            if (!envelope.m_bValid)
            {
                return Fail(error, "invalid_envelope");
            }
            ParsedDocuments parsed;
            if (!Validate(envelope.m_Value, docs, parsed, error))
            {
                return false;
            }
            CookManagedTransactionIntent candidate;
            candidate.m_Value = envelope.m_Value;
            candidate.m_bValid = true;
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "parse_exception");
        }
    }
} // namespace NorvesLib::Tools::AssetCook
