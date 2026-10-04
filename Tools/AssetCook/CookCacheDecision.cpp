#include "CookCacheDecision.h"
#include "CookCacheDecisionTestAccess.h"
#include "CookOutputPaths.h"
#include "AssetCookLegacyOptions.h"
#include "MeshCooker.h"
#include "Asset/AssetPath.h"
#include <charconv>
#include <cstring>
#include <fstream>
#include <exception>
#include <limits>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using namespace Core::Asset;
        using Core::Container::AnsiString;
        using Core::Container::AnsiStringView;
        using Core::Container::VariableArray;
        namespace Paths = Detail::CookOutputPaths;
        bool Equal(AnsiStringView a, AnsiStringView b)
        {
            return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
        }
        bool Fail(AnsiString& error, const char* code)
        {
            error = "cook_cache: ";
            error.append(code);
            return false;
        }
        bool Read(const std::filesystem::path& path, VariableArray<uint8_t>& bytes)
        {
            std::error_code code;
            if (!std::filesystem::is_regular_file(path, code) || code)
            {
                return false;
            }
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto size = file.tellg();
            if (!file || size < 0 || static_cast<uintmax_t>(size) > std::numeric_limits<size_t>::max() ||
                static_cast<uintmax_t>(size) > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return false;
            }
            bytes.resize(static_cast<size_t>(size));
            file.seekg(0);
            if (!bytes.empty())
            {
                file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            return file && file.peek() == std::char_traits<char>::eof() && !file.bad();
        }
        bool SameDependencies(const CookDependencySnapshot& a, const CookDependencySnapshot& b)
        {
            return a.SchemaVersion == b.SchemaVersion && a.CookerRevision == b.CookerRevision &&
                   a.Fingerprint == b.Fingerprint;
        }
        bool SameKey(const AssetCookedReference& a, const AssetCookedReference& b)
        {
            return a.Kind == b.Kind && Equal(a.LogicalPath, b.LogicalPath) && Equal(a.Variant, b.Variant);
        }
        bool SameIdentity(const AssetCookedReference& a, const AssetCookedReference& b)
        {
            return SameKey(a, b) && a.SourceHash == b.SourceHash && Equal(a.Format, b.Format) &&
                   Equal(a.CookedPackage, b.CookedPackage) && Equal(a.EntryName, b.EntryName) &&
                   a.EntryType == b.EntryType && a.CookedVersion == b.CookedVersion;
        }
        bool SameReference(const AssetCookedReference& a, const AssetCookedReference& b)
        {
            if (!SameIdentity(a, b) || a.CookedHash != b.CookedHash || a.bHasSkeletalMetadata != b.bHasSkeletalMetadata)
            {
                return false;
            }
            if (!a.bHasSkeletalMetadata)
            {
                return true;
            }
            const auto& x = a.SkeletalMetadata;
            const auto& y = b.SkeletalMetadata;
            return x.VertexCount == y.VertexCount && x.IndexCount == y.IndexCount && x.JointCount == y.JointCount &&
                   x.ClipCount == y.ClipCount && x.bHasSubmeshCounts == y.bHasSubmeshCounts &&
                   (!x.bHasSubmeshCounts ||
                    (x.SubmeshCount == y.SubmeshCount && x.MaterialSlotCount == y.MaterialSlotCount));
        }
        struct CurrentPlan
        {
            CookDecisionContext Context;
            VariableArray<AssetCookedReference> Expected;
            VariableArray<std::filesystem::path> Packages;
        };
        bool NoNul(const std::filesystem::path& path)
        {
            for (auto c : path.native())
            {
                if (c == 0)
                {
                    return false;
                }
            }
            return true;
        }
#if defined(_WIN32)
        // 存在しない末尾は許すが、既存中間fileをdirectoryとして通さない。
        bool SafeTarget(const std::filesystem::path& path)
        {
            AnsiString ascii, relative;
            if (!Paths::LocalDrivePath(path) || !Paths::AsciiPath(path, ascii) ||
                !Paths::AsciiPath(path.relative_path(), relative) || !Paths::SafeOutputName(relative) ||
                !Paths::NoReparse(path))
            {
                return false;
            }
            auto prefix = path.root_path();
            for (const auto& part : path.relative_path())
            {
                prefix /= part;
                std::error_code code;
                const auto status = std::filesystem::status(prefix, code);
                if (code == std::errc::no_such_file_or_directory)
                {
                    return true;
                }
                if (code)
                {
                    return false;
                }
                if (!std::filesystem::exists(status))
                {
                    return true;
                }
                if (Paths::SamePath(prefix, path))
                {
                    return std::filesystem::is_regular_file(status);
                }
                if (!std::filesystem::is_directory(status))
                {
                    return false;
                }
            }
            return false;
        }
        bool Aliases(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            if (Paths::SamePath(a.lexically_normal(), b.lexically_normal()))
            {
                return true;
            }
            const auto ca = std::filesystem::weakly_canonical(a), cb = std::filesystem::weakly_canonical(b);
            if (Paths::SamePath(ca, cb))
            {
                return true;
            }
            if (std::filesystem::exists(a) && std::filesystem::exists(b))
            {
                return std::filesystem::equivalent(a, b);
            }
            return false;
        }
        bool Prefix(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            auto i = a.begin(), j = b.begin();
            for (; i != a.end() && j != b.end(); ++i, ++j)
            {
                if (!Paths::SamePath(*i, *j))
                {
                    return false;
                }
            }
            return i == a.end();
        }
#endif
        bool AddExpected(CurrentPlan& plan, AssetCookedReference row, const std::filesystem::path& package,
                         AnsiString& error)
        {
            if (!Detail::MakeCachePackagePath(package, plan.Context.Request.ManifestPath.parent_path(),
                                              row.CookedPackage, error) ||
                !Paths::SafeOutputName(row.CookedPackage))
            {
                return Fail(error, "unsafe_package_relative_path");
            }
            for (size_t i = 0; i < plan.Expected.size(); ++i)
            {
                if (SameKey(plan.Expected[i], row))
                {
                    return Fail(error, "generated_key_collision");
                }
            }
            plan.Expected.push_back(std::move(row));
            plan.Packages.push_back(package);
            return true;
        }
        bool BuildInventory(CurrentPlan& plan, AnsiString& error)
        {
            const auto& r = plan.Context.Request;
            const auto& dependencies = plan.Context.Dependencies;
            if (dependencies.Files.empty() || dependencies.Files[0].Role != CookDependencyRole::Source)
            {
                return Fail(error, "missing_source_snapshot");
            }
            VariableArray<uint8_t> bytes;
            if (!Read(r.InputPath, bytes) || bytes.size() != dependencies.Files[0].Size ||
                ComputeAssetPackagePayloadHash(bytes.data(), bytes.size()) != dependencies.Files[0].ContentHash)
            {
                return Fail(error, "source_changed_during_inventory");
            }
            AssetCookedReference primary;
            primary.LogicalPath = r.LogicalPath;
            primary.Variant = r.Variant;
            primary.EntryName = r.EntryName;
            primary.Format = r.Format;
            primary.SourceHash = dependencies.Files[0].ContentHash;
            if (!TryParseAssetKind(r.Kind, primary.Kind) ||
                !TryParseAssetPackageFourCCText(r.EntryTypeText, primary.EntryType))
            {
                return Fail(error, "invalid_normalized_profile");
            }
            ModelCookFingerprint fingerprint;
            if (primary.Kind == AssetKind::Model)
            {
                AnsiString source, overrideText;
                if (!Detail::MakeLosslessModelPath(r.InputPath, source) ||
                    (!r.ImportSettingsOverridePath.empty() &&
                     !Detail::MakeLosslessModelPath(r.ImportSettingsOverridePath, overrideText)))
                {
                    return Fail(error, "model_native_path_not_lossless");
                }
                Core::AssetImport::ImportSettingsFileOptions settings;
                settings.OverridePath = r.ImportSettingsOverridePath;
                settings.bDisabled = r.bNoSidecar;
                settings.bRequired = r.bRequireSidecar;
                const auto* decode = IsSupportedSkeletalCookFormat(r.Format) ? &r.SkeletalDecode : nullptr;
                if (!FingerprintModelCookSource(bytes.data(), bytes.size(), r.Format, source, r.LogicalPath,
                                                fingerprint, error, &settings, decode))
                {
                    return false;
                }
                primary.SourceHash = fingerprint.SourceHash;
            }
            if (!AddExpected(plan, std::move(primary), r.PackagePath, error))
            {
                return false;
            }
            if (IsSupportedMeshCookFormat(r.Format))
            {
                for (const auto& image : fingerprint.EmbeddedImages)
                {
                    AnsiString name;
                    Paths::AsciiPath(r.PackagePath.filename(), name);
                    name.append(".img");
                    char number[32];
                    const auto result = std::to_chars(number, number + sizeof(number), image.ImageIndex);
                    if (result.ec != std::errc{})
                    {
                        return Fail(error, "image_index_conversion");
                    }
                    name.append(number, static_cast<size_t>(result.ptr - number));
                    name.append(".nvpkg");
                    AssetCookedReference row;
                    row.LogicalPath = image.LogicalPath;
                    row.Kind = AssetKind::Texture;
                    row.SourceHash = image.SourceHash;
                    row.Variant = "default";
                    row.Format = image.Format;
                    row.EntryName = "__texture__";
                    row.EntryType = MakeAssetPackageFourCC('T', 'e', 'x', '0');
                    if (!AddExpected(plan, std::move(row),
                                     r.PackagePath.parent_path() / std::filesystem::path(name.c_str()), error))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        bool GuardTargets(const CurrentPlan& plan, const AssetManifest* manifest, AnsiString& error)
        {
#if !defined(_WIN32)
            (void)plan;
            (void)manifest;
            return Fail(error, "Windows_output_boundary_required");
#else
            const auto& r = plan.Context.Request;
            VariableArray<std::filesystem::path> targets = plan.Packages;
            targets.push_back(r.ManifestPath);
            for (size_t i = 0; i < targets.size(); ++i)
            {
                if (!SafeTarget(targets[i]))
                {
                    return Fail(error, "unsafe_output_target");
                }
                for (size_t j = 0; j < i; ++j)
                {
                    if (Aliases(targets[i], targets[j]) || Prefix(targets[i], targets[j]) ||
                        Prefix(targets[j], targets[i]))
                    {
                        return Fail(error, "output_alias_or_prefix");
                    }
                }
                for (const auto& dependency : plan.Context.Dependencies.Files)
                {
                    if (Aliases(targets[i], dependency.Path) || Prefix(targets[i], dependency.Path) ||
                        Prefix(dependency.Path, targets[i]))
                    {
                        return Fail(error, "output_dependency_alias");
                    }
                }
            }
            if (manifest && manifest->IsLoaded())
            {
                for (size_t index = 0; index < manifest->GetReferenceCount(); ++index)
                {
                    const auto& row = manifest->GetReference(index);
                    bool bOurs = false;
                    for (const auto& expected : plan.Expected)
                    {
                        if (SameKey(expected, row))
                        {
                            bOurs = true;
                            break;
                        }
                    }
                    if (bOurs)
                    {
                        continue;
                    }
                    if (!Paths::SafeOutputName(row.CookedPackage))
                    {
                        return Fail(error, "unsafe_retained_package_path");
                    }
                    const auto normalized = AssetPath::Normalize(AnsiStringView(row.CookedPackage));
                    if (!normalized.HasLogicalPath() || !Equal(normalized.GetLogicalPath(), row.CookedPackage))
                    {
                        return Fail(error, "unstable_retained_package_path");
                    }
                    const auto retained =
                        r.ManifestPath.parent_path() / std::filesystem::path(row.CookedPackage.c_str());
                    for (const auto& target : targets)
                    {
                        if (Aliases(target, retained) || Prefix(target, retained) || Prefix(retained, target))
                        {
                            return Fail(error, "retained_key_output_alias");
                        }
                    }
                }
            }
            return true;
#endif
        }
        // Windowsのabsolute化で末尾dot/spaceが消える前に、要求そのものの物理名を検査する。
        bool SafeRawOutputLocator(const std::filesystem::path& path)
        {
            AnsiString relative;
            if (path.empty() || !NoNul(path) || (path.has_root_path() && !path.is_absolute()) ||
                !Paths::AsciiPath(path.relative_path(), relative))
            {
                return false;
            }
            return Paths::SafeOutputName(relative);
        }
        bool Prepare(const SingleAssetCookRequest& request, uint64_t revision, const AssetManifest* manifest,
                     CurrentPlan& plan, AnsiString& error)
        {
            if (!revision || !NoNul(request.InputPath) || !NoNul(request.ImportSettingsOverridePath))
            {
                return Fail(error, "invalid_revision_or_locator");
            }
            if (!SafeRawOutputLocator(request.PackagePath) || !SafeRawOutputLocator(request.ManifestPath))
            {
                return Fail(error, "unsafe_output_spelling");
            }
            if (!Detail::NormalizeCacheCookRequest(request, plan.Context.Request, error) ||
                !CaptureCookDependencySnapshot(plan.Context.Request, revision, plan.Context.Dependencies, error) ||
                !BuildInventory(plan, error) || !GuardTargets(plan, manifest, error))
            {
                return false;
            }
            return true;
        }
        bool Stable(const CurrentPlan& plan, AnsiString& error)
        {
            CookDependencySnapshot after;
            if (!CaptureCookDependencySnapshot(plan.Context.Request, plan.Context.Dependencies.CookerRevision, after,
                                               error))
            {
                return false;
            }
            return SameDependencies(plan.Context.Dependencies, after) ||
                   Fail(error, "dependencies_changed_during_operation");
        }
        bool CaptureOutputs(const CurrentPlan& plan, const AssetManifest* manifest,
                            VariableArray<CookRecordedOutput>& out, AnsiString& error,
                            CookDecisionReason* reason = nullptr)
        {
            if (reason)
            {
                *reason = CookDecisionReason::ManifestMismatch;
            }
            if (!manifest || !manifest->IsLoaded())
            {
                return Fail(error, "output_manifest_missing_or_invalid");
            }
            VariableArray<CookRecordedOutput> candidate;
            for (size_t i = 0; i < plan.Expected.size(); ++i)
            {
                const auto& expected = plan.Expected[i];
                const auto found = manifest->Resolve(expected.LogicalPath, expected.Kind, expected.Variant);
                if (!found.ShouldUseCooked() || !SameIdentity(expected, found.Reference))
                {
                    return Fail(error, "output_manifest_identity_mismatch");
                }
                if (reason)
                {
                    *reason = CookDecisionReason::PackageInvalid;
                }
                CookRecordedOutput row;
                row.Reference = found.Reference;
                VariableArray<uint8_t> bytes;
                if (!Read(plan.Packages[i], bytes) ||
                    !ValidateCookOutputPackage(row.Reference, bytes, row.Package, error))
                {
                    return Fail(error, "output_package_invalid");
                }
                row.Reference.SourceHashHex = FormatAssetHashHex(row.Reference.SourceHash);
                row.Reference.CookedHashHex = FormatAssetHashHex(row.Reference.CookedHash);
                row.Reference.EntryTypeText = FormatAssetPackageFourCCText(row.Reference.EntryType);
                candidate.push_back(std::move(row));
                if (reason)
                {
                    *reason = CookDecisionReason::ManifestMismatch;
                }
            }
            out = std::move(candidate);
            return true;
        }
    } // namespace
    CookDecision Detail::DecideCookCacheWithProbe(const SingleAssetCookRequest& request, uint64_t revision,
                                                  bool bAllowSkip, const CookOutputRecord* previous,
                                                  const Core::Asset::AssetManifest* manifest, CookDecisionContext& out,
                                                  Core::Container::AnsiString& error, void (*afterPrepare)(void*),
                                                  void* probeContext)
    {
        error.clear();
        try
        {
            CurrentPlan plan;
            if (!Prepare(request, revision, manifest, plan, error))
            {
                return CookDecision::Error;
            }
            if (afterPrepare)
            {
                afterPrepare(probeContext);
            }
            auto reason = CookDecisionReason::Current;
            if (!bAllowSkip)
            {
                reason = CookDecisionReason::Forced;
            }
            else if (!previous)
            {
                reason = CookDecisionReason::MissingRecord;
            }
            else if (previous->SchemaVersion != 1 ||
                     previous->DependencySchemaVersion != plan.Context.Dependencies.SchemaVersion ||
                     previous->CookerRevision != revision)
            {
                reason = CookDecisionReason::RecordVersion;
            }
            else if (previous->DependencyFingerprint != plan.Context.Dependencies.Fingerprint)
            {
                reason = CookDecisionReason::DependenciesChanged;
            }
            else if (previous->Outputs.size() != plan.Expected.size())
            {
                reason = CookDecisionReason::InventoryChanged;
            }
            else
            {
                for (size_t i = 0; i < plan.Expected.size(); ++i)
                {
                    if (!SameIdentity(previous->Outputs[i].Reference, plan.Expected[i]))
                    {
                        reason = CookDecisionReason::InventoryChanged;
                        break;
                    }
                }
                if (reason == CookDecisionReason::Current)
                {
                    VariableArray<CookRecordedOutput> actual;
                    CookDecisionReason invalidReason = CookDecisionReason::PackageInvalid;
                    if (!CaptureOutputs(plan, manifest, actual, error, &invalidReason))
                    {
                        reason = invalidReason;
                    }
                    else
                    {
                        for (size_t i = 0; i < actual.size(); ++i)
                        {
                            if (!SameReference(actual[i].Reference, previous->Outputs[i].Reference))
                            {
                                reason = CookDecisionReason::ManifestMismatch;
                                break;
                            }
                            if (actual[i].Package.Size != previous->Outputs[i].Package.Size ||
                                actual[i].Package.ContentHash != previous->Outputs[i].Package.ContentHash)
                            {
                                reason = CookDecisionReason::PackageChanged;
                                break;
                            }
                        }
                    }
                }
            }
            if (!Stable(plan, error))
            {
                return CookDecision::Error;
            }
            plan.Context.Reason = reason;
            out = std::move(plan.Context);
            error.clear();
            return reason == CookDecisionReason::Current ? CookDecision::Skip : CookDecision::Cook;
        }
        catch (const std::exception&)
        {
            Fail(error, "evaluation_exception");
            return CookDecision::Error;
        }
    }
    bool Detail::CaptureCookOutputRecordWithProbe(const CookDecisionContext& before,
                                                  const Core::Asset::AssetManifest& manifest, CookOutputRecord& out,
                                                  Core::Container::AnsiString& error, void (*afterPrepare)(void*),
                                                  void* probeContext)
    {
        error.clear();
        try
        {
            CurrentPlan plan;
            if (!Prepare(before.Request, before.Dependencies.CookerRevision, &manifest, plan, error))
            {
                return false;
            }
            if (!SameDependencies(before.Dependencies, plan.Context.Dependencies))
            {
                return Fail(error, "before_cook_dependencies_changed");
            }
            if (afterPrepare)
            {
                afterPrepare(probeContext);
            }
            CookOutputRecord candidate;
            candidate.DependencySchemaVersion = plan.Context.Dependencies.SchemaVersion;
            candidate.CookerRevision = plan.Context.Dependencies.CookerRevision;
            candidate.DependencyFingerprint = plan.Context.Dependencies.Fingerprint;
            if (!CaptureOutputs(plan, &manifest, candidate.Outputs, error) || !Stable(plan, error))
            {
                return false;
            }
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "capture_exception");
        }
    }
    CookDecision DecideCookCache(const SingleAssetCookRequest& request, uint64_t revision, bool bAllowSkip,
                                 const CookOutputRecord* previous, const Core::Asset::AssetManifest* manifest,
                                 CookDecisionContext& out, Core::Container::AnsiString& error)
    {
        return Detail::DecideCookCacheWithProbe(request, revision, bAllowSkip, previous, manifest, out, error, nullptr,
                                                nullptr);
    }
    bool CaptureCookOutputRecord(const CookDecisionContext& before, const Core::Asset::AssetManifest& manifest,
                                 CookOutputRecord& out, Core::Container::AnsiString& error)
    {
        return Detail::CaptureCookOutputRecordWithProbe(before, manifest, out, error, nullptr, nullptr);
    }

} // namespace NorvesLib::Tools::AssetCook
