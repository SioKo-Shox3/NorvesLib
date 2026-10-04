// 依存印のraw byte・不在・policy・失敗保持を実file/Json/glTF resolverで検証する。
#include "Tools/AssetCook/CookDependencySnapshot.h"
#include "Resource/ImportSettingsFile.h"
#include "Resource/GltfBufferFile.h"
#include "Asset/AssetPackageFormat.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#if defined(_WIN32)
#include <Windows.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)
using namespace NorvesLib::Core;
using namespace NorvesLib::Tools::AssetCook;
namespace
{
    using Bytes=Container::VariableArray<uint8_t>;
    Bytes Read(const std::filesystem::path& path)
    { std::ifstream file(path,std::ios::binary);CHECK(file);Bytes bytes;char c;while(file.get(c)) bytes.push_back(static_cast<uint8_t>(c));CHECK(file.eof());return bytes; }
    void Write(const std::filesystem::path& path,Container::Span<const uint8_t> bytes)
    { std::ofstream file(path,std::ios::binary|std::ios::trunc);CHECK(file);if(!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));file.close();CHECK(!file.fail()); }
    void WriteTextFixture(const std::filesystem::path& path,const char* value)
    { Write(path,{reinterpret_cast<const uint8_t*>(value),std::strlen(value)}); }
    SingleAssetCookRequest Model(const std::filesystem::path& path)
    {
        SingleAssetCookRequest r;r.InputPath=path;r.Kind="model";r.LogicalPath="Models/test.gltf";r.EntryName="test.nvmesh";
        r.EntryTypeText="Msh0";r.Format="nvmesh.v0.mesh3d.pnt.u32.clustered";r.Variant="default";return r;
    }
    CookDependencySnapshot Capture(const SingleAssetCookRequest& request,uint64_t revision=1)
    {
        CookDependencySnapshot out;Container::AnsiString error;
        if(!CaptureCookDependencySnapshot(request,revision,out,error)) { std::fprintf(stderr,"capture error: %s\n",error.c_str());CHECK(false); }
        return out;
    }
    void Reject(const SingleAssetCookRequest& request,uint64_t revision=1)
    {
        CookDependencySnapshot out;out.SchemaVersion=7;out.CookerRevision=99;out.Fingerprint=0xfeed;
        out.Files.push_back({"held",CookDependencyRole::ExternalImage,true,123,456});Container::AnsiString error;
        CHECK(!CaptureCookDependencySnapshot(request,revision,out,error));
        CHECK(out.SchemaVersion==7 && out.CookerRevision==99 && out.Fingerprint==0xfeed && out.Files.size()==1);
        CHECK(out.Files[0].Path=="held" && out.Files[0].Role==CookDependencyRole::ExternalImage && out.Files[0].bPresent && out.Files[0].Size==123 && out.Files[0].ContentHash==456);
        CHECK(!error.empty());
    }
    size_t Count(const CookDependencySnapshot& snapshot,CookDependencyRole role)
    { size_t n=0;for(const auto& file:snapshot.Files) if(file.Role==role) ++n;return n; }
}
int main()
{
#if defined(_WIN32)
    char name[96];std::snprintf(name,sizeof(name),"norves-dependencies-%lu-%llu",GetCurrentProcessId(),static_cast<unsigned long long>(GetTickCount64()));
#else
    const char* name="norves-dependencies-native-required";
#endif
    const auto root=std::filesystem::temp_directory_path()/name;CHECK(std::filesystem::create_directory(root));
    const auto modelDir=root/"model";
    std::filesystem::copy("Assets/Models/Rendering3DTestSilverGltf",modelDir,std::filesystem::copy_options::recursive);
    auto request=Model(modelDir/"Rendering3DTestSilverGltf.gltf");
    const auto initial=Capture(request);
    CHECK(initial.Files.size()==6 && Count(initial,CookDependencyRole::Source)==1 && Count(initial,CookDependencyRole::Sidecar)==1);
    CHECK(Count(initial,CookDependencyRole::ExternalBuffer)==1 && Count(initial,CookDependencyRole::ExternalImage)==3);
    CHECK(!initial.Files[1].bPresent && initial.Files[1].Size==0 && initial.Files[1].ContentHash==0);
    CHECK(Capture(request).Fingerprint==initial.Fingerprint && Capture(request,2).Fingerprint!=initial.Fingerprint);
    auto changed=request;changed.Variant="night";CHECK(Capture(changed).Fingerprint!=initial.Fingerprint);
    changed=request;changed.PackagePath="other/output";changed.ManifestPath="other/manifest";changed.bSkipIfUnchanged=true;
    CHECK(Capture(changed).Fingerprint==initial.Fingerprint);
    changed=request;changed.SkeletalDecode.InfluencePolicy=Skeletal::SkeletalInfluencePolicy::ReduceToFour;
    CHECK(Capture(changed).Fingerprint!=initial.Fingerprint);
    Reject(request,0);
    const auto originalRoot=Read(request.InputPath);auto rootWhitespace=originalRoot;rootWhitespace.push_back('\n');Write(request.InputPath,rootWhitespace);
    CHECK(Capture(request).Fingerprint!=initial.Fingerprint);Write(request.InputPath,originalRoot);CHECK(Capture(request).Fingerprint==initial.Fingerprint);
    const auto buffer=modelDir/"Rendering3DTestSilverGltf.bin";
    const auto originalBuffer=Read(buffer);auto excess=originalBuffer;excess.push_back(0x42);Write(buffer,excess);
    CHECK(Capture(request).Fingerprint!=initial.Fingerprint);Write(buffer,originalBuffer);
    const auto image=modelDir/"textures/silver_albedo.png";const auto originalImage=Read(image);auto extraImage=originalImage;extraImage.push_back(0x37);Write(image,extraImage);
    CHECK(Capture(request).Fingerprint!=initial.Fingerprint);Write(image,originalImage);
    for(const auto& path:{buffer,image})
    {
        auto held=path;held+=".held";std::filesystem::rename(path,held);Reject(request);std::filesystem::rename(held,path);
        CHECK(Capture(request).Fingerprint==initial.Fingerprint);
    }
    auto sidecar=request.InputPath;sidecar+=".import.json";
    WriteTextFixture(sidecar,"{\"version\":1}");const auto present=Capture(request);
    CHECK(present.Files[1].bPresent && present.Fingerprint!=initial.Fingerprint);
    WriteTextFixture(sidecar,"{ \"version\" : 1 }\n");const auto whitespace=Capture(request);
    CHECK(whitespace.Fingerprint!=present.Fingerprint);
    AssetImport::LoadedImportSettings loaded;CHECK(AssetImport::LoadImportSettingsFile(request.InputPath,{},loaded).Result==AssetImport::SettingsFileResult::Success);
    const auto sidecarBytes=Read(sidecar);CHECK(loaded.RawSourceBytes.size()==sidecarBytes.size());
    CHECK(std::memcmp(loaded.RawSourceBytes.data(),sidecarBytes.data(),sidecarBytes.size())==0 && loaded.Settings.Scale==1);
    CHECK(whitespace.Files[1].ContentHash==Asset::ComputeAssetPackagePayloadHash(sidecarBytes.data(),sidecarBytes.size()));
    AssetImport::LoadedImportSettingsDocument loadedDocument;
    CHECK(AssetImport::LoadImportSettingsDocument(request.InputPath,{},loadedDocument).Result==AssetImport::SettingsFileResult::Success);
    CHECK(loadedDocument.RawSourceBytes.size()==sidecarBytes.size() && std::memcmp(loadedDocument.RawSourceBytes.data(),sidecarBytes.data(),sidecarBytes.size())==0);
    changed=request;changed.bNoSidecar=true;const auto disabled=Capture(changed);CHECK(Count(disabled,CookDependencyRole::Sidecar)==0);
    WriteTextFixture(sidecar,"invalid JSON");Reject(request);CHECK(Capture(changed).Fingerprint==disabled.Fingerprint);
    std::filesystem::remove(sidecar);CHECK(Capture(request).Fingerprint==initial.Fingerprint);
    changed=request;changed.bRequireSidecar=true;Reject(changed);
    changed=request;changed.ImportSettingsOverridePath=root/"absent.import.json";Reject(changed);
    const auto overrideA=root/"a.import.json",overrideB=root/"b.import.json";
    WriteTextFixture(overrideA,"{\"version\":1}");WriteTextFixture(overrideB,"{\"version\":1}");
    changed=request;changed.ImportSettingsOverridePath=overrideA;const auto a=Capture(changed);
    changed.ImportSettingsOverridePath=overrideB;CHECK(Capture(changed).Fingerprint!=a.Fingerprint);
    const auto unicodeOverride=root/std::filesystem::u8path(reinterpret_cast<const char*>(u8"設定🐺.json"));
    WriteTextFixture(unicodeOverride,"{\"version\":1}");changed.ImportSettingsOverridePath=unicodeOverride;
    CHECK(Capture(changed).Fingerprint!=a.Fingerprint);
    changed.bNoSidecar=true;Reject(changed);
#if defined(_WIN32)
    WriteTextFixture(sidecar,"{\"version\":1}");
    const auto lock=CreateFileW(sidecar.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(lock!=INVALID_HANDLE_VALUE);Reject(request);CHECK(CloseHandle(lock));std::filesystem::remove(sidecar);
#endif
    // 実GLBの内包buffer/imageはsource本体に含み、外部fileを重複作成しない。
    const auto glb=root/"embedded.glb";std::filesystem::copy_file("Test/Core/Asset/Fixtures/AssetCook/embedded_tri.glb",glb);
    const auto embedded=Capture(Model(glb));CHECK(embedded.Files.size()==2 && Count(embedded,CookDependencyRole::ExternalBuffer)==0 && Count(embedded,CookDependencyRole::ExternalImage)==0);
    const auto originalGlb=Read(glb);auto truncated=originalGlb;truncated.pop_back();Write(glb,truncated);Reject(Model(glb));Write(glb,originalGlb);
    CHECK(Capture(Model(glb)).Fingerprint==embedded.Fingerprint);
    std::filesystem::copy("Assets/Models/M9Skinned",root/"skeletal",std::filesystem::copy_options::recursive);
    auto skeletal=Model(root/"skeletal/ValidU8Float.gltf");skeletal.EntryTypeText="Skl0";skeletal.Format="nvskel.v0.skinned.pnujiw.u32";
    CHECK(Count(Capture(skeletal),CookDependencyRole::ExternalBuffer)==1);
    // URI復号とcanonical境界は既存readerを通す。画像byteはここでは完全decodeしない。
    const auto percent=root/"percent.gltf";WriteTextFixture(root/"payload a.bin","X");Write(root/"image a.png",originalImage);
    const char* percentJson=R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"payload%20a.bin"}],"images":[{"uri":"image%20a.png"}]})";
    WriteTextFixture(percent,percentJson);const auto percentSnapshot=Capture(Model(percent));CHECK(percentSnapshot.Files.size()==4);
    Gltf::BufferFileContext context{percent};Bytes readBytes{1,2};std::filesystem::path resolved="held";
    const char* uri="payload a.bin";
    CHECK(Gltf::ReadBufferFileWithPath({reinterpret_cast<const uint8_t*>(uri),std::strlen(uri)},readBytes,&context,&resolved)==Gltf::ExternalBufferReadResult::Success);
    CHECK(resolved==std::filesystem::weakly_canonical(root/"payload a.bin") && readBytes.size()==1 && readBytes[0]=='X');
    uri="missing.bin";resolved="held";
    CHECK(Gltf::ReadBufferFileWithPath({reinterpret_cast<const uint8_t*>(uri),std::strlen(uri)},readBytes,&context,&resolved)!=Gltf::ExternalBufferReadResult::Success);
    CHECK(resolved=="held" && readBytes.empty());
    WriteTextFixture(percent,R"({"buffers":[{"byteLength":1,"uri":"payload%20a.bin"}],"images":[],"images":[]})");Reject(Model(percent));
    WriteTextFixture(percent,R"({"buffers":[{"byteLength":1,"uri":"../outside.bin"}]})");Reject(Model(percent));
    // opaque種別はsourceだけ。出力のformat/payload検証は増分decision層の責任。
    for(const char* kind:{"raw","texture","audio"})
    {
        auto opaque=Model(root/"payload a.bin");opaque.Kind=kind;
        if (std::strcmp(kind,"raw")==0) { opaque.Format="raw.v0";opaque.EntryTypeText="Raw"; }
        else if (std::strcmp(kind,"texture")==0) { opaque.Format="nvtex.v0.rgba8.linear";opaque.EntryTypeText="Tex0"; }
        else { opaque.Format="nvaud.v0.pcm16";opaque.EntryTypeText="Aud0"; }
        const auto snapshot=Capture(opaque);CHECK(snapshot.Files.size()==1);
        opaque.bNoSidecar=true;Reject(opaque);
    }
    std::filesystem::remove_all(root);
    std::puts("CookDependencySnapshotTest PASS: raw_dependencies_presence_options_failure_atomicity");return 0;
}
