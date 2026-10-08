// 分離bundleのidentity、全clip内容、実Registry四型公開と失敗原子性。
#include "RigSplitPublicationFixture.h"
#include "Resource/SkeletalAssetPublicationTestAccess.h"
#include <limits>
#include <new>
#include <thread>
#include "Thread/Atomic.h"
namespace F = NorvesLib::Tests::RigSplitPublicationFixture;
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace S = Core::Skeletal;
namespace R = Core::ResourceIO;
namespace D = R::Detail;
namespace T = NorvesLib::Thread;
using Point = D::SkeletalPublicationPoint;
namespace
{
    R::SkeletalCacheDomain Domain(Core::ResourceRegistry& registry)
    {
        R::SkeletalCacheDomain domain;
        R::SkeletalPublicationReport report;
        RIG_CHECK(R::AllocateSkeletalCacheDomain(F::Context(registry), domain, report));
        return domain;
    }
    R::SkeletalPreparedPublication Prepare(const C::TSharedPtr<const R::RigSplitPublicationReceipt>& receipt,
                                           Core::ResourceRegistry& registry)
    {
        R::SkeletalPreparedPublication result;
        R::SkeletalPublicationReport report;
        RIG_CHECK(R::PrepareRigSplitPublication(receipt, F::Context(registry), result, report));
        RIG_CHECK(report.SplitAssembly.Status == S::RigV1Status::Success);
        return result;
    }
    struct State
    {
        size_t Count, Paths, Memory, Pools;
        C::VariableArray<Core::ResourceRecord> Records;
        explicit State(Core::ResourceRegistry& r)
            : Count(r.GetResourceCount()), Paths(r.GetCachedPathCount()), Memory(r.GetTotalMemoryUsage()),
              Pools(D::SkeletalRegistryPoolCountForTest(r)), Records(r.GetRecords())
        {
            std::sort(Records.begin(), Records.end(), [](const auto& a, const auto& b) { return a.Id < b.Id; });
        }
        void Check(Core::ResourceRegistry& r) const
        {
            State b(r);
            RIG_CHECK(Count == b.Count && Paths == b.Paths && Memory == b.Memory && Pools == b.Pools &&
                      Records.size() == b.Records.size());
            for (size_t i = 0; i < Records.size(); ++i)
            {
                const auto& x = Records[i];
                const auto& y = b.Records[i];
                RIG_CHECK(x.Id == y.Id && x.Generation == y.Generation && x.URI == y.URI && x.Type == y.Type &&
                          x.LoadState == y.LoadState && x.VersionHash == y.VersionHash &&
                          x.DependencyCount == y.DependencyCount && x.MemoryUsage == y.MemoryUsage);
            }
        }
    };
    void Identity(F::Fixture& f)
    {
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        const auto domain = Domain(registry);
        auto base = F::Identity(f, domain, f.Request);
        const auto* stable = base.GetData();
        const auto same = F::Identity(f, domain, f.Request);
        RIG_CHECK(R::SameRigSplitRequestIdentity(base, same));
        auto q = f.Request;
        q.Policy.Tolerance.TranslationMeters = -0.;
        auto zero = F::Identity(f, domain, q);
        q.Policy.Tolerance.TranslationMeters = 0.;
        RIG_CHECK(R::SameRigSplitRequestIdentity(zero, F::Identity(f, domain, q)));
        RIG_CHECK(!std::signbit(zero.GetData()->Plan.Policy.Tolerance.TranslationMeters));
        q = f.Request;
        q.Policy.bAllowRestMismatch = true;
        RIG_CHECK(!R::SameRigSplitRequestIdentity(base, F::Identity(f, domain, q)));
        q = f.Request;
        q.Policy.Tolerance.RotationRadians *= 2;
        RIG_CHECK(!R::SameRigSplitRequestIdentity(base, F::Identity(f, domain, q)));
        q = f.Request;
        q.Limits.MaxWireBytes -= 1;
        RIG_CHECK(!R::SameRigSplitRequestIdentity(base, F::Identity(f, domain, q)));
        q = f.Request;
        q.MaxTotalPackageBytes -= 1;
        RIG_CHECK(!R::SameRigSplitRequestIdentity(base, F::Identity(f, domain, q)));
        q = f.Request;
        std::swap(q.BankPaths[0], q.BankPaths[1]);
        RIG_CHECK(!R::SameRigSplitRequestIdentity(base, F::Identity(f, domain, q)));
        q = f.Request;
        q.Variant = "alternate";
        auto missing = F::Identity(f, domain, q);
        RIG_CHECK(!missing.GetData()->bCanCache);
        C::TSharedPtr<const R::RigSplitPublicationReceipt> failed;
        Core::RigSplitAssetDiagnostics diagnostics;
        RIG_CHECK(!R::LoadRigSplitForPublication(missing, failed, diagnostics) && !failed &&
                  diagnostics.Load.Status == R::RigSplitLoadStatus::ResolveRejected);
        RIG_CHECK(!R::SameRigSplitRequestIdentity(base, F::Identity(f, domain, f.Request, 2)));
        RIG_CHECK(!R::SameRigSplitRequestIdentity(base, F::Identity(f, Domain(registry), f.Request)));
        R::RigSplitRequestIdentity output = base;
        const auto build = [&](const S::RigSplitRequest& request, size_t limit)
        {
            return R::BuildRigSplitRequestIdentity(request, f.Snapshot, domain.Session, domain.Ordinal, 1, limit,
                                                   output);
        };
        RIG_CHECK(build(f.Request, stable->Key.size()) == R::RigSplitIdentityStatus::Success);
        output = base;
        RIG_CHECK(build(f.Request, stable->Key.size() - 1) == R::RigSplitIdentityStatus::LimitExceeded &&
                  output.GetData() == stable);
        q = f.Request;
        q.Policy.Tolerance.LogScale = std::numeric_limits<double>::quiet_NaN();
        RIG_CHECK(build(q, R::RigSplitMaximumKeyBytes) == R::RigSplitIdentityStatus::InvalidRequest);
        q = f.Request;
        q.Policy.Tolerance.LogScale = -1;
        RIG_CHECK(build(q, R::RigSplitMaximumKeyBytes) == R::RigSplitIdentityStatus::InvalidRequest);
        q = f.Request;
        q.BankPaths.push_back(q.BankPaths[0]);
        RIG_CHECK(build(q, R::RigSplitMaximumKeyBytes) == R::RigSplitIdentityStatus::InvalidRequest);
        q = f.Request;
        q.SkeletonPath = "../rig";
        RIG_CHECK(build(q, R::RigSplitMaximumKeyBytes) == R::RigSplitIdentityStatus::InvalidRequest);
        auto another = f.MakeSnapshot(f.Json);
        RIG_CHECK(R::BuildRigSplitRequestIdentity(f.Request, another, domain.Session, domain.Ordinal, 1,
                                                  R::RigSplitMaximumKeyBytes,
                                                  output) == R::RigSplitIdentityStatus::Success);
        RIG_CHECK(output.GetData()->Key == base.GetData()->Key && !R::SameRigSplitRequestIdentity(base, output));
        const auto receipt = F::Load(base);
        RIG_CHECK(receipt->GetCpu().GetData()->Clips.size() == 3 && receipt->GetEvidence().Entries.size() == 5);
        for (size_t i = 0; i < 5; ++i)
        {
            RIG_CHECK(
                R::SameRigSplitReference(receipt->GetEvidence().Entries[i].Reference, base.GetData()->References[i]));
        }
        std::printf("SPLIT_PUBLICATION_CASE result=pass complete_identity_policy_order_limits_snapshot_exact_budget\n");
    }
    void Basic(F::Fixture& f)
    {
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        const auto domain = Domain(registry);
        auto id = F::Identity(f, domain, f.Request);
        auto receipt = F::Load(id);
        auto prepared = Prepare(receipt, registry);
        R::SkeletalPublishedAsset value;
        R::SkeletalPublicationReport report;
        RIG_CHECK(registry.GetResourceCount() == 0);
        RIG_CHECK(R::CommitSkeletalPublication(prepared, id.GetData()->Key, F::Limits(), value, report));
        RIG_CHECK(report.PublishedResources == 6 && registry.GetResourceCount() == 6 &&
                  registry.GetCachedPathCount() == 1);
        F::Handles(registry, value);
        F::Pose(value.Asset);
        RIG_CHECK(value.Asset->GetResourcePath() == id.GetData()->BundleUri &&
                  value.Asset->GetResourcePath() != id.GetData()->Key);
        RIG_CHECK(R::RigSplitAssetAccess::Get(*value.Asset)->GetDiagnostics()->Assembly.Status ==
                  S::RigV1Status::Success);
        RIG_CHECK(R::RigSplitAssetAccess::Get(*value.Asset)->GetOwnedBytes() > 0 &&
                  value.Asset->GetMesh()->GetSplitMesh()->Materials.size() == 1);
        R::SkeletalPublishedAsset acquired;
        RIG_CHECK(R::FindPublishedRigSplitAsset(F::Context(registry), id, F::Limits(), acquired, report) &&
                  report.bCacheHit && acquired.Asset == value.Asset);
        const auto old = value.Asset;
        const State before(registry);
        RIG_CHECK(!R::CommitSkeletalPublication(prepared, _T("wrong"), F::Limits(), value, report) &&
                  value.Asset == old);
        before.Check(registry);
        RIG_CHECK(!R::FindPublishedSkeletalAsset(F::Context(registry), id.GetData()->Key, id.GetData()->BundleUri,
                                                 F::Limits(), value, report) &&
                  report.Status == R::SkeletalPublicationStatus::InvalidCachedAsset);
        const auto ownedReceipt = R::RigSplitAssetAccess::Get(*value.Asset);
        C::VariableArray<C::TSharedPtr<Core::AnimationClipResource>> keptClips;
        for (size_t i = 0; i < value.Asset->GetClipCount(); ++i)
        {
            keptClips.push_back(value.Asset->GetClip(i));
        }
        value.Asset->SetClipResources(value.Asset->GetMesh(), value.Asset->GetSkeleton(), keptClips);
        RIG_CHECK(!R::RigSplitAssetAccess::Get(*value.Asset));
        RIG_CHECK(!R::FindPublishedSkeletalAsset(F::Context(registry), id.GetData()->Key, id.GetData()->BundleUri,
                                                 F::Limits(), value, report) &&
                  report.Status == R::SkeletalPublicationStatus::InvalidCachedAsset);
        RIG_CHECK(!R::FindPublishedRigSplitAsset(F::Context(registry), id, F::Limits(), value, report) &&
                  report.Status == R::SkeletalPublicationStatus::InvalidCachedAsset);
        R::RigSplitAssetAccess::Attach(*value.Asset, ownedReceipt);
        const auto original = value.Asset->GetClip(0)->GetClip();
        auto changed = original;
        changed.Channels[0].Samples[0].Value.Y += 1;
        value.Asset->GetClip(0)->SetClip(std::move(changed));
        RIG_CHECK(value.Asset->GetClip(0)->IsLoaded());
        RIG_CHECK(!R::FindPublishedRigSplitAsset(F::Context(registry), id, F::Limits(), value, report) &&
                  report.Status == R::SkeletalPublicationStatus::InvalidCachedAsset && value.Asset == old);
        value.Asset->GetClip(0)->SetClip(S::SkeletalAnimationClip(original));
        RIG_CHECK(R::FindPublishedRigSplitAsset(F::Context(registry), id, F::Limits(), value, report));
        auto other = f.Request;
        other.MeshPath = "Models/Mesh2.nvskel";
        auto id2 = F::Identity(f, domain, other);
        auto prepared2 = Prepare(F::Load(id2), registry);
        R::SkeletalPublishedAsset second;
        RIG_CHECK(R::CommitSkeletalPublication(prepared2, id2.GetData()->Key, F::Limits(), second, report));
        RIG_CHECK(second.Asset->GetSkeleton() != value.Asset->GetSkeleton() &&
                  second.SkeletonHandle != value.SkeletonHandle);
        RIG_CHECK(second.Asset->GetSkeleton()->GetSplitSkeleton()->ContentHash ==
                  value.Asset->GetSkeleton()->GetSplitSkeleton()->ContentHash);
        F::Pose(second.Asset, -6);
        F::Pose(value.Asset);
        std::printf(
            "SPLIT_PUBLICATION_CASE result=pass registry_six_resources_one_path_full_clip_cache_guard_mesh_owned_pose\n");
    }
    struct Fault
    {
        Point Target;
        uint32_t Index;
        bool Throw = false, Called = false;
        C::VariableArray<C::TWeakPtr<Core::Resource>> Weak;
    };
    bool Probe(Point point, uint32_t index, const C::TSharedPtr<Core::SkeletalAssetResource>& asset, void* p)
    {
        auto& f = *static_cast<Fault*>(p);
        if (point != f.Target || index != f.Index)
        {
            return true;
        }
        f.Called = true;
        f.Weak.push_back(asset);
        f.Weak.push_back(asset->GetMesh());
        f.Weak.push_back(asset->GetSkeleton());
        for (size_t i = 0; i < asset->GetClipCount(); ++i)
        {
            f.Weak.push_back(asset->GetClip(i));
        }
        if (f.Throw)
        {
            throw std::bad_alloc();
        }
        return false;
    }
    void Faults(F::Fixture& f)
    {
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            Core::ResourceRegistry registry;
            RIG_CHECK(registry.Initialize());
            D::StressSkeletalOuterRehashForTest(registry);
            auto domain = Domain(registry);
            auto id = F::Identity(f, domain, f.Request);
            auto receipt = F::Load(id);
            R::SkeletalPublishedAsset existing;
            R::SkeletalPublicationReport report;
            C::TSharedPtr<Core::SkeletonResource> sentinel;
            if (mode == 1 || mode == 2)
            {
                auto seedId = F::Identity(f, domain, f.Request, 2);
                auto seed = Prepare(F::Load(seedId), registry);
                RIG_CHECK(R::CommitSkeletalPublication(seed, seedId.GetData()->Key, F::Limits(), existing, report));
                if (mode == 2)
                {
                    seed = {};
                    existing = {};
                    registry.CollectGarbage();
                    registry.CollectGarbage();
                    RIG_CHECK(registry.GetResourceCount() == 0);
                }
            }
            if (mode == 3)
            {
                sentinel = registry.CreateTransient<Core::SkeletonResource>(_T("sentinel"));
                RIG_CHECK(sentinel && sentinel->Load());
            }
            const State before(registry);
            R::SkeletalPublishedAsset out = existing;
            for (bool throwing : {false, true})
            {
                for (Point point : {Point::AfterAssembly, Point::AfterClone, Point::AfterRegister,
                                    Point::AfterPlaceholder, Point::BeforeCommit})
                {
                    const uint32_t count =
                        point == Point::AfterRegister
                            ? 6
                            : (point == Point::AfterAssembly || point == Point::BeforeCommit ? 1 : 4);
                    for (uint32_t index = 0; index < count; ++index)
                    {
                        Fault fault{point, index, throwing};
                        R::SkeletalPreparedPublication prepared;
                        if (point == Point::AfterAssembly)
                        {
                            RIG_CHECK(!D::PrepareRigSplitPublicationWithProbe(receipt, F::Context(registry), prepared,
                                                                              report, Probe, &fault));
                        }
                        else
                        {
                            prepared = Prepare(receipt, registry);
                            RIG_CHECK(!D::CommitSkeletalPublicationWithProbe(prepared, id.GetData()->Key, F::Limits(),
                                                                             out, report, Probe, &fault));
                        }
                        RIG_CHECK(fault.Called && out.Asset == existing.Asset);
                        before.Check(registry);
                        prepared = {};
                        for (const auto& weak : fault.Weak)
                        {
                            RIG_CHECK(weak.expired());
                        }
                    }
                }
            }
            auto prepared = Prepare(receipt, registry);
            auto low = F::Limits();
            low.MaxSlots = 1;
            RIG_CHECK(!R::CommitSkeletalPublication(prepared, id.GetData()->Key, low, out, report));
            before.Check(registry);
            T::Atomic<bool> stop{false}, bad{false}, started{false};
            std::thread reader(
                [&]()
                {
                    started.Store(true);
                    while (!stop.Load())
                    {
                        const auto count = registry.GetResourceCount();
                        if (count != before.Count && count != before.Count + 6)
                        {
                            bad.Store(true);
                        }
                    }
                });
            while (!started.Load())
            {
                std::this_thread::yield();
            }
            const bool committed = R::CommitSkeletalPublication(prepared, id.GetData()->Key, F::Limits(), out, report);
            stop.Store(true);
            reader.join();
            RIG_CHECK(committed && !bad.Load());
            F::Handles(registry, out);
            RIG_CHECK(registry.GetResourceCount() == before.Count + 6 &&
                      registry.GetCachedPathCount() == before.Paths + 1);
        }
        std::printf("SPLIT_PUBLICATION_CASE result=pass empty_existing_free_missing_pools_faults_retry_all_or_zero\n");
    }
    void Lifetime(F::Fixture& f)
    {
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        auto domain = Domain(registry);
        auto id = F::Identity(f, domain, f.Request);
        auto receipt = F::Load(id);
        auto prepared = Prepare(receipt, registry);
        R::SkeletalPublicationReport report;
        R::SkeletalPublishedAsset published;
        auto wrong = F::Context(registry);
        wrong.OwnerThread = {};
        R::SkeletalPreparedPublication out;
        RIG_CHECK(!R::PrepareRigSplitPublication(receipt, wrong, out, report) &&
                  report.Status == R::SkeletalPublicationStatus::WrongOwnerThread);
        registry.Shutdown();
        RIG_CHECK(registry.Initialize());
        RIG_CHECK(!R::CommitSkeletalPublication(prepared, id.GetData()->Key, F::Limits(), published, report) &&
                  report.Status == R::SkeletalPublicationStatus::SessionChanged);
        prepared = {};
        domain = Domain(registry);
        id = F::Identity(f, domain, f.Request);
        receipt = F::Load(id);
        prepared = Prepare(receipt, registry);
        RIG_CHECK(R::CommitSkeletalPublication(prepared, id.GetData()->Key, F::Limits(), published, report));
        const auto handle = published.AggregateHandle;
        auto clip = published.Asset->GetClip(1);
        C::TWeakPtr<const R::RigSplitPublicationReceipt> weak = R::RigSplitAssetAccess::Get(*published.Asset);
        prepared = {};
        published = {};
        receipt.reset();
        id = {};
        registry.CollectGarbage();
        registry.CollectGarbage();
        RIG_CHECK(weak.expired() && !registry.Resolve(handle) && registry.GetResourceCount() == 1 &&
                  registry.GetCachedPathCount() == 0);
        clip.reset();
        registry.CollectGarbage();
        RIG_CHECK(registry.GetResourceCount() == 0);
        std::printf("SPLIT_PUBLICATION_CASE result=pass wrong_owner_session_stale_handles_receipt_gc_clip_lifetime\n");
    }
    void RestPolicy()
    {
        F::Fixture f(true);
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        auto domain = Domain(registry);
        auto q = f.Request;
        q.Policy.bAllowRestMismatch = true;
        auto overrideId = F::Identity(f, domain, q);
        auto receipt = F::Load(overrideId);
        RIG_CHECK(receipt->GetCpu().GetData()->BindingReport.Banks.size() == 3);
        for (const auto& report : receipt->GetCpu().GetData()->BindingReport.Banks)
        {
            RIG_CHECK(report.bOverrideUsed && report.bComparisonComplete && report.Snapshots[0].ExceededJoints == 1);
        }
        auto prepared = Prepare(receipt, registry);
        R::SkeletalPublicationReport report;
        R::SkeletalPublishedAsset out;
        RIG_CHECK(R::CommitSkeletalPublication(prepared, overrideId.GetData()->Key, F::Limits(), out, report));
        const State before(registry);
        auto strict = F::Identity(f, domain, f.Request);
        RIG_CHECK(!R::SameRigSplitRequestIdentity(strict, overrideId));
        Core::RigSplitAssetDiagnostics diagnostics;
        auto stable = receipt;
        RIG_CHECK(!R::LoadRigSplitForPublication(strict, receipt, diagnostics) && receipt == stable);
        RIG_CHECK(diagnostics.Load.Status == R::RigSplitLoadStatus::BindingRejected &&
                  diagnostics.Load.BindingReport.Status == S::RigV1Status::RestMismatch &&
                  diagnostics.Load.BindingReport.FailedBank == 0);
        RIG_CHECK(!R::FindPublishedRigSplitAsset(F::Context(registry), strict, F::Limits(), out, report) &&
                  report.Status == R::SkeletalPublicationStatus::CacheMiss);
        before.Check(registry);
        std::printf(
            "SPLIT_PUBLICATION_CASE result=pass explicit_override_report_strict_separate_cache_default_rejection\n");
    }
} // namespace
int main()
{
    {
        F::Fixture fixture;
        Identity(fixture);
        Basic(fixture);
        Faults(fixture);
        Lifetime(fixture);
    }
    RestPolicy();
    std::printf("SPLIT_PUBLICATION result=pass complete_identity_owner_atomic_cache_receipt_cpu_no_gpu\n");
    return 0;
}
