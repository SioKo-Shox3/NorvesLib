#include "Resource/MaterialImportPolicy.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::AssetImport;
using Status=MaterialPolicyStatus;
namespace
{
    bool Near(double a,double b,double tolerance=1e-12)
    {
        return std::abs(a-b)<=tolerance;
    }
    void ArmPolicy()
    {
        const auto ai=DefaultArmImportPolicy(true);
        const auto authored=DefaultArmImportPolicy(false);
        assert(ai.Channels[0].Mode==ArmMode::Ignore && ai.Channels[1].Mode==ArmMode::Auto && ai.Channels[2].Mode==ArmMode::Ignore);
        assert(authored.Channels[2].Mode==ArmMode::Auto);
        uint64_t trellis[256]{}; trellis[192]=200; trellis[237]=9600; trellis[255]=200;
        ArmChannelDecision out;
        assert(AnalyzeArmHistogram(trellis,ArmChannel::Metallic,ai.Channels[2],1,out)==Status::Success && !out.UseTexture && out.Scalar==0);
        assert(out.Mean>.9 && out.Mean<.95);
        assert(AnalyzeArmHistogram(trellis,ArmChannel::Metallic,authored.Channels[2],1,out)==Status::Success && out.UseTexture);
        auto texture=ai.Channels[2]; texture.Mode=ArmMode::Texture;
        assert(AnalyzeArmHistogram(trellis,ArmChannel::Metallic,texture,1,out)==Status::Success && out.UseTexture);
        uint64_t pixal[256]{}; pixal[249]=1; pixal[254]=2499; pixal[255]=7500;
        assert(AnalyzeArmHistogram(pixal,ArmChannel::Roughness,ai.Channels[1],1,out)==Status::Success && !out.UseTexture);
        assert(out.Minimum==249 && out.Maximum==255 && out.Percentile1==254 && out.Percentile99==255 && out.Scalar>.99);
        assert(Near(out.Mean,(249.0+254*2499+255*7500)/(10000*255.0)));
        uint64_t two[256]{}; two[0]=1; two[255]=1;
        assert(AnalyzeArmHistogram(two,ArmChannel::Roughness,ai.Channels[1],1,out)==Status::Success);
        assert(Near(out.Percentile1,2.55) && Near(out.Percentile99,252.45) && Near(out.EffectivePercentileWidth,.98) && out.UseTexture);
        assert(AnalyzeArmHistogram(two,ArmChannel::Occlusion,ai.Channels[0],.5,out)==Status::Success && !out.UseTexture && out.Scalar==1);
        auto autoAO=ai.Channels[0]; autoAO.Mode=ArmMode::Auto; autoAO.AutoWidth=1;
        assert(AnalyzeArmHistogram(two,ArmChannel::Occlusion,autoAO,.5,out)==Status::Success && Near(out.Scalar,.75));
        auto constant=ai.Channels[1]; constant.Mode=ArmMode::Constant; constant.Constant=.8;
        assert(AnalyzeArmHistogram(two,ArmChannel::Roughness,constant,.2,out)==Status::Success && out.Scalar==.8 && !out.UseTexture);
        assert(AnalyzeArmHistogram(two,ArmChannel::Metallic,authored.Channels[2],0,out)==Status::Success && !out.UseTexture && out.Scalar==0);
        uint64_t edge[256]{}; edge[0]=500; edge[4]=500;
        assert(AnalyzeArmHistogram(edge,ArmChannel::Roughness,ai.Channels[1],1,out)==Status::Success && !out.UseTexture);
        edge[4]=0; edge[5]=500;
        assert(AnalyzeArmHistogram(edge,ArmChannel::Roughness,ai.Channels[1],1,out)==Status::Success && out.UseTexture);
        uint64_t huge[256]{}; huge[200]=UINT64_MAX;
        assert(AnalyzeArmHistogram(huge,ArmChannel::Roughness,ai.Channels[1],1,out)==Status::Success && out.SampleCount==UINT64_MAX && out.Percentile1==200 && out.Percentile99==200);
        assert(Near(out.Mean,200.0/255));
        huge[255]=1; out.SampleCount=123;
        assert(AnalyzeArmHistogram(huge,ArmChannel::Roughness,ai.Channels[1],1,out)==Status::HistogramOverflow && out.SampleCount==123);
        // 独立のsorted列から百分位を求め、histogramの順位境界を多数の長さで照合する。
        uint32_t seed=1234567;
        for (size_t count=1;count<=300;++count)
        {
            uint8_t sorted[300]{};
            uint64_t bins[256]{};
            for (size_t index=0;index<count;++index)
            {
                seed=seed*1664525u+1013904223u;
                sorted[index]=static_cast<uint8_t>(seed>>24);
                ++bins[sorted[index]];
            }
            std::sort(sorted,sorted+count);
            const auto expected = [&](double p)
            {
                const double position=(count-1)*p;
                const auto lower=static_cast<size_t>(std::floor(position));
                const size_t upper=std::min(lower+1,count-1);
                return sorted[lower]+(sorted[upper]-sorted[lower])*(position-lower);
            };
            assert(AnalyzeArmHistogram(bins,ArmChannel::Roughness,ai.Channels[1],1,out)==Status::Success);
            assert(Near(out.Percentile1,expected(.01),1e-10) && Near(out.Percentile99,expected(.99),1e-10));
        }
        out.SampleCount=123;
        uint64_t empty[256]{};
        assert(AnalyzeArmHistogram(empty,ArmChannel::Roughness,ai.Channels[1],1,out)==Status::EmptyHistogram && out.SampleCount==123);
        assert(AnalyzeArmHistogram(two,ArmChannel::Roughness,ai.Channels[1],std::numeric_limits<double>::quiet_NaN(),out)==Status::InvalidInput && out.SampleCount==123);
        for (ArmChannel channel : {ArmChannel::Occlusion,ArmChannel::Roughness,ArmChannel::Metallic})
        {
            for (uint32_t value=0;value<256;++value)
            {
                uint8_t baked=0;
                assert(BakeArmByte(static_cast<uint8_t>(value),channel,1,baked)==Status::Success && baked==value);
            }
        }
        uint8_t byte=99;
        assert(BakeArmByte(0,ArmChannel::Occlusion,.5,byte)==Status::Success && byte==128);
        assert(BakeArmByte(255,ArmChannel::Occlusion,.5,byte)==Status::Success && byte==255);
        assert(BakeArmByte(255,ArmChannel::Metallic,.5,byte)==Status::Success && byte==128);
        assert(BakeArmByte(0,ArmChannel::Metallic,-1,byte)==Status::InvalidInput && byte==128);
    }
    void EmissionPolicy()
    {
        const double black[3]{};
        const double red[]{1,0,0};
        const double gray[]{.5,.5,.5};
        ImportedEmission out; out.Nits=123;
        assert(ImportEmission(black,1,{},out)==Status::Success && !out.Emitting && out.Nits==0); // textureだけもfactor既定0。
        assert(ImportEmission(red,0,{},out)==Status::Success && !out.Emitting);
        out.Nits=123;
        assert(ImportEmission(red,1,{},out)==Status::MissingNitsPerUnit && out.Nits==123);
        assert(ImportEmission(red,2,{true,100},out)==Status::Success && out.Emitting);
        assert(Near(out.Nits,42.52,1e-5) && Near(out.Color[0]*out.Nits,200,1e-4) && out.Color[1]==0 && out.Color[2]==0);
        assert(ImportEmission(gray,4,{true,100},out)==Status::Success && Near(out.Nits,200,1e-5));
        assert(Near(out.Color[0],1,1e-6) && Near(out.Color[1],1,1e-6) && Near(out.Color[2],1,1e-6));
        auto set=SelectEmissiveScale({}, {}, {true,100}); assert(set.Present && set.NitsPerUnit==100);
        auto asset=SelectEmissiveScale({}, {true,200}, {true,100}); assert(asset.NitsPerUnit==200);
        auto material=SelectEmissiveScale({true,300}, {true,200}, {true,100}); assert(material.NitsPerUnit==300);
        auto invalid=SelectEmissiveScale({true,-1}, {true,200}, {true,100});
        out.Nits=123;
        assert(ImportEmission(red,1,invalid,out)==Status::InvalidNitsPerUnit && out.Nits==123);
        assert(ImportEmission(red,1,{true,0},out)==Status::InvalidNitsPerUnit && out.Nits==123);
        const double white[]{1,1,1};
        assert(ImportEmission(white,1,{true,65503},out)==Status::Success);
        out.Nits=123;
        assert(ImportEmission(white,1,{true,65504},out)==Status::EmissiveOutOfRange && out.Nits==123);
        assert(ImportEmission(red,-1,{true,100},out)==Status::InvalidInput && out.Nits==123);
        assert(ImportEmission(red,std::numeric_limits<double>::infinity(),{true,100},out)==Status::InvalidInput);
        const double bad[]{-1,0,0};
        assert(ImportEmission(bad,1,{true,100},out)==Status::InvalidInput);
        const double recoverable[]{1e-200,0,0};
        assert(ImportEmission(recoverable,1e-130,{true,1e308},out)==Status::Success && out.Emitting);
        assert(std::abs(double(out.Nits)/2.126e-23-1)<1e-6 && Near(out.Color[0],1/.2126,1e-6));
        const double tiny[]{std::numeric_limits<double>::denorm_min(),0,0};
        assert(ImportEmission(tiny,std::numeric_limits<double>::denorm_min(),{},out)==Status::MissingNitsPerUnit);
        assert(ImportEmission(tiny,std::numeric_limits<double>::denorm_min(),{true,1},out)==Status::EmissiveOutOfRange);
        assert(ImportEmission(tiny,1,{true,1e308},out)==Status::Success && out.Emitting);
        const long double expected=static_cast<long double>(std::numeric_limits<double>::denorm_min())*1e308L*.2126L;
        assert(std::abs(static_cast<long double>(out.Nits)/expected-1)<1e-6L && Near(out.Color[0],1/.2126,1e-6));
        assert(ImportEmission(white,std::numeric_limits<double>::max(),{true,std::numeric_limits<double>::max()},out)==Status::EmissiveOutOfRange);

    }
}
int main()
{
    ArmPolicy(); EmissionPolicy();
    std::cout << "MaterialImportPolicyTest PASS: ai_defaults_percentile_histogram_factors_emission_scale_required\n";
    return 0;
}
