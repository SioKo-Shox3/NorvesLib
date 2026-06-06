#include "TextureCooker.h"

#include "Asset/CookedTextureFormat.h"

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
        using NorvesLib::Core::Asset::ComputeCookedTextureFullMipCount;
        using NorvesLib::Core::Asset::ComputeCookedTexturePayloadHash;
        using NorvesLib::Core::Asset::CookedTextureColorSpace;
        using NorvesLib::Core::Asset::CookedTexturePixelFormat;
        using NorvesLib::Core::Asset::GetCookedTextureBytesPerPixel;
        namespace Format = NorvesLib::Core::Asset::CookedTextureFormatV0;
        namespace HeaderOffset = NorvesLib::Core::Asset::CookedTextureFormatV0::HeaderOffset;
        namespace MipRecordOffset = NorvesLib::Core::Asset::CookedTextureFormatV0::MipRecordOffset;

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
            std::vector<uint8_t> Bytes;
        };

        void WriteLe16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value)
        {
            bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        }

        void WriteLe32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
        {
            bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
            bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
            bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
        }

        void WriteLe64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value)
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
                               std::string_view sourceName,
                               MipImage &outBaseMip,
                               std::string &error)
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
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decodedOwner(decoded, stbi_image_free);
            if (decoded == nullptr)
            {
                error = "failed to decode texture input";
                if (!sourceName.empty())
                {
                    error += ": ";
                    error += std::string(sourceName);
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
                           std::vector<MipImage> &outMips,
                           std::string &error)
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

        bool BuildNvtexBytes(const std::vector<MipImage> &mips,
                             const TextureFormatInfo &format,
                             std::vector<uint8_t> &outBytes,
                             std::string &error)
        {
            if (mips.empty() || mips.front().Width == 0 || mips.front().Height == 0)
            {
                error = "texture has no mip data";
                return false;
            }

            const size_t bytesPerPixel = GetCookedTextureBytesPerPixel(format.PixelFormat);
            if (bytesPerPixel == 0 || bytesPerPixel != format.OutputChannels)
            {
                error = "texture format byte size mismatch";
                return false;
            }

            const size_t mipCount = mips.size();
            if (mipCount > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
            {
                error = "texture mip count is too large";
                return false;
            }

            size_t mipTableSize = 0;
            if (!CheckedMultiply(mipCount, Format::MipRecordSize, mipTableSize))
            {
                error = "texture mip table size overflow";
                return false;
            }

            size_t payloadOffset = 0;
            if (!CheckedAdd(Format::HeaderSize, mipTableSize, payloadOffset))
            {
                error = "texture payload offset overflow";
                return false;
            }

            size_t payloadSize = 0;
            for (const MipImage &mip : mips)
            {
                size_t expectedPixelCount = 0;
                size_t expectedSize = 0;
                if (!CheckedMultiply(static_cast<size_t>(mip.Width), static_cast<size_t>(mip.Height), expectedPixelCount) ||
                    !CheckedMultiply(expectedPixelCount, bytesPerPixel, expectedSize))
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
            WriteLe32(outBytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(Format::HeaderSize));
            WriteLe16(outBytes, HeaderOffset::VersionMajor, Format::VersionMajor);
            WriteLe16(outBytes, HeaderOffset::VersionMinor, Format::VersionMinor);
            WriteLe32(outBytes, HeaderOffset::EndianMarker, Format::EndianMarker);
            WriteLe32(outBytes, HeaderOffset::MipRecordSize, static_cast<uint32_t>(Format::MipRecordSize));
            WriteLe64(outBytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
            WriteLe64(outBytes, HeaderOffset::MipTableOffset, static_cast<uint64_t>(Format::HeaderSize));
            WriteLe64(outBytes, HeaderOffset::MipTableSize, static_cast<uint64_t>(mipTableSize));
            WriteLe64(outBytes, HeaderOffset::PayloadOffset, static_cast<uint64_t>(payloadOffset));
            WriteLe64(outBytes, HeaderOffset::PayloadSize, static_cast<uint64_t>(payloadSize));
            WriteLe32(outBytes, HeaderOffset::Width, mips.front().Width);
            WriteLe32(outBytes, HeaderOffset::Height, mips.front().Height);
            WriteLe32(outBytes, HeaderOffset::LayerCount, 1);
            WriteLe32(outBytes, HeaderOffset::MipCount, static_cast<uint32_t>(mipCount));
            WriteLe32(outBytes, HeaderOffset::PixelFormat, static_cast<uint32_t>(format.PixelFormat));
            WriteLe32(outBytes, HeaderOffset::ColorSpace, static_cast<uint32_t>(format.ColorSpace));
            WriteLe32(outBytes, HeaderOffset::Flags, 0);
            WriteLe32(outBytes, HeaderOffset::Reserved0, 0);
            WriteLe64(outBytes, HeaderOffset::Reserved1, 0);

            size_t payloadCursor = payloadOffset;
            for (size_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
            {
                const MipImage &mip = mips[mipIndex];
                const size_t recordOffset = Format::HeaderSize + mipIndex * Format::MipRecordSize;
                WriteLe64(outBytes, recordOffset + MipRecordOffset::DataOffset, static_cast<uint64_t>(payloadCursor));
                WriteLe64(outBytes, recordOffset + MipRecordOffset::DataSize, static_cast<uint64_t>(mip.Bytes.size()));
                WriteLe32(outBytes, recordOffset + MipRecordOffset::Width, mip.Width);
                WriteLe32(outBytes, recordOffset + MipRecordOffset::Height, mip.Height);
                WriteLe32(outBytes, recordOffset + MipRecordOffset::Reserved0, 0);
                WriteLe32(outBytes, recordOffset + MipRecordOffset::Reserved1, 0);

                std::memcpy(outBytes.data() + payloadCursor, mip.Bytes.data(), mip.Bytes.size());
                payloadCursor += mip.Bytes.size();
            }

            WriteLe64(outBytes,
                      HeaderOffset::PayloadHash,
                      ComputeCookedTexturePayloadHash(outBytes.data() + payloadOffset, payloadSize));
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
                            std::string_view sourceName,
                            TextureCookResult &outResult,
                            std::string &error)
    {
        TextureFormatInfo formatInfo;
        if (!ParseTextureFormat(format, formatInfo))
        {
            error = "unsupported texture format: " + std::string(format);
            return false;
        }

        MipImage baseMip;
        if (!DecodeSourceImage(sourceBytes, sourceSize, formatInfo, sourceName, baseMip, error))
        {
            return false;
        }

        std::vector<MipImage> mips;
        if (!BuildMipChain(std::move(baseMip), formatInfo, mips, error))
        {
            return false;
        }

        TextureCookResult result;
        result.Width = mips.front().Width;
        result.Height = mips.front().Height;
        result.MipCount = static_cast<uint32_t>(mips.size());
        result.BytesPerPixel = formatInfo.OutputChannels;
        if (!BuildNvtexBytes(mips, formatInfo, result.NvtexBytes, error))
        {
            return false;
        }

        outResult = std::move(result);
        return true;
    }
}
