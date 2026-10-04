#include "Asset/CookedMeshWireValidation.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
namespace NorvesLib::Core::Asset
{
    namespace
    {
        using Status=CookedMeshWireStatus;
        static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
        uint32_t U32(const uint8_t* p)
        {
            return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
        }
        uint64_t U64(const uint8_t* p)
        {
            return uint64_t(U32(p)) | (uint64_t(U32(p+4))<<32);
        }
        uint16_t U16(const uint8_t* p)
        {
            return static_cast<uint16_t>(uint16_t(p[0]) | (uint16_t(p[1])<<8));
        }
        float F32(const uint8_t* p)
        {
            return std::bit_cast<float>(U32(p));
        }
        bool Storage(const void* p,size_t size)
        {
            return (p || size==0) && size<=UINTPTR_MAX-reinterpret_cast<uintptr_t>(p);
        }
        bool Overlap(const void* a,size_t aSize,const void* b,size_t bSize)
        {
            const auto x=reinterpret_cast<uintptr_t>(a), y=reinterpret_cast<uintptr_t>(b);
            return x<y+bSize && y<x+aSize;
        }
        bool Zero(const uint8_t* p,size_t count)
        {
            for (size_t index=0;index<count;++index)
            {
                if (p[index]!=0)
                {
                    return false;
                }
            }
            return true;
        }
        uint64_t Hash(const uint8_t* p,size_t count)
        {
            uint64_t hash=CookedMeshFormatV0::Fnv1a64OffsetBasis;
            for (size_t index=0;index<count;++index)
            {
                hash=(hash^p[index])*CookedMeshFormatV0::Fnv1a64Prime;
            }
            return hash;
        }
    }
    CookedMeshWireStatus ValidateCookedMeshWireEnvelope(Container::Span<const uint8_t> bytes, CookedMeshWireEnvelope& out) noexcept
    {
        using namespace CookedMeshFormatV0;
        using H=CookedMeshWireEnvelope;
        if (bytes.size()<HeaderSize || !Storage(bytes.data(),bytes.size()) || !Storage(&out,sizeof(out)) ||
            Overlap(bytes.data(),bytes.size(),&out,sizeof(out)))
        {
            return Status::InvalidInput;
        }
        const uint8_t* p=bytes.data();
        H value;
        value.VersionMajor=U16(p+HeaderOffset::VersionMajor);
        if (value.VersionMajor>1 || U16(p+HeaderOffset::VersionMinor)!=0)
        {
            return Status::UnsupportedVersion;
        }
        if (std::memcmp(p,value.VersionMajor==0 ? Magic : CookedMeshFormatV1::Magic,MagicSize)!=0)
        {
            return Status::BadMagic;
        }
        if (U32(p+HeaderOffset::HeaderSize)!=HeaderSize || U32(p+HeaderOffset::EndianMarker)!=EndianMarker)
        {
            return Status::InvalidHeader;
        }
        value.MaterialRecordSize=value.VersionMajor==0 ? MaterialRecordSize : CookedMeshFormatV1::MaterialRecordSize;
        value.ClusterRecordSize=value.VersionMajor==0 ? ClusterRecordSize : CookedMeshFormatV1::ClusterRecordSize;
        if (U32(p+HeaderOffset::VertexRecordSize)!=VertexRecordSize || U32(p+HeaderOffset::SubmeshRecordSize)!=SubmeshRecordSize ||
            U32(p+HeaderOffset::MaterialRecordSize)!=value.MaterialRecordSize || U32(p+HeaderOffset::ClusterRecordSize)!=value.ClusterRecordSize ||
            U32(p+HeaderOffset::StringRefRecordSize)!=StringRefRecordSize)
        {
            return Status::InvalidRecordSize;
        }
        if (U64(p+HeaderOffset::FileSize)!=bytes.size())
        {
            return Status::InvalidFileSize;
        }
        if (!Zero(p+HeaderOffset::Flags,HeaderSize-HeaderOffset::Flags))
        {
            return Status::InvalidReserved;
        }
        value.VertexCount=U32(p+HeaderOffset::VertexCount); value.IndexCount=U32(p+HeaderOffset::IndexCount);
        value.SubmeshCount=U32(p+HeaderOffset::SubmeshCount); value.MaterialCount=U32(p+HeaderOffset::MaterialCount);
        value.ClusterCount=U32(p+HeaderOffset::ClusterCount); value.StringByteCount=U32(p+HeaderOffset::StringByteCount);
        if (value.ClusterCount==0 || value.IndexCount%3!=0 ||
            (value.VersionMajor==0 ? (value.SubmeshCount!=1 || value.MaterialCount!=1) :
                (value.VertexCount==0 || value.IndexCount==0 || value.SubmeshCount==0 || value.MaterialCount==0 ||
                 value.SubmeshCount>value.IndexCount/3 || value.ClusterCount>value.IndexCount/3)))
        {
            return Status::InvalidCounts;
        }
        const uint64_t sizes[]{uint64_t(value.SubmeshCount)*SubmeshRecordSize,uint64_t(value.MaterialCount)*value.MaterialRecordSize,
            uint64_t(value.ClusterCount)*value.ClusterRecordSize,value.StringByteCount,uint64_t(value.VertexCount)*VertexRecordSize,uint64_t(value.IndexCount)*4};
        uint64_t cursor=HeaderSize;
        for (size_t index=0;index<6;++index)
        {
            auto& section=value.Sections[index];
            section.Offset=U64(p+48+index*16); section.Size=U64(p+56+index*16);
            if (section.Size!=sizes[index])
            {
                return Status::InvalidCounts;
            }
            if (section.Size>UINT64_MAX-section.Offset || cursor>UINT64_MAX-(SectionAlignment-1))
            {
                return Status::IntegerOverflow;
            }
            if (section.Offset<HeaderSize || section.Offset>bytes.size() || section.Size>bytes.size()-section.Offset)
            {
                return Status::InvalidRange;
            }
            if (section.Offset%SectionAlignment!=0)
            {
                return Status::InvalidAlignment;
            }
            const uint64_t aligned=(cursor+SectionAlignment-1)&~uint64_t(SectionAlignment-1);
            if (section.Offset!=aligned)
            {
                return Status::InvalidPacking;
            }
            if (!Zero(p+static_cast<size_t>(cursor),static_cast<size_t>(section.Offset-cursor)))
            {
                return Status::InvalidPadding;
            }
            cursor=section.Offset+section.Size;
        }
        if (cursor!=bytes.size())
        {
            return Status::InvalidFileSize;
        }
        if (Hash(p+HeaderSize,bytes.size()-HeaderSize)!=U64(p+HeaderOffset::PayloadHash))
        {
            return Status::InvalidHash;
        }
        for (size_t index=0;index<3;++index)
        {
            value.BoundsCenter[index]=F32(p+HeaderOffset::TotalBoundsCenterX+index*4);
            if (!std::isfinite(value.BoundsCenter[index]))
            {
                return Status::InvalidBounds;
            }
        }
        value.BoundsRadius=F32(p+HeaderOffset::TotalBoundsRadius);
        if (!std::isfinite(value.BoundsRadius) || value.BoundsRadius<0)
        {
            return Status::InvalidBounds;
        }
        if (U32(p+HeaderOffset::ClusterAlgorithmId)!=ClusterAlgorithmId || U32(p+HeaderOffset::ClusterAlgorithmVersion)!=ClusterAlgorithmVersion ||
            U32(p+HeaderOffset::ClusterMaxTriangles)!=ClusterMaxTriangles || U32(p+HeaderOffset::ClusterMaxVertices)!=ClusterMaxVertices ||
            U32(p+HeaderOffset::ClusterSettingsFlags)!=ClusterSettingsFlags)
        {
            return Status::UnsupportedFeature;
        }
        out=value;
        return Status::Success;
    }
    CookedMeshWireStatus ReadCookedMeshLod0Cluster(Container::Span<const uint8_t> bytes, uint16_t versionMajor,
        uint32_t vertexCount,uint32_t indexCount,uint32_t materialCount,CookedMeshLod0Cluster& out) noexcept
    {
        using namespace CookedMeshFormatV0;
        if (versionMajor>1)
        {
            return Status::UnsupportedVersion;
        }
        const size_t expected=versionMajor==0 ? ClusterRecordSize : CookedMeshFormatV1::ClusterRecordSize;
        if (bytes.size()!=expected || !Storage(bytes.data(),bytes.size()) || !Storage(&out,sizeof(out)) ||
            Overlap(bytes.data(),bytes.size(),&out,sizeof(out)))
        {
            return Status::InvalidInput;
        }
        const uint8_t* p=bytes.data();
        if (U32(p+68)!=0 || U64(p+72)!=0 || (versionMajor==1 && U64(p+120)!=0))
        {
            return Status::InvalidReserved;
        }
        const float lodError=F32(p+56);
        if (!std::isfinite(lodError))
        {
            return Status::InvalidBounds;
        }
        if (U32(p+40)!=0 || U32(p+52)!=0 || lodError!=0 || U32(p+60)!=0 || U32(p+64)!=0 ||
            (versionMajor==1 && !Zero(p+80,40)))
        {
            return Status::UnsupportedFeature;
        }
        CookedMeshLod0Cluster value;
        for (size_t index=0;index<3;++index)
        {
            value.BoundsCenter[index]=F32(p+index*4); value.ConeAxis[index]=F32(p+16+index*4);
            if (!std::isfinite(value.BoundsCenter[index]) || !std::isfinite(value.ConeAxis[index]))
            {
                return Status::InvalidBounds;
            }
        }
        value.BoundsRadius=F32(p+12); value.ConeCutoff=F32(p+28);
        if (!std::isfinite(value.BoundsRadius) || value.BoundsRadius<0 || !std::isfinite(value.ConeCutoff))
        {
            return Status::InvalidBounds;
        }
        value.IndexOffset=U32(p+32); value.IndexCount=U32(p+36);
        value.VertexCount=U32(p+44); value.MaterialIndex=U32(p+48);
        if (value.IndexOffset%3!=0 || value.IndexCount%3!=0 || uint64_t(value.IndexOffset)+value.IndexCount>indexCount ||
            value.VertexCount>vertexCount || (versionMajor==0 ? value.MaterialIndex!=0 :
                (value.MaterialIndex>=materialCount || value.IndexCount==0 || value.IndexCount>ClusterMaxTriangles*3)))
        {
            return Status::InvalidClusterRange;
        }
        out=value;
        return Status::Success;
    }
    CookedMeshWireStatus ValidateCookedMeshV1Partitions(Container::Span<const CookedMeshWireSubmesh> submeshes,
        Container::Span<const CookedMeshLod0Cluster> clusters, Container::Span<const uint32_t> indices,
        uint32_t vertexCount,uint32_t materialCount) noexcept
    {
        if (submeshes.empty() || clusters.empty() || indices.empty() || !vertexCount || !materialCount ||
            submeshes.size()>UINT32_MAX || clusters.size()>UINT32_MAX || indices.size()>UINT32_MAX || indices.size()%3!=0)
        {
            return Status::InvalidCounts;
        }
        if (submeshes.size()>SIZE_MAX/sizeof(CookedMeshWireSubmesh) || clusters.size()>SIZE_MAX/sizeof(CookedMeshLod0Cluster) ||
            indices.size()>SIZE_MAX/sizeof(uint32_t) || !Storage(submeshes.data(),submeshes.size()*sizeof(CookedMeshWireSubmesh)) ||
            !Storage(clusters.data(),clusters.size()*sizeof(CookedMeshLod0Cluster)) || !Storage(indices.data(),indices.size()*sizeof(uint32_t)))
        {
            return Status::InvalidInput;
        }
        for (uint32_t vertex : indices)
        {
            if (vertex>=vertexCount)
            {
                return Status::InvalidIndexRange;
            }
        }
        uint64_t nextIndex=0,nextCluster=0;
        for (const auto& submesh : submeshes)
        {
            const uint64_t indexEnd=uint64_t(submesh.IndexOffset)+submesh.IndexCount;
            const uint64_t clusterEnd=uint64_t(submesh.ClusterOffset)+submesh.ClusterCount;
            if (submesh.IndexOffset!=nextIndex || submesh.IndexCount==0 || submesh.IndexCount%3!=0 || indexEnd>indices.size() ||
                submesh.VertexCount>vertexCount || submesh.MaterialIndex>=materialCount)
            {
                return Status::InvalidIndexRange;
            }
            if (submesh.ClusterOffset!=nextCluster || submesh.ClusterCount==0 || clusterEnd>clusters.size())
            {
                return Status::InvalidClusterRange;
            }
            uint64_t clusterIndexCursor=nextIndex;
            for (uint64_t index=nextCluster;index<clusterEnd;++index)
            {
                const auto& cluster=clusters[static_cast<size_t>(index)];
                const uint64_t end=uint64_t(cluster.IndexOffset)+cluster.IndexCount;
                if (cluster.IndexOffset!=clusterIndexCursor || cluster.IndexCount==0 || cluster.IndexCount%3!=0 ||
                    cluster.IndexCount>CookedMeshFormatV1::ClusterMaxTriangles*3 || end>indexEnd ||
                    cluster.MaterialIndex!=submesh.MaterialIndex || cluster.VertexCount>vertexCount)
                {
                    return Status::InvalidClusterRange;
                }
                uint32_t unique[CookedMeshFormatV1::ClusterMaxVertices]{};
                uint32_t uniqueCount=0;
                for (uint64_t at=cluster.IndexOffset;at<end;++at)
                {
                    const uint32_t vertex=indices[static_cast<size_t>(at)];
                    if (submesh.VertexCount!=0 && vertex>=submesh.VertexCount)
                    {
                        return Status::InvalidIndexRange;
                    }
                    if (cluster.VertexCount!=0 && vertex>=cluster.VertexCount)
                    {
                        return Status::InvalidClusterRange;
                    }
                    bool present=false;
                    for (uint32_t known=0;known<uniqueCount;++known)
                    {
                        present=present || unique[known]==vertex;
                    }
                    if (!present)
                    {
                        if (uniqueCount==CookedMeshFormatV1::ClusterMaxVertices)
                        {
                            return Status::InvalidClusterRange;
                        }
                        unique[uniqueCount++]=vertex;
                    }
                }
                clusterIndexCursor=end;
            }
            if (clusterIndexCursor!=indexEnd)
            {
                return Status::InvalidClusterRange;
            }
            nextIndex=indexEnd; nextCluster=clusterEnd;
        }
        return nextIndex==indices.size() && nextCluster==clusters.size() ? Status::Success : Status::InvalidClusterRange;
    }
}
