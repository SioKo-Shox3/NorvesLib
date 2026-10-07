// 分離modeも実GR83 runtimeの同ready/owner/取消/Drainを使うことを反証する。
#include "RigSplitPublicationFixture.h"
#include "Resource/SkeletalAssetRuntimeTestAccess.h"
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
        for (const char* name : {"sharing", "policy", "mixed", "reentry", "failures", "cancel", "handoff", "session"})
        {
            Child(argv[0], name, mode);
        }
    }
    std::printf(
        "SPLIT_RUNTIME result=pass existing_worker_ready_owner_delegate_identity_policy_cancel_session_cpu_no_gpu\n");
    return 0;
}
