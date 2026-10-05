// texture v1の不在runtime用stageを準備し、共通controllerへ公開を委譲する。
#include "CookManagedBootstrap.h"
#include "CookManagedTransactionController.h"
#include "CookManagedBootstrapTestAccess.h"
#include "CookManagedStoreAccess.h"
#include "CookManagedStoreNative.h"
#include "CookManagedTransactionIntent.h"
#include "CookOutputSetGuard.h"
#include "CookReferenceValues.h"
#include "TextureAssetSetOutput.h"
#include "Text/JsonDocument.h"
#include "Text/UnicodeText.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#include <bcrypt.h>
#endif
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using namespace Detail::ManagedTransaction;
        using BootResult = CookManagedBootstrapResult;
        using BootstrapProbe = Detail::CookManagedBootstrapProbe;
        using BootstrapPoint = Detail::CookManagedBootstrapPoint;
        bool ForwardCheckpoint(Point point, const std::filesystem::path& transaction,
                               const std::filesystem::path& runtime, size_t, void* context)
        {
            const auto& probe = *static_cast<const BootstrapProbe*>(context);
            if (static_cast<unsigned>(point) > static_cast<unsigned>(Point::RollbackRootRestored))
            {
                return true;
            }
            return !probe.Checkpoint ||
                   probe.Checkpoint(static_cast<BootstrapPoint>(point), transaction, runtime, probe.Context);
        }
        Probe Adapt(const BootstrapProbe& probe)
        {
            Probe out;
            out.Checkpoint = ForwardCheckpoint;
            out.Context = const_cast<BootstrapProbe*>(&probe);
            out.LockFault = probe.LockFault;
            out.bAbandonedObserved = probe.bAbandonedObserved;
            return out;
        }
