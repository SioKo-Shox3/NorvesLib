// PS5.1集約の境界と、既存directoryを置換しない公開primitiveを検証する。
#include "Tools/AssetCook/TextureAssetSetOutput.h"
#include "Text/JsonDocument.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#if defined(_WIN32)
#include <Windows.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Tools::AssetCook;
int main()
{
    AssetCookedReference rows[2];
    for (auto& row:rows)
    {
        row.Kind=AssetKind::Texture;row.EntryType=MakeAssetPackageFourCC('T','e','x','0');
        row.LogicalPath="Textures/z.ppm";row.EntryName="z.nvtex";row.Variant="default";
        row.Format="nvtex.v0.rgba8.linear";row.CookedPackage="Cooked/z.nvpkg";
        row.SourceHash=0x1234;row.CookedHash=0x5678;
    }
    rows[1].LogicalPath="Textures/a.ppm";rows[1].Variant="'\"\\<>&/";
    Container::AnsiString json="held",error;
    CHECK(Detail::SerializeLegacyTextureManifest(rows,json,error));
    CHECK(json.size()>4 && json[0]=='{' && json[1]=='\r' && json[2]=='\n' && json.back()=='}');
    const auto* z=std::strstr(json.c_str(),"Textures/z.ppm");const auto* a=std::strstr(json.c_str(),"Textures/a.ppm");
    CHECK(z && a && z<a);
    CHECK(std::strstr(json.c_str(),"\\u0027\\\"\\\\\\u003c\\u003e\\u0026/")!=nullptr);
    for (size_t i=0;i<json.size();++i) if (json[i]=='\n') CHECK(i>0 && json[i-1]=='\r');
    JsonDocument parsed;CHECK(JsonDocument::TryParseUtf8({reinterpret_cast<const uint8_t*>(json.data()),json.size()},parsed));
    CHECK(parsed.GetRoot().FindMember("assets").GetArraySize()==2);
    rows[1].Kind=AssetKind::Model;const auto held=json;
    CHECK(!Detail::SerializeLegacyTextureManifest(rows,json,error) && json==held);
    rows[1].Kind=AssetKind::Texture;rows[1].Variant="bad\n";
    CHECK(!Detail::SerializeLegacyTextureManifest(rows,json,error) && json==held);
    CHECK(!Detail::SerializeLegacyTextureManifest({},json,error) && json==held);
#if defined(_WIN32)
    // stage完成後に宛先が現れた状態を、サービスと同じno-replace primitiveで再現する。
    char name[96];std::snprintf(name,sizeof(name),"norves-texture-publish-%lu-%llu",GetCurrentProcessId(),static_cast<unsigned long long>(GetTickCount64()));
    const auto root=std::filesystem::temp_directory_path()/name;
    CHECK(std::filesystem::create_directory(root));
    const auto stage=root/"stage",dest=root/"dest";
    CHECK(std::filesystem::create_directory(stage));CHECK(std::filesystem::create_directory(dest));
    { std::ofstream file(stage/"payload",std::ios::binary);file<<"new";CHECK(file.good()); }
    CHECK(!Detail::PublishNewTextureAssetSet(stage,dest,error));
    CHECK(std::filesystem::exists(stage/"payload") && std::filesystem::is_empty(dest));
    { std::ofstream file(dest/"intruder",std::ios::binary);file<<"old";CHECK(file.good()); }
    CHECK(!Detail::PublishNewTextureAssetSet(stage,dest,error));
    { std::ifstream file(dest/"intruder",std::ios::binary);char bytes[3]={};file.read(bytes,3);CHECK(file && std::memcmp(bytes,"old",3)==0); }
    std::filesystem::remove_all(dest);
    CHECK(Detail::PublishNewTextureAssetSet(stage,dest,error));
    CHECK(!std::filesystem::exists(stage) && std::filesystem::exists(dest/"payload"));
    std::filesystem::remove_all(root);
#else
    CHECK(!Detail::PublishNewTextureAssetSet("stage","dest",error));
#endif
    std::puts("TextureAssetSetOutputTest PASS: serializer_atomicity_no_replace");return 0;
}
