#pragma once
#include "Container/Span.h"
#include <cstdint>
#include <limits>

namespace NorvesLib::Core
{
    // 元材質番号と、primitive初出順のslot番号を同じcatalogに混ぜない。
    enum class MaterialIdentityDomain : uint8_t { SourceMaterial, GeneratedSlot };
    struct MaterialIdentityView
    {
        uint32_t IdentityIndex = 0;
        Container::Span<const uint8_t> Name;
    };
    enum class MaterialSelectorKind : uint8_t { UniqueName, IndexAndExpectedName };
    struct MaterialSelectorView
    {
        MaterialSelectorKind Kind = MaterialSelectorKind::UniqueName;
        uint32_t IdentityIndex = 0;
        Container::Span<const uint8_t> Name;
        bool bExpectedNamePresent = false;
    };
    enum class MaterialSelectionStatus : uint8_t
    {
        Success, InvalidInput, InvalidDomain, InvalidCatalog, InvalidName, InvalidSelector,
        InsufficientStorage, OverlappingStorage, Unmatched, AmbiguousName,
        ExpectedNameMismatch, DuplicateTarget
    };
    struct MaterialSelectionResult
    {
        MaterialSelectionStatus Status = MaterialSelectionStatus::Success;
        size_t Selector = std::numeric_limits<size_t>::max();
        size_t ConflictingSelector = std::numeric_limits<size_t>::max();
        uint32_t CatalogRow = UINT32_MAX;
        uint32_t DuplicateNameGroups = 0;
        uint32_t FirstDuplicateRow = UINT32_MAX;
        uint32_t SecondDuplicateRow = UINT32_MAX;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == MaterialSelectionStatus::Success;
        }
    };
    [[nodiscard]] bool GetMaterialSelectionScratchCount(size_t catalogCount, size_t selectorCount,
        size_t& count) noexcept;
    // 名前は厳密UTF-8、NUL不可、正規化なし。catalogは0..N-1のidentityを各1回含む。
    // 成功時だけrowsの先頭S件にcatalog行番号を書き、末尾を保持する。scratchは失敗時も変更し得る。
    // 名前の重複は警告情報として返す。同名の入れ替えは検出できず、元での改名が必要。
    [[nodiscard]] MaterialSelectionResult ResolveMaterialSelection(MaterialIdentityDomain domain,
        Container::Span<const MaterialIdentityView> catalog,
        Container::Span<const MaterialSelectorView> selectors,
        Container::Span<uint32_t> scratch, Container::Span<uint32_t> rows) noexcept;
}
