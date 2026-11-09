#pragma once

#include "CombatSliceTypes.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
} // namespace NorvesLib::Core::GameMode

namespace Game::GameModes
{
    // 台本の移動と実際の通知を照合して、遭遇の一巡を確かめる。
    class EncounterSmokeDriver
    {
    public:
        EncounterSmokeDriver();
        ~EncounterSmokeDriver();
        EncounterSmokeDriver(const EncounterSmokeDriver&) = delete;
        EncounterSmokeDriver& operator=(const EncounterSmokeDriver&) = delete;
        EncounterSmokeDriver(EncounterSmokeDriver&&) = delete;
        EncounterSmokeDriver& operator=(EncounterSmokeDriver&&) = delete;

        bool Begin(NorvesLib::Core::GameMode::GameModeContext& ctx,
                   const CombatSliceSceneIds& ids, const CombatSliceOptions& options);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, float deltaTime);
        bool IsFinished() const;
        int32_t GetExitCode() const;
        // ScopeRef の片付けより前に呼ぶ。何度呼んでもよい。
        void End(NorvesLib::Core::GameMode::GameModeContext& ctx);

    private:
        struct State;
        void Unsubscribe();

        NorvesLib::Core::Container::TSharedPtr<State> m_State;
        bool m_bFinished = false;
        int32_t m_ExitCode = 0;
    };
} // namespace Game::GameModes
