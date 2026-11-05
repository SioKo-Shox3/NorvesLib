#pragma once

#include "Asset/AssetBlob.h"
#include "Asset/AssetPackageFormat.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

// 散布の cook 済み形式と、読み書き・量子化の入口。
namespace NorvesLib::Core::Asset
{
    namespace CookedScatterFormatV0
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'S', 'C', 'T', 'v', '0', 0};
        inline constexpr size_t MagicSize = sizeof(Magic);
        inline constexpr size_t HeaderSize = 192;
        inline constexpr uint32_t Version = 0;
        inline constexpr uint32_t EndianMarker = 0x01020304u;
        inline constexpr size_t SpeciesRecordSize = 96;
        inline constexpr size_t ChunkRecordSize = 48;
        inline constexpr size_t RangeRecordSize = 16;
        inline constexpr size_t InstanceRecordSize = 16;
        inline constexpr uint32_t InstanceAlignment = 16;
        inline constexpr uint32_t MaxSpeciesCount = 64;
        inline constexpr uint32_t MaxLodCount = 4;
        inline constexpr AssetPackageFourCC PackageEntryType = MakeAssetPackageFourCC('S', 'c', 't', '0');
        inline constexpr const char* ManifestFormat = "nvsct.v0.inst16";

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;
            inline constexpr size_t HeaderSize = 8;
            inline constexpr size_t Version = 12;
            inline constexpr size_t EndianMarker = 16;
            inline constexpr size_t Reserved0 = 20;
            inline constexpr size_t FileSize = 24;
            inline constexpr size_t PayloadHash = 32;
            inline constexpr size_t Seed = 40;
            inline constexpr size_t TerrainCookedHash = 48;
            inline constexpr size_t ChunkSizeMeters = 56;
            inline constexpr size_t GridCountX = 60;
            inline constexpr size_t GridCountZ = 64;
            inline constexpr size_t SpeciesCount = 68;
            inline constexpr size_t ChunkCount = 72;
            inline constexpr size_t Reserved1 = 76;
            inline constexpr size_t OriginX = 80;
            inline constexpr size_t OriginZ = 88;
            inline constexpr size_t SpeciesTableOffset = 96;
            inline constexpr size_t SpeciesTableSize = 104;
            inline constexpr size_t ChunkTableOffset = 112;
            inline constexpr size_t ChunkTableSize = 120;
            inline constexpr size_t RangeTableOffset = 128;
            inline constexpr size_t RangeTableSize = 136;
            inline constexpr size_t StringTableOffset = 144;
            inline constexpr size_t StringTableSize = 152;
            inline constexpr size_t InstanceOffset = 160;
            inline constexpr size_t InstanceSize = 168;
            inline constexpr size_t Reserved2 = 176;
            inline constexpr size_t Reserved3 = 184;
        }

        namespace SpeciesRecordOffset
        {
            inline constexpr size_t Name = 0;
            inline constexpr size_t RenderPath = 8;
            inline constexpr size_t LodCount = 12;
            inline constexpr size_t LodPaths = 16;
            inline constexpr size_t MeshRadiusMeters = 48;
            inline constexpr size_t MinSpacingMeters = 52;
            inline constexpr size_t ScaleMin = 56;
            inline constexpr size_t ScaleMax = 60;
            inline constexpr size_t SlopeFollow = 64;
            inline constexpr size_t BrightnessVariation = 68;
            inline constexpr size_t WindStiffness = 72;
            inline constexpr size_t WindPhaseSeed = 76;
            inline constexpr size_t Flags = 80;
            inline constexpr size_t Reserved0 = 84;
            inline constexpr size_t Reserved1 = 88;
        }

        namespace ChunkRecordOffset
        {
            inline constexpr size_t ChunkX = 0;
            inline constexpr size_t ChunkZ = 4;
            inline constexpr size_t MinY = 8;
            inline constexpr size_t MaxY = 12;
            inline constexpr size_t AabbMin = 16;
            inline constexpr size_t AabbMax = 28;
            inline constexpr size_t Reserved = 40;
        }

        namespace RangeRecordOffset
        {
            inline constexpr size_t Offset = 0;
            inline constexpr size_t Count = 8;
        }

        static_assert(HeaderOffset::Reserved3 + 8 == HeaderSize);
        static_assert(SpeciesRecordOffset::Reserved1 + 8 == SpeciesRecordSize);
        static_assert(ChunkRecordOffset::Reserved + 8 == ChunkRecordSize);
        static_assert(RangeRecordOffset::Count + 8 == RangeRecordSize);
    }

    enum class ScatterRenderPath : uint32_t
    {
        Mega = 0,
        Foliage = 1,
    };

    struct InstanceRecord16
    {
        uint16_t PosX = 0;
        uint16_t PosZ = 0;
        uint16_t PosY = 0;
        uint16_t Yaw = 0;
        uint8_t Scale = 0;
        uint8_t Rank = 0;
        uint8_t NormalOct[2] = {};
        uint8_t Tint[3] = {};
        uint8_t Dry = 0;
    };
    static_assert(sizeof(InstanceRecord16) == CookedScatterFormatV0::InstanceRecordSize);
    static_assert(offsetof(InstanceRecord16, Dry) == 15);

    struct CookedScatterSpecies
    {
        Container::AnsiString Name;
        ScatterRenderPath RenderPath = ScatterRenderPath::Mega;
        uint32_t LodCount = 0;
        Container::AnsiString LodPaths[CookedScatterFormatV0::MaxLodCount];
        float MeshRadiusMeters = 0.0f;
        float MinSpacingMeters = 0.0f;
        float ScaleMin = 1.0f;
        float ScaleMax = 1.0f;
        float SlopeFollow = 0.0f;
        float BrightnessVariation = 0.0f;
        float WindStiffness = 0.0f;
        uint32_t WindPhaseSeed = 0;
        uint32_t Flags = 0;
    };

    struct CookedScatterChunk
    {
        uint32_t ChunkX = 0;
        uint32_t ChunkZ = 0;
        float MinY = 0.0f;
        float MaxY = 0.0f;
        float AabbMin[3] = {};
        float AabbMax[3] = {};
    };

    struct CookedScatterRange
    {
        uint64_t Offset = 0;
        uint64_t Count = 0;
    };

    struct ScatterInstanceValue
    {
        double PositionX = 0.0;
        double PositionY = 0.0;
        double PositionZ = 0.0;
        float YawRadians = 0.0f;
        float Scale = 1.0f;
        uint8_t Rank = 0;
        float Normal[3] = {0.0f, 1.0f, 0.0f};
        float Tint[3] = {1.0f, 1.0f, 1.0f};
        float Dry = 0.0f;
    };

    enum class CookedScatterParseStatus : uint8_t
    {
        Success,
        InvalidBlob,
        TruncatedHeader,
        BadMagic,
        UnsupportedVersion,
        BadEndian,
        BadHeaderSize,
        FileSizeMismatch,
        HashMismatch,
        ReservedNonZero,
        InvalidDimensions,
        TableOutOfRange,
        StringOutOfRange,
        SpeciesOutOfRange,
        ChunkOutOfRange,
        InvalidChunkBounds,
        InstanceRangeOutOfRange,
    };

    struct CookedScatterData
    {
        AssetBlob Source;
        uint64_t PayloadHash = 0;
        uint64_t Seed = 0;
        uint64_t TerrainCookedHash = 0;
        float ChunkSizeMeters = 0.0f;
        double OriginX = 0.0;
        double OriginZ = 0.0;
        uint32_t GridCountX = 0;
        uint32_t GridCountZ = 0;
        Container::VariableArray<CookedScatterSpecies> Species;
        Container::VariableArray<CookedScatterChunk> Chunks;
        // chunkIndex * Species.size() + speciesIndex。
        Container::VariableArray<CookedScatterRange> SpeciesRanges;
        uint64_t InstanceOffset = 0;
        uint64_t InstanceSize = 0;

        // 16 バイト単位の非所有ビュー。寿命は Source に従う。
        [[nodiscard]] Container::Span<const uint8_t> GetInstances(uint32_t chunkIndex, uint32_t speciesIndex) const noexcept;
    };

    struct CookedScatterParseResult
    {
        CookedScatterParseStatus Status = CookedScatterParseStatus::InvalidBlob;
        CookedScatterData Scatter;
        [[nodiscard]] bool Succeeded() const noexcept { return Status == CookedScatterParseStatus::Success; }
    };

    // 32..39 のハッシュ欄を除くファイル全体の FNV-1a64。
    [[nodiscard]] constexpr uint64_t ComputeCookedScatterFileHash(Container::Span<const uint8_t> bytes) noexcept
    {
        uint64_t hash = AssetPackageFormatV1::Fnv1a64OffsetBasis;
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            if (bytes.size() >= 40 && i >= 32 && i < 40)
            {
                continue;
            }
            hash ^= bytes[i];
            hash *= AssetPackageFormatV1::Fnv1a64Prime;
        }
        return hash;
    }

    [[nodiscard]] CookedScatterParseResult ParseCookedScatter(AssetBlob sourceBlob);
    [[nodiscard]] const char* GetCookedScatterParseStatusName(CookedScatterParseStatus status) noexcept;

    // 位置はワールド座標、角度はラジアン、色と枯れ度は 0..1。失敗時は出力を変えない。
    [[nodiscard]] bool EncodeScatterInstance(const ScatterInstanceValue& value, const CookedScatterChunk& chunk,
                                             const CookedScatterSpecies& species, float chunkSizeMeters,
                                             double originX, double originZ, InstanceRecord16& out) noexcept;
    [[nodiscard]] bool DecodeScatterInstance(const InstanceRecord16& record, const CookedScatterChunk& chunk,
                                             const CookedScatterSpecies& species, float chunkSizeMeters,
                                             double originX, double originZ, ScatterInstanceValue& out) noexcept;
    // ファイル上の 16 バイトをバイト順と整列に依らず読み取る。
    [[nodiscard]] bool ReadScatterInstance(Container::Span<const uint8_t> bytes, InstanceRecord16& out) noexcept;

    struct CookedScatterWriteDesc
    {
        uint64_t Seed = 0;
        uint64_t TerrainCookedHash = 0;
        float ChunkSizeMeters = 0.0f;
        double OriginX = 0.0;
        double OriginZ = 0.0;
        uint32_t GridCountX = 0;
        uint32_t GridCountZ = 0;
        Container::VariableArray<CookedScatterSpecies> Species;
        Container::VariableArray<CookedScatterChunk> Chunks;
        Container::VariableArray<CookedScatterRange> SpeciesRanges;
        Container::VariableArray<InstanceRecord16> Instances;
    };

    enum class CookedScatterWriteStatus : uint8_t
    {
        Success,
        InvalidDimensions,
        InvalidSpecies,
        InvalidChunk,
        InvalidRanges,
        TooLarge,
    };

    // 入力順のまま書く。失敗時は outBytes を空にする。
    [[nodiscard]] CookedScatterWriteStatus WriteCookedScatter(const CookedScatterWriteDesc& desc,
                                                               Container::VariableArray<uint8_t>& outBytes);
} // namespace NorvesLib::Core::Asset
