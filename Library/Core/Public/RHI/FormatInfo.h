#pragma once

#include "RHITypes.h"
#include <cassert>
#include <cstddef>

namespace NorvesLib::RHI
{
    /**
     * @brief 形式のブロック寸法とチャンネル・数値表現の性質
     */
    struct FormatInfo
    {
        Format Value;
        uint32_t BlockWidth;
        uint32_t BlockHeight;
        uint32_t BlockBytes;
        uint32_t ChannelCount;
        bool bCompressed;
        bool bDepth;
        bool bStencil;
        bool bSrgb;
        bool bUnsignedInteger;
        bool bFloat;
    };

    // Format の値を添字として使う。非圧縮は 1x1 画素を1ブロックとして扱う。
    inline constexpr FormatInfo kFormatInfoTable[] =
    {
        {Format::UNKNOWN, 1, 1, 0, 0, false, false, false, false, false, false},
        {Format::R8_UNORM, 1, 1, 1, 1, false, false, false, false, false, false},
        {Format::R8G8_UNORM, 1, 1, 2, 2, false, false, false, false, false, false},
        {Format::R8G8B8A8_UNORM, 1, 1, 4, 4, false, false, false, false, false, false},
        {Format::R8G8B8A8_SRGB, 1, 1, 4, 4, false, false, false, true, false, false},
        {Format::B8G8R8A8_UNORM, 1, 1, 4, 4, false, false, false, false, false, false},
        {Format::B8G8R8A8_SRGB, 1, 1, 4, 4, false, false, false, true, false, false},
        {Format::R16_FLOAT, 1, 1, 2, 1, false, false, false, false, false, true},
        {Format::R16G16_FLOAT, 1, 1, 4, 2, false, false, false, false, false, true},
        {Format::R16G16B16A16_FLOAT, 1, 1, 8, 4, false, false, false, false, false, true},
        {Format::R32_FLOAT, 1, 1, 4, 1, false, false, false, false, false, true},
        {Format::R32G32_FLOAT, 1, 1, 8, 2, false, false, false, false, false, true},
        {Format::R32G32B32_FLOAT, 1, 1, 12, 3, false, false, false, false, false, true},
        {Format::R32G32B32A32_FLOAT, 1, 1, 16, 4, false, false, false, false, false, true},
        {Format::D16_UNORM, 1, 1, 2, 1, false, true, false, false, false, false},
        {Format::D24_UNORM_S8_UINT, 1, 1, 4, 2, false, true, true, false, false, false},
        {Format::D32_FLOAT, 1, 1, 4, 1, false, true, false, false, false, true},
        {Format::R16_UNORM, 1, 1, 2, 1, false, false, false, false, false, false},
        {Format::BC1_UNORM, 4, 4, 8, 4, true, false, false, false, false, false},
        {Format::BC1_SRGB, 4, 4, 8, 4, true, false, false, true, false, false},
        {Format::BC4_UNORM, 4, 4, 8, 1, true, false, false, false, false, false},
        {Format::BC5_UNORM, 4, 4, 16, 2, true, false, false, false, false, false},
        {Format::BC7_UNORM, 4, 4, 16, 4, true, false, false, false, false, false},
        {Format::BC7_SRGB, 4, 4, 16, 4, true, false, false, true, false, false},
        {Format::R32_UINT, 1, 1, 4, 1, false, false, false, false, true, false},
        {Format::R32G32_UINT, 1, 1, 8, 2, false, false, false, false, true, false},
        {Format::R16G16_UNORM, 1, 1, 4, 2, false, false, false, false, false, false},
        {Format::R10G10B10A2_UNORM, 1, 1, 4, 4, false, false, false, false, false, false},
        {Format::R11G11B10_FLOAT, 1, 1, 4, 3, false, false, false, false, false, true},
        {Format::BC3_UNORM, 4, 4, 16, 4, true, false, false, false, false, false},
        {Format::BC3_SRGB, 4, 4, 16, 4, true, false, false, true, false, false},
        {Format::BC6H_UFLOAT, 4, 4, 16, 3, true, false, false, false, false, true},
    };

    inline constexpr uint32_t FormatCount = static_cast<uint32_t>(sizeof(kFormatInfoTable) / sizeof(kFormatInfoTable[0]));

    static_assert(FormatCount == static_cast<uint32_t>(Format::BC6H_UFLOAT) + 1,
                  "形式表は UNKNOWN から末尾の形式まですべて含める");
    static_assert([]() constexpr
    {
        for (uint32_t index = 0; index < FormatCount; ++index)
        {
            if (static_cast<uint32_t>(kFormatInfoTable[index].Value) != index)
            {
                return false;
            }
        }
        return true;
    }(), "形式表の各行は Format の値の順に並べる");

    /**
     * @brief 形式の性質を返す。範囲外は検出し、UNKNOWN の行を返す。
     */
    inline const FormatInfo& GetFormatInfo(Format format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        if (index >= FormatCount)
        {
            assert(false && "形式が形式表の範囲外です");
            return kFormatInfoTable[static_cast<uint32_t>(Format::UNKNOWN)];
        }
        return kFormatInfoTable[index];
    }

    /**
     * @brief 符号なし整数の形式か
     * @note 整数のカラー添付はブレンドできず、クリア値も整数（AttachmentDesc::clearColorUint）で渡す。
     */
    inline bool IsUnsignedIntegerFormat(Format format)
    {
        return GetFormatInfo(format).bUnsignedInteger;
    }

    /**
     * @brief ブロック圧縮の形式か
     */
    inline bool IsBlockCompressedFormat(Format format)
    {
        return GetFormatInfo(format).bCompressed;
    }

    /**
     * @brief 形式の1画素あたりのバイト数を返す
     * @note ブロック圧縮形式と UNKNOWN は 0 を返す。範囲外は検出して 0 を返す。
     */
    inline size_t GetFormatBytesPerPixel(Format format)
    {
        const FormatInfo& info = GetFormatInfo(format);
        return info.bCompressed ? 0 : info.BlockBytes;
    }

    /**
     * @brief 形式の1ブロックの大きさ
     * @note 非圧縮の形式は 1x1 画素を1ブロックとして扱う。
     */
    struct FormatBlockInfo
    {
        uint32_t BlockWidth = 1;  ///< ブロックの幅（画素）
        uint32_t BlockHeight = 1; ///< ブロックの高さ（画素）
        uint32_t BlockBytes = 0;  ///< 1ブロックのバイト数
    };

    /**
     * @brief 形式の1ブロックの幅・高さ・バイト数を返す
     */
    inline FormatBlockInfo GetFormatBlockInfo(Format format)
    {
        const FormatInfo& info = GetFormatInfo(format);
        return {info.BlockWidth, info.BlockHeight, info.BlockBytes};
    }

} // namespace NorvesLib::RHI
