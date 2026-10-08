#pragma once
// 保存stateとFINAL planの固定key/package対応に使う値比較。file I/Oを行わない。
#include "CookOwnedState.h"
#include <algorithm>
#include <cstring>
namespace NorvesLib::Tools::AssetCook::Detail::CookInventoryValues
{
    using InventoryView = Core::Container::AnsiStringView;
    using Reference = Core::Asset::AssetCookedReference;
    struct Key
    {
        Core::Asset::AssetKind Kind;
        InventoryView Logical, Variant;
    };
    inline Key View(const CookStateKey& key)
    {
        return {key.Kind, key.LogicalPath, key.Variant};
    }
    inline Key View(const Reference& ref)
    {
        return {ref.Kind, ref.LogicalPath, ref.Variant};
    }
    inline int Compare(InventoryView a, InventoryView b)
    {
        const size_t n = std::min(a.size(), b.size());
        const int c = n ? std::memcmp(a.data(), b.data(), n) : 0;
        if (c)
        {
            return c;
        }
        return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
    }
    inline int Compare(Key a, Key b)
    {
        if (a.Kind != b.Kind)
        {
            return static_cast<unsigned>(a.Kind) < static_cast<unsigned>(b.Kind) ? -1 : 1;
        }
        const int c = Compare(a.Logical, b.Logical);
        return c ? c : Compare(a.Variant, b.Variant);
    }
    inline bool Equal(InventoryView a, InventoryView b)
    {
        return Compare(a, b) == 0;
    }
    inline bool SameFixedOutput(const Reference& a, const Reference& b)
    {
        return Compare(View(a), View(b)) == 0 && Equal(a.CookedPackage, b.CookedPackage);
    }
} // namespace NorvesLib::Tools::AssetCook::Detail::CookInventoryValues
