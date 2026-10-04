// 設定の階層解決・正規形・hashを反証する。JSON/実cookの受入れとは分ける。
#include "Resource/MaterialImportSettings.h"
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
            std::fprintf(stderr, "assertion: %s line=%d\n", #expression, __LINE__); \
            std::abort(); \
        } \
    } while (false)
using namespace NorvesLib::Core::AssetImport;
using Status = MaterialSettingsStatus;
int main()
{
    MaterialSettingsLayer asset, material;
    ResolvedMaterialSettings result;
    assert(ResolveMaterialSettings(MaterialSourceProfile::AiGenerated, asset, material, {}, result) == Status::Success);
    assert(result.Arm.Channels[0].Mode == ArmMode::Ignore && result.Arm.Channels[1].Mode == ArmMode::Auto &&
        result.Arm.Channels[2].Mode == ArmMode::Ignore && !result.Emission.Present);
    ImportedEmission emission;
    const double black[3]{};
    const double red[3]{1,0,0};
    assert(ImportEmission(black,1,result.Emission,emission) == MaterialPolicyStatus::Success);
    assert(ImportEmission(red,1,result.Emission,emission) == MaterialPolicyStatus::MissingNitsPerUnit);
    assert(ResolveMaterialSettings(MaterialSourceProfile::Source,asset,material,{},result) == Status::Success);
    assert(result.Arm.Channels[2].Mode == ArmMode::Auto);
    asset.ArmMask=2;
    asset.Arm.Channels[1].Mode=ArmMode::Constant;
    asset.Arm.Channels[1].Constant=.7;
    asset.Emission={true,200};
    asset.DoubleSided=DoubleSidedSetting::ForceTrue;
    asset.AlphaMode=AlphaModeSetting::ForceOpaque;
    material.ArmMask=4;
    material.Arm.Channels[2].Mode=ArmMode::Texture;
    material.Emission={true,400};
    material.DoubleSided=DoubleSidedSetting::FromSource;
    assert(ResolveMaterialSettings(MaterialSourceProfile::AiGenerated,asset,material,{true,100},result) == Status::Success);
    assert(result.Arm.Channels[1].Constant==.7 && result.Arm.Channels[2].Mode==ArmMode::Texture);
    assert(result.Emission.NitsPerUnit==400 && result.DoubleSided==DoubleSidedSetting::FromSource && result.AlphaMode==AlphaModeSetting::ForceOpaque);
    const auto canonical=EncodeCanonicalMaterialSettings(result);
    const uint8_t golden[] = {0x01,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x66,0x66,0x66,0x66,0x66,0x66,0xe6,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x79,0x40};
    static_assert(sizeof(golden)==CanonicalMaterialSettingsSize);
    assert(canonical.Size==sizeof(golden) && std::memcmp(golden,canonical.Bytes,sizeof(golden))==0);
    assert(AppendMaterialSettingsHash(UINT64_C(0x123456789abcdef0),result).Value==UINT64_C(0xff4637ca142e511e));
    auto altered=result;
    altered.Arm.Channels[2].Constant=.31;
    altered.Arm.Channels[2].AutoWidth=.77;
    const auto inactive=EncodeCanonicalMaterialSettings(altered);
    assert(inactive.Size==canonical.Size && std::memcmp(canonical.Bytes,inactive.Bytes,canonical.Size)==0);
    auto changed=result;
    changed.Emission.NitsPerUnit=401;
    assert(AppendMaterialSettingsHash(1,changed).Value!=AppendMaterialSettingsHash(1,result).Value);
    changed=result;changed.Profile=MaterialSourceProfile::Source;
    assert(AppendMaterialSettingsHash(1,changed).Value!=AppendMaterialSettingsHash(1,result).Value);
    changed=result;changed.Arm.Channels[1].Mode=ArmMode::Auto;
    const auto widthHash=AppendMaterialSettingsHash(1,changed).Value;
    changed.Arm.Channels[1].AutoWidth=.01;
    assert(AppendMaterialSettingsHash(1,changed).Value!=widthHash);
    changed=result;changed.Arm.Channels[1].Constant=-0.0;
    const auto zeroHash=AppendMaterialSettingsHash(1,changed).Value;
    changed.Arm.Channels[1].Constant=0.0;
    assert(AppendMaterialSettingsHash(1,changed).Value==zeroHash);
    changed=result;changed.DoubleSided=DoubleSidedSetting::Inherit;
    assert(EncodeCanonicalMaterialSettings(changed).Size==0 && !AppendMaterialSettingsHash(1,changed).Valid);
    material.Emission={};
    assert(ResolveMaterialSettings(MaterialSourceProfile::AiGenerated,asset,material,{true,100},result)==Status::Success && result.Emission.NitsPerUnit==200);
    asset.Emission={};
    assert(ResolveMaterialSettings(MaterialSourceProfile::AiGenerated,asset,material,{true,100},result)==Status::Success && result.Emission.NitsPerUnit==100);
    const auto before=EncodeCanonicalMaterialSettings(result);
    const auto held=[&]()
    {
        const auto after=EncodeCanonicalMaterialSettings(result);
        assert(after.Size==before.Size && std::memcmp(after.Bytes,before.Bytes,before.Size)==0);
    };
    material.Emission={true,400};
    asset.Emission={true,0};
    assert(ResolveMaterialSettings(MaterialSourceProfile::AiGenerated,asset,material,{true,100},result)==Status::InvalidNitsPerUnit);held();
    asset.Emission={};
    assert(ResolveMaterialSettings(MaterialSourceProfile::AiGenerated,asset,material,{true,std::numeric_limits<double>::infinity()},result)==Status::InvalidNitsPerUnit);held();
    asset.ArmMask=8;
    assert(ResolveMaterialSettings(MaterialSourceProfile::Source,asset,material,{},result)==Status::InvalidArmMask);held();
    asset.ArmMask=2;asset.Arm.Channels[1].Constant=std::numeric_limits<double>::quiet_NaN();
    assert(ResolveMaterialSettings(MaterialSourceProfile::Source,asset,material,{},result)==Status::InvalidArmPolicy);held();
    asset={};asset.DoubleSided=static_cast<DoubleSidedSetting>(9);
    assert(ResolveMaterialSettings(MaterialSourceProfile::Source,asset,material,{},result)==Status::InvalidDoubleSided);held();
    asset={};asset.AlphaMode=static_cast<AlphaModeSetting>(9);
    assert(ResolveMaterialSettings(MaterialSourceProfile::Source,asset,material,{},result)==Status::InvalidAlphaMode);held();
    asset={};
    assert(ResolveMaterialSettings(static_cast<MaterialSourceProfile>(9),asset,material,{},result)==Status::InvalidProfile);held();
    assert(std::strcmp(MaterialSettingsErrorKey(Status::InvalidNitsPerUnit),"emissiveNitsPerUnit")==0);
    std::puts("MaterialImportSettingsTest PASS: layered_policy_canonical_hash_failure_preservation");
    return 0;
}
