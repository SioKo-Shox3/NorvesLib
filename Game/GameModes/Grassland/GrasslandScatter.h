// 草原の散布をクック済み資産から読み、シーンの World に登録する。
#pragma once

#include "Core/Public/Scatter/ScatterSystem.h"

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}

namespace Game::GameModes
{
    NorvesLib::Core::Scatter::ScatterHandle LoadGrasslandScatter(
        NorvesLib::Core::GameMode::GameModeContext& ctx, uint32_t stressCount);
} // namespace Game::GameModes
