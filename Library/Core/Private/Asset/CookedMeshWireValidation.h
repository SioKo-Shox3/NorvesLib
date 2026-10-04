// NVMESHの外枠とLOD0 clusterだけを検証する。頂点・材質・submeshの意味検証は別工程。
#pragma once
#include "Asset/CookedMeshWireFormat.h"
#include "Container/Span.h"
namespace NorvesLib::Core::Asset
{
    enum class CookedMeshWireStatus
    {
        Success, InvalidInput, BadMagic, UnsupportedVersion, InvalidHeader, InvalidRecordSize,
        InvalidFileSize, InvalidCounts, IntegerOverflow, InvalidRange, InvalidAlignment, InvalidPacking,
        InvalidPadding, InvalidHash, InvalidReserved, InvalidBounds, UnsupportedFeature, InvalidClusterRange
    };
    struct CookedMeshWireSection { uint64_t Offset=0, Size=0; };
    struct CookedMeshWireEnvelope
    {
        uint16_t VersionMajor=0;
        uint32_t MaterialRecordSize=0, ClusterRecordSize=0;
        uint32_t VertexCount=0, IndexCount=0, SubmeshCount=0, MaterialCount=0, ClusterCount=0, StringByteCount=0;
        CookedMeshWireSection Sections[6]{}; // submesh/material/cluster/string/vertex/index
        float BoundsCenter[3]{};
        float BoundsRadius=0;
    };
    struct CookedMeshLod0Cluster
    {
        float BoundsCenter[3]{}, BoundsRadius=0, ConeAxis[3]{}, ConeCutoff=0;
        uint32_t IndexOffset=0, IndexCount=0, VertexCount=0, MaterialIndex=0;
    };
    // 失敗時out保持、入力とoutの領域重複は拒否。受理版はv0.0/v1.0のみ。
    [[nodiscard]] CookedMeshWireStatus ValidateCookedMeshWireEnvelope(Container::Span<const uint8_t> bytes,
        CookedMeshWireEnvelope& out) noexcept;
    [[nodiscard]] CookedMeshWireStatus ReadCookedMeshLod0Cluster(Container::Span<const uint8_t> bytes,
        uint16_t versionMajor, uint32_t vertexCount, uint32_t indexCount, uint32_t materialCount,
        CookedMeshLod0Cluster& out) noexcept;
}
