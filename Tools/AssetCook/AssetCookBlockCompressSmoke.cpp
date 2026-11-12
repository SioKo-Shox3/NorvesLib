// BlockCompressor のスモーク。合成画像と Silver・CobbleStoneFloor の実物をブロック圧縮し、画質を確かめる。
// あわせて、スレッド数によらず同じバイト列になること、
// 4 の倍数でない大きさと不正な入力の扱いを確かめる。

#include "BlockCompressor.h"
#include "TextureCooker.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace NorvesLib::Tools::AssetCook;

    constexpr uint32_t ImageSize = 64;
    constexpr uint32_t HdrEdgeLeft = 17, HdrEdgeRight = 43, HdrEdgeTop = 21, HdrEdgeBottom = 39;
    // PSNR は実測 - 0.5 dB を 0.1 単位で切り下げ、角度は実測の 1.1 倍を 0.1 度単位で切り上げる。
    constexpr double Bc3MinimumPsnr = 43.5;
    constexpr double HdrEpsilon = 1e-4;
    // smooth・edge の順で、RGB ごとの対数誤差の実測に 25% の余裕を持たせる。
    constexpr double HdrMeanLimits[2][3] = {
        {0.102875765 * 1.25, 0.127080772 * 1.25, 0.081776418 * 1.25},
        {2.142390715 * 1.25, 1.787221085 * 1.25, 1.088750332 * 1.25}};
    constexpr double HdrMaxLimits[2][3] = {
        {0.586951121 * 1.25, 0.626146373 * 1.25, 0.754447294 * 1.25},
        {10.196525320 * 1.25, 9.364955411 * 1.25, 15.012908128 * 1.25}};
    constexpr double SilverMinimumPsnr = 98.5;
    constexpr double SilverNormalMeanLimit = 0.0;
    constexpr double SilverNormalMaxLimit = 0.0;
    // ORM の最大絶対差は実測の各チャンネルに 1/255 の余裕を持たせる。
    constexpr int SilverOrmLimits[] = {1, 2, 1};
    constexpr double CobblestoneMinimumPsnr = 45.5;
    constexpr double CobblestoneNormalMeanLimit = 1.1;
    constexpr double CobblestoneNormalMaxLimit = 24.0;
    constexpr int CobblestoneOrmLimits[] = {11, 14, 3};

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

    bool IsHdrEdge(uint32_t x, uint32_t y)
    {
        return x >= HdrEdgeLeft && x < HdrEdgeRight && y >= HdrEdgeTop && y < HdrEdgeBottom;
    }

    bool IsHdrEdgeBlock(uint32_t x, uint32_t y)
    {
        const uint32_t left = (x / 4) * 4;
        const uint32_t top = (y / 4) * 4;
        // 跳ねる区画に一部でもかかるブロックは、滑らかな画素も含めて edge として集計する。
        return left < HdrEdgeRight && left + 4 > HdrEdgeLeft &&
               top < HdrEdgeBottom && top + 4 > HdrEdgeTop;
    }

    void RunHdrCase()
    {
        NorvesLib::Core::Container::VariableArray<float> source(static_cast<size_t>(ImageSize) * ImageSize * 4);
        for (uint32_t y = 0; y < ImageSize; ++y)
        {
            for (uint32_t x = 0; x < ImageSize; ++x)
            {
                const double u = static_cast<double>(x) / (ImageSize - 1);
                const double v = static_cast<double>(y) / (ImageSize - 1);
                float* pixel = source.data() + (static_cast<size_t>(y) * ImageSize + x) * 4;
                pixel[0] = static_cast<float>(std::pow(10.0, -3.0 + 6.0 * u));
                pixel[1] = static_cast<float>(std::pow(10.0, 3.0 - 6.0 * v));
                pixel[2] = static_cast<float>(std::pow(10.0, 3.0 * std::sin(u * 6.0) * std::cos(v * 4.0)));
                pixel[3] = 1.0f;
                // ブロック境界に揃わない領域で、隣り合う画素の輝度を大きく変える。
                if (IsHdrEdge(x, y))
                {
                    const bool bBright = ((x + y) % 2) != 0;
                    pixel[0] = bBright ? 100.0f : 0.01f;
                    pixel[1] = bBright ? 0.02f : 50.0f;
                    pixel[2] = (y % 3) == 0 ? 0.001f : 1000.0f;
                }
            }
        }
        BlockCompressParams params;
        params.Format = BlockFormat::BC6H;
        params.Quality = BlockQuality::Normal;
        ErrorString error;
        ByteArray blocks;
        NorvesLib::Core::Container::VariableArray<float> decoded;
        if (!CompressRGBAFloat(source.data(), ImageSize, ImageSize, params, blocks, error) ||
            !DecompressToRGBAFloat(blocks.data(), blocks.size(), ImageSize, ImageSize, params.Format, decoded, error))
        {
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL BC6H: 往復に失敗: %s\n", error.c_str());
            ++g_failures;
            return;
        }
        if (decoded.size() != source.size())
        {
            Check(false, "BC6H: 復号後の画素数が元と違う");
            return;
        }
        const char* channels[] = {"r", "g", "b"};
        const char* regions[] = {"smooth", "edge"};
        for (size_t region = 0; region < 2; ++region)
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                double sum = 0.0;
                double maximum = 0.0;
                size_t samples = 0;
                for (size_t pixel = 0; pixel < source.size() / 4; ++pixel)
                {
                    const uint32_t x = static_cast<uint32_t>(pixel % ImageSize);
                    const uint32_t y = static_cast<uint32_t>(pixel / ImageSize);
                    if (IsHdrEdgeBlock(x, y) != (region == 1))
                    {
                        continue;
                    }
                    const double src = source[pixel * 4 + channel];
                    const double dec = decoded[pixel * 4 + channel];
                    if (!std::isfinite(dec))
                    {
                        Check(false, "BC6H: 復号値が非有限");
                        return;
                    }
                    const double diff = std::abs(std::log2(dec > HdrEpsilon ? dec : HdrEpsilon) -
                                                 std::log2(src > HdrEpsilon ? src : HdrEpsilon));
                    sum += diff;
                    ++samples;
                    maximum = diff > maximum ? diff : maximum;
                }
                const double mean = sum / static_cast<double>(samples);
                std::printf("BLOCK_COMPRESS_SMOKE case=BC6H log2_mean=%.9f log2_max=%.9f channel=%s region=%s samples=%zu epsilon=%.4f mean_limit=%.9f max_limit=%.9f\n",
                            mean, maximum, channels[channel], regions[region], samples, HdrEpsilon,
                            HdrMeanLimits[region][channel], HdrMaxLimits[region][channel]);
                Check((mean <= HdrMeanLimits[region][channel] && maximum <= HdrMaxLimits[region][channel]),
                      "BC6H: 対数誤差が基準を超えた");
            }
        }
    }

    struct RgbMoments
    {
        uint64_t Sum[3] = {};
        uint64_t SumSquared[3] = {};
    };

    struct ImageWindow
    {
        uint32_t Left = 0;
        uint32_t Top = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    void PrintImageStats(const char* filename, const char* region, const DecodedTextureRgba8& image)
    {
        uint8_t minimum[4] = {255, 255, 255, 255};
        uint8_t maximum[4] = {};
        uint64_t sum[4] = {};
        uint64_t sumSquared[4] = {};
        // RGB24 の全組み合わせを 2 MiB のビット列で数え、画素数に比例する集合の確保を避ける。
        ByteArray seen(size_t{1} << 21, 0);
        uint32_t distinctRgb = 0;
        const size_t pixels = image.Pixels.size() / 4;
        for (size_t pixel = 0; pixel < pixels; ++pixel)
        {
            const uint8_t* rgba = image.Pixels.data() + pixel * 4;
            for (size_t channel = 0; channel < 4; ++channel)
            {
                const uint8_t value = rgba[channel];
                minimum[channel] = value < minimum[channel] ? value : minimum[channel];
                maximum[channel] = value > maximum[channel] ? value : maximum[channel];
                sum[channel] += value;
                sumSquared[channel] += static_cast<uint64_t>(value) * value;
            }
            const uint32_t rgb = (static_cast<uint32_t>(rgba[0]) << 16) |
                                 (static_cast<uint32_t>(rgba[1]) << 8) | rgba[2];
            const uint8_t mask = static_cast<uint8_t>(1u << (rgb & 7));
            if ((seen[rgb >> 3] & mask) == 0)
            {
                seen[rgb >> 3] |= mask;
                ++distinctRgb;
            }
        }
        double deviation[4];
        for (size_t channel = 0; channel < 4; ++channel)
        {
            const double mean = static_cast<double>(sum[channel]) / static_cast<double>(pixels);
            const double variance = static_cast<double>(sumSquared[channel]) / static_cast<double>(pixels) - mean * mean;
            deviation[channel] = std::sqrt(variance > 0.0 ? variance : 0.0);
        }
        std::printf("BLOCK_COMPRESS_SMOKE image_stats=%s region=%s r_min=%u r_max=%u r_std=%.9f g_min=%u g_max=%u g_std=%.9f b_min=%u b_max=%u b_std=%.9f a_min=%u a_max=%u a_std=%.9f distinct_rgb=%u\n",
                    filename, region, static_cast<unsigned>(minimum[0]), static_cast<unsigned>(maximum[0]), deviation[0],
                    static_cast<unsigned>(minimum[1]), static_cast<unsigned>(maximum[1]), deviation[1],
                    static_cast<unsigned>(minimum[2]), static_cast<unsigned>(maximum[2]), deviation[2],
                    static_cast<unsigned>(minimum[3]), static_cast<unsigned>(maximum[3]), deviation[3], distinctRgb);
    }

    ImageWindow SelectMostVariedWindow(const DecodedTextureRgba8& image)
    {
        if (image.Width < 512 || image.Height < 512)
        {
            return {0, 0, image.Width, image.Height};
        }
        constexpr uint32_t stride = 256;
        const uint32_t columns = (image.Width + stride - 1) / stride;
        const uint32_t rows = (image.Height + stride - 1) / stride;
        NorvesLib::Core::Container::VariableArray<RgbMoments> tiles(static_cast<size_t>(columns) * rows);
        // 窓は 256 四方のタイル 4 枚の和で求め、重なる窓ごとに画像を読み直さない。
        for (uint32_t y = 0; y < image.Height; ++y)
        {
            for (uint32_t x = 0; x < image.Width; ++x)
            {
                RgbMoments& tile = tiles[static_cast<size_t>(y / stride) * columns + x / stride];
                const uint8_t* pixel = image.Pixels.data() + (static_cast<size_t>(y) * image.Width + x) * 4;
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    const uint64_t value = pixel[channel];
                    tile.Sum[channel] += value;
                    tile.SumSquared[channel] += value * value;
                }
            }
        }
        ImageWindow selected{0, 0, 512, 512};
        uint64_t largestVariance = 0;
        constexpr uint64_t count = 512 * 512;
        for (uint32_t top = 0; top + 512 <= image.Height; top += stride)
        {
            for (uint32_t left = 0; left + 512 <= image.Width; left += stride)
            {
                RgbMoments window;
                for (uint32_t dy = 0; dy < 2; ++dy)
                {
                    for (uint32_t dx = 0; dx < 2; ++dx)
                    {
                        const RgbMoments& tile = tiles[static_cast<size_t>(top / stride + dy) * columns + left / stride + dx];
                        for (size_t channel = 0; channel < 3; ++channel)
                        {
                            window.Sum[channel] += tile.Sum[channel];
                            window.SumSquared[channel] += tile.SumSquared[channel];
                        }
                    }
                }
                // 分散に共通の分母 count^2 を掛け、整数で同点を判定する。上から左の順で最初の窓を残す。
                uint64_t variance = 0;
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    variance += count * window.SumSquared[channel] - window.Sum[channel] * window.Sum[channel];
                }
                if (variance > largestVariance)
                {
                    largestVariance = variance;
                    selected.Left = left;
                    selected.Top = top;
                }
            }
        }
        return selected;
    }

    bool LoadTextureImage(const char* sourceRoot, const char* directory, const char* filename, DecodedTextureRgba8& image)
    {
        ErrorString path(sourceRoot);
        path += "/Assets/Textures/";
        path += directory;
        path += "/";
        path += filename;
        FILE* file = nullptr;
        if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr)
        {
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL %s: 画像を開けない\n", path.c_str());
            ++g_failures;
            return false;
        }
        const bool bSeekOk = std::fseek(file, 0, SEEK_END) == 0;
        const long length = bSeekOk ? std::ftell(file) : -1;
        if (length <= 0 || std::fseek(file, 0, SEEK_SET) != 0)
        {
            std::fclose(file);
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL %s: 画像の大きさを取得できない\n", path.c_str());
            ++g_failures;
            return false;
        }
        ByteArray encoded(static_cast<size_t>(length));
        const size_t readBytes = std::fread(encoded.data(), 1, encoded.size(), file);
        std::fclose(file);
        ErrorString error;
        // 16bit PNG の 8bit への変換は DecodeTextureRgba8 に任せる。
        if (readBytes != encoded.size() || !DecodeTextureRgba8({encoded.data(), encoded.size()}, image, error))
        {
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL %s: 画像の読み込み・復号に失敗: %s\n", path.c_str(), error.c_str());
            ++g_failures;
            return false;
        }
        const unsigned bitDepth = encoded.size() > 24 && std::memcmp(encoded.data(), "\x89PNG\r\n\x1a\n", 8) == 0 ? encoded[24] : 0;
        std::printf("BLOCK_COMPRESS_SMOKE image_source=%s bit_depth=%u decoded_bit_depth=8\n", filename, bitDepth);
        PrintImageStats(filename, "full", image);
        return true;
    }

    void CropTextureImage(const char* filename, const DecodedTextureRgba8& image,
                         const ImageWindow& window, DecodedTextureRgba8& crop)
    {
        crop.Width = window.Width;
        crop.Height = window.Height;
        crop.Pixels.resize(static_cast<size_t>(crop.Width) * crop.Height * 4);
        for (uint32_t y = 0; y < crop.Height; ++y)
        {
            std::memcpy(crop.Pixels.data() + static_cast<size_t>(y) * crop.Width * 4,
                        image.Pixels.data() + (static_cast<size_t>(window.Top + y) * image.Width + window.Left) * 4,
                        static_cast<size_t>(crop.Width) * 4);
        }
        std::printf("BLOCK_COMPRESS_SMOKE image=%s width=%u height=%u crop_x=%u crop_y=%u crop_width=%u crop_height=%u\n",
                    filename, image.Width, image.Height, window.Left, window.Top, crop.Width, crop.Height);
        PrintImageStats(filename, "crop", crop);
    }

    bool RoundTripTexture(const char* name, const DecodedTextureRgba8& source,
                         BlockFormat format, bool bPerceptual, ByteArray& decoded)
    {
        BlockCompressParams params;
        params.Format = format;
        params.Quality = BlockQuality::Normal;
        params.bPerceptual = bPerceptual;
        ByteArray blocks;
        ErrorString error;
        if (!CompressRGBA8(source.Pixels.data(), source.Width, source.Height, params, blocks, error) ||
            !DecompressToRGBA8(blocks.data(), blocks.size(), source.Width, source.Height, format, decoded, error))
        {
            std::printf("BLOCK_COMPRESS_SMOKE_FAIL %s: 往復に失敗: %s\n", name, error.c_str());
            ++g_failures;
            return false;
        }
        if (decoded.size() != source.Pixels.size())
        {
            Check(false, "実物テクスチャ: 復号後の画素数が元と違う");
            return false;
        }
        return true;
    }

    void DecodeNormal(const uint8_t* pixel, double (&normal)[3])
    {
        normal[0] = static_cast<double>(pixel[0]) / 255.0 * 2.0 - 1.0;
        normal[1] = static_cast<double>(pixel[1]) / 255.0 * 2.0 - 1.0;
        const double zSquared = 1.0 - normal[0] * normal[0] - normal[1] * normal[1];
        normal[2] = std::sqrt(zSquared > 0.0 ? zSquared : 0.0);
        const double length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        for (double& component : normal)
        {
            component /= length;
        }
    }

    void RunSilverCases(const char* sourceRoot)
    {
        if (sourceRoot == nullptr || sourceRoot[0] == '\0')
        {
            Check(false, "Silver: --source-root で元画像のルートを指定する必要がある");
            return;
        }
        const char* filenames[] = {"silver_albedo.png", "silver_normal-ogl.png", "silver_ao.png",
                                   "silver_roughness.png", "silver_metallic.png"};
        DecodedTextureRgba8 images[5];
        DecodedTextureRgba8 sources[5];
        bool bLoaded = true;
        for (size_t i = 0; i < 5; ++i)
        {
            if (!LoadTextureImage(sourceRoot, "Silver", filenames[i], sources[i]))
            {
                bLoaded = false;
            }
        }
        if (!bLoaded)
        {
            return;
        }
        ImageWindow windows[5];
        const bool bSharedOrm = sources[2].Width == sources[3].Width && sources[2].Width == sources[4].Width &&
                                sources[2].Height == sources[3].Height && sources[2].Height == sources[4].Height;
        std::printf("BLOCK_COMPRESS_SMOKE orm_crop_mode=%s\n", bSharedOrm ? "shared_ao" : "independent_dimensions_differ");
        for (size_t i = 0; i < 5; ++i)
        {
            windows[i] = bSharedOrm && i >= 3 ? windows[2] : SelectMostVariedWindow(sources[i]);
            CropTextureImage(filenames[i], sources[i], windows[i], images[i]);
            sources[i].Pixels = ByteArray();
        }
        ByteArray decoded;
        if (!RoundTripTexture("silver_albedo", images[0], BlockFormat::BC7, true, decoded))
        {
            return;
        }
        const double albedoPsnr = ComputePsnr(images[0].Pixels, decoded, 3);
        std::printf("BLOCK_COMPRESS_SMOKE case=silver_albedo psnr=%.9f min=%.1f\n", albedoPsnr, SilverMinimumPsnr);
        Check(albedoPsnr >= SilverMinimumPsnr, "silver_albedo: PSNR が基準に届かない");
        if (!RoundTripTexture("silver_normal", images[1], BlockFormat::BC5, false, decoded))
        {
            return;
        }
        double angleSum = 0.0;
        double angleMax = 0.0;
        const size_t pixelCount = images[1].Pixels.size() / 4;
        for (size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            double sourceNormal[3];
            double decodedNormal[3];
            DecodeNormal(images[1].Pixels.data() + pixel * 4, sourceNormal);
            DecodeNormal(decoded.data() + pixel * 4, decodedNormal);
            double dot = 0.0;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                dot += sourceNormal[channel] * decodedNormal[channel];
            }
            dot = dot < -1.0 ? -1.0 : (dot > 1.0 ? 1.0 : dot);
            const double angle = std::acos(dot) * (180.0 / std::acos(-1.0));
            angleSum += angle;
            angleMax = angle > angleMax ? angle : angleMax;
        }
        const double angleMean = angleSum / static_cast<double>(pixelCount);
        std::printf("BLOCK_COMPRESS_SMOKE case=silver_normal angle_mean_deg=%.9f angle_max_deg=%.9f mean_limit_deg=%.1f max_limit_deg=%.1f\n",
                    angleMean, angleMax, SilverNormalMeanLimit, SilverNormalMaxLimit);
        Check((angleMean <= SilverNormalMeanLimit && angleMax <= SilverNormalMaxLimit),
              "silver_normal: 法線の角度誤差が基準を超えた");
        DecodedTextureRgba8 orm;
        orm.Width = images[2].Width;
        orm.Height = images[2].Height;
        if (images[3].Width != orm.Width || images[3].Height != orm.Height ||
            images[4].Width != orm.Width || images[4].Height != orm.Height)
        {
            Check(false, "silver_orm: 元画像の切り出し後の大きさが揃っていない");
            return;
        }
        orm.Pixels.resize(images[2].Pixels.size());
        for (size_t pixel = 0; pixel < orm.Pixels.size() / 4; ++pixel)
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                orm.Pixels[pixel * 4 + channel] = images[channel + 2].Pixels[pixel * 4];
            }
            orm.Pixels[pixel * 4 + 3] = 255;
        }
        if (!RoundTripTexture("silver_orm", orm, BlockFormat::BC7, false, decoded))
        {
            return;
        }
        int maxDiff[3] = {};
        for (size_t pixel = 0; pixel < orm.Pixels.size() / 4; ++pixel)
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                const int diff = std::abs(static_cast<int>(orm.Pixels[pixel * 4 + channel]) - decoded[pixel * 4 + channel]);
                maxDiff[channel] = diff > maxDiff[channel] ? diff : maxDiff[channel];
            }
        }
        std::printf("BLOCK_COMPRESS_SMOKE case=silver_orm max_diff_r=%d max_diff_g=%d max_diff_b=%d limit_r=%d limit_g=%d limit_b=%d denominator=255\n",
                    maxDiff[0], maxDiff[1], maxDiff[2], SilverOrmLimits[0], SilverOrmLimits[1], SilverOrmLimits[2]);
        Check((maxDiff[0] <= SilverOrmLimits[0] && maxDiff[1] <= SilverOrmLimits[1] && maxDiff[2] <= SilverOrmLimits[2]),
              "silver_orm: チャンネルの最大絶対差が基準を超えた");
        const bool bAlbedoTarget = albedoPsnr >= 40.0;
        const bool bNormalTarget = angleMean <= 1.0 && angleMax <= 6.0;
        const bool bOrmTarget = maxDiff[0] <= 8 && maxDiff[1] <= 8 && maxDiff[2] <= 8;
        std::printf("BLOCK_COMPRESS_SMOKE roadmap_silver=%s albedo=%s normal=%s orm=%s albedo_min_psnr=40.0 normal_mean_limit_deg=1.0 normal_max_limit_deg=6.0 orm_limit=8\n",
                    bAlbedoTarget && bNormalTarget && bOrmTarget ? "met" : "unmet",
                    bAlbedoTarget ? "met" : "unmet", bNormalTarget ? "met" : "unmet", bOrmTarget ? "met" : "unmet");
    }

    void RunCobblestoneCases(const char* sourceRoot)
    {
        if (sourceRoot == nullptr || sourceRoot[0] == '\0')
        {
            Check(false, "CobbleStoneFloor: --source-root で元画像のルートを指定する必要がある");
            return;
        }
        const char* filenames[] = {"cobblestone_floor_09_diff_4k.png", "cobblestone_floor_09_nor_gl_4k.png",
                                   "cobblestone_floor_09_arm_4k.png"};
        DecodedTextureRgba8 images[3];
        bool bLoaded = true;
        for (size_t i = 0; i < 3; ++i)
        {
            DecodedTextureRgba8 source;
            if (!LoadTextureImage(sourceRoot, "CobbleStoneFloor", filenames[i], source))
            {
                bLoaded = false;
                continue;
            }
            const ImageWindow window = SelectMostVariedWindow(source);
            CropTextureImage(filenames[i], source, window, images[i]);
        }
        if (!bLoaded)
        {
            return;
        }
        ByteArray decoded;
        if (!RoundTripTexture("cobblestone_albedo", images[0], BlockFormat::BC7, true, decoded))
        {
            return;
        }
        const double albedoPsnr = ComputePsnr(images[0].Pixels, decoded, 3);
        std::printf("BLOCK_COMPRESS_SMOKE case=cobblestone_albedo psnr=%.9f min=%.1f\n", albedoPsnr, CobblestoneMinimumPsnr);
        Check(albedoPsnr >= CobblestoneMinimumPsnr, "cobblestone_albedo: PSNR が基準に届かない");
        if (!RoundTripTexture("cobblestone_normal", images[1], BlockFormat::BC5, false, decoded))
        {
            return;
        }
        double angleSum = 0.0;
        double angleMax = 0.0;
        const size_t pixelCount = images[1].Pixels.size() / 4;
        for (size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            double sourceNormal[3];
            double decodedNormal[3];
            DecodeNormal(images[1].Pixels.data() + pixel * 4, sourceNormal);
            DecodeNormal(decoded.data() + pixel * 4, decodedNormal);
            double dot = 0.0;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                dot += sourceNormal[channel] * decodedNormal[channel];
            }
            dot = dot < -1.0 ? -1.0 : (dot > 1.0 ? 1.0 : dot);
            const double angle = std::acos(dot) * (180.0 / std::acos(-1.0));
            angleSum += angle;
            angleMax = angle > angleMax ? angle : angleMax;
        }
        const double angleMean = angleSum / static_cast<double>(pixelCount);
        std::printf("BLOCK_COMPRESS_SMOKE case=cobblestone_normal angle_mean_deg=%.9f angle_max_deg=%.9f mean_limit_deg=%.1f max_limit_deg=%.1f\n",
                    angleMean, angleMax, CobblestoneNormalMeanLimit, CobblestoneNormalMaxLimit);
        Check((angleMean <= CobblestoneNormalMeanLimit && angleMax <= CobblestoneNormalMaxLimit),
              "cobblestone_normal: 法線の角度誤差が基準を超えた");
        // ARM は R=AO・G=粗さ・B=メタリックとして詰め済みの値をそのまま圧縮する。
        if (!RoundTripTexture("cobblestone_orm", images[2], BlockFormat::BC7, false, decoded))
        {
            return;
        }
        int maxDiff[3] = {};
        for (size_t pixel = 0; pixel < images[2].Pixels.size() / 4; ++pixel)
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                const int diff = std::abs(static_cast<int>(images[2].Pixels[pixel * 4 + channel]) - decoded[pixel * 4 + channel]);
                maxDiff[channel] = diff > maxDiff[channel] ? diff : maxDiff[channel];
            }
        }
        std::printf("BLOCK_COMPRESS_SMOKE case=cobblestone_orm max_diff_r=%d max_diff_g=%d max_diff_b=%d limit_r=%d limit_g=%d limit_b=%d denominator=255\n",
                    maxDiff[0], maxDiff[1], maxDiff[2], CobblestoneOrmLimits[0], CobblestoneOrmLimits[1], CobblestoneOrmLimits[2]);
        Check((maxDiff[0] <= CobblestoneOrmLimits[0] && maxDiff[1] <= CobblestoneOrmLimits[1] && maxDiff[2] <= CobblestoneOrmLimits[2]),
              "cobblestone_orm: チャンネルの最大絶対差が基準を超えた");
        const bool bAlbedoTarget = albedoPsnr >= 40.0;
        const bool bNormalTarget = angleMean <= 1.0 && angleMax <= 6.0;
        const bool bOrmTarget = maxDiff[0] <= 8 && maxDiff[1] <= 8 && maxDiff[2] <= 8;
        std::printf("BLOCK_COMPRESS_SMOKE roadmap_cobblestone=%s albedo=%s normal=%s orm=%s albedo_min_psnr=40.0 normal_mean_limit_deg=1.0 normal_max_limit_deg=6.0 orm_limit=8\n",
                    bAlbedoTarget && bNormalTarget && bOrmTarget ? "met" : "unmet",
                    bAlbedoTarget ? "met" : "unmet", bNormalTarget ? "met" : "unmet", bOrmTarget ? "met" : "unmet");
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

int main(int argc, char** argv)
{
    const char* sourceRoot = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--source-root") == 0 && i + 1 < argc)
        {
            sourceRoot = argv[++i];
        }
        else
        {
            Check(false, "引数が不正。--source-root の後にパスを指定する");
        }
    }
    // 基準: BC7・BC5・BC4 は 40 dB 以上、BC1 は 32 dB 以上。
    RunCase("BC7", BlockFormat::BC7, false, 3, 40.0);
    RunCase("BC7_alpha", BlockFormat::BC7, true, 4, 40.0);
    RunCase("BC5", BlockFormat::BC5, false, 2, 40.0);
    RunCase("BC4", BlockFormat::BC4, false, 1, 40.0);
    RunCase("BC1", BlockFormat::BC1, false, 3, 32.0);
    RunCase("BC3", BlockFormat::BC3, true, 4, Bc3MinimumPsnr);
    RunHdrCase();
    RunSilverCases(sourceRoot);
    RunCobblestoneCases(sourceRoot);

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
