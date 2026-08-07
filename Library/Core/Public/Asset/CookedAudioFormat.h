#pragma once

#include "Asset/AssetBlob.h"
#include "Container/Span.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    namespace CookedAudioFormatV0
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'A', 'U', 'D', 'v', '0', '\0'};
        inline constexpr size_t MagicSize = sizeof(Magic);
        inline constexpr uint16_t VersionMajor = 0;
        inline constexpr uint16_t VersionMinor = 0;
        inline constexpr uint32_t EndianMarker = 0x01020304u;
        inline constexpr uint32_t EntryType =
            static_cast<uint32_t>('A') |
            (static_cast<uint32_t>('u') << 8) |
            (static_cast<uint32_t>('d') << 16) |
            (static_cast<uint32_t>('0') << 24);
        inline constexpr uint64_t Fnv1a64OffsetBasis = 14695981039346656037ull;
        inline constexpr uint64_t Fnv1a64Prime = 1099511628211ull;
        inline constexpr size_t HeaderSize = 68;

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;
            inline constexpr size_t HeaderSize = 8;
            inline constexpr size_t VersionMajor = 12;
            inline constexpr size_t VersionMinor = 14;
            inline constexpr size_t EndianMarker = 16;
            inline constexpr size_t Reserved0 = 20;
            inline constexpr size_t FileSize = 24;
            inline constexpr size_t PayloadOffset = 32;
            inline constexpr size_t PayloadSize = 40;
            inline constexpr size_t PayloadHash = 48;
            inline constexpr size_t SampleRate = 56;
            inline constexpr size_t ChannelCount = 60;
            inline constexpr size_t BitsPerSample = 62;
            inline constexpr size_t BlockAlignment = 64;
            inline constexpr size_t Reserved1 = 66;
        }
    } // namespace CookedAudioFormatV0

    enum class CookedAudioParseStatus : uint8_t
    {
        Success,
        InvalidBlob,
        EmptyBlob,
        HeaderTooSmall,
        BadMagic,
        UnsupportedVersion,
        EndianMismatch,
        HeaderSizeMismatch,
        FileSizeMismatch,
        ReservedFieldNonZero,
        UnsupportedSampleRate,
        UnsupportedChannelCount,
        UnsupportedBitsPerSample,
        BlockAlignmentMismatch,
        InvalidPayloadSize,
        PayloadBlockMisaligned,
        PayloadOutOfRange,
        PayloadHashMismatch,
    };

    struct CookedAudioData
    {
        AssetBlob SourceBlob;
        uint32_t SampleRate = 0;
        uint16_t ChannelCount = 0;
        uint16_t BitsPerSample = 0;
        uint16_t BlockAlignment = 0;
        uint64_t FrameCount = 0;
        uint64_t PayloadHash = 0;
        size_t PayloadOffset = 0;
        size_t PayloadSize = 0;

        [[nodiscard]] Container::Span<const uint8_t> GetPcmBytes() const noexcept;
    };

    struct CookedAudioParseResult
    {
        CookedAudioParseStatus Status = CookedAudioParseStatus::InvalidBlob;
        CookedAudioData Audio;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == CookedAudioParseStatus::Success;
        }
    };

    [[nodiscard]] constexpr uint64_t ComputeCookedAudioPayloadHash(const uint8_t* data, size_t size) noexcept
    {
        uint64_t hash = CookedAudioFormatV0::Fnv1a64OffsetBasis;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<uint64_t>(data[index]);
            hash *= CookedAudioFormatV0::Fnv1a64Prime;
        }
        return hash;
    }

    [[nodiscard]] CookedAudioParseResult ParseCookedAudio(AssetBlob sourceBlob);
} // namespace NorvesLib::Core::Asset
