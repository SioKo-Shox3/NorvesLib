// 純値codecの反証。ここでの番号IDはモデル値であり、実file ID試験は共通cache fixtureが担う。
#include "Tools/AssetCook/CookManagedTransactionIntent.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace IntentTest
{
    using namespace NorvesLib::Tools::AssetCook;
    namespace Asset = NorvesLib::Core::Asset;
    using IntentText = NorvesLib::Core::Container::AnsiString;
    template <class T> using Array = NorvesLib::Core::Container::VariableArray<T>;
    using Span = NorvesLib::Core::Container::Span<const uint8_t>;
    Span Bytes(const IntentText& s)
    {
        return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
    }
    CookManagedObjectId Id(uint64_t n)
    {
        CookManagedObjectId x;
        x.Volume = 19;
        for (size_t i = 0; i < 8; ++i)
        {
            x.File[i] = static_cast<uint8_t>(n >> (i * 8));
        }
        return x;
    }
    CookManagedFileImage Image(uint64_t n, const IntentText& bytes)
    {
        return {Id(n), bytes.size(), Asset::ComputeAssetPackagePayloadHash(Bytes(bytes).data(), bytes.size())};
    }
    CookOwnedRecord Owned(const char* key, const char* package)
    {
        CookOwnedRecord r;
        r.PrimaryKey = {key, Asset::AssetKind::Raw, "default"};
        r.Record.CookerRevision = 7;
        r.Record.DependencyFingerprint = 91;
        CookRecordedOutput output;
        auto& ref = output.Reference;
        ref.LogicalPath = key;
        ref.Kind = Asset::AssetKind::Raw;
        ref.SourceHash = 1;
        ref.Variant = "default";
        ref.Format = "raw.v0";
        ref.CookedPackage = package;
        ref.EntryName = "__asset__";
        ref.EntryType = Asset::AssetPackageFormatV1::RawEntryType;
        ref.CookedHash = 2;
        output.Package = {1024, 2};
        r.Record.Outputs.push_back(output);
        return r;
    }
    struct Fixture
    {
        CookManagedIntentBuildInput Input;
        CookManagedControlDocuments Docs;
        CookManagedStoreIndex BeforeIndex, AfterIndex;
        CookOwnedState BeforeState, AfterState;
        Array<CookPreparedPlan> Plans;
        NorvesLib::Core::Container::FixedArray<IntentText, 4> ControlText;
        IntentText Manifest, Error, Receipt;
        explicit Fixture(bool bBootstrap)
        {
            auto& d = Input.Draft;
            d.Mode = bBootstrap ? CookManagedIntentMode::Bootstrap : CookManagedIntentMode::Update;
            d.TransactionId = "11111111111111111111111111111111";
            d.ClaimId = "22222222222222222222222222222222";
            d.Anchor.StoreId = "33333333333333333333333333333333";
            d.Anchor.RootLeaf = "Runtime";
            d.Anchor.Binding = {"44444444444444444444444444444444", "C:/Fixture/Runtime", "manifest.json"};
            d.Anchor.Workspace = Id(1);
            d.Anchor.Store = Id(2);
            d.Anchor.Pending = Id(3);
            d.Root = Id(4);
            d.IndexGeneration = 2;
            d.StateGeneration = bBootstrap ? 1 : 2;
            BeforeIndex.StoreId = d.Anchor.StoreId;
            AfterIndex = BeforeIndex;
            AfterIndex.Generation = 2;
            CookManagedRootClaim claim;
            claim.ClaimId = d.ClaimId;
            claim.OwnerId = d.Anchor.Binding.OwnerId;
            claim.RootLeaf = "Runtime";
            claim.DirectoryId = d.Root.File;
            if (!bBootstrap)
            {
                BeforeIndex.Roots.push_back(claim);
            }
            AfterIndex.Roots.push_back(claim);
            // 無関係なsibling claimを保持する。
            claim.ClaimId = "55555555555555555555555555555555";
            claim.RootLeaf = "Sibling";
            claim.DirectoryId = Id(5).File;
            BeforeIndex.Roots.push_back(claim);
            AfterIndex.Roots.push_back(claim);
            BeforeState.Binding = d.Anchor.Binding;
            BeforeState.Records.push_back(Owned("Test/a", "Cooked/a.nvpkg"));
            BeforeState.Records.push_back(Owned("Test/b", "b.nvpkg"));
            AfterState = BeforeState;
            AfterState.Generation = d.StateGeneration;
            for (size_t i = 0; i < AfterState.Records.size(); ++i)
            {
                const auto& ref = AfterState.Records[i].Record.Outputs[0].Reference;
                CookPreparedPlan p;
                p.Context.Dependencies.CookerRevision = 7;
                p.Context.Dependencies.Fingerprint = 91;
                p.Context.Request.ManifestPath = "C:/Fixture/Runtime/manifest.json";
                p.Outputs.push_back({ref, std::filesystem::path("C:/Fixture/Runtime") / ref.CookedPackage.c_str()});
                Plans.push_back(p);
                if (!bBootstrap && i == 1)
                {
                    continue;
                } // bはSkip、aはCook。
                CookManagedPackageMutation mutation;
                mutation.Package = ref.CookedPackage;
                mutation.Before.Parent = i ? d.Root : Id(6);
                mutation.Before.bPresent = !bBootstrap;
                if (!bBootstrap)
                {
                    mutation.Before.File = {Id(30 + i), 1024, 9};
                } // 破損beforeは旧state印と違ってよい。
                mutation.After = {Id(40 + i), 1024, 2};
                d.Packages.push_back(mutation);
            }
            if (bBootstrap)
            {
                d.Directories.push_back({"Cooked", Id(6), d.Root});
            }
            d.ManifestBefore.Parent = d.Root;
            d.ManifestBefore.bPresent = !bBootstrap;
            if (!bBootstrap)
            {
                d.ManifestBefore.File = {Id(17), 3, 7};
            }
            Refresh();
        }
        void Refresh()
        {
            auto& d = Input.Draft;
            CHECK(SerializeCookManagedStoreIndex(BeforeIndex, ControlText[0], Error));
            CHECK(SerializeCookManagedStoreIndex(AfterIndex, ControlText[1], Error));
            if (d.Mode == CookManagedIntentMode::Update)
            {
                CHECK(SerializeCookOwnedState(BeforeState, ControlText[2], Error));
            }
            else
            {
                ControlText[2].clear();
            }
            CHECK(SerializeCookOwnedState(AfterState, ControlText[3], Error));
            for (size_t i = 0; i < 4; ++i)
            {
                if (i == 2 && d.Mode == CookManagedIntentMode::Bootstrap)
                {
                    d.Controls[i] = {};
                    Docs.Controls[i] = {};
                }
                else
                {
                    d.Controls[i] = Image(10 + i, ControlText[i]);
                    Docs.Controls[i] = {d.Controls[i].Object, Bytes(ControlText[i])};
                }
            }
            Manifest = "{\"version\":1,\"assets\":[";
            for (size_t i = 0; i < AfterState.Records.size(); ++i)
            {
                if (i)
                {
                    Manifest.push_back(',');
                }
                const auto& ref = AfterState.Records[i].Record.Outputs[0].Reference;
                Manifest.append("{\"logical_path\":\"");
                Manifest.append(ref.LogicalPath);
                Manifest.append(
                    "\",\"kind\":\"raw\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"raw.v0\",\"cooked_package\":\"");
                Manifest.append(ref.CookedPackage);
                Manifest.append(
                    "\",\"entry_name\":\"__asset__\",\"entry_type\":\"Raw \",\"cooked_hash\":\"0000000000000002\",\"cooked_version\":0}");
            }
            Manifest.append("]}");
            d.ManifestAfter = Image(14, Manifest);
            Docs.ManifestAfter = {d.ManifestAfter.Object, Bytes(Manifest)};
            CHECK(MakeCookManagedReceiptBody(d, Receipt, Error));
            d.Receipt = Image(15, Receipt);
            Input.FinalPlans = Plans;
            Input.FinalRecords = AfterState.Records;
        }
        bool Build(CookManagedTransactionIntent& out)
        {
            return BuildCookManagedTransactionIntent(Input, Docs, out, Error);
        }
    };
    IntentText Replace(const IntentText& source, const char* from, const char* to)
    {
        const char* found = std::strstr(source.c_str(), from);
        CHECK(found);
        const size_t offset = found - source.data();
        IntentText result;
        result.append(source.data(), offset);
        result.append(to);
        result.append(source.data() + offset + std::strlen(from), source.size() - offset - std::strlen(from));
        return result;
    }
    void Happy(bool bBootstrap)
    {
        Fixture f(bBootstrap);
        CookManagedTransactionIntent intent;
        if (!f.Build(intent))
        {
            std::fprintf(stderr, "intent fixture: %s\n", f.Error.c_str());
            CHECK(false);
        }
        CHECK(intent.IsValid());
        IntentText wire, again;
        CHECK(SerializeCookManagedTransactionIntent(intent, wire, f.Error));
        CookManagedTransactionEnvelope envelope;
        CHECK(ParseCookManagedTransactionEnvelope(Bytes(wire), f.Input.Draft.Anchor, envelope, f.Error));
        CHECK(envelope.IsValid() && envelope.Control(CookManagedControlRole::IndexBefore) && envelope.ManifestAfter());
        CHECK(!envelope.Control(static_cast<CookManagedControlRole>(4)));
        CHECK(ParseCookManagedTransactionIntent(envelope, f.Docs, intent, f.Error));
        CHECK(SerializeCookManagedTransactionIntent(intent, again, f.Error) && again == wire);
        // 値所有と入力寿命。envelopeは入力wireの領域を保持しない。
        wire.clear();
        auto copy = envelope;
        envelope = {};
        CHECK(ParseCookManagedTransactionIntent(copy, f.Docs, intent, f.Error));
        auto moved = std::move(intent);
        CHECK(moved.IsValid() && !intent.IsValid());
        CookManagedTransactionIntent assigned;
        assigned = std::move(moved);
        CHECK(assigned.IsValid() && !moved.IsValid());
        moved = std::move(assigned);
        CHECK(moved.IsValid() && !assigned.IsValid());
        auto movedEnvelope = std::move(copy);
        CHECK(movedEnvelope.IsValid() && !copy.IsValid() && copy.ClaimId().empty());
        CHECK(!copy.Control(CookManagedControlRole::IndexBefore) && !copy.ManifestAfter());
        copy = std::move(movedEnvelope);
        CHECK(copy.IsValid() && !movedEnvelope.IsValid() && movedEnvelope.ClaimId().empty());
        CHECK(!movedEnvelope.Control(CookManagedControlRole::StateAfter) && !movedEnvelope.ManifestAfter());
        CHECK(SerializeCookManagedTransactionIntent(moved, wire, f.Error) && wire == again);
        auto bad = f.Docs;
        bad.Controls[3].ObservedObject = Id(999);
        CHECK(!ParseCookManagedTransactionIntent(copy, bad, moved, f.Error));
        CHECK(SerializeCookManagedTransactionIntent(moved, wire, f.Error) && wire == again);
        bad = f.Docs;
        bad.Controls[3].Bytes = Bytes(f.ControlText[0]);
        CHECK(!ParseCookManagedTransactionIntent(copy, bad, moved, f.Error));
        bad = f.Docs;
        bad.ManifestAfter.ObservedObject = Id(998);
        CHECK(!ParseCookManagedTransactionIntent(copy, bad, moved, f.Error));
        auto anchor = f.Input.Draft.Anchor;
        anchor.Pending = Id(888);
        CHECK(!ParseCookManagedTransactionEnvelope(Bytes(wire), anchor, copy, f.Error));
        CHECK(copy.IsValid());
        for (const auto& malformed :
             {Replace(wire, "\"schema\":1", "\"schema\":1,\"schema\":1"),
              Replace(wire, "\"schema\":1", "\"schema\":1,\"unknown\":1"),
              Replace(wire, "Cooked/a.nvpkg", "../a.nvpkg"), Replace(wire, "\"mode\":", "\"unknown_mode\":"),
              Replace(wire, "\"index_generation\":\"0000000000000002\"", "\"index_generation\":2")})
        {
            CHECK(!ParseCookManagedTransactionEnvelope(Bytes(malformed), f.Input.Draft.Anchor, copy, f.Error));
            CHECK(copy.IsValid());
        }
        IntentText oversized(MaximumCookManagedIntentBytes + 1, ' ');
        CHECK(!ParseCookManagedTransactionEnvelope(Bytes(oversized), f.Input.Draft.Anchor, copy, f.Error));
    }
    void Negative()
    {
        CookManagedTransactionIntent out;
        {
            Fixture f(true);
            f.Input.Draft.Packages.pop_back();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(true);
            f.Input.Draft.Directories.push_back({"Unused", Id(99), f.Input.Draft.Root});
            CHECK(!f.Build(out));
        }
        {
            Fixture f(true);
            f.Input.Draft.Directories[0].Parent = Id(99);
            CHECK(!f.Build(out));
        }
        {
            Fixture f(true);
            f.Input.Draft.Packages[0].Before.bPresent = true;
            f.Input.Draft.Packages[0].Before.File = {Id(80), 9, 9};
            CHECK(!f.Build(out));
        }
        {
            Fixture f(true);
            f.BeforeIndex.Roots.push_back(f.AfterIndex.Roots[0]);
            f.Refresh();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Input.Draft.Packages.push_back(f.Input.Draft.Packages[0]);
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Input.Draft.Packages[0].After.Object = f.Input.Draft.Controls[0].Object;
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Input.Draft.Packages[0].Before.Parent = {};
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Input.Draft.Packages[0].Before.Parent = f.Input.Draft.Root;
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Input.Draft.Receipt.ContentHash ^= 1;
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.AfterIndex.Roots[1].RootLeaf = "Changed";
            f.Refresh();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.AfterIndex.Roots[0].DirectoryId = Id(80).File;
            f.Refresh();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.BeforeIndex.Generation = UINT64_MAX;
            f.Refresh();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.BeforeState.Generation = UINT64_MAX;
            f.Refresh();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.AfterState.Generation = 9;
            f.Refresh();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.AfterState.Records[1].Record.CookerRevision = 8;
            f.Refresh();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.AfterState.Records[0].Record.Outputs[0].Reference.CookedPackage = "renamed.nvpkg";
            f.Refresh();
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Plans[0].Context.Dependencies.Fingerprint++;
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Plans[0].Outputs[0].TargetPath = "C:/Elsewhere/a.nvpkg";
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Input.Draft.ClaimId = "00000000000000000000000000000000";
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Input.Draft.Packages.resize(MaximumCookStateOutputs + 1);
            CHECK(!f.Build(out));
        }
        {
            Fixture f(false);
            f.Input.Draft.Directories.push_back({"Cooked", Id(6), f.Input.Draft.Root});
            CHECK(!f.Build(out));
        }
        // 順序変更を許し、Skipと無関係なfileはmutationへ追加しない。
        {
            Fixture f(false);
            std::swap(f.AfterState.Records[0], f.AfterState.Records[1]);
            std::swap(f.AfterIndex.Roots[0], f.AfterIndex.Roots[1]);
            f.Refresh();
            CHECK(f.Build(out));
        }
    }
    void DeepSharedParents()
    {
        Fixture f(true);
        auto& d = f.Input.Draft;
        d.Packages.clear();
        d.Directories.clear();
        f.AfterState.Records.clear();
        f.Plans.clear();
        IntentText prefix;
        CookManagedObjectId parent = d.Root;
        for (size_t i = 0; i < 250; ++i)
        {
            if (i)
            {
                prefix.push_back('/');
            }
            prefix.append("abcdefghijklmno");
            const auto object = Id(100 + i);
            d.Directories.push_back({prefix, object, parent});
            parent = object;
        }
        for (size_t i = 0; i < 2000; ++i)
        {
            char leaf[32], key[32];
            std::snprintf(leaf, sizeof(leaf), "/b%04zu", i);
            std::snprintf(key, sizeof(key), "Test/%04zu", i);
            IntentText directory = prefix;
            directory.append(leaf);
            const auto object = Id(1000 + i);
            d.Directories.push_back({directory, object, parent});
            IntentText package = directory;
            package.append("/item.nvpkg");
            auto record = Owned(key, package.c_str());
            f.AfterState.Records.push_back(record);
            CookManagedPackageMutation mutation;
            mutation.Package = package;
            mutation.Before.Parent = object;
            mutation.After = {Id(5000 + i), 1024, 2};
            d.Packages.push_back(std::move(mutation));
            CookPreparedPlan plan;
            plan.Context.Request.ManifestPath = "C:/Fixture/Runtime/manifest.json";
            plan.Context.Dependencies.CookerRevision = 7;
            plan.Context.Dependencies.Fingerprint = 91;
            plan.Outputs.push_back(
                {record.Record.Outputs[0].Reference, std::filesystem::path("C:/Fixture/Runtime") / package.c_str()});
            f.Plans.push_back(std::move(plan));
        }
        f.Refresh();
        CookManagedTransactionIntent intent;
        if (!f.Build(intent))
        {
            std::fprintf(stderr, "deep intent: %s\n", f.Error.c_str());
            CHECK(false);
        }
        IntentText wire;
        CHECK(SerializeCookManagedTransactionIntent(intent, wire, f.Error));
        CHECK(wire.size() < MaximumCookManagedIntentBytes);
        CookManagedTransactionEnvelope envelope;
        CHECK(ParseCookManagedTransactionEnvelope(Bytes(wire), d.Anchor, envelope, f.Error));
        CHECK(ParseCookManagedTransactionIntent(envelope, f.Docs, intent, f.Error));
        CHECK(intent.Value().Directories.size() == 2250 && intent.Value().Packages.size() == 2000);
    }
    void IndexCodec()
    {
        CookManagedStoreIndex index;
        index.StoreId = "33333333333333333333333333333333";
        IntentText text, error;
        CHECK(SerializeCookManagedStoreIndex(index, text, error));
        CHECK(
            text ==
            "{\"schema\":1,\"store_id\":\"33333333333333333333333333333333\",\"generation\":\"0000000000000001\",\"roots\":[]}");
        CookManagedStoreIndex parsed;
        CHECK(ParseCookManagedStoreIndex(Bytes(text), index.StoreId, 0, parsed, error));
        CookManagedRootClaim claim;
        claim.ClaimId = "22222222222222222222222222222222";
        claim.OwnerId = "44444444444444444444444444444444";
        claim.RootLeaf = "Runtime";
        index.Roots.push_back(claim);
        CHECK(!SerializeCookManagedStoreIndex(index, text, error)); // default IDはzero
        index.Roots[0].DirectoryId = Id(8).File;
        CHECK(SerializeCookManagedStoreIndex(index, text, error));
        CHECK(!ParseCookManagedStoreIndex(Bytes(text), index.StoreId, 0, parsed, error));
        CHECK(parsed.Roots.empty());
        CHECK(ParseCookManagedStoreIndex(Bytes(text), index.StoreId, 1, parsed, error));
        index.Roots.push_back(index.Roots[0]);
        CHECK(!SerializeCookManagedStoreIndex(index, text, error));
    }
} // namespace IntentTest
int main()
{
    IntentTest::IndexCodec();
    IntentTest::Happy(true);
    IntentTest::Happy(false);
    IntentTest::Negative();
    IntentTest::DeepSharedParents();
    std::puts(
        "COOK_TRANSACTION_INTENT result=pass fixed_side_documents_claims_generations_known_images_source_free_value_codec");
    return 0;
}
