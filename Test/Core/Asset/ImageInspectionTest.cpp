#include "Tools/AssetCook/ImageInspection.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib::Tools::AssetCook;
int main()
{
    const uint8_t Png8[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,36,0,0,0,26,73,68,65,84,120,156,99,96,224,18,145,211,48,178,113,99,8,136,74,201,171,104,234,153,6,0,27,186,4,177,166,126,190,208,0,0,0,0,73,69,78,68,174,66,96,130};
    const uint8_t Png16[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,16,0,0,0,0,129,217,252,21,0,0,0,13,73,68,65,84,120,156,99,96,96,248,255,31,0,3,2,1,255,230,119,11,174,0,0,0,0,73,69,78,68,174,66,96,130};
    const uint8_t Huge[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,50,200,0,0,50,200,8,6,0,0,0,226,162,232,56,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};
    const uint8_t Jpeg[]={255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,255,219,0,132,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,255,192,0,17,8,0,2,0,2,3,1,17,0,2,17,1,3,17,1,255,196,1,162,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,16,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,49,65,6,19,81,97,7,34,113,20,50,129,145,161,8,35,66,177,193,21,82,209,240,36,51,98,114,130,9,10,22,23,24,25,26,37,38,39,40,41,42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,225,226,227,228,229,230,231,232,233,234,241,242,243,244,245,246,247,248,249,250,1,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,17,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119,0,1,2,3,17,4,5,33,49,6,18,65,81,7,97,113,19,34,50,129,8,20,66,145,161,177,193,9,35,51,82,240,21,98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,40,41,42,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,226,227,228,229,230,231,232,233,234,242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,2,17,3,17,0,63,0,250,42,191,218,195,252,139,63,255,217};
    ImageInspection out;
    assert(InspectImage(Png8,out)==ImageInspectionStatus::Success);
    assert(out.Width==2 && out.Height==2 && out.Channels==4 && out.BitsPerChannel==8);
    for (uint32_t channel=0;channel<4;++channel)
    {
        assert(out.Channel[channel].Minimum==channel*10 && out.Channel[channel].Maximum==120+channel*10);
        assert(out.Channel[channel].Mean==60+channel*10);
    }
    assert(InspectImage(Jpeg,out)==ImageInspectionStatus::Success);
    assert(out.Width==2 && out.Height==2 && out.Channels==3 && out.BitsPerChannel==8);
    for (uint32_t channel=0;channel<3;++channel)
    {
        const double expected=80.0*(channel+1);
        assert(out.Channel[channel].Mean>=expected-3 && out.Channel[channel].Mean<=expected+3);
    }
    assert(InspectImage(Png16,out)==ImageInspectionStatus::Success);
    assert(out.Width==2 && out.Height==1 && out.Channels==1 && out.BitsPerChannel==16);
    assert(out.Channel[0].Minimum==0 && out.Channel[0].Maximum==65535 && out.Channel[0].Mean==32767.5);
    assert(out.Channel[1].Mean==0 && out.Channel[1].Maximum==0);
    const auto retained=out;
    assert(InspectImage(Huge,out)==ImageInspectionStatus::DecodedSizeLimit);
    assert(InspectImage({Png8,size_t{20}},out)==ImageInspectionStatus::DecodeFailed);
    const uint8_t invalid[]={1,2,3};
    assert(InspectImage(invalid,out)==ImageInspectionStatus::UnsupportedFormat);
    const uint8_t badJpeg[]={255,216,255};
    assert(InspectImage(badJpeg,out)==ImageInspectionStatus::DecodeFailed);
    assert(InspectImage({},out)==ImageInspectionStatus::InvalidInput);
    assert(InspectImage({nullptr,size_t{8}},out)==ImageInspectionStatus::InvalidInput);
    assert(InspectImage({reinterpret_cast<const uint8_t*>(&out),sizeof(out)},out)==ImageInspectionStatus::InvalidInput);
    assert(out.Width==retained.Width && out.Height==retained.Height && out.Channels==retained.Channels &&
        out.BitsPerChannel==retained.BitsPerChannel && out.Channel[0].Mean==retained.Channel[0].Mean &&
        out.Channel[0].Minimum==retained.Channel[0].Minimum && out.Channel[0].Maximum==retained.Channel[0].Maximum);
    std::cout << "ImageInspectionTest PASS: real_stb_png8_png16_statistics_limits_failure_preservation\n";
    return 0;
}
