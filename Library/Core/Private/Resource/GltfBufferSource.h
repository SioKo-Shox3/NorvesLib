#pragma once

#include "Resource/GltfContainer.h"

namespace NorvesLib::Core::Gltf
{
    enum class BufferSourceResult : uint8_t
    {
        Success, NotDataUri, InvalidArgument, InvalidDataUri, UnsupportedMediaType,
        UnsupportedEncoding, InvalidPercentEscape, InsufficientBuffer, OverlappingBuffers,
        MissingBin, InvalidBinIndex, InvalidByteLength, InvalidBinPadding
    };
    enum class DataUriMime : uint8_t
    {
        Unknown, OctetStream, GltfBuffer, Png, Jpeg
    };
    struct ByteDecodeOutcome
    {
        BufferSourceResult Result = BufferSourceResult::InvalidArgument;
        size_t Size = 0;
    };
    // %HHの置換だけを行う。UTF-8/URI文法/経路/NULの可否は呼出側が検査する。
    [[nodiscard]] ByteDecodeOutcome GetPercentDecodedSize(Container::Span<const uint8_t> encoded) noexcept;
    // 入出力の交差を拒否し、失敗時は出力非変更。サイズは値で返す。
    [[nodiscard]] ByteDecodeOutcome DecodePercentBytes(Container::Span<const uint8_t> encoded,
        Container::Span<uint8_t> output) noexcept;

    struct DataUriView
    {
        DataUriMime Mime = DataUriMime::Unknown;
        Container::Span<const uint8_t> EncodedPayload;
        size_t PercentDecodedSize = 0;
    };
    struct DataUriOutcome
    {
        BufferSourceResult Result = BufferSourceResult::InvalidArgument;
        DataUriView View;
    };
    // 入力に借用する。対応MIMEとbase64 flag、parameter/escapeを確認する。
    // payloadのpercent復号とBase64検証は後続。失敗時は空viewを値で返す。
    [[nodiscard]] DataUriOutcome ParseDataUri(Container::Span<const uint8_t> uri) noexcept;

    struct BinBufferOutcome
    {
        BufferSourceResult Result = BufferSourceResult::InvalidArgument;
        Container::Span<const uint8_t> Bytes;
    };
    // uriのないbuffer用。buffer0だけをBINへ結び、宣言長外の0〜3byteゼロpaddingを除く。
    // 成功viewは元GLBへ借用する。JSON自体の型/uriの有無は呼出側が検証する。
    [[nodiscard]] BinBufferOutcome BindGlbBuffer(const ContainerView& container, size_t bufferIndex,
        size_t declaredByteLength) noexcept;
}
