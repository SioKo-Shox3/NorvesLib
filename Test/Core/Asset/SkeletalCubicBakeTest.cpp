#include "Resource/SkeletalCubicBake.h"
#include "Animation/SkeletalSamplingMath.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Skeletal;
namespace
{
    CubicBakeKey workspace[4096];
    CubicBakeKey output[4096];
    CubicBakeKey repeatWorkspace[4096];
    CubicBakeKey repeatOutput[4096];
    long double Hermite(double a, double z, double out, double in, long double h, long double u)
    {
        return (2*u*u*u-3*u*u+1)*a+(u*u*u-2*u*u+u)*h*out+(-2*u*u*u+3*u*u)*z+(u*u*u-u*u)*h*in;
    }
    void Verify(const CubicBakeInputKey (&input)[2], const CubicBakeOptions& options, const CubicBakeResult& result)
    {
        assert(result.Status == CubicBakeStatus::Success && result.SampleCount >= 2);
        assert(result.MaximumAcceptedErrorUpper <= options.Tolerance);
        assert(output[0].Time == input[0].Time && output[result.SampleCount-1].Time == input[1].Time);
        const bool bRotation = options.Kind == CubicBakeKind::Rotation;
        for (size_t sample = 1; sample < result.SampleCount; ++sample)
        {
            const auto& a = output[sample-1];
            const auto& z = output[sample];
            assert(z.Time > a.Time && z.Time-a.Time > NorvesLib::Math::Constants::EPSILON);
            if (bRotation)
            {
                const auto first = Animation::Detail::NormalizeQuaternion({a.Value.Values[0],a.Value.Values[1],a.Value.Values[2],a.Value.Values[3]});
                const auto last = Animation::Detail::NormalizeQuaternion({z.Value.Values[0],z.Value.Values[1],z.Value.Values[2],z.Value.Values[3]});
                const float dot=first.x*last.x+first.y*last.y+first.z*last.z+first.w*last.w;
                assert(dot>0.9995f);
            }
            for (size_t division=0;division<=64;++division)
            {
                const float time=static_cast<float>(double(a.Time)+(double(z.Time)-a.Time)*double(division)/64);
                const float alpha=Animation::Detail::ComputeLinearAlpha(a.Time,z.Time,time);
                const long double h=static_cast<long double>(input[1].Time)-input[0].Time;
                const long double u=(static_cast<long double>(time)-input[0].Time)/h;
                long double original[4]={};
                for(size_t component=0;component<(bRotation?4u:3u);++component)
                {
                    original[component]=Hermite(input[0].Value.Values[component],input[1].Value.Values[component],
                        input[0].Outgoing.Values[component],input[1].Incoming.Values[component],h,u)*options.ValueScale;
                }
                if(bRotation)
                {
                    auto value=Animation::Detail::Slerp({a.Value.Values[0],a.Value.Values[1],a.Value.Values[2],a.Value.Values[3]},
                        {z.Value.Values[0],z.Value.Values[1],z.Value.Values[2],z.Value.Values[3]},alpha);
                    // 実Sampleのconjugate後Normalizeまで同じhelperを使う。
                    value=Animation::Detail::NormalizeQuaternion({-value.x,-value.y,-value.z,value.w});
                    long double norm=0;
                    for(long double component:original)
                    {
                        norm+=component*component;
                    }
                    norm=std::sqrt(norm);
                    const long double valueNorm=std::sqrt(static_cast<long double>(value.x)*value.x+static_cast<long double>(value.y)*value.y+
                        static_cast<long double>(value.z)*value.z+static_cast<long double>(value.w)*value.w);
                    const long double dot=(-original[0]*value.x-original[1]*value.y-original[2]*value.z+original[3]*value.w)/(norm*valueNorm);
                    const long double error=2*std::acos(std::clamp(std::abs(dot),0.0L,1.0L));
                    assert(error<=result.MaximumAcceptedErrorUpper);
                }
                else
                {
                    long double squared=0;
                    for(size_t component=0;component<3;++component)
                    {
                        const float value=a.Value.Values[component]+(z.Value.Values[component]-a.Value.Values[component])*alpha;
                        squared+=(original[component]-value)*(original[component]-value);
                    }
                    assert(std::sqrt(squared)<=result.MaximumAcceptedErrorUpper);
                }
            }
        }
    }
}
int main()
{
    using Status=CubicBakeStatus;
    CubicBakeInputKey input[2];
    input[1].Time=2;
    input[1].Value.Values[0]=2;
    input[0].Outgoing.Values[0]=input[1].Incoming.Values[0]=1;
    CubicBakeOptions options;
    auto result=BakeCubicChannel(input,options,workspace,output);
    assert(result.SampleCount==2);
    Verify(input,options,result);
    // 中点相殺のS字にも複数キーが必要で、最終倍率の単位で認証する。
    input[1].Value={}; input[0].Outgoing.Values[0]=input[1].Incoming.Values[0]=6;
    options.ValueScale=10;
    result=BakeCubicChannel(input,options,workspace,output);
    assert(result.SampleCount>2); Verify(input,options,result);
    const auto repeated=BakeCubicChannel(input,options,repeatWorkspace,repeatOutput);
    assert(repeated.SampleCount==result.SampleCount && repeated.MaximumAcceptedErrorUpper==result.MaximumAcceptedErrorUpper);
    for(size_t index=0;index<result.SampleCount;++index)
    {
        assert(output[index].Time==repeatOutput[index].Time);
        for(size_t component=0;component<4;++component)
        {
            assert(output[index].Value.Values[component]==repeatOutput[index].Value.Values[component]);
        }
    }
    // midpointをfloatへ保存すると分割比が正確な0.5にならない時刻でも認証する。
    input[0].Time=0.1f; input[1].Time=1.0f; options.ValueScale=0.6;
    result=BakeCubicChannel(input,options,workspace,output);
    Verify(input,options,result);
    // binary64では時刻差の下位bitを失う指数差も、拡張精度の参照hへ保つ。
    input[0].Time=std::ldexp(1.0f,-60); input[1].Time=1; options.ValueScale=1;
    if constexpr (std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits)
    {
        const long double h=static_cast<long double>(input[1].Time)-input[0].Time;
        assert(h!=static_cast<long double>(static_cast<double>(h)));
    }
    result=BakeCubicChannel(input,options,workspace,output);
    Verify(input,options,result);
    // 角度が大きくても全leafを実NLERP枝へ細分する。
    input[0]={}; input[1]={}; input[1].Time=1;
    input[0].Value.Values[3]=1;
    input[1].Value.Values[2]=input[1].Value.Values[3]=std::sqrt(0.5f);
    for(size_t component=0;component<4;++component)
    {
        input[0].Outgoing.Values[component]=input[1].Incoming.Values[component]=input[1].Value.Values[component]-input[0].Value.Values[component];
    }
    options={}; options.Kind=CubicBakeKind::Rotation;
    result=BakeCubicChannel(input,options,workspace,output);
    assert(result.SampleCount>2); Verify(input,options,result);
    CubicBakeKey saved[4096];
    std::copy(std::begin(output),std::end(output),std::begin(saved));
    const auto unchanged=[&]()
    {
        for(size_t index=0;index<4096;++index)
        {
            assert(output[index].Time==saved[index].Time);
            for(size_t component=0;component<4;++component)
            {
                assert(output[index].Value.Values[component]==saved[index].Value.Values[component]);
            }
        }
    };
    auto limited=options; limited.MaximumSamples=2;
    assert(BakeCubicChannel(input,limited,workspace,output).Status==Status::SampleLimitExceeded); unchanged();
    limited=options; limited.MaximumDepth=0;
    assert(BakeCubicChannel(input,limited,workspace,output).Status==Status::DepthExceeded); unchanged();
    assert(BakeCubicChannel(input,options,{workspace,2},output).Status==Status::InsufficientStorage); unchanged();
    assert(BakeCubicChannel(input,options,workspace,{output,1}).Status==Status::InsufficientStorage); unchanged();
    assert(BakeCubicChannel(input,options,output,output).Status==Status::OverlappingStorage); unchanged();
    assert(BakeCubicChannel(input,options,{output+1,100},{output,100}).Status==Status::OverlappingStorage); unchanged();
    assert(BakeCubicChannel(input,options,{reinterpret_cast<CubicBakeKey*>(input),2},output).Status==Status::OverlappingStorage); unchanged();
    assert(BakeCubicChannel(input,options,workspace,{reinterpret_cast<CubicBakeKey*>(&options),2}).Status==Status::OverlappingStorage); unchanged();
    assert(BakeCubicChannel(input,options,workspace,{output,SIZE_MAX}).Status==Status::InvalidInput); unchanged();
    limited=options; limited.Tolerance=1e-8;
    assert(BakeCubicChannel(input,limited,workspace,output).Status==Status::NumericBudgetExceeded); unchanged();
    limited=options; limited.ValueScale=2;
    assert(BakeCubicChannel(input,limited,workspace,output).Status==Status::InvalidOptions); unchanged();
    input[0].Incoming.Values[2]=std::numeric_limits<float>::infinity();
    assert(BakeCubicChannel(input,options,workspace,output).Status==Status::InvalidInput); unchanged();
    input[0].Incoming={}; input[1].Time=0;
    assert(BakeCubicChannel(input,options,workspace,output).Status==Status::InvalidInput); unchanged();
    input[1].Time=std::numeric_limits<float>::epsilon();
    assert(BakeCubicChannel(input,options,workspace,output).Status==Status::ShortInterval); unchanged();
    // 同一向きの両端でも中央zeroは拒否し、prefixを公開しない。
    input[0]={}; input[1]={}; input[1].Time=1;
    input[0].Value.Values[3]=input[1].Value.Values[3]=1;
    input[0].Outgoing.Values[3]=-4; input[1].Incoming.Values[3]=4;
    const auto zero=BakeCubicChannel(input,options,workspace,output);
    assert(zero.Status!=Status::Success && zero.SampleCount==0); unchanged();
    options={}; input[0]={}; input[1]={};
    input[0].Time=1024; input[1].Time=std::nextafter(input[0].Time,std::numeric_limits<float>::infinity());
    input[0].Outgoing.Values[0]=input[1].Incoming.Values[0]=1e6f;
    assert(BakeCubicChannel(input,options,workspace,output).Status==Status::TimeCollision); unchanged();
    input[0]={}; input[1]={}; input[1].Time=1;
    const int rounding=std::fegetround();
    assert(std::fesetround(FE_DOWNWARD)==0);
    assert(BakeCubicChannel(input,options,workspace,output).Status==Status::UnsupportedArithmetic);
    assert(std::fesetround(rounding)==0); unchanged();
    // 元キーを跨ぐ所有列も厳密増加し、全元キー時刻を残す。
    CubicBakeInputKey multiple[3]; multiple[1].Time=1; multiple[2].Time=2;
    multiple[1].Value.Values[0]=1; multiple[2].Value.Values[0]=2;
    auto multi=BakeCubicChannel(multiple,options,workspace,output);
    assert(multi.Status==Status::Success);
    bool bFoundMiddle=false;
    for(size_t index=0;index<multi.SampleCount;++index)
    {
        bFoundMiddle=bFoundMiddle || output[index].Time==1;
        assert(index==0 || output[index].Time>output[index-1].Time);
    }
    assert(bFoundMiddle && output[multi.SampleCount-1].Time==2);
    std::cout<<"SkeletalCubicBakeTest PASS: adaptive_vector_rotation_shared_sampler_numeric_bounds_limits_atomicity\n";
    return 0;
}
