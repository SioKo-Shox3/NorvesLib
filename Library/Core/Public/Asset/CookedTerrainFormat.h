#pragma once

#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "Container/String.h"

namespace NorvesLib::Core::Asset
{
    namespace CookedTerrainFormatV0
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'T', 'R', 'N', 'v', '0', '\0'};
        inline constexpr size_t MagicSize = sizeof(Magic);
        inline constexpr uint16_t VersionMajor = 0;
        inline constexpr uint16_t VersionMinor = 0;
        inline constexpr uint16_t VersionMinorOuterRing = 1;
        inline constexpr uint32_t HeaderFlagOuterRing = 1u;
        inline constexpr uint32_t EndianMarker = 0x01020304u;
        inline constexpr size_t HeaderSize = 192;
        inline constexpr size_t LayerRecordSize = 32;
        inline constexpr size_t TileRecordHeadSize = 80;
        inline constexpr size_t TileLayerRangeSize = 24;
        inline constexpr size_t OuterRingBlockSize = 80;
        inline constexpr size_t OuterTileRecordSize = TileRecordHeadSize + TileLayerRangeSize;
        inline constexpr uint32_t OuterTileFlagOuter = 1u;
        inline constexpr uint32_t OuterTileFlagSharedNegZ = 2u;
        inline constexpr uint32_t OuterTileFlagSharedPosX = 4u;
        inline constexpr uint32_t OuterTileFlagSharedPosZ = 8u;
        inline constexpr uint32_t OuterTileFlagSharedNegX = 16u;
        inline constexpr uint32_t PayloadAlignment = 64;
        inline constexpr uint32_t MaxLodCount = 8;
        inline constexpr uint32_t MaxLayerCount = 16;
        inline constexpr uint32_t MaxSemanticBytes = 255;
        inline constexpr AssetPackageFourCC HeightLayerFourCC = MakeAssetPackageFourCC('H', 'G', 'T', '0');
        // R8UNorm・線形の埋め込み NVTEX。R は凹み度（0..255 → 0..1）、層が無い古いデータは 0 として読む。
        inline constexpr AssetPackageFourCC PuddleLayerFourCC = MakeAssetPackageFourCC('P', 'D', 'L', '0');
        inline constexpr const char* PuddleSemantic = "terrain.puddle.potential";
        inline constexpr AssetPackageFourCC PackageEntryType = MakeAssetPackageFourCC('T', 'r', 'n', '0');
        inline constexpr const char* ManifestFormat = "nvtrn.v0.height16";
        // 高さは z 行ごとに x 昇順の uint16 リトルエンディアン標本。
        inline constexpr uint32_t LayerEncodingRawHeight16 = 1;
        // NVTEX v0 のファイル全体をそのまま格納する。
        inline constexpr uint32_t LayerEncodingEmbeddedNvtex = 2;

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;              // uint8[8]、形式の識別子。
            inline constexpr size_t HeaderSize = 8;         // uint32、ヘッダの大きさ（192）。
            inline constexpr size_t VersionMajor = 12;      // uint16、主版（0）。
            inline constexpr size_t VersionMinor = 14;      // uint16、副版（0）。
            inline constexpr size_t EndianMarker = 16;      // uint32、バイト順の識別子。
            inline constexpr size_t Flags = 20;             // uint32、予約（0）。
            inline constexpr size_t FileSize = 24;          // uint64、ファイル全体のバイト数。
            inline constexpr size_t PayloadHash = 32;       // uint64、この欄の 8 バイトを除く全体の FNV-1a64。
            inline constexpr size_t CellCountX = 40;        // uint32、全体の x 方向のセル数。
            inline constexpr size_t CellCountZ = 44;        // uint32、全体の z 方向のセル数。
            inline constexpr size_t TileCells = 48;         // uint32、タイルの一辺のセル数。
            inline constexpr size_t TileCountX = 52;        // uint32、CellCountX / TileCells。
            inline constexpr size_t TileCountZ = 56;        // uint32、CellCountZ / TileCells。
            inline constexpr size_t LodCount = 60;          // uint32、有効な LOD 数（1..8）。
            inline constexpr size_t CellSizeM = 64;         // float、セルの一辺 [m]。有限で正。
            inline constexpr size_t Reserved0 = 68;         // uint32、予約（0）。
            inline constexpr size_t OriginX = 72;           // double、標本 (0,0) の世界座標 x [m]。
            inline constexpr size_t OriginY = 80;           // double、高さの基準 [m]。
            inline constexpr size_t OriginZ = 88;           // double、標本 (0,0) の世界座標 z [m]。
            inline constexpr size_t HeightMinM = 96;        // float、標本値 0 の高さ [m]。
            inline constexpr size_t HeightMaxM = 100;       // float、標本値 65535 の高さ [m]。有限で最小値以上。
            inline constexpr size_t LayerCount = 104;       // uint32、レイヤ数（1..16）。
            inline constexpr size_t LayerRecordSize = 108; // uint32、レイヤ表の一件のバイト数（32）。
            inline constexpr size_t TileRecordSize = 112;  // uint32、タイル表の一件のバイト数（80 + LayerCount*24）。
            inline constexpr size_t Reserved1 = 116;       // uint32、予約（0）。
            inline constexpr size_t LayerTableOffset = 120; // uint64、レイヤ表の絶対位置（192）。
            inline constexpr size_t LayerTableSize = 128;   // uint64、LayerCount*32 バイト。
            inline constexpr size_t TileTableOffset = 136;  // uint64、レイヤ表の直後の絶対位置。
            inline constexpr size_t TileTableSize = 144;    // uint64、TileCountX*TileCountZ*TileRecordSize バイト。
            inline constexpr size_t StringTableOffset = 152; // uint64、タイル表の直後の絶対位置。
            inline constexpr size_t StringTableSize = 160;  // uint64、意味の文字列を連結したバイト数。
            inline constexpr size_t PayloadOffset = 168;    // uint64、文字列表の後を 64 境界へ切り上げた絶対位置。
            inline constexpr size_t PayloadSize = 176;      // uint64、ファイル末尾までのバイト数。
            inline constexpr size_t Reserved2 = 184;        // uint64、予約（0）。
        }

        // 外周の記述の先頭からの相対位置。
        namespace OuterRingOffset
        {
            inline constexpr size_t CellCountX = 0;
            inline constexpr size_t CellCountZ = 4;
            inline constexpr size_t TileCells = 8;
            inline constexpr size_t TileCountX = 12;
            inline constexpr size_t TileCountZ = 16;
            inline constexpr size_t HoleTileMinX = 20;
            inline constexpr size_t HoleTileMinZ = 24;
            inline constexpr size_t HoleTileCountX = 28;
            inline constexpr size_t HoleTileCountZ = 32;
            inline constexpr size_t CellSizeM = 36;
            inline constexpr size_t OriginX = 40;
            inline constexpr size_t OriginZ = 48;
            inline constexpr size_t TileTableOffset = 56;
            inline constexpr size_t TileTableSize = 64;
            inline constexpr size_t Reserved = 72;
        }

        namespace LayerRecordOffset
        {
            inline constexpr size_t FourCC = 0;          // uint32、レイヤの識別子。重複不可。
            inline constexpr size_t Encoding = 4;        // uint32、標本の格納方法（1 または 2）。
            inline constexpr size_t PixelFormat = 8;     // uint32、CookedTexturePixelFormat の値。
            inline constexpr size_t ColorSpace = 12;     // uint32、CookedTextureColorSpace の値。
            inline constexpr size_t SemanticOffset = 16; // uint32、文字列表の先頭からの相対位置。
            inline constexpr size_t SemanticSize = 20;   // uint32、NUL を含まない印字可能 ASCII の長さ（1..255）。
            inline constexpr size_t Reserved = 24;       // uint64、予約（0）。
        }

        namespace TileRecordOffset
        {
            inline constexpr size_t TileX = 0;          // uint32、タイルの x 座標。
            inline constexpr size_t TileZ = 4;          // uint32、タイルの z 座標。
            inline constexpr size_t HeightMinM = 8;     // float、写像後のタイルの最小高さ [m]（OriginY なし）。
            inline constexpr size_t HeightMaxM = 12;    // float、写像後のタイルの最大高さ [m]（OriginY なし）。
            inline constexpr size_t AabbMin = 16;       // float[3]、原点からの相対の最小座標 [m]。
            inline constexpr size_t AabbMax = 28;       // float[3]、原点からの相対の最大座標 [m]。
            inline constexpr size_t Reserved0 = 40;     // uint32、予約（0）。
            inline constexpr size_t LodMaxErrorM = 44;  // float[8]、LOD ごとの最大誤差 [m]。有効欄は有限で非負、残りは 0。
            inline constexpr size_t Reserved1 = 76;     // uint32、予約（0）。
            inline constexpr size_t LayerRanges = 80;   // 下の区間レコードをレイヤ順に並べた先頭。
        }

        namespace TileLayerRangeOffset
        {
            inline constexpr size_t DataOffset = 0;  // uint64、レイヤのバイト列の絶対位置（64 の倍数）。
            inline constexpr size_t DataSize = 8;    // uint64、レイヤのバイト数。高さは (TileCells+1)²*2。
            inline constexpr size_t DataHash = 16;   // uint64、レイヤのバイト列の FNV-1a64。
        }

        static_assert(HeaderOffset::Reserved2 + 8 == HeaderSize);
        static_assert(TileRecordOffset::Reserved1 + 4 == TileRecordHeadSize);
    }

    // 読み手はヘッダ、全体ハッシュ、寸法、表、レイヤ、タイルの順に検査する。
    // 表の予約欄と未使用 LOD 欄は、その表を読むときに検査する。
    enum class CookedTerrainParseStatus : uint8_t
    {
        Success,
        InvalidBlob,             // 無効または空のバイト列。
        HeaderTooSmall,          // 192 バイト未満。
        BadMagic,
        UnsupportedVersion,      // 主版または副版が 0 以外。
        EndianMismatch,
        HeaderSizeMismatch,
        FileSizeMismatch,        // 宣言したファイル長と実際の長さが不一致。
        ReservedFieldNonZero,    // 予約欄または未使用 LOD 欄が 0 以外。
        PayloadHashMismatch,
        InvalidDimensions,       // セル数、タイル数、セル寸法、高さの写像、原点が不正。
        InvalidLodCount,
        InvalidLayerCount,
        RecordSizeMismatch,
        TableLayoutMismatch,     // 表の順序、大きさ、64 境界、ファイル長との整合が不正。
        IntegerOverflow,
        InvalidLayerRecord,      // レイヤの規則または意味の文字列が不正。
        DuplicateLayerFourCC,
        TileIndexOutOfRange,
        DuplicateTile,
        InvalidTileBounds,       // 高さまたは AABB が非有限、あるいは最小値が最大値を超える。
        LayerRangeOutOfRange,    // ペイロード範囲外、64 境界以外、または大きさ 0。
        HeightSizeMismatch,
        LayerHashMismatch,
        EmbeddedTextureInvalid, // NVTEX の解析失敗、または形式・色空間の不一致。
        InvalidOuterRing,       // 外周の記述、表の位置・大きさ、またはタイルの印が不正。
        OuterEdgeMismatch       // 穴の縁で外周と近景の高さ標本が不一致。
    };

    struct CookedTerrainLayer
    {
        AssetPackageFourCC FourCC = 0;
        uint32_t Encoding = 0;
        CookedTexturePixelFormat PixelFormat = CookedTexturePixelFormat::R16UNorm;
        CookedTextureColorSpace ColorSpace = CookedTextureColorSpace::Linear;
        Container::AnsiString Semantic;
    };

    struct CookedTerrainLayerRange
    {
        uint64_t DataOffset = 0;
        uint64_t DataSize = 0;
        uint64_t DataHash = 0;
    };

    struct CookedTerrainTile
    {
        uint32_t TileX = 0;
        uint32_t TileZ = 0;
        float HeightMinM = 0.0f;
        float HeightMaxM = 0.0f;
        float AabbMin[3] = {};
        float AabbMax[3] = {};
        float LodMaxErrorM[CookedTerrainFormatV0::MaxLodCount] = {};
    };

    struct CookedTerrainOuterRing
    {
        uint32_t CellCountX = 0;
        uint32_t CellCountZ = 0;
        uint32_t TileCells = 0;
        uint32_t TileCountX = 0;
        uint32_t TileCountZ = 0;
        uint32_t HoleTileMinX = 0;
        uint32_t HoleTileMinZ = 0;
        uint32_t HoleTileCountX = 0;
        uint32_t HoleTileCountZ = 0;
        float CellSizeM = 0.0f;
        double OriginX = 0.0;
        double OriginZ = 0.0;
        // 書き手は表の位置と大きさを自動で埋める。
        uint64_t TileTableOffset = 0;
        uint64_t TileTableSize = 0;
        uint64_t Reserved = 0;
    };

    struct CookedTerrainOuterTile : CookedTerrainTile
    {
        uint32_t Flags = 0;
        CookedTerrainLayerRange Height;
    };

    struct CookedTerrainData
    {
        AssetBlob SourceBlob;
        uint32_t CellCountX = 0;
        uint32_t CellCountZ = 0;
        uint32_t TileCells = 0;
        uint32_t TileCountX = 0;
        uint32_t TileCountZ = 0;
        uint32_t LodCount = 0;
        float CellSizeM = 0.0f;
        double OriginX = 0.0;
        double OriginY = 0.0;
        double OriginZ = 0.0;
        float HeightMinM = 0.0f;
        float HeightMaxM = 0.0f;
        uint64_t PayloadHash = 0;
        Container::VariableArray<CookedTerrainLayer> Layers;
        Container::VariableArray<CookedTerrainTile> Tiles; // ファイル内の順。
        Container::VariableArray<CookedTerrainLayerRange> LayerRanges; // tileIndex * Layers.size() + layerIndex。
        Container::VariableArray<uint32_t> TileIndexByCoord; // TileZ * TileCountX + TileX から Tiles の番号を引く。
        bool bHasOuterRing = false;
        CookedTerrainOuterRing OuterRing;
        Container::VariableArray<CookedTerrainOuterTile> OuterTiles;
        Container::VariableArray<uint32_t> OuterTileIndexByCoord; // 穴の添字は 0xFFFFFFFF。

        [[nodiscard]] uint32_t GetSamplesPerSide() const noexcept; // TileCells + 1。
        [[nodiscard]] uint32_t GetOuterSamplesPerSide() const noexcept; // 外周がなければ 0。
        [[nodiscard]] const CookedTerrainOuterTile* FindOuterTile(uint32_t tileX, uint32_t tileZ, uint32_t* outIndex = nullptr) const noexcept;
        [[nodiscard]] uint16_t GetOuterHeightSample(uint32_t outerTileIndex, uint32_t sampleX, uint32_t sampleZ) const noexcept;
        [[nodiscard]] const CookedTerrainTile* FindTile(uint32_t tileX, uint32_t tileZ, uint32_t* outTileIndex = nullptr) const noexcept;
        [[nodiscard]] int32_t FindLayer(AssetPackageFourCC fourCC) const noexcept; // 見つからなければ -1。
        [[nodiscard]] const CookedTerrainLayerRange* GetLayerRange(uint32_t tileIndex, uint32_t layerIndex) const noexcept;
        // SourceBlob.TryCreateSubBlob を使う。範囲外なら AssetBlob::Invalid()。
        [[nodiscard]] AssetBlob GetLayerBlob(uint32_t tileIndex, uint32_t layerIndex) const;
        [[nodiscard]] uint16_t GetHeightSample(uint32_t tileIndex, uint32_t sampleX, uint32_t sampleZ) const noexcept; // 範囲外なら 0。
        // HeightMinM + (HeightMaxM - HeightMinM) * value / 65535。OriginY は加算しない。
        [[nodiscard]] float DecodeHeightM(uint16_t value) const noexcept;
    };

    struct CookedTerrainParseResult
    {
        CookedTerrainParseStatus Status = CookedTerrainParseStatus::InvalidBlob;
        CookedTerrainData Terrain;
        [[nodiscard]] bool Succeeded() const noexcept { return Status == CookedTerrainParseStatus::Success; }
    };

    [[nodiscard]] CookedTerrainParseResult ParseCookedTerrain(AssetBlob sourceBlob);

    // 40 バイト以上なら 32..39 のハッシュ欄を入力から除き、それ未満なら全体をハッシュする。
    [[nodiscard]] constexpr uint64_t ComputeCookedTerrainFileHash(Container::Span<const uint8_t> fileBytes) noexcept
    {
        uint64_t hash = CookedTextureFormatV0::Fnv1a64OffsetBasis;
        for (size_t i = 0; i < fileBytes.size(); ++i)
        {
            if (fileBytes.size() >= 40 && i >= 32 && i < 40)
            {
                continue;
            }
            hash ^= fileBytes[i];
            hash *= CookedTextureFormatV0::Fnv1a64Prime;
        }
        return hash;
    }

    struct CookedTerrainLayerDesc
    {
        AssetPackageFourCC FourCC = 0;
        uint32_t Encoding = 0;
        CookedTexturePixelFormat PixelFormat = CookedTexturePixelFormat::R16UNorm;
        CookedTextureColorSpace ColorSpace = CookedTextureColorSpace::Linear;
        Container::AnsiString Semantic;
    };

    struct CookedTerrainTileDesc
    {
        uint32_t TileX = 0;
        uint32_t TileZ = 0;
        float HeightMinM = 0.0f;
        float HeightMaxM = 0.0f;
        float AabbMin[3] = {};
        float AabbMax[3] = {};
        float LodMaxErrorM[CookedTerrainFormatV0::MaxLodCount] = {};
        // レイヤ表と同じ数・順の非所有ビュー。
        Container::VariableArray<Container::Span<const uint8_t>> LayerBytes;
    };

    struct CookedTerrainOuterTileDesc : CookedTerrainTile
    {
        Container::Span<const uint8_t> HeightBytes;
    };

    struct CookedTerrainWriteDesc
    {
        uint32_t CellCountX = 0;
        uint32_t CellCountZ = 0;
        uint32_t TileCells = 0;
        uint32_t LodCount = 0;
        float CellSizeM = 0.0f;
        double OriginX = 0.0;
        double OriginY = 0.0;
        double OriginZ = 0.0;
        float HeightMinM = 0.0f;
        float HeightMaxM = 0.0f;
        Container::VariableArray<CookedTerrainLayerDesc> Layers;
        Container::VariableArray<CookedTerrainTileDesc> Tiles;
        bool bHasOuterRing = false;
        CookedTerrainOuterRing OuterRing;
        Container::VariableArray<CookedTerrainOuterTileDesc> OuterTiles;
    };

    enum class CookedTerrainWriteStatus : uint8_t
    {
        Success,
        InvalidDimensions,
        InvalidLodCount,
        InvalidLayerCount,
        InvalidLayer,
        DuplicateLayerFourCC,
        TileCountMismatch,
        TileIndexOutOfRange,
        DuplicateTile,
        InvalidTileBounds,
        LayerBytesCountMismatch,
        HeightSizeMismatch,
        EmbeddedTextureInvalid,
        IntegerOverflow,
        InvalidOuterRing,
        OuterEdgeMismatch
    };

    // 全入力を検査して座標順に書く。失敗時は outBytes を空にする。
    [[nodiscard]] CookedTerrainWriteStatus WriteCookedTerrain(const CookedTerrainWriteDesc& desc,
                                                            Container::VariableArray<uint8_t>& outBytes);
} // namespace NorvesLib::Core::Asset
