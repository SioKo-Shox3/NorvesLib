#include "Library/Core/Public/GameMode/GameModeRegistry.h"
#include "Library/Core/Public/GameMode/GameModeId.h"
#include "Library/Core/Public/GameMode/GameModeParams.h"
#include "Library/Core/Public/GameMode/IGameMode.h"
#include "Library/Core/Public/GameMode/IStateMachine.h"

#include <cassert>
#include <iostream>

// ---------------------------------------------------------------------------
// テスト用ダミー IGameMode 実装
// Phase 3 以降の IGameMode は GameModeContext を受け取る新シグネチャを使う。
// 本テストは Register/Contains/Create のみを検証するため、Enter/Tick/Leave は
// 呼ばれず、GameModeContext を構築することはない。
// ---------------------------------------------------------------------------
namespace
{
    // テスト用 GameModeId 定数（Identity ベース）
    // GameModeId は Identity のエイリアス。コア列挙は廃止されたためここで定義する。
    const NorvesLib::Core::GameMode::GameModeId kModeA       = NorvesLib::Core::Identity("Rendering3DTest");
    const NorvesLib::Core::GameMode::GameModeId kModeB       = NorvesLib::Core::Identity("MemoryAgingTest");
    const NorvesLib::Core::GameMode::GameModeId kModeUnregistered = NorvesLib::Core::Identity("Unregistered");

    struct DummyGameMode : public NorvesLib::Core::GameMode::IGameMode
    {
        bool* bEntered = nullptr;

        explicit DummyGameMode(bool* entered) : bEntered(entered) {}

        NorvesLib::Core::GameMode::GameModeEnterResult
        Enter(NorvesLib::Core::GameMode::GameModeContext& ctx) override
        {
            (void)ctx;
            if (bEntered)
            {
                *bEntered = true;
            }
            return NorvesLib::Core::GameMode::GameModeEnterResult::Succeeded;
        }

        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, float deltaTime) override
        {
            (void)ctx;
            (void)deltaTime;
        }

        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx,
                   NorvesLib::Core::GameMode::GameModeExitReason reason) override
        {
            (void)ctx;
            (void)reason;
            if (bEntered)
            {
                *bEntered = false;
            }
        }

        const char* GetDebugName() const override { return "Dummy"; }
    };

} // anonymous namespace

// ---------------------------------------------------------------------------
// テスト本体
// ---------------------------------------------------------------------------
int main()
{
    using namespace NorvesLib::Core::GameMode;
    using namespace NorvesLib::Core::Container;

    std::cout << "[GameModeRegistryTest] START" << std::endl;

    // -----------------------------------------------------------------------
    // 1. 登録前は Contains が false を返すこと
    // -----------------------------------------------------------------------
    {
        GameModeRegistry registry;
        assert(!registry.Contains(kModeA));
        assert(!registry.Contains(kModeB));
        std::cout << "[PASS] Contains returns false before Register" << std::endl;
    }

    // -----------------------------------------------------------------------
    // 2. Register 後は Contains が true を返すこと
    // -----------------------------------------------------------------------
    {
        GameModeRegistry registry;

        registry.Register(kModeA, [](const GameModeParams&)
        {
            bool dummy = false;
            return MakeUnique<DummyGameMode>(&dummy);
        });

        assert(registry.Contains(kModeA));
        assert(!registry.Contains(kModeB));
        std::cout << "[PASS] Contains returns true after Register" << std::endl;
    }

    // -----------------------------------------------------------------------
    // 3. 登録済み ID の Create が非 null を返し、Creator が実行されること
    // -----------------------------------------------------------------------
    {
        GameModeRegistry registry;
        bool bCreated = false;

        registry.Register(kModeA, [&bCreated](const GameModeParams&)
        {
            bCreated = true;
            bool dummy = false;
            return MakeUnique<DummyGameMode>(&dummy);
        });

        GameModeParams params;
        auto mode = registry.Create(kModeA, params);

        assert(mode != nullptr);
        assert(bCreated);
        std::cout << "[PASS] Create returns non-null and invokes creator" << std::endl;
    }

    // -----------------------------------------------------------------------
    // 4. 未登録 ID の Create が nullptr を返すこと
    // -----------------------------------------------------------------------
    {
        GameModeRegistry registry;

        GameModeParams params;
        auto mode = registry.Create(kModeUnregistered, params);

        assert(mode == nullptr);
        std::cout << "[PASS] Create returns null for unregistered id" << std::endl;
    }

    // -----------------------------------------------------------------------
    // 5. 同一 ID を 2 回 Register した場合、後勝ち（上書き）になること
    // -----------------------------------------------------------------------
    {
        GameModeRegistry registry;
        int callCount = 0;

        // 1 回目の登録
        registry.Register(kModeA, [&callCount](const GameModeParams&)
        {
            callCount = 1;
            bool dummy = false;
            return MakeUnique<DummyGameMode>(&dummy);
        });

        // 2 回目の登録（上書き）
        registry.Register(kModeA, [&callCount](const GameModeParams&)
        {
            callCount = 2;
            bool dummy = false;
            return MakeUnique<DummyGameMode>(&dummy);
        });

        GameModeParams params;
        auto mode = registry.Create(kModeA, params);

        assert(mode != nullptr);
        assert(callCount == 2);
        std::cout << "[PASS] Last-wins: second Registration overwrites first" << std::endl;
    }

    std::cout << "[GameModeRegistryTest] ALL PASSED" << std::endl;
    return 0;
}
