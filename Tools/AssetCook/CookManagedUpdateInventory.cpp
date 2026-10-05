// 旧stateが宣言するtargetと新planを対応付ける。実fileの所有証明やpublicationは行わない。
#include "CookManagedUpdateInventory.h"
#include "CookOutputSetGuard.h"
#include "CookPathIdentity.h"
#include "CookOutputPaths.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using InventoryText = Core::Container::AnsiString;
        using InventoryView = Core::Container::AnsiStringView;
        using Core::Container::VariableArray;
        using Reference = Core::Asset::AssetCookedReference;
        struct Key
        {
            Core::Asset::AssetKind Kind;
            InventoryView Logical, Variant;
        };
        Key View(const CookStateKey& key)
        {
            return {key.Kind, key.LogicalPath, key.Variant};
        }
        Key View(const Reference& ref)
        {
            return {ref.Kind, ref.LogicalPath, ref.Variant};
        }
        int Compare(InventoryView a, InventoryView b)
        {
            const size_t n = std::min(a.size(), b.size());
            const int c = n ? std::memcmp(a.data(), b.data(), n) : 0;
            if (c)
            {
                return c;
            }
            return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
        }
        int Compare(Key a, Key b)
        {
            if (a.Kind != b.Kind)
            {
                return static_cast<unsigned>(a.Kind) < static_cast<unsigned>(b.Kind) ? -1 : 1;
            }
            const int c = Compare(a.Logical, b.Logical);
            return c ? c : Compare(a.Variant, b.Variant);
        }
        bool Equal(InventoryView a, InventoryView b)
        {
            return Compare(a, b) == 0;
        }
        bool Fail(InventoryText& error, const char* reason)
        {
            error = "cook_managed_inventory: ";
            error.append(reason);
            return false;
        }
        bool Budget(size_t amount, size_t& total, InventoryText& error)
        {
            if (amount > MaximumCookManagedInventoryBytes - total)
            {
                return Fail(error, "metadata_limit");
            }
            total += amount;
            return true;
        }
        bool TextBudget(InventoryView text, size_t& total, InventoryText& error)
        {
            return text.size() <= MaximumCookStateStringBytes ? Budget(text.size(), total, error)
                                                              : Fail(error, "string_limit");
        }
        bool PathBudget(const std::filesystem::path& path, size_t& total, InventoryText& error)
        {
            const auto count = path.native().size();
            if (count > Detail::MaximumCookLocatorUnits)
            {
                return Fail(error, "locator_limit");
            }
            return Budget(count * sizeof(std::filesystem::path::value_type), total, error);
        }
        bool ReferenceBudget(const Reference& ref, size_t& total, InventoryText& error)
        {
            for (const auto* text : {&ref.LogicalPath, &ref.SourceHashHex, &ref.Variant, &ref.Format,
                                     &ref.CookedPackage, &ref.EntryName, &ref.EntryTypeText, &ref.CookedHashHex})
            {
                if (!TextBudget(*text, total, error))
                {
                    return false;
                }
            }
            return true;
        }
        bool PlanBudget(const CookPreparedPlan& plan, size_t& total, size_t& dependencies, InventoryText& error)
        {
            const auto& r = plan.Context.Request;
            for (const auto* path : {&r.InputPath, &r.PackagePath, &r.ManifestPath, &r.ImportSettingsOverridePath})
            {
                if (!PathBudget(*path, total, error))
                {
                    return false;
                }
            }
            for (const auto* text : {&r.LogicalPath, &r.Kind, &r.EntryName, &r.EntryTypeText, &r.Format, &r.Variant})
            {
                if (!TextBudget(*text, total, error))
                {
                    return false;
                }
            }
            if (plan.Context.Dependencies.SchemaVersion != 1 || plan.Context.Dependencies.CookerRevision == 0 ||
                plan.Context.Dependencies.Files.size() > MaximumCookSetProtectedOccurrences - dependencies)
            {
                return Fail(error, "dependency_profile_or_limit");
            }
            dependencies += plan.Context.Dependencies.Files.size();
            for (const auto& file : plan.Context.Dependencies.Files)
            {
                if (!PathBudget(file.Path, total, error))
                {
                    return false;
                }
            }
            for (const auto& output : plan.Outputs)
            {
                if (!PathBudget(output.TargetPath, total, error) ||
                    !ReferenceBudget(output.ExpectedIdentity, total, error))
                {
                    return false;
                }
            }
            return true;
        }
        bool PrimaryConsistent(const CookPreparedPlan& plan, InventoryText& error)
        {
            const auto& request = plan.Context.Request;
            const auto& primary = plan.Outputs[0].ExpectedIdentity;
            Core::Asset::AssetKind kind;
            Core::Asset::AssetPackageFourCC entry;
            if (request.bSkipIfUnchanged || !Core::Asset::TryParseAssetKind(request.Kind, kind) ||
                !Core::Asset::TryParseAssetPackageFourCCText(request.EntryTypeText, entry) || kind != primary.Kind ||
                entry != primary.EntryType || !Equal(request.LogicalPath, primary.LogicalPath) ||
                !Equal(request.Variant, primary.Variant) || !Equal(request.Format, primary.Format) ||
                !Equal(request.EntryName, primary.EntryName))
            {
                return Fail(error, "inconsistent_primary_plan");
            }
            return true;
        }
        bool BindingEqual(const CookStateBinding& a, const CookStateBinding& b)
        {
            return Equal(a.OwnerId, b.OwnerId) && Equal(a.RuntimeRootIdentity, b.RuntimeRootIdentity) &&
                   Equal(a.ManifestName, b.ManifestName);
        }
