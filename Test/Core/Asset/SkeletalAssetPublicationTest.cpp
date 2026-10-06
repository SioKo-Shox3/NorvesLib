// Registryの4型shadow公開と失敗保持。実GPU/非同期runtimeの試験ではない。
#include "Resource/SkeletalAssetPublication.h"
#include "Resource/SkeletalAssetPublicationTestAccess.h"
#include "SkeletalLoaderFixture.h"
#include "Asset/AssetSystem.h"
#include "Thread/ConditionVariable.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Skeletal publication %s:%d %s\n", __FILE__, __LINE__, #x);                           \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace A = Core::Asset;
namespace R = Core::ResourceIO;
namespace F = NorvesLib::Tests::SkeletalLoaderFixture;
namespace T = NorvesLib::Thread;
using Point = R::Detail::SkeletalPublicationPoint;
namespace
{
    struct Fixture
    {
        std::filesystem::path Root;
        R::CookedSkeletalCpuAsset Cpu;
        Fixture()
        {
            char text[80];
            std::snprintf(text, sizeof(text), "norves-publish-%lld",
                          static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            Root = std::filesystem::temp_directory_path() / text;
            CHECK(std::filesystem::create_directory(Root));
            const auto payload = F::BuildThreeClips();
            uint64_t hash = 0;
            const auto package = F::Package(payload, A::MakeAssetPackageFourCC('S', 'k', 'l', '0'), hash);
            std::ofstream f(Root / "asset.nvpkg", std::ios::binary);
            CHECK(f);
            f.write(reinterpret_cast<const char*>(package.data()), static_cast<std::streamsize>(package.size()));
            f.close();
            CHECK(!f.fail());
            auto assets = C::MakeShared<A::AssetSystem>(C::AnsiString(Root.generic_string().c_str()));
            const auto manifest =
                C::AnsiString(
                    "{\"version\":1,\"assets\":[{\"logical_path\":\"Actors/Animal\",\"kind\":\"model\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"nvskel.v0.skinned.pnujiw.u32\",\"cooked_package\":\"asset.nvpkg\",\"entry_name\":\"a\",\"entry_type\":\"Skl0\",\"cooked_hash\":\"") +
                A::FormatAssetHashHex(hash) + "\",\"cooked_version\":0}]}";
            CHECK(assets->LoadManifestFromJsonText(F::CoreText(manifest)));
            R::SkeletalAssetLoadReport report;
            CHECK(R::LoadCookedSkeletalForWorker({assets, "Actors/Animal"}, Cpu, report));
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };
    R::SkeletalAssetCreateContext Context(Core::ResourceRegistry& r)
    {
        return {&r, T::Thread::GetCurrentThreadId()};
    }
    R::SkeletalPreparedPublication Prepare(Fixture& f, const R::SkeletalAssetCreateContext& context)
    {
        R::SkeletalPreparedPublication out;
        R::SkeletalPublicationReport report;
        CHECK(R::PrepareSkeletalPublication(f.Cpu, context, out, report) && out.IsValid());
        return out;
    }
    bool Same(const R::SkeletalPublishedAsset& a, const R::SkeletalPublishedAsset& b)
    {
        return a.Asset == b.Asset && a.AggregateHandle == b.AggregateHandle && a.MeshHandle == b.MeshHandle &&
               a.SkeletonHandle == b.SkeletonHandle && a.ClipHandles == b.ClipHandles;
    }
    struct Snapshot
    {
        size_t Count = 0, Paths = 0, Memory = 0, Pools = 0;
        C::VariableArray<Core::ResourceRecord> Records;
    };
    Snapshot Observe(Core::ResourceRegistry& r)
    {
        Snapshot s{r.GetResourceCount(), r.GetCachedPathCount(), r.GetTotalMemoryUsage(),
                   R::Detail::SkeletalRegistryPoolCountForTest(r), r.GetRecords()};
        std::sort(s.Records.begin(), s.Records.end(), [](const auto& a, const auto& b) { return a.Id < b.Id; });
        return s;
    }
    void Unchanged(const Snapshot& a, Core::ResourceRegistry& r)
    {
        const auto b = Observe(r);
        CHECK(a.Count == b.Count && a.Paths == b.Paths && a.Memory == b.Memory && a.Pools == b.Pools &&
              a.Records.size() == b.Records.size());
        for (size_t i = 0; i < a.Records.size(); ++i)
        {
            const auto& x = a.Records[i];
            const auto& y = b.Records[i];
            CHECK(x.Id == y.Id && x.Generation == y.Generation && x.URI == y.URI && x.Type == y.Type &&
                  x.LoadState == y.LoadState && x.VersionHash == y.VersionHash &&
                  x.DependencyCount == y.DependencyCount && x.MemoryUsage == y.MemoryUsage);
        }
    }
    void Handles(Core::ResourceRegistry& registry, const R::SkeletalPublishedAsset& value)
    {
        CHECK(value.Asset && registry.Resolve(value.AggregateHandle) == value.Asset);
        CHECK(registry.Resolve(value.MeshHandle) == value.Asset->GetMesh() &&
              registry.Resolve(value.SkeletonHandle) == value.Asset->GetSkeleton());
        CHECK(value.ClipHandles.size() == value.Asset->GetClipCount());
        for (size_t i = 0; i < value.ClipHandles.size(); ++i)
        {
            CHECK(registry.Resolve(value.ClipHandles[i]) == value.Asset->GetClip(i));
        }
    }
    size_t Memory(const C::TSharedPtr<Core::SkeletalAssetResource>& asset)
    {
        size_t total =
            asset->GetMemorySize() + asset->GetMesh()->GetMemorySize() + asset->GetSkeleton()->GetMemorySize();
        for (size_t i = 0; i < asset->GetClipCount(); ++i)
        {
            total += asset->GetClip(i)->GetMemorySize();
        }
        return total;
    }
    void Basic(Fixture& f)
    {
        Core::ResourceRegistry r;
        CHECK(r.Initialize());
        const auto context = Context(r);
        R::SkeletalPublicationLimits limits;
        R::SkeletalPublicationReport report;
        R::SkeletalPublishedAsset out;
        auto prepared = Prepare(f, context);
        CHECK(r.GetResourceCount() == 0);
        const auto asset = R::Detail::GetPreparedSkeletalAsset(prepared);
        const auto expectedMemory = Memory(asset);
        CHECK(R::CommitSkeletalPublication(prepared, "skeletal:1:Animal", limits, out, report));
        CHECK(report.PublishedResources == 6 && !report.bCacheHit && r.GetResourceCount() == 6 &&
              r.GetCachedPathCount() == 1);
        CHECK(r.GetTotalMemoryUsage() == expectedMemory && out.Asset == asset);
        Handles(r, out);
        for (const auto& row : r.GetRecords())
        {
            CHECK(row.URI == C::String("Actors/Animal"));
        }
        CHECK(!r.FindHandle<Core::AnimationClipResource>("Actors/Animal").IsValid());
        CHECK(!r.FindHandle<Core::SkinnedMeshResource>("Actors/Animal").IsValid());
        const auto before = Observe(r);
        const auto saved = out;
        CHECK(R::CommitSkeletalPublication(prepared, "skeletal:1:Animal", limits, out, report) && report.bCacheHit &&
              Same(saved, out));
        CHECK(R::FindPublishedSkeletalAsset(context, "skeletal:1:Animal", "Actors/Animal", limits, out, report) &&
              report.bCacheHit);
        CHECK(!R::FindPublishedSkeletalAsset(context, "missing", "Actors/Animal", limits, out, report) &&
              report.Status == R::SkeletalPublicationStatus::CacheMiss);
        CHECK(!R::FindPublishedSkeletalAsset(context, "skeletal:1:Animal", "Actors/Other", limits, out, report) &&
              report.Status == R::SkeletalPublicationStatus::KeyCollision);
        CHECK(!R::CommitSkeletalPublication(prepared, "another-key", limits, out, report) &&
              report.Status == R::SkeletalPublicationStatus::IdCollision);
        CHECK(Same(saved, out));
        Unchanged(before, r);
        auto duplicate = Prepare(f, context);
        C::TWeakPtr<Core::SkeletalAssetResource> weak = R::Detail::GetPreparedSkeletalAsset(duplicate);
        CHECK(R::CommitSkeletalPublication(duplicate, "skeletal:1:Animal", limits, out, report) && report.bCacheHit &&
              Same(saved, out));
        duplicate = {};
        CHECK(weak.expired());
        Unchanged(before, r);
        auto budget = limits;
        budget.MaxBundleClips = 2;
        CHECK(!R::FindPublishedSkeletalAsset(context, "skeletal:1:Animal", "Actors/Animal", budget, out, report));
        CHECK(report.Status == R::SkeletalPublicationStatus::BudgetExceeded && Same(saved, out));
        auto fresh = Prepare(f, context);
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            budget = limits;
            if (mode == 0)
            {
                budget.MaxSlots = 11;
            }
            else if (mode == 1)
            {
                budget.MaxMapEntries = 13;
            }
            else if (mode == 2)
            {
                budget.MaxCopiedBuckets = 1;
            }
            else
            {
                budget.MaxBundleClips = 2;
            }
            CHECK(!R::CommitSkeletalPublication(fresh, "new-key", budget, out, report));
            CHECK(report.Status == R::SkeletalPublicationStatus::BudgetExceeded && Same(saved, out));
            Unchanged(before, r);
        }
        using Char = C::String::value_type;
        const C::String nul{Char('a'), Char(0), Char('b')};
        for (const auto& key : {C::String{}, nul})
        {
            CHECK(!R::CommitSkeletalPublication(fresh, key, limits, out, report) && Same(saved, out));
        }
        bool wrong = false;
        T::Thread worker(
            [&]()
            {
                auto result = out;
                R::SkeletalPublicationReport local;
                wrong = !R::CommitSkeletalPublication(fresh, "new-key", limits, result, local) &&
                        local.Status == R::SkeletalPublicationStatus::WrongOwnerThread && Same(result, out);
                CHECK(!R::FindPublishedSkeletalAsset(context, "skeletal:1:Animal", "Actors/Animal", limits, result,
                                                     local));
                CHECK(local.Status == R::SkeletalPublicationStatus::WrongOwnerThread);
            });
        worker.Join();
        CHECK(wrong);
        Unchanged(before, r);
        // 公開済みaggregateが利用者に改変されたとき、重複childを正常cacheと扱わない。
        C::VariableArray<C::TSharedPtr<Core::AnimationClipResource>> original;
        for (size_t i = 0; i < out.Asset->GetClipCount(); ++i)
        {
            original.push_back(out.Asset->GetClip(i));
        }
        out.Asset->SetClipResources(out.Asset->GetMesh(), out.Asset->GetSkeleton(),
                                    {original[0], original[0], original[2]});
        CHECK(!R::FindPublishedSkeletalAsset(context, "skeletal:1:Animal", "Actors/Animal", limits, out, report));
        CHECK(report.Status == R::SkeletalPublicationStatus::InvalidCachedAsset);
        out.Asset->SetClipResources(out.Asset->GetMesh(), out.Asset->GetSkeleton(), original);
        Unchanged(before, r);
    }
    struct Fault
    {
        Point Target = Point::BeforeCommit;
        uint32_t Index = 0;
        bool bThrow = false, bCalled = false;
        C::VariableArray<C::TWeakPtr<Core::Resource>> Weak;
    };
    bool FaultProbe(Point point, uint32_t index, const C::TSharedPtr<Core::SkeletalAssetResource>& asset, void* context)
    {
        auto& f = *static_cast<Fault*>(context);
        if (point != f.Target || index != f.Index)
        {
            return true;
        }
        f.bCalled = true;
        f.Weak.push_back(asset);
        f.Weak.push_back(asset->GetMesh());
        f.Weak.push_back(asset->GetSkeleton());
        for (size_t i = 0; i < asset->GetClipCount(); ++i)
        {
            f.Weak.push_back(asset->GetClip(i));
        }
        if (f.bThrow)
        {
            throw std::bad_alloc();
        }
        return false;
    }
    void Faults(Fixture& f)
    {
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            Core::ResourceRegistry r;
            CHECK(r.Initialize());
            R::Detail::StressSkeletalOuterRehashForTest(r);
            const auto context = Context(r);
            R::SkeletalPublicationLimits limits;
            R::SkeletalPublicationReport report;
            R::SkeletalPublishedAsset existing;
            C::TSharedPtr<Core::SkeletonResource> sentinel;
            if (mode == 1 || mode == 2)
            {
                auto seed = Prepare(f, context);
                CHECK(R::CommitSkeletalPublication(seed, "old", limits, existing, report));
                if (mode == 2)
                {
                    seed = {};
                    existing = {};
                    r.CollectGarbage();
                    r.CollectGarbage();
                    CHECK(r.GetResourceCount() == 0);
                }
            }
            else if (mode == 3)
            {
                sentinel = r.CreateTransient<Core::SkeletonResource>("sentinel");
                CHECK(sentinel && sentinel->Load());
            }
            const auto before = Observe(r);
            R::SkeletalPublishedAsset out = existing;
            const auto old = out;
            for (bool throwing : {false, true})
            {
                for (Point point :
                     {Point::AfterClone, Point::AfterRegister, Point::AfterPlaceholder, Point::BeforeCommit})
                {
                    const uint32_t count = point == Point::AfterRegister ? 6 : point == Point::BeforeCommit ? 1 : 4;
                    for (uint32_t index = 0; index < count; ++index)
                    {
                        auto prepared = Prepare(f, context);
                        Fault fault{point, index, throwing};
                        CHECK(!R::Detail::CommitSkeletalPublicationWithProbe(prepared, "new", limits, out, report,
                                                                             FaultProbe, &fault));
                        CHECK(fault.bCalled && Same(old, out));
                        Unchanged(before, r);
                        CHECK(report.Status == (throwing ? R::SkeletalPublicationStatus::Exception
                                                         : R::SkeletalPublicationStatus::InjectedFailure));
                        prepared = {};
                        for (const auto& weak : fault.Weak)
                        {
                            CHECK(weak.expired());
                        }
                    }
                }
            }
            auto prepared = Prepare(f, context);
            CHECK(R::CommitSkeletalPublication(prepared, "new", limits, out, report));
            Handles(r, out);
            CHECK(r.GetResourceCount() == before.Count + 6 && r.GetCachedPathCount() == before.Paths + 1);
            if (existing.Asset)
            {
                Handles(r, existing);
                CHECK(existing.Asset->IsLoaded() && existing.Asset->GetMesh()->IsLoaded());
            }
        }
    }
    void Sessions(Fixture& f)
    {
        Core::ResourceRegistry r;
        CHECK(r.Initialize());
        const auto context = Context(r);
        R::SkeletalPublicationLimits limits;
        R::SkeletalPublicationReport report;
        R::SkeletalPublishedAsset out;
        auto stale = Prepare(f, context);
        const auto oldAsset = R::Detail::GetPreparedSkeletalAsset(stale);
        r.Shutdown();
        CHECK(r.Initialize());
        CHECK(!R::CommitSkeletalPublication(stale, "key", limits, out, report) &&
              report.Status == R::SkeletalPublicationStatus::SessionChanged);
        CHECK(r.GetResourceCount() == 0);
        auto fresh = Prepare(f, context);
        CHECK(R::CommitSkeletalPublication(fresh, "key", limits, out, report));
        CHECK(!R::CommitSkeletalPublication(stale, "key", limits, out, report) &&
              report.Status == R::SkeletalPublicationStatus::SessionChanged);
        auto defaultContext = context;
        defaultContext.OwnerThread = {};
        CHECK(!R::PrepareSkeletalPublication(f.Cpu, defaultContext, stale, report) &&
              R::Detail::GetPreparedSkeletalAsset(stale) == oldAsset);
        const auto reset = +[](Point point, uint32_t, const C::TSharedPtr<Core::SkeletalAssetResource>&, void* ctx)
        {
            CHECK(point == Point::AfterAssembly);
            auto& registry = *static_cast<Core::ResourceRegistry*>(ctx);
            registry.Shutdown();
            CHECK(registry.Initialize());
            return true;
        };
        CHECK(!R::Detail::PrepareSkeletalPublicationWithProbe(f.Cpu, context, stale, report, reset, &r));
        CHECK(report.Status == R::SkeletalPublicationStatus::SessionChanged &&
              R::Detail::GetPreparedSkeletalAsset(stale) == oldAsset && r.GetResourceCount() == 0);
        Core::ResourceRegistry empty;
        CHECK(!R::PrepareSkeletalPublication(f.Cpu, Context(empty), stale, report) &&
              report.Status == R::SkeletalPublicationStatus::RegistryNotReady);
        auto invalid = Prepare(f, context);
        auto value = R::Detail::GetPreparedSkeletalAsset(invalid);
        value->GetMesh()->Unload();
        CHECK(!R::CommitSkeletalPublication(invalid, "invalid", limits, out, report) &&
              report.Status == R::SkeletalPublicationStatus::InvalidCandidate);
        CHECK(r.GetResourceCount() == 0);
    }
    void Garbage(Fixture& f)
    {
        Core::ResourceRegistry r;
        CHECK(r.Initialize());
        const auto context = Context(r);
        R::SkeletalPublicationLimits limits;
        R::SkeletalPublicationReport report;
        R::SkeletalPublishedAsset out;
        auto prepared = Prepare(f, context);
        CHECK(R::CommitSkeletalPublication(prepared, "key", limits, out, report));
        const auto aggregate = out.AggregateHandle;
        const auto mesh = out.MeshHandle;
        const auto skeleton = out.SkeletonHandle;
        const auto clips = out.ClipHandles;
        auto held = out.Asset->GetClip(size_t{1});
        const auto heldMemory = held->GetMemorySize();
        auto lease = out.Asset->GetMesh()->GetRenderAssetLease();
        CHECK(lease && lease->HasValidRenderData());
        C::TWeakPtr<Core::SkinnedMeshResource> weakMesh = out.Asset->GetMesh();
        prepared = {};
        out = {};
        r.CollectGarbage();
        r.CollectGarbage();
        CHECK(r.GetResourceCount() == 1 && r.GetCachedPathCount() == 0 && r.GetTotalMemoryUsage() == heldMemory);
        CHECK(!r.Resolve(aggregate) && !r.Resolve(mesh) && !r.Resolve(skeleton) && r.Resolve(clips[1]) == held);
        CHECK(weakMesh.expired() && lease->GetVertices().size() == 3 && lease->GetIndices().size() == 6);
        held.reset();
        r.CollectGarbage();
        CHECK(r.GetResourceCount() == 0 && !r.Resolve(clips[1]));
        prepared = Prepare(f, context);
        CHECK(R::CommitSkeletalPublication(prepared, "key", limits, out, report));
        CHECK(out.AggregateHandle.Index == aggregate.Index && out.AggregateHandle.Generation != aggregate.Generation);
        CHECK(!r.Resolve(aggregate) && !r.Resolve(mesh) && !r.Resolve(skeleton));
        for (const auto& old : clips)
        {
            CHECK(!r.Resolve(old));
        }
        Handles(r, out);
    }
    void AtomicReader(Fixture& f)
    {
        for (bool abort : {false, true})
        {
            Core::ResourceRegistry r;
            CHECK(r.Initialize());
            const auto context = Context(r);
            auto prepared = Prepare(f, context);
            R::SkeletalPublishedAsset out;
            R::SkeletalPublicationReport report;
            struct Gate
            {
                T::Mutex Mutex;
                T::ConditionVariable Cv;
                bool bEntered = false, bAttempt = false, bAbort = false;
            } gate;
            gate.bAbort = abort;
            const auto probe = +[](Point p, uint32_t, const C::TSharedPtr<Core::SkeletalAssetResource>&, void* ptr)
            {
                if (p != Point::BeforeCommit)
                {
                    return true;
                }
                auto& g = *static_cast<Gate*>(ptr);
                T::ScopedLock lock(g.Mutex);
                g.bEntered = true;
                g.Cv.NotifyAll();
                CHECK(g.Cv.WaitFor(g.Mutex, std::chrono::seconds(10), [&]() { return g.bAttempt; }));
                return !g.bAbort;
            };
            size_t observed = SIZE_MAX;
            T::Thread reader(
                [&]()
                {
                    {
                        T::ScopedLock lock(gate.Mutex);
                        CHECK(gate.Cv.WaitFor(gate.Mutex, std::chrono::seconds(10), [&]() { return gate.bEntered; }));
                        gate.bAttempt = true;
                        gate.Cv.NotifyAll();
                    }
                    observed = r.GetRecords().size();
                });
            CHECK(R::Detail::CommitSkeletalPublicationWithProbe(prepared, "key", {}, out, report, probe, &gate) ==
                  !abort);
            reader.Join();
            CHECK(observed == (abort ? 0 : 6));
        }
    }
} // namespace
int main()
{
    Fixture f;
    Basic(f);
    Faults(f);
    Sessions(f);
    Garbage(f);
    AtomicReader(f);
    std::puts(
        "SKELETAL_BUNDLE_PUBLICATION result=pass shadow_all_or_none_typed_handles_exact_cache_session_budgets_failure_gc_reader_no_async_gpu");
    return 0;
}
