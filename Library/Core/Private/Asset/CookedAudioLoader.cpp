#include "Asset/CookedAudioFormat.h"

#include <bit>
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

        /// ペイロードの範囲を検査してバイト列を返す。
        Container::Span<const uint8_t> GetPayloadBytes(const CookedAudioData& audio) noexcept
        {
            if (!audio.SourceBlob.IsValid())
            {
                return {};
            }

            const auto bytes = audio.SourceBlob.GetSpan();
            if (audio.PayloadOffset > bytes.size() || audio.PayloadSize > bytes.size() - audio.PayloadOffset)
            {
                return {};
            }
            return Container::Span<const uint8_t>(bytes.data() + audio.PayloadOffset, audio.PayloadSize);
        }
    }

    Container::Span<const uint8_t> CookedAudioData::GetPcmBytes() const noexcept
    {
        if (Codec != CookedAudioCodec::Pcm16)
        {
            return {};
        }
        return GetPayloadBytes(*this);
    }

    Container::Span<const uint8_t> CookedAudioData::GetEncodedBytes() const noexcept
    {
        return GetPayloadBytes(*this);
    }

    CookedAudioParseResult ParseCookedAudioV0(AssetBlob sourceBlob)
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
        result.Audio.TotalFrames = result.Audio.FrameCount;
        result.Audio.PayloadHash = payloadHash;
        result.Audio.PayloadOffset = payloadOffset;
        result.Audio.PayloadSize = payloadSize;
        return result;
    }

    CookedAudioParseResult ParseCookedAudioV1(AssetBlob sourceBlob)
    {
        using namespace CookedAudioFormatV1;
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
        const uint16_t flags = ReadLe16(data, HeaderOffset::Flags);
        if (ReadLe32(data, HeaderOffset::Reserved) != 0 ||
            (flags & static_cast<uint16_t>(~KnownFlagsMask)) != 0)
        {
            return Fail(CookedAudioParseStatus::ReservedFieldNonZero);
        }
        if (ReadLe64(data, HeaderOffset::FileSize) != bytes.size())
        {
            return Fail(CookedAudioParseStatus::FileSizeMismatch);
        }

        const uint16_t codecValue = ReadLe16(data, HeaderOffset::Codec);
        if (codecValue != static_cast<uint16_t>(CookedAudioCodec::Pcm16) &&
            codecValue != static_cast<uint16_t>(CookedAudioCodec::Vorbis))
        {
            return Fail(CookedAudioParseStatus::UnsupportedCodec);
        }
        const auto codec = static_cast<CookedAudioCodec>(codecValue);
        const uint32_t sampleRate = ReadLe32(data, HeaderOffset::SampleRate);
        if (sampleRate != 44100 && sampleRate != 48000)
        {
            return Fail(CookedAudioParseStatus::UnsupportedSampleRate);
        }
        const uint16_t channelCount = ReadLe16(data, HeaderOffset::ChannelCount);
        if (channelCount != 1 && channelCount != 2)
        {
            return Fail(CookedAudioParseStatus::UnsupportedChannelCount);
        }
        const uint16_t bitsPerSample = ReadLe16(data, HeaderOffset::BitsPerSample);
        if ((codec == CookedAudioCodec::Pcm16 && bitsPerSample != 16) ||
            (codec == CookedAudioCodec::Vorbis && bitsPerSample != 0))
        {
            return Fail(CookedAudioParseStatus::UnsupportedBitsPerSample);
        }
        const uint16_t blockAlignment = ReadLe16(data, HeaderOffset::BlockAlignment);
        const uint16_t expectedBlockAlignment = codec == CookedAudioCodec::Pcm16
            ? static_cast<uint16_t>(channelCount * sizeof(int16_t))
            : 0;
        if (blockAlignment != expectedBlockAlignment)
        {
            return Fail(CookedAudioParseStatus::BlockAlignmentMismatch);
        }
        const uint16_t categoryValue = ReadLe16(data, HeaderOffset::Category);
        if (categoryValue > static_cast<uint16_t>(CookedAudioCategory::Ui))
        {
            return Fail(CookedAudioParseStatus::UnsupportedCategory);
        }
        const uint32_t gainBits = ReadLe32(data, HeaderOffset::GainDb);
        if ((gainBits & 0x7f800000u) == 0x7f800000u)
        {
            return Fail(CookedAudioParseStatus::InvalidGain);
        }
        const float gainDb = std::bit_cast<float>(gainBits);

        const uint64_t payloadOffset64 = ReadLe64(data, HeaderOffset::PayloadOffset);
        const uint64_t payloadSize64 = ReadLe64(data, HeaderOffset::PayloadSize);
        if (payloadSize64 == 0)
        {
            return Fail(CookedAudioParseStatus::InvalidPayloadSize);
        }
        if (codec == CookedAudioCodec::Pcm16 && payloadSize64 % blockAlignment != 0)
        {
            return Fail(CookedAudioParseStatus::PayloadBlockMisaligned);
        }
        if (payloadOffset64 != HeaderSize || payloadOffset64 > bytes.size() ||
            payloadSize64 != static_cast<uint64_t>(bytes.size()) - payloadOffset64)
        {
            return Fail(CookedAudioParseStatus::PayloadOutOfRange);
        }

        const uint64_t totalFrames = ReadLe64(data, HeaderOffset::TotalFrames);
        const uint32_t encoderDelay = ReadLe32(data, HeaderOffset::EncoderDelay);
        if (totalFrames == 0 ||
            (codec == CookedAudioCodec::Pcm16 &&
             (payloadSize64 / blockAlignment != totalFrames || encoderDelay != 0)))
        {
            return Fail(CookedAudioParseStatus::FrameCountMismatch);
        }
        const bool bHasLoop = (flags & FlagHasLoop) != 0;
        const uint64_t loopStartFrame = ReadLe64(data, HeaderOffset::LoopStartFrame);
        const uint64_t loopEndFrame = ReadLe64(data, HeaderOffset::LoopEndFrame);
        if ((bHasLoop && (loopStartFrame >= loopEndFrame || loopEndFrame > totalFrames)) ||
            (!bHasLoop && (loopStartFrame != 0 || loopEndFrame != 0)))
        {
            return Fail(CookedAudioParseStatus::LoopRangeInvalid);
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
        result.Audio.FrameCount = totalFrames;
        result.Audio.PayloadHash = payloadHash;
        result.Audio.PayloadOffset = payloadOffset;
        result.Audio.PayloadSize = payloadSize;
        result.Audio.FormatVersion = VersionMajor;
        result.Audio.Codec = codec;
        result.Audio.TotalFrames = totalFrames;
        result.Audio.HasLoop = bHasLoop;
        result.Audio.LoopStartFrame = loopStartFrame;
        result.Audio.LoopEndFrame = loopEndFrame;
        result.Audio.EncoderDelay = encoderDelay;
        result.Audio.GainDb = gainDb;
        result.Audio.Category = static_cast<CookedAudioCategory>(categoryValue);
        return result;
    }

    CookedAudioParseResult ParseCookedAudio(AssetBlob sourceBlob)
    {
        if (sourceBlob.IsValid())
        {
            const auto bytes = sourceBlob.GetSpan();
            if (bytes.size() >= CookedAudioFormatV1::MagicSize &&
                std::memcmp(bytes.data(), CookedAudioFormatV1::Magic, CookedAudioFormatV1::MagicSize) == 0)
            {
                return ParseCookedAudioV1(std::move(sourceBlob));
            }
        }
        return ParseCookedAudioV0(std::move(sourceBlob));
    }
} // namespace NorvesLib::Core::Asset
