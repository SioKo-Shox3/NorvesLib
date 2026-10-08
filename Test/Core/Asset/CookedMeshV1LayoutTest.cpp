#include "Asset/CookedMeshWireValidation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib::Core::Asset;
using S=CookedMeshWireStatus;
int main()
{
    CookedMeshWireSubmesh subs[]={{0,3,6,0,0,1},{3,3,6,1,1,1}};
    CookedMeshLod0Cluster clusters[2];
    clusters[0].IndexCount=3;clusters[0].VertexCount=6;
    clusters[1].IndexOffset=3;clusters[1].IndexCount=3;clusters[1].VertexCount=6;clusters[1].MaterialIndex=1;
    uint32_t indices[]={0,1,2,3,4,5};
    const auto check=[&]()
    {
        return ValidateCookedMeshV1Partitions(subs,clusters,indices,6,2);
    };
    assert(check()==S::Success);
    subs[1].IndexOffset=0;assert(check()==S::InvalidIndexRange);subs[1].IndexOffset=3;
    subs[1].ClusterOffset=0;assert(check()==S::InvalidClusterRange);subs[1].ClusterOffset=1;
    subs[0].ClusterCount=0;assert(check()==S::InvalidClusterRange);subs[0].ClusterCount=1;
    subs[1].MaterialIndex=2;assert(check()==S::InvalidIndexRange);subs[1].MaterialIndex=1;
    clusters[1].MaterialIndex=0;assert(check()==S::InvalidClusterRange);clusters[1].MaterialIndex=1;
    clusters[1].IndexOffset=0;assert(check()==S::InvalidClusterRange);clusters[1].IndexOffset=3;
    clusters[1].IndexCount=0;assert(check()==S::InvalidClusterRange);clusters[1].IndexCount=3;
    indices[5]=6;assert(check()==S::InvalidIndexRange);indices[5]=5;
    subs[1].VertexCount=5;assert(check()==S::InvalidIndexRange);subs[1].VertexCount=6;
    clusters[1].VertexCount=5;assert(check()==S::InvalidClusterRange);clusters[1].VertexCount=6;
    assert(ValidateCookedMeshV1Partitions({subs,1},clusters,indices,6,2)==S::InvalidClusterRange);
    assert(ValidateCookedMeshV1Partitions(subs,clusters,{nullptr,6},6,2)==S::InvalidInput);
    assert(ValidateCookedMeshV1Partitions({},clusters,indices,6,2)==S::InvalidCounts);
    uint32_t many[129];
    for (uint32_t i=0;i<129;++i)
    {
        many[i]=i;
    }
    CookedMeshWireSubmesh one{0,129,129,0,0,1};
    CookedMeshLod0Cluster big;big.IndexCount=129;big.VertexCount=129;
    assert(ValidateCookedMeshV1Partitions({&one,1},{&big,1},many,129,1)==S::InvalidClusterRange);
    for (auto& vertex : many)
    {
        vertex=128;
    }
    assert(ValidateCookedMeshV1Partitions({&one,1},{&big,1},many,129,1)==S::Success); // cap129はunique1と別。
    big.VertexCount=128;
    assert(ValidateCookedMeshV1Partitions({&one,1},{&big,1},many,129,1)==S::InvalidClusterRange);
    big.VertexCount=0;one.VertexCount=0;
    assert(ValidateCookedMeshV1Partitions({&one,1},{&big,1},many,129,1)==S::Success);
    std::cout << "CookedMeshV1LayoutTest PASS: partitions_materials_absolute_caps_unique_vertices\n";
    return 0;
}
