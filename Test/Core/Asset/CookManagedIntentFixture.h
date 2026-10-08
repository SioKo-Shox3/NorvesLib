#pragma once
// 全kindの実cook/captureからintentへ接続するfixture。公開executorの試験ではない。
#include "Tools/AssetCook/CookManagedTransactionIntent.h"
#include "Tools/AssetCook/CookPathIdentity.h"
#include "Tools/AssetCook/CookStateFile.h"
#include <cstdio>
#include <fstream>
#include <utility>
namespace ManagedIntentFixture
{
    using NorvesLib::Tools::AssetCook::BuildCookManagedTransactionIntent;
    using NorvesLib::Tools::AssetCook::CookManagedBeforeImage;
    using NorvesLib::Tools::AssetCook::CookManagedControlDocuments;
    using NorvesLib::Tools::AssetCook::CookManagedFileImage;
    using NorvesLib::Tools::AssetCook::CookManagedIntentBuildInput;
    using NorvesLib::Tools::AssetCook::CookManagedIntentMode;
    using NorvesLib::Tools::AssetCook::CookManagedObjectId;
    using NorvesLib::Tools::AssetCook::CookManagedPackageMutation;
    using NorvesLib::Tools::AssetCook::CookManagedRootClaim;
    using NorvesLib::Tools::AssetCook::CookManagedStoreIndex;
    using NorvesLib::Tools::AssetCook::CookManagedTransactionEnvelope;
    using NorvesLib::Tools::AssetCook::CookManagedTransactionIntent;
    using NorvesLib::Tools::AssetCook::CookManagedTreeDirectory;
    using NorvesLib::Tools::AssetCook::CookOwnedRecord;
    using NorvesLib::Tools::AssetCook::CookOwnedState;
    using NorvesLib::Tools::AssetCook::CookPreparedPlan;
    using NorvesLib::Tools::AssetCook::CookStateFileRequest;
    using NorvesLib::Tools::AssetCook::MakeCookManagedReceiptBody;
    using NorvesLib::Tools::AssetCook::ParseCookManagedTransactionEnvelope;
    using NorvesLib::Tools::AssetCook::ParseCookManagedTransactionIntent;
    using NorvesLib::Tools::AssetCook::PrepareCookOutputPlan;
    using NorvesLib::Tools::AssetCook::SerializeCookManagedStoreIndex;
    using NorvesLib::Tools::AssetCook::SerializeCookManagedTransactionIntent;
    using NorvesLib::Tools::AssetCook::SerializeCookOwnedState;
    using IntentText = NorvesLib::Core::Container::AnsiString;
    using IntentBytes = NorvesLib::Core::Container::VariableArray<uint8_t>;
    using ByteSpan = NorvesLib::Core::Container::Span<const uint8_t>;
    inline CookManagedObjectId Identity(const std::filesystem::path& path, bool bDirectory)
    {
        NorvesLib::Tools::AssetCook::Detail::CookPathIdentity value;
        IntentText error;
        CHECK(bDirectory ? NorvesLib::Tools::AssetCook::Detail::ObserveCookDirectoryIdentity(path, value, error)
                         : NorvesLib::Tools::AssetCook::Detail::ObserveCookPathIdentity(path, value, error));
        CHECK(value.bPresent);
        CookManagedObjectId id;
        id.Volume = value.VolumeSerial;
        id.File = value.FileId;
        return id;
    }
    inline IntentBytes ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        CHECK(file);
        IntentBytes bytes;
        char c;
        while (file.get(c))
        {
            bytes.push_back(static_cast<uint8_t>(c));
        }
        CHECK(file.eof());
        return bytes;
    }
    inline void WriteBytes(const std::filesystem::path& path, ByteSpan bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        CHECK(file);
        if (!bytes.empty())
        {
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        file.close();
        CHECK(!file.fail());
    }
    inline ByteSpan BytesOf(const IntentText& text)
    {
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    }
    inline CookManagedFileImage Image(const std::filesystem::path& path, ByteSpan bytes)
    {
        CookManagedFileImage out;
        out.Object = Identity(path, false);
        out.Size = bytes.size();
        out.ContentHash = NorvesLib::Core::Asset::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size());
        return out;
    }
    inline CookManagedBeforeImage Before(const std::filesystem::path& path)
    {
        CookManagedBeforeImage out;
        out.bPresent = true;
        out.Parent = Identity(path.parent_path(), true);
        const auto bytes = ReadBytes(path);
        out.File = Image(path, bytes);
        return out;
    }
    inline void VerifyUpdate(const std::filesystem::path& workspace, const char* leaf,
                             const CookStateFileRequest& stateFile, const CookOwnedState& previous,
                             const CookOwnedRecord& captured, const CookPreparedPlan& finalPlan,
                             const CookPreparedPlan& stagedPlan)
    {
        IntentText error;
        const auto store = workspace / ".norves-assetcook";
        const auto pending = store / "pending";
        CHECK(!std::filesystem::exists(store));
        CHECK(std::filesystem::create_directory(store));
        CHECK(std::filesystem::create_directory(pending));
        CookManagedIntentBuildInput input;
        auto& d = input.Draft;
        d.Mode = CookManagedIntentMode::Update;
        d.TransactionId = "11111111111111111111111111111111";
        d.ClaimId = "22222222222222222222222222222222";
        d.Anchor.StoreId = "33333333333333333333333333333333";
        d.Anchor.RootLeaf = leaf;
        d.Anchor.Binding = stateFile.ExpectedBinding;
        d.Anchor.Workspace = Identity(workspace, true);
        d.Anchor.Store = Identity(store, true);
        d.Anchor.Pending = Identity(pending, true);
        d.Root = Identity(stateFile.RuntimeRoot, true);
        d.IndexGeneration = 2;
        d.StateGeneration = previous.Generation + 1;
        CookManagedStoreIndex index;
        index.StoreId = d.Anchor.StoreId;
        CookManagedRootClaim claim;
        claim.ClaimId = d.ClaimId;
        claim.RootLeaf = leaf;
        claim.OwnerId = d.Anchor.Binding.OwnerId;
        claim.DirectoryId = d.Root.File;
        index.Roots.push_back(claim);
        IntentText indexBefore, indexAfter, stateAfter;
        CHECK(SerializeCookManagedStoreIndex(index, indexBefore, error));
        index.Generation = 2;
        CHECK(SerializeCookManagedStoreIndex(index, indexAfter, error));
        CookOwnedState next = previous;
        next.Generation = d.StateGeneration;
        next.Records[0] = captured;
        CHECK(SerializeCookOwnedState(next, stateAfter, error));
        // 旧fileの実bytesをcontrol fixtureへ置く。parse後の再serializeで置換しない。
        const auto stateBefore = ReadBytes(stateFile.StatePath);
        const std::filesystem::path paths[4] = {store / "roots.json", pending / "index.stage",
                                                store / "state-22222222222222222222222222222222.json",
                                                pending / "state.stage"};
        WriteBytes(paths[0], BytesOf(indexBefore));
        WriteBytes(paths[1], BytesOf(indexAfter));
        WriteBytes(paths[2], stateBefore);
        WriteBytes(paths[3], BytesOf(stateAfter));
        NorvesLib::Core::Container::FixedArray<IntentBytes, 4> bytes;
        CookManagedControlDocuments docs;
        for (size_t i = 0; i < 4; ++i)
        {
            bytes[i] = ReadBytes(paths[i]);
            d.Controls[i] = Image(paths[i], bytes[i]);
            docs.Controls[i] = {d.Controls[i].Object, bytes[i]};
        }
        d.ManifestBefore = Before(finalPlan.Context.Request.ManifestPath);
        const auto manifestBytes = ReadBytes(stagedPlan.Context.Request.ManifestPath);
        d.ManifestAfter = Image(stagedPlan.Context.Request.ManifestPath, manifestBytes);
        docs.ManifestAfter = {d.ManifestAfter.Object, manifestBytes};
        for (size_t i = 0; i < finalPlan.Outputs.size(); ++i)
        {
            CookManagedPackageMutation row;
            row.Package = captured.Record.Outputs[i].Reference.CookedPackage;
            row.Before = Before(finalPlan.Outputs[i].TargetPath);
            const auto packageBytes = ReadBytes(stagedPlan.Outputs[i].TargetPath);
            row.After = Image(stagedPlan.Outputs[i].TargetPath, packageBytes);
            CHECK(row.After.Size == captured.Record.Outputs[i].Package.Size &&
                  row.After.ContentHash == captured.Record.Outputs[i].Package.ContentHash);
            d.Packages.push_back(std::move(row));
        }
        IntentText receipt;
        CHECK(MakeCookManagedReceiptBody(d, receipt, error));
        WriteBytes(pending / "receipt.stage", BytesOf(receipt));
        d.Receipt = Image(pending / "receipt.stage", BytesOf(receipt));
        input.FinalPlans = {&finalPlan, 1};
        input.FinalRecords = {&captured, 1};
        CookManagedTransactionIntent intent;
        if (!BuildCookManagedTransactionIntent(input, docs, intent, error))
        {
            std::fprintf(stderr, "intent %s: %s\n", leaf, error.c_str());
            CHECK(false);
        }
        IntentText wire;
        CHECK(SerializeCookManagedTransactionIntent(intent, wire, error));
        CookManagedTransactionEnvelope envelope;
        CookManagedTransactionIntent restored;
        // sourceが消えても保存intentの意味検査はsourceを読まない。元の名前は必ず戻す。
        const auto source = finalPlan.Context.Request.InputPath;
        auto held = source;
        held += ".intent-held";
        CHECK(!std::filesystem::exists(held));
        std::filesystem::rename(source, held);
        CHECK(!std::filesystem::exists(source));
        const bool bEnvelope = ParseCookManagedTransactionEnvelope(BytesOf(wire), d.Anchor, envelope, error);
        const bool bRestored = bEnvelope && ParseCookManagedTransactionIntent(envelope, docs, restored, error);
        std::filesystem::rename(held, source);
        CHECK(bRestored && restored.IsValid());
        IntentText again;
        CHECK(SerializeCookManagedTransactionIntent(restored, again, error));
        CHECK(again == wire);
        // 同bytesでも別objectをside docとして通さない。失敗時に既存intentを保持する。
        WriteBytes(pending / "same-state-copy", bytes[3]);
        auto changed = docs;
        changed.Controls[3].ObservedObject = Identity(pending / "same-state-copy", false);
        CHECK(!ParseCookManagedTransactionIntent(envelope, changed, restored, error));
        CHECK(restored.IsValid());
        CHECK(SerializeCookManagedTransactionIntent(restored, again, error) && again == wire);
        // 欠落leaf・破損beforeは許容し、after/captured印を変えると拒否する。
        auto repaired = input;
        repaired.Draft.Packages[0].Before.bPresent = false;
        repaired.Draft.Packages[0].Before.File = {};
        CHECK(BuildCookManagedTransactionIntent(repaired, docs, restored, error));
        repaired = input;
        repaired.Draft.Packages[0].Before.File.ContentHash ^= 1;
        CHECK(BuildCookManagedTransactionIntent(repaired, docs, restored, error));
        repaired.Draft.Packages[0].After.ContentHash ^= 1;
        CHECK(!BuildCookManagedTransactionIntent(repaired, docs, restored, error));
        if (input.Draft.Packages.size() > 1)
        {
            auto partial = input;
            partial.Draft.Packages.pop_back();
            CHECK(!BuildCookManagedTransactionIntent(partial, docs, restored, error));
        }
        // Bootstrapは別の不在final rootと、実際に作った既知stageを使う。
        auto bootstrap = input;
        auto& boot = bootstrap.Draft;
        IntentText bootLeaf = leaf;
        bootLeaf.append(".bootstrap");
        const auto bootFinal = workspace / bootLeaf.c_str();
        CHECK(!std::filesystem::exists(bootFinal));
        IntentText bootStageLeaf = leaf;
        bootStageLeaf.append(".bootstrap-stage");
        const auto bootStage = workspace / bootStageLeaf.c_str();
        CHECK(std::filesystem::create_directory(bootStage));
        boot.Mode = CookManagedIntentMode::Bootstrap;
        boot.ClaimId = "44444444444444444444444444444444";
        boot.TransactionId = "55555555555555555555555555555555";
        boot.Anchor.RootLeaf = bootLeaf;
        boot.Anchor.Binding.RuntimeRootIdentity = bootFinal.generic_string().c_str();
        boot.Anchor.Binding.OwnerId = "66666666666666666666666666666666";
        boot.Root = Identity(bootStage, true);
        boot.StateGeneration = 1;
        boot.Packages.clear();
        boot.Directories.clear();
        boot.ManifestBefore = {};
        boot.ManifestBefore.Parent = boot.Root;
        boot.Controls[2] = {};
        auto bootDocs = docs;
        bootDocs.Controls[2] = {};
        auto request = finalPlan.Context.Request;
        request.PackagePath = bootFinal / captured.Record.Outputs[0].Reference.CookedPackage.c_str();
        request.ManifestPath = bootFinal / boot.Anchor.Binding.ManifestName.c_str();
        CookPreparedPlan bootPlan;
        CHECK(PrepareCookOutputPlan(request, finalPlan.Context.Dependencies.CookerRevision, nullptr, bootPlan, error));
        bootstrap.FinalPlans = {&bootPlan, 1};
        for (size_t i = 0; i < stagedPlan.Outputs.size(); ++i)
        {
            const auto& package = captured.Record.Outputs[i].Reference.CookedPackage;
            const auto destination = bootStage / package.c_str();
            std::filesystem::create_directories(destination.parent_path());
            CHECK(std::filesystem::copy_file(stagedPlan.Outputs[i].TargetPath, destination));
            CookManagedPackageMutation row;
            row.Package = package;
            row.Before.Parent = Identity(destination.parent_path(), true);
            const auto actual = ReadBytes(destination);
            row.After = Image(destination, actual);
            boot.Packages.push_back(std::move(row));
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(bootStage))
        {
            if (entry.is_directory())
            {
                CookManagedTreeDirectory dir;
                dir.Relative = entry.path().lexically_relative(bootStage).generic_string().c_str();
                dir.Object = Identity(entry.path(), true);
                dir.Parent = Identity(entry.path().parent_path(), true);
                boot.Directories.push_back(std::move(dir));
            }
        }
        const auto bootManifest = bootStage / boot.Anchor.Binding.ManifestName.c_str();
        CHECK(std::filesystem::copy_file(stagedPlan.Context.Request.ManifestPath, bootManifest));
        boot.ManifestAfter = Image(bootManifest, manifestBytes);
        bootDocs.ManifestAfter = {boot.ManifestAfter.Object, manifestBytes};
        CookManagedRootClaim bootClaim;
        bootClaim.ClaimId = boot.ClaimId;
        bootClaim.OwnerId = boot.Anchor.Binding.OwnerId;
        bootClaim.RootLeaf = bootLeaf;
        bootClaim.DirectoryId = boot.Root.File;
        index.Roots.push_back(bootClaim);
        CHECK(SerializeCookManagedStoreIndex(index, indexAfter, error));
        WriteBytes(paths[1], BytesOf(indexAfter));
        bytes[1] = ReadBytes(paths[1]);
        boot.Controls[1] = Image(paths[1], bytes[1]);
        bootDocs.Controls[1] = {boot.Controls[1].Object, bytes[1]};
        next.Binding = boot.Anchor.Binding;
        next.Generation = 1;
        CHECK(SerializeCookOwnedState(next, stateAfter, error));
        WriteBytes(paths[3], BytesOf(stateAfter));
        bytes[3] = ReadBytes(paths[3]);
        boot.Controls[3] = Image(paths[3], bytes[3]);
        bootDocs.Controls[3] = {boot.Controls[3].Object, bytes[3]};
        CHECK(MakeCookManagedReceiptBody(boot, receipt, error));
        WriteBytes(pending / "receipt.stage", BytesOf(receipt));
        boot.Receipt = Image(pending / "receipt.stage", BytesOf(receipt));
        if (!BuildCookManagedTransactionIntent(bootstrap, bootDocs, restored, error))
        {
            std::fprintf(stderr, "bootstrap %s: %s\n", leaf, error.c_str());
            CHECK(false);
        }
        CHECK(SerializeCookManagedTransactionIntent(restored, wire, error));
        CHECK(ParseCookManagedTransactionEnvelope(BytesOf(wire), boot.Anchor, envelope, error));
        CHECK(ParseCookManagedTransactionIntent(envelope, bootDocs, restored, error));
        CHECK(!std::filesystem::exists(bootFinal));
        // fixture専用に作ったstoreだけを解放する。runtime/sourceには触れない。
        CHECK(std::filesystem::remove_all(store) > 0);
        std::printf("COOK_TRANSACTION_LIVE asset=%s result=pass update_bootstrap_native_images_source_absent_parse\n", leaf);
    }
} // namespace ManagedIntentFixture
