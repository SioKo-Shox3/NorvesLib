#pragma once

// バイト列の SHA-256 を 16 進の小文字 64 文字で求める（Windows の CNG）。入力のファイルの同一性を、
// 書いた側（Scripts の JSON・CookAssets.ps1 の印）と同じ SHA-256 で突き合わせるときに使う。

#include "Container/String.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    // data の SHA-256 を outHex（16 進の小文字 64 文字）に書く。求められないときは false
    [[nodiscard]] bool ComputeSha256Hex(const uint8_t *data, size_t size, Core::Container::AnsiString &outHex);
} // namespace NorvesLib::Tools::AssetCook
