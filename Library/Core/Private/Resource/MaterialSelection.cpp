#include "Resource/MaterialSelection.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <algorithm>
#include <cstring>

namespace NorvesLib::Core
{
    namespace
    {
        template<typename T>
        bool Valid(Container::Span<T> view) noexcept
        {
            return Asset::SkeletalNameDetail::ValidStorage(view.data(), view.size(), sizeof(T), alignof(T));
        }
        template<typename A, typename B>
        bool Overlap(Container::Span<A> a, Container::Span<B> b) noexcept
        {
            return Asset::SkeletalNameDetail::Overlaps(a.data(), a.size() * sizeof(A), b.data(), b.size() * sizeof(B));
        }
        int Compare(Container::Span<const uint8_t> a, Container::Span<const uint8_t> b) noexcept
        {
            const size_t common = std::min(a.size(), b.size());
            const int prefix = common ? std::memcmp(a.data(), b.data(), common) : 0;
            return prefix ? prefix : a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
        }
        bool ValidName(Container::Span<const uint8_t> name) noexcept
        {
            return Asset::MeasureSkeletalNameDecoding<char>(2, name).Succeeded();
        }
    }
    bool GetMaterialSelectionScratchCount(size_t n, size_t s, size_t& count) noexcept
    {
        if (n > UINT32_MAX || s > UINT32_MAX || s > (SIZE_MAX - n) / 2)
        {
            return false;
        }
        count = n + 2 * s;
        return true;
    }
    MaterialSelectionResult ResolveMaterialSelection(MaterialIdentityDomain domain,
        Container::Span<const MaterialIdentityView> catalog,
        Container::Span<const MaterialSelectorView> selectors,
        Container::Span<uint32_t> scratch, Container::Span<uint32_t> rows) noexcept
    {
        MaterialSelectionResult result;
        using Status = MaterialSelectionStatus;
        const auto fail = [&](Status status)
        {
            result.Status = status;
            return result;
        };
        size_t needed = 0;
        const size_t n = catalog.size(), s = selectors.size();
        if (!GetMaterialSelectionScratchCount(n, s, needed) || !Valid(catalog) || !Valid(selectors) ||
            !Valid(scratch) || !Valid(rows))
        {
            return fail(Status::InvalidInput);
        }
        if (domain != MaterialIdentityDomain::SourceMaterial && domain != MaterialIdentityDomain::GeneratedSlot)
        {
            return fail(Status::InvalidDomain);
        }
        if (scratch.size() < needed || rows.size() < s)
        {
            return fail(Status::InsufficientStorage);
        }
        if (Overlap(scratch, rows) || Overlap(scratch, catalog) || Overlap(scratch, selectors) ||
            Overlap(rows, catalog) || Overlap(rows, selectors))
        {
            return fail(Status::OverlappingStorage);
        }
        // 全借用領域を先に検査し、scratch書込で後続入力が壊れることを防ぐ。
        for (size_t i = 0; i < n + s; ++i)
        {
            const auto name = i < n ? catalog[i].Name : selectors[i - n].Name;
            if (!Valid(name))
            {
                return fail(Status::InvalidInput);
            }
            if (Overlap(scratch, name) || Overlap(rows, name))
            {
                return fail(Status::OverlappingStorage);
            }
            if (!ValidName(name))
            {
                return fail(Status::InvalidName);
            }
        }
        for (size_t i = 0; i < n; ++i)
        {
            scratch[i] = UINT32_MAX;
        }
        for (size_t i = 0; i < n; ++i)
        {
            const auto id = catalog[i].IdentityIndex;
            if (id >= n || scratch[id] != UINT32_MAX)
            {
                return fail(Status::InvalidCatalog);
            }
            scratch[id] = static_cast<uint32_t>(i);
        }
        // name順の行番号表。一致範囲と警告を同じ完全一致比較から作る。
        if (n > 1)
        {
            std::sort(scratch.data(), scratch.data() + n, [&](uint32_t a, uint32_t b)
            {
                const int cmp = Compare(catalog[a].Name, catalog[b].Name);
                return cmp ? cmp < 0 : catalog[a].IdentityIndex < catalog[b].IdentityIndex;
            });
        }
        for (size_t i = 1; i < n; ++i)
        {
            if (Compare(catalog[scratch[i - 1]].Name, catalog[scratch[i]].Name) == 0 &&
                (i == 1 || Compare(catalog[scratch[i - 2]].Name, catalog[scratch[i]].Name) != 0))
            {
                if (result.DuplicateNameGroups++ == 0)
                {
                    result.FirstDuplicateRow = scratch[i - 1];
                    result.SecondDuplicateRow = scratch[i];
                }
            }
        }
        for (size_t i = 0; i < s; ++i)
        {
            result.Selector = i;
            result.CatalogRow = UINT32_MAX;
            const auto& query = selectors[i];
            if (query.Kind == MaterialSelectorKind::UniqueName)
            {
                if (query.bExpectedNamePresent || query.IdentityIndex != 0 ||
                    (domain == MaterialIdentityDomain::SourceMaterial && query.Name.empty()))
                {
                    return fail(Status::InvalidSelector);
                }
                size_t lo = 0, hi = n;
                while (lo < hi)
                {
                    const size_t mid = lo + (hi - lo) / 2;
                    if (Compare(catalog[scratch[mid]].Name, query.Name) < 0)
                    {
                        lo = mid + 1;
                    }
                    else
                    {
                        hi = mid;
                    }
                }
                if (lo == n || Compare(catalog[scratch[lo]].Name, query.Name) != 0)
                {
                    return fail(Status::Unmatched);
                }
                if (lo + 1 < n && Compare(catalog[scratch[lo + 1]].Name, query.Name) == 0)
                {
                    return fail(Status::AmbiguousName);
                }
                scratch[n + i] = scratch[lo];
            }
            else if (query.Kind != MaterialSelectorKind::IndexAndExpectedName || !query.bExpectedNamePresent)
            {
                return fail(Status::InvalidSelector);
            }
        }
        for (size_t i = 0; i < n; ++i)
        {
            scratch[catalog[i].IdentityIndex] = static_cast<uint32_t>(i);
        }
        for (size_t i = 0; i < s; ++i)
        {
            result.Selector = i;
            result.CatalogRow = UINT32_MAX;
            const auto& query = selectors[i];
            if (query.Kind == MaterialSelectorKind::IndexAndExpectedName)
            {
                if (query.IdentityIndex >= n)
                {
                    return fail(Status::Unmatched);
                }
                const uint32_t row = scratch[query.IdentityIndex];
                result.CatalogRow = row;
                if (Compare(catalog[row].Name, query.Name) != 0)
                {
                    return fail(Status::ExpectedNameMismatch);
                }
                scratch[n + i] = row;
            }
            scratch[n + s + i] = static_cast<uint32_t>(i);
        }
        if (s > 1)
        {
            std::sort(scratch.data() + n + s, scratch.data() + needed, [&](uint32_t a, uint32_t b)
            {
                return scratch[n + a] != scratch[n + b] ? scratch[n + a] < scratch[n + b] : a < b;
            });
        }
        for (size_t i = 1; i < s; ++i)
        {
            const auto a = scratch[n + s + i - 1], b = scratch[n + s + i];
            if (scratch[n + a] == scratch[n + b])
            {
                result.Selector = b;
                result.ConflictingSelector = a;
                result.CatalogRow = scratch[n + a];
                return fail(Status::DuplicateTarget);
            }
        }
        for (size_t i = 0; i < s; ++i)
        {
            rows[i] = scratch[n + i];
        }
        result.Selector = SIZE_MAX;
        result.CatalogRow = UINT32_MAX;
        return result;
    }
}
