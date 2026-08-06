#include "AudioCooker.h"

#include "Asset/AssetBlob.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedAudioFormat.h"
#include "Container/Span.h"

#include <cstring>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        uint16_t ReadLe16(const uint8_t* data, size_t offset)
        {
            return static_cast<uint16_t>(data[offset]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
        }

        uint32_t ReadLe32(const uint8_t* data, size_t offset)
        {
            return static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        void WriteLe16(Core::Container::VariableArray<uint8_t>& bytes, size_t offset, uint16_t value)
        {
            bytes[offset] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        }

        void WriteLe32(Core::Container::VariableArray<uint8_t>& bytes, size_t offset, uint32_t value)
        {
            bytes[offset] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
            bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
            bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
        }

        void WriteLe64(Core::Container::VariableArray<uint8_t>& bytes, size_t offset, uint64_t value)
        {
            WriteLe32(bytes, offset, static_cast<uint32_t>(value));
            WriteLe32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
        }

        bool HasFourCc(const uint8_t* data, const char (&text)[5])
        {
            return std::memcmp(data, text, 4) == 0;
        }
    } // namespace

    bool IsSupportedAudioCookFormat(Core::Container::AnsiStringView format)
    {
        return format == Core::Container::AnsiStringView("nvaud.v0.pcm16");
    }

    bool CookWaveToNvaud(const uint8_t* sourceData,
                         size_t sourceSize,
                         Core::Container::AnsiStringView format,
                         AudioCookResult& outResult,
                         Core::Container::AnsiString& outError)
    {
        outResult = {};
        outError.clear();
        if (!IsSupportedAudioCookFormat(format))
        {
            outError = "unsupported audio cook format";
            return false;
        }
        if (sourceData == nullptr || sourceSize < 12 ||
            !HasFourCc(sourceData, "RIFF") || !HasFourCc(sourceData + 8, "WAVE"))
        {
            outError = "input is not a RIFF WAVE file";
            return false;
        }
        if (ReadLe32(sourceData, 4) != sourceSize - 8)
        {
            outError = "RIFF size does not match input size";
            return false;
        }

        bool bFoundFormat = false;
        bool bFoundData = false;
        uint16_t audioFormat = 0;
        uint16_t channelCount = 0;
        uint32_t sampleRate = 0;
        uint32_t byteRate = 0;
        uint16_t blockAlignment = 0;
        uint16_t bitsPerSample = 0;
        const uint8_t* pcmData = nullptr;
        size_t pcmSize = 0;
        size_t cursor = 12;
        while (cursor < sourceSize)
        {
            if (sourceSize - cursor < 8)
            {
                outError = "truncated WAVE chunk header";
                return false;
            }
            const uint32_t chunkSize = ReadLe32(sourceData, cursor + 4);
            const size_t chunkDataOffset = cursor + 8;
            if (chunkDataOffset > sourceSize || chunkSize > sourceSize - chunkDataOffset)
            {
                outError = "WAVE chunk exceeds input size";
                return false;
            }

            if (HasFourCc(sourceData + cursor, "fmt "))
            {
                if (bFoundFormat || chunkSize != 16)
                {
                    outError = "WAVE must contain one canonical 16-byte fmt chunk";
                    return false;
                }
                audioFormat = ReadLe16(sourceData, chunkDataOffset);
                channelCount = ReadLe16(sourceData, chunkDataOffset + 2);
                sampleRate = ReadLe32(sourceData, chunkDataOffset + 4);
                byteRate = ReadLe32(sourceData, chunkDataOffset + 8);
                blockAlignment = ReadLe16(sourceData, chunkDataOffset + 12);
                bitsPerSample = ReadLe16(sourceData, chunkDataOffset + 14);
                bFoundFormat = true;
            }
            else if (HasFourCc(sourceData + cursor, "data"))
            {
                if (bFoundData)
                {
                    outError = "WAVE must contain one data chunk";
                    return false;
                }
                pcmData = sourceData + chunkDataOffset;
                pcmSize = chunkSize;
                bFoundData = true;
            }

            const size_t paddedChunkSize = static_cast<size_t>(chunkSize) + (chunkSize & 1u);
            if (paddedChunkSize > sourceSize - chunkDataOffset)
            {
                outError = "WAVE chunk padding exceeds input size";
                return false;
            }
            cursor = chunkDataOffset + paddedChunkSize;
        }

        if (!bFoundFormat || !bFoundData)
        {
            outError = "WAVE requires fmt and data chunks";
            return false;
        }
        if (audioFormat != 1 || bitsPerSample != 16)
        {
            outError = "audio v0 requires signed PCM16";
            return false;
        }
        if (channelCount != 1 && channelCount != 2)
        {
            outError = "audio v0 requires mono or stereo";
            return false;
        }
        if (sampleRate != 44100 && sampleRate != 48000)
        {
            outError = "audio v0 requires 44.1 or 48 kHz";
            return false;
        }
        const uint16_t expectedBlockAlignment = static_cast<uint16_t>(channelCount * sizeof(int16_t));
        if (blockAlignment != expectedBlockAlignment || byteRate != sampleRate * blockAlignment)
        {
            outError = "WAVE block alignment or byte rate is inconsistent";
            return false;
        }
        if (pcmSize == 0 || pcmSize % blockAlignment != 0)
        {
            outError = "WAVE PCM payload is not strictly block aligned";
            return false;
        }
        if (pcmSize > std::numeric_limits<size_t>::max() - Core::Asset::CookedAudioFormatV0::HeaderSize)
        {
            outError = "audio payload size overflow";
            return false;
        }

        using namespace Core::Asset::CookedAudioFormatV0;
        outResult.NvaudBytes.assign(HeaderSize + pcmSize, 0);
        std::memcpy(outResult.NvaudBytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(outResult.NvaudBytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(outResult.NvaudBytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(outResult.NvaudBytes, HeaderOffset::VersionMinor, VersionMinor);
        WriteLe32(outResult.NvaudBytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe64(outResult.NvaudBytes, HeaderOffset::FileSize, outResult.NvaudBytes.size());
        WriteLe64(outResult.NvaudBytes, HeaderOffset::PayloadOffset, HeaderSize);
        WriteLe64(outResult.NvaudBytes, HeaderOffset::PayloadSize, pcmSize);
        WriteLe64(outResult.NvaudBytes,
                  HeaderOffset::PayloadHash,
                  Core::Asset::ComputeCookedAudioPayloadHash(pcmData, pcmSize));
        WriteLe32(outResult.NvaudBytes, HeaderOffset::SampleRate, sampleRate);
        WriteLe16(outResult.NvaudBytes, HeaderOffset::ChannelCount, channelCount);
        WriteLe16(outResult.NvaudBytes, HeaderOffset::BitsPerSample, bitsPerSample);
        WriteLe16(outResult.NvaudBytes, HeaderOffset::BlockAlignment, blockAlignment);
        std::memcpy(outResult.NvaudBytes.data() + HeaderSize, pcmData, pcmSize);

        const auto parsed = Core::Asset::ParseCookedAudio(Core::Asset::AssetBlob::CopyBytes(
            Core::Container::Span<const uint8_t>(outResult.NvaudBytes.data(), outResult.NvaudBytes.size())));
        if (!parsed.Succeeded())
        {
            outResult = {};
            outError = "cooked audio self-validation failed";
            return false;
        }

        outResult.SourceHash = Core::Asset::ComputeAssetPackagePayloadHash(sourceData, sourceSize);
        outResult.SampleRate = sampleRate;
        outResult.ChannelCount = channelCount;
        outResult.BlockAlignment = blockAlignment;
        outResult.FrameCount = pcmSize / blockAlignment;
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
