#pragma once
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/FixedArray.h"
#include "Container/VariableArray.h"
#include "Container/Span.h"
#include <cstddef>
#include <cstdint>
namespace NorvesLib::Tools::AssetCook
{
    struct CookManagedRootClaim
    {
        Core::Container::AnsiString ClaimId, RootLeaf, OwnerId;
        Core::Container::FixedArray<uint8_t, 16> DirectoryId = Core::Container::FixedArray<uint8_t, 16>(uint8_t{0});
    };
    struct CookManagedStoreIndex
    {
        Core::Container::AnsiString StoreId;
        uint64_t Generation = 1;
        Core::Container::VariableArray<CookManagedRootClaim> Roots;
    };
    inline constexpr size_t MaximumCookStoreRoots = 4096;
    inline constexpr size_t MaximumCookStoreIndexBytes = 1024 * 1024;
    // 値codecだけ。expectedは独立に検証したheaderのStoreId。errorは入力/outと独立に渡す。
    // maximumRootsは祖先集合の残予算。失敗時outを保持し、既存rootの採用はしない。
    [[nodiscard]] bool ParseCookManagedStoreIndex(Core::Container::Span<const uint8_t> bytes,
                                                  Core::Container::AnsiStringView expectedStoreId, size_t maximumRoots,
                                                  CookManagedStoreIndex& out, Core::Container::AnsiString& error);
    [[nodiscard]] bool SerializeCookManagedStoreIndex(const CookManagedStoreIndex& index,
                                                      Core::Container::AnsiString& outJson,
                                                      Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
