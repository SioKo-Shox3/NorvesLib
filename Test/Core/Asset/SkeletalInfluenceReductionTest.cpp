#include "Resource/SkeletalInfluenceReduction.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Skeletal;
int main()
{
    using Status=InfluenceReductionStatus;
    InfluenceReductionOptions options;
    SkinInfluence scratch[16];
    ReducedSkinInfluences out;
    const SkinInfluence five[]={{0,0.4},{1,0.3},{2,0.15},{3,0.1},{4,0.05}};
    auto result=ReduceSkinInfluences(five,5,options,scratch,out);
    assert(result.Status==Status::Success && result.KeptCount==4 && result.OriginalNonzeroCount==5);
    assert(result.bReduced && result.bWarning && result.bRenormalized && std::abs(result.DroppedWeight-0.05)<1e-12);
    for (size_t i=0;i<4;++i)
    {
        assert(out.Joints[i]==i && std::abs(out.Weights[i]-five[i].Weight/0.95)<1e-7);
    }
    const auto expected=out;
    SkinInfluence permutation[5]; std::copy(std::begin(five),std::end(five),permutation);
    // 全120順列で同じbitsを要求する。
    do
    {
        assert(ReduceSkinInfluences(permutation,5,options,scratch,out).Status==Status::Success);
        for (size_t i=0;i<4;++i) assert(out.Joints[i]==expected.Joints[i] && out.Weights[i]==expected.Weights[i]);
    }
    while (std::next_permutation(std::begin(permutation),std::end(permutation),[](auto a,auto b){return a.JointIndex<b.JointIndex;}));
    options.FailDroppedWeight=0.04;
    result=ReduceSkinInfluences(five,5,options,scratch,out);
    assert(result.Status==Status::DroppedWeightExceeded && result.bWarning && std::abs(result.DroppedWeight-0.05)<1e-12);
    for (size_t i=0;i<4;++i) assert(out.Joints[i]==expected.Joints[i] && out.Weights[i]==expected.Weights[i]);
    options={};
    const SkinInfluence duplicates[]={{3,0.25},{1,0.125},{0,0.25},{1,0.125},{2,0.25},{4,0}};
    result=ReduceSkinInfluences(duplicates,5,options,scratch,out);
    assert(result.Status==Status::Success && result.UniqueNonzeroCount==4 && result.DroppedWeight==0 && result.bReduced);
    for(size_t i=0;i<4;++i) assert(out.Joints[i]==i && out.Weights[i]==0.25f);
    const SkinInfluence ties[]={{7,0.125},{6,0.125},{5,0.125},{4,0.125},{3,0.125},{2,0.125},{1,0.125},{0,0.125}};
    options.FailDroppedWeight=0.5; options.WarnDroppedWeight=0.5;
    result=ReduceSkinInfluences(ties,8,options,scratch,out);
    assert(result.Status==Status::Success && result.DroppedWeight==0.5 && !result.bWarning);
    for(size_t i=0;i<4;++i) assert(out.Joints[i]==i && out.Weights[i]==0.25f);
    options.FailDroppedWeight=std::nextafter(0.5,0.0); options.WarnDroppedWeight=0;
    assert(ReduceSkinInfluences(ties,8,options,scratch,out).Status==Status::DroppedWeightExceeded);
    options={};
    const SkinInfluence tiny[]={{0,1.0},{1,1e-20}};
    result=ReduceSkinInfluences(tiny,2,options,scratch,out);
    assert(result.Status==Status::Success && result.KeptCount==1 && result.DroppedWeight>0 && result.DroppedWeight==1e-20);
    assert(out.Joints[0]==0 && out.Weights[0]==1 && out.Weights[1]==0 && out.Joints[1]==0);
    const auto retained=out;
    const SkinInfluence zero[]={{0,0},{1,-0.0}};
    assert(ReduceSkinInfluences(zero,2,options,scratch,out).Status==Status::InvalidWeightSum);
    const SkinInfluence negative[]={{0,0.5},{1,-0.1},{0,0.6}};
    assert(ReduceSkinInfluences(negative,2,options,scratch,out).Status==Status::InvalidWeight);
    const SkinInfluence invalidJoint[]={{2,0},{0,1}};
    assert(ReduceSkinInfluences(invalidJoint,2,options,scratch,out).Status==Status::InvalidJoint);
    const SkinInfluence nan[]={{0,std::numeric_limits<double>::quiet_NaN()}};
    assert(ReduceSkinInfluences(nan,2,options,scratch,out).Status==Status::InvalidWeight);
    const SkinInfluence inf[]={{0,std::numeric_limits<double>::infinity()}};
    assert(ReduceSkinInfluences(inf,2,options,scratch,out).Status==Status::InvalidWeight);
    const SkinInfluence badSum[]={{0,0.9}};
    assert(ReduceSkinInfluences(badSum,2,options,scratch,out).Status==Status::InvalidWeightSum);
    assert(ReduceSkinInfluences(five,5,options,{scratch,4},out).Status==Status::InsufficientWorkspace);
    assert(ReduceSkinInfluences({},5,options,scratch,out).Status==Status::InvalidInput);
    assert(ReduceSkinInfluences(five,0,options,scratch,out).Status==Status::InvalidInput);
    SkinInfluence alias[16]={{0,1}};
    assert(ReduceSkinInfluences({alias,1},1,options,alias,out).Status==Status::OverlappingStorage);
    options.MinimumWeight=0.25;
    assert(ReduceSkinInfluences(duplicates,5,options,scratch,out).Status==Status::AllWeightsRemoved);
    options={}; options.MinimumWeight=0;
    const SkinInfluence unrepresentable[]={{0,1},{1,1e-300}};
    assert(ReduceSkinInfluences(unrepresentable,2,options,scratch,out).Status==Status::Unrepresentable);
    options.FailDroppedWeight=-1;
    assert(ReduceSkinInfluences(five,5,options,scratch,out).Status==Status::InvalidOptions);
    options={}; options.WarnDroppedWeight=0.3;
    assert(ReduceSkinInfluences(five,5,options,scratch,out).Status==Status::InvalidOptions);
    options={}; options.InputWeightSumTolerance=std::numeric_limits<double>::quiet_NaN();
    assert(ReduceSkinInfluences(five,5,options,scratch,out).Status==Status::InvalidOptions);
    for(size_t i=0;i<4;++i) assert(out.Joints[i]==retained.Joints[i] && out.Weights[i]==retained.Weights[i]);
    std::cout << "SkeletalInfluenceReductionTest PASS: top4_merge_order_ties_loss_limits_atomicity\n";
    return 0;
}
