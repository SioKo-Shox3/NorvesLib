// 実JsonDocumentと材質設定parserの接続契約。Windows/Coreの実行が必要。
#include "Resource/MaterialImportSettingsJson.h"
#include "Text/JsonDocument.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
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
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::AssetImport;
namespace
{
    SettingsResult Parse(const char* bytes, MaterialSourceProfile& profile, MaterialSettingsLayer& layer, bool bAsset=true)
    {
        Container::String text;
        for (; *bytes != 0; ++bytes)
        {
            text.push_back(static_cast<Container::String::value_type>(static_cast<uint8_t>(*bytes)));
        }
        JsonDocument document;
        assert(JsonDocument::TryParse(text,document));
        return bAsset ? ParseAssetMaterialSettings(document.GetRoot(),profile,layer) :
            ParseMaterialSettingsLayer(document.GetRoot(),layer);
    }
}
int main()
{
    MaterialSourceProfile profile=MaterialSourceProfile::AiGenerated;
    MaterialSettingsLayer layer;
    layer.Emission={true,99};
    assert(ParseAssetMaterialSettings({},profile,layer)==SettingsResult::Success);
    assert(profile==MaterialSourceProfile::Source && layer.ArmMask==0 && !layer.Emission.Present);
    assert(Parse("{}",profile,layer)==SettingsResult::Success);
    assert(Parse(R"({"profile":"ai_generated","arm":{"occlusion":"ignore","roughness":"constant:0.7","metallic":"texture"},"doubleSided":"force_true","alphaMode":"force_opaque","emissiveNitsPerUnit":400})",profile,layer)==SettingsResult::Success);
    assert(profile==MaterialSourceProfile::AiGenerated && layer.ArmMask==7 && layer.Arm.Channels[1].Constant==.7 && layer.Arm.Channels[2].Mode==ArmMode::Texture);
    assert(layer.DoubleSided==DoubleSidedSetting::ForceTrue && layer.AlphaMode==AlphaModeSetting::ForceOpaque && layer.Emission.NitsPerUnit==400);
    ResolvedMaterialSettings resolved;
    assert(ResolveMaterialSettings(profile,layer,{}, {},resolved)==MaterialSettingsStatus::Success);
    const auto before=EncodeCanonicalMaterialSettings(resolved);
    for (const char* invalid : {
        R"(null)",R"([])",R"({"profile":null})",R"({"profile":"guessed_glb"})",
        R"({"profile":"source","profile":"ai_generated"})",R"({"extra":1})",
        R"({"arm":null})",R"({"arm":"auto"})",R"({"arm":{"metallic":"texture","metallic":"ignore"}})",
        R"({"arm":{"metallic":0}})",R"({"arm":{"roughness":"constant:NaN"}})",R"({"arm":{"roughness":"constant:1.1"}})",
        R"({"arm":{"occlusion":"AUTO"}})",R"({"arm":{"ao":"auto"}})",R"({"arm":{"metallic":"texture\u0000"}})",
        R"({"arm":{"metallic":"\u3042"}})",R"({"doubleSided":true})",R"({"doubleSided":"inherit"})",
        R"({"alphaMode":"MASK"})",R"({"alphaMode":null})",R"({"emissiveNitsPerUnit":0})",
        R"({"emissiveNitsPerUnit":-1})",R"({"emissiveNitsPerUnit":"400"})",R"({"emissiveNitsPerUnit":null})",
        R"({"emissiveNitsPerUnit":400,"emissiveNitsPerUnit":500})",R"({"profile":"source","arm":{"roughness":"constant:0.5"},"emissiveNitsPerUnit":0})",
        R"({"profile\u0000":"source"})"})
    {
        assert(Parse(invalid,profile,layer)!=SettingsResult::Success);
        assert(profile==MaterialSourceProfile::AiGenerated);
        assert(ResolveMaterialSettings(profile,layer,{}, {},resolved)==MaterialSettingsStatus::Success);
        const auto after=EncodeCanonicalMaterialSettings(resolved);
        assert(after.Size==before.Size && std::memcmp(before.Bytes,after.Bytes,before.Size)==0);
    }
    assert(Parse(R"({"doubleSided":"auto","alphaMode":"from_source","arm":{}})",profile,layer,false)==SettingsResult::Success);
    assert(layer.ArmMask==0 && layer.DoubleSided==DoubleSidedSetting::Auto && layer.AlphaMode==AlphaModeSetting::FromSource && !layer.Emission.Present);
    assert(Parse(R"({"profile":"source"})",profile,layer,false)==SettingsResult::UnknownField);
    assert(ParseMaterialSettingsLayer({},layer)==SettingsResult::Success && layer.ArmMask==0);
    std::puts("MaterialImportSettingsJsonTest PASS: strict_fields_profile_layers_atomic");
    return 0;
}
