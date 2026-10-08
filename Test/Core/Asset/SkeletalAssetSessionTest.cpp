// production session/owner順序/M9一回event/World attachをCPUだけで検査する。
#include "Engine/SkeletalAssetSession.h"
#include "Engine/SkeletalAssetSessionTestAccess.h"
#include "Resource/SkeletalAssetRuntimeTestAccess.h"
#include "Application/ApplicationHandlerBase.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "GameMode/GameModeScope.h"
#include "Component/SkinnedMeshComponent.h"
#include "Thread/JobSystem.h"
#include "Asset/AssetSystem.h"
#include "SkeletalLoaderFixture.h"
#include "GameModes/Rendering3DTest/M9SkeletalPreparation.h"
#include "GameModes/Rendering3DTest/M9WorldSkeletal.h"
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
            std::fprintf(stderr, "Skeletal session %s:%d %s\n", __FILE__, __LINE__, #x);                               \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace A = Core::Asset;
namespace T = NorvesLib::Thread;
namespace F = NorvesLib::Tests::SkeletalLoaderFixture;
namespace M9 = Game::GameModes;
using S = Core::SkeletalRuntimeStatus;
using Access = Core::ResourceIO::Detail::SkeletalRuntimeTestAccess;
using SessionAccess = Core::Engine::Detail::SkeletalSessionTestAccess;
namespace
{
    struct Fixture
    {
        std::filesystem::path Root;
        C::TSharedPtr<A::AssetSystem> Snapshot;
        Fixture()
        {
            char name[80];
            std::snprintf(name, sizeof(name), "norves-session-%llu",
                          static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            Root = std::filesystem::temp_directory_path() / name;
            CHECK(std::filesystem::create_directory(Root));
            uint64_t hash = 0;
            const auto bytes = F::Package(F::BuildThreeClips(), A::MakeAssetPackageFourCC('S', 'k', 'l', '0'), hash);
            std::ofstream file(Root / "asset.nvpkg", std::ios::binary);
            CHECK(file);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            file.close();
            CHECK(!file.fail());
            const auto json =
                C::AnsiString(
                    "{\"version\":1,\"assets\":[{\"logical_path\":\"Actors/A\",\"kind\":\"model\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"nvskel.v0.skinned.pnujiw.u32\",\"cooked_package\":\"asset.nvpkg\",\"entry_name\":\"a\",\"entry_type\":\"Skl0\",\"cooked_hash\":\"") +
                A::FormatAssetHashHex(hash) + "\",\"cooked_version\":0}]}";
            Snapshot = C::MakeShared<A::AssetSystem>(C::AnsiString(Root.generic_string().c_str()));
            CHECK(Snapshot->LoadManifestFromJsonText(F::CoreText(json)));
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };
    struct Handler final : Core::Application::ApplicationHandlerBase
    {
        M9::M9SkeletalPreparation* Preparation = nullptr;
        M9::M9SkeletalEvent Event;
        uint32_t Updates = 0, Consumed = 0;
        bool bAdvance = true;
        void OnUpdate(float) override
        {
            ++Updates;
            if (bAdvance && Preparation && Preparation->TakeEvent(Event))
            {
                ++Consumed;
            }
        }
        bool ShouldAdvanceSimulation() const override
        {
            return bAdvance;
        }
        bool HasPendingAssetConsumers() const override
        {
            return Preparation && Preparation->HasPendingWork();
        }
    };
    void ThrowPreparation(void*)
    {
        throw std::bad_alloc();
    }
    void Lifecycle(T::JobSystem::ExecutionMode mode)
    {
        Fixture files;
        auto& jobs = T::JobSystem::Get();
        jobs.Initialize(1, mode);
        Core::ResourceRegistry registry;
        Core::SkeletalAssetSession session;
        CHECK(session.Begin(registry, jobs, {}) == S::WrongOwner && !registry.IsInitialized());
        SessionAccess::SetPrepareHook(session, ThrowPreparation, nullptr);
        CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::PreparationException);
        CHECK(!registry.IsInitialized() && !session.IsActive());
        SessionAccess::SetPrepareHook(session, nullptr, nullptr);
        CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Success);
        CHECK(session.OwnsRegistry() && registry.IsInitialized() && !session.HasPinnedSnapshot());
        CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::AlreadyBound);
        Handler handler;
        CHECK(Core::TickSkeletalOwnerAssetsAndHandler(session, &handler, 0.f).Status == S::Success &&
              handler.Updates == 1);
        CHECK(session.BindSnapshot(files.Snapshot) == S::Success);
        CHECK(session.BindSnapshot(files.Snapshot) == S::Unchanged);
        auto different = C::MakeShared<A::AssetSystem>();
        CHECK(session.BindSnapshot(different) == S::Busy);
        M9::M9SkeletalPreparation preparation;
        handler.Preparation = &preparation;
        CHECK(preparation.Start(*session.GetRuntime(), "Actors/A", C::StringView(_T("Wave"))).Status == S::Accepted);
        CHECK(Core::HasPendingSkeletalConsumers(session, &handler));
        CHECK(Access::WaitReady(*session.GetRuntime(), 1));
        CHECK(registry.GetResourceCount() == 0 && handler.Consumed == 0);
        handler.bAdvance = false;
        CHECK(Core::TickSkeletalOwnerAssetsAndHandler(session, &handler, 0.f).Callbacks == 1);
        CHECK(session.GetPendingCount() == 0 && handler.Consumed == 0 && registry.GetResourceCount() == 6);
        CHECK(Core::HasPendingSkeletalConsumers(session, &handler)); // pause中は配送済みeventを保持。
        handler.bAdvance = true;
        CHECK(Core::TickSkeletalOwnerAssetsAndHandler(session, &handler, 0.f).Callbacks == 0 && handler.Consumed == 1);
        CHECK(handler.Event.bReady && !Core::HasPendingSkeletalConsumers(session, &handler));
        M9::M9SkeletalEvent unchanged = handler.Event;
        CHECK(!preparation.TakeEvent(unchanged) && unchanged.Asset == handler.Event.Asset);
        std::thread wrong(
            [&]()
            {
                CHECK(session.Flush().Status == S::WrongOwner);
                CHECK(session.BindSnapshot(files.Snapshot) == S::WrongOwner);
                CHECK(session.Close() == S::WrongOwner);
            });
        wrong.join();
        const auto old = Access::Counts(*session.GetRuntime());
        auto weak = Access::WeakState(*session.GetRuntime());
        preparation.Cancel();
        handler.Event = {};
        unchanged = {};
        CHECK(session.Close() == S::Success && session.Drain() == S::Drained && session.End());
        CHECK(!registry.IsInitialized() && !session.IsActive());
        jobs.Shutdown();
        CHECK(weak.expired());
        jobs.Initialize(1, mode);
        CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Success);
        CHECK(session.BindSnapshot(files.Snapshot) == S::Success);
        const auto fresh = Access::Counts(*session.GetRuntime());
        CHECK(fresh.Session != old.Session);
        // 全session越しの旧typed handle拒否は、この形式では保証しない。
        CHECK(session.Close() == S::Success && session.Drain() == S::Drained && session.End() && session.End());
        jobs.Shutdown();
        std::printf("SKELETAL_OWNER_CASE result=pass owned_lifecycle_pause_event_fresh_session\n");
    }
    void BorrowedAndUninitialized(T::JobSystem::ExecutionMode mode)
    {
        auto& jobs = T::JobSystem::Get();
        jobs.Initialize(1, mode);
        {
            Core::ResourceRegistry registry;
            auto occupied = registry.CreateTransient<Core::SkeletonResource>("preinitialize");
            CHECK(occupied);
            const auto id = occupied->GetResourceId();
            Core::SkeletalAssetSession session;
            CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Busy);
            CHECK(!registry.IsInitialized() && registry.GetResourceCount() == 1 && occupied->GetResourceId() == id);
        }
        {
            Core::ResourceRegistry registry;
            CHECK(registry.Initialize());
            auto sentinel = registry.CreateTransient<Core::SkeletonResource>("sentinel");
            CHECK(sentinel);
            const auto id = sentinel->GetResourceId();
            const auto handle = registry.GetHandle<Core::SkeletonResource>(id);
            Core::SkeletalAssetSession session;
            SessionAccess::SetPrepareHook(session, ThrowPreparation, nullptr);
            CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::PreparationException);
            CHECK(registry.IsInitialized() && registry.Resolve(handle) == sentinel && registry.GetResourceCount() == 1);
            SessionAccess::SetPrepareHook(session, nullptr, nullptr);
            CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Success &&
                  !session.OwnsRegistry());
            CHECK(session.Flush().Status == S::Success && registry.GetResourceCount() == 1);
            CHECK(session.Close() == S::Success && session.Drain() == S::Drained && session.End());
            CHECK(registry.IsInitialized() && registry.Resolve(handle) == sentinel && registry.GetResourceCount() == 1);
            sentinel.reset();
            registry.Shutdown();
        }
        jobs.Shutdown();
        std::printf("SKELETAL_OWNER_CASE result=pass borrowed_registry_and_dirty_uninitialized_refusal\n");
    }
    void DeferredAndCanceled(T::JobSystem::ExecutionMode mode)
    {
        Fixture files;
        auto& jobs = T::JobSystem::Get();
        jobs.Initialize(1, mode);
        Core::ResourceRegistry registry;
        Core::SkeletalAssetSession session;
        CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Success);
        CHECK(session.BindSnapshot(files.Snapshot) == S::Success);
        Handler handler;
        bool delivered = false;
        auto admitted = session.GetRuntime()->LoadAsync("Actors/A",
                                                        [&](const Core::SkeletalAssetCompletion& value)
                                                        {
                                                            CHECK(value.Asset);
                                                            delivered = true;
                                                            CHECK(session.Close() == S::Success &&
                                                                  session.Drain() == S::Deferred && !session.End());
                                                            CHECK(registry.IsInitialized() && session.IsActive());
                                                        });
        CHECK(admitted.Status == S::Accepted);
        CHECK(Access::WaitReady(*session.GetRuntime(), 1));
        CHECK(Core::TickSkeletalOwnerAssetsAndHandler(session, &handler, 0.f).Status == S::Closed);
        CHECK(delivered && handler.Updates == 0 && session.IsActive());
        CHECK(session.Drain() == S::Drained && session.End());
        CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Success);
        CHECK(session.BindSnapshot(files.Snapshot) == S::Success);
        M9::M9SkeletalPreparation preparation;
        auto started = preparation.Start(*session.GetRuntime(), "Actors/A", C::StringView(_T("Wave")));
        CHECK(started.Status == S::Accepted);
        auto weak = Access::WeakRequest(*session.GetRuntime(), started.RequestId);
        CHECK(session.Close() == S::Success && session.Drain() == S::Drained);
        CHECK(weak.expired() && registry.GetResourceCount() == 0);
        preparation.Cancel();
        CHECK(session.End());
        jobs.Shutdown();
        std::printf("SKELETAL_OWNER_CASE result=pass callback_deferred_shutdown_and_no_late_consumer\n");
    }
    void EventFailuresAndReplacement(T::JobSystem::ExecutionMode mode)
    {
        Fixture files;
        auto& jobs = T::JobSystem::Get();
        jobs.Initialize(1, mode);
        Core::ResourceRegistry registry;
        Core::SkeletalAssetSession session;
        CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Success &&
              session.BindSnapshot(files.Snapshot) == S::Success);
        auto& runtime = *session.GetRuntime();
        M9::M9SkeletalPreparation preparation;
        M9::M9SkeletalEvent event;
        CHECK(preparation.CanPrepare() && !preparation.CanEnter());
        CHECK(preparation.Start(runtime, "Actors/Missing", C::StringView(_T("Wave"))).Status == S::Accepted);
        CHECK(preparation.CanPrepare() && preparation.CanEnter());
        CHECK(Access::WaitReady(runtime, 1));
        CHECK(session.Flush().Callbacks == 1);
        CHECK(preparation.TakeEvent(event) && !event.bReady &&
              event.Failure == Core::SkeletalRuntimeFailure::ResolveRejected);
        CHECK(!preparation.TakeEvent(event));
        CHECK(!preparation.CanPrepare() && !preparation.CanEnter());
        preparation.Cancel();
        CHECK(preparation.CanPrepare() && !preparation.CanEnter());
        CHECK(preparation.Start(runtime, "Actors/A", C::StringView(_T("missing"))).Status == S::Accepted);
        CHECK(Access::WaitReady(runtime, 1));
        CHECK(session.Flush().Callbacks == 1);
        CHECK(preparation.TakeEvent(event) && !event.bReady && event.bClipSelectionFailed);
        preparation.Cancel();
        const auto old = preparation.Start(runtime, "Actors/A", C::StringView(_T("Wave")));
        CHECK(old.Status == S::Accepted);
        preparation.Cancel();
        CHECK(!preparation.HasPendingWork() && !preparation.IsActive());
        const auto fresh = preparation.Start(runtime, "Actors/A", C::StringView(_T("Wave")));
        CHECK(fresh.Status == S::Accepted && fresh.RequestId != old.RequestId);
        CHECK(Access::WaitReady(runtime, 2));
        CHECK(session.Flush().Callbacks == 1);
        CHECK(preparation.CanEnter() && preparation.CanPrepare()); // early eventでも初回Enterは可能。
        CHECK(preparation.TakeEvent(event) && event.bReady && event.Asset && event.Clip);
        CHECK(event.Clip == event.Asset->GetClip(C::StringView(_T("Wave"))));
        const auto consumedId = preparation.GetRequestId();
        CHECK(!preparation.CanEnter() && !preparation.CanPrepare());
        const auto busy = preparation.Start(runtime, "Actors/A", C::StringView(_T("Wave")));
        CHECK(busy.Status == S::Busy && busy.RequestId == 0);
        CHECK(preparation.GetRequestId() == consumedId && !preparation.HasPendingWork());
        CHECK(session.GetPendingCount() == 0 && !preparation.TakeEvent(event));
        preparation.Cancel();
        CHECK(preparation.CanPrepare() && !preparation.CanEnter()); // Leave後に無準備の再Enterは拒否。
        CHECK(preparation.Start(runtime, "Actors/A", C::StringView(_T("Wave"))).Status == S::Accepted);
        CHECK(preparation.CanEnter()); // fresh Prepare後だけ再Enterを許可。
        CHECK(Access::WaitReady(runtime, 1));
        CHECK(session.Flush().Callbacks == 1 && preparation.CanEnter());
        CHECK(preparation.TakeEvent(event) && event.bReady && !preparation.CanEnter());
        preparation.Cancel();
        event = {};
        jobs.StopAcceptingTasks();
        const auto rejected = preparation.Start(runtime, "Actors/Missing", C::StringView(_T("Wave")));
        CHECK(rejected.Status == S::SubmitRejected && rejected.RequestId == 0);
        CHECK(preparation.TakeEvent(event) && !event.bReady && event.Admission == S::SubmitRejected);
        preparation.Cancel();
        CHECK(session.Close() == S::Success && session.Drain() == S::Drained && session.End());
        jobs.Shutdown();
        std::printf("SKELETAL_OWNER_CASE result=pass named_clip_failure_request_replacement_and_refusal\n");
    }
    void ClipSelectionAndAttach(T::JobSystem::ExecutionMode mode)
    {
        Fixture files;
        auto& jobs = T::JobSystem::Get();
        jobs.Initialize(1, mode);
        Core::ResourceRegistry registry;
        Core::SkeletalAssetSession session;
        CHECK(session.Begin(registry, jobs, T::Thread::GetCurrentThreadId()) == S::Success &&
              session.BindSnapshot(files.Snapshot) == S::Success);
        M9::M9SkeletalPreparation preparation;
        M9::M9SkeletalEvent event;
        CHECK(preparation.Start(*session.GetRuntime(), "Actors/A", C::StringView(_T("Wave"))).Status == S::Accepted);
        CHECK(Access::WaitReady(*session.GetRuntime(), 1));
        CHECK(session.Flush().Callbacks == 1);
        CHECK(preparation.TakeEvent(event) && event.bReady);
        Core::World world;
        world.Initialize();
        Core::GameMode::GameModeScope scope(&world, nullptr);
        Core::Entity* object = nullptr;
        Core::Component::SkinnedMeshComponent* component = nullptr;
        CHECK(M9::AttachM9SkeletalAsset(world, scope, event.Asset, event.Clip, object, component));
        CHECK(world.GetObjectCount() == 1 && object && component);
        CHECK(!preparation.CanPrepare() && !preparation.CanEnter());
        CHECK(preparation.Start(*session.GetRuntime(), "Actors/A", C::StringView(_T("Wave"))).Status == S::Busy);
        CHECK(!preparation.HasPendingWork() && session.GetPendingCount() == 0);
        CHECK(!M9::AttachM9SkeletalAsset(world, scope, event.Asset, event.Clip, object, component) &&
              world.GetObjectCount() == 1);
        component->SetAnimationTimeSeconds(1.f);
        CHECK(component->EvaluatePose());
        Core::Rendering::SkinnedMeshProxy first;
        CHECK(component->BuildSkinnedMeshProxy(first));
        const auto serial = component->GetPoseSerial();
        auto second = event.Asset->GetClip(size_t{1});
        CHECK(second && second != event.Clip);
        CHECK(component->SetAnimationClip(second));
        CHECK(component->EvaluatePose() && component->GetPoseSerial() == serial + 1);
        Core::Rendering::SkinnedMeshProxy selected;
        CHECK(component->BuildSkinnedMeshProxy(selected));
        CHECK(std::abs(first.BonePalette[1].values[13] - 1.f) < 1e-5f);
        CHECK(std::abs(selected.BonePalette[1].values[13] - (-2.f)) < 1e-5f);
        auto foreign = registry.CreateTransient<Core::AnimationClipResource>("foreign");
        CHECK(foreign);
        foreign->SetClip(Core::Skeletal::SkeletalAnimationClip(second->GetClip()));
        CHECK(foreign->Load());
        CHECK(!component->SetAnimationClip(foreign) && !component->SetAnimationClip({}) &&
              component->GetAnimationClip() == second);
        const auto savedClip = second->GetClip();
        second->Unload();
        CHECK(!component->EvaluatePose());
        second->SetClip(Core::Skeletal::SkeletalAnimationClip(savedClip));
        CHECK(second->Load() && component->EvaluatePose());
        auto mesh = event.Asset->GetMesh();
        auto skeleton = event.Asset->GetSkeleton();
        event.Asset->SetResources(mesh, skeleton, event.Clip);
        CHECK(!component->GetAnimationClip() && !component->EvaluatePose()); // 失われたoverrideは先頭へ落とさない。
        component->SetSkeletalAsset(event.Asset);
        CHECK(component->GetAnimationClip() == event.Clip && component->EvaluatePose());
        event.Asset->SetResources(mesh, skeleton, foreign);
        CHECK(component->GetAnimationClip() == foreign && component->EvaluatePose());
        preparation.Cancel();
        scope.Cleanup();
        CHECK(world.GetObjectCount() == 0);
        Core::GameMode::GameModeScope failedScope(&world, nullptr);
        Core::Entity* failedObject = nullptr;
        Core::Component::SkinnedMeshComponent* failedComponent = nullptr;
        auto other = registry.CreateTransient<Core::AnimationClipResource>("not-member");
        CHECK(other);
        other->SetClip(Core::Skeletal::SkeletalAnimationClip(savedClip));
        CHECK(other->Load());
        CHECK(!M9::AttachM9SkeletalAsset(world, failedScope, event.Asset, other, failedObject, failedComponent));
        CHECK(!failedObject && !failedComponent && world.GetObjectCount() == 1);
        failedScope.Cleanup();
        CHECK(world.GetObjectCount() == 0);
        world.Finalize();
        event = {};
        second.reset();
        foreign.reset();
        other.reset();
        mesh.reset();
        skeleton.reset();
        first = {};
        selected = {};
        CHECK(session.Close() == S::Success && session.Drain() == S::Drained && session.End());
        jobs.Shutdown();
        std::printf("SKELETAL_OWNER_CASE result=pass actual_world_scope_attach_clip_selection_legacy_alias\n");
    }
    bool Run(const char* name, T::JobSystem::ExecutionMode mode)
    {
        if (std::strcmp(name, "lifecycle") == 0)
        {
            Lifecycle(mode);
        }
        else if (std::strcmp(name, "borrowed") == 0)
        {
            BorrowedAndUninitialized(mode);
        }
        else if (std::strcmp(name, "deferred") == 0)
        {
            DeferredAndCanceled(mode);
        }
        else if (std::strcmp(name, "events") == 0)
        {
            EventFailuresAndReplacement(mode);
        }
        else if (std::strcmp(name, "attach") == 0)
        {
            ClipSelectionAndAttach(mode);
        }
        else
        {
            return false;
        }
        return true;
    }
    void Child(const char* exe, const char* name, const char* mode)
    {
        const char* argv[] = {exe, "--owner-child", name, mode, nullptr};
        const intptr_t process = _spawnv(_P_NOWAIT, exe, argv);
        CHECK(process != -1);
        HANDLE handle = reinterpret_cast<HANDLE>(process);
        const DWORD wait = WaitForSingleObject(handle, 60000);
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
    if (argc == 4 && std::strcmp(argv[1], "--owner-child") == 0)
    {
        return Run(argv[2], std::strcmp(argv[3], "simple") == 0 ? T::JobSystem::EXECUTION_SIMPLE
                                                                : T::JobSystem::EXECUTION_WORK_STEALING)
                   ? 0
                   : 1;
    }
    for (const char* mode : {"simple", "steal"})
    {
        for (const char* name : {"lifecycle", "borrowed", "deferred", "events", "attach"})
        {
            Child(argv[0], name, mode);
        }
    }
    std::printf(
        "SKELETAL_OWNER_LIFECYCLE result=pass production_session_owner_tick_m9_event_world_attach_clip_owned_borrowed_deferred_no_gpu_audio_claim\n");
    return 0;
}
