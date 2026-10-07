// texture spec v1の所有・旧default・path境界・失敗保持を実JsonDocumentで検証する。
#include "Tools/AssetCook/TextureAssetSetSpec.h"
#include "Text/JsonDocument.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)
using namespace NorvesLib::Core;
using namespace NorvesLib::Tools::AssetCook;
namespace
{
    const char* Base = R"({"version":1,"name":"set","package_root":"Cooked/Set","default_variant":"default","textures":[{"logical_path":"Assets/Textures/a.png","source_path":"Assets/a.png","format":"nvtex.v0.rgba8.linear","package_name":"a.nvpkg","entry_name":"Assets/Textures/a.nvtex","usage":"arm"}]})";
    bool Equals(const Container::AnsiString& a, const char* b) { return a.size()==std::strlen(b) && std::memcmp(a.data(),b,a.size())==0; }
    JsonDocument Json(const Container::AnsiString& text)
    {
        JsonDocument doc;
        const bool parsed=JsonDocument::TryParseUtf8({reinterpret_cast<const uint8_t*>(text.data()),text.size()},doc);
        if (!parsed)
        {
            std::fprintf(stderr,"fixture_json_bytes=%zu:",text.size());
            for (const unsigned char byte:text) std::fprintf(stderr,"%02x",byte);
            std::fprintf(stderr,"\n");
        }
        CHECK(parsed);
        return doc;
    }
    Container::AnsiString Changed(const char* before, const char* after)
    {
        // 部分置換の終端NULにfixtureを依存させず、3区間を明示連結する。
        const auto* found=std::strstr(Base,before);CHECK(found);
        CHECK(std::strstr(found+std::strlen(before),before)==nullptr);
        Container::AnsiString text;
        text.append(Base,static_cast<size_t>(found-Base));
        text.append(after);
        text.append(found+std::strlen(before));
        return text;
    }
    void Reject(const Container::AnsiString& text, TextureAssetSetError code)
    {
        auto doc=Json(text);TextureAssetSetSpec out;
        out.Name="held";out.PackageRoot="held-root";out.DefaultVariant="held-default";
        TextureAssetSetEntry entry;
        entry.LogicalPath="held-logical";entry.SourcePath="held-source";entry.Format="held-format";
        entry.PackageName="held-package";entry.EntryName="held-entry";entry.Variant="held-variant";
        out.Textures.push_back(entry);out.Textures.push_back(entry);
        const auto result=ParseTextureAssetSetSpec(doc.GetRoot(),out);
        CHECK(result.Code==code && Equals(out.Name,"held") && Equals(out.PackageRoot,"held-root") && Equals(out.DefaultVariant,"held-default"));
        CHECK(out.Textures.size()==2);
        for (const auto& saved:out.Textures)
        {
            CHECK(Equals(saved.LogicalPath,"held-logical") && Equals(saved.SourcePath,"held-source") && Equals(saved.Format,"held-format"));
            CHECK(Equals(saved.PackageName,"held-package") && Equals(saved.EntryName,"held-entry") && Equals(saved.Variant,"held-variant"));
        }
    }
    void ActualSpecs()
    {
        const char* names[]={"Rendering3DTestSilverTextures","Rendering3DTestSilverGltfTextures"};
        for (size_t i=0;i<2;++i)
        {
            Container::AnsiString path="Assets/AssetSets/";path.append(names[i]);path.append(".json");
            std::ifstream file(path.c_str(),std::ios::binary);CHECK(file);
            Container::VariableArray<uint8_t> bytes;char c;
            while (file.get(c)) bytes.push_back(static_cast<uint8_t>(c));
            CHECK(file.eof());JsonDocument doc;
            CHECK(JsonDocument::TryParseUtf8({bytes.data(),bytes.size()},doc));
            TextureAssetSetSpec spec;CHECK(ParseTextureAssetSetSpec(doc.GetRoot(),spec).Succeeded());
            CHECK(spec.Textures.size()==(i==0?5:3) && Equals(spec.Name,names[i]));
            doc.Reset();bytes.clear();
            CHECK(Equals(spec.DefaultVariant,"default") && Equals(spec.PackageRoot,i==0?"Cooked/Silver":"Cooked/SilverGltf"));
            const char* classic[]={"silver_albedo","silver_normal-ogl","silver_metallic","silver_roughness","silver_ao"};
            const char* gltf[]={"silver_albedo","silver_normal-ogl","silver_arm"};
            const char* prefix=i==0?"Textures/Silver/":"Models/Rendering3DTestSilverGltf/textures/";
            for (size_t row=0;row<spec.Textures.size();++row)
            {
                const char* base=i==0?classic[row]:gltf[row];
                Container::AnsiString logical=prefix;logical.append(base);logical.append(".png");
                Container::AnsiString source="Assets/";source.append(logical);
                Container::AnsiString package=base;package.append(".nvpkg");
                Container::AnsiString entry=prefix;entry.append(base);entry.append(".nvtex");
                const auto& value=spec.Textures[row];
                CHECK(Equals(value.LogicalPath,logical.c_str()) && Equals(value.SourcePath,source.c_str()));
                CHECK(Equals(value.PackageName,package.c_str()) && Equals(value.EntryName,entry.c_str()) && Equals(value.Variant,"default"));
                const char* format=row==0?"nvtex.v0.rgba8.srgb":(i==0 && row>=2?"nvtex.v0.r8.linear":"nvtex.v0.rgba8.linear");
                CHECK(Equals(value.Format,format));
            }
        }
    }
}
int main()
{
    {
        const Container::AnsiString mixed =
            R"({"version":2,"name":"mixed","package_root":"Cooked/Mixed","default_variant":"default","assets":[{"kind":"raw","logical_path":"Data/config","source_path":"config.bin","format":"raw.v0","package_name":"config.nvpk","entry_name":"config"},{"kind":"model","logical_path":"Models/rig","source_path":"rig.gltf","format":"nvskel.v0.skinned.pnujiw.u32","package_name":"rig.nvpk","entry_name":"rig"}]})";
        const auto doc = Json(mixed);
        TextureAssetSetSpec spec;
        CHECK(ParseTextureAssetSetSpec(doc.GetRoot(), spec).Code == TextureAssetSetError::InvalidVersion);
        CHECK(ParseTextureAssetSetSpec(doc.GetRoot(), spec, true).Succeeded());
        CHECK(spec.Version == 2 && spec.Textures.size() == 2 && spec.Textures[0].Kind == "raw" &&
              spec.Textures[0].EntryType == "Raw " && spec.Textures[1].EntryType == "Skl0");
    }

    auto doc=Json(Base);TextureAssetSetSpec spec;
    CHECK(ParseTextureAssetSetSpec(doc.GetRoot(),spec).Succeeded());doc.Reset();
    CHECK(spec.Textures.size()==1 && Equals(spec.Textures[0].LogicalPath,"Textures/a.png"));
    CHECK(Equals(spec.Textures[0].EntryName,"Textures/a.nvtex") && Equals(spec.Textures[0].Variant,"default"));
    doc=Json(Changed("\"usage\":\"arm\"","\"variant\":\"night\",\"usage\":{\"unknown\":true}"));
    CHECK(ParseTextureAssetSetSpec(doc.GetRoot(),spec).Succeeded() && Equals(spec.Textures[0].Variant,"night"));
    doc=Json(Changed("Assets/Textures/a.png","  Assets\\\\Textures\\\\a.png  "));
    CHECK(ParseTextureAssetSetSpec(doc.GetRoot(),spec).Succeeded() && Equals(spec.Textures[0].LogicalPath,"Textures/a.png"));
    for (const char* version:{"1.0","1e0","true","\"1\"","0","2"}) Reject(Changed("\"version\":1",(Container::AnsiString("\"version\":")+version).c_str()),TextureAssetSetError::InvalidVersion);
    Reject(Changed("\"version\":1,",""),TextureAssetSetError::MissingField);
    Reject(Changed("\"version\":1,","\"version\":1,\"version\":1,"),TextureAssetSetError::DuplicateField);
    Reject(Changed("\"name\":\"set\"","\"name\":null"),TextureAssetSetError::InvalidType);
    Reject(Changed("\"name\":\"set\"","\"name\":\"\\u0000\""),TextureAssetSetError::InvalidValue);
    Reject(Changed("\"name\":\"set\"","\"name\\u0000\":\"set\""),TextureAssetSetError::InvalidValue);
    Reject(Changed("\"usage\":\"arm\"","\"variant\":\" \""),TextureAssetSetError::InvalidValue);
    Reject(Changed("\"usage\":\"arm\"","\"variant\":null"),TextureAssetSetError::InvalidType);
    Reject(Changed("\"usage\":\"arm\"","\"variant\":\"\""),TextureAssetSetError::InvalidValue);
    const char* utf8Source=reinterpret_cast<const char*>(u8"Assets/犬🐺.png");
    doc=Json(Changed("Assets/a.png",utf8Source));
    CHECK(ParseTextureAssetSetSpec(doc.GetRoot(),spec).Succeeded());doc.Reset();
    CHECK(Equals(spec.Textures[0].SourcePath,utf8Source));
    doc=Json(Changed("Assets/a.png","Assets/\\u72ac\\ud83d\\udc3a.png"));
    CHECK(ParseTextureAssetSetSpec(doc.GetRoot(),spec).Succeeded());doc.Reset();
    CHECK(Equals(spec.Textures[0].SourcePath,utf8Source));
    for (const char* path:{"../a","/a","C:/a","C:a","a//b","a/./b","a/../b","a./b","a /b","a:b"})
        Reject(Changed("a.nvpkg",path),TextureAssetSetError::UnsafePath);
    Reject(Changed("Assets/Textures/a.png","Assets"),TextureAssetSetError::UnsafePath);
    Reject(Changed("Assets/Textures/a.png","assets"),TextureAssetSetError::UnsafePath);
    for (const char* blank:{"\\u000b", "\\u000c", "\\u0085", "\\u00a0", "\\u3000"})
        Reject(Changed("\"name\":\"set\"",(Container::AnsiString("\"name\":\"")+blank+"\"").c_str()),TextureAssetSetError::InvalidValue);
    doc=Json(Changed("Assets/Textures/a.png","\\u3000Assets/Textures/a.png\\u00a0"));
    CHECK(ParseTextureAssetSetSpec(doc.GetRoot(),spec).Succeeded() && Equals(spec.Textures[0].LogicalPath,"Textures/a.png"));
    const char* duplicate=R"(},{"logical_path":"assets/Textures/A.png","source_path":"b.png","format":"f","package_name":"b.nvpkg","entry_name":"b"}]})";
    // Assets prefixの大文字小文字は旧wrapperと同じく一致時だけ除去する。
    auto two=Changed("}]}",duplicate);doc=Json(two);
    CHECK(ParseTextureAssetSetSpec(doc.GetRoot(),spec).Succeeded() && spec.Textures.size()==2);
    two=Changed("}]}",R"(},{"logical_path":"Assets/textures/A.PNG","source_path":"b.png","format":"f","package_name":"b.nvpkg","entry_name":"b"}]})");
    Reject(two,TextureAssetSetError::DuplicateLogicalKey);
    two=Changed("}]}",R"(},{"logical_path":"b.png","source_path":"b.png","format":"f","package_name":"A.NVPKG","entry_name":"b"}]})");
    Reject(two,TextureAssetSetError::DuplicatePackage);
    doc=Json("[1,1.0,1e0,-1,0,true,null]");
    CHECK(doc.GetRoot().GetArrayElement(0).IsIntegerLiteral());
    CHECK(doc.GetRoot().GetArrayElement(0).AsNumber()==1 && doc.GetRoot().GetArrayElement(1).AsNumber()==1 && doc.GetRoot().GetArrayElement(2).AsNumber()==1);
    CHECK(!doc.GetRoot().GetArrayElement(1).IsIntegerLiteral() && !doc.GetRoot().GetArrayElement(2).IsIntegerLiteral());
    CHECK(doc.GetRoot().GetArrayElement(3).IsIntegerLiteral() && doc.GetRoot().GetArrayElement(4).IsIntegerLiteral());
    CHECK(!doc.GetRoot().GetArrayElement(5).IsIntegerLiteral() && !JsonValue{}.IsIntegerLiteral());
    ActualSpecs();
    std::puts("TextureAssetSetSpecTest PASS: v1_owned_paths_types_defaults_duplicates");
    return 0;
}
