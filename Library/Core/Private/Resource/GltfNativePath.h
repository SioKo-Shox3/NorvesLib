#pragma once
#include "Text/UnicodeText.h"
#include <filesystem>

namespace NorvesLib::Core::Gltf
{
    // 空locatorは自己完結入力の互換用。NULと不正Unicodeはfile APIへ渡す前に拒否する。
    [[nodiscard]] inline bool IsValidNativeSourcePath(const std::filesystem::path& path)
    {
        const auto& native = path.native();
        const Container::Span<const std::filesystem::path::value_type> units{native.data(), native.size()};
        bool bNoNul = true;
        const bool bValid = TextDetail::ForEachUnicodeScalar(units,
                                                             [&](uint32_t scalar)
                                                             {
                                                                 bNoNul = bNoNul && scalar != 0;
                                                             });
        return bValid && bNoNul;
    }
} // namespace NorvesLib::Core::Gltf
