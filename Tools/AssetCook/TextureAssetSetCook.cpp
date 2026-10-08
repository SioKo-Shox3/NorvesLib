// texture spec v1の入力を共通管理controllerへ適合する。独立した公開・増分判断は置かない。
#include "TextureAssetSetCook.h"
#include "TextureAssetSetSpec.h"
#include "CookManagedStoreInitialization.h"
#include "CookOutputSetGuard.h"
#include "CookOutputPaths.h"
#include "CookPathIdentity.h"
#include "Text/JsonDocument.h"
#include <fstream>
#include <exception>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Text = Core::Container::AnsiString;
        using Bytes = Core::Container::VariableArray<uint8_t>;
        using Result = TextureAssetSetCookResult;
        namespace Paths = Detail::CookOutputPaths;
        Result Fail(Text& error, const char* code)
        {
            error = "texture_asset_set: ";
            error.append(code);
            return Result::Error;
        }
        bool ReadSpecBytes(const std::filesystem::path& path, Bytes& bytes)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto size = file.tellg();
            if (!file || size <= 0 || static_cast<uintmax_t>(size) > MaximumCookStateBytes)
            {
                return false;
            }
            bytes.resize(static_cast<size_t>(size));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file && file.peek() == std::char_traits<char>::eof() && !file.bad();
        }
        Result ObservationFailure(CookManagedStoreResult result)
        {
            switch (result)
            {
            case CookManagedStoreResult::NeedsRecovery:
                return Result::NeedsRecovery;
            case CookManagedStoreResult::Busy:
                return Result::Busy;
            case CookManagedStoreResult::Conflict:
                return Result::Conflict;
            default:
                return Result::Error;
            }
        }
        Result CookImpl(const TextureAssetSetCookRequest& request, CookManagedBootstrapOutcome& out, Text& error)
        {
#if !defined(_WIN32)
            (void)request;
            (void)out;
            return Fail(error, "windows_required");
#else
            if (request.SpecPath.empty() || request.RuntimeRoot.empty())
            {
                return Fail(error, "spec_and_runtime_required");
            }
            const auto cwd = std::filesystem::current_path();
            std::filesystem::path specPath, sourceRoot, runtime, manifest;
            if (!Detail::ResolveTextureCliPath(request.SpecPath, cwd, specPath, error) ||
                !Detail::ResolveTextureCliPath(request.RuntimeRoot, cwd, runtime, error) ||
                !Detail::ResolveTextureCliPath(request.SourceRoot.empty() ? cwd : request.SourceRoot, cwd, sourceRoot,
                                               error, true) ||
                !Detail::ResolveTextureCliPath(request.ManifestPath.empty() ? runtime / "manifest.json"
                                                                            : request.ManifestPath,
                                               cwd, manifest, error))
            {
                return Result::Error;
            }
            Text runtimeText, manifestName;
            if (!Paths::AsciiPath(runtime, runtimeText) || !Paths::AsciiPath(manifest.filename(), manifestName) ||
                !Paths::SamePath(manifest.parent_path(), runtime) || !Paths::SafeOutputName(manifestName))
            {
                return Fail(error, "manifest_and_runtime_scope");
            }
            CookOwnerResolveRequest owner{specPath, runtime, manifestName};
            CookManagedStoreObservation observation;
            auto observed = ObserveCookManagedStore(owner, observation, error);
            if (observed != CookManagedStoreResult::Observed && observed != CookManagedStoreResult::StoreMissing)
            {
                return ObservationFailure(observed);
            }
            // 固定pendingの検査後に初めてsource内容とspec JSONを読む。
            if (!std::filesystem::is_directory(sourceRoot) || !Paths::NoReparse(sourceRoot))
            {
                return Fail(error, "source_root_missing_or_reparse");
            }
            Bytes bytes;
            if (!ReadSpecBytes(specPath, bytes))
            {
                return Fail(error, "spec_read_or_size");
            }
            const size_t bom = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
            Core::JsonDocument document;
            if (!Core::JsonDocument::TryParseUtf8({bytes.data() + bom, bytes.size() - bom}, document))
            {
                return Fail(error, "invalid_spec_json");
            }
            TextureAssetSetSpec spec;
            const auto parsed = ParseTextureAssetSetSpec(document.GetRoot(), spec, true);
            if (!parsed.Succeeded())
            {
                error = "texture_asset_set: invalid spec field ";
                error.append(parsed.Field);
                return Result::Error;
            }
            if (spec.Version == 1 && request.bWarnBudget)
            {
                return Fail(error, "warn_budget_requires_spec_v2");
            }
            document.Reset();
            if (request.Report && spec.Version == 2)
            {
                request.Report->bEnabled = true;
                request.Report->bWarnBudget = request.bWarnBudget;
                request.Report->RuntimeRoot = runtime;
            }
            Core::Container::VariableArray<CookAssetBudget> budgets;
            Core::Container::VariableArray<SingleAssetCookRequest> assets;
            Core::Container::VariableArray<CookPreparedPlan> plans;
            for (const auto& entry : spec.Textures)
            {
                SingleAssetCookRequest single;
                const auto rawSource = std::filesystem::u8path(entry.SourcePath.begin(), entry.SourcePath.end());
                if (!Detail::ResolveTextureCliPath(rawSource, sourceRoot, single.InputPath, error))
                {
                    return Result::Error;
                }
                Text package = spec.PackageRoot;
                package.push_back('/');
                package.append(entry.PackageName);
                if (!Paths::SafeOutputName(package))
                {
                    return Fail(error, "unsafe_package_name");
                }
                single.PackagePath = runtime / package.c_str();
                single.ManifestPath = manifest;
                single.LogicalPath = entry.LogicalPath;
                single.Kind = entry.Kind == "skeletal" ? Text("model") : entry.Kind;
                budgets.push_back(entry.Budget);
                single.EntryName = entry.EntryName;
                single.EntryTypeText = entry.EntryType;
                single.Format = entry.Format;
                single.Variant = entry.Variant;
                single.ClipJointNodes = entry.JointNodes;
                if (!entry.SkeletonPath.empty())
                {
                    if (!Detail::ResolveTextureCliPath(
                            std::filesystem::u8path(entry.SkeletonPath.begin(), entry.SkeletonPath.end()), sourceRoot,
                            single.RetargetSkeletonPath, error) ||
                        !Detail::ResolveTextureCliPath(
                            std::filesystem::u8path(entry.RoleProfilePath.begin(), entry.RoleProfilePath.end()),
                            sourceRoot, single.RetargetProfilePath, error))
                    {
                        return Result::Error;
                    }
                    single.RetargetSourceClip = entry.SourceClip;
                    single.RetargetClipName = entry.ClipName;
                }
                single.AssetSetEmission = spec.Emission;
                CookPreparedPlan plan;
                if (!PrepareCookOutputPlan(single, spec.Version, nullptr, plan, error))
                {
                    return Result::Error;
                }
                plans.push_back(std::move(plan));
                assets.push_back(std::move(single));
            }
            if (!ValidateCookOutputSet(plans, {&specPath, 1}, error))
            {
                return Result::Error;
            }
            if (observed == CookManagedStoreResult::StoreMissing)
            {
                const auto initialized = InitializeNewCookManagedStore(owner, observation, error);
                switch (initialized)
                {
                case CookManagedStoreInitializationResult::Created:
                case CookManagedStoreInitializationResult::StoreExists:
                    break;
                case CookManagedStoreInitializationResult::NeedsRecovery:
                    return Result::NeedsRecovery;
                case CookManagedStoreInitializationResult::PublishedButError:
                    return Result::StorePublishedButError;
                case CookManagedStoreInitializationResult::Busy:
                    return Result::Busy;
                case CookManagedStoreInitializationResult::Conflict:
                    return Result::Conflict;
                default:
                    return Result::Error;
                }
            }
            observed = ObserveCookManagedStore(owner, observation, error);
            if (observed != CookManagedStoreResult::Observed)
            {
                return ObservationFailure(observed);
            }
            CookManagedBootstrapRequest managed{owner, assets, bytes, spec.Version, spec.Version == 1};
            managed.Jobs = request.Jobs;
            managed.bForce = request.bForce;
            managed.bPrune = request.bPrune;
            if (spec.Version == 2)
            {
                managed.Budgets = budgets;
                managed.TotalBudget = spec.TotalBudget;
                managed.Report = request.Report;
            }
            CookManagedBootstrapOutcome candidate;
            if (observation.Owner.bFinalRuntimeRootPresent && observation.bRuntimeRootClaimed)
            {
                switch (UpdateCookManagedAssetSet(managed, candidate, error))
                {
                case CookManagedUpdateResult::NoChange:
                    out = std::move(candidate);
                    return Result::NoChange;
                case CookManagedUpdateResult::Updated:
                    out = std::move(candidate);
                    return Result::Updated;
                case CookManagedUpdateResult::NeedsRecovery:
                    return Result::NeedsRecovery;
                case CookManagedUpdateResult::CommittedButError:
                    return Result::CommittedButError;
                case CookManagedUpdateResult::Busy:
                    return Result::Busy;
                case CookManagedUpdateResult::Conflict:
                    return Result::Conflict;
                default:
                    return Result::Error;
                }
            }
            if (observation.Owner.bFinalRuntimeRootPresent || observation.bRuntimeRootClaimed)
            {
                return Fail(error, "unclaimed_runtime");
            }
            switch (BootstrapCookManagedAssetSet(managed, candidate, error))
            {
            case CookManagedBootstrapResult::Created:
                out = std::move(candidate);
                return Result::Created;
            case CookManagedBootstrapResult::NeedsRecovery:
                return Result::NeedsRecovery;
            case CookManagedBootstrapResult::CommittedButError:
                return Result::CommittedButError;
            case CookManagedBootstrapResult::Busy:
                return Result::Busy;
            case CookManagedBootstrapResult::Conflict:
                return Result::Conflict;
            default:
                return Result::Error;
            }
#endif
        }
    } // 名前空間
    bool Detail::ResolveTextureCliPath(const std::filesystem::path& path, const std::filesystem::path& cwd,
                                       std::filesystem::path& out, Text& error, bool bDirectory)
    {
#if !defined(_WIN32)
        (void)path;
        (void)cwd;
        (void)out;
        (void)bDirectory;
        Fail(error, "windows_required");
        return false;
#else
        if (path.empty() || (path.has_root_path() && !path.is_absolute()))
        {
            Fail(error, "drive_relative_or_empty_path");
            return false;
        }
        const auto absolute = path.is_absolute() ? path : cwd / path;
        if (!Paths::LocalDrivePath(absolute))
        {
            Fail(error, "local_drive_required");
            return false;
        }
        // dotを畳む前に各実componentを共有validatorで検査する。CON/../safe等も拒否する。
        size_t components = 0;
        for (const auto& part : absolute.relative_path())
        {
            if (++components > MaximumCookLocatorComponents)
            {
                Fail(error, "path_component_limit");
                return false;
            }
            if (part == "." || part == ".." || (bDirectory && part.empty()))
            {
                continue;
            }
            std::filesystem::path checked;
            if (!NormalizeCookGuardLocator(absolute.root_path() / part, checked, error))
            {
                return false;
            }
        }
        auto normalized = absolute.lexically_normal();
        if (bDirectory)
        {
            while (normalized != normalized.root_path() && !normalized.has_filename())
            {
                normalized = normalized.parent_path();
            }
        }
        if (normalized == normalized.root_path())
        {
            out = normalized;
            return true;
        }
        return NormalizeCookGuardLocator(normalized, out, error);
#endif
    }
    TextureAssetSetCookResult CookTextureAssetSetWithOutcome(const TextureAssetSetCookRequest& request,
                                                             CookManagedBootstrapOutcome& out, Text& error)
    {
        error.clear();
        try
        {
            CookBatchReport local;
            auto selected = request;
            selected.Report = request.Report ? request.Report : &local;
            *selected.Report = {};
            auto result = CookImpl(selected, out, error);
            auto& report = *selected.Report;
            const bool succeeded = result == Result::Created || result == Result::Updated || result == Result::NoChange;
            report.bFailed = !succeeded;
            report.Error = error;
            if (!succeeded && report.BudgetErrors && !report.bWarnBudget)
            {
                result = Result::BudgetExceeded;
            }
            Text reportError;
            if (!WriteCookBatchReport(report, reportError))
            {
                if (result == Result::BudgetExceeded)
                {
                    error += "; report: " + reportError;
                    return result;
                }
                error = reportError;
                return succeeded ? Result::CommittedButError : Result::Error;
            }
            return result;
        }
        catch (const std::exception&)
        {
            return Fail(error, "adapter_exception");
        }
    }
    bool CookTextureAssetSet(const TextureAssetSetCookRequest& request, Text& error)
    {
        CookManagedBootstrapOutcome out;
        const auto result = CookTextureAssetSetWithOutcome(request, out, error);
        return result == Result::Created || result == Result::Updated || result == Result::NoChange;
    }
} // 名前空間 NorvesLib::Tools::AssetCook