#if defined(_WIN32)
        struct BootstrapOperation
        {
            Operation Transaction;
            const CookManagedBootstrapRequest* Request = nullptr;
            BootResult Status = BootResult::Error;
        };
        bool EnsureParents(const Identity& root, View relative, Identity& parent, Text& error)
        {
            if (!P::SafeOutputName(relative) || relative.size() > MaximumCookStateStringBytes)
            {
                return Fail(error, "relative_package");
            }
            Identity current = root;
            size_t start = 0;
            for (size_t i = 0; i < relative.size(); ++i)
            {
                if (relative[i] != '/')
                {
                    continue;
                }
                const auto leaf = Leaf({relative.data() + start, i - start});
                N::Entries entries;
                const N::Entry* existing = nullptr;
                if (!Find(current, leaf, existing, entries, error))
                {
                    return false;
                }
                Identity next;
                if (existing)
                {
                    if (!N::Directory(current.Canonical / leaf, next, error) || !N::Direct(current, next) ||
                        next.Canonical.filename().native() != leaf.native())
                    {
                        return false;
                    }
                }
                else if (!NewDirectory(current, leaf, next, error))
                {
                    return false;
                }
                current = std::move(next);
                start = i + 1;
            }
            parent = std::move(current);
            return true;
        }
        bool PlanScope(const CookPreparedPlan& plan, const CookStateBinding& binding, Text& error)
        {
            if (plan.Outputs.size() != 1 || plan.Outputs[0].ExpectedIdentity.Kind != Core::Asset::AssetKind::Texture)
            {
                return Fail(error, "texture_v1_only");
            }
            Text target, manifest, expected = binding.RuntimeRootIdentity;
            expected.push_back('/');
            expected.append(plan.Outputs[0].ExpectedIdentity.CookedPackage);
            if (!P::AsciiPath(plan.Outputs[0].TargetPath.lexically_normal(), target) || target != expected)
            {
                return Fail(error, "final_package_scope");
            }
            expected = binding.RuntimeRootIdentity;
            expected.push_back('/');
            expected.append(binding.ManifestName);
            if (!P::AsciiPath(plan.Context.Request.ManifestPath.lexically_normal(), manifest) || manifest != expected)
            {
                return Fail(error, "final_manifest_scope");
            }
            return true;
        }
        bool SamePlan(const CookPreparedPlan& a, const CookPreparedPlan& b)
        {
            if (a.Context.Dependencies.SchemaVersion != b.Context.Dependencies.SchemaVersion ||
                a.Context.Dependencies.CookerRevision != b.Context.Dependencies.CookerRevision ||
                a.Context.Dependencies.Fingerprint != b.Context.Dependencies.Fingerprint ||
                a.Outputs.size() != b.Outputs.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.Outputs.size(); ++i)
            {
                if (!Detail::CookReferenceValues::SameIdentity(a.Outputs[i].ExpectedIdentity,
                                                               b.Outputs[i].ExpectedIdentity) ||
                    a.Outputs[i].TargetPath != b.Outputs[i].TargetPath)
                {
                    return false;
                }
            }
            return true;
        }
        bool Fresh(Operation& op, const CookManagedBootstrapRequest& request, const CookDestinationLockContext& lock,
                   const CookResolvedOwnerBinding& owner, const File& spec, const File& beforeIndex,
                   const Array<CookPreparedPlan>& plans, Text& error)
        {
            CookManagedStoreObservation observation;
            if (Detail::ObserveCookManagedStoreLocked(lock, request.Owner, observation, error) !=
                    CookManagedStoreResult::Observed ||
                observation.Owner.bFinalRuntimeRootPresent ||
                observation.Owner.ExpectedBinding.OwnerId != owner.ExpectedBinding.OwnerId ||
                observation.Owner.Identity.CanonicalSpecLocator != owner.Identity.CanonicalSpecLocator ||
                observation.Owner.Identity.CanonicalFinalRuntimeRootIdentity !=
                    owner.Identity.CanonicalFinalRuntimeRootIdentity)
            {
                return Fail(error, "owner_or_store_changed");
            }
            File currentSpec, currentIndex, currentHeader;
            if (!ReadSpec(owner, currentSpec, error) || !Same(currentSpec.Image, spec.Image) ||
                !EqualBytes(currentSpec.Data, request.ExpectedSpecBytes) ||
                !ReadChild(op.Scope.Store, L"roots.json", MaximumCookStoreIndexBytes, currentIndex, error) ||
                !Same(currentIndex.Image, beforeIndex.Image) ||
                !ReadChild(op.Scope.Store, L"header.json", 16384, currentHeader, error) ||
                !Same(currentHeader.Image, op.Scope.Header.Image))
            {
                return Fail(error, "control_or_spec_changed");
            }
            Array<CookPreparedPlan> fresh;
            for (const auto& plan : plans)
            {
                CookPreparedPlan candidate;
                if (!PrepareCookOutputPlan(plan.Context.Request, request.CookerRevision, nullptr, candidate, error) ||
                    !SamePlan(plan, candidate))
                {
                    return Fail(error, "dependencies_changed");
                }
                fresh.push_back(std::move(candidate));
            }

            // guardのlocatorはdrive-formが必要。source specとFINAL全依存はここで再検査する。
            return ValidateCookOutputSet(fresh, {&owner.SpecLocator, 1}, error);
        }
        bool PrepareBootstrap(Operation& op, const CookManagedBootstrapRequest& request,
                              const CookDestinationLockContext& lock)
        {
            if (request.Assets.empty() || !request.Assets.data() || request.Assets.size() > MaximumCookSetPlans ||
                request.ExpectedSpecBytes.empty() || !request.ExpectedSpecBytes.data() ||
                request.ExpectedSpecBytes.size() > MaximumCookStateBytes || !request.CookerRevision)
            {
                return Fail(op.Error, "request_limits");
            }
            CookManagedStoreObservation observation;
            const auto observed = Detail::ObserveCookManagedStoreLocked(lock, request.Owner, observation, op.Error);
            if (observed != CookManagedStoreResult::Observed || observation.Owner.bFinalRuntimeRootPresent)
            {
                op.bConflict = true;
                return Fail(op.Error, "absent_runtime_and_initialized_store_required");
            }
            const auto& owner = observation.Owner;
            File spec, beforeIndex;
            if (!ReadSpec(owner, spec, op.Error) || !EqualBytes(spec.Data, request.ExpectedSpecBytes))
            {
                return Fail(op.Error, "parsed_spec_changed");
            }
            if (!ReadChild(op.Scope.Store, L"roots.json", MaximumCookStoreIndexBytes, beforeIndex, op.Error))
            {
                return false;
            }
            CookManagedStoreIndex index;
            if (!ParseCookManagedStoreIndex(beforeIndex.Data, op.Scope.StoreId,
                                            MaximumCookStoreRoots - op.Scope.AncestorRoots, index, op.Error) ||
                index.Generation == UINT64_MAX || index.Roots.size() + op.Scope.AncestorRoots >= MaximumCookStoreRoots)
            {
                return Fail(op.Error, "index_capacity");
            }
            Array<CookPreparedPlan> plans, stages;
            Array<Identity> workDirectories;
            Array<std::filesystem::path> workLeaves;
            for (const auto& asset : request.Assets)
            {
                if (asset.Kind != "texture")
                {
                    return Fail(op.Error, "texture_v1_only");
                }
                CookPreparedPlan plan;
                if (!PrepareCookOutputPlan(asset, request.CookerRevision, nullptr, plan, op.Error) ||
                    !PlanScope(plan, owner.ExpectedBinding, op.Error))
                {
                    return false;
                }
                plans.push_back(std::move(plan));
            }
            if (!ValidateCookOutputSet(plans, {&owner.SpecLocator, 1}, op.Error))
            {
                return false;
            }
            Text transaction, claim, stageName;
            if (!Token(transaction, op.Error) || !Token(claim, op.Error))
            {
                return false;
            }
            stageName = ".transaction-stage-";
            stageName.append(transaction);
            if (!NewDirectory(op.Scope.Store, Leaf(stageName), op.Scope.Pending, op.Error) ||
                !Absent(op.Scope.Store, StateLeaf(claim), op.Error))
            {
                return false;
            }
            Identity root;
            if (!NewDirectory(op.Scope.Pending, L"root.stage", root, op.Error))
            {
                return false;
            }
            const auto callerStage = owner.FinalRuntimeRootLocator.parent_path() / N::StoreLeaf / Leaf(stageName);
            for (size_t i = 0; i < plans.size(); ++i)
            {
                Text name = "work-";
                char number[32];
                const auto written = std::to_chars(number, number + sizeof(number), i);
                name.append(number, static_cast<size_t>(written.ptr - number));
                Identity directory;
                if (!NewDirectory(op.Scope.Pending, Leaf(name), directory, op.Error))
                {
                    return false;
                }
                CookPreparedPlan staged;
                if (!PrepareCookStagingPlan(plans[i], callerStage / Leaf(name), staged, op.Error))
                {
                    return false;
                }
                workLeaves.push_back(Leaf(name));
                workDirectories.push_back(std::move(directory));
                stages.push_back(std::move(staged));
            }
            if (!ValidateCookStagingOutputSet(stages, {&owner.SpecLocator, 1}, op.Error))
            {
                return false;
            }
            CookManagedIntentBuildInput input;
            auto& draft = input.Draft;
            draft.TransactionId = transaction;
            draft.ClaimId = claim;
            draft.Anchor.StoreId = op.Scope.StoreId;
            draft.Anchor.Workspace = Object(op.Scope.Workspace);
            draft.Anchor.Store = Object(op.Scope.Store);
            draft.Anchor.Pending = Object(op.Scope.Pending);
            draft.Anchor.Binding = owner.ExpectedBinding;
            if (!P::AsciiPath(owner.FinalRuntimeRootLocator.filename(), draft.Anchor.RootLeaf))
            {
                return Fail(op.Error, "runtime_leaf");
            }
            draft.Root = Object(root);
            draft.IndexGeneration = index.Generation + 1;
            draft.StateGeneration = 1;
            draft.Controls[0] = beforeIndex.Image;
            draft.ManifestBefore.Parent = draft.Root;
            CookOwnedState state;
            state.Binding = owner.ExpectedBinding;
            Array<Core::Asset::AssetCookedReference> references;
            for (size_t i = 0; i < stages.size(); ++i)
            {
                if (!CookSingleAsset(stages[i].Context.Request, op.Error))
                {
                    return false;
                }
                File fragmentFile;
                Core::Asset::AssetManifest fragment;
                CookOutputRecord record;
                if (!ReadChild(workDirectories[i], Leaf(owner.ExpectedBinding.ManifestName), MaximumCookStateBytes,
                               fragmentFile, op.Error) ||
                    !LoadManifest(fragmentFile.Data, fragment, op.Error) ||
                    !CaptureStagedCookOutputRecord(plans[i], stages[i], fragment, record, op.Error) ||
                    record.Outputs.size() != 1)
                {
                    return Fail(op.Error, "staged_capture");
                }
                Array<TreeNode> work;
                if (!ScanTree(workDirectories[i], work, nullptr, op.Error))
                {
                    return false;
                }
                const auto& recorded = record.Outputs[0];
                const TreeNode* package = nullptr;
                for (const auto& node : work)
                {
                    if (node.bDirectory)
                    {
                        Text prefix = node.Relative;
                        prefix.push_back('/');
                        if (recorded.Reference.CookedPackage.size() <= prefix.size() ||
                            std::memcmp(recorded.Reference.CookedPackage.data(), prefix.data(), prefix.size()))
                        {
                            return Fail(op.Error, "unexpected_work_directory");
                        }
                    }
                    else if (node.Relative == recorded.Reference.CookedPackage)
                    {
                        package = &node;
                    }
                    else if (node.Relative != owner.ExpectedBinding.ManifestName)
                    {
                        return Fail(op.Error, "unexpected_work_file");
                    }
                }
                if (!package || package->Image.Size != recorded.Package.Size ||
                    package->Image.ContentHash != recorded.Package.ContentHash)
                {
                    return Fail(op.Error, "staged_package_changed");
                }
                Identity parent;
                if (!EnsureParents(root, recorded.Reference.CookedPackage, parent, op.Error) ||
                    !Rename(package->Parent, package->Native.Canonical.filename(), package->Image.Object, parent,
                            Leaf(recorded.Reference.CookedPackage).filename(), false, &package->Image, op.Error))
                {
                    return false;
                }
                CookManagedPackageMutation mutation;
                mutation.Package = recorded.Reference.CookedPackage;
                mutation.Before.Parent = Object(parent);
                mutation.After = package->Image;
                draft.Packages.push_back(std::move(mutation));
                references.push_back(recorded.Reference);
                CookOwnedRecord owned;
                owned.PrimaryKey = {recorded.Reference.LogicalPath, recorded.Reference.Kind,
                                    recorded.Reference.Variant};
                owned.Record = std::move(record);
                state.Records.push_back(std::move(owned));
                if (!DeleteKnown(workDirectories[i], Leaf(owner.ExpectedBinding.ManifestName),
                                 fragmentFile.Image.Object, false, &fragmentFile.Image, op.Error))
                {
                    return false;
                }
                std::sort(work.begin(), work.end(),
                          [](const auto& a, const auto& b)
                          {
                              return a.Relative.size() > b.Relative.size();
                          });
                for (const auto& node : work)
                {
                    if (node.bDirectory && !DeleteKnown(node.Parent, node.Native.Canonical.filename(),
                                                        Object(node.Native), true, nullptr, op.Error))
                    {
                        return false;
                    }
                }
                if (!DeleteKnown(op.Scope.Pending, workLeaves[i], Object(workDirectories[i]), true, nullptr, op.Error))
                {
                    return false;
                }
            }
            Text aggregate;
            File manifest;
            if (!Detail::SerializeLegacyTextureManifest(references, aggregate, op.Error) ||
                !NewFile(root, Leaf(owner.ExpectedBinding.ManifestName), BytesOf(aggregate), manifest, op.Error))
            {
                return false;
            }
            draft.ManifestAfter = manifest.Image;
            Array<TreeNode> tree;
            if (!ScanTree(root, tree, nullptr, op.Error))
            {
                return false;
            }
            for (const auto& node : tree)
            {
                if (node.bDirectory)
                {
                    draft.Directories.push_back({node.Relative, Object(node.Native), Object(node.Parent)});
                }
            }
            CookManagedRootClaim rootClaim;
            rootClaim.ClaimId = claim;
            rootClaim.RootLeaf = draft.Anchor.RootLeaf;
            rootClaim.OwnerId = owner.ExpectedBinding.OwnerId;
            rootClaim.DirectoryId = root.FileId;
            index.Generation = draft.IndexGeneration;
            index.Roots.push_back(rootClaim);
            Text indexText, stateText, receiptText, wire;
            File afterIndex, afterState, receipt, intentFile;
            if (!SerializeCookManagedStoreIndex(index, indexText, op.Error) ||
                !SerializeCookOwnedState(state, stateText, op.Error) ||
                !NewFile(op.Scope.Pending, L"index.after", BytesOf(indexText), afterIndex, op.Error) ||
                !NewFile(op.Scope.Pending, L"state.after", BytesOf(stateText), afterState, op.Error))
            {
                return false;
            }
            draft.Controls[1] = afterIndex.Image;
            draft.Controls[3] = afterState.Image;
            if (!MakeCookManagedReceiptBody(draft, receiptText, op.Error) ||
                !NewFile(op.Scope.Pending, L"receipt.stage", BytesOf(receiptText), receipt, op.Error))
            {
                return false;
            }
            draft.Receipt = receipt.Image;
            input.FinalPlans = plans;
            input.FinalRecords = state.Records;
            CookManagedControlDocuments docs;
            docs.Controls[0] = {beforeIndex.Image.Object, beforeIndex.Data};
            docs.Controls[1] = {afterIndex.Image.Object, afterIndex.Data};
            docs.Controls[3] = {afterState.Image.Object, afterState.Data};
            docs.ManifestAfter = {manifest.Image.Object, manifest.Data};
            CookManagedTransactionIntent intent;
            if (!BuildCookManagedTransactionIntent(input, docs, intent, op.Error) ||
                !SerializeCookManagedTransactionIntent(intent, wire, op.Error) ||
                !NewFile(op.Scope.Pending, L"intent.json", BytesOf(wire), intentFile, op.Error) ||
                !Checkpoint(op, Point::Prepared) ||
                !Fresh(op, request, lock, owner, spec, beforeIndex, plans, op.Error) ||
                !Absent(op.Scope.Workspace, Leaf(draft.Anchor.RootLeaf), op.Error) ||
                !Absent(op.Scope.Store, StateLeaf(claim), op.Error))
            {
                return false;
            }
            if (!ValidatePrepared(op))
            {
                return false;
            }
            const auto id = Object(op.Scope.Pending);
            if (!Rename(op.Scope.Store, Leaf(stageName), id, op.Scope.Store, L"pending", true, nullptr, op.Error,
                        &op.bPendingPublished))
            {
                return false;
            }
            if (!N::Directory(op.Scope.Store.Canonical / L"pending", op.Scope.Pending, op.Error) ||
                !Same(op.Scope.Pending, id) || !Checkpoint(op, Point::PendingPublished))
            {
                return false;
            }
            return true;
        }
        bool BootstrapCallback(const CookDestinationLockContext& lock, void* data, Text& error)
        {
            auto& work = *static_cast<BootstrapOperation*>(data);
            auto& op = work.Transaction;
            if (op.Test && op.Test->bAbandonedObserved)
            {
                *op.Test->bAbandonedObserved = lock.bAbandoned;
            }
            if (!OpenStore(lock, op.Scope, op.Error))
            {
                op.bConflict = true;
                error = op.Error;
                return false;
            }
            if (op.Scope.bPending)
            {
                op.bPendingPublished = true;
                return Fail(op.Error, "pending_requires_recovery");
            }
            const bool bSuccess = PrepareBootstrap(op, *work.Request, lock) && PublishBootstrap(op);
            if (bSuccess)
            {
                work.Status = BootResult::Created;
            }
            error = op.Error;
            return bSuccess;
        }
