// 用途ごとのテクスチャのクックの中身を確かめるスモーク。
// ORM の詰め方と既定値、法線のミップ(非正規化ベクトルの平均→再正規化)、R16 の精度、BC4・BC7 の色空間と
// ミップ数・バイト数を、クックした NVTEX を読み戻して確かめる。ヘッダの形式は AssetCookTextureSmoke(CLI)側でも見る。

#include "Asset/CookedTextureFormat.h"
#include "BlockCompressor.h"
#include "TextureCooker.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace
{
    using namespace NorvesLib::Tools::AssetCook;
    using NorvesLib::Core::Asset::AssetBlob;
    using NorvesLib::Core::Asset::CookedTextureColorSpace;
    using NorvesLib::Core::Asset::CookedTextureParseResult;
    using NorvesLib::Core::Asset::CookedTexturePixelFormat;
    using NorvesLib::Core::Container::Span;

    int g_failures = 0;

    void Check(bool condition, const char *message)
    {
        if (!condition)
        {
            std::printf("TEXTURE_USAGE_SMOKE_FAIL %s\n", message);
            ++g_failures;
        }
    }

    // PNM(P5/P6)を作る。maxval が 255 を超えるときは 16 ビットのビッグエンディアン。
    ByteArray MakePnm(char magic, uint32_t width, uint32_t height, uint32_t maxval, const ByteArray &samples)
    {
        char header[64];
        const int headerLength = std::snprintf(header, sizeof(header), "P%c\n%u %u\n%u\n", magic, width, height, maxval);
        ByteArray bytes;
        bytes.reserve(static_cast<size_t>(headerLength) + samples.size());
        for (int i = 0; i < headerLength; ++i)
        {
            bytes.push_back(static_cast<uint8_t>(header[i]));
        }
        for (const uint8_t value : samples)
        {
            bytes.push_back(value);
        }
        return bytes;
    }

    uint32_t Crc32(const uint8_t *data, size_t size)
    {
        uint32_t crc = 0xffffffffu;
        for (size_t i = 0; i < size; ++i)
        {
            crc ^= data[i];
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
            }
        }
        return ~crc;
    }

    void AppendBe32(ByteArray &bytes, uint32_t value)
    {
        bytes.push_back(static_cast<uint8_t>(value >> 24));
        bytes.push_back(static_cast<uint8_t>(value >> 16));
        bytes.push_back(static_cast<uint8_t>(value >> 8));
        bytes.push_back(static_cast<uint8_t>(value));
    }

    void AppendPngChunk(ByteArray &png, const char *type, const ByteArray &data)
    {
        AppendBe32(png, static_cast<uint32_t>(data.size()));
        ByteArray typeAndData;
        for (int i = 0; i < 4; ++i)
        {
            typeAndData.push_back(static_cast<uint8_t>(type[i]));
        }
        for (const uint8_t value : data)
        {
            typeAndData.push_back(value);
        }
        for (const uint8_t value : typeAndData)
        {
            png.push_back(value);
        }
        AppendBe32(png, Crc32(typeAndData.data(), typeAndData.size()));
    }

    // 16 ビットのグレースケール PNG(圧縮なしの zlib)。values は行優先で width * height 個。
    ByteArray MakeGray16Png(uint32_t width, uint32_t height, const NorvesLib::Core::Container::VariableArray<uint16_t> &values)
    {
        ByteArray raw;
        for (uint32_t y = 0; y < height; ++y)
        {
            raw.push_back(0); // フィルタなし
            for (uint32_t x = 0; x < width; ++x)
            {
                const uint16_t value = values[static_cast<size_t>(y) * width + x];
                raw.push_back(static_cast<uint8_t>(value >> 8));
                raw.push_back(static_cast<uint8_t>(value & 0xff));
            }
        }

        // 無圧縮ブロックは 1 つが 65535 バイトまでなので、収まらない大きさは複数のブロックに分ける。
        ByteArray zlib;
        zlib.push_back(0x78);
        zlib.push_back(0x01);
        uint32_t adlerA = 1;
        uint32_t adlerB = 0;
        size_t blockStart = 0;
        do
        {
            const size_t blockSize = raw.size() - blockStart < 65535 ? raw.size() - blockStart : 65535;
            const bool bFinal = blockStart + blockSize == raw.size();
            zlib.push_back(bFinal ? 1 : 0); // 無圧縮ブロック（最後だけ最終ブロックの印）
            zlib.push_back(static_cast<uint8_t>(blockSize & 0xff));
            zlib.push_back(static_cast<uint8_t>((blockSize >> 8) & 0xff));
            zlib.push_back(static_cast<uint8_t>(~blockSize & 0xff));
            zlib.push_back(static_cast<uint8_t>((~blockSize >> 8) & 0xff));
            for (size_t i = 0; i < blockSize; ++i)
            {
                const uint8_t value = raw[blockStart + i];
                zlib.push_back(value);
                adlerA = (adlerA + value) % 65521u;
                adlerB = (adlerB + adlerA) % 65521u;
            }
            blockStart += blockSize;
        } while (blockStart < raw.size());
        AppendBe32(zlib, (adlerB << 16) | adlerA);

        ByteArray header;
        AppendBe32(header, width);
        AppendBe32(header, height);
        header.push_back(16); // ビット深度
        header.push_back(0);  // グレースケール
        header.push_back(0);
        header.push_back(0);
        header.push_back(0);

        ByteArray png;
        const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        for (const uint8_t value : signature)
        {
            png.push_back(value);
        }
        AppendPngChunk(png, "IHDR", header);
        AppendPngChunk(png, "IDAT", zlib);
        AppendPngChunk(png, "IEND", ByteArray());
        return png;
    }

    ByteArray MakeGray8(uint32_t width, uint32_t height, uint8_t value)
    {
        return MakePnm('5', width, height, 255, ByteArray(static_cast<size_t>(width) * height, value));
    }

    TextureSourceImage Source(const ByteArray &bytes, const char *name)
    {
        TextureSourceImage source;
        source.Bytes = bytes.data();
        source.Size = bytes.size();
        source.Name = name;
        return source;
    }

    bool Cook(TextureUsage usage,
              const TextureSourceImage &source,
              const OrmSourceImages &orm,
              TextureCookResult &outResult,
              CookedTextureParseResult &outParsed,
              TextureFormatOverride formatOverride = TextureFormatOverride::Default)
    {
        TextureUsageCookParams params;
        params.Usage = usage;
        params.FormatOverride = formatOverride;
        params.ThreadCount = 1;
        ErrorString error;
        if (!CookTextureForUsage(source, orm, params, outResult, error))
        {
            std::printf("TEXTURE_USAGE_SMOKE_FAIL クックに失敗: usage=%s error=%s\n", GetTextureUsageName(usage), error.c_str());
            ++g_failures;
            return false;
        }

        const Span<const uint8_t> span(outResult.NvtexBytes.data(), outResult.NvtexBytes.size());
        outParsed = NorvesLib::Core::Asset::ParseCookedTexture(AssetBlob::CopyBytes(span, "usage smoke"));
        if (!outParsed.Succeeded())
        {
            std::printf("TEXTURE_USAGE_SMOKE_FAIL クック済みテクスチャを解釈できない: usage=%s status=%d\n",
                        GetTextureUsageName(usage), static_cast<int>(outParsed.Status));
            ++g_failures;
            return false;
        }
        return true;
    }

    bool DecodeMip(const CookedTextureParseResult &parsed, size_t mip, BlockFormat format, ByteArray &outRgba)
    {
        const Span<const uint8_t> bytes = parsed.Texture.GetMipBytes(mip);
        ErrorString error;
        return DecompressToRGBA8(bytes.data(), bytes.size(), parsed.Texture.Mips[mip].Width,
                                 parsed.Texture.Mips[mip].Height, format, outRgba, error);
    }

    bool Near(int actual, int expected, int tolerance)
    {
        return actual >= expected - tolerance && actual <= expected + tolerance;
    }

    // ミップ数と、ブロック単位のバイト数の合計(BC は最小 1 ブロック)を確かめる。
    void CheckMipLayout(const CookedTextureParseResult &parsed, uint32_t blockBytes, const char *label)
    {
        uint64_t expectedTotal = 0;
        uint32_t width = parsed.Texture.Width;
        uint32_t height = parsed.Texture.Height;
        uint32_t expectedMips = 0;
        for (;;)
        {
            const uint64_t blocks = static_cast<uint64_t>((width + 3) / 4) * ((height + 3) / 4);
            expectedTotal += blocks * blockBytes;
            ++expectedMips;
            if (width == 1 && height == 1)
            {
                break;
            }
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
        }

        uint64_t actualTotal = 0;
        for (const auto &mip : parsed.Texture.Mips)
        {
            actualTotal += mip.DataSize;
        }
        std::printf("TEXTURE_USAGE_SMOKE %s mips=%u bytes=%llu\n", label, parsed.Texture.MipCount,
                    static_cast<unsigned long long>(actualTotal));
        Check(parsed.Texture.MipCount == expectedMips, label);
        Check(actualTotal == expectedTotal, label);
    }

    void RunOrmPacking()
    {
        // AO=100・メタリック=200 だけを渡し、粗さの枠は無いので 1(255)になる。
        const ByteArray ao = MakeGray8(8, 8, 100);
        const ByteArray metallic = MakeGray8(8, 8, 200);
        OrmSourceImages orm;
        orm.Ao = Source(ao, "ao");
        orm.Metallic = Source(metallic, "metallic");

        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (!Cook(TextureUsage::Orm, TextureSourceImage{}, orm, result, parsed))
        {
            return;
        }

        Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::BC7, "ORM が BC7 ではない");
        Check(parsed.Texture.ColorSpace == CookedTextureColorSpace::Linear, "ORM が linear ではない");
        CheckMipLayout(parsed, 16, "orm_8x8");

        ByteArray rgba;
        if (!DecodeMip(parsed, 0, BlockFormat::BC7, rgba))
        {
            Check(false, "ORM の BC7 を復号できない");
            return;
        }
        Check(Near(rgba[0], 100, 2), "ORM の R が AO と合わない");
        Check(Near(rgba[1], 255, 2), "ORM の G が無い粗さの既定値 1 と合わない");
        Check(Near(rgba[2], 200, 2), "ORM の B がメタリックと合わない");

        // 粗さだけ渡す: AO=1(255)・メタリック=0。
        const ByteArray roughness = MakeGray8(8, 8, 60);
        OrmSourceImages roughnessOnly;
        roughnessOnly.Roughness = Source(roughness, "roughness");
        if (Cook(TextureUsage::Orm, TextureSourceImage{}, roughnessOnly, result, parsed) &&
            DecodeMip(parsed, 0, BlockFormat::BC7, rgba))
        {
            Check(Near(rgba[0], 255, 2), "AO が無いときの既定値 1 と合わない");
            Check(Near(rgba[1], 60, 2), "ORM の G が粗さと合わない");
            Check(Near(rgba[2], 0, 2), "メタリックが無いときの既定値 0 と合わない");
        }

        // 大きさの違う枠と、枠が 1 つも無い入力は断る。
        const ByteArray smallAo = MakeGray8(4, 4, 10);
        OrmSourceImages mismatched;
        mismatched.Ao = Source(smallAo, "small_ao");
        mismatched.Metallic = Source(metallic, "metallic");
        TextureUsageCookParams params;
        params.Usage = TextureUsage::Orm;
        params.ThreadCount = 1;
        ErrorString error;
        Check(!CookTextureForUsage(TextureSourceImage{}, mismatched, params, result, error), "大きさの違う ORM の枠を受け付けた");
        Check(!CookTextureForUsage(TextureSourceImage{}, OrmSourceImages{}, params, result, error), "枠が無い ORM を受け付けた");
    }

    void RunNormalMips()
    {
        // 横に A=(1,0,0)・B=(0,0,1) が交互に並ぶ 8x8。2x2 の平均は (0.5,0,0.5) で、再正規化すると x≈0.7071。
        // バイトのまま平均すると R=191 になり、再正規化していれば R≈218 になる。
        ByteArray samples;
        for (uint32_t y = 0; y < 8; ++y)
        {
            for (uint32_t x = 0; x < 8; ++x)
            {
                const bool bA = ((x + y) & 1u) == 0;
                samples.push_back(bA ? 255 : 128);
                samples.push_back(128);
                samples.push_back(bA ? 128 : 255);
            }
        }
        const ByteArray image = MakePnm('6', 8, 8, 255, samples);

        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (!Cook(TextureUsage::Normal, Source(image, "normal"), OrmSourceImages{}, result, parsed))
        {
            return;
        }

        Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::BC5, "法線が BC5 ではない");
        Check(parsed.Texture.ColorSpace == CookedTextureColorSpace::Linear, "法線が linear ではない");
        CheckMipLayout(parsed, 16, "normal_8x8");

        ByteArray rgba;
        if (!DecodeMip(parsed, 0, BlockFormat::BC5, rgba))
        {
            Check(false, "法線の BC5 を復号できない");
            return;
        }
        // 先頭の画素は A。
        Check(Near(rgba[0], 255, 6), "法線の mip0 の R が元と合わない");
        Check(Near(rgba[1], 128, 6), "法線の mip0 の G が元と合わない");

        if (!DecodeMip(parsed, 1, BlockFormat::BC5, rgba))
        {
            Check(false, "法線の mip1 を復号できない");
            return;
        }
        std::printf("TEXTURE_USAGE_SMOKE normal_mip1 r=%d g=%d\n", rgba[0], rgba[1]);
        Check(Near(rgba[0], 218, 6), "法線の mip1 が非正規化の平均→再正規化になっていない");
        Check(Near(rgba[1], 128, 6), "法線の mip1 の G が 0 付近ではない");
    }

    void RunHeight16Precision()
    {
        // 16 ビットの値(0x1234)をそのまま R16 に残す。mip1 は 2x2 が同じ値なので平均も同じ。
        NorvesLib::Core::Container::VariableArray<uint16_t> values;
        for (uint32_t i = 0; i < 8 * 8; ++i)
        {
            values.push_back(i == 0 ? 0xABCD : 0x1234);
        }
        const ByteArray image16 = MakeGray16Png(8, 8, values);

        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (Cook(TextureUsage::Height16, Source(image16, "height16"), OrmSourceImages{}, result, parsed))
        {
            Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::R16UNorm, "高さが R16 ではない");
            Check(parsed.Texture.ColorSpace == CookedTextureColorSpace::Linear, "高さが linear ではない");
            Check(parsed.Texture.MipCount == 4, "高さのミップ数が合わない");
            const Span<const uint8_t> mip0 = parsed.Texture.GetMipBytes(0);
            Check(mip0.size() == 8 * 8 * 2, "高さの mip0 のバイト数が合わない");
            if (mip0.size() >= 4)
            {
                Check(mip0[0] == 0xCD && mip0[1] == 0xAB, "16 ビットの値の精度が保たれていない(先頭の画素)");
                Check(mip0[2] == 0x34 && mip0[3] == 0x12, "16 ビットの値の精度が保たれていない");
            }
            const Span<const uint8_t> mip2 = parsed.Texture.GetMipBytes(2);
            if (mip2.size() == 2 * 2 * 2)
            {
                // mip2(2x2)の先頭は 4x4 の mip1 の先頭 2x2 の平均で、先頭の画素の 0xABCD を含むが、他は 0x1234。
                const int value = mip2[2] | (mip2[3] << 8);
                Check(value == 0x1234, "高さの mip の平均が合わない");
            }
            else
            {
                Check(false, "高さの mip2 のバイト数が合わない");
            }
        }

        // 8 ビットの入力は 0..65535 へ拡大する(x * 257)。
        const ByteArray image8 = MakeGray8(4, 4, 100);
        if (Cook(TextureUsage::Height16, Source(image8, "height8"), OrmSourceImages{}, result, parsed))
        {
            const Span<const uint8_t> mip0 = parsed.Texture.GetMipBytes(0);
            const int value = mip0.size() >= 2 ? (mip0[0] | (mip0[1] << 8)) : -1;
            Check(value == 100 * 257, "8 ビットの入力が 16 ビットへ拡大されていない");
            Check(parsed.Texture.MipCount == 3, "8 ビットの高さのミップ数が合わない");
        }
    }

    // v0.2 のタイル配置: R16 は可逆なので、タイルの中身を元の画素から独立に作った期待と照合できる。
    // 512x256 の R16 はタイルが 256x128 texel で、段 0 は 2x2、段 1（256x128）は 1 枚、段 2 からミップテイル。
    void RunHeight16Tiles()
    {
        constexpr uint32_t width = 512;
        constexpr uint32_t height = 256;
        NorvesLib::Core::Container::VariableArray<uint16_t> values;
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                values.push_back(static_cast<uint16_t>((x * 131u + y * 257u + (x ^ y) * 7u) & 0xffffu));
            }
        }
        const ByteArray image16 = MakeGray16Png(width, height, values);

        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (!Cook(TextureUsage::Height16, Source(image16, "height16_tiles"), OrmSourceImages{}, result, parsed))
        {
            return;
        }

        const auto &texture = parsed.Texture;
        Check(texture.bTiled && texture.VersionMinor == 2, "クックした NVTEX が v0.2 ではない");
        Check(texture.Tiling.TileWidth == 256 && texture.Tiling.TileHeight == 128, "R16 のタイルの形状が 256x128 ではない");
        Check(texture.Tiling.FirstTailMip == 2, "R16 のミップテイルの先頭が 2 ではない");
        Check(texture.Tiling.Tiles.size() == 5, "R16 のタイルの数が 5 ではない");

        const Span<const uint8_t> all = texture.SourceBlob.GetSpan();
        for (uint32_t tileY = 0; tileY < 2; ++tileY)
        {
            for (uint32_t tileX = 0; tileX < 2; ++tileX)
            {
                NorvesLib::Core::Asset::CookedTextureTile tile;
                if (!texture.FindTile(0, 0, tileX, tileY, tile))
                {
                    Check(false, "段 0 のタイルを表から引けない");
                    continue;
                }

                Check(tile.DataSize == 256u * 128u * 2u, "R16 のタイルが 64 KiB ではない");
                bool bSame = tile.DataOffset + tile.DataSize <= all.size();
                for (uint32_t row = 0; bSame && row < 128; ++row)
                {
                    for (uint32_t column = 0; column < 256; ++column)
                    {
                        const uint16_t expected = values[(tileY * 128 + row) * width + tileX * 256 + column];
                        const uint8_t *actual = all.data() + tile.DataOffset + (row * 256 + column) * 2;
                        if (actual[0] != (expected & 0xffu) || actual[1] != (expected >> 8))
                        {
                            bSame = false;
                            break;
                        }
                    }
                }
                Check(bSame, "R16 のタイルの中身が元の画素と一致しない");
            }
        }

        // 行優先への展開も元の画素に戻る。
        const Span<const uint8_t> mip0 = texture.GetMipBytes(0);
        bool bMip0Same = mip0.size() == static_cast<size_t>(width) * height * 2;
        for (size_t index = 0; bMip0Same && index < values.size(); ++index)
        {
            bMip0Same = mip0[index * 2] == (values[index] & 0xffu) && mip0[index * 2 + 1] == (values[index] >> 8);
        }
        Check(bMip0Same, "R16 の段 0 を行優先へ展開した結果が元の画素と一致しない");
    }

    void RunSingleAndAlbedo()
    {
        TextureCookResult result;
        CookedTextureParseResult parsed;
        ByteArray rgba;

        const ByteArray gray = MakeGray8(8, 8, 90);
        if (Cook(TextureUsage::Single, Source(gray, "single"), OrmSourceImages{}, result, parsed))
        {
            Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::BC4, "single が BC4 ではない");
            Check(parsed.Texture.ColorSpace == CookedTextureColorSpace::Linear, "single が linear ではない");
            CheckMipLayout(parsed, 8, "single_8x8");
            if (DecodeMip(parsed, 3, BlockFormat::BC4, rgba))
            {
                Check(Near(rgba[0], 90, 2), "single の最小ミップの値が合わない");
            }
        }

        // 3x3 の BC7 sRGB(4 の倍数でない大きさ)。ミップは 3x3→1x1 の 2 段。
        ByteArray samples(3 * 3 * 3, 0);
        for (size_t i = 0; i < 9; ++i)
        {
            samples[i * 3 + 0] = 120;
            samples[i * 3 + 1] = 60;
            samples[i * 3 + 2] = 30;
        }
        const ByteArray color = MakePnm('6', 3, 3, 255, samples);
        if (Cook(TextureUsage::Albedo, Source(color, "albedo"), OrmSourceImages{}, result, parsed))
        {
            Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::BC7, "albedo が BC7 ではない");
            Check(parsed.Texture.ColorSpace == CookedTextureColorSpace::SRGB, "albedo が sRGB ではない");
            CheckMipLayout(parsed, 16, "albedo_3x3");
            Check(parsed.Texture.MipCount == 2, "albedo(3x3)のミップ数が合わない");
            if (DecodeMip(parsed, 0, BlockFormat::BC7, rgba))
            {
                Check(Near(rgba[0], 120, 2) && Near(rgba[1], 60, 2) && Near(rgba[2], 30, 2), "albedo の色が合わない");
            }
        }
    }

    void RunExistingUsageHashes()
    {
        constexpr uint32_t width = 23;
        constexpr uint32_t height = 19;
        ByteArray colorSamples;
        ByteArray gray8Samples;
        NorvesLib::Core::Container::VariableArray<uint16_t> gray16Samples;
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                colorSamples.push_back(static_cast<uint8_t>((x * 37u + y * 11u + 3u) & 255u));
                colorSamples.push_back(static_cast<uint8_t>((x * 7u + y * 53u + 19u) & 255u));
                colorSamples.push_back(static_cast<uint8_t>((x * 29u + y * 31u + 101u) & 255u));
                gray8Samples.push_back(static_cast<uint8_t>((x * 23u + y * 47u + x * y * 3u) & 255u));
                gray16Samples.push_back(static_cast<uint16_t>((x * 3329u + y * 577u + (x ^ y) * 191u + 1234u) & 0xffffu));
            }
        }
        const ByteArray color = MakePnm('6', width, height, 255, colorSamples);
        const ByteArray gray8 = MakePnm('5', width, height, 255, gray8Samples);
        const ByteArray gray16 = MakeGray16Png(width, height, gray16Samples);

        struct UsageHashCase
        {
            TextureUsage Usage;
            const ByteArray *Image;
            const char *Name;
            uint64_t ExpectedHash;
        };
        // この値は GR88 の前のクッカーの出力。既存の用途の出力を変える変更は意図して値を更新する。
        const UsageHashCase cases[] = {
            {TextureUsage::Albedo, &color, "albedo", 0x6798b7c35c5224bbull},
            {TextureUsage::Normal, &color, "normal", 0xa6e067ba65739381ull},
            {TextureUsage::Orm, &color, "orm", 0x3497f3dfe32eb49bull},
            {TextureUsage::Single, &gray8, "single", 0x7252efccc0abcfceull},
            {TextureUsage::Height16, &gray16, "height16", 0x391507ca5b3f8e13ull},
            {TextureUsage::Height16, &gray8, "height16_8bit", 0xeb0494cf08c28bcaull},
        };

        for (const UsageHashCase &test : cases)
        {
            TextureCookResult result;
            CookedTextureParseResult parsed;
            if (!Cook(test.Usage, Source(*test.Image, test.Name), OrmSourceImages{}, result, parsed))
            {
                continue;
            }

            if (test.Usage == TextureUsage::Albedo)
            {
                TextureCookResult repeated;
                CookedTextureParseResult repeatedParsed;
                if (!Cook(test.Usage, Source(*test.Image, test.Name), OrmSourceImages{}, repeated, repeatedParsed))
                {
                    continue;
                }
                if (result.NvtexBytes.size() != repeated.NvtexBytes.size() ||
                    std::memcmp(result.NvtexBytes.data(), repeated.NvtexBytes.data(), result.NvtexBytes.size()) != 0)
                {
                    Check(false, "BC7 の再クック結果が一致しない: usage=albedo");
                    return;
                }
                std::printf("TEXTURE_USAGE_SMOKE BC7 の再クック結果が一致: usage=albedo bytes=%zu\n",
                            result.NvtexBytes.size());
            }

            const uint64_t actual = NorvesLib::Core::Asset::ComputeCookedTexturePayloadHash(
                result.NvtexBytes.data(), result.NvtexBytes.size());
            if (actual != test.ExpectedHash)
            {
                std::printf("TEXTURE_USAGE_SMOKE_FAIL 既存の用途の出力が変わった: usage=%s actual=0x%016llx\n",
                            test.Name, static_cast<unsigned long long>(actual));
                ++g_failures;
            }
        }
    }


    // 16 ビット RGB の PNG。8x8 の検査用なので zlib の無圧縮ブロック 1 個に収まる。
    ByteArray MakeRgb16Png(const NorvesLib::Core::Container::VariableArray<uint16_t>& values)
    {
        ByteArray raw;
        for (size_t y = 0; y < 8; ++y)
        {
            raw.push_back(0); // フィルタなし
            for (size_t i = y * 8 * 3; i < (y + 1) * 8 * 3; ++i)
            {
                raw.push_back(static_cast<uint8_t>(values[i] >> 8));
                raw.push_back(static_cast<uint8_t>(values[i]));
            }
        }
        ByteArray zlib;
        zlib.push_back(0x78);
        zlib.push_back(0x01);
        zlib.push_back(0x01); // 最終の無圧縮ブロック
        const uint16_t length = static_cast<uint16_t>(raw.size());
        zlib.push_back(static_cast<uint8_t>(length));
        zlib.push_back(static_cast<uint8_t>(length >> 8));
        zlib.push_back(static_cast<uint8_t>(~length));
        zlib.push_back(static_cast<uint8_t>((~length >> 8) & 0xff));
        uint32_t adlerA = 1;
        uint32_t adlerB = 0;
        for (const uint8_t value : raw)
        {
            zlib.push_back(value);
            adlerA = (adlerA + value) % 65521u;
            adlerB = (adlerB + adlerA) % 65521u;
        }
        AppendBe32(zlib, (adlerB << 16) | adlerA);

        ByteArray header;
        AppendBe32(header, 8);
        AppendBe32(header, 8);
        header.push_back(16); // ビット深度
        header.push_back(2);  // RGB
        header.push_back(0);
        header.push_back(0);
        header.push_back(0);
        ByteArray png;
        const uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        for (const uint8_t value : signature)
        {
            png.push_back(value);
        }
        AppendPngChunk(png, "IHDR", header);
        AppendPngChunk(png, "IDAT", zlib);
        AppendPngChunk(png, "IEND", ByteArray());
        return png;
    }

    ByteArray ReadFixture(const char* path)
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0 || file == nullptr)
        {
            Check(false, "EXR の fixture を開けない");
            return {};
        }
        const bool bSeekEnd = std::fseek(file, 0, SEEK_END) == 0;
        const long fileSize = std::ftell(file);
        const bool bSeekStart = std::fseek(file, 0, SEEK_SET) == 0;
        ByteArray bytes(fileSize > 0 ? static_cast<size_t>(fileSize) : 0, 0);
        const size_t readSize = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
        std::fclose(file);
        if (!bSeekEnd || !bSeekStart || bytes.empty() || readSize != bytes.size())
        {
            Check(false, "EXR の fixture を読み切れない");
            return {};
        }
        return bytes;
    }

    bool CookWithParams(const TextureSourceImage& source, const TextureUsageCookParams& params,
                        TextureCookResult& result, CookedTextureParseResult& parsed)
    {
        ErrorString error;
        if (!CookTextureForUsage(source, OrmSourceImages{}, params, result, error))
        {
            std::printf("TEXTURE_USAGE_SMOKE_FAIL クックに失敗: usage=%s error=%s\n",
                        GetTextureUsageName(params.Usage), error.c_str());
            ++g_failures;
            return false;
        }
        parsed = NorvesLib::Core::Asset::ParseCookedTexture(AssetBlob::CopyBytes(
            Span<const uint8_t>(result.NvtexBytes.data(), result.NvtexBytes.size()), "float usage smoke"));
        Check(parsed.Succeeded(), "クック済みの浮動小数・16bit テクスチャを解釈できない");
        return parsed.Succeeded();
    }

    // 非圧縮の全ミップの寸法とバイト数を、入力の寸法から確かめる。
    void CheckRawMipLayout(const CookedTextureParseResult& parsed, uint32_t width, uint32_t height,
                           uint32_t pixelBytes, const char* label)
    {
        Check(parsed.Texture.Width == width && parsed.Texture.Height == height, label);
        uint32_t mipCount = 0;
        for (;;)
        {
            if (mipCount < parsed.Texture.Mips.size())
            {
                const auto& mip = parsed.Texture.Mips[mipCount];
                Check(mip.Width == width && mip.Height == height &&
                      mip.DataSize == static_cast<uint64_t>(width) * height * pixelBytes, label);
                Check(parsed.Texture.GetMipBytes(mipCount).size() == mip.DataSize, label);
            }
            ++mipCount;
            if (width == 1 && height == 1)
            {
                break;
            }
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
        }
        Check(parsed.Texture.MipCount == mipCount && parsed.Texture.Mips.size() == mipCount, label);
    }

    uint16_t ReadLe16(Span<const uint8_t> bytes, size_t offset)
    {
        return static_cast<uint16_t>(bytes[offset] | (static_cast<uint16_t>(bytes[offset + 1]) << 8));
    }

    // binary16 を算術で復号する。書き出し側の float→half の変換には依存しない。
    float HalfToFloat(uint16_t bits)
    {
        const int exponent = (bits >> 10) & 31;
        const int fraction = bits & 1023;
        const float magnitude = exponent == 0 ? std::ldexp(static_cast<float>(fraction), -24) :
            exponent == 31 ? (fraction == 0 ? std::numeric_limits<float>::infinity() :
                                               std::numeric_limits<float>::quiet_NaN()) :
                             std::ldexp(1.0f + static_cast<float>(fraction) / 1024.0f, exponent - 15);
        return (bits & 0x8000u) != 0 ? -magnitude : magnitude;
    }

    bool NearHalf(float actual, float expected)
    {
        const float relative = std::abs(expected) * 0x1p-11f;
        const float tolerance = relative > 0x1p-24f ? relative : 0x1p-24f;
        return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
    }

    // hdr_gradient.exr は 16x8、RGBA 各 32bit float、無圧縮。
    // 座標は左上が (0,0): R=x*0.5、G=y*0.25、B=0.125+x*y/128、A=1。
    float GradientValue(uint32_t x, uint32_t y, size_t channel)
    {
        const float values[] = {x * 0.5f, y * 0.25f, 0.125f + x * y / 128.0f, 1.0f};
        return values[channel];
    }

    void RunHdrBc6H(const TextureSourceImage& source)
    {
        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (!Cook(TextureUsage::Hdr, source, OrmSourceImages{}, result, parsed))
        {
            return;
        }
        Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::BC6H, "既定の HDR が BC6H ではない");
        Check(parsed.Texture.ColorSpace == CookedTextureColorSpace::Linear, "既定の HDR が線形ではない");
        Check(parsed.Texture.Width == 16 && parsed.Texture.Height == 8 && parsed.Texture.MipCount == 5,
              "既定の HDR の大きさかミップ数が合わない");
        Check(result.BytesPerPixel == 0, "既定の HDR がブロック圧縮ではない");
        CheckMipLayout(parsed, 16, "hdr_bc6h_16x8");
    }

    void RunHdrExr(const TextureSourceImage& source)
    {
        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (!Cook(TextureUsage::Hdr, source, OrmSourceImages{}, result, parsed, TextureFormatOverride::Rgba16F))
        {
            return;
        }
        Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::RGBA16F, "EXR の HDR が RGBA16F ではない");
        Check(parsed.Texture.ColorSpace == CookedTextureColorSpace::Linear, "EXR の HDR が線形ではない");
        CheckRawMipLayout(parsed, 16, 8, 8, "EXR の HDR の全ミップの配置が合わない");
        const auto mip0 = parsed.Texture.GetMipBytes(0);
        const auto mip1 = parsed.Texture.GetMipBytes(1);
        if (mip0.size() != 16 * 8 * 8 || mip1.size() != 8 * 4 * 8)
        {
            return;
        }
        for (uint32_t y = 0; y < 8; ++y)
        {
            for (uint32_t x = 0; x < 16; ++x)
            {
                for (size_t c = 0; c < 4; ++c)
                {
                    Check(NearHalf(HalfToFloat(ReadLe16(mip0, (y * 16 + x) * 8 + c * 2)), GradientValue(x, y, c)),
                          "EXR の HDR の mip0 が勾配の式と half の丸めの範囲で一致しない");
                }
            }
        }
        // mip1 の (3,2) は元画像の (6,4) から (7,5) の 2x2 の線形平均。
        for (size_t c = 0; c < 4; ++c)
        {
            float sum = 0.0f;
            for (uint32_t y = 4; y < 6; ++y)
            {
                for (uint32_t x = 6; x < 8; ++x)
                {
                    sum += GradientValue(x, y, c);
                }
            }
            Check(NearHalf(HalfToFloat(ReadLe16(mip1, (2 * 8 + 3) * 8 + c * 2)), sum / 4.0f),
                  "EXR の HDR の mip1 が 2x2 の線形平均と一致しない");
        }
    }

    void RunRadianceHdr()
    {
        // 幅 3 は RLE を使わない。RGBE=(16,64,128,131) は RGB=(0.5,2,4) を正確に表す。
        const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 3\n";
        ByteArray image;
        for (size_t i = 0; i < sizeof(header) - 1; ++i)
        {
            image.push_back(static_cast<uint8_t>(header[i]));
        }
        for (size_t i = 0; i < 3 * 2; ++i)
        {
            const uint8_t rgbe[] = {16, 64, 128, 131};
            for (const uint8_t value : rgbe)
            {
                image.push_back(value);
            }
        }
        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (!Cook(TextureUsage::Hdr, Source(image, "radiance.hdr"), OrmSourceImages{}, result, parsed,
                  TextureFormatOverride::Rgba16F))
        {
            return;
        }
        Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::RGBA16F, "Radiance HDR が RGBA16F ではない");
        const auto mip0 = parsed.Texture.GetMipBytes(0);
        Check(mip0.size() == 3 * 2 * 8, "Radiance HDR の mip0 のバイト数が合わない");
        if (mip0.size() == 3 * 2 * 8)
        {
            const float expected[] = {0.5f, 2.0f, 4.0f, 1.0f};
            for (size_t i = 0; i < 3 * 2 * 4; ++i)
            {
                Check(HalfToFloat(ReadLe16(mip0, i * 2)) == expected[i % 4], "Radiance HDR の RGBE の値が保たれない");
            }
        }
    }

    void RunHeight16Exr(const TextureSourceImage& source)
    {
        TextureUsageCookParams params;
        params.Usage = TextureUsage::Height16;
        params.ThreadCount = 1;
        params.bHasHeightRange = true;
        params.HeightRangeMin = 0.0f;
        params.HeightRangeMax = 7.5f;
        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (CookWithParams(source, params, result, parsed))
        {
            Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::R16UNorm, "EXR の高さが R16 ではない");
            const auto mip0 = parsed.Texture.GetMipBytes(0);
            Check(mip0.size() == 16 * 8 * 2, "EXR の高さの mip0 のバイト数が合わない");
            if (mip0.size() == 16 * 8 * 2)
            {
                for (uint32_t y = 0; y < 8; ++y)
                {
                    for (uint32_t x = 0; x < 16; ++x)
                    {
                        const uint16_t expected = static_cast<uint16_t>(std::round((x * 0.5 - 0.0) / 7.5 * 65535.0));
                        Check(ReadLe16(mip0, (y * 16 + x) * 2) == expected, "EXR の高さの範囲の正規化が整数の期待値と一致しない");
                    }
                }
            }
        }
        ErrorString error;
        params.HeightRangeMax = 4.0f;
        Check(!CookTextureForUsage(source, OrmSourceImages{}, params, result, error), "高さの範囲外の EXR を受け付けた");
        Check(std::strstr(error.c_str(), source.Name.c_str()) != nullptr, "高さの範囲外のエラーに入力の名前が無い");
        params.bHasHeightRange = false;
        Check(!CookTextureForUsage(source, OrmSourceImages{}, params, result, error), "高さの範囲なしの EXR を受け付けた");
    }

    void RunRequire16Bit()
    {
        const ByteArray gray = MakeGray8(8, 8, 100);
        const ByteArray rgb = MakePnm('6', 8, 8, 255, ByteArray(8 * 8 * 3, 100));
        const TextureSourceImage sources[] = {Source(gray, "require16_gray.pgm"), Source(rgb, "require16_rgb.ppm")};
        const TextureUsage usages[] = {TextureUsage::Height16, TextureUsage::Rg16};
        for (size_t i = 0; i < 2; ++i)
        {
            TextureUsageCookParams params;
            params.Usage = usages[i];
            params.ThreadCount = 1;
            params.bRequire16Bit = true;
            TextureCookResult result;
            ErrorString error;
            Check(!CookTextureForUsage(sources[i], OrmSourceImages{}, params, result, error), "16bit 必須の用途が 8bit の入力を受け付けた");
            params.bRequire16Bit = false;
            CookedTextureParseResult parsed;
            if (CookWithParams(sources[i], params, result, parsed))
            {
                const auto mip0 = parsed.Texture.GetMipBytes(0);
                const size_t channels = i == 0 ? 1 : 2;
                Check(mip0.size() == 8 * 8 * channels * 2, "8bit から拡大した mip0 のバイト数が合わない");
                if (mip0.size() == 8 * 8 * channels * 2)
                {
                    for (size_t offset = 0; offset < mip0.size(); offset += 2)
                    {
                        Check(ReadLe16(mip0, offset) == 100 * 257, "8bit の値が 16bit へ拡大されない");
                    }
                }
            }
        }
    }

    void RunRg16Precision()
    {
        NorvesLib::Core::Container::VariableArray<uint16_t> values;
        for (uint32_t i = 0; i < 8 * 8; ++i)
        {
            values.push_back(static_cast<uint16_t>(0x1234 + i * 17));
            values.push_back(static_cast<uint16_t>(0xabcd - i * 29));
            values.push_back(0x3456);
        }
        const ByteArray image = MakeRgb16Png(values);
        TextureCookResult result;
        CookedTextureParseResult parsed;
        if (!Cook(TextureUsage::Rg16, Source(image, "rg16_precision.png"), OrmSourceImages{}, result, parsed))
        {
            return;
        }
        Check(parsed.Texture.PixelFormat == CookedTexturePixelFormat::RG16UNorm, "RG16 が RG16UNorm ではない");
        CheckRawMipLayout(parsed, 8, 8, 4, "RG16 の全ミップの配置が合わない");
        const auto mip0 = parsed.Texture.GetMipBytes(0);
        if (mip0.size() == 8 * 8 * 4)
        {
            for (size_t i = 0; i < 8 * 8; ++i)
            {
                Check(ReadLe16(mip0, i * 4) == values[i * 3], "RG16 の R の 16bit の精度が保たれない");
                Check(ReadLe16(mip0, i * 4 + 2) == values[i * 3 + 1], "RG16 の G の 16bit の精度が保たれない");
            }
        }
    }

    void RunInvalidUsage()
    {
        TextureUsage usage = TextureUsage::Albedo;
        Check(!ParseTextureUsage("diffuse", usage), "未知の用途を受け付けた");
        Check(ParseTextureUsage("height16", usage) && usage == TextureUsage::Height16, "height16 を解釈できない");

        TextureCookResult result;
        TextureUsageCookParams params;
        params.Usage = TextureUsage::Albedo;
        ErrorString error;
        Check(!CookTextureForUsage(TextureSourceImage{}, OrmSourceImages{}, params, result, error), "空の入力を受け付けた");
        const ByteArray garbage(32, 7);
        Check(!CookTextureForUsage(Source(garbage, "garbage"), OrmSourceImages{}, params, result, error), "画像でない入力を受け付けた");
        Check(std::strstr(error.c_str(), "garbage") != nullptr, "復号の失敗に入力の名前が残らない");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        Check(false, "EXR の fixture のパスが無い");
        return 1;
    }
    const ByteArray fixture = ReadFixture(argv[1]);
    if (fixture.empty())
    {
        return 1;
    }
    const TextureSourceImage exr = Source(fixture, argv[1]);
    RunHdrBc6H(exr);
    RunHdrExr(exr);
    RunRadianceHdr();
    RunHeight16Exr(exr);
    RunRequire16Bit();
    RunRg16Precision();
    RunOrmPacking();
    RunNormalMips();
    RunHeight16Precision();
    RunHeight16Tiles();
    RunSingleAndAlbedo();
    RunInvalidUsage();
    RunExistingUsageHashes();

    if (g_failures != 0)
    {
        std::printf("TEXTURE_USAGE_SMOKE_RESULT failed=%d\n", g_failures);
        return 1;
    }
    std::printf("TEXTURE_USAGE_SMOKE_RESULT passed\n");
    return 0;
}
