#include "Library/Core/Public/GameMode/GameModeStateMachine.h"
#include "Library/Core/Public/GameMode/GameModeContext.h"
#include "Library/Core/Public/GameMode/GameModeScope.h"
#include "Library/Core/Public/GameMode/IGameMode.h"
#include "Library/Core/Public/Engine/Engine.h"

#include <cassert>
#include <iostream>

namespace
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    using NorvesLib::Core::Engine::GEngine;

    uint64_t g_FakePending = 0;
    uint64_t g_FakeVoices = 0;

#if NORVES_ENABLE_STATS
    bool HasLeak(const GameModeScope& scope, const char* name, uint64_t baseline, uint64_t current)
    {
        for (const GameModeLeakEntry& entry : scope.CollectLeaks())
        {
            if (entry.Name == Container::String(name) && entry.Baseline == baseline && entry.Current == current)
            {
                return true;
            }
        }
        return false;
    }
#endif

    struct ResetObservations
    {
        size_t BaselineRoots = 0;
        uint64_t BaselinePending = 0;
        uint64_t BaselineVoices = 0;
        uint64_t PreviousIds[4] = {};
        int EnterCount = 0;
        int LeaveCount = 0;
        int CleanupCount = 0;
    };

    struct ResetMode : IGameMode
    {
        explicit ResetMode(ResetObservations& observations) : Observations(observations) {}

        GameModeEnterResult Enter(GameModeContext& ctx) override
        {
            const size_t roots = ctx.WorldRef.GetObjectCount();
            if (Observations.EnterCount == 0)
            {
                Observations.BaselineRoots = roots;
                Observations.BaselinePending = g_FakePending;
                Observations.BaselineVoices = g_FakeVoices;
            }
            assert(roots == Observations.BaselineRoots);
            assert(g_FakePending == Observations.BaselinePending && g_FakePending == 0);
            assert(g_FakeVoices == Observations.BaselineVoices && g_FakeVoices == 0);
            for (uint64_t id : Observations.PreviousIds)
            {
                assert(id == 0 || ctx.WorldRef.FindEntityByObjectId(id) == nullptr);
            }
            ++Observations.EnterCount;

            GameModeScope& scope = ctx.ScopeRef;
            scope.AddLeakCounter("FakePending", []() -> uint64_t { return g_FakePending; });
            scope.AddLeakCounter("FakeVoices", []() -> uint64_t { return g_FakeVoices; });

            Entity* first = scope.SpawnObject<Entity>();
            Entity* second = scope.SpawnObject<Entity>();
            Entity* third = scope.SpawnObject<Entity>();
            assert(first != nullptr && second != nullptr && third != nullptr);
            Entity* child = scope.SpawnObject<Entity>(first);
            assert(child != nullptr && child->GetParentEntity() == first);
            Observations.PreviousIds[0] = first->GetObjectId();
            Observations.PreviousIds[1] = second->GetObjectId();
            Observations.PreviousIds[2] = third->GetObjectId();
            Observations.PreviousIds[3] = child->GetObjectId();
            assert(ctx.WorldRef.GetObjectCount() == Observations.BaselineRoots + 3);

            ++g_FakePending;
            scope.AddCleanup(GameModeCleanupPhase::BeforeEntities, []() { --g_FakePending; });
            g_FakeVoices += 2;
            scope.AddCleanup(GameModeCleanupPhase::BeforeEntities, []() { g_FakeVoices = 0; });

#if NORVES_ENABLE_STATS
            assert(HasLeak(scope, "RootEntities", Observations.BaselineRoots, Observations.BaselineRoots + 3));
            assert(HasLeak(scope, "FakePending", Observations.BaselinePending, Observations.BaselinePending + 1));
            assert(HasLeak(scope, "FakeVoices", Observations.BaselineVoices, Observations.BaselineVoices + 2));
#endif

            // 状態機械が作った各スコープで、Entity と偽の資源を片付けた直後の差を調べる。
            GameModeScope* scopePtr = &scope;
            ResetObservations* observations = &Observations;
            scope.AddCleanup(GameModeCleanupPhase::AfterEntities, [scopePtr, observations]()
            {
#if NORVES_ENABLE_STATS
                assert(scopePtr->CollectLeaks().empty());
#else
                (void)scopePtr;
#endif
                ++observations->CleanupCount;
            });
            return GameModeEnterResult::Succeeded;
        }

        void Tick(GameModeContext&, float) override {}

        void Leave(GameModeContext&, GameModeExitReason) override
        {
            ++Observations.LeaveCount;
        }

        const char* GetDebugName() const override { return "ScopeLeakReset"; }

        ResetObservations& Observations;
    };

    void TestRepeatedReset()
    {
        const GameModeId id = Identity("ScopeLeakReset");
        ResetObservations observations;
        GameModeStateMachine machine;
        machine.Registry().Register(id, [&observations](const GameModeParams&)
        {
            return Container::MakeUnique<ResetMode>(observations);
        });

        machine.Start(id);
        machine.Update(0.016f);
        assert(observations.EnterCount == 1);
        for (int i = 0; i < 50; ++i)
        {
            machine.RequestReset(id);
            machine.Update(0.016f);
            assert(observations.EnterCount == i + 2);
            assert(observations.LeaveCount == i + 1);
            assert(observations.CleanupCount == i + 1);
            assert(GEngine->GetWorld().GetObjectCount() == observations.BaselineRoots + 3);
            assert(g_FakePending == 1 && g_FakeVoices == 2);
        }

        machine.Shutdown();
        assert(observations.LeaveCount == 51 && observations.CleanupCount == 51);
        assert(GEngine->GetWorld().GetObjectCount() == observations.BaselineRoots);
        assert(g_FakePending == 0 && g_FakeVoices == 0);
        for (uint64_t idToFind : observations.PreviousIds)
        {
            assert(GEngine->GetWorld().FindEntityByObjectId(idToFind) == nullptr);
        }
        std::cout << "[PASS] RequestReset 50 回で基準の数へ戻る" << std::endl;
    }

    void TestPhasesAndOrder()
    {
        World& world = GEngine->GetWorld();
        const size_t baseline = world.GetObjectCount();
        Container::VariableArray<char> calls;
        GameModeScope scope(&world, &GEngine->GetRenderResources());

        auto recordBefore = [&](char name)
        {
            assert(world.GetObjectCount() == baseline + 1);
            calls.push_back(name);
        };
        auto recordAfter = [&](char name)
        {
            assert(world.GetObjectCount() == baseline);
            calls.push_back(name);
        };
        scope.AddCleanup(GameModeCleanupPhase::BeforeEntities, [&]() { recordBefore('A'); });
        scope.AddCleanup(GameModeCleanupPhase::BeforeEntities, [&]() { recordBefore('B'); });
        scope.AddCleanup(GameModeCleanupPhase::AfterEntities, [&]() { recordAfter('C'); });
        scope.AddCleanup(GameModeCleanupPhase::AfterEntities, [&]() { recordAfter('D'); });
        Entity* entity = scope.SpawnObject<Entity>();
        assert(entity != nullptr && world.GetObjectCount() == baseline + 1);

        scope.Cleanup();
        const char expected[] = { 'B', 'A', 'D', 'C' };
        assert(calls.size() == 4);
        for (size_t i = 0; i < 4; ++i)
        {
            assert(calls[i] == expected[i]);
        }
        scope.Cleanup();
        assert(calls.size() == 4);
        assert(world.GetObjectCount() == baseline);
#if NORVES_ENABLE_STATS
        assert(scope.CollectLeaks().empty());
#endif
        std::cout << "[PASS] 後始末の段・順・二度呼び" << std::endl;
    }

    void TestLeakReporting()
    {
        World& world = GEngine->GetWorld();
        const size_t baseline = world.GetObjectCount();
        GameModeScope scope(&world, &GEngine->GetRenderResources());
        Entity* entity = scope.SpawnObject<Entity>();
        assert(entity != nullptr && world.GetObjectCount() == baseline + 1);
#if NORVES_ENABLE_STATS
        assert(HasLeak(scope, "RootEntities", baseline, baseline + 1));
#endif
        scope.Cleanup();
        assert(world.GetObjectCount() == baseline);
#if NORVES_ENABLE_STATS
        assert(scope.CollectLeaks().empty());
#endif
        std::cout << "[PASS] 未後始末の Entity の検出と後始末後の差" << std::endl;
    }

    void TestRegistrationDuringCleanup()
    {
        World& world = GEngine->GetWorld();
        const size_t baseline = world.GetObjectCount();
        Container::VariableArray<char> calls;
        GameModeScope scope(&world, &GEngine->GetRenderResources());
        Entity* entity = scope.SpawnObject<Entity>();
        assert(entity != nullptr);

        scope.AddCleanup(GameModeCleanupPhase::BeforeEntities, [&]() { calls.push_back('A'); });
        scope.AddCleanup(GameModeCleanupPhase::BeforeEntities, [&]()
        {
            calls.push_back('B');
            scope.AddCleanup(GameModeCleanupPhase::BeforeEntities, [&]() { calls.push_back('N'); });
            scope.Cleanup();
            calls.push_back('b');
        });
        scope.AddCleanup(GameModeCleanupPhase::AfterEntities, [&]() { calls.push_back('C'); });
        scope.AddCleanup(GameModeCleanupPhase::AfterEntities, [&]()
        {
            calls.push_back('D');
            scope.AddCleanup(GameModeCleanupPhase::BeforeEntities, [&]()
            {
                assert(world.GetObjectCount() == baseline);
                calls.push_back('X');
            });
        });

        scope.Cleanup();
        const char expected[] = { 'B', 'b', 'N', 'A', 'D', 'C', 'X' };
        assert(calls.size() == 7);
        for (size_t i = 0; i < 7; ++i)
        {
            assert(calls[i] == expected[i]);
        }
        assert(world.GetObjectCount() == baseline && scope.IsEmpty());
        std::cout << "[PASS] Cleanup 中の登録と再入の抑止" << std::endl;
    }

    void TestTextureCallbackGeneration()
    {
        using Rendering::TextureHandle;
        Delegate<void, TextureHandle> cacheCallback;
        Delegate<void, TextureHandle> scopeCallback;
        Delegate<void, TextureHandle> nextGeneration;
        int cacheCalls = 0;
        int scopeCalls = 0;
        int nextCalls = 0;
        const TextureHandle invalid = TextureHandle::Invalid();
        {
            GameModeScope scope(&GEngine->GetWorld(), &GEngine->GetRenderResources());
            cacheCallback = scope.WrapAsyncTextureCallback(
                [&](TextureHandle) { ++cacheCalls; }, GameModeAsyncTextureOwnership::Cache);
            scopeCallback = scope.WrapAsyncTextureCallback(
                [&](TextureHandle) { ++scopeCalls; }, GameModeAsyncTextureOwnership::Scope);
            cacheCallback.Invoke(invalid);
            scopeCallback.Invoke(invalid);
            assert(cacheCalls == 1 && scopeCalls == 1);

            scope.Cleanup();
            cacheCallback.Invoke(invalid);
            scopeCallback.Invoke(invalid);
            assert(cacheCalls == 1 && scopeCalls == 1);

            nextGeneration = scope.WrapAsyncTextureCallback(
                [&](TextureHandle) { ++nextCalls; }, GameModeAsyncTextureOwnership::Cache);
            nextGeneration.Invoke(invalid);
            assert(nextCalls == 1);
        }
        cacheCallback.Invoke(invalid);
        scopeCallback.Invoke(invalid);
        nextGeneration.Invoke(invalid);
        assert(cacheCalls == 1 && scopeCalls == 1 && nextCalls == 1);
        std::cout << "[PASS] テクスチャの呼び戻しの世代とスコープ破棄" << std::endl;
    }
}

int main()
{
    std::cout << "[GameModeScopeLeakTest] 開始" << std::endl;
    GEngine = new NorvesLib::Core::Engine::Engine();
    GEngine->GetWorld().Initialize();

    TestRepeatedReset();
    TestPhasesAndOrder();
    TestLeakReporting();
    TestRegistrationDuringCleanup();
    TestTextureCallbackGeneration();

    delete GEngine;
    GEngine = nullptr;
    std::cout << "[GameModeScopeLeakTest] 全項目が成功しました" << std::endl;
    return 0;
}
