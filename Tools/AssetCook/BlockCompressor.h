#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    using ByteArray = Core::Container::VariableArray<uint8_t>;
    using ErrorString = Core::Container::AnsiString;

    // クッカーが焼くブロック圧縮の形式。エンコーダの実体(bc7enc_rdo)はこの境界の内側に閉じる。
    enum class BlockFormat : uint8_t
    {
        BC1, // RGB(アルファは無視)。ブロック 8 バイト
        BC4, // R の 1 チャンネル。ブロック 8 バイト
        BC5, // R・G の 2 チャンネル。ブロック 16 バイト
        BC7, // RGBA。ブロック 16 バイト
    };

    enum class BlockQuality : uint8_t
    {
        Fast,
        Normal,
        Best,
    };

    struct BlockCompressParams
    {
        BlockFormat Format = BlockFormat::BC7;
        BlockQuality Quality = BlockQuality::Normal;
        // BC7 の誤差を YCbCr で測る。PSNR で比べるときや線形の入力では false のままにする。
        bool bPerceptual = false;
        // 画像を帯に分けて走らせるスレッド数。0 はハードウェアの並列数。
        // ブロックは互いに独立なので、結果のバイト列はスレッド数によらず同じ。
        uint32_t ThreadCount = 0;
    };

    [[nodiscard]] uint32_t GetBlockBytes(BlockFormat format) noexcept;

    // 4 の倍数でない大きさは端のブロックを端の画素の複製で埋める。0 を返すのは大きさが 0 のときと桁あふれのとき。
    [[nodiscard]] size_t ComputeCompressedSize(BlockFormat format, uint32_t width, uint32_t height) noexcept;

    // RGBA8(R が先頭、行は詰めて並ぶ)をブロック列へ圧縮する。BC4 は R、BC5 は R・G だけを読む。
    [[nodiscard]] bool CompressRGBA8(const uint8_t *rgba,
                                     uint32_t width,
                                     uint32_t height,
                                     const BlockCompressParams &params,
                                     ByteArray &outBlocks,
                                     ErrorString &error);

    // ブロック列を RGBA8 へ戻す(検証用)。BC4 は R のみ、BC5 は R・G のみ値が入り、残りは 0、アルファは 255。
    [[nodiscard]] bool DecompressToRGBA8(const uint8_t *blocks,
                                         size_t blockBytesSize,
                                         uint32_t width,
                                         uint32_t height,
                                         BlockFormat format,
                                         ByteArray &outRgba,
                                         ErrorString &error);
}
