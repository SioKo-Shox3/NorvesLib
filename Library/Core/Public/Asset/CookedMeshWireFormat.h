#pragma once
#include "Asset/CookedMaterialFormat.h"
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
        inline constexpr size_t MaterialRecordSize = 128;
        inline constexpr size_t ClusterRecordSize = 128;
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

        namespace MaterialRecordOffset = CookedMaterialFormatV1;

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
            inline constexpr size_t SelfBoundsCenterX = 80;
            inline constexpr size_t SelfBoundsCenterY = 84;
            inline constexpr size_t SelfBoundsCenterZ = 88;
            inline constexpr size_t SelfBoundsRadius = 92;
            inline constexpr size_t ParentBoundsCenterX = 96;
            inline constexpr size_t ParentBoundsCenterY = 100;
            inline constexpr size_t ParentBoundsCenterZ = 104;
            inline constexpr size_t ParentBoundsRadius = 108;
            inline constexpr size_t ParentError = 112;
            inline constexpr size_t GroupId = 116;
            inline constexpr size_t Reserved1 = 120;
        } // namespace ClusterRecordOffset

        namespace StringRefRecordOffset
        {
            inline constexpr size_t StringOffset = 0;
            inline constexpr size_t StringLength = 8;
            inline constexpr size_t Reserved0 = 12;
        } // namespace StringRefRecordOffset
    } // namespace CookedMeshFormatV1
}
