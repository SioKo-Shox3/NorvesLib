#include "Tools/AssetCook/MeshCooker.h"
#include "Resource/ModelStaging.h"
#include "Resource/ImportSettingsFile.h"
#include "Asset/CookedMeshFormat.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Tools::AssetCook;
namespace Staging = NorvesLib::Core::ResourceIO::ModelStaging;
namespace Import = NorvesLib::Core::AssetImport;
namespace Asset = NorvesLib::Core::Asset;
namespace
{
    using Bytes = VariableArray<uint8_t>;
    constexpr const char* Geometry = R"json("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}])json";
    constexpr uint8_t Triangle[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,1,0,2,0};
    AnsiString Json(const char* buffers)
    {
        return AnsiString("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[") + buffers + "]," + Geometry + "}";
    }
    Bytes Copy(AnsiStringView text)
    {
        Bytes bytes;
        bytes.resize(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }
    void Append32(Bytes& bytes, uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(static_cast<uint8_t>(value >> shift));
        }
    }
    Bytes Glb(AnsiStringView json, bool bIncludeBin = true, Span<const uint8_t> binary = Triangle)
    {
        const size_t jsonSize = (json.size() + 3) & ~size_t{3};
        const size_t binSize = (binary.size() + 3) & ~size_t{3};
        Bytes bytes;
        Append32(bytes, 0x46546c67);
        Append32(bytes, 2);
        Append32(bytes, static_cast<uint32_t>(20 + jsonSize + (bIncludeBin ? 8 + binSize : 0)));
        Append32(bytes, static_cast<uint32_t>(jsonSize));
        Append32(bytes, 0x4e4f534a);
        for (const char value : json)
        {
            bytes.push_back(static_cast<uint8_t>(value));
        }
        while (bytes.size() < 20 + jsonSize)
        {
            bytes.push_back(' ');
        }
        if (bIncludeBin)
        {
            Append32(bytes, static_cast<uint32_t>(binSize));
            Append32(bytes, 0x004e4942);
            for (const auto value : binary)
            {
                bytes.push_back(value);
            }
            while (bytes.size() % 4 != 0)
            {
                bytes.push_back(0);
            }
        }
        return bytes;
    }
    void Write(const std::filesystem::path& path, Span<const uint8_t> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        assert(file.is_open());
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        assert(file.good());
    }
    struct TemporaryDirectory
    {
        std::filesystem::path Path;
        TemporaryDirectory()
        {
            // 並列試験の既存directoryを再利用しない。
            char suffix[64] = {};
            const auto converted = std::to_chars(suffix, suffix + sizeof(suffix),
                std::chrono::steady_clock::now().time_since_epoch().count());
            assert(converted.ec == std::errc{});
            const AnsiString name = AnsiString("norves-import-static-") +
                AnsiString(AnsiStringView(suffix, static_cast<size_t>(converted.ptr - suffix)));
            Path = std::filesystem::temp_directory_path() / std::filesystem::path(name.begin(), name.end());
            assert(std::filesystem::create_directory(Path));
        }
        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }
    };

    String CorePath(const std::filesystem::path& path)
    {
#if defined(UNICODE)
        return String(path.c_str());
#else
        return String(path.generic_string().c_str());
#endif
    }
    void SamePayload(const MeshCookResult& a,const MeshCookResult& b)
    {
        assert(a.NvmeshBytes.size()==b.NvmeshBytes.size());
        assert(std::memcmp(a.NvmeshBytes.data(),b.NvmeshBytes.data(),a.NvmeshBytes.size())==0);
    }
    bool Near(float a,float b) { return std::abs(a-b)<1e-6f; }
    void SameGeometry(const MeshCookResult& cooked,const Staging::ModelStagingData& loose)
    {
        const auto parsed=Asset::ParseCookedMesh(Asset::AssetBlob::CopyBytes(cooked.NvmeshBytes,"import-static"));
        assert(parsed.Succeeded() && parsed.Mesh.Vertices.size()==3 && loose.Vertices.size()==3);
        for(size_t i=0;i<3;++i)
        {
            const auto& a=parsed.Mesh.Vertices[i]; const auto& b=loose.Vertices[i];
            assert(Near(a.Position.X,b.Position[0]) && Near(a.Position.Y,b.Position[1]) && Near(a.Position.Z,b.Position[2]));
            assert(Near(a.Normal.X,b.Normal[0]) && Near(a.Normal.Y,b.Normal[1]) && Near(a.Normal.Z,b.Normal[2]));
            assert(Near(a.TexCoord.U,b.TexCoord[0]) && Near(a.TexCoord.V,b.TexCoord[1]));
        }
        assert(parsed.Mesh.Indices.size()==loose.ClusterizedIndices.size());
        for(size_t i=0;i<parsed.Mesh.Indices.size();++i) { assert(parsed.Mesh.Indices[i]==loose.ClusterizedIndices[i]); }
        assert(Near(parsed.Mesh.TotalBoundsCenter.X,loose.TotalBounds.CenterX));
        assert(Near(parsed.Mesh.TotalBoundsCenter.Y,loose.TotalBounds.CenterY));
        assert(Near(parsed.Mesh.TotalBoundsCenter.Z,loose.TotalBounds.CenterZ));
        assert(Near(parsed.Mesh.TotalBoundsRadius,loose.TotalBounds.Radius));
    }
}
int main()
{
    TemporaryDirectory directory;
    Write(directory.Path/"triangle.bin",Triangle);
    for(bool bGlb:{false,true})
    {
        const auto file=directory.Path/(bGlb?"model.glb":"model.gltf");
        const auto path=CorePath(file);
        const AnsiString cookPath(file.generic_string().c_str());
        const Bytes source=bGlb?Glb(Json(R"({"byteLength":102})")):Copy(Json(R"({"byteLength":102,"uri":"triangle.bin"})"));
        Write(file,source);
        auto sidecar=file; sidecar+=".import.json";
        AnsiString error;
        auto cook=[&](MeshCookResult& result,const Import::ImportSettingsFileOptions* options=nullptr)
        {
            ModelCookFingerprint fingerprint;
            const bool identified=FingerprintModelCookSource(source.data(),source.size(),
                "nvmesh.v0.mesh3d.pnt.u32.clustered",cookPath,"Models/model",fingerprint,error,options);
            const bool cooked=CookGltfToNvmesh(source.data(),source.size(),"nvmesh.v0.mesh3d.pnt.u32.clustered",
                cookPath,"Models/model",result,error,options);
            if (cooked)
            {
                assert(identified && fingerprint.SourceHash==result.SourceHash);
                assert(fingerprint.bHasImportSettings==result.bHasImportSettings &&
                    fingerprint.ImportSettingsHash==result.ImportSettingsHash &&
                    fingerprint.ImportSettingsPath==result.ImportSettingsPath);
            }
            return cooked;
        };
        auto stage=[&](Staging::ModelStagingData& result,const Import::ImportSettingsFileOptions* options=nullptr)
        {
            return Staging::BuildModelStagingFromLooseGltf(path,path,result,"test",0,options);
        };
        MeshCookResult baseline;
        Staging::ModelStagingData baselineLoose;
        assert(cook(baseline) && stage(baselineLoose));
        assert(!baseline.bHasImportSettings && baseline.ImportSettingsHash==0);
        SameGeometry(baseline,baselineLoose);

        // 空source locatorの自己完結GLB/data URIは従来どおりauto無しでcookできる。
        const Bytes selfContained = bGlb ? source : Copy(Json(
            R"({"byteLength":102,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA"})"));
        MeshCookResult memoryWithPath, memoryAuto;
        auto memoryCook = [&](MeshCookResult& result, AnsiStringView location,
                              const Import::ImportSettingsFileOptions* options=nullptr)
        {
            return CookGltfToNvmesh(selfContained.data(),selfContained.size(),"nvmesh.v0.mesh3d.pnt.u32.clustered",
                location,"Models/model",result,error,options);
        };
        assert(memoryCook(memoryWithPath,cookPath) && memoryCook(memoryAuto,{}));
        SamePayload(memoryWithPath,memoryAuto);
        assert(memoryAuto.SourceHash==memoryWithPath.SourceHash && !memoryAuto.bHasImportSettings);
        Import::ImportSettingsFileOptions memoryRequired; memoryRequired.bRequired=true;
        const auto memoryHash=memoryAuto.SourceHash;
        assert(!memoryCook(memoryAuto,{},&memoryRequired) && memoryAuto.SourceHash==memoryHash);
        Import::ImportSettingsFileOptions memoryOverride; memoryOverride.OverridePath=directory.Path/"memory.import.json";
        Write(memoryOverride.OverridePath,Copy(R"({"version":1,"units":{"scale":2}})"));
        assert(memoryCook(memoryAuto,{},&memoryOverride) && memoryAuto.bHasImportSettings && memoryAuto.SourceHash!=memoryHash);

        Write(sidecar,Copy(R"({"version":1})"));
        MeshCookResult identity;
        assert(cook(identity) && identity.bHasImportSettings && identity.SourceHash!=baseline.SourceHash);
        assert(identity.ImportSettingsPath==sidecar.generic_string().c_str());
        SamePayload(baseline,identity);
        Import::ImportSettingsFileOptions disabled; disabled.bDisabled=true;
        MeshCookResult disabledCook;
        assert(cook(disabledCook,&disabled) && !disabledCook.bHasImportSettings && disabledCook.SourceHash==baseline.SourceHash);
        SamePayload(baseline,disabledCook);
        Staging::ModelStagingData disabledLoose;
        assert(stage(disabledLoose,&disabled)); SameGeometry(baseline,disabledLoose);

        const char* settings[]={
            R"({"version":1,"units":{"scale":2}})",
            R"({"version":1,"units":{"fit":{"axis":"up","meters":0.6}},"origin":{"mode":"bounds_bottom_center"}})",
            R"({"version":1,"axes":{"up":"+Z","forward":"+X"},"mesh":{"flipU":true,"flipV":true}})",
            R"({"version":1,"axes":{"mirrorX":true},"mesh":{"winding":"auto"}})",
            R"({"version":1,"origin":{"mode":"bounds_center"}})",
            R"({"version":1,"units":{"scale":2},"origin":{"mode":"custom","offset":[3,-4,5]}})"
        };
        MeshCookResult transformed;
        Staging::ModelStagingData loose;
        uint64_t scaleHash=0,scaleSettingsHash=0;
        Bytes scalePayload;
        for(size_t i=0;i<sizeof(settings)/sizeof(settings[0]);++i)
        {
            Write(sidecar,Copy(settings[i]));
            assert(cook(transformed) && stage(loose));
            assert(transformed.bHasImportSettings && transformed.SourceHash!=baseline.SourceHash);
            SameGeometry(transformed,loose);
            if(i==0)
            {
                assert(loose.Vertices[1].Position[0]==2 && loose.Vertices[2].Position[1]==2);
                scaleHash=transformed.SourceHash; scaleSettingsHash=transformed.ImportSettingsHash; scalePayload=transformed.NvmeshBytes;
            }
            if(i==1)
            {
                assert(Near(loose.Vertices[2].Position[1],0.6f) && loose.Vertices[0].Position[1]==0);
                assert(Near(loose.Vertices[0].Position[0],-0.3f));
            }
        }
        Write(sidecar,Copy(R"({ "meta":{"note":"format only"},"units":{"scale":2},"version":1 })"));
        assert(cook(transformed) && transformed.SourceHash==scaleHash && transformed.ImportSettingsHash==scaleSettingsHash);
        assert(transformed.NvmeshBytes.size()==scalePayload.size() &&
            std::memcmp(transformed.NvmeshBytes.data(),scalePayload.data(),scalePayload.size())==0);
        Import::ImportSettingsFileOptions overrideSettings; overrideSettings.OverridePath=directory.Path/"override.import.json";
        Write(overrideSettings.OverridePath,Copy(R"({"version":1,"units":{"scale":3}})"));
        assert(cook(transformed,&overrideSettings) && stage(loose,&overrideSettings));
        assert(loose.Vertices[1].Position[0]==3 && transformed.SourceHash!=scaleHash);
        SameGeometry(transformed,loose);

        const uint64_t beforeHash=transformed.SourceHash;
        const auto* beforeVertices=loose.Vertices.data();
        const Bytes beforePayload=transformed.NvmeshBytes;
        for(const char* invalid:{R"({"version":1,"origin":{"mode":"surface_centroid"}})",
            R"({"version":1,"units":{"scale":-1}})",
            R"({"version":1,"units":{"fit":{"axis":"forward","meters":1}}})",
            R"({"version":1,"units":{"scale":1e38}})"})
        {
            Write(sidecar,Copy(invalid));
            assert(!cook(transformed) && !stage(loose));
            assert(transformed.SourceHash==beforeHash && loose.Vertices.data()==beforeVertices);
            assert(transformed.NvmeshBytes.size()==beforePayload.size() &&
                std::memcmp(transformed.NvmeshBytes.data(),beforePayload.data(),beforePayload.size())==0);
        }
        // hash計算成功はgeometryのcook可能性を保証しない（巨大scaleは焼込時に拒否）。
        Write(sidecar,Copy(R"({"version":1,"units":{"scale":1e308}})"));
        ModelCookFingerprint fingerprint;
        assert(FingerprintModelCookSource(source.data(),source.size(),"nvmesh.v0.mesh3d.pnt.u32.clustered",
            cookPath,"Models/model",fingerprint,error));
        assert(!cook(transformed));
        const auto previousFingerprint=fingerprint;
        Write(sidecar,Copy("invalid json"));
        assert(!FingerprintModelCookSource(source.data(),source.size(),"nvmesh.v0.mesh3d.pnt.u32.clustered",
            cookPath,"Models/model",fingerprint,error));
        assert(fingerprint.SourceHash==previousFingerprint.SourceHash &&
            fingerprint.ImportSettingsHash==previousFingerprint.ImportSettingsHash &&
            fingerprint.ImportSettingsPath==previousFingerprint.ImportSettingsPath);
        assert(std::filesystem::remove(sidecar));
        Import::ImportSettingsFileOptions required; required.bRequired=true;
        assert(!cook(transformed,&required) && !stage(loose,&required));
        assert(transformed.SourceHash==beforeHash && loose.Vertices.data()==beforeVertices);
        MeshCookResult restored;
        assert(cook(restored) && restored.SourceHash==baseline.SourceHash);
        SamePayload(baseline,restored);
    }
    std::cout << "ImportStaticIntegrationTest PASS: gltf_glb_sidecar_cook_loose_hash_options_failure_preservation\n";
    return 0;
}
