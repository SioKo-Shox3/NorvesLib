#include "TextureCooker.h"

#include "Asset/CookedTextureFormat.h"
#include "BlockCompressor.h"
#include "Container/PointerTypes.h"
#include "Container/StringView.h"

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using NorvesLib::Core::Container::AnsiStringView;
        using NorvesLib::Core::Container::TUniquePtr;
        using NorvesLib::Core::Container::VariableArray;
        using NorvesLib::Core::Asset::ComputeCookedTextureFullMipCount;
        using NorvesLib::Core::Asset::ComputeCookedTextureFirstTailMip;
        using NorvesLib::Core::Asset::ComputeCookedTexturePayloadHash;
        using NorvesLib::Core::Asset::ComputeCookedTextureTileGrid;
        using NorvesLib::Core::Asset::ComputeCookedTextureTileRect;
        using NorvesLib::Core::Asset::CookedTextureTileRect;
        using NorvesLib::Core::Asset::CookedTextureTileShape;
        using NorvesLib::Core::Asset::GatherCookedTextureTile;
        using NorvesLib::Core::Asset::GetCookedTextureStandardTileShape;
        using NorvesLib::Core::Asset::CookedTextureColorSpace;
        using NorvesLib::Core::Asset::CookedTexturePixelFormat;
        using NorvesLib::Core::Asset::GetCookedTextureBytesPerPixel;
        namespace Format = NorvesLib::Core::Asset::CookedTextureFormatV0;
        namespace HeaderOffset = NorvesLib::Core::Asset::CookedTextureFormatV0::HeaderOffset;
        namespace MipRecordOffset = NorvesLib::Core::Asset::CookedTextureFormatV0::MipRecordOffset;
        namespace TiledHeaderOffset = NorvesLib::Core::Asset::CookedTextureFormatV0::TiledHeaderOffset;
        namespace TileRecordOffset = NorvesLib::Core::Asset::CookedTextureFormatV0::TileRecordOffset;

        struct TextureFormatInfo
        {
            CookedTexturePixelFormat PixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
            CookedTextureColorSpace ColorSpace = CookedTextureColorSpace::Linear;
            uint32_t OutputChannels = 4;
            bool bSrgb = false;
        };

        struct MipImage
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            ByteArray Bytes;
        };

        void WriteLe16(ByteArray &bytes, size_t offset, uint16_t value)
        {
            bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        }

        void WriteLe32(ByteArray &bytes, size_t offset, uint32_t value)
        {
            bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
            bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
            bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
        }

        void WriteLe64(ByteArray &bytes, size_t offset, uint64_t value)
        {
            WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
            WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
        }

        bool CheckedAdd(size_t lhs, size_t rhs, size_t &outValue)
        {
            if (lhs > std::numeric_limits<size_t>::max() - rhs)
            {
                return false;
            }

            outValue = lhs + rhs;
            return true;
        }

        bool CheckedMultiply(size_t lhs, size_t rhs, size_t &outValue)
        {
            if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs)
            {
                return false;
            }

            outValue = lhs * rhs;
            return true;
        }

        bool ParseTextureFormat(std::string_view format, TextureFormatInfo &outInfo)
        {
            if (format == "nvtex.v0.rgba8.srgb")
            {
                outInfo.PixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
                outInfo.ColorSpace = CookedTextureColorSpace::SRGB;
                outInfo.OutputChannels = 4;
                outInfo.bSrgb = true;
                return true;
            }

            if (format == "nvtex.v0.rgba8.linear")
            {
                outInfo.PixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
                outInfo.ColorSpace = CookedTextureColorSpace::Linear;
                outInfo.OutputChannels = 4;
                outInfo.bSrgb = false;
                return true;
            }

            if (format == "nvtex.v0.rg8.linear")
            {
                outInfo.PixelFormat = CookedTexturePixelFormat::RG8UNorm;
                outInfo.ColorSpace = CookedTextureColorSpace::Linear;
                outInfo.OutputChannels = 2;
                outInfo.bSrgb = false;
                return true;
            }

            if (format == "nvtex.v0.r8.linear")
            {
                outInfo.PixelFormat = CookedTexturePixelFormat::R8UNorm;
                outInfo.ColorSpace = CookedTextureColorSpace::Linear;
                outInfo.OutputChannels = 1;
                outInfo.bSrgb = false;
                return true;
            }

            return false;
        }

        double Srgb8ToLinear(uint8_t value)
        {
            const double srgb = static_cast<double>(value) / 255.0;
            if (srgb <= 0.04045)
            {
                return srgb / 12.92;
            }

            return std::pow((srgb + 0.055) / 1.055, 2.4);
        }

        uint8_t LinearToSrgb8(double value)
        {
            const double linear = std::clamp(value, 0.0, 1.0);
            const double srgb = linear <= 0.0031308
                                    ? linear * 12.92
                                    : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
            const double scaled = std::clamp(srgb, 0.0, 1.0) * 255.0;
            return static_cast<uint8_t>(std::clamp(std::lround(scaled), 0l, 255l));
        }

        uint8_t AverageByteSum(uint64_t sum, uint64_t count)
        {
            return static_cast<uint8_t>((sum + count / 2u) / count);
        }

        uint8_t AverageSrgbSum(double linearSum, uint64_t count)
        {
            return LinearToSrgb8(linearSum / static_cast<double>(count));
        }

        bool DecodeSourceImage(const uint8_t *sourceBytes,
                               size_t sourceSize,
                               const TextureFormatInfo &format,
                               const ErrorString &sourceName,
                               MipImage &outBaseMip,
                               ErrorString &error)
        {
            if (sourceBytes == nullptr || sourceSize == 0)
            {
                error = "texture input is empty";
                return false;
            }

            if (sourceSize > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                error = "texture input is too large for stb_image";
                return false;
            }

            int width = 0;
            int height = 0;
            int sourceChannels = 0;
            stbi_uc *decoded = stbi_load_from_memory(sourceBytes,
                                                     static_cast<int>(sourceSize),
                                                     &width,
                                                     &height,
                                                     &sourceChannels,
                                                     4);
            TUniquePtr<stbi_uc, decltype(&stbi_image_free)> decodedOwner(decoded, stbi_image_free);
            if (decoded == nullptr)
            {
                error = "failed to decode texture input";
                if (!sourceName.empty())
                {
                    error += ": ";
                    error += sourceName;
                }

                const char *reason = stbi_failure_reason();
                if (reason != nullptr)
                {
                    error += ": ";
                    error += reason;
                }
                return false;
            }

            if (width <= 0 || height <= 0)
            {
                error = "decoded texture has invalid dimensions";
                return false;
            }

            const uint32_t outputChannels = format.OutputChannels;
            size_t pixelCount = 0;
            if (!CheckedMultiply(static_cast<size_t>(width), static_cast<size_t>(height), pixelCount))
            {
                error = "decoded texture size overflow";
                return false;
            }

            size_t outputSize = 0;
            if (!CheckedMultiply(pixelCount, outputChannels, outputSize))
            {
                error = "decoded texture byte size overflow";
                return false;
            }

            outBaseMip.Width = static_cast<uint32_t>(width);
            outBaseMip.Height = static_cast<uint32_t>(height);
            outBaseMip.Bytes.resize(outputSize);

            for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                const stbi_uc *sourcePixel = decoded + pixelIndex * 4;
                uint8_t *targetPixel = outBaseMip.Bytes.data() + pixelIndex * outputChannels;
                for (uint32_t channelIndex = 0; channelIndex < outputChannels; ++channelIndex)
                {
                    targetPixel[channelIndex] = sourcePixel[channelIndex];
                }
            }

            return true;
        }

        const uint8_t *GetPixel(const MipImage &image, uint32_t x, uint32_t y, uint32_t channels)
        {
            const size_t offset = (static_cast<size_t>(y) * image.Width + x) * channels;
            return image.Bytes.data() + offset;
        }

        uint32_t ComputeCoverageStart(uint32_t sourceSize, uint32_t targetSize, uint32_t targetIndex)
        {
            return static_cast<uint32_t>((static_cast<uint64_t>(targetIndex) * sourceSize) / targetSize);
        }

        uint32_t ComputeCoverageEnd(uint32_t sourceSize, uint32_t targetSize, uint32_t targetIndex)
        {
            const uint64_t end = (static_cast<uint64_t>(targetIndex + 1u) * sourceSize) / targetSize;
            return static_cast<uint32_t>(std::clamp<uint64_t>(end, 1u, sourceSize));
        }

        bool BuildNextMip(const MipImage &source, const TextureFormatInfo &format, MipImage &outMip)
        {
            const uint32_t targetWidth = source.Width > 1 ? source.Width / 2 : 1;
            const uint32_t targetHeight = source.Height > 1 ? source.Height / 2 : 1;
            const uint32_t channels = format.OutputChannels;

            size_t pixelCount = 0;
            size_t byteSize = 0;
            if (!CheckedMultiply(static_cast<size_t>(targetWidth), static_cast<size_t>(targetHeight), pixelCount) ||
                !CheckedMultiply(pixelCount, channels, byteSize))
            {
                return false;
            }

            outMip.Width = targetWidth;
            outMip.Height = targetHeight;
            outMip.Bytes.assign(byteSize, 0);

            for (uint32_t y = 0; y < targetHeight; ++y)
            {
                for (uint32_t x = 0; x < targetWidth; ++x)
                {
                    const uint32_t sourceXBegin = ComputeCoverageStart(source.Width, targetWidth, x);
                    const uint32_t sourceXEnd = ComputeCoverageEnd(source.Width, targetWidth, x);
                    const uint32_t sourceYBegin = ComputeCoverageStart(source.Height, targetHeight, y);
                    const uint32_t sourceYEnd = ComputeCoverageEnd(source.Height, targetHeight, y);
                    const uint64_t sampleCount = static_cast<uint64_t>(sourceXEnd - sourceXBegin) *
                                                 static_cast<uint64_t>(sourceYEnd - sourceYBegin);
                    uint8_t *target = outMip.Bytes.data() + (static_cast<size_t>(y) * targetWidth + x) * channels;

                    for (uint32_t channelIndex = 0; channelIndex < channels; ++channelIndex)
                    {
                        uint64_t byteSum = 0;
                        double srgbLinearSum = 0.0;
                        for (uint32_t sourceY = sourceYBegin; sourceY < sourceYEnd; ++sourceY)
                        {
                            for (uint32_t sourceX = sourceXBegin; sourceX < sourceXEnd; ++sourceX)
                            {
                                const uint8_t value = GetPixel(source, sourceX, sourceY, channels)[channelIndex];
                                if (format.bSrgb && channelIndex < 3)
                                {
                                    srgbLinearSum += Srgb8ToLinear(value);
                                }
                                else
                                {
                                    byteSum += value;
                                }
                            }
                        }

                        if (format.bSrgb && channelIndex < 3)
                        {
                            target[channelIndex] = AverageSrgbSum(srgbLinearSum, sampleCount);
                        }
                        else
                        {
                            target[channelIndex] = AverageByteSum(byteSum, sampleCount);
                        }
                    }
                }
            }

            return true;
        }

        bool BuildMipChain(MipImage baseMip,
                           const TextureFormatInfo &format,
                           VariableArray<MipImage> &outMips,
                           ErrorString &error)
        {
            outMips.clear();
            outMips.push_back(std::move(baseMip));

            while (outMips.back().Width > 1 || outMips.back().Height > 1)
            {
                MipImage nextMip;
                if (!BuildNextMip(outMips.back(), format, nextMip))
                {
                    error = "texture mip generation overflow";
                    return false;
                }

                outMips.push_back(std::move(nextMip));
            }

            const uint32_t expectedMipCount = ComputeCookedTextureFullMipCount(outMips.front().Width, outMips.front().Height);
            if (outMips.size() != expectedMipCount)
            {
                error = "texture mip generation did not produce a full mip chain";
                return false;
            }

            return true;
        }

        // ミップの並びを NVTEX に詰める。各ミップのバイト数は形式のブロック単位の大きさと一致していなければならない。
        // ブロック圧縮と R16 は v0.1 以上、それ以外は v0.0 で書ける。
        // versionMinor が v0.2 のときは、タイルより小さくない段を標準ブロック形状のタイル単位（ミップごと、行優先）に
        // 並べ直し、残りの段（ミップテイル）を行優先のまま続け、タイルの表を持つヘッダで書く。
        bool BuildNvtexBytes(const VariableArray<MipImage> &mips,
                             CookedTexturePixelFormat pixelFormat,
                             CookedTextureColorSpace colorSpace,
                             uint16_t versionMinor,
                             ByteArray &outBytes,
                             ErrorString &error)
        {
            if (mips.empty() || mips.front().Width == 0 || mips.front().Height == 0)
            {
                error = "texture has no mip data";
                return false;
            }

            const size_t mipCount = mips.size();
            if (mipCount > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
            {
                error = "texture mip count is too large";
                return false;
            }

            const bool bTiled = versionMinor == Format::VersionMinorTiled;
            const size_t headerSize = bTiled ? Format::HeaderSizeTiled : Format::HeaderSize;

            size_t mipTableSize = 0;
            if (!CheckedMultiply(mipCount, Format::MipRecordSize, mipTableSize))
            {
                error = "texture mip table size overflow";
                return false;
            }

            // v0.2: タイルの段の数とタイルの総数を形式から決める。
            uint32_t firstTailMip = 0;
            size_t tileCount = 0;
            if (bTiled)
            {
                if (mipCount != ComputeCookedTextureFullMipCount(mips.front().Width, mips.front().Height))
                {
                    error = "texture mip chain must be complete for tiled layout";
                    return false;
                }

                firstTailMip = ComputeCookedTextureFirstTailMip(pixelFormat, mips.front().Width, mips.front().Height);
                for (uint32_t mipIndex = 0; mipIndex < firstTailMip; ++mipIndex)
                {
                    uint32_t tilesX = 0;
                    uint32_t tilesY = 0;
                    size_t mipTiles = 0;
                    if (!ComputeCookedTextureTileGrid(pixelFormat, mips[mipIndex].Width, mips[mipIndex].Height, tilesX, tilesY) ||
                        !CheckedMultiply(static_cast<size_t>(tilesX), static_cast<size_t>(tilesY), mipTiles) ||
                        !CheckedAdd(tileCount, mipTiles, tileCount))
                    {
                        error = "texture tile count overflow";
                        return false;
                    }
                }
            }

            size_t tileTableSize = 0;
            if (!CheckedMultiply(tileCount, Format::TileRecordSize, tileTableSize))
            {
                error = "texture tile table size overflow";
                return false;
            }

            size_t tileTableOffset = 0;
            size_t payloadOffset = 0;
            if (!CheckedAdd(headerSize, mipTableSize, tileTableOffset) ||
                !CheckedAdd(tileTableOffset, tileTableSize, payloadOffset))
            {
                error = "texture payload offset overflow";
                return false;
            }

            size_t payloadSize = 0;
            for (const MipImage &mip : mips)
            {
                uint64_t rowBytes = 0;
                uint64_t rowCount = 0;
                if (!ComputeCookedTextureMipLayout(pixelFormat, mip.Width, mip.Height, rowBytes, rowCount))
                {
                    error = "この形式にはミップの並びを決められません";
                    return false;
                }

                size_t expectedSize = 0;
                if (!CheckedMultiply(static_cast<size_t>(rowBytes), static_cast<size_t>(rowCount), expectedSize))
                {
                    error = "texture mip byte size overflow";
                    return false;
                }

                if (mip.Bytes.size() != expectedSize)
                {
                    error = "texture mip byte size mismatch";
                    return false;
                }

                if (!CheckedAdd(payloadSize, mip.Bytes.size(), payloadSize))
                {
                    error = "texture payload size overflow";
                    return false;
                }
            }

            if (payloadSize == 0)
            {
                error = "texture payload must not be empty";
                return false;
            }

            size_t fileSize = 0;
            if (!CheckedAdd(payloadOffset, payloadSize, fileSize))
            {
                error = "texture file size overflow";
                return false;
            }

            outBytes.assign(fileSize, 0);
            std::memcpy(outBytes.data() + HeaderOffset::Magic, Format::Magic, Format::MagicSize);
            WriteLe32(outBytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(headerSize));
            WriteLe16(outBytes, HeaderOffset::VersionMajor, Format::VersionMajor);
            WriteLe16(outBytes, HeaderOffset::VersionMinor, versionMinor);
            WriteLe32(outBytes, HeaderOffset::EndianMarker, Format::EndianMarker);
            WriteLe32(outBytes, HeaderOffset::MipRecordSize, static_cast<uint32_t>(Format::MipRecordSize));
            WriteLe64(outBytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
            WriteLe64(outBytes, HeaderOffset::MipTableOffset, static_cast<uint64_t>(headerSize));
            WriteLe64(outBytes, HeaderOffset::MipTableSize, static_cast<uint64_t>(mipTableSize));
            WriteLe64(outBytes, HeaderOffset::PayloadOffset, static_cast<uint64_t>(payloadOffset));
            WriteLe64(outBytes, HeaderOffset::PayloadSize, static_cast<uint64_t>(payloadSize));
            WriteLe32(outBytes, HeaderOffset::Width, mips.front().Width);
            WriteLe32(outBytes, HeaderOffset::Height, mips.front().Height);
            WriteLe32(outBytes, HeaderOffset::LayerCount, 1);
            WriteLe32(outBytes, HeaderOffset::MipCount, static_cast<uint32_t>(mipCount));
            WriteLe32(outBytes, HeaderOffset::PixelFormat, static_cast<uint32_t>(pixelFormat));
            WriteLe32(outBytes, HeaderOffset::ColorSpace, static_cast<uint32_t>(colorSpace));
            WriteLe32(outBytes, HeaderOffset::Flags, 0);
            WriteLe32(outBytes, HeaderOffset::Reserved0, 0);
            WriteLe64(outBytes, HeaderOffset::Reserved1, 0);

            size_t payloadCursor = payloadOffset;
            size_t tileRecordIndex = 0;
            size_t tailOffset = 0;
            for (size_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
            {
                const MipImage &mip = mips[mipIndex];
                const size_t recordOffset = headerSize + mipIndex * Format::MipRecordSize;
                WriteLe64(outBytes, recordOffset + MipRecordOffset::DataOffset, static_cast<uint64_t>(payloadCursor));
                WriteLe64(outBytes, recordOffset + MipRecordOffset::DataSize, static_cast<uint64_t>(mip.Bytes.size()));
                WriteLe32(outBytes, recordOffset + MipRecordOffset::Width, mip.Width);
                WriteLe32(outBytes, recordOffset + MipRecordOffset::Height, mip.Height);
                WriteLe32(outBytes, recordOffset + MipRecordOffset::Reserved0, 0);
                WriteLe32(outBytes, recordOffset + MipRecordOffset::Reserved1, 0);

                if (bTiled && mipIndex < firstTailMip)
                {
                    uint32_t tilesX = 0;
                    uint32_t tilesY = 0;
                    (void)ComputeCookedTextureTileGrid(pixelFormat, mip.Width, mip.Height, tilesX, tilesY);
                    size_t tileCursor = payloadCursor;
                    for (uint32_t tileY = 0; tileY < tilesY; ++tileY)
                    {
                        for (uint32_t tileX = 0; tileX < tilesX; ++tileX)
                        {
                            CookedTextureTileRect rect;
                            if (!ComputeCookedTextureTileRect(pixelFormat, mip.Width, mip.Height, tileX, tileY, rect))
                            {
                                error = "texture tile rect is invalid";
                                return false;
                            }

                            GatherCookedTextureTile(pixelFormat, mip.Width, rect, mip.Bytes.data(), outBytes.data() + tileCursor);

                            const size_t tileRecordOffset = tileTableOffset + tileRecordIndex * Format::TileRecordSize;
                            WriteLe64(outBytes, tileRecordOffset + TileRecordOffset::DataOffset, static_cast<uint64_t>(tileCursor));
                            WriteLe64(outBytes, tileRecordOffset + TileRecordOffset::DataSize, rect.DataBytes);
                            WriteLe32(outBytes, tileRecordOffset + TileRecordOffset::MipIndex, static_cast<uint32_t>(mipIndex));
                            WriteLe32(outBytes, tileRecordOffset + TileRecordOffset::LayerIndex, 0);
                            WriteLe32(outBytes, tileRecordOffset + TileRecordOffset::TileX, tileX);
                            WriteLe32(outBytes, tileRecordOffset + TileRecordOffset::TileY, tileY);

                            tileCursor += static_cast<size_t>(rect.DataBytes);
                            ++tileRecordIndex;
                        }
                    }

                    if (tileCursor != payloadCursor + mip.Bytes.size())
                    {
                        error = "texture tiles do not cover the mip";
                        return false;
                    }
                }
                else
                {
                    if (bTiled && mipIndex == firstTailMip)
                    {
                        tailOffset = payloadCursor;
                    }

                    std::memcpy(outBytes.data() + payloadCursor, mip.Bytes.data(), mip.Bytes.size());
                }
                payloadCursor += mip.Bytes.size();
            }

            if (bTiled)
            {
                if (tileRecordIndex != tileCount || tailOffset == 0)
                {
                    error = "texture tile table is inconsistent";
                    return false;
                }

                const CookedTextureTileShape shape = GetCookedTextureStandardTileShape(pixelFormat);
                WriteLe32(outBytes, TiledHeaderOffset::TileWidth, shape.Width);
                WriteLe32(outBytes, TiledHeaderOffset::TileHeight, shape.Height);
                WriteLe32(outBytes, TiledHeaderOffset::FirstTailMip, firstTailMip);
                WriteLe32(outBytes, TiledHeaderOffset::TileDataBytes, Format::StandardTileBytes);
                WriteLe64(outBytes, TiledHeaderOffset::TileTableOffset, static_cast<uint64_t>(tileTableOffset));
                WriteLe64(outBytes, TiledHeaderOffset::TileTableSize, static_cast<uint64_t>(tileTableSize));
                WriteLe64(outBytes, TiledHeaderOffset::TailOffset, static_cast<uint64_t>(tailOffset));
                WriteLe64(outBytes, TiledHeaderOffset::TailSize, static_cast<uint64_t>(payloadOffset + payloadSize - tailOffset));
            }

            WriteLe64(outBytes,
                      HeaderOffset::PayloadHash,
                      ComputeCookedTexturePayloadHash(outBytes.data() + payloadOffset, payloadSize));
            return true;
        }

        // ---- 用途別のクック(NVTEX v0.2。標準ブロック形状のタイル配置) ----

        // 16 ビット 1 チャンネルのミップ(R16 の高さ用)。
        struct MipImage16
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            VariableArray<uint16_t> Values;
        };

        // 3 成分 float のミップ(法線用)。xyz は -1..1 のベクトルで、最初のミップだけ非正規化のまま持つ。
        struct MipImageVec3
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            VariableArray<float> Xyz;
        };

        ErrorString MakeSourceError(const char *what, const ErrorString &sourceName)
        {
            ErrorString message = what;
            if (!sourceName.empty())
            {
                message += ": ";
                message += sourceName;
            }
            return message;
        }

        void ComputeMipSize(uint32_t sourceWidth, uint32_t sourceHeight, uint32_t &outWidth, uint32_t &outHeight)
        {
            outWidth = sourceWidth > 1 ? sourceWidth / 2 : 1;
            outHeight = sourceHeight > 1 ? sourceHeight / 2 : 1;
        }

        // RGBA8 の 1 ミップをブロック圧縮し、NVTEX に詰められるバイト列にする。
        bool CompressMipImage(const MipImage &rgbaMip,
                              const BlockCompressParams &params,
                              MipImage &outCompressed,
                              ErrorString &error)
        {
            ByteArray blocks;
            ErrorString compressError;
            if (!CompressRGBA8(rgbaMip.Bytes.data(), rgbaMip.Width, rgbaMip.Height, params, blocks, compressError))
            {
                error = compressError;
                return false;
            }

            outCompressed.Width = rgbaMip.Width;
            outCompressed.Height = rgbaMip.Height;
            outCompressed.Bytes.assign(blocks.data(), blocks.data() + blocks.size());
            return true;
        }

        bool CompressMipChain(const VariableArray<MipImage> &rgbaMips,
                              const BlockCompressParams &params,
                              VariableArray<MipImage> &outCompressed,
                              ErrorString &error)
        {
            outCompressed.clear();
            outCompressed.reserve(rgbaMips.size());
            for (const MipImage &mip : rgbaMips)
            {
                MipImage compressed;
                if (!CompressMipImage(mip, params, compressed, error))
                {
                    return false;
                }
                outCompressed.push_back(std::move(compressed));
            }
            return true;
        }

        // 1 チャンネルのミップを、R に値を持つ RGBA8 に広げる(G・B は 0、A は 255)。BC4 は R だけを読む。
        VariableArray<MipImage> ExpandRChainToRgba8(const VariableArray<MipImage> &r8Mips)
        {
            VariableArray<MipImage> expanded;
            expanded.reserve(r8Mips.size());
            for (const MipImage &r8 : r8Mips)
            {
                MipImage rgba;
                rgba.Width = r8.Width;
                rgba.Height = r8.Height;
                const size_t pixelCount = static_cast<size_t>(r8.Width) * r8.Height;
                rgba.Bytes.assign(pixelCount * 4, 0);
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    rgba.Bytes[i * 4 + 0] = r8.Bytes[i];
                    rgba.Bytes[i * 4 + 3] = 255;
                }
                expanded.push_back(std::move(rgba));
            }
            return expanded;
        }

        // 16 ビットで読み込み、1 チャンネルへ変換する。8 ビットの入力は 0..255 を 0..65535 へ拡大する(x * 257)。
        bool DecodeSourceImage16(const TextureSourceImage &source, MipImage16 &outBase, ErrorString &error)
        {
            if (!source.IsPresent())
            {
                error = "テクスチャの入力が空です";
                return false;
            }

            if (source.Size > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                error = "テクスチャの入力が stb_image の扱える大きさを超えています";
                return false;
            }

            int width = 0;
            int height = 0;
            int sourceChannels = 0;
            stbi_us *decoded = stbi_load_16_from_memory(source.Bytes,
                                                        static_cast<int>(source.Size),
                                                        &width,
                                                        &height,
                                                        &sourceChannels,
                                                        1);
            TUniquePtr<stbi_us, decltype(&stbi_image_free)> decodedOwner(decoded, stbi_image_free);
            if (decoded == nullptr)
            {
                error = MakeSourceError("テクスチャの入力を復号できません", source.Name);
                const char *reason = stbi_failure_reason();
                if (reason != nullptr)
                {
                    error += ": ";
                    error += reason;
                }
                return false;
            }

            if (width <= 0 || height <= 0)
            {
                error = "復号したテクスチャの大きさが不正です";
                return false;
            }

            size_t pixelCount = 0;
            if (!CheckedMultiply(static_cast<size_t>(width), static_cast<size_t>(height), pixelCount))
            {
                error = "復号したテクスチャの大きさが桁あふれしました";
                return false;
            }

            outBase.Width = static_cast<uint32_t>(width);
            outBase.Height = static_cast<uint32_t>(height);
            outBase.Values.assign(decoded, decoded + pixelCount);
            return true;
        }

        void BuildNextMip16(const MipImage16 &source, MipImage16 &outMip)
        {
            ComputeMipSize(source.Width, source.Height, outMip.Width, outMip.Height);
            outMip.Values.assign(static_cast<size_t>(outMip.Width) * outMip.Height, 0);

            for (uint32_t y = 0; y < outMip.Height; ++y)
            {
                for (uint32_t x = 0; x < outMip.Width; ++x)
                {
                    const uint32_t xBegin = ComputeCoverageStart(source.Width, outMip.Width, x);
                    const uint32_t xEnd = ComputeCoverageEnd(source.Width, outMip.Width, x);
                    const uint32_t yBegin = ComputeCoverageStart(source.Height, outMip.Height, y);
                    const uint32_t yEnd = ComputeCoverageEnd(source.Height, outMip.Height, y);
                    const uint64_t sampleCount = static_cast<uint64_t>(xEnd - xBegin) * (yEnd - yBegin);

                    uint64_t sum = 0;
                    for (uint32_t sy = yBegin; sy < yEnd; ++sy)
                    {
                        for (uint32_t sx = xBegin; sx < xEnd; ++sx)
                        {
                            sum += source.Values[static_cast<size_t>(sy) * source.Width + sx];
                        }
                    }
                    outMip.Values[static_cast<size_t>(y) * outMip.Width + x] =
                        static_cast<uint16_t>((sum + sampleCount / 2u) / sampleCount);
                }
            }
        }

        bool BuildHeight16Mips(MipImage16 base, VariableArray<MipImage> &outMips, ErrorString &error)
        {
            VariableArray<MipImage16> chain;
            chain.push_back(std::move(base));
            while (chain.back().Width > 1 || chain.back().Height > 1)
            {
                MipImage16 next;
                BuildNextMip16(chain.back(), next);
                chain.push_back(std::move(next));
            }

            if (chain.size() != ComputeCookedTextureFullMipCount(chain.front().Width, chain.front().Height))
            {
                error = "テクスチャのミップが最後まで作れませんでした";
                return false;
            }

            outMips.clear();
            outMips.reserve(chain.size());
            for (const MipImage16 &level : chain)
            {
                MipImage mip;
                mip.Width = level.Width;
                mip.Height = level.Height;
                mip.Bytes.resize(level.Values.size() * 2);
                for (size_t i = 0; i < level.Values.size(); ++i)
                {
                    mip.Bytes[i * 2 + 0] = static_cast<uint8_t>(level.Values[i] & 0xffu);
                    mip.Bytes[i * 2 + 1] = static_cast<uint8_t>((level.Values[i] >> 8) & 0xffu);
                }
                outMips.push_back(std::move(mip));
            }
            return true;
        }

        float ByteToSigned(uint8_t value)
        {
            return static_cast<float>(value) / 255.0f * 2.0f - 1.0f;
        }

        uint8_t SignedToByte(float value)
        {
            const float scaled = std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f;
            return static_cast<uint8_t>(std::clamp(std::lround(scaled), 0l, 255l));
        }

        // 長さが 0 に近いときは平らな法線(0,0,1)に倒す。
        void NormalizeVec3(float *xyz)
        {
            const float lengthSquared = xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2];
            if (lengthSquared < 1.0e-12f)
            {
                xyz[0] = 0.0f;
                xyz[1] = 0.0f;
                xyz[2] = 1.0f;
                return;
            }

            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            xyz[0] *= inverseLength;
            xyz[1] *= inverseLength;
            xyz[2] *= inverseLength;
        }

        void BuildNextMipNormal(const MipImageVec3 &source, MipImageVec3 &outMip)
        {
            ComputeMipSize(source.Width, source.Height, outMip.Width, outMip.Height);
            outMip.Xyz.assign(static_cast<size_t>(outMip.Width) * outMip.Height * 3, 0.0f);

            for (uint32_t y = 0; y < outMip.Height; ++y)
            {
                for (uint32_t x = 0; x < outMip.Width; ++x)
                {
                    const uint32_t xBegin = ComputeCoverageStart(source.Width, outMip.Width, x);
                    const uint32_t xEnd = ComputeCoverageEnd(source.Width, outMip.Width, x);
                    const uint32_t yBegin = ComputeCoverageStart(source.Height, outMip.Height, y);
                    const uint32_t yEnd = ComputeCoverageEnd(source.Height, outMip.Height, y);

                    // 平均は非正規化のベクトルで取り、最後に 1 回だけ再正規化する(短いベクトルほど重みが小さい)。
                    double sum[3] = {0.0, 0.0, 0.0};
                    for (uint32_t sy = yBegin; sy < yEnd; ++sy)
                    {
                        for (uint32_t sx = xBegin; sx < xEnd; ++sx)
                        {
                            const float *sample = source.Xyz.data() + (static_cast<size_t>(sy) * source.Width + sx) * 3;
                            sum[0] += sample[0];
                            sum[1] += sample[1];
                            sum[2] += sample[2];
                        }
                    }

                    float *target = outMip.Xyz.data() + (static_cast<size_t>(y) * outMip.Width + x) * 3;
                    target[0] = static_cast<float>(sum[0]);
                    target[1] = static_cast<float>(sum[1]);
                    target[2] = static_cast<float>(sum[2]);
                    NormalizeVec3(target);
                }
            }
        }

        // 法線を量子化した RGBA8(R=x・G=y・B=z・A=255)にする。BC5 は R・G だけを使う。
        void QuantizeNormalMip(const MipImageVec3 &level, MipImage &outRgba)
        {
            outRgba.Width = level.Width;
            outRgba.Height = level.Height;
            const size_t pixelCount = static_cast<size_t>(level.Width) * level.Height;
            outRgba.Bytes.assign(pixelCount * 4, 255);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                outRgba.Bytes[i * 4 + 0] = SignedToByte(level.Xyz[i * 3 + 0]);
                outRgba.Bytes[i * 4 + 1] = SignedToByte(level.Xyz[i * 3 + 1]);
                outRgba.Bytes[i * 4 + 2] = SignedToByte(level.Xyz[i * 3 + 2]);
            }
        }

        bool BuildNormalMips(const MipImage &baseRgba, VariableArray<MipImage> &outRgbaMips, ErrorString &error)
        {
            const size_t pixelCount = static_cast<size_t>(baseRgba.Width) * baseRgba.Height;
            VariableArray<MipImageVec3> chain;
            MipImageVec3 base;
            base.Width = baseRgba.Width;
            base.Height = baseRgba.Height;
            base.Xyz.resize(pixelCount * 3);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                base.Xyz[i * 3 + 0] = ByteToSigned(baseRgba.Bytes[i * 4 + 0]);
                base.Xyz[i * 3 + 1] = ByteToSigned(baseRgba.Bytes[i * 4 + 1]);
                base.Xyz[i * 3 + 2] = ByteToSigned(baseRgba.Bytes[i * 4 + 2]);
            }
            chain.push_back(std::move(base));

            while (chain.back().Width > 1 || chain.back().Height > 1)
            {
                MipImageVec3 next;
                BuildNextMipNormal(chain.back(), next);
                chain.push_back(std::move(next));
            }

            if (chain.size() != ComputeCookedTextureFullMipCount(chain.front().Width, chain.front().Height))
            {
                error = "テクスチャのミップが最後まで作れませんでした";
                return false;
            }

            outRgbaMips.clear();
            outRgbaMips.reserve(chain.size());
            for (const MipImageVec3 &level : chain)
            {
                MipImage rgba;
                QuantizeNormalMip(level, rgba);
                outRgbaMips.push_back(std::move(rgba));
            }
            return true;
        }

        // ORM の 3 枠を R(AO)・G(粗さ)・B(メタリック)に詰めた RGBA8 を作る。
        // 無い枠は AO=1・粗さ=1・メタリック=0。大きさは存在する枠で一致していなければならない。
        bool PackOrmBase(const OrmSourceImages &orm, MipImage &outBase, ErrorString &error)
        {
            const TextureSourceImage *slots[3] = {&orm.Ao, &orm.Roughness, &orm.Metallic};
            const uint8_t missingValues[3] = {255, 255, 0};

            MipImage decoded[3];
            bool bAnyPresent = false;
            uint32_t width = 0;
            uint32_t height = 0;
            TextureFormatInfo singleChannel;
            singleChannel.PixelFormat = CookedTexturePixelFormat::R8UNorm;
            singleChannel.OutputChannels = 1;

            for (int slot = 0; slot < 3; ++slot)
            {
                if (!slots[slot]->IsPresent())
                {
                    continue;
                }

                if (!DecodeSourceImage(slots[slot]->Bytes, slots[slot]->Size, singleChannel, slots[slot]->Name,
                                       decoded[slot], error))
                {
                    return false;
                }

                if (!bAnyPresent)
                {
                    bAnyPresent = true;
                    width = decoded[slot].Width;
                    height = decoded[slot].Height;
                }
                else if (decoded[slot].Width != width || decoded[slot].Height != height)
                {
                    error = MakeSourceError("ORM の元画像は同じ大きさにしてください", slots[slot]->Name);
                    return false;
                }
            }

            if (!bAnyPresent)
            {
                error = "ORM には AO・粗さ・メタリックのどれか 1 枚が要ります";
                return false;
            }

            const size_t pixelCount = static_cast<size_t>(width) * height;
            outBase.Width = width;
            outBase.Height = height;
            outBase.Bytes.assign(pixelCount * 4, 255);
            for (int slot = 0; slot < 3; ++slot)
            {
                const bool bPresent = slots[slot]->IsPresent();
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    outBase.Bytes[i * 4 + slot] = bPresent ? decoded[slot].Bytes[i] : missingValues[slot];
                }
            }
            return true;
        }

        bool CookBlockCompressedUsage(const TextureSourceImage &source,
                                      const OrmSourceImages &orm,
                                      const TextureUsageCookParams &params,
                                      TextureCookResult &outResult,
                                      ErrorString &error)
        {
            BlockCompressParams compress;
            compress.Quality = params.Quality;
            compress.ThreadCount = params.ThreadCount;

            CookedTexturePixelFormat pixelFormat = CookedTexturePixelFormat::BC7;
            CookedTextureColorSpace colorSpace = CookedTextureColorSpace::Linear;
            const char *pixelFormatName = "BC7";
            VariableArray<MipImage> sourceMips;

            switch (params.Usage)
            {
            case TextureUsage::Albedo:
            {
                TextureFormatInfo info;
                info.PixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
                info.ColorSpace = CookedTextureColorSpace::SRGB;
                info.OutputChannels = 4;
                info.bSrgb = true;

                MipImage base;
                if (!DecodeSourceImage(source.Bytes, source.Size, info, source.Name, base, error) ||
                    !BuildMipChain(std::move(base), info, sourceMips, error))
                {
                    return false;
                }

                compress.Format = BlockFormat::BC7;
                compress.bPerceptual = true;
                colorSpace = CookedTextureColorSpace::SRGB;
                break;
            }
            case TextureUsage::Orm:
            {
                TextureFormatInfo info;
                info.PixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
                info.OutputChannels = 4;

                MipImage base;
                if (!PackOrmBase(orm, base, error) ||
                    !BuildMipChain(std::move(base), info, sourceMips, error))
                {
                    return false;
                }

                compress.Format = BlockFormat::BC7;
                break;
            }
            case TextureUsage::Normal:
            {
                TextureFormatInfo info;
                info.PixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
                info.OutputChannels = 4;

                MipImage base;
                if (!DecodeSourceImage(source.Bytes, source.Size, info, source.Name, base, error) ||
                    !BuildNormalMips(base, sourceMips, error))
                {
                    return false;
                }

                compress.Format = BlockFormat::BC5;
                pixelFormat = CookedTexturePixelFormat::BC5;
                pixelFormatName = "BC5";
                break;
            }
            case TextureUsage::Single:
            {
                TextureFormatInfo info;
                info.PixelFormat = CookedTexturePixelFormat::R8UNorm;
                info.OutputChannels = 1;

                MipImage base;
                VariableArray<MipImage> r8Mips;
                if (!DecodeSourceImage(source.Bytes, source.Size, info, source.Name, base, error) ||
                    !BuildMipChain(std::move(base), info, r8Mips, error))
                {
                    return false;
                }

                sourceMips = ExpandRChainToRgba8(r8Mips);
                compress.Format = BlockFormat::BC4;
                pixelFormat = CookedTexturePixelFormat::BC4;
                pixelFormatName = "BC4";
                break;
            }
            case TextureUsage::Height16:
                error = "height16 はブロック圧縮の用途ではありません";
                return false;
            }

            VariableArray<MipImage> compressedMips;
            if (!CompressMipChain(sourceMips, compress, compressedMips, error))
            {
                return false;
            }

            TextureCookResult result;
            result.Width = compressedMips.front().Width;
            result.Height = compressedMips.front().Height;
            result.MipCount = static_cast<uint32_t>(compressedMips.size());
            result.BytesPerPixel = 0;
            result.PixelFormatName = pixelFormatName;
            if (!BuildNvtexBytes(compressedMips, pixelFormat, colorSpace,
                                 Format::VersionMinorTiled, result.NvtexBytes, error))
            {
                return false;
            }

            outResult = std::move(result);
            return true;
        }

        bool CookHeight16Usage(const TextureSourceImage &source, TextureCookResult &outResult, ErrorString &error)
        {
            MipImage16 base;
            VariableArray<MipImage> mips;
            if (!DecodeSourceImage16(source, base, error) ||
                !BuildHeight16Mips(std::move(base), mips, error))
            {
                return false;
            }

            TextureCookResult result;
            result.Width = mips.front().Width;
            result.Height = mips.front().Height;
            result.MipCount = static_cast<uint32_t>(mips.size());
            result.BytesPerPixel = 2;
            result.PixelFormatName = "R16";
            if (!BuildNvtexBytes(mips, CookedTexturePixelFormat::R16UNorm, CookedTextureColorSpace::Linear,
                                 Format::VersionMinorTiled, result.NvtexBytes, error))
            {
                return false;
            }

            outResult = std::move(result);
            return true;
        }
    }

    bool IsSupportedTextureCookFormat(std::string_view format) noexcept
    {
        TextureFormatInfo ignored;
        return ParseTextureFormat(format, ignored);
    }

    bool CookTextureToNvtex(const uint8_t *sourceBytes,
                            size_t sourceSize,
                            std::string_view format,
                            const ErrorString &sourceName,
                            TextureCookResult &outResult,
                            ErrorString &error)
    {
        TextureFormatInfo formatInfo;
        if (!ParseTextureFormat(format, formatInfo))
        {
            error = ErrorString("未対応のテクスチャ形式です: ") + ErrorString(AnsiStringView(format.data(), format.size()));
            return false;
        }

        MipImage baseMip;
        if (!DecodeSourceImage(sourceBytes, sourceSize, formatInfo, sourceName, baseMip, error))
        {
            return false;
        }

        VariableArray<MipImage> mips;
        if (!BuildMipChain(std::move(baseMip), formatInfo, mips, error))
        {
            return false;
        }

        TextureCookResult result;
        result.Width = mips.front().Width;
        result.Height = mips.front().Height;
        result.MipCount = static_cast<uint32_t>(mips.size());
        result.BytesPerPixel = formatInfo.OutputChannels;
        result.PixelFormatName = formatInfo.OutputChannels == 4 ? "RGBA8" : (formatInfo.OutputChannels == 2 ? "RG8" : "R8");
        if (!BuildNvtexBytes(mips, formatInfo.PixelFormat, formatInfo.ColorSpace, Format::VersionMinor,
                             result.NvtexBytes, error))
        {
            return false;
        }

        outResult = std::move(result);
        return true;
    }

    bool ParseTextureUsage(Core::Container::AnsiStringView text, TextureUsage &outUsage) noexcept
    {
        if (text == Core::Container::AnsiStringView("albedo"))
        {
            outUsage = TextureUsage::Albedo;
            return true;
        }
        if (text == Core::Container::AnsiStringView("normal"))
        {
            outUsage = TextureUsage::Normal;
            return true;
        }
        if (text == Core::Container::AnsiStringView("orm"))
        {
            outUsage = TextureUsage::Orm;
            return true;
        }
        if (text == Core::Container::AnsiStringView("single"))
        {
            outUsage = TextureUsage::Single;
            return true;
        }
        if (text == Core::Container::AnsiStringView("height16"))
        {
            outUsage = TextureUsage::Height16;
            return true;
        }
        return false;
    }

    const char *GetTextureUsageName(TextureUsage usage) noexcept
    {
        switch (usage)
        {
        case TextureUsage::Albedo: return "albedo";
        case TextureUsage::Normal: return "normal";
        case TextureUsage::Orm: return "orm";
        case TextureUsage::Single: return "single";
        case TextureUsage::Height16: return "height16";
        }
        return "unknown";
    }

    const char *GetTextureUsageManifestFormat(TextureUsage usage) noexcept
    {
        switch (usage)
        {
        case TextureUsage::Albedo: return "nvtex.v0.2.bc7.srgb";
        case TextureUsage::Normal: return "nvtex.v0.2.bc5.linear";
        case TextureUsage::Orm: return "nvtex.v0.2.bc7.linear";
        case TextureUsage::Single: return "nvtex.v0.2.bc4.linear";
        case TextureUsage::Height16: return "nvtex.v0.2.r16.linear";
        }
        return "";
    }

    bool CookTextureForUsage(const TextureSourceImage &source,
                             const OrmSourceImages &orm,
                             const TextureUsageCookParams &params,
                             TextureCookResult &outResult,
                             ErrorString &error)
    {
        if (params.Usage == TextureUsage::Height16)
        {
            return CookHeight16Usage(source, outResult, error);
        }

        return CookBlockCompressedUsage(source, orm, params, outResult, error);
    }
}
