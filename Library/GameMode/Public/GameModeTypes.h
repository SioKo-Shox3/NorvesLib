#pragma once

#include "TStateMachine.h"
#include "GameModeFactory.h"
#include "IGameMode.h"

namespace NorvesLib::GameMode
{
    // GameModeProcのエイリアス定義
    // TStateMachineのテンプレート引数としてIGameModeとGameModeFactoryを指定
    using GameModeProc = TStateMachine<IGameMode, GameModeFactory>;

} // namespace NorvesLib::GameMode
