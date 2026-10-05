// managed index schema v1を観測・初期化・transactionで共有する。
#include "CookManagedStoreIndex.h"
#include "CookOutputPaths.h"
#include "ManagedStoreJson.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Text = Core::Container::AnsiString;
        using Bytes = Core::Container::VariableArray<uint8_t>;
        namespace Paths = Detail::CookOutputPaths;
        using namespace Detail::ManagedStoreJson;
        bool Fail(Text& error, const char* reason)
        {
            error = "cook_managed_index: ";
            error.append(reason);
            return false;
        }
        bool RootLeaf(const Text& leaf)
        {
            if (leaf.size() > 255 || !Paths::SafeOutputName(leaf) || std::strchr(leaf.c_str(), '/'))
            {
                return false;
            }
            constexpr char reserved[] = ".norves-assetcook";
            if (leaf.size() != sizeof(reserved) - 1)
            {
                return true;
            }
            for (size_t i = 0; i < leaf.size(); ++i)
            {
                char c = leaf[i];
                if (c >= 'A' && c <= 'Z')
                {
                    c += 'a' - 'A';
                }
                if (c != reserved[i])
                {
                    return true;
                }
            }
            return false;
        }
        bool ParseValue(const Bytes& bytes, CookManagedStoreIndex& out, size_t maximumRoots)
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
                !ReadU64(v.FindMember("generation"), out.Generation, true))
            {
                return false;
            }
            const auto roots = v.FindMember("roots");
            if (!roots.IsArray() || roots.GetArraySize() > maximumRoots)
            {
                return false;
            }
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

        Text HexValue(uint64_t n)
        {
            constexpr char digits[] = "0123456789abcdef";
            Text s;
            for (size_t i = 0; i < 16; ++i)
            {
                s.push_back(digits[(n >> (4 * (15 - i))) & 15]);
            }
            return s;
        }
        Text HexId(const Core::Container::FixedArray<uint8_t, 16>& id)
        {
            constexpr char digits[] = "0123456789abcdef";
            Text s;
            for (uint8_t b : id)
            {
                s.push_back(digits[b >> 4]);
                s.push_back(digits[b & 15]);
            }
            return s;
        }
        void Field(Text& out, const char* key, Core::Container::AnsiStringView value)
        {
            out.push_back('"');
            out.append(key);
            out.append("\":\"");
            out.append(value);
            out.push_back('"');
        }
    } // namespace
    bool ParseCookManagedStoreIndex(Core::Container::Span<const uint8_t> bytes,
                                    Core::Container::AnsiStringView expectedStoreId, size_t maximumRoots,
                                    CookManagedStoreIndex& out, Text& error)
    {
        error.clear();
        try
        {
            if (bytes.empty() || !bytes.data() || bytes.size() > MaximumCookStoreIndexBytes ||
                maximumRoots > MaximumCookStoreRoots || !Detail::ManagedStoreJson::Token(expectedStoreId, 32))
            {
                return Fail(error, "input_or_scope_limit");
            }
            Bytes owned;
            owned.assign(bytes.begin(), bytes.end());
            CookManagedStoreIndex candidate;
            candidate.StoreId.append(expectedStoreId);
            if (!ParseValue(owned, candidate, maximumRoots))
            {
                return Fail(error, "schema_or_duplicate");
            }
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "parse_exception");
        }
    }
    bool SerializeCookManagedStoreIndex(const CookManagedStoreIndex& index, Text& outJson, Text& error)
    {
        error.clear();
        try
        {
            if (!Detail::ManagedStoreJson::Token(index.StoreId, 32) || !index.Generation ||
                index.Roots.size() > MaximumCookStoreRoots)
            {
                return Fail(error, "header_or_count");
            }
            Text json = "{\"schema\":1,";
            Field(json, "store_id", index.StoreId);
            json.push_back(',');
            Field(json, "generation", HexValue(index.Generation));
            json.append(",\"roots\":[");
            for (size_t i = 0; i < index.Roots.size(); ++i)
            {
                const auto& row = index.Roots[i];
                const auto id = HexId(row.DirectoryId);
                if (!Detail::ManagedStoreJson::Token(row.ClaimId, 32) ||
                    !Detail::ManagedStoreJson::Token(row.OwnerId, 32) || !Detail::ManagedStoreJson::Token(id, 32) ||
                    !RootLeaf(row.RootLeaf))
                {
                    return Fail(error, "invalid_claim");
                }
                if (i)
                {
                    json.push_back(',');
                }
                json.push_back('{');
                Field(json, "claim_id", row.ClaimId);
                json.push_back(',');
                Field(json, "leaf", row.RootLeaf);
                json.push_back(',');
                Field(json, "directory_id", id);
                json.push_back(',');
                Field(json, "owner_id", row.OwnerId);
                json.push_back('}');
                if (json.size() > MaximumCookStoreIndexBytes)
                {
                    return Fail(error, "wire_limit");
                }
            }
            json.append("]}");
            CookManagedStoreIndex check;
            if (!ParseCookManagedStoreIndex({reinterpret_cast<const uint8_t*>(json.data()), json.size()}, index.StoreId,
                                            MaximumCookStoreRoots, check, error))
            {
                return false;
            }
            outJson = std::move(json);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "serialize_exception");
        }
    }
} // namespace NorvesLib::Tools::AssetCook
