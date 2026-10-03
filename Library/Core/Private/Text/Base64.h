#pragma once

#include "Container/Span.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Text
{
    enum class Base64DecodeResult : uint8_t
    {
        Success,
        InvalidArgument,
        InvalidLength,
        InvalidCharacter,
        InvalidPadding,
        NonCanonicalPadding,
        InsufficientBuffer,
        OverlappingBuffers
    };

    struct Base64DecodeOutcome
    {
        Base64DecodeResult Result = Base64DecodeResult::InvalidArgument;
        size_t Size = 0;
    };

    // RFC 4648標準alphabetと必須paddingを検証する。空は0byte、空白/URL-safe alphabetは拒否。
    // 成功時だけ必要長を返す。サイズは参照引数でなく戻り値に持ち、入力とのaliasを避ける。
    [[nodiscard]] Base64DecodeOutcome GetBase64DecodedSize(Container::Span<const uint8_t> encoded) noexcept;

    // 入出力spanの重なりを拒否。全検証後に書き込み、失敗時はoutputを変えずSize=0。
    // 使用中に入力を変更しないこと。outputの必要長以後は成功時も変更しない。
    [[nodiscard]] Base64DecodeOutcome DecodeBase64(Container::Span<const uint8_t> encoded,
        Container::Span<uint8_t> output) noexcept;
}
