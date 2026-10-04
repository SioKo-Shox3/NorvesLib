#pragma once

#include "Asset/AssetBlob.h"
#include "Asset/CookedMeshWireFormat.h"
#include "Container/Span.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{


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
        UnsupportedV1Feature,
        InvalidMaterialRecord
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
        CookedMeshStringRef EmissiveTexture;
        CookedMaterialRecord Pbr;
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
    };

    struct CookedMeshData
    {
        AssetBlob SourceBlob;
        CookedMeshFloat3 TotalBoundsCenter;
        float TotalBoundsRadius = 0.0f;
        uint64_t PayloadHash = 0;
        size_t StringTableOffset = 0;
        size_t StringTableSize = 0;
        Container::VariableArray<CookedMeshVertex> Vertices;
        Container::VariableArray<CookedMeshSubmesh> Submeshes;
        Container::VariableArray<CookedMeshMaterial> Materials;
        Container::VariableArray<CookedMeshCluster> Clusters;
        Container::VariableArray<uint32_t> Indices;
        uint16_t VersionMajor = 0;

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

    [[nodiscard]] CookedMeshParseResult ParseCookedMesh(AssetBlob sourceBlob);
} // namespace NorvesLib::Core::Asset
