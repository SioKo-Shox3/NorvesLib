// 分離modeも実GR83 runtimeの同ready/owner/取消/Drainを使うことを反証する。
#include "RigSplitPublicationFixture.h"
#include "Resource/SkeletalAssetRuntimeTestAccess.h"
#include "Resource/SkinnedMaterialBuilder.h"
#include "Rendering/RenderResources.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "Tools/AssetCook/AssetCookOutput.h"
#include "Tools/AssetCook/TextureCooker.h"
#include "Animation/SkeletalAssetRuntime.h"
#include "Engine/SkeletalAssetSession.h"
#include "Application/ApplicationHandlerBase.h"
#include "Thread/JobSystem.h"
#include "Thread/ConditionVariable.h"
#include "Thread/Atomic.h"
#include <thread>
#include <new>
#include <process.h>
#include <windows.h>
namespace F = NorvesLib::Tests::RigSplitPublicationFixture;
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace R = Core::ResourceIO;
namespace D = R::Detail;
namespace T = NorvesLib::Thread;
using S = Core::SkeletalRuntimeStatus;
using Failure = Core::SkeletalRuntimeFailure;
using Runtime = Core::SkeletalAssetRuntime;
using Completion = Core::SkeletalAssetCompletion;
using Access = D::SkeletalRuntimeTestAccess;
using Point = D::SkeletalRuntimePoint;
using Observation = D::SkeletalRuntimeObservation;
namespace
{
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
            RIG_CHECK(Condition.WaitFor(Mutex, std::chrono::seconds(10), [&]() { return Signaled; }));
        }
    };
    struct Environment
    {
        F::Fixture Files;
        Core::ResourceRegistry Registry;
        T::JobSystem& Jobs = T::JobSystem::Get();
        Runtime Value;
        Environment(T::JobSystem::ExecutionMode mode, bool mismatch = false,
                    const Core::SkeletalRuntimeLimits& limits = {})
            : Files(mismatch)
        {
            RIG_CHECK(Registry.Initialize());
            Jobs.Initialize(1, mode);
            RIG_CHECK(Value.Bind(Registry, Jobs, Files.Snapshot, T::Thread::GetCurrentThreadId(), limits) ==
                      S::Success);
        }
        ~Environment()
        {
            Value.Close();
            RIG_CHECK(Value.Drain() == S::Drained);
            Jobs.Shutdown();
            Registry.Shutdown();
        }
    };
    uint64_t Load(Runtime& runtime, const Core::Skeletal::RigSplitRequest& request, Runtime::Callback callback)
    {
        const auto r = runtime.LoadRigSplitAsync(request, std::move(callback));
        RIG_CHECK(r.Status == S::Accepted && r.RequestId);
        return r.RequestId;
    }
    Completion One(Environment& env, const Core::Skeletal::RigSplitRequest& request)
    {
        Completion result;
        unsigned calls = 0;
        Load(env.Value, request,
             [&](const Completion& c)
             {
                 result = c;
                 ++calls;
             });
        RIG_CHECK(calls == 0 && Access::WaitReady(env.Value, 1));
        const auto f = env.Value.FlushCompleted();
        RIG_CHECK(f.Status == S::Success && f.Groups == 1 && f.Callbacks == 1 && calls == 1);
        return result;
    }
    void Sharing(T::JobSystem::ExecutionMode mode)
    {
        Environment env(mode);
        Completion first, second;
        unsigned calls = 0;
        const auto id = Load(env.Value, env.Files.Request,
                             [&](const Completion& c)
                             {
                                 first = c;
                                 ++calls;
                             });
        RIG_CHECK(Load(env.Value, env.Files.Request,
                       [&](const Completion& c)
                       {
                           second = c;
                           ++calls;
                       }) == id);
        const auto weak = Access::WeakRequest(env.Value, id);
        RIG_CHECK(Access::WaitReady(env.Value, 1));
        RIG_CHECK(env.Registry.GetResourceCount() == 0 && calls == 0 && Access::Counts(env.Value).WorkerCalls == 1);
        RIG_CHECK(env.Value.FlushCompleted().Callbacks == 2 && calls == 2 && weak.expired());
        RIG_CHECK(first.Failure == Failure::None && first.Asset == second.Asset && first.SplitDiagnostics);
        RIG_CHECK(first.SplitDiagnostics->Load.BindingReport.Banks.size() == 3 &&
                  first.SplitDiagnostics->Assembly.Status == Core::Skeletal::RigV1Status::Success);
        RIG_CHECK(env.Registry.GetResourceCount() == 6 && env.Registry.GetCachedPathCount() == 1);
        F::Pose(first.Asset);
        const auto hit = One(env, env.Files.Request);
        RIG_CHECK(hit.bCacheHit && hit.Asset == first.Asset && hit.SplitDiagnostics == first.SplitDiagnostics);
        RIG_CHECK(Access::Counts(env.Value).AssemblyCalls == 1 && Access::Counts(env.Value).CommitCalls == 1);
        auto reordered = env.Files.Request;
        std::swap(reordered.BankPaths[0], reordered.BankPaths[1]);
        const auto reverse = One(env, reordered);
        RIG_CHECK(reverse.Failure == Failure::None && reverse.Asset != first.Asset);
        RIG_CHECK(reverse.Asset->GetClip(0)->GetClip().Name == C::String(_T("Walk")) &&
                  reverse.Asset->GetClip(1)->GetClip().Name == C::String(_T("Wave")));
        auto old = first.Asset->GetClip(0)->GetClip();
        auto changed = old;
        changed.Channels[0].Samples[0].Value.Y += 2;
        first.Asset->GetClip(0)->SetClip(std::move(changed));
        const auto reject = env.Value.LoadRigSplitAsync(env.Files.Request, [](const Completion&) { RIG_CHECK(false); });
        RIG_CHECK(reject.Status == S::CacheRejected && reject.RequestId == 0);
        first.Asset->GetClip(0)->SetClip(std::move(old));
        std::printf("SPLIT_RUNTIME_CASE result=pass real_worker_join_deferred_hit_order_full_clip_guard\n");
    }
    void Policy(T::JobSystem::ExecutionMode mode)
    {
        {
            Environment env(mode, true);
            auto allow = env.Files.Request;
            allow.Policy.bAllowRestMismatch = true;
            Completion accepted, strict;
            const auto a = Load(env.Value, allow, [&](const Completion& c) { accepted = c; });
            const auto b = Load(env.Value, env.Files.Request, [&](const Completion& c) { strict = c; });
            RIG_CHECK(a != b);
            RIG_CHECK(Access::WaitReady(env.Value, 2));
            RIG_CHECK(env.Value.FlushCompleted().Callbacks == 2);
            RIG_CHECK(accepted.Failure == Failure::None &&
                      accepted.SplitDiagnostics->Load.BindingReport.Banks[0].bOverrideUsed);
            RIG_CHECK(strict.Failure == Failure::BindingRejected && !strict.Asset && !strict.bCacheHit &&
                      strict.SplitDiagnostics);
            RIG_CHECK(strict.SplitDiagnostics->Load.BindingReport.Status == Core::Skeletal::RigV1Status::RestMismatch &&
                      strict.SplitDiagnostics->Load.BindingReport.FailedBank == 0);
            RIG_CHECK(env.Registry.GetResourceCount() == 6);
            const auto retry = One(env, env.Files.Request);
            RIG_CHECK(retry.Failure == Failure::BindingRejected && env.Registry.GetResourceCount() == 6);
            const auto hit = One(env, allow);
            RIG_CHECK(hit.bCacheHit && hit.Asset == accepted.Asset &&
                      hit.SplitDiagnostics->Load.BindingReport.Banks[0].bOverrideUsed);
            auto strictFirst = env.Files.Request;
            strictFirst.BankPaths.pop_back();
            RIG_CHECK(One(env, strictFirst).Failure == Failure::BindingRejected);
            strictFirst.Policy.bAllowRestMismatch = true;
            RIG_CHECK(One(env, strictFirst).Failure == Failure::None);
        }
        {
            Environment env(mode);
            const auto base = One(env, env.Files.Request);
            auto allow = env.Files.Request;
            allow.Policy.bAllowRestMismatch = true;
            const auto noMismatch = One(env, allow);
            RIG_CHECK(noMismatch.Failure == Failure::None && !noMismatch.bCacheHit && noMismatch.Asset != base.Asset);
            RIG_CHECK(!noMismatch.SplitDiagnostics->Load.BindingReport.Banks[0].bOverrideUsed);
            auto tighter = env.Files.Request;
            tighter.MaxPackageBytes = 1;
            const auto refused = One(env, tighter);
            RIG_CHECK(refused.Failure == Failure::ResolveRejected && !refused.bCacheHit);
            RIG_CHECK(refused.SplitDiagnostics->Load.PackageReadStatus == Core::Asset::AssetReadStatus::SizeTooLarge);
            tighter = env.Files.Request;
            tighter.Policy.Tolerance.TranslationMeters *= 2;
            const auto tolerance = One(env, tighter);
            RIG_CHECK(tolerance.Failure == Failure::None && !tolerance.bCacheHit && tolerance.Asset != base.Asset);
        }
        std::printf(
            "SPLIT_RUNTIME_CASE result=pass override_strict_both_orders_no_mismatch_policy_tolerance_budget_isolation\n");
    }
    void Mixed(T::JobSystem::ExecutionMode mode)
    {
        Environment env(mode);
        unsigned split = 0, legacy = 0;
        Completion retained;
        const auto a = Load(env.Value, env.Files.Request,
                            [&](const Completion& c)
                            {
                                RIG_CHECK(c.Failure == Failure::None);
                                retained = c;
                                ++split;
                            });
        const auto b = env.Value.LoadAsync("Actors/Legacy",
                                           [&](const Completion& c)
                                           {
                                               RIG_CHECK(c.Failure == Failure::None && !c.SplitDiagnostics);
                                               ++legacy;
                                           });
        RIG_CHECK(b.Status == S::Accepted && a != b.RequestId && Access::WaitReady(env.Value, 2));
        RIG_CHECK(env.Value.FlushCompleted(1).Callbacks == 1 && split + legacy == 1 &&
                  env.Value.GetPendingCount() == 1);
        RIG_CHECK(env.Value.FlushCompleted(1).Callbacks == 1 && split == 1 && legacy == 1);
        unsigned inside = 0;
        Load(env.Value, env.Files.Request,
             [&](const Completion& c)
             {
                 RIG_CHECK(c.bCacheHit);
                 ++inside;
                 RIG_CHECK(env.Value.FlushCompleted().Status == S::Reentrant);
                 Load(env.Value, env.Files.Request,
                      [&](const Completion& next)
                      {
                          RIG_CHECK(next.Asset == retained.Asset);
                          ++inside;
                      });
             });
        RIG_CHECK(env.Value.FlushCompleted().Callbacks == 1 && inside == 1);
        RIG_CHECK(env.Value.FlushCompleted().Callbacks == 1 && inside == 2);
        RIG_CHECK(env.Value.SetSnapshot(env.Files.Snapshot) == S::Unchanged);
        auto newSnapshot = env.Files.MakeSnapshot(env.Files.Json);
        RIG_CHECK(env.Value.SetSnapshot(newSnapshot) == S::Success);
        const auto newer = One(env, env.Files.Request);
        RIG_CHECK(newer.Failure == Failure::None && !newer.bCacheHit && newer.Asset != retained.Asset);
        F::Pose(retained.Asset);
        F::Pose(newer.Asset);
        const auto counts = Access::Counts(env.Value);
        RIG_CHECK(counts.Generation == 2);
        Access::SetGeneration(env.Value, UINT64_MAX);
        RIG_CHECK(env.Value.SetSnapshot(env.Files.Snapshot) == S::GenerationExhausted);
        std::thread wrong(
            [&]()
            {
                RIG_CHECK(env.Value.LoadRigSplitAsync(env.Files.Request, [](const Completion&) {}).Status ==
                          S::WrongOwner);
            });
        wrong.join();
        std::printf("SPLIT_RUNTIME_CASE result=pass shared_legacy_budget_reentry_snapshot_generation_wrong_owner\n");
    }
    void MutatedReentry(T::JobSystem::ExecutionMode mode)
    {
        for (bool eraseReceipt : {false, true})
        {
            Environment env(mode);
            unsigned calls = 0, rejectedCalls = 0, nextCalls = 0;
            uint64_t firstId = 0;
            firstId =
                Load(env.Value, env.Files.Request,
                     [&](const Completion& completed)
                     {
                         RIG_CHECK(completed.Failure == Failure::None);
                         ++calls;
                         const auto savedReceipt = R::RigSplitAssetAccess::Get(*completed.Asset);
                         const auto savedClip = completed.Asset->GetClip(0)->GetClip();
                         if (eraseReceipt)
                         {
                             C::VariableArray<C::TSharedPtr<Core::AnimationClipResource>> clips;
                             for (size_t i = 0; i < completed.Asset->GetClipCount(); ++i)
                             {
                                 clips.push_back(completed.Asset->GetClip(i));
                             }
                             completed.Asset->SetClipResources(completed.Asset->GetMesh(),
                                                               completed.Asset->GetSkeleton(), clips);
                             RIG_CHECK(!R::RigSplitAssetAccess::Get(*completed.Asset));
                         }
                         else
                         {
                             auto changed = savedClip;
                             changed.Channels[0].Samples[0].Value.Y += 1;
                             completed.Asset->GetClip(0)->SetClip(std::move(changed));
                             RIG_CHECK(completed.Asset->GetClip(0)->IsLoaded());
                         }
                         const auto refusal = env.Value.LoadRigSplitAsync(env.Files.Request,
                                                                          [&](const Completion&) { ++rejectedCalls; });
                         RIG_CHECK(refusal.Status == S::CacheRejected && refusal.RequestId == 0 && rejectedCalls == 0);
                         // 元の検証済み状態を復元し、有効な再入の次Flush配送も同じgroupで検査する。
                         if (eraseReceipt)
                         {
                             R::RigSplitAssetAccess::Attach(*completed.Asset, savedReceipt);
                         }
                         else
                         {
                             completed.Asset->GetClip(0)->SetClip(Core::Skeletal::SkeletalAnimationClip(savedClip));
                         }
                         RIG_CHECK(Load(env.Value, env.Files.Request,
                                        [&](const Completion& next)
                                        {
                                            RIG_CHECK(next.Failure == Failure::None);
                                            ++nextCalls;
                                        }) == firstId);
                     });
            RIG_CHECK(Load(env.Value, env.Files.Request,
                           [&](const Completion& c)
                           {
                               RIG_CHECK(c.Failure == Failure::None);
                               ++calls;
                           }) == firstId);
            RIG_CHECK(Access::WaitReady(env.Value, 1));
            RIG_CHECK(env.Value.FlushCompleted().Callbacks == 2 && calls == 2 && rejectedCalls == 0 && nextCalls == 0);
            RIG_CHECK(env.Value.FlushCompleted().Callbacks == 1 && nextCalls == 1 && rejectedCalls == 0);
            const auto counts = Access::Counts(env.Value);
            RIG_CHECK(counts.WorkerCalls == 1 && counts.AssemblyCalls == 1 && counts.CommitCalls == 1 &&
                      env.Registry.GetResourceCount() == 6 && env.Registry.GetCachedPathCount() == 1);
        }
        std::printf(
            "SPLIT_RUNTIME_CASE result=pass finalized_reentry_changed_clip_erased_receipt_rejected_existing_subscribers_preserved\n");
    }
    void Failures(T::JobSystem::ExecutionMode mode)
    {
        Environment env(mode);
        unsigned calls = 0;
        RIG_CHECK(env.Value.LoadRigSplitAsync(env.Files.Request, {}).Status == S::EmptyCallback);
        auto bad = env.Files.Request;
        bad.BankPaths.push_back(bad.BankPaths[0]);
        RIG_CHECK(env.Value.LoadRigSplitAsync(bad, [](const Completion&) { RIG_CHECK(false); }).Status ==
                  S::InvalidPath);
        auto absent = env.Files.Request;
        absent.BankPaths[1] = "Animations/Missing";
        const auto missing = One(env, absent);
        RIG_CHECK(missing.Failure == Failure::ResolveRejected &&
                  missing.SplitDiagnostics->Load.LogicalPath == absent.BankPaths[1]);
        struct Fault
        {
            Point Target;
            bool Called = false;
            bool Nonstandard = false;
        };
        const auto inject = +[](Point point, uint64_t, void* context)
        {
            auto& f = *static_cast<Fault*>(context);
            if (point == f.Target)
            {
                f.Called = true;
                if (f.Nonstandard)
                {
                    throw 73;
                }
                throw std::bad_alloc();
            }
        };
        for (Point point : {Point::Admission, Point::BeforeHandler, Point::AfterHandler, Point::BetweenIndexes,
                            Point::BeforeSubmit, Point::WorkerBeforeLoad, Point::WorkerAfterLoad, Point::BeforeAssembly,
                            Point::AfterAssembly, Point::BeforeCommit})
        {
            Fault fault{point};
            Access::SetHooks(env.Value, {inject, nullptr, &fault});
            Completion done;
            const auto request = env.Value.LoadRigSplitAsync(env.Files.Request,
                                                             [&](const Completion& c)
                                                             {
                                                                 done = c;
                                                                 ++calls;
                                                             });
            if (request.Status == S::Accepted)
            {
                RIG_CHECK(Access::WaitReady(env.Value, 1));
                RIG_CHECK(env.Value.FlushCompleted().Callbacks == 1);
                RIG_CHECK(done.Failure == ((point == Point::WorkerBeforeLoad || point == Point::WorkerAfterLoad)
                                               ? Failure::WorkerException
                                               : Failure::OwnerException));
            }
            else
            {
                RIG_CHECK(request.RequestId == 0);
            }
            RIG_CHECK(fault.Called && env.Registry.GetResourceCount() == 0);
            Access::SetHooks(env.Value, {});
        }
        Fault foreign{Point::WorkerAfterLoad, false, true};
        Access::SetHooks(env.Value, {inject, nullptr, &foreign});
        RIG_CHECK(One(env, env.Files.Request).Failure == Failure::WorkerException && foreign.Called);
        Access::SetHooks(env.Value, {});
        const auto success = One(env, env.Files.Request);
        RIG_CHECK(success.Failure == Failure::None);
        Load(env.Value, env.Files.Request, [&](const Completion&) { throw 9; });
        RIG_CHECK(env.Value.FlushCompleted().CallbackExceptions == 1);
        Load(env.Value, env.Files.Request,
             [&](const Completion&)
             {
                 env.Value.Close();
                 RIG_CHECK(env.Value.Drain() == S::Deferred);
             });
        RIG_CHECK(env.Value.FlushCompleted().Callbacks == 1 && env.Value.Drain() == S::Drained);
        std::printf(
            "SPLIT_RUNTIME_CASE result=pass typed_missing_preparation_worker_owner_faults_retry_callback_deferred_drain\n");
    }
    void Cancellation(T::JobSystem::ExecutionMode mode)
    {
        for (Point point : {Point::WorkerBeforeLoad, Point::WorkerAfterLoad})
        {
            Environment env(mode);
            struct State
            {
                Point Target;
                Gate Enter, Release;
            } state{point};
            const auto hook = +[](Point p, uint64_t, void* context)
            {
                auto& s = *static_cast<State*>(context);
                if (p == s.Target)
                {
                    s.Enter.Signal();
                    s.Release.Wait();
                }
            };
            Access::SetHooks(env.Value, {hook, nullptr, &state});
            const auto id = Load(env.Value, env.Files.Request, [](const Completion&) { RIG_CHECK(false); });
            state.Enter.Wait();
            RIG_CHECK(env.Value.Cancel(id));
            state.Release.Signal();
            RIG_CHECK(Access::WaitReady(env.Value, 1));
            RIG_CHECK(env.Value.FlushCompleted().Callbacks == 0 && env.Registry.GetResourceCount() == 0);
            Access::SetHooks(env.Value, {});
        }
        for (bool close : {false, true})
        {
            for (Point point :
                 {Point::BeforeAssembly, Point::AfterAssembly, Point::BeforeCommit, Point::BeforeCallback})
            {
                Environment env(mode);
                struct State
                {
                    Runtime* Value;
                    Point Target;
                    bool Close, Called = false;
                } state{&env.Value, point, close};
                const auto hook = +[](Point p, uint64_t id, void* context)
                {
                    auto& s = *static_cast<State*>(context);
                    if (p == s.Target)
                    {
                        s.Called = true;
                        if (s.Close)
                        {
                            s.Value->Close();
                        }
                        else
                        {
                            RIG_CHECK(s.Value->Cancel(id));
                        }
                    }
                };
                Access::SetHooks(env.Value, {hook, nullptr, &state});
                Load(env.Value, env.Files.Request, [](const Completion&) { RIG_CHECK(false); });
                RIG_CHECK(Access::WaitReady(env.Value, 1));
                RIG_CHECK(env.Value.FlushCompleted().Callbacks == 0 && state.Called);
                RIG_CHECK(env.Registry.GetResourceCount() == (point == Point::BeforeCallback ? 6 : 0));
                Access::SetHooks(env.Value, {});
            }
        }
        std::printf(
            "SPLIT_RUNTIME_CASE result=pass cancel_close_worker_ready_assembly_precommit_notification_windows\n");
    }
    void Handoff(T::JobSystem::ExecutionMode mode)
    {
        Environment env(mode);
        struct State
        {
            Runtime* Value;
            Gate Enter, Release, DrainWaiting;
            bool Inside = false, Outside = false;
        } state{&env.Value};
        const auto observe = +[](Observation p, uint64_t, void* context) noexcept
        {
            auto& s = *static_cast<State*>(context);
            if (p == Observation::InsidePublicationCommit)
            {
                std::thread t([&]() { s.Inside = !Access::TryStateLock(*s.Value); });
                t.join();
            }
            if (p == Observation::AfterCommit)
            {
                std::thread t([&]() { s.Outside = Access::TryStateLock(*s.Value); });
                t.join();
            }
        };
        Access::SetHooks(env.Value, {nullptr, observe, &state});
        RIG_CHECK(One(env, env.Files.Request).Failure == Failure::None);
        RIG_CHECK(state.Inside && state.Outside);
        Access::SetHooks(env.Value, {});
        auto other = env.Files.Request;
        other.MeshPath = "Models/Mesh2.nvskel";
        const auto pause = +[](Observation p, uint64_t, void* context) noexcept
        {
            auto& s = *static_cast<State*>(context);
            if (p == Observation::BeforeHandoffAck)
            {
                s.Enter.Signal();
                s.Release.Wait();
            }
            if (p == Observation::DrainWaiting)
            {
                s.DrainWaiting.Signal();
            }
        };
        Access::SetHooks(env.Value, {nullptr, pause, &state});
        Load(env.Value, other, [](const Completion&) { RIG_CHECK(false); });
        state.Enter.Wait();
        RIG_CHECK(Access::Counts(env.Value).Handoffs == 1);
        env.Value.Close();
        T::Atomic<bool> drained{false};
        std::thread waiter(
            [&]()
            {
                RIG_CHECK(env.Value.Drain() == S::Drained);
                drained.Store(true);
            });
        state.DrainWaiting.Wait();
        RIG_CHECK(!drained.Load());
        state.Release.Signal();
        waiter.join();
        RIG_CHECK(drained.Load());
        Access::SetHooks(env.Value, {});
        std::printf("SPLIT_RUNTIME_CASE result=pass state_registry_commit_gate_terminal_handoff_ack_close_drain\n");
    }
    struct Handler : Core::Application::ApplicationHandlerBase
    {
        C::TSharedPtr<Core::SkeletalAssetResource> Event;
        bool Advance = false;
        unsigned Updates = 0, Consumed = 0;
        void OnUpdate(float) override
        {
            ++Updates;
            if (Advance && Event)
            {
                F::Pose(Event);
                Event.reset();
                ++Consumed;
            }
        }
        bool ShouldAdvanceSimulation() const override
        {
            return Advance;
        }
        bool HasPendingAssetConsumers() const override
        {
            return bool(Event);
        }
    };
    void Session(T::JobSystem::ExecutionMode mode)
    {
        F::Fixture files;
        auto& jobs = T::JobSystem::Get();
        jobs.Initialize(1, mode);
        for (bool borrowed : {false, true})
        {
            Core::ResourceRegistry registry;
            if (borrowed)
            {
                RIG_CHECK(registry.Initialize());
            }
            Core::SkeletalAssetSession session;
            RIG_CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Success &&
                      session.OwnsRegistry() != borrowed);
            RIG_CHECK(session.BindSnapshot(files.Snapshot) == S::Success);
            Handler handler;
            Load(*session.GetRuntime(), files.Request,
                 [&](const Completion& c)
                 {
                     RIG_CHECK(c.Failure == Failure::None);
                     handler.Event = c.Asset;
                 });
            RIG_CHECK(Access::WaitReady(*session.GetRuntime(), 1));
            RIG_CHECK(Core::TickSkeletalOwnerAssetsAndHandler(session, &handler, 0).Callbacks == 1);
            RIG_CHECK(handler.Updates == 1 && handler.Consumed == 0 &&
                      Core::HasPendingSkeletalConsumers(session, &handler));
            handler.Advance = true;
            RIG_CHECK(Core::TickSkeletalOwnerAssetsAndHandler(session, &handler, 0).Callbacks == 0 &&
                      handler.Consumed == 1);
            RIG_CHECK(!Core::HasPendingSkeletalConsumers(session, &handler));
            RIG_CHECK(session.BindSnapshot(files.MakeSnapshot(files.Json)) == S::Busy);
            RIG_CHECK(session.Close() == S::Success && session.Drain() == S::Drained);
            RIG_CHECK(session.End());
            RIG_CHECK(registry.IsInitialized() == borrowed);
            if (borrowed)
            {
                registry.Shutdown();
            }
        }
        jobs.Shutdown();
        std::printf("SPLIT_RUNTIME_CASE result=pass actual_session_pause_delivery_consumer_owned_borrowed_shutdown\n");
    }
    namespace X = NorvesLib::Tests::RigSplitFixture;
    namespace V = NorvesLib::Tests::RigV1Fixture;
    namespace H = NorvesLib::RHI;
    namespace Rendering = Core::Rendering;
    namespace Cook = NorvesLib::Tools::AssetCook;

    class FakeTexture final : public H::ITexture
    {
    public:
        explicit FakeTexture(const H::TextureDesc& desc) : Desc(desc) {}
        uint32_t GetWidth() const override { return Desc.Width; }
        uint32_t GetHeight() const override { return Desc.Height; }
        uint32_t GetDepth() const override { return Desc.Depth; }
        uint32_t GetMipLevels() const override { return Desc.MipLevels; }
        uint32_t GetArraySize() const override { return Desc.ArraySize; }
        H::Format GetFormat() const override { return Desc.TextureFormat; }
        H::ResourceUsage GetUsage() const override { return Desc.Usage; }
        bool IsCubemap() const override { return Desc.IsCubemap; }
        void Update(const void* data, uint32_t row, uint32_t slice, uint32_t mip = 0, uint32_t layer = 0) override
        {
            RIG_CHECK(data && row && slice && mip < Desc.MipLevels && layer < Desc.ArraySize);
            const auto* bytes = static_cast<const uint8_t*>(data);
            Pixels.assign(bytes, bytes + slice);
        }
    private:
        H::TextureDesc Desc;
        V::Bytes Pixels;
    };
    class FakeBuffer final : public H::IBuffer
    {
    public:
        explicit FakeBuffer(const H::BufferDesc& desc) : Desc(desc), Data(static_cast<size_t>(desc.Size)) {}
        uint64_t GetSize() const override { return Desc.Size; }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < Data.size() ? Data.data() + static_cast<size_t>(offset) : nullptr;
        }
        void Unmap() override {}
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            RIG_CHECK(data && offset <= Data.size() && size <= Data.size() - offset);
            std::memcpy(Data.data() + static_cast<size_t>(offset), data, static_cast<size_t>(size));
        }
        H::ResourceUsage GetUsage() const override { return Desc.Usage; }
    private:
        H::BufferDesc Desc;
        V::Bytes Data;
    };
    class FakeDevice final : public H::IDevice
    {
    public:
        H::BufferPtr CreateBuffer(const H::BufferDesc& desc) override { return C::MakeShared<FakeBuffer>(desc); }
        H::TexturePtr CreateTexture(const H::TextureDesc& desc) override { return C::MakeShared<FakeTexture>(desc); }
        H::SamplerPtr CreateSampler(const H::SamplerDesc&) override { return {}; }
        H::ShaderPtr CreateShader(const H::ShaderDesc&) override { return {}; }
        H::CommandListPtr CreateCommandList() override { return {}; }
        H::SwapChainPtr CreateSwapChain(const H::SwapChainDesc&) override { return {}; }
        H::RenderPassPtr CreateRenderPass(const H::RenderPassDesc&) override { return {}; }
        H::FramebufferPtr CreateFramebuffer(const H::FramebufferDesc&) override { return {}; }
        H::PipelinePtr CreateGraphicsPipeline(const H::GraphicsPipelineDesc&) override { return {}; }
        H::PipelinePtr CreateComputePipeline(const H::ComputePipelineDesc&) override { return {}; }
        H::DescriptorSetPtr CreateDescriptorSet(const H::DescriptorSetDesc&) override { return {}; }
        H::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        H::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
        void WaitIdle() override {}
        H::API GetAPI() const override { return H::API::None; }
        const H::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(const NorvesLib::Math::Matrix4x4& p,
                                                              bool flip = true) const override
        {
            (void)flip;
            return p;
        }
    private:
        H::DeviceCapabilities Capabilities;
    };
    V::Text TwoMaterials(const V::Text& base, const V::Text& first, const V::Text& second)
    {
        const V::Text primitive =
            R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":5},"indices":8,"mode":4})";
        const auto a = V::Replace(primitive, "\"mode\":4", "\"mode\":4,\"material\":0");
        const auto b = V::Replace(primitive, "\"mode\":4", "\"mode\":4,\"material\":1");
        auto json = V::Replace(base, primitive.c_str(), a + "," + b);
        return V::Replace(json, "\"skins\":", V::Text("\"materials\":[") + first + "," + second + "],\"skins\":");
    }
    V::Text WriteMaterialTextures(const Cook::RigSplitCookResult& cooked)
    {
        V::Text json = "{\"version\":1,\"assets\":[";
        bool first = true;
        for (const auto& image : cooked.TexturePlans)
        {
            RIG_CHECK(image.Payload == Cook::MeshImagePayload::RawRgba8);
            Cook::TextureCookResult texture;
            V::Text error;
            RIG_CHECK(Cook::CookRgba8ToNvtex(image.GetBytes(), image.Width, image.Height, image.Format, texture, error));
            V::Bytes package;
            uint64_t hash = 0;
            // package codec の既存 API 境界だけは標準文字列を受け取る。
            std::string packageError;
            RIG_CHECK(Cook::Detail::BuildSingleSkeletalEntryPackage(
                "asset", Core::Asset::MakeAssetPackageFourCC('T', 'e', 'x', '0'),
                texture.NvtexBytes, package, hash, packageError));
            const auto path = image.LogicalPath + ".nvpk";
            std::filesystem::create_directories(std::filesystem::path(path.c_str()).parent_path());
            V::WriteBytes(path.c_str(), package);
            if (!first)
            {
                json += ",";
            }
            first = false;
            json += V::Text("{\"logical_path\":\"") + image.LogicalPath +
                    "\",\"kind\":\"texture\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"" +
                    image.Format + "\",\"cooked_package\":\"" + path +
                    "\",\"entry_name\":\"asset\",\"entry_type\":\"Tex0\",\"cooked_hash\":\"" +
                    Core::Asset::FormatAssetHashHex(hash) + "\",\"cooked_version\":0}";
        }
        return json + "]}";
    }
    void Materials(T::JobSystem::ExecutionMode mode)
    {
        // 契約: cook の slot 値、slot ごとの失敗、資産解放後の材質回収を CPU だけで反証する。
        Environment env(mode);
        const V::Text body =
            R"({"name":"Body","pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,1,1],"metallicFactor":0.25,"roughnessFactor":0.75}})";
        const V::Text trim =
            R"({"name":"Trim","pbrMetallicRoughness":{"metallicFactor":1,"roughnessFactor":0.5}})";
        const auto paths = X::Request();
        // 既定の Auto は開いた形を両面にするが、スキンでは両面を表せないため取り込み時に切る。
        X::Sidecar(R"({"version":1,"material":{"doubleSided":"force_false"}})");
        const auto cooked = X::CookSource(TwoMaterials(env.Files.Files.Json, body, trim), paths);
        std::filesystem::remove("RigV1Fixture/rig.gltf.import.json");
        X::WritePackages(cooked);
        const auto textureManifest = WriteMaterialTextures(cooked);
        Core::Skeletal::SkinMeshV1 mesh;
        Core::Skeletal::RigV1Report report;
        RIG_CHECK(Core::Skeletal::ParseSkinMeshV1(V::View(cooked.Mesh.Payload), mesh, report));
        const auto* data = mesh.GetData();
        RIG_CHECK(data && data->Slots.size() == 2 && data->Materials.size() == 2);
        RIG_CHECK(cooked.SlotSourceMaterials.size() == 2 &&
                  cooked.SlotSourceMaterials[0] == 0 && cooked.SlotSourceMaterials[1] == 1);
        for (size_t i = 0; i < 2; ++i)
        {
            const auto& material = data->Materials[i];
            std::printf("SPLIT_RUNTIME_MATERIAL slot=%zu name=%s base=(%g,%g,%g,%g) metallic=%g roughness=%g "
                        "albedo=%s normal=%s arm=%s emissive=%s\n",
                        i, data->Slots[i].Name.c_str(), material.Record.BaseColor[0], material.Record.BaseColor[1],
                        material.Record.BaseColor[2], material.Record.BaseColor[3],
                        material.Record.Metallic, material.Record.Roughness, material.Textures[0].c_str(),
                        material.Textures[1].c_str(), material.Textures[2].c_str(), material.Textures[3].c_str());
        }
        const auto manifest = [](const Cook::RigSplitCookResult& source, const Cook::RigSplitCookRequest& request)
        {
            return V::Text("{\"version\":1,\"assets\":[") +
                   F::Record(source.ManifestJson, request.SkeletonPath) + "," +
                   F::Record(source.ManifestJson, request.MeshPath) + "," +
                   F::Record(source.ManifestJson, request.BankPath) + "]}";
        };
        RIG_CHECK(env.Value.SetSnapshot(env.Files.MakeSnapshot(manifest(cooked, paths))) == S::Success);
        Core::Skeletal::RigSplitRequest request;
        request.SkeletonPath = paths.SkeletonPath;
        request.MeshPath = paths.MeshPath;
        request.BankPaths = {paths.BankPath};
        auto completed = One(env, request);
        RIG_CHECK(completed.Failure == Failure::None && completed.Asset);

        auto device = C::MakeShared<FakeDevice>();
        Rendering::RenderResources resources;
        resources.SetGeometryPoolBlockBytes(1ull << 20);
        RIG_CHECK(resources.Initialize(device));
        RIG_CHECK(resources.Textures().SetTextureAssetRoot(
            C::String(env.Files.Files.Root.generic_string().c_str())));
        RIG_CHECK(resources.Textures().LoadTextureAssetManifestFromJsonText(
            NorvesLib::Tests::SkeletalLoaderFixture::CoreText(textureManifest)));
        {
            // builder は借用先の resources より先に破棄する。
            R::SkinnedMaterialBuilder builder(resources, T::Thread::GetCurrentThreadId());
            const auto before = resources.Materials().GetCount();
            const auto built = builder.Build(completed.Asset);
            if (built && built->Status != R::SkinnedMaterialBuildStatus::Success)
            {
                for (const auto& slot : built->Slots)
                {
                    std::fprintf(stderr, "SPLIT_RUNTIME_MATERIAL_FAILURE slot=%u status=%u reason=%s\n",
                                 slot.Slot, unsigned(slot.Status), slot.Reason.c_str());
                }
            }
            RIG_CHECK(built && built->Status == R::SkinnedMaterialBuildStatus::Success && built->Slots.size() == 2);
            const auto& a = built->Slots[0];
            const auto& b = built->Slots[1];
            RIG_CHECK(a.Slot == 0 && a.Name == C::String(_T("Body")) &&
                      b.Slot == 1 && b.Name == C::String(_T("Trim")));
            RIG_CHECK(a.Status == R::SkinnedMaterialSlotStatus::Created &&
                      b.Status == R::SkinnedMaterialSlotStatus::Created);
            RIG_CHECK(a.CreateData.BaseColor[0] == 1 && a.CreateData.BaseColor[1] == 1 &&
                      a.CreateData.BaseColor[2] == 1 && a.CreateData.BaseColor[3] == 1 &&
                      a.CreateData.Metallic == 0.25f && a.CreateData.Roughness == 0.75f &&
                      a.CreateData.AlbedoTexture.IsValid() && !a.CreateData.NormalTexture.IsValid() &&
                      !a.CreateData.AOTexture.IsValid() && !a.CreateData.RoughnessTexture.IsValid() &&
                      !a.CreateData.MetallicTexture.IsValid());
            RIG_CHECK(b.CreateData.BaseColor[0] == 1 && b.CreateData.BaseColor[1] == 1 &&
                      b.CreateData.BaseColor[2] == 1 && b.CreateData.BaseColor[3] == 1 &&
                      b.CreateData.Metallic == 1 && b.CreateData.Roughness == 0.5f &&
                      !b.CreateData.AlbedoTexture.IsValid() && !b.CreateData.NormalTexture.IsValid() &&
                      !b.CreateData.AOTexture.IsValid() && !b.CreateData.RoughnessTexture.IsValid() &&
                      !b.CreateData.MetallicTexture.IsValid());
            RIG_CHECK(a.Material.IsValid() && b.Material.IsValid() &&
                      resources.Materials().GetData(a.Material) && resources.Materials().GetData(b.Material));
            RIG_CHECK(builder.Build(completed.Asset) == built && resources.Materials().GetCount() == before + 2);

            // texture の manifest が無い場合は、対応する slot だけを失敗させる。
            Rendering::RenderResources missingTextures;
            missingTextures.SetGeometryPoolBlockBytes(1ull << 20);
            RIG_CHECK(missingTextures.Initialize(device));
            {
                R::SkinnedMaterialBuilder missingBuilder(missingTextures, T::Thread::GetCurrentThreadId());
                const auto missing = missingBuilder.Build(completed.Asset);
                RIG_CHECK(missing && missing->Status == R::SkinnedMaterialBuildStatus::PartialFailure &&
                          missing->Slots.size() == 2);
                RIG_CHECK(missing->Slots[0].Status == R::SkinnedMaterialSlotStatus::TextureFailed &&
                          missing->Slots[0].Reason == "imported_opaque: cooked_texture_required role=albedo" &&
                          !missing->Slots[0].Material.IsValid());
                RIG_CHECK(missing->Slots[1].Status == R::SkinnedMaterialSlotStatus::Created &&
                          missing->Slots[1].Material.IsValid() &&
                          missingTextures.Materials().GetData(missing->Slots[1].Material));
            }
            missingTextures.Shutdown();

            // MASK は別 mesh の snapshot で読み、正常な Body が作成されることも確かめる。
            auto maskPaths = paths;
            maskPaths.MeshPath = "Models/MaskMesh.nvskel";
            const auto maskTrim = V::Replace(trim, "\"name\":\"Trim\"", "\"name\":\"Trim\",\"alphaMode\":\"MASK\"");
            X::Sidecar(R"({"version":1,"material":{"doubleSided":"force_false"}})");
            const auto masked = X::CookSource(TwoMaterials(env.Files.Files.Json, body, maskTrim), maskPaths);
            std::filesystem::remove("RigV1Fixture/rig.gltf.import.json");
            X::WritePackages(masked);
            const auto maskTextures = WriteMaterialTextures(masked);
            RIG_CHECK(resources.Textures().LoadTextureAssetManifestFromJsonText(
                NorvesLib::Tests::SkeletalLoaderFixture::CoreText(maskTextures)));
            RIG_CHECK(env.Value.SetSnapshot(env.Files.MakeSnapshot(manifest(masked, maskPaths))) == S::Success);
            auto maskRequest = request;
            maskRequest.MeshPath = maskPaths.MeshPath;
            auto maskCompleted = One(env, maskRequest);
            RIG_CHECK(maskCompleted.Failure == Failure::None && maskCompleted.Asset);
            {
                R::SkinnedMaterialBuilder maskBuilder(resources, T::Thread::GetCurrentThreadId());
                const auto mask = maskBuilder.Build(maskCompleted.Asset);
                RIG_CHECK(mask && mask->Status == R::SkinnedMaterialBuildStatus::PartialFailure &&
                          mask->Slots.size() == 2);
                RIG_CHECK(mask->Slots[0].Status == R::SkinnedMaterialSlotStatus::Created &&
                          mask->Slots[0].Material.IsValid() && resources.Materials().GetData(mask->Slots[0].Material));
                RIG_CHECK(mask->Slots[1].Status == R::SkinnedMaterialSlotStatus::Unsupported &&
                          mask->Slots[1].Reason == "imported_opaque: alphaMode_mask_unsupported" &&
                          !mask->Slots[1].Material.IsValid());
            }
            maskCompleted = {};

            const Rendering::MaterialHandle handles[] = {a.Material, b.Material};
            completed = {};
            env.Registry.CollectGarbage();
            env.Registry.CollectGarbage();
            RIG_CHECK(builder.Collect() >= 1);
            for (const auto handle : handles)
            {
                RIG_CHECK(resources.Materials().GetData(handle) == nullptr);
            }
            RIG_CHECK(builder.GetEntryCount() == 0 && resources.Materials().GetCount() == before);
        }
        resources.Shutdown();
        std::printf("SPLIT_RUNTIME_CASE materials result=pass\n");
    }
    bool Run(const char* name, T::JobSystem::ExecutionMode mode)
    {
        if (std::strcmp(name, "sharing") == 0)
        {
            Sharing(mode);
        }
        else if (std::strcmp(name, "policy") == 0)
        {
            Policy(mode);
        }
        else if (std::strcmp(name, "mixed") == 0)
        {
            Mixed(mode);
        }
        else if (std::strcmp(name, "reentry") == 0)
        {
            MutatedReentry(mode);
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
            Handoff(mode);
        }
        else if (std::strcmp(name, "session") == 0)
        {
            Session(mode);
        }
        else if (std::strcmp(name, "materials") == 0)
        {
            Materials(mode);
        }
        else
        {
            return false;
        }
        return true;
    }
    void Child(const char* executable, const char* name, const char* mode)
    {
        const char* args[] = {executable, "--split-runtime-child", name, mode, nullptr};
        const intptr_t child = _spawnv(_P_NOWAIT, executable, args);
        RIG_CHECK(child != -1);
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
        RIG_CHECK(wait == WAIT_OBJECT_0 && read && code == 0);
    }
} // namespace
int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--split-runtime-child") == 0)
    {
        return Run(argv[2], std::strcmp(argv[3], "simple") == 0 ? T::JobSystem::EXECUTION_SIMPLE
                                                                : T::JobSystem::EXECUTION_WORK_STEALING)
                   ? 0
                   : 1;
    }
    for (const char* mode : {"simple", "steal"})
    {
        for (const char* name : {"sharing", "policy", "mixed", "reentry", "failures", "cancel", "handoff", "session", "materials"})
        {
            Child(argv[0], name, mode);
        }
    }
    std::printf(
        "SPLIT_RUNTIME result=pass existing_worker_ready_owner_delegate_identity_policy_cancel_session_cpu_no_gpu\n");
    return 0;
}
