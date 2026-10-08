#include "Tools/AssetCook/GeometryInspection.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
using namespace NorvesLib::Tools::AssetCook;
int main()
{
    InspectionVertex vertices[]={
        {{0,0,0},{0,0,1}},{{1,0,0},{0,0,1}},{{0,1,0},{0,0,1}},
        {{-0.0f,0,0},{0,0,0}},{{0,1,0},{0,0,1}},{{0,0,2},{0,0,1}},
        {{10,0,0},{0,0,1}},{{11,0,0},{0,0,1}},{{10,1,0},{0,0,1}},
        {{30,0,0},{0,0,0}}};
    const uint32_t indices[]={0,1,2,3,4,5,6,7,8};
    uint32_t order[10], representatives[10],parents[10];
    GeometryInspection out;
    assert(InspectGeometry(vertices,indices,order,representatives,parents,out));
    assert(out.VertexCount==10 && out.TriangleCount==3 && out.WeldedVertexCount==8);
    assert(out.ConnectedComponentCount==3 && out.ZeroNormalCount==2);
    assert(out.Minimum[0]==0 && out.Maximum[0]==30 && out.Length[0]==30 && out.LongestAxis==0);
    assert(out.Length[1]==1 && out.Length[2]==2);
    assert(vertices[3].Position[0]==0 && std::signbit(vertices[3].Position[0]));
    const auto expected=out;
    const uint32_t reversed[]={8,7,6,5,4,3,2,1,0};
    assert(InspectGeometry(vertices,reversed,order,representatives,parents,out));
    assert(out.WeldedVertexCount==expected.WeldedVertexCount && out.ConnectedComponentCount==expected.ConnectedComponentCount);
    const uint32_t degenerate[]={0,3,0};
    assert(InspectGeometry(vertices,degenerate,order,representatives,parents,out));
    assert(out.ConnectedComponentCount==8);
    // 非ゼロの微小法線をゼロ扱いせず、近接した別位置を勝手に溶接しない。
    vertices[3].Position[0]=std::numeric_limits<float>::denorm_min();
    vertices[3].Normal[0]=std::numeric_limits<float>::denorm_min();
    assert(InspectGeometry(vertices,indices,order,representatives,parents,out));
    assert(out.WeldedVertexCount==9 && out.ZeroNormalCount==1 && out.ConnectedComponentCount==3);
    const auto retained=out;
    uint32_t invalid[]={0,1,10};
    assert(!InspectGeometry(vertices,invalid,order,representatives,parents,out));
    assert(out.VertexCount==retained.VertexCount && out.WeldedVertexCount==retained.WeldedVertexCount);
    assert(!InspectGeometry(vertices,indices,{order,9},representatives,parents,out));
    assert(!InspectGeometry(vertices,indices,order,order,parents,out));
    assert(!InspectGeometry(vertices,{nullptr,size_t{3}},order,representatives,parents,out));
    assert(!InspectGeometry(vertices,{indices,size_t{2}},order,representatives,parents,out));
    assert(!InspectGeometry({},indices,order,representatives,parents,out));
    uint32_t shared[30]={};
    // 使用するprefixだけが別でも、渡したSpan全体が重複するなら拒否する。
    assert(!InspectGeometry(vertices,indices,{shared,30},{shared+10,20},{shared+20,10},out));
    uint32_t largerOrder[11],largerRepresentatives[12],largerParents[13];
    assert(InspectGeometry(vertices,indices,largerOrder,largerRepresentatives,largerParents,out));
    assert(out.WeldedVertexCount==retained.WeldedVertexCount && out.ConnectedComponentCount==retained.ConnectedComponentCount);
    vertices[0].Position[0]=std::numeric_limits<float>::infinity();
    assert(!InspectGeometry(vertices,indices,order,representatives,parents,out));
    vertices[0].Position[0]=0;
    vertices[0].Normal[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!InspectGeometry(vertices,indices,order,representatives,parents,out));
    // floatの両極端をdoubleで引き、軸長をoverflowさせない。
    vertices[0].Normal[0]=0;
    vertices[0].Position[0]=-std::numeric_limits<float>::max();
    vertices[1].Position[0]=std::numeric_limits<float>::max();
    assert(InspectGeometry(vertices,indices,order,representatives,parents,out));
    assert(std::isfinite(out.Length[0]) && out.Length[0]>std::numeric_limits<float>::max());
    std::cout << "GeometryInspectionTest PASS: bounds_exact_weld_components_normals_invalid_input\n";
    return 0;
}
