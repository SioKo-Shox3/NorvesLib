// 実worker/P1/P2aからowner delegateまでを子processで反証する。
#include "Animation/SkeletalAssetRuntime.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Resource/SkeletalAssetRuntimeTestAccess.h"
#include "Resource/SkeletalAssetPublication.h"
#include "Resource/SkeletalAssetPublicationTestAccess.h"
#include "SkeletalLoaderFixture.h"
#include "Asset/AssetSystem.h"
#include "Thread/JobSystem.h"
#include "Thread/ConditionVariable.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <thread>
#include <process.h>
#include <windows.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Skeletal runtime %s:%d %s\n", __FILE__, __LINE__, #x);                               \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace A = Core::Asset;
namespace R = Core::ResourceIO;
namespace D = R::Detail;
namespace T = NorvesLib::Thread;
namespace F = NorvesLib::Tests::SkeletalLoaderFixture;
using S = Core::SkeletalRuntimeStatus;
using Failure = Core::SkeletalRuntimeFailure;
using Runtime = Core::SkeletalAssetRuntime;
using Completion = Core::SkeletalAssetCompletion;
using Access = D::SkeletalRuntimeTestAccess;
using Point = D::SkeletalRuntimePoint;
using Observation = D::SkeletalRuntimeObservation;
namespace NorvesLib::Thread
{
    // この実行fileには他のJobSystemTestAccess定義をリンクしない。
    struct JobSystemTestAccess
    {
        static void FailPush(JobSystem& jobs, bool enabled)
        {
            jobs.m_submitPreparationHook = enabled ? +[](JobSystem::SubmitPreparationPoint p, void*)
            {
                if (p == JobSystem::SubmitPreparationPoint::BeforeGlobalPush || p == JobSystem::SubmitPreparationPoint::BeforeLocalPush) { throw std::bad_alloc(); }
            } : nullptr;
        }
    };
} // namespace NorvesLib::Thread
namespace
{
    constexpr const char* Unicode = "Actors/\xE7\x8A\xAC\xF0\x9F\x90\x95";
    struct Gate
    {
        T::Mutex Mutex;
        T::ConditionVariable Condition;
        bool Signaled = false;
        void Signal()
        {
            T::ScopedLock lock(Mutex);
            Signaled = true;
            Condition.NotifyAll();
        }
        void Wait()
        {
            T::ScopedLock lock(Mutex);
            CHECK(Condition.WaitFor(Mutex, std::chrono::seconds(10), [&]() { return Signaled; }));
        }
    };
    struct Fixture
    {
        std::filesystem::path Root;
        C::TSharedPtr<A::AssetSystem> Snapshot;
        explicit Fixture(int variant = 0)
        {
            char name[96];
            std::snprintf(name, sizeof(name), "norves-runtime-%llu-%d",
                          static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()),
                          variant);
            Root = std::filesystem::temp_directory_path() / name;
            CHECK(std::filesystem::create_directory(Root));
            auto payload = F::BuildThreeClips();
            if (variant == 1)
            {
                F::WriteFloat(payload, 320, 2.f);
                F::RecomputeSkeletalHash(payload);
            }
            if (variant == 2)
            {
                payload[0] ^= 1;
            }
            uint64_t hash = 0;
            const auto package = F::Package(payload, A::MakeAssetPackageFourCC('S', 'k', 'l', '0'), hash);
            std::ofstream file(Root / "asset.nvpkg", std::ios::binary);
            CHECK(file);
            file.write(reinterpret_cast<const char*>(package.data()), static_cast<std::streamsize>(package.size()));
            file.close();
            CHECK(!file.fail());
            Snapshot = C::MakeShared<A::AssetSystem>(C::AnsiString(Root.generic_string().c_str()));
            C::AnsiString manifest = "{\"version\":1,\"assets\":[";
            const char* paths[] = {"Actors/A", "Actors/B", "Actors/C", "Actors/D"};
            bool first = true;
            for (const auto* path : paths)
            {
                if (!first)
                {
                    manifest += ",";
                }
                first = false;
                manifest += "{\"logical_path\":\"";
                manifest += path;
                manifest +=
                    "\",\"kind\":\"model\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"";
                manifest += variant == 3 ? "wrong" : "nvskel.v0.skinned.pnujiw.u32";
                manifest +=
                    "\",\"cooked_package\":\"asset.nvpkg\",\"entry_name\":\"a\",\"entry_type\":\"Skl0\",\"cooked_hash\":\"";
                manifest += A::FormatAssetHashHex(hash);
                manifest += "\",\"cooked_version\":0";
                if (variant == 4)
                {
                    manifest +=
                        ",\"metadata\":{\"vertex_count\":999,\"index_count\":6,\"joint_count\":2,\"clip_count\":3}";
                }
                manifest += "}";
            }
            manifest += "]}";
            A::AssetManifest parsedManifest;
            const bool parsed = parsedManifest.LoadFromJsonText(F::CoreText(manifest));
            if (!parsed)
            {
                std::fprintf(stderr, "Runtime fixture manifest: status=%u error=%s json=%s\n",
                             static_cast<unsigned>(parsedManifest.GetParseStatus()),
                             parsedManifest.GetParseError().c_str(), manifest.c_str());
            }
            CHECK(parsed);
            CHECK(Snapshot->LoadManifestFromJsonText(F::CoreText(manifest)));
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };
    struct Environment
    {
        Fixture Files;
        Core::ResourceRegistry Registry;
        T::JobSystem& Jobs = T::JobSystem::Get();
        Runtime Value;
        Environment(T::JobSystem::ExecutionMode mode, int variant = 0, const Core::SkeletalRuntimeLimits& limits = {})
            : Files(variant)
        {
            CHECK(Registry.Initialize());
            Jobs.Initialize(1, mode);
            CHECK(Value.Bind(Registry, Jobs, Files.Snapshot, T::Thread::GetCurrentThreadId(), limits) == S::Success);
        }
        ~Environment()
        {
            Value.Close();
            CHECK(Value.Drain() == S::Drained);
            Jobs.Shutdown();
            Registry.Shutdown();
        }
    };
    uint64_t Load(Runtime& runtime, const char* path, Runtime::Callback callback)
    {
        const auto result = runtime.LoadAsync(C::AnsiStringView(path), std::move(callback));
        CHECK(result.Status == S::Accepted && result.RequestId != 0);
        return result.RequestId;
    }
    Completion LoadOne(Environment& env, const char* path = "Actors/A")
    {
        Completion value;
        int calls = 0;
        Load(env.Value, path,
             [&](const Completion& c)
             {
                 value = c;
                 ++calls;
             });
        CHECK(calls == 0);
        CHECK(Access::WaitReady(env.Value, 1));
        const auto f = env.Value.FlushCompleted();
        CHECK(f.Status == S::Success && f.Groups == 1 && f.Callbacks == 1 && calls == 1);
        CHECK(env.Value.GetPendingCount() == 0);
        return value;
    }
    void CheckAsset(Core::ResourceRegistry& registry, const C::TSharedPtr<Core::SkeletalAssetResource>& asset)
    {
        CHECK(asset && asset->GetClipCount() == 3);
        CHECK(registry.Resolve(registry.GetHandle<Core::SkeletalAssetResource>(asset->GetResourceId())) == asset);
        CHECK(registry.Resolve(registry.GetHandle<Core::SkinnedMeshResource>(asset->GetMesh()->GetResourceId())) ==
              asset->GetMesh());
        CHECK(registry.Resolve(registry.GetHandle<Core::SkeletonResource>(asset->GetSkeleton()->GetResourceId())) ==
              asset->GetSkeleton());
        for (size_t i = 0; i < asset->GetClipCount(); ++i)
        {
            CHECK(registry.Resolve(registry.GetHandle<Core::AnimationClipResource>(
                      asset->GetClip(i)->GetResourceId())) == asset->GetClip(i));
        }
        const auto clip = asset->GetClip(C::StringView(_T("Wave")));
        CHECK(clip);
        const auto& v = asset->GetMesh()->GetMeshNodeGlobalTransform();
        NorvesLib::Math::Matrix4x4 matrix(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11],
                                          v[12], v[13], v[14], v[15]);
        Core::Animation::SkeletalPoseSnapshot pose;
        CHECK(Core::Animation::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *clip, *asset->GetMesh(), 1.f,
                                                                matrix, pose));
        CHECK(pose.BonePalette.size() == 2 && std::abs(pose.BonePalette[1].values[13] - 1.f) < 1e-5f);
    }
    void SharingAndLifetime(T::JobSystem::ExecutionMode mode)
    {
        Environment env(mode);
        CHECK(env.Value.Drain() == S::NotClosed);
        C::TSharedPtr<Core::SkeletalAssetResource> asset;
        size_t callbacks = 0;
        const auto first = Load(env.Value, "Actors/./A",
                                [&](const Completion& c)
                                {
                                    CHECK(c.Failure == Failure::None);
                                    asset = c.Asset;
                                    ++callbacks;
                                });
        for (int i = 0; i < 5; ++i)
        {
            CHECK(Load(env.Value, "Assets/Actors/A",
                       [&](const Completion& c)
                       {
                           CHECK(c.Asset == asset);
                           ++callbacks;
                       }) == first);
        }
        CHECK(Access::WaitReady(env.Value, 1));
        const auto before = Access::Counts(env.Value);
        CHECK(before.WorkerCalls == 1 && before.AssemblyCalls == 0 && callbacks == 0 &&
              env.Registry.GetResourceCount() == 0);
        const auto weakRequest = Access::WeakRequest(env.Value, first);
        CHECK(env.Value.FlushCompleted().Callbacks == 6 && callbacks == 6 && weakRequest.expired());
        CHECK(env.Registry.GetResourceCount() == 6 && env.Registry.GetCachedPathCount() == 1);
        CheckAsset(env.Registry, asset);
        const auto handle = env.Registry.GetHandle<Core::SkeletalAssetResource>(asset->GetResourceId());
        C::TWeakPtr<Core::SkeletalAssetResource> weakAsset = asset;
        const auto cachedId = Load(env.Value, "Actors/A",
                                   [&](const Completion& c)
                                   {
                                       CHECK(c.Asset == weakAsset.lock() && c.bCacheHit);
                                       ++callbacks;
                                   });
        CHECK(cachedId != first && callbacks == 6 && Access::Counts(env.Value).WorkerCalls == 1);
        asset.reset();
        env.Registry.CollectGarbage();
        env.Registry.CollectGarbage();
        CHECK(!weakAsset.expired());
        CHECK(env.Value.FlushCompleted(1).Callbacks == 1 && callbacks == 7);
        env.Registry.CollectGarbage();
        env.Registry.CollectGarbage();
        CHECK(weakAsset.expired() && !env.Registry.Resolve(handle));
        CHECK(env.Registry.GetResourceCount() == 0 && env.Registry.GetCachedPathCount() == 0);
        auto next = LoadOne(env);
        CHECK(next.Asset && next.Asset->GetResourceId() != handle.ResourceId);
        auto clip = next.Asset->GetClip(size_t{2});
        const auto clipHandle = env.Registry.GetHandle<Core::AnimationClipResource>(clip->GetResourceId());
        next.Asset.reset();
        env.Registry.CollectGarbage();
        env.Registry.CollectGarbage();
        CHECK(env.Registry.GetResourceCount() == 1 && env.Registry.Resolve(clipHandle) == clip &&
              env.Registry.GetCachedPathCount() == 0);
        clip.reset();
        env.Registry.CollectGarbage();
        CHECK(env.Registry.GetResourceCount() == 0);
        auto unicode = LoadOne(env, Unicode);
        CHECK(unicode.Failure == Failure::ResolveRejected && !unicode.Asset);
        CHECK(env.Registry.GetResourceCount() == 0);
        // 既存manifest入口はASCII限定。UTF-8の入力検証と非ASCII資産の読込成功を混同しない。
        const auto nonAsciiManifest =
            C::AnsiString("{\"version\":1,\"assets\":[{\"logical_path\":\"") + Unicode +
            "\",\"kind\":\"model\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"nvskel.v0.skinned.pnujiw.u32\",\"cooked_package\":\"asset.nvpkg\",\"entry_name\":\"a\",\"entry_type\":\"Skl0\",\"cooked_hash\":\"0000000000000001\",\"cooked_version\":0}]}";
        A::AssetManifest rejected;
        CHECK(!rejected.LoadFromJsonText(F::CoreText(nonAsciiManifest)));
        CHECK(rejected.GetParseStatus() == A::AssetManifestParseStatus::RequiredFieldMissing);
        const auto retained = LoadOne(env, "Actors/D");
        CHECK(retained.Failure == Failure::None && retained.Asset);
        env.Value.Close();
        CHECK(env.Value.Drain() == S::Drained);
        CHECK(retained.Asset->IsLoaded());
        std::printf("SKELETAL_RUNTIME_CASE result=pass sharing_gc_utf8_input_ascii_manifest_boundary\n");
    }
    void ReentrantBatch(T::JobSystem::ExecutionMode mode)
    {
        Environment env(mode);
        size_t a = 0, b = 0, c = 0, newCalls = 0;
        uint64_t idA = 0, idB = 0;
        idA = Load(env.Value, "Actors/A",
                   [&](const Completion& result)
                   {
                       ++a;
                       CHECK(result.Failure == Failure::None);
                       CHECK(env.Value.FlushCompleted().Status == S::Reentrant);
                       CHECK(env.Value.SetSnapshot(env.Files.Snapshot) == S::Busy);
                       CHECK(Load(env.Value, "Actors/A",
                                  [asset = result.Asset, &a](const Completion& next)
                                  {
                                      CHECK(next.Asset == asset);
                                      ++a;
                                  }) == idA);
                       CHECK(Load(env.Value, "Actors/B", [&](const Completion&) { ++b; }) == idB);
                       Load(env.Value, "Actors/D", [&](const Completion&) { ++newCalls; });
                   });
        CHECK(Access::WaitReady(env.Value, 1));
        idB = Load(env.Value, "Actors/B", [&](const Completion&) { ++b; });
        CHECK(Access::WaitReady(env.Value, 2));
        Load(env.Value, "Actors/C", [&](const Completion&) { ++c; });
        CHECK(Access::WaitReady(env.Value, 3));
        auto first = env.Value.FlushCompleted(2);
        CHECK(first.Groups == 2 && first.Callbacks == 2 && a == 1 && b == 1 && c == 0 && newCalls == 0);
        CHECK(Access::WaitReady(env.Value, 4));
        auto second = env.Value.FlushCompleted();
        CHECK(second.Groups == 4 && second.Callbacks == 4 && a == 2 && b == 2 && c == 1 && newCalls == 1);
        CHECK(Access::Counts(env.Value).WorkerCalls == 4 && Access::Counts(env.Value).CommitCalls == 4);
        int called = 0;
        Load(env.Value, "Actors/A",
             [&](const Completion&)
             {
                 ++called;
                 throw 7;
             });
        Load(env.Value, "Actors/A", [&](const Completion&) { ++called; });
        auto exceptions = env.Value.FlushCompleted();
        CHECK(exceptions.Callbacks == 2 && exceptions.CallbackExceptions == 1 && called == 2);
        std::printf("SKELETAL_RUNTIME_CASE result=pass finite_batch_reentrant_exceptions\n");
    }
    struct ThrowAt
    {
        Point At;
        bool BadAlloc;
    };
    void ThrowFault(Point point, uint64_t, void* context)
    {
        const auto& value = *static_cast<ThrowAt*>(context);
        if (point == value.At)
        {
            if (value.BadAlloc)
            {
                throw std::bad_alloc();
            }
            throw 17;
        }
    }
    void Failures(T::JobSystem::ExecutionMode mode)
    {
        for (const auto point : {Point::Admission, Point::BeforeHandler, Point::AfterHandler, Point::BetweenIndexes,
                                 Point::BeforeSubmit, Point::WorkerBeforeLoad, Point::WorkerAfterLoad,
                                 Point::BeforeAssembly, Point::AfterAssembly, Point::BeforeCommit})
        {
            for (const bool bad : {false, true})
            {
                Environment env(mode);
                ThrowAt fault{point, bad};
                Access::SetHooks(env.Value, {ThrowFault, nullptr, &fault});
                Completion result;
                int calls = 0;
                const auto admitted = env.Value.LoadAsync("Actors/A",
                                                          [&](const Completion& value)
                                                          {
                                                              result = value;
                                                              ++calls;
                                                          });
                if (point == Point::Admission || point == Point::BeforeHandler || point == Point::AfterHandler ||
                    point == Point::BetweenIndexes || point == Point::BeforeSubmit)
                {
                    CHECK(admitted.RequestId == 0 &&
                          admitted.Status ==
                              (point == Point::BeforeSubmit ? S::SubmitException : S::PreparationException));
                    CHECK(calls == 0 && env.Value.GetPendingCount() == 0 && Access::Counts(env.Value).Handoffs == 0);
                }
                else
                {
                    CHECK(admitted.Status == S::Accepted);
                    CHECK(Access::WaitReady(env.Value, 1));
                    CHECK(env.Value.FlushCompleted().Callbacks == 1 && calls == 1 && !result.Asset);
                    CHECK(result.Failure == ((point == Point::WorkerBeforeLoad || point == Point::WorkerAfterLoad)
                                                 ? Failure::WorkerException
                                                 : Failure::OwnerException));
                }
                CHECK(env.Registry.GetResourceCount() == 0);
                Access::SetHooks(env.Value, {});
                CHECK(LoadOne(env).Failure == Failure::None);
            }
        }
        for (int variant = 2; variant <= 4; ++variant)
        {
            Environment env(mode, variant);
            const auto result = LoadOne(env);
            CHECK(result.Failure == (variant == 2   ? Failure::ParseRejected
                                     : variant == 3 ? Failure::FormatRejected
                                                    : Failure::MetadataMismatch));
            CHECK(!result.Asset && env.Registry.GetResourceCount() == 0);
        }
        {
            Environment env(mode);
            auto missing = LoadOne(env, "Actors/Missing");
            CHECK(missing.Failure == Failure::ResolveRejected);
            CHECK(env.Value.LoadAsync("Actors/A", {}).Status == S::EmptyCallback);
            const char invalid[] = {'x', char(0xff)}, nul[] = {'A', 0, 'B'};
            auto cb = [](const Completion&) { CHECK(false); };
            CHECK(env.Value.LoadAsync(C::AnsiStringView(invalid, 2), cb).Status == S::InvalidPath);
            CHECK(env.Value.LoadAsync(C::AnsiStringView(nul, 3), cb).Status == S::InvalidPath);
            CHECK(env.Value.LoadAsync("C:/absolute", cb).Status == S::InvalidPath);
            CHECK(env.Value.LoadAsync("../outside", cb).Status == S::InvalidPath);
            T::JobSystemTestAccess::FailPush(env.Jobs, true);
            CHECK(env.Value.LoadAsync("Actors/A", cb).Status == S::SubmitException);
            T::JobSystemTestAccess::FailPush(env.Jobs, false);
            CHECK(Access::Counts(env.Value).Handoffs == 0 && env.Value.GetPendingCount() == 0);
            env.Jobs.DrainAcceptedFiniteTasks();
            env.Jobs.StopAcceptingTasks();
            CHECK(env.Value.LoadAsync("Actors/A", cb).Status == S::SubmitRejected);
            CHECK(Access::Counts(env.Value).Handoffs == 0 && env.Value.GetPendingCount() == 0);
        }
        {
            Core::SkeletalRuntimeLimits limits;
            limits.MaxRegistrySlots = 1;
            Environment env(mode, 0, limits);
            const auto result = LoadOne(env);
            CHECK(result.Failure == Failure::PublicationBudget && !result.Asset &&
                  env.Registry.GetResourceCount() == 0);
        }
        std::printf("SKELETAL_RUNTIME_CASE result=pass typed_failures_refusals_actual_submit\n");
    }
    struct CancelAt
    {
        Runtime* Value;
        Point At;
        bool Close;
    };
    void CancelFault(Point point, uint64_t id, void* context)
    {
        auto& x = *static_cast<CancelAt*>(context);
        if (x.At == point && (point == Point::AfterAssembly || point == Point::BeforeCommit))
        {
            CHECK(!Access::WeakPrepared(*x.Value).expired());
        }
        if (x.At == point)
        {
            if (x.Close)
            {
                x.Value->Close();
            }
            else
            {
                CHECK(x.Value->Cancel(id));
            }
        }
    }
    struct ObserveCancel
    {
        Runtime* Value;
        Observation At;
        bool Close;
    };
    void CancelObservation(Observation point, uint64_t id, void* context) noexcept
    {
        auto& x = *static_cast<ObserveCancel*>(context);
        if (point == x.At)
        {
            if (x.Close)
            {
                x.Value->Close();
            }
            else
            {
                CHECK(x.Value->Cancel(id));
            }
        }
    }
    void Cancellation(T::JobSystem::ExecutionMode mode)
    {
        for (const bool close : {false, true})
        {
            for (const auto point :
                 {Point::BeforeSubmit, Point::WorkerBeforeLoad, Point::WorkerAfterLoad, Point::BeforeAssembly,
                  Point::AfterAssembly, Point::BeforeCommit, Point::BeforeCallback})
            {
                Environment env(mode);
                CancelAt action{&env.Value, point, close};
                Access::SetHooks(env.Value, {CancelFault, nullptr, &action});
                Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
                CHECK(Access::WaitReady(env.Value, 1));
                const auto flushed = env.Value.FlushCompleted();
                CHECK(flushed.Callbacks == 0);
                CHECK(env.Registry.GetResourceCount() == (point == Point::BeforeCallback ? 6 : 0));
                if (point != Point::BeforeCallback)
                {
                    CHECK(Access::WeakPrepared(env.Value).expired());
                }
                env.Value.Close();
                CHECK(env.Value.Drain() == S::Drained);
            }
            for (const auto point : {Observation::AfterDetach, Observation::AfterCommit})
            {
                Environment env(mode);
                ObserveCancel action{&env.Value, point, close};
                // detachはgroup id無しなのでClose専用、Cancelは実commit後で確認する。
                if (!close && point == Observation::AfterDetach)
                {
                    continue;
                }
                Access::SetHooks(env.Value, {nullptr, CancelObservation, &action});
                Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
                CHECK(Access::WaitReady(env.Value, 1));
                CHECK(env.Value.FlushCompleted().Callbacks == 0);
                CHECK(env.Registry.GetResourceCount() == (point == Observation::AfterCommit ? 6 : 0));
            }
        }
        {
            Environment env(mode);
            int called = 0;
            const auto old = Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
            const auto weak = Access::WeakRequest(env.Value, old);
            CHECK(env.Value.Cancel(old));
            const auto fresh = Load(env.Value, "Actors/A",
                                    [&](const Completion& c)
                                    {
                                        CHECK(c.Failure == Failure::None);
                                        ++called;
                                    });
            CHECK(fresh != old);
            CHECK(Access::WaitReady(env.Value, 2));
            CHECK(env.Value.FlushCompleted().Groups == 2 && called == 1 && weak.expired());
            CHECK(env.Registry.GetResourceCount() == 6);
        }
        for (const bool close : {false, true})
        {
            Environment env(mode);
            uint64_t id = 0;
            int called = 0;
            id = Load(env.Value, "Actors/A",
                      [&](const Completion&)
                      {
                          ++called;
                          if (close)
                          {
                              env.Value.Close();
                              CHECK(env.Value.Drain() == S::Deferred);
                          }
                          else
                          {
                              CHECK(env.Value.Cancel(id));
                          }
                      });
            Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
            CHECK(Access::WaitReady(env.Value, 1));
            CHECK(env.Value.FlushCompleted().Callbacks == 1 && called == 1);
            CHECK(env.Registry.GetResourceCount() == 6);
            env.Value.Close();
            CHECK(env.Value.Drain() == S::Drained);
        }
        std::printf("SKELETAL_RUNTIME_CASE result=pass cancellation_publication_order_deferred\n");
    }
    struct BlockAt
    {
        Observation At;
        Gate Entered, Release, DrainEntered;
    };
    void BlockObservation(Observation point, uint64_t, void* context) noexcept
    {
        auto& x = *static_cast<BlockAt*>(context);
        if (point == x.At)
        {
            x.Entered.Signal();
            x.Release.Wait();
        }
        if (point == Observation::DrainWaiting)
        {
            x.DrainEntered.Signal();
        }
    }
    struct BlockWorker
    {
        Gate Entered, Release;
    };
    void BlockWorkerFault(Point point, uint64_t, void* context)
    {
        if (point == Point::WorkerBeforeLoad)
        {
            auto& x = *static_cast<BlockWorker*>(context);
            x.Entered.Signal();
            x.Release.Wait();
        }
    }
    struct PublicationRace
    {
        Gate Inside, Attempt, Release, Canceled;
    };
    void PublicationObservation(Observation point, uint64_t, void* context) noexcept
    {
        auto& race = *static_cast<PublicationRace*>(context);
        if (point == Observation::InsidePublicationCommit)
        {
            race.Inside.Signal();
            race.Release.Wait();
        }
        if (point == Observation::AfterCommit)
        {
            race.Canceled.Wait();
        }
    }
    void PublicationCancelRace(T::JobSystem::ExecutionMode mode)
    {
        for (const bool close : {false, true})
        {
            Environment env(mode);
            PublicationRace race;
            Access::SetHooks(env.Value, {nullptr, PublicationObservation, &race});
            CHECK(Access::TryStateLock(env.Value));
            const auto id = Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
            CHECK(Access::WaitReady(env.Value, 1));
            std::thread cancel(
                [&]()
                {
                    race.Inside.Wait();
                    CHECK(!Access::TryStateLock(env.Value));
                    race.Attempt.Signal();
                    if (close)
                    {
                        env.Value.Close();
                    }
                    else
                    {
                        CHECK(env.Value.Cancel(id));
                    }
                    CHECK(env.Registry.GetResourceCount() == 6 && env.Registry.GetCachedPathCount() == 1);
                    race.Canceled.Signal();
                });
            std::thread release(
                [&]()
                {
                    race.Attempt.Wait();
                    race.Release.Signal();
                });
            CHECK(env.Value.FlushCompleted().Callbacks == 0);
            cancel.join();
            release.join();
            CHECK(env.Registry.GetResourceCount() == 6 && Access::TryStateLock(env.Value));
        }
        std::printf("SKELETAL_RUNTIME_CASE result=pass commit_lock_cancel_close_order\n");
    }
    void HandoffAndClose(T::JobSystem::ExecutionMode mode)
    {
        {
            Environment env(mode);
            BlockAt block{Observation::BeforeHandoffAck};
            Access::SetHooks(env.Value, {nullptr, BlockObservation, &block});
            auto id = Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
            auto task = Access::PeekTask(env.Value, id);
            CHECK(task);
            block.Entered.Wait();
            task->Wait();
            CHECK(task->GetState() == T::Task::State::COMPLETED && Access::Counts(env.Value).Handoffs == 1);
            env.Value.Close();
            Gate attempted, finished;
            T::Atomic<bool> drained(false);
            std::thread waiter(
                [&]()
                {
                    attempted.Signal();
                    CHECK(env.Value.Drain() == S::Drained);
                    drained = true;
                    finished.Signal();
                });
            attempted.Wait();
            block.DrainEntered.Wait();
            CHECK(!drained.Load());
            block.Release.Signal();
            finished.Wait();
            waiter.join();
            CHECK(drained.Load() && Access::Counts(env.Value).Handoffs == 0 && env.Registry.GetResourceCount() == 0);
            task.reset();
        }
        {
            Environment env(mode);
            BlockWorker block;
            Access::SetHooks(env.Value, {BlockWorkerFault, nullptr, &block});
            const auto id = Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
            block.Entered.Wait();
            const auto weak = Access::WeakRequest(env.Value, id);
            env.Value.Close();
            CHECK(env.Value.LoadAsync("Actors/B", [](const Completion&) {}).Status == S::Closed);
            T::Atomic<bool> drained(false);
            std::thread waiter(
                [&]()
                {
                    CHECK(env.Value.Drain() == S::Drained);
                    drained = true;
                });
            CHECK(!drained.Load());
            block.Release.Signal();
            waiter.join();
            CHECK(weak.expired() && env.Registry.GetResourceCount() == 0);
        }
        {
            Environment env(mode);
            BlockAt block{Observation::AfterSubmit};
            Access::SetHooks(env.Value, {nullptr, BlockObservation, &block});
            // ownerはLoadの返答前で停止。別threadは実worker完了を観測してCloseする。
            std::thread closer(
                [&]()
                {
                    block.Entered.Wait();
                    env.Jobs.DrainAcceptedFiniteTasks();
                    CHECK(Access::Counts(env.Value).Handoffs == 0);
                    env.Value.Close();
                    block.Release.Signal();
                });
            const auto result = env.Value.LoadAsync("Actors/A", [](const Completion&) { CHECK(false); });
            closer.join();
            CHECK(result.Status == S::Accepted && result.RequestId != 0);
            CHECK(env.Value.Drain() == S::Drained);
            CHECK(env.Registry.GetResourceCount() == 0);
        }
        {
            Environment env(mode);
            BlockAt block{Observation::AfterSubmit};
            Access::SetHooks(env.Value, {nullptr, BlockObservation, &block});
            T::Atomic<int> calls(0);
            C::TSharedPtr<Core::SkeletalAssetResource> asset;
            std::thread complete(
                [&]()
                {
                    block.Entered.Wait();
                    env.Jobs.DrainAcceptedFiniteTasks();
                    const auto before = Access::Counts(env.Value);
                    CHECK(before.Handoffs == 0 && before.Ready == 0 && before.Accepted == 0);
                    CHECK(before.WorkerCalls == 1 && before.AssemblyCalls == 0 && before.CommitCalls == 0);
                    CHECK(calls.Load() == 0 && env.Registry.GetResourceCount() == 0);
                    block.Release.Signal();
                });
            const auto result = env.Value.LoadAsync("Actors/A",
                                                    [&](const Completion& value)
                                                    {
                                                        CHECK(value.Failure == Failure::None);
                                                        asset = value.Asset;
                                                        calls++;
                                                    });
            complete.join();
            CHECK(result.Status == S::Accepted && result.RequestId != 0);
            const auto ready = Access::Counts(env.Value);
            CHECK(ready.Ready == 1 && ready.Accepted == 1 && ready.Handoffs == 0 && calls.Load() == 0);
            CHECK(env.Registry.GetResourceCount() == 0);
            const auto flush = env.Value.FlushCompleted();
            const auto after = Access::Counts(env.Value);
            CHECK(flush.Groups == 1 && flush.Callbacks == 1 && calls.Load() == 1 && asset);
            CHECK(after.WorkerCalls == 1 && after.AssemblyCalls == 1 && after.CommitCalls == 1 && after.Accepted == 0);
            CHECK(env.Registry.GetResourceCount() == 6 && env.Registry.GetCachedPathCount() == 1);
            CHECK(env.Value.FlushCompleted().Callbacks == 0);
        }
        {
            Environment env(mode);
            BlockAt block{Observation::CallbackReserved};
            Access::SetHooks(env.Value, {nullptr, BlockObservation, &block});
            int calls = 0;
            const auto id = Load(env.Value, "Actors/A", [&](const Completion&) { ++calls; });
            Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
            CHECK(Access::WaitReady(env.Value, 1));
            std::thread cancel(
                [&]()
                {
                    block.Entered.Wait();
                    CHECK(env.Value.Cancel(id));
                    block.Release.Signal();
                });
            CHECK(env.Value.FlushCompleted().Callbacks == 1 && calls == 1);
            cancel.join();
        }
        std::printf("SKELETAL_RUNTIME_CASE result=pass terminal_ack_close_submit_reserved_callback\n");
    }
    void DomainsSnapshotsOwner(T::JobSystem::ExecutionMode mode)
    {
        Environment env(mode);
        Fixture otherFiles(1);
        const auto original = LoadOne(env);
        CHECK(original.Failure == Failure::None);
        const auto originalHandle =
            env.Registry.GetHandle<Core::SkeletalAssetResource>(original.Asset->GetResourceId());
        auto before = env.Registry.CreateResource<Core::SkeletonResource>("unregistered-before");
        Runtime other;
        CHECK(other.Bind(env.Registry, env.Jobs, otherFiles.Snapshot, T::Thread::GetCurrentThreadId()) == S::Success);
        auto after = env.Registry.CreateResource<Core::SkeletonResource>("unregistered-after");
        CHECK(after->GetResourceId() == before->GetResourceId() + 1);
        CHECK(Access::Counts(other).Domain != Access::Counts(env.Value).Domain);
        C::TSharedPtr<Core::SkeletalAssetResource> second;
        Load(other, "Actors/A", [&](const Completion& c) { second = c.Asset; });
        CHECK(Access::WaitReady(other, 1));
        CHECK(other.FlushCompleted().Callbacks == 1);
        CHECK(second != original.Asset && second->GetMesh()->GetVertices()[0].Position.X == 2.f);
        CHECK(env.Value.SetSnapshot(env.Files.Snapshot) == S::Unchanged);
        CHECK(env.Value.SetSnapshot(otherFiles.Snapshot) == S::Success);
        auto changed = LoadOne(env);
        CHECK(changed.Asset != original.Asset && env.Registry.Resolve(originalHandle) == original.Asset);
        const auto id = Load(env.Value, "Actors/A", [](const Completion&) {});
        CHECK(env.Value.SetSnapshot(env.Files.Snapshot) == S::Busy);
        std::thread wrong(
            [&]()
            {
                CHECK(env.Value.LoadAsync("Actors/A", [](const Completion&) {}).Status == S::WrongOwner);
                CHECK(env.Value.FlushCompleted().Status == S::WrongOwner);
                CHECK(env.Value.SetSnapshot(env.Files.Snapshot) == S::WrongOwner);
            });
        wrong.join();
        CHECK(env.Value.Cancel(id));
        CHECK(env.Value.FlushCompleted().Callbacks == 0);
        Access::SetGeneration(env.Value, UINT64_MAX);
        CHECK(env.Value.SetSnapshot(env.Files.Snapshot) == S::GenerationExhausted);
        Access::SetNextId(env.Value, UINT64_MAX);
        CHECK(env.Value.LoadAsync("Actors/B", [](const Completion&) {}).Status == S::IdExhausted);
        other.Close();
        CHECK(other.Drain() == S::Drained);
        // idle旧runtimeのsessionだけを入れ替え、新sessionへ古いdomainを公開しない。
        env.Registry.Shutdown();
        CHECK(env.Registry.Initialize());
        Access::SetNextId(env.Value, 10);
        CHECK(env.Value.LoadAsync("Actors/A", [](const Completion&) {}).Status == S::RegistrySessionChanged);
        CHECK(env.Registry.GetResourceCount() == 0);
        std::printf("SKELETAL_RUNTIME_CASE result=pass domain_snapshot_owner_session\n");
    }
    struct CleanupSignals
    {
        Gate Dropping, Release, SecondWaiting;
    };
    struct CaptureLifetime
    {
        CleanupSignals* Signals;
        ~CaptureLifetime()
        {
            Signals->Dropping.Signal();
            Signals->Release.Wait();
        }
    };
    void CleanupObservation(Observation point, uint64_t, void* context) noexcept
    {
        if (point == Observation::DrainWaiting)
        {
            static_cast<CleanupSignals*>(context)->SecondWaiting.Signal();
        }
    }
    void MultipleDrainAndCaptureDestruction(T::JobSystem::ExecutionMode mode)
    {
        Environment env(mode);
        const auto saved = LoadOne(env);
        CHECK(saved.Asset);
        CleanupSignals signals;
        Access::SetHooks(env.Value, {nullptr, CleanupObservation, &signals});
        auto lifetime = C::MakeShared<CaptureLifetime>();
        lifetime->Signals = &signals;
        const auto id = Load(env.Value, "Actors/A", [lifetime](const Completion&) { CHECK(false); });
        lifetime.reset();
        env.Value.Close();
        T::Atomic<bool> first(false), second(false);
        std::thread a(
            [&]()
            {
                CHECK(env.Value.Drain() == S::Drained);
                first = true;
            });
        signals.Dropping.Wait();
        // capture destructor停止中にもState mutexを取れる。cleanupへの新しいpinを作らない。
        env.Value.Close();
        CHECK(!env.Value.Cancel(id));
        std::thread b(
            [&]()
            {
                CHECK(env.Value.Drain() == S::Drained);
                second = true;
            });
        signals.SecondWaiting.Wait();
        CHECK(!first.Load() && !second.Load());
        signals.Release.Signal();
        a.join();
        b.join();
        CHECK(first.Load() && second.Load() && saved.Asset->IsLoaded());
        std::printf("SKELETAL_RUNTIME_CASE result=pass multiple_drain_capture_destruction_outside_lock\n");
    }
    void LimitsAndWeakOwnership(T::JobSystem::ExecutionMode mode)
    {
        C::TWeakPtr<void> stateWeak;
        C::TWeakPtr<const A::AssetSystem> snapshotWeak;
        {
            Core::SkeletalRuntimeLimits limits;
            limits.MaxPendingGroups = 1;
            limits.MaxSubscribersPerGroup = 2;
            Environment env(mode, 0, limits);
            stateWeak = Access::WeakState(env.Value);
            snapshotWeak = env.Files.Snapshot;
            env.Files.Snapshot.reset();
            const auto id = Load(env.Value, "Actors/A", [](const Completion&) {});
            CHECK(Load(env.Value, "Actors/A", [](const Completion&) {}) == id);
            CHECK(env.Value.LoadAsync("Actors/A", [](const Completion&) {}).Status == S::LimitExceeded);
            CHECK(env.Value.LoadAsync("Actors/B", [](const Completion&) {}).Status == S::LimitExceeded);
            CHECK(Access::WaitReady(env.Value, 1));
            const auto weak = Access::WeakRequest(env.Value, id);
            CHECK(env.Value.FlushCompleted().Callbacks == 2 && weak.expired());
            env.Value.Close();
            CHECK(env.Value.Drain() == S::Drained);
            CHECK(snapshotWeak.expired());
            CHECK(!stateWeak.expired());
        }
        CHECK(stateWeak.expired());
        {
            Environment env(mode);
            Runtime unbound;
            CHECK(unbound.LoadAsync("Actors/A", [](const Completion&) {}).Status == S::NotBound);
            CHECK(unbound.Bind(env.Registry, env.Jobs, env.Files.Snapshot, {}) == S::WrongOwner);
            CHECK(env.Value.Bind(env.Registry, env.Jobs, env.Files.Snapshot, T::Thread::GetCurrentThreadId()) ==
                  S::AlreadyBound);
            D::SetSkeletalCacheDomainCounterForTest(env.Registry, UINT64_MAX);
            CHECK(unbound.Bind(env.Registry, env.Jobs, env.Files.Snapshot, T::Thread::GetCurrentThreadId()) ==
                  S::LimitExceeded);
            CHECK(Access::Counts(unbound).Domain == 0 && env.Registry.GetResourceCount() == 0);
            D::SetSkeletalCacheDomainCounterForTest(env.Registry, 2);
            CHECK(unbound.Bind(env.Registry, env.Jobs, env.Files.Snapshot, T::Thread::GetCurrentThreadId()) ==
                  S::Success);
            CHECK(Access::Counts(unbound).Domain == 2);
            unbound.Close();
            CHECK(unbound.Drain() == S::Drained);
            Runtime replacement;
            CHECK(replacement.Bind(env.Registry, env.Jobs, env.Files.Snapshot, T::Thread::GetCurrentThreadId()) ==
                  S::Success);
            CHECK(Access::Counts(replacement).Domain == 3);
        }
        {
            Core::SkeletalRuntimeLimits limits;
            limits.MaxPathBytes = 3;
            Environment env(mode, 0, limits);
            CHECK(env.Value.LoadAsync("Actors/A", [](const Completion&) {}).Status == S::InvalidPath);
        }
        {
            Core::SkeletalRuntimeLimits limits;
            limits.MaxKeyBytes = 8;
            Environment env(mode, 0, limits);
            CHECK(env.Value.LoadAsync("A", [](const Completion&) {}).Status == S::InvalidPath);
        }
        {
            Environment env(mode);
            const auto id = Load(env.Value, "Actors/A", [](const Completion&) { CHECK(false); });
            CHECK(Access::WaitReady(env.Value, 1));
            // 索引Identityが一致しても保持する完全keyが異なれば合流しない。
            Access::AlterStoredKey(env.Value, id, F::CoreText("different-exact-key"));
            const auto refused = env.Value.LoadAsync("Actors/A", [](const Completion&) { CHECK(false); });
            CHECK(refused.Status == S::CacheRejected && refused.RequestId == 0);
            CHECK(env.Value.Cancel(id));
            CHECK(env.Value.FlushCompleted().Callbacks == 0);
            CHECK(env.Registry.GetResourceCount() == 0 && env.Value.GetPendingCount() == 0);
            CHECK(LoadOne(env).Failure == Failure::None);
        }
        std::printf("SKELETAL_RUNTIME_CASE result=pass limits_domain_exhaustion_weak_ownership\n");
    }
    bool Run(const char* name, T::JobSystem::ExecutionMode mode)
    {
        if (std::strcmp(name, "sharing") == 0)
        {
            SharingAndLifetime(mode);
        }
        else if (std::strcmp(name, "batch") == 0)
        {
            ReentrantBatch(mode);
        }
        else if (std::strcmp(name, "failures") == 0)
        {
            Failures(mode);
        }
        else if (std::strcmp(name, "cancel") == 0)
        {
            Cancellation(mode);
        }
        else if (std::strcmp(name, "handoff") == 0)
        {
            HandoffAndClose(mode);
            PublicationCancelRace(mode);
        }
        else if (std::strcmp(name, "domains") == 0)
        {
            DomainsSnapshotsOwner(mode);
        }
        else if (std::strcmp(name, "limits") == 0)
        {
            LimitsAndWeakOwnership(mode);
            MultipleDrainAndCaptureDestruction(mode);
        }
        else
        {
            return false;
        }
        return true;
    }
    void Child(const char* executable, const char* name, const char* mode)
    {
        const char* args[] = {executable, "--runtime-child", name, mode, nullptr};
        const intptr_t child = _spawnv(_P_NOWAIT, executable, args);
        CHECK(child != -1);
        const auto handle = reinterpret_cast<HANDLE>(child);
        const auto wait = WaitForSingleObject(handle, 60000);
        if (wait != WAIT_OBJECT_0)
        {
            TerminateProcess(handle, 1);
            WaitForSingleObject(handle, INFINITE);
        }
        DWORD code = 1;
        const bool read = GetExitCodeProcess(handle, &code) != 0;
        CloseHandle(handle);
        CHECK(wait == WAIT_OBJECT_0 && read && code == 0);
    }
} // namespace
int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--runtime-child") == 0)
    {
        return Run(argv[2], std::strcmp(argv[3], "simple") == 0 ? T::JobSystem::EXECUTION_SIMPLE
                                                                : T::JobSystem::EXECUTION_WORK_STEALING)
                   ? 0
                   : 1;
    }
    for (const char* mode : {"simple", "steal"})
    {
        for (const char* name : {"sharing", "batch", "failures", "cancel", "handoff", "domains", "limits"})
        {
            Child(argv[0], name, mode);
        }
    }
    std::printf(
        "SKELETAL_EVENT_RUNTIME result=pass actual_worker_ready_owner_publication_delegate_dedup_cache_cancel_close_drain_domain_snapshot_gc_no_product_gpu\n");
    return 0;
}
