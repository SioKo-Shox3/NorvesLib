#pragma once

#include "Asset/AssetBlob.h"
#include "Asset/AssetPackageFormat.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

// 草の場の cook 済み形式と、その読み書きの入口。
namespace NorvesLib::Core::Asset
{
    namespace CookedGrassFieldFormatV0
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'G', 'F', 'L', 'D', 'v', '0'};
        inline constexpr size_t MagicSize = sizeof(Magic);
        inline constexpr size_t HeaderSize = 128;
        inline constexpr uint32_t Version = 0;
        inline constexpr uint32_t EndianMarker = 0x01020304u;
        inline constexpr size_t ChunkRecordSize = 48;
        inline constexpr size_t SpeciesNameRecordSize = 64;
        inline constexpr uint32_t MaxSpeciesNameLength = 63;
        inline constexpr uint32_t MaxSpeciesCount = 64;
        inline constexpr uint32_t MaxLayerResolution = 1024;
        inline constexpr uint32_t PayloadAlignment = 16;
        inline constexpr AssetPackageFourCC PackageEntryType = MakeAssetPackageFourCC('G', 'f', 'd', '0');
        inline constexpr const char* ManifestFormat = "nvgfld.v0";

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;
            inline constexpr size_t HeaderSize = 8;
            inline constexpr size_t Version = 12;
            inline constexpr size_t EndianMarker = 16;
            inline constexpr size_t Reserved0 = 20;
            inline constexpr size_t FileSize = 24;
            inline constexpr size_t PayloadHash = 32;
            inline constexpr size_t ChunkSizeMeters = 40;
            inline constexpr size_t LayerAResolution = 44;
            inline constexpr size_t LayerBResolution = 48;
            inline constexpr size_t ChunkCount = 52;
            inline constexpr size_t ChunkTableOffset = 56;
            inline constexpr size_t ChunkTableSize = 64;
            inline constexpr size_t SpeciesCount = 72;
            inline constexpr size_t Reserved1 = 76;
            inline constexpr size_t SpeciesTableOffset = 80;
            inline constexpr size_t SpeciesTableSize = 88;
            inline constexpr size_t PayloadOffset = 96;
            inline constexpr size_t PayloadSize = 104;
            inline constexpr size_t Reserved2 = 112;
            inline constexpr size_t Reserved3 = 120;
        }

        namespace ChunkRecordOffset
        {
            inline constexpr size_t ChunkX = 0;
            inline constexpr size_t ChunkZ = 4;
            inline constexpr size_t Flags = 8;
            inline constexpr size_t MaxDensity = 12;
            inline constexpr size_t MaxHeightFactor = 13;
            inline constexpr size_t Reserved = 14;
            inline constexpr size_t LayerAOffset = 16;
            inline constexpr size_t LayerASize = 24;
            inline constexpr size_t LayerBOffset = 32;
            inline constexpr size_t LayerBSize = 40;
        }

        static_assert(HeaderOffset::Reserved3 + 8 == HeaderSize);
        static_assert(ChunkRecordOffset::LayerBSize + 8 == ChunkRecordSize);
    } // namespace CookedGrassFieldFormatV0

    enum class CookedGrassFieldParseStatus : uint8_t
    {
        Success,
        InvalidBlob,
        TruncatedHeader,
        BadMagic,
        UnsupportedVersion,
        EndianMismatch,
        HeaderSizeMismatch,
        FileSizeMismatch,
        ReservedFieldNonZero,
        InvalidFieldDesc,
        SectionOutOfRange,
        TruncatedPayload,
        PayloadHashMismatch,
        InvalidSpeciesName,
        DuplicateSpeciesName,
        InvalidChunkRecord,
        ChunkRangeOutOfRange,
        DuplicateChunk,
        ChunkStatsMismatch,
        InvalidSpeciesId,
    };

    struct CookedGrassFieldChunk
    {
        int32_t X = 0;
        int32_t Z = 0;
        uint32_t Flags = 0;
        uint8_t MaxDensity = 0;
        uint8_t MaxHeightFactor = 0;
        // レイヤの位置はペイロード先頭からの相対バイト位置。
        uint64_t LayerAOffset = 0;
        uint64_t LayerASize = 0;
        uint64_t LayerBOffset = 0;
        uint64_t LayerBSize = 0;
    };

    struct CookedGrassFieldData
    {
        float ChunkSizeMeters = 0.0f;
        uint32_t LayerAResolution = 0;
        uint32_t LayerBResolution = 0;
        uint64_t PayloadHash = 0;
        // 名前は UTF-8 のバイト列、添字は種別 ID。
        Container::VariableArray<Container::AnsiString> SpeciesNames;
        Container::VariableArray<CookedGrassFieldChunk> Chunks;
        AssetBlob Source;
        uint64_t PayloadOffset = 0;

        // 返したビューの寿命は Source または同じ所有者を共有する blob に従う。
        [[nodiscard]] Container::Span<const uint8_t> GetLayerA(uint32_t chunkIndex) const noexcept;
        [[nodiscard]] Container::Span<const uint8_t> GetLayerB(uint32_t chunkIndex) const noexcept;
    };

    struct CookedGrassFieldParseResult
    {
        CookedGrassFieldParseStatus Status = CookedGrassFieldParseStatus::InvalidBlob;
        CookedGrassFieldData Field;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == CookedGrassFieldParseStatus::Success;
        }
    };

    [[nodiscard]] CookedGrassFieldParseResult ParseCookedGrassField(AssetBlob sourceBlob);
    [[nodiscard]] const char* GetCookedGrassFieldParseStatusName(CookedGrassFieldParseStatus status) noexcept;

    struct CookedGrassFieldWriteChunk
    {
        int32_t X = 0;
        int32_t Z = 0;
        Container::Span<const uint8_t> LayerA;
        Container::Span<const uint8_t> LayerB;
    };

    struct CookedGrassFieldWriteDesc
    {
        float ChunkSizeMeters = 32.0f;
        uint32_t LayerAResolution = 64;
        uint32_t LayerBResolution = 32;
        Container::VariableArray<Container::AnsiString> SpeciesNames;
        Container::VariableArray<CookedGrassFieldWriteChunk> Chunks;
    };

    enum class CookedGrassFieldWriteStatus : uint8_t
    {
        Success,
        InvalidFieldDesc,
        InvalidSpeciesName,
        DuplicateSpeciesName,
        InvalidChunk,
        DuplicateChunk,
        InvalidSpeciesId,
        TooLarge,
    };

    // 全入力を検査し、座標順に書く。失敗時は outBytes を空にする。
    [[nodiscard]] CookedGrassFieldWriteStatus WriteCookedGrassField(const CookedGrassFieldWriteDesc& desc,
                                                                     Container::VariableArray<uint8_t>& outBytes);
} // namespace NorvesLib::Core::Asset