#endif
        BootResult BootstrapImpl(const CookManagedBootstrapRequest& request, const BootstrapProbe* probe,
                                 CookManagedBootstrapOutcome& out, Text& error)
        {
            error.clear();
#if !defined(_WIN32)
            (void)request;
            (void)probe;
            (void)out;
            Fail(error, "windows_required");
            return BootResult::Error;
#else
            BootstrapOperation work;
            work.Request = &request;
            auto& op = work.Transaction;
            Probe adapted;
            if (probe)
            {
                adapted = Adapt(*probe);
                op.Test = &adapted;
            }
            op.RuntimeLocator = request.Owner.FinalRuntimeRoot;
            try
            {
                const auto result =
                    probe ? Detail::WithCookDestinationLockForTest({request.Owner.FinalRuntimeRoot}, BootstrapCallback,
                                                                   &work, error, probe->LockFault)
                          : WithCookDestinationLock({request.Owner.FinalRuntimeRoot}, BootstrapCallback, &work, error);
                if (result == CookDestinationLockResult::Busy)
                {
                    return BootResult::Busy;
                }
                if (result == CookDestinationLockResult::Executed && work.Status == BootResult::Created)
                {
                    out = std::move(op.Outcome);
                    error.clear();
                    return BootResult::Created;
                }
            }
            catch (const std::exception&)
            {
                Fail(error, "bootstrap_exception");
            }
            if (!op.Error.empty())
            {
                error = op.Error;
            }
            if (op.bPendingPublished && !op.bRetired)
            {
                error.append("; pending_preserved");
                return BootResult::NeedsRecovery;
            }
            if (op.bCommitted && op.bRetired)
            {
                error.append("; committed_state_preserved");
                return BootResult::CommittedButError;
            }
            return op.bConflict ? BootResult::Conflict : BootResult::Error;
#endif
        }
        RecoveryResult RecoveryImpl(const std::filesystem::path& runtime, const BootstrapProbe* probe,
                                    CookManagedBootstrapOutcome& out, Text& error)
        {
            Probe adapted;
            if (probe)
            {
                adapted = Adapt(*probe);
            }
            return Recover(runtime, probe ? &adapted : nullptr, out, error);
        }
    } // 名前空間
    CookManagedBootstrapResult BootstrapCookManagedAssetSet(const CookManagedBootstrapRequest& request,
                                                            CookManagedBootstrapOutcome& out, Text& error)
    {
        return BootstrapImpl(request, nullptr, out, error);
    }
    CookManagedRecoveryResult RecoverCookManagedPending(const std::filesystem::path& runtime,
                                                        CookManagedBootstrapOutcome& out, Text& error)
    {
        return RecoveryImpl(runtime, nullptr, out, error);
    }
    CookManagedBootstrapResult Detail::BootstrapCookManagedAssetSetForTest(const CookManagedBootstrapRequest& request,
                                                                           const CookManagedBootstrapProbe& probe,
                                                                           CookManagedBootstrapOutcome& out,
                                                                           Text& error)
    {
        return BootstrapImpl(request, &probe, out, error);
    }
    CookManagedRecoveryResult Detail::RecoverCookManagedPendingForTest(const std::filesystem::path& runtime,
                                                                       const CookManagedBootstrapProbe& probe,
                                                                       CookManagedBootstrapOutcome& out, Text& error)
    {
        return RecoveryImpl(runtime, &probe, out, error);
    }
} // 名前空間 NorvesLib::Tools::AssetCook
