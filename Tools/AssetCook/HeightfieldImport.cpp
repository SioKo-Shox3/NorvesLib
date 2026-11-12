// 高さファイルの復号、メートル写像、向きと寸法の調整、平滑化を順に行う。
#include "HeightfieldImport.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "stb_image.h"
#include "exr.h"

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Core::Container::AnsiString;
        using Core::Container::Span;
        using Core::Container::VariableArray;

        template <typename... Args>
        AnsiString Message(const char* format, Args... args)
        {
            char text[2048];
            std::snprintf(text, sizeof(text), format, args...);
            return AnsiString(text);
        }

        template <typename... Args>
        bool Fail(AnsiString& error, const char* format, Args... args)
        {
            error = Message(format, args...);
            return false;
        }

        bool AllocateGrid(uint32_t width, uint32_t height, TerrainHeightGrid& grid, AnsiString& error)
        {
            if (width < 2 || height < 2)
            {
                return Fail(error, "高さ入力は %u×%u 標本です。幅・高さをそれぞれ 2 以上にしてください。", width, height);
            }
            const uint64_t count = uint64_t(width) * height;
            if (count > grid.HeightsM.max_size())
            {
                return Fail(error, "高さ格子 %u×%u 標本が格納上限を超えます。入力の寸法を小さくしてください。", width, height);
            }
            grid.HeightsM.resize(static_cast<size_t>(count));
            grid.Width = width;
            grid.Height = height;
            return true;
        }

        struct StbiPixels
        {
            void* Data = nullptr;
            ~StbiPixels() { stbi_image_free(Data); }
        };

        struct ExrPixels
        {
            exr_image Image{};
            ~ExrPixels() { exr_image_free(&Image); }
        };

        template <typename T>
        bool CopyPng(const T* pixels, int channels, TerrainHeightGrid& grid, AnsiString& error)
        {
            for (size_t i = 0; i < grid.HeightsM.size(); ++i)
            {
                const T* pixel = pixels + i * channels;
                if (channels >= 3 && (pixel[0] != pixel[1] || pixel[0] != pixel[2]))
                {
                    return Fail(error, "PNG の標本 (x=%zu, z=%zu) が R=%u G=%u B=%u です。全標本を R=G=B の高さ画像にしてください。",
                        i % grid.Width, i / grid.Width, unsigned(pixel[0]), unsigned(pixel[1]), unsigned(pixel[2]));
                }
                grid.HeightsM[i] = pixel[0];
            }
            return true;
        }

        bool ReadPng(Span<const uint8_t> bytes, const HeightImportDesc& desc, HeightImportResult& result, AnsiString& error)
        {
            constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (bytes.size() < sizeof(signature) || std::memcmp(bytes.data(), signature, sizeof(signature)) != 0)
            {
                return Fail(error, "PNG 指定の入力（%zu バイト）に PNG 識別子がありません。PNG ファイルと形式指定を確認してください。", bytes.size());
            }
            if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return Fail(error, "PNG が %zu バイトで読み込み上限を超えます。入力を小さくしてください。", bytes.size());
            }
            const int size = static_cast<int>(bytes.size());
            const bool b16 = stbi_is_16_bit_from_memory(bytes.data(), size) != 0;
            if (desc.Format == HeightSourceFormat::Png16 && !b16)
            {
                return Fail(error, "png16 と指定されたが 16bit の PNG ではない（%zu バイト）。16bit PNG を用意するか png8 を指定してください。", bytes.size());
            }
            if (desc.Format == HeightSourceFormat::Png8 && b16)
            {
                return Fail(error, "png8 指定の入力は 16bit PNG です。形式を png16 にしてください。");
            }
            if (!b16 && desc.RequireBitDepth == 16)
            {
                return Fail(error, "require_bit_depth=16 に対して入力は 8bit です。16bit 入力を使うか require_bit_depth を変更してください。");
            }
            int width = 0, height = 0, channels = 0;
            if (!stbi_info_from_memory(bytes.data(), size, &width, &height, &channels))
            {
                return Fail(error, "PNG 情報を読めません（%zu バイト）。破損していない PNG を指定してください。", bytes.size());
            }
            if (channels < 1 || channels > 4 || width < 2 || height < 2)
            {
                return Fail(error, "PNG は %d×%d 標本、%d チャンネルです。幅・高さ 2 以上、1..4 チャンネルの PNG を指定してください。", width, height, channels);
            }
            if (!AllocateGrid(static_cast<uint32_t>(width), static_cast<uint32_t>(height), result.Grid, error))
            {
                return false;
            }
            StbiPixels pixels;
            pixels.Data = b16 ? static_cast<void*>(stbi_load_16_from_memory(bytes.data(), size, &width, &height, &channels, 0))
                               : static_cast<void*>(stbi_load_from_memory(bytes.data(), size, &width, &height, &channels, 0));
            if (!pixels.Data)
            {
                return Fail(error, "PNG の %d×%d 標本を復号できません。破損していない PNG を指定してください。", width, height);
            }
            result.SourceBitDepth = b16 ? 16 : 8;
            if (!b16)
            {
                result.Warnings.push_back(Message("terrain_height_bit_depth=8 8bit の高さは 256 段の段差になる（1 段 %.17g m）。spec の smooth（σ[m]）で均せる。", std::abs(desc.Scale)));
            }
            return b16 ? CopyPng(static_cast<const stbi_us*>(pixels.Data), channels, result.Grid, error)
                       : CopyPng(static_cast<const stbi_uc*>(pixels.Data), channels, result.Grid, error);
        }

        bool ReadRaw(Span<const uint8_t> bytes, const HeightImportDesc& desc, HeightImportResult& result, AnsiString& error)
        {
            const uint32_t sampleBytes = desc.Format == HeightSourceFormat::Raw16 ? 2 : 4;
            const uint64_t count = uint64_t(desc.RawWidth) * desc.RawHeight;
            if (count > std::numeric_limits<uint64_t>::max() / sampleBytes)
            {
                return Fail(error, "raw の寸法 %u×%u と %u バイト/標本が大きすぎます。寸法を小さくしてください。", desc.RawWidth, desc.RawHeight, sampleBytes);
            }
            const uint64_t expected = count * sampleBytes;
            if (bytes.size() != expected)
            {
                return Fail(error, "raw は %zu バイトですが %u×%u×%u = %llu バイト必要です。raw の幅・高さ・形式を修正してください。",
                    bytes.size(), desc.RawWidth, desc.RawHeight, sampleBytes, static_cast<unsigned long long>(expected));
            }
            if (!AllocateGrid(desc.RawWidth, desc.RawHeight, result.Grid, error))
            {
                return false;
            }
            for (size_t i = 0; i < result.Grid.HeightsM.size(); ++i)
            {
                uint32_t bits = 0;
                for (uint32_t j = 0; j < sampleBytes; ++j)
                {
                    const uint32_t shift = 8 * (desc.bRawBigEndian ? sampleBytes - 1 - j : j);
                    bits |= uint32_t(bytes[i * sampleBytes + j]) << shift;
                }
                result.Grid.HeightsM[i] = sampleBytes == 2 ? double(bits) : double(std::bit_cast<float>(bits));
            }
            result.SourceBitDepth = sampleBytes * 8;
            return true;
        }

        bool ReadExr(Span<const uint8_t> bytes, const HeightImportDesc& desc, HeightImportResult& result, AnsiString& error)
        {
            ExrPixels pixels;
            const exr_result status = exr_load_from_memory(bytes.data(), bytes.size(), nullptr, &pixels.Image);
            if (!EXR_OK(status))
            {
                return Fail(error, "EXR の %zu バイトを読めません（エラー %d: %s）。対応する破損のない EXR を指定してください。", bytes.size(), int(status), exr_result_string(status));
            }
            if (pixels.Image.num_parts != 1)
            {
                return Fail(error, "EXR は %d パートです。高さを単一パートの EXR として保存してください。", pixels.Image.num_parts);
            }
            const exr_part& part = pixels.Image.parts[0];
            if (part.is_deep || !part.images)
            {
                return Fail(error, "EXR の深度標本形式（is_deep=%u）は高さ格子にできません。通常の平面 EXR に変換してください。", unsigned(part.is_deep));
            }
            int selected = -1;
            AnsiString names;
            for (int i = 0; i < part.header.num_channels; ++i)
            {
                const char* name = part.header.channels[i].name;
                if (i != 0)
                {
                    names += ", ";
                }
                names += name;
                if ((!desc.ExrChannel.empty() && desc.ExrChannel == name) ||
                    (desc.ExrChannel.empty() && (part.header.num_channels == 1 || std::strcmp(name, "Y") == 0)))
                {
                    selected = i;
                }
            }
            if (selected < 0)
            {
                error = Message("EXR の読み取りチャンネルを選べません（指定: %s、%d 本）。exr_channel に存在するチャンネル名を完全一致で指定してください。利用可能: ",
                    desc.ExrChannel.empty() ? "自動" : desc.ExrChannel.c_str(), part.header.num_channels);
                error += names;
                return false;
            }
            const exr_channel& channel = part.header.channels[selected];
            if (channel.x_sampling != 1 || channel.y_sampling != 1)
            {
                return Fail(error, "EXR チャンネル %s の標本間隔は %d×%d です。間隔 1×1 のチャンネルを指定してください。", channel.name, channel.x_sampling, channel.y_sampling);
            }
            if (part.width < 2 || part.height < 2 || !part.images[selected])
            {
                return Fail(error, "EXR のデータウィンドウは %d×%d 標本です。幅・高さ 2 以上の画素を持つ EXR にしてください。", part.width, part.height);
            }
            if (!AllocateGrid(static_cast<uint32_t>(part.width), static_cast<uint32_t>(part.height), result.Grid, error))
            {
                return false;
            }
            result.SourceBitDepth = channel.pixel_type == EXR_PIXEL_HALF ? 16 : 32;
            for (size_t i = 0; i < result.Grid.HeightsM.size(); ++i)
            {
                double value = 0;
                switch (channel.pixel_type)
                {
                case EXR_PIXEL_HALF:
                {
                    float decoded;
                    exr_half_to_float(static_cast<const uint16_t*>(part.images[selected]) + i, &decoded, 1);
                    value = decoded;
                    break;
                }
                case EXR_PIXEL_FLOAT:
                    value = static_cast<const float*>(part.images[selected])[i];
                    break;
                case EXR_PIXEL_UINT:
                    value = static_cast<const uint32_t*>(part.images[selected])[i];
                    break;
                default:
                    return Fail(error, "EXR チャンネル %s の型 %d は未対応です。half・float・uint を使ってください。", channel.name, int(channel.pixel_type));
                }
                result.Grid.HeightsM[i] = value;
            }
            return true;
        }

        bool FitDimension(uint32_t source, const HeightImportDesc& desc, uint32_t& target, AnsiString& error)
        {
            const uint64_t lowerTiles = (source - 1) / desc.TileCells;
            if (lowerTiles > 0 && (source - 1) % desc.TileCells == 0)
            {
                target = source;
                return true;
            }
            if (desc.Fit == HeightFitMode::Error)
            {
                return Fail(error, "標本数 %u は TileCells=%u の TileCells*N+1 ではありません。近い有効寸法 %llu または %llu にするか fit を crop/resample にしてください。",
                    source, desc.TileCells, static_cast<unsigned long long>(std::max(uint64_t(1), lowerTiles) * desc.TileCells + 1),
                    static_cast<unsigned long long>((lowerTiles + 1) * desc.TileCells + 1));
            }
            uint64_t tiles = lowerTiles;
            if (desc.Fit == HeightFitMode::Crop && tiles == 0)
            {
                return Fail(error, "標本数 %u は crop に必要な %u 標本未満です。入力を大きくするか fit=resample を指定してください。", source, desc.TileCells + 1);
            }
            if (desc.Fit == HeightFitMode::Resample)
            {
                tiles = std::max(uint64_t(1), static_cast<uint64_t>(std::round(double(source - 1) / desc.TileCells)));
            }
            const uint64_t samples = tiles * desc.TileCells + 1;
            if (samples > std::numeric_limits<uint32_t>::max())
            {
                return Fail(error, "fit 後の標本数 %llu が 32bit 寸法を超えます。入力の寸法を小さくしてください。", static_cast<unsigned long long>(samples));
            }
            target = static_cast<uint32_t>(samples);
            return true;
        }

        bool FitGrid(const HeightImportDesc& desc, HeightImportResult& result, AnsiString& error)
        {
            TerrainHeightGrid& source = result.Grid;
            uint32_t width, height;
            if (!FitDimension(source.Width, desc, width, error) || !FitDimension(source.Height, desc, height, error))
            {
                return false;
            }
            if (width == source.Width && height == source.Height)
            {
                return true;
            }
            TerrainHeightGrid target;
            if (!AllocateGrid(width, height, target, error))
            {
                return false;
            }
            for (uint32_t z = 0; z < height; ++z)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    double value;
                    if (desc.Fit == HeightFitMode::Crop)
                    {
                        value = source.HeightsM[size_t(z) * source.Width + x];
                    }
                    else
                    {
                        const double sx = double(x) * (source.Width - 1) / (width - 1);
                        const double sz = double(z) * (source.Height - 1) / (height - 1);
                        const uint32_t x0 = static_cast<uint32_t>(sx), z0 = static_cast<uint32_t>(sz);
                        const uint32_t x1 = std::min(x0 + 1, source.Width - 1), z1 = std::min(z0 + 1, source.Height - 1);
                        const double a = std::lerp(source.HeightsM[size_t(z0) * source.Width + x0], source.HeightsM[size_t(z0) * source.Width + x1], sx - x0);
                        const double b = std::lerp(source.HeightsM[size_t(z1) * source.Width + x0], source.HeightsM[size_t(z1) * source.Width + x1], sx - x0);
                        value = std::lerp(a, b, sz - z0);
                    }
                    target.HeightsM[size_t(z) * width + x] = value;
                }
            }
            result.Warnings.push_back(Message("高さ格子を fit=%s で %u×%u から %u×%u 標本へ調整しました。",
                desc.Fit == HeightFitMode::Crop ? "crop" : "resample", source.Width, source.Height, width, height));
            source = std::move(target);
            return true;
        }

        bool SmoothGrid(const HeightImportDesc& desc, TerrainHeightGrid& grid, AnsiString& error)
        {
            if (desc.SmoothSigmaM == 0)
            {
                return true;
            }
            const double sigma = desc.SmoothSigmaM / desc.CellSizeM;
            const double radiusValue = std::ceil(3 * sigma);
            VariableArray<double> weights;
            if (!std::isfinite(radiusValue) || radiusValue >= double(weights.max_size()) || radiusValue >= double(std::numeric_limits<int64_t>::max()))
            {
                return Fail(error, "smooth の σ=%.17g m、cell_size_m=%.17g で半径が格納上限を超えます。σ を小さくするか標本間隔を大きくしてください。", desc.SmoothSigmaM, desc.CellSizeM);
            }
            // 比のアンダーフローは、その標本間隔では変化を表せないため恒等変換とする。
            if (sigma == 0)
            {
                return true;
            }
            const int64_t radius = static_cast<int64_t>(radiusValue);
            weights.resize(static_cast<size_t>(radius) + 1);
            double total = 0;
            for (int64_t k = 0; k <= radius; ++k)
            {
                const double distance = double(k) / sigma;
                weights[static_cast<size_t>(k)] = std::exp(-0.5 * distance * distance);
                total += weights[static_cast<size_t>(k)] * (k == 0 ? 1 : 2);
            }
            for (double& weight : weights)
            {
                weight /= total;
            }
            VariableArray<double> scratch(grid.HeightsM.size());
            for (int axis = 0; axis < 2; ++axis)
            {
                for (uint32_t z = 0; z < grid.Height; ++z)
                {
                    for (uint32_t x = 0; x < grid.Width; ++x)
                    {
                        const size_t index = size_t(z) * grid.Width + x;
                        double value = 0, accumulated = 0;
                        for (int64_t k = -radius; k <= radius; ++k)
                        {
                            const double weight = weights[static_cast<size_t>(k < 0 ? -k : k)];
                            if (weight == 0)
                            {
                                continue;
                            }
                            const int64_t coordinate = axis == 0 ? x : z;
                            const int64_t last = int64_t(axis == 0 ? grid.Width : grid.Height) - 1;
                            // 加算前に端へ丸め、巨大な半径でも符号付き加算を溢れさせない。
                            const int64_t sample = k < -coordinate ? 0 : (k > last - coordinate ? last : coordinate + k);
                            const size_t sampleIndex = axis == 0 ? size_t(z) * grid.Width + size_t(sample) : size_t(sample) * grid.Width + x;
                            // 重み付き平均を段階的に計算し、有限な最大高さでも途中和を溢れさせない。
                            value = std::lerp(value, grid.HeightsM[sampleIndex], weight / (accumulated + weight));
                            accumulated += weight;
                        }
                        scratch[index] = value;
                    }
                }
                grid.HeightsM.swap(scratch);
            }
            return true;
        }
    } // namespace

    bool ImportHeightfield(Span<const uint8_t> fileBytes, const HeightImportDesc& desc,
                           HeightImportResult& outResult, AnsiString& outError)
    {
        outError.clear();
        if (desc.TileCells < 8 || desc.TileCells > 1024 || (desc.TileCells & (desc.TileCells - 1)) != 0)
        {
            return Fail(outError, "TileCells=%u は無効です。8..1024 の 2 の冪にしてください。", desc.TileCells);
        }
        if (!std::isfinite(desc.CellSizeM) || desc.CellSizeM <= 0)
        {
            return Fail(outError, "CellSizeM=%.17g は無効です。有限で正の標本間隔 [m] にしてください。", desc.CellSizeM);
        }
        if (!std::isfinite(desc.SmoothSigmaM) || desc.SmoothSigmaM < 0)
        {
            return Fail(outError, "SmoothSigmaM=%.17g は無効です。有限で 0 以上の σ [m] にしてください。", desc.SmoothSigmaM);
        }
        if (!std::isfinite(desc.Scale) || !std::isfinite(desc.Offset))
        {
            return Fail(outError, "高さ写像の Scale=%.17g、Offset=%.17g は無効です。両方を有限値にしてください。", desc.Scale, desc.Offset);
        }
        if (desc.Fit != HeightFitMode::Error && desc.Fit != HeightFitMode::Crop && desc.Fit != HeightFitMode::Resample)
        {
            return Fail(outError, "fit の値 %u は無効です。error・crop・resample のいずれかを指定してください。", unsigned(desc.Fit));
        }
        try
        {
            HeightImportResult result;
            bool bRead = false;
            switch (desc.Format)
            {
            case HeightSourceFormat::Png16:
            case HeightSourceFormat::Png8:
                bRead = ReadPng(fileBytes, desc, result, outError);
                break;
            case HeightSourceFormat::Raw16:
            case HeightSourceFormat::Raw32F:
                bRead = ReadRaw(fileBytes, desc, result, outError);
                break;
            case HeightSourceFormat::Exr:
                bRead = ReadExr(fileBytes, desc, result, outError);
                break;
            default:
                return Fail(outError, "高さ入力形式 %u は無効です。png16・png8・raw16・raw32f・exr を指定してください。", unsigned(desc.Format));
            }
            if (!bRead)
            {
                return false;
            }
            TerrainHeightGrid& grid = result.Grid;
            result.SourceWidth = grid.Width;
            result.SourceHeight = grid.Height;
            for (size_t i = 0; i < grid.HeightsM.size(); ++i)
            {
                const double value = grid.HeightsM[i];
                const double height = desc.Offset + desc.Scale * value;
                if (!std::isfinite(value) || !std::isfinite(height))
                {
                    return Fail(outError, "標本 (x=%zu, z=%zu) の入力 %.17g または写像後の高さ %.17g が非有限です。入力と Scale=%.17g・Offset=%.17g を有限な範囲に修正してください。",
                        i % grid.Width, i / grid.Width, value, height, desc.Scale, desc.Offset);
                }
                grid.HeightsM[i] = height;
            }
            if (desc.bFlipZ)
            {
                for (uint32_t z = 0; z < grid.Height / 2; ++z)
                {
                    for (uint32_t x = 0; x < grid.Width; ++x)
                    {
                        std::swap(grid.HeightsM[size_t(z) * grid.Width + x], grid.HeightsM[size_t(grid.Height - 1 - z) * grid.Width + x]);
                    }
                }
            }
            if ((!desc.bKeepSourceSize && !FitGrid(desc, result, outError)) || !SmoothGrid(desc, grid, outError))
            {
                return false;
            }
            outResult = std::move(result);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(outError, "高さ格子または平滑化のメモリを確保できません。入力寸法や smooth の σ=%.17g m を小さくしてください。", desc.SmoothSigmaM);
        }
        catch (const std::length_error&)
        {
            return Fail(outError, "高さ格子または平滑化の配列が格納上限を超えました。入力寸法や smooth の σ=%.17g m を小さくしてください。", desc.SmoothSigmaM);
        }
    }
} // namespace NorvesLib::Tools::AssetCook
