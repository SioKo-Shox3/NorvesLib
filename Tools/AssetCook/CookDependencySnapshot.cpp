#include "CookDependencySnapshot.h"
#include "RigSingleCook.h"
#include "RigRetargetCook.h"
#include <bit>
#include "SkeletalRoleFileInput.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Resource/GltfBufferFile.h"
#include "Resource/GltfBufferJson.h"
#include "Resource/GltfImageSource.h"
#include "Resource/ImportSettingsFile.h"
#include "Resource/SkeletalImportPolicy.h"
#include "Text/JsonDocument.h"
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Core::Container::AnsiString;
        using Core::Container::AnsiStringView;
        using Core::Container::VariableArray;
        namespace Gltf=Core::Gltf;
        bool Equal(AnsiStringView a,const char* b)
        { return a.size()==std::strlen(b) && (a.empty() || std::memcmp(a.data(),b,a.size())==0); }
        bool Fail(AnsiString& error,const char* reason) { error="cook_dependencies: ";error.append(reason);return false; }
        struct FingerprintBuilder
        {
            uint64_t Value=Core::Asset::AssetPackageFormatV1::Fnv1a64OffsetBasis;
            void Byte(uint8_t byte) { Value^=byte;Value*=Core::Asset::AssetPackageFormatV1::Fnv1a64Prime; }
            void Integer(uint64_t value) { for (unsigned i=0;i<8;++i) Byte(static_cast<uint8_t>(value>>(i*8))); }
            void Text(AnsiStringView value) { Integer(value.size());for (const auto unit:value) Byte(static_cast<uint8_t>(unit)); }
            bool Path(const std::filesystem::path& path)
            {
                const auto& native=path.native();
                const Core::Container::Span<const std::filesystem::path::value_type> source{native.data(),native.size()};
                const auto size=Core::Asset::MeasureSkeletalNameEncoding(2,source);
                if (!size.Succeeded()) return false;
                VariableArray<uint8_t> encoded;encoded.resize(size.ByteCount);
                if (!Core::Asset::EncodeSkeletalWireName(2,source,{encoded.data(),encoded.size()}).Succeeded()) return false;
                Integer(encoded.size());for (auto byte:encoded)
                {
#if defined(_WIN32)
                    if (byte=='\\') byte='/';
#endif
                    Byte(byte);
                }
                return true;
            }
        };
        bool Read(const std::filesystem::path& path, VariableArray<uint8_t>& bytes, size_t maximum = SIZE_MAX)
        {
            if (!std::filesystem::is_regular_file(path)) return false;
            std::ifstream file(path,std::ios::binary|std::ios::ate);const auto length=file.tellg();
            if (!file || length < 0 || static_cast<uintmax_t>(length) > maximum ||
                static_cast<uintmax_t>(length) > std::numeric_limits<size_t>::max() ||
                static_cast<uintmax_t>(length) > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return false;
            }
            bytes.resize(static_cast<size_t>(length));file.seekg(0);
            if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            return file && file.peek()==std::char_traits<char>::eof() && !file.bad();
        }
        void Add(CookDependencySnapshot& snapshot,const std::filesystem::path& path,CookDependencyRole role,
            Core::Container::Span<const uint8_t> bytes,bool present=true)
        {
            CookDependencyFile record;
            record.Path=path;record.Role=role;record.bPresent=present;
            record.Size=bytes.size();record.ContentHash=present?Core::Asset::ComputeAssetPackagePayloadHash(bytes.data(),bytes.size()):0;
            snapshot.Files.push_back(std::move(record));
        }
        struct ReaderContext
        {
            Gltf::BufferFileContext File;
            CookDependencySnapshot* Snapshot=nullptr;
            CookDependencyRole Role=CookDependencyRole::ExternalBuffer;
        };
        Gltf::ExternalBufferReadResult ReadDependency(Core::Container::Span<const uint8_t> uri,VariableArray<uint8_t>& bytes,void* context)
        {
            auto& reader=*static_cast<ReaderContext*>(context);std::filesystem::path resolved;
            const auto result=Gltf::ReadBufferFileWithPath(uri,bytes,&reader.File,&resolved);
            if (result==Gltf::ExternalBufferReadResult::Success) Add(*reader.Snapshot,resolved,reader.Role,{bytes.data(),bytes.size()});
            return result;
        }
        bool Capture(const SingleAssetCookRequest& request,uint64_t revision,CookDependencySnapshot& out,AnsiString& error)
        {
            const bool retarget = Detail::HasRigRetarget(request);
            const bool model = (Equal(request.Kind, "model") || Equal(request.Kind, "animation")) &&
                               !Detail::IsBvhRetargetSource(request);
            const bool bRole = HasSkeletalRoleFileRequest(request.RoleProfile);
            if (bRole && !ValidateSkeletalRoleFileRequest(request, error)) return false;
            if (revision == 0 || request.InputPath.empty() ||
                (!model && !Equal(request.Kind, "raw") && !Equal(request.Kind, "texture") &&
                 !Equal(request.Kind, "audio") && !(retarget && Equal(request.Kind, "animation"))))
            {
                return Fail(error, "invalid source, kind, or cooker revision");
            }
            if (!model && (request.bNoSidecar || request.bRequireSidecar || !request.ImportSettingsOverridePath.empty()))
                return Fail(error,"sidecar options require model kind");
            const auto source=std::filesystem::absolute(request.InputPath).lexically_normal();
            VariableArray<uint8_t> rootBytes;
            if (!Read(source, rootBytes,
                      Detail::IsRigSingleFormat(request.Format) ? Core::Skeletal::RigV1Limits{}.MaxSourceBytes
                                                                : SIZE_MAX))
            {
                return Fail(error, "source read failed");
            }
            CookDependencySnapshot candidate;candidate.CookerRevision=revision;
            Add(candidate,std::filesystem::weakly_canonical(source),CookDependencyRole::Source,{rootBytes.data(),rootBytes.size()});
            FingerprintBuilder hash;hash.Text("norves.cook-dependencies");hash.Integer(candidate.SchemaVersion);hash.Integer(revision);
            // locatorも含める。symlink先が同じでも相対URIのbaseが違えば別の要求である。
            if (!hash.Path(source)) return Fail(error,"source locator is not Unicode");
            hash.Text(request.Kind);hash.Text(request.LogicalPath);hash.Text(request.EntryName);hash.Text(request.EntryTypeText);
            hash.Text(request.Format);hash.Text(request.Variant);
            if (Detail::IsRigSingleFormat(request.Format))
            {
                hash.Text("rig-single.profile3.analysis1");
                hash.Integer(request.ClipJointNodes.size());
                for (uint32_t node : request.ClipJointNodes)
                {
                    hash.Integer(node);
                }
            }
            if (request.AssetSetEmission.Present)
            {
                hash.Text("asset-set.emissiveNitsPerUnit");
                hash.Integer(std::bit_cast<uint64_t>(request.AssetSetEmission.NitsPerUnit));
            }
            hash.Integer(request.bNoSidecar);hash.Integer(request.bRequireSidecar);
            hash.Integer(!request.ImportSettingsOverridePath.empty());
            if (model)
            {
                Core::AssetImport::ImportSettingsFileOptions options;
                options.OverridePath=request.ImportSettingsOverridePath;options.bDisabled=request.bNoSidecar;options.bRequired=request.bRequireSidecar;
                Core::AssetImport::LoadedImportSettings loaded;
                Core::AssetImport::SettingsFileOutcome result;
                if (request.Format == "nvmesh.v1.mesh3d.pnt.u32.clustered" || Detail::IsRigSingleFormat(request.Format))
                {
                    Core::AssetImport::LoadedImportSettingsDocument document;
                    result = Core::AssetImport::LoadImportSettingsDocument(source, options, document);
                    loaded.Path = std::move(document.Path);
                    loaded.bPresent = document.bPresent;
                    loaded.RawSourceBytes = std::move(document.RawSourceBytes);
                }
                else
                {
                    result = Core::AssetImport::LoadImportSettingsFile(source, options, loaded);
                }
                if (result.Result!=Core::AssetImport::SettingsFileResult::Success) return Fail(error,"sidecar validation/read failed");
                if (!loaded.Path.empty())
                {
                    const auto locator=std::filesystem::absolute(loaded.Path).lexically_normal();
                    if (!hash.Path(locator)) return Fail(error,"sidecar locator is not Unicode");
                    Add(candidate,std::filesystem::weakly_canonical(locator),CookDependencyRole::Sidecar,
                        {loaded.RawSourceBytes.data(),loaded.RawSourceBytes.size()},loaded.bPresent);
                }
                const auto policy=Core::Skeletal::EncodeSkeletalImportPolicy(request.SkeletalDecode);
                if (!policy.bValid) return Fail(error,"invalid skeletal options");
                hash.Integer(policy.Size);
                for (size_t i=0;i<policy.Size;++i) hash.Byte(policy.Bytes[i]);
                hash.Integer(Core::Skeletal::SkeletalReductionAlgorithmVersion);
                Gltf::ContainerView container;
                const auto parsed=Gltf::ParseContainer({rootBytes.data(),rootBytes.size()},container);
                if (parsed!=Gltf::ContainerParseResult::Success && parsed!=Gltf::ContainerParseResult::NotGlb) return Fail(error,"invalid glTF container");
                Core::JsonDocument document;
                if (!Core::JsonDocument::TryParseUtf8(container.Json,document) || !document.GetRoot().IsObject()) return Fail(error,"invalid glTF JSON");
                ReaderContext context{{source},&candidate,CookDependencyRole::ExternalBuffer};
                Gltf::BufferSet buffers;
                if (Gltf::ResolveJsonBuffers(document.GetRoot(),container,ReadDependency,&context,buffers).Result!=Gltf::BufferResolveResult::Success)
                    return Fail(error,"glTF buffer dependency resolution failed");
                size_t imageFields=0;
                for (size_t i=0;i<document.GetRoot().GetObjectSize();++i)
                {
                    const auto& key=document.GetRoot().GetMemberName(i);
                    bool imageKey=key.size()==6;
                    for (size_t j=0;j<key.size();++j)
                    {
                        if (key[j]==0) return Fail(error,"NUL in glTF root member name");
                        if (imageKey && key[j]!="images"[j]) imageKey=false;
                    }
                    if (imageKey) ++imageFields;
                }
                if (imageFields>1) return Fail(error,"duplicate glTF images field");
                const auto images=document.GetRoot().FindMember("images");
                if (images.IsValid() && !images.IsArray()) return Fail(error,"glTF images must be an array");
                for (size_t i=0;i<images.GetArraySize();++i)
                {
                    Gltf::ImageSource image;
                    if (Gltf::ImageSource::Resolve(document.GetRoot(),i,buffers,image)!=Gltf::ImageSourceResult::Success) return Fail(error,"invalid glTF image source");
                    if (image.GetKind()==Gltf::ImageSourceKind::ExternalFile)
                    {
                        context.Role=CookDependencyRole::ExternalImage;VariableArray<uint8_t> bytes;
                        if (ReadDependency(image.GetExternalUri(),bytes,&context)!=Gltf::ExternalBufferReadResult::Success)
                            return Fail(error,"glTF image dependency read failed");
                    }
                }
            }
            if (bRole)
            {
                SkeletalRoleFileInputs inputs;
                if (!LoadSkeletalRoleFileInputs(request.RoleProfile, inputs, error)) return false;
                const auto bvh = std::filesystem::absolute(request.RoleProfile.BvhPath).lexically_normal();
                const auto profile = std::filesystem::absolute(request.RoleProfile.ProfilePath).lexically_normal();
                if (!hash.Path(bvh) || !hash.Path(profile) ||
                    !AppendSkeletalRoleFileSettingsHash(hash.Value, request.RoleProfile, hash.Value, error)) return false;
                Add(candidate, std::filesystem::weakly_canonical(bvh), CookDependencyRole::Bvh,
                    {inputs.BvhBytes.data(), inputs.BvhBytes.size()});
                Add(candidate, std::filesystem::weakly_canonical(profile), CookDependencyRole::RoleProfile,
                    {inputs.ProfileBytes.data(), inputs.ProfileBytes.size()});
            }
            if (retarget)
            {
                if (request.RetargetSkeletonPath.empty() || request.RetargetProfilePath.empty() ||
                    request.Kind != "animation" || request.Format != "nvskel.v1.clips")
                {
                    return Fail(error, "retarget_request");
                }
                hash.Text("retarget.clipbank.v1.pipeline1");
                hash.Text(request.RetargetSourceClip);
                hash.Text(request.RetargetClipName);
                const auto targetPath = std::filesystem::absolute(request.RetargetSkeletonPath).lexically_normal();
                const auto profilePath = std::filesystem::absolute(request.RetargetProfilePath).lexically_normal();
                if (!hash.Path(targetPath) || !hash.Path(profilePath))
                {
                    return Fail(error, "retarget_path");
                }
                // 同じglTF依存採取をtarget骨格にも使い、外部buffer/sidecarの判定を分岐側へ複製しない。
                auto target = request;
                target.InputPath = targetPath;
                target.Kind = "model";
                target.EntryTypeText = "Skm1";
                target.Format = "nvskel.v1.skinmesh.pnujiw.u32";
                target.RetargetSkeletonPath.clear();
                target.RetargetProfilePath.clear();
                target.RetargetSourceClip.clear();
                target.RetargetClipName.clear();
                target.ImportSettingsOverridePath.clear();
                target.bNoSidecar = false;
                target.bRequireSidecar = false;
                target.ClipJointNodes.clear();
                CookDependencySnapshot targetDependencies;
                if (!Capture(target, revision, targetDependencies, error))
                {
                    return false;
                }
                hash.Integer(targetDependencies.Fingerprint);
                for (auto& file : targetDependencies.Files)
                {
                    if (file.Role == CookDependencyRole::Source)
                    {
                        file.Role = CookDependencyRole::TargetSkeleton;
                    }
                    candidate.Files.push_back(std::move(file));
                }
                if (!std::filesystem::is_regular_file(profilePath) ||
                    std::filesystem::file_size(profilePath) > 1024 * 1024)
                {
                    return Fail(error, "retarget_profile_size");
                }
                VariableArray<uint8_t> profileBytes;
                if (!Read(profilePath, profileBytes, 1024 * 1024))
                {
                    return Fail(error, "retarget_profile_read");
                }
                Add(candidate, std::filesystem::weakly_canonical(profilePath), CookDependencyRole::RoleProfile,
                    profileBytes);
            }
            hash.Integer(candidate.Files.size());
            for (const auto& file:candidate.Files)
            {
                hash.Integer(static_cast<uint64_t>(file.Role));hash.Integer(file.bPresent);
                if (!hash.Path(file.Path)) return Fail(error,"dependency path is not Unicode");
                hash.Integer(file.Size);hash.Integer(file.ContentHash);
            }
            candidate.Fingerprint=hash.Value;out=std::move(candidate);error.clear();return true;
        }
    }
    bool CaptureCookDependencySnapshot(const SingleAssetCookRequest& request,uint64_t cookerRevision,CookDependencySnapshot& out,AnsiString& error)
    {
        try { return Capture(request,cookerRevision,out,error); }
        catch (const std::exception& exception) { error="cook_dependencies: ";error.append(exception.what());return false; }
    }
}
