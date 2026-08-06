#include "Asset/CookedAudioFormat.h"

#include <cstring>
#include <utility>

namespace NorvesLib::Core::Asset
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

        uint64_t ReadLe64(const uint8_t* data, size_t offset)
        {
            return static_cast<uint64_t>(ReadLe32(data, offset)) |
                   (static_cast<uint64_t>(ReadLe32(data, offset + 4)) << 32);
        }

        CookedAudioParseResult Fail(CookedAudioParseStatus status)
        {
            CookedAudioParseResult result;
            result.Status = status;
            return result;
        }
    }

    Container::Span<const uint8_t> CookedAudioData::GetPcmBytes() const noexcept
    {
        if (!SourceBlob.IsValid())
        {
            return {};
        }

        const auto bytes = SourceBlob.GetSpan();
        if (PayloadOffset > bytes.size() || PayloadSize > bytes.size() - PayloadOffset)
        {
            return {};
        }
        return Container::Span<const uint8_t>(bytes.data() + PayloadOffset, PayloadSize);
    }

    CookedAudioParseResult ParseCookedAudio(AssetBlob sourceBlob)
    {
        using namespace CookedAudioFormatV0;
        if (!sourceBlob.IsValid())
        {
            return Fail(CookedAudioParseStatus::InvalidBlob);
        }

        const auto bytes = sourceBlob.GetSpan();
        if (bytes.empty())
        {
            return Fail(CookedAudioParseStatus::EmptyBlob);
        }
        if (bytes.size() < HeaderSize)
        {
            return Fail(CookedAudioParseStatus::HeaderTooSmall);
        }
        if (std::memcmp(bytes.data(), Magic, MagicSize) != 0)
        {
            return Fail(CookedAudioParseStatus::BadMagic);
        }

        const uint8_t* data = bytes.data();
        if (ReadLe16(data, HeaderOffset::VersionMajor) != VersionMajor ||
            ReadLe16(data, HeaderOffset::VersionMinor) != VersionMinor)
        {
            return Fail(CookedAudioParseStatus::UnsupportedVersion);
        }
        if (ReadLe32(data, HeaderOffset::EndianMarker) != EndianMarker)
        {
            return Fail(CookedAudioParseStatus::EndianMismatch);
        }
        if (ReadLe32(data, HeaderOffset::HeaderSize) != HeaderSize)
        {
            return Fail(CookedAudioParseStatus::HeaderSizeMismatch);
        }
        if (ReadLe32(data, HeaderOffset::Reserved0) != 0 || ReadLe16(data, HeaderOffset::Reserved1) != 0)
        {
            return Fail(CookedAudioParseStatus::ReservedFieldNonZero);
        }
        if (ReadLe64(data, HeaderOffset::FileSize) != bytes.size())
        {
            return Fail(CookedAudioParseStatus::FileSizeMismatch);
        }

        const uint32_t sampleRate = ReadLe32(data, HeaderOffset::SampleRate);
        const uint16_t channelCount = ReadLe16(data, HeaderOffset::ChannelCount);
        const uint16_t bitsPerSample = ReadLe16(data, HeaderOffset::BitsPerSample);
        const uint16_t blockAlignment = ReadLe16(data, HeaderOffset::BlockAlignment);
        if (sampleRate != 44100 && sampleRate != 48000)
        {
            return Fail(CookedAudioParseStatus::UnsupportedSampleRate);
        }
        if (channelCount != 1 && channelCount != 2)
        {
            return Fail(CookedAudioParseStatus::UnsupportedChannelCount);
        }
        if (bitsPerSample != 16)
        {
            return Fail(CookedAudioParseStatus::UnsupportedBitsPerSample);
        }
        const uint16_t expectedBlockAlignment = static_cast<uint16_t>(channelCount * sizeof(int16_t));
        if (blockAlignment != expectedBlockAlignment)
        {
            return Fail(CookedAudioParseStatus::BlockAlignmentMismatch);
        }

        const uint64_t payloadOffset64 = ReadLe64(data, HeaderOffset::PayloadOffset);
        const uint64_t payloadSize64 = ReadLe64(data, HeaderOffset::PayloadSize);
        if (payloadSize64 == 0)
        {
            return Fail(CookedAudioParseStatus::InvalidPayloadSize);
        }
        if (payloadSize64 % blockAlignment != 0)
        {
            return Fail(CookedAudioParseStatus::PayloadBlockMisaligned);
        }
        if (payloadOffset64 != HeaderSize || payloadOffset64 > bytes.size() ||
            payloadSize64 > static_cast<uint64_t>(bytes.size()) - payloadOffset64)
        {
            return Fail(CookedAudioParseStatus::PayloadOutOfRange);
        }

        const size_t payloadOffset = static_cast<size_t>(payloadOffset64);
        const size_t payloadSize = static_cast<size_t>(payloadSize64);
        const uint64_t payloadHash = ReadLe64(data, HeaderOffset::PayloadHash);
        if (payloadHash != ComputeCookedAudioPayloadHash(data + payloadOffset, payloadSize))
        {
            return Fail(CookedAudioParseStatus::PayloadHashMismatch);
        }

        CookedAudioParseResult result;
        result.Status = CookedAudioParseStatus::Success;
        result.Audio.SourceBlob = std::move(sourceBlob);
        result.Audio.SampleRate = sampleRate;
        result.Audio.ChannelCount = channelCount;
        result.Audio.BitsPerSample = bitsPerSample;
        result.Audio.BlockAlignment = blockAlignment;
        result.Audio.FrameCount = payloadSize / blockAlignment;
        result.Audio.PayloadHash = payloadHash;
        result.Audio.PayloadOffset = payloadOffset;
        result.Audio.PayloadSize = payloadSize;
        return result;
    }
} // namespace NorvesLib::Core::Asset
