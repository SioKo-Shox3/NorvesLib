#pragma once

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    using AssetPackageFourCC = uint32_t;

    [[nodiscard]] constexpr AssetPackageFourCC MakeAssetPackageFourCC(char a, char b, char c, char d) noexcept
    {
        return static_cast<AssetPackageFourCC>(static_cast<uint8_t>(a)) |
               (static_cast<AssetPackageFourCC>(static_cast<uint8_t>(b)) << 8) |
               (static_cast<AssetPackageFourCC>(static_cast<uint8_t>(c)) << 16) |
               (static_cast<AssetPackageFourCC>(static_cast<uint8_t>(d)) << 24);
    }

    [[nodiscard]] constexpr uint64_t ComputeAssetPackagePayloadHash(const uint8_t *data, size_t size) noexcept;

    enum class AssetPackageCompression : uint32_t
    {
        None = 0
    };

    namespace AssetPackageFormatV1
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'P', 'K', 'G', 'v', '1', '\0'};
        inline constexpr size_t MagicSize = sizeof(Magic);

        inline constexpr uint16_t VersionMajor = 1;
        inline constexpr uint16_t VersionMinor = 0;
        inline constexpr uint32_t EndianMarker = 0x01020304u;
        inline constexpr uint32_t MinimumAlignment = 8;

        inline constexpr uint64_t Fnv1a64OffsetBasis = 14695981039346656037ull;
        inline constexpr uint64_t Fnv1a64Prime = 1099511628211ull;
        inline constexpr uint64_t ZeroSizePayloadHash = Fnv1a64OffsetBasis;

        inline constexpr AssetPackageFourCC RawEntryType = MakeAssetPackageFourCC('R', 'a', 'w', ' ');
        inline constexpr const char *RawEntryName = "__raw__";

        inline constexpr size_t HeaderSize = 96;
        inline constexpr size_t EntryRecordSize = 64;

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;              // uint8[8]
            inline constexpr size_t HeaderSize = 8;         // uint32
            inline constexpr size_t VersionMajor = 12;      // uint16
            inline constexpr size_t VersionMinor = 14;      // uint16
            inline constexpr size_t EndianMarker = 16;      // uint32
            inline constexpr size_t EntryRecordSize = 20;   // uint32
            inline constexpr size_t PackageSize = 24;       // uint64
            inline constexpr size_t EntryCount = 32;        // uint32
            inline constexpr size_t Flags = 36;             // uint32, reserved zero
            inline constexpr size_t EntryTableOffset = 40;  // uint64
            inline constexpr size_t EntryTableSize = 48;    // uint64
            inline constexpr size_t NameTableOffset = 56;   // uint64
            inline constexpr size_t NameTableSize = 64;     // uint64
            inline constexpr size_t BlobDataOffset = 72;    // uint64
            inline constexpr size_t Alignment = 80;         // uint32
            inline constexpr size_t Reserved0 = 84;         // uint32, reserved zero
            inline constexpr size_t Reserved1 = 88;         // uint64, reserved zero
        }

        namespace EntryOffset
        {
            inline constexpr size_t NameOffset = 0;         // uint64, absolute package offset
            inline constexpr size_t NameSize = 8;           // uint32
            inline constexpr size_t Type = 12;              // uint32 FourCC
            inline constexpr size_t Compression = 16;       // uint32
            inline constexpr size_t Flags = 20;             // uint32, reserved zero
            inline constexpr size_t DataOffset = 24;        // uint64, absolute package offset
            inline constexpr size_t StoredSize = 32;        // uint64
            inline constexpr size_t UncompressedSize = 40;  // uint64
            inline constexpr size_t PayloadHash = 48;       // uint64, FNV-1a over stored payload bytes
            inline constexpr size_t Reserved0 = 56;         // uint64, reserved zero
        }
    }

    [[nodiscard]] constexpr uint64_t ComputeAssetPackagePayloadHash(const uint8_t *data, size_t size) noexcept
    {
        uint64_t hash = AssetPackageFormatV1::Fnv1a64OffsetBasis;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<uint64_t>(data[index]);
            hash *= AssetPackageFormatV1::Fnv1a64Prime;
        }
        return hash;
    }
}
