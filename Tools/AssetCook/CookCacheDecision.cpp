#include "CookCacheDecision.h"
#include "CookOutputPlan.h"
#include "CookReferenceValues.h"
#include "CookOutputSetGuardTestAccess.h"
#include "CookPathIdentity.h"
#include "CookCacheDecisionTestAccess.h"
#include "CookOutputPaths.h"
#include "AssetCookLegacyOptions.h"
#include "MeshCooker.h"
#include "Asset/AssetPath.h"
#include <charconv>
#include <algorithm>
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
        using Detail::CookReferenceValues::SameKey;
        using Detail::CookReferenceValues::SameIdentity;
        using Detail::CookReferenceValues::SameReference;
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
                Core::AssetImport::ImportSettingsFileOptions settings;
                settings.OverridePath = r.ImportSettingsOverridePath;
                settings.bDisabled = r.bNoSidecar;
                settings.bRequired = r.bRequireSidecar;
                const auto* decode = IsSupportedSkeletalCookFormat(r.Format) ? &r.SkeletalDecode : nullptr;
                if (!FingerprintModelCookSourceNativePath(bytes.data(), bytes.size(), r.Format, r.InputPath, r.LogicalPath,
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

    namespace
    {
        bool MatchesPreparedPlan(const CookPreparedPlan& saved, const CurrentPlan& current, AnsiString& error)
        {
            if (!SameDependencies(saved.Context.Dependencies, current.Context.Dependencies) ||
                saved.Outputs.size() != current.Expected.size())
            {
                return Fail(error, "prepared_dependencies_or_inventory_changed");
            }
            for (size_t i = 0; i < saved.Outputs.size(); ++i)
            {
                if (!SameReference(saved.Outputs[i].ExpectedIdentity, current.Expected[i]) ||
                    saved.Outputs[i].TargetPath != current.Packages[i])
                {
                    return Fail(error, "prepared_output_changed");
                }
            }
            return true;
        }
        bool Reprepare(const CookPreparedPlan& saved, CurrentPlan& current, AnsiString& error)
        {
            return Prepare(saved.Context.Request, saved.Context.Dependencies.CookerRevision, nullptr, current, error) &&
                   MatchesPreparedPlan(saved, current, error) && Stable(current, error);
        }
        CookPreparedPlan ExportPlan(const CurrentPlan& current)
        {
            CookPreparedPlan out;
            out.Context = current.Context;
            out.Context.Reason = CookDecisionReason::Forced;
            out.Outputs.reserve(current.Expected.size());
            for (size_t i = 0; i < current.Expected.size(); ++i)
            {
                out.Outputs.push_back({current.Expected[i], current.Packages[i]});
            }
            return out;
        }
#if defined(_WIN32)
        bool DirectoryLocator(const std::filesystem::path& path)
        {
            AnsiString full, relative;
            return path.is_absolute() && Paths::LocalDrivePath(path) && Paths::AsciiPath(path, full) &&
                   Paths::AsciiPath(path.relative_path(), relative) && Paths::SafeOutputName(relative) &&
                   Paths::NoReparse(path);
        }
        bool PhysicalDirectory(const std::filesystem::path& path, bool bRequirePresent, std::filesystem::path& out)
        {
            if (!DirectoryLocator(path))
            {
                return false;
            }
            auto existing = path;
            VariableArray<std::filesystem::path> missing;
            for (;;)
            {
                const DWORD attributes = GetFileAttributesW(existing.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES)
                {
                    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    {
                        return false;
                    }
                    break;
                }
                const DWORD code = GetLastError();
                if (bRequirePresent || (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) ||
                    existing == existing.root_path())
                {
                    return false;
                }
                missing.push_back(existing.filename());
                existing = existing.parent_path();
            }
            HANDLE h = CreateFileW(existing.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            struct Close
            {
                HANDLE Handle;
                ~Close()
                {
                    CloseHandle(Handle);
                }
            } close{h};
            FILE_ATTRIBUTE_TAG_INFO info{};
            if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &info, sizeof(info)) ||
                (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                return false;
            }
            const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_GUID;
            const DWORD required = GetFinalPathNameByHandleW(h, nullptr, 0, flags);
            if (required == 0 || required > 32767)
            {
                return false;
            }
            VariableArray<wchar_t> buffer(static_cast<size_t>(required) + 1, 0);
            const DWORD count = GetFinalPathNameByHandleW(h, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
            if (count == 0 || count >= buffer.size())
            {
                return false;
            }
            std::filesystem::path candidate(buffer.data(), buffer.data() + count);
            for (size_t i = missing.size(); i > 0; --i)
            {
                candidate /= missing[i - 1];
            }
            out = std::move(candidate);
            return true;
        }
        bool DisjointRoots(const std::filesystem::path& finalRoot, const std::filesystem::path& stageRoot,
                           AnsiString& error)
        {
            std::filesystem::path finalPhysical, stagePhysical;
            if (!PhysicalDirectory(finalRoot, false, finalPhysical) ||
                !PhysicalDirectory(stageRoot, true, stagePhysical) || Prefix(finalPhysical, stagePhysical) ||
                Prefix(stagePhysical, finalPhysical))
            {
                return Fail(error, "stage_root_invalid_or_overlaps_final");
            }
            return true;
        }
#endif
        bool CheckStageMapping(const CurrentPlan& finalPlan, const CurrentPlan& stagePlan, AnsiString& error)
        {
#if !defined(_WIN32)
            (void)finalPlan;
            (void)stagePlan;
            return Fail(error, "Windows_output_boundary_required");
#else
            const auto& a = finalPlan.Context.Request;
            const auto& b = stagePlan.Context.Request;
            const auto stageRoot = b.ManifestPath.parent_path();
            // 骨格policy等は共通snapshotが同じ意味をhashする。物理出力pathはその印から除かれている。
            if (!SameDependencies(finalPlan.Context.Dependencies, stagePlan.Context.Dependencies) ||
                a.InputPath != b.InputPath || a.ImportSettingsOverridePath != b.ImportSettingsOverridePath ||
                !Equal(a.LogicalPath, b.LogicalPath) || !Equal(a.Kind, b.Kind) || !Equal(a.Format, b.Format) ||
                !Equal(a.EntryName, b.EntryName) || !Equal(a.EntryTypeText, b.EntryTypeText) ||
                !Equal(a.Variant, b.Variant) || a.bNoSidecar != b.bNoSidecar ||
                a.bRequireSidecar != b.bRequireSidecar || a.ManifestPath.filename() != b.ManifestPath.filename() ||
                finalPlan.Expected.size() != stagePlan.Expected.size() ||
                !DisjointRoots(a.ManifestPath.parent_path(), stageRoot, error))
            {
                if (error.empty())
                {
                    Fail(error, "stage_request_semantics_changed");
                }
                return false;
            }
            for (size_t i = 0; i < finalPlan.Expected.size(); ++i)
            {
                const auto& expected = finalPlan.Expected[i];
                if (!SameReference(expected, stagePlan.Expected[i]) ||
                    stagePlan.Packages[i] != stageRoot / std::filesystem::path(expected.CookedPackage.c_str()))
                {
                    return Fail(error, "stage_relative_inventory_changed");
                }
            }
            return true;
#endif
        }
        bool CompleteFragment(const CurrentPlan& stage, const AssetManifest& manifest, AnsiString& error)
        {
            if (!manifest.IsLoaded() || manifest.GetReferenceCount() != stage.Expected.size())
            {
                return Fail(error, "stage_fragment_inventory_mismatch");
            }
            for (const auto& expected : stage.Expected)
            {
                const auto found = manifest.Resolve(expected.LogicalPath, expected.Kind, expected.Variant);
                if (!found.ShouldUseCooked() || !SameIdentity(expected, found.Reference))
                {
                    return Fail(error, "stage_fragment_identity_mismatch");
                }
            }
            return true;
        }
    } // namespace
    bool PrepareCookOutputPlan(const SingleAssetCookRequest& request, uint64_t revision, const AssetManifest* manifest,
                               CookPreparedPlan& out, AnsiString& error)
    {
        error.clear();
        try
        {
            CurrentPlan current;
            if (!Prepare(request, revision, manifest, current, error) || !Stable(current, error))
            {
                return false;
            }
            auto candidate = ExportPlan(current);
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "prepare_output_plan_exception");
        }
    }
    bool PrepareCookStagingPlan(const CookPreparedPlan& finalPlan, const std::filesystem::path& stageRoot,
                                CookPreparedPlan& out, AnsiString& error)
    {
        error.clear();
#if !defined(_WIN32)
        (void)finalPlan;
        (void)stageRoot;
        (void)out;
        return Fail(error, "Windows_output_boundary_required");
#else
        try
        {
            CurrentPlan finalCurrent;
            if (!Reprepare(finalPlan, finalCurrent, error) || finalCurrent.Expected.empty() ||
                !DisjointRoots(finalCurrent.Context.Request.ManifestPath.parent_path(), stageRoot, error))
            {
                return false;
            }
            std::error_code code;
            const auto contents = std::filesystem::directory_iterator(stageRoot, code);
            if (code || contents != std::filesystem::directory_iterator{})
            {
                return Fail(error, "stage_must_be_empty_owned_directory");
            }
            auto request = finalCurrent.Context.Request;
            request.PackagePath = stageRoot / std::filesystem::path(finalCurrent.Expected[0].CookedPackage.c_str());
            request.ManifestPath = stageRoot / finalCurrent.Context.Request.ManifestPath.filename();
            CurrentPlan stage;
            if (!Prepare(request, finalCurrent.Context.Dependencies.CookerRevision, nullptr, stage, error) ||
                !CheckStageMapping(finalCurrent, stage, error) || !Stable(finalCurrent, error) || !Stable(stage, error))
            {
                return false;
            }
            auto candidate = ExportPlan(stage);
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "prepare_staging_exception");
        }
#endif
    }
    bool CaptureStagedCookOutputRecord(const CookPreparedPlan& finalPlan, const CookPreparedPlan& stagePlan,
                                       const AssetManifest& fragment, CookOutputRecord& out, AnsiString& error)
    {
        error.clear();
        try
        {
            CurrentPlan finalCurrent, stageCurrent;
            if (!Reprepare(finalPlan, finalCurrent, error) || !Reprepare(stagePlan, stageCurrent, error) ||
                !CheckStageMapping(finalCurrent, stageCurrent, error) ||
                !CompleteFragment(stageCurrent, fragment, error))
            {
                return false;
            }
            CookOutputRecord candidate;
            if (!CaptureCookOutputRecord(stagePlan.Context, fragment, candidate, error) || !Stable(finalCurrent, error))
            {
                return false;
            }
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "capture_staging_exception");
        }
    }

    namespace
    {
        enum class SetRole
        {
            Package,
            Manifest,
            Protected
        };
        struct SetEndpoint
        {
            std::filesystem::path Locator;
            size_t IdentityIndex = 0;
            SetRole Role = SetRole::Protected;
            int ExpectedPresence = -1;
        };
        struct SetObservation
        {
            Detail::CookPathIdentity Identity;
            size_t Outputs = 0, Protected = 0;
        };
        struct SetKey
        {
            size_t Plan = 0, Output = 0;
        };
        bool Budget(size_t amount, size_t& total, AnsiString& error)
        {
            if (amount > MaximumCookSetMetadataBytes - total)
            {
                return Fail(error, "set_metadata_limit");
            }
            total += amount;
            return true;
        }
        bool AddSetEndpoint(VariableArray<SetEndpoint>& endpoints, const std::filesystem::path& path, SetRole role,
                            int presence, size_t& budget, AnsiString& error)
        {
            SetEndpoint candidate;
            candidate.Role = role;
            candidate.ExpectedPresence = presence;
            if (!Detail::NormalizeCookGuardLocator(path, candidate.Locator, error) ||
                !Budget(candidate.Locator.native().size() * sizeof(std::filesystem::path::value_type), budget, error))
            {
                return false;
            }
            endpoints.push_back(std::move(candidate));
            return true;
        }
        int CompareKeyText(AnsiStringView a, AnsiStringView b)
        {
            const size_t n = std::min(a.size(), b.size());
            const int result = n ? std::memcmp(a.data(), b.data(), n) : 0;
            if (result)
            {
                return result;
            }
            return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
        }
        bool AggregatePaths(VariableArray<SetEndpoint>& endpoints, VariableArray<SetObservation>& observations,
                            Detail::CookOutputSetGuardStats& stats, size_t& budget, bool bSharedManifest, AnsiString& error)
        {
            VariableArray<size_t> ordered;
            ordered.reserve(endpoints.size());
            for (size_t i = 0; i < endpoints.size(); ++i)
            {
                ordered.push_back(i);
            }
            std::sort(ordered.begin(), ordered.end(),
                      [&](size_t a, size_t b)
                      {
                          return endpoints[a].Locator.native() < endpoints[b].Locator.native();
                      });
            for (size_t position = 0; position < ordered.size();)
            {
                const auto first = ordered[position];
                size_t end = position + 1;
                while (end < ordered.size() &&
                       endpoints[ordered[end]].Locator.native() == endpoints[first].Locator.native())
                {
                    ++end;
                }
                SetObservation observed;
                ++stats.AggregateIdentityObservations;
                if (!Detail::ObserveCookPathIdentity(endpoints[first].Locator, observed.Identity, error) ||
                    !Budget(observed.Identity.Canonical.native().size() * sizeof(std::filesystem::path::value_type),
                            budget, error) ||
                    !Budget(observed.Identity.Components.size() * sizeof(Detail::CookPathComponent), budget, error))
                {
                    return false;
                }
                const size_t index = observations.size();
                observations.push_back(std::move(observed));
                ++stats.UniqueLocators;
                for (size_t i = position; i < end; ++i)
                {
                    endpoints[ordered[i]].IdentityIndex = index;
                }
                position = end;
            }
            bool bManifestSeen = false;
            size_t manifestIdentity = 0;
            for (const auto& endpoint : endpoints)
            {
                auto& observed = observations[endpoint.IdentityIndex];
                if (endpoint.ExpectedPresence != -1 && observed.Identity.bPresent != (endpoint.ExpectedPresence == 1))
                {
                    return Fail(error, "set_dependency_presence_changed");
                }
                if (endpoint.Role == SetRole::Manifest && bSharedManifest)
                {
                    if (bManifestSeen)
                    {
                        if (!Detail::SameCookManifestEndpoint(observations[manifestIdentity].Identity, observed.Identity))
                        {
                            return Fail(error, "set_requires_one_manifest_path");
                        }
                        continue;
                    }
                    bManifestSeen = true;
                    manifestIdentity = endpoint.IdentityIndex;
                }
                if (endpoint.Role == SetRole::Protected)
                {
                    ++observed.Protected;
                }
                else
                {
                    ++observed.Outputs;
                    if (observed.Identity.bPresent && observed.Identity.LinkCount > 1)
                    {
                        return Fail(error, "set_output_hardlink_not_supported");
                    }
                }
            }
            return true;
        }
        bool RoleConflict(size_t outputs, size_t protectedCount)
        {
            return outputs > 1 || (outputs && protectedCount);
        }
        bool CheckSetPaths(const VariableArray<SetObservation>& observed, AnsiString& error)
        {
            VariableArray<size_t> order;
            for (size_t i = 0; i < observed.size(); ++i)
            {
                if (observed[i].Outputs || observed[i].Protected)
                {
                    order.push_back(i);
                }
            }
            std::sort(order.begin(), order.end(),
                      [&](size_t a, size_t b)
                      {
                          return Detail::CompareCookPhysicalPath(observed[a].Identity, observed[b].Identity) < 0;
                      });
            struct Group
            {
                size_t Identity = 0, Outputs = 0, Protected = 0;
            };
            VariableArray<Group> groups;
            for (size_t i = 0; i < order.size();)
            {
                Group group{order[i]};
                size_t j = i;
                while (j < order.size() &&
                       Detail::CompareCookPhysicalPath(observed[order[i]].Identity, observed[order[j]].Identity) == 0)
                {
                    group.Outputs += observed[order[j]].Outputs;
                    group.Protected += observed[order[j]].Protected;
                    ++j;
                }
                if (RoleConflict(group.Outputs, group.Protected))
                {
                    return Fail(error, "set_physical_path_alias");
                }
                groups.push_back(group);
                i = j;
            }
            // component順なのでa/a/xは隣接subtreeとなる。祖先stackはa!による見落としを作らない。
            VariableArray<size_t> ancestors;
            size_t outputAncestors = 0;
            for (size_t i = 0; i < groups.size(); ++i)
            {
                const auto& current = groups[i];
                while (!ancestors.empty() &&
                       !Detail::CookPhysicalAncestor(observed[groups[ancestors.back()].Identity].Identity,
                                                     observed[current.Identity].Identity))
                {
                    if (groups[ancestors.back()].Outputs)
                    {
                        --outputAncestors;
                    }
                    ancestors.pop_back();
                }
                if ((current.Outputs && !ancestors.empty()) || (current.Protected && outputAncestors))
                {
                    return Fail(error, "set_file_directory_prefix");
                }
                ancestors.push_back(i);
                if (current.Outputs)
                {
                    ++outputAncestors;
                }
            }
            order.clear();
            for (size_t i = 0; i < observed.size(); ++i)
            {
                if (observed[i].Identity.bPresent && (observed[i].Outputs || observed[i].Protected))
                {
                    order.push_back(i);
                }
            }
            std::sort(order.begin(), order.end(),
                      [&](size_t a, size_t b)
                      {
                          return Detail::CompareCookFileIdentity(observed[a].Identity, observed[b].Identity) < 0;
                      });
            for (size_t i = 0; i < order.size();)
            {
                size_t outputs = 0, protectedCount = 0, j = i;
                while (j < order.size() &&
                       Detail::CompareCookFileIdentity(observed[order[i]].Identity, observed[order[j]].Identity) == 0)
                {
                    outputs += observed[order[j]].Outputs;
                    protectedCount += observed[order[j]].Protected;
                    ++j;
                }
                if (RoleConflict(outputs, protectedCount))
                {
                    return Fail(error, "set_file_id_alias");
                }
                i = j;
            }
            return true;
        }
    } // namespace
    namespace
    {
        bool ValidateSetImpl(Core::Container::Span<const CookPreparedPlan> plans,
                             Core::Container::Span<const std::filesystem::path> controls,
                             Detail::CookOutputSetGuardStats& stats, bool bSharedManifest, AnsiString& error,
                             void (*afterObservation)(void*), void* probeContext)
        {
            error.clear();
            stats = {};
#if !defined(_WIN32)
            (void)plans;
            (void)controls;
            (void)afterObservation;
            (void)probeContext;
            (void)bSharedManifest;
            return Fail(error, "Windows_output_boundary_required");
#else
            try
            {
                if (plans.empty() || !plans.data() || plans.size() > MaximumCookSetPlans ||
                    (controls.size() && !controls.data()) || controls.size() > MaximumCookSetProtectedOccurrences)
                {
                    return Fail(error, "set_input_limit");
                }
                VariableArray<CurrentPlan> current;
                current.reserve(plans.size());
                VariableArray<SetEndpoint> endpoints;
                VariableArray<SetKey> keys;
                size_t outputs = 0, protectedCount = controls.size(), budget = 0;
                for (size_t i = 0; i < plans.size(); ++i)
                {
                    CurrentPlan plan;
                    if (!Reprepare(plans[i], plan, error))
                    {
                        return false;
                    }
                    if (plan.Expected.size() > MaximumCookSetOutputs - outputs ||
                        plan.Context.Dependencies.Files.size() > MaximumCookSetProtectedOccurrences - protectedCount)
                    {
                        return Fail(error, "set_occurrence_limit");
                    }
                    outputs += plan.Expected.size();
                    protectedCount += plan.Context.Dependencies.Files.size();
                    for (size_t j = 0; j < plan.Expected.size(); ++j)
                    {
                        const auto& key = plan.Expected[j];
                        if (!Budget(key.LogicalPath.size(), budget, error) ||
                            !Budget(key.Variant.size(), budget, error) ||
                            !AddSetEndpoint(endpoints, plan.Packages[j], SetRole::Package, -1, budget, error))
                        {
                            return false;
                        }
                        keys.push_back({i, j});
                    }
                    if (!AddSetEndpoint(endpoints, plan.Context.Request.ManifestPath, SetRole::Manifest, -1, budget,
                                        error))
                    {
                        return false;
                    }
                    // 公開planのFilesではなく、同じauthorityで今採取した依存を使う。
                    for (const auto& dependency : plan.Context.Dependencies.Files)
                    {
                        if (!AddSetEndpoint(endpoints, dependency.Path, SetRole::Protected, dependency.bPresent ? 1 : 0,
                                            budget, error))
                        {
                            return false;
                        }
                    }
                    current.push_back(std::move(plan));
                }
                for (const auto& path : controls)
                {
                    if (!AddSetEndpoint(endpoints, path, SetRole::Protected, -1, budget, error))
                    {
                        return false;
                    }
                }
                std::sort(keys.begin(), keys.end(),
                          [&](const SetKey& a, const SetKey& b)
                          {
                              const auto& x = current[a.Plan].Expected[a.Output];
                              const auto& y = current[b.Plan].Expected[b.Output];
                              if (x.Kind != y.Kind)
                              {
                                  return static_cast<uint8_t>(x.Kind) < static_cast<uint8_t>(y.Kind);
                              }
                              const int path = CompareKeyText(x.LogicalPath, y.LogicalPath);
                              return path ? path < 0 : CompareKeyText(x.Variant, y.Variant) < 0;
                          });
                for (size_t i = 1; i < keys.size(); ++i)
                {
                    const auto& a = keys[i - 1];
                    const auto& b = keys[i];
                    if (SameKey(current[a.Plan].Expected[a.Output], current[b.Plan].Expected[b.Output]))
                    {
                        return Fail(error, "set_duplicate_output_key");
                    }
                }
                stats.LocatorOccurrences = endpoints.size();
                VariableArray<SetObservation> observations;
                if (!AggregatePaths(endpoints, observations, stats, budget, bSharedManifest, error) ||
                    !CheckSetPaths(observations, error))
                {
                    return false;
                }
                if (afterObservation)
                {
                    afterObservation(probeContext);
                }
                for (const auto& plan : current)
                {
                    if (!Stable(plan, error))
                    {
                        return false;
                    }
                }
                return true;
            }
            catch (const std::exception&)
            {
                return Fail(error, "set_guard_exception");
            }
#endif
        }
    } // namespace
    bool Detail::ValidateCookOutputSetForTest(Core::Container::Span<const CookPreparedPlan> plans,
                                              Core::Container::Span<const std::filesystem::path> controls,
                                              CookOutputSetGuardStats& stats, AnsiString& error,
                                              void (*afterObservation)(void*), void* probeContext)
    {
        return ValidateSetImpl(plans, controls, stats, true, error, afterObservation, probeContext);
    }
    bool ValidateCookStagingOutputSet(Core::Container::Span<const CookPreparedPlan> plans,
                                      Core::Container::Span<const std::filesystem::path> controls, AnsiString& error)
    {
        Detail::CookOutputSetGuardStats stats;
        return ValidateSetImpl(plans, controls, stats, false, error, nullptr, nullptr);
    }
    bool ValidateCookOutputSet(Core::Container::Span<const CookPreparedPlan> plans,
                               Core::Container::Span<const std::filesystem::path> controls, AnsiString& error)
    {
        Detail::CookOutputSetGuardStats stats;
        return Detail::ValidateCookOutputSetForTest(plans, controls, stats, error, nullptr, nullptr);
    }
} // namespace NorvesLib::Tools::AssetCook
