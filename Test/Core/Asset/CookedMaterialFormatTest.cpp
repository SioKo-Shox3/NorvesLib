#include "Asset/CookedMaterialFormat.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>

using namespace NorvesLib::Core::Asset;
namespace
{
    void Put32(uint8_t* out, uint32_t value)
    {
        for (size_t index=0;index<4;++index)
        {
            out[index]=static_cast<uint8_t>(value>>(index*8));
        }
    }
}
int main()
{
    using Status = CookedMaterialStatus;
    using namespace CookedMaterialFormatV1;
    static_assert(RecordSize==128 && StringRefSize==16 && Offset::Reserved==124 && DefaultLit==0);
    const uint8_t golden[128] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x80,0x3e,0x00,0x00,0x00,0x3f,0x00,0x00,0x40,0x3f,0x00,0x00,0x80,0x3f,
        0x00,0x00,0x80,0x3f,0x00,0x00,0x80,0x3f,0x00,0x00,0x80,0x3f,0x00,0x00,0x00,0x40,
        0x00,0x00,0x80,0xbf,0x00,0x00,0xc0,0x3e,0xcd,0xcc,0x4c,0x3f,0x00,0x00,0x00,0xc0,
        0x00,0x00,0xa0,0x3f,0x3b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    };
    CookedMaterialRecord value;
    assert(ReadCookedMaterialRecord(golden,15,value)==Status::Success);
    assert(value.Albedo.Offset==0 && value.Albedo.Length==4 && value.Normal.Offset==4 && value.Arm.Offset==7 && value.Emissive.Length==5);
    assert(value.BaseColor[0]==.25f && value.BaseColor[2]==.75f && value.EmissiveNits==2 && value.Metallic==-1 && value.Roughness==.375f);
    assert(value.NormalScale==-2 && value.AlphaCutoff==1.25f && value.Flags==59);
    uint8_t output[132]; std::memset(output,0xa5,sizeof(output));
    assert(WriteCookedMaterialRecord(value,15,output)==Status::Success);
    assert(std::memcmp(output,golden,128)==0);
    for(size_t index=128;index<132;++index)
    {
        assert(output[index]==0xa5);
    }
    const auto rejectBytes = [&](size_t offset,uint32_t bits,Status expected)
    {
        uint8_t bad[128]; std::memcpy(bad,golden,128); Put32(bad+offset,bits);
        CookedMaterialRecord out; out.Flags=12345; out.NormalScale=17;
        assert(ReadCookedMaterialRecord(bad,15,out)==expected && out.Flags==12345 && out.NormalScale==17);
    };
    for(size_t offset : {size_t{12},size_t{28},size_t{44},size_t{60},size_t{124}})
    {
        rejectBytes(offset,1,Status::InvalidReserved);
    }
    rejectBytes(0,16,Status::InvalidReference);
    rejectBytes(8,UINT32_MAX,Status::InvalidReference);
    for(uint32_t bit=6;bit<32;++bit)
    {
        rejectBytes(116,59|(1u<<bit),Status::InvalidFlags);
    }
    rejectBytes(116,6,Status::InvalidFlags);
    rejectBytes(120,1,Status::UnsupportedShadingModel);
    for(size_t offset : {size_t{64},size_t{68},size_t{72},size_t{76},size_t{96},size_t{100},size_t{104},size_t{108},size_t{112}})
    {
        rejectBytes(offset,0x7fc00000,Status::InvalidNumeric);
        rejectBytes(offset,0x7f800000,Status::InvalidNumeric);
    }
    rejectBytes(64,0xbf800000,Status::InvalidNumeric);
    rejectBytes(76,0x40000000,Status::InvalidNumeric);
    rejectBytes(96,0x40000000,Status::InvalidNumeric);
    rejectBytes(104,0xbf800000,Status::InvalidNumeric);
    rejectBytes(112,0xbf800000,Status::InvalidNumeric);
    rejectBytes(80,0x7fc00000,Status::InvalidEmissive);
    rejectBytes(92,0xbf800000,Status::InvalidEmissive);
    rejectBytes(92,0,Status::InvalidEmissive);
    rejectBytes(92,std::bit_cast<uint32_t>(65504.0f),Status::InvalidEmissive);
    rejectBytes(80,std::bit_cast<uint32_t>(1.1f),Status::InvalidEmissive);
    const auto rejectValue = [&](CookedMaterialRecord bad,Status expected)
    {
        uint8_t out[128]; std::memset(out,0x5a,sizeof(out));
        assert(WriteCookedMaterialRecord(bad,15,out)==expected);
        for(auto byte:out)
        {
            assert(byte==0x5a);
        }
    };
    auto bad=value;bad.Flags=UINT32_MAX;rejectValue(bad,Status::InvalidFlags);
    bad=value;bad.EmissiveNits=-1;rejectValue(bad,Status::InvalidEmissive);
    bad=value;bad.Arm.Length=0;rejectValue(bad,Status::InvalidFlags);
    bad=value;bad.Albedo.Offset=UINT64_MAX;rejectValue(bad,Status::InvalidReference);
    bad=value;
    for (float& color : bad.EmissiveColor)
    {
        color=0.99995f;
    }
    bad.EmissiveNits=65506.0f;
    assert(ValidateCookedMaterialRecord(bad,15)==Status::InvalidEmissive);
    rejectValue(bad,Status::InvalidEmissive);
    uint8_t normalizedOverflow[128]; std::memcpy(normalizedOverflow,golden,128);
    for (size_t offset : {size_t{80},size_t{84},size_t{88}})
    {
        Put32(normalizedOverflow+offset,std::bit_cast<uint32_t>(0.99995f));
    }
    Put32(normalizedOverflow+92,std::bit_cast<uint32_t>(65506.0f));
    CookedMaterialRecord held; held.NormalScale=17;
    assert(ReadCookedMaterialRecord(normalizedOverflow,15,held)==Status::InvalidEmissive && held.NormalScale==17);
    bad.EmissiveNits=65503.0f;
    assert(WriteCookedMaterialRecord(bad,15,output)==Status::Success);
    assert(ReadCookedMaterialRecord({output,128},15,held)==Status::Success);
    bad=value; bad.EmissiveNits=65503.0f;
    assert(ValidateCookedMaterialRecord(bad,15)==Status::Success);
    CookedMaterialRecord neutral;
    assert(ValidateCookedMaterialRecord(neutral,0)==Status::Success);
    assert(WriteCookedMaterialRecord(neutral,0,output)==Status::Success);
    CookedMaterialRecord decoded;
    assert(ReadCookedMaterialRecord({output,128},0,decoded)==Status::Success && decoded.Metallic==-1 && decoded.Roughness==-1);
    neutral.Metallic=-100;neutral.NormalScale=-5;neutral.AlphaCutoff=3;
    assert(WriteCookedMaterialRecord(neutral,0,output)==Status::Success);
    neutral.BaseColor[0]=-0.0f;
    assert(WriteCookedMaterialRecord(neutral,0,output)==Status::Success);
    assert(ReadCookedMaterialRecord({output,128},0,decoded)==Status::Success && std::bit_cast<uint32_t>(decoded.BaseColor[0])==0x80000000u);
    neutral.Albedo={UINT64_MAX,0};
    assert(ValidateCookedMaterialRecord(neutral,UINT64_MAX)==Status::Success);
    neutral.Albedo.Length=1;
    assert(ValidateCookedMaterialRecord(neutral,UINT64_MAX)==Status::InvalidReference);
    assert(ReadCookedMaterialRecord({golden,127},15,decoded)==Status::InvalidInput);
    assert(ReadCookedMaterialRecord({nullptr,128},15,decoded)==Status::InvalidInput);
    std::memset(output,0xa5,sizeof(output));
    assert(WriteCookedMaterialRecord(value,15,{output,127})==Status::InvalidInput && output[0]==0xa5);
    assert(WriteCookedMaterialRecord(value,15,{nullptr,128})==Status::InvalidInput);
    assert(WriteCookedMaterialRecord(value,15,{output,std::numeric_limits<size_t>::max()})==Status::InvalidInput);
    alignas(CookedMaterialRecord) uint8_t storage[256]{};
    auto* alias = new(storage) CookedMaterialRecord;
    assert(WriteCookedMaterialRecord(*alias,0,storage)==Status::InvalidInput);
    assert(ReadCookedMaterialRecord({storage,128},0,*alias)==Status::InvalidInput && alias->NormalScale==1);
    std::cout << "CookedMaterialFormatTest PASS: golden128_fields_flags_refs_numeric_emissive_atomic_alias\n";
    return 0;
}
