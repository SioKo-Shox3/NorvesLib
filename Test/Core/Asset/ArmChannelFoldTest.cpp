// ARMのpixel判定・焼込・不正/alias時の出力保持を検証する。
#include "Tools/AssetCook/MaterialImport.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#undef assert
#define assert(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            std::fprintf(stderr,"assertion: %s line=%d\n",#expression,__LINE__); \
            std::abort(); \
        } \
    } while (false)
using namespace NorvesLib::Tools::AssetCook;
using namespace NorvesLib::Core::AssetImport;
namespace
{
    ArmImportPolicy AllTexture()
    {
        auto p=DefaultArmImportPolicy(false);
        for (auto& c:p.Channels)
        {
            c.Mode=ArmMode::Texture;
        }
        return p;
    }
    void DefaultsAndPixels()
    {
        ArmImagePlan plan;
        const double factors[]={.4,.5,.25};
        ArmImageInputs absent;
        assert(AnalyzeArmImages(absent,AllTexture(),factors,plan)==ArmImageStatus::Success);
        assert(plan.TextureMask==0 && plan.ByteCount==0 && plan.Width==0 && plan.Height==0);
        assert(!plan.HasSourceImage[0] && !plan.HasSourceImage[1] && !plan.HasSourceImage[2]);
        assert(plan.Channels[0].Scalar==1 && plan.Channels[1].Scalar==.5 && plan.Channels[2].Scalar==.25);
        uint8_t untouched[16];std::memset(untouched,0xcd,sizeof(untouched));
        assert(BakeArmImages(absent,AllTexture(),factors,{untouched,sizeof(untouched)},plan)==ArmImageStatus::Success);
        for (auto b:untouched)
        {
            assert(b==0xcd);
        }
        uint8_t source[]={0,64,255,3, 255,128,64,17, 128,255,0,128, 64,0,128,250};
        ArmImageInputs images;
        images.Occlusion={true,2,2,{source,sizeof(source)}};
        images.MetallicRoughness=images.Occlusion;
        const double identity[]={1,1,1};
        uint8_t output[24];std::memset(output,0xcd,sizeof(output));
        assert(BakeArmImages(images,AllTexture(),identity,{output,sizeof(output)},plan)==ArmImageStatus::Success);
        assert(plan.TextureMask==7 && plan.Width==2 && plan.Height==2 && plan.ByteCount==16);
        assert(std::memcmp(source,output,sizeof(source))==0);
        for (size_t i=16;i<sizeof(output);++i)
        {
            assert(output[i]==0xcd);
        }
        const double scaled[]={.5,.25,.75};
        const uint8_t expected[]={128,16,191,3, 255,32,48,17, 192,64,0,128, 160,0,96,250};
        assert(BakeArmImages(images,AllTexture(),scaled,{output,sizeof(output)},plan)==ArmImageStatus::Success);
        assert(std::memcmp(expected,output,sizeof(expected))==0);
        auto ai=DefaultArmImportPolicy(true);
        assert(BakeArmImages(images,ai,identity,{output,sizeof(output)},plan)==ArmImageStatus::Success);
        assert(plan.TextureMask==2);
        for (size_t i=0;i<4;++i)
        {
            assert(output[i*4]==255 && output[i*4+1]==source[i*4+1] && output[i*4+2]==0 && output[i*4+3]==source[i*4+3]);
        }
        // 有効なtexture同士だけ寸法一致を要求する。
        images.MetallicRoughness={true,1,2,{source,8}};
        assert(AnalyzeArmImages(images,AllTexture(),identity,plan)==ArmImageStatus::IncompatibleDimensions);
        assert(AnalyzeArmImages(images,ai,identity,plan)==ArmImageStatus::Success && plan.Width==1 && plan.Height==2);
    }
    void SyntheticDog()
    {
        uint8_t source[40000];
        for (size_t i=0;i<10000;++i)
        {
            source[i*4]=static_cast<uint8_t>(i);
            source[i*4+1]=i==0 ? 249 : i<2500 ? 254 : 255;
            source[i*4+2]=i==0 ? 192 : i==1 ? 255 : static_cast<uint8_t>(232+i%16);
            source[i*4+3]=static_cast<uint8_t>(i);
        }
        ArmImageInputs images;
        images.MetallicRoughness={true,100,100,{source,sizeof(source)}};
        auto policy=DefaultArmImportPolicy(true);
        const double factors[]={1,1,1};
        ArmImagePlan plan;
        assert(BakeArmImages(images,policy,factors,{},plan)==ArmImageStatus::Success);
        assert(plan.TextureMask==0 && plan.ByteCount==0 && plan.Channels[2].Scalar==0);
        assert(plan.HasSourceImage[1] && plan.Channels[1].SampleCount==10000 && plan.Channels[1].Minimum==249);
        assert(plan.Channels[1].Percentile1==254 && plan.Channels[1].Percentile99==255);
        assert(plan.Channels[2].Mean>.9 && plan.Channels[2].Mean<.95 && plan.Channels[2].Maximum==255);
        policy.Channels[2].Mode=ArmMode::Texture;
        uint8_t output[40000];
        assert(BakeArmImages(images,policy,factors,{output,sizeof(output)},plan)==ArmImageStatus::Success);
        assert(plan.TextureMask==4 && plan.ByteCount==sizeof(output));
        for (size_t i=0;i<10000;++i)
        {
            assert(output[i*4]==255 && output[i*4+1]==255 && output[i*4+2]==source[i*4+2] && output[i*4+3]==source[i*4+3]);
        }
    }
    void QuantizationBoundaries()
    {
        for (uint32_t numerator=1;numerator<=3;++numerator)
        {
            const double factors[]={numerator/4.0,numerator/4.0,numerator/4.0};
            for (uint32_t value=0;value<256;++value)
            {
                uint8_t source[]={static_cast<uint8_t>(value),static_cast<uint8_t>(value),static_cast<uint8_t>(value),37};
                ArmImageInputs images;
                images.Occlusion={true,1,1,{source,4}};
                images.MetallicRoughness=images.Occlusion;
                // 独立した整数式によるnearest、半端は上へ。
                const uint8_t expected[]={static_cast<uint8_t>((255*(4-numerator)+numerator*value+2)/4),
                    static_cast<uint8_t>((numerator*value+2)/4),static_cast<uint8_t>((numerator*value+2)/4)};
                for (uint8_t mask=0;mask<8;++mask)
                {
                    auto policy=AllTexture();
                    for (size_t c=0;c<3;++c)
                    {
                        policy.Channels[c].Mode=(mask&(1u<<c))!=0 ? ArmMode::Texture : ArmMode::Auto;
                    }
                    ArmImagePlan plan;
                    uint8_t output[]={0xcd,0xcd,0xcd,0xcd};
                    assert(BakeArmImages(images,policy,factors,{output,4},plan)==ArmImageStatus::Success);
                    assert(plan.TextureMask==mask);
                    for (size_t c=0;c<3;++c)
                    {
                        assert(plan.Channels[c].QuantizedScalar==expected[c]);
                        assert(output[c]==(mask!=0 ? expected[c] : 0xcd));
                    }
                    assert(output[3]==(mask!=0 ? 37 : 0xcd));
                }
            }
        }
        uint8_t ao[]={10,0,0,7};
        uint8_t mr[]={0,22,22,41};
        ArmImageInputs images{{true,1,1,{ao,4}},{true,1,1,{mr,4}}};
        auto policy=AllTexture();policy.Channels[1].Mode=policy.Channels[2].Mode=ArmMode::Auto;
        const double factors[]={.5,.75,.75};
        uint8_t output[12]{};
        ArmImagePlan plan;
        assert(BakeArmImages(images,policy,factors,{output,sizeof(output)},plan)==ArmImageStatus::Success);
        const uint8_t expected[]={133,17,17,7};
        assert(plan.TextureMask==1 && std::memcmp(output,expected,4)==0);
        // 非単色の平均も、正規化を経由せず整数和から量子化する。
        uint8_t mixed[]={10,21,21,7, 10,23,23,9};
        images={{true,2,1,{mixed,8}},{true,2,1,{mixed,8}}};
        assert(BakeArmImages(images,policy,factors,{output,sizeof(output)},plan)==ArmImageStatus::Success);
        assert(output[0]==133 && output[1]==17 && output[2]==17 && output[4]==133 && output[5]==17 && output[6]==17);
        uint8_t thirds[]={3,22,22,7, 4,22,22,9, 4,22,22,11};
        images={{true,3,1,{thirds,12}},{true,3,1,{thirds,12}}};
        policy.Channels[0].Mode=ArmMode::Auto;policy.Channels[1].Mode=ArmMode::Texture;
        const double quarters[]={.75,.75,.75};
        assert(BakeArmImages(images,policy,quarters,{output,sizeof(output)},plan)==ArmImageStatus::Success);
        assert(plan.TextureMask==2 && output[0]==67 && output[4]==67 && output[8]==67);
    }
    void Failures()
    {
        uint8_t source[16]{};
        uint8_t output[24];std::memset(output,0xcd,sizeof(output));
        ArmImageInputs images;
        images.Occlusion={true,2,2,{source,16}};
        images.MetallicRoughness=images.Occlusion;
        auto policy=AllTexture();
        double factors[]={1,1,1};
        ArmImagePlan plan;plan.ByteCount=12345;plan.Width=777;
        uint8_t before[sizeof(plan)];std::memcpy(before,&plan,sizeof(plan));
        const auto held=[&]()
        {
            assert(std::memcmp(before,&plan,sizeof(plan))==0);
            for (auto b:output)
            {
                assert(b==0xcd);
            }
        };
        assert(BakeArmImages(images,policy,factors,{output,15},plan)==ArmImageStatus::InvalidOutput);held();
        assert(BakeArmImages(images,policy,factors,{nullptr,1},plan)==ArmImageStatus::InvalidOutput);held();
        factors[1]=std::numeric_limits<double>::quiet_NaN();
        assert(BakeArmImages(images,policy,factors,{output,sizeof(output)},plan)==ArmImageStatus::InvalidPolicy);held();
        factors[1]=1;policy.Channels[2].Mode=static_cast<ArmMode>(9);
        assert(BakeArmImages(images,policy,factors,{output,sizeof(output)},plan)==ArmImageStatus::InvalidPolicy);held();
        policy=AllTexture();
        images.MetallicRoughness={true,1,2,{source,8}};
        assert(BakeArmImages(images,policy,factors,{output,sizeof(output)},plan)==ArmImageStatus::IncompatibleDimensions);held();
        images.MetallicRoughness=images.Occlusion;
        images.Occlusion.Width=UINT32_MAX;images.Occlusion.Height=UINT32_MAX;
        assert(AnalyzeArmImages(images,policy,factors,plan)==ArmImageStatus::InvalidImage);held();
        images.Occlusion={false,2,2,{source,16}};
        assert(AnalyzeArmImages(images,policy,factors,plan)==ArmImageStatus::InvalidImage);held();
        images.Occlusion={true,2,2,{nullptr,16}};
        assert(AnalyzeArmImages(images,policy,factors,plan)==ArmImageStatus::InvalidImage);held();
        images.Occlusion={true,2,2,{source,15}};
        assert(AnalyzeArmImages(images,policy,factors,plan)==ArmImageStatus::InvalidImage);held();
        images.Occlusion=images.MetallicRoughness;
        assert(BakeArmImages(images,policy,factors,{source,16},plan)==ArmImageStatus::OverlappingStorage);held();
        assert(BakeArmImages(images,policy,factors,{reinterpret_cast<uint8_t*>(factors),16},plan)==ArmImageStatus::OverlappingStorage);held();
        assert(BakeArmImages(images,policy,factors,{reinterpret_cast<uint8_t*>(&policy),16},plan)==ArmImageStatus::OverlappingStorage);held();
        assert(BakeArmImages(images,policy,factors,{reinterpret_cast<uint8_t*>(&images),16},plan)==ArmImageStatus::OverlappingStorage);held();
        assert(BakeArmImages(images,policy,factors,{reinterpret_cast<uint8_t*>(&plan),16},plan)==ArmImageStatus::OverlappingStorage);held();
        static_assert(sizeof(plan)%4==0);
        images.Occlusion={true,1,static_cast<uint32_t>(sizeof(plan)/4),{reinterpret_cast<const uint8_t*>(&plan),sizeof(plan)}};
        assert(AnalyzeArmImages(images,policy,factors,plan)==ArmImageStatus::OverlappingStorage);held();
        assert(BakeArmImages(images,policy,factors,{output,sizeof(output)},plan)==ArmImageStatus::OverlappingStorage);held();
    }
}
int main()
{
    DefaultsAndPixels();SyntheticDog();QuantizationBoundaries();Failures();
    std::puts("ArmChannelFoldTest PASS: image_histogram_fold_bake_defaults_dimensions_atomic_alias");
    return 0;
}
