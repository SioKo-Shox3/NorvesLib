#include "Resource/SkeletalAssetPublication.h"
#include "Resource/SkeletalAssetPublicationTestAccess.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <algorithm>
#include "Debug/Stats.h"
#include <exception>
#include <limits>
#include <type_traits>
#include <utility>

namespace NorvesLib::Core::ResourceIO
{
    namespace C = Container;
    using Status = SkeletalPublicationStatus;
    using Probe = Detail::SkeletalPublicationProbe;
    using Point = Detail::SkeletalPublicationPoint;
    struct SkeletalPreparedPublication::State
    {
        ResourceRegistry* Registry = nullptr;
        NorvesLib::Thread::Thread::ThreadId Owner;
        uint64_t Session = 0;
        C::String LogicalPath;
        C::TSharedPtr<SkeletalAssetResource> Asset;
    };
    SkeletalPreparedPublication::SkeletalPreparedPublication() = default;
    SkeletalPreparedPublication::~SkeletalPreparedPublication() = default;
    SkeletalPreparedPublication::SkeletalPreparedPublication(SkeletalPreparedPublication&&) noexcept = default;
    SkeletalPreparedPublication& SkeletalPreparedPublication::operator=(SkeletalPreparedPublication&&) noexcept =
        default;
    bool SkeletalPreparedPublication::IsValid() const noexcept
    {
        return bool(m_State);
    }
    namespace
    {
        bool Fail(SkeletalPublicationReport& r, Status s)
        {
            r.Status = s;
            return false;
        }
        bool Owner(const NorvesLib::Thread::Thread::ThreadId& owner)
        {
            return owner != NorvesLib::Thread::Thread::ThreadId{} &&
                   owner == NorvesLib::Thread::Thread::GetCurrentThreadId();
        }
        bool Text(const C::String& text, size_t limit)
        {
            if (text.empty())
            {
                return false;
            }
            const auto measured =
                Asset::MeasureSkeletalNameEncoding(2, C::Span<const C::String::value_type>{text.data(), text.size()});
            return measured.Succeeded() && measured.ByteCount <= limit;
        }
        bool Limits(const SkeletalPublicationLimits& l)
        {
            return l.MaxBundleClips > 0 && l.MaxBundleClips <= UINT32_MAX - 3 && l.MaxKeyBytes > 0 && l.MaxSlots > 0 &&
                   l.MaxMapEntries > 0 && l.MaxCopiedBuckets > 0;
        }
        bool Call(Probe probe, Point point, uint32_t index, const C::TSharedPtr<SkeletalAssetResource>& asset,
                  void* context, SkeletalPublicationReport& r)
        {
            return !probe || probe(point, index, asset, context) || Fail(r, Status::InjectedFailure);
        }
        bool Complete(const C::TSharedPtr<SkeletalAssetResource>& asset, size_t maximum)
        {
            if (!asset || !asset->IsLoaded() || !asset->IsValid() || asset->GetResourceId() == 0 || !asset->GetMesh() ||
                !asset->GetSkeleton() || asset->GetClipCount() == 0 || asset->GetClipCount() > maximum)
            {
                return false;
            }
            const auto valid = [&](const auto& r)
            {
                return r && r->GetResourceId() != 0 && r->IsLoaded() && r->IsValid() &&
                       r->GetResourcePath() == asset->GetResourcePath();
            };
            if (!valid(asset->GetMesh()) || !valid(asset->GetSkeleton()))
            {
                return false;
            }
            for (size_t i = 0; i < asset->GetClipCount(); ++i)
            {
                if (!valid(asset->GetClip(i)))
                {
                    return false;
                }
            }
            return true;
        }
        bool DistinctIds(const C::TSharedPtr<SkeletalAssetResource>& asset)
        {
            C::VariableArray<uint64_t> ids;
            ids.reserve(asset->GetClipCount() + 3);
            ids.push_back(asset->GetResourceId());
            ids.push_back(asset->GetMesh()->GetResourceId());
            ids.push_back(asset->GetSkeleton()->GetResourceId());
            for (size_t i = 0; i < asset->GetClipCount(); ++i)
            {
                ids.push_back(asset->GetClip(i)->GetResourceId());
            }
            std::sort(ids.begin(), ids.end());
            return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
        }
    } // namespace
    // Registryの4型だけへアクセスする。既存の単体Registerは変更しない。
    class SkeletalBundlePublisherAccess
    {
        template <typename T> using Pool = ResourceRegistry::TResourcePool<T>;
        using Base = ResourceRegistry::IResourcePool;
        using PoolPointer = C::TUniquePtr<Base>;
        static uint64_t Session(ResourceRegistry& r)
        {
            NorvesLib::Thread::ScopedLock lock(r.m_Mutex);
            return r.m_bInitialized ? r.m_SessionEpoch : 0;
        }
        static bool SessionLocked(const ResourceRegistry& r, uint64_t expected, SkeletalPublicationReport& report)
        {
            if (!r.m_bInitialized)
            {
                return Fail(report, Status::RegistryNotReady);
            }
            report.Session = r.m_SessionEpoch;
            return expected == 0 || expected == r.m_SessionEpoch || Fail(report, Status::SessionChanged);
        }
        template <typename T> static const Pool<T>* Old(const ResourceRegistry& r)
        {
            return r.FindPool<T>();
        }
        template <typename T>
        static bool HandleFor(const ResourceRegistry& r, const C::TSharedPtr<T>& value, ResourceHandle<T>& out)
        {
            const auto* pool = Old<T>(r);
            if (!pool || !value)
            {
                return false;
            }
            const auto handle = pool->FindById(value->GetResourceId());
            if (!handle.IsValid() || pool->Resolve(handle) != value)
            {
                return false;
            }
            out = handle;
            return true;
        }
        static bool AcquireLocked(ResourceRegistry& r, const Identity& key, const C::String& logicalPath,
                                  const SkeletalPublicationLimits& limits, SkeletalPublishedAsset& candidate,
                                  SkeletalPublicationReport& report)
        {
            const auto* pool = Old<SkeletalAssetResource>(r);
            if (!pool || pool->m_PathToIndex.find(key) == pool->m_PathToIndex.end())
            {
                return Fail(report, Status::CacheMiss);
            }
            const auto handle = pool->FindByPath(key);
            const auto asset = pool->Resolve(handle);
            if (!handle.IsValid() || !asset || pool->m_Slots[handle.Index].Path.GetView() != key.GetView() ||
                asset->GetResourcePath() != logicalPath)
            {
                return Fail(report, Status::KeyCollision);
            }
            if (asset->GetClipCount() > limits.MaxBundleClips)
            {
                return Fail(report, Status::BudgetExceeded);
            }
            if (!Complete(asset, limits.MaxBundleClips) || !DistinctIds(asset))
            {
                return Fail(report, Status::InvalidCachedAsset);
            }
            SkeletalPublishedAsset local;
            local.Asset = asset;
            local.AggregateHandle = handle;
            if (!HandleFor(r, asset->GetMesh(), local.MeshHandle) ||
                !HandleFor(r, asset->GetSkeleton(), local.SkeletonHandle))
            {
                return Fail(report, Status::InvalidCachedAsset);
            }
            local.ClipHandles.resize(asset->GetClipCount());
            for (size_t i = 0; i < asset->GetClipCount(); ++i)
            {
                if (!HandleFor(r, asset->GetClip(i), local.ClipHandles[i]))
                {
                    return Fail(report, Status::InvalidCachedAsset);
                }
            }
            candidate = std::move(local);
            report.bCacheHit = true;
            report.Status = Status::Success;
            return true;
        }
        template <typename T>
        static bool Budget(const ResourceRegistry& r, uint64_t adds, const SkeletalPublicationLimits& limits,
                           SkeletalPublicationReport& report, uint64_t& finalSlots, uint64_t& finalEntries)
        {
            const auto* pool = Old<T>(r);
            const uint64_t slots = pool ? pool->m_Slots.size() : 0, free = pool ? pool->m_FreeList.size() : 0;
            const uint64_t ids = pool ? pool->m_IdToIndex.size() : 0, paths = pool ? pool->m_PathToIndex.size() : 0;
            const uint64_t idBuckets = pool ? pool->m_IdToIndex.bucket_count() : 0;
            const uint64_t pathBuckets = pool ? pool->m_PathToIndex.bucket_count() : 0;
            if (report.CopiedBuckets > limits.MaxCopiedBuckets ||
                idBuckets > limits.MaxCopiedBuckets - report.CopiedBuckets ||
                pathBuckets > limits.MaxCopiedBuckets - report.CopiedBuckets - idBuckets)
            {
                return Fail(report, Status::BudgetExceeded);
            }
            const uint64_t buckets = idBuckets + pathBuckets;
            const uint64_t growth = adds > free ? adds - free : 0;
            if (slots > UINT32_MAX || growth > UINT32_MAX - slots)
            {
                return Fail(report, Status::BudgetExceeded);
            }
            // 4poolの件数はu32 slot数で有界。map数もslot数以下という既存pool契約を検査する。
            if (free > slots || ids > slots || paths > slots ||
                buckets > limits.MaxCopiedBuckets - report.CopiedBuckets)
            {
                return Fail(report, Status::BudgetExceeded);
            }
            report.CopiedSlots += slots;
            report.CopiedIdEntries += ids;
            report.CopiedPathEntries += paths;
            report.CopiedFreeIndices += free;
            report.CopiedBuckets += buckets;
            finalSlots += slots + growth;
            finalEntries += ids + paths + adds;
            return (finalSlots <= limits.MaxSlots && finalEntries <= limits.MaxMapEntries) ||
                   Fail(report, Status::BudgetExceeded);
        }
        template <typename T> static C::TUniquePtr<Pool<T>> Clone(const ResourceRegistry& r)
        {
            const auto* old = Old<T>(r);
            return old ? C::MakeUnique<Pool<T>>(*old) : C::MakeUnique<Pool<T>>();
        }
        template <typename T> static bool Unregistered(const ResourceRegistry& r, const C::TSharedPtr<T>& value)
        {
            const auto* pool = Old<T>(r);
            return !pool || pool->m_IdToIndex.find(value->GetResourceId()) == pool->m_IdToIndex.end();
        }
        static_assert(noexcept(std::hash<std::type_index>{}(std::declval<const std::type_index&>())));
        static_assert(noexcept(std::declval<const std::type_index&>() == std::declval<const std::type_index&>()));
        static_assert(std::is_nothrow_destructible_v<std::type_index>);
        struct PlaceholderGuard
        {
            ResourceRegistry& Registry;
            C::FixedArray<const std::type_info*, 4> Keys{&typeid(SkinnedMeshResource), &typeid(SkeletonResource),
                                                         &typeid(AnimationClipResource),
                                                         &typeid(SkeletalAssetResource)};
            C::FixedArray<bool, 4> Added{false, false, false, false};
            bool bCommitted = false;
            ~PlaceholderGuard() noexcept
            {
                if (bCommitted)
                {
                    return;
                }
                // type_indexのhash/等値は確保しない。今回のnull nodeだけを削除する。
                for (size_t i = 0; i < 4; ++i)
                {
                    if (Added[i])
                    {
                        Registry.m_TypePools.erase(std::type_index(*Keys[i]));
                    }
                }
            }
        };

