#pragma once

#include "TStateMachine.h"
#include "GameModeFactory.h"
#include "IGameMode.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief GameModeProcのエイリアス定義
     *
     * TStateMachineのテンプレート引数としてIGameModeとGameModeFactoryを指定した
     * 標準的なゲームモードステートマシンの型。
     */
    using GameModeProc = TStateMachine<IGameMode, GameModeFactory>;

} // namespace NorvesLib::Core::GameMode
