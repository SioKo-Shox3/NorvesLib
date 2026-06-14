#include "Library/Core/Public/GameMode/GameModeRegistry.h"
#include "Library/Core/Public/GameMode/GameModeId.h"
#include "Library/Core/Public/GameMode/GameModeParams.h"
#include "Library/Core/Public/GameMode/IGameMode.h"
#include "Library/Core/Public/GameMode/IStateMachine.h"

#include <cassert>
#include <iostream>

// ---------------------------------------------------------------------------
// テスト用ダミー IGameMode 実装
// Phase 2 では IGameMode は旧シグネチャ（IStateMachine*）をそのまま使う。
// ---------------------------------------------------------------------------
namespace
{

    struct DummyGameMode : public NorvesLib::Core::GameMode::IGameMode
    {
        bool* bEntered = nullptr;

        explicit DummyGameMode(bool* entered) : bEntered(entered) {}

        void Enter(NorvesLib::Core::GameMode::IStateMachine* proc) override
        {
            if (bEntered)
            {
                *bEntered = true;
            }
        }

        void Do(NorvesLib::Core::GameMode::IStateMachine* proc, float deltaTime) override {}

        void Leave(NorvesLib::Core::GameMode::IStateMachine* proc) override
        {
            if (bEntered)
            {
                *bEntered = false;
            }
        }
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
        assert(!registry.Contains(GameModeId::Rendering3DTest));
        assert(!registry.Contains(GameModeId::MemoryAgingTest));
        std::cout << "[PASS] Contains returns false before Register" << std::endl;
    }

    // -----------------------------------------------------------------------
    // 2. Register 後は Contains が true を返すこと
    // -----------------------------------------------------------------------
    {
        GameModeRegistry registry;

        registry.Register(GameModeId::Rendering3DTest, [](const GameModeParams&)
        {
            bool dummy = false;
            return MakeUnique<DummyGameMode>(&dummy);
        });

        assert(registry.Contains(GameModeId::Rendering3DTest));
        assert(!registry.Contains(GameModeId::MemoryAgingTest));
        std::cout << "[PASS] Contains returns true after Register" << std::endl;
    }

    // -----------------------------------------------------------------------
    // 3. 登録済み ID の Create が非 null を返し、Creator が実行されること
    // -----------------------------------------------------------------------
    {
        GameModeRegistry registry;
        bool bCreated = false;

        registry.Register(GameModeId::Rendering3DTest, [&bCreated](const GameModeParams&)
        {
            bCreated = true;
            bool dummy = false;
            return MakeUnique<DummyGameMode>(&dummy);
        });

        GameModeParams params;
        auto mode = registry.Create(GameModeId::Rendering3DTest, params);

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
        auto mode = registry.Create(GameModeId::MemoryAgingTest, params);

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
        registry.Register(GameModeId::Rendering3DTest, [&callCount](const GameModeParams&)
        {
            callCount = 1;
            bool dummy = false;
            return MakeUnique<DummyGameMode>(&dummy);
        });

        // 2 回目の登録（上書き）
        registry.Register(GameModeId::Rendering3DTest, [&callCount](const GameModeParams&)
        {
            callCount = 2;
            bool dummy = false;
            return MakeUnique<DummyGameMode>(&dummy);
        });

        GameModeParams params;
        auto mode = registry.Create(GameModeId::Rendering3DTest, params);

        assert(mode != nullptr);
        assert(callCount == 2);
        std::cout << "[PASS] Last-wins: second Registration overwrites first" << std::endl;
    }

    std::cout << "[GameModeRegistryTest] ALL PASSED" << std::endl;
    return 0;
}
