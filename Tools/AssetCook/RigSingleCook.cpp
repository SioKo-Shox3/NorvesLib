#include "RigSingleCook.h"
#include "RigClipBankCook.h"
#include "CookRigPayload.h"
#include "Resource/RigGltfImportCapture.h"
#include "CookDependencySnapshot.h"
#include "CookOutputSetGuard.h"
#include "CookOutputPaths.h"
#include "AssetCookLegacyOptions.h"
#include "AssetCookOutput.h"
#include <charconv>
#include <fstream>
#include <cmath>
namespace NorvesLib::Tools::AssetCook::Detail
{
    namespace C = Core::Container;
    namespace A = Core::Asset;
    namespace S = Core::Skeletal;
    namespace I = Core::AssetImport;
    namespace
    {
        bool Fail(C::AnsiString& error, const char* reason)
        {
            error = reason;
            return false;
        }
        bool Read(const std::filesystem::path& path, C::VariableArray<uint8_t>& out)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            const auto size = input.tellg();
            if (!input || size <= 0 || uint64_t(size) > S::RigV1Limits{}.MaxSourceBytes)
            {
                return false;
            }
            out.resize(size_t(size));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
            return bool(input) && input.peek() == std::char_traits<char>::eof();
        }
    } // namespace
    bool IsRigSingleFormat(C::AnsiStringView format)
    {
        return format == C::AnsiStringView("nvskel.v1.skinmesh.pnujiw.u32") ||
               format == C::AnsiStringView("nvskel.v1.clips");
    }
    bool NormalizeRigSingleRequest(const SingleAssetCookRequest& request, SingleAssetCookRequest& out,
                                   C::AnsiString& error)
    {
        const bool bMesh = request.Format == "nvskel.v1.skinmesh.pnujiw.u32";
        if (!IsRigSingleFormat(request.Format) || request.Kind != (bMesh ? "model" : "animation") ||
            request.EntryTypeText != (bMesh ? "Skm1" : "Anm1") || HasSkeletalRoleFileRequest(request.RoleProfile) ||
            (bMesh && !request.ClipJointNodes.empty()) || request.ClipJointNodes.size() > 256 ||
            (request.AssetSetEmission.Present &&
             (!std::isfinite(request.AssetSetEmission.NitsPerUnit) || request.AssetSetEmission.NitsPerUnit <= 0)))
        {
            return Fail(error, "rig_single_profile");
        }
        // path/文字列/sidecarの既存正規化は、旧model profileに写して共有する。
        auto legacy = request;
        legacy.Kind = "model";
        legacy.Format = "nvskel.v0.skinned.pnujiw.u32";
        legacy.EntryTypeText = "Skl0";
        SingleAssetCookRequest normalized;
        if (!NormalizeCacheCookRequest(legacy, normalized, error))
        {
            return false;
        }
        normalized.Kind = request.Kind;
        normalized.Format = request.Format;
        normalized.EntryTypeText = request.EntryTypeText;
        normalized.AssetSetEmission = request.AssetSetEmission;
        normalized.ClipJointNodes = request.ClipJointNodes;
        out = std::move(normalized);
        return true;
    }
    bool BuildRigSingleOutputs(const SingleAssetCookRequest& request, C::Span<const uint8_t> source,
                               bool bInventoryOnly, C::VariableArray<RigSplitCookEntry>& out, C::AnsiString& error)
    {
        C::VariableArray<RigSplitCookEntry> entries;
        C::VariableArray<MeshEmbeddedImage> images;
        uint64_t hash = 0;
        S::RigV1Limits limits;
        limits.MaxJoints = 256;
        constexpr auto profile = S::RigImportProfile::StaticRootFrame256;
        I::ImportSettingsFileOptions options{request.ImportSettingsOverridePath, request.bNoSidecar,
                                             request.bRequireSidecar};
        S::RigV1Report report;
        if (request.Kind == "model")
        {
            RigSplitCookRequest split;
            split.SourcePath = request.InputPath;
            split.MeshPath = request.LogicalPath;
            split.SkeletonPath = request.LogicalPath + ".skeleton";
            split.BankPath = request.LogicalPath + ".clips";
            split.Variant = request.Variant;
            split.Profile = profile;
            split.Limits = limits;
            split.ImportOptions = options;
            split.AssetSetEmission = request.AssetSetEmission;
            split.DecodeOptions = request.SkeletalDecode;
            split.bAnalyzeClips = true;
            split.bInventoryOnly = bInventoryOnly;
            RigSplitCookResult result;
            if (!CookRigSplitV1NativePath(source, split, result, report, error))
            {
                return false;
            }
            hash = result.SourceHash;
            images = std::move(result.TexturePlans);
            // primaryを常に先頭にし、derived roleと画像を安定順に並べる。
            entries.push_back(std::move(result.Mesh));
            entries.push_back(std::move(result.Skeleton));
            entries.push_back(std::move(result.Bank));
            const C::AnsiString paths[] = {split.MeshPath, split.SkeletonPath, split.BankPath};
            const char* formats[] = {"nvskel.v1.skinmesh.pnujiw.u32", "nvskel.v1.skeleton", "nvskel.v1.clips"};
            const A::AssetKind kinds[] = {A::AssetKind::Model, A::AssetKind::Skeleton, A::AssetKind::Animation};
            const A::AssetPackageFourCC types[] = {A::MakeAssetPackageFourCC('S', 'k', 'm', '1'),
                                                   A::MakeAssetPackageFourCC('S', 'k', 'e', '1'),
                                                   A::MakeAssetPackageFourCC('A', 'n', 'm', '1')};
            const char* suffixes[] = {"", ".skeleton.nvpk", ".clips.nvpk"};
            for (size_t i = 0; i < 3; ++i)
            {
                auto& r = entries[i].Reference;
                r.LogicalPath = paths[i];
                r.Format = formats[i];
                r.Kind = kinds[i];
                r.EntryType = types[i];
                r.EntryName = i == 0 ? request.EntryName : C::AnsiString(i == 1 ? "__skeleton__" : "__clips__");
                auto package = request.PackagePath;
                package += suffixes[i];
                if (!MakeCachePackagePath(package, request.ManifestPath.parent_path(), r.CookedPackage, error))
                {
                    return false;
                }
            }
        }
        else
        {
            CookDependencySnapshot dependencies;
            if (!CaptureCookDependencySnapshot(request, 1, dependencies, error))
            {
                return false;
            }
            const auto sameBytes = [](const CookDependencyFile& file, C::Span<const uint8_t> bytes)
            {
                return file.bPresent && file.Size == bytes.size() &&
                       file.ContentHash == A::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size());
            };
            if (dependencies.Files.empty() || dependencies.Files[0].Role != CookDependencyRole::Source ||
                !sameBytes(dependencies.Files[0], source))
            {
                return Fail(error, "clip_source_snapshot_mismatch");
            }
            hash = dependencies.Fingerprint;
            RigSplitCookEntry entry;
            auto& r = entry.Reference;
            r.LogicalPath = request.LogicalPath;
            r.Format = request.Format;
            r.Kind = A::AssetKind::Animation;
            r.EntryName = request.EntryName;
            r.EntryType = A::MakeAssetPackageFourCC('A', 'n', 'm', '1');
            if (!MakeCachePackagePath(request.PackagePath, request.ManifestPath.parent_path(), r.CookedPackage, error))
            {
                return false;
            }
            if (!bInventoryOnly)
            {
                I::LoadedImportSettingsDocument document;
                if (I::LoadImportSettingsDocument(request.InputPath, options, document).Result !=
                    I::SettingsFileResult::Success)
                {
                    return Fail(error, "clip_sidecar");
                }
                I::LoadedImportSettings geometry;
                geometry.Settings = document.Settings.Geometry;
                geometry.Path = document.Path;
                geometry.bPresent = document.bPresent;
                S::RigClipSourceSelection selection{request.ClipJointNodes};
                S::RigClipAnalysisOptions analysis;
                const auto sidecarPath =
                    document.Path.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(document.Path);
                bool bSidecarMatched = document.Path.empty() && !document.bPresent;
                for (const auto& file : dependencies.Files)
                {
                    if (file.Role == CookDependencyRole::Sidecar && file.Path == sidecarPath &&
                        file.bPresent == document.bPresent &&
                        (!document.bPresent || sameBytes(file, document.RawSourceBytes)))
                    {
                        bSidecarMatched = true;
                    }
                }
                if (!bSidecarMatched)
                {
                    return Fail(error, "clip_sidecar_snapshot_mismatch");
                }
                S::RigGltfImportCapture capture;
                RigClipBankCookResult cooked;
                if (!CookRigClipBankV1NativePath(source, request.InputPath, request.Format, cooked, report, limits,
                                                 &geometry, &request.SkeletalDecode, profile, &selection, &analysis,
                                                 &capture))
                {
                    return Fail(error, "clip_source_cook");
                }
                for (size_t i = 0; i < capture.Buffers.GetCount(); ++i)
                {
                    if (capture.Buffers.GetSourceKind(i) != Core::Gltf::BufferStorageKind::ExternalFile)
                    {
                        continue;
                    }
                    bool bMatched = false;
                    if (i < capture.SourceCanonicalFiles.size())
                    {
                        for (const auto& file : dependencies.Files)
                        {
                            if (file.Role == CookDependencyRole::ExternalBuffer &&
                                file.Path == capture.SourceCanonicalFiles[i] &&
                                sameBytes(file, capture.Buffers.GetSourceBytes(i)))
                            {
                                bMatched = true;
                            }
                        }
                    }
                    if (!bMatched)
                    {
                        return Fail(error, "clip_buffer_snapshot_mismatch");
                    }
                }
                CookDependencySnapshot after;
                if (!CaptureCookDependencySnapshot(request, 1, after, error) ||
                    after.Fingerprint != dependencies.Fingerprint)
                {
                    return Fail(error, "clip_dependencies_changed");
                }
                entry.Payload = std::move(cooked.Bytes);
                r.bHasRigSplitMetadata = true;
                if (!InspectCookedRigPayload(r.Format, entry.Payload, 3, r.RigSplitMetadata, error))
                {
                    return false;
                }
            }
            entries.push_back(std::move(entry));
        }
        // 既存package codecへのerror引渡しだけstd文字列を使う。
        std::string codecError;
        for (auto& entry : entries)
        {
            auto& r = entry.Reference;
            r.SourceHash = hash;
            r.SourceHashHex = A::FormatAssetHashHex(hash);
            r.Variant = request.Variant;
            r.CookedVersion = 1;
            r.EntryTypeText = A::FormatAssetPackageFourCCText(r.EntryType);
            if (!bInventoryOnly)
            {
                if (!BuildSingleSkeletalEntryPackage(r.EntryName, r.EntryType, entry.Payload, entry.Package,
                                                     r.CookedHash, codecError))
                {
                    return Fail(error, codecError.c_str());
                }
                r.CookedHashHex = A::FormatAssetHashHex(r.CookedHash);
            }
        }
        for (const auto& image : images)
        {
            RigSplitCookEntry entry;
            auto& r = entry.Reference;
            r.LogicalPath = image.LogicalPath;
            r.Kind = A::AssetKind::Texture;
            r.Variant = "default";
            r.Format = image.Format;
            r.SourceHash = image.SourceHash;
            r.SourceHashHex = A::FormatAssetHashHex(r.SourceHash);
            r.EntryName = "__texture__";
            r.EntryType = A::MakeAssetPackageFourCC('T', 'e', 'x', '0');
            r.EntryTypeText = "Tex0";
            char number[32];
            const auto end = std::to_chars(number, number + sizeof(number), image.ImageIndex);
            if (end.ec != std::errc{})
            {
                return Fail(error, "rig_image_index");
            }
            auto package = request.PackagePath;
            package += ".img";
            package += C::AnsiString(number, size_t(end.ptr - number)).c_str();
            package += ".nvpkg";
            if (!MakeCachePackagePath(package, request.ManifestPath.parent_path(), r.CookedPackage, error))
            {
                return false;
            }
            if (!bInventoryOnly)
            {
                TextureCookResult texture;
                DecodedTextureRgba8 decoded;
                if (image.Payload == MeshImagePayload::RawRgba8)
                {
                    if (!CookRgba8ToNvtex(image.GetBytes(), image.Width, image.Height, image.Format, texture, error))
                    {
                        return false;
                    }
                }
                else if (!DecodeRigSplitImage(image.GetBytes(), {}, decoded, error) ||
                         !CookRgba8ToNvtex(decoded.Pixels, decoded.Width, decoded.Height, image.Format, texture, error))
                {
                    return false;
                }
                entry.Payload = std::move(texture.NvtexBytes);
                if (!BuildSingleSkeletalEntryPackage(r.EntryName, r.EntryType, entry.Payload, entry.Package,
                                                     r.CookedHash, codecError))
                {
                    return Fail(error, codecError.c_str());
                }
                r.CookedHashHex = A::FormatAssetHashHex(r.CookedHash);
            }
            entries.push_back(std::move(entry));
        }
        out = std::move(entries);
        return true;
    }
    bool CookRigSingleAsset(const SingleAssetCookRequest& request, C::AnsiString& error)
    {
        SingleAssetCookRequest normalized;
        CookPreparedPlan plan;
        if (!NormalizeRigSingleRequest(request, normalized, error) ||
            !PrepareCookOutputPlan(normalized, 1, nullptr, plan, error) ||
            !ValidateCookOutputSet({&plan, 1}, {}, error))
        {
            return false;
        }
        C::VariableArray<uint8_t> source;
        C::VariableArray<RigSplitCookEntry> entries;
        if (!Read(normalized.InputPath, source) || !BuildRigSingleOutputs(normalized, source, false, entries, error))
        {
            return false;
        }
        CookDependencySnapshot after;
        if (!CaptureCookDependencySnapshot(normalized, 1, after, error) ||
            after.Fingerprint != plan.Context.Dependencies.Fingerprint)
        {
            return Fail(error, "rig_source_changed");
        }
        C::VariableArray<A::AssetCookedReference> references;
        for (const auto& entry : entries)
        {
            references.push_back(entry.Reference);
        }
        C::AnsiString manifest;
        std::string codecError;
        if (!SerializeCookedManifestReferences(references, manifest, codecError))
        {
            return Fail(error, codecError.c_str());
        }
        for (const auto& entry : entries)
        {
            const auto path = normalized.ManifestPath.parent_path() / entry.Reference.CookedPackage.c_str();
            if (!WriteSkeletalBinaryFile(path, entry.Package, codecError))
            {
                return Fail(error, codecError.c_str());
            }
        }
        return WriteSkeletalTextFile(normalized.ManifestPath, manifest, codecError) || Fail(error, codecError.c_str());
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
