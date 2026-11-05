#pragma once

#include "CombatSliceTypes.h"

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
} // namespace NorvesLib::Core::GameMode

namespace Game::GameModes
{
    // 前回の後始末とセーブからの復元を繰り返し、資源数の定常状態を確かめる。
    class CombatResetSoakDriver
    {
    public:
        static CombatResetSoakCounts CaptureCounts(NorvesLib::Core::GameMode::GameModeContext& ctx);

        bool Begin(NorvesLib::Core::GameMode::GameModeContext& ctx,
                   CombatSliceResetState& resetState, const CombatResetSoakCounts& enterCounts);
        // リセットを要求するときだけ true を返す。
        bool Tick(NorvesLib::Core::GameMode::GameModeContext& ctx);
        void End(NorvesLib::Core::GameMode::GameModeContext& ctx);

    private:
        enum class Phase : uint8_t
        {
            Ready,
            Save,
            Finished
        };

        void Fail(NorvesLib::Core::GameMode::GameModeContext& ctx, const char* reason);
        bool CheckCounts(NorvesLib::Core::GameMode::GameModeContext& ctx, const char* stage,
                         const CombatResetSoakCounts& baseline, const CombatResetSoakCounts& current,
                         bool bCompareVoices);

        CombatSliceResetState* m_ResetState = nullptr;
        uint64_t m_StartingWrites = 0;
        uint32_t m_Index = 0;
        uint32_t m_Target = 0;
        uint32_t m_TickCount = 0;
        Phase m_Phase = Phase::Finished;
    };
} // namespace Game::GameModes
