#pragma once

#include "Core/Public/GameMode/TGameMode.h"
#include "MemoryAgingTestRoutine.h"
#include "MemoryAgingTestData.h"

namespace Game::GameModes
{

    /**
     * @brief メモリエージングテスト用ゲームモード
     *
     * TGameModeテンプレートを使用して、ロジックとデータを分離した
     * メモリエージングテスト用のゲームモードを定義します。
     */
    using MemoryAgingTestMode = NorvesLib::Core::GameMode::TGameMode<MemoryAgingTestRoutine, MemoryAgingTestData>;

} // namespace Game::GameModes
