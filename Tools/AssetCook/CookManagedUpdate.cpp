// 既存texture rootの増分判断とstage準備。復旧・公開機構は共通controllerへ委譲する。
#include "CookManagedUpdate.h"
#include "CookManagedUpdateTestAccess.h"
#include "CookManagedUpdateInventory.h"
#include "CookManagedStoreAccess.h"
#include "CookOutputSetGuard.h"
#include "CookReferenceValues.h"
#include "TextureAssetSetOutput.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <exception>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using namespace Detail::ManagedTransaction;
        using Result = CookManagedUpdateResult;
#if defined(_WIN32)
        struct Snapshot
        {
            CookResolvedOwnerBinding Owner;
            Text Claim;
            Identity Root;
            File Spec, Index, State;
            OptionalFile Manifest;
            Core::Asset::AssetManifest ParsedManifest;
            bool bManifestParsed = false;
            CookManagedStoreIndex ParsedIndex;
            CookOwnedState ParsedState;
            CookManagedUpdateInventory Inventory;
            Array<CookPreparedPlan> Plans;
            Array<CookDecision> Decisions;
        };
        struct UpdateOperation
        {
            Operation Transaction;
            const CookManagedUpdateRequest* Request = nullptr;
            Result Status = Result::Error;
        };
        bool ReadBefore(const Identity& root, View package, CookManagedBeforeImage& image, Text& error)
        {
            Identity parent;
            std::filesystem::path leaf;
            OptionalFile file;
            if (!ExistingParent(root, package, parent, leaf, error) ||
                !ReadOptional(parent, leaf, MaximumPackageBytes, file, error))
            {
                return false;
            }
            image.Parent = Object(parent);
            image.bPresent = file.bPresent;
            if (file.bPresent)
            {
                image.File = file.Value.Image;
            }
            return true;
        }
        bool SameBefore(const CookManagedBeforeImage& a, const CookManagedBeforeImage& b)
        {
            return a.bPresent == b.bPresent && Same(a.Parent, b.Parent) && (!a.bPresent || Same(a.File, b.File));
        }
        CookManagedBeforeImage ManifestBefore(const Snapshot& snap)
        {
            CookManagedBeforeImage image;
            image.Parent = Object(snap.Root);
            image.bPresent = snap.Manifest.bPresent;
            if (image.bPresent)
            {
                image.File = snap.Manifest.Value.Image;
            }
            return image;
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
        bool Fresh(Operation& op, const CookManagedUpdateRequest& request, const CookDestinationLockContext& lock,
                   const Snapshot& snap, Core::Container::Span<const CookManagedPackageMutation> changed)
        {
            CookManagedStoreObservation observed;
            if (Detail::ObserveCookManagedStoreLocked(lock, request.Owner, observed, op.Error) !=
                    CookManagedStoreResult::Observed ||
                !observed.bRuntimeRootClaimed || observed.CurrentRootClaimId != snap.Claim ||
                observed.Owner.ExpectedBinding.OwnerId != snap.Owner.ExpectedBinding.OwnerId ||
                observed.Owner.Identity.CanonicalSpecLocator != snap.Owner.Identity.CanonicalSpecLocator ||
                observed.Owner.Identity.CanonicalFinalRuntimeRootIdentity !=
                    snap.Owner.Identity.CanonicalFinalRuntimeRootIdentity ||
                !RecheckDirectory(snap.Root, op.Error))
            {
                return Fail(op.Error, "update_owner_changed");
            }
            File spec, index, state, header;
            OptionalFile manifest;
            if (!ReadSpec(snap.Owner, spec, op.Error) || !Same(spec.Image, snap.Spec.Image) ||
                !EqualBytes(spec.Data, request.ExpectedSpecBytes) ||
                !ReadChild(op.Scope.Store, L"roots.json", MaximumCookStoreIndexBytes, index, op.Error) ||
                !Same(index.Image, snap.Index.Image) ||
                !ReadChild(op.Scope.Store, StateLeaf(snap.Claim), MaximumCookStateBytes, state, op.Error) ||
                !Same(state.Image, snap.State.Image) ||
                !ReadChild(op.Scope.Store, L"header.json", 16384, header, op.Error) ||
                !Same(header.Image, op.Scope.Header.Image) ||
                !ReadOptional(snap.Root, Leaf(snap.Owner.ExpectedBinding.ManifestName), MaximumCookStateBytes, manifest,
                              op.Error) ||
                manifest.bPresent != snap.Manifest.bPresent ||
                (manifest.bPresent && !Same(manifest.Value.Image, snap.Manifest.Value.Image)))
            {
                return Fail(op.Error, "update_control_or_spec_changed");
            }
            Array<CookPreparedPlan> current;
            for (size_t i = 0; i < snap.Plans.size(); ++i)
            {
                const auto& old = snap.ParsedState.Records[snap.Inventory.Assets[i].PreviousRecordIndex].Record;
                CookDecisionContext decision;
                const auto* parsed = snap.bManifestParsed ? &snap.ParsedManifest : nullptr;
                const auto now =
                    DecideCookCache(request.Assets[i], request.CookerRevision, true, &old, parsed, decision, op.Error);
                CookPreparedPlan plan;
                if (now != snap.Decisions[i] || now == CookDecision::Error ||
                    !PrepareCookOutputPlan(request.Assets[i], request.CookerRevision, parsed, plan, op.Error) ||
                    !SamePlan(plan, snap.Plans[i]))
                {
                    return Fail(op.Error, "update_dependencies_or_decision_changed");
                }
                current.push_back(std::move(plan));
            }
            for (const auto& package : changed)
            {
                CookManagedBeforeImage currentImage;
                if (!ReadBefore(snap.Root, package.Package, currentImage, op.Error) ||
                    !SameBefore(package.Before, currentImage))
                {
                    return Fail(op.Error, "update_before_changed");
                }
            }
            return ValidateCookOutputSet(current, {&snap.Owner.SpecLocator, 1}, op.Error);
        }
        bool ObserveUpdate(Operation& op, const CookManagedUpdateRequest& request,
                           const CookDestinationLockContext& lock, Snapshot& snap)
        {
            if (request.Assets.empty() || !request.Assets.data() || request.Assets.size() > MaximumCookSetPlans ||
                request.ExpectedSpecBytes.empty() || !request.ExpectedSpecBytes.data() ||
                request.ExpectedSpecBytes.size() > MaximumCookStateBytes || !request.CookerRevision)
            {
                return Fail(op.Error, "request_limits");
            }
            CookManagedStoreObservation observed;
            if (Detail::ObserveCookManagedStoreLocked(lock, request.Owner, observed, op.Error) !=
                    CookManagedStoreResult::Observed ||
                !observed.Owner.bFinalRuntimeRootPresent || !observed.bRuntimeRootClaimed)
            {
                op.bConflict = true;
                return Fail(op.Error, "claimed_runtime_required");
            }
            snap.Owner = observed.Owner;
            snap.Claim = observed.CurrentRootClaimId;
            if (!N::Directory(snap.Owner.FinalRuntimeRootLocator, snap.Root, op.Error) ||
                !ReadSpec(snap.Owner, snap.Spec, op.Error) || !EqualBytes(snap.Spec.Data, request.ExpectedSpecBytes) ||
                !ReadChild(op.Scope.Store, L"roots.json", MaximumCookStoreIndexBytes, snap.Index, op.Error) ||
                !ParseCookManagedStoreIndex(snap.Index.Data, op.Scope.StoreId,
                                            MaximumCookStoreRoots - op.Scope.AncestorRoots, snap.ParsedIndex,
                                            op.Error) ||
                !ReadChild(op.Scope.Store, StateLeaf(snap.Claim), MaximumCookStateBytes, snap.State, op.Error) ||
                !ParseCookOwnedState(snap.State.Data, snap.Owner.ExpectedBinding, snap.ParsedState, op.Error) ||
                !ReadOptional(snap.Root, Leaf(snap.Owner.ExpectedBinding.ManifestName), MaximumCookStateBytes,
                              snap.Manifest, op.Error))
            {
                return false;
            }
            bool bRootClaim = false;
            for (const auto& claim : snap.ParsedIndex.Roots)
            {
                if (claim.ClaimId == snap.Claim && claim.OwnerId == snap.Owner.ExpectedBinding.OwnerId &&
                    std::memcmp(claim.DirectoryId.data(), snap.Root.FileId.data(), 16) == 0)
                {
                    bRootClaim = true;
                }
            }
            if (!bRootClaim)
            {
                return Fail(op.Error, "update_root_claim_changed");
            }
            if (snap.Manifest.bPresent)
            {
                Text parseError;
                snap.bManifestParsed = LoadManifest(snap.Manifest.Value.Data, snap.ParsedManifest, parseError);
            }
            for (const auto& asset : request.Assets)
            {
                CookPreparedPlan plan;
                if (asset.Kind != "texture" ||
                    !PrepareCookOutputPlan(asset, request.CookerRevision,
                                           snap.bManifestParsed ? &snap.ParsedManifest : nullptr, plan, op.Error) ||
                    plan.Outputs.size() != 1 ||
                    plan.Outputs[0].ExpectedIdentity.Kind != Core::Asset::AssetKind::Texture)
                {
                    return Fail(op.Error, "texture_v1_only");
                }
                snap.Plans.push_back(std::move(plan));
            }
            CookStateFileRequest stateScope;
            stateScope.RuntimeRoot = snap.Owner.FinalRuntimeRootLocator;
            stateScope.StatePath = stateScope.RuntimeRoot.parent_path() / N::StoreLeaf / StateLeaf(snap.Claim);
            stateScope.ExpectedBinding = snap.Owner.ExpectedBinding;
            if (!BuildCookManagedUpdateInventory(stateScope, snap.ParsedState, snap.Plans, snap.Inventory, op.Error) ||
                !ValidateCookOutputSet(snap.Plans, {&snap.Owner.SpecLocator, 1}, op.Error))
            {
                return false;
            }
            for (size_t i = 0; i < snap.Plans.size(); ++i)
            {
                CookDecisionContext context;
                const auto& old = snap.ParsedState.Records[snap.Inventory.Assets[i].PreviousRecordIndex].Record;
                const auto decision =
                    DecideCookCache(request.Assets[i], request.CookerRevision, true, &old,
                                    snap.bManifestParsed ? &snap.ParsedManifest : nullptr, context, op.Error);
                if (decision == CookDecision::Error ||
                    context.Dependencies.Fingerprint != snap.Plans[i].Context.Dependencies.Fingerprint)
                {
                    return Fail(op.Error, "update_decision");
                }
                snap.Decisions.push_back(decision);
            }
            return true;
        }
        bool PrepareUpdate(UpdateOperation& work, const CookDestinationLockContext& lock)
        {
            auto& op = work.Transaction;
            const auto& request = *work.Request;
            Snapshot snap;
            if (!ObserveUpdate(op, request, lock, snap))
            {
                return false;
            }
            Array<Core::Asset::AssetCookedReference> references;
            bool bAllSkip = true;
            for (size_t i = 0; i < snap.Plans.size(); ++i)
            {
                bAllSkip &= snap.Decisions[i] == CookDecision::Skip;
                references.push_back(
                    snap.ParsedState.Records[snap.Inventory.Assets[i].PreviousRecordIndex].Record.Outputs[0].Reference);
            }
            Text aggregate;
            if (!Detail::SerializeLegacyTextureManifest(references, aggregate, op.Error))
            {
                return false;
            }
            if (bAllSkip && snap.Manifest.bPresent && EqualBytes(snap.Manifest.Value.Data, BytesOf(aggregate)))
            {
                if (!Fresh(op, request, lock, snap, {}))
                {
                    return false;
                }
                op.Outcome.ClaimId = snap.Claim;
                op.Outcome.IndexGeneration = snap.ParsedIndex.Generation;
                op.Outcome.StateGeneration = snap.ParsedState.Generation;
                work.Status = Result::NoChange;
                return true;
            }
            // NoChangeに世代上限やstage作成を要求しない。ここから先だけがmutation。
            if (!snap.Inventory.bCanAdvanceGeneration || snap.ParsedIndex.Generation == UINT64_MAX)
            {
                return Fail(op.Error, "update_generation_limit");
            }
            CookManagedIntentBuildInput input;
            auto& draft = input.Draft;
            draft.Mode = CookManagedIntentMode::Update;
            draft.ClaimId = snap.Claim;
            draft.Anchor.StoreId = op.Scope.StoreId;
            draft.Anchor.Workspace = Object(op.Scope.Workspace);
            draft.Anchor.Store = Object(op.Scope.Store);
            draft.Anchor.Binding = snap.Owner.ExpectedBinding;
            draft.Root = Object(snap.Root);
            draft.IndexGeneration = snap.ParsedIndex.Generation + 1;
            draft.StateGeneration = snap.ParsedState.Generation + 1;
            draft.Controls[0] = snap.Index.Image;
            draft.Controls[2] = snap.State.Image;
            draft.ManifestBefore = ManifestBefore(snap);
            if (!P::AsciiPath(snap.Owner.FinalRuntimeRootLocator.filename(), draft.Anchor.RootLeaf) ||
                !Token(draft.TransactionId, op.Error))
            {
                return false;
            }
            Array<size_t> cookIndices;
            for (size_t i = 0; i < snap.Plans.size(); ++i)
            {
                if (snap.Decisions[i] == CookDecision::Skip)
                {
                    continue;
                }
                CookManagedPackageMutation mutation;
                mutation.Package = snap.Plans[i].Outputs[0].ExpectedIdentity.CookedPackage;
                if (!ReadBefore(snap.Root, mutation.Package, mutation.Before, op.Error))
                {
                    return false;
                }
                cookIndices.push_back(i);
                draft.Packages.push_back(std::move(mutation));
            }
            Text stageName = ".transaction-stage-";
            stageName.append(draft.TransactionId);
            if (!NewDirectory(op.Scope.Store, Leaf(stageName), op.Scope.Pending, op.Error))
            {
                return false;
            }
            draft.Anchor.Pending = Object(op.Scope.Pending);
            const auto callerStage = snap.Owner.FinalRuntimeRootLocator.parent_path() / N::StoreLeaf / Leaf(stageName);
            Array<Identity> workDirectories;
            Array<std::filesystem::path> workLeaves;
            Array<CookPreparedPlan> stages;
            for (size_t i = 0; i < cookIndices.size(); ++i)
            {
                Text name = "work-";
                char number[32];
                const auto end = std::to_chars(number, number + sizeof(number), i);
                name.append(number, static_cast<size_t>(end.ptr - number));
                Identity directory;
                CookPreparedPlan staged;
                if (!NewDirectory(op.Scope.Pending, Leaf(name), directory, op.Error) ||
                    !PrepareCookStagingPlan(snap.Plans[cookIndices[i]], callerStage / Leaf(name), staged, op.Error))
                {
                    return false;
                }
                workDirectories.push_back(std::move(directory));
                workLeaves.push_back(Leaf(name));
                stages.push_back(std::move(staged));
            }
            if (!stages.empty() && !ValidateCookStagingOutputSet(stages, {&snap.Owner.SpecLocator, 1}, op.Error))
            {
                return false;
            }
            CookOwnedState afterState = snap.ParsedState;
            afterState.Generation = draft.StateGeneration;
            for (size_t i = 0; i < stages.size(); ++i)
            {
                if (!CookSingleAsset(stages[i].Context.Request, op.Error))
                {
                    return false;
                }
                File fragmentFile;
                Core::Asset::AssetManifest fragment;
                CookOutputRecord record;
                const size_t assetIndex = cookIndices[i];
                if (!ReadChild(workDirectories[i], Leaf(snap.Owner.ExpectedBinding.ManifestName), MaximumCookStateBytes,
                               fragmentFile, op.Error) ||
                    !LoadManifest(fragmentFile.Data, fragment, op.Error) ||
                    !CaptureStagedCookOutputRecord(snap.Plans[assetIndex], stages[i], fragment, record, op.Error) ||
                    record.Outputs.size() != 1)
                {
                    return Fail(op.Error, "update_staged_capture");
                }
                Array<TreeNode> tree;
                if (!ScanTree(workDirectories[i], tree, nullptr, op.Error))
                {
                    return false;
                }
                const TreeNode* package = nullptr;
                for (const auto& node : tree)
                {
                    if (node.bDirectory)
                    {
                        Text prefix = node.Relative;
                        prefix.push_back('/');
                        if (draft.Packages[i].Package.size() <= prefix.size() ||
                            std::memcmp(draft.Packages[i].Package.data(), prefix.data(), prefix.size()))
                        {
                            return Fail(op.Error, "unexpected_work_directory");
                        }
                    }
                    else if (node.Relative == draft.Packages[i].Package)
                    {
                        package = &node;
                    }
                    else if (node.Relative != snap.Owner.ExpectedBinding.ManifestName)
                    {
                        return Fail(op.Error, "unexpected_work_file");
                    }
                }
                if (!package || package->Image.Size != record.Outputs[0].Package.Size ||
                    package->Image.ContentHash != record.Outputs[0].Package.ContentHash ||
                    !Rename(package->Parent, package->Native.Canonical.filename(), package->Image.Object,
                            op.Scope.Pending, PackageSlot(false, i), false, &package->Image, op.Error))
                {
                    return Fail(op.Error, "update_staged_package");
                }
                draft.Packages[i].After = package->Image;
                references[assetIndex] = record.Outputs[0].Reference;
                afterState.Records[snap.Inventory.Assets[assetIndex].PreviousRecordIndex].Record = std::move(record);
                if (!DeleteKnown(workDirectories[i], Leaf(snap.Owner.ExpectedBinding.ManifestName),
                                 fragmentFile.Image.Object, false, &fragmentFile.Image, op.Error))
                {
                    return false;
                }
                std::sort(tree.begin(), tree.end(),
                          [](const auto& a, const auto& b)
                          {
                              return a.Relative.size() > b.Relative.size();
                          });
                for (const auto& node : tree)
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
            File manifest, index, state, receipt, intentFile;
            Text indexText, stateText, receiptText, wire;
            CookManagedStoreIndex newIndex = snap.ParsedIndex;
            newIndex.Generation = draft.IndexGeneration;
            if (!Detail::SerializeLegacyTextureManifest(references, aggregate, op.Error) ||
                !SerializeCookManagedStoreIndex(newIndex, indexText, op.Error) ||
                !SerializeCookOwnedState(afterState, stateText, op.Error) ||
                !NewFile(op.Scope.Pending, L"manifest.after", BytesOf(aggregate), manifest, op.Error) ||
                !NewFile(op.Scope.Pending, L"index.after", BytesOf(indexText), index, op.Error) ||
                !NewFile(op.Scope.Pending, L"state.after", BytesOf(stateText), state, op.Error))
            {
                return false;
            }
            draft.ManifestAfter = manifest.Image;
            draft.Controls[1] = index.Image;
            draft.Controls[3] = state.Image;
            if (!MakeCookManagedReceiptBody(draft, receiptText, op.Error) ||
                !NewFile(op.Scope.Pending, L"receipt.stage", BytesOf(receiptText), receipt, op.Error))
            {
                return false;
            }
            draft.Receipt = receipt.Image;
            input.FinalPlans = snap.Plans;
            input.FinalRecords = afterState.Records;
            CookManagedControlDocuments docs;
            docs.Controls[0] = {snap.Index.Image.Object, snap.Index.Data};
            docs.Controls[1] = {index.Image.Object, index.Data};
            docs.Controls[2] = {snap.State.Image.Object, snap.State.Data};
            docs.Controls[3] = {state.Image.Object, state.Data};
            docs.ManifestAfter = {manifest.Image.Object, manifest.Data};
            CookManagedTransactionIntent intent;
            if (!BuildCookManagedTransactionIntent(input, docs, intent, op.Error) ||
                !SerializeCookManagedTransactionIntent(intent, wire, op.Error) ||
                !NewFile(op.Scope.Pending, L"intent.json", BytesOf(wire), intentFile, op.Error) ||
                !Checkpoint(op, Point::Prepared) || !Fresh(op, request, lock, snap, draft.Packages) ||
                !ValidatePrepared(op))
            {
                return false;
            }
            const auto pendingId = Object(op.Scope.Pending);
            if (!Rename(op.Scope.Store, Leaf(stageName), pendingId, op.Scope.Store, L"pending", true, nullptr, op.Error,
                        &op.bPendingPublished) ||
                !N::Directory(op.Scope.Store.Canonical / L"pending", op.Scope.Pending, op.Error) ||
                !Same(op.Scope.Pending, pendingId) || !Checkpoint(op, Point::PendingPublished) || !PublishUpdate(op))
            {
                return false;
            }
            work.Status = Result::Updated;
            return true;
        }
        bool UpdateCallback(const CookDestinationLockContext& lock, void* context, Text& error)
        {
            auto& work = *static_cast<UpdateOperation*>(context);
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
                Fail(op.Error, "pending_requires_recovery");
                error = op.Error;
                return false;
            }
            const bool bSuccess = PrepareUpdate(work, lock);
            error = op.Error;
            return bSuccess;
        }
#endif
        Result UpdateImpl(const CookManagedUpdateRequest& request, const Probe* probe, CookManagedUpdateOutcome& out,
                          Text& error)
        {
            error.clear();
#if !defined(_WIN32)
            (void)request;
            (void)probe;
            (void)out;
            Fail(error, "windows_required");
            return Result::Error;
#else
            UpdateOperation work;
            work.Request = &request;
            auto& op = work.Transaction;
            op.Test = probe;
            op.RuntimeLocator = request.Owner.FinalRuntimeRoot;
            try
            {
                const auto result =
                    probe ? Detail::WithCookDestinationLockForTest({request.Owner.FinalRuntimeRoot}, UpdateCallback,
                                                                   &work, error, probe->LockFault)
                          : WithCookDestinationLock({request.Owner.FinalRuntimeRoot}, UpdateCallback, &work, error);
                if (result == CookDestinationLockResult::Busy)
                {
                    return Result::Busy;
                }
                if (result == CookDestinationLockResult::Executed &&
                    (work.Status == Result::NoChange || work.Status == Result::Updated))
                {
                    out = std::move(op.Outcome);
                    error.clear();
                    return work.Status;
                }
            }
            catch (const std::exception&)
            {
                Fail(error, "update_exception");
            }
            if (!op.Error.empty())
            {
                error = op.Error;
            }
            if (op.bPendingPublished && !op.bRetired)
            {
                error.append("; pending_preserved");
                return Result::NeedsRecovery;
            }
            if (op.bCommitted && op.bRetired)
            {
                error.append("; committed_state_preserved");
                return Result::CommittedButError;
            }
            return op.bConflict ? Result::Conflict : Result::Error;
#endif
        }
    } // 名前空間
    CookManagedUpdateResult UpdateCookManagedAssetSet(const CookManagedUpdateRequest& request,
                                                      CookManagedUpdateOutcome& out, Text& error)
    {
        return UpdateImpl(request, nullptr, out, error);
    }
    CookManagedUpdateResult Detail::UpdateCookManagedAssetSetForTest(const CookManagedUpdateRequest& request,
                                                                     const CookManagedUpdateProbe& probe,
                                                                     CookManagedUpdateOutcome& out, Text& error)
    {
        return UpdateImpl(request, &probe, out, error);
    }
    CookManagedRecoveryResult Detail::RecoverCookManagedUpdateForTest(const std::filesystem::path& runtime,
                                                                      const CookManagedUpdateProbe& probe,
                                                                      CookManagedUpdateOutcome& out, Text& error)
    {
        return Detail::ManagedTransaction::Recover(runtime, &probe, out, error);
    }
} // 名前空間 NorvesLib::Tools::AssetCook
