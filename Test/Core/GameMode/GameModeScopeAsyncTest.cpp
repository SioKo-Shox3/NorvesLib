// モードを離れた後の非同期通知と、スコープが所有する資源の返却を確かめる。
#include "Engine/Engine.h"
#include "GameMode/GameModeContext.h"
#include "GameMode/GameModeScope.h"
#include "GameMode/GameModeStateMachine.h"
#include "GameMode/IGameMode.h"
#include "Rendering/RenderResources.h"
#include "Streaming/StreamingSystem.h"
#include "Thread/Atomic.h"
#include "Thread/ConditionVariable.h"
#include "Thread/Mutex.h"
#include "Thread/Thread.h"

#include <iostream>
#include <utility>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    using namespace NorvesLib::Core::Streaming;
    using Engine::GEngine;

    int g_Failures = 0;

    bool Check(bool bCondition, const char8_t* message)
    {
        if (!bCondition)
        {
            ++g_Failures;
            std::cerr << reinterpret_cast<const char*>(u8"検査失敗: ")
                      << reinterpret_cast<const char*>(message) << '\n';
        }
        return bCondition;
    }

    class RunningWork final : public IGameModeAsyncWork
    {
    public:
        ~RunningWork() override
        {
            // 検査が失敗しても、生存中のスレッドを残さない。
            Stop();
        }

        void Start(Delegate<void, Rendering::TextureHandle> completion)
        {
            Completion = std::move(completion);
            Worker.Start([this]()
            {
                {
                    Thread::ScopedLock lock(Mutex);
                    ++Pending;
                    ++WorkSteps;
                    bStarted = true;
                    Changed.NotifyAll();
                }
                // 取り消しを受けるまで実スレッド上で仕事を続ける。
                while (!bCancelled.Load())
                {
                    ++WorkSteps;
                    std::this_thread::yield();
                }
                Thread::ScopedLock lock(Mutex);
                // 取り消しと競合した最後の完了も、GameThread の受け口へ残す。
                ++Pending;
                bFinished = true;
            });
            Thread::ScopedLock lock(Mutex);
            Changed.Wait(Mutex, [this]() { return bStarted; });
        }

        void CancelAndJoin() override
        {
            ++CancelCalls;
            Check(bLeft, u8"Leave より前に非同期の仕事を停止しました");
            Stop();
            bJoined = !Worker.Joinable() && bFinished && WorkSteps > 0;
            Check(bJoined, u8"CancelAndJoin が戻った時点でワーカーが結合済みではありません");
        }

        void ReleaseUnreturned() override
        {
            Check(CancelCalls == 1 && bJoined, u8"結合より前に返しそびれた資源を返しました");
            Check(bAfterEntities, u8"Entity の後始末より前に返しそびれた資源を返しました");
            ++ReleaseCalls;
            --Unreturned;
        }

        uint32_t Drain()
        {
            uint32_t pending;
            {
                Thread::ScopedLock lock(Mutex);
                pending = Pending;
                Pending = 0;
            }
            for (uint32_t i = 0; i < pending; ++i)
            {
                Completion.Invoke(Rendering::TextureHandle::Invalid());
            }
            return pending;
        }

        bool IsRunning()
        {
            Thread::ScopedLock lock(Mutex);
            return bStarted && !bFinished;
        }

        int CancelCalls = 0;
        int ReleaseCalls = 0;
        int Unreturned = 1;
        bool bLeft = false;
        bool bJoined = false;
        bool bAfterEntities = false;

    private:
        void Stop()
        {
            {
                Thread::ScopedLock lock(Mutex);
                bCancelled.Store(true);
                Changed.NotifyAll();
            }
            Worker.Join();
        }

        Thread::Mutex Mutex;
        Thread::ConditionVariable Changed;
        Thread::Thread Worker;
        Delegate<void, Rendering::TextureHandle> Completion;
        uint32_t Pending = 0;
        uint64_t WorkSteps = 0;
        bool bStarted = false;
        Thread::Atomic<bool> bCancelled{false};
        bool bFinished = false;
    };

    class AsyncMode final : public IGameMode
    {
    public:
        AsyncMode(RunningWork& work, int& delivered) : Work(work), Delivered(delivered) {}

        GameModeEnterResult Enter(GameModeContext& ctx) override
        {
            ctx.ScopeRef.TrackAsync(Work);
            int* delivered = &Delivered;
            Work.Start(ctx.ScopeRef.WrapAsyncTextureCallback(
                [delivered](Rendering::TextureHandle) { ++*delivered; }));
            RunningWork* work = &Work;
            ctx.ScopeRef.AddCleanup(GameModeCleanupPhase::BeforeEntities, [work]()
            {
                Check(work->bJoined && work->ReleaseCalls == 0,
                      u8"後始末の先頭で非同期の仕事を結合していません");
            });
            ctx.ScopeRef.AddCleanup(GameModeCleanupPhase::AfterEntities,
                                    [work]() { work->bAfterEntities = true; });
            return GameModeEnterResult::Succeeded;
        }

        void Tick(GameModeContext&, float) override {}

        void Leave(GameModeContext& ctx, GameModeExitReason) override
        {
            Work.bLeft = true;
            // 生存中の同じスコープを明示的に二度片付け、状態機械の後始末も続けて通す。
            ctx.ScopeRef.Cleanup();
            Check(Work.bJoined && Work.CancelCalls == 1 && Work.ReleaseCalls == 1 && Work.Unreturned == 0,
                  u8"Leave の後始末で仕事の結合と一度だけの返却が完了していません");
            Check(Work.Drain() == 1 && Delivered == 0,
                  u8"Leave 後に取り消しと競合した完了が利用者へ届きました");
            ctx.ScopeRef.Cleanup();
            Check(Work.CancelCalls == 1 && Work.ReleaseCalls == 1 && Work.Unreturned == 0 && Work.Drain() == 0,
                  u8"二度目の後始末で非同期の仕事や資源の数が変わりました");
        }

        const char* GetDebugName() const override { return "ScopeAsync"; }

    private:
        RunningWork& Work;
        int& Delivered;
    };

    void TestLeaveWorker()
    {
        RunningWork work;
        int delivered = 0;
        GameModeStateMachine machine;
        const GameModeId id = Identity("ScopeAsync");
        machine.Registry().Register(id, [&work, &delivered](const GameModeParams&)
        {
            return Container::MakeUnique<AsyncMode>(work, delivered);
        });
        machine.Start(id);
        machine.Update(0.016f);
        Check(work.IsRunning(), u8"Leave の前にワーカーが走っていません");
        // 受け口が実際に通知を届けられることを、世代を閉じる前にも確かめる。
        Check(work.Drain() == 1 && delivered == 1, u8"生存中の完了通知が受け口へ届きません");
        delivered = 0;
        machine.RequestPop();
        machine.Update(0.016f);
        Check(work.bJoined && work.CancelCalls == 1 && work.ReleaseCalls == 1 && work.Unreturned == 0,
              u8"状態機械の Leave 後に仕事や資源が残りました");
        Check(work.Drain() == 0 && delivered == 0, u8"Cleanup の後に新しい完了が届きました");
        machine.Shutdown();
    }

    class FakeClock final : public IStreamingClock
    {
    public:
        double NowMs() const override { return TimeMs; }
        double TimeMs = 0.0;
    };

    class FakeExecutor final : public IStreamingJobExecutor
    {
    public:
        bool Submit(Thread::TaskFunction job) override
        {
            Jobs.push_back(std::move(job));
            return true;
        }

        void RunOne()
        {
            if (!Check(!Jobs.empty(), u8"実行する準備の仕事がありません"))
            {
                return;
            }
            auto job = std::move(Jobs.front());
            Jobs.erase(Jobs.begin());
            job();
        }

        void RunRemainingOnWorker()
        {
            Worker.Start([this]()
            {
                for (auto& job : Jobs)
                {
                    job();
                }
                bFinished.Store(true);
            });
        }

        ~FakeExecutor() override { Worker.Join(); }

        Container::VariableArray<Thread::TaskFunction> Jobs;
        Thread::Thread Worker;
        Thread::Atomic<bool> bFinished{false};
    };

    class FakeHandler final : public IStreamingPayloadHandler
    {
    public:
        explicit FakeHandler(FakeClock& clock) : Clock(clock) {}
        const char* GetKind() const override { return "scope"; }

        EStreamingPrepareResult Prepare(const StreamingPayloadContext& context,
                                        const StreamingCancelToken& cancel) override
        {
            const uint32_t index = context.Cell - 11;
            ++Prepares[index];
            ++Prepared[index];
            if (context.Cell == 13)
            {
                {
                    Thread::ScopedLock lock(Mutex);
                    bPreparing = true;
                    Changed.NotifyAll();
                }
                while (!cancel.IsCancelled())
                {
                    std::this_thread::yield();
                }
                bCancelled.Store(true);
                return EStreamingPrepareResult::Cancelled;
            }
            return EStreamingPrepareResult::Success;
        }

        EStreamingStepResult CommitStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor,
                                        uint64_t& outCommittedBytes) override
        {
            const uint32_t index = context.Cell - 11;
            ++Commits[index];
            Committed[index] = 1;
            ++cursor.Step;
            outCommittedBytes = 16;
            Clock.TimeMs += 2.0;
            return EStreamingStepResult::InProgress;
        }

        void SetDormant(const StreamingPayloadContext&, bool) override {}

        EStreamingStepResult UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor) override
        {
            const uint32_t index = context.Cell - 11;
            ++Unloads[index];
            if (++cursor.Step < 2)
            {
                return EStreamingStepResult::InProgress;
            }
            --Committed[index];
            return EStreamingStepResult::Done;
        }

        void DiscardPrepared(const StreamingPayloadContext& context) override
        {
            const uint32_t index = context.Cell - 11;
            ++Discards[index];
            --Prepared[index];
        }

        StreamingPayloadBytes GetResidentBytes(const StreamingPayloadContext&) const override { return {}; }

        void WaitPreparing()
        {
            Thread::ScopedLock lock(Mutex);
            Changed.Wait(Mutex, [this]() { return bPreparing; });
        }

        FakeClock& Clock;
        int Prepares[3] = {};
        int Commits[3] = {};
        int Unloads[3] = {};
        int Discards[3] = {};
        int Prepared[3] = {};
        int Committed[3] = {};
        Thread::Atomic<bool> bCancelled{false};

    private:
        Thread::Mutex Mutex;
        Thread::ConditionVariable Changed;
        bool bPreparing = false;
    };

    void TestStreamingCleanup()
    {
        FakeClock clock;
        FakeExecutor executor;
        FakeHandler handler(clock);
        StreamingSystem system;
        GameModeScope scope(&GEngine->GetWorld(), &GEngine->GetRenderResources());
        constexpr StreamingScopeId ScopeId = 71;
        system.SetEnvironment(&clock, &executor);
        StreamingSystemSettings settings;
        settings.Ring.ActiveRadius = 10.0f;
        settings.Ring.DormantRadius = 25.0f;
        settings.Ring.UnloadRadius = 40.0f;
        settings.Ring.DirectionWeight = 0.0f;
        settings.Ring.LatencySeconds = 0.0f;
        settings.Budget.PerFrameCommitMs = 1.0;
        if (!Check(system.SetSettings(settings) && system.RegisterHandler(&handler),
                   u8"ストリーミングの試験環境を設定できません"))
        {
            return;
        }
        const Container::String manifest = R"({
            "format":"norves.cell-manifest.v1","origin":[0,0,0],"cellSize":1,
            "cells":[
                {"id":11,"bounds":{"min":[0,-1,-1],"max":[1,1,1]},"payloads":[{"kind":"scope","logicalPath":"cells/first"}]},
                {"id":12,"bounds":{"min":[2,-1,-1],"max":[3,1,1]},"payloads":[{"kind":"scope","logicalPath":"cells/second"}]},
                {"id":13,"bounds":{"min":[4,-1,-1],"max":[5,1,1]},"payloads":[{"kind":"scope","logicalPath":"cells/third"}]}
            ]})";
        CellManifestError error;
        if (!Check(system.LoadManifestFromText(manifest, &error, ScopeId), u8"スコープ付きマニフェストを読めません"))
        {
            return;
        }
        scope.TrackStreaming(system, ScopeId);
        system.SetFocus({Math::Vector3(0.0f, 0.0f, 0.0f), Math::Vector3(0.0f, 0.0f, 0.0f)});
        system.Update();
        Check(executor.Jobs.size() == 3, u8"三つのセルの準備が投入されません");
        executor.RunOne();
        executor.RunOne();
        executor.RunRemainingOnWorker();
        handler.WaitPreparing();
        system.Update();
        Check(system.GetCellState(11) == EStreamingCellState::Committing && handler.Commits[0] == 1,
              u8"一つ目のセルをコミットの途中で止められません");
        Check(system.GetCellState(12) == EStreamingCellState::ReadyToCommit && handler.Commits[1] == 0,
              u8"二つ目のセルを準備済みかつ未コミットで止められません");
        Check(system.GetCellState(13) == EStreamingCellState::Preparing && !handler.bCancelled.Load(),
              u8"三つ目のセルがワーカー上で準備中ではありません");

        scope.Cleanup();
        Check(handler.bCancelled.Load(), u8"Cleanup が準備中のワーカーを取り消して完了を待ちません");
        // 実行器のスレッドの後片付けは、StreamingSystem が仕事の完了を待った後に行う。
        executor.Worker.Join();
        Check(executor.bFinished.Load(), u8"準備の実行器が終了していません");
        Check(!system.HasManifest(), u8"Cleanup 後にマニフェストが残りました");
        for (uint32_t i = 0; i < 3; ++i)
        {
            Check(system.GetCellState(11 + i) == EStreamingCellState::Unloaded,
                  u8"Cleanup 後にセルが未読込へ戻りません");
            Check(handler.Prepares[i] == 1 && handler.Commits[i] == (i == 0 ? 1 : 0) &&
                  handler.Discards[i] == 1 && handler.Prepared[i] == 0 &&
                  handler.Committed[i] == 0, u8"準備済みまたはコミット途中の資源が一度だけ返りません");
        }
        Check(handler.Unloads[0] == 2 && handler.Unloads[1] == 0 && handler.Unloads[2] == 0,
              u8"コミットを始めたセルだけを最後まで戻していません");

        system.Update();
        scope.Cleanup();
        system.Update();
        for (uint32_t i = 0; i < 3; ++i)
        {
            Check(handler.Prepares[i] == 1 && handler.Commits[i] == (i == 0 ? 1 : 0) &&
                  handler.Unloads[i] == (i == 0 ? 2 : 0) && handler.Discards[i] == 1 &&
                  handler.Prepared[i] == 0 && handler.Committed[i] == 0,
                  u8"二度目の Cleanup またはその後の更新でハンドラや資源の数が変わりました");
        }
        Check(!system.HasManifest(), u8"二度目の Cleanup 後にマニフェストが復活しました");
    }

    void TestTrackedResources()
    {
        World& world = GEngine->GetWorld();
        auto& resources = GEngine->GetRenderResources();
        Check(!resources.IsInitialized(), u8"デバイスを持たない試験で描画資源が初期化されています");
        // MeshResources::Register は初期化済みのデバイスと GPU ストアが必須で、CPU 登録口はない。
        // TextureResources::CreateTexture / RegisterExternalTexture も初期化済みの GPU ストアが必須。
        // この試験では両者を作らず、件数が変わらないことだけを確認する。
        // スコープ外のものを残し、二重の後始末が別の所有物を返さないことも調べる。
        Entity* survivor = world.SpawnEntity<Entity>();
        Rendering::MaterialCreateData materialInfo;
        const auto survivorMaterial = resources.Materials().Create(materialInfo);
        if (!Check(survivor != nullptr && survivorMaterial.IsValid(), u8"スコープ外の比較用資源を作れません"))
        {
            return;
        }
        const uint64_t survivorId = survivor->GetObjectId();
        {
            GameModeScope scope(&world, &resources);
            const auto baseline = scope.CaptureCounts();
            const size_t baselineMaterials = resources.Materials().GetCount();
            Entity* entity = scope.SpawnObject<Entity>();
            const auto material = resources.Materials().Create(materialInfo);
            if (material.IsValid())
            {
                scope.TrackMaterial(material);
            }
            Check(entity != nullptr && material.IsValid(), u8"追跡する Entity と材質を作れません");
            const uint64_t entityId = entity != nullptr ? entity->GetObjectId() : 0;
            Check(world.GetObjectCount() == baseline.RootEntities + 1 &&
                  resources.Materials().GetCount() == baselineMaterials + 1,
                  u8"追跡する資源の数が増えていません");
            scope.Cleanup();
            const auto checkReturned = [&]()
            {
                const auto counts = scope.CaptureCounts();
                Check(counts.RootEntities == baseline.RootEntities && counts.Meshes == baseline.Meshes &&
                      counts.Textures == baseline.Textures && resources.Materials().GetCount() == baselineMaterials,
                      u8"後始末後の World と描画資源の数が基準へ戻りません");
                Check(world.FindEntityByObjectId(entityId) == nullptr && resources.Materials().GetData(material) == nullptr,
                      u8"追跡した Entity または材質が残りました");
                Check(world.FindEntityByObjectId(survivorId) == survivor &&
                      resources.Materials().GetData(survivorMaterial) != nullptr,
                      u8"後始末がスコープ外の Entity または材質を返しました");
            };
            checkReturned();
            scope.Cleanup();
            checkReturned();
        }
        world.RemoveObject(survivor);
        resources.Materials().Release(survivorMaterial);
    }
}

int main()
{
    GEngine = new Engine::Engine();
    GEngine->GetWorld().Initialize();
    TestLeaveWorker();
    TestStreamingCleanup();
    TestTrackedResources();
    delete GEngine;
    GEngine = nullptr;
    if (g_Failures != 0)
    {
        std::cerr << reinterpret_cast<const char*>(u8"失敗数: ") << g_Failures << '\n';
    }
    else
    {
        std::cout << reinterpret_cast<const char*>(u8"GameModeScopeAsyncTest: 全項目が成功しました") << '\n';
    }
    return g_Failures == 0 ? 0 : 1;
}
