#pragma once

#include "CombatSliceTypes.h"
#include "Container/PointerTypes.h"
#include "GameMode/GameModeContext.h"

namespace Game::GameModes
{
    // 判定窓の開閉によるゲームスレッドの区間時間の差を採取する。
    class CombatBenchDriver
    {
    public:
        CombatBenchDriver();
        ~CombatBenchDriver();
        CombatBenchDriver(const CombatBenchDriver&) = delete;
        CombatBenchDriver& operator=(const CombatBenchDriver&) = delete;

        bool Begin(NorvesLib::Core::GameMode::GameModeContext& ctx,
                   const CombatSliceSceneIds& ids, const CombatSliceOptions& options);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, float deltaTime);
        bool IsFinished() const;
        int32_t GetExitCode() const;
        // ScopeRef の片付けより前に呼ぶ。何度呼んでもよい。
        void End(NorvesLib::Core::GameMode::GameModeContext& ctx);

    private:
        struct State;
        void Fail(NorvesLib::Core::GameMode::GameModeContext& ctx, const char* reason);

        NorvesLib::Core::Container::TUniquePtr<State> m_State;
        bool m_bFinished = false;
        int32_t m_ExitCode = 0;
    };
} // namespace Game::GameModes
