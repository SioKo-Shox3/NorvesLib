// BlockCompressor のスモーク。既知の 64x64 の画像を BC1・BC4・BC5・BC7 に圧縮し、
// bc7enc_rdo の復号で戻した PSNR が基準以上かを確かめる。あわせて、スレッド数によらず同じバイト列になること、
// 4 の倍数でない大きさと不正な入力の扱いを確かめる。

#include "BlockCompressor.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{
    using namespace NorvesLib::Tools::AssetCook;

    constexpr uint32_t ImageSize = 64;

    int g_failures = 0;

    void Check(bool condition, const char *message)
    {
        if (!condition)
        {
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL %s\n", message);
            ++g_failures;
        }
    }

    uint8_t ToByte(float value)
    {
        const float clamped = value < 0.0f ? 0.0f : (value > 255.0f ? 255.0f : value);
        return static_cast<uint8_t>(clamped + 0.5f);
    }

    // 滑らかな勾配と低周波の模様に、硬い縁を持つ円と矩形を重ねた決定的な画像。
    // withAlpha が false のときアルファは 255。
    ByteArray MakeImage(uint32_t width, uint32_t height, bool withAlpha)
    {
        ByteArray rgba(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const float fx = static_cast<float>(x);
                const float fy = static_cast<float>(y);
                float r = 128.0f + 90.0f * std::sin(fx * 0.07f) * std::cos(fy * 0.05f);
                float g = 40.0f + 2.0f * fy + 20.0f * std::sin((fx + fy) * 0.04f);
                float b = 200.0f - 1.5f * fx + 25.0f * std::cos(fy * 0.06f);

                const float dx = fx - 40.0f;
                const float dy = fy - 24.0f;
                if (dx * dx + dy * dy < 100.0f)
                {
                    r = 230.0f;
                    g = 60.0f;
                    b = 40.0f;
                }
                if (x >= 8 && x < 24 && y >= 40 && y < 52)
                {
                    r = 20.0f;
                    g = 180.0f;
                    b = 90.0f;
                }

                uint8_t *p = rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
                p[0] = ToByte(r);
                p[1] = ToByte(g);
                p[2] = ToByte(b);
                p[3] = withAlpha ? ToByte(255.0f - 3.0f * fx) : 255;
            }
        }
        return rgba;
    }

    // 先頭の channelCount チャンネルだけを比べた PSNR(dB)。
    double ComputePsnr(const ByteArray &a, const ByteArray &b, uint32_t channelCount)
    {
        double squaredError = 0.0;
        size_t samples = 0;
        for (size_t pixel = 0; pixel < a.size() / 4; ++pixel)
        {
            for (uint32_t channel = 0; channel < channelCount; ++channel)
            {
                const double diff = static_cast<double>(a[pixel * 4 + channel]) - static_cast<double>(b[pixel * 4 + channel]);
                squaredError += diff * diff;
                ++samples;
            }
        }
        if (samples == 0)
        {
            return 0.0;
        }
        const double mse = squaredError / static_cast<double>(samples);
        return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
    }

    void RunCase(const char *name,
                 BlockFormat format,
                 bool withAlpha,
                 uint32_t comparedChannels,
                 double minimumPsnr)
    {
        const ByteArray source = MakeImage(ImageSize, ImageSize, withAlpha);

        BlockCompressParams params;
        params.Format = format;
        params.Quality = BlockQuality::Normal;

        ErrorString error;
        ByteArray blocks;
        if (!CompressRGBA8(source.data(), ImageSize, ImageSize, params, blocks, error))
        {
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL %s: 圧縮に失敗: %s\n", name, error.c_str());
            ++g_failures;
            return;
        }
        Check(blocks.size() == ComputeCompressedSize(format, ImageSize, ImageSize), "圧縮後のバイト数が見積りと違う");

        ByteArray decoded;
        if (!DecompressToRGBA8(blocks.data(), blocks.size(), ImageSize, ImageSize, format, decoded, error))
        {
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL %s: 復号に失敗: %s\n", name, error.c_str());
            ++g_failures;
            return;
        }

        const double psnr = ComputePsnr(source, decoded, comparedChannels);
        std::printf("BLOCK_COMPRESS_SMOKE case=%s bytes=%zu psnr=%.2f min=%.1f\n", name, blocks.size(), psnr, minimumPsnr);
        if (psnr < minimumPsnr)
        {
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL %s: PSNR %.2f dB が基準 %.1f dB に届かない\n", name, psnr, minimumPsnr);
            ++g_failures;
        }
    }

    void RunThreadDeterminism()
    {
        const ByteArray source = MakeImage(ImageSize, ImageSize, true);
        const BlockFormat formats[] = {BlockFormat::BC1, BlockFormat::BC4, BlockFormat::BC5, BlockFormat::BC7};
        for (BlockFormat format : formats)
        {
            BlockCompressParams single;
            single.Format = format;
            single.ThreadCount = 1;
            BlockCompressParams multi = single;
            multi.ThreadCount = 5;

            ErrorString error;
            ByteArray a;
            ByteArray b;
            const bool okA = CompressRGBA8(source.data(), ImageSize, ImageSize, single, a, error);
            const bool okB = CompressRGBA8(source.data(), ImageSize, ImageSize, multi, b, error);
            Check(okA && okB, "スレッド数を変えた圧縮に失敗");
            Check(a == b, "スレッド数でバイト列が変わった");
        }
    }

    void RunNonMultipleOfFour()
    {
        constexpr uint32_t width = 30;
        constexpr uint32_t height = 18;
        const ByteArray source = MakeImage(width, height, false);
        const BlockFormat formats[] = {BlockFormat::BC1, BlockFormat::BC4, BlockFormat::BC5, BlockFormat::BC7};
        const uint32_t channels[] = {3, 1, 2, 3};
        for (size_t i = 0; i < 4; ++i)
        {
            BlockCompressParams params;
            params.Format = formats[i];
            ErrorString error;
            ByteArray blocks;
            ByteArray decoded;
            const bool ok = CompressRGBA8(source.data(), width, height, params, blocks, error) &&
                            DecompressToRGBA8(blocks.data(), blocks.size(), width, height, formats[i], decoded, error);
            Check(ok, "4 の倍数でない大きさの往復に失敗");
            if (ok)
            {
                // 8x5 ブロック(端を複製で埋める)。
                Check(blocks.size() == static_cast<size_t>(8) * 5 * GetBlockBytes(formats[i]), "端のブロックの数が違う");
                Check(decoded.size() == source.size(), "復号後の画素数が元と違う");
                Check(ComputePsnr(source, decoded, channels[i]) >= 30.0, "4 の倍数でない大きさの PSNR が低い");
            }
        }
    }

    void RunInvalidInputs()
    {
        BlockCompressParams params;
        ErrorString error;
        ByteArray blocks;
        const ByteArray source = MakeImage(8, 8, false);

        Check(!CompressRGBA8(nullptr, 8, 8, params, blocks, error), "null の入力を受け付けた");
        Check(!CompressRGBA8(source.data(), 0, 8, params, blocks, error), "幅 0 を受け付けた");
        Check(!CompressRGBA8(source.data(), 8, 0, params, blocks, error), "高さ 0 を受け付けた");

        ByteArray rgba;
        const ByteArray shortBlocks(15, 0);
        Check(!DecompressToRGBA8(shortBlocks.data(), shortBlocks.size(), 8, 8, BlockFormat::BC7, rgba, error),
              "バイト数の足りないブロック列を復号した");
    }
}

int main()
{
    // 基準: BC7・BC5・BC4 は 40 dB 以上、BC1 は 32 dB 以上。
    RunCase("BC7", BlockFormat::BC7, false, 3, 40.0);
    RunCase("BC7_alpha", BlockFormat::BC7, true, 4, 40.0);
    RunCase("BC5", BlockFormat::BC5, false, 2, 40.0);
    RunCase("BC4", BlockFormat::BC4, false, 1, 40.0);
    RunCase("BC1", BlockFormat::BC1, false, 3, 32.0);

    RunThreadDeterminism();
    RunNonMultipleOfFour();
    RunInvalidInputs();

    if (g_failures != 0)
    {
        std::printf("BLOCK_COMPRESS_SMOKE_RESULT failed=%d\n", g_failures);
        return 1;
    }
    std::printf("BLOCK_COMPRESS_SMOKE_RESULT passed\n");
    return 0;
}
