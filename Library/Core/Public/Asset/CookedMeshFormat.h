#pragma once

#include "Asset/AssetBlob.h"
#include "Container/Span.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    namespace CookedMeshFormatV0
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'M', 'E', 'S', 'H', 'v', '0'};
        inline constexpr size_t MagicSize = sizeof(Magic);

        inline constexpr uint16_t VersionMajor = 0;
        inline constexpr uint16_t VersionMinor = 0;
        inline constexpr uint32_t EndianMarker = 0x01020304u;

        inline constexpr size_t HeaderSize = 256;
        inline constexpr size_t VertexRecordSize = 32;
        inline constexpr size_t SubmeshRecordSize = 64;
        inline constexpr size_t MaterialRecordSize = 64;
        inline constexpr size_t ClusterRecordSize = 80;
        inline constexpr size_t StringRefRecordSize = 16;
        inline constexpr size_t SectionAlignment = 8;

        inline constexpr uint32_t ClusterAlgorithmId = 1;
        inline constexpr uint32_t ClusterAlgorithmVersion = 0;
        inline constexpr uint32_t ClusterMaxTriangles = 128;
        inline constexpr uint32_t ClusterMaxVertices = 128;
        inline constexpr uint32_t ClusterSettingsFlags = 0;

        inline constexpr uint64_t Fnv1a64OffsetBasis = 14695981039346656037ull;
        inline constexpr uint64_t Fnv1a64Prime = 1099511628211ull;
        inline constexpr uint64_t ZeroSizePayloadHash = Fnv1a64OffsetBasis;

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;
            inline constexpr size_t HeaderSize = 8;
            inline constexpr size_t VersionMajor = 12;
            inline constexpr size_t VersionMinor = 14;
            inline constexpr size_t EndianMarker = 16;
            inline constexpr size_t VertexRecordSize = 20;
            inline constexpr size_t SubmeshRecordSize = 24;
            inline constexpr size_t MaterialRecordSize = 28;
            inline constexpr size_t ClusterRecordSize = 32;
            inline constexpr size_t StringRefRecordSize = 36;
            inline constexpr size_t FileSize = 40;
            inline constexpr size_t SubmeshTableOffset = 48;
            inline constexpr size_t SubmeshTableSize = 56;
            inline constexpr size_t MaterialTableOffset = 64;
            inline constexpr size_t MaterialTableSize = 72;
            inline constexpr size_t ClusterTableOffset = 80;
            inline constexpr size_t ClusterTableSize = 88;
            inline constexpr size_t StringTableOffset = 96;
            inline constexpr size_t StringTableSize = 104;
            inline constexpr size_t VertexPayloadOffset = 112;
            inline constexpr size_t VertexPayloadSize = 120;
            inline constexpr size_t IndexPayloadOffset = 128;
            inline constexpr size_t IndexPayloadSize = 136;
            inline constexpr size_t PayloadHash = 144;
            inline constexpr size_t VertexCount = 152;
            inline constexpr size_t IndexCount = 156;
            inline constexpr size_t SubmeshCount = 160;
            inline constexpr size_t MaterialCount = 164;
            inline constexpr size_t ClusterCount = 168;
            inline constexpr size_t StringByteCount = 172;
            inline constexpr size_t TotalBoundsCenterX = 176;
            inline constexpr size_t TotalBoundsCenterY = 180;
            inline constexpr size_t TotalBoundsCenterZ = 184;
            inline constexpr size_t TotalBoundsRadius = 188;
            inline constexpr size_t ClusterAlgorithmId = 192;
            inline constexpr size_t ClusterAlgorithmVersion = 196;
            inline constexpr size_t ClusterMaxTriangles = 200;
            inline constexpr size_t ClusterMaxVertices = 204;
            inline constexpr size_t ClusterSettingsFlags = 208;
            inline constexpr size_t Flags = 212;
            inline constexpr size_t Reserved0 = 216;
            inline constexpr size_t Reserved1 = 224;
            inline constexpr size_t Reserved2 = 232;
            inline constexpr size_t Reserved3 = 240;
            inline constexpr size_t Reserved4 = 248;
        } // namespace HeaderOffset

        namespace VertexRecordOffset
        {
            inline constexpr size_t PositionX = 0;
            inline constexpr size_t PositionY = 4;
            inline constexpr size_t PositionZ = 8;
            inline constexpr size_t NormalX = 12;
            inline constexpr size_t NormalY = 16;
            inline constexpr size_t NormalZ = 20;
            inline constexpr size_t TexCoordU = 24;
            inline constexpr size_t TexCoordV = 28;
        } // namespace VertexRecordOffset

        namespace SubmeshRecordOffset
        {
            inline constexpr size_t IndexOffset = 0;
            inline constexpr size_t IndexCount = 4;
            inline constexpr size_t VertexOffset = 8;
            inline constexpr size_t VertexCount = 12;
            inline constexpr size_t MaterialIndex = 16;
            inline constexpr size_t ClusterOffset = 20;
            inline constexpr size_t ClusterCount = 24;
            inline constexpr size_t Flags = 28;
            inline constexpr size_t BoundsCenterX = 32;
            inline constexpr size_t BoundsCenterY = 36;
            inline constexpr size_t BoundsCenterZ = 40;
            inline constexpr size_t BoundsRadius = 44;
            inline constexpr size_t Reserved0 = 48;
            inline constexpr size_t Reserved1 = 56;
        } // namespace SubmeshRecordOffset

        namespace MaterialRecordOffset
        {
            inline constexpr size_t AlbedoTexture = 0;
            inline constexpr size_t NormalTexture = 16;
            inline constexpr size_t ArmTexture = 32;
            inline constexpr size_t Flags = 48;
            inline constexpr size_t Reserved0 = 52;
            inline constexpr size_t Reserved1 = 56;
        } // namespace MaterialRecordOffset

        namespace ClusterRecordOffset
        {
            inline constexpr size_t BoundsCenterX = 0;
            inline constexpr size_t BoundsCenterY = 4;
            inline constexpr size_t BoundsCenterZ = 8;
            inline constexpr size_t BoundsRadius = 12;
            inline constexpr size_t ConeAxisX = 16;
            inline constexpr size_t ConeAxisY = 20;
            inline constexpr size_t ConeAxisZ = 24;
            inline constexpr size_t ConeCutoff = 28;
            inline constexpr size_t IndexOffset = 32;
            inline constexpr size_t IndexCount = 36;
            inline constexpr size_t VertexOffset = 40;
            inline constexpr size_t VertexCount = 44;
            inline constexpr size_t MaterialIndex = 48;
            inline constexpr size_t LODLevel = 52;
            inline constexpr size_t LODError = 56;
            inline constexpr size_t ParentStart = 60;
            inline constexpr size_t ParentCount = 64;
            inline constexpr size_t Flags = 68;
            inline constexpr size_t Reserved0 = 72;
        } // namespace ClusterRecordOffset

        namespace StringRefRecordOffset
        {
            inline constexpr size_t StringOffset = 0;
            inline constexpr size_t StringLength = 8;
            inline constexpr size_t Reserved0 = 12;
        } // namespace StringRefRecordOffset
    } // namespace CookedMeshFormatV0

    // v1: LOD の階層（クラスタの DAG）を持つ形式。形式は Docs/Architecture/NVMESHv1.md を参照。
    // 頂点・サブメッシュ・材質・文字列の参照レコードは v0 と同じ並びで、クラスタのレコードが 128B、
    // グループの表が増え、ヘッダ末尾（v0 の予約領域）に グループ・LOD 段数・フォールバックの範囲を持つ。
    namespace CookedMeshFormatV1
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'M', 'E', 'S', 'H', 'v', '1'};
        inline constexpr size_t MagicSize = sizeof(Magic);

        inline constexpr uint16_t VersionMajor = 1;
        inline constexpr uint16_t VersionMinor = 0;
        inline constexpr uint32_t EndianMarker = 0x01020304u;

        inline constexpr size_t HeaderSize = 256;
        inline constexpr size_t VertexRecordSize = 32;
        inline constexpr size_t SubmeshRecordSize = 64;
        inline constexpr size_t MaterialRecordSize = 64;
        inline constexpr size_t ClusterRecordSize = 128;
        inline constexpr size_t GroupRecordSize = 48;
        inline constexpr size_t StringRefRecordSize = 16;
        inline constexpr size_t SectionAlignment = 8;

        inline constexpr uint32_t ClusterAlgorithmId = 2;
        inline constexpr uint32_t ClusterAlgorithmVersion = 0;
        inline constexpr uint32_t ClusterMaxTriangles = 128;
        inline constexpr uint32_t ClusterMaxVertices = 128;
        inline constexpr uint32_t ClusterSettingsFlags = 0;

        inline constexpr uint32_t MaxLODLevels = 64;
        inline constexpr uint32_t InvalidGroupId = 0xffffffffu;
        // 根のクラスタの親の誤差。無限大の代わりに有限の最大値を置く（NaN・Inf は拒否するため）
        inline constexpr float RootParentError = 3.402823466e+38f;
        inline constexpr uint32_t ClusterFlagRoot = 1u;

        // v1.1: クラスタのグループをページ（128 KiB）に詰めた形式（VersionMinor=1）。形式は NVMESHv1.md の「v1.1」を参照。
        inline constexpr uint16_t VersionMinorPaged = 1;
        // ページの大きさの上限。常駐する根のページ（粗い段とフォールバックの段を持つ）も同じ上限で、複数のページに分かれる
        inline constexpr size_t PageSize = 128 * 1024;
        inline constexpr size_t PageHeaderSize = 64;
        inline constexpr size_t PageTableRecordSize = 48;
        inline constexpr uint32_t RootPageId = 0;
        inline constexpr uint32_t InvalidPageId = 0xffffffffu;
        inline constexpr uint32_t PageFlagRoot = 1u;

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;
            inline constexpr size_t HeaderSize = 8;
            inline constexpr size_t VersionMajor = 12;
            inline constexpr size_t VersionMinor = 14;
            inline constexpr size_t EndianMarker = 16;
            inline constexpr size_t VertexRecordSize = 20;
            inline constexpr size_t SubmeshRecordSize = 24;
            inline constexpr size_t MaterialRecordSize = 28;
            inline constexpr size_t ClusterRecordSize = 32;
            inline constexpr size_t StringRefRecordSize = 36;
            inline constexpr size_t FileSize = 40;
            inline constexpr size_t SubmeshTableOffset = 48;
            inline constexpr size_t SubmeshTableSize = 56;
            inline constexpr size_t MaterialTableOffset = 64;
            inline constexpr size_t MaterialTableSize = 72;
            inline constexpr size_t ClusterTableOffset = 80;
            inline constexpr size_t ClusterTableSize = 88;
            inline constexpr size_t StringTableOffset = 96;
            inline constexpr size_t StringTableSize = 104;
            inline constexpr size_t VertexPayloadOffset = 112;
            inline constexpr size_t VertexPayloadSize = 120;
            inline constexpr size_t IndexPayloadOffset = 128;
            inline constexpr size_t IndexPayloadSize = 136;
            inline constexpr size_t PayloadHash = 144;
            inline constexpr size_t VertexCount = 152;
            inline constexpr size_t IndexCount = 156;
            inline constexpr size_t SubmeshCount = 160;
            inline constexpr size_t MaterialCount = 164;
            inline constexpr size_t ClusterCount = 168;
            inline constexpr size_t StringByteCount = 172;
            inline constexpr size_t TotalBoundsCenterX = 176;
            inline constexpr size_t TotalBoundsCenterY = 180;
            inline constexpr size_t TotalBoundsCenterZ = 184;
            inline constexpr size_t TotalBoundsRadius = 188;
            inline constexpr size_t ClusterAlgorithmId = 192;
            inline constexpr size_t ClusterAlgorithmVersion = 196;
            inline constexpr size_t ClusterMaxTriangles = 200;
            inline constexpr size_t ClusterMaxVertices = 204;
            inline constexpr size_t ClusterSettingsFlags = 208;
            inline constexpr size_t Flags = 212;
            inline constexpr size_t GroupRecordSize = 216;
            inline constexpr size_t GroupCount = 220;
            inline constexpr size_t GroupTableOffset = 224;
            inline constexpr size_t GroupTableSize = 232;
            inline constexpr size_t LODLevelCount = 240;
            inline constexpr size_t FallbackIndexOffset = 244;
            inline constexpr size_t FallbackIndexCount = 248;
            inline constexpr size_t FallbackError = 252;
        } // namespace HeaderOffset

        // v1.1 のヘッダで意味が変わる項目（位置は v1.0 と同じ）。クラスタ・頂点・インデックスの節は無く、
        // 代わりにページの表（ClusterTable の位置）とページの領域（VertexPayload の位置）を持つ。
        // IndexPayload は大きさ 0 で、領域の末尾（ファイルの末尾）に置く。
        namespace PagedHeaderOffset
        {
            inline constexpr size_t PageTableOffset = HeaderOffset::ClusterTableOffset;
            inline constexpr size_t PageTableSize = HeaderOffset::ClusterTableSize;
            inline constexpr size_t PageRegionOffset = HeaderOffset::VertexPayloadOffset;
            inline constexpr size_t PageRegionSize = HeaderOffset::VertexPayloadSize;
        } // namespace PagedHeaderOffset

        // ページの表のレコード（48B）。ページはファイル内の自己完結した範囲で、範囲読みで1ページずつ読める。
        namespace PageTableRecordOffset
        {
            inline constexpr size_t FileOffset = 0;
            inline constexpr size_t Size = 8;
            inline constexpr size_t ParentPageId = 12;
            inline constexpr size_t Flags = 16;
            inline constexpr size_t ClusterCount = 20;
            inline constexpr size_t FirstClusterIndex = 24;
            inline constexpr size_t VertexCount = 28;
            inline constexpr size_t IndexCount = 32;
            inline constexpr size_t Reserved0 = 36;
            inline constexpr size_t PageHash = 40;
        } // namespace PageTableRecordOffset

        // ページの先頭（64B）。続けてクラスタのレコード・頂点・クラスタのインデックス・フォールバックのインデックスが並ぶ。
        // オフセットはページの先頭からのバイト数。
        namespace PageHeaderOffset
        {
            inline constexpr size_t ClusterCount = 0;
            inline constexpr size_t VertexCount = 4;
            inline constexpr size_t IndexCount = 8;
            inline constexpr size_t FallbackIndexCount = 12;
            inline constexpr size_t ClusterOffset = 16;
            inline constexpr size_t VertexOffset = 20;
            inline constexpr size_t IndexOffset = 24;
            inline constexpr size_t FallbackIndexOffset = 28;
            inline constexpr size_t PageId = 32;
            inline constexpr size_t Flags = 36;
            inline constexpr size_t Reserved0 = 40;
            inline constexpr size_t Reserved1 = 48;
            inline constexpr size_t Reserved2 = 56;
        } // namespace PageHeaderOffset

        // 頂点・サブメッシュ・材質・文字列の参照レコードは v0 と同じ
        namespace VertexRecordOffset = CookedMeshFormatV0::VertexRecordOffset;
        namespace SubmeshRecordOffset = CookedMeshFormatV0::SubmeshRecordOffset;
        namespace MaterialRecordOffset = CookedMeshFormatV0::MaterialRecordOffset;
        namespace StringRefRecordOffset = CookedMeshFormatV0::StringRefRecordOffset;

        namespace ClusterRecordOffset
        {
            inline constexpr size_t SelfCenterX = 0;
            inline constexpr size_t SelfCenterY = 4;
            inline constexpr size_t SelfCenterZ = 8;
            inline constexpr size_t SelfRadius = 12;
            inline constexpr size_t SelfError = 16;
            inline constexpr size_t GroupId = 20;
            inline constexpr size_t ParentCenterX = 24;
            inline constexpr size_t ParentCenterY = 28;
            inline constexpr size_t ParentCenterZ = 32;
            inline constexpr size_t ParentRadius = 36;
            inline constexpr size_t ParentError = 40;
            inline constexpr size_t LODLevel = 44;
            inline constexpr size_t ConeAxisX = 48;
            inline constexpr size_t ConeAxisY = 52;
            inline constexpr size_t ConeAxisZ = 56;
            inline constexpr size_t ConeCutoff = 60;
            inline constexpr size_t IndexOffset = 64;
            inline constexpr size_t IndexCount = 68;
            inline constexpr size_t VertexOffset = 72;
            inline constexpr size_t VertexCount = 76;
            inline constexpr size_t MaterialIndex = 80;
            inline constexpr size_t PageId = 84;
            inline constexpr size_t Flags = 88;
            inline constexpr size_t Reserved0 = 92;
            inline constexpr size_t Reserved1 = 96;
            inline constexpr size_t Reserved2 = 104;
            inline constexpr size_t Reserved3 = 112;
            inline constexpr size_t Reserved4 = 120;
        } // namespace ClusterRecordOffset

        namespace GroupRecordOffset
        {
            inline constexpr size_t BoundsCenterX = 0;
            inline constexpr size_t BoundsCenterY = 4;
            inline constexpr size_t BoundsCenterZ = 8;
            inline constexpr size_t BoundsRadius = 12;
            inline constexpr size_t Error = 16;
            inline constexpr size_t ClusterOffset = 20;
            inline constexpr size_t ClusterCount = 24;
            inline constexpr size_t LODLevel = 28;
            inline constexpr size_t Flags = 32;
            inline constexpr size_t Reserved0 = 36;
            inline constexpr size_t Reserved1 = 40;
        } // namespace GroupRecordOffset
    } // namespace CookedMeshFormatV1

    enum class CookedMeshParseStatus : uint8_t
    {
        Success,
        InvalidBlob,
        EmptyBlob,
        HeaderTooSmall,
        BadMagic,
        UnsupportedVersion,
        EndianMismatch,
        HeaderSizeMismatch,
        RecordSizeMismatch,
        FileSizeMismatch,
        ReservedFieldNonZero,
        PaddingByteNonZero,
        SectionOutOfRange,
        SectionMisalignment,
        SectionPackingMismatch,
        PayloadHashMismatch,
        InvalidCounts,
        InvalidFloatOrBounds,
        InvalidIndexRange,
        InvalidClusterRange,
        InvalidStringTable,
        InvalidPath,
        InvalidMaterialTextureReference,
        UnsupportedV0Feature,
        IntegerOverflow,
        // v1 で足した拒否理由（既存の値を変えないよう末尾に足す）
        InvalidGroupTable,
        InvalidLODGraph,
        InvalidFallbackRange,
        UnsupportedV1Feature,
        // v1.1（ページ）で足した拒否理由
        InvalidPageTable,
        InvalidPageData,
        PageHashMismatch
    };

    struct CookedMeshFloat2
    {
        float U = 0.0f;
        float V = 0.0f;
    };

    struct CookedMeshFloat3
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
    };

    struct CookedMeshVertex
    {
        CookedMeshFloat3 Position;
        CookedMeshFloat3 Normal;
        CookedMeshFloat2 TexCoord;
    };

    struct CookedMeshStringRef
    {
        size_t StringOffset = 0;
        size_t StringLength = 0;
    };

    struct CookedMeshSubmesh
    {
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
        uint32_t VertexOffset = 0;
        uint32_t VertexCount = 0;
        uint32_t MaterialIndex = 0;
        uint32_t ClusterOffset = 0;
        uint32_t ClusterCount = 0;
        CookedMeshFloat3 BoundsCenter;
        float BoundsRadius = 0.0f;
    };

    struct CookedMeshMaterial
    {
        CookedMeshStringRef AlbedoTexture;
        CookedMeshStringRef NormalTexture;
        CookedMeshStringRef ArmTexture;
    };

    struct CookedMeshCluster
    {
        CookedMeshFloat3 BoundsCenter;
        float BoundsRadius = 0.0f;
        CookedMeshFloat3 ConeAxis;
        float ConeCutoff = 0.0f;
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
        uint32_t VertexOffset = 0;
        uint32_t VertexCount = 0;
        uint32_t MaterialIndex = 0;
        uint32_t LODLevel = 0;
        float LODError = 0.0f;
        uint32_t ParentStart = 0;
        uint32_t ParentCount = 0;

        // v1 の LOD の階層。BoundsCenter・BoundsRadius・LODError が自分の境界球と誤差、
        // 下の Parent* が「このクラスタを含むグループを簡略化した結果（親）」の境界球と誤差。
        // v0 のクラスタは階層を持たない単独の根として読む（GroupId=InvalidGroupId、bIsRoot=true、ParentError=最大値）。
        CookedMeshFloat3 ParentBoundsCenter;
        float ParentBoundsRadius = 0.0f;
        float ParentError = CookedMeshFormatV1::RootParentError;
        uint32_t GroupId = CookedMeshFormatV1::InvalidGroupId;
        uint32_t PageId = 0;
        bool bIsRoot = true;
    };

    // v1 のクラスタのグループ。同じグループのクラスタは同じ親の境界球と誤差を持ち、同じ判断で描く・描かないが決まる。
    // メンバのクラスタは Clusters の連続した範囲に並ぶ。
    struct CookedMeshClusterGroup
    {
        CookedMeshFloat3 BoundsCenter;
        float BoundsRadius = 0.0f;
        float Error = 0.0f;
        uint32_t ClusterOffset = 0;
        uint32_t ClusterCount = 0;
        uint32_t LODLevel = 0;
    };

    // v1.1 のページの表の1行。ページはクラスタのグループを詰めた128 KiB 以下の自己完結した範囲で、
    // 頂点・インデックス・クラスタの記録を自分の中のオフセットで持つ。根のページ（bIsRoot）は、
    // 粗い段のクラスタとフォールバックの段を持って常駐するページで、0 番から連続して並ぶ（1つ以上）。
    struct CookedMeshPage
    {
        // ファイルの先頭からのバイト位置と大きさ（範囲読みの単位）
        uint64_t FileOffset = 0;
        uint32_t Size = 0;
        // 親のクラスタを持つページのうち最も小さい番号（このページを描くには、先に親のページが要る）。根のページは InvalidPageId
        uint32_t ParentPageId = CookedMeshFormatV1::InvalidPageId;
        bool bIsRoot = false;
        // Clusters の中の、このページのクラスタの範囲（ページの順に連続して並ぶ）
        uint32_t FirstClusterIndex = 0;
        uint32_t ClusterCount = 0;
        // Vertices・Indices の中の、このページの頂点・クラスタのインデックスの範囲（フォールバックのインデックスは含まない）
        uint32_t FirstVertex = 0;
        uint32_t VertexCount = 0;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        uint64_t Hash = 0;
    };

    struct CookedMeshData
    {
        AssetBlob SourceBlob;
        // 読んだ形式の主版（0 か 1）。0 は従来の1段のメッシュ（LODLevelCount=1、グループ無し）
        uint16_t FormatMajor = 0;
        // 副版。v1.0 は 0、ページに詰めた v1.1 は 1（Pages が空でない）。v0 は 0
        uint16_t FormatMinor = 0;
        CookedMeshFloat3 TotalBoundsCenter;
        float TotalBoundsRadius = 0.0f;
        uint64_t PayloadHash = 0;
        size_t StringTableOffset = 0;
        size_t StringTableSize = 0;
        uint32_t LODLevelCount = 1;
        // RT・影のための常駐の粗い段のインデックスの範囲（Indices の要素の位置と数。基点の頂点は 0）。
        // v0 は全体の範囲（0, IndexCount）で誤差 0。v1 はクラスタのインデックスの後ろに続く別の範囲。
        uint32_t FallbackIndexOffset = 0;
        uint32_t FallbackIndexCount = 0;
        float FallbackError = 0.0f;
        Container::VariableArray<CookedMeshVertex> Vertices;
        Container::VariableArray<CookedMeshSubmesh> Submeshes;
        Container::VariableArray<CookedMeshMaterial> Materials;
        Container::VariableArray<CookedMeshCluster> Clusters;
        Container::VariableArray<CookedMeshClusterGroup> Groups;
        // v1.1 のページの表（v1.0・v0 は空）。クラスタの PageId はここへの添字
        Container::VariableArray<CookedMeshPage> Pages;
        Container::VariableArray<uint32_t> Indices;

        [[nodiscard]] Container::AnsiStringView GetString(const CookedMeshStringRef& stringRef) const noexcept;
    };

    struct CookedMeshParseResult
    {
        CookedMeshParseStatus Status = CookedMeshParseStatus::InvalidBlob;
        CookedMeshData Mesh;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == CookedMeshParseStatus::Success;
        }
    };

    [[nodiscard]] constexpr uint64_t ComputeCookedMeshPayloadHash(const uint8_t* data, size_t size) noexcept
    {
        uint64_t hash = CookedMeshFormatV0::Fnv1a64OffsetBasis;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<uint64_t>(data[index]);
            hash *= CookedMeshFormatV0::Fnv1a64Prime;
        }
        return hash;
    }

    [[nodiscard]] constexpr uint64_t ComputeCookedMeshPayloadHash(Container::Span<const uint8_t> bytes) noexcept
    {
        return ComputeCookedMeshPayloadHash(bytes.data(), bytes.size());
    }

    // 先頭の magic で v0・v1 を判別して読む。v0 は従来の1段のメッシュとして返す。
    [[nodiscard]] CookedMeshParseResult ParseCookedMesh(AssetBlob sourceBlob);

    // v1 の書き出しの入力。検証は ParseCookedMesh が行う（書き出し側は表の組み立てと hash だけ）。
    struct CookedMeshV1WriteInput
    {
        CookedMeshFloat3 TotalBoundsCenter;
        float TotalBoundsRadius = 0.0f;
        uint32_t LODLevelCount = 1;
        float FallbackError = 0.0f;
        Container::VariableArray<CookedMeshVertex> Vertices;
        Container::VariableArray<CookedMeshCluster> Clusters;
        Container::VariableArray<CookedMeshClusterGroup> Groups;
        // クラスタのインデックス（Clusters の IndexOffset の順に連続して並べる）
        Container::VariableArray<uint32_t> ClusterIndices;
        // フォールバックの段のインデックス（基点の頂点は 0。クラスタのインデックスの後ろへ書かれる）
        Container::VariableArray<uint32_t> FallbackIndices;
        Container::AnsiStringView AlbedoTexture;
        Container::AnsiStringView NormalTexture;
        Container::AnsiStringView ArmTexture;
    };

    // v1 のバイト列を組み立てる。件数が 32bit に収まらないときは false。
    [[nodiscard]] bool SerializeCookedMeshV1(const CookedMeshV1WriteInput& input,
                                             Container::VariableArray<uint8_t>& outBytes);

    // 1ページの中身（ParseCookedMeshPage の出力）。クラスタの IndexOffset・VertexOffset はページの中での位置。
    struct CookedMeshPageContent
    {
        Container::VariableArray<CookedMeshCluster> Clusters;
        Container::VariableArray<CookedMeshVertex> Vertices;
        // クラスタのインデックス（クラスタの頂点の範囲の先頭からの相対）
        Container::VariableArray<uint32_t> Indices;
        // フォールバックのインデックス（根のページだけ。ページの頂点への添字）。根のページは、フォールバックの三角形の
        // 一部（0 個以上）をこのページの頂点だけで持ち、全部の根のページを合わせると粗い形が閉じる
        Container::VariableArray<uint32_t> FallbackIndices;
    };

    // v1.1 のページのバイト列（ページの表の FileOffset・Size の範囲）を単独で読んで検査する。
    // 他のページや表を見ずに、ページの中の範囲・インデックス・クラスタの記録だけで検査できる。
    // lodLevelCount・groupCount は、ヘッダの値（クラスタの段・グループの番号の範囲の検査に使う）。
    // bIsRoot は、ページの表の根の印（ページの先頭の flags と一致する）。
    [[nodiscard]] CookedMeshParseStatus ParseCookedMeshPage(Container::Span<const uint8_t> pageBytes,
                                                            uint32_t pageId,
                                                            bool bIsRoot,
                                                            uint32_t lodLevelCount,
                                                            uint32_t groupCount,
                                                            CookedMeshPageContent& outContent);

    // ページに詰めて書くときの設定
    struct CookedMeshPagedWriteOptions
    {
        // 通常のページの大きさの上限（バイト）。既定は PageSize（128 KiB）
        uint32_t PageSizeBytes = static_cast<uint32_t>(CookedMeshFormatV1::PageSize);
        // 根のページ（常駐）に入れる粗い段のクラスタの合計の目安（バイト）。最も粗い段（根）は常に入り、
        // その下の段は、足して目安以内に収まる間だけ段ごとまとめて入る（フォールバックの段は別に、根のページへ追加する）
        uint32_t RootClusterBudgetBytes = 512 * 1024;
    };

    enum class CookedMeshPagedWriteStatus : uint8_t
    {
        Success,
        // 入力のクラスタ・グループ・インデックスの範囲が食い違う、または有限でない値がある
        InvalidInput,
        // 1つのグループがページの上限に収まらない（Info の LargestGroupBytes・LargestGroupIndex）
        GroupExceedsPage,
        // 件数が 32bit に収まらない
        TooLarge
    };

    struct CookedMeshPagedWriteInfo
    {
        uint32_t PageCount = 0;
        // 根のページ（常駐する、0 番から連続した全ページ）の数・大きさの合計・クラスタの数の合計
        uint32_t RootPageCount = 0;
        uint32_t RootPageBytes = 0;
        uint32_t RootPageClusterCount = 0;
        // 根のページに入った最も細かい段（これ以上の段が常駐する）
        uint32_t RootPageMinLODLevel = 0;
        // 全ページの大きさの最大と、根のページ以外のページの大きさの合計
        uint32_t MaxPageBytes = 0;
        uint64_t NonRootPageBytes = 0;
        // 最も大きいグループが占めるページ内のバイト数と、そのグループの番号（グループを持たない根のクラスタは、クラスタの番号）。
        // GroupExceedsPage のときは、収まらなかったグループ
        uint32_t LargestGroupBytes = 0;
        uint32_t LargestGroupIndex = 0;
    };

    // v1.1（ページに詰めた v1）のバイト列を組み立てる。入力は SerializeCookedMeshV1 と同じ（クラスタは段ごとに
    // グループのメンバが連続して並ぶ平らな表）で、ここで次を行う。
    //   - 根のページ（0 番から連続した常駐のページ）: 最も粗い段と、収まる限りの粗い段のクラスタ、フォールバックの段
    //     （専用の頂点つき。ページの上限を超えるときはクラスタを持たない根のページへ続ける）
    //   - 他のページ: 1つの段のグループを、1つのグループが1つのページに収まるように詰める（粗い段のページから順に）
    // クラスタは、ページの順（根のページ → 粗い段 → 細かい段）に並べ替えて書く。検査は ParseCookedMesh が行う。
    [[nodiscard]] CookedMeshPagedWriteStatus SerializeCookedMeshV1Paged(const CookedMeshV1WriteInput& input,
                                                                        const CookedMeshPagedWriteOptions& options,
                                                                        Container::VariableArray<uint8_t>& outBytes,
                                                                        CookedMeshPagedWriteInfo& outInfo);
} // namespace NorvesLib::Core::Asset