      public:
        static void SetDomainCounter(ResourceRegistry& registry, uint64_t next)
        {
            NorvesLib::Thread::ScopedLock lock(registry.m_Mutex);
            registry.m_NextSkeletalCacheDomain = next;
        }
        static bool Domain(const SkeletalAssetCreateContext& context, SkeletalCacheDomain& domain,
                           SkeletalPublicationReport& report, bool allocate)
        {
            report = {};
            if (!Owner(context.OwnerThread))
            {
                return Fail(report, Status::WrongOwnerThread);
            }
            if (!context.Registry)
            {
                return Fail(report, Status::RegistryNotReady);
            }
            auto& r = *context.Registry;
            NorvesLib::Thread::ScopedLock lock(r.m_Mutex);
            if (!SessionLocked(r, allocate ? 0 : domain.Session, report))
            {
                return false;
            }
            if (allocate)
            {
                if (r.m_NextSkeletalCacheDomain == UINT64_MAX)
                {
                    return Fail(report, Status::BudgetExceeded);
                }
                domain = {r.m_SessionEpoch, r.m_NextSkeletalCacheDomain++};
            }
            else if (domain.Session == 0 || domain.Ordinal == 0 || domain.Ordinal >= r.m_NextSkeletalCacheDomain)
            {
                return Fail(report, Status::InvalidRequest);
            }
            report.Status = Status::Success;
            return true;
        }
        static size_t PoolCount(ResourceRegistry& r)
        {
            NorvesLib::Thread::ScopedLock lock(r.m_Mutex);
            return r.m_TypePools.size();
        }
        static void StressRehash(ResourceRegistry& r)
        {
            NorvesLib::Thread::ScopedLock lock(r.m_Mutex);
            r.m_TypePools.max_load_factor(0.125f);
        }
        static C::TSharedPtr<SkeletalAssetResource> Peek(const SkeletalPreparedPublication& p)
        {
            return p.m_State ? p.m_State->Asset : C::TSharedPtr<SkeletalAssetResource>{};
        }
        static bool Prepare(const CookedSkeletalCpuAsset& cpu, const SkeletalAssetCreateContext& context,
                            SkeletalPreparedPublication& out, SkeletalPublicationReport& report, Probe probe,
                            void* user)
        {
            report = {};
            try
            {
                if (!Owner(context.OwnerThread))
                {
                    return Fail(report, Status::WrongOwnerThread);
                }
                if (!context.Registry)
                {
                    return Fail(report, Status::RegistryNotReady);
                }
                const auto session = Session(*context.Registry);
                if (!session)
                {
                    return Fail(report, Status::RegistryNotReady);
                }
                auto state = C::MakeUnique<SkeletalPreparedPublication::State>();
                state->Registry = context.Registry;
                state->Owner = context.OwnerThread;
                state->Session = session;
                if (!AssembleCookedSkeletalAsset(cpu, context, state->Asset, report.Assembly))
                {
                    return Fail(report, Status::AssemblyFailed);
                }
                state->LogicalPath = *cpu.GetResourcePath();
                if (!Call(probe, Point::AfterAssembly, 0, state->Asset, user, report))
                {
                    return false;
                }
                if (Session(*context.Registry) != session)
                {
                    return Fail(report, Status::SessionChanged);
                }
                report.Session = session;
                report.Status = Status::Success;
                out.m_State = std::move(state);
                return true;
            }
            catch (const std::exception&)
            {
                return Fail(report, Status::Exception);
            }
        }
        static bool Find(const SkeletalAssetCreateContext& context, const C::String& keyText, const C::String& path,
                         const SkeletalPublicationLimits& limits, SkeletalPublishedAsset& out,
                         SkeletalPublicationReport& report)
        {
            report = {};
            try
            {
                if (!Owner(context.OwnerThread))
                {
                    return Fail(report, Status::WrongOwnerThread);
                }
                if (!context.Registry)
                {
                    return Fail(report, Status::RegistryNotReady);
                }
                if (!Limits(limits) || !Text(keyText, limits.MaxKeyBytes) || !Text(path, limits.MaxKeyBytes))
                {
                    return Fail(report, Status::InvalidRequest);
                }
                const Identity key(keyText);
                if (!key.IsValid() || key.GetView() != C::StringView(keyText))
                {
                    return Fail(report, Status::KeyCollision);
                }
                auto& r = *context.Registry;
                NorvesLib::Thread::ScopedLock lock(r.m_Mutex);
                if (!SessionLocked(r, 0, report))
                {
                    return false;
                }
                SkeletalPublishedAsset candidate;
                if (!AcquireLocked(r, key, path, limits, candidate, report))
                {
                    return false;
                }
                out = std::move(candidate);
                return true;
            }
            catch (const std::exception&)
            {
                return Fail(report, Status::Exception);
            }
        }
        static bool Commit(const SkeletalPreparedPublication& prepared, const C::String& keyText,
                           const SkeletalPublicationLimits& limits, SkeletalPublishedAsset& out,
                           SkeletalPublicationReport& report, Probe probe, void* user)
        {
            report = {};
            try
            {
                if (!prepared.m_State)
                {
                    return Fail(report, Status::InvalidRequest);
                }
                const auto& s = *prepared.m_State;
                if (!Owner(s.Owner))
                {
                    return Fail(report, Status::WrongOwnerThread);
                }
                if (!Limits(limits) || !Text(keyText, limits.MaxKeyBytes))
                {
                    return Fail(report, Status::InvalidRequest);
                }
                if (!s.Registry)
                {
                    return Fail(report, Status::RegistryNotReady);
                }
                const Identity key(keyText);
                if (!key.IsValid() || key.GetView() != C::StringView(keyText))
                {
                    return Fail(report, Status::KeyCollision);
                }
                auto& r = *s.Registry;
                NorvesLib::Thread::ScopedLock lock(r.m_Mutex);
                if (!SessionLocked(r, s.Session, report))
                {
                    return false;
                }
                SkeletalPublishedAsset candidate;
                if (AcquireLocked(r, key, s.LogicalPath, limits, candidate, report))
                {
                    out = std::move(candidate);
                    return true;
                }
                if (report.Status != Status::CacheMiss)
                {
                    return false;
                }
                report.Status = Status::InvalidCandidate;
                const auto& asset = s.Asset;
                if (asset && asset->GetClipCount() > limits.MaxBundleClips)
                {
                    return Fail(report, Status::BudgetExceeded);
                }
                if (!Complete(asset, limits.MaxBundleClips) || asset->GetResourcePath() != s.LogicalPath)
                {
                    return false;
                }
                if (!DistinctIds(asset) || !Unregistered(r, asset) || !Unregistered(r, asset->GetMesh()) ||
                    !Unregistered(r, asset->GetSkeleton()))
                {
                    return Fail(report, Status::IdCollision);
                }
                for (size_t i = 0; i < asset->GetClipCount(); ++i)
                {
                    if (!Unregistered(r, asset->GetClip(i)))
                    {
                        return Fail(report, Status::IdCollision);
                    }
                }
                uint64_t finalSlots = 0, finalEntries = 1; // aggregateの新path 1件を先に含める。
                if (!Budget<SkinnedMeshResource>(r, 1, limits, report, finalSlots, finalEntries) ||
                    !Budget<SkeletonResource>(r, 1, limits, report, finalSlots, finalEntries) ||
                    !Budget<AnimationClipResource>(r, asset->GetClipCount(), limits, report, finalSlots,
                                                   finalEntries) ||
                    !Budget<SkeletalAssetResource>(r, 1, limits, report, finalSlots, finalEntries))
                {
                    return false;
                }
                C::FixedArray<PoolPointer, 4> shadow;
                // 計測scopeの終了もlive pool変更前に済ませる。
                const auto prepareShadow = [&]() -> bool
                {
                    NORVES_STAT_SCOPE_CATEGORY("SkeletalAsset.PublishShadow", "AssetLoad");
                    auto mesh = Clone<SkinnedMeshResource>(r);
                    if (!Call(probe, Point::AfterClone, 0, asset, user, report))
                    {
                        return false;
                    }
                    auto skeleton = Clone<SkeletonResource>(r);
                    if (!Call(probe, Point::AfterClone, 1, asset, user, report))
                    {
                        return false;
                    }
                    auto clips = Clone<AnimationClipResource>(r);
                    if (!Call(probe, Point::AfterClone, 2, asset, user, report))
                    {
                        return false;
                    }
                    auto aggregate = Clone<SkeletalAssetResource>(r);
                    if (!Call(probe, Point::AfterClone, 3, asset, user, report))
                    {
                        return false;
                    }
                    candidate.Asset = asset;
                    candidate.MeshHandle = mesh->Register(asset->GetMesh(), {});
                    if (!Call(probe, Point::AfterRegister, 0, asset, user, report))
                    {
                        return false;
                    }
                    candidate.SkeletonHandle = skeleton->Register(asset->GetSkeleton(), {});
                    if (!Call(probe, Point::AfterRegister, 1, asset, user, report))
                    {
                        return false;
                    }
                    candidate.ClipHandles.resize(asset->GetClipCount());
                    for (size_t i = 0; i < asset->GetClipCount(); ++i)
                    {
                        candidate.ClipHandles[i] = clips->Register(asset->GetClip(i), {});
                        if (!Call(probe, Point::AfterRegister, static_cast<uint32_t>(i + 2), asset, user, report))
                        {
                            return false;
                        }
                    }
                    candidate.AggregateHandle = aggregate->Register(asset, keyText);
                    if (!Call(probe, Point::AfterRegister, static_cast<uint32_t>(asset->GetClipCount() + 2), asset,
                              user, report))
                    {
                        return false;
                    }
                    if (mesh->Resolve(candidate.MeshHandle) != asset->GetMesh() ||
                        skeleton->Resolve(candidate.SkeletonHandle) != asset->GetSkeleton() ||
                        aggregate->Resolve(candidate.AggregateHandle) != asset ||
                        aggregate->FindByPath(key) != candidate.AggregateHandle)
                    {
                        return Fail(report, Status::InvalidCandidate);
                    }
                    for (size_t i = 0; i < asset->GetClipCount(); ++i)
                    {
                        if (clips->Resolve(candidate.ClipHandles[i]) != asset->GetClip(i))
                        {
                            return Fail(report, Status::InvalidCandidate);
                        }
                    }
                    shadow[0] = std::move(mesh);
                    shadow[1] = std::move(skeleton);
                    shadow[2] = std::move(clips);
                    shadow[3] = std::move(aggregate);
                    return true;
                };
                if (!prepareShadow())
                {
                    return false;
                }
                PlaceholderGuard guard{r};
                C::FixedArray<PoolPointer*, 4> targets{nullptr, nullptr, nullptr, nullptr};
                for (size_t i = 0; i < 4; ++i)
                {
                    const auto inserted = r.m_TypePools.emplace(std::type_index(*guard.Keys[i]), nullptr);
                    guard.Added[i] = inserted.second;
                    targets[i] = &inserted.first->second; // unordered_mapのmapped参照はrehashで失効しない。
                    if (!Call(probe, Point::AfterPlaceholder, static_cast<uint32_t>(i), asset, user, report))
                    {
                        return false;
                    }
                }
                if (!Call(probe, Point::BeforeCommit, 0, asset, user, report))
                {
                    return false;
                }
                static_assert(noexcept(std::declval<PoolPointer&>().swap(std::declval<PoolPointer&>())));
                static_assert(std::is_nothrow_move_assignable_v<SkeletalPublishedAsset>);
                for (size_t i = 0; i < 4; ++i)
                {
                    targets[i]->swap(shadow[i]);
                }
                guard.bCommitted = true;
                report.PublishedResources = static_cast<uint32_t>(asset->GetClipCount() + 3);
                report.Status = Status::Success;
                out = std::move(candidate);
                return true;
            }
            catch (const std::exception&)
            {
                return Fail(report, Status::Exception);
            }
        }
    };
    void Detail::SetSkeletalCacheDomainCounterForTest(ResourceRegistry& registry, uint64_t next)
    {
        SkeletalBundlePublisherAccess::SetDomainCounter(registry, next);
    }
    bool AllocateSkeletalCacheDomain(const SkeletalAssetCreateContext& context, SkeletalCacheDomain& out,
                                     SkeletalPublicationReport& report)
    {
        return SkeletalBundlePublisherAccess::Domain(context, out, report, true);
    }
    bool ValidateSkeletalCacheDomain(const SkeletalAssetCreateContext& context, const SkeletalCacheDomain& domain,
                                     SkeletalPublicationReport& report)
    {
        auto copy = domain;
        return SkeletalBundlePublisherAccess::Domain(context, copy, report, false);
    }
    bool PrepareSkeletalPublication(const CookedSkeletalCpuAsset& cpu, const SkeletalAssetCreateContext& context,
                                    SkeletalPreparedPublication& out, SkeletalPublicationReport& report)
    {
        return SkeletalBundlePublisherAccess::Prepare(cpu, context, out, report, nullptr, nullptr);
    }
    bool FindPublishedSkeletalAsset(const SkeletalAssetCreateContext& context, const C::String& key,
                                    const C::String& path, const SkeletalPublicationLimits& limits,
                                    SkeletalPublishedAsset& out, SkeletalPublicationReport& report)
    {
        return SkeletalBundlePublisherAccess::Find(context, key, path, limits, out, report);
    }
    bool CommitSkeletalPublication(const SkeletalPreparedPublication& prepared, const C::String& key,
                                   const SkeletalPublicationLimits& limits, SkeletalPublishedAsset& out,
                                   SkeletalPublicationReport& report)
    {
        return SkeletalBundlePublisherAccess::Commit(prepared, key, limits, out, report, nullptr, nullptr);
    }
    bool Detail::PrepareSkeletalPublicationWithProbe(const CookedSkeletalCpuAsset& cpu,
                                                     const SkeletalAssetCreateContext& context,
                                                     SkeletalPreparedPublication& out,
                                                     SkeletalPublicationReport& report, Probe probe, void* user)
    {
        return SkeletalBundlePublisherAccess::Prepare(cpu, context, out, report, probe, user);
    }
    bool Detail::CommitSkeletalPublicationWithProbe(const SkeletalPreparedPublication& prepared, const C::String& key,
                                                    const SkeletalPublicationLimits& limits,
                                                    SkeletalPublishedAsset& out, SkeletalPublicationReport& report,
                                                    Probe probe, void* user)
    {
        return SkeletalBundlePublisherAccess::Commit(prepared, key, limits, out, report, probe, user);
    }
    C::TSharedPtr<SkeletalAssetResource> Detail::GetPreparedSkeletalAsset(const SkeletalPreparedPublication& prepared)
    {
        return SkeletalBundlePublisherAccess::Peek(prepared);
    }
    size_t Detail::SkeletalRegistryPoolCountForTest(ResourceRegistry& r)
    {
        return SkeletalBundlePublisherAccess::PoolCount(r);
    }
    void Detail::StressSkeletalOuterRehashForTest(ResourceRegistry& r)
    {
        SkeletalBundlePublisherAccess::StressRehash(r);
    }
} // namespace NorvesLib::Core::ResourceIO
