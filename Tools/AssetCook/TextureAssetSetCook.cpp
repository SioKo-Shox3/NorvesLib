#include "TextureAssetSetCook.h"
#include "TextureAssetSetSpec.h"
#include "TextureAssetSetOutput.h"
#include "SingleAssetCook.h"
#include "Text/JsonDocument.h"
#include "Asset/AssetSystem.h"
#include "Asset/CookedTextureFormat.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Core::Container::AnsiString;
        using Core::Container::AnsiStringView;
        using Core::Container::VariableArray;
        using Core::Asset::AssetCookedReference;
        bool Equal(AnsiStringView a, AnsiStringView b)
        { return a.size()==b.size() && (a.empty() || std::memcmp(a.data(),b.data(),a.size())==0); }
        bool Fail(AnsiString& error, const char* reason) { error="texture_asset_set: ";error.append(reason);return false; }
        bool Read(const std::filesystem::path& path, VariableArray<uint8_t>& bytes)
        {
            std::ifstream file(path,std::ios::binary|std::ios::ate);
            const auto size=file.tellg();
            if (!file || size<0 || static_cast<uintmax_t>(size)>std::numeric_limits<size_t>::max() ||
                static_cast<uintmax_t>(size)>static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) return false;
            bytes.resize(static_cast<size_t>(size));file.seekg(0);
            if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            return file && file.peek()==std::char_traits<char>::eof() && !file.bad();
        }
        bool Write(const std::filesystem::path& path, AnsiStringView text)
        {
            if (text.size()>static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) return false;
            std::ofstream file(path,std::ios::binary|std::ios::trunc);
            if (!file) return false;
            file.write(text.data(),static_cast<std::streamsize>(text.size()));file.flush();
            if (!file) return false;
            file.close();return !file.fail();
        }
        // 現AssetSystemのpath境界はANSI。新規出力rootはASCIIに限定し、黙った文字変換を避ける。
        bool AsciiPath(const std::filesystem::path& path, AnsiString& out)
        {
            out.clear();
            for (const auto unit:path.native())
            {
                if (unit<32 || unit>=127) return false;
                out.push_back(unit=='\\'?'/':static_cast<char>(unit));
            }
            return true;
        }
        bool LoadManifest(const std::filesystem::path& path, Core::Asset::AssetManifest& manifest, AnsiString& text)
        {
            VariableArray<uint8_t> bytes;
            if (!Read(path,bytes)) return false;
            Core::Container::String native;text.clear();
            for (const auto byte:bytes)
            {
                if (byte>=128 || byte==0) return false;
                native.push_back(static_cast<Core::Container::String::value_type>(byte));text.push_back(static_cast<char>(byte));
            }
            AnsiString pathText;
            return AsciiPath(path,pathText) && manifest.LoadFromJsonText(native,pathText);
        }
        bool SafeOutputName(AnsiStringView name)
        {
            if (name.empty()) return false;
            size_t segment=0;
            for (size_t i=0;i<=name.size();++i)
            {
                if (i<name.size())
                {
                    const unsigned char c=name[i];
                    if (c<32 || c>=127 || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') return false;
                    if (c!='/') continue;
                }
                if (i==segment || name[i-1]=='.' || name[i-1]==' ') return false;
                AnsiString stem;
                for (size_t j=segment;j<i && name[j]!='.';++j)
                { const char c=name[j];stem.push_back(c>='a' && c<='z'?static_cast<char>(c-'a'+'A'):c); }
                if ((!stem.empty() && stem.back()==' ') || Equal(stem,"CON") || Equal(stem,"CONIN$") || Equal(stem,"CONOUT$") ||
                    Equal(stem,"PRN") || Equal(stem,"AUX") || Equal(stem,"NUL") || Equal(stem,"CLOCK$") ||
                    (stem.size()==4 && (std::memcmp(stem.data(),"COM",3)==0 || std::memcmp(stem.data(),"LPT",3)==0) && stem[3]>='1' && stem[3]<='9')) return false;
                segment=i+1;
            }
            return true;
        }
        AnsiString Lower(AnsiStringView text)
        {
            AnsiString out;
            for (const char c:text) out.push_back(c>='A' && c<='Z'?static_cast<char>(c-'A'+'a'):c);
            return out;
        }
#if defined(_WIN32)
        bool Absent(const std::filesystem::path& path)
        {
            if (GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) return false;
            const auto code=GetLastError();return code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND;
        }
        bool NoReparse(const std::filesystem::path& path)
        {
            std::filesystem::path prefix=path.root_path();
            const auto rootAttributes=GetFileAttributesW(prefix.c_str());
            if (rootAttributes==INVALID_FILE_ATTRIBUTES || (rootAttributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0) return false;
            for (const auto& part:path.relative_path())
            {
                prefix/=part;
                const auto attr=GetFileAttributesW(prefix.c_str());
                if (attr==INVALID_FILE_ATTRIBUTES)
                {
                    const auto code=GetLastError();return code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND;
                }
                if ((attr&FILE_ATTRIBUTE_REPARSE_POINT)!=0) return false;
            }
            return true;
        }
        bool LocalDrivePath(const std::filesystem::path& path)
        {
            const auto rootPath=path.root_name();
            const auto& root=rootPath.native();
            if (!path.is_absolute() || root.size()!=2 || root[1]!=':' ||
                !((root[0]>='A' && root[0]<='Z') || (root[0]>='a' && root[0]<='z'))) return false;
            const auto type=GetDriveTypeW(path.root_path().c_str());
            return type==DRIVE_FIXED || type==DRIVE_REMOVABLE || type==DRIVE_CDROM || type==DRIVE_RAMDISK;
        }
        bool SamePath(const std::filesystem::path& a,const std::filesystem::path& b)
        { return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL; }
        struct StageOwner
        {
            std::filesystem::path Path;
            bool Active=false;
            AnsiString* Error=nullptr;
            ~StageOwner()
            {
                if (!Active) return;
                std::error_code code;std::filesystem::remove_all(Path,code);
                if (code)
                {
                    AnsiString location;AsciiPath(Path,location);
                    if (Error) { Error->append("; staging cleanup failed: ");Error->append(location); }
                    std::fprintf(stderr,"texture_asset_set: staging cleanup failed: %s\n",location.c_str());
                }
            }
        };
        bool CreateStage(const std::filesystem::path& parent,const std::filesystem::path& destination,StageOwner& owner)
        {
            for (uint32_t attempt=0;attempt<100;++attempt)
            {
                AnsiString name=".assetcook-stage-";char digits[32];
                for (const uint64_t value:{static_cast<uint64_t>(GetCurrentProcessId()),GetTickCount64(),static_cast<uint64_t>(attempt)})
                { const auto converted=std::to_chars(digits,digits+sizeof(digits),value);name.append(digits,static_cast<size_t>(converted.ptr-digits));name.push_back('-'); }
                owner.Path=parent/std::filesystem::path(name.c_str());
                if (SamePath(owner.Path,destination)) continue;
                std::error_code code;
                if (std::filesystem::create_directory(owner.Path,code)) { owner.Active=true;return true; }
                if (code && code!=std::errc::file_exists) return false;
            }
            return false;
        }
#endif
        bool ValidateReference(const AssetCookedReference& row,const TextureAssetSetEntry& entry,AnsiStringView package)
        {
            return row.Kind==Core::Asset::AssetKind::Texture && row.CookedVersion==0 && !row.bHasSkeletalMetadata &&
                row.EntryType==Core::Asset::MakeAssetPackageFourCC('T','e','x','0') && Equal(row.LogicalPath,entry.LogicalPath) &&
                Equal(row.Variant,entry.Variant) && Equal(row.Format,entry.Format) && Equal(row.EntryName,entry.EntryName) &&
                Equal(row.CookedPackage,package);
        }
        bool CookImpl(const TextureAssetSetCookRequest& request,AnsiString& error)
        {
#if !defined(_WIN32)
            (void)request;return Fail(error,"publication requires Windows");
#else
            if (request.SpecPath.empty() || request.RuntimeRoot.empty()) return Fail(error,"--asset-set and --runtime-root are required");
            const auto cwd=std::filesystem::current_path();
            const auto absolute=[&](const std::filesystem::path& path)
            { return (path.is_absolute()?path:cwd/path).lexically_normal(); };
            for (const auto* path:{&request.SpecPath,&request.SourceRoot,&request.RuntimeRoot,&request.ManifestPath})
                if (!path->empty() && path->has_root_path() && !path->is_absolute()) return Fail(error,"drive-relative/root-relative paths are unsupported");
            const auto specPath=absolute(request.SpecPath);
            const auto sourceRoot=request.SourceRoot.empty()?cwd:absolute(request.SourceRoot);
            const auto runtime=absolute(request.RuntimeRoot);
            const auto parent=runtime.parent_path();
            const auto manifest=request.ManifestPath.empty()?runtime/"manifest.json":absolute(request.ManifestPath);
            if (!LocalDrivePath(specPath) || !LocalDrivePath(sourceRoot) || !LocalDrivePath(runtime) || !LocalDrivePath(manifest))
                return Fail(error,"texture v1 requires local drive paths");
            AnsiString runtimeText,manifestName;
            if (!AsciiPath(runtime,runtimeText) || !AsciiPath(manifest.filename(),manifestName)) return Fail(error,"output paths require ASCII in texture v1");
            if (!SamePath(manifest.parent_path(),runtime) || !SafeOutputName(manifestName)) return Fail(error,"manifest must be a safe filename directly under runtime root");
            AnsiString runtimeName;AsciiPath(runtime.filename(),runtimeName);
            if (!SafeOutputName(runtimeName) || !Absent(runtime)) return Fail(error,"runtime root must be absent");
            if (!std::filesystem::is_directory(parent) || !std::filesystem::is_directory(sourceRoot) || !NoReparse(parent) ||
                !NoReparse(specPath) || !NoReparse(sourceRoot)) return Fail(error,"root/spec directory missing or reparse path rejected");
            VariableArray<uint8_t> specBytes;
            if (!Read(specPath,specBytes) || specBytes.empty()) return Fail(error,"spec read failed");
            size_t bom=specBytes.size()>=3 && specBytes[0]==0xef && specBytes[1]==0xbb && specBytes[2]==0xbf?3:0;
            Core::JsonDocument document;
            if (!Core::JsonDocument::TryParseUtf8({specBytes.data()+bom,specBytes.size()-bom},document)) return Fail(error,"invalid spec JSON");
            TextureAssetSetSpec spec;
            const auto parsed=ParseTextureAssetSetSpec(document.GetRoot(),spec);
            if (!parsed.Succeeded()) { error="texture_asset_set: invalid spec field ";error.append(parsed.Field);return false; }
            document.Reset();
            VariableArray<std::filesystem::path> sources;
            VariableArray<AnsiString> packages,outputs;
            outputs.push_back(Lower(manifestName));
            for (const auto& entry:spec.Textures)
            {
                if (!Equal(entry.Format,"nvtex.v0.rgba8.srgb") && !Equal(entry.Format,"nvtex.v0.rgba8.linear") &&
                    !Equal(entry.Format,"nvtex.v0.r8.linear") && !Equal(entry.Format,"nvtex.v0.rg8.linear")) return Fail(error,"unsupported texture format");
                auto source=std::filesystem::u8path(entry.SourcePath.begin(),entry.SourcePath.end());
                if (source.has_root_path() && !source.is_absolute()) return Fail(error,"drive-relative/root-relative source path rejected");
                source=(source.is_absolute()?source:sourceRoot/source).lexically_normal();
                if (!LocalDrivePath(source) || !NoReparse(source) || !std::filesystem::is_regular_file(source)) { error="texture_asset_set: source missing or reparse path: ";error.append(entry.SourcePath);return false; }
                sources.push_back(source);
                AnsiString package=spec.PackageRoot;package.push_back('/');package.append(entry.PackageName);
                if (!SafeOutputName(package)) return Fail(error,"unsafe Windows package name");
                packages.push_back(package);outputs.push_back(Lower(package));
            }
            std::sort(outputs.begin(),outputs.end(),[](const AnsiString& a,const AnsiString& b){ return a.compare(b)<0; });
            for (size_t i=0;i<outputs.size();++i)
            {
                if (i!=0 && Equal(outputs[i-1],outputs[i])) return Fail(error,"duplicate output path");
                for (size_t position=0;position<outputs[i].size();++position)
                {
                    if (outputs[i][position]!='/') continue;
                    AnsiString prefix;prefix.append(outputs[i].data(),position);
                    const auto found=std::lower_bound(outputs.begin(),outputs.end(),prefix,[](const AnsiString& a,const AnsiString& b){ return a.compare(b)<0; });
                    if (found!=outputs.end() && Equal(*found,prefix)) return Fail(error,"output file/directory collision");
                }
            }
            StageOwner stage;stage.Error=&error;
            if (!CreateStage(parent,runtime,stage)) return Fail(error,"exclusive staging directory creation failed");
            const auto stagedManifest=stage.Path/manifest.filename();
            VariableArray<AssetCookedReference> references;
            for (size_t i=0;i<spec.Textures.size();++i)
            {
                const auto& entry=spec.Textures[i];
                SingleAssetCookRequest single;
                single.InputPath=sources[i];single.PackagePath=stage.Path/std::filesystem::path(packages[i].c_str());single.ManifestPath=stagedManifest;
                single.LogicalPath=entry.LogicalPath;single.Kind="texture";single.EntryName=entry.EntryName;single.EntryTypeText="Tex0";
                single.Format=entry.Format;single.Variant=entry.Variant;
                AnsiString reason;
                if (!CookSingleAsset(single,reason)) { error="texture_asset_set: cook failed for ";error.append(entry.SourcePath);error.append(": ");error.append(reason);return false; }
                Core::Asset::AssetManifest cooked;AnsiString text;
                if (!LoadManifest(stagedManifest,cooked,text) || cooked.GetReferenceCount()!=1 || !ValidateReference(cooked.GetReference(0),entry,packages[i]))
                    return Fail(error,"single texture manifest verification failed");
                references.push_back(cooked.GetReference(0));
            }
            AnsiString aggregate;
            if (!Detail::SerializeLegacyTextureManifest({references.data(),references.size()},aggregate,error) || !Write(stagedManifest,aggregate))
            { if (error.empty()) Fail(error,"aggregate write failed");return false; }
            Core::Asset::AssetManifest check;AnsiString reread;
            if (!LoadManifest(stagedManifest,check,reread) || !Equal(aggregate,reread) || check.GetReferenceCount()!=references.size()) return Fail(error,"aggregate disk verification failed");
            AnsiString stageText;AsciiPath(stage.Path,stageText);
            Core::Asset::AssetSystem system(stageText);system.SetManifest(check);
            for (size_t i=0;i<references.size();++i)
            {
                if (!ValidateReference(check.GetReference(i),spec.Textures[i],packages[i])) return Fail(error,"aggregate reference changed");
                const auto resolved=system.ResolveAsset(spec.Textures[i].LogicalPath,Core::Asset::AssetKind::Texture,spec.Textures[i].Variant);
                if (!resolved.UsedCooked() || !Core::Asset::ParseCookedTexture(resolved.Blob).Succeeded()) return Fail(error,"aggregate cooked-only resolve failed");
            }
            size_t fileCount=0;
            for (const auto& item:std::filesystem::recursive_directory_iterator(stage.Path))
            {
                if (!NoReparse(item.path())) return Fail(error,"reparse entry in staging");
                if (item.is_regular_file()) ++fileCount;
                else if (!item.is_directory()) return Fail(error,"unexpected staging entry");
            }
            if (fileCount!=references.size()+1 || !NoReparse(parent)) return Fail(error,"staging inventory or parent changed");
            if (!Detail::PublishNewTextureAssetSet(stage.Path,runtime,error)) return false;
            stage.Active=false;error.clear();return true;
#endif
        }
    }
    bool CookTextureAssetSet(const TextureAssetSetCookRequest& request,AnsiString& error)
    {
        error.clear();
        try { return CookImpl(request,error); }
        catch (const std::exception& exception)
        {
            const AnsiString details=error;
            error="texture_asset_set: ";error.append(exception.what());
            if (!details.empty()) { error.append("; ");error.append(details); }
            return false;
        }
    }
}
