// 実JsonDocumentからPBR source値を読む契約。実Windows/Coreで検証する。
#include "Resource/GltfMaterialSource.h"
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
using namespace NorvesLib::Core::Gltf;
namespace
{
    MaterialReadResult Parse(const char* bytes, MaterialSource& out, uint32_t count=5)
    {
        Container::String text;
        for (;*bytes!=0;++bytes)
        {
            text.push_back(static_cast<Container::String::value_type>(static_cast<uint8_t>(*bytes)));
        }
        JsonDocument document;
        assert(JsonDocument::TryParse(text,document));
        return ReadMaterialSource(document.GetRoot(),count,out);
    }
    bool Same(const MaterialSource& a,const MaterialSource& b)
    {
        if (std::memcmp(a.BaseColor,b.BaseColor,sizeof(a.BaseColor))!=0 ||
            std::memcmp(a.EmissiveFactor,b.EmissiveFactor,sizeof(a.EmissiveFactor))!=0 ||
            a.Metallic!=b.Metallic || a.Roughness!=b.Roughness || a.NormalScale!=b.NormalScale ||
            a.OcclusionStrength!=b.OcclusionStrength || a.EmissiveStrength!=b.EmissiveStrength ||
            a.AlphaMode!=b.AlphaMode || a.AlphaCutoff!=b.AlphaCutoff || a.DoubleSided!=b.DoubleSided)
        {
            return false;
        }
        for (size_t index=0;index<5;++index)
        {
            if (a.Textures[index].Present!=b.Textures[index].Present || a.Textures[index].Index!=b.Textures[index].Index)
            {
                return false;
            }
        }
        return true;
    }
}
int main()
{
    MaterialSource out;
    assert(Parse("{}",out,0).Succeeded() && Same(out,MaterialSource{}));
    assert(Parse(R"({"emissiveTexture":{"index":0}})",out,1).Succeeded());
    assert(out.Textures[4].Present && out.EmissiveFactor[0]==0 && out.EmissiveStrength==1);
    assert(Parse(R"({"extensions":{"KHR_materials_emissive_strength":{}}})",out).Succeeded() && out.EmissiveStrength==1);
    assert(Parse(R"({"extensions":{"UNKNOWN_optional":{"value":1}}})",out).Succeeded());
    assert(Parse(R"({"alphaMode":"BLEND"})",out).Succeeded() && out.AlphaMode==MaterialAlphaMode::Blend);
    assert(Parse(R"({"emissiveFactor":[1e-200,0,0],"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":1e-130}}})",out).Succeeded());
    assert(out.EmissiveFactor[0]==1e-200 && out.EmissiveStrength==1e-130);
    assert(Parse(R"({"name":"test","pbrMetallicRoughness":{"baseColorFactor":[0.1,0.2,0.3,0.4],"baseColorTexture":{"index":0,"texCoord":0},"metallicFactor":0.25,"roughnessFactor":0.75,"metallicRoughnessTexture":{"index":2}},"normalTexture":{"index":1,"scale":-2},"occlusionTexture":{"index":3,"strength":0.4},"emissiveTexture":{"index":4},"emissiveFactor":[0.1,0.2,0.3],"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":4}},"alphaMode":"MASK","alphaCutoff":1.25,"doubleSided":true})",out).Succeeded());
    assert(out.BaseColor[0]==.1 && out.BaseColor[3]==.4 && out.Metallic==.25 && out.Roughness==.75);
    assert(out.NormalScale==-2 && out.OcclusionStrength==.4 && out.EmissiveFactor[2]==.3 && out.EmissiveStrength==4);
    assert(out.AlphaCutoff==1.25 && out.AlphaMode==MaterialAlphaMode::Mask && out.DoubleSided);
    for (size_t index=0;index<5;++index)
    {
        assert(out.Textures[index].Present && out.Textures[index].Index==index);
    }
    const auto before=out;
    for (const char* bad:{R"(null)",R"([])",R"({"name":0})",R"({"name":"a\u0000b"})",
        R"({"emissiveFactor":[0,0]})",R"({"emissiveFactor":[0,null,0]})",R"({"emissiveFactor":[0,2,0]})",
        R"({"pbrMetallicRoughness":null})",R"({"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1,1]}})",
        R"({"pbrMetallicRoughness":{"metallicFactor":-1}})",R"({"pbrMetallicRoughness":{"roughnessFactor":"1"}})",
        R"({"alphaMode":"mask"})",R"({"alphaMode":0})",R"({"alphaCutoff":-1})",R"({"doubleSided":1})",
        R"({"normalTexture":null})",R"({"normalTexture":{}})",R"({"normalTexture":{"index":-1}})",
        R"({"normalTexture":{"index":0.5}})",R"({"normalTexture":{"index":4294967296}})",R"({"normalTexture":{"index":5}})",
        R"({"normalTexture":{"index":0,"scale":null}})",R"({"occlusionTexture":{"index":0,"strength":1.1}})",
        R"({"normalTexture":{"index":0,"texCoord":1}})",R"({"normalTexture":{"index":0,"texCoord":0.5}})",
        R"({"normalTexture":{"index":0,"extensions":{}}})",R"({"normalTexture":{"index":0,"extensions":{"KHR_texture_transform":{}}}})",
        R"({"extensions":null})",R"({"extensions":{"KHR_materials_emissive_strength":null}})",
        R"({"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":-1}}})",
        R"({"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":1,"emissiveStrength":2}}})",
        R"({"emissiveFactor":[0,0,0],"emissiveFactor":[1,1,1]})",R"({"doubleSided\u0000":true})",
        R"({"extensions":{"UNKNOWN_optional":null}})",R"({"normalTexture":{"index":0,"index":1}})",
        R"({"normalTexture":{"index":0,"texCoord":0,"texCoord":0}})"})
    {
        const auto failure=Parse(bad,out);
        assert(!failure.Succeeded() && failure.Field[0]!=0 && Same(out,before));
    }
    assert(ReadMaterialSource({},0,out).Succeeded() && Same(out,MaterialSource{}));
    std::puts("GltfMaterialSourceJsonTest PASS: source_defaults_fields_refs_extension_atomic");
    return 0;
}
