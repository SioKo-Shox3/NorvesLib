// filesystemの不存在でも値対応だけを検査できることを明示する。実cookは共通cache試験でも接続する。
#include "Tools/AssetCook/CookManagedUpdateInventory.h"
#include "Tools/AssetCook/CookOutputSetGuard.h"
#include "Asset/AssetPackageFormat.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace ManagedInventoryTest
{
    using namespace NorvesLib::Tools::AssetCook;
    namespace Asset = NorvesLib::Core::Asset;
    using InventoryText = NorvesLib::Core::Container::AnsiString;
    using Plans = NorvesLib::Core::Container::VariableArray<CookPreparedPlan>;
    CookRecordedOutput Output(const char* key, const char* package, Asset::AssetKind kind = Asset::AssetKind::Raw)
    {
        CookRecordedOutput out;
        auto& r = out.Reference;
        r.LogicalPath = key;
        r.Kind = kind;
        r.SourceHash = 1;
        r.CookedHash = 2;
        r.Variant = "default";
        r.Format = "raw.v0";
        r.CookedPackage = package;
        r.EntryName = "__asset__";
        r.EntryType = Asset::AssetPackageFormatV1::RawEntryType;
        out.Package = {1024, 2};
        return out;
    }
    CookOwnedRecord Owned(const char* key, const char* package)
    {
        CookOwnedRecord r;
        r.PrimaryKey = {key, Asset::AssetKind::Raw, "default"};
        r.Record.CookerRevision = 7;
        r.Record.DependencyFingerprint = 91;
        r.Record.Outputs.push_back(Output(key, package));
        return r;
    }
    CookPreparedPlan Plan(const CookStateFileRequest& scope, const CookOwnedRecord& owned)
    {
        CookPreparedPlan plan;
        const auto& primary = owned.Record.Outputs[0].Reference;
        auto& r = plan.Context.Request;
        r.InputPath = scope.RuntimeRoot.parent_path() / "source.bin";
        r.PackagePath = scope.RuntimeRoot / std::filesystem::path(primary.CookedPackage.c_str());
        r.ManifestPath = scope.RuntimeRoot / std::filesystem::path(scope.ExpectedBinding.ManifestName.c_str());
        r.LogicalPath = primary.LogicalPath;
        r.Kind = primary.Kind == Asset::AssetKind::Model ? "model" : "raw";
        r.EntryName = primary.EntryName;
        r.EntryTypeText = Asset::FormatAssetPackageFourCCText(primary.EntryType);
        r.Format = primary.Format;
        r.Variant = primary.Variant;
        plan.Context.Dependencies.CookerRevision = 7;
        plan.Context.Dependencies.Fingerprint = 91;
        for (const auto& output : owned.Record.Outputs)
        {
            auto target = scope.RuntimeRoot / std::filesystem::path(output.Reference.CookedPackage.c_str());
            plan.Outputs.push_back({output.Reference, target});
        }
        return plan;
    }
    CookManagedUpdateInventory Good(const CookStateFileRequest& scope, const CookOwnedState& previous,
                                    const Plans& plans)
    {
        CookManagedUpdateInventory out;
        InventoryText error;
        if (!BuildCookManagedUpdateInventory(scope, previous, plans, out, error))
        {
            std::fprintf(stderr, "inventory: %s\n", error.c_str());
            CHECK(false);
        }
        CHECK(error.empty());
        return out;
    }
    void Bad(const CookStateFileRequest& scope, const CookOwnedState& state, const Plans& plans,
             bool bAllowChanges = false)
    {
        CookManagedUpdateInventory held;
        held.BaseGeneration = 123;
        held.ManifestTarget = "held";
        InventoryText error;
        CHECK(!BuildCookManagedUpdateInventory(scope, state, plans, held, error, bAllowChanges));
        CHECK(!error.empty() && held.BaseGeneration == 123 && held.ManifestTarget == "held" && held.Assets.empty());
    }
} // namespace ManagedInventoryTest
int main()
{
#if !defined(_WIN32)
    return 125;
#else
    using namespace ManagedInventoryTest;
    char leaf[100];
    std::snprintf(leaf, sizeof(leaf), "norves-value-inventory-%lu-%llu", GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()));
    const auto root = std::filesystem::temp_directory_path() / leaf;
    CookStateFileRequest scope;
    scope.RuntimeRoot = root / "runtime";
    scope.StatePath = root / "state.json";
    scope.ExpectedBinding = {"0123456789abcdef0123456789abcdef", scope.RuntimeRoot.generic_string().c_str(),
                             "manifest.json"};
    CookOwnedState state;
    state.Binding = scope.ExpectedBinding;
    state.Generation = 4;
    state.Records = {Owned("Raw/A", "Cooked/A.nvpkg"), Owned("Raw/B", "Cooked/B.nvpkg")};
    Plans plans = {Plan(scope, state.Records[1]), Plan(scope, state.Records[0])};
    CHECK(!std::filesystem::exists(root));
    auto inventory = Good(scope, state, plans);
    CHECK(!std::filesystem::exists(root));
    CHECK(inventory.BaseGeneration == 4 && inventory.bCanAdvanceGeneration &&
          inventory.ProposedMutationGeneration == 5);
    CHECK(inventory.Assets.size() == 2 && inventory.Assets[0].PreviousRecordIndex == 1 &&
          inventory.Assets[1].PreviousRecordIndex == 0);
    CHECK(inventory.Assets[0].Packages[0].PlanOutputIndex == 0 &&
          inventory.Assets[0].Packages[0].PreviousOutputIndex == 0);
    CHECK(inventory.StateBefore == CookBeforeImageRequirement::CaptureExactPreviousStateFile);
    CHECK(inventory.ManifestBefore == CookBeforeImageRequirement::CaptureOwnedFileOrProveAbsence);
    auto changed = plans;
    changed[0].Context.Dependencies.CookerRevision = 8;
    changed[0].Context.Dependencies.Fingerprint = 92;
    changed[0].Outputs[0].ExpectedIdentity.SourceHash = 9;
    // 値対応は形式の変化も保持する。ここではその形式が実cook可能だとは判定しない。
    changed[0].Context.Request.Format = "raw.future";
    changed[0].Outputs[0].ExpectedIdentity.Format = "raw.future";
    CHECK(Good(scope, state, changed).Assets[0].FinalPlan.Context.Dependencies.CookerRevision == 8);
    auto badScope = scope;
    badScope.ExpectedBinding.OwnerId = "1123456789abcdef0123456789abcdef";
    Bad(badScope, state, plans);
    badScope = scope;
    badScope.RuntimeRoot = root / "other";
    Bad(badScope, state, plans);
    badScope = scope;
    badScope.StatePath = scope.RuntimeRoot / "state.json";
    Bad(badScope, state, plans);
    badScope.StatePath = "relative";
    Bad(badScope, state, plans);
    auto badState = state;
    badState.Binding.ManifestName = "nested/manifest.json";
    badScope = scope;
    badScope.ExpectedBinding = badState.Binding;
    Bad(badScope, badState, plans);
    badState = state;
    badState.Generation = 0;
    Bad(scope, badState, plans);
    badState = state;
    badState.SchemaVersion = 2;
    Bad(scope, badState, plans);
    badState = state;
    badState.Generation = std::numeric_limits<uint64_t>::max();
    auto capped = Good(scope, badState, plans);
    CHECK(!capped.bCanAdvanceGeneration && capped.ProposedMutationGeneration == 0 &&
          capped.BaseGeneration == badState.Generation);
    badState = state;
    badState.Records[0].PrimaryKey.LogicalPath = "mismatch";
    Bad(scope, badState, plans);
    changed = plans;
    changed.pop_back();
    Bad(scope, state, changed);
    changed = plans;
    changed.push_back(plans[0]);
    Bad(scope, state, changed);
    changed = plans;
    changed[1] = changed[0];
    Bad(scope, state, changed);
    Bad(scope, state, changed, true);
    changed = plans;
    changed[0].Context.Request.LogicalPath = "different";
    Bad(scope, state, changed);
    changed = plans;
    changed[0].Context.Request.bSkipIfUnchanged = true;
    Bad(scope, state, changed);
    changed = plans;
    changed[0].Outputs[0].ExpectedIdentity.CookedPackage = "Cooked/renamed.nvpkg";
    Bad(scope, state, changed);
    changed = plans;
    changed[0].Context.Request.PackagePath = root / "foreign.nvpkg";
    changed[0].Outputs[0].TargetPath = root / "foreign.nvpkg";
    Bad(scope, state, changed);
    changed = plans;
    changed[0].Context.Request.ManifestPath = root / "foreign.json";
    Bad(scope, state, changed);
    changed = plans;
    changed[0].Outputs.clear();
    Bad(scope, state, changed);
    // model primaryと派生2件を順序非依存で対応し、primaryは常に先頭のままにする。
    CookOwnedState model;
    model.Binding = scope.ExpectedBinding;
    auto owned = Owned("Model/A", "Cooked/model.nvpkg");
    owned.PrimaryKey.Kind = Asset::AssetKind::Model;
    owned.Record.Outputs[0].Reference.Kind = Asset::AssetKind::Model;
    owned.Record.Outputs[0].Reference.Format = "nvmesh.v0.mesh3d.pnt.u32.clustered";
    owned.Record.Outputs[0].Reference.EntryType = Asset::MakeAssetPackageFourCC('M', 's', 'h', '0');
    for (unsigned i = 0; i < 2; ++i)
    {
        char key[50], package[70];
        std::snprintf(key, sizeof(key), "Model/A.img%u", i);
        std::snprintf(package, sizeof(package), "Cooked/model.img%u.nvpkg", i);
        auto output = Output(key, package, Asset::AssetKind::Texture);
        output.Reference.Format = "nvtex.v0.rgba8.srgb";
        output.Reference.EntryType = Asset::MakeAssetPackageFourCC('T', 'e', 'x', '0');
        owned.Record.Outputs.push_back(output);
    }
    model.Records.push_back(owned);
    Plans modelPlans = {Plan(scope, owned)};
    std::swap(modelPlans[0].Outputs[1], modelPlans[0].Outputs[2]);
    const auto mapped = Good(scope, model, modelPlans);
    CHECK(mapped.Assets[0].Packages[1].PreviousOutputIndex == 2 &&
          mapped.Assets[0].Packages[2].PreviousOutputIndex == 1);
    changed = modelPlans;
    changed[0].Outputs.pop_back();
    Bad(scope, model, changed);
    changed = modelPlans;
    changed[0].Outputs.push_back(changed[0].Outputs[1]);
    Bad(scope, model, changed);
    changed = modelPlans;
    changed[0].Outputs[1].TargetPath = changed[0].Outputs[0].TargetPath;
    Bad(scope, model, changed);
    changed = modelPlans;
    changed[0].Outputs[1].ExpectedIdentity.LogicalPath = changed[0].Outputs[2].ExpectedIdentity.LogicalPath;
    Bad(scope, model, changed);
    Bad(scope, model, changed, true);
    // 4096recordを二乗探索せず照合する。これもfile I/Oはしない。
    CookOwnedState many;
    many.Binding = scope.ExpectedBinding;
    Plans manyPlans;
    for (size_t i = 0; i < MaximumCookSetPlans; ++i)
    {
        char key[50], package[60];
        std::snprintf(key, sizeof(key), "Many/%zu", i);
        std::snprintf(package, sizeof(package), "Cooked/%zu.nvpkg", i);
        many.Records.push_back(Owned(key, package));
        manyPlans.push_back(Plan(scope, many.Records.back()));
    }
    CHECK(Good(scope, many, manyPlans).Assets.size() == MaximumCookSetPlans);
    many.Records.push_back(many.Records[0]);
    manyPlans.push_back(manyPlans[0]);
    Bad(scope, many, manyPlans);
    changed = plans;
    changed[0].Context.Request.Format = InventoryText(MaximumCookStateStringBytes + 1, 'x');
    changed[0].Outputs[0].ExpectedIdentity.Format = changed[0].Context.Request.Format;
    Bad(scope, state, changed);
    changed = plans;
    CookDependencyFile file;
    InventoryText longPart(280, 'x');
    file.Path = root / std::filesystem::path(longPart.c_str());
    changed[0].Context.Dependencies.Files.resize(MaximumCookSetProtectedOccurrences, file);
    CookManagedUpdateInventory held;
    InventoryText error;
    CHECK(!BuildCookManagedUpdateInventory(scope, state, changed, held, error));
    CHECK(std::strstr(error.c_str(), "metadata_limit"));
    changed[0].Context.Dependencies.Files.push_back(file);
    Bad(scope, state, changed);
    // callerのstate/plan寿命を超えて値を保持する。
    state.Records.clear();
    plans.clear();
    CHECK(inventory.PreviousState.Records.size() == 2 &&
          inventory.Assets[0].FinalPlan.Outputs[0].ExpectedIdentity.LogicalPath == "Raw/B");
    CHECK(!std::filesystem::exists(root));
    std::puts("COOK_MANAGED_INVENTORY result=pass fixed_owned_mapping_order_bounds_generation_hold_values_only");
    return 0;
#endif
}