#if defined(_WIN32)
        bool Prefix(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            const auto& x = a.native();
            const auto& y = b.native();
            return x.size() <= y.size() &&
                   CompareStringOrdinal(x.data(), static_cast<int>(x.size()), y.data(), static_cast<int>(x.size()),
                                        TRUE) == CSTR_EQUAL &&
                   (x.size() == y.size() || y[x.size()] == static_cast<wchar_t>(92));
        }
#endif
        bool Normalize(const std::filesystem::path& path, std::filesystem::path& out, InventoryText& error)
        {
            return Detail::NormalizeCookGuardLocator(path, out, error);
        }
        bool Matches(const std::filesystem::path& path, const std::filesystem::path& expected, InventoryText& error)
        {
            std::filesystem::path normalized, normalizedExpected;
            if (!Normalize(path, normalized, error) || !Normalize(expected, normalizedExpected, error))
            {
                return false;
            }
            return normalized.native() == normalizedExpected.native() ? true : Fail(error, "declared_target_mismatch");
        }
    } // namespace
    bool BuildCookManagedUpdateInventory(const CookStateFileRequest& scope, const CookOwnedState& previous,
                                         Core::Container::Span<const CookPreparedPlan> plans,
                                         CookManagedUpdateInventory& out, InventoryText& error)
    {
        error.clear();
#if !defined(_WIN32)
        (void)scope;
        (void)previous;
        (void)plans;
        (void)out;
        return Fail(error, "windows_locator_profile_required");
#else
        try
        {
            if (plans.empty() || !plans.data() || plans.size() > MaximumCookSetPlans ||
                previous.Records.size() != plans.size())
            {
                return Fail(error, "primary_inventory_changed_or_limit");
            }
            if (!IsValidCookStateBinding(scope.ExpectedBinding) ||
                !BindingEqual(scope.ExpectedBinding, previous.Binding))
            {
                return Fail(error, "independent_binding_mismatch");
            }
            if (std::strchr(scope.ExpectedBinding.ManifestName.c_str(), '/'))
            {
                return Fail(error, "flat_manifest_profile_required");
            }
            CookManagedUpdateInventory candidate;
            candidate.Scope.ExpectedBinding = scope.ExpectedBinding;
            if (!Normalize(scope.RuntimeRoot, candidate.Scope.RuntimeRoot, error) ||
                !Normalize(scope.StatePath, candidate.Scope.StatePath, error))
            {
                return false;
            }
            InventoryText rootText, stateText;
            if (!Detail::CookOutputPaths::AsciiPath(candidate.Scope.RuntimeRoot, rootText) ||
                !Equal(rootText, scope.ExpectedBinding.RuntimeRootIdentity) ||
                !Detail::CookOutputPaths::AsciiPath(candidate.Scope.StatePath, stateText))
            {
                return Fail(error, "lexical_scope_mismatch");
            }
            if (Prefix(candidate.Scope.RuntimeRoot, candidate.Scope.StatePath) ||
                Prefix(candidate.Scope.StatePath, candidate.Scope.RuntimeRoot))
            {
                return Fail(error, "lexical_state_scope_conflict");
            }
            candidate.ManifestTarget =
                candidate.Scope.RuntimeRoot / std::filesystem::path(scope.ExpectedBinding.ManifestName.c_str());
            candidate.ManifestTarget.make_preferred();
            // codecを共有し、derived hex文字列も正規化して値所有する。元JSON bytesのbackupには使わない。
            InventoryText serialized;
            if (!SerializeCookOwnedState(previous, serialized, error) ||
                !ParseCookOwnedState({reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size()},
                                     scope.ExpectedBinding, candidate.PreviousState, error))
            {
                return false;
            }
            size_t budget = 0, dependencies = 0, outputs = 0;
            if (!Budget(serialized.size(), budget, error) || !PathBudget(candidate.Scope.RuntimeRoot, budget, error) ||
                !PathBudget(candidate.Scope.StatePath, budget, error) ||
                !PathBudget(candidate.ManifestTarget, budget, error) ||
                !TextBudget(scope.ExpectedBinding.OwnerId, budget, error) ||
                !TextBudget(scope.ExpectedBinding.RuntimeRootIdentity, budget, error) ||
                !TextBudget(scope.ExpectedBinding.ManifestName, budget, error))
            {
                return false;
            }
            VariableArray<size_t> oldOrder, newOrder;
            oldOrder.reserve(plans.size());
            newOrder.reserve(plans.size());
            for (size_t i = 0; i < plans.size(); ++i)
            {
                if (plans[i].Outputs.empty() || plans[i].Outputs.size() > MaximumCookSetOutputs - outputs)
                {
                    return Fail(error, "output_inventory_limit");
                }
                outputs += plans[i].Outputs.size();
                if (!PrimaryConsistent(plans[i], error) || !PlanBudget(plans[i], budget, dependencies, error) ||
                    !Matches(plans[i].Context.Request.ManifestPath, candidate.ManifestTarget, error) ||
                    !Matches(plans[i].Context.Request.PackagePath, plans[i].Outputs[0].TargetPath, error))
                {
                    return false;
                }
                oldOrder.push_back(i);
                newOrder.push_back(i);
            }
            const auto& records = candidate.PreviousState.Records;
            std::sort(oldOrder.begin(), oldOrder.end(),
                      [&](size_t a, size_t b)
                      {
                          return Compare(View(records[a].PrimaryKey), View(records[b].PrimaryKey)) < 0;
                      });
            std::sort(newOrder.begin(), newOrder.end(),
                      [&](size_t a, size_t b)
                      {
                          return Compare(View(plans[a].Outputs[0].ExpectedIdentity),
                                         View(plans[b].Outputs[0].ExpectedIdentity)) < 0;
                      });
            candidate.Assets.resize(plans.size());
            for (size_t i = 0; i < plans.size(); ++i)
            {
                const size_t oldIndex = oldOrder[i], newIndex = newOrder[i];
                const auto& plan = plans[newIndex];
                const auto& record = records[oldIndex];
                if (Compare(View(record.PrimaryKey), View(plan.Outputs[0].ExpectedIdentity)) != 0 ||
                    record.Record.Outputs.size() != plan.Outputs.size())
                {
                    return Fail(error, "primary_or_derived_inventory_changed");
                }
                auto& asset = candidate.Assets[newIndex];
                asset.PreviousRecordIndex = oldIndex;
                asset.FinalPlan = plan;
                asset.Packages.resize(plan.Outputs.size());
                VariableArray<size_t> before, after;
                for (size_t j = 0; j < plan.Outputs.size(); ++j)
                {
                    before.push_back(j);
                    after.push_back(j);
                }
                std::sort(before.begin(), before.end(),
                          [&](size_t a, size_t b)
                          {
                              return Compare(View(record.Record.Outputs[a].Reference),
                                             View(record.Record.Outputs[b].Reference)) < 0;
                          });
                std::sort(after.begin(), after.end(),
                          [&](size_t a, size_t b)
                          {
                              return Compare(View(plan.Outputs[a].ExpectedIdentity),
                                             View(plan.Outputs[b].ExpectedIdentity)) < 0;
                          });
                for (size_t j = 0; j < after.size(); ++j)
                {
                    const size_t oldOutput = before[j], newOutput = after[j];
                    const auto& previousRef = record.Record.Outputs[oldOutput].Reference;
                    const auto& currentRef = plan.Outputs[newOutput].ExpectedIdentity;
                    if (Compare(View(previousRef), View(currentRef)) != 0 ||
                        !Equal(previousRef.CookedPackage, currentRef.CookedPackage))
                    {
                        return Fail(error, "output_key_or_package_changed");
                    }
                    auto target =
                        candidate.Scope.RuntimeRoot / std::filesystem::path(previousRef.CookedPackage.c_str());
                    target.make_preferred();
                    if (!Matches(plan.Outputs[newOutput].TargetPath, target, error) ||
                        !PathBudget(target, budget, error))
                    {
                        return false;
                    }
                    asset.Packages[newOutput] = {newOutput, oldOutput, std::move(target),
                                                 CookBeforeImageRequirement::CaptureOwnedFileOrProveAbsence};
                }
            }
            candidate.BaseGeneration = previous.Generation;
            candidate.bCanAdvanceGeneration = previous.Generation < std::numeric_limits<uint64_t>::max();
            candidate.ProposedMutationGeneration = candidate.bCanAdvanceGeneration ? previous.Generation + 1 : 0;
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "inventory_exception");
        }
#endif
    }
} // namespace NorvesLib::Tools::AssetCook
