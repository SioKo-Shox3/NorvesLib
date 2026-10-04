// 用途ごとのテクスチャのクックの中身を確かめるスモーク。
// ORM の詰め方と既定値、法線のミップ(非正規化ベクトルの平均→再正規化)、R16 の精度、BC4・BC7 の色空間と
// ミップ数・バイト数を、クックした NVTEX を読み戻して確かめる。ヘッダの形式は AssetCookTextureSmoke(CLI)側でも見る。

#include "Asset/CookedTextureFormat.h"
#include "BlockCompressor.h"
#include "TextureCooker.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

        // 1 ブロックに収まる大きさだけを扱う(8x8 で 136 バイト)。
        ByteArray zlib;
        zlib.push_back(0x78);
        zlib.push_back(0x01);
        zlib.push_back(1); // 最終ブロック・無圧縮
        zlib.push_back(static_cast<uint8_t>(raw.size() & 0xff));
        zlib.push_back(static_cast<uint8_t>(raw.size() >> 8));
        zlib.push_back(static_cast<uint8_t>(~raw.size() & 0xff));
        zlib.push_back(static_cast<uint8_t>((~raw.size() >> 8) & 0xff));
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
              CookedTextureParseResult &outParsed)
    {
        TextureUsageCookParams params;
        params.Usage = usage;
        params.ThreadCount = 1;
        ErrorString error;
        if (!CookTextureForUsage(source, orm, params, outResult, error))
        {
            std::printf("TEXTURE_USAGE_SMOKE_FAIL cook failed: usage=%s error=%s\n", GetTextureUsageName(usage), error.c_str());
            ++g_failures;
            return false;
        }

        const Span<const uint8_t> span(outResult.NvtexBytes.data(), outResult.NvtexBytes.size());
        outParsed = NorvesLib::Core::Asset::ParseCookedTexture(AssetBlob::CopyBytes(span, "usage smoke"));
        if (!outParsed.Succeeded())
        {
            std::printf("TEXTURE_USAGE_SMOKE_FAIL cooked texture does not parse: usage=%s status=%d\n",
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

int main()
{
    RunOrmPacking();
    RunNormalMips();
    RunHeight16Precision();
    RunSingleAndAlbedo();
    RunInvalidUsage();

    if (g_failures != 0)
    {
        std::printf("TEXTURE_USAGE_SMOKE_RESULT failed=%d\n", g_failures);
        return 1;
    }
    std::printf("TEXTURE_USAGE_SMOKE_RESULT passed\n");
    return 0;
}
