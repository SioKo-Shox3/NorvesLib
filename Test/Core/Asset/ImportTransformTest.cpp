#include "Resource/ImportTransform.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::AssetImport;
namespace
{
    struct Vertex
    {
        uint32_t Tag = 0x12345678;
        float Position[3] = {};
        float Normal[3] = {0,0,1};
        float UV[2] = {0.25f,0.75f};
    };
    constexpr ImportVertexLayout Layout{sizeof(Vertex),offsetof(Vertex,Position),offsetof(Vertex,Normal),offsetof(Vertex,UV)};
    template<size_t N>
    Container::Span<uint8_t> Bytes(Vertex (&vertices)[N])
    {
        return {reinterpret_cast<uint8_t*>(vertices),sizeof(vertices)};
    }
    bool Near(double a, double b)
    {
        return std::abs(a - b) <= 1e-6;
    }
    void Triangle(Vertex (&vertices)[3])
    {
        for (auto& vertex : vertices) { vertex = {}; }
        vertices[1].Position[0] = 2;
        vertices[2].Position[1] = 4;
    }
    void UnchangedFailure(Vertex (&vertices)[3], uint32_t (&indices)[3], const ImportSettings& settings,
                          TransformResult expected, ImportVertexLayout layout = Layout)
    {
        uint8_t oldVertices[sizeof(vertices)], oldIndices[sizeof(indices)];
        std::memcpy(oldVertices,vertices,sizeof(vertices)); std::memcpy(oldIndices,indices,sizeof(indices));
        assert(ApplyImportTransform(Bytes(vertices),3,layout,indices,settings).Result == expected);
        assert(std::memcmp(oldVertices,vertices,sizeof(vertices)) == 0);
        assert(std::memcmp(oldIndices,indices,sizeof(indices)) == 0);
    }
    double Volume(const Vertex (&vertices)[4], const uint32_t (&indices)[12])
    {
        double sum = 0;
        for (size_t i = 0; i < 12; i += 3)
        {
            const auto& a=vertices[indices[i]].Position;
            const auto& b=vertices[indices[i+1]].Position;
            const auto& c=vertices[indices[i+2]].Position;
            sum += a[0]*(b[1]*c[2]-b[2]*c[1]) + a[1]*(b[2]*c[0]-b[0]*c[2]) + a[2]*(b[0]*c[1]-b[1]*c[0]);
        }
        return sum/6.0;
    }
}
int main()
{
    Vertex vertices[3]; Triangle(vertices);
    uint32_t indices[] = {0,1,2};
    ImportSettings settings;
    auto result=ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings);
    assert(result.Result==TransformResult::Success && result.AppliedScale==1 && !result.bFlippedWinding);
    assert(vertices[1].Position[0]==2 && vertices[2].Position[1]==4);
    for(const auto& vertex:vertices) { assert(vertex.Tag==0x12345678 && vertex.Normal[2]==1); }

    // up=+Z/forward=+Xではsource(X,Y,Z)をtarget(Y,Z,X)へ写す。
    Triangle(vertices); vertices[0].Position[0]=1; vertices[0].Position[1]=2; vertices[0].Position[2]=3;
    settings.Up=SignedAxis::PositiveZ; settings.Forward=SignedAxis::PositiveX;
    assert(ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings).Result==TransformResult::Success);
    assert(vertices[0].Position[0]==2 && vertices[0].Position[1]==3 && vertices[0].Position[2]==1);
    assert(vertices[0].Normal[0]==0 && vertices[0].Normal[1]==1 && vertices[0].Normal[2]==0);

    Triangle(vertices); settings={}; settings.Fit=FitAxis::Up; settings.FitMeters=0.6; settings.Origin=OriginMode::BoundsBottomCenter;
    result=ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings);
    assert(result.Result==TransformResult::Success && Near(result.AppliedScale,0.15));
    assert(Near(vertices[0].Position[0],-0.15) && vertices[0].Position[1]==0 && Near(vertices[2].Position[1],0.6));
    assert(Near((vertices[0].Position[0]+vertices[1].Position[0])/2,0));
    Triangle(vertices); settings={}; settings.Fit=FitAxis::Longest; settings.FitMeters=6;
    result=ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings);
    assert(result.Result==TransformResult::Success && result.AppliedScale==1.5 && vertices[2].Position[1]==6);
    Triangle(vertices); vertices[2].Position[2]=8; settings={}; settings.Fit=FitAxis::Forward; settings.FitMeters=2;
    result=ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings);
    assert(result.Result==TransformResult::Success && result.AppliedScale==0.25 && vertices[2].Position[2]==2);
    Triangle(vertices); settings={}; settings.Origin=OriginMode::BoundsCenter;
    result=ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings);
    assert(result.Result==TransformResult::Success && result.Pivot[0]==1 && result.Pivot[1]==2);
    assert(vertices[0].Position[0]==-1 && vertices[0].Position[1]==-2);
    Triangle(vertices); settings={}; settings.Origin=OriginMode::SurfaceCentroid;
    result=ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings);
    assert(result.Result==TransformResult::Success && Near(result.Pivot[0],2.0/3) && Near(result.Pivot[1],4.0/3));

    Vertex weighted[6];
    weighted[1].Position[0]=2; weighted[2].Position[1]=1;
    weighted[3].Position[0]=10; weighted[4].Position[0]=14; weighted[5].Position[0]=10; weighted[5].Position[1]=2;
    uint32_t weightedIndices[]={0,1,2,3,4,5,0,0,0};
    result=ApplyImportTransform(Bytes(weighted),6,Layout,weightedIndices,settings);
    assert(result.Result==TransformResult::Success && Near(result.Pivot[0],9.2) && Near(result.Pivot[1],0.6));

    // 巨大な共通成分の相殺で非縮退面が0にならず、循環index順にも依存しない。
    for (bool symmetric : {false,true})
    {
        double referencePivot[3] = {};
        for (uint32_t rotation=0;rotation<3;++rotation)
        {
            Vertex extreme[3];
            extreme[0].Position[0]=1e20f; extreme[0].Position[1]=1e20f;
            if (symmetric)
            {
                extreme[1].Position[0]=-1e20f; extreme[1].Position[1]=-1e20f;
            }
            extreme[2].Position[0]=1; extreme[2].Position[1]=2;
            uint32_t cycle[]={rotation,(rotation+1)%3,(rotation+2)%3};
            result=ApplyImportTransform(Bytes(extreme),3,Layout,cycle,settings);
            assert(result.Result==TransformResult::Success);
            if(rotation==0) { std::memcpy(referencePivot,result.Pivot,sizeof(referencePivot)); }
            else { assert(std::memcmp(referencePivot,result.Pivot,sizeof(referencePivot))==0); }
            if(symmetric) { assert(Near(result.Pivot[0],1.0/3) && Near(result.Pivot[1],2.0/3)); }
        }
    }
    for (uint32_t rotation=0;rotation<3;++rotation)
    {
        Vertex extreme[3];
        extreme[0].Position[0]=1e20f; extreme[1].Position[0]=-1e20f;
        extreme[2].Position[0]=1; extreme[2].Position[1]=3;
        uint32_t cycle[]={rotation,(rotation+1)%3,(rotation+2)%3};
        result=ApplyImportTransform(Bytes(extreme),3,Layout,cycle,settings);
        assert(result.Result==TransformResult::Success && Near(result.Pivot[0],1.0/3));
        assert(Near(extreme[2].Position[0],2.0/3));
    }
    // 面の列挙順による累積momentの相殺も同じ結果にする。
    const uint32_t permutations[][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for(const auto& permutation:permutations)
    {
        Vertex surfaces[9];
        const float x[]={1e20f,-1e20f,1};
        uint32_t surfaceIndices[9];
        for(size_t triangle=0;triangle<3;++triangle)
        {
            for(size_t vertex=0;vertex<3;++vertex)
            {
                surfaces[triangle*3+vertex].Position[0]=x[triangle];
                surfaceIndices[triangle*3+vertex]=permutation[triangle]*3+static_cast<uint32_t>(vertex);
            }
            surfaces[triangle*3+1].Position[1]=1;
            surfaces[triangle*3+2].Position[2]=1;
        }
        result=ApplyImportTransform(Bytes(surfaces),9,Layout,surfaceIndices,settings);
        assert(result.Result==TransformResult::Success && Near(result.Pivot[0],1.0/3));
        assert(Near(surfaces[6].Position[0],2.0/3));
    }
    // 各面の中心へ丸める前の+1が、巨大な正負中心を合算した後にも残ること。
    for(bool reverse:{false,true})
    {
        Vertex extreme[6];
        const float magnitude=std::ldexp(1.0f,100), tiny=std::ldexp(1.0f,-100);
        extreme[0].Position[0]=magnitude; extreme[3].Position[0]=-magnitude;
        extreme[2].Position[0]=1; extreme[2].Position[1]=tiny;
        extreme[5].Position[0]=1; extreme[5].Position[1]=tiny;
        uint32_t faces[]={0,1,2,3,4,5};
        if(reverse) { for(size_t i=0;i<3;++i) { std::swap(faces[i],faces[i+3]); } }
        result=ApplyImportTransform(Bytes(extreme),6,Layout,faces,settings);
        assert(result.Result==TransformResult::Success && Near(result.Pivot[0],1.0/3));
        assert(Near(extreme[2].Position[0],2.0/3) && Near(extreme[5].Position[0],2.0/3));
    }
    Triangle(vertices); settings={}; settings.Scale=2; settings.Origin=OriginMode::Custom;
    settings.OriginOffset[0]=3; settings.OriginOffset[1]=-4; settings.OriginOffset[2]=5;
    settings.bFlipU=settings.bFlipV=true;
    for(auto& vertex:vertices) { vertex.Normal[2]=2; }
    assert(ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings).Result==TransformResult::Success);
    assert(vertices[1].Position[0]==7 && vertices[2].Position[1]==4 && vertices[0].Position[2]==5);
    assert(vertices[0].Normal[2]==1 && vertices[0].UV[0]==0.75f && vertices[0].UV[1]==0.25f);

    // 24の正規直交軸と鏡像の有無で、auto windingが符号付き体積を維持する。
    size_t cases=0;
    for(uint8_t up=0;up<6;++up)
    for(uint8_t forward=0;forward<6;++forward)
    {
        if(up/2==forward/2) { continue; }
        for(bool mirror:{false,true})
        {
            Vertex tetra[4];
            tetra[1].Position[0]=1; tetra[2].Position[1]=1; tetra[3].Position[2]=1;
            uint32_t faces[]={0,2,1,0,1,3,0,3,2,1,2,3};
            const double before=Volume(tetra,faces);
            settings={}; settings.Up=static_cast<SignedAxis>(up); settings.Forward=static_cast<SignedAxis>(forward);
            settings.bMirrorX=mirror; settings.Winding=WindingMode::Auto;
            result=ApplyImportTransform(Bytes(tetra),4,Layout,faces,settings);
            assert(result.Result==TransformResult::Success && result.bFlippedWinding==mirror);
            assert(Near(Volume(tetra,faces),before));
            const int signUp=up%2==0?1:-1, signForward=forward%2==0?1:-1;
            assert(tetra[up/2+1].Position[1]*signUp==1 && tetra[forward/2+1].Position[2]*signForward==1);
            for(const auto& vertex:tetra)
            {
                const double length=std::hypot(vertex.Normal[0],vertex.Normal[1],vertex.Normal[2]);
                assert(Near(length,1) && vertex.Tag==0x12345678);
                for(size_t axis=0;axis<3;++axis)
                {
                    assert(vertex.Normal[axis]==tetra[3].Position[axis]);
                }

            }
            ++cases;
        }
    }
    Triangle(vertices); settings={}; settings.bMirrorX=true; settings.Winding=WindingMode::Keep;
    assert(!ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings).bFlippedWinding);
    assert(indices[1]==1 && indices[2]==2);
    Triangle(vertices); settings={}; settings.Winding=WindingMode::Flip;
    assert(ApplyImportTransform(Bytes(vertices),3,Layout,indices,settings).bFlippedWinding);
    assert(indices[1]==2 && indices[2]==1);
    indices[1]=1; indices[2]=2;

    // 全spanと全頂点の事前検証により、終盤の失敗でも最初の頂点を変更しない。
    Triangle(vertices); settings={}; settings.Scale=-1;
    UnchangedFailure(vertices,indices,settings,TransformResult::InvalidSettings);
    settings={}; auto invalidLayout=Layout; invalidLayout.NormalOffset=invalidLayout.PositionOffset;
    UnchangedFailure(vertices,indices,settings,TransformResult::InvalidLayout,invalidLayout);
    invalidLayout=Layout; invalidLayout.Stride=0;
    UnchangedFailure(vertices,indices,settings,TransformResult::InvalidLayout,invalidLayout);
    invalidLayout=Layout; invalidLayout.PositionOffset=std::numeric_limits<size_t>::max();
    UnchangedFailure(vertices,indices,settings,TransformResult::InvalidLayout,invalidLayout);
    indices[2]=3; UnchangedFailure(vertices,indices,settings,TransformResult::InvalidIndices); indices[2]=2;
    settings.Scale=2;
    vertices[2].Position[0]=std::numeric_limits<float>::quiet_NaN();
    UnchangedFailure(vertices,indices,settings,TransformResult::InvalidVertex);
    Triangle(vertices); vertices[2].Normal[2]=0;
    UnchangedFailure(vertices,indices,settings,TransformResult::InvalidVertex);
    Triangle(vertices); vertices[2].UV[1]=std::numeric_limits<float>::infinity();
    UnchangedFailure(vertices,indices,settings,TransformResult::InvalidVertex);
    Triangle(vertices); settings={}; settings.Fit=FitAxis::Forward;
    UnchangedFailure(vertices,indices,settings,TransformResult::DegenerateFit);
    settings={}; settings.Scale=std::numeric_limits<double>::max();
    UnchangedFailure(vertices,indices,settings,TransformResult::Unrepresentable);
    settings.Scale=std::numeric_limits<double>::denorm_min();
    UnchangedFailure(vertices,indices,settings,TransformResult::Unrepresentable);
    Triangle(vertices); vertices[1].Position[0]=0.125f; vertices[2].Position[1]=0.25f;
    UnchangedFailure(vertices,indices,settings,TransformResult::Unrepresentable);
    Triangle(vertices);
    settings={}; settings.Origin=OriginMode::SurfaceCentroid; vertices[2].Position[1]=0;
    UnchangedFailure(vertices,indices,settings,TransformResult::DegenerateSurface);
    Triangle(vertices); settings={};
    assert(ApplyImportTransform({Bytes(vertices).data(),sizeof(vertices)-1},3,Layout,indices,settings).Result==TransformResult::InvalidLayout);
    assert(ApplyImportTransform(Bytes(vertices),0,Layout,indices,settings).Result==TransformResult::InvalidLayout);
    assert(ApplyImportTransform({},3,Layout,indices,settings).Result==TransformResult::InvalidLayout);
    assert(ApplyImportTransform(Bytes(vertices),std::numeric_limits<size_t>::max(),Layout,indices,settings).Result==TransformResult::InvalidLayout);
    assert(ApplyImportTransform(Bytes(vertices),3,Layout,{},settings).Result==TransformResult::InvalidIndices);
    assert(ApplyImportTransform(Bytes(vertices),3,Layout,{nullptr,3},settings).Result==TransformResult::InvalidIndices);
    assert(ApplyImportTransform(Bytes(vertices),3,Layout,{indices,2},settings).Result==TransformResult::InvalidIndices);
    assert(ApplyImportTransform(Bytes(vertices),3,Layout,{reinterpret_cast<uint32_t*>(vertices),3},settings).Result==TransformResult::InvalidIndices);

    // 非整列なbyte storageにもmemcpyでアクセスし、tagなど未指定fieldを保つ。
    uint8_t unaligned[sizeof(vertices)+1];
    std::memcpy(unaligned+1,vertices,sizeof(vertices));
    settings.Scale=2;
    assert(ApplyImportTransform({unaligned+1,sizeof(vertices)},3,Layout,indices,settings).Result==TransformResult::Success);
    std::memcpy(vertices,unaligned+1,sizeof(vertices));
    assert(vertices[1].Position[0]==4 && vertices[2].Position[1]==8 && vertices[1].Tag==0x12345678);
    std::cout << "ImportTransformTest PASS: " << cases << " oriented_meshes_fit_origin_normals_uv_failure_atomicity\n";
    return 0;
}
